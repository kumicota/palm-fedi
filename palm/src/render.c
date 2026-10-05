/* Layout and drawing of a single post. The same routine is used to measure
 * (flags without renderDraw) and to draw, so hit-testing and scrolling always
 * agree with what is on screen. All coordinates are standard (160-wide). */
#include "PalmFedi.h"

#define kListMaxLines   10
#define kGap            2

static IndexedColorType Gray(void)
{
    RGBColorType rgb;
    rgb.index = 0;
    rgb.r = rgb.g = rgb.b = 0x70;
    return WinRGBToIndex(&rgb);
}

static IndexedColorType Accent(void)
{
    RGBColorType rgb;
    rgb.index = 0;
    rgb.r = 0x30; rgb.g = 0x40; rgb.b = 0xC0;   /* Akkoma-ish blue */
    return WinRGBToIndex(&rgb);
}

static FontID BodyFont(void)
{
    return gPrefs.bodyFont ? largeFont : stdFont;
}

/* Draw (or measure) word-wrapped text. Returns the height used. */
static Coord Wrap(const char *text, Coord x, Coord y, Coord w, UInt16 maxLines,
                  Boolean draw, Boolean *truncated)
{
    Coord lh = FntLineHeight();
    UInt16 lines = 0;
    const char *p = text;

    if (truncated)
        *truncated = false;
    while (*p) {
        UInt16 n = FntWordWrap(p, w);
        UInt16 shown;
        if (n == 0)
            n = 1;
        if (maxLines && lines == maxLines) {
            if (truncated)
                *truncated = true;
            break;
        }
        shown = n;
        while (shown && (p[shown - 1] == '\n' || p[shown - 1] == ' '))
            shown--;
        if (draw && shown)
            WinDrawChars(p, shown, x, y + lines * lh);
        lines++;
        p += n;
    }
    return lines * lh;
}

static void DrawRight(const char *s, Coord right, Coord y)
{
    Int16 len = StrLen(s);
    WinDrawChars(s, len, right - FntCharsWidth(s, len), y);
}

static const char *VisLabel(const ItemType *item)
{
    switch (item->f[fVis][0]) {
    case 'u': return " (unlisted)";
    case 'k': return " (followers)";
    case 'd': return " (direct)";
    }
    return "";
}

/* Media grid geometry for an item of width w. */
static void MediaGeometry(Coord w, UInt16 flags, Coord *size, UInt16 *perRow)
{
    if (flags & renderFull) {
        *perRow = 2;
        *size = (w - 4 - kGap) / 2;
    } else {
        *perRow = 4;
        *size = kThumbStd;
        if (4 * (kThumbStd + kGap) > w - 4)
            *size = (w - 4) / 4 - kGap;
    }
}

static Boolean MediaHidden(const ItemType *item, UInt16 flags)
{
    return !(flags & renderFull) && ProtoHasFlag(item, 'S');
}

static void DrawBox(Coord x, Coord y, Coord w, Coord h, const char *label)
{
    RectangleType r;
    Int16 len = StrLen(label);
    RctSetRectangle(&r, x, y, w, h);
    WinDrawRectangleFrame(simpleFrame, &r);
    WinDrawChars(label, len, x + (w - FntCharsWidth(label, len)) / 2,
                 y + (h - FntLineHeight()) / 2);
}

