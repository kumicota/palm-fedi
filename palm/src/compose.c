/* Compose / reply form. */
#include "PalmFedi.h"

#define kMaxPostChars 4000

ComposeArgs gCompose;

static char gVisLabel[16];
static char gCountLabel[8];
static const char kVisCodes[4] = { 'p', 'u', 'k', 'd' };

static void SetVis(char code)
{
    ListPtr lst = (ListPtr)GetObjectPtr(ComposeVisList);
    UInt16 i;
    for (i = 0; i < 4; i++)
        if (kVisCodes[i] == code)
            break;
    if (i == 4)
        i = 0;
    gCompose.vis = kVisCodes[i];
    LstSetSelection(lst, i);
    StrNCopyZ(gVisLabel, LstGetSelectionText(lst, i), sizeof(gVisLabel));
    CtlSetLabel((ControlPtr)GetObjectPtr(ComposeVisTrigger), gVisLabel);
}

static void UpdateCount(void)
{
    FieldPtr fld = (FieldPtr)GetObjectPtr(ComposeTextField);
    FieldPtr cw = (FieldPtr)GetObjectPtr(ComposeCWField);
    FormPtr frm = FrmGetActiveForm();
    UInt16 n = FldGetTextLength(fld) + FldGetTextLength(cw);
    StrIToA(gCountLabel, n);
    FrmHideObject(frm, FrmGetObjectIndex(frm, ComposeCountLabel));
    FrmCopyLabel(frm, ComposeCountLabel, gCountLabel);
    FrmShowObject(frm, FrmGetObjectIndex(frm, ComposeCountLabel));
}

static void UpdateScroll(void)
{
    FieldPtr fld = (FieldPtr)GetObjectPtr(ComposeTextField);
    UInt16 pos, textH, fieldH;
    Int16 maxV;
    FldGetScrollValues(fld, &pos, &textH, &fieldH);
    maxV = (textH > fieldH) ? (Int16)(textH - fieldH) : 0;
    if (pos > maxV)
        maxV = pos;
    SclSetScrollBar((ScrollBarPtr)GetObjectPtr(ComposeTextScroll), pos, 0, maxV,
                    fieldH > 1 ? fieldH - 1 : 1);
}

static void LayoutForm(FormPtr frm, Coord W, Coord H)
{
    RectangleType r;
    Coord by = H - 13;
    UInt16 idx;

    idx = FrmGetObjectIndex(frm, ComposeCWField);
    FrmGetObjectBounds(frm, idx, &r);
    r.extent.x = W - r.topLeft.x - 2;
    FrmSetObjectBounds(frm, idx, &r);

    /* text field fills the space between the CW line and the buttons;
     * its height must be a whole number of lines */
    {
        Coord top = r.topLeft.y + r.extent.y + 4;
        Coord lh, h;
        FntSetFont(stdFont);
        lh = FntLineHeight();
        h = ((by - 3 - top) / lh) * lh;
        SizeObject(frm, ComposeTextField, 1, top, W - 10, h);
        SizeObject(frm, ComposeTextScroll, W - 7, top, 7, h);
    }
    MoveObject(frm, ComposePostButton, 1, by);
    FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, ComposePostButton), &r);
    MoveObject(frm, ComposeCancelButton, r.topLeft.x + r.extent.x + 5, by);
    FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, ComposeCancelButton), &r);
    MoveObject(frm, ComposeVisTrigger, r.topLeft.x + r.extent.x + 6, by);
    FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, ComposeCountLabel), &r);
    MoveObject(frm, ComposeCountLabel, W - 24, by + 1);
}

static void Done(Boolean posted)
{
    if (gCompose.prefill) {
        MemPtrFree(gCompose.prefill);
        gCompose.prefill = NULL;
    }
    if (posted && (gTL.view.kind == kindHome || gTL.view.kind == kindThread))
        gTL.needsLoad = true;
    FrmGotoForm(gCompose.replyTo[0] && !posted ? DetailForm : MainForm);
}

static void Post(void)
{
    const char *text = GetFieldText(ComposeTextField);
    const char *cw = GetFieldText(ComposeCWField);
    UInt16 size = StrLen(text) * 3 + StrLen(cw) * 3 + 120;
    char *body, *buf, vis[2];
    UInt32 len;
    Err err;

    if (!text[0]) {
        ShowError("Write something first.");
        return;
    }
    body = (char *)MemPtrNew(size);
    if (!body) {
        ShowError("Out of memory");
        return;
    }
    body[0] = 0;
    vis[0] = gCompose.vis;
    vis[1] = 0;
    UrlAdd(body, size, "k", gPrefs.key);
    UrlAdd(body, size, "vis", vis);
    if (gCompose.replyTo[0])
        UrlAdd(body, size, "reply", gCompose.replyTo);
    if (cw[0])
        UrlAdd(body, size, "cw", cw);
    UrlAdd(body, size, "text", text);

    err = HttpFetch("POST", "/p/post", body, &buf, &len);
    MemPtrFree(body);
    if (err) {
        ShowError(NetLastError());
        return;
    }
    MemPtrFree(buf);
    Done(true);
}

