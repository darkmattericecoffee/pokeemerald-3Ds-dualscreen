/*
 * The optional settings of the ENHANCEMENTS and CHEATS pages of OPTIONS
 * (3ds_extras.h). Each feature adds its line to gCtrExtras and, for the game
 * code that reads it, a function below.
 */
#include <stdio.h>
#include <string.h>
#include "global.h"
#include "3ds_locale.h"
#include "3ds_extras.h"
#include "3ds_locale_text.h"
#include "3ds_online.h"

/* This build's third tab is ONLINE (CTR_EXTRAS_ONLINE); the cheats it held
 * are left out. 1 brings them back (on a tab of their own only if
 * CTR_EXTRAS_ONLINE is given back its own number). */
#ifndef CTR_CHEATS_MENU
#define CTR_CHEATS_MENU 0
#endif
#include "3ds_randomizer.h"
#include "3ds_nuzlocke_ui.h"
#include "3ds_platform.h"
#include "3ds_daynight.h"
#include "event_data.h"
#include "pokedex.h"
#include "pokemon.h"
#include "constants/pokedex.h"
#include "sound.h"
#include "characters.h"
#include "item.h"
#include "constants/item.h"
#include "constants/items.h"
#include "constants/songs.h"
#include "3ds_stereo_mon.h"

const char *const gCtrExtrasOffOn[2] = {CTR_TEXT("OFF", "NO", "NON", "NO", "NENHUM", "AUS", "オフ"), CTR_TEXT("ON", "SÍ", "OUI", "SÌ", "SIM", "AN", "オン")};

static const char *const sDayNight[] = {CTR_TEXT("OFF", "NO", "NON", "NO", "NENHUM", "AUS", "オフ"), "AUTO"};
static const char *const sClockSpeeds[] = {"1X", "6X", "12X", "24X", "60X", "120X"};
/* SYNC CLOCK: the local clock and 1X again, bonus berry time kept; on disk
 * with the next in-game save, like any other progress. */
static const char *const sClockSync[] = {CTR_TEXT("SAVE TO KEEP", "GUARDA PARA FIJAR", "SAUVER P. GARDER", "SALVA PER TENERE", "SALVE P/ MANTER", "SPEICHERN NÖTIG", "セーブで ほぞん")};

static void SyncClock(void)
{
    PlaySE(CtrDayNight_SyncToConsole() ? SE_SUCCESS : SE_FAILURE);
}

static const char *const sTimedEncounters[] = {CTR_TEXT("ORIGINAL", "ORIGINAL", "ORIGINAL", "ORIGINALE", "ORIGINAL", "ORIGINAL", "オリジナル"),
                                               CTR_TEXT("TIMED", "POR HORARIO", "PAR HORAIRE", "PER ORARIO", "POR HORÁRIO", "NACH UHRZEIT", "じかんたい")};

static const char *const sVisibleWild[] = {CTR_TEXT("OFF", "NO", "NON", "NO", "NENHUM", "AUS", "オフ"), CTR_TEXT("FEW", "POCOS", "PEU", "POCHE", "POUCOS", "WENIGE", "すこし"), CTR_TEXT("SOME", "ALGUNOS", "MOYEN", "ALCUNE", "ALGUNS", "EINIGE", "ふつう"), CTR_TEXT("MANY", "MUCHOS", "BEAUCOUP", "MOLTE", "MUITOS", "VIELE", "たくさん")};
#if CTR_CHEATS_MENU
static const char *const sEncounterRate[] = {CTR_TEXT("NORMAL", "NORMAL", "NORMAL", "NORMALE", "NORMAL", "NORMAL", "ふつう"), CTR_TEXT("OFF", "NO", "NON", "NO", "NENHUM", "AUS", "オフ"), "1/4", "1/2", "2X"};
static const char *const sShinyOdds[] = {"1/8192", "1/4096", "1/1024", "1/256", "1/64", CTR_TEXT("ALWAYS", "SIEMPRE", "TOUJOURS", "SEMPRE", "SEMPRE", "IMMER", "かならず")};
#endif
/* The voxel world per eye with the 3D slider (3ds_video.c): AUTO is on a
 * New 3DS only (the second pass costs an Old 3DS the frame), and how deep
 * the slider's full travel goes, in percent of the renderer's depth. Read
 * only with the voxel overworld on: a build without it has no cells for
 * them, and with it off they are greyed (CtrExtras_NeedsVoxel). */
static const char *const sVoxelStereo[] = {CTR_TEXT("AUTO", "AUTO", "AUTO", "AUTO", "AUTO", "AUTO", "オート"), CTR_TEXT("ON", "SÍ", "OUI", "SÌ", "SIM", "AN", "オン"), CTR_TEXT("OFF", "NO", "NON", "NO", "NENHUM", "AUS", "オフ")};
static const char *const sVoxelDepths[] = {"25%", "50%", "75%", "100%", "125%", "150%", "200%"};
/* The 3D depths as a whole (the scenery, the two sides, the planes) that
 * the battle, the editor, the Pokédex and the voting view all take: one of
 * four presets (3ds_stereo_mon.h CtrStereoDepths, numbered as listed here). */
