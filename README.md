# PalmFedi

A fediverse client for **Palm OS 5**, built for the **Palm LifeDrive**, that
talks to **Akkoma** (and Pleroma or Mastodon, since it uses the Mastodon API),
with image support.

```
 ┌──────────────┐  plain HTTP/1.0, Wi-Fi   ┌──────────────────┐  HTTPS + Mastodon API  ┌──────────┐
 │ Palm LifeDrive│ ───────────────────────▶ │ PalmFedi gateway │ ─────────────────────▶ │  Akkoma  │
 │  PalmFedi.prc │ ◀─────────────────────── │ (Python, your PC │ ◀───────────────────── │ instance │
 └──────────────┘ tiny text records +      │  / server / Pi)  │  JSON, HTML, JPEG/PNG  └──────────┘
                  ready-to-blit bitmaps    └──────────────────┘
```

## Why a gateway?

A 2005 Palm can't do today's fediverse directly:

* **No modern TLS.** Palm OS 5's network stack can't negotiate TLS 1.2/1.3,
  and every instance requires HTTPS.
* **JSON + HTML + UTF-8 + JPEG is heavy** for a 68k app running under PACE
  with a few MB of dynamic heap.

The gateway does the heavy lifting: OAuth and HTTPS upstream; posts flattened
to plain Windows-1252 text in a tiny record format; images decoded, scaled and
sent as RGB565 bitmaps that the Palm just copies into memory and draws. See
[`docs/PROTOCOL.md`](docs/PROTOCOL.md).

## Features

**Palm app**
* Home, Local, Federated, Notifications, Mentions and Bookmarks timelines
* Thread view (with the opened post highlighted) and author profile timelines
* Post detail with reply, boost, favourite, bookmark and copy text
* **Polls:** results with bars, or tick the options and vote in the post view
* **Follow / unfollow:** profiles start with a header (bio, fields, counts,
  "Follows you"); open it for the Follow / Unfollow button, or use
  *Post → Follow / Unfollow* on any post
* **Search** (*View → Search*, or "Search..." in the timeline picker): people,
  hashtags and posts; `@user@host` or a post URL finds remote ones, and
  `#tag` opens that hashtag's timeline
* Compose and reply with content warning and visibility (public, unlisted,
  followers, direct); your mentions are pre-filled on replies
* **Images:** thumbnails in the list, larger previews in the detail view, and a
  full-screen viewer with alt text. Images use the LifeDrive's 320×480
  high-density screen at full resolution. Video and audio show their preview frame.
* Avatars (optional), content-warning folding, sensitive media hidden until opened
* LifeDrive niceties: collapsible input area for a taller timeline (160×225),
  landscape rotation, 5-way navigator (up/down moves a highlight from post to
  post, paging through long posts; center opens the highlighted post; left
  goes back), drag-to-scroll with the stylus, "Older" paging and automatic
  paging when you move past the end
* Light on memory: images are cached within a 200 KB budget, the post view's
  big previews are freed when you leave it, and *Options → Free memory*
  drops all cached images and shows how much of the heap is free.
  *Options → Exit* quits to the launcher and turns the network off
* 16-bit or 8-bit images (8-bit uses half the RAM), normal or large text

**Gateway**
* Pure Python 3 standard library + Pillow, one process, SQLite for state
* Optional: `ffmpeg` on the PATH gives video previews (Akkoma doesn't make
  them) and decodes images your Pillow can't (e.g. AVIF on older Pillow);
  `pip install ".[formats]"` adds AVIF/HEIC support through Pillow plugins
* Two ways to log in: a **web page** (OAuth in a desktop browser, then type a
  10-letter device key on the Palm), or **username/password on the Palm**
  (Akkoma and Pleroma only; Mastodon doesn't allow the password grant)
* Image proxy that only serves media from posts it has handed out (it can't
  be used as an open proxy), with an in-memory cache
* Device keys and passwords are never logged

## Quick start

### 1. Run the gateway

On any machine your Palm can reach over Wi-Fi (PC, home server, Raspberry Pi):

```sh
cd gateway
pip install .
palmfedi-gateway --port 8080          # or: python -m palmfedi_gateway
```

or with Docker:

```sh
docker build -t palmfedi-gateway gateway        # add --build-arg WITH_FFMPEG=1 for video previews
docker run -d -p 8080:8080 -v palmfedi:/data palmfedi-gateway
```

Options: `--db PATH` (SQLite file with tokens, keep it private),
`--public-url http://192.168.1.10:8080` (the address browsers use, needed for
OAuth redirects if it differs from the `Host` header), `--image-cache-mb`, and
`--no-password-login`.

### 2. Link your account

Either open `http://<gateway>:8080/` in a desktop browser, enter your instance,
approve the app, and note the **device key**, or skip this and log in from
the Palm (step 4).

### 3. Install the app

Install `dist/PalmFedi.prc` with HotSync (Palm Desktop's Install Tool), or
copy it to the SD card / LifeDrive drive and use the Launcher's **Copy**.

### 4. Configure

Open PalmFedi, **Menu → Options → Preferences**:

* **Gateway:** the gateway's IP or hostname, **Port:** 8080
* **Key:** the device key from step 2, *or* tap **Log in** and enter your
  instance, user name and password
* Tap **Test** to check, then **OK**.

## Security notes

The Palm↔gateway link is plain HTTP, because the Palm has no usable TLS. So:

* Run the gateway on your **home network** (or reach it over a VPN), not on the
  open internet.
* The device key acts as a password for your account through the gateway.
  Revoke it by deleting the row from the `sessions` table, or revoke the
  "PalmFedi" app in your Akkoma settings.
* Prefer the web-page login: then your password never crosses the plain-HTTP
  hop. Start the gateway with `--no-password-login` to require it.

## Repository layout

```
gateway/            Python gateway (palmfedi_gateway package + tests)
palm/src/           Palm OS C sources
palm/rsc/           PilRC resources (forms, menus, alerts, icons)
palm/tests/         host-side unit tests for the C parser/encoder
dist/PalmFedi.prc   prebuilt app
docs/PROTOCOL.md    Palm ⇄ gateway wire format
docs/DESIGN.md      design notes (UI, memory, LifeDrive specifics)
docs/BUILDING.md    how to build the PRC with prc-tools-remix
```

## Development

```sh
cd gateway && pip install -e ".[test]" && pytest     # gateway tests
make -C palm/tests                                   # C parser tests on the host
make -C palm                                         # build PalmFedi.prc (see docs/BUILDING.md)
```
