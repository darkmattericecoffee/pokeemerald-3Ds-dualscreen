/*
 * The stereoscopic planes' editor (3ds_stereo_mon.h), game side: the
 * bottom screen while the gallery's Pokémon is being edited.
 *
 * The top screen keeps showing the Pokémon in its own colours as its stack
 * of planes (the viewer, 3ds_stereo_mon.c, composing the planes again after
 * every change), the plane being edited outlined there in the colour
 * opposite the Pokémon's own, so it shows against any Pokémon; X tints it
 * there by plane too (VIEW LAYERS), and the battle button shows it as a
 * battle would. The bottom screen is the editor: the picture over the whole
 * screen with every pixel tinted by its plane's colour (four clashing hues,
 * the plane being edited bright, the others muted, the tint's strength a
 * button), the four planes down the right nearest first with the grid's
 * and the tint's buttons under them; over the picture, at its top, what
 * concerns the picture (the chooser, the front/back switch, the frame, the
 * battle's view, FINAL), and at its left the two sliders (zoom, brush
 * size) with each tool under the slider it goes with. A tool paints
 * planes, never pixels: the picture is the game's, a plane number is all a
 * pixel has, so painting a pixel into one plane takes it out of the other
 * and there is no eraser.
 *
 *  BRUSH   the pixels under the stylus join the plane being edited.
 *  FILL    a tap: the colour cell under the stylus joins the plane (same
 *          colour, bounded by the outline and transparency, with the outline
 *          that touches it); a loop: every pixel inside it does.
 *  PAN     a drag moves the picture; so does the Circle Pad.
 *
 * A short press of L goes to the next plane; L held and a tap picks the
 * plane under the stylus. R (and the frame button) turns to the next frame;
 * the C-stick goes to the next picture, the DEX button (and SELECT) to the
 * chooser: every Pokémon's front and back, the trainers' fronts and the
 * players' backs (both planes files), searched by name, the edited ones on
 * a list of their own or first. The F/B switch swaps a Pokémon's front and
 * back. A tap on a plane in the list picks it; one held still is grabbed
 * and dropped elsewhere in the stack. Every picture has the same four
 * planes here (CTR_STEREO_EDIT_PLANES): a file with more is folded into
 * them, one with fewer spread over them, when it is opened.
 *
 * Every change on one frame is carried to the others by the packer's rule
 * (the nearest pixel of the same colour) unless that is turned off in MORE.
 * Edits are kept on the SD card per Pokémon (CtrStereoMon_SaveEdit): on
 * START, on leaving, and on switching Pokémon; each save is signed with the
 * author's name (MORE: AUTHOR) and numbered, and FINAL marks a picture
 * whose planes are done.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "3ds_bottom.h"
#include "3ds_bottom_draw.h"
#include "3ds_edit_icons.h"
#include "3ds_input.h"
#include "3ds_log.h"
#include "3ds_platform.h"
#include "3ds_stereo_mon.h"

/* 3ds_stereo_mon_game.c: the name of what an id shows, in ASCII capitals. */
void CtrStereoMonGame_Name(uint16_t id, char *out, unsigned size);

#define W CTR_BOTTOM_WIDTH
#define H CTR_BOTTOM_HEIGHT
#define DIM CTR_STEREO_MON_DIM
#define N (DIM * DIM)
#define NONE CTR_STEREO_MON_NONE
#define PLANES CTR_STEREO_EDIT_PLANES
#define FRAME_BYTES (N / 2)
#define MAX_FRAMES CTR_STEREO_MON_MAX_FRAMES

/* The layout: the picture over the screen but for the planes' column. */
#define CAN_X 0
#define RIGHT_X 264
#define CAN_W (RIGHT_X - CAN_X)
/* Over the picture's top: the chooser, the front/back switch, the frame,
 * the battle's view; FINAL at the right. */
#define BAR_Y 3
#define BAR_H 24
#define DEX_X 4
#define DEX_W 28
#define FB_X 34
#define FB_W 18
#define FRAME_X (FB_X + 2 * FB_W + 4)
#define FRAME_W 38
#define BATTLE_X (FRAME_X + FRAME_W + 4)
#define BATTLE_W 28
#define FINAL_W 60
#define FINAL_X (RIGHT_X - 4 - FINAL_W)
/* Over its left: the sliders, their tracks from SL_TOP to SL_BOTTOM, and
 * under each the tool that goes with it (PAN under the zoom, BRUSH under
 * the size), FILL and UNDO under those. */
#define SL_TOP 44
#define SL_BOTTOM 142
#define SL_ZOOM_X 14
#define SL_SIZE_X 40
#define SL_REACH 12
#define KNOB_R 9
#define TOOL_W 24
#define TOOL_H 24
#define TOOL_Y0 156
#define TOOL_STEP 28
/* The planes: a framed thumbnail each, nearest at the top. */
#define ROW_X (RIGHT_X + 4)
#define ROW_Y0 2
#define ROW_STEP 48
#define ROW_FRAME 3
#define ROW_SIDE 46
#define THUMB (ROW_SIDE - 2 * ROW_FRAME)
#define ROWS_END (ROW_Y0 + PLANES * ROW_STEP)
/* The buttons under them, three by two: the grid's three looks, then the
 * tint, the outline and MORE. */
#define BTN_Y0 (ROWS_END + 4)
#define BTN_W 16
#define BTN_H 18
#define BTN_STEP_X 17
#define BTN_STEP_Y 20
/* Frames a row is held still before it is grabbed for moving. */
#define GRAB_FRAMES 24

#define UNDO_DEPTH 12
#define LASSO_POINTS 512
#define TOAST_FRAMES 90
/* How far the propagation looks for a pixel of the same colour. */
#define PROPAGATE_RADIUS 7
/* Below this size (screen pixels) a lasso is a tap. */
#define TAP_SLOP 6

enum { T_BRUSH, T_FILL, T_PAN, T_COUNT };
/* The left's buttons: the tools, and UNDO beside FILL. */
enum { L_PAN, L_BRUSH, L_FILL, L_UNDO, L_COUNT };
enum { B_GRID_OFF, B_GRID_WHITE, B_GRID_BLACK, B_OPACITY, B_OUTLINE, B_MORE, B_COUNT };
/* The pressed button's number: the left's, the right's, the bar's. */
enum { P_NONE = -1, P_LEFT = 0, P_RIGHT = 16, P_DEX = 32, P_FRONT, P_BACK, P_FRAME, P_BATTLE, P_FINAL };
enum { SLIDER_NONE = -1, SLIDER_ZOOM, SLIDER_SIZE };
enum { GRID_OFF, GRID_WHITE, GRID_BLACK };
/* The chooser: its bar (the search, ALL/EDITED, the order, the pages),
 * then a page of icons and names. */
#define CH_COLS 3
#define CH_ROWS 6
#define CH_ROW_H 33
#define CH_Y0 24
#define CH_PER_PAGE (CH_COLS * CH_ROWS)
#define CH_SEARCH_X 2
#define CH_SEARCH_W 112
#define CH_SHOW_X 118
#define CH_SHOW_W 58
#define CH_SORT_X 180
#define CH_SORT_W 70
#define CH_PREV_X 254
#define CH_NEXT_X 287
#define CH_PAGE_W 31
/* Every picture of both files at most. */
#define LIST_MAX (2 * CTR_STEREO_MON_MAX_COUNT)
/* The keyboard: letters and digits, then DEL, SPACE, CLEAR and OK. */
#define KB_COLS 9
#define KB_ROWS 4
#define KB_X 7
#define KB_Y 40
#define KB_KEY_W 34
#define KB_KEY_H 30
#define KB_ROW2_Y (KB_Y + KB_ROWS * KB_KEY_H + 6)
#define QUERY_MAX 12

/* The zoom's steps (screen pixels a picture pixel), the brush's sizes, and
 * the tint's strengths (twentieths of the plane's colour over the pixel). */
static const uint8_t sZooms[] = {3, 4, 5, 6, 8, 10};
#define ZOOMS ((int)(sizeof(sZooms) / sizeof(sZooms[0])))
#define SIZES 4
static const uint8_t sOpacities[] = {15, 10, 6, 3};
static const char *const sOpacityNames[] = {"75", "50", "30", "15"};
#define OPACITIES ((int)(sizeof(sOpacities) / sizeof(sOpacities[0])))
/* The planes' colours, far to near (3ds_stereo_mon.h): the same for every
 * Pokémon, and the top screen's VIEW LAYERS uses them too. */
static const uint16_t sPlaneColours[PLANES] = CTR_STEREO_EDIT_COLOURS;

enum { S_EDIT, S_OPTIONS, S_CHOOSE, S_CONFIRM_RESET, S_KEYBOARD };
enum { O_DEPTH, O_PROPAGATE, O_PROPAGATE_NOW, O_AUTHOR, O_RESET, O_CLOSE, O_COUNT };
/* What the keyboard is typing. */
enum { KB_SEARCH, KB_AUTHOR };
/* Frames L is held before a press is a hold (a pick), not a step. */
#define L_HOLD_FRAMES 18
/* The C-stick: past this it has moved, back under it it is ready again. */
#define CSTICK_ON 60
#define CSTICK_OFF 30

typedef struct
{
    uint8_t planes[MAX_FRAMES * FRAME_BYTES];
} Snapshot;

static struct
{
    bool active;
    uint8_t screen;
    uint16_t id;
    uint8_t *pl;
    unsigned planes, frames;
    const uint16_t *pic[MAX_FRAMES];
    unsigned frame, layer;
    uint8_t tool, size, zoom, opacity, grid;
    bool propagate, outlineOn, layersView, battleView, animate, fromGallery;
    bool dirtyDraw, dirtyFile;
    /* Where picture pixel (0, 0) lies, in screen pixels from the canvas's
     * top left; the Circle Pad's movement below a pixel. */
    int originX, originY;
    int padX, padY;
    /* The lasso's loop while it is drawn, in canvas pixels. */
    int16_t path[LASSO_POINTS][2];
    unsigned pathLen;
    bool lassoing;
    /* A stroke: the pixels it changed (for the other frame), its name. */
    bool stroking;
    int lastX, lastY;
    uint8_t changed[N / 8];
    unsigned strokeCount;
    const char *strokeName;
    /* A drag moving the picture; a slider being dragged. */
    bool panning;
    int panX, panY;
    int slider;
    /* The keys held (for L + a tap), how long L has been, whether it
     * picked while held; the C-stick's latch. */
    unsigned heldKeys, lFrames;
    bool lPicked, cstickMoved;
    int lastTouchX, lastTouchY;
    /* A plane's row touched: picked on release, grabbed when held still. */
    bool rowTouch;
    int rowStartY, grabRow, grabTarget;
    unsigned holdFrames;
    /* The outline: the two darkest colours in use. */
    uint16_t outline[2];
    unsigned outlineCount;
    /* The colour opposite the Pokémon's own: the plane's outline. */
    uint16_t mark;
    /* The chooser: its list (gallery indices, filtered and ordered), the
     * cursor in it, only the edited ones, the edited ones first, the search. */
    unsigned chooseCursor;
    bool iconsMissing;
    bool onlyEdited, editedFirst;
    char query[QUERY_MAX + 1];
    /* The keyboard: what for, the text, the screen it came from. */
    uint8_t kbFor, kbReturn;
    char kbText[CTR_STEREO_EDIT_AUTHOR + 1];
    /* The pressed button, for the frame of the tap. */
    int pressed;
    char toast[40];
    unsigned toastFrames;
    uint32_t tick;
    bool touching;
    char name[CTR_STEREO_MON_NAME];
} sE;

static Snapshot sUndo[UNDO_DEPTH], sRedo[UNDO_DEPTH];
static unsigned sUndoCount, sUndoNext, sRedoCount;
static uint16_t sCanvas[W * H] __attribute__((aligned(32)));
/* The lasso's and the colour cells' working space. */
static int16_t sLabel[N];
static int16_t sQueue[N];
static uint8_t sMask[N / 8];
/* The chooser's list, and every picture's name as the search compares it
 * (capitals and digits only), made once. */
static uint16_t sList[LIST_MAX];
static unsigned sListCount;
static char sSearchNames[LIST_MAX][12];
static bool sSearchNamesMade;

/* ── Planes and pixels ──────────────────────────────────────────────── */

static inline unsigned GetPlane(unsigned f, unsigned i)
{
    unsigned k = f * N + i;
    unsigned v = (sE.pl[k >> 1] >> ((k & 1) * 4)) & 15;

    return v >= sE.planes ? NONE : v;
}

static inline void PutPlane(unsigned f, unsigned i, unsigned v)
{
    unsigned k = f * N + i;
    uint8_t *b = &sE.pl[k >> 1];

    if (k & 1) *b = (uint8_t)((*b & 0x0F) | (v << 4));
    else *b = (uint8_t)((*b & 0xF0) | v);
}

static inline bool Opaque(unsigned f, unsigned i) { return sE.pic[f] != NULL && sE.pic[f][i] != 0; }
static inline bool BitGet(const uint8_t *bits, unsigned i) { return (bits[i >> 3] >> (i & 7)) & 1; }
static inline void BitSet(uint8_t *bits, unsigned i) { bits[i >> 3] |= (uint8_t)(1 << (i & 7)); }

