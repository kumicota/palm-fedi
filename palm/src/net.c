/* Tiny HTTP/1.0 client on top of Palm OS NetLib. Talks only to the gateway
 * (plain HTTP on the LAN / internet), never to the fediverse directly. */
#include "PalmFedi.h"

#define kMaxBody      62000L     /* record responses must fit one chunk */

static UInt16 gNetRef = 0;
static Boolean gNetOpen = false;
static char gNetErr[80];
static char gResolvedHost[64];
static NetIPAddr gResolvedAddr = 0;

const char *NetLastError(void)
{
    return gNetErr;
}

static Int32 Timeout(void)
{
    return SysTicksPerSecond() * 20;
}

static Err Fail(const char *what, Err err)
{
    char num[12];
    StrNCopyZ(gNetErr, what, sizeof(gNetErr));
    if (err) {
        StrIToH(num, err);
        StrNCatZ(gNetErr, " (", sizeof(gNetErr));
        /* StrIToH gives 8 hex digits; the low 4 are the error code */
        StrNCatZ(gNetErr, num + 4, sizeof(gNetErr));
        StrNCatZ(gNetErr, ")", sizeof(gNetErr));
    }
    return err ? err : 1;
}

Err NetStart(void)
{
    Err err;
    UInt16 ifErr = 0;

    if (gNetOpen)
        return errNone;
    if (!gNetRef) {
        err = SysLibFind("Net.lib", &gNetRef);
        if (err)
            return Fail("No network library", err);
    }
    err = NetLibOpen(gNetRef, &ifErr);
    if (err == netErrAlreadyOpen)
        err = errNone;
    if (err || ifErr) {
        if (!err) {
            NetLibClose(gNetRef, true);
            err = ifErr;
        }
        return Fail("Can't connect to network", err);
    }
    gNetOpen = true;
    return errNone;
}

void NetStop(Boolean now)
{
    if (gNetOpen) {
        /* normally keep the link up briefly for other apps; Exit drops it */
        NetLibClose(gNetRef, now);
        gNetOpen = false;
    }
}

static Err Resolve(NetIPAddr *addr)
{
    Err err = 0;
    NetHostInfoBufType *info;
    NetHostInfoPtr h;

    if (gResolvedAddr && StrCompare(gResolvedHost, gPrefs.host) == 0) {
        *addr = gResolvedAddr;
        return errNone;
    }
    *addr = NetLibAddrAToIN(gNetRef, gPrefs.host);
    if (*addr != (NetIPAddr)-1 && *addr != 0)
        goto ok;

    info = (NetHostInfoBufType *)MemPtrNew(sizeof(NetHostInfoBufType));
    if (!info)
        return Fail("Out of memory", memErrNotEnoughSpace);
    h = NetLibGetHostByName(gNetRef, gPrefs.host, info, Timeout(), &err);
    if (!h || !h->addrListP || !h->addrListP[0]) {
        MemPtrFree(info);
        return Fail("Can't find gateway host", err);
    }
    *addr = *(NetIPAddr *)h->addrListP[0];
    MemPtrFree(info);
ok:
    StrNCopyZ(gResolvedHost, gPrefs.host, sizeof(gResolvedHost));
    gResolvedAddr = *addr;
    return errNone;
}

static Err SendAll(NetSocketRef s, const char *p, UInt16 len)
{
    Err err = 0;
    while (len) {
        Int16 n = NetLibSend(gNetRef, s, (void *)p, len, 0, NULL, 0, Timeout(), &err);
        if (n <= 0)
            return Fail("Send failed", err ? err : netErrSocketClosedByRemote);
        p += n;
        len -= n;
    }
    return errNone;
}

/* Buffered single byte read; returns -1 on EOF/error. */
static Int16 ReadByte(HttpConn *c)
{
    if (c->bufPos >= c->bufLen) {
        Err err = 0;
        Int16 n = NetLibReceive(gNetRef, c->sock, c->buf, sizeof(c->buf), 0,
                                NULL, NULL, Timeout(), &err);
        if (n <= 0)
            return -1;
        c->bufLen = n;
        c->bufPos = 0;
    }
    return (UInt8)c->buf[c->bufPos++];
}