static const char *const sDepthPresets[] = {CTR_TEXT("GENTLE", "SUAVE", "DOUX", "LEGGERO", "GENTLE", "GENTLE", "GENTLE"), CTR_TEXT("STANDARD", "ESTÁNDAR", "STANDARD", "STANDARD", "STANDARD", "STANDARD", "STANDARD"), CTR_TEXT("CLOSE", "CERCA", "PROCHE", "VICINO", "CLOSE", "CLOSE", "CLOSE"), CTR_TEXT("DEEP", "PROFUNDO", "PROFOND", "PROFONDO", "DEEP", "DEEP", "DEEP")};
/* The plane editor's tune (3ds_stereo_mon_game.c EditorMusic): the Pokémon
 * Center's as before, the place's own left playing, none, or a town's or
 * route's. A tester missed drawing to Dewford's. sEditorSongs runs with
 * sEditorMusic, one id a name. */
static const char *const sEditorMusic[] = {
    "POKE CENTER", "MAP'S OWN", "OFF", "DEWFORD", "LITTLEROOT", "PETALBURG", "OLDALE", "SLATEPORT", "RUSTBORO",
    "VERDANTURF", "FALLARBOR", "FORTREE", "LILYCOVE", "SOOTOPOLIS", "EVER GRANDE", "ROUTE 101", "ROUTE 104",
    "ROUTE 110", "ROUTE 113", "ROUTE 119", "ROUTE 120", "ROUTE 122", "SURF", "CYCLING", "PETALBURG WOODS",
    "ABANDONED SHIP", "CAVE OF ORIGIN", "MT PYRE", "SEALED CHAMBER", "SAFARI ZONE", "UNDERWATER", "SCHOOL",
    "GAME CORNER", "CONTEST LOBBY", "MT CHIMNEY", "CABLE CAR", "PALLET", "PEWTER", "CELADON", "VIRIDIAN FOREST",
    "SEVII ROUTE", "KANTO CYCLING",
};
static const u16 sEditorSongs[] = {
    MUS_POKE_CENTER, CTR_EDITOR_SONG_MAP, CTR_EDITOR_SONG_OFF, MUS_DEWFORD, MUS_LITTLEROOT, MUS_PETALBURG, MUS_OLDALE,
    MUS_SLATEPORT, MUS_RUSTBORO, MUS_VERDANTURF, MUS_FALLARBOR, MUS_FORTREE, MUS_LILYCOVE, MUS_SOOTOPOLIS,
    MUS_EVER_GRANDE, MUS_ROUTE101, MUS_ROUTE104, MUS_ROUTE110, MUS_ROUTE113, MUS_ROUTE119, MUS_ROUTE120,
    MUS_ROUTE122, MUS_SURF, MUS_CYCLING, MUS_PETALBURG_WOODS, MUS_ABANDONED_SHIP, MUS_CAVE_OF_ORIGIN, MUS_MT_PYRE,
    MUS_SEALED_CHAMBER, MUS_SAFARI_ZONE, MUS_UNDERWATER, MUS_SCHOOL, MUS_GAME_CORNER, MUS_CONTEST_LOBBY,
    MUS_MT_CHIMNEY, MUS_CABLE_CAR, MUS_RG_PALLET, MUS_RG_PEWTER, MUS_RG_CELADON, MUS_RG_VIRIDIAN_FOREST,
    MUS_RG_SEVII_ROUTE, MUS_RG_CYCLING,
};
#define EDITOR_SONGS ((int)ARRAY_COUNT(sEditorSongs))

u16 CtrExtras_EditorSong(void)
{
    int choice = CtrExtras_Get(CTR_EDITOR_MUSIC_KEY);

    return sEditorSongs[choice >= 0 && choice < EDITOR_SONGS ? choice : 0];
}

const char *CtrExtras_EditorSongName(void)
{
    int choice = CtrExtras_Get(CTR_EDITOR_MUSIC_KEY);

    return sEditorMusic[choice >= 0 && choice < EDITOR_SONGS ? choice : 0];
}

/* The stereoscopic battle's lines are shown only when the build carries
 * planes (romfs/stereo/mons.bin or backs.bin, 3ds_stereo_mon.c): without
 * them the settings would do nothing. */
static bool PlanesShown(void)
{
    return CtrStereoMon_Load() || CtrStereoMon_BacksHeader() != NULL;
}

#if CTR_CHEATS_MENU
/*
 * Pokedex (CHEATS): the Hoenn Pokedex complete, the National Pokedex
 * unlocked, or complete (and unlocked): every Pokemon seen and caught. OPTIONS
 * is only open in the field; the player saves to keep it.
 */
static void SetDexSeenCaught(u16 national)
{
    GetSetPokedexFlag(national, FLAG_SET_SEEN);
    GetSetPokedexFlag(national, FLAG_SET_CAUGHT);
}

static void CompleteHoennDex(void)
{
    for (u16 hoenn = 1; hoenn <= HOENN_DEX_COUNT; hoenn++)
        SetDexSeenCaught(HoennToNationalOrder(hoenn));
    PlaySE(SE_SUCCESS);
}

static void UnlockNationalDex(void)
{
    EnableNationalPokedex();
    PlaySE(SE_SUCCESS);
}

static void CompleteNationalDex(void)
{
    EnableNationalPokedex();
    for (u16 national = 1; national <= NATIONAL_DEX_COUNT; national++)
        SetDexSeenCaught(national);
    PlaySE(SE_SUCCESS);
}

