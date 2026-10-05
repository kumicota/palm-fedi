import io
import struct
import threading
import urllib.parse
import urllib.request

import pytest
from PIL import Image

from palmfedi_gateway import images, server
from palmfedi_gateway.convert import (notification_fields, poll_field, relationship_flags,
                                      relationship_label, status_fields)
from palmfedi_gateway.mastodon import next_max_id
from palmfedi_gateway.render import (GS, RS, US, encode_body, fit_budget, html_to_text, palm_text,
                                     short_time, time_left)
from palmfedi_gateway.store import Store


def keyer(thumb, full):
    return "k" + str(abs(hash((thumb, full))) % 1000)


ACCOUNT = {"id": "9", "username": "alice", "acct": "alice@example.social",
           "display_name": "Alice ✨", "avatar_static": "https://e/a.png", "note": "<p>hi</p>"}


def make_status(**kw):
    s = {
        "id": "100", "account": ACCOUNT, "created_at": "2026-10-05T10:00:00Z",
        "content": "<p>Hello <a href='x'><span class='invisible'>https://</span>"
                   "<span>link.example</span></a> café \U0001f600</p><p>2nd</p>",
        "spoiler_text": "", "visibility": "public", "media_attachments": [],
        "mentions": [{"acct": "bob"}, {"acct": "me"}], "replies_count": 1,
        "reblogs_count": 2, "favourites_count": 3, "favourited": True,
    }
    s.update(kw)
    return s


def test_html_to_text():
    assert html_to_text("<p>a<br>b</p><p>c &amp; d</p>") == "a\nb\n\nc & d"
    assert html_to_text("<ul><li>x</li><li>y</li></ul>") == "• x\n• y"


def test_palm_text_transliterates():
    assert palm_text("café “q”") == "café “q”"  # cp1252 has these
    assert palm_text("❤ \U0001f600") == "<3 :D"
    assert palm_text("Łódź") == "Lodz"[:0] + "Lódz"
    assert palm_text("\U0001f9a5") == "*"
    assert palm_text("a\x1fb\x1ec") == "abc"
    palm_text("日本").encode("cp1252")


def test_short_time():
    from datetime import datetime, timezone
    now = datetime(2026, 10, 5, 12, 0, tzinfo=timezone.utc)
    assert short_time("2026-10-05T11:59:30Z", now) == "now"
    assert short_time("2026-10-05T11:00:00.000Z", now) == "1h"
    assert short_time("2026-09-01T11:00:00Z", now) == "Sep 1"
    assert short_time("2025-09-01T11:00:00Z", now) == "Sep 1 25"


def test_status_fields_boost_and_media():
    inner = make_status(media_attachments=[
        {"type": "image", "url": "https://e/full.jpg", "preview_url": "https://e/s.jpg",
         "description": "a cat"},
        {"type": "video", "url": "https://e/v.mp4", "preview_url": "https://e/v.jpg"},
    ], sensitive=True, in_reply_to_id="5")
    wrapper = {"id": "200", "account": {"username": "carol", "display_name": ""}, "reblog": inner}
    f = status_fields(wrapper, keyer, me_acct="me")
    assert f[0] == "S" and f[1] == "100"
    assert f[2] == "Alice ✨"
    assert f[5].startswith("Hello link.example café")
    assert f[7] == "carol boosted"
    assert f[8] == "FSR"
    assert f[9] == "1 2 3"
    assert [m[0] for m in f[10]] == ["I", "V"]
    assert f[11] == ["a cat", ""]
    assert f[14] == "@alice@example.social @bob "
    assert f[15] == "9"
    assert f[16] == []  # no poll


def test_record_keeps_list_separators():
    f = status_fields(make_status(media_attachments=[
        {"type": "image", "url": "https://e/1.jpg", "description": "one\x1dtwo"},
        {"type": "image", "url": "https://e/2.jpg", "description": "b"},
    ]), keyer)
    rec = records(encode_body([f]))[0]
    media = rec[10].split(GS)
    assert len(media) == 2 and all(m.startswith("I:k") for m in media)
    assert rec[11].split(GS) == ["onetwo", "b"]  # GS inside text can't break framing


POLL = {"id": "p1", "expires_at": "2026-10-07T12:00:00Z", "expired": False, "multiple": False,
        "votes_count": 4, "voters_count": 4, "voted": True, "own_votes": [1],
        "options": [{"title": "Tea", "votes_count": 1}, {"title": "Coffee", "votes_count": 3}]}


