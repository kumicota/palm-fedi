/* Timeline form: scrolling list of posts drawn into a gadget. */
#include "PalmFedi.h"

#define kTitleH   15
#define kButtonsH 15
#define kScrollW  7
#define kDragSlop 4

static WinHandle gOff = NULL;          /* offscreen buffer for flicker-free scroll */
static Coord gOffW = 0, gOffH = 0;
static Int16 gOffScroll = 0;           /* scroll offset the buffer shows */
static Boolean gOffValid = false;      /* buffer holds the current list */
static char gTriggerLabel[32];
static Boolean gIdleDone = true;

static void GetListBounds(RectangleType *r)
{
    FormPtr frm = FrmGetActiveForm();
    FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, MainGadget), r);
}

static UInt16 ItemFlags(UInt16 i)
{
    return ((gTL.focus == (Int16)i) ? renderFocus : 0) |
           ((gTL.hl == (Int16)i) ? renderSelected : 0);
}

/* Recompute heights and y positions (only dirty items are re-measured).
 * Returns true if anything moved. */
static Boolean Layout(Coord w)
{
    UInt16 i;
    Int16 y = 0;
    Boolean all = (w != gTL.layoutWidth), changed = all;
    for (i = 0; i < gTL.numItems; i++) {
        ItemType *it = &gTL.items[i];
        if (all || it->height < 0) {
            it->height = RenderItem(it, 0, 0, w, ItemFlags(i), NULL);
            changed = true;
        }
        it->y = y;
        y += it->height;
    }
    gTL.totalHeight = y;
    gTL.layoutWidth = w;
    return changed;
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
    gOffValid = false;
}

/* Draw the items that overlap area (draw-window coordinates, also the clip);
 * the top-left of the list view is at (ox, oy). */
static void DrawItems(Coord ox, Coord oy, Coord w, const RectangleType *area)
{
    UInt16 i;
    Int16 from = area->topLeft.y - oy, to = from + area->extent.y;
    WinSetClip(area);
    WinEraseRectangle(area, 0);
    for (i = 0; i < gTL.numItems; i++) {
        ItemType *it = &gTL.items[i];
        Int16 top = it->y - gTL.scroll;
        if (top + it->height <= from)
            continue;
        if (top >= to)
            break;
        RenderItem(it, ox, oy + top, w, renderDraw | ItemFlags(i), area);
    }
    WinResetClip();
}

/* Redraw view rows [top, top + h) and put them on screen. */
static void Repaint(Int16 top, Int16 h)
{
    RectangleType r, area;

    GetListBounds(&r);
    if (top < 0) {
        h += top;
        top = 0;
    }
    if (top + h > r.extent.y)
        h = r.extent.y - top;
    if (h <= 0 || !gTL.numItems)
        return;
    if (gOff && gOffValid) {
        WinHandle old = WinSetDrawWindow(gOff);
        RctSetRectangle(&area, 0, top, r.extent.x, h);
        DrawItems(0, 0, r.extent.x, &area);
        WinSetDrawWindow(old);
        WinCopyRectangle(gOff, NULL, &area, r.topLeft.x, r.topLeft.y + top, winPaint);
    } else {
        RctSetRectangle(&area, r.topLeft.x, r.topLeft.y + top, r.extent.x, h);
        DrawItems(r.topLeft.x, r.topLeft.y, r.extent.x, &area);
    }
}

static void RepaintItem(Int16 i)
{
    if (i >= 0 && i < (Int16)gTL.numItems)
        Repaint(gTL.items[i].y - gTL.scroll, gTL.items[i].height);
}