Int16 RenderItem(ItemType *item, Coord x, Coord y, Coord w, UInt16 flags,
                 const RectangleType *clip)
{
    Boolean draw = (flags & renderDraw) != 0;
    Boolean full = (flags & renderFull) != 0;
    Boolean avatars = gPrefs.showAvatars && gPrefs.loadImages && item->f[fAvatar][0];
    Coord left = x + 3, avail = w - 6, cy = y + 2, tx, avatarTop, lh;
    IndexedColorType gray = Gray();
    char buf[48];
    Boolean failed, truncated;

    if (draw) {
        WinPushDrawState();
        WinSetTextColor(UIColorGetTableEntryIndex(UIObjectForeground));
        if (flags & renderFocus) {
            RectangleType bar;
            RctSetRectangle(&bar, x, y, 2, item->height > 0 ? item->height : 20);
            WinSetForeColor(Accent());
            WinDrawRectangle(&bar, 0);
        }
    }

    /* "Bob boosted" / "Carol favourited" */
    FntSetFont(stdFont);
    lh = FntLineHeight();
    if (item->f[fContext][0]) {
        if (draw) {
            WinSetTextColor(item->f[fKind][0] == 'N' ? Accent() : gray);
            WinDrawTruncChars(item->f[fContext], StrLen(item->f[fContext]), left, cy, avail);
        }
        cy += lh;
    }

    /* avatar + name/time + @acct */
    avatarTop = cy;
    tx = left;
    if (avatars) {
        if (draw) {
            PfImage *img = ThumbGet(item->f[fAvatar], kAvatarStd * 2, kAvatarStd * 2, true,
                                    false, &failed);
            if (img)
                ImgDraw(img, left, cy, clip);
            else {
                RectangleType r;
                RctSetRectangle(&r, left, cy, kAvatarStd, kAvatarStd);
                WinSetForeColor(gray);
                WinDrawRectangleFrame(simpleFrame, &r);
            }
        }
        tx += kAvatarStd + 3;
    }
    FntSetFont(stdFont);
    {
        Coord timeW = FntCharsWidth(item->f[fTime], StrLen(item->f[fTime]));
        if (draw) {
            WinSetTextColor(gray);
            DrawRight(item->f[fTime], x + w - 3, cy);
        }
        FntSetFont(boldFont);
        if (draw) {
            WinSetTextColor(UIColorGetTableEntryIndex(UIObjectForeground));
            WinDrawTruncChars(item->f[fName], StrLen(item->f[fName]), tx, cy,
                              left + avail - tx - timeW - 4);
        }
        cy += FntLineHeight();
    }
    FntSetFont(stdFont);
    if (draw) {
        buf[0] = '@';
        StrNCopyZ(buf + 1, item->f[fAcct], sizeof(buf) - 1);
        StrNCatZ(buf, VisLabel(item), sizeof(buf));
        WinSetTextColor(gray);
        WinDrawTruncChars(buf, StrLen(buf), tx, cy, left + avail - tx);
    }
    cy += lh;
    if (avatars && cy < avatarTop + kAvatarStd + 1)
        cy = avatarTop + kAvatarStd + 1;
    cy += 1;

    /* content warning */
    if (item->f[fCW][0]) {
        FntSetFont(boldFont);
        if (draw)
            WinSetTextColor(UIColorGetTableEntryIndex(UIObjectForeground));
        if (draw)
            WinDrawChars("CW:", 3, left, cy);
        cy += Wrap(item->f[fCW], left + FntCharsWidth("CW: ", 4), cy,
                   avail - FntCharsWidth("CW: ", 4), full ? 0 : 3, draw, NULL);
        FntSetFont(stdFont);
        if (!full && !item->showBody) {
            if (draw) {
                WinSetTextColor(Accent());
                WinDrawChars("[tap to read]", 13, left, cy);
            }
            cy += lh;
        }
    }

    /* body */
    if (full || item->showBody) {
        FntSetFont(BodyFont());
        if (draw)
            WinSetTextColor(UIColorGetTableEntryIndex(UIObjectForeground));
        cy += Wrap(item->f[fText], left, cy, avail, full ? 0 : kListMaxLines, draw, &truncated);
        FntSetFont(stdFont);
        if (truncated) {
            if (draw) {
                WinSetTextColor(Accent());
                WinDrawChars("more...", 7, left, cy);
            }
            cy += lh;
        }
    }

    /* media */
    item->thumbY = -1;
    if (item->numMedia) {
        cy += 2;
        item->thumbY = cy - y;
        if (gPrefs.loadImages && !MediaHidden(item, flags)) {
            Coord size;
            UInt16 perRow, i;
            MediaGeometry(w, flags, &size, &perRow);
            for (i = 0; i < item->numMedia; i++) {
                Coord mx = left + (i % perRow) * (size + kGap);
                Coord my = cy + (i / perRow) * (size + kGap);
                if (draw) {
                    PfImage *img = ThumbGet(item->mediaKey[i], size * 2, size * 2, true,
                                            false, &failed);
                    if (img) {
                        ImgDraw(img, mx + (size - (Coord)img->stdW) / 2,
                                my + (size - (Coord)img->stdH) / 2, clip);
                        if (item->mediaType[i] == 'V' || item->mediaType[i] == 'A') {
                            WinSetTextColor(UIColorGetTableEntryIndex(UIObjectForeground));
                            WinDrawInvertedChars(item->mediaType[i] == 'V' ? "VID" : "AUD", 3,
                                                 mx + 1, my + size - lh);
                        }
                    } else {
                        WinSetForeColor(gray);
                        WinSetTextColor(gray);
                        DrawBox(mx, my, size, size, failed ? "x" :
                                item->mediaType[i] == 'V' ? "VID" :
                                item->mediaType[i] == 'A' ? "AUD" : "...");
                    }
                }
            }
            cy += ((item->numMedia + perRow - 1) / perRow) * (size + kGap);
        } else {
            if (draw) {
                StrCopy(buf, "[");
                StrIToA(buf + 1, item->numMedia);
                StrCat(buf, ProtoHasFlag(item, 'S') ? " sensitive media]" :
                            item->numMedia == 1 ? " attachment]" : " attachments]");
                WinSetTextColor(Accent());
                WinDrawChars(buf, StrLen(buf), left, cy);
            }
            cy += lh;
        }
        if (full) {
            UInt16 i;
            for (i = 0; i < item->numMedia; i++) {
                if (!item->mediaAlt[i][0])
                    continue;
                if (draw) {
                    WinSetTextColor(gray);
                    StrCopy(buf, "Alt ");
                    StrIToA(buf + 4, i + 1);
                    StrCat(buf, ":");
                    WinDrawChars(buf, StrLen(buf), left, cy);
                }
                cy += Wrap(item->mediaAlt[i], left + 30, cy, avail - 30, 0, draw, NULL);
            }
        }
    }

    /* footer: counts */
    if (item->f[fKind][0] == 'S') {
        char *p = item->counts;
        static const char *labels[3] = { "re ", "boost ", "fav " };
        static const char marks[3] = { 0, 'B', 'F' };
        UInt16 k;
        Coord fx = left;
        cy += 2;
        for (k = 0; k < 3; k++) {
            char num[8];
            UInt16 n = 0;
            Boolean on = marks[k] && ProtoHasFlag(item, marks[k]);
            while (*p == ' ')
                p++;
            while (*p && *p != ' ' && n < sizeof(num) - 1)
                num[n++] = *p++;
            num[n] = 0;
            StrCopy(buf, labels[k]);
            StrCat(buf, num);
            FntSetFont(on ? boldFont : stdFont);
            if (draw) {
                WinSetTextColor(on ? Accent() : gray);
                WinDrawChars(buf, StrLen(buf), fx, cy);
            }
            fx += FntCharsWidth(buf, StrLen(buf)) + 8;
        }
        FntSetFont(stdFont);
        if (ProtoHasFlag(item, 'K') && draw) {
            WinSetTextColor(Accent());
            DrawRight("saved", x + w - 3, cy);
        }
        cy += lh;
    }

    cy += 2;
    if (draw) {
        WinSetForeColor(gray);
        WinDrawGrayLine(x, cy, x + w - 1, cy);
        WinPopDrawState();
    }
    cy += 1;
    FntSetFont(stdFont);
    return cy - y;
}

