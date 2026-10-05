/* Follow / unfollow. Relationship flags come from the gateway:
 * W you follow them, Q follow requested, Y they follow you, M it's you. */
#include "PalmFedi.h"

static Boolean Has(const char *flags, char flag)
{
    return StrChr(flags, flag) != NULL;
}

/* Mirrors relationship_label() in gateway/palmfedi_gateway/convert.py */
const char *AcctRelLabel(const char *flags)
{
    if (Has(flags, 'M'))
        return "This is you";
    if (Has(flags, 'W'))
        return Has(flags, 'Y') ? "You follow each other" : "You follow them";
    if (Has(flags, 'Q'))
        return "Follow requested";
    if (Has(flags, 'Y'))
        return "Follows you";
    return "";
}

/* Label for the follow button. */
const char *AcctFollowLabel(const char *flags)
{
    if (Has(flags, 'W'))
        return "Unfollow";
    if (Has(flags, 'Q'))
        return "Requested";
    return Has(flags, 'Y') ? "Follow back" : "Follow";
}

/* One request to /p/rel or /p/follow; copies the returned flags. */
static Boolean Request(const char *method, const char *path, const char *body,
                       char *flags, UInt16 size)
{
    char *buf, *rec[1], *f[2];
    UInt32 len;

    if (HttpFetch(method, path, body, &buf, &len) != errNone) {
        ShowError(NetLastError());
        return false;
    }
    ProtoSplit(buf, len, rec, 1);
    ProtoFields(rec[0], f, 2);
    StrNCopyZ(flags, f[1], size);
    MemPtrFree(buf);
    return true;
}

Boolean AcctFollowToggle(const char *acctId, const char *name, char *flags, UInt16 size,
                         Boolean known)
{
    char url[128], msg[80];
    Boolean following, requested;

    if (!acctId || !acctId[0])
        return false;
    if (!known) {
        UrlInit(url, sizeof(url), "/p/rel");
        UrlAdd(url, sizeof(url), "k", gPrefs.key);
        UrlAdd(url, sizeof(url), "id", acctId);
        if (!Request("GET", url, NULL, flags, size))
            return false;
    }
    if (Has(flags, 'M')) {
        ShowInfo("That's your own account.");
        return false;
    }
    following = Has(flags, 'W');
    requested = Has(flags, 'Q');
    StrCopy(msg, following ? "Unfollow " : requested ? "Cancel your follow request to "
                                                     : "Follow ");
    StrNCatZ(msg, name && name[0] ? name : "this account", sizeof(msg) - 1);
    StrNCatZ(msg, "?", sizeof(msg));
    if (FrmCustomAlert(ConfirmAlert, msg, "", "") != 0)
        return false;

    url[0] = 0;
    UrlAdd(url, sizeof(url), "k", gPrefs.key);
    UrlAdd(url, sizeof(url), "id", acctId);
    UrlAdd(url, sizeof(url), "a", following || requested ? "unfollow" : "follow");
    return Request("POST", "/p/follow", url, flags, size);
}
