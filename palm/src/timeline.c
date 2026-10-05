/* Timeline model: pages of records fetched from the gateway. Item strings
 * point straight into the page buffers, so a page stays allocated while any
 * of its items is in the list. */
#include "PalmFedi.h"

TimelineType gTL;

static const char *kKindParam[kindCount] = {
    "home", "local", "public", "notif", "mentions", "bookmarks", "thread", "user"
};
static const char *kKindName[kindCount] = {
    "Home", "Local", "Federated", "Notifications", "Mentions", "Bookmarks",
    "Thread", "Posts"
};

const char *TLKindName(UInt8 kind)
{
    return kind < kindCount ? kKindName[kind] : "";
}

Boolean TLInit(void)
{
    MemSet(&gTL, sizeof(gTL), 0);
    gTL.items = (ItemType *)MemPtrNew(sizeof(ItemType) * kMaxItems);
    gTL.focus = -1;
    gTL.selected = -1;
    gTL.view.kind = gPrefs.lastKind < kindThread ? gPrefs.lastKind : kindHome;
    StrCopy(gTL.view.title, TLKindName(gTL.view.kind));
    return gTL.items != NULL;
}

void TLClear(void)
{
    UInt8 i;
    for (i = 0; i < gTL.numPages; i++)
        MemPtrFree(gTL.pages[i]);
    gTL.numPages = 0;
    gTL.numItems = 0;
    gTL.cursor[0] = 0;
    gTL.focus = -1;
    gTL.scroll = 0;
    gTL.totalHeight = 0;
    gTL.selected = -1;
}

void TLFree(void)
{
    TLClear();
    if (gTL.items)
        MemPtrFree(gTL.items);
    gTL.items = NULL;
}

/* Drop the oldest page to make room when paging further back. */
static void DropFirstPage(void)
{
    UInt16 i, keep = 0;
    Int16 removedHeight = 0;

    for (i = 0; i < gTL.numItems; i++) {
        if (gTL.items[i].page == 0) {
            if (gTL.items[i].height > 0)
                removedHeight += gTL.items[i].height;
            continue;
        }
        gTL.items[keep] = gTL.items[i];
        gTL.items[keep].page--;
        keep++;
    }
    gTL.numItems = keep;
    MemPtrFree(gTL.pages[0]);
    for (i = 1; i < gTL.numPages; i++)
        gTL.pages[i - 1] = gTL.pages[i];
    gTL.numPages--;
    gTL.scroll -= removedHeight;
    if (gTL.scroll < 0)
        gTL.scroll = 0;
    gTL.focus = -1;
    gTL.layoutWidth = 0;  /* force relayout of y positions */
}

Err TLLoad(Boolean older)
{
    char url[200];
    char *buf, *records[41], *head[4];
    UInt32 len;
    UInt16 n, i;
    Err err;

    if (!gTL.items)
        return memErrNotEnoughSpace;
    if (older && !gTL.cursor[0] && gTL.numItems)
        return errNone;  /* nothing more */

    UrlInit(url, sizeof(url), "/p/tl");
    UrlAdd(url, sizeof(url), "k", gPrefs.key);
    UrlAdd(url, sizeof(url), "t", kKindParam[gTL.view.kind]);
    if (gTL.view.target[0])
        UrlAdd(url, sizeof(url), "id", gTL.view.target);
    if (older)
        UrlAdd(url, sizeof(url), "max", gTL.cursor);
    UrlAddInt(url, sizeof(url), "n", 20);
    UrlAddInt(url, sizeof(url), "b", 30000);

    err = HttpFetch("GET", url, NULL, &buf, &len);
    if (err)
        return err;

    if (!older)
        TLClear();
    else {
        while (gTL.numPages >= kMaxPages || gTL.numItems + 40 > kMaxItems)
            DropFirstPage();
    }

    n = ProtoSplit(buf, len, records, 41);
    ProtoFields(records[0], head, 4);
    StrNCopyZ(gTL.cursor, head[1], sizeof(gTL.cursor));
    if (gTL.view.kind == kindThread && !older)
        gTL.focus = gTL.numItems + StrAToI(head[2]);

    gTL.pages[gTL.numPages] = buf;
    for (i = 1; i < n && gTL.numItems < kMaxItems; i++)
        ProtoFillItem(&gTL.items[gTL.numItems++], records[i], gTL.numPages);
    gTL.numPages++;
    gTL.layoutWidth = 0;
    if (gTL.view.kind < kindThread)
        gPrefs.lastKind = gTL.view.kind;
    return errNone;
}

void TLSetView(UInt8 kind, const char *target, const char *title, Boolean push)
{
    if (push) {
        if (gTL.historyLen == kMaxHistory) {
            MemMove(&gTL.history[0], &gTL.history[1], sizeof(ViewType) * (kMaxHistory - 1));
            gTL.historyLen--;
        }
        gTL.history[gTL.historyLen++] = gTL.view;
    } else {
        gTL.historyLen = 0;
    }
    gTL.view.kind = kind;
    StrNCopyZ(gTL.view.target, target ? target : "", sizeof(gTL.view.target));
    StrNCopyZ(gTL.view.title, title ? title : TLKindName(kind), sizeof(gTL.view.title));
    gTL.needsLoad = true;
}

Boolean TLBack(void)
{
    if (!gTL.historyLen)
        return false;
    gTL.view = gTL.history[--gTL.historyLen];
    gTL.needsLoad = true;
    return true;
}