static Boolean EditMenu(UInt16 id)
{
    FormPtr frm = FrmGetActiveForm();
    UInt16 focus = FrmGetFocus(frm);
    FieldPtr fld;
    if (focus == noFocus || FrmGetObjectType(frm, focus) != frmFieldObj)
        fld = (FieldPtr)GetObjectPtr(ComposeTextField);
    else
        fld = (FieldPtr)FrmGetObjectPtr(frm, focus);
    switch (id) {
    case MenuEditUndo:      FldUndo(fld); break;
    case MenuEditCut:       FldCut(fld); break;
    case MenuEditCopy:      FldCopy(fld); break;
    case MenuEditPaste:     FldPaste(fld); break;
    case MenuEditSelectAll: FldSetSelection(fld, 0, FldGetTextLength(fld)); break;
    case MenuEditKeyboard:  SysKeyboardDialog(kbdDefault); break;
    case MenuEditGraffiti:  SysGraffitiReferenceDialog(referenceDefault); break;
    default: return false;
    }
    UpdateCount();
    UpdateScroll();
    return true;
}

Boolean ComposeFormHandleEvent(EventType *e)
{
    FormPtr frm = FrmGetActiveForm();
    RectangleType r;

    switch (e->eType) {
    case frmOpenEvent:
        DiaFormOpen(frm, true);
        DiaResizeForm(frm, &r);
        LayoutForm(frm, r.extent.x, r.extent.y);
        FrmSetTitle(frm, gCompose.replyTo[0] ? "Reply" : "New post");
        FldSetMaxChars((FieldPtr)GetObjectPtr(ComposeTextField), kMaxPostChars);
        SetFieldText(ComposeTextField, gCompose.prefill ? gCompose.prefill : "");
        SetFieldText(ComposeCWField, gCompose.cw);
        SetVis(gCompose.vis ? gCompose.vis : 'p');
        FrmDrawForm(frm);
        FrmSetFocus(frm, FrmGetObjectIndex(frm, ComposeTextField));
        FldSetInsPtPosition((FieldPtr)GetObjectPtr(ComposeTextField),
                            StrLen(GetFieldText(ComposeTextField)));
        UpdateCount();
        UpdateScroll();
        return true;

    case frmCloseEvent:
        if (gCompose.prefill) {
            MemPtrFree(gCompose.prefill);
            gCompose.prefill = NULL;
        }
        return false;

    case winDisplayChangedEvent:
        if (DiaResizeForm(frm, &r)) {
            LayoutForm(frm, r.extent.x, r.extent.y);
            FldRecalculateField((FieldPtr)GetObjectPtr(ComposeTextField), false);
            FrmEraseForm(frm);
            FrmDrawForm(frm);
            UpdateScroll();
        }
        return true;

    case keyDownEvent:
        /* let the form/field handle it first, then refresh counters */
        FrmHandleEvent(frm, e);
        UpdateCount();
        UpdateScroll();
        return true;

    case fldChangedEvent:
        UpdateScroll();
        return false;

    case sclRepeatEvent: {
        FieldPtr fld = (FieldPtr)GetObjectPtr(ComposeTextField);
        Int16 delta = e->data.sclRepeat.newValue - e->data.sclRepeat.value;
        if (delta > 0)
            FldScrollField(fld, delta, winDown);
        else if (delta < 0)
            FldScrollField(fld, -delta, winUp);
        return false;
    }

    case popSelectEvent:
        if (e->data.popSelect.controlID == ComposeVisTrigger) {
            SetVis(kVisCodes[e->data.popSelect.selection & 3]);
            return true;
        }
        return false;

    case ctlSelectEvent:
        switch (e->data.ctlSelect.controlID) {
        case ComposePostButton:
            Post();
            return true;
        case ComposeCancelButton:
            if (FldGetTextLength((FieldPtr)GetObjectPtr(ComposeTextField)) >
                    StrLen(gCompose.prefill ? gCompose.prefill : "") &&
                FrmAlert(ConfirmDiscardAlert) != 0)
                return true;
            Done(false);
            return true;
        }
        return false;

    case menuEvent:
        return EditMenu(e->data.menu.itemID);

    default:
        return false;
    }
}
