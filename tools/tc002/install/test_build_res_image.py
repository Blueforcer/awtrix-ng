"""Tests for build_res_image.py. Needs squashfs-tools (run in WSL/Linux).

The real device dump is private and never part of the repository; point
AWTRIX_TC002_RES_BACKUP at a backup directory (manifest.json + mtd3.bin) and AWTRIX_TC002_BUNDLE
at a release bundle to include it.
"""
import filecmp
import hashlib
import json
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_res_image as builder  # noqa: E402
import bundle as bundle_format  # noqa: E402
import image_test_support as inspect_image  # noqa: E402

HAVE_SQUASHFS = bool(shutil.which("mksquashfs") and shutil.which("unsquashfs"))
HELP = subprocess.run(["mksquashfs", "-help"], capture_output=True,
                      text=True) if HAVE_SQUASHFS else None
HAVE_OVERRIDE = bool(HELP) and "-pseudo-override" in HELP.stdout + HELP.stderr
IMAGE_TOOLS = Path(__file__).resolve().parents[3] / "docs/assets/tc002/image"
HAVE_IMAGE_TOOLS = bool(shutil.which("node") and (IMAGE_TOOLS / "build.json").is_file())
COUNTER = 1790000000
LOADER = "lib/libawtrix-loader.so"
LOOP = "awtrix-ng/loop.ko"
ADDED = {LOADER, "awtrix-ng", LOOP}
STOCK_CONFIG = (b'{\n\t"baud":"115200",\n\t"startupLibPath":"/res/lib/libzkgui.so",\n'
                b'\t"languageCode":"zh_CN",\n\t"resPath":"/res/ui/"\n}')
MKFS_TIME = 1785901771
PARTITION = builder.DEFAULT_PARTITION_SIZE
ERASE = builder.DEFAULT_ERASE_SIZE
BLUETOOTH = ("bin/gattserverbin", "bin/hciattach", "bin/hciconfig", "bin/hcitool")


def fake_elf(e_type, size):
    header = bytearray(b"\x7fELF\x01\x01\x01" + bytes(9))
    header += struct.pack("<HH", e_type, 40)
    return bytes(header) + random.Random(size + e_type).randbytes(size)


def fake_loader(size=4096):
    return fake_elf(3, size)


def fake_loop(size=3000):
    return fake_elf(1, size) + b"\0vermagic=4.9.84 SMP preempt mod_unload ARMv7 p2v8 \0"


def fake_image(size):
    """Bytes that pass for a release image: a squashfs superblock claiming all of them."""
    data = bytearray(random.Random(size).randbytes(size))
    data[:4] = b"hsqs"
    data[40:48] = size.to_bytes(8, "little")
    return bytes(data)


BUNDLE = (("bin/awtrix-linux", 60000), ("bin/awtrix-tc002d", 20000),
          ("bin/awtrix-tc002-flash", 9000), ("bin/awtrix-tc002-audio-pcm", 3000), ("bin/udhcpc", 900),
          ("bin/dhcp-callback", 700), ("bin/wpa_supplicant", 1200), ("share/index.html.gz", 7000),
          ("share/ca-certificates.crt", 5000), ("share/licenses.txt.gz", 3000),
          ("lib/modules/aic8800_bsp.ko", 500), ("lib/modules/aic8800_fdrv.ko", 800),
          ("lib/modules/awtrix_pcm.ko", 4000), ("lib/libawtrix-loader.so", 4096),
          ("lib/modules/loop.ko", 3000))


def make_bundle(root, image_bytes=200000, counter=COUNTER):
    """A bundle as build_bundle.sh leaves it, with a stand-in release image of image_bytes."""
    noise = random.Random(str(root))
    for path, size in BUNDLE:
        (root / path).parent.mkdir(parents=True, exist_ok=True)
        data = (fake_loader(size) if path == LOADER else fake_loop(size)
                if path.endswith("loop.ko") else noise.randbytes(size))
        (root / path).write_bytes(data)
    manifest = bundle_format.write_manifest(root, "1.1.2-gabc1234", "abc1234", False, counter=counter)
    image = fake_image(image_bytes)
    (root / bundle_format.IMAGE).write_bytes(image)
    manifest["image"] = {"size": len(image), "sha256": hashlib.sha256(image).hexdigest()}
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return bundle_format.verify(root)


