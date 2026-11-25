#!/usr/bin/env python3
import argparse
import html
import posixpath
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SITE = ROOT / "site"
GITHUB = "https://github.com/MarlonXDev/NyrexVJ"
BLOB = GITHUB + "/blob/main/"
ALLOWED_HOSTS = {"github.com", "docs.github.com"}
MAX_MANUAL_BYTES = 1 << 20

PARTS = [
    ("start", ["welcome", "install", "tour", "first-show"]),
    ("play", ["clip-grid", "decks", "layers", "clip-window", "effects", "library", "presets", "master"]),
    ("music", ["audio", "modulation"]),
    ("content", ["video", "shaders", "models", "creator", "generators", "nodes", "live-sources"]),
    ("output", ["outputs", "recording"]),
    ("shows", ["control", "sync", "timeline", "stage", "plugins"]),
    ("reference", ["settings", "shortcuts", "performance", "files", "troubleshooting", "cli", "languages", "glossary"]),
]

TEXT = {
    "en": {
        "lang": "en",
        "dir": "manual",
        "all": "all.html",
        "site_prefix": "",
        "manual": "Manual",
        "title": "NyrexVJ Manual",
        "parts": {
            "start": "Getting started",
            "play": "Playing visuals",
            "music": "Music and motion",
            "content": "Your own content",
            "output": "Output and recording",
            "shows": "Live control and shows",
            "reference": "Reference and help",
        },
        "chapter_of": "Chapter {n} of {total}",
        "minutes": "{m} min read",
        "prev": "Previous",
        "next": "Next",
        "contents": "Contents",
        "on_page": "On this page",
        "single": "Read the whole manual on one page",
        "single_title": "The whole manual on one page",
        "single_hint": "Use the search of your browser (Ctrl+F) to find a word anywhere in the manual.",
        "in_program": "The same manual is built into the program, so you have it without internet. Open it with F1 or from the Help menu, then User manual.",
        "mistake": "Found a mistake or something missing?",
        "mistake_link": "Tell us on GitHub",
        "index_title": "The NyrexVJ manual",
        "index_lead": "Everything about NyrexVJ in one place: how to install it, how to play your first show, where every window and setting is, and what to do when something does not work.",
        "index_desc": "The complete NyrexVJ manual: installing, first show, clips, layers, effects, audio, outputs, MIDI and OSC, timeline, DMX and settings.",
        "start_here": "Start here",
        "quick": [("first-show", "Your first show in five minutes"), ("troubleshooting", "Something does not work"), ("shortcuts", "Keyboard and mouse"), ("outputs", "Send the picture to OBS and projectors")],
        "toc_open": "Contents",
        "skip": "Skip to content",
        "figure": "Picture",
        "tip": "Tip",
        "warning": "Warning",
        "note": "Note",
        "page_title": "{chapter} - NyrexVJ Manual",
        "page_desc": "{desc}",
        "issues": GITHUB + "/issues",
        "other_lang": "de",
        "all_title": "NyrexVJ Manual - the whole manual on one page",
        "all_desc": "The complete NyrexVJ manual on a single page, for searching with Ctrl+F and for printing.",
    },
    "de": {
        "lang": "de",
        "dir": "de/handbuch",
        "all": "alle.html",
        "site_prefix": "de/",
        "manual": "Handbuch",
        "title": "NyrexVJ-Handbuch",
        "parts": {
            "start": "Erste Schritte",
            "play": "Visuals spielen",
            "music": "Musik und Bewegung",
            "content": "Eigene Inhalte",
            "output": "Ausgabe und Aufnahme",
            "shows": "Live-Steuerung und Shows",
            "reference": "Nachschlagen und Hilfe",
        },
        "chapter_of": "Kapitel {n} von {total}",
        "minutes": "{m} Min. Lesezeit",
        "prev": "Zurück",
        "next": "Weiter",
        "contents": "Inhalt",
        "on_page": "Auf dieser Seite",
        "single": "Das ganze Handbuch auf einer Seite lesen",
        "single_title": "Das ganze Handbuch auf einer Seite",
        "single_hint": "Mit der Suche deines Browsers (Strg+F) findest du jedes Wort im ganzen Handbuch.",
        "in_program": "Dasselbe Handbuch steckt im Programm, du hast es also auch ohne Internet. Öffne es mit F1 oder über das Menü Hilfe, Handbuch.",
        "mistake": "Fehler gefunden oder etwas vergessen?",
        "mistake_link": "Sag es uns auf GitHub",
        "index_title": "Das NyrexVJ-Handbuch",
        "index_lead": "Alles über NyrexVJ an einem Ort: wie du es installierst, wie du deine erste Show spielst, wo jedes Fenster und jede Einstellung liegt und was du tust, wenn etwas nicht klappt.",
        "index_desc": "Das komplette NyrexVJ-Handbuch: Installation, erste Show, Clips, Layer, Effekte, Audio, Ausgänge, MIDI und OSC, Zeitleiste, DMX und Einstellungen.",
        "start_here": "Hier anfangen",
        "quick": [("first-show", "Deine erste Show in fünf Minuten"), ("troubleshooting", "Etwas funktioniert nicht"), ("shortcuts", "Tastatur und Maus"), ("outputs", "Das Bild an OBS und Projektoren schicken")],
        "toc_open": "Inhalt",
        "skip": "Zum Inhalt springen",
        "figure": "Bild",
        "tip": "Tipp",
        "warning": "Warnung",
        "note": "Hinweis",
        "page_title": "{chapter} - NyrexVJ-Handbuch",
        "page_desc": "{desc}",
        "issues": GITHUB + "/issues",
        "other_lang": "en",
        "all_title": "NyrexVJ-Handbuch - das ganze Handbuch auf einer Seite",
        "all_desc": "Das komplette NyrexVJ-Handbuch auf einer Seite, zum Suchen mit Strg+F und zum Drucken.",
    },
}

