#!/usr/bin/env python3
"""Provision or check the files that awtrix-linux --hardened loads at startup.

generate writes admin.token, tls.key and a self-signed tls.crt into a directory
without replacing anything that exists; check answers whether the application
accepts an existing set for one origin. Python 3.12 standard library plus the
openssl command for key and certificate generation; Linux only. Secret bytes
never reach stdout, stderr or an exception message.
"""
from __future__ import annotations

import argparse
import datetime
import errno
import hashlib
import ipaddress
import json
import os
import re
import secrets
import shutil
import ssl
import stat
import subprocess
import sys
import time

FILES = ("admin.token", "tls.key", "tls.crt")
MODES = {"admin.token": 0o600, "tls.key": 0o600, "tls.crt": 0o644}
SECRET = {"admin.token", "tls.key"}
MAX_FILE = 65536
TOKEN_BYTES = 32
PORT_RANGE = (1024, 65535)
DAYS_RANGE = (1, 3650)
DEFAULT_DAYS = 365
CIPHERS = "ECDHE+AESGCM:ECDHE+CHACHA20:@SECLEVEL=2"
STEPS = ("create_key_tmp", "write_key", "fsync_key", "create_cert_tmp", "write_cert", "fsync_cert",
         "create_token_tmp", "write_token", "fsync_token", "link_token", "link_key", "link_cert",
         "unlink_tmp_token", "unlink_tmp_key", "unlink_tmp_cert", "fsync_dir")
ORIGIN = re.compile(r"https://([a-z0-9.-]{1,253}):([1-9][0-9]{0,4})")
TOKEN = re.compile(rb"[0-9A-Fa-f]{64}")
# The headers the application's PEM_read_bio_X509 accepts; it skips TRUSTED CERTIFICATE blocks.
CERTIFICATE = re.compile(rb"-----BEGIN (?:X509 )?CERTIFICATE-----.*?"
                         rb"-----END (?:X509 )?CERTIFICATE-----", re.S)
# A tab hides TRUSTED CERTIFICATE blocks from both PEM readers without changing line lengths.
# verify_tls refuses input containing the hidden spelling; the rewrite is reversible.
TRUSTED, HIDDEN = b"TRUSTED CERTIFICATE", b"TRUSTED\tCERTIFICATE"
TRUSTED_BEGIN = re.compile(rb"^-----BEGIN TRUSTED CERTIFICATE-----", re.M)


class ProvisionError(ValueError):
    """The environment or the file set is refused; exit status 1."""


class UsageError(ValueError):
    """An argument is malformed; exit status 2."""


def validate_path(value: str, name: str) -> str:
    if not value or any(ord(c) <= 0x20 or ord(c) == 0x7F for c in value):
        raise UsageError(f"{name} must be a path without whitespace or control characters")
    return value


def parse_origin(origin: str) -> tuple[str, bool, int]:
    match = ORIGIN.fullmatch(origin)
    host = match.group(1) if match else ""
    port = int(match.group(2)) if match else 0
    # Empty and over-long labels are the only names in ORIGIN's alphabet that the IDNA codec
    # behind ssl's server_hostname cannot encode.
    labels_valid = all(0 < len(label) < 64 for label in host.split("."))
    if not match or not labels_valid or not PORT_RANGE[0] <= port <= PORT_RANGE[1]:
        raise UsageError("--origin must be https://HOST:PORT with a lowercase DNS name (labels of 1 to 63 "
                         "characters) or IPv4 address and the explicit port 1024-65535 the application "
                         "listens on")
    is_ipv4 = False
    if re.fullmatch(r"[0-9.]+", host):
        try:
            ipaddress.IPv4Address(host)
        except ValueError:
            raise UsageError("--origin host consists of digits and dots but is not an IPv4 address") from None
        is_ipv4 = True
    return host, is_ipv4, port


