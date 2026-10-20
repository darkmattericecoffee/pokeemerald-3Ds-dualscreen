/*
 * The stereoscopic Pokémon viewer, native side: the planes file, the texture
 * the planes are composed into, and the drawing (3ds_stereo_mon.h).
 *
 * A Pokémon is a stack of flat cut-outs, one per depth plane, like the
 * layered sprites the 3DS's own 2D games use: each plane is drawn shifted
 * sideways for each eye in proportion to its depth (front projection, which
 * is what the Sprite Stereo Lab previews). The stack never turns: turned,
 * each plane would slide by its depth and the slices show at the edges,
 * and the cards would lose their whole-pixel scale.
 */
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include "3ds_platform.h"
#include "3ds_video.h"
#include "3ds_input.h"
#include "3ds_log.h"
#include "3ds_stereo_mon.h"
#include "3ds_online.h"
#include "3ds_button_sprites.h"
#include "3ds_stereo_strip.h"

/*
 * One texture in linear memory with a 64x64 tile per frame and plane, two
 * tiles across once there are more than eight, at most 128x512 (128 KiB):
 * the linear heap has a few hundred KiB spare once the field is up. Both
 * frames of the picture fit with up to eight planes; with more, only the
 * first frame is kept and the animation rests. Fewer planes are fewer tiles
 * and a smaller texture. Made once, at the largest size it will need, and
 * kept: the emulator's texture cache keys surfaces by address, and a fresh
 * texture over memory a freed one used was read with the old one's layout.
 */
#define DIM CTR_STEREO_MON_DIM
#define TEX_MAX_TILES 32
/* Up to five frames of four groups: 32 tiles, four across. After a battle
 * the linear heap may hold no run that long (the battle keeps a texture of
 * its own): then half, then a quarter (eight tiles: the editor's four
 * planes and two frames, or a gallery picture's eight planes, one frame). */
#define TEX_MAX_W (4 * DIM)
#define TEX_MAX_H 512
static unsigned sTexW = TEX_MAX_W, sTexTiles = TEX_MAX_TILES;
/* Tiles composed per frame, at most. */
#define TILES_PER_FRAME 6
/* Screen pixels per sprite pixel, and where the sprite's middle sits. */
#define SCALE 3.0f
#define CENTRE_X (CTR_GAME_WIDTH / 2)
#define CENTRE_Y 130.0f
/* Total stack depth in sprite pixels, independent of the exported plane count. */
#define DEPTH_SPAN 14.4f /* Original ten-plane span, independent of plane count. */
/* PLANES SIT's AROUND (CtrStereoDepths): the plane at the picture's own
 * depth, most of the body behind it, the nearest planes a little in front. */
#define PIVOT_FRACTION 0.62f
/* The Pokédex picture's planes at most: with eight both frames of the
 * picture fit the texture, so its animation plays. */
#define DEX_GROUPS_MAX 8
/* The Pokédex's screen window on the top screen, and the picture in it. */
#define DEX_SCALE 2.0f
#define DEX_WIN_X 70
#define DEX_WIN_Y 28
#define DEX_WIN_W 260
#define DEX_WIN_H 180
#define DEX_WIN_CX (DEX_WIN_X + DEX_WIN_W / 2)
/* The backdrop (the species' battle terrain, CtrStereoMon_BackdropRow): a
 * 256x128 RGBA5551 texture, made once, drawn behind the screen a little. */
#define BACKDROP_W 256
#define BACKDROP_H 128
/* Its magnification in the window, and where the opponent's mound is in
 * it (the battle stands the opponent there): under the Pokémon's feet. */
#define BACKDROP_SCALE 1.6f
#define BACKDROP_MOUND_X 176
#define BACKDROP_MOUND_Y 76
/* The animation: the second frame for a moment, every so often. */
#define ANIM_PERIOD 150
#define ANIM_HOLD 14
/* How many planes the file's are merged into (PLANES_KEY): fewer are
 * thicker slices, fewer tiles and less VRAM, the same depth overall. */
#define PLANES_KEY "stereo_planes"
#define GROUPS_MIN 2
/* Pixels of the planes behind carried on each card (CtrStereoMon_ComposeTile). */
/* Four planes 4.8 sprite pixels apart shift up to about three sprite pixels
 * between the front and the back at 100%: the far cards go on that far. */
#define SKIRT 4
/* A back's depth against a front's (DepthOf; 3ds_stereo_battle.c too). */
#define BACK_DEPTH 0.6f
/* The editor's animation: each frame as long as the others. */
#define EDIT_ANIM_STEP 30

/* A planes file: its header and index, resident. The viewer's own (the
 * Pokémon's fronts) and the battle's other pictures (the backs file). */
typedef struct
{
    const char *path;
    uint8_t index[CTR_STEREO_MON_HEADER_BYTES + CTR_STEREO_MON_INDEX_BYTES * CTR_STEREO_MON_MAX_COUNT];
    uint32_t indexBytes, fileSize;
    CtrStereoMonHeader header;
    bool loaded, tried;
} PlanesFile;
static PlanesFile sFront = {.path = CTR_STEREO_MON_FILE}, sBacks = {.path = CTR_STEREO_BACKS_FILE};
/* The viewer's shorthands for its own file. */
#define sIndex sFront.index
#define sIndexBytes sFront.indexBytes
#define sHeader sFront.header
#define sLoaded sFront.loaded
/* One Pokémon's planes, read on show (the pack's, or the SD card's edited
 * ones), and the header they go with: the file's, its planes overridden by
 * an edited file's or the editor's. */
static uint8_t sPlanes[CTR_STEREO_MON_MAX_FRAMES * DIM * DIM / 2];
static CtrStereoMonHeader sShown;
static bool sEdited;
/* The editor (3ds_stereo_edit.c): open on the Pokémon, and the plane it edits. */
static bool sEditing;
static unsigned sEditPlane;
/* What the editor shows on this screen: its frame (or the animation), and
 * the plane it edits outlined: the pixels just outside that plane, as runs
 * along each row, worked out again whenever the planes or the frame change. */
static unsigned sEditFrame;
static bool sEditAnimate, sEditOutline;
/* The editor's strays (CtrStereoMon_SetStrays): their frame, where, how
 * many, and a version for the redraw key. Ringed over the plane they are on
 * (any plane: the ring sits at the screen's depth, over the whole stack).
 * They blink every STRAY_BLINK frames. */
#define STRAY_BLINK 20
static unsigned sStrayFrame, sStrayCount, sStrayVersion;
static uint8_t sStrayXY[CTR_STEREO_STRAYS_MAX][2];
static uint32_t sEditMark;
/* The editor's other views: every pixel in its plane's colour (composed
 * from sTinted instead of the picture), and the battle's. */
static bool sEditLayers, sEditBattle, sEditControls = true;
/* The Pokédex's view (CtrStereoMon_SetChooser), the versions screen's
 * (CtrStereoMon_SetVersionsView). */
static bool sChooser, sVersionsView;
static uint16_t sTinted[CTR_STEREO_MON_MAX_FRAMES][CTR_STEREO_MON_DIM * CTR_STEREO_MON_DIM];
/* The shown picture's edit file's trailer (CtrStereoMon_EditMeta). */
static CtrStereoEditMeta sMeta;
/* The picture's opaque pixels' extent over all its frames: its feet. */
static int sBoxLeft, sBoxRight, sBoxBottom;
typedef struct { uint8_t x, y, len; } OutlineRun;
static OutlineRun sRuns[2048];
static unsigned sRunCount, sRunsVersion = 1, sPlanesVersion;
static uint16_t sRunsId;
static unsigned sRunsFrame, sRunsPlane;
/* Where DrawPlanes put each plane for the eye being drawn. */
static float sPlaneX[CTR_STEREO_MON_MAX_PLANES], sPlaneY, sPlaneScale;

static C3D_Tex sTex;
static bool sTexReady, sTexAllocated;
/* The texture's layout for what is shown: tiles across, and how many of the
 * picture's frames it holds. */
static unsigned sTexCols, sTexFrames, sTexGroups;
static Tex3DS_SubTexture sTiles[CTR_STEREO_MON_MAX_FRAMES * CTR_STEREO_MON_MAX_PLANES];

/* What is shown: the picture is copied here and composed inside the frame,
 * tile by tile (sComposed of sToCompose done). */
static bool sActive, sGallery;
static unsigned sComposed, sToCompose;
static uint16_t sId;
static unsigned sFrames;
static uint16_t sPicture[CTR_STEREO_MON_MAX_FRAMES][DIM * DIM];
static char sName[CTR_STEREO_MON_NAME];

/* The view: the number of planes (kept in the settings), the animation.
 * The depth is everybody's (CtrStereoDepths_Get). */
static int sGroups = -1;
static bool sAnimate = true;
static uint32_t sTick;
/* Whether sDrawn (CtrStereoMon_NeedsFrame) describes the frame on the
 * screen, and whether this frame's tick has been counted already. */
static bool sDrawnValid, sTicked;
/* Moves on with every word redrawn on the help sheet (CtrStereoMon_HelpCell). */
static unsigned sHelpVersion;
/* How long the gallery shows an online job's result after it ends. */
#define ONLINE_LINE_MS 6000

static bool LoadEdit(uint16_t national);