CHROME = {
    "en": {
        "nav": [("index", "Overview"), ("manual", "Manual"), ("download", "Download"), ("docs", "Docs")],
        "changelog": "Changelog",
        "github": "GitHub",
        "switch": "Deutsch",
        "nav_label": "Navigation",
        "cols": [
            ("Software", [("index", "Overview"), ("download", "Download"), ("changelog", "Changelog"), ("releases", "Releases")]),
            ("Learn", [("manual", "Manual"), ("manual/first-show.html", "Your first show"), ("manual/troubleshooting.html", "Troubleshooting"), ("manual/shortcuts.html", "Keyboard and mouse")]),
            ("Project", [("github", "GitHub"), ("issues", "Issues"), ("roadmap", "Roadmap"), ("contributing", "Contributing")]),
            ("About", [("privacy", "Privacy"), ("license", "MIT licence"), ("docs", "All documents")]),
        ],
        "fine": "NyrexVJ is an independent open source project. It is not affiliated with, endorsed by or sponsored by Resolume. Resolume, Arena and Avenue are names of their respective owner.",
    },
    "de": {
        "nav": [("index", "Überblick"), ("manual", "Handbuch"), ("download", "Download"), ("docs", "Doku")],
        "changelog": "Änderungen",
        "github": "GitHub",
        "switch": "English",
        "nav_label": "Navigation",
        "cols": [
            ("Software", [("index", "Überblick"), ("download", "Download"), ("changelog", "Änderungen"), ("releases", "Releases")]),
            ("Lernen", [("manual", "Handbuch"), ("manual/first-show.html", "Deine erste Show"), ("manual/troubleshooting.html", "Fehlersuche"), ("manual/shortcuts.html", "Tastatur und Maus")]),
            ("Projekt", [("github", "GitHub"), ("issues", "Issues"), ("roadmap", "Roadmap"), ("contributing", "Mitmachen")]),
            ("Über", [("privacy", "Datenschutz"), ("license", "MIT-Lizenz"), ("docs", "Alle Dokumente")]),
        ],
        "fine": "NyrexVJ ist ein unabhängiges Open-Source-Projekt. Es steht in keiner Verbindung zu Resolume und wird von Resolume weder unterstützt noch gefördert. Resolume, Arena und Avenue sind Namen ihres jeweiligen Inhabers.",
    },
}

EXTERNAL = {
    "changelog": BLOB + "CHANGELOG.md",
    "releases": GITHUB + "/releases",
    "github": GITHUB,
    "issues": GITHUB + "/issues",
    "roadmap": BLOB + "docs/ROADMAP.md",
    "contributing": BLOB + "CONTRIBUTING.md",
    "license": BLOB + "LICENSE",
}


class Span:
    def __init__(self, text, style="n", target=""):
        self.text = text
        self.style = style
        self.target = target


class Block:
    def __init__(self, kind):
        self.kind = kind
        self.level = 0
        self.id = ""
        self.items = []
        self.columns = 0
        self.text = ""
        self.alt = ""
        self.note = "note"


class Chapter:
    def __init__(self, title, ident):
        self.title = title
        self.id = ident
        self.blocks = []


def slugify(title):
    slug = []
    pending = False
    for ch in title:
        add = None
        if ch.isascii() and ch.isalnum():
            add = ch.lower()
        elif ch in "äÄ":
            add = "ae"
        elif ch in "öÖ":
            add = "oe"
        elif ch in "üÜ":
            add = "ue"
        elif ch == "ß":
            add = "ss"
        elif "À" <= ch <= "ÿ":
            pending = bool(slug)
            continue
        elif ord(ch) >= 0x80:
            continue
        else:
            pending = bool(slug)
            continue
        if pending:
            slug.append("-")
            pending = False
        slug.append(add)
    return "".join(slug)


def append_span(out, text, style, target=""):
    if not text:
        return
    if out and out[-1].style == style and style != "l":
        out[-1].text += text
        return
    out.append(Span(text, style, target))


def parse_inline(text):
    out = []
    plain = []

    def flush():
        append_span(out, "".join(plain), "n")
        plain.clear()

    i = 0
    while i < len(text):
        c = text[i]
        if c == "\\" and i + 1 < len(text):
            plain.append(text[i + 1])
            i += 2
        elif c == "`":
            close = text.find("`", i + 1)
            if close < 0:
                plain.append(c)
                i += 1
            else:
                flush()
                append_span(out, text[i + 1:close], "c")
                i = close + 1
        elif c == "*" and text[i + 1:i + 2] == "*":
            close = text.find("**", i + 2)
            if close < 0 or close == i + 2:
                plain.append(c)
                i += 1
            else:
                flush()
                append_span(out, text[i + 2:close], "b")
                i = close + 2
        elif c == "[":
            mid = text.find("](", i + 1)
            end = -1 if mid < 0 else text.find(")", mid + 2)
            if mid < 0 or end < 0 or mid == i + 1:
                plain.append(c)
                i += 1
            else:
                flush()
                out.append(Span(text[i + 1:mid], "l", text[mid + 2:end]))
                i = end + 1
        else:
            plain.append(c)
            i += 1
    flush()
    return out


def plain_text(spans):
    return "".join(s.text for s in spans)


def split_cells(line):
    cells = []
    current = []
    body = line.strip()
    if body.startswith("|"):
        body = body[1:]
    escaped = False
    for c in body:
        if escaped:
            current.append(c)
            escaped = False
        elif c == "\\":
            current.append(c)
            escaped = True
        elif c == "|":
            cells.append("".join(current).strip())
            current = []
        else:
            current.append(c)
    if "".join(current).strip():
        cells.append("".join(current).strip())
    return cells


