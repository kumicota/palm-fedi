"""Map Mastodon/Akkoma entities to PalmFedi item records (see docs/PROTOCOL.md)."""

from __future__ import annotations

from typing import Callable

from .render import html_to_text, short_time, time_left

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
                  context: str = "", kind: str = "S", poll_text: bool = False) -> list:
    """Fields for a status; *status* may be a boost wrapper."""
    if status.get("reblog"):
        booster = display_name(status.get("account") or {})
        status = status["reblog"]
        context = context or f"{booster} boosted"

    account = status.get("account") or {}
    text = html_to_text(status.get("content"))
    poll = status.get("poll")
    if poll and poll_text:
        # protocol v1 clients don't know the poll field: show the options as text
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
        alts.append(m.get("description") or "")

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
        media_keys,
        alts,
        keyer(avatar, avatar) if avatar else "",
        _VIS.get(status.get("visibility"), "p"),
        _mentions(status, me_acct),
        account.get("id", ""),
        poll_field(poll),
    ]


def _count(n: int, one: str, many: str) -> str:
    return f"{n} {one if n == 1 else many}"


def poll_field(poll: dict | None) -> list:
    """Poll as a GS list: id, flags, summary, then one entry per option.

    flags: ``M`` multiple choice, ``V`` you voted, ``X`` closed.
    option: ``*`` (your vote) or ``-``, the percentage (empty if the server
    hides totals), ``:``, the title.
    """
    if not poll or not poll.get("id"):
        return []
    multiple = bool(poll.get("multiple"))
    expired = bool(poll.get("expired"))
    flags = ("M" if multiple else "") + ("V" if poll.get("voted") else "") + ("X" if expired else "")
    votes = poll.get("votes_count") or 0
    voters = poll.get("voters_count")
    # multiple choice: percentages are of people, as in Mastodon's web UI
    total = voters if multiple and voters else votes
    own = set(poll.get("own_votes") or [])
    options = []
    for i, opt in enumerate(poll.get("options") or []):
        count = opt.get("votes_count")
        pct = "" if count is None else str(round(count * 100 / total)) if total else "0"
        options.append(("*" if i in own else "-") + pct + ":" + (opt.get("title") or ""))
    people = voters if voters is not None else votes
    summary = _count(people, "person", "people") if multiple else _count(votes, "vote", "votes")
    if expired:
        summary += " \u00b7 closed"
    elif poll.get("expires_at"):
        left = time_left(poll["expires_at"])
        summary += f" \u00b7 {left} left" if left else " \u00b7 closing"
    return [poll["id"], flags, summary] + options


def relationship_flags(rel: dict | None, me: bool = False) -> str:
    """``W`` you follow them, ``Q`` follow requested, ``Y`` they follow you,
    ``M`` it's you."""
    if me:
        return "M"
    rel = rel or {}
    return (("W" if rel.get("following") else "") + ("Q" if rel.get("requested") else "")
            + ("Y" if rel.get("followed_by") else ""))


def relationship_label(flags: str) -> str:
    """Mirrors AcctRelLabel() in palm/src/account.c."""
    if "M" in flags:
        return "This is you"
    if "W" in flags:
        return "You follow each other" if "Y" in flags else "You follow them"
    if "Q" in flags:
        return "Follow requested"
    if "Y" in flags:
        return "Follows you"
    return ""


def account_fields(account: dict, keyer: Keyer, kind: str = "N", context: str = "",
                   flags: str = "", profile: bool = False) -> list:
    """An account as an item: ``P`` profile header (user timeline) or ``N``
    (search result; tapping it opens the account's posts)."""
    avatar = account.get("avatar_static") or account.get("avatar")
    text = html_to_text(account.get("note"))
    if profile:
        stats = "{} posts \u00b7 {} following \u00b7 {} followers".format(
            account.get("statuses_count") or 0, account.get("following_count") or 0,
            account.get("followers_count") or 0)
        extra = []
        for f in account.get("fields") or []:
            value = html_to_text(f.get("value"))
            if f.get("name") or value:
                extra.append(f"{html_to_text(f.get('name'))}: {value}")
        text = "\n\n".join(p for p in (text, "\n".join(extra), stats) if p)
    return [
        kind, "", display_name(account), account.get("acct", ""), "",
        text, "", context, flags, "0 0 0", [], [],
        keyer(avatar, avatar) if avatar else "", "p", "", account.get("id", ""), [],
    ]


def tag_fields(tag: dict) -> list:
    """A hashtag search result; the tag name is in the id field."""
    name = tag.get("name") or ""
    uses = 0
    for day in (tag.get("history") or [])[:7]:
        try:
            uses += int(day.get("uses") or 0)
        except (TypeError, ValueError):
            pass
    text = _count(uses, "post", "posts") + " this week" if tag.get("history") else ""
    return ["T", name, "#" + name, "", "", text, "", "", "", "0 0 0", [], [], "", "p", "", "", []]


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


def notification_fields(n: dict, keyer: Keyer, me_acct: str | None = None,
                        poll_text: bool = False) -> list:
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
        fields = status_fields(status, keyer, me_acct, context=context, poll_text=poll_text)
        if ntype == "mention":
            fields[7] = ""  # the item itself shows the author; no need to repeat
        return fields

    avatar = account.get("avatar_static") or account.get("avatar")
    note = html_to_text(account.get("note"))
    return [
        "N", "", who, account.get("acct", ""), short_time(n.get("created_at")),
        note, "", context, "", "0 0 0", [], [],
        keyer(avatar, avatar) if avatar else "", "p", "", account.get("id", ""), [],
    ]