static const char *const sDexDone[] = {CTR_TEXT("TAP TO SET", "TOCA PARA ACTIVAR", "ACTIVER", "ATTIVA", "TOQUE P/ ATIVAR", "ANTIPPEN", "タッチで せってい")};
/*
 * Give items (CHEATS): a pocket, an item of it, a count, and GIVE puts them in
 * the bag (OPTIONS is only open in the field). The item steps through the
 * pocket's items in the game's order, skipping unused ids.
 */
static const char *const sGivePockets[] = {CTR_TEXT("ITEMS", "OBJETOS", "OBJETS", "OGGETTI", "ITENS", "ITEMS", "どうぐ"), CTR_TEXT("POKE BALLS", "POKé BALLS", "POKÉ BALLS", "POKé BALL", "POKé BOLAS", "POKé BÄLLE", "ボール"), CTR_TEXT("TMS & HMS", "MT Y MO", "CT ET CS", "MT E MN", "MTs E MOs", "TM & VM", "わざマシン"), CTR_TEXT("BERRIES", "BAYAS", "BAIES", "BACCHE", "FRUTAS", "BEEREN", "きのみ"), CTR_TEXT("KEY ITEMS", "OBJ. CLAVE", "OBJ. RARES", "STRUM. BASE", "ITENS CHAVE", "BASIS-ITEMS", "たいせつなもの")};
static const char *const sGiveCounts[] = {"1", "5", "10", "50", "99"};
static const u8 sGiveCountValues[] = {1, 5, 10, 50, 99};

static bool8 GiveItemUsable(u16 item, u8 pocket)
{
    u8 name[ITEM_NAME_LENGTH + 1];

    if (item == ITEM_NONE || item >= ITEMS_COUNT || GetPocketByItemId(item) != pocket)
        return FALSE;
    CopyItemName(item, name);
    return name[0] != CHAR_QUESTION_MARK && name[0] != EOS;
}

static u8 GivePocket(void)
{
    int pocket = CtrSettings_GetInt("give_pocket", 0);

    return POCKET_ITEMS + (pocket >= 0 && pocket < (int)ARRAY_COUNT(sGivePockets) ? pocket : 0);
}

/* The chosen item, or the pocket's first when that is not one of it. */
static u16 GiveItem(void)
{
    u16 item = CtrSettings_GetInt("give_item", ITEM_NONE);
    u8 pocket = GivePocket();

    if (GiveItemUsable(item, pocket))
        return item;
    for (item = 1; item < ITEMS_COUNT; item++)
        if (GiveItemUsable(item, pocket))
            return item;
    return ITEM_NONE;
}

static void GiveItemStep(int direction)
{
    u16 item = GiveItem();
    u8 pocket = GivePocket();

    if (item == ITEM_NONE)
        return;
    for (u16 tries = 0; tries < ITEMS_COUNT; tries++)
    {
        item = direction < 0 ? (item <= 1 ? ITEMS_COUNT - 1 : item - 1) : (item + 1 >= ITEMS_COUNT ? 1 : item + 1);
        if (GiveItemUsable(item, pocket))
            break;
    }
    CtrSettings_SetInt("give_item", item);
}

static const u8 *GiveItemText(void)
{
    static u8 name[ITEM_NAME_LENGTH + 1];
    u16 item = GiveItem();

    if (item == ITEM_NONE)
        name[0] = EOS;
    else
        CopyItemName(item, name);
    return name;
}

static void GiveItemsNow(void)
{
    u16 item = GiveItem();
    int count = CtrSettings_GetInt("give_count", 0);
    u16 quantity = sGiveCountValues[count >= 0 && count < (int)ARRAY_COUNT(sGiveCountValues) ? count : 0];

    /* Key items come one at a time. */
    if (GetPocketByItemId(item) == POCKET_KEY_ITEMS)
        quantity = 1;
    if (item != ITEM_NONE && CheckBagHasSpace(item, quantity) && AddBagItem(item, quantity))
        PlaySE(SE_SUCCESS);
    else
        PlaySE(SE_FAILURE);
}

static const char *const sGiveText[] = {CTR_TEXT("TAP TO GIVE", "TOCA PARA DAR", "DONNER", "DAI", "TOQUE P/ DAR", "GEBEN", "タッチで もらう")};
static const char *const sGiveOpenText[] = {CTR_TEXT("TAP TO OPEN", "TOCA PARA ABRIR", "OUVRIR", "APRI", "TOQUE P/ ABRIR", "ÖFFNEN", "タッチで ひらく")};
static const char *const sGiveBackText[] = {CTR_TEXT("BACK TO CHEATS", "VOLVER A TRUCOS", "RETOUR", "INDIETRO", "VOLTAR", "ZURÜCK", "チートに もどる")};

/* The cells have a screen of their own, opened from the first. */
static void GiveOpen(void)
{
    CtrExtras_ShowScreen(CTR_EXTRAS_CHEATS, 1);
}

static void GiveBack(void)
{
    CtrExtras_ShowScreen(CTR_EXTRAS_CHEATS, 0);
}
#endif

/*
 * Fast-forward's SPEED (1x, 1.5x, 2x, 3x, 4x; settings.txt speed=, ZR/ZL on a New 3DS):
 * with the tabs, a cell of ENHANCEMENTS rather than a seventh row of
 * SETTINGS.
 */
static void SpeedStep(int direction)
{
    CtrSettings_StepSpeed(direction, true);
}

static const u8 *SpeedText(void)
{
    static u8 text[6];
    int halves = CtrSettings_SpeedHalves();
    u8 *end = text;

    *end++ = CHAR_0 + halves / 2;
    if (halves & 1)
    {
        *end++ = CHAR_PERIOD;
        *end++ = CHAR_5;
    }
    *end++ = CHAR_x;
    *end = EOS;
    return text;
}