/* Full redraw of the list. */
static void DrawList(void)
{
    RectangleType r;
    Err err;

    GetListBounds(&r);
    Layout(r.extent.x);
    ClampScroll(r.extent.y);
    UpdateScrollBar(r.extent.y);
    gIdleDone = false;  /* newly visible posts may need images */

    if (!gTL.numItems) {
        gOffValid = false;
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
    gOffValid = (gOff != NULL);
    gOffScroll = gTL.scroll;
    Repaint(0, r.extent.y);
}

/* Show gTL.scroll after it changed. The pixels already drawn are shifted
 * and only the rows that scrolled into view are drawn, which keeps dragging
 * smooth even with many images on screen. */
static void ScrollView(void)
{
    RectangleType r, all, vacated;
    Int16 d;
    WinHandle old;

    GetListBounds(&r);
    if (Layout(r.extent.x) || !gOff || !gOffValid) {
        DrawList();
        return;
    }
    ClampScroll(r.extent.y);
    d = gTL.scroll - gOffScroll;
    if (!d)
        return;
    if (d >= r.extent.y || -d >= r.extent.y) {
        DrawList();
        return;
    }
    UpdateScrollBar(r.extent.y);
    gIdleDone = false;
    old = WinSetDrawWindow(gOff);
    RctSetRectangle(&all, 0, 0, r.extent.x, r.extent.y);
    WinScrollRectangle(&all, d > 0 ? winUp : winDown, d > 0 ? d : -d, &vacated);
    gOffScroll = gTL.scroll;
    DrawItems(0, 0, r.extent.x, &vacated);
    WinSetDrawWindow(old);
    WinCopyRectangle(gOff, NULL, &all, r.topLeft.x, r.topLeft.y, winPaint);
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

static Err Load(Boolean older)
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
    return err;
}

static Boolean CanPageOlder(void)
{
    return gTL.cursor[0] && gTL.view.kind != kindThread && gTL.view.kind != kindSearch;
}

/* Scroll so item i shows: all of it if it fits, else its top (or, coming
 * from below, its end). */
static void Reveal(Int16 i, Coord viewH, Boolean fromBelow)
{
    ItemType *it = &gTL.items[i];
    Int16 bottom = it->y + it->height;
    if (it->height <= viewH) {
        if (it->y < gTL.scroll)
            gTL.scroll = it->y;
        else if (bottom > gTL.scroll + viewH)
            gTL.scroll = bottom - viewH;
    } else if (!fromBelow)
        gTL.scroll = it->y;
    else if (bottom <= gTL.scroll)
        gTL.scroll = bottom - viewH;
}

/* First (or last) item at least partly on screen. */
static Int16 VisibleItem(Coord viewH, Boolean last)
{
    Int16 i, found = -1;
    for (i = 0; i < (Int16)gTL.numItems; i++) {
        ItemType *it = &gTL.items[i];
        if (it->y + it->height <= gTL.scroll)
            continue;
        if (it->y >= gTL.scroll + viewH)
            break;
        /* prefer a post whose top is on screen */
        if (!last && found < 0 && it->y < gTL.scroll && i + 1 < (Int16)gTL.numItems &&
            gTL.items[i + 1].y < gTL.scroll + viewH)
            continue;
        found = i;
        if (!last)
            break;
    }
    return found;
}

static Boolean OnScreen(Int16 i, Coord viewH)
{
    return i >= 0 && i < (Int16)gTL.numItems &&
           gTL.items[i].y + gTL.items[i].height > gTL.scroll &&
           gTL.items[i].y < gTL.scroll + viewH;
}

/* 5-way up/down: move the highlight one post; a post taller than the
 * screen is scrolled through a page at a time first. */
static void NavMove(Int16 dir)
{
    RectangleType r;
    Coord viewH;
    Int16 old = gTL.hl, step;

    if (!gTL.numItems)
        return;
    GetListBounds(&r);
    viewH = r.extent.y;
    step = viewH > 30 ? viewH - 20 : viewH;
    Layout(r.extent.x);

    if (!OnScreen(old, viewH)) {
        gTL.hl = VisibleItem(viewH, dir == navUp);
    } else {
        ItemType *it = &gTL.items[old];
        if (dir == navDown) {
            Int16 below = it->y + it->height - (gTL.scroll + viewH);
            if (below > 0)
                gTL.scroll += below < step ? below : step;
            else if (old + 1 < (Int16)gTL.numItems) {
                gTL.hl = old + 1;
                Reveal(gTL.hl, viewH, false);
            } else if (CanPageOlder()) {
                /* past the end: fetch older posts and step onto the first */
                Load(true);
                if (gTL.hl >= 0 && gTL.hl + 1 < (Int16)gTL.numItems) {
                    gTL.hl++;
                    Reveal(gTL.hl, viewH, false);
                }
                DrawList();
                return;
            }
        } else {
            Int16 above = gTL.scroll - it->y;
            if (above > 0)
                gTL.scroll -= above < step ? above : step;
            else if (old > 0) {
                gTL.hl = old - 1;
                Reveal(gTL.hl, viewH, true);
            }
        }
    }
    ScrollView();
    if (gTL.hl != old) {
        RepaintItem(old);
        RepaintItem(gTL.hl);
    }
}

static Int16 ItemAt(Coord ly)
{
    UInt16 i;
    for (i = 0; i < gTL.numItems; i++)
        if (ly >= gTL.items[i].y && ly < gTL.items[i].y + gTL.items[i].height)
            return i;
    return -1;
}

/* Load a view just pushed with TLSetView. If that fails (e.g. "Nothing
 * found") go back: the list still holds the previous view's posts. */
static void LoadPushed(void)
{
    gTL.needsLoad = false;
    SetTitleTrigger();
    ShowBack(FrmGetActiveForm());
    if (Load(false) != errNone && TLBack()) {
        gTL.needsLoad = false;
        SetTitleTrigger();
        ShowBack(FrmGetActiveForm());
        DrawList();
    }
}

/* Open a profile or hashtag. */
static void ShowView(UInt8 kind, const char *target, const char *title)
{
    TLSetView(kind, target, title, true);  /* copies the strings before Load frees them */
    LoadPushed();
}

static void Search(void)
{
    if (SearchRun())
        LoadPushed();
    else {
        SetTitleTrigger();
        DrawList();
    }
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
    if (it->f[fKind][0] == 'N') {  /* follow notification, account search result */
        if (it->f[fAcctId][0])
            ShowView(kindUser, it->f[fAcctId], it->f[fName]);
        return;
    }
    if (it->f[fKind][0] == 'T') {  /* hashtag search result */
        if (it->f[fId][0])
            ShowView(kindTag, it->f[fId], it->f[fName]);
        return;
    }
    gTL.selected = i;
    gTL.hl = i;  /* still marked when you come back */
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
            gTL.scroll -= (ny - lastY);
            ScrollView();
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
            RepaintItem(i);  /* only this post changed */
            return true;
        }
    }
    gIdleDone = true;
    return false;
}

