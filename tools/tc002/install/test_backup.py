"""Tests of the private flash backup (backup.py) against a fake device; never touches a clock."""
import contextlib
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib"))
import backup  # noqa: E402
import build_res_image  # noqa: E402
import tc002_device  # noqa: E402
import tc002_install as installer  # noqa: E402

TABLE = 'dev:    size   erasesize  name\nmtd0: 00000100 00000080 "boot"\nmtd3: 00000200 00000080 "res"'
BOOT_ID = "f7598211-021f-4a8f-a9d6-0ff78f0a2528"
# NUL, CR, LF, 0xff and every other byte value: a text channel would corrupt this.
DATA = {0: bytes(range(256)), 3: bytes(range(256)) * 2}


class FakeDevice:
    """The shell commands and adb pull the backup uses, with the stock shell's CR line ends."""
    serial = "0123456789ABCDEF"

    def __init__(self, failure=None, kind="nor", reboot=False, changed_table=False, extra=b""):
        self.failure, self.kind, self.reboot = failure, kind, reboot
        self.changed_table, self.extra = changed_table, extra
        self.commands, self.pulls = [], []

    def shell(self, command, timeout=120, check=True):
        self.commands.append(command)
        if command == "cat /proc/mtd":
            extra = '\nmtd4: 00000100 00000080 "new"' if self.changed_table and self.pulls else ""
            return 0, TABLE + extra + "\n"
        if command == backup.BOOT_ID:
            return 0, (BOOT_ID if not self.reboot or not self.pulls else "6" + BOOT_ID[1:]) + "\n"
        if command.startswith("cat /sys/class/mtd/"):
            return 0, self.kind + "\n"
        if command.startswith("if [ -r /dev/mtd"):
            return 0, command.split("echo ", 1)[1].split(";", 1)[0] + "\n"
        if command == "uname -srm":
            return 0, "Linux 4.9.84 armv7l\n"
        if command == "cat /sys/firmware/devicetree/base/model":
            return 0, "PIONEER3 SSC021A-S01A-S\0"
        return 1, ""

    def pull(self, remote, local, timeout=120):
        index = int(remote.removeprefix("/dev/mtd"))
        self.pulls.append((index, timeout))
        if self.failure is not None and index == 3:
            Path(local).write_bytes(DATA[index][:17])
            raise self.failure
        Path(local).write_bytes(DATA[index] + (self.extra if index == 3 else b""))


class BackupTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.parent = Path(self.temp.name)
        self.output = self.parent / "private-backup"

    def backup(self, device=None):
        return backup.capture(device or FakeDevice(), self.output, timeout=77)

    def read(self, name="manifest.json"):
        return json.loads((self.output / name).read_text())

    def write(self, data, name="manifest.json"):
        (self.output / name).write_text(json.dumps(data))

    def test_every_partition_is_copied_byte_for_byte_and_verified_offline(self):
        device = FakeDevice()
        self.backup(device)
        self.assertEqual(device.pulls, [(0, 77), (3, 77)])
        self.assertEqual((self.output / "mtd0.bin").read_bytes(), DATA[0])
        self.assertEqual((self.output / "mtd3.bin").read_bytes(), DATA[3])
        with mock.patch.object(subprocess, "Popen", side_effect=AssertionError("verify ran a process")):
            result = backup.verify(self.output)
        self.assertEqual((result["partitions"], result["bytes"]), (2, 768))
        manifest, profile = self.read(), self.read("profile.json")
        self.assertTrue(manifest["live_unquiesced"])
        self.assertEqual(profile["observations"]["device_tree_model"], "PIONEER3 SSC021A-S01A-S")
        self.assertIn("modules", profile["unavailable"])
        self.assertEqual([p["device"] for p in profile["partitions"]], ["/dev/mtd0", "/dev/mtd3"])
        allowed = set(backup.SNAPSHOT_COMMANDS.values()) | {"cat /proc/mtd"}
        allowed |= {f"cat /sys/class/mtd/mtd{index}/type" for index in DATA}
        self.assertEqual({c for c in device.commands if not c.startswith("if [ -r ")} - allowed, set())

    def test_the_stock_res_image_builder_reads_the_backup(self):
        self.backup()
        dump, sha256, size, partition, erase = build_res_image.read_backup(self.output)
        self.assertEqual((dump, size, partition, erase), (self.output / "mtd3.bin", 512, 512, 128))
        self.assertEqual(sha256, self.read()["partitions"][1]["sha256"])

    def test_changed_dump_of_the_same_length_is_detected(self):
        self.backup()
        data = bytearray(DATA[0])
        data[77] ^= 1
        (self.output / "mtd0.bin").write_bytes(data)
        with self.assertRaisesRegex(backup.BackupError, "SHA-256 mismatch"):
            backup.verify(self.output)

    def test_truncated_and_extended_dumps_are_detected(self):
        self.backup()
        for data in (DATA[0][:-1], DATA[0] + b"\x00"):
            with self.subTest(length=len(data)):
                (self.output / "mtd0.bin").write_bytes(data)
                with self.assertRaises(backup.BackupError):
                    backup.verify(self.output)

    def test_a_read_of_the_wrong_size_leaves_the_backup_incomplete(self):
        with self.assertRaisesRegex(backup.BackupError, "mtd3 read 513 bytes"):
            self.backup(FakeDevice(extra=b"x"))
        self.assertEqual(self.read()["status"], "incomplete")

    def test_lost_usb_keeps_the_first_partition_and_the_partial_second(self):
        with self.assertRaisesRegex(tc002_device.DeviceError, "offline"):
            self.backup(FakeDevice(failure=tc002_device.DeviceError("adb pull failed: offline")))
        self.assertEqual(self.read()["status"], "incomplete")
        self.assertEqual((self.output / "mtd0.bin").read_bytes(), DATA[0])
        self.assertEqual((self.output / "mtd3.bin.partial").stat().st_size, 17)
        with self.assertRaisesRegex(backup.BackupError, "incomplete"):
            backup.verify(self.output)

    def test_keyboard_interrupt_leaves_an_incomplete_manifest(self):
        with self.assertRaises(KeyboardInterrupt):
            self.backup(FakeDevice(failure=KeyboardInterrupt()))
        self.assertEqual(self.read()["status"], "incomplete")

    def test_a_reboot_or_a_changed_mtd_table_prevents_completion(self):
        for options in ({"reboot": True}, {"changed_table": True}):
            with self.subTest(options=options):
                target = self.parent / next(iter(options))
                with self.assertRaises(backup.BackupError):
                    backup.capture(FakeDevice(**options), target)
                self.assertEqual(json.loads((target / "manifest.json").read_text())["status"],
                                 "incomplete")

    def test_nand_and_unknown_types_are_refused_before_the_directory_exists(self):
        for kind in ("nand", "unknown", "not-a-type\nextra"):
            with self.subTest(kind=kind), self.assertRaises(backup.BackupError):
                self.backup(FakeDevice(kind=kind))
        self.assertFalse(self.output.exists())

    def test_duplicate_missing_and_traversing_records_are_refused(self):
        self.backup()
        original = self.read()
        for records in ([original["partitions"][0]] * 2, original["partitions"][:1],
                        [dict(original["partitions"][0], file="../mtd0.bin"), original["partitions"][1]],
                        [dict(original["partitions"][0], file="/mtd0.bin"), original["partitions"][1]]):
            with self.subTest(records=records):
                self.write(dict(original, partitions=records))
                with self.assertRaises(backup.BackupError):
                    backup.verify(self.output)

    def test_duplicate_json_keys_and_leftover_temporary_files_are_refused(self):
        self.backup()
        manifest = (self.output / "manifest.json").read_text()
        (self.output / "manifest.json").write_text(
            manifest.replace('"status": "complete"', '"status": "complete", "status": "complete"', 1))
        with self.assertRaisesRegex(backup.BackupError, "duplicate JSON"):
            backup.verify(self.output)
        (self.output / "manifest.json").write_text(manifest)
        (self.output / "manifest.json.tmp").write_text("incomplete")
        with self.assertRaisesRegex(backup.BackupError, "unexpected files"):
            backup.verify(self.output)

    def test_atomic_metadata_preserves_previous_file_on_sync_failure_and_can_retry(self):
        record = self.parent / "record.json"
        backup.atomic_json(record, {"complete": False})
        with mock.patch.object(backup.os, "fsync", side_effect=OSError("sync failed")):
            with self.assertRaisesRegex(OSError, "sync failed"):
                backup.atomic_json(record, {"complete": True})
        self.assertEqual(json.loads(record.read_text()), {"complete": False})
        self.assertEqual(list(self.parent.glob("record.json.*.tmp")), [])
        (self.parent / "record.json.tmp").write_text("interrupted")
        backup.atomic_json(record, {"complete": True})
        self.assertEqual(json.loads(record.read_text()), {"complete": True})
        if os.name != "nt":
            self.assertEqual(record.stat().st_mode & 0o777, 0o600)

    def test_invalid_manifest_types_give_controlled_errors(self):
        self.backup()
        manifest = self.read()
        for key, value in (("schema_version", True), ("operation", []), ("operation", "diagnose"),
                           ("partitions", {}), ("profile", [])):
            with self.subTest(key=key, value=value):
                self.write(dict(manifest, **{key: value}))
                with self.assertRaises(backup.BackupError):
                    backup.verify(self.output)

    def test_a_changed_profile_is_refused(self):
        self.backup()
        profile = self.read("profile.json")
        profile["partitions"][0]["size"] += 1
        self.write(profile, "profile.json")
        with self.assertRaises(backup.BackupError):
            backup.verify(self.output)
        manifest = self.read()
        size, digest = backup.file_digest(self.output / "profile.json")
        manifest["profile"].update(bytes=size, sha256=digest)
        self.write(manifest)
        with self.assertRaisesRegex(backup.BackupError, "do not match"):
            backup.verify(self.output)

    def test_the_directory_must_be_new_and_outside_the_repository(self):
        with self.assertRaisesRegex(backup.BackupError, "outside"):
            backup.create_output(backup.REPOSITORY / "tc002-test-must-not-exist")
        with self.assertRaisesRegex(backup.BackupError, "must not exist"):
            backup.create_output(self.parent)

    def test_low_disk_space_leaves_an_incomplete_backup_without_reads(self):
        device = FakeDevice()
        with mock.patch.object(backup.shutil, "disk_usage", return_value=type("Disk", (), {"free": 0})()):
            with self.assertRaisesRegex(backup.BackupError, "free space"):
                self.backup(device)
        self.assertEqual(self.read()["status"], "incomplete")
        self.assertEqual(device.pulls, [])

    def test_symlinked_data_is_refused(self):
        self.backup()
        external = self.parent / "external.bin"
        external.write_bytes(DATA[0])
        data = self.output / "mtd0.bin"
        data.unlink()
        try:
            data.symlink_to(external)
        except OSError:
            self.skipTest("creating symlinks needs OS permissions")
        with self.assertRaisesRegex(backup.BackupError, "regular files"):
            backup.verify(self.output)

    def test_the_mtd_table_is_parsed_whatever_the_indices(self):
        result = backup.parse_mtd(TABLE)
        self.assertEqual([item["index"] for item in result], [0, 3])
        self.assertEqual(result[1]["size"], 512)
        self.assertEqual(backup.parse_mtd(TABLE.replace('"boot"', '"../private"'))[0]["name"], "../private")
        for table in ("", TABLE + '\nmtd0: 100 80 "again"', TABLE.replace("00000100", "00000101"),
                      TABLE.replace("00000100", "00000000"), TABLE.replace("mtd0:", "../mtd0:"),
                      TABLE.replace('"boot"', '"boot\nline"'), TABLE.replace("00000080", "00000000")):
            with self.subTest(table=table), self.assertRaises(backup.BackupError):
                backup.parse_mtd(table)

    def test_verify_backup_command(self):
        self.backup()
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            self.assertEqual(installer.main(["verify-backup", str(self.output)]), 0)
            self.assertEqual(installer.main(["verify-backup", str(self.parent)]), 1)
        self.assertEqual(json.loads(out.getvalue())["partitions"], 2)
        self.assertIn("error:", err.getvalue())


if __name__ == "__main__":
    unittest.main()