static bool IsOutline(uint16_t colour)
{
    for (unsigned k = 0; k < sE.outlineCount; ++k)
        if (sE.outline[k] == colour) return true;
    return false;
}

static unsigned Luma(uint16_t p)
{
    return 2 * ((p >> 11) & 31) + 5 * ((p >> 6) & 31) + ((p >> 1) & 31);
}

/* The two darkest colours the picture uses: its outline. */
static void FindOutline(void)
{
    uint16_t colours[64];
    unsigned count = 0;

    for (unsigned f = 0; f < sE.frames; ++f)
        for (unsigned i = 0; i < N; ++i)
        {
            uint16_t p = sE.pic[f][i];
            unsigned k;

            if (p == 0) continue;
            for (k = 0; k < count && colours[k] != p; ++k) {}
            if (k == count && count < 64) colours[count++] = p;
        }
    sE.outlineCount = 0;
    for (unsigned pick = 0; pick < 2 && pick < count; ++pick)
    {
        int best = -1;

        for (unsigned k = 0; k < count; ++k)
        {
            if (IsOutline(colours[k])) continue;
            if (best < 0 || Luma(colours[k]) < Luma(colours[best])) best = (int)k;
        }
        if (best >= 0) sE.outline[sE.outlineCount++] = colours[best];
    }
}

static uint16_t Rgb565Of(uint16_t rgba5551)
{
    unsigned r = (rgba5551 >> 11) & 31, g = (rgba5551 >> 6) & 31, b = (rgba5551 >> 1) & 31;

    return (uint16_t)((r << 11) | (((g << 1) | (g >> 4)) << 5) | b);
}

/* A fully saturated, full bright colour of the hue (degrees), RGB565. */
static uint16_t HueColour(float hue)
{
    float h = fmodf(hue, 360.0f) / 60.0f, x = 1.0f - fabsf(fmodf(h, 2.0f) - 1.0f), r, g, b;

    if (h < 0) h += 6.0f;
    switch ((int)h)
    {
    case 0: r = 1, g = x, b = 0; break;
    case 1: r = x, g = 1, b = 0; break;
    case 2: r = 0, g = 1, b = x; break;
    case 3: r = 0, g = x, b = 1; break;
    case 4: r = x, g = 0, b = 1; break;
    default: r = 1, g = 0, b = x; break;
    }
    return (uint16_t)(((unsigned)(r * 31.0f + 0.5f) << 11) | ((unsigned)(g * 63.0f + 0.5f) << 5)
                      | (unsigned)(b * 31.0f + 0.5f));
}

/* The Pokémon's own hue (its pixels' hues averaged round the colour wheel,
 * the saturated and bright ones counting most) turned half way round: a
 * colour that stands out against it. A grey Pokémon gets magenta. */
static void FindMark(void)
{
    const uint16_t *pic = sE.pic[0];
    float sx = 0.0f, sy = 0.0f, total = 0.0f;

    for (unsigned i = 0; i < N; ++i)
    {
        float r, g, b, mx, mn, hue, sat;

        if (pic[i] == 0) continue;
        r = (float)((pic[i] >> 11) & 31);
        g = (float)((pic[i] >> 6) & 31);
        b = (float)((pic[i] >> 1) & 31);
        mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
        mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
        total += 1.0f;
        if (mx <= 0.0f || mx == mn) continue;
        sat = (mx - mn) / mx;
        if (mx == r) hue = 60.0f * fmodf((g - b) / (mx - mn) + 6.0f, 6.0f);
        else if (mx == g) hue = 60.0f * ((b - r) / (mx - mn) + 2.0f);
        else hue = 60.0f * ((r - g) / (mx - mn) + 4.0f);
        sx += sat * mx * cosf(hue * 3.14159265f / 180.0f);
        sy += sat * mx * sinf(hue * 3.14159265f / 180.0f);
    }
    if (total == 0.0f || sqrtf(sx * sx + sy * sy) < total * 2.0f)
        sE.mark = HueColour(300.0f);
    else
        sE.mark = HueColour(atan2f(sy, sx) * 180.0f / 3.14159265f + 180.0f);
}

/* ── History ────────────────────────────────────────────────────────── */

static void Snap(Snapshot *s) { memcpy(s->planes, sE.pl, sE.frames * FRAME_BYTES); }

static void Restore(const Snapshot *s) { memcpy(sE.pl, s->planes, sE.frames * FRAME_BYTES); }

static void PushUndo(void)
{
    Snap(&sUndo[sUndoNext]);
    sUndoNext = (sUndoNext + 1) % UNDO_DEPTH;
    if (sUndoCount < UNDO_DEPTH) ++sUndoCount;
    sRedoCount = 0;
}

static void Toast(const char *text)
{
    snprintf(sE.toast, sizeof(sE.toast), "%s", text);
    sE.toastFrames = TOAST_FRAMES;
    sE.dirtyDraw = true;
}

static void Changed(void)
{
    sE.dirtyDraw = true;
    sE.dirtyFile = true;
    CtrStereoMon_Recompose();
}

static void Undo(void)
{
    if (sUndoCount == 0)
    {
        Toast("NOTHING TO UNDO");
        return;
    }
    sUndoNext = (sUndoNext + UNDO_DEPTH - 1) % UNDO_DEPTH;
    --sUndoCount;
    if (sRedoCount < UNDO_DEPTH) Snap(&sRedo[sRedoCount++]);
    Restore(&sUndo[sUndoNext]);
    Changed();
    Toast("UNDONE");
}

static void Redo(void)
{
    if (sRedoCount == 0)
    {
        Toast("NOTHING TO REDO");
        return;
    }
    Snap(&sUndo[sUndoNext]);
    sUndoNext = (sUndoNext + 1) % UNDO_DEPTH;
    if (sUndoCount < UNDO_DEPTH) ++sUndoCount;
    Restore(&sRedo[--sRedoCount]);
    Changed();
    Toast("REDONE");
}

/* ── The other frame ────────────────────────────────────────────────── */

/* Every pixel of the other frame whose nearest pixel of the same colour on
 * this frame is one the stroke changed takes that pixel's plane (the
 * packer's propagation, gen_stereo_mons.py). */
static unsigned PropagateTo(unsigned g, const uint8_t *changed)
{
    unsigned f = sE.frame, carried = 0;
    const uint16_t *src = sE.pic[f], *dst = sE.pic[g];

    if (g == f || g >= sE.frames || src == NULL || dst == NULL) return 0;
    for (unsigned j = 0; j < N; ++j)
    {
        uint16_t c = dst[j];
        int x = (int)(j % DIM), y = (int)(j / DIM), best = -1, bestD = 1 << 30;

        if (c == 0) continue;
        for (int r = 0; r <= PROPAGATE_RADIUS; ++r)
        {
            if (best >= 0 && r * r > bestD) break;
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx)
                {
                    int nx, ny, d;
                    unsigned i;

                    if (dy != -r && dy != r && dx != -r && dx != r) continue; /* the ring only */
                    nx = x + dx;
                    ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= DIM || ny >= DIM) continue;
                    i = (unsigned)(ny * DIM + nx);
                    if (src[i] != c || GetPlane(f, i) == NONE) continue;
                    d = dx * dx + dy * dy;
                    if (d < bestD)
                    {
                        bestD = d;
                        best = (int)i;
                    }
                }
        }
        if (best >= 0 && (changed == NULL || BitGet(changed, (unsigned)best)))
        {
            unsigned v = GetPlane(f, (unsigned)best);

            if (GetPlane(g, j) != v)
            {
                PutPlane(g, j, v);
                ++carried;
            }
        }
    }
    return carried;
}

static unsigned PropagateChanged(const uint8_t *changed)
{
    unsigned carried = 0;

    for (unsigned g = 0; g < sE.frames; ++g) carried += PropagateTo(g, changed);
    return carried;
}

/* ── Strokes ────────────────────────────────────────────────────────── */

static void BeginStroke(const char *name)
{
    PushUndo();
    memset(sE.changed, 0, sizeof(sE.changed));
    sE.strokeCount = 0;
    sE.strokeName = name;
    sE.stroking = true;
    sE.lastX = sE.lastY = -1;
}

static void EndStroke(void)
{
    char line[64];
    unsigned carried = 0;

    if (!sE.stroking) return;
    sE.stroking = false;
    if (sE.strokeCount == 0)
    {
        /* Nothing changed: the snapshot goes. */
        sUndoNext = (sUndoNext + UNDO_DEPTH - 1) % UNDO_DEPTH;
        --sUndoCount;
        return;
    }
    if (sE.propagate) carried = PropagateChanged(sE.changed);
    snprintf(line, sizeof(line), "f%u %s P%u %upx +%u", sE.frame, sE.strokeName, sE.layer, sE.strokeCount, carried);
    CtrStereoMon_LogEdit(line);
    Changed();
}

static void SetPixel(unsigned i)
{
    if (!Opaque(sE.frame, i) || GetPlane(sE.frame, i) == sE.layer) return;
    PutPlane(sE.frame, i, sE.layer);
    BitSet(sE.changed, i);
    ++sE.strokeCount;
}

/* The brush at one picture pixel: a disc `size` pixels across. */
static void BrushAt(int x, int y)
{
    int r = (int)sE.size - 1;

    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
        {
            int px = x + dx, py = y + dy;

            if (px < 0 || py < 0 || px >= DIM || py >= DIM || dx * dx + dy * dy > r * r + r) continue;
            SetPixel((unsigned)(py * DIM + px));
        }
}

/* The brush along the line from the last touch to this one. */
static void BrushLine(int x, int y)
{
    int x0 = sE.lastX < 0 ? x : sE.lastX, y0 = sE.lastY < 0 ? y : sE.lastY;
    int dx = x - x0, dy = y - y0, steps = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);

    if (steps == 0) BrushAt(x, y);
    for (int s = 1; s <= steps; ++s) BrushAt(x0 + dx * s / steps, y0 + dy * s / steps);
    sE.lastX = x;
    sE.lastY = y;
}

/* ── The view ───────────────────────────────────────────────────────── */

static int Zoom(void) { return sZooms[sE.zoom]; }

/* The picture kept at least partly on the canvas. */
static void ClampView(void)
{
    int side = DIM * Zoom(), keep = 32;

    if (sE.originX > CAN_W - keep) sE.originX = CAN_W - keep;
    if (sE.originX < keep - side) sE.originX = keep - side;
    if (sE.originY > H - keep) sE.originY = H - keep;
    if (sE.originY < keep - side) sE.originY = keep - side;
}

/* The picture pixel under a screen point, or false off the picture. */
static bool PictureAt(int x, int y, int *px, int *py)
{
    int dx = x - CAN_X - sE.originX, dy = y - sE.originY, s = Zoom();

    if (dx < 0 || dy < 0) return false;
    *px = dx / s;
    *py = dy / s;
    return *px < DIM && *py < DIM;
}

/* Another zoom, the picture pixel at canvas point (cx, cy) staying put. */
static void SetZoom(int step, int cx, int cy)
{
    int old = Zoom();

    if (step < 0) step = 0;
    if (step >= (int)ZOOMS) step = ZOOMS - 1;
    if (step == sE.zoom) return;
    sE.zoom = (uint8_t)step;
    sE.originX = cx - (cx - sE.originX) * Zoom() / old;
    sE.originY = cy - (cy - sE.originY) * Zoom() / old;
    ClampView();
    sE.dirtyDraw = true;
}

