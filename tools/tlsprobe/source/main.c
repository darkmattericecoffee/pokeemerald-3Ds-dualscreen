/*
 * tlsprobe: does this 3DS's http:C module reach a given HTTPS server, and
 * under which certificate-verification setting?  Throwaway hardware probe for
 * docs/ONLINE_SPRITE_SHARING.md §3.
 *
 * Input, on the SD card (all optional):
 *   sdmc:/3ds/tlsprobe/urls.txt   one URL per line (# comments); default below
 *   sdmc:/3ds/tlsprobe/root.der   an extra root certificate (DER) to trust
 * Output:
 *   sdmc:/3ds/tlsprobe/result.txt  everything shown on screen, appended
 *
 * For each URL it tries: GET with the module's default verification, GET with
 * ISRG Root X1 (Let's Encrypt, from RomFS) added, GET with root.der added,
 * GET with verification disabled, and the same URL over plain http://.
 * The response status and up to 120 bytes of body are logged for each.
 */

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>

#define OUT_DIR "sdmc:/3ds/tlsprobe"
#define MAX_URLS 16

static FILE *sLog;

static void Log(const char *fmt, ...)
{
    char line[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    printf("%s\n", line);
    if (sLog != NULL)
    {
        fprintf(sLog, "%s\n", line);
        fflush(sLog);
    }
    gfxFlushBuffers();
    gfxSwapBuffers();
}

static u8 *ReadFile(const char *path, u32 *size)
{
    FILE *f = fopen(path, "rb");
    u8 *buf;
    long n;

    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 64 * 1024) { fclose(f); return NULL; }
    buf = malloc((size_t)n);
    if (buf != NULL && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    *size = (u32)n;
    return buf;
}

typedef enum { V_DEFAULT, V_ISRG, V_SDROOT, V_DISABLE, V_PLAIN } Verify;
static const char *const sVerifyName[] = {"default verify", "+ISRG Root X1", "+sd root.der", "verify OFF", "plain http"};

static u8 *sIsrg, *sSdRoot;
static u32 sIsrgSize, sSdRootSize;

/* One attempt; logs what happened. */
static void Probe(const char *url, Verify v)
{
    httpcContext ctx;
    Result r;
    u32 status = 0, got = 0;
    u8 body[121];
    char plain[256];
    u64 t0 = osGetTime();

    if (v == V_PLAIN)
    {
        if (strncmp(url, "https://", 8) != 0) return;
        snprintf(plain, sizeof(plain), "http://%s", url + 8);
        url = plain;
    }
    if (v == V_ISRG && sIsrg == NULL) return;
    if (v == V_SDROOT && sSdRoot == NULL) return;

    r = httpcOpenContext(&ctx, HTTPC_METHOD_GET, url, 0);
    if (R_FAILED(r)) { Log("  %-15s open      FAILED %08lx", sVerifyName[v], r); return; }

    switch (v)
    {
    case V_ISRG: r = httpcAddTrustedRootCA(&ctx, sIsrg, sIsrgSize); break;
    case V_SDROOT: r = httpcAddTrustedRootCA(&ctx, sSdRoot, sSdRootSize); break;
    case V_DISABLE: r = httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify); break;
    default: break;
    }
    if (R_FAILED(r)) { Log("  %-15s cert/opt  FAILED %08lx", sVerifyName[v], r); httpcCloseContext(&ctx); return; }

    httpcAddRequestHeaderField(&ctx, "User-Agent", "emerald3ds-tlsprobe/1.0");
    httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_DISABLED);

    r = httpcBeginRequest(&ctx);
    if (R_FAILED(r))
    {
        Log("  %-15s begin     FAILED %08lx %lums", sVerifyName[v], r, (u32)(osGetTime() - t0));
        httpcCloseContext(&ctx);
        return;
    }
    r = httpcGetResponseStatusCodeTimeout(&ctx, &status, 15000000000ULL);
    if (R_FAILED(r))
    {
        Log("  %-15s status    FAILED %08lx %lums", sVerifyName[v], r, (u32)(osGetTime() - t0));
        httpcCloseContext(&ctx);
        return;
    }
    memset(body, 0, sizeof(body));
    r = httpcDownloadData(&ctx, body, sizeof(body) - 1, &got);
    for (u32 i = 0; i < got; i++)
        if (body[i] < 32 || body[i] > 126) body[i] = '.';
    Log("  %-15s OK status %lu, %lu bytes, %lums: %s", sVerifyName[v], status, got, (u32)(osGetTime() - t0), body);
    httpcCloseContext(&ctx);
}

int main(void)
{
    char *urls[MAX_URLS];
    unsigned nUrls = 0;
    u8 *list;
    u32 listSize;
    u32 wifi = 0;
    Result r;

    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);
    romfsInit();
    mkdir("sdmc:/3ds", 0777);
    mkdir(OUT_DIR, 0777);
    sLog = fopen(OUT_DIR "/result.txt", "a");

    Log("== tlsprobe ==");
    {
        u8 model = 0;
        char ver[32] = "?";
        CFGU_GetSystemModel(&model);
        osGetSystemVersionDataString(NULL, NULL, ver, sizeof(ver));
        Log("model %u, system %s", model, ver);
    }

    acInit();
    r = ACU_GetWifiStatus(&wifi);
    Log("wifi status %lu (res %08lx)", wifi, r);
    if (wifi == 0) Log("no Wi-Fi connection: results below will all fail");

    list = ReadFile(OUT_DIR "/urls.txt", &listSize);
    if (list != NULL)
    {
        char *save = NULL, *tok;
        char *text = malloc(listSize + 1);
        memcpy(text, list, listSize);
        text[listSize] = 0;
        for (tok = strtok_r(text, "\r\n", &save); tok != NULL && nUrls < MAX_URLS; tok = strtok_r(NULL, "\r\n", &save))
        {
            while (*tok == ' ') tok++;
            if (*tok == 0 || *tok == '#') continue;
            urls[nUrls++] = strdup(tok);
        }
        free(text);
        free(list);
        Log("%u URLs from urls.txt", nUrls);
    }
    if (nUrls == 0)
    {
        urls[nUrls++] = strdup("https://letsencrypt.org/");
        urls[nUrls++] = strdup("https://www.digicert.com/");
        urls[nUrls++] = strdup("https://httpbin.org/get");
        Log("no urls.txt: using %u built-in URLs", nUrls);
    }

    sIsrg = ReadFile("romfs:/isrgrootx1.der", &sIsrgSize);
    Log("ISRG Root X1: %s (%lu bytes)", sIsrg ? "loaded" : "MISSING", sIsrgSize);
    sSdRoot = ReadFile(OUT_DIR "/root.der", &sSdRootSize);
    Log("sd root.der: %s (%lu bytes)", sSdRoot ? "loaded" : "none", sSdRoot ? sSdRootSize : 0);

    r = httpcInit(0);
    Log("httpcInit %08lx", r);

    for (unsigned i = 0; i < nUrls; i++)
    {
        Log("%s", urls[i]);
        for (Verify v = V_DEFAULT; v <= V_PLAIN; v++) Probe(urls[i], v);
    }
    Log("== done: START exits ==");

    httpcExit();
    acExit();
    while (aptMainLoop())
    {
        gspWaitForVBlank();
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
    }
    if (sLog != NULL) fclose(sLog);
    romfsExit();
    gfxExit();
    return 0;
}
