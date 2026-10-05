/* Image download and display.
 *
 * The gateway sends pre-scaled RGB565 (or 8-bit indexed) pixels. They are
 * streamed into a column of "strip" bitmaps of at most ~16 KB each, so a big
 * picture never needs one huge allocation. On high-density screens (the
 * LifeDrive is 320x480) each strip gets a V3 double-density wrapper so it is
 * drawn at full native resolution with plain standard-coordinate calls.
 */
#include "PalmFedi.h"

#define kStripBytes   16000
#define kThumbSlots   40

typedef struct {
    BitmapType   *base;
    BitmapTypeV3 *v3;
} StripType;

typedef struct {
    PfImage    hdr;              /* hdr.bmp[] unused; strips follow */
    StripType  strips[1];
} PfImageImpl;

typedef struct {
    char     key[24];
    UInt16   w, h;
    UInt8    crop;
    UInt8    failed;
    UInt32   used;
    PfImage *img;
} ThumbSlot;

static ThumbSlot *gThumbs = NULL;
static UInt32 gThumbClock = 0;
static ColorTableType *gColorTable = NULL;
static Boolean gHiRes = false;

Boolean ImgHiRes(void)
{
    return gHiRes;
}

/* The fixed palette shared with gateway/palmfedi_gateway/images.py */
static void BuildColorTable(void)
{
    RGBColorType *e;
    UInt16 i, r, g, b;
    static const UInt8 levels[6] = { 0, 51, 102, 153, 204, 255 };

    gColorTable = (ColorTableType *)MemPtrNew(sizeof(ColorTableType) +
                                              256 * sizeof(RGBColorType));
    if (!gColorTable)
        return;
    gColorTable->numEntries = 256;
    e = ColorTableEntries(gColorTable);
    i = 0;
    for (r = 0; r < 6; r++)
        for (g = 0; g < 6; g++)
            for (b = 0; b < 6; b++, i++) {
                e[i].index = (UInt8)i;
                e[i].r = levels[r];
                e[i].g = levels[g];
                e[i].b = levels[b];
            }
    for (r = 1; r <= 40; r++, i++) {
        UInt8 v = (UInt8)((r * 255L + 20) / 41);
        e[i].index = (UInt8)i;
        e[i].r = e[i].g = e[i].b = v;
    }
}

void ImgInit(void)
{
    UInt32 winVersion = 0;
    gHiRes = (FtrGet(sysFtrCreator, sysFtrNumWinVersion, &winVersion) == errNone &&
              winVersion >= 4);
    BuildColorTable();
    gThumbs = (ThumbSlot *)MemPtrNew(sizeof(ThumbSlot) * kThumbSlots);
    if (gThumbs)
        MemSet(gThumbs, sizeof(ThumbSlot) * kThumbSlots, 0);
}

void ImgShutdown(void)
{
    ThumbFlush();
    if (gThumbs)
        MemPtrFree(gThumbs);
    if (gColorTable)
        MemPtrFree(gColorTable);
    gThumbs = NULL;
    gColorTable = NULL;
}

void ImgFree(PfImage *img)
{
    PfImageImpl *impl = (PfImageImpl *)img;
    UInt16 i;
    if (!img)
        return;
    for (i = 0; i < img->numStrips; i++) {
        if (impl->strips[i].v3)
            BmpDelete((BitmapType *)impl->strips[i].v3);
        if (impl->strips[i].base)
            BmpDelete(impl->strips[i].base);
    }
    MemPtrFree(img);
}

static UInt16 Get16(const UInt8 *p)
{
    return ((UInt16)p[0] << 8) | p[1];
}