def unpack(image, destination):
    subprocess.run(["unsquashfs", "-no-progress", "-quiet", "-d", str(destination), str(image)],
                   check=True, capture_output=True, preexec_fn=lambda: os.umask(0))


def assert_same_listing(test, stock_image, built_image, changed_sizes, removed=BLUETOOTH):
    stock, built = inspect_image.listing(stock_image), inspect_image.listing(built_image)
    test.assertEqual(built.keys() - stock.keys(), ADDED)
    test.assertEqual(stock.keys() - built.keys(), set(removed))
    test.assertEqual(built[LOADER]["mode"], "-rwxr-xr-x")
    test.assertEqual((built["awtrix-ng"]["mode"], built["awtrix-ng"]["owner"]), ("drwxr-xr-x", "0/0"))
    test.assertEqual((built[LOOP]["mode"], built[LOOP]["owner"]), ("-rw-r--r--", "0/0"))
    for path, entry in stock.items():
        if path in removed:
            continue
        ours = dict(built[path])
        if path in changed_sizes or entry["mode"].startswith("d"):
            ours["size"] = entry["size"]
        test.assertEqual(ours, entry, path)


def compare_unchanged(test, stock_tree, built_tree, changed, removed=BLUETOOTH):
    for directory, _, files in os.walk(stock_tree):
        for name in files:
            relative = Path(directory, name).relative_to(stock_tree)
            ours = built_tree / relative
            if relative.as_posix() in removed:
                test.assertFalse(ours.exists() or ours.is_symlink(), relative)
                continue
            if relative.as_posix() in changed:
                continue
            test.assertTrue(filecmp.cmp(Path(directory, name), ours, shallow=False), relative)
            test.assertEqual(Path(directory, name).lstat().st_mode, ours.lstat().st_mode)
            test.assertEqual(int(Path(directory, name).lstat().st_mtime), int(ours.lstat().st_mtime))