def parse_days(value) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not DAYS_RANGE[0] <= value <= DAYS_RANGE[1]:
        raise UsageError(f"--days must be an integer from {DAYS_RANGE[0]} to {DAYS_RANGE[1]}")
    return value


def require_linux() -> None:
    # O_NOFOLLOW, dir_fd-relative calls, hard-link publication and /proc/self/fd
    # are relied on; the application itself only runs on Linux.
    if os.name != "posix" or sys.platform != "linux":
        raise ProvisionError("run the provisioning tool on Linux or WSL")


def run_openssl(executable: str, arguments: list[str]) -> bytes:
    try:
        result = subprocess.run([executable, *arguments], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                stderr=subprocess.DEVNULL, check=False, timeout=60)
    except OSError as error:
        raise ProvisionError(f"OpenSSL {arguments[0]} could not start ({error.strerror})") from None
    # OpenSSL diagnostics are discarded: they can quote input material.
    if result.returncode or not result.stdout:
        raise ProvisionError(f"OpenSSL {arguments[0]} failed")
    return result.stdout


def directory_name(directory: str) -> str:
    # A trailing slash or "." component would make the kernel follow a final symlink despite
    # O_NOFOLLOW. ".." keeps its meaning: it names a different directory.
    name = directory
    while True:
        stripped = name.rstrip("/")
        if len(stripped) > 1 and stripped.endswith("/."):
            stripped = stripped[:-2]
        if stripped == name:
            return name or "/"
        name = stripped


def open_directory(directory: str, data: str | None, require_owner: bool) -> int:
    name = directory_name(directory)
    try:
        fd = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
    except OSError as error:
        reasons = {errno.ELOOP: "directory must not be a symlink", errno.ENOTDIR: "not a directory",
                   errno.ENOENT: "missing"}
        reason = reasons.get(error.errno, error.strerror)
        # Linux reports a symlink as ENOTDIR when O_DIRECTORY and O_NOFOLLOW are combined.
        if error.errno == errno.ENOTDIR and os.path.islink(name):
            reason = reasons[errno.ELOOP]
        raise ProvisionError(f"{directory}: {reason}") from None
    try:
        info = os.fstat(fd)
        if info.st_uid != os.geteuid() and (require_owner or info.st_uid != 0):
            accepted = "the invoking account" if require_owner else "the invoking account or root"
            raise ProvisionError(f"{directory}: directory owner must be {accepted}")
        if info.st_mode & 0o022:
            raise ProvisionError(f"{directory}: directory must not be writable by group or other")
        if data is not None:
            real, real_data = os.path.realpath(name), os.path.realpath(data)
            if real == real_data or real.startswith(real_data.rstrip("/") + "/"):
                raise ProvisionError(f"{directory}: must not be inside the application data directory")
    except BaseException:
        os.close(fd)
        raise
    return fd


def _io(step: str, call, *args, **kwargs):
    """Every filesystem side effect of generate passes through here; tests fault one step."""
    return call(*args, **kwargs)


def io_step(step: str, name: str, call, *args, **kwargs):
    try:
        return _io(step, call, *args, **kwargs)
    except OSError as error:
        raise ProvisionError(f"{name}: {step.replace('_', ' ')} failed ({error.strerror})") from None


def create_temporary(fd: int, name: str, mode: int) -> int:
    handle = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600, dir_fd=fd)
    try:
        os.fchmod(handle, mode)
    except BaseException:
        os.close(handle)
        raise
    return handle


def write_all(handle: int, payload: bytes) -> None:
    view = memoryview(payload)
    while view:
        view = view[os.write(handle, view):]


def write_temporary(fd: int, name: str, kind: str, payload: bytes, created: list[str]) -> str:
    temporary = f".{name}.{secrets.token_hex(4)}.partial"
    handle = io_step(f"create_{kind}_tmp", name, create_temporary, fd, temporary, MODES[name])
    created.append(temporary)
    try:
        io_step(f"write_{kind}", name, write_all, handle, payload)
        io_step(f"fsync_{kind}", name, os.fsync, handle)
    finally:
        os.close(handle)
    return temporary