static bool LoadFile(PlanesFile *pf)
{
    FILE *f;
    long size;

    if (pf->tried) return pf->loaded;
    pf->tried = true;
    f = CtrFs_OpenAsset(pf->path);
    if (f == NULL)
    {
        CtrLog_Write(CTR_LOG_VIDEO, "stereo: no %s", pf->path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    pf->indexBytes = (uint32_t)fread(pf->index, 1, sizeof(pf->index), f);
    fclose(f);
    if (size > 0 && CtrStereoMon_ParseHeader(pf->index, pf->indexBytes, &pf->header) && pf->header.count > 0
        && pf->header.count <= CTR_STEREO_MON_MAX_COUNT)
    {
        pf->fileSize = (uint32_t)size;
        pf->loaded = true;
        CtrLog_Write(CTR_LOG_VIDEO, "stereo: %s: %u entries, %u planes, up to %u frames (%ld bytes)", pf->path,
                     pf->header.count, pf->header.planes, pf->header.frames, size);
    }
    else
        CtrLog_Write(CTR_LOG_ERROR, "stereo: %s is not a planes file (%ld bytes)", pf->path, size);
    return pf->loaded;
}

bool CtrStereoMon_Load(void) { return LoadFile(&sFront); }

/* The gallery's order: the fronts, then the backs file by kind. */
static uint16_t sGallery_[2 * CTR_STEREO_MON_MAX_COUNT];
static unsigned sGalleryCount;
static bool sGalleryBuilt;

static void GalleryBuild(void)
{
    static const uint16_t kinds[] = {CTR_STEREO_ID_BACK, CTR_STEREO_ID_TRAINER, CTR_STEREO_ID_PLAYER};

    if (sGalleryBuilt) return;
    sGalleryBuilt = true;
    sGalleryCount = 0;
    if (!LoadFile(&sFront)) return;
    for (unsigned i = 0; i < sHeader.count; ++i) sGallery_[sGalleryCount++] = CtrStereoMon_NationalAt(i);
    if (!LoadFile(&sBacks)) return;
    for (unsigned k = 0; k < 3; ++k)
        for (unsigned i = 0; i < sBacks.header.count && sGalleryCount < 2 * CTR_STEREO_MON_MAX_COUNT; ++i)
        {
            uint16_t id = 0;

            if (CtrStereoMon_IndexEntry(sBacks.index, sBacks.indexBytes, &sBacks.header, i, &id, NULL)
                && (id & 0xC000) == kinds[k])
                sGallery_[sGalleryCount++] = id;
        }
}

unsigned CtrStereoMon_GalleryCount(void)
{
    GalleryBuild();
    return sGalleryCount;
}

uint16_t CtrStereoMon_GalleryIdAt(unsigned index)
{
    GalleryBuild();
    return index < sGalleryCount ? sGallery_[index] : 0;
}

int CtrStereoMon_GalleryIndexOf(uint16_t id)
{
    GalleryBuild();
    for (unsigned i = 0; i < sGalleryCount; ++i)
        if (sGallery_[i] == id) return (int)i;
    return -1;
}

uint16_t CtrStereoMon_Counterpart(uint16_t id)
{
    if ((id & 0xC000) == 0) return CtrStereoMon_HasBack(CTR_STEREO_ID_BACK | id) ? (CTR_STEREO_ID_BACK | id) : 0;
    if ((id & 0xC000) == CTR_STEREO_ID_BACK) return CtrStereoMon_Has(id & 0x3FFF) ? (id & 0x3FFF) : 0;
    return 0;
}

/* The file an id's planes are in, and the id's index there (or -1). */
static PlanesFile *FileOf(uint16_t id, int *index)
{
    PlanesFile *pf = (id & 0xC000) ? &sBacks : &sFront;

    *index = LoadFile(pf) ? CtrStereoMon_Find(pf->index, pf->indexBytes, &pf->header, id) : -1;
    return pf;
}

unsigned CtrStereoMon_Count(void) { return sLoaded ? sHeader.count : 0; }

uint16_t CtrStereoMon_NationalAt(unsigned index)
{
    uint16_t national = 0;

    if (sLoaded) CtrStereoMon_IndexEntry(sIndex, sIndexBytes, &sHeader, index, &national, NULL);
    return national;
}

bool CtrStereoMon_Has(uint16_t national)
{
    return sLoaded && CtrStereoMon_Find(sIndex, sIndexBytes, &sHeader, national) >= 0;
}

const CtrStereoMonHeader *CtrStereoMon_Header(void) { return sLoaded ? &sHeader : NULL; }
const CtrStereoMonHeader *CtrStereoMon_BacksHeader(void) { return LoadFile(&sBacks) ? &sBacks.header : NULL; }

/*
 * Deoxys (national 386): the game copies the second 64x64 of both its
 * sheets - its Speed form, Emerald's own - over the first
 * (DuplicateDeoxysTiles), so a battle only ever shows that second frame
 * and the front never animates. The viewer and the editor take the planes
 * of that frame as the one frame there is; where the frame count is not
 * ours to change (CtrStereoMon_PlanesFor, the battle's cache) the first
 * frame is made the second's, as the game does with the tiles.
 */
#define DEOXYS_NATIONAL 386
static void FormFrameOnly(uint16_t id, uint8_t *planes, uint32_t frameBytes, unsigned *frames)
{
    if ((id & 0x3FFF) != DEOXYS_NATIONAL || ((id & 0xC000) != 0 && (id & 0xC000) != CTR_STEREO_ID_BACK)) return;
    if (frames == NULL || *frames < 2 || planes == NULL) return;
    memmove(planes, planes + frameBytes, frameBytes);
    *frames = 1;
}

static void FormFrameCopied(uint16_t id, uint8_t *planes, uint32_t frameBytes, unsigned frames)
{
    if ((id & 0x3FFF) != DEOXYS_NATIONAL || ((id & 0xC000) != 0 && (id & 0xC000) != CTR_STEREO_ID_BACK)) return;
    if (frames < 2 || planes == NULL) return;
    memcpy(planes, planes + frameBytes, frameBytes);
}

/* One entry's planes from a file into `out` (`size` bytes), with its frames. */
static bool ReadPlanesInto(PlanesFile *pf, unsigned index, uint8_t *out, uint32_t size, unsigned *frames)
{
    uint32_t offset, bytes;
    unsigned entryFrames = CtrStereoMon_EntryFrames(pf->index, pf->indexBytes, &pf->header, index);
    FILE *f;
    bool ok;

    bytes = CtrStereoMon_FramesBytes(&pf->header, entryFrames);
    if (entryFrames == 0 || !CtrStereoMon_IndexEntry(pf->index, pf->indexBytes, &pf->header, index, NULL, &offset)
        || offset > pf->fileSize || (uint64_t)offset + bytes > pf->fileSize || bytes > size)
        return false;
    f = CtrFs_OpenAsset(pf->path);
    if (f == NULL) return false;
    ok = fseek(f, (long)offset, SEEK_SET) == 0 && fread(out, 1, bytes, f) == bytes;
    fclose(f);
    if (ok && frames) *frames = entryFrames;
    return ok;
}

bool CtrStereoMon_PlanesFor(uint16_t national, uint8_t *out, uint32_t size)
{
    int index;
    unsigned frames = 0;

    if (!CtrStereoMon_Load()) return false;
    index = CtrStereoMon_Find(sIndex, sIndexBytes, &sHeader, national);
    if (index < 0 || !ReadPlanesInto(&sFront, (unsigned)index, out, size, &frames)) return false;
    FormFrameCopied(national, out, CtrStereoMon_FramesBytes(&sHeader, 1), frames);
    return true;
}

bool CtrStereoMon_HasBack(uint16_t id)
{
    return LoadFile(&sBacks) && CtrStereoMon_Find(sBacks.index, sBacks.indexBytes, &sBacks.header, id) >= 0;
}

bool CtrStereoMon_BackPlanesFor(uint16_t id, uint8_t *out, uint32_t size, unsigned *frames)
{
    int index;
    unsigned n = 0;

    if (!LoadFile(&sBacks)) return false;
    index = CtrStereoMon_Find(sBacks.index, sBacks.indexBytes, &sBacks.header, id);
    if (index < 0 || !ReadPlanesInto(&sBacks, (unsigned)index, out, size, &n)) return false;
    FormFrameOnly(id, out, CtrStereoMon_FramesBytes(&sBacks.header, 1), &n);
    if (frames) *frames = n;
    return true;
}

bool CtrStereoMon_Show(uint16_t national, const uint16_t *picture, unsigned frames,
                       const char *name, bool gallery)
{
    return CtrStereoMon_ShowId(national, picture, frames, name, gallery);
}

bool CtrStereoMon_ShowId(uint16_t id, const uint16_t *picture, unsigned frames, const char *name, bool gallery)
{
    int index;
    PlanesFile *pf = FileOf(id, &index);
    unsigned entryFrames = 0;

    if (!CtrStereoMon_Load() || picture == NULL || frames == 0) return false;
    if (index < 0 || !ReadPlanesInto(pf, (unsigned)index, sPlanes, sizeof(sPlanes), &entryFrames))
    {
        CtrLog_Write(CTR_LOG_ERROR, "stereo: id %04x has no readable planes", id);
        return false;
    }
    FormFrameOnly(id, sPlanes, CtrStereoMon_FramesBytes(&pf->header, 1), &entryFrames);
    if (frames > CTR_STEREO_MON_MAX_FRAMES) frames = CTR_STEREO_MON_MAX_FRAMES;
    memcpy(sPicture, picture, frames * sizeof(sPicture[0]));
    sFrames = frames;
    sBoxLeft = DIM;
    sBoxRight = sBoxBottom = -1;
    for (unsigned f = 0; f < frames; ++f)
        for (int i = 0; i < DIM * DIM; ++i)
            if (sPicture[f][i] != 0)
            {
                int x = i % DIM, y = i / DIM;

                if (x < sBoxLeft) sBoxLeft = x;
                if (x > sBoxRight) sBoxRight = x;
                if (y > sBoxBottom) sBoxBottom = y;
            }
    if (sBoxRight < 0) sBoxLeft = 0, sBoxRight = sBoxBottom = DIM - 1;
    sId = id;
    sShown = pf->header;
    sShown.frames = (uint8_t)entryFrames;
    sEdited = LoadEdit(id);
    snprintf(sName, sizeof(sName), "%s", name ? name : "");
    if (sGroups < 0)
    {
        sGroups = CtrSettings_GetInt(PLANES_KEY, sHeader.planes);
        if (sGroups < GROUPS_MIN || sGroups > sHeader.planes) sGroups = sHeader.planes;
    }
    if (!sActive) sTick = 0;
    sActive = true;
    sGallery = gallery;
    /* Composed afresh on the next frame (TextureReady). */
    sTexReady = false;
    return true;
}

/* The planes merged into another number of groups: composed again. */
static void SetGroups(int groups)
{
    if (groups < GROUPS_MIN) groups = GROUPS_MIN;
    if (groups > (int)sShown.planes) groups = (int)sShown.planes;
    if (groups == sGroups) return;
    sGroups = groups;
    CtrSettings_SetInt(PLANES_KEY, sGroups);
    sTexReady = false;
}

/* The planes drawn with: the setting, capped for the hologram. */
static unsigned Groups(void)
{
    if (sEditing) return sShown.planes;
    if (sGroups > (int)sShown.planes) return sShown.planes;
    return !sGallery && sGroups > DEX_GROUPS_MAX ? DEX_GROUPS_MAX : (unsigned)sGroups;
}

static bool sUndrawn;

void CtrStereoMon_Hide(void) { sActive = false; }
bool CtrStereoMon_Active(void) { return sActive && !sUndrawn; }
void CtrStereoMon_SetDrawn(bool drawn) { sUndrawn = !drawn; }
bool CtrStereoMon_Hologram(void) { return sActive && !sGallery; }

void CtrStereoMon_Press(unsigned keys)
{
    if (!sActive) return;
    if (keys & CTR_KEY_L) SetGroups(sGroups - 1);
    if (keys & CTR_KEY_R) SetGroups(sGroups + 1);
    if (keys & CTR_KEY_X) sAnimate = !sAnimate;
}

/* ── The SD card's edited planes (3ds_stereo_edit.c) ────────────────── */

#define EDIT_HEADER_BYTES 16
#define EDIT_FRAME_BYTES (DIM * DIM / 2)
/* The trailer after the frames (3ds_stereo_mon.h): who, when, how far. */
#define META_MAGIC "EM3DSMD1"
#define META_BYTES 32
#define META_FINAL 1
/* 1970-01-01 to 2000-01-01, in seconds: the trailer's epoch. */
#define META_EPOCH 946684800u

#define EDIT_DIR "sdmc:/3ds/emerald3ds/" CTR_STEREO_EDIT_DIR
/* Which Pokémon have edit files (CtrStereoMon_EditScan). */
static uint8_t sEditMarks[(CTR_STEREO_MON_MAX_COUNT + 7) / 8];
static bool sEditScanned;

/* The Pokémon's file, opened without the data layer's logging: most
 * Pokémon have none. */
/* An id's edit file: NNN for a front, bNNN a back, tNNN a trainer, pNNN a player. */
static void EditPath(uint16_t id, const char *ext, char *path, unsigned size)
{
    static const char prefix[4] = {0, 't', 'b', 'p'};
    unsigned kind = id >> 14;

    if (kind == 0) snprintf(path, size, EDIT_DIR "/%03u.%s", id, ext);
    else snprintf(path, size, EDIT_DIR "/%c%03u.%s", prefix[kind], id & 0x3FFF, ext);
}

static FILE *OpenEdit(uint16_t id, const char *ext, const char *mode)
{
    char path[64];

    EditPath(id, ext, path, sizeof(path));
    return fopen(path, mode);
}

/* The edit marks: a bit per front (by national number) and per backs
 * entry (by index); the same again for the finished ones. */
static uint8_t sBackMarks[(CTR_STEREO_MON_MAX_COUNT + 7) / 8];
static uint8_t sFinalMarks[2][(CTR_STEREO_MON_MAX_COUNT + 7) / 8];

static uint8_t *MarkOf(uint16_t id, unsigned *bit, bool final)
{
    if ((id & 0xC000) == 0)
    {
        if (id >= CTR_STEREO_MON_MAX_COUNT) return NULL;
        *bit = id;
        return final ? sFinalMarks[0] : sEditMarks;
    }
    else
    {
        int index;

        FileOf(id, &index);
        if (index < 0) return NULL;
        *bit = (unsigned)index;
        return final ? sFinalMarks[1] : sBackMarks;
    }
}

static void SetMarkOf(uint16_t id, bool final, bool on)
{
    unsigned bit;
    uint8_t *marks = MarkOf(id, &bit, final);

    if (marks == NULL) return;
    if (on) marks[bit >> 3] |= (uint8_t)(1 << (bit & 7));
    else marks[bit >> 3] &= (uint8_t)~(1 << (bit & 7));
}

static void SetMark(uint16_t id, bool on) { SetMarkOf(id, false, on); }

/* An edit file's trailer, after its `frames` frames, into `meta`; false
 * (and `meta` empty) for a file without one. */
static bool ReadMeta(FILE *f, unsigned frames, CtrStereoEditMeta *meta)
{
    uint8_t t[META_BYTES];

    memset(meta, 0, sizeof(*meta));
    if (fseek(f, EDIT_HEADER_BYTES + (long)frames * EDIT_FRAME_BYTES, SEEK_SET) != 0
        || fread(t, 1, sizeof(t), f) != sizeof(t) || memcmp(t, META_MAGIC, 8) != 0)
        return false;
    meta->finalized = (t[8] & META_FINAL) != 0;
    meta->revision = (uint16_t)(t[10] | (t[11] << 8));
    meta->saved = (uint32_t)t[12] | ((uint32_t)t[13] << 8) | ((uint32_t)t[14] << 16) | ((uint32_t)t[15] << 24);
    memcpy(meta->author, t + 16, CTR_STEREO_EDIT_AUTHOR);
    meta->author[CTR_STEREO_EDIT_AUTHOR] = 0;
    return true;
}

/* ── The author ─────────────────────────────────────────────────────── */

static char sAuthor[CTR_STEREO_EDIT_AUTHOR + 1];
static bool sAuthorRead;

/* Capitals, digits, spaces and a few signs, at most 16. */
static void CleanName(const char *in, char *out)
{
    unsigned n = 0;

    for (; *in && n < CTR_STEREO_EDIT_AUTHOR; ++in)
    {
        char c = *in;

        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' || c == '-' || c == '_')
            out[n++] = c;
    }
    while (n && out[n - 1] == ' ') --n;
    out[n] = 0;
}

const char *CtrStereoMon_Author(void)
{
    FILE *f;

    if (sAuthorRead) return sAuthor;
    sAuthorRead = true;
    f = fopen(EDIT_DIR "/author.txt", "r");
    if (f)
    {
        char line[64] = {0};

        if (fgets(line, sizeof(line), f)) CleanName(line, sAuthor);
        fclose(f);
    }
    if (sAuthor[0] == 0 && R_SUCCEEDED(cfguInit()))
    {
        /* The console's user name: UTF-16, ten characters at most. */
        uint16_t wide[14] = {0};
        char narrow[16] = {0};

        if (R_SUCCEEDED(CFGU_GetConfigInfoBlk2(0x1C, 0x000A0000, wide)))
            for (unsigned i = 0; i < 10 && wide[i]; ++i) narrow[i] = wide[i] < 128 ? (char)wide[i] : '?';
        cfguExit();
        CleanName(narrow, sAuthor);
    }
    return sAuthor;
}

/* An id's edit file read: its planes into `out`, its counts; false without one. */
static bool ReadEdit(uint16_t id, uint8_t *out, uint32_t size, unsigned *planes, unsigned *frames)
{
    uint8_t header[EDIT_HEADER_BYTES];
    FILE *f = OpenEdit(id, "bin", "rb");
    bool ok = false;

    if (f == NULL) return false;
    if (fread(header, 1, sizeof(header), f) == sizeof(header) && memcmp(header, CTR_STEREO_EDIT_MAGIC, 8) == 0
        && (header[8] | (header[9] << 8)) == id && header[12] == DIM && header[13] == 0 && header[14] == DIM
        && header[15] == 0)
    {
        unsigned p = header[10], n = header[11];

        if (p >= GROUPS_MIN && p <= CTR_STEREO_EDIT_MAX_PLANES && n >= 1 && n <= CTR_STEREO_MON_MAX_FRAMES
            && n * EDIT_FRAME_BYTES <= size && fread(out, 1, n * EDIT_FRAME_BYTES, f) == n * EDIT_FRAME_BYTES)
        {
            *planes = p;
            *frames = n;
            FormFrameOnly(id, out, EDIT_FRAME_BYTES, frames);
            ok = true;
        }
    }
    fclose(f);
    return ok;
}

bool CtrStereoMon_EditPlanesFor(uint16_t id, uint8_t *out, uint32_t size, unsigned *frames)
{
    int index;
    PlanesFile *pf = FileOf(id, &index);
    unsigned planes;

    if (index < 0 || !CtrStereoMon_HasEdit(id) || !ReadEdit(id, out, size, &planes, frames)) return false;
    return planes == pf->header.planes;
}

/* The Pokémon's edited planes from the SD card into sPlanes and sShown;
 * false (and the pack's planes left alone) without a well-formed file. */
static bool LoadEdit(uint16_t id)
{
    unsigned planes, frames;
    FILE *f = OpenEdit(id, "bin", "rb");

    memset(&sMeta, 0, sizeof(sMeta));
    if (f == NULL) return false;
    fclose(f);
    if (!ReadEdit(id, sPlanes, sizeof(sPlanes), &planes, &frames))
    {
        int index;
        PlanesFile *pf = FileOf(id, &index);
        unsigned entryFrames = 0;

        CtrLog_Write(CTR_LOG_ERROR, "stereo: id %04x's edit file is malformed; the pack's planes are shown", id);
        /* Whatever the partial read overwrote comes back from the pack. */
        if (index >= 0 && ReadPlanesInto(pf, (unsigned)index, sPlanes, sizeof(sPlanes), &entryFrames))
        {
            sShown = pf->header;
            sShown.frames = (uint8_t)entryFrames;
        }
        return false;
    }
    sShown.planes = (uint8_t)planes;
    sShown.frames = (uint8_t)frames;
    f = OpenEdit(id, "bin", "rb");
    if (f)
    {
        ReadMeta(f, frames, &sMeta);
        fclose(f);
    }
    CtrLog_Write(CTR_LOG_VIDEO, "stereo: id %04x edited planes from the SD card (%u planes, %u frames, r%u by %s%s)", id,
                 planes, frames, sMeta.revision, sMeta.author[0] ? sMeta.author : "?", sMeta.finalized ? ", final" : "");
    return true;
}

uint8_t *CtrStereoMon_EditPlanes(unsigned *planes, unsigned *frames)
{
    if (!sActive) return NULL;
    if (planes) *planes = sShown.planes;
    if (frames) *frames = sShown.frames < sFrames ? sShown.frames : sFrames;
    return sPlanes;
}

const uint16_t *CtrStereoMon_Picture(unsigned frame)
{
    if (!sActive || frame >= sFrames) return NULL;
    return sPicture[frame];
}

uint16_t CtrStereoMon_Shown(void) { return sActive ? sId : 0; }

void CtrStereoMon_SetPlaneCount(unsigned planes)
{
    if (planes < GROUPS_MIN) planes = GROUPS_MIN;
    if (planes > CTR_STEREO_EDIT_MAX_PLANES) planes = CTR_STEREO_EDIT_MAX_PLANES;
    sShown.planes = (uint8_t)planes;
    sTexReady = false;
    ++sPlanesVersion;
}

void CtrStereoMon_Recompose(void)
{
    sTexReady = false;
    ++sPlanesVersion;
}

void CtrStereoMon_SetEditing(bool editing, unsigned plane)
{
    if (editing != sEditing) sTexReady = false;
    sEditing = editing;
    sEditPlane = plane;
}

static int Choice(const char *key, int count, int standard)
{
    int choice = CtrSettings_GetInt(key, standard);

    return choice >= 0 && choice < count ? choice : standard;
}

CtrStereoDepths CtrStereoDepths_Get(void)
{
    /* As OPTIONS lists them (3ds_extras.c sDepthPresets): back, foe, own,
     * planes, where the planes sit. */
    static const CtrStereoDepths presets[CTR_DEPTH_PRESETS] = {
        {4.0f, 1.0f, 0.0f, 2.0f, PIVOT_FRACTION}, /* GENTLE: light, easy on the eyes */
        {6.0f, 2.0f, 0.0f, 3.0f, PIVOT_FRACTION}, /* STANDARD */
        {6.0f, 1.0f, 1.0f, 3.0f, PIVOT_FRACTION}, /* CLOSE: the two Pokémon near each other */
        {8.0f, 3.0f, -1.0f, 4.0f, PIVOT_FRACTION}, /* DEEP: the player's side out of the screen */
    };
    const CtrStereoDepths d = presets[Choice(CTR_DEPTH_PRESET_KEY, CTR_DEPTH_PRESETS, CTR_DEPTH_PRESET_STANDARD)];

    return d;
}

void CtrStereoMon_SetEditView(unsigned frame, bool animate, bool outline, uint16_t mark, unsigned view, bool controls)
{
    bool layers = view == CTR_STEREO_VIEW_LAYERS;

    sEditControls = controls;

    sEditFrame = frame;
    sEditAnimate = animate;
    sEditOutline = outline;
    sEditMark = C2D_Color32(((mark >> 11) & 31) * 255 / 31, ((mark >> 5) & 63) * 255 / 63, (mark & 31) * 255 / 31, 230);
    if (layers != sEditLayers) sTexReady = false;
    sEditLayers = layers;
    sEditBattle = view == CTR_STEREO_VIEW_BATTLE;
}

const CtrStereoEditMeta *CtrStereoMon_EditMeta(void) { return &sMeta; }

void CtrStereoMon_SetStrays(unsigned frame, const uint8_t (*xy)[2], unsigned count)
{
    if (count > CTR_STEREO_STRAYS_MAX) count = CTR_STEREO_STRAYS_MAX;
    if (frame == sStrayFrame && count == sStrayCount && (count == 0 || memcmp(sStrayXY, xy, count * 2) == 0)) return;
    sStrayFrame = frame;
    sStrayCount = count;
    if (count) memcpy(sStrayXY, xy, count * 2);
    ++sStrayVersion;
}

/* The strays' rings: a pixel outside each cell, at the plane's place on
 * screen (the nearest plane's X, so the ring sits over the stack). */
static void DrawStrays(unsigned frame)
{
    const uint32_t ring = (sTick / STRAY_BLINK) & 1 ? C2D_Color32(255, 255, 255, 240) : C2D_Color32(0, 0, 0, 240);
    const float s = sPlaneScale, x0 = sPlaneX[0];

    if (!sEditing || sStrayCount == 0 || frame != sStrayFrame) return;
    for (unsigned q = 0; q < sStrayCount; ++q)
    {
        float x = x0 + sStrayXY[q][0] * s, y = sPlaneY + sStrayXY[q][1] * s;

        C2D_DrawRectSolid(x - 1, y - 1, 0, s + 2, 1, ring);
        C2D_DrawRectSolid(x - 1, y + s, 0, s + 2, 1, ring);
        C2D_DrawRectSolid(x - 1, y, 0, 1, s, ring);
        C2D_DrawRectSolid(x + s, y, 0, 1, s, ring);
    }
}

/* The companion: the other side's picture, flat, in a 128x64 texture (its
 * frames side by side). */
/*
 * The other side of the picture on show (its front, or its back) as a stack
 * of planes of its own, for the versions screen and the Pokédex, which show
 * both sides in 3D. Its picture comes from the game side
 * (CtrStereoMon_SetCompanion, only while those screens are open); its planes
 * from its edit file on the card, else from the pack; composed at once, in
 * MATE_GROUPS planes, into a texture of its own.
 */
#define MATE_GROUPS 4
#define MATE_FRAMES 2
#define MATE_TEX_W (MATE_GROUPS * DIM)
#define MATE_TEX_H (MATE_FRAMES * DIM)
static C3D_Tex sMateTex;
static bool sMateAllocated, sMateReady;
static unsigned sMateFrames;
static uint16_t sMateId;
static uint16_t sMatePicture[MATE_FRAMES][DIM * DIM];
static uint8_t sMatePlanes[CTR_STEREO_MON_MAX_FRAMES * EDIT_FRAME_BYTES];
static Tex3DS_SubTexture sMateTiles[MATE_FRAMES * MATE_GROUPS];

/* The other side's planes: the card's edit, else the pack's. */
static bool MatePlanes(uint16_t id, CtrStereoMonHeader *header)
{
    int index;
    PlanesFile *pf = FileOf(id, &index);
    unsigned planes, frames;

    if (index < 0) return false;
    *header = pf->header;
    if (ReadEdit(id, sMatePlanes, sizeof(sMatePlanes), &planes, &frames))
    {
        header->planes = (uint8_t)planes;
        header->frames = (uint8_t)frames;
        return true;
    }
    if (!ReadPlanesInto(pf, (unsigned)index, sMatePlanes, sizeof(sMatePlanes), &frames)) return false;
    header->frames = (uint8_t)frames;
    return true;
}

static void ComposeMate(void)
{
    CtrStereoMonHeader header;

    sMateReady = false;
    if (sMateId == 0 || sMateFrames == 0 || !MatePlanes(sMateId, &header)) return;
    if (!sMateAllocated)
    {
        if (!C3D_TexInit(&sMateTex, MATE_TEX_W, MATE_TEX_H, GPU_RGBA5551))
        {
            CtrLog_Write(CTR_LOG_ERROR, "stereo: no linear memory for the other side's planes (free=%lu)",
                         (unsigned long)linearSpaceFree());
            return;
        }
        C3D_TexSetFilter(&sMateTex, GPU_NEAREST, GPU_NEAREST);
        C3D_TexSetWrap(&sMateTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        sMateAllocated = true;
    }
    memset(sMateTex.data, 0, MATE_TEX_W * MATE_TEX_H * 2);
    for (unsigned f = 0; f < sMateFrames; ++f)
        for (unsigned g = 0; g < MATE_GROUPS; ++g)
        {
            CtrStereoMon_ComposeTile(sMatePicture[f], sMatePlanes, &header, f < header.frames ? f : 0, MATE_GROUPS, g,
                                     sMateTex.data, MATE_TEX_W, g * DIM, f * DIM, SKIRT);
            sMateTiles[f * MATE_GROUPS + g] = (Tex3DS_SubTexture){DIM, DIM, g * DIM / (float)MATE_TEX_W,
                                                                  1.0f - f * DIM / (float)MATE_TEX_H,
                                                                  (g + 1) * DIM / (float)MATE_TEX_W,
                                                                  1.0f - (f + 1) * DIM / (float)MATE_TEX_H};
        }
    C3D_TexFlush(&sMateTex);
    sMateReady = true;
    CtrLog_Write(CTR_LOG_VIDEO, "stereo: other side %04x as planes (%u planes, %u frames)", sMateId, header.planes, sMateFrames);
}

void CtrStereoMon_SetCompanion(uint16_t id, const uint16_t *picture, unsigned frames)
{
    sMateReady = false;
    sMateId = 0;
    if (picture == NULL || frames == 0 || id == 0) return;
    if (frames > MATE_FRAMES) frames = MATE_FRAMES;
    memcpy(sMatePicture, picture, frames * sizeof(sMatePicture[0]));
    sMateFrames = frames;
    sMateId = id;
    ComposeMate();
    sDrawnValid = false;
}

/* The other side's stack, its top-left corner at (left, top). */
static void DrawMateStack(float left, float top, float scale, float shift, unsigned frame)
{
    const float pivot = CtrStereoDepths_Get().pivot * (MATE_GROUPS - 1), spacing = DEPTH_SPAN / (float)(MATE_GROUPS - 1);
    unsigned f = frame < sMateFrames ? frame : 0;

    if (!sMateReady) return;
    for (unsigned g = 0; g < MATE_GROUPS; ++g)
        C2D_DrawImageAt((C2D_Image){&sMateTex, &sMateTiles[f * MATE_GROUPS + g]}, left + shift * (g - pivot) * spacing, top, 0,
                        NULL, scale, scale);
}

void CtrStereoMon_SetFinalized(bool finalized) { sMeta.finalized = finalized; }

bool CtrStereoMon_IsFinal(uint16_t id)
{
    unsigned bit;
    const uint8_t *marks;

    if (!sEditScanned) CtrStereoMon_EditScan();
    marks = MarkOf(id, &bit, true);
    return marks != NULL && ((marks[bit >> 3] >> (bit & 7)) & 1);
}

void CtrStereoMon_SetAuthor(const char *name)
{
    FILE *f;

    CleanName(name ? name : "", sAuthor);
    sAuthorRead = true;
    mkdir(EDIT_DIR, 0777);
    f = fopen(EDIT_DIR "/author.txt", "w");
    if (f)
    {
        fprintf(f, "%s\n", sAuthor);
        fclose(f);
    }
}

/* Whether picture pixel (x, y) of the frame is on the plane. */
static bool OnPlane(unsigned frame, int x, int y, unsigned plane)
{
    unsigned i, k;

    if (x < 0 || y < 0 || x >= DIM || y >= DIM) return false;
    i = (unsigned)(y * DIM + x);
    k = frame * DIM * DIM + i;
    return sPicture[frame][i] != 0 && ((sPlanes[k >> 1] >> ((k & 1) * 4)) & 15) == plane;
}

/* The outline's runs for the frame and the plane, unless they are known. */
static void OutlineRuns(unsigned frame, unsigned plane)
{
    if (sRunsVersion == sPlanesVersion && sRunsId == sId && sRunsFrame == frame && sRunsPlane == plane)
        return;
    sRunsVersion = sPlanesVersion;
    sRunsId = sId;
    sRunsFrame = frame;
    sRunsPlane = plane;
    sRunCount = 0;
    for (int y = 0; y < DIM; ++y)
        for (int x = 0; x < DIM; ++x)
        {
            bool edge = !OnPlane(frame, x, y, plane)
                     && (OnPlane(frame, x - 1, y, plane) || OnPlane(frame, x + 1, y, plane)
                         || OnPlane(frame, x, y - 1, plane) || OnPlane(frame, x, y + 1, plane));

            if (!edge) continue;
            if (sRunCount && sRuns[sRunCount - 1].y == y && sRuns[sRunCount - 1].x + sRuns[sRunCount - 1].len == x)
                ++sRuns[sRunCount - 1].len;
            else if (sRunCount < sizeof(sRuns) / sizeof(sRuns[0]))
                sRuns[sRunCount++] = (OutlineRun){(uint8_t)x, (uint8_t)y, 1};
        }
}

/* The outline at the plane's own depth, over the whole stack. */
static void DrawOutline(void)
{
    for (unsigned r = 0; r < sRunCount; ++r)
        C2D_DrawRectSolid(sPlaneX[sRunsPlane] + sRuns[r].x * sPlaneScale, sPlaneY + sRuns[r].y * sPlaneScale, 0,
                          sRuns[r].len * sPlaneScale, sPlaneScale, sEditMark);
}

static void EnsureEditDir(void)
{
    mkdir(EDIT_DIR, 0777);
}

/* The edit log's waiting lines (CtrStereoMon_LogEdit). */
static char sLogBuf[2048];
static unsigned sLogLen;
static uint16_t sLogId;

bool CtrStereoMon_SaveEdit(void)
{
    uint8_t header[EDIT_HEADER_BYTES] = {0};
    FILE *f;
    unsigned frames = sShown.frames < sFrames ? sShown.frames : sFrames;
    bool ok;

    if (!sActive) return false;
    EnsureEditDir();
    f = OpenEdit(sId, "bin", "wb");
    if (f == NULL)
    {
        CtrLog_Write(CTR_LOG_ERROR, "stereo: cannot write #%u's edit file", sId);
        return false;
    }
    memcpy(header, CTR_STEREO_EDIT_MAGIC, 8);
    header[8] = (uint8_t)sId;
    header[9] = (uint8_t)(sId >> 8);
    header[10] = sShown.planes;
    header[11] = (uint8_t)frames;
    header[12] = DIM;
    header[14] = DIM;
    {
        uint8_t t[META_BYTES] = {0};
        time_t now = time(NULL);
        uint32_t saved = (uint32_t)now > META_EPOCH ? (uint32_t)now - META_EPOCH : 0;
        uint16_t revision = (uint16_t)(sMeta.revision + 1);

        memcpy(t, META_MAGIC, 8);
        t[8] = sMeta.finalized ? META_FINAL : 0;
        t[9] = (uint8_t)CtrStereoDepths_Get().spread;
        t[10] = (uint8_t)revision;
        t[11] = (uint8_t)(revision >> 8);
        for (unsigned b = 0; b < 4; ++b) t[12 + b] = (uint8_t)(saved >> (8 * b));
        memcpy(t + 16, CtrStereoMon_Author(), strlen(CtrStereoMon_Author()));
        ok = fwrite(header, 1, sizeof(header), f) == sizeof(header)
          && fwrite(sPlanes, 1, frames * EDIT_FRAME_BYTES, f) == frames * EDIT_FRAME_BYTES
          && fwrite(t, 1, sizeof(t), f) == sizeof(t);
        if (ok)
        {
            sMeta.revision = revision;
            sMeta.saved = saved;
            snprintf(sMeta.author, sizeof(sMeta.author), "%s", CtrStereoMon_Author());
        }
    }
    fclose(f);
    if (ok)
    {
        sEdited = true;
        SetMark(sId, true);
        SetMarkOf(sId, true, sMeta.finalized);
        CtrStereoBattle_ForgetPlanes();
        CtrStereoMon_FlushLog();
    }
    CtrLog_Write(ok ? CTR_LOG_FS : CTR_LOG_ERROR, "stereo: id %04x edit %s (r%u by %s%s)", sId, ok ? "saved" : "NOT saved",
                 sMeta.revision, sMeta.author, sMeta.finalized ? ", final" : "");
    return ok;
}

bool CtrStereoMon_ResetEdit(void)
{
    char path[64];
    int index;
    PlanesFile *pf = FileOf(sId, &index);
    unsigned entryFrames = 0;

    if (!sActive) return false;
    if (sLogId == sId) sLogLen = 0;
    EditPath(sId, "bin", path, sizeof(path));
    remove(path);
    EditPath(sId, "log", path, sizeof(path));
    remove(path);
    if (index < 0 || !ReadPlanesInto(pf, (unsigned)index, sPlanes, sizeof(sPlanes), &entryFrames)) return false;
    sShown = pf->header;
    sShown.frames = (uint8_t)entryFrames;
    sEdited = false;
    SetMark(sId, false);
    SetMarkOf(sId, true, false);
    memset(&sMeta, 0, sizeof(sMeta));
    CtrStereoBattle_ForgetPlanes();
    sTexReady = false;
    return true;
}

void CtrStereoMon_EditScan(void)
{
    DIR *d = opendir(EDIT_DIR);
    struct dirent *e;

    memset(sEditMarks, 0, sizeof(sEditMarks));
    memset(sBackMarks, 0, sizeof(sBackMarks));
    memset(sFinalMarks, 0, sizeof(sFinalMarks));
    sEditScanned = true;
    if (d == NULL) return;
    while ((e = readdir(d)) != NULL)
    {
        unsigned n = 0, k = 0, kind = 0;

        if (e->d_name[0] == 'b') kind = CTR_STEREO_ID_BACK, k = 1;
        else if (e->d_name[0] == 't') kind = CTR_STEREO_ID_TRAINER, k = 1;
        else if (e->d_name[0] == 'p') kind = CTR_STEREO_ID_PLAYER, k = 1;
        for (; e->d_name[k] >= '0' && e->d_name[k] <= '9'; ++k) n = n * 10 + (unsigned)(e->d_name[k] - '0');
        if (k > (kind ? 1u : 0u) && strcmp(e->d_name + k, ".bin") == 0 && n < 0x4000 && (kind || n > 0))
        {
            uint16_t id = (uint16_t)(kind | n);
            uint8_t header[EDIT_HEADER_BYTES];
            CtrStereoEditMeta meta;
            FILE *f;

            SetMark(id, true);
            /* Its trailer, for the finished mark. */
            f = OpenEdit(id, "bin", "rb");
            if (f == NULL) continue;
            if (fread(header, 1, sizeof(header), f) == sizeof(header) && ReadMeta(f, header[11], &meta) && meta.finalized)
                SetMarkOf(id, true, true);
            fclose(f);
        }
    }
    closedir(d);
}

static unsigned sReloads;

/* The picture on show read from the card again (its edit file, else the pack's). */
static void ReloadShown(void)
{
    if (!LoadEdit(sId))
    {
        int index;
        PlanesFile *pf = FileOf(sId, &index);
        unsigned entryFrames = 0;

        if (index >= 0 && ReadPlanesInto(pf, (unsigned)index, sPlanes, sizeof(sPlanes), &entryFrames))
        {
            sShown = pf->header;
            sShown.frames = (uint8_t)entryFrames;
        }
        sEdited = false;
    }
    else sEdited = true;
    sTexReady = false;
    ++sPlanesVersion;
    ++sReloads;
    if (sMateId) ComposeMate();
}

void CtrStereoMon_EditsChanged(void)
{
    CtrStereoMon_EditScan();
    CtrStereoBattle_ForgetPlanes();
    /* The gallery's picture comes from the card again; one in the editor
     * keeps what the editor holds (the card's copy is replaced under it,
     * the one before kept in stereo/backup). */
    if (sActive && !sEditing) ReloadShown();
    else if (sMateId) ComposeMate();
}

void CtrStereoMon_ReloadShown(void)
{
    CtrStereoMon_EditScan();
    CtrStereoBattle_ForgetPlanes();
    if (sActive) ReloadShown();
}

unsigned CtrStereoMon_ReloadCount(void) { return sReloads; }

bool CtrStereoMon_HasEdit(uint16_t id)
{
    unsigned bit;
    const uint8_t *marks;

    if (!sEditScanned) CtrStereoMon_EditScan();
    marks = MarkOf(id, &bit, false);
    return marks != NULL && ((marks[bit >> 3] >> (bit & 7)) & 1);
}

/* The edit log's lines wait here and go to the card with the picture's
 * save (or when the buffer fills, another picture logs, or the game ends):
 * opening the file for every stroke stalled the drawing. */

void CtrStereoMon_FlushLog(void)
{
    FILE *f;

    if (sLogLen == 0) return;
    EnsureEditDir();
    f = OpenEdit(sLogId, "log", "a");
    if (f)
    {
        fwrite(sLogBuf, 1, sLogLen, f);
        fclose(f);
    }
    sLogLen = 0;
}

void CtrStereoMon_LogEdit(const char *line)
{
    size_t n;

    if (!sActive || line == NULL) return;
    n = strlen(line) + 1;
    if (sLogLen && (sLogId != sId || sLogLen + n > sizeof(sLogBuf))) CtrStereoMon_FlushLog();
    if (n > sizeof(sLogBuf)) return;
    sLogId = sId;
    memcpy(sLogBuf + sLogLen, line, n - 1);
    sLogBuf[sLogLen + n - 1] = '\n';
    sLogLen += (unsigned)n;
}

/* ── The texture ─────────────────────────────────────────────────────── */

/* The texture for the Pokémon shown with the planes chosen: its layout set
 * and its tiles queued for composing whenever either changes (sTexReady
 * false), its memory made once; false without the memory for it. */
static bool TextureReady(void)
{
    unsigned groups = Groups(), frames = sFrames, tiles, cols;

    if (sTexReady) return true;
    if (!sTexAllocated)
    {
        static const unsigned widths[] = {TEX_MAX_W, 2 * DIM, DIM};
        unsigned k;

        for (k = 0; k < 3; ++k)
            if (C3D_TexInit(&sTex, (uint16_t)widths[k], TEX_MAX_H, GPU_RGBA5551)) break;
        if (k == 3)
        {
            CtrLog_Write(CTR_LOG_ERROR, "stereo: no linear memory for even a %ux%u texture (free=%lu)", DIM, TEX_MAX_H,
                         (unsigned long)linearSpaceFree());
            return false;
        }
        sTexW = widths[k];
        sTexTiles = (sTexW / DIM) * (TEX_MAX_H / DIM);
        C3D_TexSetFilter(&sTex, GPU_NEAREST, GPU_NEAREST);
        C3D_TexSetWrap(&sTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        sTexAllocated = true;
        CtrLog_Write(CTR_LOG_VIDEO, "stereo: a %ux%u texture, %u tiles (linear free=%lu)", sTexW, TEX_MAX_H, sTexTiles,
                     (unsigned long)linearSpaceFree());
    }
    if (groups > sTexTiles) groups = sTexTiles;
    if (frames * groups > sTexTiles) frames = 1;
    tiles = frames * groups;
    cols = (tiles + TEX_MAX_H / DIM - 1) / (TEX_MAX_H / DIM);
    if (cols < 1) cols = 1;
    /* Every texel written, so that nothing of what the memory held is read. */
    memset(sTex.data, 0, sTexW * TEX_MAX_H * 2);
    C3D_TexFlush(&sTex);
    sTexCols = cols;
    sTexFrames = frames;
    sTexGroups = groups;
    for (unsigned tile = 0; tile < tiles; ++tile)
    {
        unsigned x = (tile % cols) * DIM, y = (tile / cols) * DIM;

        sTiles[tile] = (Tex3DS_SubTexture){DIM, DIM, x / (float)sTexW, 1.0f - y / (float)TEX_MAX_H,
                                           (x + DIM) / (float)sTexW, 1.0f - (y + DIM) / (float)TEX_MAX_H};
    }
    sTexReady = true;
    sComposed = 0;
    sToCompose = tiles;
    CtrLog_Write(CTR_LOG_VIDEO, "stereo: %u planes, %u frame(s), %u tiles %u across", groups, frames, tiles, cols);
    return true;
}

/* The next tiles into the texture; a Pokémon is drawn only once all its
 * tiles are there. */
/* The editor's VIEW LAYERS: every pixel in its plane's colour, its own
 * light and shade kept a little (RGBA5551, as the picture). */
static void TintLayers(void)
{
    static const uint16_t colours[CTR_STEREO_EDIT_PLANES] = CTR_STEREO_EDIT_COLOURS;

    for (unsigned f = 0; f < sFrames && f < sTexFrames; ++f)
        for (unsigned i = 0; i < DIM * DIM; ++i)
        {
            uint16_t p = sPicture[f][i];
            unsigned pf = f < sShown.frames ? f : sShown.frames - 1u;
            unsigned k = pf * DIM * DIM + i, v = (sPlanes[k >> 1] >> ((k & 1) * 4)) & 15, r, g, b, light, cr, cg, cb;

            if (p == 0 || v >= CTR_STEREO_EDIT_PLANES || v >= sShown.planes)
            {
                sTinted[f][i] = p;
                continue;
            }
            r = (p >> 11) & 31;
            g = (p >> 6) & 31;
            b = (p >> 1) & 31;
            light = (2 * r + 5 * g + b) / 8; /* 0..31 */
            cr = (colours[v] >> 11) & 31;
            cg = ((colours[v] >> 5) & 63) >> 1;
            cb = colours[v] & 31;
            /* Three quarters the plane's colour at the pixel's light, a quarter its own. */
            r = (cr * (12 + light) / 43 * 3 + r) / 4;
            g = (cg * (12 + light) / 43 * 3 + g) / 4;
            b = (cb * (12 + light) / 43 * 3 + b) / 4;
            sTinted[f][i] = (uint16_t)((r << 11) | (g << 6) | (b << 1) | 1);
        }
}

static void ComposeSome(void)
{
    unsigned groups = Groups();
    const bool tinted = sEditing && sEditLayers;

    if (tinted && sComposed == 0) TintLayers();
    for (unsigned n = 0; n < TILES_PER_FRAME && sComposed < sToCompose; ++n, ++sComposed)
        CtrStereoMon_ComposeTile(tinted ? sTinted[sComposed / groups] : sPicture[sComposed / groups], sPlanes, &sShown, sComposed / groups, groups,
                                 sComposed % groups, sTex.data, sTexW,
                                 (sComposed % sTexCols) * DIM, (sComposed / sTexCols) * DIM, SKIRT);
    C3D_TexFlush(&sTex);
}

/* Between showings the texture stays allocated (see above); its layout is
 * set again on the next. */
void CtrStereoMon_Idle(void)
{
    sTexReady = false;
    sDrawnValid = false;
}

/* The voting view's filmstrip (3ds_stereo_strip.c) borrows the texture:
 * nothing is composed into it meanwhile, and the picture on show is
 * composed again once it is back. */
static bool sLent;

void *CtrStereoMon_LendTexture(bool lend, unsigned *width, unsigned *height)
{
    if (!lend)
    {
        sLent = false;
        sTexReady = false;
        sDrawnValid = false;
        return NULL;
    }
    if (!sTexAllocated && !TextureReady()) return NULL;
    sLent = true;
    sTexReady = false;
    sDrawnValid = false;
    if (width) *width = sTexW;
    if (height) *height = TEX_MAX_H;
    return &sTex;
}

void CtrStereoMon_Invalidate(void) { sDrawnValid = false; }

const uint16_t *CtrStereoMon_SidePicture(uint16_t id)
{
    if (!sActive || id == 0) return NULL;
    if (id == sId && sFrames) return sPicture[0];
    if (id == sMateId && sMateFrames) return sMatePicture[0];
    return NULL;
}

/* ── Text: a 3x5 font, drawn as rectangles ──────────────────────────── */

#define GLYPH_PIXEL 2
#define GLYPH_ADVANCE (4 * GLYPH_PIXEL)

static const char *const sGlyphs[] = {
    "111101101101111", "010110010010111", "111001111100111", "111001111001111", "101101111001001",
    "111100111001111", "111100111101111", "111001001001001", "111101111101111", "111101111001111",
    "010101111101101", "110101110101110", "111100100100111", "110101101101110", "111100111100111",
    "111100111100100", "111100101101111", "101101111101101", "111010010010111", "001001001101111",
    "101101110101101", "100100100100111", "101111111101101", "110101101101101", "111101101101111",
    "111101111100100", "111101101111001", "110101110101101", "111100111001111", "111010010010010",
    "101101101101111", "101101101101010", "101101111111101", "101101010101101", "101101010010010",
    "111001010100111",
    /* . - : < > ^ v % / */
    "000000000000010", "000000111000000", "000010000010000", "001010100010001", "100010001010100",
    "010101000000000", "000000000101010", "101001010100101", "001001010100100",
    /* + */
    "000010111010000",
};

static int GlyphOf(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return 10 + c - 'A';
    if (c >= 'a' && c <= 'z') return 10 + c - 'a';
    switch (c)
    {
    case '.': return 36;
    case '-': return 37;
    case ':': return 38;
    case '<': return 39;
    case '>': return 40;
    case '^': return 41;
    case 'v': return 42;
    case '%': return 43;
    case '/': return 44;
    case '+': return 45;
    default: return -1;
    }
}

static void DrawTextSized(float x, float y, const char *text, uint32_t colour, int px)
{
    for (; *text; ++text, x += 4 * px)
    {
        int g = *text == 'v' ? 42 : GlyphOf(*text);

        if (g < 0) continue;
        for (unsigned p = 0; p < 15; ++p)
            if (sGlyphs[g][p] == '1')
                C2D_DrawRectSolid(x + (p % 3) * px, y + (p / 3) * px, 0, px, px, colour);
    }
}

static void DrawText(float x, float y, const char *text, uint32_t colour)
{
    DrawTextSized(x, y, text, colour, GLYPH_PIXEL);
}

/* ── Drawing ─────────────────────────────────────────────────────────── */

/* A back's stack is shallower in the battle: seen from behind and drawn
 * large at the bottom of the battle, it tore apart at a front's depth. Only
 * the editor's BATTLE view keeps that (sBattleView): looked at alone or
 * beside its front (the Pokédex, the versions screen), a back is judged as
 * a front is, at the same depth and the same place. A tester found the two
 * sides wanted different slider positions otherwise. */
static bool BackSide(uint16_t id)
{
    return (id & 0xC000) == CTR_STEREO_ID_BACK || (id & 0xC000) == CTR_STEREO_ID_PLAYER;
}

static bool sBattleView;
static float DepthOf(uint16_t id) { return sBattleView && BackSide(id) ? BACK_DEPTH : 1.0f; }

/*
 * Every view stands a picture as the battle does (CtrStereoDepths, the
 * battle's 3ds_video.c and 3ds_stereo_battle.c), in screen pixels, so the
 * depth seen while editing is the depth a battle gives: the stack spans
 * the preset's planes at the battle's magnification whatever the view's,
 * the opponent's side and the player's stand at the preset's depths, and
 * the scenery lies the preset's back depth behind. `sign` is the eye's (+1 the
 * left), `slider` the 3D slider's 0..1.
 */
/* The eye's shift per sprite pixel of the stack's depth (DrawStack): every
 * picture's planes span the 3D PRESET's planes. */
static float StackShift(float sign, float slider, uint16_t id)
{
    return sign * slider * CtrStereoDepths_Get().spread * CTR_BATTLE_ZOOM / DEPTH_SPAN * DepthOf(id);
}

/* The eye's shift of the picture's own depth, where PLANES SIT stands the stack. */
static float PictureShift(float sign, float slider, uint16_t id)
{
    const CtrStereoDepths d = CtrStereoDepths_Get();

    return -sign * slider * (sBattleView && BackSide(id) ? d.own : d.mon);
}

/* The eye's shift of the scenery behind. */
static float BackdropShift(float sign, float slider)
{
    return -sign * slider * CtrStereoDepths_Get().back;
}

/* The stack of planes for one eye at `scale` screen pixels per sprite
 * pixel, `tint` over every plane or NULL. */
/* The stack with its top-left corner at (left, top), each plane moved
 * `shift` screen pixels (with the eye's sign) per sprite pixel of depth. */
static void DrawStack(float left, float top, float scale, float shift, unsigned frame, const C2D_ImageTint *tint)
{
    const unsigned groups = Groups();
    const float pivot = CtrStereoDepths_Get().pivot * (groups - 1);
    /* Thicker slices stand further apart: the stack keeps its depth. */
    const float spacing = DEPTH_SPAN / (float)(groups - 1);

    if (!(sTexReady && sToCompose && sComposed == sToCompose)) return;
    for (unsigned plane = 0; plane < groups; ++plane)
    {
        /* The plane's depth, towards the viewer, in sprite pixels. */
        float z = (plane - pivot) * spacing;
        float x = left + shift * z;
        C2D_Image image = {&sTex, &sTiles[(frame < sTexFrames ? frame : 0) * groups + plane]};

        sPlaneX[plane] = x;
        sPlaneY = top;
        sPlaneScale = scale;
        C2D_DrawImageAt(image, x, top, 0, tint, scale, scale);
    }
}

static void DrawPlanes(float sign, float slider, unsigned frame, const C2D_ImageTint *tint, float scale)
{
    DrawStack(CENTRE_X - DIM / 2 * scale + PictureShift(sign, slider, sId), CENTRE_Y - DIM / 2 * scale, scale,
              StackShift(sign, slider, sId), frame, tint);
}


/* ── The Pokédex's backdrop ──────────────────────────────────────────── */

static C3D_Tex sBackdrop;
static bool sBackdropAllocated, sBackdropFailed, sBackdropReady;
static unsigned sBackdropRows;
/* The sky: the backdrop's top-left pixel, for the window above it. */
static uint32_t sSky;

void CtrStereoMon_BackdropBegin(void)
{
    sBackdropReady = false;
    sBackdropRows = 0;
    if (sBackdropAllocated || sBackdropFailed) return;
    if (!C3D_TexInit(&sBackdrop, BACKDROP_W, BACKDROP_H, GPU_RGBA5551))
    {
        sBackdropFailed = true;
        CtrLog_Write(CTR_LOG_ERROR, "stereo: no linear memory for the backdrop (free=%lu)",
                     (unsigned long)linearSpaceFree());
        return;
    }
    C3D_TexSetFilter(&sBackdrop, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&sBackdrop, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    sBackdropAllocated = true;
}

void CtrStereoMon_BackdropRow(unsigned y, const uint16_t *row)
{
    uint16_t *texels = sBackdrop.data;

    if (!sBackdropAllocated || y >= BACKDROP_H || row == NULL) return;
    if (y == 0)
        sSky = C2D_Color32(((row[0] >> 11) & 31) * 255 / 31, ((row[0] >> 6) & 31) * 255 / 31,
                           ((row[0] >> 1) & 31) * 255 / 31, 255);
    for (unsigned x = 0; x < BACKDROP_W; ++x)
        texels[CtrVideo_Texel(x, y, BACKDROP_W)] = row[x];
    if (++sBackdropRows == BACKDROP_H)
    {
        C3D_TexFlush(&sBackdrop);
        sBackdropReady = true;
    }
}

/* ── The Pokédex ────────────────────────────────────────────────────── */

/*
 * The Pokédex as the device itself: its red shell across the top screen,
 * the hinge along the top, the blue lens and the lights at the left, the
 * buttons and the pad below and beside its screen window; in the window
 * the species' terrain a little behind the screen and the Pokémon's planes
 * at 2x (every pixel an exact 2x2) at their own depths, its name on a
 * strip along the window's bottom. No turn.
 */
static void DrawDex(float sign, float slider, unsigned frame)
{
    const uint32_t red = C2D_Color32(222, 48, 36, 255), redDark = C2D_Color32(150, 26, 20, 255);
    const uint32_t redLight = C2D_Color32(240, 92, 70, 255);
    const uint32_t grey = C2D_Color32(52, 52, 58, 255), greyLight = C2D_Color32(190, 190, 196, 255);
    const int x0 = DEX_WIN_X, y0 = DEX_WIN_Y, w = DEX_WIN_W, h = DEX_WIN_H;
    char line[64];

    /* The shell, lit from the top left; the hinge; the lens and the lights. */
    C2D_DrawRectangle(0, 0, 0, CTR_GAME_WIDTH, CTR_GAME_HEIGHT, redLight, red, red, redDark);
    C2D_DrawRectSolid(0, 0, 0, CTR_GAME_WIDTH, 12, redDark);
    C2D_DrawRectSolid(0, 12, 0, CTR_GAME_WIDTH, 2, redLight);
    /* The lens: a disc of stacked bars (the ellipse primitive draws a box
     * in the emulator), lit at its top left. */
    for (int r = -17; r <= 17; ++r)
    {
        float half = sqrtf(17.0f * 17.0f - (float)(r * r));

        C2D_DrawRectSolid(28 - half, 58 + r, 0, 2 * half, 1, C2D_Color32(20, 40, 90, 255));
        if (r > -15 && r < 15)
        {
            float inner = sqrtf(14.0f * 14.0f - (float)(r * r));

            C2D_DrawRectSolid(28 - inner, 58 + r, 0, 2 * inner, 1,
                              r < -4 ? C2D_Color32(90, 160, 240, 255) : C2D_Color32(40, 110, 220, 255));
        }
    }
    C2D_DrawRectSolid(20, 48, 0, 8, 4, C2D_Color32(200, 225, 255, 255));
    C2D_DrawRectSolid(54, 44, 0, 6, 6, C2D_Color32(60, 220, 80, 255));
    C2D_DrawRectSolid(54, 54, 0, 6, 6, C2D_Color32(240, 200, 40, 255));
    C2D_DrawRectSolid(54, 64, 0, 6, 6, C2D_Color32(240, 60, 60, 255));
    /* The buttons below the window and the pad beside it. */
    C2D_DrawRectSolid(x0 + 40, y0 + h + 10, 0, 44, 10, grey);
    C2D_DrawRectSolid(x0 + 96, y0 + h + 10, 0, 44, 10, grey);
    C2D_DrawRectSolid(x0 + 42, y0 + h + 12, 0, 40, 3, greyLight);
    C2D_DrawRectSolid(x0 + 98, y0 + h + 12, 0, 40, 3, greyLight);
    C2D_DrawRectSolid(356, 186, 0, 14, 42, grey);
    C2D_DrawRectSolid(342, 200, 0, 42, 14, grey);
    C2D_DrawRectSolid(361, 191, 0, 4, 32, greyLight);
    C2D_DrawRectSolid(347, 205, 0, 32, 4, greyLight);
    /* The window: a dark bevel, a light rim, the screen. */
    C2D_DrawRectSolid(x0 - 6, y0 - 6, 0, w + 12, h + 12, grey);
    C2D_DrawRectSolid(x0 - 2, y0 - 2, 0, w + 4, h + 4, greyLight);
    C2D_DrawRectSolid(x0, y0, 0, w, h, C2D_Color32(150, 190, 120, 255));
    C2D_Flush();
    /* The screen's picture, clipped to the window: the terrain a little
     * behind the screen (the left eye sees it shifted left), the Pokémon
     * over it at its planes' depths. */
    C3D_SetScissor(GPU_SCISSOR_NORMAL, (unsigned)(CTR_GAME_HEIGHT - y0 - h), (unsigned)x0,
                   (unsigned)(CTR_GAME_HEIGHT - y0), (unsigned)(x0 + w));
    if (sBackdropReady)
    {
        const Tex3DS_SubTexture whole = {BACKDROP_W, BACKDROP_H, 0.0f, 1.0f, 1.0f, 0.0f};
        /* The mound under the Pokémon's feet, a little above the window's
         * name strip; the sky fills what is left above the picture. */
        const float bx = DEX_WIN_CX - BACKDROP_MOUND_X * BACKDROP_SCALE + BackdropShift(sign, slider);
        const float by = (CENTRE_Y + DIM / 2 * DEX_SCALE - 6) - BACKDROP_MOUND_Y * BACKDROP_SCALE;

        C2D_DrawRectSolid(x0, y0, 0, w, h, sSky);
        C2D_DrawImageAt((C2D_Image){&sBackdrop, &whole}, bx, by, 0, NULL, BACKDROP_SCALE, BACKDROP_SCALE);
    }
    DrawPlanes(sign, slider, frame, NULL, DEX_SCALE);
    C2D_Flush();
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
    /* The name strip. */
    C2D_DrawRectSolid(x0, y0 + h - 16, 0, w, 16, C2D_Color32(20, 30, 20, 170));
    snprintf(line, sizeof(line), "NO.%03u %s", sId, sName);
    DrawText(x0 + 6, y0 + h - 12, line, C2D_Color32(230, 245, 220, 255));
    DrawText(x0 + w - 6 - GLYPH_ADVANCE * 9, y0 + h - 12, "X EDIT 3D", C2D_Color32(180, 200, 170, 255));
}

/* ── The editor's top screen ────────────────────────────────────────── */

/*
 * The help sheet: the editor's words in the game's own small font, drawn
 * by the editor on the game side and handed over cell by cell
 * (CtrStereoMon_HelpCell), two cells a row in a 256x128 LA4 texture
 * (32 KiB of linear memory, made once); drawn here with a colour and the
 * game's one-pixel shadow.
 */
#define HELP_TEX_W 256
#define HELP_TEX_H 256
static C3D_Tex sHelpTex;
static bool sHelpAllocated, sHelpFailed;
static uint8_t sHelpWidth[CTR_HELP_CELLS];

void CtrStereoMon_SetChooser(bool chooser) { sChooser = chooser; }
void CtrStereoMon_SetVersionsView(bool on) { sVersionsView = on; }

void CtrStereoMon_HelpCell(unsigned cell, const uint8_t *alpha, unsigned width)
{
    uint8_t *texels;
    unsigned cx, cy;

    if (cell >= CTR_HELP_CELLS || alpha == NULL || sHelpFailed) return;
    ++sHelpVersion;
    if (!sHelpAllocated)
    {
        if (!C3D_TexInit(&sHelpTex, HELP_TEX_W, HELP_TEX_H, GPU_LA4))
        {
            sHelpFailed = true;
            CtrLog_Write(CTR_LOG_ERROR, "stereo edit: no linear memory for the help sheet (free=%lu)",
                         (unsigned long)linearSpaceFree());
            return;
        }
        C3D_TexSetFilter(&sHelpTex, GPU_NEAREST, GPU_NEAREST);
        C3D_TexSetWrap(&sHelpTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        memset(sHelpTex.data, 0, HELP_TEX_W * HELP_TEX_H);
        sHelpAllocated = true;
    }
    texels = sHelpTex.data;
    cx = (cell % 2) * CTR_HELP_CELL_W;
    cy = (cell / 2) * CTR_HELP_CELL_H;
    for (unsigned y = 0; y < CTR_HELP_CELL_H; ++y)
        for (unsigned x = 0; x < CTR_HELP_CELL_W; ++x)
            texels[CtrVideo_Texel(cx + x, cy + y, HELP_TEX_W)] = alpha[y * CTR_HELP_CELL_W + x] ? 0xFF : 0x00;
    sHelpWidth[cell] = (uint8_t)(width > CTR_HELP_CELL_W ? CTR_HELP_CELL_W : width);
    C3D_TexFlush(&sHelpTex);
}

static int HelpWidth(unsigned cell) { return cell < CTR_HELP_CELLS && sHelpAllocated ? sHelpWidth[cell] : 0; }

/* A cell at (x, y) in `colour`, over the game's dark shadow a pixel down and right. */
static void DrawHelpCell(unsigned cell, float x, float y, uint32_t colour)
{
    unsigned cx = (cell % 2) * CTR_HELP_CELL_W, cy = (cell / 2) * CTR_HELP_CELL_H, w = sHelpWidth[cell];
    Tex3DS_SubTexture sub;
    C2D_Image image = {&sHelpTex, &sub};
    C2D_ImageTint tint;

    if (cell >= CTR_HELP_CELLS || !sHelpAllocated || w == 0) return;
    sub = (Tex3DS_SubTexture){(uint16_t)w, CTR_HELP_CELL_H, cx / (float)HELP_TEX_W, 1.0f - cy / (float)HELP_TEX_H,
                              (cx + w) / (float)HELP_TEX_W, 1.0f - (cy + CTR_HELP_CELL_H) / (float)HELP_TEX_H};
    C2D_PlainImageTint(&tint, C2D_Color32(30, 32, 38, 255), 1.0f);
    C2D_DrawImageAt(image, x + 1, y + 1, 0, &tint, 1.0f, 1.0f);
    C2D_PlainImageTint(&tint, colour, 1.0f);
    C2D_DrawImageAt(image, x, y, 0, &tint, 1.0f, 1.0f);
}

/* A pixel-art rounded box, a 1-pixel rim, the corners cut by one pixel. */
static void PixelBox(int x, int y, int w, int h, uint32_t rim, uint32_t face)
{
    C2D_DrawRectSolid(x + 1, y, 0, w - 2, h, rim);
    C2D_DrawRectSolid(x, y + 1, 0, w, h - 2, rim);
    C2D_DrawRectSolid(x + 1, y + 1, 0, w - 2, h - 2, face);
}

/*
 * The console's controls as little pictures beside the help: the author's
 * own (3ds_button_sprites.h, 24 px), uploaded once into a 512x32 texture
 * and drawn from it; the touch screen is drawn here.
 */
#define BTN_TEX_W 512
#define BTN_TEX_H 32
static C3D_Tex sButtonTex;
static bool sButtonTexReady;

static bool ButtonTexture(void)
{
    uint16_t *texels;

    if (sButtonTexReady) return true;
    if (!C3D_TexInit(&sButtonTex, BTN_TEX_W, BTN_TEX_H, GPU_RGBA5551))
    {
        CtrLog_Write(CTR_LOG_ERROR, "stereo: no linear memory for the buttons' texture");
        return false;
    }
    C3D_TexSetFilter(&sButtonTex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&sButtonTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    texels = sButtonTex.data;
    memset(texels, 0, BTN_TEX_W * BTN_TEX_H * 2);
    for (unsigned b = 0; b < BTN_COUNT - 1; ++b)
        for (unsigned y = 0; y < BTN_SIZE; ++y)
            for (unsigned x = 0; x < BTN_SIZE; ++x)
                texels[CtrVideo_Texel(b * BTN_SIZE + x, y, BTN_TEX_W)] = sButtonSprites[b][y * BTN_SIZE + x];
    C3D_TexFlush(&sButtonTex);
    sButtonTexReady = true;
    return true;
}

static int ButtonWidth(int kind) { return kind == BTN_TOUCH ? 20 : BTN_SIZE; }
static int ButtonHeight(int kind) { return kind == BTN_TOUCH ? 20 : BTN_SIZE; }

static void DrawButton(int kind, int x, int y)
{
    Tex3DS_SubTexture sub;
    C2D_Image image = {&sButtonTex, &sub};
    float u0;

    if (kind == BTN_TOUCH)
    {
        /* The touch screen with the stylus's point on it. */
        const uint32_t rim = C2D_Color32(248, 248, 250, 255), face = C2D_Color32(62, 64, 70, 255);

        PixelBox(x, y + 3, 20, 14, rim, face);
        C2D_DrawRectSolid(x + 8, y + 8, 0, 4, 4, C2D_Color32(250, 200, 40, 255));
        C2D_DrawRectSolid(x + 11, y, 0, 2, 7, rim);
        return;
    }
    if (!ButtonTexture()) return;
    u0 = kind * BTN_SIZE / (float)BTN_TEX_W;
    sub = (Tex3DS_SubTexture){BTN_SIZE, BTN_SIZE, u0, 1.0f, u0 + BTN_SIZE / (float)BTN_TEX_W, 1.0f - BTN_SIZE / (float)BTN_TEX_H};
    C2D_DrawImageAt(image, x, y, 0, NULL, 1.0f, 1.0f);
}

/* A line of help: the control (with HOLD after it for the picker), then
 * its words beside it; the right-hand column has the control at the
 * screen's edge and the words to its left. */
typedef struct { int button; bool hold; unsigned cell; } Help;
#define HELP_ROW 20

static void DrawHelp(bool right, int y, const Help *help, unsigned count)
{
    const uint32_t words = C2D_Color32(225, 230, 240, 255);

    for (unsigned i = 0; i < count; ++i, y += HELP_ROW)
    {
        const Help *h = &help[i];
        /* START and SELECT are the same shaded circle: their name to its left. */
        int label = h->button == BTN_START ? CTR_HELP_WORD_START : h->button == BTN_SELECT ? CTR_HELP_WORD_SELECT : -1;
        int labelW = label >= 0 ? HelpWidth((unsigned)label) + 3 : 0;
        int controls = labelW + ButtonWidth(h->button) + (h->hold ? 4 + HelpWidth(CTR_HELP_HOLD) : 0);
        int x0 = right ? CTR_GAME_WIDTH - 6 - controls : 6, bx = x0 + labelW, wide = HelpWidth(h->cell);

        if (label >= 0) DrawHelpCell((unsigned)label, x0, y + 3, words);
        /* The controls sit in the middle of the row's 18 pixels. */
        DrawButton(h->button, bx, y + (18 - ButtonHeight(h->button)) / 2);
        if (h->hold) DrawHelpCell(CTR_HELP_HOLD, bx + ButtonWidth(h->button) + 4, y + 3, words);
        /* The words beside the whole group (START's and SELECT's names
         * included), never over them. */
        DrawHelpCell(h->cell, right ? x0 - 8 - wide : x0 + controls + 8, y + 3, words);
    }
}

/*
 * The editor's top screen: the picture over a grid at the screen's depth
 * with a shadow under its feet, its name and frame at the top left, what is
 * being edited at the top right, and the help in two columns as the hands
 * hold the console: the left-hand controls down the left, the right-hand
 * ones down the right. Planes are numbered for people front to back: 1 the
 * nearest.
 */
/*
 * The battle view: the picture where a battle puts it, over the species'
 * battle backdrop, the other side of it (flat) in its own place, and the
 * battle's boxes mocked up: the two HP boxes and the text box. GBA
 * pixels at 1.5x, the frame's 240 columns in the middle of the 400.
 */
#define BATTLE_SCALE 1.5f
#define BATTLE_X0 ((CTR_GAME_WIDTH - 240 * BATTLE_SCALE) / 2)
/* The 64x64 pictures' top-left corners in GBA pixels: the opponent's
 * front, the player's back (where the game's own sprites stand). */
#define BATTLE_FOE_X 144
#define BATTLE_FOE_Y 20
#define BATTLE_OWN_X 32
#define BATTLE_OWN_Y 60
static float GbaX(float x) { return BATTLE_X0 + x * BATTLE_SCALE; }
static float GbaY(float y) { return y * BATTLE_SCALE; }

static void DrawBattleBox(float x, float y, float w, float h, const char *name, bool foe)
{
    const uint32_t rim = C2D_Color32(40, 44, 56, 255), face = C2D_Color32(244, 244, 236, 255);
    const uint32_t bar = C2D_Color32(82, 86, 92, 255), hp = C2D_Color32(96, 200, 80, 255);
    float bx = x + 8, by = y + h - 10;

    PixelBox((int)x, (int)y, (int)w, (int)h, rim, face);
    DrawText(x + 8, y + 5, name, C2D_Color32(60, 64, 72, 255));
    DrawText(x + w - 8 - GLYPH_ADVANCE * 4, y + 5, "L100", C2D_Color32(60, 64, 72, 255));
    C2D_DrawRectSolid(bx, by, 0, w - 16, 5, bar);
    C2D_DrawRectSolid(bx + 1, by + 1, 0, w - 18, 3, hp);
    (void)foe;
}

static void DrawBattleEye(float sign, float slider, unsigned frame)
{
    const bool back = (sId & 0xC000) != 0;
    const float ownX = GbaX(BATTLE_OWN_X), ownY = GbaY(BATTLE_OWN_Y), foeX = GbaX(BATTLE_FOE_X), foeY = GbaY(BATTLE_FOE_Y);
    char line[48];

    C2D_DrawRectSolid(0, 0, 0, CTR_GAME_WIDTH, CTR_GAME_HEIGHT, sBackdropReady ? sSky : C2D_Color32(62, 64, 70, 255));
    if (sBackdropReady)
    {
        const Tex3DS_SubTexture whole = {BACKDROP_W, BACKDROP_H, 0.0f, 1.0f, 1.0f, 0.0f};

        /* The terrain a little behind the screen. */
        C2D_DrawImageAt((C2D_Image){&sBackdrop, &whole}, GbaX(-8) + BackdropShift(sign, slider), GbaY(0), 0, NULL,
                        BATTLE_SCALE, BATTLE_SCALE);
    }
    /* The text box along the bottom. */
    C2D_DrawRectSolid(0, GbaY(112), 0, CTR_GAME_WIDTH, CTR_GAME_HEIGHT - GbaY(112), C2D_Color32(40, 48, 72, 255));
    C2D_DrawRectSolid(GbaX(0), GbaY(114), 0, 240 * BATTLE_SCALE, GbaY(44), C2D_Color32(232, 232, 224, 255));
    C2D_DrawRectSolid(GbaX(2), GbaY(116), 0, 236 * BATTLE_SCALE, GbaY(40), C2D_Color32(56, 80, 128, 255));
    snprintf(line, sizeof(line), "WHAT WILL %s DO?", sName);
    DrawText(GbaX(10), GbaY(124), line, C2D_Color32(240, 240, 240, 255));
    /* The other side, flat, then the planes in their place. */
    sBattleView = true;
    DrawMateStack((back ? foeX : ownX) + PictureShift(sign, slider, sMateId), back ? foeY : ownY, BATTLE_SCALE,
                  StackShift(sign, slider, sMateId), frame);
    DrawStack((back ? ownX : foeX) + PictureShift(sign, slider, sId), back ? ownY : foeY, BATTLE_SCALE,
              StackShift(sign, slider, sId), frame, NULL);
    sBattleView = false;
    if (sEditOutline && sEditPlane < Groups() && sTexReady && sComposed == sToCompose)
    {
        OutlineRuns(frame, sEditPlane);
        DrawOutline();
    }
    DrawStrays(frame);
    /* The HP boxes: the opponent's at the top left, the player's at the right. */
    DrawBattleBox(GbaX(20), GbaY(14), 100 * BATTLE_SCALE, 24 * BATTLE_SCALE, sName, true);
    DrawBattleBox(GbaX(130), GbaY(82), 104 * BATTLE_SCALE, 26 * BATTLE_SCALE, sName, false);
    DrawHelpCell(CTR_HELP_FRAME, 8, GbaY(2), C2D_Color32(80, 230, 240, 255));
    DrawHelpCell(CTR_HELP_STATE, CTR_GAME_WIDTH - 8 - HelpWidth(CTR_HELP_STATE), GbaY(2),
                 sMeta.finalized ? C2D_Color32(120, 240, 120, 255) : C2D_Color32(245, 181, 69, 255));
}

/* The controls: the left hand's down the left, the right hand's down the
 * right, Y and A along the bottom in the middle; off by the editor's
 * SHOW CONTROLS. */
static void DrawControls(void)
{
    static const Help left[] = {
        {BTN_L, false, CTR_HELP_L_PLANE},
        {BTN_PAD, false, CTR_HELP_L_PAN},
        {BTN_DPAD_UD, false, CTR_HELP_L_ZOOM},
        {BTN_DPAD_LR, false, CTR_HELP_L_UNDO},
    };
    static const Help right[] = {
        {BTN_R, false, CTR_HELP_R_FRAME},
        {BTN_X, false, CTR_HELP_R_LAYERS},
        {BTN_CSTICK, false, CTR_HELP_R_MON},
        {BTN_START, false, CTR_HELP_R_SAVE},
        {BTN_SELECT, false, CTR_HELP_R_DEX},
        {BTN_B, false, CTR_HELP_R_BACK},
    };
    const uint32_t words = C2D_Color32(225, 230, 240, 255);
    int y = CTR_GAME_HEIGHT - 22, x;

    if (!sEditControls) return;
    DrawHelp(false, 134, left, sizeof(left) / sizeof(left[0]));
    DrawHelp(true, 114, right, sizeof(right) / sizeof(right[0]));
    /* Y ANIMATE and A OUTLINE side by side at the bottom. */
    x = CTR_GAME_WIDTH / 2 - (HelpWidth(CTR_HELP_R_ANIMATE) + HelpWidth(CTR_HELP_B_OUTLINE) + 2 * BTN_SIZE + 40) / 2;
    DrawHelpCell(CTR_HELP_R_ANIMATE, x, y + 3, words);
    x += HelpWidth(CTR_HELP_R_ANIMATE) + 8;
    DrawButton(BTN_Y, x, y + 2);
    x += BTN_SIZE + 24;
    DrawHelpCell(CTR_HELP_B_OUTLINE, x, y + 3, words);
    x += HelpWidth(CTR_HELP_B_OUTLINE) + 8;
    DrawButton(BTN_A, x, y + 2);
}

/* Help along a row, centred on `cx`: each button with its word after it. */
static void DrawHelpRow(int cx, int y, const Help *help, unsigned count)
{
    const uint32_t words = C2D_Color32(225, 230, 240, 255);
    int total = 0, x;

    for (unsigned i = 0; i < count; ++i)
        total += ButtonWidth(help[i].button) + 4 + HelpWidth(help[i].cell) + (i + 1 < count ? 18 : 0);
    x = cx - total / 2;
    for (unsigned i = 0; i < count; ++i)
    {
        DrawButton(help[i].button, x, y + (18 - ButtonHeight(help[i].button)) / 2);
        x += ButtonWidth(help[i].button) + 4;
        DrawHelpCell(help[i].cell, x, y + 3, words);
        x += HelpWidth(help[i].cell) + 18;
    }
}

/*
 * The top screen of the versions screen and of the Pokédex (3ds_stereo_edit.c):
 * the picture's front on the left and its back on the right, both as their
 * planes in 3D (the one on show and its other side, DrawMateStack); words
 * under each, the picture's name at the top left, the version (or who made
 * it) at the top right, and the screen's own controls along the bottom.
 */
#define PAIR_SCALE 2.0f
static void DrawPairEye(float sign, float slider, unsigned frame, bool chooser)
{
    static const Help versions1[] = {
        {BTN_DPAD_UD, false, CTR_HELP_C_VERSIONS},
        {BTN_DPAD_LR, false, CTR_HELP_C_POKEMON},
    };
    static const Help versions2[] = {
        {BTN_A, false, CTR_HELP_C_VOTE},
        {BTN_Y, false, CTR_HELP_R_ANIMATE},
        {BTN_B, false, CTR_HELP_C_CLOSE},
    };
    static const Help dex1[] = {
        {BTN_PAD, false, CTR_HELP_C_POKEMON},
        {BTN_L, false, CTR_HELP_C_PREV_PAGE},
        {BTN_R, false, CTR_HELP_C_NEXT_PAGE},
    };
    static const Help dex2[] = {
        {BTN_A, false, CTR_HELP_C_EDIT},
        {BTN_X, false, CTR_HELP_C_VOTE},
        {BTN_Y, false, CTR_HELP_C_SEARCH},
        {BTN_START, false, CTR_HELP_C_FILTER},
        {BTN_B, false, CTR_HELP_C_CLOSE},
    };
    const bool back = (sId & 0xC000) == CTR_STEREO_ID_BACK, pair = sMateReady;
    const float size = DIM * PAIR_SCALE, top = 40;
    const float leftX = pair ? CTR_GAME_WIDTH / 4 - size / 2 : CTR_GAME_WIDTH / 2 - size / 2;
    const float rightX = CTR_GAME_WIDTH * 3 / 4 - size / 2;
    const uint32_t white = C2D_Color32(240, 242, 248, 255), gold = C2D_Color32(255, 214, 72, 255);
    const uint32_t dim = C2D_Color32(185, 190, 205, 255), cyan = C2D_Color32(80, 230, 240, 255);
    const float grid = BackdropShift(sign, slider);
    /* Under the front and the back: the versions screen's FRONT and BACK,
     * the Pokédex's counts of each. */
    const unsigned underFront = chooser ? CTR_HELP_VERSION_BY : CTR_HELP_C_PREV_PAGE;
    const unsigned underBack = chooser ? CTR_HELP_VERSION_VOTES : CTR_HELP_C_NEXT_PAGE;
    float frontX = back && pair ? rightX : leftX, mateX = back ? leftX : rightX;

    (void)frontX;
    C2D_DrawRectSolid(0, 0, 0, CTR_GAME_WIDTH, CTR_GAME_HEIGHT, C2D_Color32(52, 56, 66, 255));
    for (int gx = 8 - 24; gx < CTR_GAME_WIDTH + 24; gx += 24)
        C2D_DrawRectSolid(gx + grid, 0, 0, 1, CTR_GAME_HEIGHT, C2D_Color32(66, 70, 82, 255));
    /* The front on the left, the back on the right, whichever is on show. */
    DrawStack((back && pair ? rightX : leftX) + PictureShift(sign, slider, sId), top, PAIR_SCALE,
              StackShift(sign, slider, sId), frame, NULL);
    if (pair)
        DrawMateStack(mateX + PictureShift(sign, slider, sMateId), top, PAIR_SCALE, StackShift(sign, slider, sMateId), frame);
    DrawHelpCell(underFront, leftX + size / 2 - HelpWidth(underFront) / 2, top + size + 2, chooser || !back ? cyan : dim);
    if (pair) DrawHelpCell(underBack, rightX + size / 2 - HelpWidth(underBack) / 2, top + size + 2, chooser || back ? cyan : dim);
    DrawHelpCell(CTR_HELP_CHOOSE_NAME, 8, 4, white);
    DrawHelpCell(CTR_HELP_PAGE, 8, 18, cyan);
    DrawHelpCell(CTR_HELP_VERSION, CTR_GAME_WIDTH - 8 - HelpWidth(CTR_HELP_VERSION), 4, gold);
    if (!chooser)
    {
        DrawHelpCell(CTR_HELP_VERSION_BY, CTR_GAME_WIDTH - 8 - HelpWidth(CTR_HELP_VERSION_BY), 18, white);
        DrawHelpCell(CTR_HELP_VERSION_VOTES, CTR_GAME_WIDTH - 8 - HelpWidth(CTR_HELP_VERSION_VOTES), 32, dim);
        DrawHelpRow(CTR_GAME_WIDTH / 2, CTR_GAME_HEIGHT - 42, versions1, sizeof(versions1) / sizeof(versions1[0]));
        DrawHelpRow(CTR_GAME_WIDTH / 2, CTR_GAME_HEIGHT - 21, versions2, sizeof(versions2) / sizeof(versions2[0]));
    }
    else
    {
        DrawHelpRow(CTR_GAME_WIDTH / 2, CTR_GAME_HEIGHT - 42, dex1, sizeof(dex1) / sizeof(dex1[0]));
        DrawHelpRow(CTR_GAME_WIDTH / 2, CTR_GAME_HEIGHT - 21, dex2, sizeof(dex2) / sizeof(dex2[0]));
    }
}

static void DrawEditEye(float sign, float slider, unsigned frame)
{
    static const uint16_t colours[CTR_STEREO_EDIT_PLANES] = CTR_STEREO_EDIT_COLOURS;
    const unsigned groups = Groups();
    const uint32_t cyan = C2D_Color32(80, 230, 240, 255), white = C2D_Color32(240, 242, 248, 255);
    const uint32_t dim = C2D_Color32(185, 190, 205, 255);
    int y;

    if (sEditBattle)
    {
        DrawBattleEye(sign, slider, frame);
        DrawControls();
        return;
    }
    /* The grid at the scenery's depth (BackdropShift), drawn wider than
     * the screen so its displaced edge still has picture; a shadow under
     * the feet at the picture's. */
    {
        const float back = BackdropShift(sign, slider);

        C2D_DrawRectSolid(0, 0, 0, CTR_GAME_WIDTH, CTR_GAME_HEIGHT, C2D_Color32(62, 64, 70, 255));
        for (int gx = 8 - 24; gx < CTR_GAME_WIDTH + 24; gx += 24)
            C2D_DrawRectSolid(gx + back, 0, 0, 1, CTR_GAME_HEIGHT, C2D_Color32(80, 82, 90, 255));
        for (int gy = 10; gy < CTR_GAME_HEIGHT; gy += 24) C2D_DrawRectSolid(0, gy, 0, CTR_GAME_WIDTH, 1, C2D_Color32(80, 82, 90, 255));
    }
    {
        int scx = (int)roundf(CENTRE_X + ((sBoxLeft + sBoxRight + 1) / 2.0f - DIM / 2) * SCALE
                              + PictureShift(sign, slider, sId));
        int scy = (int)(CENTRE_Y - DIM / 2 * SCALE + (sBoxBottom + 1) * SCALE) - 2;
        int rx = (int)((sBoxRight - sBoxLeft + 1) * SCALE * 0.45f);

        for (int row = -6; row < 6; ++row)
        {
            float dy = (row + 0.5f) / 6.0f;
            int half = (int)(rx * sqrtf(1.0f - dy * dy) + 0.5f);

            if (half > 0) C2D_DrawRectSolid(scx - half, scy + row, 0, 2 * half, 1, C2D_Color32(235, 238, 245, 220));
        }
    }
    DrawPlanes(sign, slider, frame, NULL, SCALE);
    if (sEditOutline && sEditPlane < groups && sTexReady && sComposed == sToCompose)
    {
        OutlineRuns(frame, sEditPlane);
        DrawOutline();
    }
    DrawStrays(frame);
    /* The name and the frame at the top left; what is being edited (the
     * plane in its colour, the depth, who, how far) at the top right. */
    DrawHelpCell(CTR_HELP_NAME, 8, 4, white);
    DrawHelpCell(CTR_HELP_FRAME, 8, 18, cyan);
    /* Which version this is (by whom, its votes) first, where it is seen. */
    y = 4;
    if (HelpWidth(CTR_HELP_VERSION))
    {
        DrawHelpCell(CTR_HELP_VERSION, CTR_GAME_WIDTH - 8 - HelpWidth(CTR_HELP_VERSION), y, C2D_Color32(255, 214, 72, 255));
        DrawHelpCell(CTR_HELP_VERSION_BY, CTR_GAME_WIDTH - 8 - HelpWidth(CTR_HELP_VERSION_BY), y + 14, white);
        DrawHelpCell(CTR_HELP_VERSION_VOTES, CTR_GAME_WIDTH - 8 - HelpWidth(CTR_HELP_VERSION_VOTES), y + 28, dim);
        y += 42;
    }
    if (sEditPlane < CTR_STEREO_EDIT_PLANES)
    {
        uint16_t c = colours[sEditPlane];

        DrawHelpCell(CTR_HELP_PLANE, CTR_GAME_WIDTH - 8 - HelpWidth(CTR_HELP_PLANE), y,
                     C2D_Color32(((c >> 11) & 31) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31, 255));
    }
    y += 14;
    DrawHelpCell(CTR_HELP_DEPTH, CTR_GAME_WIDTH - 8 - HelpWidth(CTR_HELP_DEPTH), y, dim);
    y += 14;
    DrawHelpCell(CTR_HELP_REVISION, CTR_GAME_WIDTH - 8 - HelpWidth(CTR_HELP_REVISION), y, dim);
    y += 14;
    DrawHelpCell(CTR_HELP_STATE, CTR_GAME_WIDTH - 8 - HelpWidth(CTR_HELP_STATE), y,
                 sMeta.finalized ? C2D_Color32(120, 240, 120, 255) : C2D_Color32(245, 181, 69, 255));
    (void)groups;
    DrawControls();
}

static void DrawEye(C3D_RenderTarget *target, float sign, float slider, unsigned frame)
{
    const unsigned groups = Groups();
    char line[64];

    C2D_TargetClear(target, C2D_Color32(10, 12, 18, 255));
    C2D_SceneBegin(target);
    C2D_ViewReset();
    C3D_AlphaTest(true, GPU_GREATER, 0);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ZERO);
    if (!sGallery || sEditing)
    {
        if (sEditing && sVersionsView && sLent) CtrStereoStrip_DrawEye(sign, slider);
        else if (sEditing && sVersionsView) DrawPairEye(sign, slider, frame, false);
        else if (sEditing) DrawEditEye(sign, slider, frame);
        else DrawDex(sign, slider, frame);
        C2D_Flush();
        return;
    }
    if (sChooser)
    {
        DrawPairEye(sign, slider, frame, true);
        C2D_Flush();
        return;
    }
    /* A quiet backdrop at the scenery's depth (BackdropShift), drawn wider
     * than the screen so its displaced edge still has picture, and a floor
     * the stack stands on, at the same depth. */
    {
        const float back = BackdropShift(sign, slider);

        C2D_DrawRectangle(back - 24, 0, 0, CTR_GAME_WIDTH + 48, CTR_GAME_HEIGHT,
                          C2D_Color32(22, 26, 40, 255), C2D_Color32(22, 26, 40, 255),
                          C2D_Color32(8, 9, 14, 255), C2D_Color32(8, 9, 14, 255));
        C2D_DrawRectangle(back - 24, CENTRE_Y + DIM * SCALE / 2 - 6, 0, CTR_GAME_WIDTH + 48, 12,
                          C2D_Color32(40, 46, 70, 0), C2D_Color32(40, 46, 70, 0),
                          C2D_Color32(40, 46, 70, 160), C2D_Color32(40, 46, 70, 160));
    }
    DrawPlanes(sign, slider, frame, NULL, SCALE);
    if ((sId & 0xC000) == 0) snprintf(line, sizeof(line), "NO.%03u %s", sId, sName);
    else snprintf(line, sizeof(line), "%s", sName);
    DrawText(8, 6, line, C2D_Color32(235, 235, 240, 255));
    snprintf(line, sizeof(line), "%u/%u", (unsigned)(CtrStereoMon_GalleryIndexOf(sId) + 1), CtrStereoMon_GalleryCount());
    DrawText(CTR_GAME_WIDTH - 8 - GLYPH_ADVANCE * (int)strlen(line), 6, line, C2D_Color32(160, 165, 180, 255));
    snprintf(line, sizeof(line), "<> MON   L/R PLANES %u%s", groups, sEdited ? "   EDITED" : "");
    DrawText(8, CTR_GAME_HEIGHT - 10 - 10 * GLYPH_PIXEL, line, C2D_Color32(160, 165, 180, 255));
    DrawText(8, CTR_GAME_HEIGHT - 6 - 5 * GLYPH_PIXEL,
             CtrOnline_Linked() ? "X ANIM   Y EDIT   SELECT SYNC   B BACK" : "X ANIM   Y EDIT   B BACK",
             C2D_Color32(160, 165, 180, 255));
    /* A sync's progress and result, under the picture's number. */
    if (CtrOnline_Recent(ONLINE_LINE_MS))
    {
        CtrOnlineStatus status = CtrOnline_Status(line, sizeof(line));

        DrawText(CTR_GAME_WIDTH - 8 - GLYPH_ADVANCE * (int)strlen(line), 6 + 8 * GLYPH_PIXEL, line,
                 status == CTR_ONLINE_ERROR ? C2D_Color32(255, 140, 120, 255) : C2D_Color32(150, 230, 160, 255));
    }
#ifdef CTR_STEREO_DEBUG
    snprintf(line, sizeof(line), "T%u C%u/%u F%u G%u X%u", sTexReady, sComposed, sToCompose, sFrames, groups, sTexCols);
    DrawText(CTR_GAME_WIDTH - 8 - GLYPH_ADVANCE * (int)strlen(line), CTR_GAME_HEIGHT - 6 - 5 * GLYPH_PIXEL, line,
             C2D_Color32(255, 200, 120, 255));
#endif
    C2D_Flush();
}

/* The frame the picture shows at tick `tick`: the gallery's blink, or while
 * editing the frame being edited (still, unless the editor animates). */
static unsigned FrameAt(uint32_t tick)
{
    unsigned frame = sAnimate && sFrames > 1 && tick % ANIM_PERIOD < ANIM_HOLD ? 1 : 0;

    if (sEditing && !sEditAnimate) frame = sEditFrame < sFrames ? sEditFrame : 0;
    else if (sEditing && sFrames) frame = (tick / EDIT_ANIM_STEP) % sFrames;
    return frame;
}

/*
 * Everything a frame of the viewer depends on, so that the compositor can
 * leave the top screen as it is (and the GPU asleep) while none of it has
 * changed: the picture and its planes, the frame, the slider, the edit
 * view, the words on the help sheet.
 */
typedef struct
{
    uint16_t id;
    uint8_t frame, groups, editing, editPlane, editOutline, edited, gallery, stereo;
    uint8_t editBattle, editLayers, editControls, backdrop, mate, final, undrawn, chooser;
    int depth, slider, boxLeft, boxRight, boxBottom;
    uint32_t mark;
    uint32_t strays;
    unsigned planesVersion, composed, toCompose, helpVersion, frames;
    bool texReady;
    char name[CTR_STEREO_MON_NAME];
    /* The gallery's online line (3ds_online.c): shown, and its text. */
    unsigned onlineSerial;
    bool online;
    /* The voting view's filmstrip (CtrStereoStrip_Version). */
    unsigned strip;
} DrawKey;

static DrawKey sDrawn;

static void MakeKey(DrawKey *k, uint32_t tick, float slider, bool stereo)
{
    memset(k, 0, sizeof(*k));
    k->id = sId;
    k->frame = (uint8_t)FrameAt(tick);
    k->groups = (uint8_t)Groups();
    k->editing = sEditing;
    k->editPlane = (uint8_t)sEditPlane;
    k->editOutline = sEditOutline;
    k->edited = sEdited;
    k->gallery = sGallery;
    k->stereo = stereo;
    k->editBattle = sEditBattle;
    k->editLayers = sEditLayers;
    k->editControls = sEditControls;
    k->chooser = (uint8_t)(sChooser | (sVersionsView << 1));
    k->backdrop = sBackdropReady;
    k->mate = sMateReady;
    k->final = sMeta.finalized;
    k->undrawn = sUndrawn;
    {
        const CtrStereoDepths d = CtrStereoDepths_Get();

        /* OPTIONS' depths, each a few pixels: changed, the frame is drawn again. */
        k->depth = (int)(d.back * 4) | (int)(d.mon * 4) << 8 | (int)((d.own + 2) * 4) << 16 | (int)(d.spread * 4) << 20
                 | (int)(d.pivot * 100) << 24;
    }
    k->slider = (int)(slider * 256.0f + 0.5f);
    k->boxLeft = sBoxLeft;
    k->boxRight = sBoxRight;
    k->boxBottom = sBoxBottom;
    k->mark = sEditMark;
    /* The strays' rings blink: the frame is drawn again each phase while any show. */
    k->strays = sEditing && sStrayCount && FrameAt(tick) == sStrayFrame ? (sStrayVersion << 1 | ((tick / STRAY_BLINK) & 1)) : 0;
    k->planesVersion = sPlanesVersion;
    k->composed = sComposed;
    k->toCompose = sToCompose;
    k->helpVersion = sHelpVersion;
    if (sGallery && !sEditing)
    {
        k->online = CtrOnline_Recent(ONLINE_LINE_MS);
        k->onlineSerial = k->online ? CtrOnline_Serial() : 0;
    }
    k->frames = sFrames;
    k->texReady = sTexReady;
    k->strip = sLent ? CtrStereoStrip_Version() : 0;
    memcpy(k->name, sName, sizeof(k->name));
}

bool CtrStereoMon_NeedsFrame(float slider, bool stereo)
{
    DrawKey next;

    if (!sActive) return false;
    /* The frame's tick, counted here when the frame is held (Draw counts
     * it otherwise), so the gallery's blink and the editor's animation keep
     * their time whether or not frames are drawn. */
    ++sTick;
    sTicked = true;
    if (!sDrawnValid || (sLent ? CtrStereoStrip_Pending() : !sTexReady || sComposed < sToCompose)) return true;
    if (slider < 0.0f) slider = 0.0f;
    if (slider > 1.0f) slider = 1.0f;
    MakeKey(&next, sTick, slider, stereo);
    return memcmp(&next, &sDrawn, sizeof(next)) != 0;
}

void CtrStereoMon_Draw(void *leftTarget, void *rightTarget, float slider)
{
    unsigned frame;

    if (!sActive) return;
    if (sLent) CtrStereoStrip_Compose();
    else if ((!sTexReady || sComposed < sToCompose) && TextureReady()) ComposeSome();
    if (!sTicked) ++sTick;
    sTicked = false;
    frame = FrameAt(sTick);
    if (slider < 0.0f) slider = 0.0f;
    if (slider > 1.0f) slider = 1.0f;
    MakeKey(&sDrawn, sTick, slider, rightTarget != NULL);
    sDrawnValid = true;
    /* The left eye sees what is nearer than the screen shifted right. */
    DrawEye((C3D_RenderTarget *)leftTarget, rightTarget ? 1.0f : 0.0f, slider, frame);
    if (rightTarget)
        DrawEye((C3D_RenderTarget *)rightTarget, -1.0f, slider, frame);
}