void CtrStereoMonGame_OpenEditor(void);
/*
 * Sharing plane edits over Wi-Fi (3ds_online.c): a screen of its own,
 * opened from ENHANCEMENTS' second. LINK asks for the website's code with
 * the system keyboard (and the server's address first when none is known);
 * the status cell shows the last job's progress or result.
 */
/* ASCII into the game's characters, a few kept at once. */
static const u8 *OnlineGame(const char *ascii)
{
    static u8 ring[4][32];
    static u8 next;
    u8 *out = ring[next++ & 3];

    CtrLocale_ToGame(out, sizeof(ring[0]), ascii);
    return out;
}

static void OnlineLink(void)
{
    char code[16];

    if (CtrOnline_Linked())
    {
        PlaySE(SE_FAILURE);
        return;
    }
    PlaySE(SE_SELECT);
    if (CtrOnline_AskCode(code, sizeof(code)))
        CtrOnline_Pair(code);
}

static const u8 *OnlineLinkText(void)
{
    char text[32];

    if (!CtrOnline_Linked())
        return OnlineGame(CTR_TEXT("TAP TO LINK", "TOCA PARA ENLAZAR", "LIER", "COLLEGA", "TAP TO LINK", "TAP TO LINK", "TAP TO LINK"));
    snprintf(text, sizeof(text), "%s", CtrOnline_User());
    for (char *c = text; *c; ++c)
        if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
    return OnlineGame(text);
}

static void OnlineUnlink(void)
{
    PlaySE(CtrOnline_Linked() ? SE_SELECT : SE_FAILURE);
    if (CtrOnline_Linked())
        CtrOnline_Unlink();
}

static const u8 *OnlineUnlinkText(void)
{
    return OnlineGame(CtrOnline_Linked() ? CTR_TEXT("TAP TO UNLINK", "TOCA", "DÉLIER", "SCOLLEGA", "TAP TO UNLINK", "TAP TO UNLINK", "TAP TO UNLINK") : "-");
}

static void OnlineUploadAll(void)
{
    PlaySE(CtrOnline_UploadAll() ? SE_SELECT : SE_FAILURE);
}

static void OnlineSync(void)
{
    PlaySE(CtrOnline_Sync() ? SE_SELECT : SE_FAILURE);
}

static const u8 *OnlineSendText(void)
{
    return OnlineGame(CtrOnline_Linked() ? CTR_TEXT("TAP TO SEND", "TOCA PARA ENVIAR", "ENVOYER", "INVIA", "TAP TO SEND", "TAP TO SEND", "TAP TO SEND") : "-");
}

static const u8 *OnlineSyncText(void)
{
    return OnlineGame(CtrOnline_Linked() ? CTR_TEXT("TAP TO FETCH", "TOCA PARA BAJAR", "RECEVOIR", "RICEVI", "TAP TO FETCH", "TAP TO FETCH", "TAP TO FETCH") : "-");
}

static const u8 *OnlineStatusText(void)
{
    char text[CTR_ONLINE_TEXT];

    if (CtrOnline_Status(text, sizeof(text)) == CTR_ONLINE_IDLE)
        snprintf(text, sizeof(text), "%s", CtrOnline_Linked() ? "READY" : "NOT LINKED");
    return OnlineGame(text);
}

/* SERVER: another address with the system keyboard; its cell shows the
 * one in use (its host name). STANDARD SERVER: back to poke.cloudnoud.nl. */
static void OnlineServer(void)
{
    PlaySE(SE_SELECT);
    CtrOnline_AskServer();
}

static const u8 *OnlineServerText(void)
{
    char host[48];
    const char *server = CtrOnline_Server(), *p = strstr(server, "://");

    snprintf(host, sizeof(host), "%s", p ? p + 3 : server);
    for (char *c = host; *c; ++c)
        if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
    return OnlineGame(host);
}

static void OnlineStandard(void)
{
    if (CtrOnline_ServerIsStandard())
    {
        PlaySE(SE_FAILURE);
        return;
    }
    PlaySE(SE_SELECT);
    CtrOnline_UseStandardServer();
}

static const u8 *OnlineStandardText(void)
{
    return OnlineGame(CtrOnline_ServerIsStandard() ? CTR_TEXT("IN USE", "EN USO", "UTILISE", "IN USO", "IN USE", "IN USE", "IN USE")
                                                   : CTR_TEXT("TAP TO USE", "TOCA PARA USAR", "UTILISER", "USA", "TAP TO USE", "TAP TO USE", "TAP TO USE"));
}

static void OnlineTest(void)
{
    PlaySE(CtrOnline_TestServer() ? SE_SELECT : SE_FAILURE);
}

static const u8 *OnlineTestText(void)
{
    return OnlineGame(CTR_TEXT("TAP TO CHECK", "TOCA PARA PROBAR", "VERIFIER", "VERIFICA", "TAP TO CHECK", "TAP TO CHECK", "TAP TO CHECK"));
}

/* The status row's refresh button: fetch what is new on the server. */
static void OnlineStatusTap(void)
{
    PlaySE(CtrOnline_Linked() && CtrOnline_Sync() ? SE_SELECT : SE_FAILURE);
}

/* YOUR NAME: the author's name with the system keyboard; once linked, the
 * name the site shows for the account too. */
