/* Timeline form: scrolling list of posts drawn into a gadget. */
#include "PalmFedi.h"

#define kTitleH   15
#define kButtonsH 15
#define kScrollW  7
#define kDragSlop 4

static WinHandle gOff = NULL;          /* offscreen buffer for flicker-free scroll */
static Coord gOffW = 0, gOffH = 0;
static char gTriggerLabel[32];
static Boolean gIdleDone = true;

static void GetListBounds(RectangleType *r)
{
    FormPtr frm = FrmGetActiveForm();
    FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, MainGadget), r);
}

static UInt16 ItemFlags(UInt16 i)
{
    return (gTL.focus == (Int16)i) ? renderFocus : 0;
}

/* Recompute heights and y positions (only dirty items are re-measured). */
static void Layout(Coord w)
{
    UInt16 i;
    Int16 y = 0;
    Boolean all = (w != gTL.layoutWidth);
    for (i = 0; i < gTL.numItems; i++) {
        ItemType *it = &gTL.items[i];
        if (all || it->height < 0)
            it->height = RenderItem(it, 0, 0, w, ItemFlags(i), NULL);
        it->y = y;
        y += it->height;
    }
    gTL.totalHeight = y;
    gTL.layoutWidth = w;
}

static void ClampScroll(Coord viewH)
{
    Int16 maxScroll = gTL.totalHeight - viewH;
    if (maxScroll < 0)
        maxScroll = 0;
    if (gTL.scroll > maxScroll)
        gTL.scroll = maxScroll;
    if (gTL.scroll < 0)
        gTL.scroll = 0;
}

static void UpdateScrollBar(Coord viewH)
{
    Int16 maxScroll = gTL.totalHeight - viewH;
    if (maxScroll < 0)
        maxScroll = 0;
    SclSetScrollBar((ScrollBarPtr)GetObjectPtr(MainScroll), gTL.scroll, 0, maxScroll,
                    viewH > 20 ? viewH - 10 : viewH);
}

static void FreeOffscreen(void)
{
    if (gOff)
        WinDeleteWindow(gOff, false);
    gOff = NULL;
    gOffW = gOffH = 0;
}

static void DrawItems(Coord ox, Coord oy, Coord w, Coord h)
{
    RectangleType clip;
    UInt16 i;
    RctSetRectangle(&clip, ox, oy, w, h);
    WinEraseRectangle(&clip, 0);
    for (i = 0; i < gTL.numItems; i++) {
        ItemType *it = &gTL.items[i];
        Int16 top = it->y - gTL.scroll;
        if (top + it->height <= 0)
            continue;
        if (top >= h)
            break;
        RenderItem(it, ox, oy + top, w, renderDraw | ItemFlags(i), &clip);
    }
}

static void DrawList(void)
{
    RectangleType r, src;
    Err err;

    GetListBounds(&r);
    Layout(r.extent.x);
    ClampScroll(r.extent.y);
    UpdateScrollBar(r.extent.y);
    gIdleDone = false;  /* newly visible posts may need images */

    if (!gTL.numItems) {
        RenderMessage(&r, !gPrefs.host[0] || !gPrefs.key[0]
                          ? "Set up the gateway: Menu > Prefs"
                          : "Nothing here. Tap Reload.");
        return;
    }

    if (!gOff || gOffW != r.extent.x || gOffH != r.extent.y) {
        FreeOffscreen();
        gOff = WinCreateOffscreenWindow(r.extent.x, r.extent.y, nativeFormat, &err);
        gOffW = r.extent.x;
        gOffH = r.extent.y;
    }
    if (gOff) {
        WinHandle old = WinSetDrawWindow(gOff);
        DrawItems(0, 0, r.extent.x, r.extent.y);
        WinSetDrawWindow(old);
        RctSetRectangle(&src, 0, 0, r.extent.x, r.extent.y);
        WinCopyRectangle(gOff, NULL, &src, r.topLeft.x, r.topLeft.y, winPaint);
    } else {
        WinSetClip(&r);
        DrawItems(r.topLeft.x, r.topLeft.y, r.extent.x, r.extent.y);
        WinResetClip();
    }
}

static Boolean GadgetHandler(FormGadgetTypeInCallback *gadgetP, UInt16 cmd, void *paramP)
{
    if (cmd == formGadgetDrawCmd) {
        DrawList();
        return true;
    }
    return false;
}

static void SetTitleTrigger(void)
{
    ControlPtr ctl = (ControlPtr)GetObjectPtr(MainKindTrigger);
    StrNCopyZ(gTriggerLabel, gTL.view.title, sizeof(gTriggerLabel));
    CtlSetLabel(ctl, gTriggerLabel);
}

static void ShowBack(FormPtr frm)
{
    UInt16 idx = FrmGetObjectIndex(frm, MainBackButton);
    if (gTL.historyLen)
        FrmShowObject(frm, idx);
    else
        FrmHideObject(frm, idx);
}

