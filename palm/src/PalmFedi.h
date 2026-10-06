/* PalmFedi - an Akkoma / Mastodon-API client for Palm OS 5 (Palm LifeDrive).
 *
 * Shared types, globals and prototypes.
 */
#ifndef PALMFEDI_H
#define PALMFEDI_H

#include <PalmOS.h>
#include "PalmFediRsc.h"

/* Code that is rarely run (dialogs, compose, viewer) lives in a second code
 * segment: 68k code segments are limited to 32 KB. See PalmFedi.def. */
#ifdef __palmos__
#define SEG_DIALOGS __attribute__ ((section ("dialogs")))
#else
#define SEG_DIALOGS
#endif

#define appCreator          'PFdi'
#define appPrefID           0
#define appPrefVersion      2
#define appVersionStr       "1.2"

/* -------------------------------------------------------------------------
 * Preferences
 * ---------------------------------------------------------------------- */
typedef struct {
    char    host[64];       /* gateway host name or dotted IP */
    UInt16  port;           /* gateway TCP port */
    char    key[16];        /* device key issued by the gateway */
    char    acct[64];       /* user@instance, informational */
    char    instance[64];   /* remembered for the login form */
    char    user[48];
    UInt8   loadImages;     /* fetch media thumbnails */
    UInt8   showAvatars;
    UInt8   depth;          /* 16 or 8 bits per pixel for images */
    UInt8   lastKind;       /* last timeline kind shown */
    UInt8   bodyFont;       /* 0 = stdFont, 1 = largeFont */
    UInt8   reserved[3];
} PrefsType;

extern PrefsType gPrefs;

/* -------------------------------------------------------------------------
 * Wire protocol (docs/PROTOCOL.md)
 * ---------------------------------------------------------------------- */
#define fKind      0
#define fId        1
#define fName      2
#define fAcct      3
#define fTime      4
#define fText      5
#define fCW        6
#define fContext   7
#define fFlags     8
#define fCounts    9
#define fMedia     10
#define fAlts      11
#define fAvatar    12
#define fVis       13
#define fMentions  14
#define fAcctId    15
#define fPoll      16
#define kNumFields 17

#define kProtoVersion   2        /* sent as v= so the gateway sends polls etc. */
#define kMaxMedia       4
#define kMaxPollOptions 20

/* poll parts (ProtoPollPart): id, flags, summary, then the options */
#define pollId      0
#define pollFlags   1
#define pollSummary 2
#define pollFirstOption 3

typedef struct {
    char   *f[kNumFields];       /* point into the page buffer (NUL terminated) */
    char    flags[8];            /* own copies, updated after actions */
    char    counts[24];
    UInt8   numMedia;
    char    mediaType[kMaxMedia];
    char   *mediaKey[kMaxMedia];
    char   *mediaAlt[kMaxMedia];
    Int16   y;                   /* top in list coordinates */
    Int16   height;              /* cached layout height, -1 = dirty */
    Int16   thumbY;              /* offset of the thumbnail row, -1 = none */
    UInt8   page;                /* index of the buffer holding the strings */
    Boolean showBody;            /* CW expanded */
    UInt8   pollParts;           /* 0 = no poll, else 3 + number of options */
    Boolean pollOwned;           /* poll is our own MemPtr (updated after voting) */
    char   *poll;                /* NUL separated poll parts */
    UInt32  pollSel;             /* options ticked in the detail view (bit mask) */
} ItemType;

/* Split a response buffer in place. Returns number of records found. */
UInt16  ProtoSplit(char *buf, UInt32 len, char **records, UInt16 maxRecords);
UInt16  ProtoFields(char *record, char **fields, UInt16 maxFields);
void    ProtoFillItem(ItemType *item, char *record, UInt8 page);
Boolean ProtoHasFlag(const ItemType *item, char flag);
void    ProtoSetFlag(ItemType *item, char flag, Boolean on);
/* Attach a poll field ("id" GS "flags" GS "summary" GS options...); splits
 * it in place. Options look like "*42:Title" (* = your vote, - = not). */
void    ProtoSetPoll(ItemType *item, char *field);
const char *ProtoPollPart(const ItemType *item, UInt16 part);
UInt16  ProtoPollOptions(const ItemType *item);
Boolean ProtoPollHas(const ItemType *item, char flag);
void    ProtoFreeItem(ItemType *item);  /* frees what the item owns, not the page */