/* Which media thumbnail is at (dx, dy) relative to the item's top-left?
 * Returns -1 if none. Requires a prior RenderItem with the same width. */
Int16 RenderMediaHit(ItemType *item, Coord w, UInt16 flags, Coord dx, Coord dy)
{
    Coord size, rel;
    UInt16 perRow, col, row, idx;

    if (item->thumbY < 0 || dy < item->thumbY || !item->numMedia)
        return -1;
    if (!gPrefs.loadImages || MediaHidden(item, flags)) {
        FntSetFont(stdFont);
        return dy < item->thumbY + FntLineHeight() ? 0 : -1;
    }
    MediaGeometry(w, flags, &size, &perRow);
    rel = dy - item->thumbY;
    dx -= 3;
    if (dx < 0)
        return -1;
    col = dx / (size + kGap);
    row = rel / (size + kGap);
    if (col >= perRow)
        return -1;
    idx = row * perRow + col;
    return idx < item->numMedia ? (Int16)idx : -1;
}

/* Fetch at most one missing image this item needs. Returns true if it did
 * network work (caller should redraw and keep idling). */
Boolean RenderFetchOne(ItemType *item, Coord w, UInt16 flags)
{
    Boolean failed;
    UInt16 i;

    if (!gPrefs.loadImages)
        return false;
    if (gPrefs.showAvatars && item->f[fAvatar][0] &&
        !ThumbGet(item->f[fAvatar], kAvatarStd * 2, kAvatarStd * 2, true, false, &failed) &&
        !failed) {
        ThumbGet(item->f[fAvatar], kAvatarStd * 2, kAvatarStd * 2, true, true, &failed);
        return true;
    }
    if (MediaHidden(item, flags))
        return false;
    for (i = 0; i < item->numMedia; i++) {
        Coord size;
        UInt16 perRow;
        MediaGeometry(w, flags, &size, &perRow);
        if (!ThumbGet(item->mediaKey[i], size * 2, size * 2, true, false, &failed) && !failed) {
            ThumbGet(item->mediaKey[i], size * 2, size * 2, true, true, &failed);
            return true;
        }
    }
    return false;
}

void RenderMessage(const RectangleType *r, const char *msg)
{
    Int16 len = StrLen(msg);
    WinEraseRectangle(r, 0);
    FntSetFont(stdFont);
    WinDrawChars(msg, len, r->topLeft.x + (r->extent.x - FntCharsWidth(msg, len)) / 2,
                 r->topLeft.y + r->extent.y / 3);
}
