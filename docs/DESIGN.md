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
mainform.c   timeline list: gadget + scrollbar, offscreen double buffer
             with partial redraws, drag-to-scroll, 5-way highlight, auto
             paging, DIA re-layout, Free memory
detail.c     single post: full text, 2-column media, alt text, actions,
             poll voting, profile follow button
compose.c    new post / reply: CW, visibility, character count, Edit menu
viewer.c     full-screen image viewer (popup form), alt text, prev/next
prefs.c      preferences + password login dialogs
search.c     search dialog (results are an ordinary timeline view)
account.c    follow / unfollow (relationship flags, confirmation)
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
* Up to 6 pages / 100 items are kept. Paging further back drops the oldest page.
* Images are streamed into **strips** of at most 16 KB each, so a 320×800
  picture never needs a 500 KB block. Each strip is a `BmpCreate` bitmap plus
  a `BmpCreateBitmapV3(..., kDensityDouble, ...)` wrapper so it draws at
  native resolution with ordinary standard-coordinate calls.
* An LRU cache of up to 40 thumbnails and avatars (~10 KB each at 16-bit) is
  kept within a 200 KB pixel budget. Images drawn in the last few lookups are
  never evicted (that would make the idle loader fetch them again forever),
  so a screen full of images can briefly go over budget. The post view's big
  previews are dropped when it closes, and the viewer's full image isn't
  cached at all. If an allocation fails, the cache is flushed and the load
  retried once.
* *Free memory* flushes the cache and the drawing buffer and reports the
  dynamic heap; *Exit* quits and closes NetLib at once instead of letting the
  connection linger.
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
  viewer. Tap a follow notification (or an account in search results) to see
  that account's posts; the profile item at the top opens the profile with a
  Follow / Unfollow button. Tap a hashtag result for its timeline.
* Polls show tick boxes (round for single choice) until you vote; in the
  detail view tap options, then **Vote**. Afterwards they show percentages
  and bars, your choices in bold. The vote reply carries the new results,
  which the item keeps in its own small buffer (the page stays read-only).
* Content warnings show `CW: …` plus `[tap to read]`. Sensitive media appear
  as `[2 sensitive media]` until opened.
* In a thread, the post you opened from has a blue bar and is scrolled into view.
* The 5-way highlight is a tinted background with a blue frame. Up/down move
  it one post; a post taller than the screen is scrolled a page at a time
  before the highlight moves on. If the highlight has scrolled off screen,
  the first press picks a post on screen. The post you opened stays
  highlighted when you come back.

### Drawing

The list is drawn into an offscreen window and copied to the screen.
Scrolling shifts the pixels already there (`WinScrollRectangle`) and draws
only the strip that came into view; a newly loaded image or a highlight
change repaints just that post. Only a layout change (new page, resize,
expanded CW) redraws everything. Text lines outside the clip are measured
but not drawn, colour indexes are looked up once, and the post view
re-measures its text only when the post changes, not on every scroll step.

### Event loop and loading

All networking is synchronous (NetLib calls block with a 20 s timeout). To keep
the UI responsive while images load, the loop calls `EvtGetEvent` with a
1-tick timeout only while there is image work left. Each `nilEvent` fetches
**one** missing thumbnail or avatar for a visible post and repaints that post,
so the user can scroll or tap between fetches.

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
* Emoji reactions (Akkoma), paging through search results
* Local caching of the last timeline in a PDB for offline reading
* A `fediverse://` exchange-manager hook so other apps can share text
* Notifications polling with the Attention Manager
