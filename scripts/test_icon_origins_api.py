"""HTTP regression against a freshly spawned simulator; never contacts a device."""

import base64
import hashlib
import io
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
import zipfile


ROOT = Path(__file__).resolve().parent.parent
BINARY = ROOT / ".pio/build/native_sim" / ("program.exe" if os.name == "nt" else "program")
GIF = base64.b64decode("R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7")


def main():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    base = f"http://127.0.0.1:{port}"
    process = None
    with tempfile.TemporaryDirectory(prefix="origins-api-", dir=ROOT / ".pio") as directory:
        data = Path(directory).resolve()
        assert data.is_relative_to((ROOT / ".pio").resolve())

        def request(method, path, body=None, content_type="application/json", expected=200):
            if isinstance(body, dict):
                body = json.dumps(body).encode()
            req = urllib.request.Request(base + path, data=body, method=method)
            if body is not None:
                req.add_header("Content-Type", content_type)
            try:
                response = urllib.request.urlopen(req, timeout=5)
            except urllib.error.HTTPError as error:
                response = error
            with response:
                payload = response.read()
                assert response.status == expected, (method, path, response.status, payload)
                return json.loads(payload) if payload.startswith(b"{") else payload

        def start():
            proc = subprocess.Popen(
                [str(BINARY), "--port", str(port), "--data", str(data),
                 "--webui", str(ROOT / "webui/index.html"), "--no-matrix"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
            )
            for _ in range(50):
                if proc.poll() is not None:
                    raise RuntimeError("simulator stopped before becoming ready")
                try:
                    request("GET", "/api/v1/icons/origins")
                    return proc
                except urllib.error.URLError:
                    time.sleep(0.1)
            proc.terminate()
            raise RuntimeError("simulator did not start")

        def upload(path, filename, payload):
            boundary = "awtrix-origins-regression-boundary"
            body = (f'--{boundary}\r\nContent-Disposition: form-data; name="file"; '
                    f'filename="{filename}"\r\nContent-Type: application/octet-stream\r\n\r\n').encode()
            body += payload + f"\r\n--{boundary}--\r\n".encode()
            return request("POST", path, body, f"multipart/form-data; boundary={boundary}")

        origin = {"name": "mail.gif", "hub": "https://custom.example:8443/awtrix/icons/",
                  "slug": "mail", "sha256": hashlib.sha256(GIF).hexdigest()}
        try:
            process = start()
            assert request("GET", "/api/v1/icons/origins") == {"icons": []}
            request("PUT", "/api/v1/icons/origins", origin, expected=404)
            request("PUT", "/api/v1/icons/origins", {**origin, "name": "../mail.gif"}, expected=400)
            upload("/api/v1/files?dir=/ICONS", "mail.gif", GIF)
            request("PUT", "/api/v1/icons/origins", origin)
            assert request("GET", "/api/v1/icons/origins") == {"icons": [origin]}
            assert json.loads((data / "config/icon-origins.json").read_text()) == {"icons": [origin]}
            # The second save exercises replacement of an existing metadata file, including Windows.
            origin["slug"] = "mail-2"
            request("PUT", "/api/v1/icons/origins", origin)
            upload("/api/v1/files?dir=/ICONS", "mail.gif", GIF + b"locally changed")
            assert request("GET", "/ICONS/mail.gif") != GIF
            assert request("GET", "/api/v1/icons/origins") == {"icons": [origin]}
            process.terminate()
            process.wait(timeout=5)
            process = start()
            assert request("GET", "/api/v1/icons/origins") == {"icons": [origin]}
            # A failed atomic metadata write must leave the existing image and link intact.
            (data / "config/icon-origins.tmp").mkdir()
            request("DELETE", "/api/v1/files?path=/ICONS/mail.gif", expected=500)
            assert (data / "ICONS/mail.gif").exists()
            assert request("GET", "/api/v1/icons/origins") == {"icons": [origin]}
            (data / "config/icon-origins.tmp").rmdir()
            request("DELETE", "/api/v1/files?path=/ICONS/mail.gif")
            upload("/api/v1/files?dir=/ICONS", "mail.gif", GIF)
            assert request("GET", "/api/v1/icons/origins") == {"icons": []}
            # Metadata precedes image in the ZIP; the restore must defer linking until images exist.
            archive = io.BytesIO()
            with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_STORED) as backup:
                backup.writestr("manifest.json", json.dumps({"app": "awtrix-ng", "backupFormat": 1}))
                backup.writestr("config/icon-origins.json", json.dumps({"icons": [origin]}))
                backup.writestr("ICONS/mail.gif", GIF)
            restored = upload("/api/v1/restore", "backup.zip", archive.getvalue())
            assert restored["ok"] and restored["applied"]["iconOrigins"] == 1
            assert request("GET", "/api/v1/icons/origins") == {"icons": [origin]}
            request("DELETE", "/api/v1/icons/origins?name=mail.gif")
            request("DELETE", "/api/v1/icons/origins?name=mail.gif")
            assert request("GET", "/ICONS/mail.gif") == GIF
            print("PASS: icon origins HTTP validation, atomic replacement, reboot persistence, local edits, deletion and ZIP restore")
        finally:
            if process is not None and process.poll() is None:
                process.terminate()
                process.wait(timeout=5)


if __name__ == "__main__":
    main()