static Boolean ReadLine(HttpConn *c, char *line, UInt16 size)
{
    UInt16 n = 0;
    Int16 ch;
    while ((ch = ReadByte(c)) >= 0) {
        if (ch == '\n')
            break;
        if (ch != '\r' && n + 1 < size)
            line[n++] = (char)ch;
    }
    line[n] = 0;
    return ch >= 0 || n > 0;
}

static Boolean HeaderIs(const char *line, const char *name)
{
    UInt16 i;
    for (i = 0; name[i]; i++) {
        char a = line[i], b = name[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b)
            return false;
    }
    return line[i] == ':';
}

static const char *HeaderValue(const char *line)
{
    const char *p = StrChr(line, ':');
    if (!p)
        return "";
    p++;
    while (*p == ' ')
        p++;
    return p;
}

Err HttpOpen(HttpConn *c, const char *method, const char *path,
             const char *body, UInt16 bodyLen)
{
    Err err = 0;
    NetIPAddr ip;
    NetSocketAddrINType addr;
    char line[128], num[8];
    UInt16 status;

    MemSet(c, sizeof(HttpConn), 0);
    c->sock = -1;
    c->contentLength = -1;
    if (!gPrefs.host[0])
        return Fail("Set the gateway address in Preferences", 1);

    if ((err = NetStart()) != 0)
        return err;
    if ((err = Resolve(&ip)) != 0)
        return err;

    c->sock = NetLibSocketOpen(gNetRef, netSocketAddrINET, netSocketTypeStream,
                               netSocketProtoIPTCP, Timeout(), &err);
    if (c->sock < 0)
        return Fail("Can't open socket", err);

    MemSet(&addr, sizeof(addr), 0);
    addr.family = netSocketAddrINET;
    addr.port = NetHToNS(gPrefs.port ? gPrefs.port : 8080);
    addr.addr = ip;
    if (NetLibSocketConnect(gNetRef, c->sock, (NetSocketAddrType *)&addr,
                            sizeof(addr), Timeout(), &err) < 0) {
        gResolvedAddr = 0;  /* re-resolve next time */
        HttpClose(c);
        return Fail("Can't reach gateway", err);
    }

    /* request line + headers */
    if ((err = SendAll(c->sock, method, StrLen(method))) != 0 ||
        (err = SendAll(c->sock, " ", 1)) != 0 ||
        (err = SendAll(c->sock, path, StrLen(path))) != 0)
        goto fail;
    StrCopy(line, " HTTP/1.0\r\nHost: ");
    StrNCatZ(line, gPrefs.host, sizeof(line));
    StrNCatZ(line, "\r\nUser-Agent: PalmFedi/" appVersionStr "\r\n", sizeof(line));
    if ((err = SendAll(c->sock, line, StrLen(line))) != 0)
        goto fail;
    if (body) {
        StrCopy(line, "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: ");
        StrIToA(num, bodyLen);
        StrCat(line, num);
        StrCat(line, "\r\n");
        if ((err = SendAll(c->sock, line, StrLen(line))) != 0)
            goto fail;
    }
    if ((err = SendAll(c->sock, "\r\n", 2)) != 0)
        goto fail;
    if (body && bodyLen && (err = SendAll(c->sock, body, bodyLen)) != 0)
        goto fail;

    /* status line */
    if (!ReadLine(c, line, sizeof(line)) || StrNCompare(line, "HTTP/1.", 7) != 0) {
        err = Fail("Bad reply from gateway", 0);
        goto fail;
    }
    status = (UInt16)StrAToI(line + 9);
    if (status != 200) {
        StrCopy(gNetErr, "Gateway said: ");
        StrNCatZ(gNetErr, line + 9, sizeof(gNetErr));
        err = 1;
        goto fail;
    }
    /* headers */
    while (ReadLine(c, line, sizeof(line)) && line[0]) {
        if (HeaderIs(line, "Content-Length"))
            c->contentLength = StrAToI(HeaderValue(line));
        else if (HeaderIs(line, "Content-Type"))
            c->isImage = StrStr(HeaderValue(line), "image") != NULL;
    }
    return errNone;

fail:
    HttpClose(c);
    return err ? err : 1;
}

