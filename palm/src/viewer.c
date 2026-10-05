/* Full-screen image viewer (popup form over the list or detail form). */
#include "PalmFedi.h"

#define kTitleH   15
#define kButtonsH 15
#define kScrollW  7

ViewerArgs gViewer;

static PfImage *gImg = NULL;
static Int16 gScroll = 0, gContentH = 0;
static char gTitle[24];
static Boolean gNeedLoad = false;

static void Bounds(RectangleType *r)
{
    FormPtr frm = FrmGetActiveForm();
    FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, ViewerGadget), r);
}

static void FreeImage(void)
{
    ImgFree(gImg);
    gImg = NULL;
}

static const char *Alt(void)
{
    return gViewer.item ? gViewer.item->mediaAlt[gViewer.index] : "";
}

static void Draw(void)
{
    RectangleType r;
    Coord y, lh;
    Int16 maxScroll;
    const char *alt = Alt();

    Bounds(&r);
    WinSetClip(&r);
    WinEraseRectangle(&r, 0);
    y = r.topLeft.y - gScroll;
    if (gImg) {
        ImgDraw(gImg, r.topLeft.x + (r.extent.x - (Coord)gImg->stdW) / 2, y, &r);
        y += gImg->stdH + 3;
    } else if (!gNeedLoad) {
        RenderMessage(&r, "Can't show this image");
        y += 30;
    }
    if (gViewer.item && (gViewer.item->mediaType[gViewer.index] == 'V' ||
                         gViewer.item->mediaType[gViewer.index] == 'A')) {
        FntSetFont(boldFont);
        WinDrawChars("Preview only (media can't play here)", 36, r.topLeft.x + 2, y);
        y += FntLineHeight();
    }
    FntSetFont(stdFont);
    lh = FntLineHeight();
    while (*alt) {
        UInt16 n = FntWordWrap(alt, r.extent.x - 4), shown = n;
        if (!n)
            break;
        while (shown && (alt[shown - 1] == '\n' || alt[shown - 1] == ' '))
            shown--;
        WinDrawChars(alt, shown, r.topLeft.x + 2, y);
        y += lh;
        alt += n;
    }
    WinResetClip();

    gContentH = y + gScroll - r.topLeft.y;
    maxScroll = gContentH - r.extent.y;
    if (maxScroll < 0)
        maxScroll = 0;
    SclSetScrollBar((ScrollBarPtr)GetObjectPtr(ViewerScroll), gScroll, 0, maxScroll,
                    r.extent.y > 20 ? r.extent.y - 10 : r.extent.y);
}

static void ClampAndDraw(void)
{
    RectangleType r;
    Int16 maxScroll;
    Bounds(&r);
    maxScroll = gContentH - r.extent.y;
    if (gScroll > maxScroll)
        gScroll = maxScroll;
    if (gScroll < 0)
        gScroll = 0;
    Draw();
}

static void Load(void)
{
    RectangleType r;
    ItemType *it = gViewer.item;
    gNeedLoad = false;
    FreeImage();
    if (!it || gViewer.index >= it->numMedia)
        return;
    Bounds(&r);
    RenderMessage(&r, "Loading image...");
    /* native pixels: full width, up to two screens tall */
    gImg = ImgLoad(it->mediaKey[gViewer.index], r.extent.x * 2, r.extent.y * 4, false);
    if (!gImg)
        ThumbFlush();  /* maybe we ran out of memory: free thumbnails and retry once */
    if (!gImg)
        gImg = ImgLoad(it->mediaKey[gViewer.index], r.extent.x * 2, r.extent.y * 4, false);
    gScroll = 0;
    Draw();
}