static void LayoutForm(FormPtr frm, Coord W, Coord H)
{
    RectangleType r;
    UInt16 idx;
    Coord by = H - kButtonsH + 2, x;

    SizeObject(frm, MainGadget, 0, kTitleH, W - kScrollW - 1, H - kTitleH - kButtonsH);
    SizeObject(frm, MainScroll, W - kScrollW, kTitleH, kScrollW, H - kTitleH - kButtonsH);

    idx = FrmGetObjectIndex(frm, MainKindTrigger);
    FrmGetObjectBounds(frm, idx, &r);
    r.topLeft.x = W - r.extent.x - 1;
    FrmSetObjectBounds(frm, idx, &r);

    x = 1;
    MoveObject(frm, MainNewButton, x, by);
    FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, MainNewButton), &r);
    x += r.extent.x + 5;
    MoveObject(frm, MainReloadButton, x, by);
    FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, MainReloadButton), &r);
    x += r.extent.x + 5;
    MoveObject(frm, MainOlderButton, x, by);
    FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, MainBackButton), &r);
    MoveObject(frm, MainBackButton, W - r.extent.x - 2, by);
}

static void ShowLoading(const char *msg)
{
    RectangleType r;
    GetListBounds(&r);
    if (gTL.numItems) {
        /* small banner at the bottom of the list so the content stays visible */
        RectangleType b;
        Int16 len = StrLen(msg);
        FntSetFont(boldFont);
        RctSetRectangle(&b, r.topLeft.x + r.extent.x / 2 - 40, r.topLeft.y + r.extent.y - 14, 80, 13);
        WinEraseRectangle(&b, 3);
        WinDrawRectangleFrame(roundFrame, &b);
        WinDrawChars(msg, len, b.topLeft.x + (80 - FntCharsWidth(msg, len)) / 2, b.topLeft.y);
        FntSetFont(stdFont);
    } else
        RenderMessage(&r, msg);
}

static void Load(Boolean older)
{
    Int16 oldCount = gTL.numItems;
    Err err;
    ShowLoading(older ? "Loading older..." : "Loading...");
    err = TLLoad(older);
    if (err)
        ShowError(NetLastError());
    else if (older && gTL.numItems == oldCount)
        ShowInfo("No older posts.");
    gIdleDone = false;
    if (!err && !older) {
        RectangleType r;
        GetListBounds(&r);
        Layout(r.extent.x);
        gTL.scroll = 0;
        if (gTL.focus >= 0 && gTL.focus < (Int16)gTL.numItems)
            gTL.scroll = gTL.items[gTL.focus].y;
    }
    DrawList();
}

static void ScrollBy(Int16 delta)
{
    RectangleType r;
    Int16 before = gTL.scroll;
    GetListBounds(&r);
    gTL.scroll += delta;
    ClampScroll(r.extent.y);
    if (gTL.scroll != before)
        DrawList();
    else if (delta > 0 && gTL.cursor[0] && gTL.view.kind != kindThread)
        Load(true);  /* paging past the end fetches older posts */
}

static Int16 ItemAt(Coord ly)
{
    UInt16 i;
    for (i = 0; i < gTL.numItems; i++)
        if (ly >= gTL.items[i].y && ly < gTL.items[i].y + gTL.items[i].height)
            return i;
    return -1;
}

static void OpenItem(Int16 i, Coord dx, Coord dy)
{
    ItemType *it;
    RectangleType r;
    Int16 media;

    if (i < 0)
        return;
    it = &gTL.items[i];
    GetListBounds(&r);
    media = RenderMediaHit(it, r.extent.x, ItemFlags(i), dx, dy);
    if (media >= 0 && gPrefs.loadImages && !ProtoHasFlag(it, 'S')) {
        gViewer.item = it;
        gViewer.index = media;
        FrmPopupForm(ViewerForm);
        return;
    }
    if (it->f[fKind][0] == 'N') {
        if (it->f[fAcctId][0]) {
            TLSetView(kindUser, it->f[fAcctId], it->f[fName], true);
            Load(false);
            ShowBack(FrmGetActiveForm());
            SetTitleTrigger();
        }
        return;
    }
    gTL.selected = i;
    FrmGotoForm(DetailForm);
}

/* Pen handling: drag scrolls the list, a tap opens the post. */
static Boolean PenDown(EventType *e)
{
    RectangleType r;
    Coord x = e->screenX, y = e->screenY, lastY = y, nx, ny;
    Boolean down = true, dragged = false;

    GetListBounds(&r);
    if (!RctPtInRectangle(x, y, &r))
        return false;
    while (down) {
        EvtGetPen(&nx, &ny, &down);
        if (!dragged && (ny - y > kDragSlop || y - ny > kDragSlop))
            dragged = true;
        if (dragged && ny != lastY) {
            Int16 before = gTL.scroll;
            gTL.scroll -= (ny - lastY);
            ClampScroll(r.extent.y);
            if (gTL.scroll != before)
                DrawList();
            lastY = ny;
        }
        if (down)
            SysTaskDelay(1);
    }
    if (!dragged) {
        Coord ly = y - r.topLeft.y + gTL.scroll;
        Int16 i = ItemAt(ly);
        if (i >= 0)
            OpenItem(i, x - r.topLeft.x, ly - gTL.items[i].y);
    } else
        gIdleDone = false;
    return true;
}

