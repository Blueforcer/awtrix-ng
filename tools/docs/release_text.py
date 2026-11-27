import html
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from only import MARKER, VARIANTS, filter_markdown  # noqa: E402

DOCS = "https://blueforcer.github.io/awtrix-ng"
HIGHLIGHT_SECTIONS = ("Breaking changes", "New", "For script developers", "Improved")
HEADING = re.compile(r"^#{2,4}\s+(.*?)\s*$")
BULLET = re.compile(r"^[*-]\s+(.*)$")
LINK = re.compile(r"\[([^\]]+)\]\([^)]*\)")
DOC_LINK = re.compile(r"\]\((?!https?:|#)([^)#]+?)\.md(#[^)]*)?\)")
LEAD = re.compile(r"^\*\*(.+?)\*\*(:?)\s*(.*)$")
SENTENCE_END = re.compile(r"(?<=[.!?])\s+(?=[A-Z*`\[])")
CLOCK_LINKS = (("esp32", "ESP32 (Ulanzi TC001, DIY)"), ("esp32-s3", "ESP32-S3"), ("tc002", "Ulanzi TC002"))


def bullets(markdown):
    found = []
    section = None
    current = None
    for line in markdown.splitlines():
        heading = HEADING.match(line)
        if heading:
            section = heading.group(1)
            current = None
            continue
        bullet = BULLET.match(line)
        if bullet and section:
            current = [section, bullet.group(1).strip()]
            found.append(current)
        elif current and line.startswith((" ", "\t")) and line.strip():
            current[1] += " " + line.strip()
        else:
            current = None
    return [(section, text) for section, text in found]


def parts(text):
    lead = LEAD.match(text)
    return (lead.group(1), lead.group(2), lead.group(3)) if lead else ("", "", text)


def entries(markdown, where):
    order = []
    texts = {}
    clocks = {}
    for variant in VARIANTS:
        for section, text in bullets(filter_markdown(markdown, variant, where)):
            key = (section, parts(text)[0] or text)
            if key not in clocks:
                clocks[key] = set()
                texts[key] = text
                order.append(key)
            clocks[key].add(variant)
    return [(key[0], texts[key], clocks[key]) for key in order]


def sections(markdown):
    names = []
    for line in MARKER.sub("", markdown).splitlines():
        heading = HEADING.match(line)
        if heading and heading.group(1) not in names:
            names.append(heading.group(1))
    return names


def label(clocks):
    if clocks == set(VARIANTS):
        return ""
    return ", ".join(VARIANTS[name] for name in VARIANTS if name in clocks)


def tagged(title, colon, rest, clocks):
    tag = label(clocks)
    suffix = " (%s)" % tag if tag else ""
    if not title:
        return "%s%s" % (rest, suffix)
    if colon or title.endswith(":"):
        return "**%s**%s: %s" % (title.rstrip(":"), suffix, rest)
    head = "**%s**%s" % (title, suffix)
    return "%s %s" % (head, rest) if rest else head


def highlight(text, clocks):
    title, colon, rest = parts(html.unescape(LINK.sub(r"\1", text)))
    rest = SENTENCE_END.split(rest.strip(), maxsplit=1)[0]
    if title.endswith((".", "!", "?")) and not colon:
        rest = ""
    return "• " + tagged(title, colon, rest, clocks)


def highlights(markdown, where="release notes"):
    found = [entry for entry in entries(markdown, where) if entry[0] in HIGHLIGHT_SECTIONS]
    if not found:
        return ""
    lines = ["**Highlights**"]
    for section in HIGHLIGHT_SECTIONS:
        for name, text, clocks in found:
            if name == section:
                entry = highlight(text, clocks)
                lines.append(entry.replace("• ", "• ⚠️ ", 1) if section == "Breaking changes" else entry)
    return "\n".join(lines)


def prefix(clocks):
    if clocks == set(VARIANTS):
        return ""
    return next(name for name in VARIANTS if name in clocks) + "/"


def absolute(text, clocks):
    def link(match):
        path = os.path.normpath(os.path.join("releases", match.group(1))).replace(os.sep, "/")
        path = path[:-len("index")] if path.endswith("index") else path + "/"
        return "](%s/%s%s%s)" % (DOCS, prefix(clocks), path, match.group(2) or "")
    return DOC_LINK.sub(link, text)


def intro(markdown):
    for line in MARKER.sub("", markdown).splitlines():
        if line.strip() and not line.startswith("#"):
            return line.strip()
    return ""


def links(version):
    pages = " ·\n".join("[%s](%s/%s/releases/%s/)" % (name, DOCS, variant, version) for variant, name in CLOCK_LINKS)
    return "**[Full release notes](%s/releases/%s/)** for your clock:\n%s" % (DOCS, version, pages)


def github(markdown, version, where="release notes"):
    found = entries(markdown, where)
    out = [intro(markdown)] if intro(markdown) else []
    for section in sections(markdown):
        items = [(text, clocks) for name, text, clocks in found if name == section]
        if not items:
            continue
        out.append("### " + section)
        out.append("\n".join("* " + tagged(*parts(absolute(text, clocks)), clocks) for text, clocks in items))
    out.append(links(version))
    return "\n\n".join(out)


def main(argv):
    usage = "usage: release_text.py highlights PAGE | github PAGE | links VERSION"
    if len(argv) != 3 or argv[1] not in ("highlights", "github", "links"):
        print(usage, file=sys.stderr)
        return 2
    if argv[1] == "links":
        text = links(argv[2])
    else:
        with open(argv[2], encoding="utf-8") as page:
            markdown = page.read()
        version = os.path.splitext(os.path.basename(argv[2]))[0]
        text = highlights(markdown, argv[2]) if argv[1] == "highlights" else github(markdown, version, argv[2])
    if text:
        sys.stdout.buffer.write((text + "\n").encode("utf-8"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
