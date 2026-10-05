/* Parsing of the gateway's record format (see docs/PROTOCOL.md). */
#include "PalmFedi.h"

#define chUS 0x1F
#define chRS 0x1E
#define chGS 0x1D

static char gEmpty[1] = { 0 };

UInt16 ProtoSplit(char *buf, UInt32 len, char **records, UInt16 maxRecords)
{
    UInt16 n = 0;
    UInt32 i;

    if (!len || !maxRecords)
        return 0;
    records[n++] = buf;
    for (i = 0; i < len; i++) {
        if (buf[i] == chRS) {
            buf[i] = 0;
            if (n == maxRecords)
                break;
            records[n++] = buf + i + 1;
        }
    }
    buf[len] = 0;  /* HttpFetch always leaves room for the terminator */
    return n;
}

UInt16 ProtoFields(char *record, char **fields, UInt16 maxFields)
{
    UInt16 n = 0, i;
    char *p = record;

    fields[n++] = p;
    while (*p) {
        if (*p == chUS) {
            *p = 0;
            if (n < maxFields)
                fields[n++] = p + 1;
        }
        p++;
    }
    for (i = n; i < maxFields; i++)
        fields[i] = gEmpty;
    return n;
}

void ProtoFillItem(ItemType *item, char *record, UInt8 page)
{
    char *p;
    UInt8 n = 0;

    MemSet(item, sizeof(ItemType), 0);
    ProtoFields(record, item->f, kNumFields);
    StrNCopyZ(item->flags, item->f[fFlags], sizeof(item->flags));
    StrNCopyZ(item->counts, item->f[fCounts], sizeof(item->counts));
    item->page = page;
    item->height = -1;
    item->thumbY = -1;
    item->showBody = (item->f[fCW][0] == 0);

    /* media: "T:key" GS "T:key" ... */
    p = item->f[fMedia];
    while (*p && n < kMaxMedia) {
        char *start = p;
        while (*p && *p != chGS)
            p++;
        if (*p)
            *p++ = 0;
        if (start[0] && start[1] == ':') {
            item->mediaType[n] = start[0];
            item->mediaKey[n] = start + 2;
            item->mediaAlt[n] = gEmpty;
            n++;
        }
    }
    item->numMedia = n;

    /* alt texts, same order */
    p = item->f[fAlts];
    n = 0;
    while (*p && n < item->numMedia) {
        char *start = p;
        while (*p && *p != chGS)
            p++;
        if (*p)
            *p++ = 0;
        item->mediaAlt[n++] = start;
    }

    ProtoSetPoll(item, item->f[fPoll]);
}

void ProtoSetPoll(ItemType *item, char *field)
{
    UInt16 n = 1;
    char *p = field;

    item->poll = NULL;
    item->pollParts = 0;
    item->pollSel = 0;
    if (!field || !*field)
        return;
    for (; *p; p++) {
        if (*p == chGS) {
            *p = 0;
            if (n == pollFirstOption + kMaxPollOptions)
                break;  /* ignore options we can't vote for */
            n++;
        }
    }
    if (n > pollFirstOption) {
        item->poll = field;
        item->pollParts = (UInt8)n;
    }
}

const char *ProtoPollPart(const ItemType *item, UInt16 part)
{
    const char *p = item->poll;
    UInt16 i;

    if (!p || part >= item->pollParts)
        return gEmpty;
    for (i = 0; i < part; i++)
        p += StrLen(p) + 1;
    return p;
}

UInt16 ProtoPollOptions(const ItemType *item)
{
    return item->pollParts ? item->pollParts - pollFirstOption : 0;
}

Boolean ProtoPollHas(const ItemType *item, char flag)
{
    return item->pollParts && StrChr(ProtoPollPart(item, pollFlags), flag) != NULL;
}

void ProtoFreeItem(ItemType *item)
{
    if (item->pollOwned && item->poll)
        MemPtrFree(item->poll);
    item->poll = NULL;
    item->pollParts = 0;
    item->pollOwned = false;
}

Boolean ProtoHasFlag(const ItemType *item, char flag)
{
    return StrChr(item->flags, flag) != NULL;
}

void ProtoSetFlag(ItemType *item, char flag, Boolean on)
{
    char *p = StrChr(item->flags, flag);
    UInt16 len;

    if (on && !p) {
        len = StrLen(item->flags);
        if (len + 1 < sizeof(item->flags)) {
            item->flags[len] = flag;
            item->flags[len + 1] = 0;
        }
    } else if (!on && p) {
        MemMove(p, p + 1, StrLen(p));
    }
}