/* The box the picture's opaque pixels fill on this frame, made square. */
static void PictureBox(int *x0, int *y0, int *side)
{
    const uint16_t *pic = sE.pic[sE.frame];
    int minX = DIM, minY = DIM, maxX = -1, maxY = -1, w, h, s;

    for (int y = 0; y < DIM; ++y)
        for (int x = 0; x < DIM; ++x)
            if (pic[y * DIM + x] != 0)
            {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
    if (maxX < 0) { *x0 = *y0 = 0; *side = DIM; return; }
    w = maxX - minX + 1;
    h = maxY - minY + 1;
    s = w > h ? w : h;
    *x0 = minX - (s - w) / 2;
    *y0 = minY - (s - h) / 2;
    *side = s;
}

/* The Pokémon as large as the canvas takes it, in the middle of what the
 * sliders and the bar leave free. */
#define FREE_X (SL_SIZE_X + SL_REACH + 2)
#define FREE_Y (BAR_Y + BAR_H + 2)
static void ResetView(void)
{
    int bx, by, side, room = (CAN_W - FREE_X) < (H - FREE_Y) ? (CAN_W - FREE_X) : (H - FREE_Y);

    PictureBox(&bx, &by, &side);
    sE.zoom = 0;
    while (sE.zoom + 1 < ZOOMS && side * sZooms[sE.zoom + 1] <= room - 4) ++sE.zoom;
    sE.originX = FREE_X - CAN_X + (CAN_W - FREE_X - side * Zoom()) / 2 - bx * Zoom();
    sE.originY = FREE_Y + (H - FREE_Y - side * Zoom()) / 2 - by * Zoom();
    ClampView();
}

/* ── Lasso: loops and colour cells ──────────────────────────────────── */

/* The colour cell of picture pixel i: connected pixels of its colour that
 * is not the outline (transparency and the outline bound them), into
 * sMask with the outline pixels that touch it; an outline pixel alone. */
static void ColourCell(unsigned s)
{
    const uint16_t *pic = sE.pic[sE.frame];
    static const int8_t nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    unsigned head = 0, tail = 0;

    memset(sMask, 0, sizeof(sMask));
    if (pic[s] == 0) return;
    if (IsOutline(pic[s]))
    {
        BitSet(sMask, s);
        return;
    }
    for (unsigned i = 0; i < N; ++i) sLabel[i] = -1;
    sLabel[s] = 0;
    sQueue[tail++] = (int16_t)s;
    while (head < tail)
    {
        unsigned j = (unsigned)sQueue[head++];
        int x = (int)(j % DIM), y = (int)(j / DIM);

        BitSet(sMask, j);
        for (unsigned n = 0; n < 4; ++n)
        {
            int nx = x + nb[n][0], ny = y + nb[n][1];
            unsigned k;

            if (nx < 0 || ny < 0 || nx >= DIM || ny >= DIM) continue;
            k = (unsigned)(ny * DIM + nx);
            if (sLabel[k] >= 0) continue;
            if (pic[k] == pic[s])
            {
                sLabel[k] = 0;
                sQueue[tail++] = (int16_t)k;
            }
            else if (pic[k] != 0 && IsOutline(pic[k]))
            {
                /* The outline touching the cell joins it, no further. */
                sLabel[k] = 1;
                BitSet(sMask, k);
            }
        }
    }
}

/* Whether the picture pixel's centre lies inside the lasso's loop. */
static bool InsideLoop(unsigned i)
{
    int s = Zoom();
    /* Doubled, so the centre is a whole number. */
    int px = 2 * sE.originX + (2 * (int)(i % DIM) + 1) * s, py = 2 * sE.originY + (2 * (int)(i / DIM) + 1) * s;
    bool inside = false;

    for (unsigned a = 0, b = sE.pathLen - 1; a < sE.pathLen; b = a++)
    {
        int ax = sE.path[a][0] * 2, ay = sE.path[a][1] * 2, bx = sE.path[b][0] * 2, by = sE.path[b][1] * 2;

        if ((ay > py) != (by > py))
        {
            long x = (long)ax + (long)(bx - ax) * (py - ay) / (by - ay);

            if (px < x) inside = !inside;
        }
    }
    return inside;
}

static void FinishLasso(void)
{
    int minX = 1 << 20, minY = 1 << 20, maxX = -(1 << 20), maxY = -(1 << 20);
    char line[40];

    sE.lassoing = false;
    sE.dirtyDraw = true;
    for (unsigned a = 0; a < sE.pathLen; ++a)
    {
        if (sE.path[a][0] < minX) minX = sE.path[a][0];
        if (sE.path[a][0] > maxX) maxX = sE.path[a][0];
        if (sE.path[a][1] < minY) minY = sE.path[a][1];
        if (sE.path[a][1] > maxY) maxY = sE.path[a][1];
    }
    if (sE.pathLen == 0) return;
    if (maxX - minX < TAP_SLOP && maxY - minY < TAP_SLOP)
    {
        /* A tap: the colour cell under it. */
        int px, py;

        if (!PictureAt(CAN_X + sE.path[0][0], sE.path[0][1], &px, &py) || !Opaque(sE.frame, (unsigned)(py * DIM + px)))
        {
            Toast("FILL: TAP A COLOUR OR LOOP A PART");
            return;
        }
        ColourCell((unsigned)(py * DIM + px));
        BeginStroke("fill");
    }
    else
    {
        memset(sMask, 0, sizeof(sMask));
        for (unsigned i = 0; i < N; ++i)
            if (InsideLoop(i)) BitSet(sMask, i);
        BeginStroke("lasso");
    }
    for (unsigned i = 0; i < N; ++i)
        if (BitGet(sMask, i)) SetPixel(i);
    if (sE.strokeCount) snprintf(line, sizeof(line), "%u PIXELS TO PLANE %u", sE.strokeCount, PLANES - sE.layer);
    else snprintf(line, sizeof(line), "ALREADY ON PLANE %u", PLANES - sE.layer);
    EndStroke();
    Toast(line);
}

/* ── Planes ─────────────────────────────────────────────────────────── */

/* Every Pokémon on the editor's four planes: more folded into them (as the
 * packer does, collapse_stereo_planes.py), fewer spread over them so each
 * keeps its place between the nearest and the farthest. */
static void FourPlanes(void)
{
    unsigned from = sE.planes;
    char line[32];

    if (from == PLANES) return;
    for (unsigned f = 0; f < sE.frames; ++f)
        for (unsigned i = 0; i < N; ++i)
        {
            unsigned k = f * N + i, v = (sE.pl[k >> 1] >> ((k & 1) * 4)) & 15;

            if (v == NONE) continue;
            if (v >= from) PutPlane(f, i, NONE);
            else if (from > PLANES) PutPlane(f, i, v * PLANES / from);
            else PutPlane(f, i, from > 1 ? v * (PLANES - 1) / (from - 1) : PLANES - 1);
        }
    sE.planes = PLANES;
    CtrStereoMon_SetPlaneCount(PLANES);
    snprintf(line, sizeof(line), "planes %u to %u", from, PLANES);
    CtrStereoMon_LogEdit(line);
}

/* Plane `from` moved to position `to` in the stack, the others closing up. */
static void MovePlane(unsigned from, unsigned to)
{
    if (from == to || from >= sE.planes || to >= sE.planes) return;
    PushUndo();
    for (unsigned f = 0; f < sE.frames; ++f)
        for (unsigned i = 0; i < N; ++i)
        {
            unsigned v = GetPlane(f, i), nv = v;

            if (v == NONE) continue;
            if (v == from) nv = to;
            else if (from < to && v > from && v <= to) nv = v - 1;
            else if (to < from && v >= to && v < from) nv = v + 1;
            if (nv != v) PutPlane(f, i, nv);
        }
    if (sE.layer == from) sE.layer = to;
    CtrStereoMon_LogEdit("move-plane");
    Changed();
    Toast(to > from ? "PLANE MOVED NEARER" : "PLANE MOVED FARTHER");
}

static void PickPlane(unsigned plane)
{
    char line[24];

    sE.layer = plane;
    snprintf(line, sizeof(line), "PLANE %u", PLANES - plane);
    Toast(line);
}

/* The other frame, when there is one. */
static void OtherFrame(void)
{
    char line[24];

    if (sE.frames < 2) return;
    sE.frame = (sE.frame + 1) % sE.frames;
    snprintf(line, sizeof(line), "FRAME %u OF %u", sE.frame + 1, sE.frames);
    Toast(line);
}

/* ── The file ───────────────────────────────────────────────────────── */

static void Save(bool quiet)
{
    if (!sE.dirtyFile)
    {
        if (!quiet) Toast("NOTHING TO SAVE");
        return;
    }
    if (CtrStereoMon_SaveEdit())
    {
        sE.dirtyFile = false;
        if (!quiet) Toast("SAVED TO SD");
    }
    else
        Toast("SAVE FAILED");
}

/* ── Binding to the viewer's Pokémon ────────────────────────────────── */

/* The plane with the most pixels: the one edited first. */
static unsigned BusiestPlane(void)
{
    unsigned counts[PLANES] = {0}, best = PLANES - 1;

    for (unsigned i = 0; i < N; ++i)
    {
        unsigned v = GetPlane(0, i);

        if (v != NONE && Opaque(0, i)) ++counts[v];
    }
    for (unsigned p = 0; p < PLANES; ++p)
        if (counts[p] > counts[best]) best = p;
    return best;
}

static bool Bind(void)
{
    uint16_t id = CtrStereoMon_Shown();
    unsigned planes, frames;
    uint8_t *pl = CtrStereoMon_EditPlanes(&planes, &frames);

    if (pl == NULL || id == 0) return false;
    if (id == sE.id && pl == sE.pl) return true;
    sE.id = id;
    sE.pl = pl;
    sE.planes = planes;
    sE.frames = frames;
    for (unsigned f = 0; f < MAX_FRAMES; ++f) sE.pic[f] = f < frames ? CtrStereoMon_Picture(f) : NULL;
    sE.frame = 0;
    sE.lassoing = sE.stroking = sE.panning = false;
    sE.dirtyFile = false;
    sE.dirtyDraw = true;
    sUndoCount = sUndoNext = sRedoCount = 0;
    FourPlanes();
    sE.layer = BusiestPlane();
    FindOutline();
    FindMark();
    ResetView();
    CtrStereoMonGame_Name(id, sE.name, sizeof(sE.name));
    CtrStereoMon_SetEditing(true, sE.layer);
    CtrLog_Write(CTR_LOG_VIDEO, "stereo edit: id %04x %s, %u planes (now %u), %u frames, mark %04x", id, sE.name,
                 planes, sE.planes, frames, sE.mark);
    return true;
}

bool CtrStereoEdit_Active(void) { return sE.active; }

void CtrStereoEdit_Open(bool fromGallery)
{
    if (sE.active) return;
    memset(&sE, 0, sizeof(sE));
    sE.active = true;
    sE.screen = S_EDIT;
    sE.size = 1;
    sE.pressed = -1;
    sE.grabRow = -1;
    sE.slider = SLIDER_NONE;
    sE.propagate = CtrSettings_GetInt("edit_propagate", 1) != 0;
    sE.outlineOn = CtrSettings_GetInt("edit_outline", 1) != 0;
    sE.grid = (uint8_t)CtrSettings_GetInt("edit_grid", GRID_OFF);
    if (sE.grid > GRID_BLACK) sE.grid = GRID_OFF;
    sE.layersView = CtrSettings_GetInt("edit_layers", 0) != 0;
    sE.opacity = (uint8_t)CtrSettings_GetInt("edit_opacity", 1);
    if (sE.opacity >= OPACITIES) sE.opacity = 1;
    sE.fromGallery = fromGallery;
    sE.dirtyDraw = true;
    CtrStereoMon_EditScan();
    Bind();
}

static void Close(bool leaveGallery)
{
    if (!sE.active) return;
    Save(true);
    CtrStereoMon_SetEditing(false, 0);
    sE.active = false;
    sE.pl = NULL;
    CtrBottom_ForceRedraw();
    if (leaveGallery) CtrStereoMonGame_CloseGallery();
}

/* Another Pokémon: saved, then the gallery is told; Bind picks it up. */
static void Choose(unsigned index)
{
    Save(true);
    sE.screen = S_EDIT;
    sE.pl = NULL;
    sE.id = 0;
    CtrStereoMonGame_SelectIndex(index);
    sE.dirtyDraw = true;
}

/* ── The chooser's list ─────────────────────────────────────────────── */

/* Every picture's name as the search compares it: capitals and digits. */
static void MakeSearchNames(void)
{
    unsigned count = CtrStereoMon_GalleryCount();
    char name[CTR_STEREO_MON_NAME];

    if (sSearchNamesMade) return;
    sSearchNamesMade = true;
    for (unsigned i = 0; i < count && i < LIST_MAX; ++i)
    {
        unsigned n = 0;

        CtrStereoMonGame_Name(CtrStereoMon_GalleryIdAt(i), name, sizeof(name));
        for (const char *c = name; *c && n + 1 < sizeof(sSearchNames[i]); ++c)
            if ((*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9')) sSearchNames[i][n++] = *c;
        sSearchNames[i][n] = 0;
    }
}

/* Whether gallery entry `index` answers the search: its name holds the
 * letters typed, or its number the digits. */
static bool Matches(unsigned index)
{
    char want[QUERY_MAX + 1], number[8];
    unsigned n = 0;
    bool digits = true;

    for (const char *c = sE.query; *c; ++c)
        if (*c != ' ')
        {
            want[n++] = *c;
            if (*c < '0' || *c > '9') digits = false;
        }
    want[n] = 0;
    if (n == 0) return true;
    if (strstr(sSearchNames[index], want)) return true;
    snprintf(number, sizeof(number), "%03u", CtrStereoMon_GalleryIdAt(index) & 0x3FFF);
    return digits && strstr(number, want) != NULL;
}

/* The list the chooser shows: every picture, or the edited ones only,
 * those answering the search, the edited ones first when asked. */
static void BuildList(void)
{
    unsigned count = CtrStereoMon_GalleryCount();

    if (count > LIST_MAX) count = LIST_MAX;
    if (sE.query[0]) MakeSearchNames();
    sListCount = 0;
    for (unsigned pass = 0; pass < 2; ++pass)
        for (unsigned i = 0; i < count; ++i)
        {
            bool edited = CtrStereoMon_HasEdit(CtrStereoMon_GalleryIdAt(i));

            if (sE.onlyEdited && !edited) continue;
            /* Edited first: those on the first pass, the rest on the second. */
            if (sE.editedFirst && !sE.onlyEdited ? edited != (pass == 0) : pass == 1) continue;
            if (sE.query[0] && !Matches(i)) continue;
            sList[sListCount++] = (uint16_t)i;
        }
    if (sE.chooseCursor >= sListCount) sE.chooseCursor = sListCount ? sListCount - 1 : 0;
    sE.dirtyDraw = true;
}

/* The chooser, its cursor on the picture being edited when it is listed. */
static void OpenChooser(void)
{
    unsigned index = CtrStereoMonGame_GalleryIndex();

    Save(true);
    sE.screen = S_CHOOSE;
    sE.chooseCursor = 0;
    BuildList();
    for (unsigned k = 0; k < sListCount; ++k)
        if (sList[k] == index) sE.chooseCursor = k;
}

static void OpenKeyboard(uint8_t what)
{
    sE.kbFor = what;
    sE.kbReturn = sE.screen;
    snprintf(sE.kbText, sizeof(sE.kbText), "%s", what == KB_SEARCH ? sE.query : CtrStereoMon_Author());
    sE.screen = S_KEYBOARD;
    sE.dirtyDraw = true;
}

/* The keyboard's text taken: the search narrows the chooser, the author's
 * name signs the saves from now on. */
static void KeyboardDone(void)
{
    if (sE.kbFor == KB_SEARCH)
    {
        sE.screen = S_CHOOSE;
        sE.chooseCursor = 0;
        BuildList();
    }
    else
    {
        char line[40];

        CtrStereoMon_SetAuthor(sE.kbText);
        sE.screen = S_EDIT;
        snprintf(line, sizeof(line), "SAVES SIGNED %s", CtrStereoMon_Author()[0] ? CtrStereoMon_Author() : "BY NOBODY");
        Toast(line);
    }
    sE.dirtyDraw = true;
}

static void KeyboardType(char c)
{
    char *text = sE.kbFor == KB_SEARCH ? sE.query : sE.kbText;
    unsigned max = sE.kbFor == KB_SEARCH ? QUERY_MAX : CTR_STEREO_EDIT_AUTHOR, n = (unsigned)strlen(text);

    if (c == '\b')
    {
        if (n) text[n - 1] = 0;
    }
    else if (c == 0)
        text[0] = 0;
    else if (n < max && !(c == ' ' && (n == 0 || text[n - 1] == ' ')))
    {
        text[n] = c;
        text[n + 1] = 0;
    }
    /* The search narrows as it is typed. */
    if (sE.kbFor == KB_SEARCH)
    {
        sE.chooseCursor = 0;
        BuildList();
    }
    sE.dirtyDraw = true;
}

/* ── Keys ───────────────────────────────────────────────────────────── */

/* The D-pad: zoom up and down (about the canvas's middle), undo and redo. */
static void DpadKeys(unsigned dpad)
{
    if (dpad & CTR_KEY_UP) SetZoom(sE.zoom + 1, CAN_W / 2, H / 2);
    if (dpad & CTR_KEY_DOWN) SetZoom(sE.zoom - 1, CAN_W / 2, H / 2);
    if (dpad & CTR_KEY_LEFT) Undo();
    if (dpad & CTR_KEY_RIGHT) Redo();
}

static void ToggleLayersView(void)
{
    sE.layersView = !sE.layersView;
    CtrSettings_SetInt("edit_layers", sE.layersView);
    Toast(sE.layersView ? "TOP SCREEN: LAYERS" : "TOP SCREEN: COLOURS");
}

static void ChooseKeys(unsigned down)
{
    unsigned count = sListCount;

    if (down & (CTR_KEY_B | CTR_KEY_SELECT)) { sE.screen = S_EDIT; return; }
    if (down & CTR_KEY_Y) { OpenKeyboard(KB_SEARCH); return; }
    if (down & CTR_KEY_X)
    {
        sE.onlyEdited = !sE.onlyEdited;
        BuildList();
        return;
    }
    if (count == 0) return;
    if (down & CTR_KEY_A) Choose(sList[sE.chooseCursor]);
    else if (down & CTR_KEY_RIGHT) sE.chooseCursor = (sE.chooseCursor + 1) % count;
    else if (down & CTR_KEY_LEFT) sE.chooseCursor = (sE.chooseCursor + count - 1) % count;
    else if (down & CTR_KEY_DOWN) sE.chooseCursor = (sE.chooseCursor + CH_COLS) % count;
    else if (down & CTR_KEY_UP) sE.chooseCursor = (sE.chooseCursor + count - CH_COLS % count) % count;
    else if (down & CTR_KEY_R) sE.chooseCursor = (sE.chooseCursor + CH_PER_PAGE) % count;
    else if (down & CTR_KEY_L) sE.chooseCursor = (sE.chooseCursor + count - CH_PER_PAGE % count) % count;
}

void CtrStereoEdit_Keys(unsigned down, unsigned held)
{
    const CtrInput *in = CtrInput_Get();
    bool lReleased = (sE.heldKeys & CTR_KEY_L) && !(held & CTR_KEY_L);
    unsigned lFrames = sE.lFrames;

    if (!sE.active) return;
    if ((held ^ sE.heldKeys) & (CTR_KEY_L | CTR_KEY_R)) sE.dirtyDraw = true;
    sE.heldKeys = held;
    sE.lFrames = (held & CTR_KEY_L) ? sE.lFrames + 1 : 0;
    sE.dirtyDraw = sE.dirtyDraw || down != 0;
    if (sE.screen == S_KEYBOARD)
    {
        /* B rubs out, and leaves once there is nothing to rub out; START is OK. */
        const char *text = sE.kbFor == KB_SEARCH ? sE.query : sE.kbText;

        if (down & CTR_KEY_START) KeyboardDone();
        else if ((down & CTR_KEY_B) && text[0]) KeyboardType('\b');
        else if (down & CTR_KEY_B) sE.screen = sE.kbReturn == S_OPTIONS ? S_EDIT : sE.kbReturn;
        return;
    }
    if (sE.screen == S_CHOOSE)
    {
        ChooseKeys(down);
        return;
    }
    if (sE.screen == S_OPTIONS || sE.screen == S_CONFIRM_RESET)
    {
        if (down & (CTR_KEY_B | CTR_KEY_START)) sE.screen = S_EDIT;
        return;
    }
    /* A short press of L: the next plane (held, it is the picker). */
    if (down & CTR_KEY_L) sE.lPicked = false;
    if (lReleased && !sE.lPicked && lFrames < L_HOLD_FRAMES)
        PickPlane(sE.layer == 0 ? PLANES - 1 : sE.layer - 1);
    if (down & CTR_KEY_R) OtherFrame();
    if (down & CTR_KEY_SELECT)
    {
        OpenChooser();
        return;
    }
    if (down & CTR_KEY_B) { Close(!sE.fromGallery); return; }
    if (down & CTR_KEY_START) Save(false);
    if (down & CTR_KEY_A) sE.tool = T_BRUSH;
    if (down & CTR_KEY_X) ToggleLayersView();
    if (down & CTR_KEY_Y)
    {
        sE.animate = !sE.animate;
        Toast(sE.animate ? "ANIMATING" : "STILL");
    }
    /* The D-pad only: the Circle Pad pans (CtrStereoEdit_BottomFrame). */
    DpadKeys(down & in->physicalDown & CTR_KEY_DPAD);
}

/* ── Touch ──────────────────────────────────────────────────────────── */

/* The value a slider's track gives at screen row y. */
static int SliderValue(int y, int steps)
{
    int v = ((SL_BOTTOM - y) * (steps - 1) * 2 + (SL_BOTTOM - SL_TOP)) / (2 * (SL_BOTTOM - SL_TOP));

    return v < 0 ? 0 : v >= steps ? steps - 1 : v;
}

static void SliderAt(int y)
{
    if (sE.slider == SLIDER_ZOOM) SetZoom(SliderValue(y, ZOOMS), CAN_W / 2, H / 2);
    else if (sE.slider == SLIDER_SIZE) sE.size = (uint8_t)(1 + SliderValue(y, SIZES));
    sE.dirtyDraw = true;
}

static int RowAt(int y)
{
    int row = (y - ROW_Y0) / ROW_STEP;

    return y < ROW_Y0 ? 0 : row >= PLANES ? PLANES - 1 : row;
}

/* The left's buttons: where each is. */
static void LeftButton(int b, int *x, int *y)
{
    *x = (b % 2 ? SL_SIZE_X : SL_ZOOM_X) - TOOL_W / 2;
    *y = TOOL_Y0 + (b / 2) * TOOL_STEP;
}

static int LeftButtonAt(int x, int y)
{
    for (int b = 0; b < L_COUNT; ++b)
    {
        int bx, by;

        LeftButton(b, &bx, &by);
        if (x >= bx && x < bx + TOOL_W && y >= by && y < by + TOOL_H) return b;
    }
    return -1;
}

static void LeftAct(int b)
{
    static const uint8_t tools[] = {T_PAN, T_BRUSH, T_FILL};

    if (b == L_UNDO) Undo();
    else sE.tool = tools[b];
}

static void RightAct(int button)
{
    char line[24];

    switch (button)
    {
    case B_GRID_OFF: case B_GRID_WHITE: case B_GRID_BLACK:
        sE.grid = (uint8_t)(button - B_GRID_OFF);
        CtrSettings_SetInt("edit_grid", sE.grid);
        break;
    case B_OPACITY:
        sE.opacity = (uint8_t)((sE.opacity + 1) % OPACITIES);
        CtrSettings_SetInt("edit_opacity", sE.opacity);
        snprintf(line, sizeof(line), "TINT STRENGTH %s", sOpacityNames[sE.opacity]);
        Toast(line);
        break;
    case B_OUTLINE:
        sE.outlineOn = !sE.outlineOn;
        CtrSettings_SetInt("edit_outline", sE.outlineOn);
        Toast(sE.outlineOn ? "OUTLINE ON" : "OUTLINE OFF");
        break;
    default: sE.screen = S_OPTIONS; break;
    }
}

/* The front's back, the back's front. */
static void SwapSide(bool toBack)
{
    uint16_t other = CtrStereoMon_Counterpart(sE.id);
    int index;

    if (toBack == ((sE.id & 0xC000) != 0)) return;
    if (other == 0 || (index = CtrStereoMon_GalleryIndexOf(other)) < 0)
    {
        Toast(toBack ? "NO BACK IN THE PACK" : "NO FRONT IN THE PACK");
        return;
    }
    Choose((unsigned)index);
}

static void ToggleFinal(void)
{
    bool final = !CtrStereoMon_EditMeta()->finalized;

    CtrStereoMon_SetFinalized(final);
    sE.dirtyFile = true;
    Save(true);
    Toast(final ? "MARKED FINAL" : "NOT FINAL ANY MORE");
}

/* The bar over the picture's top: a tap on one of its buttons. */
static bool BarAct(int x, int y)
{
    if (y < BAR_Y || y >= BAR_Y + BAR_H) return false;
    if (x >= DEX_X && x < DEX_X + DEX_W) { sE.pressed = P_DEX; OpenChooser(); }
    else if (x >= FB_X && x < FB_X + FB_W) { sE.pressed = P_FRONT; SwapSide(false); }
    else if (x >= FB_X + FB_W && x < FB_X + 2 * FB_W) { sE.pressed = P_BACK; SwapSide(true); }
    else if (x >= FRAME_X && x < FRAME_X + FRAME_W)
    {
        sE.pressed = P_FRAME;
        if (sE.frames > 1) OtherFrame();
        else Toast("ONE FRAME ONLY");
    }
    else if (x >= BATTLE_X && x < BATTLE_X + BATTLE_W)
    {
        sE.pressed = P_BATTLE;
        sE.battleView = !sE.battleView;
        Toast(sE.battleView ? "TOP SCREEN: AS IN BATTLE" : "TOP SCREEN: EDITOR");
    }
    else if (x >= FINAL_X && x < FINAL_X + FINAL_W) { sE.pressed = P_FINAL; ToggleFinal(); }
    else return false;
    return true;
}

static void TouchEdit(const CtrInput *in)
{
    int x = in->touchX, y = in->touchY;

    if (in->touchDown)
    {
        int b;

        sE.pressed = P_NONE;
        if (x >= RIGHT_X)
        {
            if (y < ROWS_END)
            {
                sE.rowTouch = true;
                sE.rowStartY = y;
                sE.holdFrames = 0;
                sE.grabRow = -1;
            }
            else if (y >= BTN_Y0)
            {
                int col = (x - ROW_X) / BTN_STEP_X, row = (y - BTN_Y0) / BTN_STEP_Y;

                if (col >= 0 && col < 3 && row >= 0 && row < 2)
                {
                    sE.pressed = P_RIGHT + row * 3 + col;
                    RightAct(row * 3 + col);
                }
            }
        }
        else if (BarAct(x, y)) {}
        else if (y >= SL_TOP - KNOB_R - 2 && y <= SL_BOTTOM + KNOB_R + 2
                 && ((x >= SL_ZOOM_X - SL_REACH && x <= SL_ZOOM_X + SL_REACH) || (x >= SL_SIZE_X - SL_REACH && x <= SL_SIZE_X + SL_REACH)))
        {
            sE.slider = x <= SL_ZOOM_X + SL_REACH ? SLIDER_ZOOM : SLIDER_SIZE;
            SliderAt(y);
        }
        else if ((b = LeftButtonAt(x, y)) >= 0)
        {
            sE.pressed = P_LEFT + b;
            LeftAct(b);
        }
        else if (sE.heldKeys & CTR_KEY_L)
        {
            /* L + a tap: the plane under the stylus. */
            int px, py;

            sE.lPicked = true;
            if (PictureAt(x, y, &px, &py) && Opaque(sE.frame, (unsigned)(py * DIM + px))
                && GetPlane(sE.frame, (unsigned)(py * DIM + px)) != NONE)
                PickPlane(GetPlane(sE.frame, (unsigned)(py * DIM + px)));
            else
                Toast("NO PLANE THERE");
        }
        else if (sE.tool == T_PAN)
        {
            sE.panning = true;
            sE.panX = x;
            sE.panY = y;
        }
        else if (sE.tool == T_FILL)
        {
            sE.lassoing = true;
            sE.path[0][0] = (int16_t)(x - CAN_X);
            sE.path[0][1] = (int16_t)y;
            sE.pathLen = 1;
        }
        else
        {
            int px, py;

            BeginStroke("brush");
            if (PictureAt(x, y, &px, &py)) BrushLine(px, py);
        }
        sE.dirtyDraw = true;
    }
    else if (in->touchActive)
    {
        if (sE.slider != SLIDER_NONE)
            SliderAt(y);
        else if (sE.rowTouch)
        {
            int dy = y - sE.rowStartY;

            if (sE.grabRow >= 0)
            {
                int row = RowAt(y);

                if (row != sE.grabTarget) { sE.grabTarget = row; sE.dirtyDraw = true; }
            }
            else if (dy > -6 && dy < 6 && ++sE.holdFrames == GRAB_FRAMES)
            {
                sE.grabRow = sE.grabTarget = RowAt(sE.rowStartY);
                Toast("DRAG THE PLANE UP OR DOWN");
            }
        }
        else if (sE.panning)
        {
            if (x != sE.panX || y != sE.panY)
            {
                sE.originX += x - sE.panX;
                sE.originY += y - sE.panY;
                ClampView();
                sE.panX = x;
                sE.panY = y;
                sE.dirtyDraw = true;
            }
        }
        else if (sE.lassoing)
        {
            int lx = x - CAN_X, ly = y;

            if (lx < 0) lx = 0;
            if (lx >= CAN_W) lx = CAN_W - 1;
            if (sE.pathLen < LASSO_POINTS && (sE.path[sE.pathLen - 1][0] != lx || sE.path[sE.pathLen - 1][1] != ly))
            {
                sE.path[sE.pathLen][0] = (int16_t)lx;
                sE.path[sE.pathLen][1] = (int16_t)ly;
                ++sE.pathLen;
                sE.dirtyDraw = true;
            }
        }
        else if (sE.stroking)
        {
            int px, py;

            if (x >= CAN_X && x < RIGHT_X && PictureAt(x, y, &px, &py))
            {
                BrushLine(px, py);
                sE.dirtyDraw = true;
            }
            else
                sE.lastX = sE.lastY = -1;
            /* The top screen follows the stroke as it goes. */
            if (sE.strokeCount && sE.tick % 8 == 0) CtrStereoMon_Recompose();
        }
    }
    if (in->touchUp || (!in->touchActive && sE.touching))
    {
        if (sE.rowTouch)
        {
            /* The stylus has gone: where it last was counts. */
            int dy = sE.lastTouchY - sE.rowStartY;

            sE.rowTouch = false;
            if (sE.grabRow >= 0)
            {
                if (sE.grabTarget != sE.grabRow)
                    MovePlane((unsigned)(PLANES - 1 - sE.grabRow), (unsigned)(PLANES - 1 - sE.grabTarget));
                sE.grabRow = -1;
            }
            else if (dy > -8 && dy < 8 && sE.lastTouchX >= RIGHT_X)
                PickPlane((unsigned)(PLANES - 1 - RowAt(sE.rowStartY)));
        }
        if (sE.lassoing) FinishLasso();
        if (sE.stroking) EndStroke();
        if (sE.slider == SLIDER_SIZE)
        {
            char line[16];

            snprintf(line, sizeof(line), "BRUSH %u", sE.size);
            Toast(line);
        }
        sE.panning = false;
        sE.slider = SLIDER_NONE;
        sE.pressed = P_NONE;
        sE.dirtyDraw = true;
    }
    if (in->touchActive)
    {
        sE.lastTouchX = x;
        sE.lastTouchY = y;
    }
    sE.touching = in->touchActive;
}

#define OPT_X 60
#define OPT_Y 30
#define OPT_W 200
#define OPT_ROW 26

static void OptionAct(int row, bool rightHalf)
{
    switch (row)
    {
    case O_DEPTH:
        CtrStereoMon_SetDepth(CtrStereoMon_Depth() + (rightHalf ? CTR_STEREO_EDIT_DEPTH_STEP : -CTR_STEREO_EDIT_DEPTH_STEP));
        break;
    case O_PROPAGATE: sE.propagate = !sE.propagate; CtrSettings_SetInt("edit_propagate", sE.propagate); break;
    case O_PROPAGATE_NOW:
        if (sE.frames > 1)
        {
            unsigned carried;

            PushUndo();
            carried = PropagateChanged(NULL);
            if (carried) Changed();
            Toast(carried ? "OTHER FRAME UPDATED" : "OTHER FRAME ALREADY MATCHES");
            CtrStereoMon_LogEdit("propagate-all");
        }
        sE.screen = S_EDIT;
        break;
    case O_AUTHOR: OpenKeyboard(KB_AUTHOR); break;
    case O_RESET:
        sE.screen = S_CONFIRM_RESET;
        break;
    case O_RESET + 100:
        if (CtrStereoMon_ResetEdit())
        {
            unsigned planes, frames;

            sE.pl = CtrStereoMon_EditPlanes(&planes, &frames);
            sE.planes = planes;
            sE.frames = frames;
            FourPlanes();
            sE.layer = BusiestPlane();
            sE.dirtyFile = false;
            sUndoCount = sUndoNext = sRedoCount = 0;
            CtrStereoMon_Recompose();
            Toast("BACK TO THE PACK'S PLANES");
        }
        sE.screen = S_EDIT;
        break;
    default: sE.screen = S_EDIT; break;
    }
    sE.dirtyDraw = true;
}

#define CF_X 60
#define CF_Y 70
#define CF_W 200
#define CF_H 88

static void TouchConfirm(const CtrInput *in)
{
    if (!in->touchDown) return;
    if (in->touchY >= CF_Y + 52 && in->touchY < CF_Y + 80)
    {
        if (in->touchX >= CF_X + 16 && in->touchX < CF_X + 96) OptionAct(O_RESET + 100, false);
        else if (in->touchX >= CF_X + 104 && in->touchX < CF_X + 184) sE.screen = S_EDIT;
    }
    sE.dirtyDraw = true;
}

static void TouchOptions(const CtrInput *in)
{
    if (!in->touchDown) return;
    if (in->touchX >= OPT_X + 8 && in->touchX < OPT_X + OPT_W - 8 && in->touchY >= OPT_Y + 8)
    {
        int row = (in->touchY - OPT_Y - 8) / OPT_ROW;

        if (row >= 0 && row < O_COUNT) OptionAct(row, in->touchX >= OPT_X + OPT_W / 2);
    }
    else
        sE.screen = S_EDIT;
    sE.dirtyDraw = true;
}

static void TouchChoose(const CtrInput *in)
{
    unsigned count = sListCount, page = sE.chooseCursor / CH_PER_PAGE, cell;
    int x = in->touchX, y = in->touchY, col, row;

    if (!in->touchDown) return;
    sE.dirtyDraw = true;
    if (y < CH_Y0)
    {
        if (x >= CH_SEARCH_X && x < CH_SEARCH_X + CH_SEARCH_W) OpenKeyboard(KB_SEARCH);
        else if (x >= CH_SHOW_X && x < CH_SHOW_X + CH_SHOW_W) { sE.onlyEdited = !sE.onlyEdited; BuildList(); }
        else if (x >= CH_SORT_X && x < CH_SORT_X + CH_SORT_W) { sE.editedFirst = !sE.editedFirst; BuildList(); }
        else if (count && x >= CH_PREV_X && x < CH_PREV_X + CH_PAGE_W)
            sE.chooseCursor = (sE.chooseCursor + count - CH_PER_PAGE % count) % count;
        else if (count && x >= CH_NEXT_X) sE.chooseCursor = (sE.chooseCursor + CH_PER_PAGE) % count;
        return;
    }
    col = x / (W / CH_COLS);
    row = (y - CH_Y0) / CH_ROW_H;
    if (row < 0 || row >= CH_ROWS || col >= CH_COLS) return;
    cell = page * CH_PER_PAGE + (unsigned)(row * CH_COLS + col);
    if (cell < count) Choose(sList[cell]);
}

/* The keyboard's keys: A..Z then 0..9, nine to a row. */
static char KeyAt(int k) { return k < 26 ? (char)('A' + k) : (char)('0' + k - 26); }

enum { KB_DEL, KB_SPACE, KB_CLEAR, KB_OK, KB_WIDE };
#define KB_WIDE_W 72
#define KB_WIDE_STEP 76

static void TouchKeyboard(const CtrInput *in)
{
    int x = in->touchX, y = in->touchY;

    if (!in->touchDown) return;
    sE.dirtyDraw = true;
    if (y >= KB_Y && y < KB_Y + KB_ROWS * KB_KEY_H && x >= KB_X && x < KB_X + KB_COLS * KB_KEY_W)
    {
        int k = (y - KB_Y) / KB_KEY_H * KB_COLS + (x - KB_X) / KB_KEY_W;

        if (k < 36) KeyboardType(KeyAt(k));
    }
    else if (y >= KB_ROW2_Y && y < KB_ROW2_Y + KB_KEY_H && x >= KB_X)
    {
        int k = (x - KB_X) / KB_WIDE_STEP;

        if (k == KB_DEL) KeyboardType('\b');
        else if (k == KB_SPACE) KeyboardType(' ');
        else if (k == KB_CLEAR) KeyboardType(0);
        else if (k == KB_OK) KeyboardDone();
    }
}

/* ── Drawing ────────────────────────────────────────────────────────── */

static inline void Px(int x, int y, uint16_t c)
{
    if (x >= 0 && y >= 0 && x < W && y < H) sCanvas[x * H + (H - 1 - y)] = c;
}

/* A rectangle, cut to the canvas left of the planes' column. */
static void CanvasFill(int x, int y, int w, int h, uint16_t c)
{
    int x0 = x < CAN_X ? CAN_X : x, x1 = x + w > RIGHT_X ? RIGHT_X : x + w;
    int y0 = y < 0 ? 0 : y, y1 = y + h > H ? H : y + h;

    for (int sx = x0; sx < x1; ++sx)
    {
        uint16_t *col = &sCanvas[sx * H + (H - 1)];

        for (int sy = y0; sy < y1; ++sy) col[-sy] = c;
    }
}

static uint16_t Mix(uint16_t a, uint16_t b, unsigned ka, unsigned k)
{
    unsigned ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
    unsigned br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;

    return (uint16_t)((((ar * ka + br * (k - ka)) / k) << 11) | (((ag * ka + bg * (k - ka)) / k) << 5)
                      | ((ab * ka + bb * (k - ka)) / k));
}

static uint16_t Rgb(unsigned r, unsigned g, unsigned b) { return (uint16_t)((r << 11) | (g << 5) | b); }

/* A colour's grey (RGB565). */
static uint16_t Grey(uint16_t c)
{
    unsigned l = (2 * ((c >> 11) & 31) + 5 * (((c >> 5) & 63) >> 1) + (c & 31)) / 8;

    return Rgb(l, l * 2, l);
}

/* A plane's colour for a plane not being edited: muted, half grey. */
static uint16_t Muted(uint16_t c) { return Mix(c, Mix(Grey(c), Rgb(10, 20, 11), 1, 2), 3, 5); }

/* The picture: every pixel tinted by its plane's colour (the plane being
 * edited bright and saturated, the others muted and greyed), pixels
 * without a plane hatched red; the grid in white or black over it. */
static void DrawPicture(void)
{
    const uint16_t out = Rgb(5, 10, 5), bg0 = Rgb(9, 18, 9), bg1 = Rgb(8, 16, 8), red = Rgb(28, 6, 8),
                   redDark = Rgb(16, 2, 4);
    const uint16_t gridInk = sE.grid == GRID_WHITE ? Rgb(31, 63, 31) : Rgb(0, 0, 0);
    const uint16_t *pic = sE.pic[sE.frame];
    const int s = Zoom(), ox = CAN_X + sE.originX, oy = sE.originY;
    const unsigned k = sOpacities[sE.opacity];

    CanvasFill(CAN_X, 0, CAN_W, H, out);
    for (int y = 0; y < DIM; ++y)
    {
        int sy = oy + y * s;

        if (sy + s <= 0 || sy >= H) continue;
        for (int x = 0; x < DIM; ++x)
        {
            int sx = ox + x * s;
            unsigned i = (unsigned)(y * DIM + x), p;
            uint16_t c;

            if (sx + s <= CAN_X || sx >= RIGHT_X) continue;
            if (pic[i] == 0) c = ((x >> 2) + (y >> 2)) & 1 ? bg0 : bg1;
            else if ((p = GetPlane(sE.frame, i)) == NONE) c = (x + y) & 1 ? red : redDark;
            else if (p == sE.layer)
            {
                /* The plane edited: its colour at full strength, lifted. */
                c = Mix(sPlaneColours[p], Rgb565Of(pic[i]), k, 20);
                c = Mix(Rgb(31, 63, 31), c, 1, 8);
            }
            else
            {
                /* The others: the pixel greyed, a muted tint, darkened. */
                c = Mix(Muted(sPlaneColours[p]), Grey(Rgb565Of(pic[i])), k, 20);
                c = Mix(out, c, 1, 3);
            }
            CanvasFill(sx, sy, s, s, c);
            if (sE.grid != GRID_OFF)
            {
                CanvasFill(sx + s - 1, sy, 1, s, gridInk);
                CanvasFill(sx, sy + s - 1, s, 1, gridInk);
            }
        }
    }
    /* The lasso's loop as it is drawn. */
    if (sE.lassoing && sE.pathLen > 1)
    {
        for (unsigned a = 1; a < sE.pathLen; ++a)
        {
            int x0 = sE.path[a - 1][0], y0 = sE.path[a - 1][1], x1 = sE.path[a][0], y1 = sE.path[a][1];
            int dx = x1 - x0, dy = y1 - y0, steps = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);

            for (int st = 0; st <= steps; ++st)
            {
                int lx = CAN_X + x0 + (steps ? dx * st / steps : 0), ly = y0 + (steps ? dy * st / steps : 0);

                CanvasFill(lx, ly, 2, 2, ((a + st / 4) & 1) ? sE.mark : Rgb(31, 63, 31));
            }
        }
    }
}

/* An icon (3ds_edit_icons.h: Lucide's, `size` pixels square) at (x, y):
 * its ink with a dark edge all round, so it reads on any plate or colour. */
static void DrawGlyph(const uint32_t *rows, int size, int x, int y, uint16_t ink, uint16_t edge)
{
#define INK(r, c) ((r) >= 0 && (r) < size && (c) >= 0 && (c) < size && ((rows[r] >> (size - 1 - (c))) & 1))
    for (int r = -1; r <= size; ++r)
        for (int c = -1; c <= size; ++c)
        {
            if (INK(r, c)) Px(x + c, y + r, ink);
            else if (INK(r - 1, c) || INK(r + 1, c) || INK(r, c - 1) || INK(r, c + 1) || INK(r - 1, c - 1)
                     || INK(r - 1, c + 1) || INK(r + 1, c - 1) || INK(r + 1, c + 1))
                Px(x + c, y + r, edge);
        }
#undef INK
}

static void Icon(int icon, bool large, int x, int y, uint16_t ink)
{
    if (large) DrawGlyph(sIconLarge[icon], ICON_LARGE, x, y, ink, Rgb(1, 2, 2));
    else DrawGlyph(sIconSmall[icon], ICON_SMALL, x, y, ink, Rgb(1, 2, 2));
}

/* An icon in the middle of a plate. */
static void PlateIcon(int x, int y, int w, int h, int look, bool pressed, int icon, bool large, uint16_t ink)
{
    int size = large ? ICON_LARGE : ICON_SMALL;

    CtrBottomDraw_Plate(x, y, w, h, look, pressed);
    Icon(icon, large, x + (w - size) / 2, y + (h - size) / 2, ink);
}

/* A plane's pixels as a preview `size` pixels square at (x, y): the
 * picture's own box, so a small Pokémon is not a speck; dimmed for a plane
 * not being edited. */
static void DrawPreview(unsigned plane, int x, int y, int size, bool dim)
{
    const uint16_t *pic = sE.pic[sE.frame];
    uint16_t bg = Rgb(6, 12, 6);
    int bx, by, side;

    PictureBox(&bx, &by, &side);
    for (int py = 0; py < size; ++py)
        for (int px = 0; px < size; ++px)
        {
            int sx = bx + px * side / size, sy = by + py * side / size;
            uint16_t c = bg;
            unsigned i;

            if (sx >= 0 && sy >= 0 && sx < DIM && sy < DIM)
            {
                i = (unsigned)(sy * DIM + sx);
                if (pic[i] != 0 && GetPlane(sE.frame, i) == plane) c = dim ? Mix(Grey(Rgb565Of(pic[i])), bg, 1, 2) : Rgb565Of(pic[i]);
            }
            Px(x + px, y + py, c);
        }
}

static void DrawDisc(int cx, int cy, int r, uint16_t c)
{
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            if (dx * dx + dy * dy <= r * r + r) Px(cx + dx, cy + dy, c);
}

/* A slim slider over the picture: its track, its steps (those up to the
 * value lit), a knob with its icon. */
static void DrawSlider(int cx, int value, int steps, int icon, uint16_t iconInk)
{
    const uint16_t track = Rgb(26, 54, 28), edge = Rgb(1, 2, 2), knob = Rgb(10, 21, 12), ring = Rgb(31, 63, 31);
    int ky = SL_BOTTOM - (SL_BOTTOM - SL_TOP) * value / (steps - 1);

    CtrBottomDraw_Fill(cx - 2, SL_TOP - 1, 5, SL_BOTTOM - SL_TOP + 3, edge);
    CtrBottomDraw_Fill(cx - 1, SL_TOP, 3, SL_BOTTOM - SL_TOP + 1, track);
    for (int s = 0; s < steps; ++s)
    {
        int ty = SL_BOTTOM - (SL_BOTTOM - SL_TOP) * s / (steps - 1);

        CtrBottomDraw_Fill(cx - 5, ty - 1, 11, 3, edge);
        CtrBottomDraw_Fill(cx - 4, ty, 9, 1, s <= value ? Rgb(31, 56, 8) : track);
    }
    DrawDisc(cx, ky, KNOB_R + 2, edge);
    DrawDisc(cx, ky, KNOB_R + 1, ring);
    DrawDisc(cx, ky, KNOB_R - 1, knob);
    Icon(icon, false, cx - ICON_SMALL / 2, ky - ICON_SMALL / 2, iconInk);
}

/* The left, over the picture: the sliders, each tool under its slider. */
static void DrawLeft(void)
{
    static const int icons[L_COUNT] = {ICON_PAN, ICON_BRUSH, ICON_FILL, ICON_UNDO};
    static const uint8_t tools[L_COUNT] = {T_PAN, T_BRUSH, T_FILL, T_COUNT};

    DrawSlider(SL_ZOOM_X, sE.zoom, ZOOMS, ICON_ZOOM, Rgb(31, 63, 31));
    DrawSlider(SL_SIZE_X, sE.size - 1, SIZES, ICON_BRUSH, sPlaneColours[sE.layer]);
    for (int b = 0; b < L_COUNT; ++b)
    {
        int x, y;
        bool on = tools[b] == sE.tool;
        /* The brush is drawn in the colour of the plane it paints. */
        uint16_t ink = b == L_BRUSH ? sPlaneColours[sE.layer] : Rgb(31, 63, 31);

        LeftButton(b, &x, &y);
        PlateIcon(x, y, TOOL_W, TOOL_H, on ? 1 : 0, on || sE.pressed == P_LEFT + b, icons[b], true, ink);
    }
}

/* The bar over the picture's top: what concerns the picture. */
static void DrawBar(void)
{
    const bool back = (sE.id & 0xC000) != 0, other = CtrStereoMon_Counterpart(sE.id) != 0;
    const bool final = CtrStereoMon_EditMeta()->finalized;
    char label[12];

    PlateIcon(DEX_X, BAR_Y, DEX_W, BAR_H, 0, sE.pressed == P_DEX, ICON_LIST, true, Rgb(31, 63, 31));
    /* The F/B switch: the side shown chosen, the other greyed when the pack has none. */
    CtrBottomDraw_Plate(FB_X, BAR_Y, FB_W, BAR_H, !back ? 1 : other ? 0 : 2, !back || sE.pressed == P_FRONT);
    CtrBottomDraw_Plate(FB_X + FB_W, BAR_Y, FB_W, BAR_H, back ? 1 : other ? 0 : 2, back || sE.pressed == P_BACK);
    CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, "F", FB_X + FB_W / 2, BAR_Y + 5, !back ? CTR_DRAW_WHITE : other ? CTR_DRAW_DARK : CTR_DRAW_OFF);
    CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, "B", FB_X + FB_W + FB_W / 2, BAR_Y + 5, back ? CTR_DRAW_WHITE : other ? CTR_DRAW_DARK : CTR_DRAW_OFF);
    snprintf(label, sizeof(label), "F%u/%u", sE.frame + 1, sE.frames);
    CtrBottomDraw_Plate(FRAME_X, BAR_Y, FRAME_W, BAR_H, sE.frames > 1 ? 0 : 2, sE.pressed == P_FRAME);
    CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, label, FRAME_X + FRAME_W / 2, BAR_Y + 5, sE.frames > 1 ? CTR_DRAW_DARK : CTR_DRAW_OFF);
    PlateIcon(BATTLE_X, BAR_Y, BATTLE_W, BAR_H, sE.battleView ? 1 : 0, sE.battleView || sE.pressed == P_BATTLE, ICON_BATTLE,
              true, Rgb(31, 63, 31));
    /* Unsaved changes: a red dot. */
    if (sE.dirtyFile)
    {
        DrawDisc(BATTLE_X + BATTLE_W + 8, BAR_Y + BAR_H / 2, 4, Rgb(1, 2, 2));
        DrawDisc(BATTLE_X + BATTLE_W + 8, BAR_Y + BAR_H / 2, 3, Rgb(31, 12, 8));
    }
    /* FINAL: a checkbox. */
    CtrBottomDraw_Plate(FINAL_X, BAR_Y, FINAL_W, BAR_H, final ? 1 : 0, final || sE.pressed == P_FINAL);
    Icon(final ? ICON_BOX_CHECK : ICON_BOX, false, FINAL_X + 5, BAR_Y + (BAR_H - ICON_SMALL) / 2,
         final ? Rgb(16, 63, 12) : Rgb(31, 63, 31));
    CtrBottomDraw_Text(CTR_DRAW_FONT_SMALL, "FINAL", FINAL_X + 21, BAR_Y + 5, final ? CTR_DRAW_WHITE : CTR_DRAW_DARK);
}