void CtrBottom_ForceRedraw(void);
static void OnlineName(void)
{
    PlaySE(SE_SELECT);
    CtrOnline_AskName();
    CtrBottom_ForceRedraw();
}

static const u8 *OnlineNameText(void)
{
    const char *name = CtrStereoMon_Author();

    return OnlineGame(name[0] ? name : CTR_TEXT("TAP TO SET", "TOCA", "CHOISIR", "SCEGLI", "TAP TO SET", "TAP TO SET", "TAP TO SET"));
}

/* NEW FROM SERVER: the editor on what the last sync brought, its chooser
 * showing only those. */
void CtrStereoMonGame_OpenEditorAt(u16 id, bool8 newOnes);
void CtrStereoEdit_PreferDex(bool newOnes);
static void OnlineOpenNew(void)
{
    u16 first;

    PlaySE(SE_SELECT);
    if (CtrOnline_NewIds(&first, 1) > 0)
        CtrStereoMonGame_OpenEditorAt(first, TRUE);
    else
    {
        /* Nothing new: the whole Pokédex, to see and vote. */
        CtrStereoEdit_PreferDex(FALSE);
        CtrStereoMonGame_OpenEditor();
    }
}

static const u8 *OnlineNewText(void)
{
    char text[24];
    unsigned count = CtrOnline_NewIds(NULL, 0);

    if (count == 0) return OnlineGame(CTR_TEXT("OPEN THE POKEDEX", "ABRIR POKEDEX", "OUVRIR POKEDEX", "APRI POKEDEX", "OPEN THE POKEDEX", "OPEN THE POKEDEX", "OPEN THE POKEDEX"));
    snprintf(text, sizeof(text), CTR_TEXT("%u NEW: OPEN", "%u NUEVOS: ABRIR", "%u NOUV.: OUVRIR", "%u NUOVI: APRI", "%u NEW: OPEN", "%u NEW: OPEN", "%u NEW: OPEN"), count);
    return OnlineGame(text);
}