/* -------------------------------------------------------------------------
 * Networking (net.c)
 * ---------------------------------------------------------------------- */
typedef struct {
    NetSocketRef sock;
    char    buf[256];            /* read-ahead buffer */
    UInt16  bufLen, bufPos;
    Int32   contentLength;       /* -1 if unknown */
    Int32   bodyRead;
    Boolean isImage;
} HttpConn;

Err     NetStart(void);
void    NetStop(Boolean now);   /* now: drop the connection instead of lingering */
Err     HttpOpen(HttpConn *c, const char *method, const char *path,
                 const char *body, UInt16 bodyLen);
Int32   HttpRead(HttpConn *c, void *dst, Int32 n);   /* <0 on error */
Err     HttpReadFully(HttpConn *c, void *dst, Int32 n);
void    HttpClose(HttpConn *c);
/* Fetch a whole record response. *bufP is a MemPtr the caller frees. */
Err     HttpFetch(const char *method, const char *path, const char *body,
                  char **bufP, UInt32 *lenP);
const char *NetLastError(void);

/* URL builder helpers */
void    UrlInit(char *dst, UInt16 size, const char *path);
void    UrlAdd(char *dst, UInt16 size, const char *name, const char *value);
void    UrlAddInt(char *dst, UInt16 size, const char *name, Int32 value);

/* -------------------------------------------------------------------------
 * Images (image.c)
 * ---------------------------------------------------------------------- */
typedef struct {
    UInt16  w, h;                /* native pixels */
    UInt16  stdW, stdH;          /* standard (160x160 grid) coordinates */
    UInt16  stripRows;           /* native rows per strip (even) */
    UInt16  numStrips;
    UInt16  density;             /* kDensityLow / kDensityDouble */
    UInt32  bytes;               /* pixel memory, for the cache budget */
    BitmapType   *bmp[1];        /* numStrips entries, V3 wrappers if HD */
} PfImage;

void    ImgInit(void);
void    ImgShutdown(void);
Boolean ImgHiRes(void);
PfImage *ImgLoad(const char *mediaKey, UInt16 maxW, UInt16 maxH, Boolean crop);
void    ImgFree(PfImage *img);
void    ImgDraw(PfImage *img, Coord x, Coord y, const RectangleType *clip);
/* Small LRU cache of thumbnails/avatars, keyed by media key + size. */
PfImage *ThumbGet(const char *key, UInt16 maxW, UInt16 maxH, Boolean crop,
                  Boolean fetch, Boolean *failed);
void    ThumbFlush(void);
void    ThumbTrim(UInt16 maxW);  /* drop cached images wider than maxW pixels */
UInt32  ThumbBytes(void);

/* -------------------------------------------------------------------------
 * Timeline model (timeline.c)
 * ---------------------------------------------------------------------- */
enum { kindHome, kindLocal, kindPublic, kindNotif, kindMentions, kindBookmarks,
       kindThread, kindUser, kindSearch, kindTag, kindCount };

#define kKindListSearch  6       /* "Search..." entry of the timeline popup */

typedef struct {
    UInt8   kind;
    char    target[64];          /* status id, account id, search text or hashtag */
    char    title[32];
} ViewType;

#define kMaxItems  100
#define kMaxPages  6
#define kMaxHistory 6

typedef struct {
    ViewType view;
    ViewType history[kMaxHistory];
    UInt8   historyLen;
    ItemType *items;
    UInt16  numItems;
    char   *pages[kMaxPages];
    UInt8   numPages;
    char    cursor[32];
    Int16   focus;               /* thread focus item, -1 if none */
    Int16   scroll;              /* pixel scroll offset */
    Int16   totalHeight;
    Int16   layoutWidth;
    Int16   selected;            /* item opened in the detail form */
    Int16   hl;                  /* item highlighted by the 5-way, -1 if none */
    Boolean needsLoad;           /* reload when the main form opens */
} TimelineType;

extern TimelineType gTL;

Boolean TLInit(void);
void    TLFree(void);
void    TLClear(void);
Err     TLLoad(Boolean older);
void    TLSetView(UInt8 kind, const char *target, const char *title, Boolean push);
Boolean TLBack(void);
const char *TLKindName(UInt8 kind);

