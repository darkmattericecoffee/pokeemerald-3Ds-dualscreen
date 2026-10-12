/*
 * The stereoscopic planes' editor (3ds_stereo_mon.h), game side: the
 * bottom screen while the gallery's Pokémon is being edited.
 *
 * The top screen keeps showing the Pokémon in its own colours as its stack
 * of planes (the viewer, 3ds_stereo_mon.c, composing the planes again after
 * every change), the plane being edited outlined there in the colour
 * opposite the Pokémon's own, so it shows against any Pokémon. The bottom
 * screen is the editor: the picture with every pixel tinted by its plane's
 * colour (a quick mask: four hues, the nearest plane the most saturated and
 * the farthest the palest, the tint's strength a button), the four planes
 * down the right nearest first, two slim sliders (zoom, brush size) and the
 * tools down the left. A tool paints planes, never pixels: the picture is
 * the game's, a plane number is all a pixel has, so painting a pixel into
 * one plane takes it out of the other and there is no eraser.
 *
 *  BRUSH   the pixels under the stylus join the plane being edited.
 *  FILL    a tap: the colour cell under the stylus joins the plane (same
 *          colour, bounded by the outline and transparency, with the outline
 *          that touches it); a loop: every pixel inside it does.
 *  PAN     a drag moves the picture; so does the Circle Pad.
 *
 * A short press of L goes to the next plane; L held and a tap picks the
 * plane under the stylus. R (and the frame button over the picture) turns
 * to the next frame; the C-stick (and the DEX button, the chooser) goes to
 * another picture: every Pokémon's front and back, the trainers' fronts
 * and the players' backs (both planes files), the button over the picture's
 * left corner swapping a Pokémon's front and back. A tap on a plane in the
 * list picks it; one held still is grabbed and dropped elsewhere in the
 * stack. Every picture has the same four planes here
 * (CTR_STEREO_EDIT_PLANES): a file with more is folded into them, one with
 * fewer spread over them, when it is opened.
 *
 * Every change on one frame is carried to the others by the packer's rule
 * (the nearest pixel of the same colour) unless that is turned off in MORE.
 * Edits are kept on the SD card per Pokémon (CtrStereoMon_SaveEdit): on
 * START, on leaving, and on switching Pokémon (SELECT: the chooser).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "3ds_bottom.h"
#include "3ds_bottom_draw.h"
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

/* The layout: the sliders and the tools at the left, the picture in the
 * middle (zoomed and panned), the planes and four buttons at the right. */
#define LEFT_W 40
#define CAN_X LEFT_W
#define RIGHT_X 264
#define CAN_W (RIGHT_X - CAN_X)
/* The DEX button (the chooser) at the top, the two sliders side by side
 * under it, their tracks from SL_TOP to SL_BOTTOM, the tools under them. */
#define DEX_Y 2
#define DEX_H 18
#define SL_TOP 32
#define SL_BOTTOM 118
#define SL_ZOOM_X 12
#define SL_SIZE_X 28
#define KNOB_R 6
#define TOOL_X 4
#define TOOL_W 32
#define TOOL_H 30
#define TOOL_Y0 134
#define TOOL_STEP 34
/* The planes: a framed thumbnail each, nearest at the top. */
#define ROW_X (RIGHT_X + 3)
#define ROW_Y0 1
#define ROW_STEP 50
#define ROW_FRAME 3
#define ROW_SIDE 50
#define THUMB (ROW_SIDE - 2 * ROW_FRAME)
#define ROWS_END (ROW_Y0 + PLANES * ROW_STEP)
/* The buttons under them, two by two. */
#define BTN_Y0 (ROWS_END + 3)
#define BTN_W 25
#define BTN_H 17
#define BTN_STEP_X 27
#define BTN_STEP_Y 19
/* Frames a row is held still before it is grabbed for moving. */
#define GRAB_FRAMES 24

#define UNDO_DEPTH 12
/* The buttons over the picture's top corners: the front/back swap, the frame. */
#define CORNER_Y 4
#define CORNER_W 50
#define CORNER_H 17
#define LASSO_POINTS 512
#define TOAST_FRAMES 90
/* How far the propagation looks for a pixel of the same colour. */
#define PROPAGATE_RADIUS 7
/* Below this size (screen pixels) a lasso is a tap. */
#define TAP_SLOP 6