def is_separator_row(cells):
    if not cells:
        return False
    for cell in cells:
        if not cell:
            return False
        if any(c not in "-: " for c in cell):
            return False
    return True


def numbered_start(line):
    i = 0
    while i < len(line) and line[i] in "0123456789":
        i += 1
    if i == 0 or i > 3 or i + 1 >= len(line) or line[i] != "." or line[i + 1] != " ":
        return -1
    return i + 2


def figure_line(line):
    if not line.startswith("!["):
        return None
    close = line.find("](")
    if close < 0 or not line.endswith(")"):
        return None
    alt = line[2:close]
    path = line[close + 2:-1]
    if not path:
        return None
    return alt, path


def heading_id(title):
    title = title.strip()
    open_at = title.rfind("{#")
    if open_at >= 0 and title and title.endswith("}"):
        ident = title[open_at + 2:-1].strip()
        return title[:open_at].strip(), ident
    return title, ""


def parse_manual(markdown):
    source = markdown.encode("utf-8")[:MAX_MANUAL_BYTES].decode("utf-8", errors="ignore")
    lines = [line.replace("\t", " ").rstrip() for line in source.split("\n")]
    chapters = []
    state = {"chapter": None, "paragraph": "", "items": [], "kind": "bullets", "in_list": False}
    note_lines = []
    table_lines = []

    def add(block):
        if state["chapter"] is not None:
            state["chapter"].blocks.append(block)

    def flush_paragraph():
        if state["paragraph"] and state["chapter"] is not None:
            block = Block("paragraph")
            block.items.append(parse_inline(state["paragraph"]))
            add(block)
        state["paragraph"] = ""

    def flush_list():
        if state["in_list"] and state["chapter"] is not None and state["items"]:
            block = Block(state["kind"])
            for item in state["items"]:
                block.items.append(parse_inline(item))
            add(block)
        state["items"] = []
        state["in_list"] = False

    def flush_all():
        flush_paragraph()
        flush_list()

    def flush_note():
        if not note_lines:
            return
        if state["chapter"] is not None:
            block = Block("note")
            joined = " ".join(note_lines)
            for prefixes, kind in ((("Tip:", "Tipp:"), "tip"), (("Warning:", "Warnung:"), "warning"), (("Note:", "Hinweis:"), "note")):
                if joined.startswith(prefixes):
                    block.note = kind
                    joined = joined[joined.find(":") + 1:].strip()
                    break
            block.items.append(parse_inline(joined))
            add(block)
        note_lines.clear()

    def flush_table():
        if not table_lines:
            return
        if state["chapter"] is not None:
            block = Block("table")
            rows = []
            for row in table_lines:
                cells = split_cells(row)
                if not is_separator_row(cells):
                    rows.append(cells)
            columns = max((len(r) for r in rows), default=0)
            block.columns = columns
            for row in rows:
                row = row + [""] * (columns - len(row))
                for cell in row:
                    block.items.append(parse_inline(cell))
            if rows and columns > 0:
                add(block)
        table_lines.clear()

    def flush_others():
        flush_all()
        flush_note()
        flush_table()

    in_code = False
    in_comment = False
    code = None
    for line in lines:
        if in_code:
            if line.lstrip().startswith("```"):
                in_code = False
                if state["chapter"] is not None:
                    if code.text.endswith("\n"):
                        code.text = code.text[:-1]
                    add(code)
                code = None
            else:
                code.text += line + "\n"
            continue
        if in_comment:
            if "-->" in line:
                in_comment = False
            continue
        trimmed = line.lstrip()
        if trimmed.startswith("<!--"):
            if "-->" not in trimmed:
                in_comment = True
            continue
        if trimmed.startswith("```"):
            flush_others()
            in_code = True
            code = Block("code")
            continue
        if not trimmed:
            flush_others()
            continue
        if table_lines and not trimmed.startswith("|"):
            flush_table()
        if note_lines and not trimmed.startswith(">"):
            flush_note()
        if trimmed.startswith("#"):
            level = 0
            while level < len(trimmed) and trimmed[level] == "#":
                level += 1
            if level <= 3 and level < len(trimmed) and trimmed[level] == " ":
                flush_others()
                title, ident = heading_id(trimmed[level + 1:])
                if level == 1:
                    flush_all()
                    chapter = Chapter(title, ident or slugify(title))
                    chapters.append(chapter)
                    state["chapter"] = chapter
                elif state["chapter"] is not None:
                    block = Block("subheading")
                    block.level = level
                    block.id = ident or slugify(title)
                    block.items.append(parse_inline(title))
                    add(block)
                continue
        if trimmed.startswith("|"):
            flush_all()
            flush_note()
            table_lines.append(trimmed)
            continue
        if trimmed.startswith(">"):
            flush_all()
            note_lines.append(trimmed[1:].strip())
            continue
        figure = figure_line(trimmed)
        if figure:
            flush_others()
            block = Block("figure")
            block.alt, block.text = figure
            add(block)
            continue
        flat = len(line) == len(trimmed)
        bullet = (trimmed.startswith("- ") or trimmed.startswith("* ")) and flat
        content_start = numbered_start(trimmed) if flat else -1
        numbered = content_start >= 0
        if bullet or numbered:
            flush_paragraph()
            kind = "bullets" if bullet else "numbered"
            if state["in_list"] and state["kind"] != kind:
                flush_list()
            state["in_list"] = True
            state["kind"] = kind
            state["items"].append((trimmed[2:] if bullet else trimmed[content_start:]).strip())
            continue
        if state["in_list"] and not flat and state["items"]:
            state["items"][-1] += " " + trimmed
            continue
        flush_list()
        if state["paragraph"]:
            state["paragraph"] += " "
        state["paragraph"] += trimmed
    flush_others()
    return chapters


