/*
 * Sharing the plane editor's edit files over Wi-Fi (3ds_online.h): the
 * console side of tools/sprite_server, docs/ONLINE_SPRITE_SHARING.md
 * section 7.
 *
 * http:C (libctru's httpc) does DNS, TCP and, for an https:// server, TLS
 * in a system module. The standard server sits behind a Cloudflare Tunnel
 * whose free edge certificate is ECDSA, which the console's TLS may not take,
 * so it is spoken to over plain HTTP and nothing secret travels: requests are
 * signed with a key only the console and the server know, and every answer is
 * signed back (section 7.6; 3ds_online_crypto.c, the server's signing.py).
 * An https:// server still works, with GlobalSign Root CA added to every
 * context (3ds_online_root.h). The calls block, so every job runs on a worker thread
 * of its own and the game only ever reads a status word. httpc is set up at
 * the start of a job and given back at its end: nothing of it is held while
 * the game plays. The console is kept awake while a job runs (closing the
 * lid would drop Wi-Fi in the middle of a transfer).
 *
 * The jobs, against the server's API (tools/sprite_server/README.md):
 *   pair    POST /api/pair2 {pub, proof, console_name} -> {pub, user, confirm}:
 *           an X25519 exchange; the token comes from the shared secret and
 *           the pairing code proves both sides, without being sent
 *   upload  POST /api/mons/<key>/versions (the edit file as it is)
 *   sync    GET /api/sync?since=<seq>, then GET /api/versions/<id> for each
 *           picture whose current version differs from the card's; a card
 *           file that differs is first copied to stereo/backup/ (as
 *           client.py does), the new one written beside it and renamed over.
 * What the server already has is remembered per file in online_sent.txt
 * (key and SHA-256), so UPLOAD ALL sends only what is new.
 */
#include <3ds.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "3ds_bottom_draw.h"
#include "3ds_log.h"
#include "3ds_online.h"
#include "3ds_video.h"
#include "3ds_stereo_strip.h"
#include "3ds_online_crypto.h"
#include "3ds_platform.h"
#include "3ds_stereo_mon.h"
#include "3ds_online_root.h"

#define DATA_DIR "sdmc:/3ds/emerald3ds"
#define LINK_PATH DATA_DIR "/online.txt"
#define SENT_PATH DATA_DIR "/online_sent.txt"
#define NEW_PATH DATA_DIR "/online_new.txt"
/* The votes per picture, sent or still to send: "key pending v1 v2 ...". */
#define VOTES_PATH DATA_DIR "/online_votes.txt"
#define MAX_FAVS 800
/* The review marks before the beta, set or still to send: "key status pending"
 * (status 0 none, 1 needs work, 2 good enough). */
#define REVIEWS_PATH DATA_DIR "/online_reviews.txt"
#define MAX_REVIEWS 1024
#define MAX_INDEX 1024
/* A vote kept on the card is tried again this often while it waits. */
#define FLUSH_RETRY_MS 60000
#define MAX_NEW 512
#define EDIT_DIR DATA_DIR "/" CTR_STEREO_EDIT_DIR
#define BACKUP_DIR EDIT_DIR "/backup"
/* The address used when online.txt names none (make ONLINE_SERVER=...). */
#ifndef CTR_ONLINE_SERVER
#define CTR_ONLINE_SERVER "http://poke.cloudnoud.nl"
#endif
#define USER_AGENT "emerald3ds/1"
/* http:C's shared memory, for request bodies (an edit is at most ~11 KiB). */
#define HTTPC_SHARED (64 * 1024)
#define TIMEOUT_NS 15000000000ULL
/* No response is bigger than a sync of every picture (~150 bytes each). */
#define MAX_RESPONSE (512 * 1024)
#define MAX_EDIT (32 * 1024)
#define MAX_SENT 1600
#define SERVER_MAX 128
#define TOKEN_MAX 80
#define USER_MAX 40
#define WORKER_STACK (32 * 1024)
/* http:C's "the certificate could not be verified" (and handshakes it cannot do). */
#define RESULT_TLS 0xD8A0A03C
/* Ours: an answer to a signed request without the server's signature. */
#define RESULT_UNSIGNED 0xE3D50001
/* The server's clock is asked for again after this long (requests carry it). */
#define TIME_REFRESH_MS (10 * 60 * 1000)

typedef struct
{
    char server[SERVER_MAX];
    char token[TOKEN_MAX];
    char user[USER_MAX];
    unsigned seq;
    bool verify;
} Link;

enum { JOB_PAIR, JOB_UPLOAD, JOB_UPLOAD_ALL, JOB_SYNC, JOB_VERSIONS, JOB_USE, JOB_FLUSH, JOB_INDEX, JOB_DELETE, JOB_TEST,
       JOB_NAME, JOB_FILES, JOB_FOCUS };

typedef struct
{
    int type;
    uint16_t id, id2;
    char code[16];
    char consoleName[40];
    uint32_t version;
    /* JOB_FILES: the versions to fetch (`id` the front's picture, `id2` the
     * back's; files[k] belongs to fileIds[k]). */
    uint8_t fileCount;
    uint32_t files[CTR_ONLINE_MAX_FILES];
    uint16_t fileIds[CTR_ONLINE_MAX_FILES];
} Job;

static CtrLock sLock;
static bool sLockReady;
/* Guarded by sLock: */
static Link sLink;
static bool sLinkRead;
static bool sBusy;
static CtrOnlineStatus sStatus = CTR_ONLINE_IDLE;
static char sText[CTR_ONLINE_TEXT];
static unsigned sSerial;
static uint64_t sEndedMs;
static bool sEditsWritten;
static bool sWifiDown;
/* The job the worker runs (written before it starts). */
static Job sJob;
static int sLastJob = -1;
/* What the last syncs brought (online_new.txt), and the last versions list
 * with each version's SHA-256 (to know which one the card has). */
static uint16_t sNew[MAX_NEW];
static unsigned sNewCount, sNewSerial;
static bool sNewRead;
static CtrOnlineVersions sVersions;
static char sVersionSha[CTR_ONLINE_MAX_VERSIONS][65];
static unsigned sVersionsSerial;
static bool sShownWritten;
/* TEST SERVER's findings (CtrOnline_TestResult), a line each. */
#define TEST_LINES 4
static char sTestLines[TEST_LINES][24];
static unsigned sTestCount;
/* The last request found no server (no answer at all): votes wait. */
static bool sNetFailed;
/* The author's name, changed here, still to reach the server (it is the
 * name the site shows for the account). */
static bool sNamePending;
/* The favourites, as the console knows them (online_votes.txt). */
typedef struct Fav Fav;
struct Fav
{
    uint16_t id;
    uint8_t n;
    bool pending;
    uint32_t v[CTR_ONLINE_MAX_VOTES];
};
static Fav sFavs[MAX_FAVS];
static unsigned sFavCount;
static bool sFavsRead;
/* The review marks, as the console knows them (online_reviews.txt): the
 * server's from the last index, the console's own until they are sent. */
typedef struct
{
    uint16_t id;
    uint8_t status;
    bool pending;
} Mark;
static Mark sMarks[MAX_REVIEWS];
static unsigned sMarkCount, sMarkSerial;
static bool sMarksRead;
/* Per picture on the server: how many versions, how many are this
 * account's favourites (GET /api/mons). */
typedef struct
{
    uint16_t id;
    uint8_t count, mine;
    char author[17]; /* who uploaded the current version */
} IndexEntry;
static IndexEntry sIndex[MAX_INDEX];
static unsigned sIndexCount, sIndexSerial;
static uint64_t sIndexAt;
/* Versions' files fetched for the voting view (CtrOnline_FetchFiles), in
 * memory only: the oldest asked for goes first when the cache is full. */
typedef struct
{
    uint32_t version, used;
    uint16_t id;
    uint8_t *data;
    uint32_t size;
} CachedFile;
#define FILE_CACHE 40
static CachedFile sFiles[FILE_CACHE];
static unsigned sFilesSerial, sFilesUse;
/* What the account's web app shows (GET /api/focus). */
static CtrOnlineFocus sFocus;
static unsigned sFocusSerial;
static bool sFocusFailed;
/* Main thread only. */
static bool sSleepHeld;
static char sUserCopy[USER_MAX];

/* ── Small helpers ──────────────────────────────────────────────────── */

static void Lock(void)
{
    if (!sLockReady)
    {
        CtrLock_Init(&sLock);
        sLockReady = true;
    }
    CtrLock_Lock(&sLock);
}

static void Unlock(void) { CtrLock_Unlock(&sLock); }

/* A string into a buffer, cut to fit (status lines are cut on purpose). */
static void Copy(char *out, size_t size, const char *in)
{
    size_t n = strlen(in);

    if (size == 0) return;
    if (n >= size) n = size - 1;
    memcpy(out, in, n);
    out[n] = 0;
}

/* The game font's characters only: capitals, digits, space and . : / -. */
static void FontSafe(char *s)
{
    for (; *s; ++s)
    {
        char c = *s;

        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' || c == ':' || c == '/' || c == '-'))
            c = ' ';
        *s = c;
    }
}

static void SetStatus(CtrOnlineStatus status, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void SetStatus(CtrOnlineStatus status, const char *format, ...)
{
    char text[64];
    va_list args;

    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    FontSafe(text);
    Lock();
    sStatus = status;
    Copy(sText, sizeof(sText), text);
    ++sSerial;
    if (status != CTR_ONLINE_BUSY) sEndedMs = CtrPlatform_Milliseconds();
    Unlock();
}

/* An id's edit file name: NNN a front, bNNN a back, tNNN a trainer, pNNN a
 * player (as 3ds_stereo_mon.c names them). */
static void KeyOf(uint16_t id, char *key, unsigned size)
{
    static const char prefix[4] = {0, 't', 'b', 'p'};
    unsigned kind = id >> 14;

    if (kind == 0) snprintf(key, size, "%03u", id);
    else snprintf(key, size, "%c%03u", prefix[kind], id & 0x3FFF);
}

/* The other way round; 0 for a name that is no edit file's. */
static uint16_t IdOf(const char *key)
{
    unsigned kind = 0, n = 0, k = 0;

    if (key[0] == 'b') kind = CTR_STEREO_ID_BACK, k = 1;
    else if (key[0] == 't') kind = CTR_STEREO_ID_TRAINER, k = 1;
    else if (key[0] == 'p') kind = CTR_STEREO_ID_PLAYER, k = 1;
    if (key[k] < '0' || key[k] > '9') return 0;
    for (; key[k] >= '0' && key[k] <= '9'; ++k) n = n * 10 + (unsigned)(key[k] - '0');
    if (key[k] != 0 || n >= 0x4000 || (kind == 0 && n == 0)) return 0;
    return (uint16_t)(kind | n);
}

static uint8_t *ReadWhole(const char *path, uint32_t max, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *data;
    long n;

    *size = 0;
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || (uint32_t)n > max || (data = malloc((size_t)n)) == NULL)
    {
        fclose(f);
        return NULL;
    }
    if (fread(data, 1, (size_t)n, f) != (size_t)n)
    {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data) *size = (uint32_t)n;
    return data;
}

/* Writes beside the file and renames over it: a failed write never leaves
 * half a file. */
static bool WriteWhole(const char *path, const void *data, uint32_t size)
{
    char tmp[96];
    FILE *f;
    bool ok;

    snprintf(tmp, sizeof(tmp), "%s.part", path);
    f = fopen(tmp, "wb");
    if (f == NULL) return false;
    ok = fwrite(data, 1, size, f) == size;
    ok = fclose(f) == 0 && ok;
    if (ok)
    {
        remove(path);
        ok = rename(tmp, path) == 0;
    }
    if (!ok) remove(tmp);
    return ok;
}

/* ── SHA-256 (FIPS 180-4), for what the server already has ──────────── */

/* The digest of `data` as 64 lower-case hex digits. */
static void Sha256Hex(const uint8_t *data, uint32_t size, char hex[65])
{
    uint8_t digest[32];

    CtrSha256_Digest(data, size, digest);
    CtrHex(digest, 32, hex);
}

/* ── The link (online.txt) ──────────────────────────────────────────── */

/* Called with sLock held. */
static void ReadLinkLocked(void)
{
    FILE *f;
    char line[160];

    if (sLinkRead) return;
    sLinkRead = true;
    memset(&sLink, 0, sizeof(sLink));
    snprintf(sLink.server, sizeof(sLink.server), "%s", CTR_ONLINE_SERVER);
    sLink.verify = true;
    f = fopen(LINK_PATH, "r");
    if (f == NULL) return;
    while (fgets(line, sizeof(line), f))
    {
        char *value = strchr(line, '='), *end;

        if (value == NULL || line[0] == '#') continue;
        *value++ = 0;
        for (end = value + strlen(value); end > value && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' '); --end)
            end[-1] = 0;
        if (strcmp(line, "server") == 0 && value[0]) snprintf(sLink.server, sizeof(sLink.server), "%s", value);
        else if (strcmp(line, "token") == 0) snprintf(sLink.token, sizeof(sLink.token), "%s", value);
        else if (strcmp(line, "user") == 0) snprintf(sLink.user, sizeof(sLink.user), "%s", value);
        else if (strcmp(line, "seq") == 0) sLink.seq = (unsigned)strtoul(value, NULL, 10);
        else if (strcmp(line, "verify") == 0) sLink.verify = strcmp(value, "0") != 0;
    }
    fclose(f);
    /* No slash at the end: paths start with one. */
    for (size_t n = strlen(sLink.server); n && sLink.server[n - 1] == '/'; --n) sLink.server[n - 1] = 0;
    /* A quick tunnel's address (the first tests') is gone for good, and the
     * standard server over HTTPS is the same server: the standard server
     * instead, the token kept (the data moved with it). */
    if (strstr(sLink.server, ".trycloudflare.com") || strcmp(sLink.server, "https://poke.cloudnoud.nl") == 0)
    {
        CtrLog_Write(CTR_LOG_FS, "online: %s: the standard server %s instead", sLink.server,
                     CTR_ONLINE_SERVER);
        snprintf(sLink.server, sizeof(sLink.server), "%s", CTR_ONLINE_SERVER);
    }
}