static void SwitchKind(UInt8 kind)
{
    TLSetView(kind, NULL, NULL, false);
    gTL.needsLoad = false;
    SetTitleTrigger();
    ShowBack(FrmGetActiveForm());
    Load(false);
}

/* Idle-time work: fetch one missing image for a visible item. */
Boolean MainIdle(void)
{
    RectangleType r;
    UInt16 i;
    if (gIdleDone || FrmGetActiveFormID() != MainForm)
        return false;
    GetListBounds(&r);
    for (i = 0; i < gTL.numItems; i++) {
        ItemType *it = &gTL.items[i];
        Int16 top = it->y - gTL.scroll;
        if (top + it->height <= 0)
            continue;
        if (top >= r.extent.y)
            break;
        if (RenderFetchOne(it, r.extent.x, ItemFlags(i))) {
            DrawList();
            return true;
        }
    }
    gIdleDone = true;
    return false;
}

static Boolean DoMenu(UInt16 id)
{
    switch (id) {
    case MenuHome:          SwitchKind(kindHome); return true;
    case MenuLocal:         SwitchKind(kindLocal); return true;
    case MenuFederated:     SwitchKind(kindPublic); return true;
    case MenuNotifications: SwitchKind(kindNotif); return true;
    case MenuMentions:      SwitchKind(kindMentions); return true;
    case MenuBookmarks:     SwitchKind(kindBookmarks); return true;
    case MenuReload:        Load(false); return true;
    case MenuTop:
        gTL.scroll = 0;
        DrawList();
        return true;
    case MenuPrefs:
        if (PrefsRun()) {
            ThumbFlush();
            gTL.layoutWidth = 0;
            Load(false);
        } else
            DrawList();
        return true;
    case MenuLogin:
        if (LoginRun())
            Load(false);
        else
            DrawList();
        return true;
    case MenuAbout:
        FrmAlert(AboutAlert);
        return true;
    }
    return false;
}

Boolean MainFormHandleEvent(EventType *e)
{
    FormPtr frm = FrmGetActiveForm();
    RectangleType r;
    Int16 dir;

    switch (e->eType) {
    case frmOpenEvent:
        DiaFormOpen(frm, true);
        DiaResizeForm(frm, &r);
        LayoutForm(frm, r.extent.x, r.extent.y);
        FrmSetGadgetHandler(frm, FrmGetObjectIndex(frm, MainGadget), GadgetHandler);
        SetTitleTrigger();
        ShowBack(frm);
        FrmDrawForm(frm);
        if (gTL.needsLoad || (!gTL.numItems && gPrefs.host[0] && gPrefs.key[0])) {
            gTL.needsLoad = false;
            Load(false);
        }
        gIdleDone = false;
        return true;

    case frmUpdateEvent:
        FrmDrawForm(frm);
        return true;

    case frmCloseEvent:
        FreeOffscreen();
        return false;

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
        gTL.scroll = e->data.sclRepeat.newValue;
        DrawList();
        return false;

    case sclExitEvent:
        gIdleDone = false;
        return false;

    case popSelectEvent:
        if (e->data.popSelect.controlID == MainKindTrigger) {
            SwitchKind((UInt8)e->data.popSelect.selection);
            return true;
        }
        return false;

    case ctlSelectEvent:
        switch (e->data.ctlSelect.controlID) {
        case MainNewButton:
            MemSet(&gCompose, sizeof(gCompose), 0);
            gCompose.vis = 'p';
            FrmGotoForm(ComposeForm);
            return true;
        case MainReloadButton:
            Load(false);
            return true;
        case MainOlderButton:
            if (gTL.view.kind == kindThread || !gTL.cursor[0])
                ShowInfo("No older posts.");
            else {
                Int16 first = gTL.numItems;
                Load(true);
                if (first < (Int16)gTL.numItems) {
                    gTL.scroll = gTL.items[first].y;
                    DrawList();
                }
            }
            return true;
        case MainBackButton:
            if (TLBack()) {
                gTL.needsLoad = false;
                SetTitleTrigger();
                ShowBack(frm);
                Load(false);
            }
            return true;
        }
        return false;

    case keyDownEvent:
        if (HandleNavKey(e, &dir)) {
            GetListBounds(&r);
            if (dir == navUp)
                ScrollBy(-(r.extent.y - 20));
            else if (dir == navDown)
                ScrollBy(r.extent.y - 20);
            else if (dir == navSelect) {
                Int16 i = ItemAt(gTL.scroll + 1);
                if (i >= 0 && gTL.items[i].y < gTL.scroll && i + 1 < (Int16)gTL.numItems)
                    i++;  /* prefer the first fully visible post */
                OpenItem(i, -1, -1);
            } else if (dir == navLeft && gTL.historyLen) {
                EventType ev;
                MemSet(&ev, sizeof(ev), 0);
                ev.eType = ctlSelectEvent;
                ev.data.ctlSelect.controlID = MainBackButton;
                EvtAddEventToQueue(&ev);
            }
            return true;
        }
        return false;

    case menuEvent:
        return DoMenu(e->data.menu.itemID);

    default:
        return false;
    }
}