def test_poll_field():
    f = poll_field(POLL)
    assert f[0] == "p1" and f[1] == "V"
    assert f[2].startswith("4 votes \u00b7 ") and f[2].endswith(" left")
    assert f[3:] == ["-25:Tea", "*75:Coffee"]
    hidden = poll_field(dict(POLL, multiple=True, expired=True, voted=False, own_votes=[],
                             voters_count=2, options=[{"title": "a", "votes_count": None}]))
    assert hidden == ["p1", "MX", "2 people \u00b7 closed", "-:a"]
    assert poll_field(None) == []
    # v1 clients still get the options as text, v2 clients get the field
    s = make_status(poll=POLL)
    assert "[ ] Coffee (3)" in status_fields(s, keyer, poll_text=True)[5]
    assert "Coffee" not in status_fields(s, keyer)[5]


def test_time_left():
    from datetime import datetime, timezone
    now = datetime(2026, 10, 5, 12, 0, tzinfo=timezone.utc)
    assert time_left("2026-10-05T12:00:20Z", now) == "1m"
    assert time_left("2026-10-05T15:30:00Z", now) == "3h"
    assert time_left("2026-10-08T12:00:00Z", now) == "3d"
    assert time_left("2026-10-05T11:00:00Z", now) == ""


def test_relationship():
    assert relationship_flags({"following": True, "followed_by": True}) == "WY"
    assert relationship_flags({"requested": True}) == "Q"
    assert relationship_label("WY") == "You follow each other"
    assert relationship_label("Y") == "Follows you"
    assert relationship_label("") == ""
    assert relationship_flags(None, me=True) == "M" and relationship_label("M") == "This is you"


def test_notification_follow():
    f = notification_fields({"id": "1", "type": "follow", "account": ACCOUNT,
                             "created_at": "2026-10-05T10:00:00Z"}, keyer)
    assert f[0] == "N" and f[7] == "Alice ✨ followed you" and f[5] == "hi"


def test_fit_budget_drops_and_moves_cursor():
    items = [(str(i), ["S", str(i), "n", "a", "t", "x" * 300]) for i in range(20)]
    body = fit_budget(lambda kept: ["OK", kept[-1][0] if kept else ""], items, 2000)
    assert len(body) <= 2000
    recs = body.decode("cp1252").split(RS)
    assert recs[0].split(US)[1] == recs[-1].split(US)[1]  # cursor == last kept id


def test_next_max_id():
    h = {"Link": '<https://x/api/v1/timelines/home?max_id=123>; rel="next", '
                 '<https://x/api/v1/timelines/home?min_id=200>; rel="prev"'}
    assert next_max_id(h) == "123"


@pytest.mark.parametrize("bpp", [16, 8])
def test_image_encoding(bpp):
    img = Image.new("RGB", (101, 40), (255, 0, 0))
    buf = io.BytesIO()
    img.save(buf, "PNG")
    out = images.transcode(buf.getvalue(), 50, 50, bpp)
    magic, w, h, b, dens, row = out[:4], *struct.unpack(">HHBBH", out[4:12])
    assert magic == b"PFI1" and w == 50 and h == 20 and b == bpp and dens == 144
    assert row % 2 == 0 and len(out) == 12 + row * h
    if bpp == 16:
        assert out[12:14] == b"\xf8\x00"  # pure red in RGB565
    else:
        assert out[12] == 5 * 36  # red in the 6x6x6 cube
    crop = images.transcode(buf.getvalue(), 32, 32, bpp, crop=True)
    assert struct.unpack(">HH", crop[4:8]) == (32, 32)


def test_transcode_rejects_garbage(monkeypatch):
    monkeypatch.setattr(images, "FFMPEG", None)
    with pytest.raises(images.ImageError):
        images.transcode(b"<html>not an image</html>", 40, 40)
    with pytest.raises(images.ImageError):
        images.transcode(b"\x89PNG\r\n\x1a\n" + b"\0" * 40, 40, 40)  # truncated PNG


@pytest.mark.skipif(not images.FFMPEG, reason="ffmpeg not installed")
def test_transcode_ffmpeg_fallback(monkeypatch):
    # pretend Pillow can't read PNG (like AVIF on an old Pillow): ffmpeg decodes it
    real_open = images.Image.open
    calls = []

    def fussy_open(fp, *a, **kw):
        calls.append(1)
        if len(calls) == 1:
            raise images.Image.UnidentifiedImageError("nope")
        return real_open(fp, *a, **kw)

    monkeypatch.setattr(images.Image, "open", fussy_open)
    buf = io.BytesIO()
    Image.new("RGB", (20, 10), (0, 0, 255)).save(buf, "PNG")
    out = images.transcode(buf.getvalue(), 40, 40, 16)
    assert out[:4] == b"PFI1" and struct.unpack(">HH", out[4:8]) == (20, 10)


# -- end-to-end through the real HTTP server with a fake upstream ----------

