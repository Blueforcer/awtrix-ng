"""Private backups of a TC002's NOR flash (tc002_install.py backup and verify-backup).

capture() reads every MTD partition with adb pull, which the stock adbd serves byte for byte, into
a new private directory outside the repository: mtdN.bin, profile.json (the MTD table and a few
facts about the kernel) and manifest.json with every file's size and SHA-256. It refuses a device
with other than NOR, ROM or RAM partitions, and a backup during which the device rebooted or its
MTD table changed stays marked incomplete. The clock keeps running, so a matching checksum does not
prove a consistent filesystem snapshot. verify() checks a backup offline. build_res_image.py takes
the stock res partition from one.
"""
import json
import os
import re
import shutil
import tempfile
from datetime import datetime, timezone
from pathlib import Path

from bundle import file_digest

SCHEMA_VERSION = 1
REPOSITORY = Path(__file__).resolve().parents[3]
CHUNK_SIZE = 1024 * 1024
TEXT_LIMIT = 128 * 1024
READABLE_MTD_TYPES = {"nor", "rom", "ram"}
MTD_PATTERN = re.compile(r'mtd([0-9]{1,3}):\s+([0-9a-fA-F]{1,16})\s+([0-9a-fA-F]{1,16})\s+"([^"\r\n]{1,128})"\s*\Z')
BOOT_ID_PATTERN = re.compile(r"[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}\Z")
BOOT_ID = "cat /proc/sys/kernel/random/boot_id"
# No getprop, process list, /proc/cmdline, environment or network configuration: they can hold
# credentials or private application data.
SNAPSHOT_COMMANDS = {
    "kernel": "uname -srm",
    "kernel_build": "cat /proc/version",
    "boot_id": BOOT_ID,
    "modules": "cat /proc/modules",
    "memory": "cat /proc/meminfo",
    "device_tree_model": "cat /sys/firmware/devicetree/base/model",
    "device_tree_compatible": "cat /sys/firmware/devicetree/base/compatible",
}


class BackupError(Exception):
    pass


def now():
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def parse_mtd(text):
    lines = text.strip().splitlines()
    if not lines or lines[0].split() != ["dev:", "size", "erasesize", "name"]:
        raise BackupError("invalid /proc/mtd header")
    partitions, ids = [], set()
    for line in lines[1:]:
        match = MTD_PATTERN.fullmatch(line)
        if not match:
            raise BackupError("invalid /proc/mtd entry")
        index, size, erase = int(match[1]), int(match[2], 16), int(match[3], 16)
        if index in ids or size <= 0 or erase <= 0 or size % erase:
            raise BackupError("duplicate MTD index or invalid partition geometry")
        ids.add(index)
        partitions.append({"index": index, "name": match[4], "size": size, "erase_size": erase})
    if not partitions:
        raise BackupError("no MTD partitions were reported")
    return sorted(partitions, key=lambda part: part["index"])


def device_text(device, command):
    """Output of a fixed device command, or None when it fails; NUL separators become spaces."""
    code, body = device.shell(command, check=False)
    if code or len(body) > TEXT_LIMIT:
        return None
    return body.strip().replace("\0", " ").strip()


def partition_path(device, index):
    if type(index) is not int or not 0 <= index <= 999:
        raise BackupError("invalid MTD index")
    paths = (f"/dev/mtd{index}", f"/dev/mtd/mtd{index}")
    path = device_text(device, f"if [ -r {paths[0]} ]; then echo {paths[0]}; "
                               f"elif [ -r {paths[1]} ]; then echo {paths[1]}; fi")
    if path not in paths:
        raise BackupError(f"mtd{index} has no readable character device")
    return path


