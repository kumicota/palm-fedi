"""HTTP server: plain-HTTP Palm endpoints (/p/...) plus a small web UI for linking."""

from __future__ import annotations

import html
import logging
import re
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from . import __version__
from .convert import notification_fields, status_fields
from .images import MAX_SOURCE_BYTES, ByteLRU, transcode
from .mastodon import USER_AGENT, ApiError, Client, next_max_id, normalize_instance
from .render import encode_body, error_body, fit_budget
from .store import Store

log = logging.getLogger("palmfedi")

_SECRETS = re.compile(r"\b(k|pass)=[^&\s]*")

RECORD_TYPE = "application/x-palmfedi"
IMAGE_TYPE = "application/x-palmfedi-image"

VIS = {"p": "public", "u": "unlisted", "k": "private", "d": "direct"}
ACTIONS = {"fav": "favourite", "unfav": "unfavourite", "boost": "reblog",
           "unboost": "unreblog", "bm": "bookmark", "unbm": "unbookmark"}


class Gateway:
    """State shared by all request handlers."""

    def __init__(self, store: Store, public_url: str | None = None,
                 image_cache_mb: int = 64, allow_password_login: bool = True):
        self.store = store
        self.public_url = public_url.rstrip("/") if public_url else None
        self.images = ByteLRU(image_cache_mb * 1024 * 1024)
        self.allow_password_login = allow_password_login

    def client(self, session) -> Client:
        instance, token, _acct = session
        return Client(instance, token)

    def oauth_app(self, instance: str, redirect: str):
        app = self.store.app(instance, redirect)
        if not app:
            app = Client(instance).register_app(redirect)
            self.store.save_app(instance, redirect, *app)
        return app


class PalmError(Exception):
    """An error shown to the Palm user as an ERR record."""


def _form_decode(raw: bytes) -> dict[str, str]:
    # Palm sends Windows-1252; decode percent escapes with that charset.
    pairs = urllib.parse.parse_qsl(raw.decode("ascii", "replace"), keep_blank_values=True,
                                   encoding="cp1252", errors="replace")
    return {k: v for k, v in pairs}


def _int(value, default, lo, hi) -> int:
    try:
        return max(lo, min(hi, int(value)))
    except (TypeError, ValueError):
        return default


