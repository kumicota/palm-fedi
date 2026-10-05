"""SQLite persistence: device sessions, OAuth apps and media keys."""

from __future__ import annotations

import base64
import hashlib
import secrets
import sqlite3
import threading
import time

# No 0/O/1/I/L so keys are easy to type with Graffiti.
_KEY_ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789"

SCHEMA = """
CREATE TABLE IF NOT EXISTS sessions (
    key TEXT PRIMARY KEY,
    instance TEXT NOT NULL,
    token TEXT NOT NULL,
    acct TEXT NOT NULL,
    created REAL NOT NULL
);
CREATE TABLE IF NOT EXISTS apps (
    instance TEXT NOT NULL,
    redirect TEXT NOT NULL,
    client_id TEXT NOT NULL,
    client_secret TEXT NOT NULL,
    PRIMARY KEY (instance, redirect)
);
CREATE TABLE IF NOT EXISTS media (
    key TEXT PRIMARY KEY,
    thumb TEXT NOT NULL,
    full TEXT NOT NULL,
    seen REAL NOT NULL
);
CREATE TABLE IF NOT EXISTS pending (
    state TEXT PRIMARY KEY,
    instance TEXT NOT NULL,
    redirect TEXT NOT NULL,
    created REAL NOT NULL
);
"""


class Store:
    def __init__(self, path: str):
        self._db = sqlite3.connect(path, check_same_thread=False, isolation_level=None)
        self._db.execute("PRAGMA journal_mode=WAL")
        self._db.executescript(SCHEMA)
        self._lock = threading.Lock()

    def _q(self, sql: str, args=()):
        with self._lock:
            return self._db.execute(sql, args).fetchall()

    # -- sessions ---------------------------------------------------------
    def new_session(self, instance: str, token: str, acct: str) -> str:
        key = "".join(secrets.choice(_KEY_ALPHABET) for _ in range(10))
        self._q("INSERT INTO sessions VALUES (?,?,?,?,?)", (key, instance, token, acct, time.time()))
        return key

    def session(self, key: str | None):
        if not key:
            return None
        rows = self._q("SELECT instance, token, acct FROM sessions WHERE key=?", (key.strip().upper(),))
        return rows[0] if rows else None

    def drop_session(self, key: str) -> None:
        self._q("DELETE FROM sessions WHERE key=?", (key,))

    # -- oauth apps -------------------------------------------------------
    def app(self, instance: str, redirect: str):
        rows = self._q("SELECT client_id, client_secret FROM apps WHERE instance=? AND redirect=?",
                       (instance, redirect))
        return rows[0] if rows else None

    def save_app(self, instance: str, redirect: str, client_id: str, client_secret: str) -> None:
        self._q("INSERT OR REPLACE INTO apps VALUES (?,?,?,?)", (instance, redirect, client_id, client_secret))

    def new_pending(self, instance: str, redirect: str) -> str:
        state = secrets.token_urlsafe(16)
        self._q("DELETE FROM pending WHERE created < ?", (time.time() - 3600,))
        self._q("INSERT INTO pending VALUES (?,?,?,?)", (state, instance, redirect, time.time()))
        return state

    def take_pending(self, state: str):
        rows = self._q("SELECT instance, redirect FROM pending WHERE state=? AND created > ?",
                       (state, time.time() - 3600))
        self._q("DELETE FROM pending WHERE state=?", (state,))
        return rows[0] if rows else None

    # -- media keys -------------------------------------------------------
    def media_key(self, thumb: str | None, full: str | None) -> str:
        """Short stable key for a media URL pair (keeps URLs off the Palm and
        stops the image endpoint from being an open proxy)."""
        thumb = thumb or full or ""
        full = full or thumb
        digest = hashlib.sha1((thumb + "\n" + full).encode()).digest()
        key = base64.b32encode(digest[:10]).decode().rstrip("=").lower()
        self._q("INSERT OR REPLACE INTO media VALUES (?,?,?,?)", (key, thumb, full, time.time()))
        return key

    def media(self, key: str):
        rows = self._q("SELECT thumb, full FROM media WHERE key=?", (key,))
        return rows[0] if rows else None

    def prune_media(self, max_age_days: float = 30) -> None:
        self._q("DELETE FROM media WHERE seen < ?", (time.time() - max_age_days * 86400,))
