"""Turn Mastodon/Akkoma JSON into Palm-friendly Windows-1252 text records."""

from __future__ import annotations

import html
import re
import unicodedata
from datetime import datetime, timezone
from html.parser import HTMLParser

US = "\x1f"  # field separator
RS = "\x1e"  # record separator
GS = "\x1d"  # list separator inside a field

# Characters cp1252 can't hold but that have a sensible ASCII-ish stand-in.
_TRANSLIT = {
    "\u2764": "<3", "\u2665": "<3", "\U0001f496": "<3", "\U0001f495": "<3",
    "\U0001f499": "<3", "\U0001f49c": "<3", "\U0001f9e1": "<3", "\U0001f49a": "<3",
    "\U0001f44d": "(y)", "\U0001f44e": "(n)", "\U0001f44b": "o/",
    "\U0001f642": ":)", "\U0001f600": ":D", "\U0001f603": ":D", "\U0001f604": ":D",
    "\U0001f601": ":D", "\U0001f606": "XD", "\U0001f602": ":'D", "\U0001f923": ":'D",
    "\U0001f609": ";)", "\U0001f61b": ":P", "\U0001f61c": ";P", "\U0001f641": ":(",
    "\u2639": ":(", "\U0001f622": ":'(", "\U0001f62d": ":'(", "\U0001f62e": ":O",
    "\U0001f610": ":|", "\U0001f914": ":?", "\U0001f60d": "*_*", "\U0001f525": "(fire)",
    "\U0001f389": "\\o/", "\u2728": "*", "\u2b50": "*", "\U0001f31f": "*",
    "\u2705": "[v]", "\u2714": "v", "\u274c": "[x]", "\u2716": "x",
    "\u2192": "->", "\u2190": "<-", "\u2191": "^", "\u2193": "v", "\u21d2": "=>",
    "\u2026": "\u2026",  # cp1252 has it
    "\u00a0": " ", "\u200b": "", "\u200c": "", "\u200d": "", "\ufe0f": "", "\ufe0e": "",
    "\u2009": " ", "\u202f": " ", "\u2002": " ", "\u2003": " ",
    "\u2212": "-", "\u2010": "-", "\u2011": "-", "\u2012": "-", "\u2015": "--",
    "\u2032": "'", "\u2033": "\"", "\u2022": "\u2022",
    "\u2190": "<-", "\u2264": "<=", "\u2265": ">=", "\u2260": "!=", "\u2248": "~",
    "\u00ad": "",
    "\u0141": "L", "\u0142": "l", "\u0110": "D", "\u0111": "d", "\u0131": "i",
    "\u0126": "H", "\u0127": "h", "\u0166": "T", "\u0167": "t",
}

_CONTROL = re.compile(r"[\x00-\x08\x0b-\x1f\x7f]")


def palm_text(s: str | None) -> str:
    """Return *s* reduced to characters representable in Windows-1252.

    The result is still a ``str``; ``encode_body`` does the final encoding.
    Separator/control characters are removed so they can never break framing.
    """
    if not s:
        return ""
    out = []
    for ch in s:
        if ch == "\r":
            continue
        if ch in _TRANSLIT:
            out.append(_TRANSLIT[ch])
            continue
        try:
            ch.encode("cp1252")
            out.append(ch)
            continue
        except UnicodeEncodeError:
            pass
        decomposed = unicodedata.normalize("NFKD", ch)
        base = "".join(c for c in decomposed if not unicodedata.combining(c))
        try:
            if base:
                base.encode("cp1252")
                out.append(base)
                continue
        except UnicodeEncodeError:
            pass
        cat = unicodedata.category(ch)
        if cat in ("So", "Sk", "Cs") or 0x1F000 <= ord(ch) <= 0x1FAFF:
            out.append("*")  # emoji and other pictographs
        elif cat.startswith("M") or cat == "Cf":
            continue  # stray combining marks / format characters
        else:
            out.append("?")
    text = "".join(out)
    text = _CONTROL.sub(lambda m: "\n" if m.group(0) == "\n" else "", text)
    return text