enum { T_BRUSH, T_FILL, T_PAN, T_COUNT };
enum { B_OUTLINE, B_OPACITY, B_GRID, B_MORE, B_COUNT };
enum { SLIDER_NONE = -1, SLIDER_ZOOM, SLIDER_SIZE };
/* The chooser: a page of icons and names. */
#define CH_COLS 3
#define CH_ROWS 6
#define CH_ROW_H 34
#define CH_Y0 22
#define CH_PER_PAGE (CH_COLS * CH_ROWS)

/* The zoom's steps (screen pixels a picture pixel), the brush's sizes, and
 * the tint's strengths (twentieths of the plane's colour over the pixel). */
static const uint8_t sZooms[] = {3, 4, 5, 6, 8, 10};
#define ZOOMS ((int)(sizeof(sZooms) / sizeof(sZooms[0])))
#define SIZES 4
static const uint8_t sOpacities[] = {15, 10, 6, 3};
static const char *const sOpacityNames[] = {"75", "50", "30", "15"};
#define OPACITIES ((int)(sizeof(sOpacities) / sizeof(sOpacities[0])))
/* The planes' colours, far to near: four hues, the saturation rising to the
 * front (peach, pink, sky blue, green). The same for every Pokémon. */
static const uint16_t sPlaneColours[PLANES] = {0xFE73, 0xFB5F, 0x3CDF, 0x07E8};

enum { S_EDIT, S_OPTIONS, S_CHOOSE, S_CONFIRM_RESET };
enum { O_DEPTH, O_PROPAGATE, O_PROPAGATE_NOW, O_RESET, O_CLOSE, O_COUNT };
/* Frames L is held before a press is a hold (a pick), not a step. */
#define L_HOLD_FRAMES 18
/* The C-stick: past this it has moved, back under it it is ready again. */
#define CSTICK_ON 60
#define CSTICK_OFF 30

/* The tools' icons: 16x16, one bit a pixel. */
static const uint16_t sIcons[T_COUNT][16] = {
    /* brush: a tilted handle, its head at the lower left */
    {0x0006, 0x000F, 0x001E, 0x003C, 0x0078, 0x00F0, 0x01E0, 0x03C0, 0x0780, 0x0F00, 0x1E00, 0x3C00, 0x7C00, 0x7E00, 0x3E00, 0x0C00},
    /* fill: a tilted bucket, paint running out of its lip */
    {0x0000, 0x0180, 0x0240, 0x0420, 0x0810, 0x1008, 0x2004, 0x4002, 0x4006, 0x200C, 0x1018, 0x0830, 0x0460, 0x02C6, 0x0186, 0x0006},
    /* pan: an open hand */
    {0x0000, 0x0240, 0x05A0, 0x05A8, 0x05B4, 0x05B4, 0x05B4, 0x3404, 0x4C04, 0x4404, 0x2004, 0x1008, 0x0808, 0x0410, 0x07F0, 0x0000},
};

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
    uint8_t tool, size, zoom, opacity;
    bool propagate, outlineOn, grid, animate, fromGallery;
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
    /* The chooser. */
    unsigned chooseCursor;
    bool iconsMissing;
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