def outline(chapters):
    out = []
    for chapter in chapters:
        out.append(f"C {chapter.id}|{chapter.title}")
        for block in chapter.blocks:
            spans = sum(len(item) for item in block.items)
            links = sum(1 for item in block.items for s in item if s.style == "l")
            if block.kind == "subheading":
                out.append(f"S{block.level} {block.id}|{plain_text(block.items[0]) if block.items else ''}")
            elif block.kind == "paragraph":
                out.append(f"B paragraph spans={spans} links={links}")
            elif block.kind in ("bullets", "numbered"):
                out.append(f"B {block.kind} items={len(block.items)} spans={spans} links={links}")
            elif block.kind == "table":
                out.append(f"B table cells={len(block.items)} columns={block.columns} spans={spans} links={links}")
            elif block.kind == "code":
                out.append(f"B code lines={block.text.count(chr(10)) + 1}")
            elif block.kind == "note":
                out.append(f"B note {block.note} spans={spans} links={links}")
            elif block.kind == "figure":
                out.append(f"B figure {block.text}|{block.alt}")
    return "\n".join(out) + "\n"


def word_count(chapter):
    words = 0
    for block in chapter.blocks:
        if block.kind == "code":
            words += len(block.text.split())
        else:
            for item in block.items:
                words += len(plain_text(item).split())
    return words


def webp_size(path):
    data = Path(path).read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WEBP":
        raise ValueError(f"{path}: not a WebP file")
    kind = data[12:16]
    if kind == b"VP8X":
        width = 1 + int.from_bytes(data[24:27], "little")
        height = 1 + int.from_bytes(data[27:30], "little")
    elif kind == b"VP8 ":
        width, height = struct.unpack("<HH", data[26:30])
        width &= 0x3FFF
        height &= 0x3FFF
    elif kind == b"VP8L":
        bits = int.from_bytes(data[21:25], "little")
        width = (bits & 0x3FFF) + 1
        height = ((bits >> 14) & 0x3FFF) + 1
    else:
        raise ValueError(f"{path}: unknown WebP chunk")
    return width, height


class Problems(list):
    def add(self, text):
        self.append(text)


class Context:
    def __init__(self, lang, chapters, problems):
        self.lang = lang
        self.text = TEXT[lang]
        self.chapters = chapters
        self.ids = {c.id: c for c in chapters}
        self.problems = problems
        self.mode = "page"
        self.current = ""
        self.part_of = {}
        for part, ids in PARTS:
            for ident in ids:
                self.part_of[ident] = part

    def problem(self, text):
        self.problems.add(f"{self.lang}: {self.current}: {text}")

    def href(self, target):
        if target.startswith("#"):
            body = target[1:]
            chapter_id, _, section = body.partition("/")
            chapter = self.ids.get(chapter_id)
            if chapter is None:
                self.problem(f"link to unknown chapter '{target}'")
                return "#"
            if section:
                sections = {b.id for b in chapter.blocks if b.kind == "subheading"}
                if section not in sections:
                    self.problem(f"link to unknown section '{target}'")
                    return "#"
            if self.mode == "all":
                return f"#{chapter_id}--{section}" if section else f"#{chapter_id}"
            return f"{chapter_id}.html" + (f"#{section}" if section else "")
        host = re.match(r"https://([^/]+)/", target + "/")
        if not target.startswith("https://github.com/") or not host or host.group(1) not in ALLOWED_HOSTS:
            self.problem(f"external link not allowed '{target}'")
        return target


DOC_REF = re.compile(r"(docs/[A-Z0-9_]+\.md)")


def autolink(text, ctx):
    out = []
    for index, piece in enumerate(DOC_REF.split(text)):
        if index % 2 == 1:
            if not (ROOT / piece).is_file():
                ctx.problem(f"document '{piece}' does not exist")
            out.append(f'<a href="{BLOB}{piece}" target="_blank" rel="noopener noreferrer">{html.escape(piece)}</a>')
        else:
            out.append(html.escape(piece, quote=False))
    return "".join(out)


def inline_html(spans, ctx):
    parts = []
    for span in spans:
        if span.style == "c":
            parts.append(f"<code>{html.escape(span.text, quote=False)}</code>")
        elif span.style == "b":
            parts.append(f"<strong>{autolink(span.text, ctx)}</strong>")
        elif span.style == "l":
            target = ctx.href(span.target)
            label = html.escape(span.text, quote=False)
            if target.startswith("https://"):
                parts.append(f'<a href="{html.escape(target)}" target="_blank" rel="noopener noreferrer">{label}</a>')
            else:
                parts.append(f'<a href="{html.escape(target)}">{label}</a>')
        else:
            parts.append(autolink(span.text, ctx))
    return "".join(parts)


def anchor_id(ctx, chapter, section):
    return f"{chapter.id}--{section}" if ctx.mode == "all" else section


def figure_html(block, ctx, chapter, prefix, images):
    name = block.text
    if not re.fullmatch(r"[a-z0-9-]+\.webp", name):
        ctx.problem(f"figure file name '{name}' must be lower case letters, digits and dashes ending in .webp")
        return ""
    rel = f"assets/manual/{ctx.lang}/{name}"
    path = SITE / rel
    images.add(rel)
    if not path.is_file():
        ctx.problem(f"figure '{rel}' is missing")
        return ""
    width, height = webp_size(path)
    alt = html.escape(block.alt)
    src = prefix + rel
    return (
        f'<figure class="shot"><a href="{src}"><img src="{src}" width="{width}" height="{height}" loading="lazy" alt="{alt}"></a>'
        f"<figcaption>{alt}</figcaption></figure>"
    )