static Link GetLink(void)
{
    Link copy;

    Lock();
    ReadLinkLocked();
    copy = sLink;
    Unlock();
    return copy;
}

/* The link as given, to memory and to the card. */
static bool PutLink(const Link *link)
{
    char text[400];
    int n;

    Lock();
    sLink = *link;
    sLinkRead = true;
    Unlock();
    /* The standard server is not written down: a later build's standard
     * then holds for this card too. */
    n = snprintf(text, sizeof(text),
                 "# The plane-sharing link (3ds_online.c). Unlink in OPTIONS or on the website.\n"
                 "%s%s%stoken=%s\nuser=%s\nseq=%u\n%s",
                 strcmp(link->server, CTR_ONLINE_SERVER) ? "server=" : "",
                 strcmp(link->server, CTR_ONLINE_SERVER) ? link->server : "",
                 strcmp(link->server, CTR_ONLINE_SERVER) ? "\n" : "",
                 link->token, link->user, link->seq, link->verify ? "" : "verify=0\n");
    return n > 0 && n < (int)sizeof(text) && WriteWhole(LINK_PATH, text, (uint32_t)n);
}

/* ── What the server already has (online_sent.txt) ──────────────────── */

typedef struct
{
    char key[8];
    char sha[65];
    uint32_t version; /* the server's version number, 0 when unknown */
} Sent;

typedef struct
{
    Sent *items;
    unsigned count;
    bool changed;
} SentList;

static void SentLoad(SentList *list)
{
    FILE *f = fopen(SENT_PATH, "r");
    char line[96];

    list->items = calloc(MAX_SENT, sizeof(Sent));
    list->count = 0;
    list->changed = false;
    if (f == NULL || list->items == NULL)
    {
        if (f) fclose(f);
        return;
    }
    while (list->count < MAX_SENT && fgets(line, sizeof(line), f))
    {
        Sent *s = &list->items[list->count];

        unsigned long version = 0;

        if (sscanf(line, "%7s %64s %lu", s->key, s->sha, &version) >= 2 && strlen(s->sha) == 64)
        {
            s->version = (uint32_t)version;
            ++list->count;
        }
    }
    fclose(f);
}

static bool SentHas(const SentList *list, const char *key, const char *sha)
{
    for (unsigned i = 0; i < list->count; ++i)
        if (strcmp(list->items[i].key, key) == 0) return strcmp(list->items[i].sha, sha) == 0;
    return false;
}

static void SentPut(SentList *list, const char *key, const char *sha, uint32_t version)
{
    unsigned i;

    if (list->items == NULL) return;
    for (i = 0; i < list->count && strcmp(list->items[i].key, key) != 0; ++i) {}
    if (i == list->count)
    {
        if (list->count == MAX_SENT) return;
        ++list->count;
        list->items[i].version = 0;
        list->items[i].sha[0] = 0;
    }
    /* The same file without a number (an older line) keeps the one it had. */
    if (version || strcmp(list->items[i].sha, sha) != 0) list->items[i].version = version;
    snprintf(list->items[i].key, sizeof(list->items[i].key), "%s", key);
    snprintf(list->items[i].sha, sizeof(list->items[i].sha), "%s", sha);
    list->changed = true;
}

static void SentSave(SentList *list)
{
    char *text;
    size_t n = 0;

    if (list->items && list->changed && (text = malloc(list->count * 80 + 1)) != NULL)
    {
        for (unsigned i = 0; i < list->count; ++i)
            n += (size_t)sprintf(text + n, "%s %s %lu\n", list->items[i].key, list->items[i].sha,
                                 (unsigned long)list->items[i].version);
        if (!WriteWhole(SENT_PATH, text, (uint32_t)n))
            CtrLog_Write(CTR_LOG_ERROR, "online: cannot write %s", SENT_PATH);
        free(text);
    }
    free(list->items);
    list->items = NULL;
}

/* ── HTTP ───────────────────────────────────────────────────────────── */

typedef struct
{
    int status;       /* the HTTP status, 0 when there was no answer */
    Result result;    /* the failing call's result, when there was none */
    uint8_t *body;    /* NUL-terminated, malloc'd (free it) */
    uint32_t length;
} Reply;

static void ReplyFree(Reply *reply)
{
    free(reply->body);
    reply->body = NULL;
}

static bool JsonNumber(const char *json, const char *name, long *out);

/* ── Randomness, the server's clock, signatures ─────────────────────── */

static bool sPsReady;
static long long sServerTime; /* seconds, as the server said at sServerTimeAt */
static uint64_t sServerTimeAt;
static bool sTimeKnown;

/* ps:ps's random bytes (the keys and nonces); a hash of the tick counter
 * only when it is not there (logged: a link made so is weaker). */
static void Random(void *out, uint32_t size)
{
    static unsigned counter;
    uint8_t digest[32];

    if (sPsReady && R_SUCCEEDED(PS_GenerateRandomBytes(out, size))) return;
    CtrLog_Write(CTR_LOG_ERROR, "online: no ps:ps random bytes; the tick counter instead");
    for (uint32_t n = 0; n < size; n += 32)
    {
        uint64_t seed[3] = {svcGetSystemTick(), osGetTime(), ++counter};

        CtrSha256_Digest(seed, sizeof(seed), digest);
        memcpy((uint8_t *)out + n, digest, size - n < 32 ? size - n : 32);
    }
}

/* The key a linked console signs with: SHA-256 of its token (what the
 * server keeps of it). */
static void DeviceKey(const Link *link, uint8_t key[32])
{
    CtrSha256_Digest(link->token, (uint32_t)strlen(link->token), key);
}

static void Mac(const uint8_t key[32], const char *text, char hex[65])
{
    CtrHmac m;
    uint8_t mac[32];

    CtrHmac_Init(&m, key, 32);
    CtrHmac_Update(&m, text, (uint32_t)strlen(text));
    CtrHmac_Final(&m, mac);
    CtrHex(mac, 32, hex);
}

static bool Http(const Link *link, HTTPC_RequestMethod method, const char *path, const void *body, uint32_t bodyLength,
                 const char *contentType, Reply *reply);

/* The server's clock (GET /api/health), which signed requests carry: the
 * console's own is whatever its owner set. */
static bool FetchTime(const Link *link, Reply *failed)
{
    Reply reply;
    long when = 0;

    if (!Http(link, HTTPC_METHOD_GET, "/api/health", NULL, 0, NULL, &reply))
    {
        *failed = reply;
        return false;
    }
    if (reply.status != 200 || !JsonNumber((const char *)reply.body, "time", &when) || when <= 0)
    {
        CtrLog_Write(CTR_LOG_ERROR, "online: %s/api/health gives no time (HTTP %d): a server from before signed "
                     "requests", link->server, reply.status);
        if (reply.status == 200) reply.status = 426;
        *failed = reply;
        return false;
    }
    ReplyFree(&reply);
    sServerTime = when;
    sServerTimeAt = CtrPlatform_Milliseconds();
    sTimeKnown = true;
    return true;
}

/*
 * One request. `body` (with its content type) for a POST. The answer's body
 * is read whole (it carries a Content-Length, as the server always sends).
 * A linked console's requests are signed (but for the clock and linking),
 * and then only a 2xx answer the server signed back counts.
 * Logs what happened, with the time it took.
 */
static bool Http(const Link *link, HTTPC_RequestMethod method, const char *path, const void *body, uint32_t bodyLength,
                 const char *contentType, Reply *reply)
{
    const char *verb = method == HTTPC_METHOD_POST ? "POST" : "GET";
    char url[SERVER_MAX + 96], text[SERVER_MAX + 256], kid[65], when[24], nonce[17], bodySha[65], sign[65] = "",
        answerSign[80] = "";
    uint8_t key[32] = {0}, random[8], digest[32];
    httpcContext ctx;
    uint64_t t0 = CtrPlatform_Milliseconds();
    u32 status = 0, total = 0, before = 0, now = 0, capacity;
    Result r;
    bool https, signing;

    memset(reply, 0, sizeof(*reply));
    signing = link->token[0] && strcmp(path, "/api/health") != 0 && strcmp(path, "/api/pair2") != 0;
    if (signing && (!sTimeKnown || CtrPlatform_Milliseconds() - sServerTimeAt > TIME_REFRESH_MS)
        && !FetchTime(link, reply))
        return false;
    snprintf(url, sizeof(url), "%s%s", link->server, path);
    https = strncmp(url, "https://", 8) == 0;
    r = httpcOpenContext(&ctx, method, url, 0);
    if (R_FAILED(r))
    {
        reply->result = r;
        CtrLog_Write(CTR_LOG_ERROR, "online: %s open failed %08lx", path, (unsigned long)r);
        return false;
    }
    if (https)
        r = link->verify ? httpcAddTrustedRootCA(&ctx, sOnlineRootCa, sizeof(sOnlineRootCa))
                         : httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
    if (R_SUCCEEDED(r)) r = httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_DISABLED);
    if (R_SUCCEEDED(r)) r = httpcAddRequestHeaderField(&ctx, "User-Agent", USER_AGENT);
    if (R_SUCCEEDED(r) && signing)
    {
        /* Who, when (by the server's clock), a nonce, and a MAC over those,
         * the method, the path and the body's digest: the token never goes. */
        DeviceKey(link, key);
        snprintf(text, sizeof(text), "EM3DS kid\n");
        {
            CtrSha256 s;

            CtrSha256_Init(&s);
            CtrSha256_Update(&s, text, (uint32_t)strlen(text));
            CtrSha256_Update(&s, key, 32);
            CtrSha256_Final(&s, digest);
        }
        CtrHex(digest, 8, kid);
        snprintf(when, sizeof(when), "%lld",
                 sServerTime + (long long)((CtrPlatform_Milliseconds() - sServerTimeAt) / 1000));
        Random(random, sizeof(random));
        CtrHex(random, sizeof(random), nonce);
        Sha256Hex(body ? body : "", body ? bodyLength : 0, bodySha);
        snprintf(text, sizeof(text), "EM3DS1\n%s\n%s\n%s\n%s\n%s", verb, path, when, nonce, bodySha);
        Mac(key, text, sign);
        r = httpcAddRequestHeaderField(&ctx, "X-Em3ds-Key", kid);
        if (R_SUCCEEDED(r)) r = httpcAddRequestHeaderField(&ctx, "X-Em3ds-Time", when);
        if (R_SUCCEEDED(r)) r = httpcAddRequestHeaderField(&ctx, "X-Em3ds-Nonce", nonce);
        if (R_SUCCEEDED(r)) r = httpcAddRequestHeaderField(&ctx, "X-Em3ds-Sign", sign);
    }
    if (R_SUCCEEDED(r) && body)
    {
        /* http:C copies the body from a word-aligned buffer. */
        u32 *words = malloc((bodyLength + 3) & ~3u);

        if (words == NULL) r = -1;
        else
        {
            memcpy(words, body, bodyLength);
            r = httpcAddRequestHeaderField(&ctx, "Content-Type", contentType);
            if (R_SUCCEEDED(r)) r = httpcAddPostDataRaw(&ctx, words, bodyLength);
            free(words);
        }
    }
    if (R_SUCCEEDED(r)) r = httpcBeginRequest(&ctx);
    if (R_SUCCEEDED(r)) r = httpcGetResponseStatusCodeTimeout(&ctx, &status, TIMEOUT_NS);
    if (R_FAILED(r))
    {
        reply->result = r;
        CtrLog_Write(CTR_LOG_ERROR, "online: %s %s failed %08lx after %lums%s", verb, path, (unsigned long)r,
                     (unsigned long)(CtrPlatform_Milliseconds() - t0),
                     (u32)r == RESULT_TLS ? " (TLS: certificate or handshake)" : "");
        if ((u32)r == RESULT_TLS)
            CtrLog_Write(CTR_LOG_ERROR, "online: TLS FAILED with %s: this console's SSL module did not accept the "
                         "server's handshake or certificate (%s). Check with tools/tlsprobe and "
                         "%s/api/health in its urls.txt; verify=0 in online.txt tells a certificate from a "
                         "handshake problem.", link->server, link->verify ? "GlobalSign Root CA pinned" : "checks off",
                         link->server);
        httpcCloseContext(&ctx);
        return false;
    }
    /* No server behind the address: some http:C answer "success" with a
     * status that is no HTTP status at all (Azahar's 0xFFFFFFFF). */
    if (status < 100 || status > 599)
    {
        reply->result = -1;
        CtrLog_Write(CTR_LOG_ERROR, "online: %s %s: no answer (status %lu) after %lums", verb, path,
                     (unsigned long)status, (unsigned long)(CtrPlatform_Milliseconds() - t0));
        httpcCloseContext(&ctx);
        return false;
    }
    reply->status = (int)status;
    httpcGetDownloadSizeState(&ctx, &before, &total);
    capacity = total ? total : 16 * 1024;
    if (capacity > MAX_RESPONSE) capacity = MAX_RESPONSE;
    reply->body = malloc(capacity + 1);
    if (reply->body == NULL)
    {
        httpcCloseContext(&ctx);
        reply->result = -1;
        return false;
    }
    /* httpcDownloadData with a timeout: what arrived is read off the
     * module's count, a call at a time. */
    for (r = HTTPC_RESULTCODE_DOWNLOADPENDING; r == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING && reply->length < capacity;)
    {
        r = httpcReceiveDataTimeout(&ctx, reply->body + reply->length, capacity - reply->length, TIMEOUT_NS);
        if (R_FAILED(httpcGetDownloadSizeState(&ctx, &now, NULL))) break;
        reply->length = now - before;
    }
    reply->body[reply->length] = 0;
    /* The server's signature over this answer (checked below). */
    if (signing && (R_FAILED(httpcGetResponseHeader(&ctx, "X-Em3ds-Sign", answerSign, sizeof(answerSign)))
                    || answerSign[0] == 0))
        /* Cloudflare sends header names in lower case; http:C may match them exactly. */
        httpcGetResponseHeader(&ctx, "x-em3ds-sign", answerSign, sizeof(answerSign));
    httpcCloseContext(&ctx);
    if (R_FAILED(r) || (total && reply->length != total))
    {
        CtrLog_Write(CTR_LOG_ERROR, "online: %s HTTP %lu, body cut at %lu of %lu bytes (%08lx)", path,
                     (unsigned long)status, (unsigned long)reply->length, (unsigned long)total, (unsigned long)r);
        reply->result = R_FAILED(r) ? r : -1;
        ReplyFree(reply);
        reply->length = 0;
        return false;
    }
    /* A good answer counts only when the server signed it, for this request. */
    if (signing && status >= 200 && status < 300)
    {
        char expect[65];

        Sha256Hex(reply->body, reply->length, bodySha);
        snprintf(text, sizeof(text), "EM3DS1 reply\n%s\n%lu\n%s", sign, (unsigned long)status, bodySha);
        Mac(key, text, expect);
        if (strcmp(expect, answerSign) != 0)
        {
            CtrLog_Write(CTR_LOG_ERROR, "online: %s %s -> HTTP %lu NOT SIGNED by the server (%s): ignored", verb, path,
                         (unsigned long)status, answerSign[0] ? "a wrong signature" : "no signature");

            reply->status = 0;
            reply->result = (Result)RESULT_UNSIGNED;
            ReplyFree(reply);
            reply->length = 0;
            return false;
        }
    }
    CtrLog_Write(CTR_LOG_FS, "online: %s %s -> HTTP %lu, %lu bytes, %lums%s", verb, path, (unsigned long)status,
                 (unsigned long)reply->length, (unsigned long)(CtrPlatform_Milliseconds() - t0),
                 signing ? ", signed" : "");
    return true;
}

