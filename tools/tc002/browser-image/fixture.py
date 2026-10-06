#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="awtrix-synthetic-res-") as temporary:
        root = Path(temporary)
        for name in ("etc", "lib", "bin"):
            (root / name).mkdir(mode=0o755)
        for name, data in {
            "etc/EasyUI.cfg": b'{"startupLibPath":"/res/lib/libzkgui.so","sample":true}\n',
            "lib/libzkgui.so": b"test", "bin/hciattach": b"bt",
        }.items():
            (root / name).write_bytes(data)
            (root / name).chmod(0o644)
        os.symlink("libzkgui.so", root / "lib/link")
        root.chmod(0o755)
        subprocess.run(["mksquashfs", str(root), str(args.output.resolve()), "-noappend", "-quiet",
                        "-comp", "xz", "-b", "131072", "-processors", "1", "-force-uid", "1000",
                        "-force-gid", "1000", "-mkfs-time", "1600000000", "-all-time", "1500000000"], check=True)


if __name__ == "__main__":
    main()
