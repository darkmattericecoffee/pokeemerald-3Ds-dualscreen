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
 * (server=, else the standard one, http://poke.cloudnoud.nl, or what
 * ONLINE_SERVER= compiles in; plain HTTP with every request and answer
 * signed, docs section 7.6), the device token
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
/* The server: http://poke.cloudnoud.nl unless online.txt names another
 * (server=); AskServer types another with the system keyboard, and
 * UseStandardServer goes back. The token is kept either way. */
const char *CtrOnline_Server(void);
bool CtrOnline_ServerIsStandard(void);
bool CtrOnline_AskServer(void);
void CtrOnline_UseStandardServer(void);
/* The system keyboard: the server's address when none is known yet, then
 * the pairing code; false when cancelled (or not on this build). */
bool CtrOnline_AskCode(char *code, unsigned size);
/* The jobs (results through CtrOnline_Status and port.log); false when one
 * is running already or the console is not linked (the status says which). */
bool CtrOnline_Pair(const char *code);
bool CtrOnline_Upload(uint16_t id);
/* A picture and its other side (a front's back) up in one job; `other` 0
 * for none. Delete takes this account's own version off the server (the
 * card keeps its file). */
bool CtrOnline_UploadPair(uint16_t id, uint16_t other);
bool CtrOnline_Delete(uint16_t id, uint32_t version);
bool CtrOnline_UploadAll(void);
bool CtrOnline_Sync(void);
/*
 * A picture's versions on the server, newest first: who uploaded each, its
 * votes, whether it is current, one of this account's (at most two)
 * favourites, the one on the card. Fetched by FetchVersions; UseVersion puts
 * one on the card (the card's own kept in stereo/backup unless the server
 * has it); Vote sends the favourites (they decide the current one).
 */
#define CTR_ONLINE_MAX_VERSIONS 16
typedef struct
{
    uint32_t version;
    char author[17];
    uint16_t revision, votes;
    bool final, current, favourite, onCard, mine;
} CtrOnlineVersion;
typedef struct
{
    uint16_t id;
    uint8_t count, maxFavourites;
    bool pinned;
    CtrOnlineVersion v[CTR_ONLINE_MAX_VERSIONS];
} CtrOnlineVersions;
bool CtrOnline_FetchVersions(uint16_t id);
bool CtrOnline_UseVersion(uint16_t id, uint32_t version);
/* A picture's favourites (0 to 2 version numbers): kept on the card at once
 * (online_votes.txt) and sent when the server can be reached, now or after
 * the next job that reaches it, else tried again every minute; the status
 * reads SENDING VOTE... and VOTE SENT, or SAVED, SENDS LATER. Favourites
 * gives what the console knows (pending: not sent yet); PendingVotes how
 * many pictures wait. */
bool CtrOnline_Vote(uint16_t id, const uint32_t *versions, unsigned count);
unsigned CtrOnline_Favourites(uint16_t id, uint32_t out[2], bool *pending);
unsigned CtrOnline_PendingVotes(void);
/* Per picture: how many versions the server has and whether this account
 * voted for one (the Pokédex's marks), from FetchIndex (a sync fetches it
 * too); IndexSerial moves on with each, ageMs how old it is. */
bool CtrOnline_FetchIndex(void);
unsigned CtrOnline_IndexSerial(unsigned *ageMs);
unsigned CtrOnline_VersionCount(uint16_t id, bool *voted);
/* Who uploaded the picture's current version (from the same list). */
bool CtrOnline_CurrentAuthor(uint16_t id, char *out, unsigned size);
/* The server's number of the version on the card (0: none, or the card's
 * own edit the server does not have). Reads the card: not every frame. */
uint32_t CtrOnline_CardVersion(uint16_t id);
bool CtrOnline_Busy(void);
/* TEST SERVER: the server in use checked as tools/tlsprobe does - the HTTPS
 * handshake with the pinned root (and, when it fails, again without the
 * certificate check, to tell the two apart), GET /api/health, and the
 * token with GET /api/me. TestResult gives its lines ("TLS OK",
 * "TLS FAIL D8A0A03C", "HEALTH OK API 1", "LINKED AS NAME"...) once
 * it is the last job and done; each goes to port.log too. */
bool CtrOnline_TestServer(void);
unsigned CtrOnline_TestResult(char lines[][24], unsigned max);
/* The last list fetched (count 0 before one); returns a serial that moves
 * on with every new list. */
unsigned CtrOnline_GetVersions(CtrOnlineVersions *out);
/*
 * The pictures the last sync that brought any put on the card (kept in
 * online_new.txt across starts): up to `max` ids into `out`, the total
 * returned. NewSerial moves on when a sync brings new ones (to open them).
 */
unsigned CtrOnline_NewIds(uint16_t *out, unsigned max);
unsigned CtrOnline_NewSerial(void);
bool CtrOnline_IsNew(uint16_t id);
/* Whether the last job that ended was a sync. */
bool CtrOnline_LastWasSync(void);
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