PfImage *ImgLoad(const char *mediaKey, UInt16 maxW, UInt16 maxH, Boolean crop)
{
    char url[160];
    UInt8 head[12];
    HttpConn c;
    UInt16 w, h, bpp, density, rowBytes, stripRows, numStrips, i;
    PfImageImpl *impl;
    Err err;

    if (!gHiRes) {
        maxW = (maxW + 1) / 2;
        maxH = (maxH + 1) / 2;
    }
    UrlInit(url, sizeof(url), "/p/img");
    UrlAdd(url, sizeof(url), "k", gPrefs.key);
    UrlAdd(url, sizeof(url), "m", mediaKey);
    UrlAddInt(url, sizeof(url), "w", maxW);
    UrlAddInt(url, sizeof(url), "h", maxH);
    UrlAddInt(url, sizeof(url), "bpp", gPrefs.depth == 8 ? 8 : 16);
    if (!gHiRes)
        UrlAdd(url, sizeof(url), "d", "1");
    if (crop)
        UrlAdd(url, sizeof(url), "fit", "crop");

    if (HttpOpen(&c, "GET", url, NULL, 0) != errNone)
        return NULL;
    if (!c.isImage || HttpReadFully(&c, head, sizeof(head)) != errNone ||
        head[0] != 'P' || head[1] != 'F' || head[2] != 'I' || head[3] != '1') {
        HttpClose(&c);
        return NULL;
    }
    w = Get16(head + 4);
    h = Get16(head + 6);
    bpp = head[8];
    density = head[9];
    rowBytes = Get16(head + 10);
    if (!w || !h || (bpp != 8 && bpp != 16) || rowBytes < (w * bpp) / 8) {
        HttpClose(&c);
        return NULL;
    }
    density = (density == 144 && gHiRes) ? kDensityDouble : kDensityLow;

    stripRows = kStripBytes / rowBytes;
    if (stripRows > h)
        stripRows = h;
    if (density == kDensityDouble && (stripRows & 1) && stripRows < h)
        stripRows--;
    if (stripRows < 2)
        stripRows = 2;
    numStrips = (h + stripRows - 1) / stripRows;

    impl = (PfImageImpl *)MemPtrNew(sizeof(PfImageImpl) + (numStrips - 1) * sizeof(StripType));
    if (!impl) {
        HttpClose(&c);
        return NULL;
    }
    MemSet(impl, sizeof(PfImageImpl) + (numStrips - 1) * sizeof(StripType), 0);
    impl->hdr.w = w;
    impl->hdr.h = h;
    impl->hdr.density = density;
    impl->hdr.stdW = density == kDensityDouble ? (w + 1) / 2 : w;
    impl->hdr.stdH = density == kDensityDouble ? (h + 1) / 2 : h;
    impl->hdr.stripRows = stripRows;
    impl->hdr.numStrips = numStrips;

    for (i = 0; i < numStrips; i++) {
        UInt16 rows = (i == numStrips - 1) ? h - i * stripRows : stripRows;
        UInt16 bmpRowBytes, r;
        UInt8 *bits;
        BitmapType *bmp = BmpCreate(w, rows, (UInt8)bpp, bpp == 8 ? gColorTable : NULL, &err);
        if (!bmp)
            goto fail;
        impl->strips[i].base = bmp;
        BmpGetDimensions(bmp, NULL, NULL, &bmpRowBytes);
        bits = (UInt8 *)BmpGetBits(bmp);
        if (bmpRowBytes == rowBytes) {
            if (HttpReadFully(&c, bits, (Int32)rowBytes * rows) != errNone)
                goto fail;
        } else {
            UInt8 pad[16];
            for (r = 0; r < rows; r++) {
                UInt16 take = rowBytes < bmpRowBytes ? rowBytes : bmpRowBytes;
                UInt16 skip = rowBytes - take;
                if (HttpReadFully(&c, bits + (UInt32)r * bmpRowBytes, take) != errNone)
                    goto fail;
                while (skip) {
                    UInt16 k = skip > sizeof(pad) ? sizeof(pad) : skip;
                    if (HttpReadFully(&c, pad, k) != errNone)
                        goto fail;
                    skip -= k;
                }
            }
        }
        if (density == kDensityDouble) {
            impl->strips[i].v3 = BmpCreateBitmapV3(bmp, kDensityDouble, bits,
                                                   bpp == 8 ? gColorTable : NULL);
            if (!impl->strips[i].v3)
                goto fail;
        }
    }
    HttpClose(&c);
    return &impl->hdr;

fail:
    HttpClose(&c);
    ImgFree(&impl->hdr);
    return NULL;
}

void ImgDraw(PfImage *img, Coord x, Coord y, const RectangleType *clip)
{
    PfImageImpl *impl = (PfImageImpl *)img;
    UInt16 i;
    Coord stripStd = img->density == kDensityDouble ? img->stripRows / 2 : img->stripRows;

    for (i = 0; i < img->numStrips; i++) {
        Coord top = y + i * stripStd;
        if (clip && (top > clip->topLeft.y + clip->extent.y || top + stripStd < clip->topLeft.y))
            continue;
        WinDrawBitmap(impl->strips[i].v3 ? (BitmapType *)impl->strips[i].v3
                                         : impl->strips[i].base, x, top);
    }
}

/* ------------------------------------------------------------------------
 * Thumbnail cache
 * --------------------------------------------------------------------- */
PfImage *ThumbGet(const char *key, UInt16 maxW, UInt16 maxH, Boolean crop,
                  Boolean fetch, Boolean *failed)
{
    UInt16 i, victim = 0;
    UInt32 oldest = 0xFFFFFFFFUL;
    ThumbSlot *s;

    *failed = false;
    if (!gThumbs || !key || !key[0])
        return NULL;
    for (i = 0; i < kThumbSlots; i++) {
        s = &gThumbs[i];
        if (s->key[0] && s->w == maxW && s->h == maxH && s->crop == crop &&
            StrCompare(s->key, key) == 0) {
            s->used = ++gThumbClock;
            *failed = s->failed;
            return s->img;
        }
        if (s->used < oldest) {
            oldest = s->used;
            victim = i;
        }
    }
    if (!fetch)
        return NULL;

    s = &gThumbs[victim];
    ImgFree(s->img);
    MemSet(s, sizeof(ThumbSlot), 0);
    s->img = ImgLoad(key, maxW, maxH, crop);
    StrNCopyZ(s->key, key, sizeof(s->key));
    s->w = maxW;
    s->h = maxH;
    s->crop = crop;
    s->failed = (s->img == NULL);
    s->used = ++gThumbClock;
    *failed = s->failed;
    return s->img;
}

void ThumbFlush(void)
{
    UInt16 i;
    if (!gThumbs)
        return;
    for (i = 0; i < kThumbSlots; i++) {
        ImgFree(gThumbs[i].img);
        MemSet(&gThumbs[i], sizeof(ThumbSlot), 0);
    }
}
