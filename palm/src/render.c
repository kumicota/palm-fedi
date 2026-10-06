/* Layout and drawing of a single post. The same routine is used to measure
 * (flags without renderDraw) and to draw, so hit-testing and scrolling always
 * agree with what is on screen. All coordinates are standard (160-wide). */
#include "PalmFedi.h"

#define kListMaxLines   10
#define kGap            2
#define kPollRowGap     3
#define kPollBox        7

/* Tap targets of the poll from the last renderFull pass, relative to the
 * item's top-left (only set while the poll can be voted on). */
static ItemType *gPollItem = NULL;
static Int16 gPollRowTop[kMaxPollOptions + 1];
static Int16 gPollVoteTop, gPollVoteBottom, gPollVoteRight;

/* Rows that can be seen while drawing (window coordinates); lines outside
 * are measured but not drawn. */
static Coord gClipTop = -32767, gClipBottom = 32767;

/* Colour lookups are cached: WinRGBToIndex searches the whole palette. */
static IndexedColorType gGray, gAccent, gTint;
static Boolean gColorsReady = false;

static IndexedColorType RGBIndex(UInt8 r, UInt8 g, UInt8 b)
{
    RGBColorType rgb;
    rgb.index = 0;
    rgb.r = r;
    rgb.g = g;
    rgb.b = b;
    return WinRGBToIndex(&rgb);
}

static void InitColors(void)
{
    if (gColorsReady)
        return;
    gGray = RGBIndex(0x70, 0x70, 0x70);
    gAccent = RGBIndex(0x30, 0x40, 0xC0);   /* Akkoma-ish blue */
    gTint = RGBIndex(0xDC, 0xE6, 0xFF);     /* 5-way highlight background */
    gColorsReady = true;
}

static IndexedColorType Gray(void)
{
    InitColors();
    return gGray;
}