static void SetTitleAndButtons(FormPtr frm)
{
    ItemType *it = gViewer.item;
    char num[8];
    UInt16 n = it ? it->numMedia : 0;

    StrCopy(gTitle, it && it->mediaType[gViewer.index] == 'V' ? "Video " :
                    it && it->mediaType[gViewer.index] == 'A' ? "Audio " : "Image ");
    StrIToA(num, gViewer.index + 1);
    StrCat(gTitle, num);
    StrCat(gTitle, " of ");
    StrIToA(num, n);
    StrCat(gTitle, num);
    FrmSetTitle(frm, gTitle);
    if (n > 1) {
        FrmShowObject(frm, FrmGetObjectIndex(frm, ViewerPrevButton));
        FrmShowObject(frm, FrmGetObjectIndex(frm, ViewerNextButton));
    } else {
        FrmHideObject(frm, FrmGetObjectIndex(frm, ViewerPrevButton));
        FrmHideObject(frm, FrmGetObjectIndex(frm, ViewerNextButton));
    }
}

static void LayoutForm(FormPtr frm, Coord W, Coord H)
{
    RectangleType r;
    Coord by = H - kButtonsH + 2;
    SizeObject(frm, ViewerGadget, 0, kTitleH, W - kScrollW - 1, H - kTitleH - kButtonsH);
    SizeObject(frm, ViewerScroll, W - kScrollW, kTitleH, kScrollW, H - kTitleH - kButtonsH);
    MoveObject(frm, ViewerDoneButton, 1, by);
    FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, ViewerNextButton), &r);
    MoveObject(frm, ViewerNextButton, W - r.extent.x - 2, by);
    MoveObject(frm, ViewerPrevButton, W - r.extent.x - 2 - 4 - r.extent.x, by);
}

static void Step(Int16 delta)
{
    ItemType *it = gViewer.item;
    if (!it || it->numMedia < 2)
        return;
    gViewer.index = (gViewer.index + it->numMedia + delta) % it->numMedia;
    SetTitleAndButtons(FrmGetActiveForm());
    Load();
}

static void Close(void)
{
    FreeImage();
    FrmReturnToForm(0);
}

static Boolean GadgetHandler(FormGadgetTypeInCallback *gadgetP, UInt16 cmd, void *paramP)
{
    if (cmd == formGadgetDrawCmd) {
        if (!gNeedLoad)
            Draw();
        return true;
    }
    return false;
}

Boolean ViewerFormHandleEvent(EventType *e)
{
    FormPtr frm = FrmGetActiveForm();
    RectangleType r;
    Int16 dir;

    switch (e->eType) {
    case frmOpenEvent:
        DiaFormOpen(frm, true);
        DiaResizeForm(frm, &r);
        LayoutForm(frm, r.extent.x, r.extent.y);
        FrmSetGadgetHandler(frm, FrmGetObjectIndex(frm, ViewerGadget), GadgetHandler);
        SetTitleAndButtons(frm);
        gNeedLoad = true;
        FrmDrawForm(frm);
        Load();
        return true;

    case winDisplayChangedEvent:
        if (DiaResizeForm(frm, &r)) {
            LayoutForm(frm, r.extent.x, r.extent.y);
            FrmEraseForm(frm);
            FrmDrawForm(frm);
        }
        return true;

    case penDownEvent: {
        Coord x = e->screenX, y = e->screenY, lastY = y, nx, ny;
        Boolean down = true;
        Bounds(&r);
        if (!RctPtInRectangle(x, y, &r))
            return false;
        while (down) {
            EvtGetPen(&nx, &ny, &down);
            if (ny != lastY) {
                gScroll -= ny - lastY;
                ClampAndDraw();
                lastY = ny;
            }
            if (down)
                SysTaskDelay(1);
        }
        return true;
    }

    case sclRepeatEvent:
        gScroll = e->data.sclRepeat.newValue;
        Draw();
        return false;

    case ctlSelectEvent:
        switch (e->data.ctlSelect.controlID) {
        case ViewerDoneButton: Close(); return true;
        case ViewerPrevButton: Step(-1); return true;
        case ViewerNextButton: Step(1); return true;
        }
        return false;

    case keyDownEvent:
        if (HandleNavKey(e, &dir)) {
            Bounds(&r);
            if (dir == navUp || dir == navDown) {
                gScroll += dir * (r.extent.y - 20);
                ClampAndDraw();
            } else if (dir == navLeft)
                Step(-1);
            else if (dir == navRight)
                Step(1);
            else
                Close();
            return true;
        }
        return false;

    case frmCloseEvent:
        FreeImage();
        return false;

    default:
        return false;
    }
}
