"""Give the framework's multipart parser a deadline.

WebServer::_parseForm in Arduino-ESP32 2.0.17 waits without a limit in three places, and it runs
inside loop(), so the clock stops with it: no HTTP, no frames, no MQTT, and no watchdog either,
because nothing watches the loop task.

  * _uploadReadByte waits for as long as the socket looks connected. A client that vanishes without
    its FIN or RST reaching the clock, which is how an upload over weak Wi-Fi ends, never looks
    otherwise: lwIP's keepalive first asks after two hours. Meanwhile new connections pile up in
    lwIP's accept queue (6 entries), and every one after that is reset right after the handshake.
  * The part-header loop and the form-field loop spin until the next line they want. A client that
    goes away between two lines leaves them spinning for good, FIN or not, until a power cycle.

3.x has a timeout in _uploadReadByte, but that core costs too much heap to move to. The source is
patched at build time, so the image pays only for the three checks. A framework whose source no
longer matches stops the build rather than quietly bringing the hang back.

The upload deadline is 20 s because that is roughly when Windows gives up retransmitting, so the
clock never abandons a transfer the sender could still finish. readStringUntil() has its own limit
(the client timeout, 5 s), so an empty line with nothing behind it means that limit has passed.
"""

import os
import sys

Import("env")

_LINE_GUARD = "if (line.length() == 0 && !client.available()) return false;\n"

_EDITS = (
    (
        "    while(!client.available() && client.connected())\n",
        "    const unsigned long waitStart = millis();\n"
        "    while(!client.available() && client.connected() && millis() - waitStart < 20000)\n",
    ),
    (
        '      if (line.length() > 19 && line.substring(0, 19).equalsIgnoreCase(F("Content-Disposition"))){\n',
        "      " + _LINE_GUARD +
        '      if (line.length() > 19 && line.substring(0, 19).equalsIgnoreCase(F("Content-Disposition"))){\n',
    ),
    (
        '              if (line.startsWith("--"+boundary)) break;\n',
        "              " + _LINE_GUARD +
        '              if (line.startsWith("--"+boundary)) break;\n',
    ),
)


def _patched(env, node):
    with open(node.srcnode().get_abspath(), encoding="utf-8", newline="") as f:
        source = f.read()
    for old, new in _EDITS:
        if source.count(old) != 1:
            sys.stderr.write(
                "webserver_stall.py: the framework's Parsing.cpp changed, this line is no longer "
                "there once:\n%s\nCheck whether _parseForm still needs the deadline.\n" % old
            )
            sys.exit(1)
        source = source.replace(old, new)
    path = os.path.join(env.subst("$BUILD_DIR"), "webserver-stall", "Parsing.cpp")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(source)
    return env.File(path)


env.AddBuildMiddleware(_patched, "*WebServer*Parsing.cpp")