static void DrawToast(void)
{
    if ((sE.heldKeys & CTR_KEY_L) || sE.toastFrames)
    {
        bool picking = sE.heldKeys & CTR_KEY_L;
        int x0 = FREE_X, w = RIGHT_X - 4 - x0;

        CtrBottomDraw_Plate(x0, H - 22, w, 20, 0, false);
        CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, picking ? "TAP A PIXEL TO PICK ITS PLANE" : sE.toast, x0 + w / 2,
                                  H - 19, picking ? CTR_DRAW_BLUE : CTR_DRAW_DARK);
    }
}

/* The planes' column: the planes nearest first, each framed in its colour
 * (bright for the plane being edited, muted for the others), then the
 * grid's three looks and the tint, the outline and MORE. */
static void DrawRight(void)
{
    static const int icons[B_COUNT] = {ICON_GRID_OFF, ICON_GRID, ICON_GRID, ICON_CONTRAST, ICON_OUTLINE, ICON_MORE};
    char label[4];

    CtrBottomDraw_Panel(RIGHT_X, 0, W, H);
    for (int r = 0; r < PLANES; ++r)
    {
        unsigned plane = (unsigned)(PLANES - 1 - r);
        int y = ROW_Y0 + r * ROW_STEP;
        bool cur = plane == sE.layer, grabbed = sE.grabRow == r;
        uint16_t colour = cur ? sPlaneColours[plane] : Muted(sPlaneColours[plane]);
        /* The number's ink: white on a dark colour, dark on a light one. */
        bool light = (Grey(colour) >> 11) > 14;

        if (cur)
        {
            CtrBottomDraw_Fill(ROW_X - 3, y - 2, ROW_SIDE + 6, ROW_SIDE + 4, Rgb(1, 2, 2));
            CtrBottomDraw_Fill(ROW_X - 2, y - 1, ROW_SIDE + 4, ROW_SIDE + 2, Rgb(31, 63, 31));
        }
        CtrBottomDraw_Fill(ROW_X, y, ROW_SIDE, ROW_SIDE, colour);
        DrawPreview(plane, ROW_X + ROW_FRAME, y + ROW_FRAME, THUMB, !cur);
        CtrBottomDraw_Fill(ROW_X + ROW_FRAME, y + ROW_FRAME, 10, 13, colour);
        snprintf(label, sizeof(label), "%d", r + 1);
        CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, label, ROW_X + ROW_FRAME + 5, y + ROW_FRAME,
                                  light ? CTR_DRAW_DARK : CTR_DRAW_WHITE);
        if (grabbed) CtrBottomDraw_Fill(ROW_X + ROW_FRAME, y + ROW_FRAME, THUMB, 4, Rgb(31, 50, 10));
        if (sE.grabRow >= 0 && sE.grabTarget == r && !grabbed)
            CtrBottomDraw_Fill(ROW_X - 2, sE.grabTarget > sE.grabRow ? y + ROW_SIDE - 1 : y - 3, ROW_SIDE + 4, 3, Rgb(31, 50, 10));
    }
    for (int b = 0; b < B_COUNT; ++b)
    {
        int x = ROW_X - 1 + (b % 3) * BTN_STEP_X, y = BTN_Y0 + (b / 3) * BTN_STEP_Y;
        bool on = (b <= B_GRID_BLACK && sE.grid == b - B_GRID_OFF) || (b == B_OUTLINE && sE.outlineOn);
        /* The grid's buttons in the grid's own ink; the outline in its colour. */
        uint16_t ink = b == B_GRID_BLACK ? Rgb(0, 0, 0) : b == B_OUTLINE ? sE.mark : Rgb(31, 63, 31);

        CtrBottomDraw_Plate(x, y, BTN_W, BTN_H, on ? 1 : 0, on || sE.pressed == P_RIGHT + b);
        if (b == B_GRID_BLACK)
            DrawGlyph(sIconSmall[icons[b]], ICON_SMALL, x + (BTN_W - ICON_SMALL) / 2, y + (BTN_H - ICON_SMALL) / 2, ink,
                      Rgb(31, 63, 31));
        else
            Icon(icons[b], false, x + (BTN_W - ICON_SMALL) / 2, y + (BTN_H - ICON_SMALL) / 2, ink);
    }
}

