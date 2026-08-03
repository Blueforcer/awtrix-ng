
import sys

Import("env")

if sys.platform == "win32":
    env.Append(LIBS=["ws2_32", "winmm"])
else:
    env.Append(LINKFLAGS=["-pthread"])
    env.Append(CCFLAGS=["-pthread"])