static IndexedColorType Accent(void)
{
    InitColors();
    return gAccent;
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
        if (draw && shown) {
            Coord ly = y + lines * lh;
            if (ly + lh > gClipTop && ly < gClipBottom)
                WinDrawChars(p, shown, x, ly);
        }
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

Boolean RenderPollCanVote(const ItemType *item)
{
    return item->pollParts && !ProtoPollHas(item, 'V') && !ProtoPollHas(item, 'X') &&
           !ProtoHasFlag(item, 'M');  /* servers don't let you vote in your own poll */
}

/* Split "*42:Title" into own-vote mark, percentage (-1 = hidden) and title. */
static const char *PollOption(const char *opt, Boolean *own, Int16 *pct)
{
    const char *p = opt;
    *own = (*p == '*');
    *pct = -1;
    if (*p)
        p++;
    if (*p >= '0' && *p <= '9') {
        *pct = 0;
        while (*p >= '0' && *p <= '9')
            *pct = *pct * 10 + (*p++ - '0');
        if (*pct > 100)
            *pct = 100;
    }
    while (*p && *p != ':')
        p++;
    return *p ? p + 1 : p;
}

/* Poll: results with bars once you voted (or it closed), otherwise tick
 * boxes; in the detail view also a Vote button. Returns the height used. */
static Coord DrawPoll(ItemType *item, Coord x, Coord y, const PointType *origin, Coord w,
                      UInt16 flags, IndexedColorType gray)
{
    Boolean draw = (flags & renderDraw) != 0;
    Boolean full = (flags & renderFull) != 0;
    Boolean vote = RenderPollCanVote(item);
    Boolean multi = ProtoPollHas(item, 'M');
    UInt16 i, n = ProtoPollOptions(item);
    Coord cy = y, lh = FntLineHeight(), top = origin->y;
    const char *summary;

    if (full && vote)
        gPollItem = item;
    for (i = 0; i < n; i++) {
        Boolean own;
        Int16 pct;
        const char *title = PollOption(ProtoPollPart(item, pollFirstOption + i), &own, &pct);
        UInt16 len = StrLen(title);

        if (full)
            gPollRowTop[i] = cy - top;
        if (!vote) {
            char label[6];
            Coord lw = 0, h = lh;
            label[0] = 0;
            if (pct >= 0) {
                StrIToA(label, pct);
                StrCat(label, "%");
            }
            FntSetFont(own ? boldFont : stdFont);
            lw = FntCharsWidth(label, StrLen(label));
            if (draw) {
                WinSetTextColor(own ? Accent() : UIColorGetTableEntryIndex(UIObjectForeground));
                DrawRight(label, x + w, cy);
            }
            if (full)
                h = Wrap(title, x, cy, w - lw - 4, 0, draw, NULL);
            else if (draw)
                WinDrawTruncChars(title, len, x, cy, w - lw - 4);
            if (!h)
                h = lh;
            cy += h;
            if (draw && pct > 0) {
                RectangleType bar;
                RctSetRectangle(&bar, x, cy, (Coord)(((Int32)w * pct) / 100), 2);
                if (!bar.extent.x)
                    bar.extent.x = 1;
                WinSetForeColor(own ? Accent() : gray);
                WinDrawRectangle(&bar, 0);
            }
            FntSetFont(stdFont);
        } else {
            Coord h = lh, tx = x + kPollBox + 4;
            if (draw) {
                RectangleType box;
                RctSetRectangle(&box, x + 1, cy + (lh - kPollBox) / 2, kPollBox, kPollBox);
                WinSetForeColor(UIColorGetTableEntryIndex(UIObjectForeground));
                WinDrawRectangleFrame(multi ? simpleFrame : roundFrame, &box);
                if (full && (item->pollSel & (1UL << i))) {
                    RctInsetRectangle(&box, 2);
                    WinDrawRectangle(&box, multi ? 0 : 2);
                }
                WinSetTextColor(UIColorGetTableEntryIndex(UIObjectForeground));
            }
            if (full)
                h = Wrap(title, tx, cy, x + w - tx, 0, draw, NULL);
            else if (draw)
                WinDrawTruncChars(title, len, tx, cy, x + w - tx);
            if (!h)
                h = lh;
            cy += h;
        }
        cy += kPollRowGap;
    }
    if (full)
        gPollRowTop[n] = cy - top;

    summary = ProtoPollPart(item, pollSummary);
    if (draw) {
        WinSetTextColor(gray);
        WinDrawTruncChars(summary, StrLen(summary), x, cy, w);
    }
    cy += lh;

    if (full && vote) {
        const char *label = multi ? "Vote (pick any)" : "Vote";
        RectangleType b;
        UInt16 len = StrLen(label);
        FntSetFont(boldFont);
        RctSetRectangle(&b, x + 1, cy + 3, FntCharsWidth(label, len) + 14, lh + 1);
        if (draw) {
            WinSetForeColor(Accent());
            WinSetTextColor(Accent());
            WinDrawRectangleFrame(roundFrame, &b);
            WinDrawChars(label, len, b.topLeft.x + 7, b.topLeft.y);
        }
        FntSetFont(stdFont);
        gPollVoteTop = cy - top;
        gPollVoteBottom = b.topLeft.y + b.extent.y + 2 - top;
        gPollVoteRight = b.topLeft.x + b.extent.x + 2 - origin->x;
        cy = b.topLeft.y + b.extent.y + 2;
    }
    return cy - y;
}

Int16 RenderPollHit(ItemType *item, Coord dx, Coord dy)
{
    UInt16 i, n;
    if (item != gPollItem || !RenderPollCanVote(item))
        return -1;
    if (dy >= gPollVoteTop && dy < gPollVoteBottom && dx < gPollVoteRight)
        return pollHitVote;
    n = ProtoPollOptions(item);
    for (i = 0; i < n; i++)
        if (dy >= gPollRowTop[i] && dy < gPollRowTop[i + 1])
            return i;
    return -1;
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
    char kind = item->f[fKind][0];

    if (full && gPollItem == item)
        gPollItem = NULL;  /* set again below if the poll is still open */

    gClipTop = clip ? clip->topLeft.y : -32767;
    gClipBottom = clip ? clip->topLeft.y + clip->extent.y : 32767;
    if (draw) {
        WinPushDrawState();
        WinSetTextColor(UIColorGetTableEntryIndex(UIObjectForeground));
        if ((flags & renderSelected) && item->height > 3) {
            /* tinted background (text cells use the back colour) + frame */
            RectangleType sel;
            RctSetRectangle(&sel, x, y, w, item->height - 1);
            WinSetForeColor(Accent());
            WinDrawRectangle(&sel, 0);
            RctInsetRectangle(&sel, 1);
            WinSetForeColor(gTint);
            WinDrawRectangle(&sel, 0);
            WinSetBackColor(gTint);
        }
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
            WinSetTextColor(kind == 'N' || kind == 'P' ? Accent() : gray);
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
    if (item->f[fAcct][0]) {  /* hashtags have none */
        if (draw) {
            buf[0] = '@';
            StrNCopyZ(buf + 1, item->f[fAcct], sizeof(buf) - 1);
            StrNCatZ(buf, VisLabel(item), sizeof(buf));
            WinSetTextColor(gray);
            WinDrawTruncChars(buf, StrLen(buf), tx, cy, left + avail - tx);
        }
        cy += lh;
    }
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
        if (item->pollParts) {
            PointType origin;
            origin.x = x;
            origin.y = y;
            cy += 3;
            cy += DrawPoll(item, left, cy, &origin, avail, flags, gray);
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
    if (kind == 'S') {
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
