# PalmFedi design notes

## Target: Palm LifeDrive

| | |
|---|---|
| OS | Palm OS 5.4 (Garnet), 68k apps under PACE |
| CPU | Intel XScale 416 MHz |
| Screen | 320×480 transflective, 16-bit colour, **high density** (160×240 standard coordinates) with a collapsible Dynamic Input Area, and landscape rotation |
| Input | stylus, Graffiti 2, 5-way navigator |
| Network | 802.11b Wi-Fi (WEP/WPA), Bluetooth |
| Storage | 4 GB microdrive; RAM is small and the dynamic heap is a few MB |

So the app should do little parsing, make few large allocations, and use the
tall high-resolution screen well.

## Split of work

| Concern | Gateway (Python) | Palm (C) |
|---|---|---|
| TLS, OAuth, HTTP/2, redirects | ✔ | — |
| JSON → records | ✔ | split on control bytes |
| HTML → text, UTF-8 → cp1252, emoji | ✔ | — |
| Relative times ("5m") | ✔ | — |
| Pagination cursors | ✔ (opaque string) | echoes it back |
| Image decode/scale/dither | ✔ (Pillow) | memcpy into bitmaps |
| Layout, word-wrap, drawing | — | ✔ |

The gateway also enforces a **byte budget** per page (30 KB by default). It
trims overly long posts and drops trailing items when needed, and the
pagination cursor always points just past the last item actually sent.

## Palm app structure

```
main.c       PilotMain, event loop (idle-time image loading via nilEvents)
mainform.c   timeline list: gadget + scrollbar, offscreen double buffer,
             drag-to-scroll, 5-way, auto paging, DIA re-layout
detail.c     single post: full text, 2-column media, alt text, actions
compose.c    new post / reply: CW, visibility, character count, Edit menu
viewer.c     full-screen image viewer (popup form), alt text, prev/next
prefs.c      preferences + password login dialogs
timeline.c   model: pages of records, history stack (Back), paging
render.c     one routine that measures *and* draws a post (so hit-testing
             and scrolling always match the screen)
image.c      PFI1 image download into strip bitmaps + LRU thumbnail cache
net.c        NetLib HTTP/1.0 client, URL/form encoding
proto.c      record parsing (in place, zero copy)
util.c       field helpers, DIA (PINS) support, 5-way key mapping
```

### Memory strategy

* A timeline page is one `MemPtrNew` chunk (≤ 62 KB, in practice ≤ 30 KB).
  Records are split **in place**, and items hold pointers into the page. Only
  the mutable bits (flags, counts) are copied into the item.
* Up to 8 pages / 120 items are kept. Paging further back drops the oldest page.
* Images are streamed into **strips** of at most 16 KB each, so a 320×800
  picture never needs a 500 KB block. Each strip is a `BmpCreate` bitmap plus
  a `BmpCreateBitmapV3(..., kDensityDouble, ...)` wrapper so it draws at
  native resolution with ordinary standard-coordinate calls.
* A 40-slot LRU cache holds thumbnails and avatars (~10 KB each at 16-bit).
  The viewer's full image isn't cached and is freed on close. If an allocation
  fails, the cache is flushed and the load retried once.
* 8-bit mode halves image memory: the gateway dithers to a fixed 256-colour
  palette (6×6×6 cube + 40 greys) that the app attaches as the bitmap colour
  table.

### Screen layout (standard coordinates)

```
┌───────────────────────────────────┐  0
│ PalmFedi                 [Home ▾] │  title + timeline picker
├───────────────────────────────────┤ 15
│ Bob boosted                       │  context line (grey)
│ [av] Alice ✦               5m     │  bold name, time
│      @alice@example.social        │  account (grey) + visibility
│ Hello fediverse from 2005!        │  body (word-wrapped, ≤10 lines in list)
│ ┌──┐┌──┐                          │  36×36 thumbnails (72×72 px)
│ └──┘└──┘                          │
│ re 1   boost 2   fav 3            │  bold + blue when you did it
│ ┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈ │
│ ...                              ▒│  scrollbar
├───────────────────────────────────┤
│ (New) (Reload) (Older)     (Back) │
└───────────────────────────────────┘ 160 / 225 with the input area closed
```

* Tap a post to open the detail view, or tap a thumbnail to open the image
  viewer. Tap a follow notification to see that account's posts.
* Content warnings show `CW: …` plus `[tap to read]`. Sensitive media appear
  as `[2 sensitive media]` until opened.
* In a thread, the post you opened from has a blue bar and is scrolled into view.

### Event loop and loading

All networking is synchronous (NetLib calls block with a 20 s timeout). To keep
the UI responsive while images load, the loop calls `EvtGetEvent` with a
1-tick timeout only while there is image work left. Each `nilEvent` fetches
**one** missing thumbnail or avatar for a visible post and redraws, so the user
can scroll or tap between fetches.

### LifeDrive specifics

* **Dynamic Input Area:** forms set `frmDIAPolicyCustom`, enable the input
  trigger, and declare size constraints (160–225 tall, 160–240 wide). On
  `winDisplayChangedEvent` they resize their window to the display window and
  re-layout the gadget, scrollbar and buttons. Devices without PINS just keep
  160×160.
* **High density:** checked through `sysFtrNumWinVersion ≥ 4`. Without it the
  app asks the gateway for half-size, low-density images (`d=1`).
* **5-way:** handles both `vchrPageUp/Down` / `vchrRocker*` and palmOne's
  `vchrNavChange` bits.

## Gateway structure

```
palmfedi_gateway/
  __main__.py   CLI
  server.py     ThreadingHTTPServer; /p/* Palm endpoints, / and /link/* web pages
  mastodon.py   urllib client: OAuth (code + password grant), API calls, Link headers
  convert.py    status / notification → item record fields
  render.py     HTML flattening, cp1252 transliteration, budgets, framing
  images.py     Pillow pipeline → PFI1 (RGB565 BE or palette-indexed), LRU
  store.py      SQLite: sessions (device keys), OAuth apps, media keys
```

## Possible next steps

* Image attachments from the Palm (photos from the LifeDrive's drive via VFS)
* Polls (voting), emoji reactions (Akkoma), follow/unfollow, search
* Local caching of the last timeline in a PDB for offline reading
* A `fediverse://` exchange-manager hook so other apps can share text
* Notifications polling with the Attention Manager