def generate(directory: str, origin: str, days: int = DEFAULT_DAYS, data: str | None = None,
             openssl: str = "openssl") -> dict:
    validate_path(directory, "--dir")
    host, is_ipv4, port = parse_origin(origin)
    days = parse_days(days)
    if data is not None:
        validate_path(data, "--data")
    executable = shutil.which(validate_path(openssl, "--openssl"))
    if executable is None:
        raise UsageError(f"--openssl: {openssl} is not an executable")
    require_linux()
    fd = open_directory(directory, data, require_owner=True)
    try:
        for name in FILES:
            try:
                os.stat(name, dir_fd=fd, follow_symlinks=False)
            except FileNotFoundError:
                continue
            raise ProvisionError(f"{name}: already exists; the tool never overwrites")
        created: list[str] = []
        try:
            key_pem = run_openssl(executable, ["genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256",
                                               "-pkeyopt", "ec_param_enc:named_curve"])
            key_temporary = write_temporary(fd, "tls.key", "key", key_pem, created)
            subject = "/CN=" + (host if len(host) <= 64 else "AWTRIX NG")
            key_path = os.path.join(os.path.realpath(directory_name(directory)), key_temporary)
            cert_pem = run_openssl(executable, ["req", "-x509", "-new", "-batch", "-sha256", "-key", key_path,
                                                "-days", str(days), "-subj", subject,
                                                "-addext", "subjectAltName=" + ("IP:" if is_ipv4 else "DNS:") + host])
            cert_temporary = write_temporary(fd, "tls.crt", "cert", cert_pem, created)
            token_temporary = write_temporary(fd, "admin.token", "token",
                                              (secrets.token_hex(TOKEN_BYTES) + "\n").encode("ascii"), created)
            publication = (("token", token_temporary, "admin.token"), ("key", key_temporary, "tls.key"),
                           ("cert", cert_temporary, "tls.crt"))
            for kind, temporary, name in publication:
                # A hard link is the one publication primitive that cannot replace an existing name.
                io_step(f"link_{kind}", name, os.link, temporary, name,
                        src_dir_fd=fd, dst_dir_fd=fd, follow_symlinks=False)
                created.append(name)
            for kind, temporary, name in publication:
                io_step(f"unlink_tmp_{kind}", name, os.unlink, temporary, dir_fd=fd)
                created.remove(temporary)
            io_step("fsync_dir", directory, os.fsync, fd)
            return check_directory(fd, directory, origin, host, port, None)
        except BaseException:
            for name in created:
                try:
                    os.unlink(name, dir_fd=fd)
                except OSError:
                    pass
            try:
                os.fsync(fd)
            except OSError:
                pass
            raise
    finally:
        os.close(fd)


def open_provisioned(fd: int, name: str) -> int:
    try:
        handle = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK, dir_fd=fd)
    except OSError as error:
        reasons = {errno.ELOOP: "must not be a symlink", errno.ENOENT: "missing"}
        raise ProvisionError(f"{name}: {reasons.get(error.errno, error.strerror)}") from None
    try:
        info = os.fstat(handle)
        secret = name in SECRET
        if not stat.S_ISREG(info.st_mode):
            raise ProvisionError(f"{name}: must be a regular file")
        if info.st_uid not in (0, os.geteuid()):
            raise ProvisionError(f"{name}: owner must be the invoking account or root")
        if info.st_mode & (0o077 if secret else 0o022):
            raise ProvisionError(f"{name}: mode must not grant group or other access" if secret
                                 else f"{name}: must not be writable by group or other")
        if not 0 < info.st_size <= MAX_FILE:
            raise ProvisionError(f"{name}: must be 1 to {MAX_FILE} bytes")
    except BaseException:
        os.close(handle)
        raise
    return handle


