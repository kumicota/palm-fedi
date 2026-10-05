/* Resource IDs shared by the C sources and PalmFedi.rcp (PilRC). */
#ifndef PALMFEDI_RSC_H
#define PALMFEDI_RSC_H

/* ---- Timeline (main) form --------------------------------------------- */
#define MainForm               1000
#define MainKindTrigger        1001
#define MainKindList           1002
#define MainGadget             1003
#define MainScroll             1004
#define MainNewButton          1005
#define MainReloadButton       1006
#define MainOlderButton        1007
#define MainBackButton         1008

/* ---- Post detail form -------------------------------------------------- */
#define DetailForm             1100
#define DetailGadget           1101
#define DetailScroll           1102
#define DetailDoneButton       1103
#define DetailReplyButton      1104
#define DetailBoostPush        1105
#define DetailFavPush          1106
#define DetailThreadButton     1107

/* ---- Compose form ------------------------------------------------------ */
#define ComposeForm            1200
#define ComposeCWLabel         1201
#define ComposeCWField         1202
#define ComposeTextField       1203
#define ComposeTextScroll      1204
#define ComposeVisTrigger      1205
#define ComposeVisList         1206
#define ComposePostButton      1207
#define ComposeCancelButton    1208
#define ComposeCountLabel      1209

/* ---- Preferences form (modal) ----------------------------------------- */
#define PrefsForm              1300
#define PrefsHostField         1301
#define PrefsPortField         1302
#define PrefsKeyField          1303
#define PrefsImagesCheck       1304
#define PrefsAvatarsCheck      1305
#define PrefsDepthTrigger      1306
#define PrefsDepthList         1307
#define PrefsOKButton          1308
#define PrefsCancelButton      1309
#define PrefsTestButton        1310
#define PrefsLoginButton       1311
#define PrefsFontTrigger       1312
#define PrefsFontList          1313

/* ---- Login form (modal) ------------------------------------------------ */
#define LoginForm              1400
#define LoginInstanceField     1401
#define LoginUserField         1402
#define LoginPassField         1403
#define LoginOKButton          1404
#define LoginCancelButton      1405

/* ---- Image viewer form ------------------------------------------------- */
#define ViewerForm             1500
#define ViewerGadget           1501
#define ViewerScroll           1502
#define ViewerDoneButton       1503
#define ViewerPrevButton       1504
#define ViewerNextButton       1505

/* ---- Menus ------------------------------------------------------------- */
#define MainMenuBar            1000
#define DetailMenuBar          1100
#define ComposeMenuBar         1200
#define MenuHome               1000
#define MenuLocal              1001
#define MenuFederated          1002
#define MenuNotifications      1003
#define MenuMentions           1004
#define MenuBookmarks          1005
#define MenuReload             1006
#define MenuTop                1007
#define MenuPrefs              1010
#define MenuLogin              1011
#define MenuAbout              1012
#define MenuDetailBookmark     1100
#define MenuDetailProfile      1101
#define MenuDetailCopy         1102
#define MenuDetailImages       1103
#define MenuEditUndo           10000
#define MenuEditCut            10001
#define MenuEditCopy           10002
#define MenuEditPaste          10003
#define MenuEditSelectAll      10004
#define MenuEditKeyboard       10006
#define MenuEditGraffiti       10007

/* ---- Alerts and strings ------------------------------------------------ */
#define ErrorAlert             2000
#define InfoAlert              2001
#define AboutAlert             2002
#define ConfirmDiscardAlert    2003
#define NoNetAlert             2004
#define RomIncompatibleAlert   2005

#define AppIconFamily          1000
#define AppSmallIconFamily     1001

#endif