static void DrawConfirm(void)
{
    CtrBottomDraw_Box(0, CF_X, CF_Y, CF_W / 8, CF_H / 8, false);
    CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, "BACK TO THE PACK'S PLANES?", CF_X + CF_W / 2, CF_Y + 10, CTR_DRAW_DARK);
    CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, "YOUR EDITS OF THIS ONE ARE LOST.", CF_X + CF_W / 2, CF_Y + 26, CTR_DRAW_RED);
    CtrBottomDraw_Plate(CF_X + 16, CF_Y + 52, 80, 24, 0, false);
    CtrBottomDraw_Plate(CF_X + 104, CF_Y + 52, 80, 24, 1, false);
    CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, "RESET", CF_X + 56, CF_Y + 57, CTR_DRAW_RED);
    CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, "KEEP", CF_X + 144, CF_Y + 57, CTR_DRAW_WHITE);
}

static void DrawOptions(void)
{
    static const char *const names[O_COUNT] = {"3D DEPTH", "AUTO PROPAGATE", "PROPAGATE NOW", "AUTHOR", "RESET TO PACK",
                                               "CLOSE"};
    char value[24];

    CtrBottomDraw_Box(0, OPT_X, OPT_Y, OPT_W / 8, (8 + O_COUNT * OPT_ROW + 15) / 8, false);
    for (int row = 0; row < O_COUNT; ++row)
    {
        int y = OPT_Y + 8 + row * OPT_ROW;

        CtrBottomDraw_Plate(OPT_X + 8, y, OPT_W - 16, OPT_ROW - 4, 0, false);
        CtrBottomDraw_Text(CTR_DRAW_FONT_SMALL, names[row], OPT_X + 14, y + 5, CTR_DRAW_DARK);
        value[0] = 0;
        if (row == O_DEPTH)
        {
            /* A minus at the left half's end and a plus at the right's:
             * a tap on either half. (The game font has no plus.) */
            int mx = OPT_X + OPT_W / 2 - 14, px = OPT_X + OPT_W - 22, cy = y + (OPT_ROW - 4) / 2;

            CtrBottomDraw_Fill(mx, cy - 1, 9, 3, Rgb(24, 6, 6));
            CtrBottomDraw_Fill(px, cy - 1, 9, 3, Rgb(24, 6, 6));
            CtrBottomDraw_Fill(px + 3, cy - 4, 3, 9, Rgb(24, 6, 6));
            snprintf(value, sizeof(value), "%d / %d", CtrStereoMon_Depth(), CTR_STEREO_EDIT_DEPTH_MAX);
            CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, value, OPT_X + OPT_W * 3 / 4 - 10, y + 5, CTR_DRAW_RED);
            value[0] = 0;
        }
        else if (row == O_PROPAGATE) snprintf(value, sizeof(value), "%s", sE.propagate ? "ON" : "OFF");
        else if (row == O_AUTHOR) snprintf(value, sizeof(value), "%s", CtrStereoMon_Author()[0] ? CtrStereoMon_Author() : "?");
        if (value[0])
            CtrBottomDraw_Text(CTR_DRAW_FONT_SMALL, value,
                               OPT_X + OPT_W - 14 - CtrBottomDraw_TextWidth(CTR_DRAW_FONT_SMALL, value), y + 5, CTR_DRAW_RED);
    }
}

