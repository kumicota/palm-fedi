/* Small UI helpers and Dynamic Input Area (DIA) / 5-way support. */
#include "PalmFedi.h"
#include <PenInputMgr.h>

/* palmOne 5-way navigator (vchrNavChange); defined here to avoid needing
 * the palmOne SDK headers. */
#ifndef vchrNavChange
#define vchrNavChange      0x0309
#endif
#define navBitUp           0x0001
#define navBitDown         0x0002
#define navBitLeft         0x0004
#define navBitRight        0x0008
#define navBitSelect       0x0010
#define navChangeSelect    0x1000

static Boolean gHasPins = false;

void *GetObjectPtr(UInt16 id)
{
    FormPtr frm = FrmGetActiveForm();
    return FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, id));
}

void SetFieldText(UInt16 id, const char *text)
{
    FieldPtr fld = (FieldPtr)GetObjectPtr(id);
    MemHandle oldH = FldGetTextHandle(fld);
    UInt16 len = text ? StrLen(text) : 0;
    MemHandle h = MemHandleNew(len + 1);

    if (!h)
        return;
    {
        char *p = (char *)MemHandleLock(h);
        if (len)
            MemMove(p, text, len);
        p[len] = 0;
        MemHandleUnlock(h);
    }
    FldSetTextHandle(fld, h);
    if (oldH)
        MemHandleFree(oldH);
}

const char *GetFieldText(UInt16 id)
{
    const char *t = FldGetTextPtr((FieldPtr)GetObjectPtr(id));
    return t ? t : "";
}

void ShowError(const char *msg)
{
    FrmCustomAlert(ErrorAlert, msg ? msg : "Unknown error", "", "");
}

void ShowInfo(const char *msg)
{
    FrmCustomAlert(InfoAlert, msg, "", "");
}

void StrNCopyZ(char *dst, const char *src, UInt16 size)
{
    UInt16 n = 0;
    if (!size)
        return;
    if (src)
        while (src[n] && n + 1 < size) {
            dst[n] = src[n];
            n++;
        }
    dst[n] = 0;
}

void StrNCatZ(char *dst, const char *src, UInt16 size)
{
    UInt16 len = StrLen(dst);
    if (len < size)
        StrNCopyZ(dst + len, src, size - len);
}

/* ------------------------------------------------------------------------
 * Dynamic Input Area: lets the LifeDrive collapse Graffiti and use the full
 * 320x480 screen (160x225 standard coordinates), and rotate to landscape.
 * --------------------------------------------------------------------- */
void DiaInit(void)
{
    UInt32 version;
    gHasPins = (FtrGet(pinCreator, pinFtrAPIVersion, &version) == errNone && version);
}

void DiaFormOpen(FormPtr frm, Boolean resizable)
{
    if (!gHasPins)
        return;
    if (resizable) {
        WinHandle win = FrmGetWindowHandle(frm);
        FrmSetDIAPolicyAttr(frm, frmDIAPolicyCustom);
        PINSetInputTriggerState(pinInputTriggerEnabled);
        WinSetConstraintsSize(win, 160, 225, 225, 160, 160, 240);
        PINSetInputAreaState(pinInputAreaUser);
    } else {
        FrmSetDIAPolicyAttr(frm, frmDIAPolicyStayOpen);
        PINSetInputTriggerState(pinInputTriggerDisabled);
    }
}

/* Fit the form's window to the current display. Returns true if the size
 * changed (caller must lay out its objects and redraw). */
Boolean DiaResizeForm(FormPtr frm, RectangleType *newBounds)
{
    RectangleType cur, disp;
    WinHandle win = FrmGetWindowHandle(frm);

    WinGetBounds(WinGetDisplayWindow(), &disp);
    WinGetBounds(win, &cur);
    *newBounds = disp;
    if (cur.extent.x == disp.extent.x && cur.extent.y == disp.extent.y &&
        cur.topLeft.x == disp.topLeft.x && cur.topLeft.y == disp.topLeft.y)
        return false;
    WinSetBounds(win, &disp);
    return true;
}

void MoveObject(FormPtr frm, UInt16 id, Coord x, Coord y)
{
    RectangleType r;
    UInt16 idx = FrmGetObjectIndex(frm, id);
    FrmGetObjectBounds(frm, idx, &r);
    r.topLeft.x = x;
    r.topLeft.y = y;
    FrmSetObjectBounds(frm, idx, &r);
}

void SizeObject(FormPtr frm, UInt16 id, Coord x, Coord y, Coord w, Coord h)
{
    RectangleType r;
    r.topLeft.x = x;
    r.topLeft.y = y;
    r.extent.x = w;
    r.extent.y = h;
    FrmSetObjectBounds(frm, FrmGetObjectIndex(frm, id), &r);
}

/* Translate hard keys / 5-way presses into a direction. */
Boolean HandleNavKey(EventType *e, Int16 *dir)
{
    WChar c;
    if (e->eType != keyDownEvent)
        return false;
    c = e->data.keyDown.chr;
    switch (c) {
    case vchrPageUp:
    case vchrRockerUp:
        *dir = navUp;
        return true;
    case vchrPageDown:
    case vchrRockerDown:
        *dir = navDown;
        return true;
    case vchrRockerLeft:
        *dir = navLeft;
        return true;
    case vchrRockerRight:
        *dir = navRight;
        return true;
    case vchrRockerCenter:
        *dir = navSelect;
        return true;
    case vchrNavChange:
        if (e->data.keyDown.keyCode & navChangeSelect) {
            *dir = navSelect;
            return true;
        }
        if (e->data.keyDown.keyCode & navBitLeft) {
            *dir = navLeft;
            return true;
        }
        if (e->data.keyDown.keyCode & navBitRight) {
            *dir = navRight;
            return true;
        }
        if (e->data.keyDown.keyCode & navBitUp) {
            *dir = navUp;
            return true;
        }
        if (e->data.keyDown.keyCode & navBitDown) {
            *dir = navDown;
            return true;
        }
        break;
    }
    return false;
}
