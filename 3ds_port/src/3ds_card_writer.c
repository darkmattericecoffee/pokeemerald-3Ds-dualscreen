/*
 * Queued whole-file writes to the SD card (3ds_card_writer.h).
 *
 * A write on the game thread stalls the frame for as long as the card
 * takes (a create, a write, a remove and a rename each cost milliseconds
 * on hardware, more in a folder of hundreds of files, more again while a
 * transfer keeps the system core busy). Here the game thread only copies
 * the bytes and goes on; a thread of its own writes them in order. The
 * thread runs on any core but the game's (-2), a notch below the game's
 * priority, as the settings' saver does.
 */
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "3ds_card_writer.h"
#include "3ds_log.h"

#define QUEUE_MAX 32
#define QUEUE_BYTES (512 * 1024)
#define STACK (16 * 1024)

typedef struct Item Item;
struct Item
{
    Item *next;
    char path[96];
    uint8_t *data;
    uint32_t size;
    bool append;
};

static LightLock sLock;
static LightEvent sWake, sIdle;
static Thread sThread;
static Item *sHead, *sTail;
static unsigned sCount, sBytes, sFailed;
static bool sReady, sNoThread;

static bool WriteNow(const char *path, const uint8_t *data, uint32_t size, bool append)
{
    char tmp[112];
    FILE *f;
    bool ok;

    if (append)
    {
        f = fopen(path, "ab");
        if (f == NULL) return false;
        ok = fwrite(data, 1, size, f) == size;
        return fclose(f) == 0 && ok;
    }
    /* Written beside the file and renamed over it: a failed write never
     * leaves half a file. */
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

static void Worker(void *arg)
{
    (void)arg;
    for (;;)
    {
        Item *item;

        LightLock_Lock(&sLock);
        item = sHead;
        if (item)
        {
            sHead = item->next;
            if (sHead == NULL) sTail = NULL;
        }
        LightLock_Unlock(&sLock);
        if (item == NULL)
        {
            LightEvent_Wait(&sWake);
            continue;
        }
        if (!WriteNow(item->path, item->data, item->size, item->append))
        {
            CtrLog_Write(CTR_LOG_ERROR, "card: cannot write %s (%lu bytes)", item->path, (unsigned long)item->size);
            ++sFailed;
        }
        LightLock_Lock(&sLock);
        --sCount;
        sBytes -= item->size;
        if (sCount == 0) LightEvent_Signal(&sIdle);
        LightLock_Unlock(&sLock);
        free(item->data);
        free(item);
    }
}

static void Ready(void)
{
    if (sReady) return;
    sReady = true;
    LightLock_Init(&sLock);
    LightEvent_Init(&sWake, RESET_ONESHOT);
    LightEvent_Init(&sIdle, RESET_STICKY);
    LightEvent_Signal(&sIdle);
}

static bool StartThread(void)
{
    s32 priority = 0x30;

    if (sThread != NULL) return true;
    if (sNoThread) return false;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    priority = priority < 0x3F ? priority + 1 : 0x3F;
    sThread = threadCreate(Worker, NULL, STACK, priority, -2, false);
    if (sThread == NULL)
    {
        sNoThread = true;
        CtrLog_Write(CTR_LOG_ERROR, "card: no writer thread: files are written on the game thread");
    }
    return sThread != NULL;
}

bool CtrCardWriter_Queue(const char *path, const void *data, uint32_t size, bool append)
{
    Item *item;

    Ready();
    if (!StartThread() || strlen(path) >= sizeof(item->path)) return WriteNow(path, data, size, append);
    item = malloc(sizeof(*item));
    if (item == NULL) return WriteNow(path, data, size, append);
    item->data = malloc(size ? size : 1);
    if (item->data == NULL)
    {
        free(item);
        return WriteNow(path, data, size, append);
    }
    memcpy(item->data, data, size);
    snprintf(item->path, sizeof(item->path), "%s", path);
    item->size = size;
    item->append = append;
    item->next = NULL;
    LightLock_Lock(&sLock);
    if (sCount >= QUEUE_MAX || sBytes + size > QUEUE_BYTES)
    {
        LightLock_Unlock(&sLock);
        free(item->data);
        free(item);
        /* Full: this one waits for the queue, then goes the slow way. */
        CtrCardWriter_Flush();
        return WriteNow(path, data, size, append);
    }
    if (sTail) sTail->next = item;
    else sHead = item;
    sTail = item;
    ++sCount;
    sBytes += size;
    LightEvent_Clear(&sIdle);
    LightLock_Unlock(&sLock);
    LightEvent_Signal(&sWake);
    return true;
}

void CtrCardWriter_Flush(void)
{
    if (!sReady || sThread == NULL) return;
    LightEvent_Wait(&sIdle);
}

bool CtrCardWriter_Pending(void)
{
    bool pending;

    if (!sReady) return false;
    LightLock_Lock(&sLock);
    pending = sCount != 0;
    LightLock_Unlock(&sLock);
    return pending;
}