/* A JSON answer's string field, as plain text (no escapes in what the
 * server sends to consoles: tokens, names, keys, hex). */
static bool JsonString(const char *json, const char *name, char *out, unsigned size)
{
    char pattern[32];
    const char *p;
    unsigned n = 0;

    snprintf(pattern, sizeof(pattern), "\"%s\":", name);
    if (size == 0 || (p = strstr(json, pattern)) == NULL) return false;
    p += strlen(pattern);
    while (*p == ' ') ++p;
    if (*p++ != '"') return false;
    for (; *p && *p != '"' && n + 1 < size; ++p)
    {
        if (*p == '\\' && p[1]) ++p;
        out[n++] = *p;
    }
    out[n] = 0;
    return true;
}

static bool JsonNumber(const char *json, const char *name, long *out)
{
    char pattern[32];
    const char *p;

    snprintf(pattern, sizeof(pattern), "\"%s\":", name);
    if ((p = strstr(json, pattern)) == NULL) return false;
    p += strlen(pattern);
    while (*p == ' ') ++p;
    if (strncmp(p, "true", 4) == 0) *out = 1;
    else if (strncmp(p, "false", 5) == 0) *out = 0;
    else if ((*p >= '0' && *p <= '9') || *p == '-') *out = strtol(p, NULL, 10);
    else return false;
    return true;
}

/* The status line for a request that got no good answer. */
static void FailStatus(const Reply *reply, const char *what)
{
    char error[64] = "";

    if (reply->status == 0)
    {
        bool wifiDown;

        sNetFailed = true;

        Lock();
        wifiDown = sWifiDown;
        Unlock();
        if ((u32)reply->result == RESULT_TLS) SetStatus(CTR_ONLINE_ERROR, "TLS FAILED");
        else if ((u32)reply->result == (u32)RESULT_UNSIGNED) SetStatus(CTR_ONLINE_ERROR, "ANSWER NOT SIGNED");
        else SetStatus(CTR_ONLINE_ERROR, wifiDown ? "NO WI-FI" : "NO CONNECTION");
        return;
    }
    if (reply->body) JsonString((const char *)reply->body, "error", error, sizeof(error));
    CtrLog_Write(CTR_LOG_ERROR, "online: %s: HTTP %d %s", what, reply->status, error);
    if (reply->status == 401) SetStatus(CTR_ONLINE_ERROR, "NOT LINKED ANY MORE");
    else if (reply->status == 429) SetStatus(CTR_ONLINE_ERROR, "TOO MANY - WAIT");
    else if (reply->status == 426) SetStatus(CTR_ONLINE_ERROR, "SERVER TOO OLD");
    else SetStatus(CTR_ONLINE_ERROR, "ERROR %d", reply->status);
}

/* ── The jobs (worker thread) ───────────────────────────────────────── */

/*
 * Linking, over plain HTTP too: an X25519 exchange. The console sends its
 * public key and a MAC of it keyed with the code's SHA-256 (what the server
 * keeps of the code); the server answers with its own and a confirmation.
 * Both make the token from the shared secret: nothing an onlooker sees gives
 * it, and the confirmation shows the server had the code.
 */
static void DoPair(const Job *job, Link *link)
{
    char body[320], text[320], token[TOKEN_MAX], user[USER_MAX], pubHex[65], serverHex[80], codeHex[65], proof[65],
        confirm[80], expect[65];
    uint8_t secret[32], pub[32], serverPub[32], shared[32], codeKey[32], tokenKey[32], zero[32] = {0};
    Reply reply;
    int n;

    SetStatus(CTR_ONLINE_BUSY, "LINKING...");
    Random(secret, sizeof(secret));
    CtrX25519_Public(pub, secret);
    CtrHex(pub, 32, pubHex);
    CtrSha256_Digest(job->code, (uint32_t)strlen(job->code), codeKey);
    CtrHex(codeKey, 32, codeHex);
    snprintf(text, sizeof(text), "EM3DS pair\n%s", pubHex);
    Mac(codeKey, text, proof);
    n = snprintf(body, sizeof(body), "{\"pub\":\"%s\",\"proof\":\"%s\",\"console_name\":\"%s\"}", pubHex, proof,
                 job->consoleName);
    if (!Http(link, HTTPC_METHOD_POST, "/api/pair2", body, (uint32_t)n, "application/json", &reply))
    {
        FailStatus(&reply, "pair");
        return;
    }
    if (reply.status == 403) SetStatus(CTR_ONLINE_ERROR, "CODE WRONG OR USED");
    else if (reply.status == 404) SetStatus(CTR_ONLINE_ERROR, "SERVER TOO OLD");
    else if (reply.status != 200 || !JsonString((const char *)reply.body, "pub", serverHex, sizeof(serverHex))
             || !JsonString((const char *)reply.body, "user", user, sizeof(user))
             || !JsonString((const char *)reply.body, "confirm", confirm, sizeof(confirm))
             || !CtrUnhex(serverHex, serverPub, 32))
        FailStatus(&reply, "pair");
    else
    {
        CtrX25519(shared, secret, serverPub);
        snprintf(text, sizeof(text), "EM3DS token\n%s\n%s\n%s", pubHex, serverHex, codeHex);
        Mac(shared, text, token);
        CtrSha256_Digest(token, (uint32_t)strlen(token), tokenKey);
        snprintf(text, sizeof(text), "EM3DS paired\n%s\n%s\n%s", pubHex, serverHex, user);
        Mac(tokenKey, text, expect);
        if (memcmp(shared, zero, 32) == 0 || strcmp(expect, confirm) != 0)
        {
            CtrLog_Write(CTR_LOG_ERROR, "online: pair: the server's confirmation does not match (another server "
                         "in between, or a wrong code)");
            SetStatus(CTR_ONLINE_ERROR, "LINK NOT CONFIRMED");
        }
        else
        {
            snprintf(link->token, sizeof(link->token), "%s", token);
            snprintf(link->user, sizeof(link->user), "%s", user);
            link->seq = 0;
            if (!PutLink(link)) SetStatus(CTR_ONLINE_ERROR, "CANNOT WRITE SD");
            else SetStatus(CTR_ONLINE_DONE, "LINKED: %s", user);
            /* The card's author name goes to the site next. */
            sNamePending = CtrStereoMon_Author()[0] != 0;
            CtrLog_Write(CTR_LOG_FS, "online: linked to %s as %s (%s), key agreed over X25519", link->server, user,
                         job->consoleName);
        }
    }
    memset(secret, 0, sizeof(secret));
    memset(shared, 0, sizeof(shared));
    ReplyFree(&reply);
}

static int UploadOne(const Link *link, uint16_t id, SentList *sent, bool *stop)
{
    char key[8], path[64], urlPath[48], sha[65];
    uint8_t *data;
    uint32_t size;
    Reply reply;
    int result = -1;

    *stop = false;
    KeyOf(id, key, sizeof(key));
    snprintf(path, sizeof(path), EDIT_DIR "/%s.bin", key);
    data = ReadWhole(path, MAX_EDIT, &size);
    if (data == NULL)
    {
        SetStatus(CTR_ONLINE_ERROR, "NO EDIT FILE");
        return -1;
    }
    Sha256Hex(data, size, sha);
    snprintf(urlPath, sizeof(urlPath), "/api/mons/%s/versions", key);
    if (!Http(link, HTTPC_METHOD_POST, urlPath, data, size, "application/octet-stream", &reply))
    {
        FailStatus(&reply, key);
        *stop = true;
    }
    else if (reply.status == 201 || reply.status == 200)
    {
        long version = 0, current = 0;

        JsonNumber((const char *)reply.body, "version_id", &version);
        JsonNumber((const char *)reply.body, "current", &current);
        SentPut(sent, key, sha, (uint32_t)version);
        CtrLog_Write(CTR_LOG_FS, "online: %s %s as version %ld%s", key, reply.status == 201 ? "uploaded" : "already there",
                     version, current ? ", current" : "");
        result = reply.status == 201 ? 1 : 0;
    }
    else
    {
        FailStatus(&reply, key);
        *stop = reply.status == 401 || reply.status == 429 || reply.status >= 500;
        if (reply.status == 400) SetStatus(CTR_ONLINE_ERROR, "REJECTED BY SERVER");
    }
    ReplyFree(&reply);
    free(data);
    return result;
}

static void DoUpload(const Job *job, const Link *link)
{
    SentList sent;
    bool stop;
    int result;

    SetStatus(CTR_ONLINE_BUSY, "UPLOADING...");
    SentLoad(&sent);
    result = UploadOne(link, job->id, &sent, &stop);
    /* The other side too (a front's back), when asked for. */
    if (job->id2 && result >= 0 && !stop)
    {
        int second = UploadOne(link, job->id2, &sent, &stop);

        if (second < 0) result = -1;
        else if (second > 0) result = 1;
    }
    if (result > 0) SetStatus(CTR_ONLINE_DONE, "UPLOADED");
    else if (result == 0) SetStatus(CTR_ONLINE_DONE, "ALREADY ON SERVER");
    SentSave(&sent);
}

static void DoUploadAll(const Link *link)
{
    uint16_t *ids = malloc(MAX_SENT * sizeof(uint16_t));
    unsigned count = 0, done = 0, known = 0, failed = 0;
    SentList sent;
    DIR *d;
    struct dirent *e;

    SetStatus(CTR_ONLINE_BUSY, "UPLOADING...");
    if (ids == NULL)
    {
        SetStatus(CTR_ONLINE_ERROR, "OUT OF MEMORY");
        return;
    }
    SentLoad(&sent);
    if ((d = opendir(EDIT_DIR)) != NULL)
    {
        while ((e = readdir(d)) != NULL && count < MAX_SENT)
        {
            char key[16];
            size_t n = strlen(e->d_name);
            uint16_t id;

            if (n < 5 || n - 4 >= sizeof(key) || strcmp(e->d_name + n - 4, ".bin") != 0) continue;
            memcpy(key, e->d_name, n - 4);
            key[n - 4] = 0;
            if ((id = IdOf(key)) != 0) ids[count++] = id;
        }
        closedir(d);
    }
    for (unsigned i = 0; i < count; ++i)
    {
        char key[8], path[64], sha[65];
        uint8_t *data;
        uint32_t size;
        bool stop = false;
        int result;

        KeyOf(ids[i], key, sizeof(key));
        snprintf(path, sizeof(path), EDIT_DIR "/%s.bin", key);
        if ((data = ReadWhole(path, MAX_EDIT, &size)) == NULL) continue;
        Sha256Hex(data, size, sha);
        free(data);
        if (SentHas(&sent, key, sha))
        {
            ++known;
            continue;
        }
        SetStatus(CTR_ONLINE_BUSY, "SENDING %u OF %u", i + 1, count);
        result = UploadOne(link, ids[i], &sent, &stop);
        if (result > 0) ++done;
        else if (result == 0) ++known;
        else ++failed;
        if (stop) break;
    }
    SentSave(&sent);
    free(ids);
    CtrLog_Write(CTR_LOG_FS, "online: upload all: %u files, %u sent, %u known, %u failed", count, done, known, failed);
    if (failed) return; /* the failure's own status stays */
    if (count == 0) SetStatus(CTR_ONLINE_DONE, "NO EDITS TO SEND");
    else if (done == 0) SetStatus(CTR_ONLINE_DONE, "NOTHING NEW TO SEND");
    else SetStatus(CTR_ONLINE_DONE, "SENT %u OF %u", done, count);
}

/* A card file about to be replaced, kept as stereo/backup/<key>-<time>.bin. */
static bool Backup(const char *key, const uint8_t *data, uint32_t size)
{
    char path[96];
    time_t now = time(NULL);
    struct tm *t = gmtime(&now);

    mkdir(BACKUP_DIR, 0777);
    snprintf(path, sizeof(path), BACKUP_DIR "/%s-%04d%02d%02d-%02d%02d%02d.bin", key, t ? t->tm_year + 1900 : 2000,
             t ? t->tm_mon + 1 : 1, t ? t->tm_mday : 1, t ? t->tm_hour : 0, t ? t->tm_min : 0, t ? t->tm_sec : 0);
    return WriteWhole(path, data, size);
}

/* online_new.txt: one key a line. Called with sLock held. */
static void ReadNewLocked(void)
{
    FILE *f;
    char line[16];

    if (sNewRead) return;
    sNewRead = true;
    sNewCount = 0;
    if ((f = fopen(NEW_PATH, "r")) == NULL) return;
    while (sNewCount < MAX_NEW && fgets(line, sizeof(line), f))
    {
        uint16_t id;

        line[strcspn(line, "\r\n ")] = 0;
        if ((id = IdOf(line)) != 0) sNew[sNewCount++] = id;
    }
    fclose(f);
}