def blocks_html(chapter, ctx, prefix, images, shift):
    out = []
    for block in chapter.blocks:
        if block.kind == "subheading":
            level = min(block.level + shift, 5)
            ident = anchor_id(ctx, chapter, block.id)
            out.append(f'<h{level} id="{ident}"><a class="anchor" href="#{ident}" aria-label="Link">#</a>{inline_html(block.items[0], ctx)}</h{level}>')
        elif block.kind == "paragraph":
            out.append(f"<p>{inline_html(block.items[0], ctx)}</p>")
        elif block.kind in ("bullets", "numbered"):
            tag = "ul" if block.kind == "bullets" else "ol"
            items = "".join(f"<li>{inline_html(item, ctx)}</li>" for item in block.items)
            out.append(f"<{tag}>{items}</{tag}>")
        elif block.kind == "table":
            rows = [block.items[i:i + block.columns] for i in range(0, len(block.items), block.columns)]
            head = "".join(f"<th scope=\"col\">{inline_html(cell, ctx)}</th>" for cell in rows[0])
            body = "".join("<tr>" + "".join(f"<td>{inline_html(cell, ctx)}</td>" for cell in row) + "</tr>" for row in rows[1:])
            out.append(f'<div class="tablewrap"><table><thead><tr>{head}</tr></thead><tbody>{body}</tbody></table></div>')
        elif block.kind == "code":
            out.append(f"<pre><code>{html.escape(block.text, quote=False)}</code></pre>")
        elif block.kind == "note":
            label = ctx.text[block.note]
            out.append(f'<aside class="note {block.note}"><strong class="note-label">{label}</strong><p>{inline_html(block.items[0], ctx)}</p></aside>')
        elif block.kind == "figure":
            out.append(figure_html(block, ctx, chapter, prefix, images))
    return "\n".join(part for part in out if part)


def rel_url(from_file, to_file):
    return posixpath.relpath(to_file, posixpath.dirname(from_file) or ".")


def root_prefix(file):
    return "../" * file.count("/")


def head_html(lang, title, desc, from_file, alternate):
    prefix = rel_url(from_file, "assets") + "/"
    other = "de" if lang == "en" else "en"
    parts = [
        '<!DOCTYPE html>',
        f'<html lang="{lang}">',
        "<head>",
        '<meta charset="utf-8">',
        '<meta name="viewport" content="width=device-width, initial-scale=1">',
        f"<title>{html.escape(title)}</title>",
        f'<meta name="description" content="{html.escape(desc)}">',
        '<meta name="referrer" content="no-referrer">',
        '<meta name="color-scheme" content="dark">',
        '<meta name="theme-color" content="#111214">',
        f'<link rel="icon" type="image/png" href="{prefix}icon.png">',
    ]
    if alternate:
        parts.append(f'<link rel="alternate" hreflang="{other}" href="{rel_url(from_file, alternate)}">')
    parts.append(f'<link rel="stylesheet" href="{prefix}style.css">')
    parts.append("</head>")
    return "\n".join(parts)


def page_path(lang, key):
    base = "" if lang == "en" else "de/"
    if key == "index":
        return base + "index.html"
    if key == "manual":
        return base + ("manual/index.html" if lang == "en" else "handbuch/index.html")
    if key in ("download", "docs", "privacy"):
        return base + key + ".html"
    raise KeyError(key)


def chrome_header(lang, current_file, active):
    c = CHROME[lang]
    items = []
    for key, label in c["nav"]:
        href = rel_url(current_file, page_path(lang, key))
        mark = ' aria-current="page"' if key == active else ""
        items.append(f'<a href="{href}"{mark}>{label}</a>')
    items.append(f'<a href="{EXTERNAL["changelog"]}" target="_blank" rel="noopener noreferrer">{c["changelog"]}</a>')
    items.append(f'<a href="{EXTERNAL["github"]}" target="_blank" rel="noopener noreferrer">{c["github"]}</a>')
    other = "de" if lang == "en" else "en"
    switch = current_switch(lang, current_file)
    items.append(f'<a class="lang" href="{rel_url(current_file, switch)}" hreflang="{other}" lang="{other}">{c["switch"]}</a>')
    home = rel_url(current_file, page_path(lang, "index"))
    icon = rel_url(current_file, "assets/icon.png")
    return (
        '<header class="top">\n<div class="top-in">\n'
        f'<a class="brand" href="{home}" aria-label="NyrexVJ"><img src="{icon}" alt="" width="34" height="34"><span>NyrexVJ</span></a>\n'
        f'<nav class="nav" aria-label="{c["nav_label"]}">\n' + "\n".join(items) + "\n</nav>\n</div>\n</header>"
    )


SWITCH_OVERRIDE = {}


def current_switch(lang, current_file):
    if current_file in SWITCH_OVERRIDE:
        return SWITCH_OVERRIDE[current_file]
    if lang == "en":
        return "de/" + current_file
    return current_file[3:]


def chrome_footer(lang, current_file):
    c = CHROME[lang]
    cols = []
    for title, links in c["cols"]:
        lis = []
        for key, label in links:
            if key in EXTERNAL:
                lis.append(f'<li><a href="{EXTERNAL[key]}" target="_blank" rel="noopener noreferrer">{label}</a></li>')
            elif key.startswith("manual/"):
                name = key.split("/", 1)[1]
                target = ("manual/" if lang == "en" else "de/handbuch/") + name
                lis.append(f'<li><a href="{rel_url(current_file, target)}">{label}</a></li>')
            else:
                lis.append(f'<li><a href="{rel_url(current_file, page_path(lang, key))}">{label}</a></li>')
        cols.append(f"<div><h4>{title}</h4><ul>" + "".join(lis) + "</ul></div>")
    return '<footer class="footer">\n<div class="wrap">\n<div class="fcols">\n' + "\n".join(cols) + f'\n</div>\n<p class="fine">{c["fine"]}</p>\n</div>\n</footer>'


def chapter_desc(chapter, limit=170):
    for block in chapter.blocks:
        if block.kind == "paragraph":
            text = plain_text(block.items[0])
            if len(text) <= limit:
                return text
            cut = text[:limit].rsplit(" ", 1)[0].rstrip(",;:")
            return cut + "..."
        if block.kind != "subheading":
            break
    return chapter.title


