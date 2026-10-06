
import os
import re

_REGFUNC = re.compile(
    r'^\s*be_regfunc\(\s*\w+\s*,\s*"([A-Za-z_]\w*)"\s*,\s*\w+\s*\)\s*;'
    r'[ \t]*(?://[ \t]*(\S.*?))?[ \t]*$'
)
_MORE = re.compile(r"^\s*//\s*([A-Za-z_]\w*)\(.*\)\s*$")
_DEF = re.compile(
    r"^\s*def\s+([A-Za-z_]\w*)\s*\(([^)]*)\)"
    r"[ \t]*(?:#[ \t]*(\S.*?))?[ \t]*$"
)
_MODULE = re.compile(r"^\s*(?:var\s+)?([A-Za-z_]\w*)\s*=\s*module\(\s*'([A-Za-z_]\w*)'\s*\)")
_MEMBER = re.compile(r"^\s*([A-Za-z_]\w*)\.([A-Za-z_]\w*)\s*=\s*([A-Za-z_]\w*)\s*$")
_CONF_MODULE = re.compile(r"^\s*#define\s+BE_USE_([A-Z0-9_]+)_MODULE\s+(\d+)")


def _read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def _public(name):
    return not name.startswith("_")


def _params(raw):
    """Render a Berry parameter list the way a reader expects to see it.

    A vararg (`*rest`) shows as `rest?`. Optionality that comes from Berry
    nil-filling a parameter the caller left off has no marker in the source at
    all, so a def that wants to advertise it carries a trailing `#` comment
    holding the whole signature -- the same rule be_regfunc lines follow in
    ScriptBindings.cpp. One convention, one place to look.
    """
    out = []
    for p in (p.strip() for p in raw.split(",")):
        if not p:
            continue
        out.append(p[1:] + "?" if p.startswith("*") else p)
    return ", ".join(out)


def _device_builtins(src):
    """be_regfunc entries from ScriptBindings.cpp, public ones only.

    A binding with more than one call shape lists the others on comment lines right below
    it, each starting with the binding's name.
    """
    out = []
    last = None
    for line in src.splitlines():
        m = _REGFUNC.match(line)
        if m:
            name, sig = m.group(1), m.group(2)
            last = name if _public(name) else None
            if last:
                out.append(sig if sig else name + "()")
            continue
        more = _MORE.match(line)
        if last and more and more.group(1) == last:
            out.append(more.group(0).strip()[2:].strip())
            continue
        last = None
    return out


def _prelude(src, inherited_modules=()):
    """The modules, module members and public functions the prelude defines."""
    defs = {}
    sigs = {}
    modules = list(inherited_modules)
    members = []
    for line in src.splitlines():
        m = _DEF.match(line)
        if m:
            defs[m.group(1)] = _params(m.group(2))
            if m.group(3):
                sigs[m.group(1)] = m.group(3)
            continue
        m = _MODULE.match(line)
        if m:
            modules.append(m.group(1))
            continue
        m = _MEMBER.match(line)
        if m and m.group(1) in modules:
            members.append((m.group(1) + "." + m.group(2), m.group(3)))

    api = []
    for dotted, impl in members:
        api.append(sigs.get(impl) or "%s(%s)" % (dotted, defs.get(impl, "")))
    for name, params in defs.items():
        if _public(name):
            api.append(sigs.get(name) or "%s(%s)" % (name, params))
    return sorted(api), sorted(modules)


def _berry_core(baselib_src, conf_src):
    """Berry's own builtins, plus the stdlib modules this build turns on."""
    names = []
    for line in baselib_src.splitlines():
        m = _REGFUNC.match(line)
        if m and not m.group(1).startswith("__"):
            names.append(m.group(1))
    for line in conf_src.splitlines():
        m = _CONF_MODULE.match(line)
        if m and m.group(2) != "0":
            names.append(m.group(1).lower())
    return sorted(set(names))