/* What a sync brought: kept for the NEW filter and the status's pictures. */
static void PutNew(const uint16_t *ids, unsigned count)
{
    char *text = malloc(count * 8 + 1);
    size_t n = 0;

    if (text)
    {
        for (unsigned i = 0; i < count; ++i)
        {
            char key[8];

            KeyOf(ids[i], key, sizeof(key));
            n += (size_t)sprintf(text + n, "%s\n", key);
        }
        if (!WriteWhole(NEW_PATH, text, (uint32_t)n)) CtrLog_Write(CTR_LOG_ERROR, "online: cannot write %s", NEW_PATH);
        free(text);
    }
    Lock();
    memcpy(sNew, ids, count * sizeof(ids[0]));
    sNewCount = count;
    sNewRead = true;
    ++sNewSerial;
    Unlock();
}

static void DoSync(Link *link)
{
    char urlPath[48];
    Reply list;
    SentList sent;
    const char *p;
    long seq = 0;
    unsigned got = 0, kept = 0, same = 0, items = 0, own = 0;
    bool ok = true;
    static uint16_t fetched[MAX_NEW];

    SetStatus(CTR_ONLINE_BUSY, "SYNCING...");
    snprintf(urlPath, sizeof(urlPath), "/api/sync?since=%u", link->seq);
    if (!Http(link, HTTPC_METHOD_GET, urlPath, NULL, 0, NULL, &list) || list.status != 200
        || !JsonNumber((const char *)list.body, "seq", &seq) || (p = strstr((const char *)list.body, "\"items\":[")) == NULL)
    {
        FailStatus(&list, "sync");
        ReplyFree(&list);
        return;
    }
    mkdir(EDIT_DIR, 0777);
    SentLoad(&sent);
    for (const char *item = strchr(p, '{'); item != NULL && ok; item = strchr(item + 1, '{'))
    {
        char object[320], key[16], sha[65], localSha[65], path[64], versionPath[40];
        const char *end = strchr(item, '}');
        long version = 0;
        uint8_t *local;
        uint32_t localSize;
        Reply file;

        if (end == NULL || (size_t)(end - item) >= sizeof(object)) break;
        memcpy(object, item, (size_t)(end - item + 1));
        object[end - item + 1] = 0;
        ++items;
        if (!JsonString(object, "key", key, sizeof(key)) || !JsonString(object, "sha256", sha, sizeof(sha))
            || !JsonNumber(object, "version_id", &version) || IdOf(key) == 0 || strlen(sha) != 64)
            continue;
        snprintf(path, sizeof(path), EDIT_DIR "/%s.bin", key);
        local = ReadWhole(path, MAX_EDIT, &localSize);
        if (local)
        {
            Sha256Hex(local, localSize, localSha);
            if (strcmp(localSha, sha) == 0)
            {
                SentPut(&sent, key, sha, (uint32_t)version);
                ++same;
                free(local);
                continue;
            }
            /* The card's file is no server version (an edit of this card's
             * own, not sent yet): it stays. A sync once put the server's
             * current over nine such edits, which went to backup unseen. */
            if (!SentHas(&sent, key, localSha))
            {
                ++own;
                CtrLog_Write(CTR_LOG_FS, "online: %s: the card's own edit kept, not the server's current", key);
                free(local);
                continue;
            }
        }
        SetStatus(CTR_ONLINE_BUSY, "FETCHING %s", key);
        snprintf(versionPath, sizeof(versionPath), "/api/versions/%ld", version);
        if (!Http(link, HTTPC_METHOD_GET, versionPath, NULL, 0, NULL, &file) || file.status != 200)
        {
            FailStatus(&file, key);
            ok = false;
        }
        else
        {
            Sha256Hex(file.body, file.length, localSha);
            if (strcmp(localSha, sha) != 0 || file.length < 16 || memcmp(file.body, CTR_STEREO_EDIT_MAGIC, 8) != 0)
            {
                CtrLog_Write(CTR_LOG_ERROR, "online: %s version %ld came down damaged (%lu bytes)", key, version,
                             (unsigned long)file.length);
                SetStatus(CTR_ONLINE_ERROR, "DOWNLOAD DAMAGED");
                ok = false;
            }
            else if (local && !Backup(key, local, localSize))
            {
                SetStatus(CTR_ONLINE_ERROR, "CANNOT WRITE SD");
                ok = false;
            }
            else if (!WriteWhole(path, file.body, file.length))
            {
                SetStatus(CTR_ONLINE_ERROR, "CANNOT WRITE SD");
                ok = false;
            }
            else
            {
                SentPut(&sent, key, sha, (uint32_t)version);
                if (got < MAX_NEW) fetched[got] = IdOf(key);
                ++got;
                kept += local != NULL;
                CtrLog_Write(CTR_LOG_FS, "online: %s now version %ld%s", key, version,
                             local ? " (the card's kept in stereo/backup)" : "");
            }
        }
        ReplyFree(&file);
        free(local);
    }
    ReplyFree(&list);
    SentSave(&sent);
    if (got)
    {
        Lock();
        sEditsWritten = true;
        Unlock();
        PutNew(fetched, got < MAX_NEW ? got : MAX_NEW);
    }
    CtrLog_Write(CTR_LOG_FS, "online: sync since %u: %u items, %u fetched, %u kept in backup, %u the same, %u own edits kept%s", link->seq,
                 items, got, kept, same, own, ok ? "" : ", STOPPED");
    if (!ok) return; /* the failure's status stays, and the next sync starts from the same place */
    link->seq = (unsigned)seq;
    if (!PutLink(link)) CtrLog_Write(CTR_LOG_ERROR, "online: cannot write %s", LINK_PATH);
    if (got == 0) SetStatus(CTR_ONLINE_DONE, "UP TO DATE");
    else if (kept) SetStatus(CTR_ONLINE_DONE, "GOT %u. BACKUP %u", got, kept);
    else SetStatus(CTR_ONLINE_DONE, "GOT %u NEW", got);
}

/* The card's file of an id: its SHA-256, or "" without one. */
static void CardSha(uint16_t id, char sha[65])
{
    char key[8], path[64];
    uint32_t size;
    uint8_t *data;

    KeyOf(id, key, sizeof(key));
    snprintf(path, sizeof(path), EDIT_DIR "/%s.bin", key);
    sha[0] = 0;
    if ((data = ReadWhole(path, MAX_EDIT, &size)) != NULL)
    {
        Sha256Hex(data, size, sha);
        free(data);
    }
}

/* The numbers in a JSON array named `name`, up to `max`. */
static unsigned JsonNumbers(const char *json, const char *name, long *out, unsigned max)
{
    char pattern[32];
    const char *p;
    unsigned n = 0;

    snprintf(pattern, sizeof(pattern), "\"%s\":[", name);
    if ((p = strstr(json, pattern)) == NULL) return 0;
    for (p += strlen(pattern); *p && *p != ']' && n < max;)
    {
        char *end;
        long v = strtol(p, &end, 10);

        if (end == p) ++p;
        else
        {
            out[n++] = v;
            p = end;
        }
    }
    return n;
}

/* The server's list of a picture's versions (GET .../versions, or a vote's
 * answer) into sVersions. */
static Fav *FavLocked(uint16_t id, bool create);
static void LearnFavouritesLocked(uint16_t id, const long *fav, unsigned favs);

static bool ParseVersions(uint16_t id, const char *json)
{
    CtrOnlineVersions list;
    char shas[CTR_ONLINE_MAX_VERSIONS][65], card[65];
    long current = 0, pinned = 0, most = CTR_ONLINE_MAX_VOTES, fav[CTR_ONLINE_MAX_VOTES];
    unsigned favs = JsonNumbers(json, "favourites", fav, CTR_ONLINE_MAX_VOTES);
    const char *p = strstr(json, "\"versions\":[");

    if (p == NULL) return false;
    memset(&list, 0, sizeof(list));
    list.id = id;
    JsonNumber(json, "current", &current);
    JsonNumber(json, "pinned", &pinned);
    JsonNumber(json, "max_favourites", &most);
    list.pinned = pinned != 0;
    list.maxFavourites = (uint8_t)(most < CTR_ONLINE_MAX_VOTES ? most : CTR_ONLINE_MAX_VOTES);
    CardSha(id, card);
    for (const char *item = strchr(p, '{'); item != NULL && list.count < CTR_ONLINE_MAX_VERSIONS;
         item = strchr(item + 1, '{'))
    {
        char object[512];
        const char *end = strchr(item, '}');
        CtrOnlineVersion *v = &list.v[list.count];
        long number = 0;

        if (end == NULL || (size_t)(end - item) >= sizeof(object)) break;
        memcpy(object, item, (size_t)(end - item + 1));
        object[end - item + 1] = 0;
        if (!JsonNumber(object, "version_id", &number)) continue;
        v->version = (uint32_t)number;
        JsonString(object, "author", v->author, sizeof(v->author));
        v->base[0] = 0;
        JsonString(object, "base", v->base, sizeof(v->base));
        FontSafe(v->base);
        FontSafe(v->author);
        if (JsonNumber(object, "revision", &number)) v->revision = (uint16_t)number;
        if (JsonNumber(object, "votes", &number)) v->votes = (uint16_t)number;
        if (JsonNumber(object, "final", &number)) v->final = number != 0;
        if (JsonNumber(object, "mine", &number)) v->mine = number != 0;
        if (JsonNumber(object, "uploaded_at", &number)) v->uploaded = (uint32_t)number;
        v->current = v->version == (uint32_t)current;
        for (unsigned f = 0; f < favs; ++f) v->favourite |= (uint32_t)fav[f] == v->version;
        JsonString(object, "sha256", shas[list.count], sizeof(shas[0]));
        v->onCard = card[0] && strcmp(card, shas[list.count]) == 0;
        ++list.count;
    }
    /* The card's file is one of these: its number kept with it. */
    for (unsigned i = 0; i < list.count; ++i)
        if (list.v[i].onCard)
        {
            SentList sent;
            char key[8];

            KeyOf(id, key, sizeof(key));
            SentLoad(&sent);
            SentPut(&sent, key, shas[i], list.v[i].version);
            SentSave(&sent);
            break;
        }
    Lock();
    LearnFavouritesLocked(id, fav, favs);
    {
        /* The console's own favourites, while they wait to be sent. */
        Fav *fv = FavLocked(id, false);

        if (fv && fv->pending)
            for (unsigned i = 0; i < list.count; ++i)
            {
                list.v[i].favourite = false;
                for (unsigned k = 0; k < fv->n; ++k) list.v[i].favourite |= fv->v[k] == list.v[i].version;
            }
    }
    sVersions = list;
    memcpy(sVersionSha, shas, sizeof(shas));
    ++sVersionsSerial;
    Unlock();
    return true;
}

static void DoVersions(const Job *job, const Link *link)
{
    char key[8], path[48];
    Reply reply;

    SetStatus(CTR_ONLINE_BUSY, "GETTING VERSIONS...");
    KeyOf(job->id, key, sizeof(key));
    snprintf(path, sizeof(path), "/api/mons/%s/versions", key);
    if (!Http(link, HTTPC_METHOD_GET, path, NULL, 0, NULL, &reply) || reply.status != 200)
        FailStatus(&reply, "versions");
    else if (!ParseVersions(job->id, (const char *)reply.body)) SetStatus(CTR_ONLINE_ERROR, "BAD ANSWER");
    else
    {
        Lock();
        unsigned count = sVersions.count;
        Unlock();
        if (count == 0) SetStatus(CTR_ONLINE_DONE, "NONE ON THE SERVER");
        else SetStatus(CTR_ONLINE_DONE, "%u VERSION%s", count, count == 1 ? "" : "S");
    }
    ReplyFree(&reply);
}

/* One version onto the card: the card's own first to stereo/backup unless
 * the server has it too. */
static void DoUse(const Job *job, const Link *link)
{
    char key[8], path[64], urlPath[40], sha[65], card[65];
    Reply reply;
    uint8_t *local;
    uint32_t localSize;
    SentList sent;

    SetStatus(CTR_ONLINE_BUSY, "LOADING %lu...", (unsigned long)job->version);
    KeyOf(job->id, key, sizeof(key));
    snprintf(urlPath, sizeof(urlPath), "/api/versions/%lu", (unsigned long)job->version);
    if (!Http(link, HTTPC_METHOD_GET, urlPath, NULL, 0, NULL, &reply) || reply.status != 200)
    {
        FailStatus(&reply, key);
        ReplyFree(&reply);
        return;
    }
    if (reply.length < 16 || memcmp(reply.body, CTR_STEREO_EDIT_MAGIC, 8) != 0
        || (reply.body[8] | (reply.body[9] << 8)) != job->id)
    {
        SetStatus(CTR_ONLINE_ERROR, "DOWNLOAD DAMAGED");
        ReplyFree(&reply);
        return;
    }
    Sha256Hex(reply.body, reply.length, sha);
    snprintf(path, sizeof(path), EDIT_DIR "/%s.bin", key);
    mkdir(EDIT_DIR, 0777);
    if ((local = ReadWhole(path, MAX_EDIT, &localSize)) != NULL)
    {
        bool known = false;

        Sha256Hex(local, localSize, card);
        Lock();
        for (unsigned i = 0; i < sVersions.count && sVersions.id == job->id; ++i)
            known |= strcmp(sVersionSha[i], card) == 0;
        Unlock();
        if (!known && strcmp(card, sha) != 0 && !Backup(key, local, localSize))
        {
            SetStatus(CTR_ONLINE_ERROR, "CANNOT WRITE SD");
            free(local);
            ReplyFree(&reply);
            return;
        }
        if (!known && strcmp(card, sha) != 0)
            CtrLog_Write(CTR_LOG_FS, "online: %s's card file was on no server version: kept in stereo/backup", key);
        free(local);
    }
    if (!WriteWhole(path, reply.body, reply.length)) SetStatus(CTR_ONLINE_ERROR, "CANNOT WRITE SD");
    else
    {
        SentLoad(&sent);
        SentPut(&sent, key, sha, job->version);
        SentSave(&sent);
        Lock();
        for (unsigned i = 0; i < sVersions.count && sVersions.id == job->id; ++i)
            sVersions.v[i].onCard = sVersions.v[i].version == job->version;
        ++sVersionsSerial;
        sEditsWritten = true;
        sShownWritten = true;
        Unlock();
        CtrLog_Write(CTR_LOG_FS, "online: %s now version %lu (picked on the console)", key, (unsigned long)job->version);
        SetStatus(CTR_ONLINE_DONE, "NOW VERSION %lu", (unsigned long)job->version);
    }
    ReplyFree(&reply);
}

