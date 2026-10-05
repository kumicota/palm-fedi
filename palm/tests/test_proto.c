/* Host unit tests for the record parser and URL encoder. */
#include "PalmFedi.h"
#include <assert.h>

PrefsType gPrefs;
void StrNCopyZ(char *dst, const char *src, UInt16 size)
{ UInt16 n = 0; if (!size) return; if (src) while (src[n] && n + 1 < size) { dst[n] = src[n]; n++; } dst[n] = 0; }
void StrNCatZ(char *dst, const char *src, UInt16 size)
{ UInt16 len = strlen(dst); if (len < size) StrNCopyZ(dst + len, src, size - len); }

int main(void)
{
    /* header + one status with 2 media + one follow notification */
    static char body[] =
        "OK\x1f" "4321\x1f" "0\x1e"
        "S\x1f" "100\x1f" "Alice\x1f" "alice@x\x1f" "5m\x1f" "Hello\nworld\x1f" "\x1f" "Bob boosted\x1f"
        "FR\x1f" "1 2 3\x1f" "I:aaa\x1dV:bbb\x1f" "a cat\x1d\x1f" "av1\x1f" "p\x1f" "@alice@x \x1f" "9\x1e"
        "N\x1f\x1f" "Carol\x1f" "carol\x1f" "1h\x1f" "bio\x1f\x1f" "Carol followed you";
    char *buf = malloc(sizeof(body));
    char *rec[8], *head[4];
    ItemType a, b;
    UInt16 n;
    char url[80];

    memcpy(buf, body, sizeof(body));
    n = ProtoSplit(buf, sizeof(body) - 1, rec, 8);
    assert(n == 3);
    ProtoFields(rec[0], head, 4);
    assert(!strcmp(head[0], "OK") && !strcmp(head[1], "4321") && !strcmp(head[2], "0"));
    assert(!strcmp(head[3], ""));

    ProtoFillItem(&a, rec[1], 0);
    assert(!strcmp(a.f[fId], "100") && !strcmp(a.f[fText], "Hello\nworld"));
    assert(!strcmp(a.f[fContext], "Bob boosted") && !strcmp(a.counts, "1 2 3"));
    assert(a.numMedia == 2 && a.mediaType[0] == 'I' && !strcmp(a.mediaKey[1], "bbb"));
    assert(!strcmp(a.mediaAlt[0], "a cat") && !strcmp(a.mediaAlt[1], ""));
    assert(!strcmp(a.f[fAcctId], "9") && a.showBody);
    assert(ProtoHasFlag(&a, 'F') && !ProtoHasFlag(&a, 'B'));
    ProtoSetFlag(&a, 'B', true);
    ProtoSetFlag(&a, 'F', false);
    assert(!strcmp(a.flags, "RB"));

    ProtoFillItem(&b, rec[2], 0);   /* short record: missing fields are "" */
    assert(b.f[fKind][0] == 'N' && !strcmp(b.f[fContext], "Carol followed you"));
    assert(b.numMedia == 0 && !strcmp(b.f[fAcctId], ""));

    UrlInit(url, sizeof(url), "/p/tl");
    UrlAdd(url, sizeof(url), "k", "AB12");
    UrlAdd(url, sizeof(url), "text", "ol\xe1 & bye");
    UrlAddInt(url, sizeof(url), "n", 20);
    assert(!strcmp(url, "/p/tl?k=AB12&text=ol%E1%20%26%20bye&n=20"));
    url[0] = 0;
    UrlAdd(url, 12, "text", "abcdefghijk");   /* truncates safely */
    assert(strlen(url) < 12);

    free(buf);
    puts("proto tests passed");
    return 0;
}