/* The Pokémon as large as the canvas takes it, in the middle. */
static void ResetView(void)
{
    int bx, by, side;

    PictureBox(&bx, &by, &side);
    sE.zoom = 0;
    while (sE.zoom + 1 < ZOOMS && side * sZooms[sE.zoom + 1] <= CAN_W - 8) ++sE.zoom;
    sE.originX = (CAN_W - side * Zoom()) / 2 - bx * Zoom();
    sE.originY = (H - side * Zoom()) / 2 - by * Zoom();
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
    sE.grid = CtrSettings_GetInt("edit_grid", 0) != 0;
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

/* ── Keys ───────────────────────────────────────────────────────────── */

/* The D-pad: zoom up and down (about the canvas's middle), undo and redo. */
static void DpadKeys(unsigned dpad)
{
    if (dpad & CTR_KEY_UP) SetZoom(sE.zoom + 1, CAN_W / 2, H / 2);
    if (dpad & CTR_KEY_DOWN) SetZoom(sE.zoom - 1, CAN_W / 2, H / 2);
    if (dpad & CTR_KEY_LEFT) Undo();
    if (dpad & CTR_KEY_RIGHT) Redo();
}

void CtrStereoEdit_Keys(unsigned down, unsigned held)
{
    unsigned count = CtrStereoMon_GalleryCount();
    const CtrInput *in = CtrInput_Get();
    bool lReleased = (sE.heldKeys & CTR_KEY_L) && !(held & CTR_KEY_L);
    unsigned lFrames = sE.lFrames;

    if (!sE.active) return;
    if ((held ^ sE.heldKeys) & (CTR_KEY_L | CTR_KEY_R)) sE.dirtyDraw = true;
    sE.heldKeys = held;
    sE.lFrames = (held & CTR_KEY_L) ? sE.lFrames + 1 : 0;
    sE.dirtyDraw = sE.dirtyDraw || down != 0;
    if (sE.screen == S_CHOOSE)
    {
        if (down & (CTR_KEY_B | CTR_KEY_SELECT)) sE.screen = S_EDIT;
        else if (down & CTR_KEY_A) Choose(sE.chooseCursor);
        else if (down & CTR_KEY_RIGHT) sE.chooseCursor = (sE.chooseCursor + 1) % count;
        else if (down & CTR_KEY_LEFT) sE.chooseCursor = (sE.chooseCursor + count - 1) % count;
        else if (down & CTR_KEY_DOWN) sE.chooseCursor = (sE.chooseCursor + 3) % count;
        else if (down & CTR_KEY_UP) sE.chooseCursor = (sE.chooseCursor + count - 3) % count;
        else if (down & CTR_KEY_R) sE.chooseCursor = (sE.chooseCursor + CH_PER_PAGE) % count;
        else if (down & CTR_KEY_L) sE.chooseCursor = (sE.chooseCursor + count - CH_PER_PAGE % count) % count;
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
        Save(true);
        sE.screen = S_CHOOSE;
        sE.chooseCursor = CtrStereoMonGame_GalleryIndex();
        return;
    }
    if (down & CTR_KEY_B) { Close(!sE.fromGallery); return; }
    if (down & CTR_KEY_START) Save(false);
    if (down & CTR_KEY_A) sE.tool = T_BRUSH;
    if (down & CTR_KEY_X)
    {
        sE.outlineOn = !sE.outlineOn;
        CtrSettings_SetInt("edit_outline", sE.outlineOn);
        Toast(sE.outlineOn ? "OUTLINE ON" : "OUTLINE OFF");
    }
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

static void ButtonAct(int button)
{
    switch (button)
    {
    case B_OUTLINE:
        sE.outlineOn = !sE.outlineOn;
        CtrSettings_SetInt("edit_outline", sE.outlineOn);
        break;
    case B_OPACITY:
        sE.opacity = (uint8_t)((sE.opacity + 1) % OPACITIES);
        CtrSettings_SetInt("edit_opacity", sE.opacity);
        break;
    case B_GRID:
        sE.grid = !sE.grid;
        CtrSettings_SetInt("edit_grid", sE.grid);
        break;
    default: sE.screen = S_OPTIONS; break;
    }
}

static void TouchEdit(const CtrInput *in)
{
    int x = in->touchX, y = in->touchY;

    if (in->touchDown)
    {
        sE.pressed = -1;
        if (x < LEFT_W)
        {
            if (y < DEX_Y + DEX_H + 2)
            {
                sE.pressed = T_COUNT + B_COUNT;
                Save(true);
                sE.screen = S_CHOOSE;
                sE.chooseCursor = CtrStereoMonGame_GalleryIndex();
            }
            else if (y < SL_BOTTOM + KNOB_R + 4)
            {
                sE.slider = x < (SL_ZOOM_X + SL_SIZE_X) / 2 ? SLIDER_ZOOM : SLIDER_SIZE;
                SliderAt(y);
            }
            else
            {
                int row = (y - TOOL_Y0) / TOOL_STEP;

                if (y >= TOOL_Y0 && row < T_COUNT && y - TOOL_Y0 - row * TOOL_STEP < TOOL_H)
                {
                    sE.tool = (uint8_t)row;
                    sE.pressed = row;
                }
            }
        }
        else if (x >= RIGHT_X)
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

                if (col >= 0 && col < 2 && row >= 0 && row < 2)
                {
                    sE.pressed = T_COUNT + row * 2 + col;
                    ButtonAct(row * 2 + col);
                }
            }
        }
        else if (y < CORNER_Y + CORNER_H + 2 && x >= CAN_X + 4 && x < CAN_X + 4 + CORNER_W && CtrStereoMon_Counterpart(sE.id))
        {
            /* The front's back, the back's front. */
            int index = CtrStereoMon_GalleryIndexOf(CtrStereoMon_Counterpart(sE.id));

            if (index >= 0) Choose((unsigned)index);
        }
        else if (y < CORNER_Y + CORNER_H + 2 && x >= RIGHT_X - 4 - CORNER_W && x < RIGHT_X - 4 && sE.frames > 1)
            OtherFrame();
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
        sE.panning = false;
        sE.slider = SLIDER_NONE;
        sE.pressed = -1;
        sE.dirtyDraw = true;
    }
    if (in->touchActive)
    {
        sE.lastTouchX = x;
        sE.lastTouchY = y;
    }
    sE.touching = in->touchActive;
}

