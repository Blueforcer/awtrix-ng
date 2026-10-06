"""The installer, the Wi-Fi tool and their ADB layer name the same device paths as the C and C++
programs."""
import re
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE.parent / "lib"))
import tc002_device as device  # noqa: E402
import tc002_install as installer  # noqa: E402
import tc002_wifi as wifi  # noqa: E402

CONTRACT = installer.REPO / "src/platform/tc002/contract"
DEFINE = re.compile(r"^#define\s+(\w+)\s+(.+?)\s*$")
TOKEN = re.compile(r'"((?:[^"\\]|\\.)*)"|(\w+)')


def macros(*headers):
    """The object-like #defines of the headers, with string literals and macro names concatenated."""
    values = {}
    for header in headers:
        text = re.sub(r"/\*.*?\*/", "", header.read_text(encoding="utf-8"), flags=re.S)
        for line in text.splitlines():
            match = DEFINE.match(line)
            if not match or "(" in match.group(1):
                continue
            # This check reads paths and integer literals, not C expressions.
            if TOKEN.sub("", match.group(2)).strip():
                continue
            parts = []
            for literal, name in TOKEN.findall(match.group(2)):
                integer = re.fullmatch(r"(\d+)[uUlL]*", name)
                if integer:
                    parts.append(int(integer.group(1)))
                elif name:
                    parts.append(values[name])
                else:
                    parts.append(literal)
            values[match.group(1)] = parts[0] if len(parts) == 1 else "".join(parts)
    return values


class Layout(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c = macros(CONTRACT / "tc002_layout.h", CONTRACT / "deploy_token.h")

    def path(self, directory, *names):
        return self.c[directory] + "".join(self.c[name] for name in names)

    def test_persistent_paths(self):
        self.assertEqual(installer.DATA_DIR, self.c["TC002_DATA_DIR"])
        for constant, name in (("STATE_DIR", "TC002_STATE"), ("BOOT_COUNTER", "TC002_BOOT_ATTEMPTS"),
                               ("UPDATE_STATE", "TC002_UPDATE_STATE")):
            with self.subTest(constant=constant):
                self.assertEqual(getattr(installer, constant), self.path("TC002_DATA_DIR", name))

    def test_release_slot(self):
        self.assertEqual(installer.RELEASE_MOUNT, self.path("TC002_VOLATILE_DIR", "TC002_RELEASE_MOUNT"))
        self.assertEqual(installer.RELEASE_DAEMON,
                         self.path("TC002_VOLATILE_DIR", "TC002_RELEASE_MOUNT", "TC002_RELEASE_DAEMON"))
        self.assertEqual(installer.LOOP_IN_RES, self.c["TC002_LOOP_MODULE"])
        self.assertEqual(installer.RES_DEVICE, f"{self.c['TC002_MTD_DIR']}/mtd3")

    def test_volatile_paths(self):
        for constant, name in (("TMP_BOOT_COUNTER", "TC002_VOLATILE_BOOT_ATTEMPTS"),
                               ("RESCUE_MARKER", "TC002_RESCUE_MARKER"), ("DEVICE_WORK", "TC002_INSTALL_WORK"),
                               ("LOCK_TOKEN", "TC002_DEPLOY_TOKEN"), ("UPDATE_WORK", "TC002_UPDATE_WORK")):
            with self.subTest(constant=constant):
                self.assertEqual(getattr(installer, constant), self.path("TC002_VOLATILE_DIR", name))
        for constant, name in (("LOADER_LOG", "TC002_LOADER_LOG"), ("VENDOR_MARKER", "TC002_VENDOR_MARKER")):
            with self.subTest(constant=constant):
                self.assertEqual(getattr(device, constant), self.path("TC002_VOLATILE_DIR", name))
        self.assertEqual(wifi.CONTROL_SOCKET, self.path("TC002_VOLATILE_DIR", "TC002_CONTROL_SOCKET"))

    def test_device_nodes(self):
        self.assertEqual(device.USB_ROLE, self.c["TC002_USB_ROLE"])
        self.assertIn(f"echo -n {self.c['TC002_USB_DEVICE_ROLE']} >", device.usb_keeper_script())

    def test_deploy_lock(self):
        self.assertEqual(installer.TOKEN_STALE_SECONDS, self.c["AWTRIX_DEPLOY_TOKEN_STALE_SECONDS"])
        self.assertEqual(installer.LOCK_SECONDS, self.c["AWTRIX_DEPLOY_HOLD_SECONDS"])
        self.assertLess(installer.TOKEN_REFRESH_SECONDS, installer.TOKEN_STALE_SECONDS)


if __name__ == "__main__":
    unittest.main()
