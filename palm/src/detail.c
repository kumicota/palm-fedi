/* Post detail form: full text, big media previews, alt text and actions. */
#include "PalmFedi.h"

#define kTitleH   15
#define kButtonsH 15
#define kScrollW  7

static Int16 gScroll = 0, gHeight = 0;
static Boolean gIdleDone = true;
static char gRelFlags[8];   /* relationship with the post's author, if fetched */

static ItemType *Current(void)
{
    if (gTL.selected < 0 || gTL.selected >= (Int16)gTL.numItems)
        return NULL;
    return &gTL.items[gTL.selected];
}

static void Bounds(RectangleType *r)
{
    FormPtr frm = FrmGetActiveForm();
    FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, DetailGadget), r);
}

static void Draw(void)
{
    RectangleType r;
    ItemType *it = Current();
    Int16 maxScroll;

    Bounds(&r);
    gIdleDone = false;  /* newly visible media may need loading */
    if (!it) {
        RenderMessage(&r, "No post");
        return;
    }
    gHeight = RenderItem(it, 0, 0, r.extent.x, renderFull, NULL);
    maxScroll = gHeight - r.extent.y;
    if (maxScroll < 0)
        maxScroll = 0;
    if (gScroll > maxScroll)
        gScroll = maxScroll;
    if (gScroll < 0)
        gScroll = 0;
    SclSetScrollBar((ScrollBarPtr)GetObjectPtr(DetailScroll), gScroll, 0, maxScroll,
                    r.extent.y > 20 ? r.extent.y - 10 : r.extent.y);
    WinSetClip(&r);
    WinEraseRectangle(&r, 0);
    RenderItem(it, r.topLeft.x, r.topLeft.y - gScroll, r.extent.x, renderFull | renderDraw, &r);
    WinResetClip();
}

static Boolean GadgetHandler(FormGadgetTypeInCallback *gadgetP, UInt16 cmd, void *paramP)
{
    if (cmd == formGadgetDrawCmd) {
        Draw();
        return true;
    }
    return false;
}

static Boolean IsProfile(const ItemType *it)
{
    return it && it->f[fKind][0] == 'P';
}

static void SyncButtons(FormPtr frm)
{
    ItemType *it = Current();
    Boolean isStatus = it && it->f[fKind][0] == 'S';
    UInt16 follow = FrmGetObjectIndex(frm, DetailFollowButton);
    UInt16 ids[4] = { DetailReplyButton, DetailBoostPush, DetailFavPush, DetailThreadButton };
    UInt16 i;

    for (i = 0; i < 4; i++) {
        UInt16 idx = FrmGetObjectIndex(frm, ids[i]);
        if (isStatus)
            FrmShowObject(frm, idx);
        else
            FrmHideObject(frm, idx);
    }
    if (isStatus) {
        CtlSetValue((ControlPtr)GetObjectPtr(DetailBoostPush), ProtoHasFlag(it, 'B'));
        CtlSetValue((ControlPtr)GetObjectPtr(DetailFavPush), ProtoHasFlag(it, 'F'));
    }
    if (IsProfile(it) && !ProtoHasFlag(it, 'M')) {
        FrmHideObject(frm, follow);  /* the label may get shorter */
        CtlSetLabel((ControlPtr)FrmGetObjectPtr(frm, follow), AcctFollowLabel(it->flags));
        FrmShowObject(frm, follow);
    } else
        FrmHideObject(frm, follow);
}

static void LayoutForm(FormPtr frm, Coord W, Coord H)
{
    UInt16 ids[5] = { DetailDoneButton, DetailReplyButton, DetailBoostPush,
                      DetailFavPush, DetailThreadButton };
    UInt16 i;
    Coord x = 1, by = H - kButtonsH + 2;
    RectangleType r;

    SizeObject(frm, DetailGadget, 0, kTitleH, W - kScrollW - 1, H - kTitleH - kButtonsH);
    SizeObject(frm, DetailScroll, W - kScrollW, kTitleH, kScrollW, H - kTitleH - kButtonsH);
    for (i = 0; i < 5; i++) {
        MoveObject(frm, ids[i], x, by);
        if (i == 1)  /* profiles show Follow where posts have Reply */
            MoveObject(frm, DetailFollowButton, x, by);
        FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, ids[i]), &r);
        x += r.extent.x + (W > 160 ? 8 : 3);
    }
}