#define OPT_X 70
#define OPT_Y 50
#define OPT_W 180
#define OPT_ROW 24

static void OptionAct(int row, bool rightHalf)
{
    switch (row)
    {
    case O_DEPTH:
        CtrStereoMon_SetDepth(CtrStereoMon_Depth() + (rightHalf ? 25 : -25));
        CtrStereoMon_Recompose();
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
    unsigned count = CtrStereoMon_GalleryCount(), page = sE.chooseCursor / CH_PER_PAGE, cell;
    int col, row;

    if (!in->touchDown) return;
    if (in->touchY < CH_Y0)
    {
        /* The page arrows at the right of the title. */
        if (in->touchX >= W - 60 && in->touchX < W - 30) sE.chooseCursor = (sE.chooseCursor + count - CH_PER_PAGE % count) % count;
        else if (in->touchX >= W - 30) sE.chooseCursor = (sE.chooseCursor + CH_PER_PAGE) % count;
        sE.dirtyDraw = true;
        return;
    }
    col = in->touchX / (W / CH_COLS);
    row = (in->touchY - CH_Y0) / CH_ROW_H;
    if (row < 0 || row >= CH_ROWS || col >= CH_COLS) return;
    cell = page * CH_PER_PAGE + (unsigned)(row * CH_COLS + col);
    if (cell < count) Choose(cell);
}

/* ── Drawing ────────────────────────────────────────────────────────── */

static inline void Px(int x, int y, uint16_t c)
{
    if (x >= 0 && y >= 0 && x < W && y < H) sCanvas[x * H + (H - 1 - y)] = c;
}

/* A rectangle, cut to the canvas between the side strips. */
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

/* The picture: every pixel tinted by its plane's colour, pixels without a
 * plane hatched red. */
static void DrawPicture(void)
{
    const uint16_t out = Rgb(5, 10, 5), bg0 = Rgb(9, 18, 9), bg1 = Rgb(8, 16, 8), red = Rgb(28, 6, 8),
                   redDark = Rgb(16, 2, 4);
    const uint16_t *pic = sE.pic[sE.frame];
    const int s = Zoom(), ox = CAN_X + sE.originX, oy = sE.originY;
    const unsigned k = sOpacities[sE.opacity];
    const bool grid = sE.grid && s >= 4;

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
                /* The plane edited: its colour, lifted a little. */
                c = Mix(sPlaneColours[p], Rgb565Of(pic[i]), k, 20);
                c = Mix(Rgb(31, 63, 31), c, 1, 6);
            }
            else
            {
                /* The others: a weaker tint, and darkened. */
                c = Mix(sPlaneColours[p], Rgb565Of(pic[i]), k * 2 / 3, 20);
                c = Mix(out, c, 2, 5);
            }
            CanvasFill(sx, sy, s, s, c);
            if (grid)
            {
                uint16_t line = Mix(c, 0, 3, 4);

                CanvasFill(sx + s - 1, sy, 1, s, line);
                CanvasFill(sx, sy + s - 1, s, 1, line);
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
    /* Over the top corners: the front/back swap, the frame; the unsaved dot. */
    if (CtrStereoMon_Counterpart(sE.id))
    {
        CtrBottomDraw_Plate(CAN_X + 4, CORNER_Y, CORNER_W, CORNER_H, 0, false);
        CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, (sE.id & 0xC000) ? "FRONT" : "BACK", CAN_X + 4 + CORNER_W / 2,
                                  CORNER_Y + 2, CTR_DRAW_DARK);
    }
    if (sE.frames > 1)
    {
        char label[12];

        snprintf(label, sizeof(label), "F %u/%u", sE.frame + 1, sE.frames);
        CtrBottomDraw_Plate(RIGHT_X - 4 - CORNER_W, CORNER_Y, CORNER_W, CORNER_H, 0, false);
        CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, label, RIGHT_X - 4 - CORNER_W / 2, CORNER_Y + 2, CTR_DRAW_DARK);
    }
    if (sE.dirtyFile) CanvasFill(RIGHT_X - 10 - CORNER_W, CORNER_Y + 6, 5, 5, Rgb(31, 12, 8));
    if ((sE.heldKeys & CTR_KEY_L) || sE.toastFrames)
    {
        bool picking = sE.heldKeys & CTR_KEY_L;

        CtrBottomDraw_Plate(CAN_X + 4, H - 22, CAN_W - 8, 20, 0, false);
        CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, picking ? "TAP A PIXEL TO PICK ITS PLANE" : sE.toast,
                                  CAN_X + CAN_W / 2, H - 19, picking ? CTR_DRAW_BLUE : CTR_DRAW_DARK);
    }
}