/* ── The voting view's files and the web's focus ─────────────────────── */

static int CachedAtLocked(uint32_t version)
{
    for (unsigned i = 0; i < FILE_CACHE; ++i)
        if (sFiles[i].data && sFiles[i].version == version) return (int)i;
    return -1;
}

/* Each asked-for version not in memory yet, one request each, kept in the
 * cache as it comes (the view takes each up as soon as it is there). */
static void DoFiles(const Job *job, const Link *link)
{
    unsigned got = 0, failed = 0;

    for (unsigned k = 0; k < job->fileCount; ++k)
    {
        char urlPath[40];
        Reply reply;
        bool have;
        int slot;

        Lock();
        have = CachedAtLocked(job->files[k]) >= 0;
        Unlock();
        if (have) continue;
        SetStatus(CTR_ONLINE_BUSY, "PICTURES %u/%u", k + 1, job->fileCount);
        snprintf(urlPath, sizeof(urlPath), "/api/versions/%lu", (unsigned long)job->files[k]);
        if (!Http(link, HTTPC_METHOD_GET, urlPath, NULL, 0, NULL, &reply) || reply.status != 200)
        {
            ++failed;
            FailStatus(&reply, "file");
            ReplyFree(&reply);
            if (sNetFailed) return;
            continue;
        }
        if (reply.length < 16 || memcmp(reply.body, CTR_STEREO_EDIT_MAGIC, 8) != 0
            || (reply.body[8] | (reply.body[9] << 8)) != job->fileIds[k])
        {
            ++failed;
            CtrLog_Write(CTR_LOG_ERROR, "online: version %lu's file is damaged", (unsigned long)job->files[k]);
            ReplyFree(&reply);
            continue;
        }
        Lock();
        /* An empty slot, else the one asked for longest ago. */
        slot = 0;
        for (unsigned i = 0; i < FILE_CACHE; ++i)
        {
            if (sFiles[i].data == NULL) { slot = (int)i; break; }
            if (sFiles[i].used < sFiles[slot].used) slot = (int)i;
        }
        free(sFiles[slot].data);
        sFiles[slot].data = reply.body;
        sFiles[slot].size = reply.length;
        sFiles[slot].version = job->files[k];
        sFiles[slot].id = job->fileIds[k];
        sFiles[slot].used = ++sFilesUse;
        ++sFilesSerial;
        Unlock();
        reply.body = NULL;
        ++got;
    }
    if (failed) SetStatus(CTR_ONLINE_ERROR, "%u PICTURE%s MISSING", failed, failed == 1 ? "" : "S");
    else SetStatus(CTR_ONLINE_DONE, got ? "PICTURES IN" : "READY");
}

/* The account's web app's focus: {"key", "version_ids", "seq", "at"}. Quiet:
 * it runs every two seconds while the console follows the web. */
static void DoFocus(const Link *link)
{
    Reply reply;
    CtrOnlineFocus focus;
    char key[16] = "";
    long seq = 0, selected = 0, ids[CTR_ONLINE_MAX_VERSIONS];

    memset(&focus, 0, sizeof(focus));
    if (!Http(link, HTTPC_METHOD_GET, "/api/focus", NULL, 0, NULL, &reply) || reply.status != 200)
    {
        if (!sFocusFailed)
            CtrLog_Write(CTR_LOG_ERROR, "online: focus: HTTP %d (logged once until it works)", reply.status);
        sFocusFailed = true;
        if (reply.status == 0) sNetFailed = true;
        Lock();
        sFocus.ok = false;
        ++sFocusSerial;
        Unlock();
        ReplyFree(&reply);
        return;
    }
    sFocusFailed = false;
    JsonNumber((const char *)reply.body, "seq", &seq);
    if (JsonString((const char *)reply.body, "key", key, sizeof(key)))
    {
        focus.id = IdOf(key);
        focus.count = (uint8_t)JsonNumbers((const char *)reply.body, "version_ids", ids, CTR_ONLINE_MAX_VERSIONS);
        for (unsigned i = 0; i < focus.count; ++i) focus.versions[i] = (uint32_t)ids[i];
    }
    focus.seq = (uint32_t)seq;
    if (focus.id && JsonNumber((const char *)reply.body, "selected", &selected) && selected > 0) focus.selected = (uint32_t)selected;
    focus.ok = true;
    Lock();
    if (sFocus.seq != focus.seq || !sFocus.ok) CtrLog_Write(CTR_LOG_FS, "online: the web shows %s (seq %ld, %u picked, version %lu chosen)",
                                                            key[0] ? key : "nothing", seq, focus.count,
                                                            (unsigned long)focus.selected);
    sFocus = focus;
    ++sFocusSerial;
    Unlock();
    ReplyFree(&reply);
}

/* ── Favourites (votes), kept on the card until the server has them ─── */

/* Called with sLock held. */
static void ReadFavsLocked(void)
{
    FILE *f;
    char line[40 + CTR_ONLINE_MAX_VOTES * 12];

    if (sFavsRead) return;
    sFavsRead = true;
    sFavCount = 0;
    if ((f = fopen(VOTES_PATH, "r")) == NULL) return;
    while (sFavCount < MAX_FAVS && fgets(line, sizeof(line), f))
    {
        char key[16], *p;
        unsigned pending = 0;
        int used = 0;
        Fav *fv = &sFavs[sFavCount];

        if (sscanf(line, "%15s %u%n", key, &pending, &used) < 2 || (fv->id = IdOf(key)) == 0) continue;
        fv->pending = pending != 0;
        fv->n = 0;
        /* The numbers after it (0 is none: the old two-column lines). */
        for (p = line + used; fv->n < CTR_ONLINE_MAX_VOTES;)
        {
            char *end;
            unsigned long v = strtoul(p, &end, 10);

            if (end == p) break;
            p = end;
            if (v) fv->v[fv->n++] = (uint32_t)v;
        }
        ++sFavCount;
    }
    fclose(f);
}

/* Called with sLock held. */
static Fav *FavLocked(uint16_t id, bool create)
{
    ReadFavsLocked();
    for (unsigned i = 0; i < sFavCount; ++i)
        if (sFavs[i].id == id) return &sFavs[i];
    if (!create || sFavCount == MAX_FAVS) return NULL;
    memset(&sFavs[sFavCount], 0, sizeof(sFavs[0]));
    sFavs[sFavCount].id = id;
    return &sFavs[sFavCount++];
}

static void WriteFavs(void)
{
    const size_t per = 16 + CTR_ONLINE_MAX_VOTES * 11;
    char *text;
    size_t n = 0, size;

    Lock();
    size = sFavCount * per + 1;
    if ((text = malloc(size)) == NULL)
    {
        Unlock();
        return;
    }
    for (unsigned i = 0; i < sFavCount; ++i)
    {
        char key[8];

        KeyOf(sFavs[i].id, key, sizeof(key));
        n += (size_t)snprintf(text + n, size - n, "%s %u", key, sFavs[i].pending ? 1u : 0u);
        for (unsigned k = 0; k < sFavs[i].n; ++k)
            n += (size_t)snprintf(text + n, size - n, " %lu", (unsigned long)sFavs[i].v[k]);
        n += (size_t)snprintf(text + n, size - n, "\n");
    }
    Unlock();
    if (!WriteWhole(VOTES_PATH, text, (uint32_t)n)) CtrLog_Write(CTR_LOG_ERROR, "online: cannot write %s", VOTES_PATH);
    free(text);
}

/* ── Review marks, kept on the card until the server has them ──────── */

/* Called with sLock held. */
static void ReadMarksLocked(void)
{
    FILE *f;
    char line[40];

    if (sMarksRead) return;
    sMarksRead = true;
    sMarkCount = 0;
    if ((f = fopen(REVIEWS_PATH, "r")) == NULL) return;
    while (sMarkCount < MAX_REVIEWS && fgets(line, sizeof(line), f))
    {
        char key[16];
        unsigned status = 0, pending = 0;
        Mark *m = &sMarks[sMarkCount];

        if (sscanf(line, "%15s %u %u", key, &status, &pending) < 2 || (m->id = IdOf(key)) == 0
            || status > CTR_ONLINE_REVIEW_OK)
            continue;
        m->status = (uint8_t)status;
        m->pending = pending != 0;
        ++sMarkCount;
    }
    fclose(f);
}

/* Called with sLock held. */
static Mark *MarkLocked(uint16_t id, bool create)
{
    ReadMarksLocked();
    for (unsigned i = 0; i < sMarkCount; ++i)
        if (sMarks[i].id == id) return &sMarks[i];
    if (!create || sMarkCount == MAX_REVIEWS) return NULL;
    memset(&sMarks[sMarkCount], 0, sizeof(sMarks[0]));
    sMarks[sMarkCount].id = id;
    return &sMarks[sMarkCount++];
}

static void WriteMarks(void)
{
    char *text;
    size_t n = 0, size;

    Lock();
    size = sMarkCount * 16 + 1;
    if ((text = malloc(size)) == NULL)
    {
        Unlock();
        return;
    }
    for (unsigned i = 0; i < sMarkCount; ++i)
    {
        char key[8];

        /* A mark the server has and that is none needs no line. */
        if (sMarks[i].status == CTR_ONLINE_REVIEW_NONE && !sMarks[i].pending) continue;
        KeyOf(sMarks[i].id, key, sizeof(key));
        n += (size_t)snprintf(text + n, size - n, "%s %u %u\n", key, sMarks[i].status, sMarks[i].pending ? 1u : 0u);
    }
    Unlock();
    if (!WriteWhole(REVIEWS_PATH, text, (uint32_t)n)) CtrLog_Write(CTR_LOG_ERROR, "online: cannot write %s", REVIEWS_PATH);
    free(text);
}

static unsigned PendingMarks(void)
{
    unsigned count = 0;

    Lock();
    ReadMarksLocked();
    for (unsigned i = 0; i < sMarkCount; ++i) count += sMarks[i].pending;
    Unlock();
    return count;
}

static const char *const sMarkWords[] = {"", "fix", "ok"};

/* Every mark still on the card to the server. False when the server could
 * not be reached (they wait for the next time). */
static bool FlushMarks(const Link *link)
{
    unsigned sent = 0;

    for (;;)
    {
        Mark m;
        bool found = false;
        char key[8], path[48], body[32];
        Reply reply;
        int n;

        Lock();
        ReadMarksLocked();
        for (unsigned i = 0; i < sMarkCount && !found; ++i)
            if (sMarks[i].pending)
            {
                m = sMarks[i];
                found = true;
            }
        Unlock();
        if (!found) break;
        SetStatus(CTR_ONLINE_BUSY, "SENDING MARK...");
        KeyOf(m.id, key, sizeof(key));
        snprintf(path, sizeof(path), "/api/mons/%s/review", key);
        n = snprintf(body, sizeof(body), "{\"status\":\"%s\"}", sMarkWords[m.status]);
        if (!Http(link, HTTPC_METHOD_POST, path, body, (uint32_t)n, "application/json", &reply) || reply.status == 0)
        {
            ReplyFree(&reply);
            CtrLog_Write(CTR_LOG_FS, "online: no server: the review marks wait on the card");
            SetStatus(CTR_ONLINE_DONE, "SAVED - SENDS LATER");
            WriteMarks();
            return false;
        }
        if (reply.status == 401 || reply.status == 429 || reply.status >= 500)
        {
            FailStatus(&reply, "mark");
            ReplyFree(&reply);
            WriteMarks();
            return false;
        }
        if (reply.status != 200)
            CtrLog_Write(CTR_LOG_ERROR, "online: %s mark %s refused (HTTP %d)", key, sMarkWords[m.status], reply.status);
        else
            CtrLog_Write(CTR_LOG_FS, "online: %s marked \"%s\"", key, sMarkWords[m.status]);
        ReplyFree(&reply);
        Lock();
        {
            Mark *now = MarkLocked(m.id, false);

            /* Sent as it stands (the player may have changed it meanwhile). */
            if (now && now->status == m.status) now->pending = false;
        }
        Unlock();
        ++sent;
    }
    WriteMarks();
    if (sent) SetStatus(CTR_ONLINE_DONE, sent == 1 ? "MARK SENT" : "%u MARKS SENT", sent);
    return true;
}

/* The server's marks ("reviews": {"025": "fix", ...} in GET /api/mons): every
 * mark not still on its way from here takes the server's, or none. */
static void LearnMarks(const char *json)
{
    const char *p = strstr(json, "\"reviews\":{");

    if (p == NULL) return;
    p += 11;
    Lock();
    ReadMarksLocked();
    for (unsigned i = 0; i < sMarkCount; ++i)
        if (!sMarks[i].pending) sMarks[i].status = CTR_ONLINE_REVIEW_NONE;
    while (*p && *p != '}')
    {
        char key[16], word[8];
        const char *q;
        size_t len;
        uint16_t id;

        if ((p = strchr(p, '"')) == NULL) break;
        q = strchr(++p, '"');
        if (q == NULL || (len = (size_t)(q - p)) >= sizeof(key)) break;
        memcpy(key, p, len);
        key[len] = 0;
        if ((p = strchr(q + 1, '"')) == NULL) break;
        q = strchr(++p, '"');
        if (q == NULL || (len = (size_t)(q - p)) >= sizeof(word)) break;
        memcpy(word, p, len);
        word[len] = 0;
        p = q + 1;
        if ((id = IdOf(key)) != 0)
        {
            Mark *m = MarkLocked(id, true);

            if (m && !m->pending)
                m->status = strcmp(word, "fix") == 0 ? CTR_ONLINE_REVIEW_FIX
                          : strcmp(word, "ok") == 0 ? CTR_ONLINE_REVIEW_OK : CTR_ONLINE_REVIEW_NONE;
        }
        while (*p == ' ' || *p == ',') ++p;
    }
    ++sMarkSerial;
    Unlock();
    WriteMarks();
}

