
import os
import shutil
import sys

Import("env")

if sys.platform == "win32":
    if shutil.which("g++") is None:
        kit = os.environ.get("W64DEVKIT", r"D:\tools\w64devkit")
        bindir = os.path.join(kit, "bin")
        if os.path.exists(os.path.join(bindir, "g++.exe")):
            env.PrependENVPath("PATH", bindir)
        else:
            print("native_toolchain.py: no g++ in PATH and no w64devkit at %s" % bindir)

    env.Append(LINKFLAGS=["-static-libstdc++", "-static-libgcc"])

    env.Append(LIBS=["ws2_32"])