/* A plane's pixels as a preview `size` pixels square at (x, y): the
 * picture's own box, so a small Pokémon is not a speck. */
static void DrawPreview(unsigned plane, int x, int y, int size)
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
                if (pic[i] != 0 && GetPlane(sE.frame, i) == plane) c = Rgb565Of(pic[i]);
            }
            Px(x + px, y + py, c);
        }
}

static void DrawIcon(const uint16_t *bits, int x, int y, uint16_t ink, uint16_t shadow)
{
    for (int r = 0; r < 16; ++r)
        for (int c = 0; c < 16; ++c)
            if ((bits[r] >> (15 - c)) & 1)
            {
                Px(x + c + 1, y + r + 1, shadow);
                Px(x + c, y + r, ink);
            }
}

static void DrawDisc(int cx, int cy, int r, uint16_t c)
{
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            if (dx * dx + dy * dy <= r * r + r) Px(cx + dx, cy + dy, c);
}

/* A slim slider: its track, its steps, a knob with its sign. */
static void DrawSlider(int cx, int value, int steps, bool zoom)
{
    const uint16_t track = Rgb(12, 26, 14), knob = Rgb(6, 16, 9), ring = Rgb(31, 63, 31);
    int ky = SL_BOTTOM - (SL_BOTTOM - SL_TOP) * value / (steps - 1);

    CtrBottomDraw_Fill(cx - 1, SL_TOP, 3, SL_BOTTOM - SL_TOP + 1, track);
    for (int s = 0; s < steps; ++s) CtrBottomDraw_Fill(cx - 3, SL_BOTTOM - (SL_BOTTOM - SL_TOP) * s / (steps - 1), 7, 1, track);
    DrawDisc(cx, ky, KNOB_R + 1, ring);
    DrawDisc(cx, ky, KNOB_R, knob);
    if (zoom)
    {
        /* A magnifier's plus. */
        CtrBottomDraw_Fill(cx - 3, ky, 7, 1, ring);
        CtrBottomDraw_Fill(cx, ky - 3, 1, 7, ring);
    }
    else
        DrawDisc(cx, ky, value, sPlaneColours[sE.layer]);
}

