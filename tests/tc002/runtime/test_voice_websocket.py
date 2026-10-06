"""Real loopback WebSocket/TLS boundaries; no third-party Python packages."""
import base64
import hashlib
import os
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

binary = sys.argv[1]

def read_exact(sock, size):
    out = b""
    while len(out) < size:
        chunk = sock.recv(size - len(out))
        if not chunk:
            raise EOFError
        out += chunk
    return out

def read_client_frame(conn):
    head = read_exact(conn, 2)
    assert head[1] & 0x80, "client frames are masked"
    length = head[1] & 0x7F
    assert length < 126, head
    mask = read_exact(conn, 4)
    payload = read_exact(conn, length)
    return head[0], bytes(x ^ mask[i % 4] for i, x in enumerate(payload))

def serve_session(conn, mode):
    if mode == "oversize":
        conn.sendall(b"\x81\x7f" + struct.pack("!Q", 65537))
    elif mode == "binary":
        conn.sendall(b"\x82\x01x")
    elif mode == "masked":
        mask = os.urandom(4)
        conn.sendall(b"\x81\x81" + mask + bytes([ord("x") ^ mask[0]]))
    elif mode == "echo":
        opcode, payload = read_client_frame(conn)
        assert (opcode, payload) == (0x82, b"\x07\x00\xff"), (opcode, payload)
        # Local STT/intent/TTS stages can arrive in one render frame.
        conn.sendall(b"\x81\x01x" * 20 + b"\x81\x05exact")
    elif mode == "fragments":
        conn.sendall(b"\x89\x04ping")
        assert read_client_frame(conn) == (0x8A, b"ping")
        conn.sendall(b"\x01\x02ex" + b"\x89\x00" + b"\x80\x03act")
        assert read_client_frame(conn) == (0x8A, b"")
        conn.sendall(b"\x88\x02\x03\xe8")
        assert read_client_frame(conn) == (0x88, b"\x03\xe8")
        return
    conn.recv(1024)

def case(host, tls, trust, mode, cert, key):
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    listener.settimeout(10)
    port = listener.getsockname()[1]
    errors = []
    def serve():
        try:
            with listener:
                conn, _ = listener.accept()
                conn.settimeout(8)
                with conn:
                    if tls:
                        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                        context.load_cert_chain(cert, key)
                        try:
                            conn = context.wrap_socket(conn, server_side=True)
                        except ssl.SSLError:
                            if mode == "reject":
                                return
                            raise
                    with conn:
                        request = b""
                        while b"\r\n\r\n" not in request:
                            request += read_exact(conn, 1)
                        headers = dict(line.split(": ", 1) for line in request.decode().split("\r\n")[1:] if ": " in line)
                        accept = base64.b64encode(hashlib.sha1((headers["Sec-WebSocket-Key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest())
                        if mode == "badaccept":
                            accept = base64.b64encode(b"\x00" * 20)
                        conn.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n\r\n")
                        serve_session(conn, mode)
        except Exception as error:
            errors.append(error)
    worker = threading.Thread(target=serve)
    if mode != "notrust":
        worker.start()
    result = subprocess.run([binary, host, str(port), "tls" if tls else "plain", trust, mode], timeout=12)
    if mode == "notrust":
        listener.settimeout(0.5)
        try:
            listener.accept()
            errors.append(AssertionError("connected without a trust store"))
        except socket.timeout:
            pass
        listener.close()
    else:
        worker.join(10)
    assert result.returncode == 0, (host, tls, mode, result.returncode)
    assert not errors, (mode, errors)
    assert not worker.is_alive()

def certificate(directory, name):
    cert, key = str(Path(directory)/f"{name}.pem"), str(Path(directory)/f"{name}-key.pem")
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1", "-subj", f"/CN={name}",
                    "-addext", f"subjectAltName=DNS:{name}", "-keyout", key, "-out", cert], check=True, capture_output=True)
    return cert, key

with tempfile.TemporaryDirectory(prefix="awtrix-voice-tls-") as directory:
    cert, key = certificate(directory, "localhost")
    other, _ = certificate(directory, "other")
    case("127.0.0.1", False, "", "echo", cert, key)
    case("localhost", True, cert, "echo", cert, key)
    case("localhost", True, other, "reject", cert, key)
    case("127.0.0.1", True, cert, "reject", cert, key)
    case("localhost", True, "", "notrust", cert, key)
    case("127.0.0.1", False, "", "oversize", cert, key)
    case("127.0.0.1", False, "", "fragments", cert, key)
    case("127.0.0.1", False, "", "binary", cert, key)
    case("127.0.0.1", False, "", "masked", cert, key)
    case("127.0.0.1", False, "", "badaccept", cert, key)
print("Voice transport: exact binary PCM, trusted TLS, untrusted CA, wrong hostname, no trust store, oversized, "
      "fragmented with pings and close, binary, masked and bad accept passed")
