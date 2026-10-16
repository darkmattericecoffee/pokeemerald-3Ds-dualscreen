/*
 * The voting view's top screen, native side (3ds_stereo_strip.h): the
 * filmstrip of a Pokémon's server versions, each column a front over a
 * back, each half its own stack of planes in 3D at the battle's size.
 *
 * The planes are composed into the viewer's texture (lent by
 * 3ds_stereo_mon.c while the view is open), a column slot at a time: a
 * slot is both halves' four planes, frame 0 only (the strip compares, it
 * does not animate). Column c lives in slot c % slots, so a slide by one
 * column composes one column anew.
 */
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "3ds_platform.h"
#include "3ds_video.h"
#include "3ds_log.h"
#include "3ds_online.h"
#include "3ds_stereo_mon.h"
#include "3ds_stereo_strip.h"

#define DIM CTR_STEREO_MON_DIM
#define GROUPS 4
#define TILES_PER_SLOT (2 * GROUPS)
/* As the viewer's own stacks (3ds_stereo_mon.c): the planes' span in
 * sprite pixels, the cards' skirt, a back's depth against a front's. */
#define DEPTH_SPAN 14.4f
#define SKIRT 4
#define BACK_DEPTH 0.6f
#define DEOXYS_NATIONAL 386
/* The battle's size, three columns across. */
#define SCALE CTR_BATTLE_ZOOM
#define COL_W (CTR_GAME_WIDTH / (float)CTR_STRIP_VISIBLE)
#define SIZE (DIM * SCALE)
#define AUTHOR_Y 16
#define FRONT_TOP 29
#define BACK_TOP 133
#define LINE_GAP 1
/* Halves composed per frame at most (four tiles each, about 25 ms in Azahar). */
#define HALVES_PER_FRAME 1

typedef struct
{
    int column;
    uint32_t version;
    bool ready, tried;
} Half;

static bool sOpen;
static uint16_t sIds[2];
static C3D_Tex *sTex;
static unsigned sTexW, sTexH, sCols, sSlots;
static unsigned sCount;
static CtrStripCell sCells[CTR_STRIP_MAX_COLUMNS][2];
static float sScroll;
static unsigned sCursorColumn, sCursorSide;
static bool sCursorShown;
static uint16_t sPicture[2][DIM * DIM];
static bool sPictureOk[2];
static Half sHalves[CTR_STRIP_SLOTS_MAX][2];
static Tex3DS_SubTexture sTiles[CTR_STRIP_SLOTS_MAX * TILES_PER_SLOT];
static unsigned sVersion = 1, sFilesSerial;
static uint8_t sFile[32 * 1024];

/* The label sheet: 256x128 LA4, 18 cells of 128x14, made once and kept. */
#define LABEL_TEX_W 256
#define LABEL_TEX_H 128
static C3D_Tex sLabelTex;
static bool sLabelAllocated, sLabelFailed;
static uint8_t sLabelWidth[CTR_STRIP_LABELS];
static int sLabelColumn[CTR_STRIP_LABELS];

/* ── The editor's side ───────────────────────────────────────────────── */

static void Forget(void)
{
    for (unsigned s = 0; s < CTR_STRIP_SLOTS_MAX; ++s)
        for (unsigned d = 0; d < 2; ++d) sHalves[s][d] = (Half){-1, 0, false, false};
    ++sVersion;
}

void CtrStereoStrip_Open(uint16_t front, uint16_t back)
{
    unsigned w = 0, h = 0;

    if (!sOpen)
    {
        sTex = (C3D_Tex *)CtrStereoMon_LendTexture(true, &w, &h);
        sTexW = w;
        sTexH = h;
        sCols = w / DIM;
        sSlots = sTex ? (sCols * (h / DIM)) / TILES_PER_SLOT : 0;
        if (sSlots > CTR_STRIP_SLOTS_MAX) sSlots = CTR_STRIP_SLOTS_MAX;
        for (unsigned t = 0; t < sSlots * TILES_PER_SLOT; ++t)
        {
            unsigned x = (t % sCols) * DIM, y = (t / sCols) * DIM;

            sTiles[t] = (Tex3DS_SubTexture){DIM, DIM, x / (float)sTexW, 1.0f - y / (float)sTexH, (x + DIM) / (float)sTexW,
                                            1.0f - (y + DIM) / (float)sTexH};
        }
        CtrLog_Write(CTR_LOG_VIDEO, "strip: %u column slots in the lent %ux%u texture", sSlots, w, h);
    }
    sOpen = true;
    if (sIds[0] != front || sIds[1] != back)
    {
        sIds[0] = front;
        sIds[1] = back;
        sPictureOk[0] = sPictureOk[1] = false;
        sCount = 0;
        sScroll = 0;
        memset(sCells, 0, sizeof(sCells));
        for (unsigned i = 0; i < CTR_STRIP_LABELS; ++i) sLabelColumn[i] = -1;
    }
    Forget();
}