class Handler(BaseHTTPRequestHandler):
    server_version = "PalmFedi/" + __version__
    protocol_version = "HTTP/1.0"  # Palm client reads until close
    gw: Gateway  # set by make_server

    # -- plumbing ---------------------------------------------------------
    def log_message(self, fmt, *args):
        # Never log device keys or passwords.
        msg = _SECRETS.sub(r"\1=***", fmt % args)
        log.info("%s %s", self.address_string(), msg)

    def _send(self, body: bytes, ctype: str, status: int = 200, extra: dict | None = None):
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _records(self, body: bytes):
        self._send(body, RECORD_TYPE)

    def _params(self) -> dict[str, str]:
        url = urllib.parse.urlsplit(self.path)
        params = {k: v for k, v in urllib.parse.parse_qsl(url.query, keep_blank_values=True,
                                                         encoding="cp1252", errors="replace")}
        if self.command == "POST":
            length = _int(self.headers.get("Content-Length"), 0, 0, 64 * 1024)
            params.update(_form_decode(self.rfile.read(length)))
        return params

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        self._dispatch()

    def do_POST(self):
        self._dispatch()

    def _dispatch(self):
        path = urllib.parse.urlsplit(self.path).path
        try:
            params = self._params()
            if path.startswith("/p/"):
                route = PALM_ROUTES.get((self.command if self.command != "HEAD" else "GET", path))
                if not route:
                    raise PalmError("Unknown request")
                route(self, params)
            else:
                route = WEB_ROUTES.get(path)
                if not route:
                    self._send(b"Not found", "text/plain", 404)
                    return
                route(self, params)
        except PalmError as e:
            self._records(error_body(str(e)))
        except ApiError as e:
            if path.startswith("/p/"):
                self._records(error_body(str(e)[:120]))
            else:
                self._page("Error", f"<p>{html.escape(str(e))}</p>", 502)
        except Exception:
            log.exception("error handling %s", path)
            if path.startswith("/p/"):
                self._records(error_body("Gateway error"))
            else:
                self._page("Error", "<p>Internal error.</p>", 500)

    def _session(self, params):
        session = self.gw.store.session(params.get("k"))
        if not session:
            raise PalmError("Not logged in. Check your key in Prefs.")
        return session

    # -- Palm endpoints ---------------------------------------------------
    def p_ping(self, params):
        self._records(encode_body([["OK", __version__]]))

    def p_login(self, params):
        if not self.gw.allow_password_login:
            raise PalmError("Password login disabled; use the web link page")
        instance = normalize_instance(params.get("inst", ""))
        user, password = params.get("user", "").strip(), params.get("pass", "")
        if not user or not password:
            raise PalmError("Enter user name and password")
        cid, secret = self.gw.oauth_app(instance, "urn:ietf:wg:oauth:2.0:oob")
        token = Client(instance).token_from_password(cid, secret, user, password)
        me = Client(instance, token).verify()
        key = self.gw.store.new_session(instance, token, me["acct"])
        self._records(encode_body([["OK", key, f"{me['acct']}@{instance}"]]))

    def p_me(self, params):
        session = self._session(params)
        me = self.gw.client(session).verify()
        self._records(encode_body([["OK", f"{me['acct']}@{session[0]}",
                                    me.get("display_name") or me["username"]]]))

    def p_tl(self, params):
        session = self._session(params)
        api = self.gw.client(session)
        me_acct = session[2]
        kind = params.get("t", "home")
        max_id = params.get("max") or None
        target = params.get("id", "")
        limit = _int(params.get("n"), 20, 1, 40)
        budget = _int(params.get("b"), 30000, 2000, 60000)
        keyer = self.gw.store.media_key
        focus = 0
        link_next = ""

        if kind == "notif":
            data, headers = api.get("/api/v1/notifications", max_id=max_id, limit=limit)
            items = [(n["id"], notification_fields(n, keyer, me_acct)) for n in data]
            link_next = next_max_id(headers)
        elif kind == "thread":
            if not target:
                raise PalmError("No post selected")
            status, _ = api.get(f"/api/v1/statuses/{target}")
            ctx, _ = api.get(f"/api/v1/statuses/{target}/context")
            ancestors = ctx.get("ancestors") or []
            chain = ancestors + [status] + (ctx.get("descendants") or [])
            focus = len(ancestors)
            items = [(s["id"], status_fields(s, keyer, me_acct)) for s in chain]
        else:
            if kind == "home":
                path, extra = "/api/v1/timelines/home", {}
            elif kind == "local":
                path, extra = "/api/v1/timelines/public", {"local": "true"}
            elif kind == "public":
                path, extra = "/api/v1/timelines/public", {}
            elif kind == "bookmarks":
                path, extra = "/api/v1/bookmarks", {}
            elif kind == "mentions":
                path, extra = "/api/v1/notifications", {"types[]": "mention"}
            elif kind == "user":
                if not target:
                    raise PalmError("No user selected")
                path, extra = f"/api/v1/accounts/{target}/statuses", {}
            else:
                raise PalmError("Unknown timeline")
            data, headers = api.get(path, max_id=max_id, limit=limit, **extra)
            link_next = next_max_id(headers)
            if kind == "mentions":
                items = [(n["id"], notification_fields(n, keyer, me_acct)) for n in data]
            else:
                items = [(s["id"], status_fields(s, keyer, me_acct)) for s in data]

        total = len(items)

        def header(kept):
            if kind == "thread":
                cursor = ""
            elif len(kept) == total and link_next:
                cursor = link_next
            else:
                cursor = kept[-1][0] if kept else ""
            return ["OK", cursor, str(min(focus, max(len(kept) - 1, 0)))]

        self._records(fit_budget(header, items, budget))

    def p_act(self, params):
        session = self._session(params)
        action = ACTIONS.get(params.get("a", ""))
        sid = params.get("id", "")
        if not action or not sid.replace("-", "").isalnum():
            raise PalmError("Bad action")
        status, _ = self.gw.client(session).post(f"/api/v1/statuses/{sid}/{action}")
        if status.get("reblog") and action in ("reblog",):
            status = status["reblog"]
        f = status_fields(status, self.gw.store.media_key, session[2])
        self._records(encode_body([["OK", f[8], f[9]]]))

    def p_post(self, params):
        session = self._session(params)
        text = params.get("text", "").replace("\r\n", "\n").replace("\r", "\n")
        if not text.strip():
            raise PalmError("Nothing to post")
        form = {
            "status": text,
            "visibility": VIS.get(params.get("vis", "p"), "public"),
            "content_type": "text/plain",  # Akkoma/Pleroma: no markdown surprises
        }
        if params.get("cw"):
            form["spoiler_text"] = params["cw"]
        if params.get("reply"):
            form["in_reply_to_id"] = params["reply"]
        status, _ = self.gw.client(session).post("/api/v1/statuses", form)
        self._records(encode_body([["OK", status.get("id", "")]]))

    def p_img(self, params):
        self._session(params)
        key = params.get("m", "")
        w = _int(params.get("w"), 64, 8, 1024)
        h = _int(params.get("h"), 64, 8, 3072)
        bpp = 8 if params.get("bpp") == "8" else 16
        crop = params.get("fit") == "crop"
        density = 72 if params.get("d") == "1" else 144  # d=1: device without hi-res
        cache_key = (key, w, h, bpp, crop, density)
        body = self.gw.images.get(cache_key)
        if body is None:
            urls = self.gw.store.media(key)
            if not urls:
                raise PalmError("Image expired; reload")
            thumb, full = urls
            # Small requests use the server preview, big ones the original.
            url = thumb if (w <= 400 and h <= 400) or not full else full
            body = transcode(_fetch(url), w, h, bpp, crop, density)
            self.gw.images.put(cache_key, body)
        self._send(body, IMAGE_TYPE)

    # -- web pages (desktop browser account linking) ----------------------
    def _page(self, title: str, body: str, status: int = 200):
        page = f"""<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{html.escape(title if title == "PalmFedi gateway" else title + " - PalmFedi gateway")}</title>
<style>body{{font-family:sans-serif;max-width:34em;margin:2em auto;padding:0 1em;line-height:1.5}}
input{{font-size:1em;padding:.3em}} code.key{{font-size:2em;letter-spacing:.15em;
background:#eee;padding:.2em .4em}}</style></head><body><h1>{html.escape(title)}</h1>{body}
</body></html>"""
        self._send(page.encode(), "text/html; charset=utf-8", status)

    def _base_url(self) -> str:
        if self.gw.public_url:
            return self.gw.public_url
        host = self.headers.get("Host") or f"localhost:{self.server.server_address[1]}"
        return f"http://{host}"

    def web_index(self, params):
        self._page("PalmFedi gateway", f"""
<p>Link your Akkoma (or other Mastodon-API) account to your Palm.</p>
<form action="/link/start" method="get">
<label>Instance <input name="instance" placeholder="example.social" required></label>
<button>Log in</button></form>
<p>After logging in you get a 10-letter <b>device key</b>. On the Palm open
<i>Menu &rarr; Options &rarr; Preferences</i>, enter this gateway's address and the key.</p>
<p><small>Gateway {__version__}</small></p>""")

    def web_link_start(self, params):
        instance = normalize_instance(params.get("instance", ""))
        redirect = self._base_url() + "/link/done"
        cid, _secret = self.gw.oauth_app(instance, redirect)
        state = self.gw.store.new_pending(instance, redirect)
        url = Client(instance).authorize_url(cid, redirect, state)
        self.send_response(302)
        self.send_header("Location", url)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def web_link_done(self, params):
        pending = self.gw.store.take_pending(params.get("state", ""))
        if not pending or not params.get("code"):
            self._page("Link failed", "<p>That login link expired. <a href='/'>Try again</a>.</p>", 400)
            return
        instance, redirect = pending
        cid, secret = self.gw.oauth_app(instance, redirect)
        token = Client(instance).token_from_code(cid, secret, redirect, params["code"])
        me = Client(instance, token).verify()
        key = self.gw.store.new_session(instance, token, me["acct"])
        self._page("Linked!", f"""
<p>Logged in as <b>{html.escape(me['acct'])}@{html.escape(instance)}</b>.</p>
<p>Your device key:</p><p><code class="key">{key}</code></p>
<p>Enter it on the Palm in <i>Preferences</i>, together with this gateway's
address <code>{html.escape(self._base_url())}</code>.</p>""")


def _fetch(url: str) -> bytes:
    if not url.startswith(("https://", "http://")):
        raise PalmError("Bad image URL")
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(req, timeout=20) as resp:
            data = resp.read(MAX_SOURCE_BYTES + 1)
    except Exception:
        raise PalmError("Can't download image") from None
    if len(data) > MAX_SOURCE_BYTES:
        raise PalmError("Image too large")
    return data


PALM_ROUTES = {
    ("GET", "/p/ping"): Handler.p_ping,
    ("POST", "/p/login"): Handler.p_login,
    ("GET", "/p/me"): Handler.p_me,
    ("GET", "/p/tl"): Handler.p_tl,
    ("POST", "/p/act"): Handler.p_act,
    ("POST", "/p/post"): Handler.p_post,
    ("GET", "/p/img"): Handler.p_img,
}

WEB_ROUTES = {
    "/": Handler.web_index,
    "/link/start": Handler.web_link_start,
    "/link/done": Handler.web_link_done,
}


def make_server(gw: Gateway, host: str, port: int) -> ThreadingHTTPServer:
    handler = type("BoundHandler", (Handler,), {"gw": gw})
    srv = ThreadingHTTPServer((host, port), handler)
    srv.daemon_threads = True
    return srv