def toc_html(ctx, current_file, chapter, mode):
    t = ctx.text
    out = []
    for part, ids in PARTS:
        out.append(f"<h4>{html.escape(t['parts'][part])}</h4><ul>")
        for ident in ids:
            item = ctx.ids[ident]
            if mode == "all":
                href = f"#{ident}"
            else:
                href = f"{ident}.html"
            current = chapter is not None and item.id == chapter.id
            mark = ' aria-current="page"' if current else ""
            out.append(f'<li><a href="{href}"{mark}>{html.escape(item.title)}</a>')
            if current and mode == "page":
                subs = [b for b in item.blocks if b.kind == "subheading"]
                if subs:
                    out.append("<ul>" + "".join(f'<li><a href="#{b.id}">{inline_html(b.items[0], ctx)}</a></li>' for b in subs) + "</ul>")
            out.append("</li>")
        out.append("</ul>")
    return "\n".join(out)


def chapter_page(ctx, chapter, index, images):
    t = ctx.text
    lang = ctx.lang
    ctx.mode = "page"
    ctx.current = chapter.id
    current_file = f"{t['dir']}/{chapter.id}.html"
    alternate = f"{TEXT[t['other_lang']]['dir']}/{chapter.id}.html"
    SWITCH_OVERRIDE[current_file] = alternate
    prefix = root_prefix(current_file)
    body = blocks_html(chapter, ctx, prefix, images, 0)
    subs = [b for b in chapter.blocks if b.kind == "subheading" and b.level == 2]
    onpage = ""
    if len(subs) >= 2:
        onpage = f'<nav class="onpage" aria-label="{t["on_page"]}"><span>{t["on_page"]}</span>' + "".join(f'<a href="#{b.id}">{inline_html(b.items[0], ctx)}</a>' for b in subs) + "</nav>"
    minutes = max(1, round(word_count(chapter) / 200))
    part = ctx.part_of[chapter.id]
    previous = ctx.chapters[index - 1] if index > 0 else None
    following = ctx.chapters[index + 1] if index + 1 < len(ctx.chapters) else None
    pager = '<nav class="pager" aria-label="Pager">'
    pager += f'<a class="prev" href="{previous.id}.html"><small>{t["prev"]}</small><span>{html.escape(previous.title)}</span></a>' if previous else "<span></span>"
    pager += f'<a class="next" href="{following.id}.html"><small>{t["next"]}</small><span>{html.escape(following.title)}</span></a>' if following else "<span></span>"
    pager += "</nav>"
    mobile_toc = f'<details class="toc-m"><summary>{t["contents"]}</summary>{toc_html(ctx, current_file, chapter, "page")}</details>'
    page = [
        head_html(lang, t["page_title"].format(chapter=chapter.title), chapter_desc(chapter), current_file, alternate),
        "<body>",
        f'<a class="skip" href="#main">{t["skip"]}</a>',
        chrome_header(lang, current_file, "manual"),
        '<div class="manual">',
        f'<aside class="toc" aria-label="{t["contents"]}">\n{toc_html(ctx, current_file, chapter, "page")}\n<p class="alllink"><a href="{t["all"]}">{t["single"]}</a></p>\n</aside>',
        '<main id="main" class="doc">',
        mobile_toc,
        f'<p class="crumb"><a href="index.html">{t["manual"]}</a> / {html.escape(t["parts"][part])}</p>',
        f"<h1>{html.escape(chapter.title)}</h1>",
        f'<p class="meta">{t["chapter_of"].format(n=index + 1, total=len(ctx.chapters))} &middot; {t["minutes"].format(m=minutes)}</p>',
        onpage,
        body,
        pager,
        f'<p class="small help">{t["mistake"]} <a href="{t["issues"]}" target="_blank" rel="noopener noreferrer">{t["mistake_link"]}</a>. {t["in_program"]}</p>',
        "</main>",
        "</div>",
        chrome_footer(lang, current_file),
        "</body>",
        "</html>",
    ]
    return current_file, "\n".join(p for p in page if p) + "\n"


def index_page(ctx):
    t = ctx.text
    lang = ctx.lang
    ctx.mode = "page"
    ctx.current = "index"
    current_file = f"{t['dir']}/index.html"
    alternate = f"{TEXT[t['other_lang']]['dir']}/index.html"
    SWITCH_OVERRIDE[current_file] = alternate
    quick = "".join(f'<a class="btn alt" href="{ident}.html">{html.escape(label)}</a>' for ident, label in t["quick"])
    for ident, _ in t["quick"]:
        if ident not in ctx.ids:
            ctx.problem(f"quick link to unknown chapter '{ident}'")
    number = 0
    sections = []
    for part, ids in PARTS:
        cards = []
        for ident in ids:
            number += 1
            chapter = ctx.ids[ident]
            cards.append(
                f'<li><a href="{ident}.html"><span class="num">{number}</span><span class="ct">{html.escape(chapter.title)}</span>'
                f'<span class="cd">{html.escape(chapter_desc(chapter, 130))}</span></a></li>'
            )
        sections.append(f'<section class="part"><h2>{html.escape(t["parts"][part])}</h2><ul class="chapters">' + "".join(cards) + "</ul></section>")
    page = [
        head_html(lang, t["title"] + " - NyrexVJ", t["index_desc"], current_file, alternate),
        "<body>",
        f'<a class="skip" href="#main">{t["skip"]}</a>',
        chrome_header(lang, current_file, "manual"),
        '<main id="main">',
        '<section class="sec manual-hero"><div class="wrap">',
        f'<h1>{t["index_title"]}</h1>',
        f'<p class="lead">{t["index_lead"]}</p>',
        f'<p class="small">{t["in_program"]}</p>',
        f'<h2 class="mini">{t["start_here"]}</h2>',
        f'<div class="actions">{quick}</div>',
        f'<p><a href="{t["all"]}">{t["single"]} &rarr;</a></p>',
        "</div></section>",
        '<section class="sec s2"><div class="wrap">',
        "\n".join(sections),
        "</div></section>",
        "</main>",
        chrome_footer(lang, current_file),
        "</body>",
        "</html>",
    ]
    return current_file, "\n".join(page) + "\n"