/* -------------------------------------------------------------------------
 * Rendering of items (render.c)
 * ---------------------------------------------------------------------- */
#define renderDraw     0x01      /* actually draw (otherwise only measure) */
#define renderFull     0x02      /* detail view: no line cap, big media */
#define renderFocus    0x04      /* thread focus (blue bar) */
#define renderSelected 0x08      /* 5-way highlight (tinted, framed) */

#define kThumbStd      36        /* list thumbnail size, standard coords */
#define kAvatarStd     20

Int16   RenderItem(ItemType *item, Coord x, Coord y, Coord w, UInt16 flags,
                   const RectangleType *clip);
Int16   RenderMediaHit(ItemType *item, Coord w, UInt16 flags, Coord dx, Coord dy);
/* Poll option under (dx, dy) in the last renderFull drawing of item:
 * the option index, pollHitVote for the Vote button, or -1. */
#define pollHitVote  -2
Int16   RenderPollHit(ItemType *item, Coord dx, Coord dy);
Boolean RenderPollCanVote(const ItemType *item);
Boolean RenderFetchOne(ItemType *item, Coord w, UInt16 flags);
void    RenderMessage(const RectangleType *r, const char *msg);

/* -------------------------------------------------------------------------
 * Forms
 * ---------------------------------------------------------------------- */
Boolean MainFormHandleEvent(EventType *e);
Boolean DetailFormHandleEvent(EventType *e);
Boolean ComposeFormHandleEvent(EventType *e) SEG_DIALOGS;
Boolean ViewerFormHandleEvent(EventType *e) SEG_DIALOGS;
Boolean PrefsRun(void) SEG_DIALOGS;          /* modal; returns true if saved */
Boolean LoginRun(void) SEG_DIALOGS;
Boolean SearchRun(void) SEG_DIALOGS;         /* modal; true if gTL now shows the results */

/* Accounts (account.c) */
const char *AcctRelLabel(const char *flags) SEG_DIALOGS;
const char *AcctFollowLabel(const char *flags) SEG_DIALOGS;
/* Ask, then follow or unfollow. flags (W/Q/Y/M, see PROTOCOL.md) are
 * fetched first unless known; they are updated on success. */
Boolean AcctFollowToggle(const char *acctId, const char *name, char *flags, UInt16 size,
                         Boolean known) SEG_DIALOGS;
Boolean MainIdle(void);          /* returns true if more idle work pending */
void    AppExit(void);           /* quit to the launcher, dropping the network */
Boolean DetailIdle(void);

/* compose parameters set before FrmGotoForm(ComposeForm) */
typedef struct {
    char   replyTo[32];
    char   vis;
    char  *prefill;              /* MemPtr owned by compose, may be NULL */
    char   cw[128];
} ComposeArgs;
extern ComposeArgs gCompose;

/* viewer parameters */
typedef struct {
    ItemType *item;
    UInt16  index;
} ViewerArgs;
extern ViewerArgs gViewer;

/* -------------------------------------------------------------------------
 * Utilities (util.c)
 * ---------------------------------------------------------------------- */
void   *GetObjectPtr(UInt16 id);
void    SetFieldText(UInt16 id, const char *text);
const char *GetFieldText(UInt16 id);
void    ShowError(const char *msg);
void    ShowInfo(const char *msg);
void    StrNCopyZ(char *dst, const char *src, UInt16 size);
void    StrNCatZ(char *dst, const char *src, UInt16 size);

/* Display / Dynamic Input Area support */
void    DiaInit(void);
void    DiaFormOpen(FormPtr frm, Boolean resizable);
Boolean DiaResizeForm(FormPtr frm, RectangleType *newBounds);
void    MoveObject(FormPtr frm, UInt16 id, Coord x, Coord y);
void    SizeObject(FormPtr frm, UInt16 id, Coord x, Coord y, Coord w, Coord h);
Boolean HandleNavKey(EventType *e, Int16 *dir); /* -1 up, 1 down, 2 select, -2/3 left/right */

#define navUp     -1
#define navDown    1
#define navSelect  2
#define navLeft   -2
#define navRight   3

#endif
