"""Minimal Mastodon-API client (works with Akkoma/Pleroma and Mastodon)."""

from __future__ import annotations

import json
import re
import urllib.error
import urllib.parse
import urllib.request

USER_AGENT = "PalmFediGateway/1.0 (+Palm OS 5 client gateway)"
SCOPES = "read write follow"


class ApiError(Exception):
    def __init__(self, message: str, status: int = 0):
        super().__init__(message)
        self.status = status


def normalize_instance(instance: str) -> str:
    instance = instance.strip().lower()
    instance = re.sub(r"^https?://", "", instance).strip("/")
    if not instance or not re.fullmatch(r"[a-z0-9.\-]+(:\d+)?", instance):
        raise ApiError("Bad instance name")
    return instance


class Client:
    def __init__(self, instance: str, token: str | None = None, timeout: float = 20):
        self.base = "https://" + normalize_instance(instance)
        self.token = token
        self.timeout = timeout

    def request(self, method: str, path: str, params: dict | None = None,
                form: dict | None = None):
        """Return (decoded_json, response_headers)."""
        url = self.base + path
        if params:
            clean = {k: v for k, v in params.items() if v not in (None, "")}
            if clean:
                url += "?" + urllib.parse.urlencode(clean, doseq=True)
        data = None
        headers = {"User-Agent": USER_AGENT, "Accept": "application/json"}
        if form is not None:
            data = urllib.parse.urlencode({k: v for k, v in form.items() if v is not None},
                                          doseq=True).encode()
            headers["Content-Type"] = "application/x-www-form-urlencoded"
        if self.token:
            headers["Authorization"] = "Bearer " + self.token
        req = urllib.request.Request(url, data=data, headers=headers, method=method)
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                body = resp.read()
                return (json.loads(body) if body else None), resp.headers
        except urllib.error.HTTPError as e:
            msg = f"HTTP {e.code}"
            try:
                err = json.loads(e.read())
                msg = err.get("error_description") or err.get("error") or msg
            except Exception:
                pass
            raise ApiError(str(msg), e.code) from None
        except (urllib.error.URLError, TimeoutError, OSError) as e:
            raise ApiError(f"Can't reach {self.base[8:]}: {getattr(e, 'reason', e)}") from None

    def get(self, path, **params):
        return self.request("GET", path, params=params)

    def post(self, path, form=None, **params):
        return self.request("POST", path, params=params, form=form or {})

    # -- OAuth ------------------------------------------------------------
    def register_app(self, redirect_uri: str):
        data, _ = self.post("/api/v1/apps", {
            "client_name": "PalmFedi", "redirect_uris": redirect_uri,
            "scopes": SCOPES, "website": "https://github.com/mateusfmcota/palm-fedi",
        })
        return data["client_id"], data["client_secret"]

    def authorize_url(self, client_id: str, redirect_uri: str, state: str) -> str:
        q = urllib.parse.urlencode({"client_id": client_id, "redirect_uri": redirect_uri,
                                    "response_type": "code", "scope": SCOPES, "state": state})
        return f"{self.base}/oauth/authorize?{q}"

    def token_from_code(self, client_id, client_secret, redirect_uri, code) -> str:
        data, _ = self.post("/oauth/token", {
            "grant_type": "authorization_code", "code": code, "client_id": client_id,
            "client_secret": client_secret, "redirect_uri": redirect_uri, "scope": SCOPES,
        })
        return data["access_token"]

    def token_from_password(self, client_id, client_secret, username, password) -> str:
        # Akkoma/Pleroma support the password grant; Mastodon does not.
        data, _ = self.post("/oauth/token", {
            "grant_type": "password", "username": username, "password": password,
            "client_id": client_id, "client_secret": client_secret, "scope": SCOPES,
        })
        return data["access_token"]

    def verify(self) -> dict:
        data, _ = self.get("/api/v1/accounts/verify_credentials")
        return data


_LINK_NEXT = re.compile(r'<([^>]+)>;\s*rel="next"')


def next_max_id(headers) -> str:
    """Extract max_id from a Link: <...>; rel="next" header, if any."""
    link = headers.get("Link") if headers else None
    if not link:
        return ""
    m = _LINK_NEXT.search(link)
    if not m:
        return ""
    q = urllib.parse.parse_qs(urllib.parse.urlparse(m.group(1)).query)
    return (q.get("max_id") or [""])[0]