void CtrStereoStrip_Close(void)
{
    if (!sOpen) return;
    sOpen = false;
    sTex = NULL;
    sSlots = 0;
    CtrStereoMon_LendTexture(false, NULL, NULL);
    Forget();
}

bool CtrStereoStrip_IsOpen(void) { return sOpen; }
unsigned CtrStereoStrip_Slots(void) { return sSlots; }

void CtrStereoStrip_SetCount(unsigned count)
{
    count = count > CTR_STRIP_MAX_COLUMNS ? CTR_STRIP_MAX_COLUMNS : count;
    if (count != sCount) ++sVersion;
    sCount = count;
}

void CtrStereoStrip_SetCell(unsigned column, unsigned side, const CtrStripCell *cell)
{
    if (column >= CTR_STRIP_MAX_COLUMNS || side > 1 || cell == NULL) return;
    if (memcmp(&sCells[column][side], cell, sizeof(*cell)) != 0) ++sVersion;
    sCells[column][side] = *cell;
}

void CtrStereoStrip_SetScroll(float first)
{
    if (first < 0) first = 0;
    if (first != sScroll) ++sVersion;
    sScroll = first;
}

void CtrStereoStrip_SetCursor(unsigned column, unsigned side, bool shown)
{
    if (column != sCursorColumn || side != sCursorSide || shown != sCursorShown) ++sVersion;
    sCursorColumn = column;
    sCursorSide = side;
    sCursorShown = shown;
}

