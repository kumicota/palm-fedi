# Palm ⇄ Gateway wire protocol (v1)

The Palm talks to the gateway over **plain HTTP/1.0** (Palm OS 5 cannot do
modern TLS). The gateway talks HTTPS + the Mastodon/Akkoma API upstream.

Everything is designed so a 68k C program can parse it with a handful of
pointer walks: no JSON, no HTML, no UTF-8.

## Text encoding

All text sent to the Palm is **Windows-1252** (Palm OS 5 "Palm Latin" is a
superset for the printable range). Characters that cannot be represented are
transliterated (`“` stays, `❤` → `<3`, unknown emoji → `*`, accents via NFKD,
otherwise `?`). Text the Palm sends (POST bodies) is form-urlencoded
Windows-1252.

Newlines inside fields are `\n` (0x0A), which is what Palm OS fields use.

## Record responses

`Content-Type: application/x-palmfedi`

```
record  := field (US field)*
body    := record (RS record)*
US = 0x1F   RS = 0x1E   GS = 0x1D (list separator inside a field)
```

No field ever contains US, RS or GS (the gateway strips them).

The **first record is always a header**:

| header             | meaning                                                  |
|--------------------|----------------------------------------------------------|
| `OK` US a US b ... | success, extra fields depend on the endpoint             |
| `ERR` US message   | failure; message is short and human readable             |

The gateway also keeps every response under the byte budget `b` the client
asks for (default 30000) by truncating long posts and dropping trailing
items, so the client can hold it in a single `MemPtrNew` chunk.

### Item record (timeline / notifications / thread / user)

| # | field     | notes                                                             |
|---|-----------|-------------------------------------------------------------------|
| 0 | kind      | `S` status, `N` notification without status (e.g. follow)        |
| 1 | status id | empty for `N`                                                     |
| 2 | name      | author display name                                               |
| 3 | acct      | `user@host` (or `user` for local)                                 |
| 4 | time      | short relative time: `now`, `5m`, `3h`, `2d`, `Mar 3`             |
| 5 | text      | plain text body (HTML flattened, links shown as their text)       |
| 6 | cw        | content warning / subject, empty if none                          |
| 7 | context   | e.g. `Bob boosted`, `Bob favourited your post`, `Bob followed you`|
| 8 | flags     | any of `F` favourited `B` boosted `K` bookmarked `S` sensitive media `R` is a reply `M` mine |
| 9 | counts    | `replies boosts favs` (space separated)                           |
|10 | media     | GS-separated `T:key` entries, T = `I` image, `V` video, `A` audio, `U` other |
|11 | alts      | GS-separated alt texts, same order as media                       |
|12 | avatar    | media key of the author avatar                                    |
|13 | vis       | `p` public, `u` unlisted, `k` followers-only, `d` direct          |
|14 | mentions  | reply prefix, e.g. `@alice@x.y @bob ` (excludes yourself)         |
|15 | acct id   | author account id (for the profile timeline)                      |

Clients must ignore extra trailing fields (forward compatibility).

## Endpoints

All Palm endpoints live under `/p/`. Authenticated ones take the device key
as `k=` (query string or form body).

| method | path        | params                                    | OK header fields               |
|--------|-------------|-------------------------------------------|--------------------------------|
| GET    | `/p/ping`   |                                           | gateway version                |
| POST   | `/p/login`  | `inst`, `user`, `pass`                    | key, acct                      |
| GET    | `/p/me`     | `k`                                       | acct, display name             |
| GET    | `/p/tl`     | `k`, `t`, `max`, `id`, `n`, `b`           | next cursor, focus index       |
| POST   | `/p/act`    | `k`, `id`, `a`                            | flags, counts                  |
| POST   | `/p/post`   | `k`, `text`, `cw`, `vis`, `reply`         | new status id                  |
| GET    | `/p/img`    | `k`, `m`, `w`, `h`, `bpp`, `fit`, `d`     | *(binary, see below)*          |

`t` (timeline kind): `home`, `local`, `public`, `notif`, `thread` (needs
`id` = status id), `user` (needs `id` = account id), `bookmarks`, `mentions`.

`max` is the opaque cursor returned in the previous page's header (empty = newest).
For `thread`, the focus index is the 0-based index of the requested status
inside the returned items (ancestors come first).

`a` (action): `fav`, `unfav`, `boost`, `unboost`, `bm`, `unbm`.

`vis`: `p`, `u`, `k`, `d` as above.

## Image format

`GET /p/img` returns `Content-Type: application/x-palmfedi-image`:

```
offset size  field
0      4     magic "PFI1"
4      2     width  (pixels, big endian)
6      2     height (pixels, big endian)
8      1     bpp    (16 or 8)
9      1     density (72 = low, 144 = double)
10     2     rowBytes (big endian, always even)
12     ...   height * rowBytes of pixel data, top row first
```

* 16 bpp: RGB565, big endian (matches `BmpCreate(...,16,...)` on Palm OS 5).
* 8 bpp: indices into the fixed PalmFedi palette (6×6×6 cube at
  `r*36+g*6+b` using levels 0,51,102,153,204,255, then 40 greys); the client
  attaches the same colour table to its bitmaps.

`w`/`h` are the **maximum** native-pixel size; `fit=crop` center-crops to
exactly `w`×`h` (used for square thumbnails), `fit=fit` (default) keeps the
aspect ratio. `d=1` asks for a low-density (72) image, used by
devices without a high-density screen. Errors are returned as a normal `ERR` record response.

On the device the image is streamed into a column of strip bitmaps of at most
~30 KB each, so large images never need one big allocation.
