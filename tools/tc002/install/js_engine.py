"""Private stdin/stdout transport to the shared JavaScript installation engine."""
import json
import shutil
import subprocess
from pathlib import Path

MAX_LINE = 48 * 1024 * 1024


class EngineError(Exception):
    pass


def run(operation, options, dispatch):
    node = shutil.which("node")
    if not node:
        raise EngineError("Node.js is required for the shared installation engine")
    command = [node, str(Path(__file__).with_name("engine.mjs"))]
    errors = {}
    with subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          text=True, encoding="utf-8") as process:
        try:
            while True:
                line = process.stdout.readline(MAX_LINE + 1)
                if not line or len(line) > MAX_LINE:
                    raise EngineError("installation engine connection closed or exceeded its limit")
                request = json.loads(line)
                reply = {"requestId": request["requestId"]}
                action, args = request["command"], request.get("args", {})
                finished = action == "engine_finish"
                try:
                    if action == "engine_start":
                        reply["result"] = {"operation": operation, "options": options}
                    elif finished:
                        reply["result"] = None
                    else:
                        value = dispatch(action, args)
                        if isinstance(value, bytes):
                            import base64
                            reply["bytes"] = base64.b64encode(value).decode("ascii")
                        else:
                            reply["result"] = value
                except Exception as error:
                    code = f"host-{len(errors) + 1}"
                    errors[code] = error
                    reply["error"] = {"message": str(error), "code": code}
                process.stdin.write(json.dumps(reply) + "\n")
                process.stdin.flush()
                if finished:
                    process.stdin.close()
                    if process.wait(timeout=10):
                        raise EngineError("installation engine exited unsuccessfully")
                    if args.get("error"):
                        original = errors.get(args.get("code"))
                        error = original if original and str(original) == args["error"] else EngineError(args["error"])
                        if original and error is not original:
                            error.__cause__ = original
                        error.recovery_needed = args.get("recoveryNeeded", False)
                        if isinstance(error, EngineError):
                            error.code = args.get("code")
                        error.cleanup_failures = [*getattr(error, "cleanup_failures", []),
                                                  *args.get("cleanupFailures", [])]
                        raise error
                    return args.get("result")
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
