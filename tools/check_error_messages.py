#!/usr/bin/env python3
"""Check quoted API error messages against source literals.

Message columns, error/status tables, message-like inline quotes and JSON API
error examples are checked in published Markdown and OpenAPI. Dynamic parts
use <name> or an ellipsis. Fixed parts must occur in literals from one source
file. printf placeholders and quoted names also support concrete examples.
"""

from pathlib import Path
import argparse
import ast
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
EXTENSIONS = {".cpp", ".h", ".hpp", ".c", ".be", ".inc"}
GENERATED = {"WebUiAsset.h", "PreludeSolidified.h", "SpeechLexiconData.h"}
TOKEN = re.compile(r'''//[^\n]*|/\*[\s\S]*?\*/|'(?:\\.|[^'\\])'|R"([^ ()\\\t\r\n]*)\(([\s\S]*?)\)\1"|"(?:\\.|[^"\\])*"''')
INLINE = re.compile(r"`([^`]+)`")
JSON_MESSAGE = re.compile(r'"message"\s*:\s*("(?:\\.|[^"\\])*")')
MESSAGE_START = re.compile(r"^(?:must |no |not |invalid |unknown |needs |only |allowed:|at most |missing |cannot |too |out of |expected |body |set |name taken|write failed|storage full|busy,|nothing )")
PLACEHOLDER = re.compile(r'<[^>]+>|\.\.\.|…')
PRINTF = re.compile(r'%(?:[-+0 #]*\d*(?:\.\d+|\.\*)?(?:ll|l|z)?)[diuoxXsf]')
BERRY_EXCEPTION = re.compile(r"^[a-z]+_error: ")


def literals(text):
    """Read C/C++ strings, including escapes/raw strings, but never comments."""
    result = []
    for match in TOKEN.finditer(text):
        token = match.group()
        if token.startswith(('//', '/*', "'")):
            continue
        if token.startswith('R"'):
            result.append(match.group(2))
        else:
            try:
                result.append(ast.literal_eval(token))
            except (SyntaxError, ValueError):
                continue
    return result


def source_messages(root):
    return {path.relative_to(root).as_posix(): literals(path.read_text(encoding="utf-8"))
            for path in sorted((root / "src").rglob("*"))
            if path.suffix in EXTENSIONS and "vendor" not in path.parts
            and path.name not in GENERATED}


def berry_messages(root):
    return {path.relative_to(root).as_posix(): literals(path.read_text(encoding="utf-8"))
            for path in sorted((root / "lib/berry/src").glob("*"))
            if path.suffix in {".c", ".h"}}


def message_phrase(value):
    value = " ".join(value.split())
    if not re.search(r"[A-Za-z].* [A-Za-z0-9<]", value):
        return False
    return not (value.startswith(("/", "GET ", "POST ", "PUT ", "PATCH ", "DELETE ",
                                 "curl ", "field:", "Retry-After:", "Content-Type:",
                                 "Content-Encoding:", "ETag:", "Cache-Control:",
                                 "HTTP/", "{", "[", "\"", "'"))
                or re.search(r"\b(?:true|false|null)\b\s*[,|]", value))


def quoted_messages(text):
    """Yield (line, message) from the documented error-message conventions."""
    seen = set()
    for item in _quoted_messages(text):
        if item not in seen:
            seen.add(item)
            yield item


def _quoted_messages(text):
    for match in re.finditer(r'"error"\s*:\s*\{([^{}]+)\}', text):
        body = match.group(1)
        if '"code"' in body:
            for message in JSON_MESSAGE.finditer(body):
                yield text.count("\n", 0, match.start()) + 1, ast.literal_eval(message.group(1))
    lines = text.splitlines()
    prose = []
    fence = False
    for line in lines:
        if line.lstrip().startswith(("```", "~~~")):
            fence = not fence
            prose.append("")
        else:
            prose.append("" if fence else line)
    prose = "\n".join(prose)
    for match in INLINE.finditer(prose):
        value = " ".join(match.group(1).split())
        if MESSAGE_START.match(value) and message_phrase(value):
            yield prose.count("\n", 0, match.start()) + 1, value
    table_columns = []
    error_table = False
    fence = False
    for number, line in enumerate(lines, 1):
        if line.lstrip().startswith(("```", "~~~")):
            fence = not fence
            continue
        if fence:
            continue
        if line.startswith("|"):
            cells = re.split(r"(?<!\\)\|", line)[1:-1]
            if number < len(lines) and re.match(r"\|\s*:?-", lines[number]):
                table_columns = [i for i, cell in enumerate(cells) if "message" in cell.lower()]
                error_table = any(cell.strip().lower() in {"status", "code"} for cell in cells)
                continue
            selected = [cells[i] for i in table_columns if i < len(cells)] if table_columns else cells if error_table else []
            for cell in selected:
                for value in INLINE.findall(cell):
                    if message_phrase(value):
                        yield number, value
        else:
            table_columns = []
            error_table = False
            for match in re.finditer(r"\bmessage(?:\s+is|\s+reads|\s+says)?\s+`([^`]+)`", line):
                yield number, match.group(1)


