"""Command line entry point: ``python -m palmfedi_gateway``."""

import argparse
import logging
import os

from .server import Gateway, make_server
from .store import Store


def main() -> None:
    ap = argparse.ArgumentParser(description="PalmFedi gateway for Palm OS 5 clients")
    ap.add_argument("--host", default=os.environ.get("PALMFEDI_HOST", "0.0.0.0"))
    ap.add_argument("--port", type=int, default=int(os.environ.get("PALMFEDI_PORT", "8080")))
    ap.add_argument("--db", default=os.environ.get("PALMFEDI_DB", "palmfedi.sqlite3"),
                    help="SQLite file holding device keys and OAuth tokens")
    ap.add_argument("--public-url", default=os.environ.get("PALMFEDI_PUBLIC_URL"),
                    help="Base URL browsers use to reach the gateway (for OAuth redirects)")
    ap.add_argument("--image-cache-mb", type=int, default=64)
    ap.add_argument("--no-password-login", action="store_true",
                    help="Only allow linking through the web page (OAuth)")
    args = ap.parse_args()

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    store = Store(args.db)
    store.prune_media()
    gw = Gateway(store, args.public_url, args.image_cache_mb,
                 allow_password_login=not args.no_password_login)
    srv = make_server(gw, args.host, args.port)
    logging.info("PalmFedi gateway listening on http://%s:%d", args.host, args.port)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
