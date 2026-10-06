/* Preferences and login dialogs (modal). */
#include "PalmFedi.h"

/* This file is rarely used code: it lives in the second code segment
 * (see PalmFedi.def). */
static void SetCheck(UInt16 id, Boolean on) SEG_DIALOGS;
static Boolean GetCheck(UInt16 id) SEG_DIALOGS;
static void SetPopup(UInt16 triggerID, UInt16 listID, UInt16 sel, char *label, UInt16 size) SEG_DIALOGS;
static void ReadFields(void) SEG_DIALOGS;
static void TestConnection(void) SEG_DIALOGS;
static Boolean PrefsHandleEvent(EventType *e) SEG_DIALOGS;

PrefsType gPrefs;

static char gDepthLabel[16];
static char gFontLabel[16];
static Boolean gLoggedIn;

static void SetCheck(UInt16 id, Boolean on)
{
    CtlSetValue((ControlPtr)GetObjectPtr(id), on);
}

static Boolean GetCheck(UInt16 id)
{
    return CtlGetValue((ControlPtr)GetObjectPtr(id)) != 0;
}

static void SetPopup(UInt16 triggerID, UInt16 listID, UInt16 sel, char *label, UInt16 size)
{
    ListPtr lst = (ListPtr)GetObjectPtr(listID);
    LstSetSelection(lst, sel);
    StrNCopyZ(label, LstGetSelectionText(lst, sel), size);
    CtlSetLabel((ControlPtr)GetObjectPtr(triggerID), label);
}

/* Copy the dialog's fields into gPrefs. */
static void ReadFields(void)
{
    const char *key;
    UInt16 i;
    StrNCopyZ(gPrefs.host, GetFieldText(PrefsHostField), sizeof(gPrefs.host));
    gPrefs.port = (UInt16)StrAToI(GetFieldText(PrefsPortField));
    if (!gPrefs.port)
        gPrefs.port = 8080;
    key = GetFieldText(PrefsKeyField);
    for (i = 0; key[i] && i + 1 < sizeof(gPrefs.key); i++)
        gPrefs.key[i] = (key[i] >= 'a' && key[i] <= 'z') ? key[i] - 32 : key[i];
    gPrefs.key[i] = 0;
    gPrefs.loadImages = GetCheck(PrefsImagesCheck);
    gPrefs.showAvatars = GetCheck(PrefsAvatarsCheck);
    gPrefs.depth = LstGetSelection((ListPtr)GetObjectPtr(PrefsDepthList)) == 1 ? 8 : 16;
    gPrefs.bodyFont = LstGetSelection((ListPtr)GetObjectPtr(PrefsFontList)) == 1 ? 1 : 0;
}

static void TestConnection(void)
{
    char url[64], msg[120], *buf, *rec[1], *f[3];
    UInt32 len;
    PrefsType saved = gPrefs;

    ReadFields();
    UrlInit(url, sizeof(url), "/p/me");
    UrlAdd(url, sizeof(url), "k", gPrefs.key);
    if (HttpFetch("GET", url, NULL, &buf, &len) != errNone) {
        ShowError(NetLastError());
    } else {
        ProtoSplit(buf, len, rec, 1);
        ProtoFields(rec[0], f, 3);
        StrCopy(msg, "Connected as ");
        StrNCatZ(msg, f[1], sizeof(msg));
        ShowInfo(msg);
        StrNCopyZ(saved.acct, f[1], sizeof(saved.acct));
        MemPtrFree(buf);
    }
    gPrefs = saved;  /* only OK commits the edits */
}

static Boolean PrefsHandleEvent(EventType *e)
{
    if (e->eType == ctlSelectEvent) {
        switch (e->data.ctlSelect.controlID) {
        case PrefsTestButton:
            TestConnection();
            return true;
        case PrefsLoginButton: {
            PrefsType saved;
            ReadFields();
            saved = gPrefs;
            if (LoginRun()) {
                SetFieldText(PrefsKeyField, gPrefs.key);
                FldDrawField((FieldPtr)GetObjectPtr(PrefsKeyField));
                saved = gPrefs;
                gLoggedIn = true;
            }
            gPrefs = saved;
            return true;
        }
        }
    }
    return false;
}

