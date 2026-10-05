/* PalmFedi application entry point and event loop. */
#include "PalmFedi.h"

#define kMinRomVersion sysMakeROMVersion(5, 0, 0, sysROMStageRelease, 0)

static Boolean gIdleWork = false;

static void LoadPrefs(void)
{
    UInt16 size = sizeof(gPrefs);
    Int16 version;

    MemSet(&gPrefs, sizeof(gPrefs), 0);
    version = PrefGetAppPreferences(appCreator, appPrefID, &gPrefs, &size, true);
    if (version == noPreferenceFound || size != sizeof(gPrefs)) {
        MemSet(&gPrefs, sizeof(gPrefs), 0);
        gPrefs.port = 8080;
        gPrefs.loadImages = 1;
        gPrefs.showAvatars = 1;
        gPrefs.depth = 16;
    }
    if (!gPrefs.port)
        gPrefs.port = 8080;
}

static Err AppStart(void)
{
    LoadPrefs();
    DiaInit();
    ImgInit();
    if (!TLInit())
        return memErrNotEnoughSpace;
    MemSet(&gCompose, sizeof(gCompose), 0);
    return errNone;
}

static void AppStop(void)
{
    FrmCloseAllForms();
    PrefSetAppPreferences(appCreator, appPrefID, appPrefVersion, &gPrefs, sizeof(gPrefs), true);
    TLFree();
    ImgShutdown();
    NetStop();
}

static Boolean AppHandleEvent(EventType *e)
{
    if (e->eType == frmLoadEvent) {
        UInt16 id = e->data.frmLoad.formID;
        FormPtr frm = FrmInitForm(id);
        FrmSetActiveForm(frm);
        switch (id) {
        case MainForm:    FrmSetEventHandler(frm, MainFormHandleEvent); break;
        case DetailForm:  FrmSetEventHandler(frm, DetailFormHandleEvent); break;
        case ComposeForm: FrmSetEventHandler(frm, ComposeFormHandleEvent); break;
        case ViewerForm:  FrmSetEventHandler(frm, ViewerFormHandleEvent); break;
        }
        return true;
    }
    return false;
}

static void AppEventLoop(void)
{
    EventType e;
    Err err;

    do {
        /* poll quickly while images are still loading, otherwise sleep */
        EvtGetEvent(&e, gIdleWork ? 1 : evtWaitForever);
        if (e.eType == nilEvent) {
            gIdleWork = MainIdle() || DetailIdle();
            continue;
        }
        if (SysHandleEvent(&e))
            continue;
        if (MenuHandleEvent(0, &e, &err))
            continue;
        if (AppHandleEvent(&e))
            continue;
        FrmDispatchEvent(&e);
        /* any user activity may reveal new images to fetch */
        if (e.eType == frmOpenEvent || e.eType == penUpEvent || e.eType == keyDownEvent ||
            e.eType == sclExitEvent || e.eType == winDisplayChangedEvent)
            gIdleWork = true;
    } while (e.eType != appStopEvent);
}

static Err RomVersionCompatible(UInt32 required, UInt16 launchFlags)
{
    UInt32 rom;
    FtrGet(sysFtrCreator, sysFtrNumROMVersion, &rom);
    if (rom < required) {
        if ((launchFlags & (sysAppLaunchFlagNewGlobals | sysAppLaunchFlagUIApp)) ==
            (sysAppLaunchFlagNewGlobals | sysAppLaunchFlagUIApp)) {
            FrmAlert(RomIncompatibleAlert);
            if (rom < sysMakeROMVersion(2, 0, 0, sysROMStageRelease, 0))
                AppLaunchWithCommand(sysFileCDefaultApp, sysAppLaunchCmdNormalLaunch, NULL);
        }
        return sysErrRomIncompatible;
    }
    return errNone;
}

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
    Err err = RomVersionCompatible(kMinRomVersion, launchFlags);
    if (err)
        return err;
    if (cmd == sysAppLaunchCmdNormalLaunch) {
        err = AppStart();
        if (err) {
            FrmCustomAlert(ErrorAlert, "Not enough memory to start.", "", "");
            return err;
        }
        FrmGotoForm(MainForm);
        AppEventLoop();
        AppStop();
    }
    return errNone;
}