static void DrawChooser(void)
{
    unsigned count = sListCount, page = sE.chooseCursor / CH_PER_PAGE;
    unsigned pages = count ? (count + CH_PER_PAGE - 1) / CH_PER_PAGE : 1;
    char line[80], name[CTR_STEREO_MON_NAME];

    CtrBottomDraw_Panel(0, 0, W, H);
    /* The bar: the search (what is typed), ALL or EDITED, the order, the pages. */
    CtrBottomDraw_Plate(CH_SEARCH_X, 2, CH_SEARCH_W, 20, sE.query[0] ? 1 : 0, sE.query[0] != 0);
    Icon(ICON_SEARCH, false, CH_SEARCH_X + 5, 6, Rgb(31, 63, 31));
    CtrBottomDraw_Text(CTR_DRAW_FONT_SMALL, sE.query[0] ? sE.query : "SEARCH  Y", CH_SEARCH_X + 22, 5,
                       sE.query[0] ? CTR_DRAW_WHITE : CTR_DRAW_OFF);
    CtrBottomDraw_Plate(CH_SHOW_X, 2, CH_SHOW_W, 20, sE.onlyEdited ? 1 : 0, sE.onlyEdited);
    Icon(sE.onlyEdited ? ICON_PENCIL : ICON_FILTER, false, CH_SHOW_X + 4, 6, Rgb(31, 63, 31));
    CtrBottomDraw_Text(CTR_DRAW_FONT_SMALL, sE.onlyEdited ? "EDITED" : "ALL", CH_SHOW_X + 19, 5,
                       sE.onlyEdited ? CTR_DRAW_WHITE : CTR_DRAW_DARK);
    CtrBottomDraw_Plate(CH_SORT_X, 2, CH_SORT_W, 20, sE.editedFirst ? 1 : 0, sE.editedFirst);
    Icon(ICON_SORT, false, CH_SORT_X + 4, 6, Rgb(31, 63, 31));
    CtrBottomDraw_Text(CTR_DRAW_FONT_SMALL, sE.editedFirst ? "EDITED 1ST" : "BY NO.", CH_SORT_X + 19, 5,
                       sE.editedFirst ? CTR_DRAW_WHITE : CTR_DRAW_DARK);
    CtrBottomDraw_Plate(CH_PREV_X, 2, CH_PAGE_W, 20, 0, false);
    CtrBottomDraw_Plate(CH_NEXT_X, 2, CH_PAGE_W, 20, 0, false);
    CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, "L", CH_PREV_X + CH_PAGE_W / 2, 5, CTR_DRAW_DARK);
    CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, "R", CH_NEXT_X + CH_PAGE_W / 2, 5, CTR_DRAW_DARK);
    sE.iconsMissing = false;
    if (count == 0)
        CtrBottomDraw_TextCentred(CTR_DRAW_FONT_NORMAL, sE.onlyEdited ? "NO EDITED PICTURES YET" : "NOTHING BY THAT NAME",
                                  W / 2, H / 2 - 8, CTR_DRAW_DARK);
    for (unsigned cell = 0; cell < CH_PER_PAGE; ++cell)
    {
        unsigned pos = page * CH_PER_PAGE + cell, index;
        int x = (int)(cell % CH_COLS) * (W / CH_COLS), y = CH_Y0 + (int)(cell / CH_COLS) * CH_ROW_H;
        uint16_t id, kind, number;
        bool cur;

        if (pos >= count) break;
        index = sList[pos];
        id = CtrStereoMon_GalleryIdAt(index);
        kind = id & 0xC000;
        number = id & 0x3FFF;
        cur = pos == sE.chooseCursor;
        CtrStereoMonGame_Name(id, name, sizeof(name));
        CtrBottomDraw_Plate(x + 2, y, W / CH_COLS - 4, CH_ROW_H - 2, cur ? 1 : 0, cur);
        /* A Pokémon's icon (front or back); a trainer's or a player's kind. */
        if (kind == 0 || kind == CTR_STEREO_ID_BACK)
        {
            if (!CtrBottomDraw_MonIcon(number, x + 2, y)) sE.iconsMissing = true;
        }
        else
            CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, kind == CTR_STEREO_ID_TRAINER ? "TR" : "PL", x + 18, y + 9,
                                      cur ? CTR_DRAW_WHITE : CTR_DRAW_OFF);
        if (kind == 0) snprintf(line, sizeof(line), "%03u", number);
        else snprintf(line, sizeof(line), "%c%03u", kind == CTR_STEREO_ID_BACK ? 'B' : kind == CTR_STEREO_ID_TRAINER ? 'T' : 'P', number);
        CtrBottomDraw_Text(CTR_DRAW_FONT_SMALL, line, x + 36, y + 3, cur ? CTR_DRAW_WHITE : CTR_DRAW_OFF);
        CtrBottomDraw_Text(CTR_DRAW_FONT_SMALL, name, x + 36, y + 16, cur ? CTR_DRAW_WHITE : CTR_DRAW_DARK);
        /* Edited: a pencil; finished: a tick. */
        if (CtrStereoMon_IsFinal(id))
            Icon(ICON_CHECK, false, x + W / CH_COLS - 18, y + 3, Rgb(8, 58, 10));
        else if (CtrStereoMon_HasEdit(id))
            Icon(ICON_PENCIL, false, x + W / CH_COLS - 18, y + 3, Rgb(31, 52, 8));
    }
    /* (The game font's strings take 39 characters at most.) */
    snprintf(line, sizeof(line), "%u SHOWN   PAGE %u/%u   X EDITED ONLY", count, count ? page + 1 : 1, pages);
    CtrBottomDraw_Text(CTR_DRAW_FONT_SMALL, line, 6, CH_Y0 + CH_ROWS * CH_ROW_H + 1, CTR_DRAW_DARK);
}

