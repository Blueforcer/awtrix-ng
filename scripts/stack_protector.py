"""Stack canaries on the code that handles outside input.

The framework compiles everything with -fstack-protector, which only protects functions holding a
char array. GCC 8.4 loses that under LTO: a plain `char line[120]` gets no canary any more, while
`uint8_t line[120]` still does. It was checked on a small file and in the image, where logf() had
none after LTO arrived. Linking with the flag or adding -funsigned-char changes nothing.

-fstack-protector-strong does not depend on the element type, and under LTO it stays with each file.
So the code listed here gets it and everything else keeps the framework's flag. A new source file
that parses something from the network, a file or a script belongs on this list.

Prebuilt ESP-IDF libraries (lwIP, mbedTLS, mDNS and so on) are not compiled here and not under LTO,
so they keep their own canaries.

Measured cost: classic image 1 534 592 -> 1 561 888 B (+27 296), S3 1 582 720 -> 1 609 968 B
(+27 248), static RAM unchanged.

This has to be the last of the build middlewares. audio_opt.py and heap_probe_opt.py have already
built their files by the time it runs, which is why audio_opt.py asks for these flags itself and why
HeapProbe.cpp, outside LTO, needs nothing. webserver_stall.py hands on its patched copy of
Parsing.cpp, under the build directory.
"""

Import("env")

_PROJECT = (
    "src/main.cpp",
    "src/media/",
    "src/persistence/",
    "src/platform/esp32/",
    "src/sound/BuzzerSink.cpp",
    "src/system/",
    "src/transport/",
    "src/core/CoreEngine.cpp",
    "src/core/Dispatcher.cpp",
    "src/core/JsonColor.cpp",
    "src/core/Settings.cpp",
    "src/core/WeekdayBarConfig.cpp",
    "src/core/api/",
    "src/core/apps/BuiltinAppConfig.cpp",
    "src/core/apps/ClockText.cpp",
    "src/core/assets/",
    "src/core/backup/",
    "src/core/icons/",
    "src/core/mirror/",
    "src/core/mqtt/",
    "src/core/net/",
    "src/core/notify/",
    "src/core/payload/",
    "src/core/radio/",
    "src/core/render/PaletteFile.cpp",
    "src/core/render/TextEncoding.cpp",
    "src/core/script/",
    "src/core/sound/",
    "src/core/synth/",
    "lib/PubSubClient/",
    "lib/TJpg_Decoder/",
    "lib/berry/",
)

# Berry's compiler stays out. Its input is a script, which runs whatever it likes anyway, and it
# recurses per nesting level: with canaries on these three, a small script with one callback
# overflowed the 8 KB loop stack while it compiled.
_COMPILER = ("/be_parser.c", "/be_lexer.c", "/be_code.c")

_FRAMEWORK = (
    "/cores/esp32/",
    "/libraries/DNSServer/",
    "/libraries/ESPmDNS/",
    "/libraries/FS/",
    "/libraries/HTTPClient/",
    "/libraries/LittleFS/",
    "/libraries/Preferences/",
    "/libraries/Update/",
    "/libraries/WebServer/",
    "/libraries/WiFi/",
    "/libraries/WiFiClientSecure/",
    "/webserver-stall/",
)


def _handles_input(path):
    path = path.replace("\\", "/")
    if path.endswith(_COMPILER):
        return False
    root = env["PROJECT_DIR"].replace("\\", "/").rstrip("/") + "/"
    if path.startswith(root) and path[len(root) :].startswith(_PROJECT):
        return True
    return any(part in path for part in _FRAMEWORK)


def _input_code_flags(_, flags):
    return [flag for flag in flags if flag != "-fstack-protector"] + ["-fstack-protector-strong"]


def _strong_on_input_code(build_env, node):
    if not hasattr(node, "srcnode") or not _handles_input(node.srcnode().get_abspath()):
        return node
    return build_env.Object(node, CCFLAGS=build_env.InputCodeFlags(build_env["CCFLAGS"]))


env.AddMethod(_input_code_flags, "InputCodeFlags")
env.AddBuildMiddleware(_strong_on_input_code)