/* Follow / unfollow the profile shown, or the author of the post. */
static void Follow(void)
{
    ItemType *it = Current();
    if (!it || !it->f[fAcctId][0])
        return;
    if (IsProfile(it)) {
        if (AcctFollowToggle(it->f[fAcctId], it->f[fName], it->flags, sizeof(it->flags), true)) {
            it->f[fContext] = (char *)AcctRelLabel(it->flags);
            it->height = -1;
            SyncButtons(FrmGetActiveForm());
            Draw();
        }
    } else if (AcctFollowToggle(it->f[fAcctId], it->f[fName], gRelFlags, sizeof(gRelFlags),
                                false)) {
        const char *label = AcctRelLabel(gRelFlags);
        ShowInfo(label[0] ? label : "Unfollowed.");
    }
}

/* Tick a poll option (single choice: replaces the previous one). */
static void PickOption(UInt16 i)
{
    ItemType *it = Current();
    if (!it)
        return;
    if (ProtoPollHas(it, 'M'))
        it->pollSel ^= 1UL << i;
    else
        it->pollSel = 1UL << i;
    Draw();
}

static void Vote(void)
{
    ItemType *it = Current();
    char body[200], choices[64], num[6], *buf, *rec[1], *f[2], *copy;
    UInt16 i, n;
    UInt32 len;

    if (!it || !it->pollParts)
        return;
    if (!it->pollSel) {
        ShowError("Tap an option first.");
        return;
    }
    choices[0] = 0;
    n = ProtoPollOptions(it);
    for (i = 0; i < n; i++) {
        if (!(it->pollSel & (1UL << i)))
            continue;
        if (choices[0])
            StrCat(choices, ",");
        StrIToA(num, i);
        StrCat(choices, num);
    }
    body[0] = 0;
    UrlAdd(body, sizeof(body), "k", gPrefs.key);
    UrlAdd(body, sizeof(body), "id", ProtoPollPart(it, pollId));
    UrlAdd(body, sizeof(body), "c", choices);
    if (HttpFetch("POST", "/p/vote", body, &buf, &len) != errNone) {
        ShowError(NetLastError());
        return;
    }
    ProtoSplit(buf, len, rec, 1);
    ProtoFields(rec[0], f, 2);
    copy = (char *)MemPtrNew(StrLen(f[1]) + 1);
    if (copy) {  /* the new results replace the poll from the page */
        StrCopy(copy, f[1]);
        ProtoFreeItem(it);
        ProtoSetPoll(it, copy);
        it->pollOwned = (it->poll == copy);
        if (!it->pollOwned)
            MemPtrFree(copy);
    }
    MemPtrFree(buf);
    it->height = -1;
    Draw();
}

/* fav / unfav / boost / unboost / bm / unbm */
static Boolean DoAction(const char *action)
{
    ItemType *it = Current();
    char body[96], *buf, *rec[2], *f[3];
    UInt32 len;

    if (!it)
        return false;
    body[0] = 0;
    UrlAdd(body, sizeof(body), "k", gPrefs.key);
    UrlAdd(body, sizeof(body), "id", it->f[fId]);
    UrlAdd(body, sizeof(body), "a", action);
    if (HttpFetch("POST", "/p/act", body, &buf, &len) != errNone) {
        ShowError(NetLastError());
        return false;
    }
    ProtoSplit(buf, len, rec, 1);
    ProtoFields(rec[0], f, 3);
    StrNCopyZ(it->flags, f[1], sizeof(it->flags));
    StrNCopyZ(it->counts, f[2], sizeof(it->counts));
    MemPtrFree(buf);
    it->height = -1;  /* main list re-measures it */
    return true;
}

static void Toggle(UInt16 ctlID, char flag, const char *on, const char *off)
{
    ItemType *it = Current();
    if (!it)
        return;
    DoAction(ProtoHasFlag(it, flag) ? off : on);
    SyncButtons(FrmGetActiveForm());
    Draw();
}

static void OpenMedia(UInt16 index)
{
    ItemType *it = Current();
    if (!it || index >= it->numMedia)
        return;
    gViewer.item = it;
    gViewer.index = index;
    FrmPopupForm(ViewerForm);
}

static Boolean PenDown(EventType *e)
{
    RectangleType r;
    Coord x = e->screenX, y = e->screenY, lastY = y, nx, ny;
    Boolean down = true, dragged = false;
    ItemType *it = Current();

    Bounds(&r);
    if (!it || !RctPtInRectangle(x, y, &r))
        return false;
    while (down) {
        EvtGetPen(&nx, &ny, &down);
        if (!dragged && (ny - y > 4 || y - ny > 4))
            dragged = true;
        if (dragged && ny != lastY) {
            gScroll -= (ny - lastY);
            Draw();
            lastY = ny;
        }
        if (down)
            SysTaskDelay(1);
    }
    if (!dragged) {
        Coord dx = x - r.topLeft.x, dy = y - r.topLeft.y + gScroll;
        Int16 p = RenderPollHit(it, dx, dy), m;
        if (p == pollHitVote)
            Vote();
        else if (p >= 0)
            PickOption(p);
        else if ((m = RenderMediaHit(it, r.extent.x, renderFull, dx, dy)) >= 0)
            OpenMedia(m);
    } else
        gIdleDone = false;
    return true;
}

