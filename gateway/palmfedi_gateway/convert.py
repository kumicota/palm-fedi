"""Map Mastodon/Akkoma entities to PalmFedi item records (see docs/PROTOCOL.md)."""

from __future__ import annotations

from typing import Callable

from .render import GS, html_to_text, short_time

Keyer = Callable[[str | None, str | None], str]  # (thumb_url, full_url) -> key

_VIS = {"public": "p", "unlisted": "u", "private": "k", "direct": "d", "local": "u", "list": "k"}
_MEDIA_TYPE = {"image": "I", "gifv": "V", "video": "V", "audio": "A"}


def display_name(account: dict) -> str:
    name = (account.get("display_name") or "").strip()
    return name or account.get("username") or account.get("acct") or "?"


def _mentions(status: dict, me_acct: str | None) -> str:
    seen: list[str] = []
    candidates = [status.get("account", {}).get("acct")]
    candidates += [m.get("acct") for m in status.get("mentions") or []]
    for acct in candidates:
        if not acct or acct == me_acct or acct in seen:
            continue
        seen.append(acct)
    return "".join(f"@{a} " for a in seen)


def status_fields(status: dict, keyer: Keyer, me_acct: str | None = None,
                  context: str = "", kind: str = "S") -> list:
    """Fields for a status; *status* may be a boost wrapper."""
    if status.get("reblog"):
        booster = display_name(status.get("account") or {})
        status = status["reblog"]
        context = context or f"{booster} boosted"

    account = status.get("account") or {}
    text = html_to_text(status.get("content"))
    poll = status.get("poll")
    if poll:
        lines = []
        for opt in poll.get("options") or []:
            votes = opt.get("votes_count")
            lines.append(f"[ ] {opt.get('title', '')}" + (f" ({votes})" if votes is not None else ""))
        text = (text + "\n\n" if text else "") + "\n".join(lines)

    media_keys, alts = [], []
    for m in status.get("media_attachments") or []:
        t = _MEDIA_TYPE.get(m.get("type"), "U")
        thumb = m.get("preview_url") or m.get("url")
        full = m.get("url") if t == "I" else thumb
        if not thumb:
            continue
        media_keys.append(f"{t}:{keyer(thumb, full)}")
        alts.append((m.get("description") or "").replace(GS, " "))

    flags = ""
    if status.get("favourited"):
        flags += "F"
    if status.get("reblogged"):
        flags += "B"
    if status.get("bookmarked"):
        flags += "K"
    if status.get("sensitive") and media_keys:
        flags += "S"
    if status.get("in_reply_to_id"):
        flags += "R"
    if me_acct and account.get("acct") == me_acct:
        flags += "M"

    counts = "{} {} {}".format(status.get("replies_count") or 0,
                               status.get("reblogs_count") or 0,
                               status.get("favourites_count") or 0)
    avatar = account.get("avatar_static") or account.get("avatar")
    return [
        kind,
        status.get("id", ""),
        display_name(account),
        account.get("acct", ""),
        short_time(status.get("created_at")),
        text,
        status.get("spoiler_text") or "",
        context,
        flags,
        counts,
        GS.join(media_keys),
        GS.join(alts),
        keyer(avatar, avatar) if avatar else "",
        _VIS.get(status.get("visibility"), "p"),
        _mentions(status, me_acct),
        account.get("id", ""),
    ]


_NOTIF_TEXT = {
    "favourite": "{} favourited",
    "reblog": "{} boosted",
    "follow": "{} followed you",
    "follow_request": "{} wants to follow you",
    "poll": "A poll has ended",
    "update": "{} edited a post",
    "move": "{} moved",
    "status": "{} posted",
    "mention": "{} mentioned you",
}


def notification_fields(n: dict, keyer: Keyer, me_acct: str | None = None) -> list:
    account = n.get("account") or {}
    who = display_name(account)
    ntype = n.get("type", "")
    if ntype in ("pleroma:emoji_reaction", "emoji_reaction"):
        context = f"{who} reacted {n.get('emoji') or ''}".rstrip()
    elif ntype == "mention" and (n.get("status") or {}).get("visibility") == "direct":
        context = f"{who} sent a DM"
    else:
        context = _NOTIF_TEXT.get(ntype, "{} " + ntype).format(who)

    status = n.get("status")
    if status:
        fields = status_fields(status, keyer, me_acct, context=context)
        if ntype == "mention":
            fields[7] = ""  # the item itself shows the author; no need to repeat
        return fields

    avatar = account.get("avatar_static") or account.get("avatar")
    note = html_to_text(account.get("note"))
    return [
        "N", "", who, account.get("acct", ""), short_time(n.get("created_at")),
        note, "", context, "", "0 0 0", "", "",
        keyer(avatar, avatar) if avatar else "", "p", "", account.get("id", ""),
    ]