static void DrawLeft(void)
{
    CtrBottomDraw_Panel(0, 0, LEFT_W, H);
    CtrBottomDraw_Plate(TOOL_X, DEX_Y, TOOL_W, DEX_H, 0, sE.pressed == T_COUNT + B_COUNT);
    CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, "DEX", TOOL_X + TOOL_W / 2, DEX_Y + 2, CTR_DRAW_DARK);
    DrawSlider(SL_ZOOM_X, sE.zoom, ZOOMS, true);
    DrawSlider(SL_SIZE_X, sE.size - 1, SIZES, false);
    for (int row = 0; row < T_COUNT; ++row)
    {
        int y = TOOL_Y0 + row * TOOL_STEP;
        bool on = sE.tool == row;
        /* The brush is drawn in the colour of the plane it paints. */
        uint16_t ink = row == T_BRUSH ? sPlaneColours[sE.layer] : on ? Rgb(31, 63, 31) : Rgb(6, 14, 8);
        uint16_t shadow = row == T_BRUSH ? Rgb(2, 4, 2) : on ? Rgb(4, 14, 8) : Rgb(22, 50, 26);

        CtrBottomDraw_Plate(TOOL_X, y, TOOL_W, TOOL_H, on ? 1 : 0, on || sE.pressed == row);
        DrawIcon(sIcons[row], TOOL_X + (TOOL_W - 16) / 2, y + (TOOL_H - 16) / 2, ink, shadow);
    }
}

static void DrawRight(void)
{
    static const uint16_t grid[16] = {0, 0, 0x7FFE, 0x4922, 0x4922, 0x7FFE, 0x4922, 0x4922, 0x7FFE,
                                      0x4922, 0x4922, 0x7FFE, 0, 0, 0, 0};
    const uint16_t ink = Rgb(6, 14, 8), shadow = Rgb(22, 50, 26);
    char label[4];

    CtrBottomDraw_Panel(RIGHT_X, 0, W, H);
    for (int r = 0; r < PLANES; ++r)
    {
        unsigned plane = (unsigned)(PLANES - 1 - r);
        int y = ROW_Y0 + r * ROW_STEP;
        bool cur = plane == sE.layer, grabbed = sE.grabRow == r;
        uint16_t colour = cur ? sPlaneColours[plane] : Mix(sPlaneColours[plane], Rgb(6, 12, 6), 1, 2);

        /* The frame in the plane's colour (bright for the plane being
         * edited, with a light rim), the thumbnail, the number. */
        if (cur) CtrBottomDraw_Fill(ROW_X - 2, y - 1, ROW_SIDE + 4, ROW_SIDE, Rgb(31, 63, 31));
        CtrBottomDraw_Fill(ROW_X, y, ROW_SIDE, ROW_SIDE - 2, colour);
        DrawPreview(plane, ROW_X + ROW_FRAME, y + ROW_FRAME, THUMB);
        CtrBottomDraw_Fill(ROW_X + ROW_FRAME, y + ROW_FRAME, 10, 13, colour);
        snprintf(label, sizeof(label), "%d", r + 1);
        CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, label, ROW_X + ROW_FRAME + 5, y + ROW_FRAME, CTR_DRAW_DARK);
        if (grabbed) CtrBottomDraw_Fill(ROW_X + ROW_FRAME, y + ROW_FRAME, THUMB, 4, Rgb(31, 50, 10));
        if (sE.grabRow >= 0 && sE.grabTarget == r && !grabbed)
            CtrBottomDraw_Fill(ROW_X - 2, sE.grabTarget > sE.grabRow ? y + ROW_SIDE - 2 : y - 2, ROW_SIDE + 4, 3, Rgb(31, 50, 10));
    }
    for (int b = 0; b < B_COUNT; ++b)
    {
        int x = ROW_X + (b % 2) * BTN_STEP_X, y = BTN_Y0 + (b / 2) * BTN_STEP_Y;
        bool on = (b == B_OUTLINE && sE.outlineOn) || (b == B_GRID && sE.grid);

        CtrBottomDraw_Plate(x, y, BTN_W, BTN_H, on ? 1 : 0, on || sE.pressed == T_COUNT + b);
        if (b == B_OUTLINE)
        {
            /* A dashed square in the mark colour. */
            for (int t = 0; t < 11; t += 2)
            {
                Px(x + 7 + t, y + 3, sE.mark);
                Px(x + 7 + t, y + 13, sE.mark);
                Px(x + 7, y + 3 + t, sE.mark);
                Px(x + 17, y + 3 + t, sE.mark);
            }
        }
        else if (b == B_OPACITY)
            CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, sOpacityNames[sE.opacity], x + BTN_W / 2, y + 1, CTR_DRAW_DARK);
        else if (b == B_GRID)
            DrawIcon(grid, x + 4, y + 1, on ? Rgb(31, 63, 31) : ink, on ? Rgb(4, 14, 8) : shadow);
        else
            for (int d = 0; d < 3; ++d) CtrBottomDraw_Fill(x + 6 + d * 5, y + 8, 3, 3, ink);
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
    static const char *const names[O_COUNT] = {"3D DEPTH  -/+", "AUTO PROPAGATE", "PROPAGATE NOW", "RESET TO PACK", "CLOSE"};
    char depth[8];

    CtrBottomDraw_Box(0, OPT_X, OPT_Y, OPT_W / 8, (8 + O_COUNT * OPT_ROW + 15) / 8, false);
    for (int row = 0; row < O_COUNT; ++row)
    {
        int y = OPT_Y + 8 + row * OPT_ROW;

        CtrBottomDraw_Plate(OPT_X + 8, y, OPT_W - 16, OPT_ROW - 4, 0, false);
        CtrBottomDraw_Text(CTR_DRAW_FONT_SMALL, names[row], OPT_X + 14, y + 4, CTR_DRAW_DARK);
        if (row == O_PROPAGATE || row == O_DEPTH)
        {
            const char *value = sE.propagate ? "ON" : "OFF";

            if (row == O_DEPTH)
            {
                snprintf(depth, sizeof(depth), "%d%%", CtrStereoMon_Depth());
                value = depth;
            }
            CtrBottomDraw_Text(CTR_DRAW_FONT_SMALL, value,
                               OPT_X + OPT_W - 14 - CtrBottomDraw_TextWidth(CTR_DRAW_FONT_SMALL, value), y + 4,
                               CTR_DRAW_RED);
        }
    }
}

