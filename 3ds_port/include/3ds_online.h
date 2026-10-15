#ifndef CTR_ONLINE_H
#define CTR_ONLINE_H

/*
 * Sharing the plane editor's edit files over Wi-Fi (3ds_online.c,
 * docs/ONLINE_SPRITE_SHARING.md section 7): a console linked once with a
 * code from the website uploads its edits to the server
 * (tools/sprite_server) and syncs everybody's current versions down.
 *
 * Every request runs on a worker thread, one job at a time; the game never
 * waits. A job started while another runs is refused (its status says so).
 * The link is kept in sdmc:/3ds/emerald3ds/online.txt: the server's address
 * (server=, else the one compiled in with ONLINE_SERVER=), the device token
 * (token=), the account (user=), the last sync (seq=) and verify=0 to turn
 * certificate checks off while debugging.
 *
 * SDK-free: game translation units include it.
 */
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    CTR_ONLINE_IDLE,  /* nothing asked yet */
    CTR_ONLINE_BUSY,  /* a job is running */
    CTR_ONLINE_DONE,  /* the last job worked */
    CTR_ONLINE_ERROR, /* the last job failed */
} CtrOnlineStatus;

/* The status text is short enough for an OPTIONS cell and in the game
 * font's characters: capitals, digits, spaces and . : / - only. */
#define CTR_ONLINE_TEXT 20

/* A token on the card (linked), and no known Wi-Fi trouble. */
bool CtrOnline_Linked(void);
bool CtrOnline_Available(void);
/* The account the console is linked to ("" when not linked). */
const char *CtrOnline_User(void);
/* The system keyboard: the server's address when none is known yet, then
 * the pairing code; false when cancelled (or not on this build). */
bool CtrOnline_AskCode(char *code, unsigned size);
/* The jobs (results through CtrOnline_Status and port.log); false when one
 * is running already or the console is not linked (the status says which). */
bool CtrOnline_Pair(const char *code);
bool CtrOnline_Upload(uint16_t id);
bool CtrOnline_UploadAll(void);
bool CtrOnline_Sync(void);
/* Forgets the token (the website's Account page revokes it for good). */
void CtrOnline_Unlink(void);
CtrOnlineStatus CtrOnline_Status(char *text, unsigned size);
/* Whether a job runs or ended in the last `ms` milliseconds (for a status
 * line that goes away by itself). */
bool CtrOnline_Recent(unsigned ms);
/* Moves on whenever the status changes. */
unsigned CtrOnline_Serial(void);
/* Once a frame on the main thread: keeps the console awake during a job
 * and, after a sync wrote files, rescans the edits (CtrStereoMon_EditsChanged). */
void CtrOnline_Frame(void);

#endif