class FakeClient:
    calls = []

    def __init__(self, instance, token=None, timeout=20):
        self.token = token

    def request(self, method, path, params=None, form=None):
        FakeClient.calls.append((method, path, params, form))
        if path == "/api/v1/apps":
            return {"client_id": "cid", "client_secret": "sec"}, {}
        if path == "/oauth/token":
            return {"access_token": "tok"}, {}
        if path == "/api/v1/accounts/verify_credentials":
            return {"acct": "me", "username": "me", "display_name": "Me"}, {}
        if path == "/api/v1/timelines/home":
            return [make_status(id=str(i)) for i in range(5)], \
                {"Link": '<https://x/api/v1/timelines/home?max_id=4>; rel="next"'}
        if path == "/api/v1/accounts/9":
            return dict(ACCOUNT, statuses_count=5, following_count=1, followers_count=2,
                        fields=[{"name": "Web", "value": "<a href='x'>x.y</a>"}]), {}
        if path == "/api/v1/accounts/9/statuses":
            return [make_status(id="50")], {}
        if path == "/api/v1/accounts/relationships":
            return [{"id": "9", "following": False, "followed_by": True}], {}
        if path == "/api/v1/accounts/9/follow":
            return {"id": "9", "following": True, "followed_by": True}, {}
        if path == "/api/v1/accounts/9/unfollow":
            return {"id": "9", "following": False}, {}
        if path == "/api/v2/search":
            if params["q"] == "zzz":
                return {"accounts": [], "statuses": [], "hashtags": []}, {}
            return {"accounts": [ACCOUNT], "statuses": [make_status(id="60")],
                    "hashtags": [{"name": "palm", "history": [{"uses": "3"}, {"uses": "2"}]}]}, {}
        if path == "/api/v1/timelines/tag/palm":
            return [make_status(id="70")], {}
        if path == "/api/v1/polls/p1/votes":
            return dict(POLL, own_votes=[0], votes_count=5, options=[{"title": "Tea", "votes_count": 2},
                                                      {"title": "Coffee", "votes_count": 3}]), {}
        if path.endswith("/favourite"):
            return make_status(favourited=True, favourites_count=4), {}
        if path == "/api/v1/statuses":
            return {"id": "777"}, {}
        raise AssertionError(path)

    get = server.Client.get
    post = server.Client.post
    register_app = server.Client.register_app
    token_from_password = server.Client.token_from_password
    verify = server.Client.verify


@pytest.fixture
def gw(tmp_path, monkeypatch):
    monkeypatch.setattr(server, "Client", FakeClient)
    g = server.Gateway(Store(str(tmp_path / "db.sqlite3")))
    srv = server.make_server(g, "127.0.0.1", 0)
    t = threading.Thread(target=srv.serve_forever, daemon=True)
    t.start()
    yield g, f"http://127.0.0.1:{srv.server_address[1]}"
    srv.shutdown()


def call(base, path, form=None):
    data = urllib.parse.urlencode(form, encoding="cp1252").encode() if form is not None else None
    with urllib.request.urlopen(base + path, data=data) as r:
        return r.read()


def records(body):
    return [r.split(US) for r in body.decode("cp1252").split(RS)]


def test_end_to_end(gw):
    g, base = gw
    assert records(call(base, "/p/ping"))[0][0] == "OK"
    assert records(call(base, "/p/tl?k=NOPE"))[0][0] == "ERR"

    login = records(call(base, "/p/login", {"inst": "example.social", "user": "me", "pass": "pw"}))
    assert login[0][0] == "OK" and login[0][2] == "me@example.social"
    key = login[0][1]

    tl = records(call(base, f"/p/tl?k={key}&t=home"))
    assert tl[0][:2] == ["OK", "4"] and len(tl) == 6
    assert tl[1][14] == "@alice@example.social @bob "

    act = records(call(base, "/p/act", {"k": key, "id": "100", "a": "fav"}))
    assert act[0] == ["OK", "F", "1 2 4"]

    post = records(call(base, "/p/post", {"k": key, "text": "olá", "vis": "k", "reply": "5"}))
    assert post[0] == ["OK", "777"]
    form = FakeClient.calls[-1][3]
    assert form["status"] == "olá" and form["visibility"] == "private"
    assert form["in_reply_to_id"] == "5"

    assert records(call(base, f"/p/img?k={key}&m=unknown"))[0][0] == "ERR"


def login(base):
    return records(call(base, "/p/login", {"inst": "example.social", "user": "me",
                                           "pass": "pw"}))[0][1]