static void DrawChooser(void)
{
    unsigned count = CtrStereoMon_GalleryCount(), page = sE.chooseCursor / CH_PER_PAGE;
    unsigned pages = (count + CH_PER_PAGE - 1) / CH_PER_PAGE;
    char line[48], name[CTR_STEREO_MON_NAME];

    CtrBottomDraw_Panel(0, 0, W, H);
    snprintf(line, sizeof(line), "CHOOSE A PICTURE  %u/%u", page + 1, pages);
    CtrBottomDraw_Text(CTR_DRAW_FONT_SMALL, line, 6, 3, CTR_DRAW_DARK);
    CtrBottomDraw_Plate(W - 60, 2, 28, 16, 0, false);
    CtrBottomDraw_Plate(W - 30, 2, 28, 16, 0, false);
    CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, "L", W - 46, 3, CTR_DRAW_DARK);
    CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, "R", W - 16, 3, CTR_DRAW_DARK);
    sE.iconsMissing = false;
    for (unsigned cell = 0; cell < CH_PER_PAGE; ++cell)
    {
        unsigned index = page * CH_PER_PAGE + cell;
        int x = (int)(cell % CH_COLS) * (W / CH_COLS), y = CH_Y0 + (int)(cell / CH_COLS) * CH_ROW_H;
        uint16_t id, kind, number;
        bool cur;

        if (index >= count) break;
        id = CtrStereoMon_GalleryIdAt(index);
        kind = id & 0xC000;
        number = id & 0x3FFF;
        cur = index == sE.chooseCursor;
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
        if (CtrStereoMon_HasEdit(id))
            CtrBottomDraw_Fill(x + W / CH_COLS - 10, y + 5, 5, 5, cur ? Rgb(31, 60, 20) : Rgb(8, 40, 12));
    }
}

