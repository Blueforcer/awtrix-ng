import argparse
import json
from pathlib import Path
import socketserver
import struct
import threading
import time
import uuid

from run import Api


def exact(sock, size):
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("incomplete request")
        data += chunk
    return data


class Device(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(5)
        try:
            transaction, protocol, length, unit, fc, address, count = struct.unpack(
                ">HHHBBHH", exact(self.request, 12))
            assert protocol == 0 and length == 6 and unit == self.server.unit
            self.server.requests.append((fc, address, count))
            if address == 102:
                time.sleep(2.5)
                return
            if address == 104:
                return
            if address == 100:
                pdu = bytes([fc | 0x80, 2])
            elif fc <= 2:
                pdu = bytes([fc, (count + 7) // 8]) + bytes([0x55]) * ((count + 7) // 8)
            else:
                values = [self.server.value] * count
                if address == 10:
                    values = [0x41BC, 0]
                pdu = bytes([fc, count * 2]) + struct.pack(">" + "H" * count, *values)
            if address == 101:
                transaction ^= 1
            size = 65535 if address == 103 else len(pdu) + 1
            answer = struct.pack(">HHHB", transaction, 0, size, unit) + pdu
            for offset in range(0, len(answer), 3):
                self.request.sendall(answer[offset:offset + 3])
                time.sleep(0.002)
        except (ConnectionError, OSError):
            pass


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def checked(api, method, path, body=None, ctype=None):
    status, result = api.call(method, path, body, ctype)
    if not 200 <= status < 300:
        raise RuntimeError(f"{method} {path}: {status}: {result}")
    return result


def source(host, port, unit, value, full):
    cases = ["['readHoldingRegisters',0,1,0]", "['readInputRegisters',10,2,0]",
             "['readCoils',0,9,0]", "['readDiscreteInputs',0,9,0]",
             "['readHoldingRegisters',100,1,2]", "['readHoldingRegisters',101,1,-1]",
             "['readHoldingRegisters',102,1,-1]", "['readHoldingRegisters',103,1,-1]",
             "['readHoldingRegisters',104,1,-1]", "['readHoldingRegisters',0,125,0]",
             "['readCoils',0,2000,0]"]
    cases = cases if full else cases[:1]
    return f"""
import modbus
class Probe
  var step, busy, value, cases
  def init()
    self.step = 0
    self.busy = false
    self.value = nil
    self.cases = [{','.join(cases)}]
  end
  def received(values, error)
    var c = self.cases[self.step]
    assert(error == c[3], str(self.step) + ': error ' + str(error))
    if error == 0
      assert(size(values) == c[2], 'count')
      if c[0] == 'readInputRegisters'
        assert(modbus.float32(values[0],values[1]) == 23.5, 'float')
      elif c[0] == 'readHoldingRegisters'
        assert(values[0] == {value}, 'wrong endpoint')
        self.value = values[0]
      else
        assert(values[0] == 1 && values[1] == 0 && values[8] == 1, 'bits')
      end
    else
      assert(values == nil, 'error values')
    end
    self.step += 1
    shared.set('passed', self.step)
    self.busy = false
  end
  def loop()
    if self.busy || self.step >= size(self.cases) return end
    self.busy = true
    var c = self.cases[self.step]
    var cb = / v,e -> self.received(v,e)
    var opts = {{'port':{port},'unit':{unit}}}
    if c[0] == 'readHoldingRegisters' modbus.readHoldingRegisters('{host}',c[1],c[2],cb,opts)
    elif c[0] == 'readInputRegisters' modbus.readInputRegisters('{host}',c[1],c[2],cb,opts)
    elif c[0] == 'readCoils' modbus.readCoils('{host}',c[1],c[2],cb,opts)
    else modbus.readDiscreteInputs('{host}',c[1],c[2],cb,opts) end
  end
  def draw()
    clear()
    if self.value != nil
      text(1,6,str(self.value),0xFFFFFF)
      pixel(0,0,self.value)
    end
  end
end
return Probe()
"""


def exercise(args):
    api = Api(args.host, args.auth)
    before = checked(api, "GET", "/api/v1/device")
    original = before.get("currentApp")
    names = ["tmb_" + uuid.uuid4().hex[:8] for _ in range(2)]
    servers, installed = [], []
    try:
        for index, name in enumerate(names):
            server = Server((args.bind, 0), Device)
            server.unit = index + 1
            server.value = 235 + index
            server.requests = []
            servers.append(server)
            threading.Thread(target=server.serve_forever, daemon=True).start()
            port = server.server_address[1]
            print(f"Modbus endpoint {args.bind}:{port}, unit {server.unit}", flush=True)
            installed.append(name)
            checked(api, "PUT", "/api/v1/apps/script/" + name,
                    source(args.bind, port, server.unit, server.value, index == 0), "text/plain")
        deadline = time.monotonic() + 110
        values = {}
        while time.monotonic() < deadline:
            for app in checked(api, "GET", "/api/v1/apps"):
                if app["name"] in names and app.get("error"):
                    raise RuntimeError(str(app["error"]))
            values = {r["owner"]: r["value"] for r in checked(api, "GET", "/api/v1/scripts/shared")
                      if r["owner"] in names and r["key"] == "passed"}
            if values.get(names[0]) == 11 and values.get(names[1]) == 1:
                break
            time.sleep(0.5)
        else:
            raise RuntimeError(f"timed out: {values}; requests: {[s.requests for s in servers]}")
        print("PASS: 12 network cases across two independent endpoints", flush=True)
        for index, name in enumerate(names):
            checked(api, "PUT", "/api/v1/apps/active", json.dumps({"name": name, "fast": True}),
                    "application/json")
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                screen = checked(api, "GET", "/api/v1/display/screen")
                if screen.get("pixels", [None])[0] == servers[index].value:
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("display pixel does not contain the fetched value")
            print(f"PASS: {name} displays {servers[index].value}", flush=True)
        return {"before": before, "after": checked(api, "GET", "/api/v1/device"),
                "passed": 14, "requests": [s.requests for s in servers]}
    finally:
        for name in installed:
            checked(api, "DELETE", "/api/v1/apps/" + name)
        if original:
            checked(api, "PUT", "/api/v1/apps/active", json.dumps({"name": original, "fast": True}),
                    "application/json")
        for server in servers:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--bind", required=True)
    parser.add_argument("--auth")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = exercise(args)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print("14 checks passed; temporary apps removed and original app restored")
