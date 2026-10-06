#!/usr/bin/env python3
"""Provision Wi-Fi credentials into a TC002 running awtrix-tc002d, over USB ADB only.

The request travels through a host-loopback `adb forward` to the daemon's root-only control
socket; nothing is pushed to the device, and neither SSID nor password appear in any argv. Like
tc002_install.py it takes adb from --adb, $ADB or PATH and, when adb does not list the serial,
waits for the clock to be switched off and on and keeps USB ADB up (tools/tc002/lib/tc002_device.py).
"""

import argparse
import getpass
import json
import socket
import struct
import sys
import time
import warnings
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "lib"))
from tc002_device import Device, DeviceError, connect, find_adb, usb_serial  # noqa: E402
sys.path.insert(0, str(Path(__file__).resolve().parent / "install"))
import js_engine  # noqa: E402

CONTROL_SOCKET = "/tmp/awtrix-tc002d/control.sock"
MAX_FRAME = 4096
EXIT_OK, EXIT_DEVICE, EXIT_USAGE, EXIT_TRANSPORT = 0, 1, 2, 3


class ToolError(Exception):
    def __init__(self, message, code):
        super().__init__(message)
        self.code = code


def encode_frame(command, payload=b""):
    body = command.encode("ascii") + b"\n" + payload
    if len(body) > MAX_FRAME:
        raise ToolError("request too large", EXIT_USAGE)
    return struct.pack(">I", len(body)) + body


def read_exact(conn, size, deadline):
    data = b""
    while len(data) < size:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise ToolError("timed out waiting for the daemon", EXIT_TRANSPORT)
        conn.settimeout(remaining)
        try:
            chunk = conn.recv(size - len(data))
        except socket.timeout:
            raise ToolError("timed out waiting for the daemon", EXIT_TRANSPORT) from None
        if not chunk:
            raise ToolError("connection closed before a complete reply (is awtrix-tc002d running?)",
                            EXIT_TRANSPORT)
        data += chunk
    return data


def read_frame(conn, timeout):
    deadline = time.monotonic() + timeout
    (length,) = struct.unpack(">I", read_exact(conn, 4, deadline))
    if length == 0 or length > MAX_FRAME:
        raise ToolError(f"invalid reply length {length}", EXIT_TRANSPORT)
    return read_exact(conn, length, deadline)


def engine(options, forward=None):
    def dispatch(action, args):
        if action == "request":
            payload = args.get("payload")
            return forward.request(args["command"], json.dumps(payload, ensure_ascii=False).encode("utf-8")
                                   if payload is not None else b"")
        if action == "status":
            print(args["message"])
        elif action == "sleep":
            time.sleep(args["ms"] / 1000)
        elif action == "now":
            return time.monotonic() * 1000
        elif action != "request":
            raise ToolError("unsupported Wi-Fi host operation", EXIT_TRANSPORT)
    try:
        return js_engine.run("wifi", options, dispatch)
    except js_engine.EngineError as error:
        code = {"wifi-device": EXIT_DEVICE, "wifi-usage": EXIT_USAGE}.get(getattr(error, "code", None), EXIT_TRANSPORT)
        raise ToolError(str(error), code) from error


class Forward:
    def __init__(self, device, target):
        self.device = device
        self.target = target
        self.port = None

    def __enter__(self):
        output = self.device.adb("forward", "tcp:0", f"localfilesystem:{self.target}",
                                 timeout=15).strip()
        if not output.isdigit():
            raise ToolError(f"unexpected adb forward reply: {output!r}", EXIT_TRANSPORT)
        self.port = int(output)
        return self

    def __exit__(self, *exc):
        if self.port is not None:
            try:
                self.device.adb("forward", "--remove", f"tcp:{self.port}", timeout=15)
            except DeviceError as error:
                print(f"warning: could not remove forward tcp:{self.port}: {error}", file=sys.stderr)
        return False

    def request(self, command, payload=b"", timeout=10.0):
        try:
            with socket.create_connection(("127.0.0.1", self.port), timeout=timeout) as conn:
                conn.sendall(encode_frame(command, payload))
                reply = read_frame(conn, timeout)
        except OSError as error:
            raise ToolError(f"control socket unreachable: {error}", EXIT_TRANSPORT) from None
        try:
            return json.loads(reply.decode("utf-8"))
        except (UnicodeDecodeError, ValueError):
            raise ToolError("the daemon sent a reply that is not JSON", EXIT_TRANSPORT) from None