void CtrStereoStrip_Label(unsigned cell, const uint8_t *alpha, unsigned width, unsigned column)
{
    unsigned cx, cy;
    uint8_t *texels;

    if (cell >= CTR_STRIP_LABELS || alpha == NULL || sLabelFailed) return;
    if (!sLabelAllocated)
    {
        if (!C3D_TexInit(&sLabelTex, LABEL_TEX_W, LABEL_TEX_H, GPU_LA4))
        {
            sLabelFailed = true;
            CtrLog_Write(CTR_LOG_ERROR, "strip: no linear memory for the label sheet (free=%lu)",
                         (unsigned long)linearSpaceFree());
            return;
        }
        C3D_TexSetFilter(&sLabelTex, GPU_NEAREST, GPU_NEAREST);
        C3D_TexSetWrap(&sLabelTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        memset(sLabelTex.data, 0, LABEL_TEX_W * LABEL_TEX_H);
        sLabelAllocated = true;
    }
    texels = sLabelTex.data;
    cx = (cell % 2) * CTR_STRIP_LABEL_W;
    cy = (cell / 2) * CTR_STRIP_LABEL_H;
    for (unsigned y = 0; y < CTR_STRIP_LABEL_H; ++y)
        for (unsigned x = 0; x < CTR_STRIP_LABEL_W; ++x)
            texels[CtrVideo_Texel(cx + x, cy + y, LABEL_TEX_W)] = alpha[y * CTR_STRIP_LABEL_W + x] ? 0xFF : 0x00;
    sLabelWidth[cell] = (uint8_t)(width > CTR_STRIP_LABEL_W ? CTR_STRIP_LABEL_W : width);
    sLabelColumn[cell] = (int)column;
    C3D_TexFlush(&sLabelTex);
    ++sVersion;
}

unsigned CtrStereoStrip_Version(void) { return sOpen ? sVersion : 0; }

/* ── Composing ───────────────────────────────────────────────────────── */

/* A version's planes (its file from the online cache) into a slot's half:
 * false while the file is not there, or when it is not this picture's. */
static bool ComposeHalf(unsigned slot, unsigned side, uint32_t version)
{
    CtrStereoMonHeader header;
    uint32_t size = 0;
    unsigned planes, frames, frame = 0;
    if (!CtrOnline_FileCopy(version, sFile, sizeof(sFile), &size) || size < 16 + DIM * DIM / 2) return false;
    if (memcmp(sFile, CTR_STEREO_EDIT_MAGIC, 8) != 0 || (sFile[8] | (sFile[9] << 8)) != sIds[side] || sFile[12] != DIM
        || sFile[14] != DIM)
        return false;
    planes = sFile[10];
    frames = sFile[11];
    if (planes < 2 || planes > CTR_STEREO_EDIT_MAX_PLANES || frames < 1 || frames > CTR_STEREO_MON_MAX_FRAMES
        || size < 16 + frames * (DIM * DIM / 2))
        return false;
    /* Deoxys's form is its second frame of planes (as the viewer has it). */
    if ((sIds[side] & 0x3FFF) == DEOXYS_NATIONAL && frames > 1) frame = 1;
    memset(&header, 0, sizeof(header));
    header.count = 1;
    header.planes = (uint8_t)planes;
    header.frames = (uint8_t)frames;
    header.width = header.height = DIM;
    header.entryBytes = frames * (DIM * DIM / 2);
    for (unsigned g = 0; g < GROUPS; ++g)
    {
        unsigned tile = (slot * 2 + side) * GROUPS + g;

        CtrStereoMon_ComposeTile(sPicture[side], sFile + 16, &header, frame, GROUPS, g, sTex->data, sTexW,
                                 (tile % sCols) * DIM, (tile / sCols) * DIM, SKIRT);
    }
    C3D_TexFlush(sTex);
    return true;
}

void CtrStereoStrip_Compose(void)
{
    unsigned budget = HALVES_PER_FRAME, files;
    int first;

    if (!sOpen || sTex == NULL || sSlots == 0) return;
    /* The pictures (the viewer's and its other side's) as they come. */
    for (unsigned d = 0; d < 2; ++d)
    {
        const uint16_t *p = sIds[d] ? CtrStereoMon_SidePicture(sIds[d]) : NULL;

        if (p && (!sPictureOk[d] || memcmp(p, sPicture[d], sizeof(sPicture[d])) != 0))
        {
            memcpy(sPicture[d], p, sizeof(sPicture[d]));
            sPictureOk[d] = true;
            for (unsigned s = 0; s < CTR_STRIP_SLOTS_MAX; ++s) sHalves[s][d].tried = sHalves[s][d].ready = false;
            ++sVersion;
        }
    }
    /* Files that came are tried again. */
    files = CtrOnline_FilesSerial();
    if (files != sFilesSerial)
    {
        sFilesSerial = files;
        for (unsigned s = 0; s < CTR_STRIP_SLOTS_MAX; ++s)
            for (unsigned d = 0; d < 2; ++d)
                if (!sHalves[s][d].ready) sHalves[s][d].tried = false;
    }
    first = (int)floorf(sScroll);
    if (first < 0) first = 0;
    for (unsigned k = 0; k < sSlots; ++k)
    {
        unsigned c = (unsigned)first + k, s = c % sSlots;

        if (c >= sCount) break;
        for (unsigned d = 0; d < 2; ++d)
        {
            Half *h = &sHalves[s][d];
            uint32_t v = sCells[c][d].version;

            if (h->column == (int)c && h->version == v && (h->ready || h->tried)) continue;
            if (budget == 0) return;
            if (h->column != (int)c || h->version != v) ++sVersion;
            h->column = (int)c;
            h->version = v;
            h->ready = false;
            h->tried = true;
            if (v == 0 || !sPictureOk[d]) continue;
            h->ready = ComposeHalf(s, d, v);
            if (h->ready) ++sVersion;
            --budget;
        }
    }
}

/* Compose runs in a frame drawn, and the frame is held while nothing
 * changes: this keeps the frames coming while columns wait. */
bool CtrStereoStrip_Pending(void)
{
    int first;

    if (!sOpen || sTex == NULL || sSlots == 0) return false;
    if (CtrOnline_FilesSerial() != sFilesSerial) return true;
    for (unsigned d = 0; d < 2; ++d)
    {
        const uint16_t *p = sIds[d] ? CtrStereoMon_SidePicture(sIds[d]) : NULL;

        if (p && (!sPictureOk[d] || memcmp(p, sPicture[d], sizeof(sPicture[d])) != 0)) return true;
    }
    first = (int)floorf(sScroll);
    if (first < 0) first = 0;
    for (unsigned k = 0; k < sSlots && (unsigned)first + k < sCount; ++k)
    {
        unsigned c = (unsigned)first + k;

        for (unsigned d = 0; d < 2; ++d)
        {
            const Half *h = &sHalves[c % sSlots][d];

            if (h->column != (int)c || h->version != sCells[c][d].version || !(h->ready || h->tried)) return true;
        }
    }
    return false;
}

/* ── Drawing ─────────────────────────────────────────────────────────── */

/*
 * The depths as the viewer's (3ds_stereo_mon.c StackShift and on): the
 * scene's (BATTLE BACK, BATTLE MONS, PLANES SIT) and the planes' own, the
 * same for every picture (MON PLANES).
 */
static float StackShift(float sign, float slider, bool back)
{
    return sign * slider * CtrStereoDepths_Get().spread * CTR_BATTLE_ZOOM / DEPTH_SPAN * (back ? BACK_DEPTH : 1.0f);
}
static float PictureShift(float sign, float slider, bool back)
{
    const CtrStereoDepths d = CtrStereoDepths_Get();

    return -sign * slider * (back ? d.own : d.mon);
}
static float Pivot(void) { return CtrStereoDepths_Get().pivot; }
static float BackdropShift(float sign, float slider) { return -sign * slider * CtrStereoDepths_Get().back; }

static void Label(unsigned cell, float x, float y, uint32_t colour)
{
    unsigned cx = (cell % 2) * CTR_STRIP_LABEL_W, cy = (cell / 2) * CTR_STRIP_LABEL_H, w;
    Tex3DS_SubTexture sub;
    C2D_Image image = {&sLabelTex, &sub};
    C2D_ImageTint tint;

    if (cell >= CTR_STRIP_LABELS || !sLabelAllocated || (w = sLabelWidth[cell]) == 0) return;
    sub = (Tex3DS_SubTexture){(uint16_t)w, CTR_STRIP_LABEL_H, cx / (float)LABEL_TEX_W, 1.0f - cy / (float)LABEL_TEX_H,
                              (cx + w) / (float)LABEL_TEX_W, 1.0f - (cy + CTR_STRIP_LABEL_H) / (float)LABEL_TEX_H};
    C2D_PlainImageTint(&tint, C2D_Color32(24, 26, 32, 255), 1.0f);
    C2D_DrawImageAt(image, x + 1, y + 1, 0, &tint, 1.0f, 1.0f);
    C2D_PlainImageTint(&tint, colour, 1.0f);
    C2D_DrawImageAt(image, x, y, 0, &tint, 1.0f, 1.0f);
}

static int LabelWidth(unsigned cell) { return cell < CTR_STRIP_LABELS && sLabelAllocated ? sLabelWidth[cell] : 0; }

/* A slot's label, only while it speaks of the column shown there. */
static void SlotLabel(unsigned slot, unsigned k, unsigned column, float cx, float y, uint32_t colour, float *right)
{
    unsigned cell = CTR_STRIP_SLOT_LABEL(slot, k);
    float x;

    if (sLabelColumn[cell] != (int)column) return;
    x = cx - LabelWidth(cell) / 2.0f;
    Label(cell, x, y, colour);
    if (right) *right = x + LabelWidth(cell);
}

static void Frame(float x, float y, float w, float h, float t, uint32_t colour)
{
    C2D_DrawRectSolid(x, y, 0, w, t, colour);
    C2D_DrawRectSolid(x, y + h - t, 0, w, t, colour);
    C2D_DrawRectSolid(x, y + t, 0, t, h - 2 * t, colour);
    C2D_DrawRectSolid(x + w - t, y + t, 0, t, h - 2 * t, colour);
}

static void DrawHalf(unsigned column, unsigned side, float cx, float sign, float slider)
{
    const unsigned slot = column % sSlots;
    const Half *h = &sHalves[slot][side];
    const CtrStripCell *cell = &sCells[column][side];
    const float top = side ? BACK_TOP : FRONT_TOP, lineY = top + SIZE + LINE_GAP;
    const bool back = side == 1;
    const uint32_t gold = C2D_Color32(255, 214, 72, 255), dim = C2D_Color32(170, 176, 192, 255);
    float right = cx;

    /* The cell's ground: a little lighter when voted for. */
    if (cell->voted) C2D_DrawRectSolid(cx - COL_W / 2 + 4, top - 1, 0, COL_W - 8, SIZE + 2, C2D_Color32(74, 70, 52, 255));
    if (cell->version == 0)
    {
        Label(CTR_STRIP_EMPTY, cx - LabelWidth(CTR_STRIP_EMPTY) / 2.0f, top + SIZE / 2 - 7, dim);
        return;
    }
    if (h->ready && h->column == (int)column && h->version == cell->version)
    {
        const float shift = StackShift(sign, slider, back);
        const float pivot = Pivot() * (GROUPS - 1), spacing = DEPTH_SPAN / (float)(GROUPS - 1);
        const float left = cx - SIZE / 2 + PictureShift(sign, slider, back);

        for (unsigned g = 0; g < GROUPS; ++g)
            C2D_DrawImageAt((C2D_Image){sTex, &sTiles[(slot * 2 + side) * GROUPS + g]}, left + shift * (g - pivot) * spacing,
                            top, 0, NULL, SCALE, SCALE);
    }
    else Label(CTR_STRIP_LOADING, cx - LabelWidth(CTR_STRIP_LOADING) / 2.0f, top + SIZE / 2 - 7, dim);
    if (cell->voted) Frame(cx - COL_W / 2 + 4, top - 1, COL_W - 8, SIZE + 2, 1, gold);
    if (sCursorShown && column == sCursorColumn && side == sCursorSide)
        Frame(cx - COL_W / 2 + 2, top - 3, COL_W - 4, SIZE + 6, 2, C2D_Color32(80, 230, 240, 255));
    /* Under it: its votes, FINAL after them. */
    SlotLabel(slot, 1 + side, column, cx, lineY, cell->voted ? gold : dim, &right);
    if (cell->final && sLabelColumn[CTR_STRIP_SLOT_LABEL(slot, 1 + side)] == (int)column)
        Label(CTR_STRIP_FINAL, right + 3, lineY, C2D_Color32(120, 240, 120, 255));
}

void CtrStereoStrip_DrawEye(float sign, float slider)
{
    const float grid = BackdropShift(sign, slider);
    const uint32_t white = C2D_Color32(240, 242, 248, 255), cyan = C2D_Color32(80, 230, 240, 255);
    int first = (int)floorf(sScroll);

    C2D_DrawRectSolid(0, 0, 0, CTR_GAME_WIDTH, CTR_GAME_HEIGHT, C2D_Color32(52, 56, 66, 255));
    for (int gx = 8 - 24; gx < CTR_GAME_WIDTH + 24; gx += 24)
        C2D_DrawRectSolid(gx + grid, 0, 0, 1, CTR_GAME_HEIGHT, C2D_Color32(64, 68, 80, 255));
    if (!sOpen) return;
    if (first < 0) first = 0;
    /* The columns in view (four while one slides half out), the ones the
     * texture holds. */
    for (int c = first; c < first + CTR_STRIP_VISIBLE + 1 && c < (int)sCount; ++c)
    {
        const float cx = (c - sScroll) * COL_W + COL_W / 2;
        const unsigned slot = (unsigned)c % (sSlots ? sSlots : 1);

        if (sSlots == 0 || c >= first + (int)sSlots) break;
        /* The column's divider, then its author over it. */
        if (c > first || sScroll == floorf(sScroll))
            C2D_DrawRectSolid(cx - COL_W / 2, AUTHOR_Y + 2, 0, 1, CTR_GAME_HEIGHT - AUTHOR_Y - 4, C2D_Color32(70, 75, 88, 255));
        SlotLabel(slot, 0, (unsigned)c, cx, AUTHOR_Y, sCells[c][0].mine || sCells[c][1].mine ? cyan : white, NULL);
        if (sCells[c][0].web || sCells[c][1].web)
            Label(CTR_STRIP_WEB, cx + COL_W / 2 - 4 - LabelWidth(CTR_STRIP_WEB), AUTHOR_Y, C2D_Color32(255, 120, 220, 255));
        DrawHalf((unsigned)c, 0, cx, sign, slider);
        if (sIds[1]) DrawHalf((unsigned)c, 1, cx, sign, slider);
    }
    Label(CTR_STRIP_TITLE, 6, 1, white);
    Label(CTR_STRIP_PAGE, CTR_GAME_WIDTH - 6 - LabelWidth(CTR_STRIP_PAGE), 1, cyan);
}