def test_profile_and_follow(gw):
    _, base = gw
    key = login(base)
    tl = records(call(base, f"/p/tl?k={key}&t=user&id=9&v=2"))
    assert tl[0][1] == "50"  # cursor skips the profile item
    profile = tl[1]
    assert profile[0] == "P" and profile[15] == "9" and profile[8] == "Y"
    assert profile[7] == "Follows you"
    assert "Web: x.y" in profile[5] and "5 posts" in profile[5]
    assert tl[2][0] == "S"
    # v1 clients and later pages get no profile item
    assert records(call(base, f"/p/tl?k={key}&t=user&id=9"))[1][0] == "S"
    assert records(call(base, f"/p/tl?k={key}&t=user&id=9&v=2&max=50"))[1][0] == "S"

    assert records(call(base, f"/p/rel?k={key}&id=9"))[0] == ["OK", "Y"]
    assert records(call(base, "/p/follow", {"k": key, "id": "9", "a": "follow"}))[0] == ["OK", "WY"]
    assert records(call(base, "/p/follow", {"k": key, "id": "9", "a": "unfollow"}))[0] == ["OK", ""]
    assert records(call(base, "/p/follow", {"k": key, "id": "9", "a": "block"}))[0][0] == "ERR"
    assert records(call(base, "/p/follow", {"k": key, "id": "../x", "a": "follow"}))[0][0] == "ERR"


def test_search_and_tags(gw):
    _, base = gw
    key = login(base)
    res = records(call(base, f"/p/tl?k={key}&t=search&id=%23palm&v=2"))
    assert res[0][:2] == ["OK", ""]  # no paging
    assert [r[0] for r in res[1:]] == ["N", "T", "S"]
    assert res[1][15] == "9" and res[1][7].startswith("Account")
    assert res[2][1] == "palm" and res[2][2] == "#palm" and res[2][5] == "5 posts this week"
    assert FakeClient.calls[-1][2]["q"] == "#palm"
    assert [r[0] for r in records(call(base, f"/p/tl?k={key}&t=search&id=x"))[1:]] == ["N", "S"]
    assert records(call(base, f"/p/tl?k={key}&t=search&id=zzz&v=2"))[0] == ["ERR", "Nothing found"]
    assert records(call(base, f"/p/tl?k={key}&t=search&id=&v=2"))[0][0] == "ERR"

    tag = records(call(base, f"/p/tl?k={key}&t=tag&id=palm&v=2"))
    assert tag[0][0] == "OK" and tag[1][1] == "70"
    assert records(call(base, f"/p/tl?k={key}&t=tag&id=a/b"))[0][0] == "ERR"


def test_vote(gw):
    _, base = gw
    key = login(base)
    res = records(call(base, "/p/vote", {"k": key, "id": "p1", "c": "0"}))
    assert res[0][0] == "OK"
    assert res[0][1].split(GS)[3:] == ["*40:Tea", "-60:Coffee"]
    assert FakeClient.calls[-1][3] == {"choices[]": [0]}
    call(base, "/p/vote", {"k": key, "id": "p1", "c": "2,0,2"})
    assert FakeClient.calls[-1][3] == {"choices[]": [0, 2]}
    assert records(call(base, "/p/vote", {"k": key, "id": "p1", "c": ""}))[0][0] == "ERR"
    assert records(call(base, "/p/vote", {"k": key, "id": "p1", "c": "x"}))[0][0] == "ERR"


def test_img_bad_media_is_remembered(gw, monkeypatch):
    g, base = gw
    key = login(base)
    fetched = []

    def fake_fetch(url):
        fetched.append(url)
        return b"<html>nope</html>", "text/html"

    monkeypatch.setattr(server, "_fetch", fake_fetch)
    monkeypatch.setattr(images, "FFMPEG", None)
    m = g.store.media_key("https://e/thumb.avif", "https://e/full.avif")
    assert records(call(base, f"/p/img?k={key}&m={m}&w=40&h=40"))[0] == ["ERR", "Can't show this image"]
    assert fetched == ["https://e/thumb.avif", "https://e/full.avif"]  # tried both
    call(base, f"/p/img?k={key}&m={m}&w=80&h=80")
    assert len(fetched) == 2  # not downloaded again


def test_img_falls_back_to_full_and_video_frames(gw, monkeypatch):
    g, base = gw
    key = login(base)
    png = io.BytesIO()
    Image.new("RGB", (10, 10), (0, 255, 0)).save(png, "PNG")

    def fake_fetch(url):
        if url.endswith(".mp4"):
            return None, "video/mp4"
        return (png.getvalue(), "image/png") if "full" in url else (b"junk", "image/avif")

    monkeypatch.setattr(server, "_fetch", fake_fetch)
    monkeypatch.setattr(images, "FFMPEG", None)
    m = g.store.media_key("https://e/thumb.avif", "https://e/full.png")
    body = call(base, f"/p/img?k={key}&m={m}&w=40&h=40")
    assert body[:4] == b"PFI1"

    frames = []
    monkeypatch.setattr(server, "video_frame", lambda url: frames.append(url) or png.getvalue())
    v = g.store.media_key("https://e/v.mp4", "https://e/v.mp4")
    assert call(base, f"/p/img?k={key}&m={v}&w=40&h=40")[:4] == b"PFI1"
    assert frames == ["https://e/v.mp4"]