/* The votes and the review marks still on the card. */
static unsigned PendingVotes(void)
{
    unsigned count = 0;

    Lock();
    ReadFavsLocked();
    for (unsigned i = 0; i < sFavCount; ++i) count += sFavs[i].pending;
    Unlock();
    return count + PendingMarks();
}

/* The server's own list of a picture's favourites: kept unless the
 * console's are still on their way. Called with sLock held. */
static void LearnFavouritesLocked(uint16_t id, const long *fav, unsigned favs)
{
    Fav *fv = FavLocked(id, favs > 0);

    if (fv == NULL || fv->pending) return;
    fv->n = (uint8_t)(favs < CTR_ONLINE_MAX_VOTES ? favs : CTR_ONLINE_MAX_VOTES);
    for (unsigned i = 0; i < fv->n; ++i) fv->v[i] = (uint32_t)fav[i];
}

/* Every vote still on the card to the server, one picture at a time. A
 * server that cannot be reached leaves them for the next time. */
static void DoFlush(const Link *link)
{
    unsigned sent = 0, refused = 0;

    if (PendingMarks() && !FlushMarks(link)) return;
    for (;;)
    {
        Fav fv;
        bool found = false;
        char key[8], path[48], body[32 + CTR_ONLINE_MAX_VOTES * 11];
        Reply reply;
        int n;

        Lock();
        ReadFavsLocked();
        for (unsigned i = 0; i < sFavCount && !found; ++i)
            if (sFavs[i].pending)
            {
                fv = sFavs[i];
                found = true;
            }
        Unlock();
        if (!found) break;
        SetStatus(CTR_ONLINE_BUSY, "SENDING VOTE...");
        KeyOf(fv.id, key, sizeof(key));
        snprintf(path, sizeof(path), "/api/mons/%s/votes", key);
        n = snprintf(body, sizeof(body), "{\"version_ids\":[");
        for (unsigned k = 0; k < fv.n; ++k)
            n += snprintf(body + n, sizeof(body) - (size_t)n, "%s%lu", k ? "," : "", (unsigned long)fv.v[k]);
        n += snprintf(body + n, sizeof(body) - (size_t)n, "]}");
        if (!Http(link, HTTPC_METHOD_POST, path, body, (uint32_t)n, "application/json", &reply) || reply.status == 0)
        {
            ReplyFree(&reply);
            CtrLog_Write(CTR_LOG_FS, "online: no server: the votes wait on the card");
            SetStatus(CTR_ONLINE_DONE, "SAVED - SENDS LATER");
            WriteFavs();
            return;
        }
        if (reply.status == 401 || reply.status == 429 || reply.status >= 500)
        {
            FailStatus(&reply, "vote");
            ReplyFree(&reply);
            WriteFavs();
            return;
        }
        Lock();
        {
            Fav *now = FavLocked(fv.id, false);

            /* Sent as it stands (the player may have changed it meanwhile). */
            if (now && now->n == fv.n && memcmp(now->v, fv.v, fv.n * sizeof(fv.v[0])) == 0) now->pending = false;
        }
        Unlock();
        if (reply.status == 200)
        {
            bool shown;

            Lock();
            shown = sVersions.id == fv.id;
            Unlock();
            if (shown) ParseVersions(fv.id, (const char *)reply.body);
            CtrLog_Write(CTR_LOG_FS, "online: %s favourites %s sent", key, body);
            ++sent;
        }
        else
        {
            /* A version that is gone: the vote is dropped, not retried for ever. */
            CtrLog_Write(CTR_LOG_ERROR, "online: %s favourites %s refused (HTTP %d)", key, body, reply.status);
            ++refused;
        }
        ReplyFree(&reply);
    }
    WriteFavs();
    if (refused) SetStatus(CTR_ONLINE_ERROR, "VOTE REFUSED");
    else if (sent) SetStatus(CTR_ONLINE_DONE, sent == 1 ? "VOTE SENT" : "%u VOTES SENT", sent);
}

/* How many versions each picture has on the server, and how many of them
 * this account voted for (the Pokédex's marks). */
static void DoIndex(const Link *link)
{
    static IndexEntry list[MAX_INDEX];
    unsigned count = 0;
    Reply reply;
    const char *p;

    SetStatus(CTR_ONLINE_BUSY, "COUNTING VERSIONS...");
    if (!Http(link, HTTPC_METHOD_GET, "/api/mons", NULL, 0, NULL, &reply) || reply.status != 200
        || (p = strstr((const char *)reply.body, "\"items\":[")) == NULL)
    {
        FailStatus(&reply, "index");
        ReplyFree(&reply);
        return;
    }
    for (const char *item = strchr(p, '{'); item != NULL && count < MAX_INDEX; item = strchr(item + 1, '{'))
    {
        char object[320], key[16];
        const char *end = strchr(item, '}');
        long versions = 0, mine = 0;

        if (end == NULL || (size_t)(end - item) >= sizeof(object)) break;
        memcpy(object, item, (size_t)(end - item + 1));
        object[end - item + 1] = 0;
        if (!JsonString(object, "key", key, sizeof(key)) || IdOf(key) == 0) continue;
        JsonNumber(object, "versions", &versions);
        JsonNumber(object, "mine", &mine);
        list[count].author[0] = 0;
        JsonString(object, "author", list[count].author, sizeof(list[count].author));
        FontSafe(list[count].author);
        list[count].id = IdOf(key);
        list[count].count = (uint8_t)(versions < 255 ? versions : 255);
        list[count].mine = (uint8_t)(mine < 255 ? mine : 255);
        ++count;
    }
    LearnMarks((const char *)reply.body);
    ReplyFree(&reply);
    Lock();
    memcpy(sIndex, list, count * sizeof(list[0]));
    sIndexCount = count;
    sIndexAt = CtrPlatform_Milliseconds();
    ++sIndexSerial;
    Unlock();
    CtrLog_Write(CTR_LOG_FS, "online: %u pictures on the server", count);
    SetStatus(CTR_ONLINE_DONE, "%u ON THE SERVER", count);
}

/* This account's own version off the server; the list comes back. */
static void DoDelete(const Job *job, const Link *link)
{
    char key[8], path[48];
    Reply reply;

    SetStatus(CTR_ONLINE_BUSY, "DELETING...");
    KeyOf(job->id, key, sizeof(key));
    snprintf(path, sizeof(path), "/api/versions/%lu/delete", (unsigned long)job->version);
    if (!Http(link, HTTPC_METHOD_POST, path, "{}", 2, "application/json", &reply) || reply.status != 200)
    {
        FailStatus(&reply, "delete");
        if (reply.status == 403) SetStatus(CTR_ONLINE_ERROR, "NOT YOURS TO DELETE");
    }
    else
    {
        SentList sent;

        /* The card keeps its file; the server no longer has it. */
        SentLoad(&sent);
        for (unsigned i = 0; i < sent.count; ++i)
            if (strcmp(sent.items[i].key, key) == 0 && sent.items[i].version == job->version)
            {
                /* Still counted as sent: UPLOAD ALL does not put it back. */
                sent.items[i].version = 0;
                sent.changed = true;
            }
        SentSave(&sent);
        ParseVersions(job->id, (const char *)reply.body);
        CtrLog_Write(CTR_LOG_FS, "online: %s version %lu deleted from the server", key, (unsigned long)job->version);
        SetStatus(CTR_ONLINE_DONE, "DELETED FROM SERVER");
    }
    ReplyFree(&reply);
}

/* The author's name as the site's name for this account (POST /api/me). */
static void DoName(const Link *link)
{
    char name[24], body[64];
    Reply reply;
    int n;

    snprintf(name, sizeof(name), "%s", CtrStereoMon_Author());
    if (name[0] == 0)
    {
        sNamePending = false;
        return;
    }
    SetStatus(CTR_ONLINE_BUSY, "SENDING YOUR NAME...");
    n = snprintf(body, sizeof(body), "{\"name\":\"%s\"}", name);
    if (!Http(link, HTTPC_METHOD_POST, "/api/me", body, (uint32_t)n, "application/json", &reply))
    {
        FailStatus(&reply, "name");
        return;
    }
    if (reply.status == 200)
    {
        sNamePending = false;
        SetStatus(CTR_ONLINE_DONE, "NAME SAVED: %s", name);
        CtrLog_Write(CTR_LOG_FS, "online: the site's name for %s is now %s", link->user, name);
    }
    else if (reply.status == 404 || reply.status == 405)
    {
        sNamePending = false;
        SetStatus(CTR_ONLINE_ERROR, "SERVER TOO OLD");
    }
    else if (reply.status == 400)
    {
        sNamePending = false;
        SetStatus(CTR_ONLINE_ERROR, "NAME REFUSED");
    }
    else FailStatus(&reply, "name");
    ReplyFree(&reply);
}

/* TEST SERVER: what tools/tlsprobe tells, for the server in use. A line
 * each, on the ONLINE tab and in port.log. */
