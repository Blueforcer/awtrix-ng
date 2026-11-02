"""Shared host trust persistence and real HTTPS TTS downloads."""
import http.server
import ssl
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

with tempfile.TemporaryDirectory(prefix="voice-ca-") as tmp:
    root = Path(tmp)
    def openssl(*args):
        subprocess.run(["openssl", *map(str, args)], check=True, capture_output=True)
    for name in ("ca", "wrong"):
        openssl("req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256", "-nodes",
                "-days", "1", "-subj", f"/CN={name}", "-addext", "basicConstraints=critical,CA:TRUE",
                "-keyout", root/f"{name}.key", "-out", root/f"{name}.pem")
    openssl("req", "-new", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256", "-nodes",
            "-subj", "/CN=localhost", "-keyout", root/"leaf.key", "-out", root/"leaf.csr")
    (root/"extensions").write_text("basicConstraints=critical,CA:FALSE\nsubjectAltName=DNS:localhost\n")
    openssl("x509", "-req", "-in", root/"leaf.csr", "-CA", root/"ca.pem", "-CAkey", root/"ca.key",
            "-CAcreateserial", "-days", "1", "-extfile", root/"extensions", "-out", root/"leaf.pem")
    received = []
    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            received.append(self.path)
            if self.path.endswith("redirect"):
                self.send_response(302)
                self.send_header("Location", "/api/tts_proxy/should-not-follow")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            self.send_response(200)
            self.send_header("Content-Length", "10")
            self.end_headers()
            self.wfile.write(b"voice-test")
        def log_message(self, *_): pass
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(root/"leaf.pem", root/"leaf.key")
    server.socket = context.wrap_socket(server.socket, server_side=True)
    worker = threading.Thread(target=server.serve_forever)
    worker.start()
    try:
        subprocess.run([sys.argv[1], str(root/"ca.pem"), str(root/"wrong.pem"), str(root/"leaf.pem"),
                        str(server.server_port)], check=True, timeout=40)
        assert received == ["/api/tts_proxy/ok", "/api/tts_proxy/redirect"], received
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