class _Flattener(HTMLParser):
    """Flatten Mastodon status HTML to plain text with sensible line breaks."""

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.parts: list[str] = []
        self._skip = 0  # depth inside invisible spans (Mastodon link ellipsis)
        self._in_pre = False

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        cls = a.get("class") or ""
        if tag == "span" and "invisible" in cls.split():
            self._skip += 1
            return
        if self._skip:
            if tag == "span":
                self._skip += 1
            return
        if tag == "br":
            self.parts.append("\n")
        elif tag in ("p", "div", "blockquote", "ul", "ol", "h1", "h2", "h3", "h4", "h5", "h6", "pre"):
            self._para()
            if tag == "blockquote":
                self.parts.append("> ")
            if tag == "pre":
                self._in_pre = True
        elif tag == "li":
            self._newline()
            self.parts.append("\u2022 ")
        elif tag == "img":
            alt = a.get("alt") or a.get("title") or ""
            if alt:
                self.parts.append(alt)

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)

    def handle_endtag(self, tag):
        if self._skip:
            if tag == "span":
                self._skip -= 1
            return
        if tag in ("p", "div", "blockquote", "ul", "ol", "h1", "h2", "h3", "h4", "h5", "h6", "pre"):
            self._para()
            if tag == "pre":
                self._in_pre = False

    def handle_data(self, data):
        if self._skip:
            return
        if not self._in_pre:
            data = re.sub(r"[ \t\r\n]+", " ", data)
        self.parts.append(data)

    def _newline(self):
        text = "".join(self.parts)
        if text and not text.endswith("\n"):
            self.parts.append("\n")

    def _para(self):
        text = "".join(self.parts)
        if not text:
            return
        if text.endswith("\n\n"):
            return
        self.parts.append("\n" if text.endswith("\n") else "\n\n")


def html_to_text(content: str | None) -> str:
    if not content:
        return ""
    if "<" not in content:
        return html.unescape(content).strip()
    p = _Flattener()
    p.feed(content)
    p.close()
    text = "".join(p.parts)
    text = re.sub(r"[ \t]+\n", "\n", text)
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text.strip()


_MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]


def short_time(iso: str | None, now: datetime | None = None) -> str:
    if not iso:
        return ""
    try:
        t = datetime.fromisoformat(iso.replace("Z", "+00:00"))
    except ValueError:
        return ""
    if t.tzinfo is None:
        t = t.replace(tzinfo=timezone.utc)
    now = now or datetime.now(timezone.utc)
    secs = int((now - t).total_seconds())
    if secs < 60:
        return "now"
    if secs < 3600:
        return f"{secs // 60}m"
    if secs < 86400:
        return f"{secs // 3600}h"
    if secs < 7 * 86400:
        return f"{secs // 86400}d"
    label = f"{_MONTHS[t.month - 1]} {t.day}"
    if t.year != now.year:
        label += f" {t.year % 100:02d}"
    return label


def clean_field(s: str | None) -> str:
    """palm_text + strip framing characters (defence in depth)."""
    return palm_text(s).replace(US, " ").replace(RS, " ").replace(GS, " ")


def record(fields) -> str:
    return US.join(clean_field(str(f)) if f is not None else "" for f in fields)


def encode_body(records: list[list]) -> bytes:
    return RS.join(record(r) for r in records).encode("cp1252", errors="replace")


def error_body(message: str) -> bytes:
    return encode_body([["ERR", message]])


def fit_budget(make_header, items: list[tuple[str, list]], budget: int,
               text_index: int = 5, max_text: int = 1500) -> bytes:
    """Encode a header plus items while keeping the payload under *budget*.

    *items* is a list of ``(cursor, fields)``; *make_header(kept)* builds the
    header from the items that survive, so the pagination cursor always points
    just past the last item the client actually received. Long texts are
    truncated first; if it still does not fit, trailing items are dropped (the
    client simply pages again from the new cursor).
    """
    items = [(c, list(f)) for c, f in items]
    for _, f in items:
        if len(f) > text_index and len(f[text_index]) > max_text:
            f[text_index] = f[text_index][: max_text - 1].rstrip() + "\u2026"
    while True:
        body = encode_body([make_header(items)] + [f for _, f in items])
        if len(body) <= budget or not items:
            return body
        longest = max(range(len(items)), key=lambda i: len(items[i][1][text_index]))
        text = items[longest][1][text_index]
        if len(text) > 400:
            items[longest][1][text_index] = text[: len(text) // 2].rstrip() + "\u2026"
            continue
        items.pop()