def snapshot(device):
    table = device_text(device, "cat /proc/mtd")
    if table is None:
        raise BackupError("cannot read /proc/mtd")
    partitions = parse_mtd(table)
    observations, unavailable = {}, []
    for key, command in SNAPSHOT_COMMANDS.items():
        value = device_text(device, command)
        if value is None:
            unavailable.append(key)
        else:
            observations[key] = value
    for part in partitions:
        kind = device_text(device, f"cat /sys/class/mtd/mtd{part['index']}/type") or ""
        part["type"] = kind if re.fullmatch(r"[a-z0-9_-]{1,32}", kind) else "unknown"
    return {"schema_version": SCHEMA_VERSION, "captured_at": now(), "adb_serial": device.serial,
            "adb_transport": "legacy-sync", "identity_confirmed_as_tc002": True,
            "observations": observations, "unavailable": unavailable, "mtd_table": table,
            "partitions": partitions}


def create_output(path):
    output = Path(path).expanduser().resolve()
    if output == REPOSITORY or REPOSITORY in output.parents:
        raise BackupError("the backup must go outside the repository")
    if os.path.lexists(Path(path).expanduser()) or output.exists():
        raise BackupError("the backup directory must not exist yet")
    if not output.parent.is_dir():
        raise BackupError("create the private parent directory first")
    output.mkdir(mode=0o700)
    return output


def sync_directory(path):
    # Python cannot flush a Windows directory handle; the files themselves are synced everywhere.
    if os.name != "nt":
        descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)


def atomic_json(path, data):
    descriptor, name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(data, handle, indent=2, ensure_ascii=True)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
        sync_directory(path.parent)
    finally:
        temporary.unlink(missing_ok=True)


def capture(device, destination, timeout=300):
    """Backs up every MTD partition of the device into the new directory destination, each read
    within timeout seconds, and returns the directory."""
    profile = snapshot(device)
    if not BOOT_ID_PATTERN.fullmatch(profile["observations"].get("boot_id", "")):
        raise BackupError("the device reports no boot ID, which tells a reboot during the backup")
    if any(part["type"] not in READABLE_MTD_TYPES for part in profile["partitions"]):
        raise BackupError("only NOR, ROM and RAM partitions can be backed up this way")
    for part in profile["partitions"]:
        part["device"] = partition_path(device, part["index"])
    directory = create_output(destination)
    atomic_json(directory / "profile.json", profile)
    profile_size, profile_hash = file_digest(directory / "profile.json")
    manifest = {"schema_version": SCHEMA_VERSION, "operation": "backup", "status": "incomplete",
                "started_at": now(),
                "profile": {"file": "profile.json", "bytes": profile_size, "sha256": profile_hash},
                "live_unquiesced": True, "restoration_tested": False,
                "partitions": [{"index": part["index"], "file": f"mtd{part['index']}.bin",
                                "expected_bytes": part["size"], "status": "pending"}
                               for part in profile["partitions"]]}
    atomic_json(directory / "manifest.json", manifest)
    try:
        sizes = [part["size"] for part in profile["partitions"]]
        if shutil.disk_usage(directory).free < sum(sizes) + max(sizes) + CHUNK_SIZE:
            raise BackupError("not enough free space on this PC for every MTD partition")
        for part, record in zip(profile["partitions"], manifest["partitions"]):
            record["status"] = "reading"
            atomic_json(directory / "manifest.json", manifest)
            partial = directory / (record["file"] + ".partial")
            device.pull(part["device"], partial, timeout=timeout)
            size, digest = file_digest(partial)
            if size != part["size"]:
                raise BackupError(f"mtd{part['index']} read {size} bytes, the partition has "
                                  f"{part['size']}")
            with partial.open("r+b") as handle:
                os.fsync(handle.fileno())
            os.replace(partial, directory / record["file"])
            sync_directory(directory)
            record.update(status="complete", bytes=size, sha256=digest)
            atomic_json(directory / "manifest.json", manifest)
        if device_text(device, BOOT_ID) != profile["observations"]["boot_id"]:
            raise BackupError("the device rebooted during the backup")
        if parse_mtd(device_text(device, "cat /proc/mtd") or "") != parse_mtd(profile["mtd_table"]):
            raise BackupError("the MTD table changed during the backup")
        manifest.update(status="complete", finished_at=now())
        atomic_json(directory / "manifest.json", manifest)
    except BaseException:
        # An interrupted backup stays incomplete and keeps its partial files for inspection.
        manifest.update(status="incomplete", failure="capture_failed_or_interrupted")
        try:
            atomic_json(directory / "manifest.json", manifest)
        except OSError:
            pass
        raise
    return directory


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise BackupError("duplicate JSON key")
        result[key] = value
    return result


