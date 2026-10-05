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
    ItemType a, b, c;
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

    /* poll field: id, flags, summary, options ("*" = own vote) */
    {
        static char pollRec[] =
            "S\x1f" "7\x1f" "Al\x1f" "al\x1f" "1m\x1f" "Tea?\x1f\x1f\x1f\x1f" "0 0 0\x1f\x1f\x1f\x1f"
            "p\x1f\x1f" "9\x1f" "p1\x1d" "MV\x1d" "4 people\x1d" "-25:Tea\x1d" "*75:Coffee: black";
        char *r = malloc(sizeof(pollRec)), *owned;
        memcpy(r, pollRec, sizeof(pollRec));
        ProtoFillItem(&c, r, 0);
        assert(c.pollParts == 5 && ProtoPollOptions(&c) == 2);
        assert(!strcmp(ProtoPollPart(&c, pollId), "p1"));
        assert(ProtoPollHas(&c, 'M') && ProtoPollHas(&c, 'V') && !ProtoPollHas(&c, 'X'));
        assert(!strcmp(ProtoPollPart(&c, pollSummary), "4 people"));
        assert(!strcmp(ProtoPollPart(&c, pollFirstOption + 1), "*75:Coffee: black"));
        assert(!strcmp(ProtoPollPart(&c, 9), ""));
        assert(!strcmp(c.f[fAcctId], "9"));

        /* after voting the item owns its poll */
        owned = malloc(32);
        strcpy(owned, "p1\x1dX\x1d" "closed\x1d" "*100:Tea");
        ProtoFreeItem(&c);
        ProtoSetPoll(&c, owned);
        c.pollOwned = true;
        assert(ProtoPollOptions(&c) == 1 && ProtoPollHas(&c, 'X'));
        ProtoFreeItem(&c);
        assert(!c.poll && !c.pollParts && !c.pollOwned);

        {   /* more options than we can vote for: the rest is ignored */
            char many[200];
            int i;
            strcpy(many, "p2\x1d\x1dsum");
            for (i = 0; i < kMaxPollOptions + 5; i++)
                strcat(many, "\x1d-1:o");
            ProtoSetPoll(&c, many);
            assert(ProtoPollOptions(&c) == kMaxPollOptions);
            assert(!strcmp(ProtoPollPart(&c, pollFirstOption + kMaxPollOptions - 1), "-1:o"));
        }
        ProtoSetPoll(&c, NULL);
        assert(c.pollParts == 0 && ProtoPollOptions(&c) == 0);
        assert(a.pollParts == 0 && !strcmp(ProtoPollPart(&a, 0), ""));
        free(r);
    }

    free(buf);
    puts("proto tests passed");
    return 0;
}
