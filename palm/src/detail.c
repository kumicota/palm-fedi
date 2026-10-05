/* Post detail form: full text, big media previews, alt text and actions. */
#include "PalmFedi.h"

#define kTitleH   15
#define kButtonsH 15
#define kScrollW  7

static Int16 gScroll = 0, gHeight = 0;
static Boolean gIdleDone = true;

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

static void SyncButtons(FormPtr frm)
{
    ItemType *it = Current();
    Boolean isStatus = it && it->f[fKind][0] == 'S';
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
        FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, ids[i]), &r);
        x += r.extent.x + (W > 160 ? 8 : 3);
    }
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
        Int16 m = RenderMediaHit(it, r.extent.x, renderFull, x - r.topLeft.x,
                                 y - r.topLeft.y + gScroll);
        if (m >= 0)
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