def read_json(path):
    if path.is_symlink() or not path.is_file() or path.stat().st_size > TEXT_LIMIT:
        raise BackupError(f"invalid metadata file {path.name}")
    try:
        data = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_unique_object)
    except (UnicodeError, ValueError) as error:
        raise BackupError(f"invalid JSON in {path.name}") from error
    if not isinstance(data, dict):
        raise BackupError(f"{path.name} is not a JSON object")
    return data


def check_file(directory, record, expected_name, expected_bytes=None):
    if not isinstance(record, dict) or record.get("file") != expected_name:
        raise BackupError("invalid or traversing file name in the manifest")
    size, checksum = record.get("bytes"), record.get("sha256")
    if type(size) is not int or size < 1 or (expected_bytes is not None and size != expected_bytes):
        raise BackupError("invalid size in the manifest")
    if not isinstance(checksum, str) or not re.fullmatch(r"[0-9a-f]{64}", checksum):
        raise BackupError("invalid SHA-256 in the manifest")
    path = directory / expected_name
    if path.is_symlink() or not path.is_file() or path.resolve().parent != directory:
        raise BackupError("backup data must be regular files in the backup directory")
    if path.stat().st_size != size or file_digest(path) != (size, checksum):
        raise BackupError(f"size or SHA-256 mismatch: {expected_name}")


def verify(directory):
    """Checks a backup offline: its manifest, its profile and every partition's size and SHA-256."""
    directory = Path(directory).expanduser().resolve()
    manifest = read_json(directory / "manifest.json")
    if type(manifest.get("schema_version")) is not int or manifest["schema_version"] != SCHEMA_VERSION \
            or manifest.get("status") != "complete":
        raise BackupError("unsupported or incomplete backup")
    if manifest.get("operation") != "backup":
        raise BackupError("not a backup")
    check_file(directory, manifest.get("profile"), "profile.json")
    profile = read_json(directory / "profile.json")
    if type(profile.get("schema_version")) is not int or profile["schema_version"] != SCHEMA_VERSION \
            or not isinstance(profile.get("mtd_table"), str):
        raise BackupError("invalid profile")
    partitions = parse_mtd(profile["mtd_table"])
    listed = profile.get("partitions")
    if not isinstance(listed, list) or len(listed) != len(partitions):
        raise BackupError("the profile's partitions do not match its MTD table")
    for part, item in zip(partitions, listed):
        if not isinstance(item, dict) or any(type(item.get(key)) is not type(value) or item[key] != value
                                             for key, value in part.items()):
            raise BackupError("the profile's partitions do not match its MTD table")
    records = manifest.get("partitions")
    if not isinstance(records, list) or len(records) != len(partitions):
        raise BackupError("the manifest does not cover exactly every MTD partition")
    for part, record in zip(partitions, records):
        if not isinstance(record, dict) or record.get("status") != "complete" or \
                type(record.get("index")) is not int or record["index"] != part["index"]:
            raise BackupError("duplicate, out-of-order or incomplete partition entry")
        if type(record.get("expected_bytes")) is not int or record["expected_bytes"] != part["size"]:
            raise BackupError("a partition size does not match the MTD table")
        check_file(directory, record, f"mtd{part['index']}.bin", part["size"])
    allowed = {"manifest.json", "profile.json", *(record["file"] for record in records)}
    if {path.name for path in directory.iterdir()} != allowed:
        raise BackupError("unexpected files or unfinished temporary data in the backup")
    return {"status": "verified", "directory": str(directory), "partitions": len(records),
            "bytes": sum(record["bytes"] for record in records)}
