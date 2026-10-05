/* Search dialog (modal). The results are an ordinary timeline view:
 * accounts, hashtags and posts, see gateway /p/tl?t=search. */
#include "PalmFedi.h"

static char gLastQuery[64];

Boolean SearchRun(void)
{
    FormPtr prev = FrmGetActiveForm(), frm;
    FieldPtr fld;
    const char *q;
    char title[32];
    UInt16 hit;

    if (!gPrefs.host[0] || !gPrefs.key[0]) {
        ShowError("Set up the gateway first: Menu > Options > Preferences.");
        return false;
    }
    frm = FrmInitForm(SearchForm);
    FrmSetActiveForm(frm);
    SetFieldText(SearchField, gLastQuery);
    fld = (FieldPtr)GetObjectPtr(SearchField);
    FrmSetFocus(frm, FrmGetObjectIndex(frm, SearchField));
    FldSetSelection(fld, 0, FldGetTextLength(fld));  /* typing replaces the old search */
    hit = FrmDoDialog(frm);
    if (hit == SearchOKButton) {
        q = GetFieldText(SearchField);
        while (*q == ' ')
            q++;
        StrNCopyZ(gLastQuery, q, sizeof(gLastQuery));
    }
    FrmDeleteForm(frm);
    FrmSetActiveForm(prev);
    if (hit != SearchOKButton || !gLastQuery[0])
        return false;

    if (gLastQuery[0] == '#' && gLastQuery[1] && !StrChr(gLastQuery, ' ')) {
        /* "#tag" goes straight to that hashtag's timeline */
        TLSetView(kindTag, gLastQuery + 1, gLastQuery, true);
    } else {
        title[0] = '"';
        StrNCopyZ(title + 1, gLastQuery, sizeof(title) - 2);
        StrNCatZ(title, "\"", sizeof(title));
        TLSetView(kindSearch, gLastQuery, title, true);
    }
    return true;
}