const CtrExtra gCtrExtras[] =
{
    /* Features add their lines here. */
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_OPTIONS, 1), CTR_TEXT("3D WORLD", "MUNDO 3D", "MONDE 3D", "MONDO 3D", "MUNDO 3D", "3D-WELT", "3D ワールド"), "voxel_stereo", 3, 0, sVoxelStereo, NULL, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_OPTIONS, 1), CTR_TEXT("3D DEPTH", "PROFUNDIDAD 3D", "PROFONDEUR 3D", "PROFOND. 3D", "PROFUNDIDADE 3D", "3D-TIEFE", "3D おくゆき"), "voxel_depth", 7, 3, sVoxelDepths, NULL, NULL, NULL},
    /* The battle's Pokémon as their depth planes (3ds_stereo_battle.c);
     * the key is read natively, so its fallback is ON there too. */
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_OPTIONS, 1), CTR_TEXT("3D BATTLE MONS", "POKÉMON 3D", "POKÉMON 3D", "POKÉMON 3D", "3D BATTLE MONS", "3D BATTLE MONS", "3D BATTLE MONS"), "battle_mon_3d", 2, 1, gCtrExtrasOffOn, NULL, NULL, NULL, PlanesShown},
    /* The 3D depths, read natively each frame (CtrStereoDepths). */
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_OPTIONS, 1), CTR_TEXT("3D PRESET", "PERFIL 3D", "PROFIL 3D", "PROFILO 3D", "3D PRESET", "3D PRESET", "3D PRESET"), CTR_DEPTH_PRESET_KEY, CTR_DEPTH_PRESETS, CTR_DEPTH_PRESET_STANDARD, sDepthPresets, NULL, NULL, NULL},
    /* The plane editor's and sharing's switches (read by 3ds_stereo_edit.c
     * and 3ds_stereo_mon_game.c): a sync that brings pictures opens the
     * Pokédex on them; FINAL uploads; the editor's controls on its top screen. */
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_OPTIONS, 1), CTR_TEXT("OPEN NEW ONES", "ABRIR NUEVOS", "OUVRIR NOUV.", "APRI NUOVI", "OPEN NEW ONES", "OPEN NEW ONES", "OPEN NEW ONES"), "online_open_new", 2, 1, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_OPTIONS, 1), CTR_TEXT("UPLOAD ON FINAL", "SUBIR AL ACABAR", "ENVOI SI FINI", "INVIA SE FINITO", "UPLOAD ON FINAL", "UPLOAD ON FINAL", "UPLOAD ON FINAL"), "online_auto", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_OPTIONS, 1), CTR_TEXT("EDITOR CONTROLS", "CONTROLES EDITOR", "COMMANDES ÉDIT.", "COMANDI EDITOR", "EDITOR CONTROLS", "EDITOR CONTROLS", "EDITOR CONTROLS"), "edit_controls", 2, 1, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_OPTIONS, 1), CTR_TEXT("EDITOR MUSIC", "MÚSICA EDITOR", "MUSIQUE ÉDIT.", "MUSICA EDITOR", "EDITOR MUSIC", "EDITOR MUSIC", "EDITOR MUSIC"), CTR_EDITOR_MUSIC_KEY, EDITOR_SONGS, 0, sEditorMusic, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("SPEED", "VELOC. JUEGO", "VITESSE", "VELOCITÀ", "VELOCIDADE", "TEMPO", "スピード"), "speed", 0, 0, NULL, NULL, SpeedStep, SpeedText},
    /* The game and the music keep real time on a slow frame; the sound
     * engine mixes on the DSP's clock (3ds_platform.c, 3ds_audio.c). */
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("REAL-TIME PACING", "RITMO REAL", "RYTHME RÉEL", "RITMO REALE", "RITMO REAL", "ECHTZEIT-TAKT", "リアルタイム"), "audio_clock", 2, 1, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("EXP FOR CATCHING", "EXP AL CAPTURAR", "EXP. CAPTURE", "ESP. CATTURA", "EXP. POR CAPTURA", "EP BEIM FANGEN", "つかまえて けいけんち"), "exp_catch", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("PARTY EXP SHARE", "EXP TODO EQUIPO", "MULTI EXP.", "CONDIV. ESP.", "EXP. PARA EQUIPE", "EP FÜR TEAM", "みんなに けいけんち"), "exp_share", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("TRADE EVO LV. 40", "EVOL. INTERC. N40", "ÉVOL. NIV. 40", "SCAMBIO LIV. 40", "EVO. TROCA NV. 40", "TAUSCH-ENTW. LV.40", "つうしん しんか Lv40"), "trade_evo", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("HMS WITHOUT MOVE", "MO SIN APRENDER", "CS SANS CAP.", "MN SENZA MOSSA", "MOS SEM GOLPE", "VM OHNE ATTACKE", "ひでんわざ なしで"), "field_hms", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("VISIBLE WILD", "SALVAJES VISIBLES", "SAUV. VISIBLES", "SELV. VISIBILI", "SELVAGEM VISÍVEL", "WILDE SICHTBAR", "やせいが みえる"), "visible_wild", 4, 0, sVisibleWild, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("IVS AND EVS", "IV Y EV", "IV ET EV", "IV ED EV", "IVS E EVS", "IS UND FP", "こたいち どりょくち"), "ivs_evs", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("REPEL PROMPT", "AVISO REPELENTE", "RAPPEL REPOUSSE", "AVVISO REPELL.", "AVISO REPELENTE", "SCHUTZ-HINWEIS", "むしよけ かくにん"), "repel_prompt", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("INFINITE TMS", "MT INFINITAS", "CT INFINIES", "MT INFINITE", "MTS INFINITAS", "ENDLOSE TMS", "わざマシン むげん"), "infinite_tms", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("RUN INDOORS", "CORRER DENTRO", "COURSE INTÉR.", "CORRI DENTRO", "CORRER DENTRO", "DRINNEN RENNEN", "へやでも はしる"), "run_indoors", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    /* Moves physical or special by their own category, as from Gen IV on
     * (pokemon.c, MoveSplitEnabled). */
    {CTR_EXTRAS_ENHANCEMENTS, CTR_TEXT("PHYS/SPEC SPLIT", "FÍSICO / ESPECIAL", "PHYS./SPÉC.", "FIS./SPEC.", "FÍS./ESP.", "PHYS./SPEZ.", "ぶつり/とくしゅ"), "move_split", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
#if CTR_CHEATS_MENU
    {CTR_EXTRAS_CHEATS, CTR_TEXT("WILD ENCOUNTERS", "ENC. SALVAJES", "RENCONTRES", "INCONTRI", "ENCONTROS SELV.", "WILDE BEGEGN.", "やせいの でかた"), "encounter_rate", 5, 0, sEncounterRate, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("SHINY ODDS", "PROB. VARIOCOLOR", "CHROMATIQUES", "PROB. SHINY", "CHANCE SHINY", "SHINY-CHANCE", "いろちがい"), "shiny_odds", 6, 0, sShinyOdds, NULL, NULL, NULL},
    /* Only wild Pokémon with depth planes, to try the 3D battle anywhere
     * (CtrStereoMonGame_WildSpecies). */
    {CTR_EXTRAS_CHEATS, CTR_TEXT("3D WILD ONLY", "SOLO SALVAJES 3D", "SAUVAGES 3D", "SOLO SELV. 3D", "SO SELV. 3D", "3D WILD ONLY", "3D WILD ONLY"), "wild_3d", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL, PlanesShown},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("ALWAYS CATCH", "CAPTURA SEGURA", "CAPTURE SÛRE", "CATTURA SICURA", "CAPTURA GARANTIDA", "IMMER FANGEN", "かならず つかまる"), "always_catch", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("INSTANT VICTORY", "VICTORIA INSTANT.", "VICTOIRE IMM.", "VITT. IMM.", "VITÓRIA IMEDIATA", "SOFORTSIEG", "すぐに かつ"), "instant_victory", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("FAST EGGS", "HUEVOS RÁPIDOS", "OEUFS RAPIDES", "UOVA VELOCI", "OVOS RÁPIDOS", "SCHNELLE EIER", "タマゴが はやい"), "fast_eggs", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("INFINITE MONEY", "DINERO INFINITO", "ARGENT INFINI", "SOLDI INFINITI", "DINHEIRO INFINITO", "ENDLOS GELD", "おかね むげん"), "infinite_money", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("HOENN DEX FULL", "DEX HOENN LLENA", "DEX HOENN PLEIN", "DEX HOENN PIENO", "DEX HOENN COMPL.", "HOENN-DEX VOLL", "ホウエンずかん かんせい"), "dex_hoenn", 0, 0, sDexDone, CompleteHoennDex, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("NATIONAL DEX ON", "DEX NACIONAL ACT.", "DEX NAT. ACTIF", "DEX NAZ. ATTIVO", "ATIVAR DEX NAC.", "NAT. DEX AN", "ぜんこくずかん オン"), "dex_national_on", 0, 0, sDexDone, UnlockNationalDex, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("NATIONAL DEX FULL", "DEX NACIONAL LLENA", "DEX NAT. PLEIN", "DEX NAZ. PIENO", "DEX NAC. COMPLETA", "NAT. DEX VOLL", "ぜんこくずかん かんせい"), "dex_national", 0, 0, sDexDone, CompleteNationalDex, NULL, NULL},
    {CTR_EXTRAS_CHEATS, CTR_TEXT("GIVE ITEMS", "DAR OBJETOS", "DONNER OBJETS", "DAI OGGETTI", "DAR ITENS", "ITEMS GEBEN", "どうぐを もらう"), "give_open", 0, 0, sGiveOpenText, GiveOpen, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_CHEATS, 1), CTR_TEXT("GIVE ITEMS", "DAR OBJETOS", "DONNER OBJETS", "DAI OGGETTI", "DAR ITENS", "ITEMS GEBEN", "どうぐを もらう"), "give_back", 0, 0, sGiveBackText, GiveBack, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_CHEATS, 1), CTR_TEXT("POCKET", "BOLSILLO", "POCHE", "TASCA", "BOLSO", "TASCHE", "ポケット"), "give_pocket", 5, 0, sGivePockets, NULL, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_CHEATS, 1), CTR_TEXT("ITEM", "OBJETO", "OBJET", "OGGETTO", "ITEM", "ITEM", "どうぐ"), "give_item", 0, 0, NULL, NULL, GiveItemStep, GiveItemText},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_CHEATS, 1), CTR_TEXT("HOW MANY", "CANTIDAD", "QUANTITÉ", "QUANTI", "QUANTIDADE", "ANZAHL", "いくつ"), "give_count", 5, 0, sGiveCounts, NULL, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_CHEATS, 1), CTR_TEXT("GIVE", "DAR", "DONNER", "DAI", "DAR", "GEBEN", "もらう"), "give_now", 0, 0, sGiveText, GiveItemsNow, NULL, NULL},