Boolean PrefsRun(void)
{
    FormPtr prev = FrmGetActiveForm(), frm;
    PrefsType before = gPrefs;
    char port[8];
    UInt16 hit;

    frm = FrmInitForm(PrefsForm);
    FrmSetActiveForm(frm);
    FrmSetEventHandler(frm, PrefsHandleEvent);
    SetFieldText(PrefsHostField, gPrefs.host);
    StrIToA(port, gPrefs.port ? gPrefs.port : 8080);
    SetFieldText(PrefsPortField, port);
    SetFieldText(PrefsKeyField, gPrefs.key);
    SetCheck(PrefsImagesCheck, gPrefs.loadImages);
    SetCheck(PrefsAvatarsCheck, gPrefs.showAvatars);
    SetPopup(PrefsDepthTrigger, PrefsDepthList, gPrefs.depth == 8 ? 1 : 0,
             gDepthLabel, sizeof(gDepthLabel));
    SetPopup(PrefsFontTrigger, PrefsFontList, gPrefs.bodyFont ? 1 : 0,
             gFontLabel, sizeof(gFontLabel));
    FrmSetFocus(frm, FrmGetObjectIndex(frm, PrefsHostField));

    gLoggedIn = false;
    hit = FrmDoDialog(frm);
    if (hit == PrefsOKButton)
        ReadFields();
    else {
        if (gLoggedIn) {  /* LoginRun already saved the new key; keep it */
            StrCopy(before.key, gPrefs.key);
            StrCopy(before.acct, gPrefs.acct);
            StrCopy(before.host, gPrefs.host);
            before.port = gPrefs.port;
        }
        gPrefs = before;
    }
    FrmDeleteForm(frm);
    FrmSetActiveForm(prev);
    if (hit == PrefsOKButton) {
        PrefSetAppPreferences(appCreator, appPrefID, appPrefVersion, &gPrefs,
                              sizeof(gPrefs), true);
        return true;
    }
    return false;
}

/* Password login through the gateway (Akkoma supports the OAuth password
 * grant). The password only travels Palm -> gateway, so run the gateway on
 * your own network, or use the gateway's web page to link instead. */
Boolean LoginRun(void)
{
    FormPtr prev = FrmGetActiveForm(), frm;
    UInt16 hit;
    Boolean ok = false;

    if (!gPrefs.host[0]) {
        ShowError("Enter the gateway address first.");
        return false;
    }
    frm = FrmInitForm(LoginForm);
    FrmSetActiveForm(frm);
    SetFieldText(LoginInstanceField, gPrefs.instance);
    SetFieldText(LoginUserField, gPrefs.user);
    FrmSetFocus(frm, FrmGetObjectIndex(frm, gPrefs.instance[0] ? LoginPassField
                                                                 : LoginInstanceField));
    while ((hit = FrmDoDialog(frm)) == LoginOKButton) {
        UInt16 size = 400;
        char *body = (char *)MemPtrNew(size), *buf, *rec[1], *f[3];
        UInt32 len;
        Err err;
        if (!body)
            break;
        body[0] = 0;
        StrNCopyZ(gPrefs.instance, GetFieldText(LoginInstanceField), sizeof(gPrefs.instance));
        StrNCopyZ(gPrefs.user, GetFieldText(LoginUserField), sizeof(gPrefs.user));
        UrlAdd(body, size, "inst", gPrefs.instance);
        UrlAdd(body, size, "user", gPrefs.user);
        UrlAdd(body, size, "pass", GetFieldText(LoginPassField));
        err = HttpFetch("POST", "/p/login", body, &buf, &len);
        MemSet(body, size, 0);  /* don't leave the password lying around */
        MemPtrFree(body);
        if (err) {
            ShowError(NetLastError());
            continue;
        }
        ProtoSplit(buf, len, rec, 1);
        ProtoFields(rec[0], f, 3);
        StrNCopyZ(gPrefs.key, f[1], sizeof(gPrefs.key));
        StrNCopyZ(gPrefs.acct, f[2], sizeof(gPrefs.acct));
        MemPtrFree(buf);
        ok = true;
        break;
    }
    SetFieldText(LoginPassField, "");
    FrmDeleteForm(frm);
    FrmSetActiveForm(prev);
    if (ok) {
        char msg[96];
        PrefSetAppPreferences(appCreator, appPrefID, appPrefVersion, &gPrefs,
                              sizeof(gPrefs), true);
        StrCopy(msg, "Logged in as ");
        StrNCatZ(msg, gPrefs.acct, sizeof(msg));
        ShowInfo(msg);
    }
    return ok;
}