def prompt_credentials(args):
    ssid = args.ssid
    if ssid is None:
        if args.password_stdin:
            raise ToolError("--password-stdin needs --ssid", EXIT_USAGE)
        ssid = input("SSID: ")
    engine({"command": "validate", "credentials": {"ssid": ssid, "password": ""}})
    if args.password_stdin:
        password = sys.stdin.readline().rstrip("\r\n")
    else:
        with warnings.catch_warnings():
            warnings.simplefilter("error", getpass.GetPassWarning)
            try:
                password = getpass.getpass("Password (empty: open network, or keep the stored one for this SSID): ")
            except getpass.GetPassWarning:
                raise ToolError("refusing to read the password: no terminal to disable echo; "
                                "use --password-stdin", EXIT_USAGE) from None
    engine({"command": "validate", "credentials": {"ssid": ssid, "password": password}})
    if password == "" and not args.open:
        if args.password_stdin:
            raise ToolError("empty password: pass --open to configure an open network", EXIT_USAGE)
        answer = input("No password: open network, or the stored key if this SSID is configured. Continue? [y/N] ")
        if answer.strip().lower() not in ("y", "yes"):
            raise ToolError("cancelled", EXIT_USAGE)
    return ssid, password


def print_networks(networks):
    if not networks:
        print("no networks found")
        return
    print(f"{'RSSI':>5}  {'SECURITY':8}  SSID")
    for network in networks:
        security = "secured" if network.get("secure") else "open"
        print(f"{network.get('rssi', 0):>5}  {security:8}  {network.get('ssid', '')}")


def run(argv, adb_program=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--serial", required=True, help="ADB serial of the TC002 (USB)")
    parser.add_argument("--allow-network", action="store_true", help="permit a network (host:port) serial")
    parser.add_argument("--adb", help="adb executable (default: $ADB, else adb on PATH)")
    parser.add_argument("--socket", default=CONTROL_SOCKET, help="daemon control socket on the device")
    parser.add_argument("command", nargs="?", default="set", choices=("set", "scan", "status", "wifi-status"))
    parser.add_argument("--ssid", help="SSID (prompted when omitted)")
    parser.add_argument("--password-stdin", action="store_true", help="read the password from the first stdin line")
    parser.add_argument("--open", action="store_true",
                        help="confirm an empty password (open network, or keep the stored key for the same SSID)")
    parser.add_argument("--wait", type=float, default=40.0, help="seconds to follow the link after set (0: off)")
    args = parser.parse_args(argv)

    if not usb_serial(args.serial) and not args.allow_network:
        raise ToolError(f"{args.serial} is a network transport; provisioning is USB-only "
                        "(use --allow-network to override)", EXIT_USAGE)
    device = Device(adb_program or find_adb(args.adb), args.serial)
    if args.command == "scan":
        connect(device, keep_present=False)
        with Forward(device, args.socket) as forward:
            print_networks(engine({"command": "scan"}, forward))
        return EXIT_OK
    if args.command != "set":
        connect(device, keep_present=False)
        with Forward(device, args.socket) as forward:
            print(json.dumps(engine({"command": args.command}, forward), indent=2, ensure_ascii=False))
        return EXIT_OK

    ssid, password = prompt_credentials(args)
    credentials = {"ssid": ssid, "password": password}
    password = None
    try:
        connect(device, keep_present=False)
        with Forward(device, args.socket) as forward:
            engine({"command": "set", "credentials": credentials, "waitSeconds": args.wait}, forward)
    finally:
        credentials.clear()
    return EXIT_OK


def main(argv=None, adb_program=None):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="backslashreplace")
    try:
        return run(sys.argv[1:] if argv is None else argv, adb_program)
    except ToolError as error:
        print(f"error: {error}", file=sys.stderr)
        return error.code
    except DeviceError as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_TRANSPORT
    except KeyboardInterrupt:
        print("cancelled", file=sys.stderr)
        return EXIT_USAGE


if __name__ == "__main__":
    sys.exit(main())