static void TestLine(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void TestLine(const char *format, ...)
{
    char line[64];
    va_list args;

    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    CtrLog_Write(CTR_LOG_FS, "online: test server: %s", line);
    FontSafe(line);
    Lock();
    if (sTestCount < TEST_LINES) Copy(sTestLines[sTestCount++], sizeof(sTestLines[0]), line);
    Unlock();
}

static void DoTest(const Link *link)
{
    Reply reply;
    long ok = 0, api = 0;
    bool https = strncmp(link->server, "https://", 8) == 0, reached;
    char user[USER_MAX];

    Lock();
    sTestCount = 0;
    Unlock();
    SetStatus(CTR_ONLINE_BUSY, "TESTING THE SERVER...");
    CtrLog_Write(CTR_LOG_FS, "online: test server %s (%s)", link->server,
                 !https ? "plain http" : link->verify ? "GlobalSign Root CA pinned" : "certificate checks off");
    /* The handshake (with the pinned root) and the health answer. */
    reached = Http(link, HTTPC_METHOD_GET, "/api/health", NULL, 0, NULL, &reply);
    if (!reached)
    {
        if ((u32)reply.result == RESULT_TLS && https)
        {
            Link off = *link;
            Reply again;

            TestLine("TLS FAIL %08lX", (unsigned long)(u32)reply.result);
            /* Certificate or handshake: the same without checking it. */
            off.verify = false;
            if (Http(&off, HTTPC_METHOD_GET, "/api/health", NULL, 0, NULL, &again))
                TestLine("CERT NOT ACCEPTED");
            else
                TestLine("HANDSHAKE FAILS %08lX", (unsigned long)(u32)again.result);
            ReplyFree(&again);
        }
        else
            TestLine("NO CONNECTION %08lX", (unsigned long)(u32)reply.result);
        ReplyFree(&reply);
        SetStatus(CTR_ONLINE_ERROR, "SERVER TEST FAILED");
        return;
    }
    TestLine("%s OK", https ? "TLS" : "HTTP");
    if (reply.status == 200 && JsonNumber((const char *)reply.body, "ok", &ok) && ok && JsonNumber((const char *)reply.body, "api", &api))
        TestLine("HEALTH OK API %ld", api);
    else
        TestLine("HEALTH HTTP %d", reply.status);
    ReplyFree(&reply);
    /* This console's link: a signed request, and the server's signed answer. */
    if (link->token[0] == 0)
        TestLine("NOT LINKED YET");
    else if (Http(link, HTTPC_METHOD_GET, "/api/me", NULL, 0, NULL, &reply) && reply.status == 200
             && JsonString((const char *)reply.body, "user", user, sizeof(user)))
        TestLine("LINKED AS %s", user);
    else if ((u32)reply.result == (u32)RESULT_UNSIGNED)
        TestLine("ANSWER NOT SIGNED");
    else if (reply.status == 426)
        TestLine("SERVER TOO OLD");
    else if (reply.status == 401)
        TestLine("SIGNATURE REFUSED");
    else
        TestLine("ME HTTP %d", reply.status);
    ReplyFree(&reply);
    SetStatus(api ? CTR_ONLINE_DONE : CTR_ONLINE_ERROR, api ? "SERVER OK" : "SERVER ANSWERS ODDLY");
}

static void Worker(void *arg)
{
    Job job = sJob;
    Link link = GetLink();
    u32 wifi = 0;
    Result r;

    (void)arg;
    /* Only for the log and a clearer message: a request is tried anyway. */
    if (R_SUCCEEDED(acInit()))
    {
        ACU_GetWifiStatus(&wifi);
        acExit();
    }
    Lock();
    sWifiDown = wifi == 0;
    Unlock();
    sNetFailed = false;
    if (job.type != JOB_FOCUS)
        CtrLog_Write(CTR_LOG_FS, "online: job %d on %s (Wi-Fi %lu%s)", job.type, link.server, (unsigned long)wifi,
                     link.verify ? "" : ", certificate checks OFF");
    /* The server's clock, once learnt, serves every job for TIME_REFRESH_MS
     * (Http): asking again per job cost a round trip, seconds on a slow
     * Wi-Fi, before every upload. */
    sPsReady = R_SUCCEEDED(psInit());
    r = httpcInit(HTTPC_SHARED);
    if (R_FAILED(r))
    {
        CtrLog_Write(CTR_LOG_ERROR, "online: httpcInit failed %08lx", (unsigned long)r);
        SetStatus(CTR_ONLINE_ERROR, "NO HTTP SERVICE");
    }
    else
    {
        switch (job.type)
        {
        case JOB_PAIR: DoPair(&job, &link); break;
        case JOB_UPLOAD: DoUpload(&job, &link); break;
        case JOB_UPLOAD_ALL: DoUploadAll(&link); break;
        case JOB_SYNC: DoSync(&link); break;
        case JOB_VERSIONS: DoVersions(&job, &link); break;
        case JOB_USE: DoUse(&job, &link); break;
        case JOB_FLUSH: DoFlush(&link); break;
        case JOB_INDEX: DoIndex(&link); break;
        case JOB_DELETE: DoDelete(&job, &link); break;
        case JOB_TEST: DoTest(&link); break;
        case JOB_NAME: DoName(&link); break;
        case JOB_FILES: DoFiles(&job, &link); break;
        case JOB_FOCUS: DoFocus(&link); break;
        }
        /* A name changed meanwhile (or just linked: the card's name). */
        if (job.type != JOB_NAME && sNamePending && !sNetFailed && link.token[0]) DoName(&link);
        /* After a sync, the Pokédex's counts; and any vote still on the
         * card goes along while the server is there. */
        if (job.type == JOB_SYNC && !sNetFailed) DoIndex(&link);
        if (job.type != JOB_FLUSH && job.type != JOB_PAIR && !sNetFailed && link.token[0] && PendingVotes())
            DoFlush(&link);
        httpcExit();
    }
    if (sPsReady) psExit();
    sPsReady = false;
    Lock();
    if (sStatus == CTR_ONLINE_BUSY)
    {
        sStatus = CTR_ONLINE_ERROR;
        snprintf(sText, sizeof(sText), "FAILED");
        ++sSerial;
        sEndedMs = CtrPlatform_Milliseconds();
    }
    sBusy = false;
    sLastJob = job.type;
    Unlock();
}

/* ── The game's side ────────────────────────────────────────────────── */

/* Starts a job unless one runs or (but for pairing) there is no link. */
static bool Start(const Job *job)
{
    Link link = GetLink();

    Lock();
    if (sBusy)
    {
        Unlock();
        CtrLog_Write(CTR_LOG_FS, "online: job %d refused, one is running", job->type);
        return false;
    }
    Unlock();
    if (link.server[0] == 0)
    {
        SetStatus(CTR_ONLINE_ERROR, "NO SERVER ADDRESS");
        return false;
    }
    if (job->type != JOB_PAIR && job->type != JOB_TEST && link.token[0] == 0)
    {
        SetStatus(CTR_ONLINE_ERROR, "NOT LINKED");
        return false;
    }
    Lock();
    sBusy = true;
    sJob = *job;
    Unlock();
    /* The web's focus is asked for every two seconds: no status for it. */
    if (job->type != JOB_FOCUS) SetStatus(CTR_ONLINE_BUSY, "STARTING...");
    /* The system core: the game's own keeps its frame. */
    if (!CtrPlatform_StartThread(Worker, NULL, WORKER_STACK, 1))
    {
        Lock();
        sBusy = false;
        Unlock();
        SetStatus(CTR_ONLINE_ERROR, "NO THREAD");
        return false;
    }
    return true;
}

bool CtrOnline_Linked(void)
{
    Link link = GetLink();

    return link.token[0] != 0 && link.server[0] != 0;
}

bool CtrOnline_Available(void)
{
    bool down;

    Lock();
    down = sWifiDown && sStatus == CTR_ONLINE_ERROR;
    Unlock();
    return CtrOnline_Linked() && !down;
}

const char *CtrOnline_User(void)
{
    Link link = GetLink();

    snprintf(sUserCopy, sizeof(sUserCopy), "%s", link.token[0] ? link.user : "");
    return sUserCopy;
}

/* The system keyboard once; false when cancelled. */
static bool Keyboard(const char *hint, const char *initial, int maxLength, char *out, unsigned size)
{
    SwkbdState kb;
    SwkbdButton button;

    swkbdInit(&kb, SWKBD_TYPE_QWERTY, 2, maxLength);
    swkbdSetHintText(&kb, hint);
    if (initial && initial[0]) swkbdSetInitialText(&kb, initial);
    swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    swkbdSetButton(&kb, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&kb, SWKBD_BUTTON_RIGHT, "OK", true);
    out[0] = 0;
    button = swkbdInputText(&kb, out, size);
    return button == SWKBD_BUTTON_RIGHT && out[0] != 0;
}

const char *CtrOnline_Server(void)
{
    static char copy[SERVER_MAX];
    Link link = GetLink();

    snprintf(copy, sizeof(copy), "%s", link.server);
    return copy;
}

bool CtrOnline_ServerIsStandard(void)
{
    Link link = GetLink();

    return strcmp(link.server, CTR_ONLINE_SERVER) == 0;
}

/* Another server for this console (the token is kept: a server that moved
 * keeps its links; on a different one LINK CONSOLE again). */
static void SetServer(const char *server)
{
    Link link = GetLink();

    snprintf(link.server, sizeof(link.server), "%s", server);
    for (size_t n = strlen(link.server); n && link.server[n - 1] == '/'; --n) link.server[n - 1] = 0;
    if (!PutLink(&link)) CtrLog_Write(CTR_LOG_ERROR, "online: cannot write %s", LINK_PATH);
    CtrLog_Write(CTR_LOG_FS, "online: server set to %s%s", link.server,
                 strcmp(link.server, CTR_ONLINE_SERVER) ? "" : " (the standard one)");
    SetStatus(CTR_ONLINE_IDLE, "SERVER CHANGED");
}

bool CtrOnline_AskServer(void)
{
    Link link = GetLink();
    char server[SERVER_MAX - 8], full[SERVER_MAX];

    if (!Keyboard("Server address, e.g. poke.cloudnoud.nl", link.server, (int)sizeof(server) - 1, server, sizeof(server)))
        return false;
    /* A bare host name is spoken to over plain HTTP (signed). */
    if (strstr(server, "://") == NULL) snprintf(full, sizeof(full), "http://%s", server);
    else snprintf(full, sizeof(full), "%s", server);
    SetServer(full);
    return true;
}

void CtrOnline_UseStandardServer(void) { SetServer(CTR_ONLINE_SERVER); }

bool CtrOnline_AskName(void)
{
    char name[24];

    if (!Keyboard("Your name, shown with your edits", CtrStereoMon_Author(), 16, name, sizeof(name))) return false;
    CtrStereoMon_SetAuthor(name);
    CtrLog_Write(CTR_LOG_FS, "online: author name %s", CtrStereoMon_Author());
    if (!CtrOnline_Linked())
    {
        SetStatus(CTR_ONLINE_IDLE, "NAME SAVED");
        return true;
    }
    sNamePending = true;
    {
        Job job;

        memset(&job, 0, sizeof(job));
        job.type = JOB_NAME;
        /* Busy: it goes after the job that runs (sNamePending). */
        Start(&job);
    }
    return true;
}

bool CtrOnline_AskCode(char *code, unsigned size)
{
    Link link = GetLink();
    char server[SERVER_MAX - 8];

    if (link.server[0] == 0)
    {
        if (!Keyboard("Server address, e.g. planes.example.com", "http://", (int)sizeof(server) - 1, server, sizeof(server)))
            return false;
        /* A bare host name is spoken to over plain HTTP (signed). */
        if (strstr(server, "://") == NULL) snprintf(link.server, sizeof(link.server), "http://%s", server);
        else snprintf(link.server, sizeof(link.server), "%s", server);
        for (size_t n = strlen(link.server); n && link.server[n - 1] == '/'; --n) link.server[n - 1] = 0;
        if (!PutLink(&link)) CtrLog_Write(CTR_LOG_ERROR, "online: cannot write %s", LINK_PATH);
        CtrLog_Write(CTR_LOG_FS, "online: server set to %s", link.server);
    }
    return Keyboard("The code from the website's Account page", NULL, 9, code, size);
}

bool CtrOnline_Pair(const char *code)
{
    Job job;
    unsigned n = 0;

    memset(&job, 0, sizeof(job));
    job.type = JOB_PAIR;
    /* Letters and digits only (the server drops the rest anyway). */
    for (; *code && n + 1 < sizeof(job.code); ++code)
    {
        char c = *code;

        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) job.code[n++] = c;
    }
    /* How the website lists the console: the author's name (letters,
     * digits, spaces and . - _ only, so nothing needs escaping). */
    snprintf(job.consoleName, sizeof(job.consoleName), "New 3DS %s", CtrStereoMon_Author());
    if (n == 0)
    {
        SetStatus(CTR_ONLINE_ERROR, "NO CODE");
        return false;
    }
    return Start(&job);
}

bool CtrOnline_Upload(uint16_t id)
{
    Job job;

    memset(&job, 0, sizeof(job));
    job.type = JOB_UPLOAD;
    job.id = id;
    return Start(&job);
}

bool CtrOnline_UploadPair(uint16_t id, uint16_t other)
{
    Job job;

    memset(&job, 0, sizeof(job));
    job.type = JOB_UPLOAD;
    job.id = id;
    job.id2 = other;
    return Start(&job);
}

bool CtrOnline_TestServer(void)
{
    Job job;

    memset(&job, 0, sizeof(job));
    job.type = JOB_TEST;
    return Start(&job);
}

unsigned CtrOnline_TestResult(char lines[][24], unsigned max)
{
    unsigned n;

    Lock();
    n = sLastJob == JOB_TEST && !sBusy ? sTestCount : 0;
    for (unsigned i = 0; i < n && i < max; ++i) memcpy(lines[i], sTestLines[i], sizeof(sTestLines[0]));
    Unlock();
    return n < max ? n : max;
}

bool CtrOnline_Delete(uint16_t id, uint32_t version)
{
    Job job;

    memset(&job, 0, sizeof(job));
    job.type = JOB_DELETE;
    job.id = id;
    job.version = version;
    return Start(&job);
}

bool CtrOnline_CurrentAuthor(uint16_t id, char *out, unsigned size)
{
    bool found = false;

    Lock();
    for (unsigned i = 0; i < sIndexCount && !found; ++i)
        if (sIndex[i].id == id)
        {
            snprintf(out, size, "%s", sIndex[i].author);
            found = out[0] != 0;
        }
    Unlock();
    return found;
}

bool CtrOnline_UploadAll(void)
{
    Job job;

    memset(&job, 0, sizeof(job));
    job.type = JOB_UPLOAD_ALL;
    return Start(&job);
}

bool CtrOnline_Sync(void)
{
    Job job;

    memset(&job, 0, sizeof(job));
    job.type = JOB_SYNC;
    return Start(&job);
}

bool CtrOnline_FetchVersions(uint16_t id)
{
    Job job;

    memset(&job, 0, sizeof(job));
    job.type = JOB_VERSIONS;
    job.id = id;
    return Start(&job);
}

bool CtrOnline_FetchFiles(const uint32_t *versions, const uint16_t *ids, unsigned count)
{
    Job job;

    memset(&job, 0, sizeof(job));
    job.type = JOB_FILES;
    Lock();
    for (unsigned i = 0; i < count && job.fileCount < CTR_ONLINE_MAX_FILES; ++i)
    {
        int at = CachedAtLocked(versions[i]);

        if (at >= 0) sFiles[at].used = ++sFilesUse;
        else
        {
            job.files[job.fileCount] = versions[i];
            job.fileIds[job.fileCount++] = ids[i];
        }
    }
    Unlock();
    if (job.fileCount == 0) return true;
    return Start(&job);
}

unsigned CtrOnline_FilesSerial(void)
{
    unsigned serial;

    Lock();
    serial = sFilesSerial;
    Unlock();
    return serial;
}

bool CtrOnline_FileCopy(uint32_t version, uint8_t *out, uint32_t max, uint32_t *size)
{
    int at;
    bool ok = false;

    Lock();
    at = CachedAtLocked(version);
    if (at >= 0 && sFiles[at].size <= max)
    {
        memcpy(out, sFiles[at].data, sFiles[at].size);
        *size = sFiles[at].size;
        ok = true;
    }
    Unlock();
    return ok;
}

bool CtrOnline_HasFile(uint32_t version)
{
    bool have;

    Lock();
    have = CachedAtLocked(version) >= 0;
    Unlock();
    return have;
}

bool CtrOnline_FetchFocus(void)
{
    Job job;

    memset(&job, 0, sizeof(job));
    job.type = JOB_FOCUS;
    return Start(&job);
}

unsigned CtrOnline_Focus(CtrOnlineFocus *out)
{
    unsigned serial;

    Lock();
    *out = sFocus;
    serial = sFocusSerial;
    Unlock();
    return serial;
}

bool CtrOnline_UseVersion(uint16_t id, uint32_t version)
{
    Job job;

    memset(&job, 0, sizeof(job));
    job.type = JOB_USE;
    job.id = id;
    job.version = version;
    return Start(&job);
}

bool CtrOnline_Vote(uint16_t id, const uint32_t *versions, unsigned count)
{
    Job job;
    bool busy;

    /* Kept on the card first, so a vote is never lost; then sent. */
    Lock();
    {
        Fav *fv = FavLocked(id, true);

        if (fv)
        {
            fv->n = (uint8_t)(count < CTR_ONLINE_MAX_VOTES ? count : CTR_ONLINE_MAX_VOTES);
            for (unsigned i = 0; i < fv->n; ++i) fv->v[i] = versions[i];
            fv->pending = true;
        }
        if (sVersions.id == id)
        {
            for (unsigned i = 0; i < sVersions.count; ++i)
            {
                bool on = false;

                for (unsigned k = 0; k < count && k < CTR_ONLINE_MAX_VOTES; ++k) on |= versions[k] == sVersions.v[i].version;
                sVersions.v[i].favourite = on;
            }
            ++sVersionsSerial;
        }
        for (unsigned i = 0; i < sIndexCount; ++i)
            if (sIndex[i].id == id) sIndex[i].mine = (uint8_t)(count < CTR_ONLINE_MAX_VOTES ? count : CTR_ONLINE_MAX_VOTES);
        ++sIndexSerial;
        busy = sBusy;
    }
    Unlock();
    WriteFavs();
    CtrLog_Write(CTR_LOG_FS, "online: favourites of %04x kept (%u)%s", id, count, busy ? ", sent after this job" : "");
    if (busy || !CtrOnline_Linked()) return true;
    memset(&job, 0, sizeof(job));
    job.type = JOB_FLUSH;
    Start(&job);
    return true;
}

unsigned CtrOnline_Favourites(uint16_t id, uint32_t out[CTR_ONLINE_MAX_VOTES], bool *pending)
{
    unsigned n = 0;

    Lock();
    {
        Fav *fv = FavLocked(id, false);

        if (fv)
        {
            n = fv->n;
            memcpy(out, fv->v, n * sizeof(out[0]));
            if (pending) *pending = fv->pending;
        }
        else if (pending) *pending = false;
    }
    Unlock();
    return n;
}

unsigned CtrOnline_PendingVotes(void) { return PendingVotes(); }

