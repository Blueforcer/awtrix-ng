"""A small MQTT 3.1.1 broker for tests: QoS 0 only, retained messages ignored. It records who
connected, what they subscribed to and every publish, and lets a test publish itself."""

from __future__ import annotations

import socket
import threading


def _length(sock):
    value, shift = 0, 0
    while True:
        byte = sock.recv(1)
        if not byte:
            raise ConnectionError
        value |= (byte[0] & 0x7F) << shift
        if not byte[0] & 0x80:
            return value
        shift += 7


def _encode_length(n):
    out = bytearray()
    while True:
        byte = n % 128
        n //= 128
        out.append(byte | (0x80 if n else 0))
        if not n:
            return bytes(out)


def _string(data, at):
    size = int.from_bytes(data[at:at + 2], "big")
    return data[at + 2:at + 2 + size].decode(errors="replace"), at + 2 + size


def matches(pattern, topic):
    p, t = pattern.split("/"), topic.split("/")
    for i, part in enumerate(p):
        if part == "#":
            return True
        if i >= len(t) or (part != "+" and part != t[i]):
            return False
    return len(p) == len(t)


def publish_packet(topic, payload):
    body = len(topic.encode()).to_bytes(2, "big") + topic.encode() + payload
    return bytes([0x30]) + _encode_length(len(body)) + body


class Broker:
    def __init__(self):
        self.server = socket.socket()
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server.bind(("127.0.0.1", 0))
        self.server.listen()
        self.port = self.server.getsockname()[1]
        self.lock = threading.Lock()
        self.clients = {}
        self.published = []
        self.running = True
        threading.Thread(target=self._accept, daemon=True).start()

    def close(self):
        self.running = False
        self.server.close()
        with self.lock:
            for sock in list(self.clients):
                sock.close()

    def subscriptions(self, client_id):
        with self.lock:
            return [f for sock, c in self.clients.items() if c["id"] == client_id for f in c["filters"]]

    def publish(self, topic, payload: bytes):
        with self.lock:
            targets = [s for s, c in self.clients.items() if any(matches(f, topic) for f in c["filters"])]
        for sock in targets:
            try:
                sock.sendall(publish_packet(topic, payload))
            except OSError:
                pass

    def _accept(self):
        while self.running:
            try:
                sock, _ = self.server.accept()
            except OSError:
                return
            threading.Thread(target=self._serve, args=(sock,), daemon=True).start()

    def _serve(self, sock):
        try:
            while True:
                head = sock.recv(1)
                if not head:
                    break
                size = _length(sock)
                data = b""
                while len(data) < size:
                    chunk = sock.recv(size - len(data))
                    if not chunk:
                        raise ConnectionError
                    data += chunk
                kind = head[0] >> 4
                if kind == 1:
                    _, at = _string(data, 0)
                    client_id, at = _string(data, at + 4)
                    with self.lock:
                        self.clients[sock] = {"id": client_id, "filters": []}
                    sock.sendall(b"\x20\x02\x00\x00")
                elif kind == 3:
                    topic, at = _string(data, 0)
                    if head[0] & 0x06:
                        at += 2
                    with self.lock:
                        self.published.append((topic, data[at:]))
                    self.publish(topic, data[at:])
                elif kind == 8:
                    packet_id = data[:2]
                    at, granted = 2, b""
                    while at < len(data):
                        topic, at = _string(data, at)
                        at += 1
                        with self.lock:
                            self.clients[sock]["filters"].append(topic)
                        granted += b"\x00"
                    sock.sendall(bytes([0x90]) + _encode_length(2 + len(granted)) + packet_id + granted)
                elif kind == 10:
                    packet_id = data[:2]
                    at = 2
                    while at < len(data):
                        topic, at = _string(data, at)
                        with self.lock:
                            if topic in self.clients[sock]["filters"]:
                                self.clients[sock]["filters"].remove(topic)
                    sock.sendall(b"\xb0\x02" + packet_id)
                elif kind == 12:
                    sock.sendall(b"\xd0\x00")
                elif kind == 14:
                    break
        except (OSError, ConnectionError, KeyError, IndexError):
            pass
        finally:
            with self.lock:
                self.clients.pop(sock, None)
            sock.close()