static void Draw(void)
{
    CtrBottomDraw_Begin(sCanvas);
    if (sE.screen == S_CHOOSE)
        DrawChooser();
    else if (sE.pl == NULL)
    {
        CtrBottomDraw_Panel(0, 0, W, H);
        CtrBottomDraw_TextCentred(CTR_DRAW_FONT_NORMAL, "LOADING", W / 2, H / 2 - 8, CTR_DRAW_DARK);
    }
    else
    {
        DrawLeft();
        DrawPicture();
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
#define TOOL_TAP(t) (TOOL_Y0 + (t) * TOOL_STEP + TOOL_H / 2)
#define BTN_TAP_X(b) (ROW_X + ((b) % 2) * BTN_STEP_X + BTN_W / 2)
#define BTN_TAP_Y(b) (BTN_Y0 + ((b) / 2) * BTN_STEP_Y + BTN_H / 2)
static const Step sScript[] = {
    {40, 41, TOOL_X + 16, TOOL_TAP(T_FILL), 0},            /* FILL */
    {60, 61, CAN_X + CAN_W / 2, H / 2, 0},                 /* a tap on the body: its colour cell */
    {90, 91, ROW_X + 25, ROW_Y0 + 2 * ROW_STEP + 25, 0},   /* plane 3 */
    {120, 152, 0, 0, 0},                                   /* a loop (drawn below) */
    {165, 166, BTN_TAP_X(B_OPACITY), BTN_TAP_Y(B_OPACITY), 0}, /* the tint lighter */
    {175, 176, TOOL_X + 16, TOOL_TAP(T_BRUSH), 0},         /* BRUSH */
    {180, 181, SL_SIZE_X, SL_TOP + 10, 0},                 /* the biggest brush */
    {190, 210, 0, 0, 0},                                   /* a stroke (drawn below) */
    {220, 221, 0, 0, CTR_KEY_UP},                          /* zoom in */
    {230, 231, BTN_TAP_X(B_GRID), BTN_TAP_Y(B_GRID), 0},   /* the grid */
    {240, 241, TOOL_X + 16, TOOL_TAP(T_PAN), 0},           /* PAN */
    {245, 260, 0, 0, 0},                                   /* a drag (drawn below) */
    {270, 271, BTN_TAP_X(B_MORE), BTN_TAP_Y(B_MORE), 0},   /* MORE */
    {285, 286, 20, 20, 0},                                 /* (closes it: a tap outside) */
    {290, 291, 0, 0, CTR_KEY_R},                           /* the other frame */
    {300, 301, 0, 0, CTR_KEY_SELECT},                      /* the chooser */
    {315, 316, 0, 0, CTR_KEY_RIGHT},
    {330, 331, 0, 0, CTR_KEY_A},                           /* the next Pokémon */
    {360, 361, CAN_X + 4 + CORNER_W / 2, CORNER_Y + 8, 0}, /* its back */
    {420, 421, 0, 0, CTR_KEY_B},                           /* back to the gallery */
};
static const uint16_t sDumpTicks[] = {30, 75, 105, 160, 172, 215, 225, 235, 265, 280, 310, 400};

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
    if (sE.screen != S_CHOOSE && !Bind())
    {
        if (sE.tick % 16 == 0) Draw();
        return true;
    }
    if (!sE.active) return true;
    if (sE.screen == S_CHOOSE) TouchChoose(in);
    else if (sE.screen == S_OPTIONS) TouchOptions(in);
    else if (sE.screen == S_CONFIRM_RESET) TouchConfirm(in);
    else
    {
        TouchEdit(in);
        PadPan(in);
        CstickMon(in);
    }
    if (!sE.active) return true;
    if (sE.screen != S_CHOOSE && sE.pl != NULL)
    {
        CtrStereoMon_SetEditing(true, sE.layer);
        CtrStereoMon_SetEditView(sE.frame, sE.animate, sE.outlineOn, sE.mark);
    }
    if (sE.toastFrames && --sE.toastFrames == 0) sE.dirtyDraw = true;
    /* The lasso's dashes run while it is drawn. */
    if (sE.lassoing && sE.tick % 8 == 0) sE.dirtyDraw = true;
    /* The chooser's icons come in one a frame. */
    if (sE.screen == S_CHOOSE && sE.iconsMissing) sE.dirtyDraw = true;
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