def extract(project_dir):
    """Returns {'api': [...], 'mods': [...], 'core': [...]}.

    Raises SystemExit when a source is missing or yields nothing -- see
    render_js for why that has to be fatal rather than a warning.
    """
    p = lambda *a: os.path.join(project_dir, *a)
    try:
        bindings = _read(p("src", "core", "script", "ScriptBindings.cpp"))
        prelude = _read(p("src", "core", "script", "Prelude.h"))
        baselib = _read(p("lib", "berry", "src", "be_baselib.c"))
        conf = _read(p("lib", "berry", "berry_conf.h"))
        layout = _read(p("src", "platform", "linux", "layout", "LayoutModule.h"))
        audio = _read(p("src", "platform", "tc002", "audio", "AudioScriptModule.h"))
    except OSError as e:
        raise SystemExit("berry api: cannot read a source: %s" % e)

    builtins = _device_builtins(bindings)
    prelude_api, mods = _prelude(prelude)
    linux_api, linux_mods = _prelude(layout)
    audio_api, _ = _prelude(audio, mods)
    linux_api = sorted(linux_api + audio_api)
    modbus_api = [
        "modbus.%s(host, address, count, callback, opts?)" % name
        for name in ("readHoldingRegisters", "readInputRegisters", "readCoils", "readDiscreteInputs")
    ] + ["modbus.int16(value)", "modbus.int32(high, low)", "modbus.float32(high, low)"]
    return {
        "api": sorted(builtins) + prelude_api + modbus_api,
        "mods": mods,
        "linux_api": linux_api,
        "linux_mods": linux_mods,
        "core": _berry_core(baselib, conf) + ["modbus"],
    }


def _js_array(names):
    return "[" + ",".join('"%s"' % n for n in names) + "]"


def render_js(project_dir):
    """The generated editor API block."""
    t = extract(project_dir)
    for key in ("api", "mods", "core"):
        if not t[key]:
            raise SystemExit(
                "berry api: extracted 0 entries for '%s'. The source moved or the "
                "pattern in scripts/berry_api.py no longer matches it." % key
            )
    return "\n".join(
        [
            "/* GENERATED by scripts/berry_api.py - DO NOT EDIT BY HAND.",
            "   Device API from ScriptBindings.cpp + Prelude.h; language builtins from",
            "   Berry's be_baselib.c and the modules berry_conf.h enables. Regenerated on",
            "   every build, so adding a binding is all it takes to teach the editor. */",
            "const BERRY_API=%s;" % _js_array(t["api"]),
            "const BERRY_MODS=%s;" % _js_array(t["mods"]),
            "const BERRY_CORE=%s;" % _js_array(t["core"]),
            "//linux:begin",
            "BERRY_API.push(...%s);" % _js_array(t["linux_api"]),
            "BERRY_MODS.push(...%s);" % _js_array(t["linux_mods"]),
            "//linux:end",
        ]
    )


BEGIN = "/* BERRY-API-START */"
END = "/* BERRY-API-END */"


def block(project_dir):
    """The marker-delimited region, exactly as it should appear in the file."""
    return BEGIN + "\n" + render_js(project_dir) + "\n" + END


def inject(project_dir, path=None):
    """Refreshes the generated source fragment; returns whether it changed."""
    path = path or os.path.join(project_dir, "webui", "src", "generated", "berry-api.js")
    with open(path, "r", encoding="utf-8", newline="") as f:
        html = f.read()
    start, end = html.find(BEGIN), html.find(END)
    if start < 0 or end < 0 or end < start:
        raise SystemExit(
            "berry api: the %s / %s markers are missing from %s" % (BEGIN, END, path)
        )
    updated = html[:start] + block(project_dir) + html[end + len(END):]
    if updated == html:
        return False
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(updated)
    return True


if __name__ == "__main__":
    import sys

    args = [a for a in sys.argv[1:] if a != "--inject"]
    root = args[0] if args else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if "--inject" in sys.argv:
        import webui_source

        webui_source.write(root)
        print("berry api: fragment and webui/index.html current")
    else:
        print(render_js(root))