#endif
    /* The clock (3ds_daynight.c): outdoor light by the hour, its speed, the
     * wild Pokémon of the hour (3ds_encounters.c) and the bottom screen's
     * clock. ENHANCEMENTS is full, twelve cells a screen: its second. */
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_ENHANCEMENTS, 1), CTR_TEXT("DAY / NIGHT", "DÍA / NOCHE", "JOUR / NUIT", "GIORNO / NOTTE", "DIA / NOITE", "TAG / NACHT", "ひると よる"), "day_night", 2, 0, sDayNight, NULL, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_ENHANCEMENTS, 1), CTR_TEXT("CLOCK SPEED", "VELOC. RELOJ", "VITESSE HORLOGE", "VELOC. OROLOGIO", "VELOC. RELÓGIO", "UHR-TEMPO", "とけいの はやさ"), "clock_speed", CTR_DAYNIGHT_SPEED_COUNT, 0, sClockSpeeds, NULL, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_ENHANCEMENTS, 1), CTR_TEXT("SYNC CLOCK", "SINCR. RELOJ", "SYNCHRO HORLOGE", "SINCR. OROLOGIO", "SINCR. RELÓGIO", "UHR SYNCHR.", "とけいを あわせる"), "clock_sync", 0, 0, sClockSync, SyncClock, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_ENHANCEMENTS, 1), CTR_TEXT("WILD POKEMON", "POKéMON SALV.", "POKÉMON SAUV.", "POKéMON SELV.", "POKéMON SELV.", "WILDE POKéMON", "やせいの ポケモン"), "timed_encounters", 2, 0, sTimedEncounters, NULL, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_ENHANCEMENTS, 1), CTR_TEXT("SHOW CLOCK", "VER RELOJ", "VOIR HORLOGE", "MOSTRA OROLOGIO", "MOSTRAR RELÓGIO", "UHR ZEIGEN", "とけい ひょうじ"), "show_clock", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    /* Sharing the plane editor's edits (3ds_online.c): the third tab, only
     * to load what is new and to send what is done (voting is in the
     * editor's Pokédex). The status takes the first row whole (with the
     * pictures the last sync brought; 3ds_bottom_ui.c); gaps keep the
     * cells in two short columns. */
    {CTR_EXTRAS_ONLINE, CTR_TEXT("STATUS", "ESTADO", "ÉTAT", "STATO", "STATUS", "STATUS", "STATUS"), "online_status", 0, 0, NULL, OnlineStatusTap, NULL, OnlineStatusText},
    {CTR_EXTRAS_ONLINE, CTR_TEXT("LOAD NEW ONES", "BAJAR NUEVOS", "CHARGER NOUV.", "SCARICA NUOVI", "LOAD NEW ONES", "LOAD NEW ONES", "LOAD NEW ONES"), "online_sync", 0, 0, NULL, OnlineSync, NULL, OnlineSyncText},
    {CTR_EXTRAS_ONLINE, CTR_TEXT("SEE AND VOTE", "VER Y VOTAR", "VOIR, VOTER", "VEDI E VOTA", "SEE AND VOTE", "SEE AND VOTE", "SEE AND VOTE"), "online_new", 0, 0, NULL, OnlineOpenNew, NULL, OnlineNewText},
    /* The server: poke.cloudnoud.nl unless another is typed (3ds_online.c). */
    {CTR_EXTRAS_ONLINE, CTR_TEXT("SERVER", "SERVIDOR", "SERVEUR", "SERVER", "SERVER", "SERVER", "SERVER"), "online_server", 0, 0, NULL, OnlineServer, NULL, OnlineServerText},
    {CTR_EXTRAS_ONLINE, CTR_TEXT("STANDARD SERVER", "SERVIDOR NORMAL", "SERVEUR NORMAL", "SERVER NORMALE", "STANDARD SERVER", "STANDARD SERVER", "STANDARD SERVER"), "online_standard", 0, 0, NULL, OnlineStandard, NULL, OnlineStandardText},
    /* The server checked as tools/tlsprobe does; the status row shows how. */
    {CTR_EXTRAS_ONLINE, CTR_TEXT("TEST SERVER", "PROBAR SERVIDOR", "TESTER SERVEUR", "PROVA SERVER", "TEST SERVER", "TEST SERVER", "TEST SERVER"), "online_test", 0, 0, NULL, OnlineTest, NULL, OnlineTestText},
    {CTR_EXTRAS_ONLINE, "", CTR_EXTRAS_GAP_KEY, 0, 0, NULL, NULL, NULL, NULL},
    {CTR_EXTRAS_ONLINE, CTR_TEXT("YOUR NAME", "TU NOMBRE", "TON NOM", "IL TUO NOME", "YOUR NAME", "YOUR NAME", "YOUR NAME"), "online_name", 0, 0, NULL, OnlineName, NULL, OnlineNameText},
    {CTR_EXTRAS_ONLINE, CTR_TEXT("UPLOAD ALL EDITS", "SUBIR EDICIONES", "ENVOYER TOUT", "INVIA TUTTO", "UPLOAD ALL EDITS", "UPLOAD ALL EDITS", "UPLOAD ALL EDITS"), "online_upload", 0, 0, NULL, OnlineUploadAll, NULL, OnlineSendText},
    {CTR_EXTRAS_ONLINE, CTR_TEXT("LINK CONSOLE", "ENLAZAR CONSOLA", "LIER CONSOLE", "COLLEGA CONSOLE", "LINK CONSOLE", "LINK CONSOLE", "LINK CONSOLE"), "online_link", 0, 0, NULL, OnlineLink, NULL, OnlineLinkText},
    {CTR_EXTRAS_ONLINE, CTR_TEXT("UNLINK", "DESENLAZAR", "DÉLIER", "SCOLLEGA", "UNLINK", "UNLINK", "UNLINK"), "online_unlink", 0, 0, NULL, OnlineUnlink, NULL, OnlineUnlinkText},
    /* The lead Pokémon walking behind the player, on HGSS's sprites from the
     * data pack (src/follower_mon.c). */
    /* The party menu, the bag, the trainer card and the Pokédex as the game
     * draws them, in a window of the bottom screen, instead of the bottom
     * screen's own (3ds_bottom_game.inc). */
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_ENHANCEMENTS, 1), CTR_TEXT("CLASSIC MENUS", "MENÚS CLÁSICOS", "MENUS CLASSIQ.", "MENU CLASSICI", "MENUS CLÁSSICOS", "KLASS. MENÜS", "むかしの メニュー"), "classic_menus", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
    {CTR_EXTRAS_SCREEN(CTR_EXTRAS_ENHANCEMENTS, 1), CTR_TEXT("FOLLOWER", "ACOMPAÑANTE", "COMPAGNON", "COMPAGNO", "COMPANHEIRO", "BEGLEITER", "つれあるき"), "follower", 2, 0, gCtrExtrasOffOn, NULL, NULL, NULL},