def all_page(ctx, images):
    t = ctx.text
    lang = ctx.lang
    ctx.mode = "all"
    current_file = f"{t['dir']}/{t['all']}"
    alternate = f"{TEXT[t['other_lang']]['dir']}/{TEXT[t['other_lang']]['all']}"
    SWITCH_OVERRIDE[current_file] = alternate
    prefix = root_prefix(current_file)
    sections = []
    for index, chapter in enumerate(ctx.chapters):
        ctx.current = chapter.id
        body = blocks_html(chapter, ctx, prefix, images, 1)
        sections.append(
            f'<section class="chapter" id="{chapter.id}"><h2>{index + 1}. {html.escape(chapter.title)}</h2>\n{body}\n'
            f'<p class="small"><a href="#top">&uarr; {t["contents"]}</a></p></section>'
        )
    ctx.current = "all"
    toc = toc_html(ctx, current_file, None, "all")
    page = [
        head_html(lang, t["all_title"], t["all_desc"], current_file, alternate),
        "<body>",
        f'<a class="skip" href="#main">{t["skip"]}</a>',
        chrome_header(lang, current_file, "manual"),
        '<div class="manual all">',
        f'<aside class="toc" aria-label="{t["contents"]}">\n{toc}\n<p class="alllink"><a href="index.html">{t["manual"]}</a></p>\n</aside>',
        '<main id="main" class="doc">',
        '<span id="top"></span>',
        f'<p class="crumb"><a href="index.html">{t["manual"]}</a></p>',
        f'<h1>{html.escape(t["single_title"])}</h1>',
        f'<p class="lead">{t["single_hint"]}</p>',
        f'<details class="toc-m" open><summary>{t["contents"]}</summary>{toc}</details>',
        "\n".join(sections),
        f'<p class="small help">{t["mistake"]} <a href="{t["issues"]}" target="_blank" rel="noopener noreferrer">{t["mistake_link"]}</a>.</p>',
        "</main>",
        "</div>",
        chrome_footer(lang, current_file),
        "</body>",
        "</html>",
    ]
    return current_file, "\n".join(page) + "\n"


def load_manual(lang):
    name = "manual.en.md" if lang == "en" else "manual.de.md"
    return parse_manual((ROOT / "docs" / "manual" / name).read_text(encoding="utf-8"))


def check_manual_structure(manuals, problems):
    en = {c.id for c in manuals["en"]}
    placed = [ident for _, ids in PARTS for ident in ids]
    for ident in placed:
        if ident not in en:
            problems.add(f"part list names chapter '{ident}' that does not exist")
    if len(placed) != len(set(placed)):
        problems.add("a chapter is listed in two parts")
    for ident in en:
        if ident not in placed:
            problems.add(f"chapter '{ident}' is not in any part (add it to PARTS)")
    en_chapters = manuals["en"]
    de_chapters = manuals["de"]
    if [c.id for c in en_chapters] != [c.id for c in de_chapters]:
        problems.add("English and German manual have different chapters or a different order")
        return
    flat = [ident for _, ids in PARTS for ident in ids]
    if flat != [c.id for c in en_chapters]:
        problems.add("the order of PARTS differs from the order of the chapters in the manual")
    for chapter in en_chapters + de_chapters:
        body = [x for x in chapter.blocks if x.kind != "subheading"]
        if not body or body[0].kind != "paragraph":
            problems.add(f"chapter '{chapter.id}' must start with a paragraph (it is used as the page description)")
    for a, b in zip(en_chapters, de_chapters):
        sa = [x.id for x in a.blocks if x.kind == "subheading"]
        sb = [x.id for x in b.blocks if x.kind == "subheading"]
        if sa != sb:
            problems.add(f"chapter '{a.id}' has different sections in English and German")
        if len(sa) != len(set(sa)):
            problems.add(f"chapter '{a.id}' has a duplicate section id")
        if "--" in a.id or any("--" in s for s in sa):
            problems.add(f"chapter '{a.id}' uses '--' in an id")
        if [(x.kind, len(x.items), x.columns) for x in a.blocks] != [(x.kind, len(x.items), x.columns) for x in b.blocks]:
            problems.add(f"chapter '{a.id}' has a different block structure in English and German")
        fa = [x.text for x in a.blocks if x.kind == "figure"]
        fb = [x.text for x in b.blocks if x.kind == "figure"]
        if fa != fb:
            problems.add(f"chapter '{a.id}' has different figure files in English and German")


def generate(problems):
    manuals = {lang: load_manual(lang) for lang in ("en", "de")}
    check_manual_structure(manuals, problems)
    files = {}
    images = set()
    for lang in ("en", "de"):
        ctx = Context(lang, manuals[lang], problems)
        name, content = index_page(ctx)
        files[name] = content
        name, content = all_page(ctx, images)
        files[name] = content
        for index, chapter in enumerate(manuals[lang]):
            name, content = chapter_page(ctx, chapter, index, images)
            files[name] = content
    return files, images, manuals


HEADER_RE = re.compile(r'<header class="top">.*?</header>', re.S)
FOOTER_RE = re.compile(r'<footer class="footer">.*?</footer>', re.S)
HAND_PAGES = {
    "index.html": ("en", "index"),
    "download.html": ("en", "download"),
    "docs.html": ("en", "docs"),
    "privacy.html": ("en", "privacy"),
    "de/index.html": ("de", "index"),
    "de/download.html": ("de", "download"),
    "de/docs.html": ("de", "docs"),
    "de/privacy.html": ("de", "privacy"),
}