@unittest.skipUnless(HAVE_OVERRIDE and HAVE_IMAGE_TOOLS, "needs squashfs-tools 4.6+, Node.js and prepared WASM image tools")
class SyntheticImageTest(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="awtrix-res-test-"))
        self.addCleanup(shutil.rmtree, self.work, ignore_errors=True)
        tree = self.source_tree = self.work / "stock-tree"
        for directory in ("bin", "etc", "font", "lib", "ui/icons"):
            (tree / directory).mkdir(parents=True)
        (tree / "etc/EasyUI.cfg").write_bytes(STOCK_CONFIG)
        (tree / "lib/libzkgui.so").write_bytes(random.Random(7).randbytes(300000))
        (tree / "lib/libAEC.so").write_bytes(b"\0" * 70000 + b"tail")
        os.link(tree / "lib/libAEC.so", tree / "lib/libAEC-copy.so")
        for name in BLUETOOTH:
            (tree / name).write_bytes(f"#!/bin/sh\necho {name}\n".encode())
        (tree / "ui/icons/a.png").write_bytes(b"\x89PNG" + bytes(range(200)))
        os.symlink("icons/a.png", tree / "ui/current.png")
        self.bundle = self.work / "bundle"
        self.manifest = make_bundle(self.bundle)
        self.loader = self.bundle / LOADER
        self.make_stock()

    def make_stock(self):
        tree = self.source_tree
        for path in sorted(tree.rglob("*"), reverse=True):
            if not path.is_symlink():
                os.chmod(path, 0o770)
                os.utime(path, (1700000000 + len(str(path)), 1700000000 + len(str(path))))
        os.chmod(tree, 0o770)
        image = self.stock_image = self.work / "stock.sqsh"
        subprocess.run(["mksquashfs", str(tree), str(image), "-noappend", "-no-progress", "-quiet",
                        "-comp", "xz", "-b", "131072", "-force-uid", "1000", "-force-gid", "1000",
                        "-mkfs-time", str(MKFS_TIME)], check=True, capture_output=True)
        data = image.read_bytes()
        self.dump = self.work / "mtd3.bin"
        self.dump.write_bytes(data + b"\xff" * (PARTITION - len(data)))
        self.sha = hashlib.sha256(self.dump.read_bytes()).hexdigest()
        self.stock_tree = self.work / "stock-unpacked"
        shutil.rmtree(self.stock_tree, ignore_errors=True)
        unpack(image, self.stock_tree)

    def build(self, name, loader=None, bundle=None, **kwargs):
        return builder.build(self.dump, self.sha, loader, self.work / name, partition_size=PARTITION,
                             work_parent=self.work, bundle=bundle or self.bundle, **kwargs)

    def test_image_changes_only_config_loader_loop_and_bluetooth(self):
        manifest = self.build("out")
        out = self.work / "out"
        self.assertEqual((out / "stock-res.img").read_bytes(), self.dump.read_bytes())
        self.assertEqual(manifest["stock"]["sha256"], self.sha)
        image = (out / "awtrix-res.img").read_bytes()
        self.assertEqual(manifest["awtrix"]["sha256"], hashlib.sha256(image).hexdigest())
        self.assertEqual(len(image) % 4096, 0)
        superblock = inspect_image.parse_superblock(image)
        self.assertEqual((superblock["compression"], superblock["block_size"],
                          superblock["mkfs_time"], superblock["id_count"]), ("xz", 131072, MKFS_TIME, 2))
        self.assertEqual(inspect_image.owners(out / "awtrix-res.img"), {"1000/1000", "0/0"})
        built = self.work / "built-unpacked"
        unpack(out / "awtrix-res.img", built)
        config = json.loads((built / "etc/EasyUI.cfg").read_bytes())
        self.assertEqual(config["startupLibPath"], "/res/lib/libawtrix-loader.so")
        self.assertEqual((built / "etc/EasyUI.cfg").read_bytes(),
                         STOCK_CONFIG.replace(b"libzkgui.so", b"libawtrix-loader.so"))
        self.assertEqual((built / LOADER).read_bytes(), self.loader.read_bytes())
        self.assertEqual((built / LOADER).stat().st_mode & 0o7777, 0o755)
        self.assertEqual(int((built / LOADER).stat().st_mtime), MKFS_TIME)
        self.assertEqual((built / LOOP).read_bytes(), (self.bundle / "lib/modules/loop.ko").read_bytes())
        self.assertEqual((built / LOOP).stat().st_mode & 0o7777, 0o644)
        self.assertEqual((built / "awtrix-ng").stat().st_mode & 0o7777, 0o755)
        self.assertEqual({int((built / p).stat().st_mtime) for p in ("awtrix-ng", LOOP)}, {MKFS_TIME})
        self.assertEqual(sorted(p.name for p in (built / "awtrix-ng").iterdir()), ["loop.ko"])
        self.assertEqual((built / "lib").stat().st_mtime, (self.stock_tree / "lib").stat().st_mtime)
        self.assertEqual(list((built / "bin").iterdir()), [])
        self.assertEqual((built / "bin").stat().st_mtime, (self.stock_tree / "bin").stat().st_mtime)
        self.assertEqual(manifest["awtrix"]["removed"], list(BLUETOOTH))
        self.assertEqual(os.readlink(built / "ui/current.png"), "icons/a.png")
        self.assertEqual((built / "lib/libAEC.so").stat().st_ino,
                         (built / "lib/libAEC-copy.so").stat().st_ino)
        compare_unchanged(self, self.source_tree, built, {"etc/EasyUI.cfg"})
        for path in self.source_tree.rglob("*"):
            relative = path.relative_to(self.source_tree)
            if relative.as_posix() in BLUETOOTH:
                continue
            self.assertEqual((built / relative).lstat().st_mode, path.lstat().st_mode, relative)
        self.assertEqual((built / "etc/EasyUI.cfg").stat().st_mode & 0o7777, 0o770)
        assert_same_listing(self, self.stock_image, out / "awtrix-res.img", {"etc/EasyUI.cfg"})
        for path in bundle_format.REQUIRED:
            self.assertFalse((built / path).exists(), "no release file goes into res")
        header = (superblock["bytes_used"] + ERASE - 1) // ERASE * ERASE
        self.assertEqual(manifest["awtrix"]["slot"], {
            "header": header, "image": header + ERASE, "capacity": PARTITION - header - ERASE,
            "release": self.manifest["release"], "release_image_bytes": 200000})
        self.assertEqual(manifest["awtrix"]["loop"], {
            "path": LOOP, "bytes": (self.bundle / "lib/modules/loop.ko").stat().st_size,
            "sha256": hashlib.sha256((self.bundle / "lib/modules/loop.ko").read_bytes()).hexdigest()})
        stored = json.loads((out / "res-images.json").read_text())
        self.assertEqual(stored, manifest)

    def test_drops_only_the_bluetooth_files_the_stock_image_has(self):
        (self.source_tree / "bin/gattserverbin").unlink()
        self.make_stock()
        manifest = self.build("out")
        present = [name for name in BLUETOOTH if name != "bin/gattserverbin"]
        self.assertEqual(manifest["awtrix"]["removed"], present)
        assert_same_listing(self, self.stock_image, self.work / "out/awtrix-res.img",
                            {"etc/EasyUI.cfg"}, present)

    def test_refuses_a_bluetooth_file_with_another_name(self):
        os.link(self.source_tree / "bin/hciattach", self.source_tree / "lib/hciattach")
        self.make_stock()
        with self.assertRaisesRegex(builder.BuildError, "bin/hciattach.*other names"):
            self.build("out")

    def test_output_is_deterministic(self):
        first = self.build("one")["awtrix"]["sha256"]
        second = self.build("two")["awtrix"]["sha256"]
        self.assertEqual(first, second)

    def test_refuses_mismatching_dump(self):
        with self.assertRaisesRegex(builder.BuildError, "does not match"):
            builder.build(self.dump, "0" * 64, None, self.work / "x", partition_size=PARTITION,
                          bundle=self.bundle)

    def test_refuses_an_image_that_already_carries_the_loader(self):
        self.build("out")
        image = (self.work / "out/awtrix-res.img").read_bytes()
        self.dump.write_bytes(image + b"\xff" * (PARTITION - len(image)))
        self.sha = hashlib.sha256(self.dump.read_bytes()).hexdigest()
        with self.assertRaisesRegex(builder.BuildError, "not a stock|already exists|several owners"):
            self.build("again")

    def replace_host_file(self, path, data):
        (self.bundle / path).write_bytes(data)
        manifest = json.loads((self.bundle / "manifest.json").read_text())
        for entry in manifest["hostOnly"]:
            if entry["path"] == path:
                entry.update(size=len(data), sha256=hashlib.sha256(data).hexdigest())
        (self.bundle / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    def test_refuses_a_host_loader_or_loop_module(self):
        loader = self.work / "host-loader.so"
        loader.write_bytes(b"ELF" + bytes(64))
        with self.assertRaisesRegex(builder.BuildError, "32-bit"):
            self.build("host", loader=loader)
        self.replace_host_file("lib/modules/loop.ko", fake_loader(3000))
        with self.assertRaisesRegex(builder.BuildError, "ARM kernel module"):
            self.build("shared")
        self.replace_host_file("lib/modules/loop.ko", fake_elf(1, 3000))
        with self.assertRaisesRegex(builder.BuildError, "vermagic"):
            self.build("plain")

    def test_a_release_image_the_slot_cannot_hold_stops_the_build(self):
        big = self.work / "big"
        manifest = make_bundle(big, image_bytes=PARTITION)
        with self.assertRaisesRegex(builder.BuildError, "release slot"):
            self.build("out", bundle=big)
        self.assertFalse((self.work / "out/awtrix-res.img").exists())
        loader = self.work / "huge-loader.so"
        loader.write_bytes(fake_loader(PARTITION))
        with self.assertRaisesRegex(builder.BuildError, "image does not fit|loader bytes|release slot"):
            self.build("huge", loader=loader)

    def test_needs_a_valid_bundle(self):
        manifest = json.loads((self.bundle / "manifest.json").read_text())
        manifest["release"] = "1.1.2-gabc1234-000000000000"
        (self.bundle / "manifest.json").write_text(json.dumps(manifest))
        with self.assertRaisesRegex(builder.BuildError, "bundle .*release name"):
            self.build("out")
        bundle_format.write_manifest(self.bundle, "1.1.2-gabc1234", "abc1234", False, counter=COUNTER)
        (self.bundle / bundle_format.IMAGE).unlink()
        with self.assertRaisesRegex(builder.BuildError, "no release image"):
            self.build("out")
        with self.assertRaisesRegex(builder.BuildError, "--bundle"):
            builder.build(self.dump, self.sha, None, self.work / "out", partition_size=PARTITION)

    def test_refuses_output_inside_repository_and_existing_images(self):
        with self.assertRaisesRegex(builder.BuildError, "outside"):
            builder.build(self.dump, self.sha, None, builder.REPO / "build-res",
                          partition_size=PARTITION, bundle=self.bundle)
        self.build("out")
        with self.assertRaisesRegex(builder.BuildError, "--replace"):
            self.build("out")
        self.build("out", replace=True)


@unittest.skipUnless(HAVE_OVERRIDE and HAVE_IMAGE_TOOLS and os.environ.get("AWTRIX_TC002_RES_BACKUP") and
                     os.environ.get("AWTRIX_TC002_BUNDLE"),
                     "set AWTRIX_TC002_RES_BACKUP to a private backup directory and "
                     "AWTRIX_TC002_BUNDLE to a release bundle")
class DeviceDumpTest(unittest.TestCase):
    def test_device_dump(self):
        backup = Path(os.environ["AWTRIX_TC002_RES_BACKUP"])
        bundle = Path(os.environ["AWTRIX_TC002_BUNDLE"])
        dump, expected, _, size, erase = builder.read_backup(backup)
        with tempfile.TemporaryDirectory(prefix="awtrix-res-device-") as work:
            work = Path(work)
            manifest = builder.build(dump, expected, None, work / "out", size, erase, work,
                                     bundle=bundle)
            self.assertEqual(manifest["stock"]["sha256"], expected)
            self.assertEqual(manifest["awtrix"]["removed"], list(BLUETOOTH))
            self.assertLessEqual(manifest["awtrix"]["slot"]["release_image_bytes"],
                                 manifest["awtrix"]["slot"]["capacity"])
            self.assertEqual((work / "out/stock-res.img").read_bytes(), Path(dump).read_bytes())
            stock_tree, built_tree = work / "stock", work / "built"
            used = inspect_image.parse_superblock(Path(dump).read_bytes())["bytes_used"]
            (work / "stock.sqsh").write_bytes(Path(dump).read_bytes()[:used])
            unpack(work / "stock.sqsh", stock_tree)
            unpack(work / "out/awtrix-res.img", built_tree)
            compare_unchanged(self, stock_tree, built_tree, {"etc/EasyUI.cfg"})
            assert_same_listing(self, work / "stock.sqsh", work / "out/awtrix-res.img",
                                {"etc/EasyUI.cfg"})
            self.assertEqual((built_tree / "lib/libzkgui.so").stat().st_mode & 0o7777, 0o770)
            self.assertEqual((built_tree / LOADER).read_bytes(), (bundle / LOADER).read_bytes())
            print(json.dumps({"bytes": manifest["awtrix"]["bytes"], "partition": size,
                              "slot": manifest["awtrix"]["slot"]}, indent=2))


if __name__ == "__main__":
    unittest.main()
