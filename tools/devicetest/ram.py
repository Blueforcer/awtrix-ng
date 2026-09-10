#!/usr/bin/env python3
"""Exercise script RAM reclamation on a device; temporary app is removed on exit.

Run before an upgrade with --baseline to record the old memory behaviour without
requiring the reclamation checks to pass. Existing apps and stores are untouched.
"""

import argparse
import json
from pathlib import Path
import time

from run import Api


NAME = "tramcheck"


def checked(api, method, path, body=None, ctype=None):
    status, result = api.call(method, path, body, ctype)
    if status != 200:
        raise RuntimeError(f"{method} {path}: HTTP {status}: {result}")
    return result


def probe(api, source, keys):
    result = checked(api, "PUT", "/api/v1/apps/script/" + NAME,
                     "# @headless true\n" + source, "text/plain")
    if result.get("error"):
        raise RuntimeError(str(result["error"]))
    # loop() is called once per second; the stack probe waits 40 callbacks for idle trimming.
    deadline = time.monotonic() + 70
    while time.monotonic() < deadline:
        rows = checked(api, "GET", "/api/v1/scripts/shared")
        values = {r["key"]: r["value"] for r in rows if r["owner"] == NAME}
        if all(key in values for key in keys):
            checked(api, "DELETE", "/api/v1/apps/" + NAME)
            return values
        apps = checked(api, "GET", "/api/v1/apps")
        error = next((a.get("error") for a in apps if a["name"] == NAME), None)
        if error:
            raise RuntimeError(str(error))
        time.sleep(0.2)
    raise RuntimeError(f"timed out waiting for {keys}; received {values}")


TRACE = """
import gc
class Probe
  var phase, before
  def init() self.phase=0 end
  def make()
    var held=[]
    for i:0..599 held.push(i) end
    def fail()
      var n=size(held)
      raise 'value_error', 'ram probe'
    end
    return fail
  end
  def loop()
    if self.phase==0
      gc.collect()
      self.before=gc.allocated()
    elif self.phase==1
      var f=self.make()
      try f() except .. shared.set('caught',true) end
      f=nil
    elif self.phase==2
      gc.collect()
      shared.set('retained',gc.allocated()-self.before)
    end
    if self.phase<3 self.phase+=1 end
  end
end
return Probe()
"""

STACK = """
import gc
class Probe
  var phase, before
  def init() self.phase=0 end
  def dive(n)
    if n==0 return 0 end
    return 1+self.dive(n-1)
  end
  def loop()
    if self.phase==0
      gc.collect()
      self.before=gc.allocated()
    elif self.phase==1
      shared.set('deep',self.dive(80))
    elif self.phase==40
      gc.collect()
      shared.set('retained',gc.allocated()-self.before)
    elif self.phase==41
      shared.set('regrown',self.dive(80))
    end
    if self.phase<42 self.phase+=1 end
  end
end
return Probe()
"""

REGEX = r"""
import string
class Probe
  def setup()
    var m=re.search('"temperature":([0-9.]+)', '{"temperature":21.5}')
    shared.set('capture',m[1])
    var all=re.matchall('\\d+', 'a1 b22 c333')
    shared.set('all',size(all)==3 && all[2]=='333')
    var s=string.char(0)
    m=re.match('.',s)
    shared.set('nul',m!=nil && size(m[0])==1 && m[0]==s)
    var long=''
    for i:0..249 long+='x' end
    m=re.match(long,long)
    shared.set('long',m!=nil && size(m[0])==250)
    m=re.match('abc','abc')
    shared.set('small',m!=nil && m[0]=='abc')
    m=re.match('(a)(b)(c)(d)(e)(f)(g)','abcdefg')
    shared.set('groups',size(m)==8 && m[7]=='g')
  end
end
return Probe()
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--auth")
    parser.add_argument("--baseline", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    api = Api(args.host, args.auth)
    original = checked(api, "GET", "/api/v1/device")
    apps = checked(api, "GET", "/api/v1/apps")
    if any(a["name"] == NAME for a in apps):
        raise RuntimeError(f"{NAME} already exists; refusing to overwrite it")
    results = {"deviceBefore": original}
    failures = []

    def expect(name, condition):
        print(("PASS " if condition else "FAIL ") + name, flush=True)
        if not condition:
            failures.append(name)

    try:
        results["trace"] = probe(api, TRACE, ["caught", "retained"])
        expect("caught exception preserves control flow", results["trace"]["caught"] is True)
        print("trace retained bytes:", results["trace"]["retained"], flush=True)
        if not args.baseline:
            expect("trace no longer roots the 600-element list",
                   results["trace"]["retained"] < 2048)
        results["stack"] = probe(api, STACK, ["deep", "retained", "regrown"])
        expect("deep recursion and growth after idle", results["stack"]["deep"] == 80
               and results["stack"]["regrown"] == 80)
        print("stack retained bytes:", results["stack"]["retained"], flush=True)
        if not args.baseline:
            expect("idle stack capacity released", results["stack"]["retained"] < 2048)
        results["regex"] = probe(api, REGEX, ["capture", "all", "nul", "long", "small", "groups"])
        expect("regex capture text", results["regex"]["capture"] == "21.5")
        for key in ("all", "nul", "long", "small", "groups"):
            expect("regex " + key, results["regex"][key] is True)
        constants = "class Probe\n def big()\n var x\n"
        constants += "".join(f"x='unique{i}'\n" for i in range(60))
        constants += "x='unique55'\n" * 100
        constants += "return x\nend\ndef setup() shared.set('answer',self.big()) end\nend\nreturn Probe()\n"
        results["constants"] = probe(api, constants, ["answer"])
        expect("deduplicated constants preserve return value", results["constants"]["answer"] == "unique55")
    finally:
        checked(api, "DELETE", "/api/v1/apps/" + NAME)
        results["deviceAfter"] = checked(api, "GET", "/api/v1/device")
        expect("device did not reboot", results["deviceAfter"]["uptimeSeconds"] >= original["uptimeSeconds"])
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print(f"{len(failures)} failures", flush=True)
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