def hand_pages(problems):
    files = {}
    for name, (lang, key) in HAND_PAGES.items():
        path = SITE / name
        if not path.is_file():
            problems.add(f"hand written page {name} is missing")
            continue
        text = path.read_text(encoding="utf-8")
        if not HEADER_RE.search(text) or not FOOTER_RE.search(text):
            problems.add(f"{name}: header or footer block not found")
            continue
        text = HEADER_RE.sub(lambda m: chrome_header(lang, name, key), text, count=1)
        text = FOOTER_RE.sub(lambda m: chrome_footer(lang, name), text, count=1)
        files[name] = text
    return files


ID_RE = re.compile(r'\sid="([^"]+)"')
URL_RE = re.compile(r'(?:href|src)="([^"]*)"')
HOST_RE = re.compile(r"https?://([^/\"'\s<>]+)")


def lint_page(name, text, known, problems):
    if re.search(r"<script", text, re.I):
        problems.add(f"{name}: contains a <script> tag")
    if re.search(r"\son[a-z]+\s*=", text, re.I):
        problems.add(f"{name}: contains an inline event handler")
    if re.search(r"javascript:", text, re.I):
        problems.add(f"{name}: contains a javascript: link")
    if re.search(r"<(iframe|object|embed|form|input)\b", text, re.I):
        problems.add(f"{name}: contains an element that is not allowed")
    for host in HOST_RE.findall(text):
        if host not in ALLOWED_HOSTS:
            problems.add(f"{name}: link to host '{host}' is not allowed")
    for tag in re.findall(r"<img\b[^>]*>", text):
        for attribute in ("alt=", "width=", "height="):
            if attribute not in tag:
                problems.add(f"{name}: an image has no {attribute[:-1]} attribute: {tag[:80]}")
    ids = ID_RE.findall(text)
    if len(ids) != len(set(ids)):
        dupes = sorted({i for i in ids if ids.count(i) > 1})
        problems.add(f"{name}: duplicate ids {dupes}")
    for url in URL_RE.findall(text):
        if url.startswith("https://") or url.startswith("mailto:"):
            continue
        if url.startswith("http://"):
            problems.add(f"{name}: plain http link '{url}'")
            continue
        target, _, fragment = url.partition("#")
        if target:
            joined = posixpath.normpath(posixpath.join(posixpath.dirname(name), target))
        else:
            joined = name
        if joined.endswith("/"):
            joined += "index.html"
        if joined not in known:
            problems.add(f"{name}: link target '{url}' does not exist")
            continue
        if fragment and joined.endswith(".html"):
            if fragment not in known[joined]:
                problems.add(f"{name}: anchor '#{fragment}' not found in {joined}")


def collect_known(pages):
    known = {}
    for path in SITE.rglob("*"):
        if path.is_file():
            known[path.relative_to(SITE).as_posix()] = set()
    for name, text in pages.items():
        known[name] = set(ID_RE.findall(text))
    for name in list(known):
        if name.endswith(".html") and name not in pages:
            known[name] = set(ID_RE.findall((SITE / name).read_text(encoding="utf-8")))
    return known


def golden_check(problems):
    fixture = ROOT / "tests" / "data" / "manual_fixture.md"
    expected = ROOT / "tests" / "data" / "manual_fixture.outline.txt"
    got = outline(parse_manual(fixture.read_text(encoding="utf-8")))
    if got != expected.read_text(encoding="utf-8"):
        problems.add("the Python parser and tests/data/manual_fixture.outline.txt disagree")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    problems = Problems()
    golden_check(problems)
    generated, images, manuals = generate(problems)
    pages = dict(generated)
    pages.update(hand_pages(problems))
    manual_dirs = [TEXT["en"]["dir"] + "/", TEXT["de"]["dir"] + "/"]
    existing = {p.relative_to(SITE).as_posix() for d in manual_dirs for p in (SITE / d).glob("*.html")} if all((SITE / d).is_dir() for d in manual_dirs) else set()
    stale = sorted(existing - set(generated))
    changed = []
    for name, text in sorted(pages.items()):
        path = SITE / name
        if not path.is_file() or path.read_text(encoding="utf-8") != text:
            changed.append(name)
    if args.check:
        for name in changed:
            problems.add(f"{name}: is not up to date, run python3 scripts/build_manual_site.py")
        for name in stale:
            problems.add(f"{name}: is not generated any more, delete it")
    else:
        for name in stale:
            (SITE / name).unlink()
        for name in changed:
            path = SITE / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(pages[name], encoding="utf-8", newline="\n")
    known = collect_known(pages)
    for name, text in sorted(pages.items()):
        lint_page(name, text, known, problems)
    used = set()
    for text in pages.values():
        used.update(re.findall(r"assets/manual/[a-z]{2}/[a-z0-9-]+\.webp", text))
    shot_dir = SITE / "assets" / "manual"
    if shot_dir.is_dir():
        for path in sorted(shot_dir.rglob("*")):
            if path.is_file():
                rel = path.relative_to(SITE).as_posix()
                if rel not in used:
                    problems.add(f"{rel}: picture is not used by any page")
    total_words = {lang: sum(word_count(c) for c in manuals[lang]) for lang in manuals}
    if problems:
        for problem in problems:
            print("PROBLEM:", problem)
        print(f"{len(problems)} problems")
        return 1
    verb = "checked" if args.check else "written"
    print(f"{len(pages)} pages {verb}, {len(manuals['en'])} chapters, about {total_words['en']} English and {total_words['de']} German words, {len(images)} pictures")
    if not args.check:
        print(f"{len(changed)} files changed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
