# PubSubClient 2.8 — local security maintenance

Upstream: <https://github.com/knolleary/pubsubclient>

Base: release 2.8, commit `2d228f2f862a95846c65a8518c79f48dfc8f188c`.
The source/header and MIT license were copied from the existing PlatformIO 2.8
dependency. No TC002 port code is included. Original file SHA-256 values:

| File | SHA-256 |
| --- | --- |
| `src/PubSubClient.cpp` | `c5ab036263d514791b1955fe44aacca103b2eea4ca07cd539bff25dd88cc4ede` |
| `src/PubSubClient.h` | `376ddb9ecda5816dfeff344f8742253d487adc16272455ebe2dfa4c071cdd348` |
| `LICENSE.txt` | `b416abfc7294f9279480389e4825463ac71b2b064b892aa7d95abd5d4b6edc63` |

Local patch `awtrix.1` validates inbound lengths before copying or indexing topic
and QoS1 packet-ID bytes. Malformed, unsupported QoS2/reserved-QoS, truncated and
oversized nonstreamed packets disconnect without invoking the application callback.
Streamed payloads remain supported when their topic/header fit the existing
buffer. QoS0/QoS1 delivery and acknowledgement contracts remain unchanged.

This addresses an out-of-bounds topic copy in upstream 2.8: for example,
`30 02 FF FF` declares a 65535-byte topic inside a two-byte MQTT body. It previously
reached `memmove` without checking that the topic existed in the packet/buffer.

All ESP32, Linux and cross-build targets use this same local source.
There is no additional receive buffer. `tests/mqtt_parser` exercises valid traffic
and malformed packets against it, with AddressSanitizer/UndefinedBehaviorSanitizer
enabled by default for the standalone regression target on supported host compilers.
