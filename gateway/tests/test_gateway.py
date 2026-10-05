import io
import struct
import threading
import urllib.parse
import urllib.request

import pytest
from PIL import Image

from palmfedi_gateway import images, server
from palmfedi_gateway.convert import notification_fields, status_fields
from palmfedi_gateway.mastodon import next_max_id
from palmfedi_gateway.render import GS, RS, US, fit_budget, html_to_text, palm_text, short_time
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
    media = f[10].split(GS)
    assert [m[0] for m in media] == ["I", "V"]
    assert f[11].split(GS) == ["a cat", ""]
    assert f[14] == "@alice@example.social @bob "
    assert f[15] == "9"


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