def read_all(handle: int, name: str) -> bytes:
    chunks: list[bytes] = []
    total = 0
    while total <= MAX_FILE:
        chunk = os.read(handle, 65536)
        if not chunk:
            break
        chunks.append(chunk)
        total += len(chunk)
    if total > MAX_FILE:
        raise ProvisionError(f"{name}: must be 1 to {MAX_FILE} bytes")
    return b"".join(chunks)


def verify_token(handle: int) -> None:
    token = read_all(handle, "admin.token")
    if token.endswith(b"\n"):
        token = token[:-1]
    if token.endswith(b"\r"):
        token = token[:-1]
    if not TOKEN.fullmatch(token):
        raise ProvisionError("admin.token: must contain exactly 64 hexadecimal characters")


def refuse_password():
    raise ProvisionError("tls.key: encrypted private keys are not accepted")


def iso_utc(seconds: float) -> str:
    return datetime.datetime.fromtimestamp(seconds, datetime.UTC).strftime("%Y-%m-%dT%H:%M:%SZ")


def handshake(server: ssl.SSLContext, client: ssl.SSLContext, host: str) -> ssl.SSLObject:
    server_in, server_out, client_in, client_out = ssl.MemoryBIO(), ssl.MemoryBIO(), ssl.MemoryBIO(), ssl.MemoryBIO()
    server_side = server.wrap_bio(server_in, server_out, server_side=True)
    client_side = client.wrap_bio(client_in, client_out, server_hostname=host)
    client_done = server_done = False
    try:
        for _ in range(32):
            if not client_done:
                try:
                    client_side.do_handshake()
                    client_done = True
                except ssl.SSLWantReadError:
                    pass
            server_in.write(client_out.read())
            if not server_done:
                try:
                    server_side.do_handshake()
                    server_done = True
                except ssl.SSLWantReadError:
                    pass
            client_in.write(server_out.read())
            if client_done and server_done:
                break
        else:
            raise ProvisionError("tls.crt: handshake did not complete")
    except ssl.SSLCertVerificationError as error:
        raise ProvisionError(f"tls.crt: {error.verify_message}") from None
    except ssl.SSLError as error:
        raise ProvisionError(f"tls.crt: {error.reason or 'handshake failed'}") from None
    return client_side