static UInt32 HeapFreeKB(void)
{
    UInt32 freeBytes = 0, maxChunk = 0;
    MemHeapFreeBytes(0, &freeBytes, &maxChunk);  /* heap 0: dynamic heap */
    return freeBytes / 1024;
}

/* Drop cached images and the drawing buffer, then report the heap. The
 * posts stay; their images load again as they come into view. */
static void FreeMemory(void)
{
    char msg[100], num[12];
    UInt32 before = HeapFreeKB(), after;

    ThumbFlush();
    FreeOffscreen();
    MemHeapCompact(0);
    DrawList();  /* re-creates the drawing buffer; count it as in use */
    after = HeapFreeKB();
    StrCopy(msg, "Released ");
    StrIToA(num, after > before ? after - before : 0);
    StrCat(msg, num);
    StrCat(msg, " KB.\nFree memory: ");
    StrIToA(num, after);
    StrCat(msg, num);
    StrCat(msg, " KB of ");
    StrIToA(num, MemHeapSize(0) / 1024);
    StrCat(msg, num);
    StrCat(msg, " KB.");
    ShowInfo(msg);
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
    case MenuSearch:        Search(); return true;
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
    case MenuFreeMemory:
        FreeMemory();
        return true;
    case MenuExit:
        AppExit();
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
        ScrollView();
        return false;

    case sclExitEvent:
        gIdleDone = false;
        return false;

    case popSelectEvent:
        if (e->data.popSelect.controlID == MainKindTrigger) {
            if (e->data.popSelect.selection == kKindListSearch)
                Search();
            else if (e->data.popSelect.selection >= 0 &&
                     e->data.popSelect.selection < kKindListSearch)
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
            if (dir == navUp || dir == navDown)
                NavMove(dir);
            else if (dir == navSelect) {
                Int16 i = OnScreen(gTL.hl, r.extent.y) ? gTL.hl
                                                         : VisibleItem(r.extent.y, false);
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