Int32 HttpRead(HttpConn *c, void *dst, Int32 n)
{
    Int32 got = 0;
    char *d = (char *)dst;

    if (c->contentLength >= 0 && c->bodyRead + n > c->contentLength)
        n = c->contentLength - c->bodyRead;
    /* drain the read-ahead buffer first */
    while (got < n && c->bufPos < c->bufLen) {
        UInt16 k = c->bufLen - c->bufPos;
        if (k > n - got)
            k = (UInt16)(n - got);
        MemMove(d + got, c->buf + c->bufPos, k);
        c->bufPos += k;
        got += k;
    }
    while (got < n) {
        Err err = 0;
        Int32 want = n - got;
        Int16 r;
        if (want > 4096)
            want = 4096;
        r = NetLibReceive(gNetRef, c->sock, d + got, (UInt16)want, 0, NULL, NULL,
                          Timeout(), &err);
        if (r < 0) {
            Fail("Receive failed", err);
            return -1;
        }
        if (r == 0)
            break;
        got += r;
    }
    c->bodyRead += got;
    return got;
}

Err HttpReadFully(HttpConn *c, void *dst, Int32 n)
{
    Int32 r = HttpRead(c, dst, n);
    if (r != n) {
        if (r >= 0)
            Fail("Connection closed early", 0);
        return 1;
    }
    return errNone;
}

void HttpClose(HttpConn *c)
{
    Err err;
    if (c->sock >= 0) {
        NetLibSocketClose(gNetRef, c->sock, Timeout(), &err);
        c->sock = -1;
    }
}

Err HttpFetch(const char *method, const char *path, const char *body,
              char **bufP, UInt32 *lenP)
{
    HttpConn c;
    Err err;
    char *buf;
    Int32 len;

    *bufP = NULL;
    *lenP = 0;
    err = HttpOpen(&c, method, path, body, body ? StrLen(body) : 0);
    if (err)
        return err;
    len = c.contentLength;
    if (len < 0 || len > kMaxBody) {
        HttpClose(&c);
        return Fail(len < 0 ? "Missing length" : "Reply too large", 0);
    }
    buf = (char *)MemPtrNew(len + 1);
    if (!buf) {
        HttpClose(&c);
        return Fail("Out of memory", memErrNotEnoughSpace);
    }
    err = HttpReadFully(&c, buf, len);
    HttpClose(&c);
    if (err) {
        MemPtrFree(buf);
        return err;
    }
    buf[len] = 0;
    if (StrNCompare(buf, "ERR", 3) == 0) {
        /* "ERR" US message */
        StrNCopyZ(gNetErr, len > 4 ? buf + 4 : "Gateway error", sizeof(gNetErr));
        MemPtrFree(buf);
        return 1;
    }
    *bufP = buf;
    *lenP = len;
    return errNone;
}

/* ------------------------------------------------------------------------
 * URL / form encoding (Windows-1252 bytes, percent-encoded)
 * --------------------------------------------------------------------- */
void UrlInit(char *dst, UInt16 size, const char *path)
{
    StrNCopyZ(dst, path, size);
    if (path[0])
        StrNCatZ(dst, "?", size);
}

void UrlAdd(char *dst, UInt16 size, const char *name, const char *value)
{
    static const char hex[] = "0123456789ABCDEF";
    UInt16 len = StrLen(dst);
    const UInt8 *v = (const UInt8 *)value;

    if (len && dst[len - 1] != '?') {
        if (len + 1 >= size)
            return;
        dst[len++] = '&';
    }
    while (*name && len + 1 < size)
        dst[len++] = *name++;
    if (len + 1 < size)
        dst[len++] = '=';
    for (; v && *v; v++) {
        UInt8 ch = *v;
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            if (len + 1 >= size)
                break;
            dst[len++] = ch;
        } else {
            if (len + 3 >= size)
                break;
            dst[len++] = '%';
            dst[len++] = hex[ch >> 4];
            dst[len++] = hex[ch & 15];
        }
    }
    dst[len] = 0;
}

void UrlAddInt(char *dst, UInt16 size, const char *name, Int32 value)
{
    char num[12];
    StrIToA(num, value);
    UrlAdd(dst, size, name, num);
}