bool CtrOnline_SetReview(uint16_t id, unsigned status)
{
    Job job;
    bool busy;

    if (status > CTR_ONLINE_REVIEW_OK) return false;
    /* Kept on the card first, so a mark is never lost; then sent. */
    Lock();
    {
        Mark *m = MarkLocked(id, true);

        if (m)
        {
            m->status = (uint8_t)status;
            m->pending = true;
        }
        ++sMarkSerial;
        busy = sBusy;
    }
    Unlock();
    WriteMarks();
    CtrLog_Write(CTR_LOG_FS, "online: review mark of %04x kept (%s)%s", id, sMarkWords[status],
                 busy ? ", sent after this job" : "");
    if (busy || !CtrOnline_Linked()) return true;
    memset(&job, 0, sizeof(job));
    job.type = JOB_FLUSH;
    Start(&job);
    return true;
}

unsigned CtrOnline_Review(uint16_t id, bool *pending)
{
    unsigned status = CTR_ONLINE_REVIEW_NONE;
    bool waiting = false;

    Lock();
    {
        Mark *m = MarkLocked(id, false);

        if (m)
        {
            status = m->status;
            waiting = m->pending;
        }
    }
    Unlock();
    if (pending) *pending = waiting;
    return status;
}

unsigned CtrOnline_ReviewSerial(void)
{
    unsigned serial;

    Lock();
    serial = sMarkSerial;
    Unlock();
    return serial;
}

bool CtrOnline_FetchIndex(void)
{
    Job job;

    memset(&job, 0, sizeof(job));
    job.type = JOB_INDEX;
    return Start(&job);
}

unsigned CtrOnline_IndexSerial(unsigned *ageMs)
{
    unsigned serial;

    Lock();
    serial = sIndexSerial;
    if (ageMs)
        *ageMs = sIndexAt ? (unsigned)(CtrPlatform_Milliseconds() - sIndexAt) : 0xFFFFFFFFu;
    Unlock();
    return serial;
}

unsigned CtrOnline_VersionCount(uint16_t id, bool *voted)
{
    unsigned count = 0;
    bool mine = false;

    Lock();
    for (unsigned i = 0; i < sIndexCount; ++i)
        if (sIndex[i].id == id)
        {
            count = sIndex[i].count;
            mine = sIndex[i].mine > 0;
            break;
        }
    {
        Fav *fv = FavLocked(id, false);

        if (fv) mine = fv->n > 0;
    }
    Unlock();
    if (voted) *voted = mine;
    return count;
}

uint32_t CtrOnline_CardVersion(uint16_t id)
{
    SentList sent;
    char key[8], card[65];
    uint32_t version = 0;

    CardSha(id, card);
    if (card[0] == 0) return 0;
    KeyOf(id, key, sizeof(key));
    SentLoad(&sent);
    for (unsigned i = 0; i < sent.count; ++i)
        if (strcmp(sent.items[i].key, key) == 0 && strcmp(sent.items[i].sha, card) == 0) version = sent.items[i].version;
    free(sent.items);
    return version;
}

bool CtrOnline_Busy(void)
{
    bool busy;

    Lock();
    busy = sBusy;
    Unlock();
    return busy;
}

unsigned CtrOnline_GetVersions(CtrOnlineVersions *out)
{
    unsigned serial;

    Lock();
    *out = sVersions;
    serial = sVersionsSerial;
    Unlock();
    return serial;
}

unsigned CtrOnline_NewIds(uint16_t *out, unsigned max)
{
    unsigned count;

    Lock();
    ReadNewLocked();
    count = sNewCount;
    for (unsigned i = 0; i < count && i < max; ++i) out[i] = sNew[i];
    Unlock();
    return count;
}

unsigned CtrOnline_NewSerial(void)
{
    unsigned serial;

    Lock();
    serial = sNewSerial;
    Unlock();
    return serial;
}

bool CtrOnline_IsNew(uint16_t id)
{
    bool found = false;

    Lock();
    ReadNewLocked();
    for (unsigned i = 0; i < sNewCount && !found; ++i) found = sNew[i] == id;
    Unlock();
    return found;
}

bool CtrOnline_LastWasSync(void)
{
    bool sync;

    Lock();
    sync = sLastJob == JOB_SYNC;
    Unlock();
    return sync;
}

void CtrOnline_Unlink(void)
{
    Link link = GetLink();
    bool busy;

    Lock();
    busy = sBusy;
    Unlock();
    if (busy)
    {
        SetStatus(CTR_ONLINE_BUSY, "BUSY - TRY AGAIN");
        return;
    }
    CtrLog_Write(CTR_LOG_FS, "online: unlinked from %s (was %s)", link.server, link.user);
    link.token[0] = 0;
    link.user[0] = 0;
    link.seq = 0;
    PutLink(&link);
    /* Everything is new to whichever account links next. */
    remove(SENT_PATH);
    SetStatus(CTR_ONLINE_IDLE, "NOT LINKED");
}

CtrOnlineStatus CtrOnline_Status(char *text, unsigned size)
{
    CtrOnlineStatus status;

    Lock();
    status = sStatus;
    if (text && size) snprintf(text, size, "%s", sText);
    Unlock();
    return status;
}

bool CtrOnline_Recent(unsigned ms)
{
    bool recent;

    Lock();
    recent = sBusy || (sStatus != CTR_ONLINE_IDLE && CtrPlatform_Milliseconds() - sEndedMs < ms);
    Unlock();
    return recent;
}

unsigned CtrOnline_Serial(void)
{
    unsigned serial;

    Lock();
    serial = sSerial;
    Unlock();
    return serial;
}

/*
 * A scripted run, for an emulator or a first hardware check: when
 * sdmc:/3ds/emerald3ds/online_test.txt exists, its lines one after the
 * other, ten seconds after start and each once the job before it ended: "wait <frames>", "pair <code>", "upload <key>",
 * "uploadall", "sync", "unlink", "versions <key>",
 * "use <key> <version>", "vote <key> [<version> [<version>]]", "index", "flush",
 * "count <key>" (what the console knows of a picture, its review mark too), "editor", "dex" (the
 * editor, or straight into its Pokédex), "verscreen" (its voting view),
 * "press <hex CTR_KEY_ bits>", "tap <x> <y>" and "cstick <x>" (keys, a touch there, the C-stick pushed),
 * "topshot" (the top screen to dump.raw). Each step and its result go to port.log.
 */
static void TestFrame(void)
{
    static FILE *script;
    static bool opened;
    static unsigned wait;
    char line[64], text[CTR_ONLINE_TEXT], arg[32];
    bool busy;

    if (!opened)
    {
        opened = true;
        script = fopen(DATA_DIR "/online_test.txt", "r");
        wait = 600;
    }
    if (script == NULL) return;
    Lock();
    busy = sBusy;
    Unlock();
    if (busy || (wait && --wait)) return;
    if (sSerial)
    {
        CtrOnlineStatus status = CtrOnline_Status(text, sizeof(text));

        CtrLog_Write(CTR_LOG_FS, "online test: -> %s \"%s\"", status == CTR_ONLINE_ERROR ? "ERROR" : "ok", text);
        static unsigned logged;
        CtrOnlineVersions list;

        if (CtrOnline_GetVersions(&list) != logged)
        {
            logged = CtrOnline_GetVersions(&list);
            for (unsigned i = 0; i < list.count; ++i)
                CtrLog_Write(CTR_LOG_FS, "online test:    #%lu %s r%u votes %u%s%s%s%s", (unsigned long)list.v[i].version,
                             list.v[i].author, list.v[i].revision, list.v[i].votes, list.v[i].current ? " current" : "",
                             list.v[i].favourite ? " favourite" : "", list.v[i].onCard ? " on-card" : "",
                             list.v[i].final ? " final" : "");
        }
    }
    if (!fgets(line, sizeof(line), script))
    {
        CtrLog_Write(CTR_LOG_FS, "online test: end of script");
        fclose(script);
        script = NULL;
        return;
    }
    line[strcspn(line, "\r\n")] = 0;
    CtrLog_Write(CTR_LOG_FS, "online test: %s", line);
    arg[0] = 0;
    sscanf(line, "%*s %31s", arg);
    if (strncmp(line, "wait", 4) == 0) wait = (unsigned)strtoul(arg, NULL, 10);
    else if (strncmp(line, "pair", 4) == 0) CtrOnline_Pair(arg);
    else if (strncmp(line, "uploadall", 9) == 0) CtrOnline_UploadAll();
    else if (strncmp(line, "upload ", 7) == 0) CtrOnline_Upload(IdOf(arg));
    else if (strncmp(line, "sync", 4) == 0) CtrOnline_Sync();
    else if (strncmp(line, "unlink", 6) == 0) CtrOnline_Unlink();
    else if (strncmp(line, "versions", 8) == 0) CtrOnline_FetchVersions(IdOf(arg));
    else if (strncmp(line, "delete", 6) == 0)
    {
        unsigned long version = 0;

        sscanf(line, "%*s %*s %lu", &version);
        CtrOnline_Delete(IdOf(arg), (uint32_t)version);
    }
    else if (strncmp(line, "uploadpair", 10) == 0)
    {
        char second[16] = "";

        sscanf(line, "%*s %*s %15s", second);
        CtrOnline_UploadPair(IdOf(arg), second[0] ? IdOf(second) : 0);
    }
    else if (strncmp(line, "dex", 3) == 0)
    {
        CtrStereoEdit_PreferDex(false);
        CtrStereoMonGame_OpenEditor();
    }
    else if (strncmp(line, "editor", 6) == 0) CtrStereoMonGame_OpenEditor();
    else if (strncmp(line, "verscreen", 9) == 0) CtrStereoEdit_TestVersions();
    else if (strncmp(line, "stroke", 6) == 0) CtrStereoEdit_TestStroke();
    else if (strncmp(line, "editoptions", 11) == 0) CtrStereoEdit_TestOptions();
    else if (strncmp(line, "optionstab", 10) == 0) CtrBottom_TestOptions((unsigned)strtoul(arg, NULL, 10));
    else if (strncmp(line, "bottomshot", 10) == 0)
    {
        char path[96];

        snprintf(path, sizeof(path), DATA_DIR "/shot-%s.bin", arg);
        CtrBottom_TestShot(path);
    }
    else if (strncmp(line, "topshot", 7) == 0)
    {
        /* The top screen's last frame to dump.raw (read with dump_frames.py). */
        CtrVideo_RequestDump();
        CtrStereoMon_Invalidate();
    }
    else if (strncmp(line, "press", 5) == 0) CtrStereoEdit_TestPress((unsigned)strtoul(arg, NULL, 16));
    else if (strncmp(line, "cstick", 6) == 0) CtrStereoEdit_TestCstick((int)strtol(arg, NULL, 10));
    else if (strncmp(line, "tap", 3) == 0)
    {
        int tx = 0, ty = 0;

        sscanf(line, "%*s %d %d", &tx, &ty);
        CtrStereoEdit_TestTap(tx, ty);
    }
    else if (strncmp(line, "shot", 4) == 0)
    {
        char path[96];

        snprintf(path, sizeof(path), DATA_DIR "/shot-%s.bin", arg);
        CtrStereoEdit_TestShot(path);
    }
    else if (strncmp(line, "testserver", 10) == 0) CtrOnline_TestServer();
    else if (strncmp(line, "index", 5) == 0)
    {
        CtrOnline_FetchIndex();
    }
    else if (strncmp(line, "flush", 5) == 0)
    {
        Job job;

        memset(&job, 0, sizeof(job));
        job.type = JOB_FLUSH;
        Start(&job);
    }
    else if (strncmp(line, "count", 5) == 0)
    {
        bool voted;
        uint32_t fav[CTR_ONLINE_MAX_VOTES] = {0};
        bool pending = false;
        unsigned n = CtrOnline_Favourites(IdOf(arg), fav, &pending);
        unsigned count = CtrOnline_VersionCount(IdOf(arg), &voted);
        bool markPending;
        unsigned mark = CtrOnline_Review(IdOf(arg), &markPending);

        CtrLog_Write(CTR_LOG_FS, "online test:    %s: %u versions%s, votes %u (%lu %lu %lu)%s, card version %lu, mark %s%s",
                     arg, count, voted ? ", voted" : "", n, (unsigned long)fav[0], (unsigned long)fav[1],
                     (unsigned long)fav[2], pending ? " PENDING" : "", (unsigned long)CtrOnline_CardVersion(IdOf(arg)),
                     mark ? sMarkWords[mark] : "none", markPending ? " PENDING" : "");
    }
    else if (strncmp(line, "use", 3) == 0)
    {
        unsigned long version = 0;

        sscanf(line, "%*s %*s %lu", &version);
        CtrOnline_UseVersion(IdOf(arg), (uint32_t)version);
    }
    else if (strncmp(line, "vote", 4) == 0)
    {
        unsigned long a = 0, b = 0, c = 0;
        uint32_t ids[3];
        int n = sscanf(line, "%*s %*s %lu %lu %lu", &a, &b, &c);

        ids[0] = (uint32_t)a;
        ids[1] = (uint32_t)b;
        ids[2] = (uint32_t)c;
        CtrOnline_Vote(IdOf(arg), ids, n > 0 ? (unsigned)n : 0);
    }
    if (wait == 0) wait = 1;
}

void CtrOnline_Frame(void)
{
    bool busy, written, shown;

    if (!sLockReady)
    {
        Lock();
        Unlock();
    }
    TestFrame();
    Lock();
    busy = sBusy;
    written = sEditsWritten;
    sEditsWritten = false;
    shown = sShownWritten;
    sShownWritten = false;
    Unlock();
    /* Awake while a transfer runs: sleep would drop Wi-Fi under it. */
    if (busy != sSleepHeld)
    {
        aptSetSleepAllowed(!busy);
        sSleepHeld = busy;
    }
    /* Votes left on the card by a missing connection: tried again now and then. */
    {
        static uint64_t lastTry;
        uint64_t now = CtrPlatform_Milliseconds();

        if (!busy && now - lastTry > FLUSH_RETRY_MS)
        {
            lastTry = now;
            if (PendingVotes() && CtrOnline_Linked())
            {
                Job job;

                memset(&job, 0, sizeof(job));
                job.type = JOB_FLUSH;
                Start(&job);
            }
        }
    }
    if (written) CtrStereoMon_EditsChanged();
    /* A version picked on the console: the picture on show (the editor's
     * too) is read from the card again. */
    if (shown) CtrStereoMon_ReloadShown();
}