def verify_tls(cert_fd: int, key_fd: int, host: str, ca_file: str | None) -> dict:
    """Perform the application's TLS handshake in-process with the interpreter's OpenSSL."""
    pem = read_all(cert_fd, "tls.crt")
    key = read_all(key_fd, "tls.key")
    for name, content in (("tls.crt", pem), ("tls.key", key)):
        if b"\0" in content:
            raise ProvisionError(f"{name}: must not contain NUL bytes")
    if HIDDEN in pem:
        raise ProvisionError("tls.crt: a TRUSTED CERTIFICATE block name written with a tab is not accepted")
    skipped = "; TRUSTED CERTIFICATE blocks are skipped" if TRUSTED_BEGIN.search(pem) else ""
    certificates = CERTIFICATE.findall(pem)
    if not certificates:
        raise ProvisionError(f"tls.crt: no PEM CERTIFICATE block{skipped}")
    server = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    server.minimum_version = ssl.TLSVersion.TLSv1_2
    server.set_ciphers(CIPHERS)
    # OpenSSL reads the same bytes that were checked above, not the files a second time.
    chain, secret = os.memfd_create("tls.crt"), os.memfd_create("tls.key")
    try:
        write_all(chain, pem.replace(TRUSTED, HIDDEN))
        write_all(secret, key)
        server.load_cert_chain(f"/proc/self/fd/{chain}", f"/proc/self/fd/{secret}", password=refuse_password)
    except ssl.SSLError as error:
        reason = error.reason or error.library or "invalid PEM"
        raise ProvisionError(f"tls.crt/tls.key: rejected by OpenSSL ({reason}){skipped}") from None
    finally:
        os.close(chain)
        os.close(secret)
    client = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    client.minimum_version = ssl.TLSVersion.TLSv1_2
    client.verify_mode = ssl.CERT_REQUIRED
    client.check_hostname = True
    client.hostname_checks_common_name = False
    if ca_file is not None:
        try:
            client.load_verify_locations(cafile=ca_file)
        except (OSError, ssl.SSLError):
            raise ProvisionError("--ca-file: cannot load trusted certificates") from None
    else:
        reader = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        reader.check_hostname = False
        reader.verify_mode = ssl.CERT_NONE
        leaf = handshake(server, reader, host).getpeercert(binary_form=True)
        try:
            client.load_verify_locations(cadata=leaf)
        except ssl.SSLError:
            raise ProvisionError("tls.crt: first certificate cannot serve as a trust anchor") from None
        client.verify_flags |= ssl.VERIFY_X509_PARTIAL_CHAIN
    client_side = handshake(server, client, host)
    peer = client_side.getpeercert()
    der = client_side.getpeercert(binary_form=True)
    not_before = ssl.cert_time_to_seconds(peer["notBefore"])
    not_after = ssl.cert_time_to_seconds(peer["notAfter"])
    digest = hashlib.sha256(der).hexdigest().upper()
    return {
        "chain_length": len(certificates),
        "chain_verified": ca_file is not None,
        "days_remaining": int((not_after - time.time()) // 86400),
        "fingerprint_sha256": ":".join(digest[i:i + 2] for i in range(0, len(digest), 2)),
        "not_after": iso_utc(not_after),
        "not_before": iso_utc(not_before),
        "openssl": ssl.OPENSSL_VERSION,
        "san": [("IP" if kind == "IP Address" else kind) + ":" + value
                for kind, value in peer.get("subjectAltName", ())],
    }


def check_directory(fd: int, directory: str, origin: str, host: str, port: int, ca_file: str | None) -> dict:
    handles: dict[str, int] = {}
    try:
        for name in FILES:
            handles[name] = open_provisioned(fd, name)
        verify_token(handles["admin.token"])
        result = verify_tls(handles["tls.crt"], handles["tls.key"], host, ca_file)
    finally:
        for handle in handles.values():
            os.close(handle)
    result.update({"directory": os.path.realpath(directory_name(directory)), "origin": origin,
                   "host": host, "port": port})
    return result


def check(directory: str, origin: str, data: str | None = None, ca_file: str | None = None) -> dict:
    validate_path(directory, "--dir")
    host, _, port = parse_origin(origin)
    if data is not None:
        validate_path(data, "--data")
    if ca_file is not None:
        validate_path(ca_file, "--ca-file")
    require_linux()
    fd = open_directory(directory, data, require_owner=False)
    try:
        return check_directory(fd, directory, origin, host, port, ca_file)
    finally:
        os.close(fd)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    generate_parser = commands.add_parser("generate", help="create admin.token, tls.key and a self-signed tls.crt")
    check_parser = commands.add_parser("check", help="report whether awtrix-linux --hardened accepts an existing set")
    for command in (generate_parser, check_parser):
        command.add_argument("--dir", required=True, help="directory holding admin.token, tls.key and tls.crt")
        command.add_argument("--origin", required=True, help="https://HOST:PORT the application is started with")
        command.add_argument("--data", help="application --data directory; the files must not be inside it")
    generate_parser.add_argument("--days", type=int, default=DEFAULT_DAYS, help="certificate validity in days")
    generate_parser.add_argument("--openssl", default="openssl", help="openssl executable")
    check_parser.add_argument("--ca-file", help="PEM trust anchor that must have issued tls.crt")
    args = parser.parse_args(argv)
    try:
        if args.command == "generate":
            result = generate(args.dir, args.origin, args.days, args.data, args.openssl)
        else:
            result = check(args.dir, args.origin, args.data, args.ca_file)
    except UsageError as error:
        parser.error(str(error))
    except (ProvisionError, OSError, ssl.SSLError, subprocess.TimeoutExpired) as error:
        print(f"{args.command} failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