static void DrawKeyboard(void)
{
    static const char *const wide[KB_WIDE] = {"DEL", "SPACE", "CLEAR", "OK"};
    static const int wideIcons[KB_WIDE] = {ICON_DELETE, -1, ICON_X, ICON_ENTER};
    const char *text = sE.kbFor == KB_SEARCH ? sE.query : sE.kbText;
    char line[64];

    CtrBottomDraw_Panel(0, 0, W, H);
    CtrBottomDraw_Text(CTR_DRAW_FONT_SMALL, sE.kbFor == KB_SEARCH ? "SEARCH A NAME OR A NUMBER" : "WHO SIGNS THE SAVES", 8, 2,
                       CTR_DRAW_DARK);
    /* The text so far, and where the next letter goes. */
    CtrBottomDraw_Plate(6, 16, W - 12, 22, 1, false);
    Icon(sE.kbFor == KB_SEARCH ? ICON_SEARCH : ICON_USER, false, 12, 21, Rgb(31, 63, 31));
    CtrBottomDraw_Text(CTR_DRAW_FONT_NORMAL, text, 30, 19, CTR_DRAW_WHITE);
    CtrBottomDraw_Fill(30 + CtrBottomDraw_TextWidth(CTR_DRAW_FONT_NORMAL, text) + 1, 33, 8, 2, Rgb(31, 63, 31));
    for (int k = 0; k < 36; ++k)
    {
        int x = KB_X + (k % KB_COLS) * KB_KEY_W, y = KB_Y + (k / KB_COLS) * KB_KEY_H;
        char key[2] = {KeyAt(k), 0};

        CtrBottomDraw_Plate(x + 1, y + 1, KB_KEY_W - 2, KB_KEY_H - 2, 0, false);
        CtrBottomDraw_TextCentred(CTR_DRAW_FONT_NORMAL, key, x + KB_KEY_W / 2, y + 6, CTR_DRAW_DARK);
    }
    for (int k = 0; k < KB_WIDE; ++k)
    {
        int x = KB_X + k * KB_WIDE_STEP;

        CtrBottomDraw_Plate(x, KB_ROW2_Y, KB_WIDE_W, KB_KEY_H - 2, k == KB_OK ? 1 : 0, false);
        if (wideIcons[k] >= 0) Icon(wideIcons[k], false, x + 6, KB_ROW2_Y + 8, Rgb(31, 63, 31));
        CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, wide[k], x + KB_WIDE_W / 2 + 6, KB_ROW2_Y + 7,
                                  k == KB_OK ? CTR_DRAW_WHITE : CTR_DRAW_DARK);
    }
    /* What the search finds so far. */
    if (sE.kbFor == KB_SEARCH)
    {
        char name[CTR_STEREO_MON_NAME];
        unsigned n = (unsigned)snprintf(line, sizeof(line), "%u FOUND", sListCount);

        for (unsigned k = 0; k < sListCount && k < 3; ++k)
        {
            CtrStereoMonGame_Name(CtrStereoMon_GalleryIdAt(sList[k]), name, sizeof(name));
            /* The game font's strings take 39 characters at most. */
            if (n + 2 + strlen(name) > 39) break;
            n += (unsigned)snprintf(line + n, sizeof(line) - n, "%s%s", k ? ", " : ": ", name);
        }
        CtrBottomDraw_Text(CTR_DRAW_FONT_SMALL, line, 8, KB_ROW2_Y + KB_KEY_H + 4, CTR_DRAW_DARK);
    }
    CtrBottomDraw_Text(CTR_DRAW_FONT_SMALL, "B RUB OUT   START OK", 8, H - 15, CTR_DRAW_OFF);
}