#include "3ds_nuzlocke_extras.inc"
#include "3ds_randomizer_extras.inc"
    #include "3ds_field_qol_extras.inc"
    {0},
};

/* The last line only keeps the array from being empty. */
const unsigned gCtrExtraCount = ARRAY_COUNT(gCtrExtras) - 1;

int CtrExtras_Value(const CtrExtra *extra)
{
    int value = CtrSettings_GetInt(extra->key, extra->fallback);

    return value >= 0 && value < extra->count ? value : extra->fallback;
}

/* One step either way, wrapping round like the game's options; an action
 * runs instead. */
void CtrExtras_Step(const CtrExtra *extra, int direction)
{
    if (extra->count == 0)
    {
        if (extra->act)
            extra->act();
        return;
    }
    CtrSettings_SetInt(extra->key, (CtrExtras_Value(extra) + (direction < 0 ? extra->count - 1 : 1)) % extra->count);
    if (extra->act)
        extra->act();
}

const CtrExtra *CtrExtras_Find(const char *key)
{
    for (unsigned i = 0; i < gCtrExtraCount; ++i)
        if (strcmp(gCtrExtras[i].key, key) == 0)
            return &gCtrExtras[i];
    return NULL;
}

int CtrExtras_Get(const char *key)
{
    for (unsigned i = 0; i < gCtrExtraCount; ++i)
        if (strcmp(gCtrExtras[i].key, key) == 0)
            return CtrExtras_Value(&gCtrExtras[i]);
    return 0;
}

bool CtrExtras_NeedsVoxel(const CtrExtra *extra)
{
    return extra->values == sVoxelStereo || extra->values == sVoxelDepths;
}