Boolean DetailIdle(void)
{
    RectangleType r;
    ItemType *it = Current();
    if (gIdleDone || !it || FrmGetActiveFormID() != DetailForm)
        return false;
    Bounds(&r);
    if (RenderFetchOne(it, r.extent.x, renderFull)) {
        Draw();
        return true;
    }
    gIdleDone = true;
    return false;
}

static void Reply(void)
{
    ItemType *it = Current();
    UInt16 len;
    if (!it)
        return;
    MemSet(&gCompose, sizeof(gCompose), 0);
    StrNCopyZ(gCompose.replyTo, it->f[fId], sizeof(gCompose.replyTo));
    gCompose.vis = it->f[fVis][0] ? it->f[fVis][0] : 'p';
    StrNCopyZ(gCompose.cw, it->f[fCW], sizeof(gCompose.cw));
    len = StrLen(it->f[fMentions]);
    gCompose.prefill = (char *)MemPtrNew(len + 1);
    if (gCompose.prefill)
        StrCopy(gCompose.prefill, it->f[fMentions]);
    FrmGotoForm(ComposeForm);
}

static void GoTimeline(UInt8 kind, const char *target, const char *title)
{
    TLSetView(kind, target, title, true);
    FrmGotoForm(MainForm);
}

Boolean DetailFormHandleEvent(EventType *e)
{
    FormPtr frm = FrmGetActiveForm();
    RectangleType r;
    ItemType *it = Current();
    Int16 dir;

    switch (e->eType) {
    case frmOpenEvent:
        gScroll = 0;
        DiaFormOpen(frm, true);
        DiaResizeForm(frm, &r);
        LayoutForm(frm, r.extent.x, r.extent.y);
        FrmSetGadgetHandler(frm, FrmGetObjectIndex(frm, DetailGadget), GadgetHandler);
        FrmSetTitle(frm, IsProfile(it) ? "Profile" : "Post");
        SyncButtons(frm);
        if (it && !it->showBody) {
            it->showBody = true;  /* stays expanded in the list too */
            it->height = -1;
        }
        FrmDrawForm(frm);
        gIdleDone = false;
        return true;

    case frmUpdateEvent:
        FrmDrawForm(frm);
        gIdleDone = false;
        return true;

    case winDisplayChangedEvent:
        if (DiaResizeForm(frm, &r)) {
            LayoutForm(frm, r.extent.x, r.extent.y);
            FrmEraseForm(frm);
            FrmDrawForm(frm);
            gIdleDone = false;
        }
        return true;

    case penDownEvent:
        return PenDown(e);

    case sclRepeatEvent:
        gScroll = e->data.sclRepeat.newValue;
        Draw();
        return false;

    case ctlSelectEvent:
        switch (e->data.ctlSelect.controlID) {
        case DetailDoneButton:
            FrmGotoForm(MainForm);
            return true;
        case DetailReplyButton:
            Reply();
            return true;
        case DetailBoostPush:
            Toggle(DetailBoostPush, 'B', "boost", "unboost");
            return true;
        case DetailFavPush:
            Toggle(DetailFavPush, 'F', "fav", "unfav");
            return true;
        case DetailThreadButton:
            if (it)
                GoTimeline(kindThread, it->f[fId], "Thread");
            return true;
        case DetailFollowButton:
            Follow();
            return true;
        }
        return false;

    case keyDownEvent:
        if (HandleNavKey(e, &dir)) {
            Bounds(&r);
            if (dir == navUp || dir == navDown) {
                gScroll += dir * (r.extent.y - 20);
                Draw();
                gIdleDone = false;
            } else if (dir == navLeft || dir == navSelect)
                FrmGotoForm(MainForm);
            return true;
        }
        return false;

    case menuEvent:
        switch (e->data.menu.itemID) {
        case MenuDetailBookmark:
            if (it && it->f[fKind][0] == 'S') {
                DoAction(ProtoHasFlag(it, 'K') ? "unbm" : "bm");
                Draw();
            }
            return true;
        case MenuDetailProfile:
            if (it && it->f[fAcctId][0])
                GoTimeline(kindUser, it->f[fAcctId], it->f[fName]);
            return true;
        case MenuDetailFollow:
            Follow();
            return true;
        case MenuDetailCopy:
            if (it)
                ClipboardAddItem(clipboardText, it->f[fText], StrLen(it->f[fText]));
            return true;
        case MenuDetailImages:
            if (it && it->numMedia)
                OpenMedia(0);
            return true;
        }
        return false;

    default:
        return false;
    }
}