static void Draw(void)
{
    CtrBottomDraw_Begin(sCanvas);
    if (sE.screen == S_CHOOSE)
        DrawChooser();
    else if (sE.screen == S_KEYBOARD)
        DrawKeyboard();
    else if (sE.pl == NULL)
    {
        CtrBottomDraw_Panel(0, 0, W, H);
        CtrBottomDraw_TextCentred(CTR_DRAW_FONT_NORMAL, "LOADING", W / 2, H / 2 - 8, CTR_DRAW_DARK);
    }
    else
    {
        DrawPicture();
        DrawLeft();
        DrawBar();
        DrawToast();
        DrawRight();
        if (sE.screen == S_OPTIONS) DrawOptions();
        if (sE.screen == S_CONFIRM_RESET) DrawConfirm();
    }
    CtrBottomDraw_End();
    CtrBottom_Blit(sCanvas, 0, W);
}

#ifdef CTR_TEST_EDIT
/*
 * The emulator aid: a scripted session (touches in bottom screen pixels,
 * keys) driven by the editor's own tick, the bottom screen dumped raw
 * (RGB565 in the canvas layout) at a few points.
 */
typedef struct { uint16_t from, to; int16_t x, y; uint16_t key; } Step;
#define LEFT_TAP_X(b) ((b) % 2 ? SL_SIZE_X : SL_ZOOM_X)
#define LEFT_TAP_Y(b) (TOOL_Y0 + ((b) / 2) * TOOL_STEP + TOOL_H / 2)
#define BTN_TAP_X(b) (ROW_X - 1 + ((b) % 3) * BTN_STEP_X + BTN_W / 2)
#define BTN_TAP_Y(b) (BTN_Y0 + ((b) / 3) * BTN_STEP_Y + BTN_H / 2)
#define KEY_TAP_X(k) (KB_X + ((k) % KB_COLS) * KB_KEY_W + KB_KEY_W / 2)
#define KEY_TAP_Y(k) (KB_Y + ((k) / KB_COLS) * KB_KEY_H + KB_KEY_H / 2)
static const Step sScript[] = {
    {40, 41, LEFT_TAP_X(L_FILL), LEFT_TAP_Y(L_FILL), 0},       /* FILL */
    {60, 61, 160, H / 2, 0},                                   /* a tap on the body: its colour cell */
    {90, 91, ROW_X + 25, ROW_Y0 + 2 * ROW_STEP + 25, 0},       /* plane 3 */
    {120, 152, 0, 0, 0},                                       /* a loop (drawn below) */
    {165, 166, BTN_TAP_X(B_OPACITY), BTN_TAP_Y(B_OPACITY), 0}, /* the tint lighter */
    {175, 176, LEFT_TAP_X(L_BRUSH), LEFT_TAP_Y(L_BRUSH), 0},   /* BRUSH */
    {180, 181, SL_SIZE_X, SL_TOP + 2, 0},                      /* the biggest brush */
    {190, 210, 0, 0, 0},                                       /* a stroke (drawn below) */
    {220, 221, 0, 0, CTR_KEY_UP},                              /* zoom in */
    {230, 231, BTN_TAP_X(B_GRID_WHITE), BTN_TAP_Y(B_GRID_WHITE), 0}, /* the white grid */
    {236, 237, 0, 0, CTR_KEY_X},                               /* VIEW LAYERS on the top screen */
    {240, 241, LEFT_TAP_X(L_PAN), LEFT_TAP_Y(L_PAN), 0},       /* PAN */
    {245, 260, 0, 0, 0},                                       /* a drag (drawn below) */
    {270, 271, BTN_TAP_X(B_MORE), BTN_TAP_Y(B_MORE), 0},       /* MORE */
    {285, 286, 10, 10, 0},                                     /* (closes it: a tap outside) */
    {290, 291, 0, 0, CTR_KEY_R},                               /* the other frame */
    {292, 293, BATTLE_X + 10, BAR_Y + 8, 0},                   /* the battle's view */
    {296, 297, FINAL_X + 20, BAR_Y + 8, 0},                    /* FINAL */
    {300, 301, 0, 0, CTR_KEY_SELECT},                          /* the chooser */
    {306, 307, CH_SORT_X + 10, 10, 0},                         /* edited first */
    {312, 313, CH_SEARCH_X + 20, 10, 0},                       /* the search */
    {318, 319, KEY_TAP_X(1), KEY_TAP_Y(1), 0},                 /* B */
    {322, 323, KEY_TAP_X(20), KEY_TAP_Y(20), 0},               /* U */
    {326, 327, KEY_TAP_X(11), KEY_TAP_Y(11), 0},               /* L */
    {330, 331, 0, 0, CTR_KEY_START},                           /* OK */
    {336, 337, 0, 0, CTR_KEY_A},                               /* the first found */
    {360, 361, FB_X + FB_W + 8, BAR_Y + 8, 0},                 /* its back */
    {420, 421, 0, 0, CTR_KEY_B},                               /* back to the gallery */
};
static const uint16_t sDumpTicks[] = {30, 75, 105, 160, 172, 215, 225, 239, 265, 280, 295, 299, 310, 316, 328, 333, 400};

static void TestDump(void)
{
    char path[64];
    FILE *f;

    for (unsigned k = 0; k < sizeof(sDumpTicks) / sizeof(sDumpTicks[0]); ++k)
        if (sDumpTicks[k] == sE.tick)
        {
            snprintf(path, sizeof(path), "sdmc:/3ds/emerald3ds/edit_%u.raw", (unsigned)sE.tick);
            f = fopen(path, "wb");
            if (f) { fwrite(sCanvas, 2, W * H, f); fclose(f); }
            CtrLog_Write(CTR_LOG_VIDEO, "stereo edit test: dumped tick %u", (unsigned)sE.tick);
        }
}

static const CtrInput *TestInput(const CtrInput *real)
{
    static CtrInput synth;
    static bool wasTouching;
    bool touching = false;
    int x = 0, y = 0;

    synth = *real;
    synth.touchActive = synth.touchDown = synth.touchUp = false;
    for (unsigned k = 0; k < sizeof(sScript) / sizeof(sScript[0]); ++k)
    {
        const Step *s = &sScript[k];

        if (sE.tick < s->from || sE.tick >= s->to || s->key) continue;
        touching = true;
        if (s->from == 120)
        {
            /* A loop around the upper left of the picture. */
            static const int8_t cx[8] = {0, 7, 10, 7, 0, -7, -10, -7}, cy[8] = {-10, -7, 0, 7, 10, 7, 0, -7};
            unsigned i = (sE.tick - 120) / 4;

            x = CAN_X + 80 + cx[i & 7] * 3;
            y = 80 + cy[i & 7] * 3;
        }
        else if (s->from == 190)
        {
            x = CAN_X + 60 + (int)(sE.tick - 190) * 5;
            y = 150;
        }
        else if (s->from == 245)
        {
            x = CAN_X + 100 - (int)(sE.tick - 245) * 3;
            y = 120 - (int)(sE.tick - 245) * 2;
        }
        else { x = s->x; y = s->y; }
    }
    if (touching)
    {
        synth.touchActive = true;
        synth.touchDown = !wasTouching;
        synth.touchX = (uint16_t)x;
        synth.touchY = (uint16_t)y;
    }
    else if (wasTouching)
        synth.touchUp = true;
    wasTouching = touching;
    for (unsigned k = 0; k < sizeof(sScript) / sizeof(sScript[0]); ++k)
        if (sScript[k].key && sE.tick == sScript[k].from)
        {
            unsigned key = sScript[k].key;

            if (sE.screen == S_EDIT && (key & CTR_KEY_DPAD)) DpadKeys(key);
            else if (sE.screen == S_KEYBOARD || sE.screen == S_CHOOSE) CtrStereoEdit_Keys(key, 0);
            else CtrStereoEdit_Keys(key, key);
        }
    return &synth;
}
#endif

/* The C-stick: pushed left or right, another Pokémon; once per push. */
static void CstickMon(const CtrInput *in)
{
    int cx = in->cstickX, ax = cx < 0 ? -cx : cx;
    unsigned count = CtrStereoMon_GalleryCount();

    if (sE.cstickMoved)
    {
        if (ax < CSTICK_OFF) sE.cstickMoved = false;
        return;
    }
    if (ax < CSTICK_ON || count == 0) return;
    sE.cstickMoved = true;
    {
        unsigned index = CtrStereoMonGame_GalleryIndex();

        Choose(cx > 0 ? (index + 1) % count : (index + count - 1) % count);
    }
}

/* The Circle Pad pans: up to six pixels a frame, its fractions kept. */
static void PadPan(const CtrInput *in)
{
    int cx = in->circleX, cy = in->circleY;

    if (cx > -24 && cx < 24) cx = 0;
    if (cy > -24 && cy < 24) cy = 0;
    if (cx == 0 && cy == 0)
    {
        sE.padX = sE.padY = 0;
        return;
    }
    sE.padX += cx;
    sE.padY += cy;
    if (sE.padX / 24 || sE.padY / 24)
    {
        /* The view goes where the pad points: the picture the other way. */
        sE.originX -= sE.padX / 24;
        sE.originY += sE.padY / 24;
        sE.padX %= 24;
        sE.padY %= 24;
        ClampView();
        sE.dirtyDraw = true;
    }
}

bool CtrStereoEdit_BottomFrame(void)
{
    const CtrInput *in = CtrInput_Get();

    if (!sE.active) return false;
    if (!CtrBottomDraw_Ready()) return true;
    ++sE.tick;
#ifdef CTR_TEST_EDIT
    in = TestInput(in);
    if (sE.pl != NULL) sE.dirtyDraw = true;
#endif
    if (sE.screen != S_CHOOSE && sE.screen != S_KEYBOARD && !Bind())
    {
        if (sE.tick % 16 == 0) Draw();
        return true;
    }
    if (!sE.active) return true;
    if (sE.screen == S_CHOOSE) TouchChoose(in);
    else if (sE.screen == S_KEYBOARD) TouchKeyboard(in);
    else if (sE.screen == S_OPTIONS) TouchOptions(in);
    else if (sE.screen == S_CONFIRM_RESET) TouchConfirm(in);
    else
    {
        TouchEdit(in);
        PadPan(in);
        CstickMon(in);
    }
    if (!sE.active) return true;
    if (sE.screen != S_CHOOSE && sE.screen != S_KEYBOARD && sE.pl != NULL)
    {
        CtrStereoMon_SetEditing(true, sE.layer);
        CtrStereoMon_SetEditView(sE.frame, sE.animate, sE.outlineOn, sE.mark, sE.layersView, sE.battleView);
    }
    if (sE.toastFrames && --sE.toastFrames == 0) sE.dirtyDraw = true;
    /* The lasso's dashes run while it is drawn. */
    if (sE.lassoing && sE.tick % 8 == 0) sE.dirtyDraw = true;
    /* The chooser's icons come in one a frame. */
    if (sE.screen == S_CHOOSE && sE.iconsMissing) sE.dirtyDraw = true;
    /* The top screen's frame number follows the animation. */
    if (sE.animate && sE.tick % 30 == 0) sE.dirtyDraw = true;
    if (sE.dirtyDraw)
    {
        static float peak;
        uint64_t start = CtrPlatform_Ticks();
        float ms;

        sE.dirtyDraw = false;
        Draw();
        ms = CtrPlatform_TickMs(CtrPlatform_Ticks() - start);
        if (ms > peak + 0.5f)
        {
            peak = ms;
            CtrLog_Write(CTR_LOG_VIDEO, "stereo edit: redraw peak %.2f ms (screen %u)", ms, sE.screen);
        }
    }
#ifdef CTR_TEST_EDIT
    TestDump();
#endif
    return true;
}