def matches(message, sources):
    quoted_name = re.search(r' "[^"\n]+"$', message)
    if quoted_name and any(message[:quoted_name.start()] in strings for strings in sources.values()):
        return True
    location = re.search(r" (?:\(line \d+, column \d+\)|\(at offset \d+\))$", message)
    if location and location.start() > 0:
        return matches(message[:location.start()], sources) and matches(location.group(), sources)
    fixed = [part.strip() for part in PLACEHOLDER.split(message) if part.strip()]
    if not fixed:
        return False
    for strings in sources.values():
        if len(fixed) > 1 or PLACEHOLDER.search(message):
            if all(any(part in literal for literal in strings) for part in fixed):
                return True
        elif any(message in literal for literal in strings):
            return True
        # printf messages preserve their words while inserting numbers or names.
        for literal in strings:
            if PRINTF.search(literal) and len(re.sub(r"[^a-zA-Z]", "", PRINTF.sub("", literal))) >= 8:
                pattern = ".+?".join(re.escape(part) for part in PRINTF.split(literal))
                if re.fullmatch(pattern, message):
                    return True
        for literal in strings:
            if len(literal.strip()) >= 8 and message.startswith(literal):
                tail = message[len(literal):]
                number = re.match(r"\d+", tail)
                if number and tail[number.end():] in strings:
                    return True
    # Shared response helpers concatenate a fixed prefix with a caller's value.
    all_strings = {value for strings in sources.values() for value in strings}
    for value in all_strings:
        if len(value.strip()) >= 8 and message.startswith(value) and message[len(value):] in all_strings:
            return True
        if value.endswith(":") and len(value) >= 8 and message.startswith(value + " "):
            choices = message[len(value):].split()
            if choices and all(choice in all_strings for choice in choices):
                return True
    # Pin checks build their text from a field name, numbers and a reason kept with the chip.
    concrete = re.sub(r"\d+(?:(?:-|\.\.)\d+)?", "<n>", re.sub(r"^\w+: ", "<field>: ", message))
    if concrete != message:
        fixed = [part.strip() for part in PLACEHOLDER.split(concrete) if len(part.strip()) >= 3]
        if fixed and all(covered(part, all_strings) for part in fixed):
            return True
    return False


def covered(part, strings):
    """True when `part` is a run of source literals, joined by single spaces."""
    pieces = {value.strip() for value in strings if len(value.strip()) >= 3 and value.strip() in part}
    reach = {0}
    for start in range(len(part) + 1):
        if start not in reach:
            continue
        for piece in pieces:
            if part.startswith(piece, start):
                end = start + len(piece)
                reach.add(end)
                if end < len(part) and part[end] == " ":
                    reach.add(end + 1)
    return len(part) in reach or any(part in value for value in strings)


def check(root):
    sources = source_messages(root)
    berry = {**sources, **berry_messages(root)}
    count = 0
    failures = []
    paths = sorted((root / "docs").rglob("*.md")) + [root / "docs/api/openapi.yaml"]
    for path in paths:
        if any(part in {"developers", "releases", "examples"} for part in path.relative_to(root / "docs").parts):
            continue
        for number, message in quoted_messages(path.read_text(encoding="utf-8")):
            # Berry exception text comes from the dependency, not this API.
            if message.startswith(("syntax_error:", "runtime_error:")):
                continue
            count += 1
            # A Berry exception keeps the VM's own text after its kind.
            kind = BERRY_EXCEPTION.match(message)
            if matches(message, sources) or kind and matches(message[kind.end():], berry):
                continue
            failures.append(f"{path.relative_to(root).as_posix()}:{number}: no source literal for {message!r}")
    return count, failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args()
    count, failures = check(args.root)
    for failure in failures:
        print(failure, file=sys.stderr)
    print(f"Error messages: {count} checked, {len(failures)} unmatched")
    return bool(failures)


if __name__ == "__main__":
    sys.exit(main())
