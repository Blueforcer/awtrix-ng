import html
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from only import VARIANTS, filter_markdown  # noqa: E402

SECTIONS = ("Breaking changes", "New", "For script developers", "Improved")
HEADING = re.compile(r"^#{2,4}\s+(.*?)\s*$")
BULLET = re.compile(r"^[*-]\s+(.*)$")
LINK = re.compile(r"\[([^\]]+)\]\([^)]*\)")
LEAD = re.compile(r"^\*\*(.+?)\*\*(:?)\s*(.*)$")
SENTENCE_END = re.compile(r"(?<=[.!?])\s+(?=[A-Z*`\[])")


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
        if bullet and section in SECTIONS:
            current = [section, bullet.group(1).strip()]
            found.append(current)
        elif current and line.startswith((" ", "\t")) and line.strip():
            current[1] += " " + line.strip()
        else:
            current = None
    return [(section, text) for section, text in found]


def label(clocks):
    if clocks == set(VARIANTS):
        return ""
    return ", ".join(VARIANTS[name] for name in VARIANTS if name in clocks)


def parts(text):
    text = html.unescape(LINK.sub(r"\1", text))
    lead = LEAD.match(text)
    return (lead.group(1), lead.group(2), lead.group(3)) if lead else ("", "", text)


def line(text, clocks):
    title, colon, rest = parts(text)
    rest = SENTENCE_END.split(rest.strip(), maxsplit=1)[0]
    tag = label(clocks)
    suffix = " (%s)" % tag if tag else ""
    if not title:
        return "• %s%s" % (rest, suffix)
    if colon or title.endswith(":"):
        return "• **%s**%s: %s" % (title.rstrip(":"), suffix, rest)
    if title.endswith((".", "!", "?")):
        return "• **%s**%s" % (title, suffix)
    return "• **%s** %s%s" % (title, rest, suffix)


def highlights(markdown, where="release notes"):
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
    if not order:
        return ""
    lines = ["**Highlights**"]
    for section in SECTIONS:
        for key in order:
            if key[0] == section:
                entry = line(texts[key], clocks[key])
                lines.append(entry.replace("• ", "• ⚠️ ", 1) if section == "Breaking changes" else entry)
    return "\n".join(lines)


def main(argv):
    if len(argv) != 2:
        print("usage: release_highlights.py docs/releases/<version>.md", file=sys.stderr)
        return 2
    with open(argv[1], encoding="utf-8") as page:
        text = highlights(page.read(), argv[1])
    if text:
        sys.stdout.buffer.write((text + "\n").encode("utf-8"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
