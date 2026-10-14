/*
 * The stereoscopic planes' editor (3ds_stereo_mon.h), game side: the
 * bottom screen while the gallery's Pokémon is being edited.
 *
 * The top screen keeps showing the Pokémon in its own colours as its stack
 * of planes (the viewer, 3ds_stereo_mon.c, composing the planes again after
 * every change), the plane being edited outlined there in the colour
 * opposite the Pokémon's own, so it shows against any Pokémon; X tints it
 * there by plane too (VIEW LAYERS). The bottom screen is the editor: the
 * picture over the whole screen with every pixel tinted by its plane's
 * colour (four clashing hues, the plane being edited bright, the others
 * dim, the tint's strength a button), the four planes down the right
 * nearest first with the grid's and the tint's buttons under them; over
 * the picture, along its top, the chooser, undo and redo, the frame, the
 * battle and FINAL; along its bottom the zoom slider with the hand beside
 * it and the front/back switch; down its left the pencil and the lasso
 * with the brush size over them while the pencil is in hand. A tool
 * paints planes, never pixels: the picture is the game's, a plane number
 * is all a pixel has, so painting a pixel into one plane takes it out of
 * the other; the eraser moves pixels to the plane of their neighbours.
 *
 *  PENCIL  the pixels under the stylus join the plane being edited.
 *  ERASER  the pixels under the stylus leave their plane for the one most
 *          of their neighbours are on: stray pixels of another plane go
 *          back into the plane around them.
 *  LASSO   a tap: the colour cell under the stylus joins the plane (same
 *          colour, bounded by the outline and transparency, with the outline
 *          that touches it); a loop: every pixel inside it does.
 *  HAND    a drag moves the picture; so does the Circle Pad.
 *
 * BATTLE saves and starts a real battle against the picture (a wild one,
 * or the trainer's), its planes as saved: the battle's own scene, text and
 * depth are the test; the editor opens again when it ends.
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
 * Each frame is edited by hand on its own: nothing is carried from one
 * frame to the other (the frames differ too much for that to be right).
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
#include "3ds_edit_sprites.h"
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
/* Along the picture's top: the chooser at the left, undo and redo in the
 * middle, the save at the right. */
#define BAR_Y 3
#define BAR_H 24
#define DEX_X 4
#define DEX_W 28
#define MORE_X (DEX_X + DEX_W + 2)
#define MORE_W 22
#define UNDO_W 22
#define UNDO_X (CAN_X + CAN_W / 2 - UNDO_W - 1)
#define REDO_X (CAN_X + CAN_W / 2 + 1)
#define SAVE_W 26
#define SAVE_X (RIGHT_X - 4 - SAVE_W)
/* At the picture's bottom right: the grid under the save, the tint (its
 * strength written on it) at its left. */
#define GRID_X SAVE_X
#define GRID_Y (H - 4 - BAR_H)
/* The tint under the planes, across the column, its strength written on it. */
#define TINT_X (ROW_X - 1)
#define TINT_W (ROW_SIDE + 2)
#define TINT_H 18
#define TINT_Y (H - 2 - TINT_H)
/* Along its bottom: the hand, the zoom slider in the middle, the grid. */
#define BOT_H 32
#define BOT_Y (H - BOT_H)
#define ZOOM_X0 (CAN_X + CAN_W / 2 - 50)
#define ZOOM_X1 (CAN_X + CAN_W / 2 + 50)
#define ZOOM_CY (BOT_Y + BOT_H / 2)
/* Down its left, in the middle of the height under the bar: the pencil,
 * the lasso, the bucket and the hand, round, a little larger to hit. The
 * brush size's slider opens to the pencil's right while it is held. */
#define TOOL_X 4
#define TOOL_W 30
#define TOOL_H 30
#define TOOL_STEP 34
/* The tools above the hand (every one but PAN), centred in the height. */
#define TOOL_Y0 ((BAR_Y + BAR_H + BOT_Y) / 2 - ((T_COUNT - 2) * TOOL_STEP - (TOOL_STEP - TOOL_H)) / 2)
#define SIZE_X0 (TOOL_X + TOOL_W + 14)
#define SIZE_X1 (SIZE_X0 + 72)
#define SIZE_HOLD_FRAMES 18
#define KNOB_R 8
/* The zoom's knob is larger: the magnifier sits on it. */
#define ZOOM_KNOB_R 12
/* The planes' column: the frames' buttons (F1, F2) at the top, then a
 * framed thumbnail a plane, nearest first; the front/back tabs down the
 * column's right edge. */
#define ROW_X (RIGHT_X + 4)
#define TAB_Y 0
#define TAB_H 14
#define RIGHT_W (W - RIGHT_X)
#define TAB_W (RIGHT_W / 2)
#define FRAME_Y (TAB_Y + TAB_H + 2)
#define FRAME_H 18
#define FRAME_W 22
#define ROW_Y0 (FRAME_Y + FRAME_H + 4)
#define ROW_STEP 46
#define ROW_FRAME 3
#define ROW_SIDE 44
#define THUMB (ROW_SIDE - 2 * ROW_FRAME)
#define ROWS_END (ROW_Y0 + PLANES * ROW_STEP)
/* The buttons under them, in a row: the tint, the outline and MORE. */
#define BTN_Y0 (ROWS_END + 2)
#define BTN_W 16
#define BTN_H 18
#define BTN_STEP_X 17
/* Frames a row is held still before it is grabbed for moving. */
#define GRAB_FRAMES 24

#define UNDO_DEPTH 12
#define LASSO_POINTS 512
#define TOAST_FRAMES 90
/* Below this size (screen pixels) a lasso is a tap. */
#define TAP_SLOP 6
/* Unsaved changes go to the SD card this long after the last one. */
#define AUTOSAVE_FRAMES 90

enum { T_BRUSH, T_ERASE, T_LASSO, T_FILL, T_PAN, T_COUNT };
enum { B_OPACITY, B_GRID, B_COUNT };
/* The pressed button's number: the tools', the right's, the bars'. */
enum { P_NONE = -1, P_TOOL = 0, P_RIGHT = 16, P_DEX = 32, P_MORE, P_UNDO, P_REDO, P_FRAME1, P_FRAME2, P_SAVE, P_TAB_F, P_TAB_B, P_BATTLE };
/* What the top screen shows: the colours, the planes tinted, the battle. */
enum { V_COLOURS, V_LAYERS, V_BATTLE, V_COUNT };
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
/* MAX is the plane's colour outright, shaded by the pixel's brightness
 * alone (DrawPicture): the Pokémon's own colours cannot blend in. */
static const uint8_t sOpacities[] = {20, 12, 6, 3};
static const char *const sOpacityNames[] = {"MAX", "60", "30", "15"};
#define OPACITIES ((int)(sizeof(sOpacities) / sizeof(sOpacities[0])))
/* The planes' colours, far to near (3ds_stereo_mon.h): the same for every
 * Pokémon, and the top screen's VIEW LAYERS uses them too. */
static const uint16_t sPlaneColours[PLANES] = CTR_STEREO_EDIT_COLOURS;

enum { S_EDIT, S_OPTIONS, S_CHOOSE, S_CONFIRM_RESET, S_CONFIRM_FINAL, S_KEYBOARD };
enum { O_DEPTH, O_CONTROLS, O_AUTHOR, O_RESET, O_CLOSE, O_COUNT };
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
    uint8_t tool, size, zoom, opacity, grid, view;
    bool outlineOn, animate, fromGallery, controls;
    /* The picture is in a real battle (CtrStereoMonGame_TryBattleOn). */
    bool inBattle;
    /* The pencil held: frames so far, and its size slider open. */
    unsigned pencilHold;
    bool sizeSlider;
    /* The tick of the last change, for the autosave. */
    uint32_t changedAt;
    /* The top screen's words want drawing again (MakeHelpSheet). */
    bool helpDirty;
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
    /* The chooser: the entry the editor had when it opened, and the one the
     * top screen shows now (the cursor's, as it moves). */
    unsigned chooseOpened, previewIndex;
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
    sE.helpDirty = true;
    sE.changedAt = sE.tick;
    CtrStereoMon_Recompose();
    CtrStereoBattle_LiveChanged();
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

static void ApplyErase(void);

static void EndStroke(void)
{
    char line[64];

    if (!sE.stroking) return;
    sE.stroking = false;
    if (sE.tool == T_ERASE && sE.strokeName && strcmp(sE.strokeName, "erase") == 0) ApplyErase();
    if (sE.strokeCount == 0)
    {
        /* Nothing changed: the snapshot goes. */
        sUndoNext = (sUndoNext + UNDO_DEPTH - 1) % UNDO_DEPTH;
        --sUndoCount;
        return;
    }
    snprintf(line, sizeof(line), "f%u %s P%u %upx", sE.frame, sE.strokeName, sE.layer, sE.strokeCount);
    CtrStereoMon_LogEdit(line);
    Changed();
}

static void SetPixelTo(unsigned i, unsigned plane)
{
    if (!Opaque(sE.frame, i) || GetPlane(sE.frame, i) == plane) return;
    PutPlane(sE.frame, i, plane);
    BitSet(sE.changed, i);
    ++sE.strokeCount;
}

static void SetPixel(unsigned i) { SetPixelTo(i, sE.layer); }

/* The eraser gathers the pixels under its stroke (sErase, shown lifted on
 * the picture) and moves them all at once when the stylus lifts
 * (ApplyErase): each goes to the plane most of the pixels around it that
 * were NOT under the stroke are on (within ERASE_REACH, the nearer
 * counting more); a pixel with none of those within reach waits for the
 * pass after, once its neighbours have been placed, so a whole patch is
 * filled in from its edge inwards. Moving them as the stroke went had the
 * pixels hopping between planes under the stylus. */
#define ERASE_REACH 3
static uint8_t sErase[N / 8];

static void ErasePixel(unsigned i)
{
    if (!Opaque(sE.frame, i) || BitGet(sErase, i)) return;
    BitSet(sErase, i);
    ++sE.strokeCount;
}

static void ApplyErase(void)
{
    static uint8_t placed[N / 8];
    bool progress = true;

    memcpy(placed, sErase, sizeof(placed));
    sE.strokeCount = 0;
    while (progress)
    {
        static uint8_t now[N / 8];

        progress = false;
        memset(now, 0, sizeof(now));
        for (unsigned i = 0; i < N; ++i)
        {
            unsigned votes[PLANES] = {0}, best = NONE, bestVotes = 0;
            int x = (int)(i % DIM), y = (int)(i / DIM);

            if (!BitGet(placed, i)) continue;
            for (int dy = -ERASE_REACH; dy <= ERASE_REACH; ++dy)
                for (int dx = -ERASE_REACH; dx <= ERASE_REACH; ++dx)
                {
                    int nx = x + dx, ny = y + dy, d = dx * dx + dy * dy;
                    unsigned j, p;

                    if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= DIM || ny >= DIM) continue;
                    j = (unsigned)(ny * DIM + nx);
                    if (BitGet(placed, j) || !Opaque(sE.frame, j)) continue;
                    p = GetPlane(sE.frame, j);
                    if (p == NONE || p >= PLANES) continue;
                    votes[p] += (unsigned)(2 * ERASE_REACH * ERASE_REACH + 1 - d);
                }
            for (unsigned p = 0; p < PLANES; ++p)
                if (votes[p] > bestVotes) { bestVotes = votes[p]; best = p; }
            if (best == NONE) continue;
            SetPixelTo(i, best);
            BitSet(now, i);
            progress = true;
        }
        /* Placed this pass: they count for their neighbours in the next. */
        for (unsigned k = 0; k < sizeof(placed); ++k) placed[k] &= (uint8_t)~now[k];
    }
    memset(sErase, 0, sizeof(sErase));
}

/* The brush (or the eraser) at one picture pixel: a disc `size` pixels across. */
static void BrushAt(int x, int y)
{
    int r = (int)sE.size - 1;

    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
        {
            int px = x + dx, py = y + dy;

            if (px < 0 || py < 0 || px >= DIM || py >= DIM || dx * dx + dy * dy > r * r + r) continue;
            if (sE.tool == T_ERASE) ErasePixel((unsigned)(py * DIM + px));
            else SetPixel((unsigned)(py * DIM + px));
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
#define FREE_X (TOOL_X + TOOL_W + 4)
#define FREE_Y (BAR_Y + BAR_H + 2)
#define FREE_BOTTOM (BOT_Y - 2)
static void ResetView(void)
{
    int bx, by, side, room = (CAN_W - FREE_X) < (FREE_BOTTOM - FREE_Y) ? (CAN_W - FREE_X) : (FREE_BOTTOM - FREE_Y);

    PictureBox(&bx, &by, &side);
    sE.zoom = 0;
    while (sE.zoom + 1 < ZOOMS && side * sZooms[sE.zoom + 1] <= room - 4) ++sE.zoom;
    sE.originX = FREE_X - CAN_X + (CAN_W - FREE_X - side * Zoom()) / 2 - bx * Zoom();
    sE.originY = FREE_Y + (FREE_BOTTOM - FREE_Y - side * Zoom()) / 2 - by * Zoom();
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
    EndStroke();
}

/* The bucket: from the picture pixel tapped, every opaque pixel reached
 * without crossing the plane being edited (nor transparency) goes to that
 * plane: the inside of a shape outlined on it. */
static void BucketFill(unsigned s)
{
    static const int8_t nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    unsigned head = 0, tail = 0;

    if (!Opaque(sE.frame, s) || GetPlane(sE.frame, s) == sE.layer) return;
    memset(sMask, 0, sizeof(sMask));
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
            if (sLabel[k] >= 0 || !Opaque(sE.frame, k) || GetPlane(sE.frame, k) == sE.layer) continue;
            sLabel[k] = 0;
            sQueue[tail++] = (int16_t)k;
        }
    }
    BeginStroke("bucket");
    for (unsigned i = 0; i < N; ++i)
        if (BitGet(sMask, i)) SetPixel(i);
    EndStroke();
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
    sE.layer = plane;
    sE.helpDirty = true;
    sE.dirtyDraw = true;
}

/* The other frame, when there is one. */
static void OtherFrame(void)
{
    char line[24];

    if (sE.frames < 2) return;
    sE.frame = (sE.frame + 1) % sE.frames;
    sE.helpDirty = true;
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
        sE.helpDirty = true;
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
    CtrStereoBattle_SetLive(id, pl, frames);
    sE.helpDirty = true;
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
    sE.outlineOn = CtrSettings_GetInt("edit_outline", 1) != 0;
    sE.grid = (uint8_t)CtrSettings_GetInt("edit_grid", GRID_WHITE);
    if (sE.grid > GRID_BLACK) sE.grid = GRID_OFF;
    sE.view = (uint8_t)CtrSettings_GetInt("edit_view", V_COLOURS);
    sE.controls = CtrSettings_GetInt("edit_controls", 1) != 0;
    if (sE.view >= V_COUNT) sE.view = V_COLOURS;
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
    CtrStereoBattle_SetLive(0, NULL, 0);
    sE.active = false;
    sE.pl = NULL;
    CtrBottom_ForceRedraw();
    if (leaveGallery) CtrStereoMonGame_CloseGallery();
}

/* Another Pokémon: saved, then the gallery is told; Bind picks it up. */
static void Choose(unsigned index)
{
    if (sE.inBattle)
    {
        Toast("AFTER THE BATTLE");
        return;
    }
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
            uint16_t id = CtrStereoMon_GalleryIdAt(i), other = CtrStereoMon_Counterpart(id);
            bool edited = CtrStereoMon_HasEdit(id) || (other && CtrStereoMon_HasEdit(other));

            /* A Pokémon is one entry: its back is reached from its front
             * (the F/B tabs); a back with no front is listed by itself. */
            if ((id & 0xC000) == CTR_STEREO_ID_BACK && other) continue;
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
    sE.chooseOpened = sE.previewIndex = index;
    BuildList();
    /* A back being edited is listed under its front. */
    if ((sE.id & 0xC000) == CTR_STEREO_ID_BACK && CtrStereoMon_Counterpart(sE.id))
    {
        int front = CtrStereoMon_GalleryIndexOf(CtrStereoMon_Counterpart(sE.id));

        if (front >= 0) index = (unsigned)front;
    }
    for (unsigned k = 0; k < sListCount; ++k)
        if (sList[k] == index) sE.chooseCursor = k;
}

/* The chooser closed without a pick: the picture it opened on comes back
 * if another was shown meanwhile. */
static void CloseChooser(void)
{
    if (sE.previewIndex != sE.chooseOpened) Choose(sE.chooseOpened);
    else sE.screen = S_EDIT;
    sE.dirtyDraw = true;
}

/* The top screen shows the entry under the chooser's cursor, as the
 * gallery would (the edit so far is saved, so the viewer may be reused). */
static void PreviewChosen(void)
{
    unsigned index;

    if (sE.screen != S_CHOOSE || sListCount == 0) return;
    index = sList[sE.chooseCursor];
    if (index == sE.previewIndex) return;
    sE.previewIndex = index;
    sE.pl = NULL;
    sE.id = 0;
    CtrStereoMon_SetEditing(false, 0);
    CtrStereoMonGame_SelectIndex(index);
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
#define B_OUTLINE 100
static void RightAct(int button);
static void TryBattle(void);

static void DpadKeys(unsigned dpad)
{
    if (dpad & CTR_KEY_UP) SetZoom(sE.zoom + 1, CAN_W / 2, H / 2);
    if (dpad & CTR_KEY_DOWN) SetZoom(sE.zoom - 1, CAN_W / 2, H / 2);
    if (dpad & CTR_KEY_LEFT) Undo();
    if (dpad & CTR_KEY_RIGHT) Redo();
}

static void CycleView(void)
{
    static const char *const names[V_COUNT] = {"TOP SCREEN: COLOURS", "TOP SCREEN: LAYERS", "TOP SCREEN: BATTLE"};

    sE.view = (uint8_t)((sE.view + 1) % V_COUNT);
    sE.helpDirty = true;
    CtrSettings_SetInt("edit_view", sE.view);
    Toast(names[sE.view]);
}

/* The brush's next size (round again after the largest). */
static void CycleSize(void)
{
    sE.size = (uint8_t)(sE.size % SIZES + 1);
    sE.dirtyDraw = true;
}

static void ChooseKeys(unsigned down)
{
    unsigned count = sListCount;

    if (down & (CTR_KEY_B | CTR_KEY_SELECT)) { CloseChooser(); return; }
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
    if (sE.screen == S_OPTIONS || sE.screen == S_CONFIRM_RESET || sE.screen == S_CONFIRM_FINAL)
    {
        if (down & (CTR_KEY_B | CTR_KEY_START)) sE.screen = S_EDIT;
        return;
    }
    /* A short press of L: the pencil, to draw on the plane just picked
     * (held, L is the picker). */
    if (down & CTR_KEY_L) sE.lPicked = false;
    if (lReleased && !sE.lPicked && lFrames < L_HOLD_FRAMES && sE.tool != T_BRUSH)
        sE.tool = T_BRUSH;
    if (down & CTR_KEY_R) OtherFrame();
    if (down & CTR_KEY_SELECT)
    {
        OpenChooser();
        return;
    }
    if (down & CTR_KEY_B) { Close(!sE.fromGallery); return; }
    if (down & CTR_KEY_START) TryBattle();
    if (down & CTR_KEY_A) RightAct(B_OUTLINE);
    if (down & CTR_KEY_X) CycleView();
    if (down & CTR_KEY_Y)
    {
        sE.animate = !sE.animate;
        sE.helpDirty = true;
        Toast(sE.animate ? "ANIMATING" : "STILL");
    }
    /* The D-pad only: the Circle Pad pans (CtrStereoEdit_BottomFrame). */
    DpadKeys(down & in->physicalDown & CTR_KEY_DPAD);
}

/* ── Touch ──────────────────────────────────────────────────────────── */

/* The value a slider's track from `lo` (0) to `hi` (steps - 1) gives at `at`. */
static int SliderValue(int at, int lo, int hi, int steps)
{
    int v = ((at - lo) * (steps - 1) * 2 + (hi - lo)) / (2 * (hi - lo));

    return v < 0 ? 0 : v >= steps ? steps - 1 : v;
}

static void SliderAt(int x, int y)
{
    (void)y;
    if (sE.slider == SLIDER_ZOOM) SetZoom(SliderValue(x, ZOOM_X0, ZOOM_X1, ZOOMS), CAN_W / 2, H / 2);
    else if (sE.slider == SLIDER_SIZE) sE.size = (uint8_t)(1 + SliderValue(x, SIZE_X0, SIZE_X1, SIZES));
    sE.dirtyDraw = true;
}

static int RowAt(int y)
{
    int row = (y - ROW_Y0) / ROW_STEP;

    return y < ROW_Y0 ? 0 : row >= PLANES ? PLANES - 1 : row;
}

/* The tools' buttons, one under the other down the left. */
static void ToolButton(int tool, int *x, int *y)
{
    *x = TOOL_X;
    *y = tool == T_PAN ? H - 4 - TOOL_H : TOOL_Y0 + tool * TOOL_STEP;
}

/* The pencil's own touch: a tap picks it, a tap on it when picked takes
 * the next brush size, a hold opens the size's slider (TouchEdit). */
/* The pencil's and the eraser's own touch: a tap picks it, a tap on it
 * when picked takes the next brush size, a hold opens the size's slider
 * (TouchEdit). */
static void PencilDown(int tool)
{
    if (sE.tool == tool) sE.pencilHold = 1;
    sE.tool = (uint8_t)tool;
}

static void CycleGrid(void)
{
    sE.grid = (uint8_t)((sE.grid + 1) % 3);
    CtrSettings_SetInt("edit_grid", sE.grid);
    Toast(sE.grid == GRID_OFF ? "GRID OFF" : sE.grid == GRID_WHITE ? "GRID WHITE" : "GRID BLACK");
}

static int ToolAt(int x, int y)
{
    for (int t = 0; t < T_COUNT; ++t)
    {
        int bx, by;

        ToolButton(t, &bx, &by);
        /* The pencil's reach takes in the size handle beside it. */
        if (x >= bx && x < bx + TOOL_W + (t == T_BRUSH || t == T_ERASE ? 9 : 0) && y >= by && y < by + TOOL_H) return t;
    }
    return -1;
}

/* The column's buttons (and A for the outline). */
static void RightAct(int button)
{
    char line[24];

    switch (button)
    {
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
    default: CycleGrid(); break;
    }
}

/* The front's back, the back's front: the tabs. */
static void SwapSide(bool toBack)
{
    uint16_t other = CtrStereoMon_Counterpart(sE.id);
    int index;

    if (toBack == ((sE.id & 0xC000) != 0)) return;
    if (sE.inBattle)
    {
        Toast("AFTER THE BATTLE");
        return;
    }
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

/* The save's button: unsaved changes go to the card; saved already, it
 * asks whether the picture is final. */
static void SaveTap(void)
{
    if (sE.dirtyFile) Save(false);
    else sE.screen = S_CONFIRM_FINAL;
}

/* A frame's button: that frame (F2 greyed with one frame only). */
static void FrameTap(unsigned frame)
{
    char line[24];

    if (frame >= sE.frames)
    {
        Toast("ONE FRAME ONLY");
        return;
    }
    if (frame == sE.frame) return;
    sE.frame = frame;
    sE.helpDirty = true;
    snprintf(line, sizeof(line), "FRAME %u OF %u", sE.frame + 1, sE.frames);
    Toast(line);
}

/* START: the picture tried in a real battle: saved, then the game side
 * starts the battle (the field, the intro at turbo, both sides as their
 * planes, the real scenery and boxes at their depths); the top screen and
 * the buttons are the game's until it ends, the bottom screen and the
 * stylus stay the editor's. START again (the game side sees it) runs
 * from the battle. */
static void TryBattle(void)
{
    if (sE.inBattle)
    {
        CtrStereoMonGame_LeaveBattle();
        Toast("LEAVING THE BATTLE");
        return;
    }
    if (!CtrStereoMonGame_TryBattle(sE.id))
    {
        Toast("A BATTLE NEEDS THE FIELD AND A PARTY");
        return;
    }
    Save(true);
    CtrStereoMon_LogEdit("battle");
    Toast("DRAW ON. START AGAIN TO COME BACK");
}

/* The bar along the picture's top: a tap on one of its buttons. */
static bool BarAct(int x, int y)
{
    if (y < BAR_Y || y >= BAR_Y + BAR_H) return false;
    if (x >= DEX_X && x < DEX_X + DEX_W) { sE.pressed = P_DEX; OpenChooser(); }
    else if (x >= MORE_X && x < MORE_X + MORE_W) { sE.pressed = P_MORE; sE.screen = S_OPTIONS; }
    else if (x >= UNDO_X && x < UNDO_X + UNDO_W) { sE.pressed = P_UNDO; Undo(); }
    else if (x >= REDO_X && x < REDO_X + UNDO_W) { sE.pressed = P_REDO; Redo(); }
    else if (x >= SAVE_X && x < SAVE_X + SAVE_W) { sE.pressed = P_SAVE; SaveTap(); }
    else return false;
    return true;
}

/* The bottom right: the tint, the grid. */
static bool CornerAct(int x, int y)
{
    if (y < GRID_Y || y >= GRID_Y + BAR_H) return false;
    if (x >= GRID_X && x < GRID_X + SAVE_W) { sE.pressed = P_RIGHT + B_GRID; RightAct(B_GRID); }
    else return false;
    return true;
}

/* The bar along the picture's bottom: the zoom slider, the grid. */
static bool BottomAct(int x, int y)
{
    if (y < BOT_Y) return false;
    if (x >= ZOOM_X0 - ZOOM_KNOB_R - 2 && x <= ZOOM_X1 + ZOOM_KNOB_R + 2)
    {
        sE.slider = SLIDER_ZOOM;
        SliderAt(x, y);
    }
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
            if (y < FRAME_Y)
            {
                bool back = x >= RIGHT_X + TAB_W;

                sE.pressed = back ? P_TAB_B : P_TAB_F;
                SwapSide(back);
            }
            else if (y < ROW_Y0)
            {
                bool second = x >= ROW_X + FRAME_W;

                sE.pressed = second ? P_FRAME2 : P_FRAME1;
                FrameTap(second ? 1 : 0);
            }
            else if (y >= TINT_Y)
            {
                sE.pressed = P_RIGHT + B_OPACITY;
                RightAct(B_OPACITY);
            }
            else if (y < ROWS_END)
            {
                sE.rowTouch = true;
                sE.rowStartY = y;
                sE.holdFrames = 0;
                sE.grabRow = -1;
            }
        }
        else if ((b = ToolAt(x, y)) >= 0)
        {
            sE.pressed = P_TOOL + b;
            if (b == T_BRUSH || b == T_ERASE) PencilDown(b);
            else sE.tool = (uint8_t)b;
        }
        else if (BarAct(x, y) || CornerAct(x, y) || BottomAct(x, y)) {}
        else if (sE.heldKeys & CTR_KEY_L)
        {
            /* L + a tap: the plane under the stylus. */
            int px, py;

            sE.lPicked = true;
            if (PictureAt(x, y, &px, &py) && Opaque(sE.frame, (unsigned)(py * DIM + px))
                && GetPlane(sE.frame, (unsigned)(py * DIM + px)) != NONE)
                PickPlane(GetPlane(sE.frame, (unsigned)(py * DIM + px)));
        }
        else if (sE.tool == T_PAN)
        {
            sE.panning = true;
            sE.panX = x;
            sE.panY = y;
        }
        else if (sE.tool == T_LASSO)
        {
            sE.lassoing = true;
            sE.path[0][0] = (int16_t)(x - CAN_X);
            sE.path[0][1] = (int16_t)y;
            sE.pathLen = 1;
        }
        else if (sE.tool == T_FILL)
        {
            int px, py;

            if (PictureAt(x, y, &px, &py)) BucketFill((unsigned)(py * DIM + px));
            else Toast("BUCKET: TAP INSIDE A SHAPE");
        }
        else
        {
            int px, py;

            BeginStroke(sE.tool == T_ERASE ? "erase" : "brush");
            memset(sErase, 0, sizeof(sErase));
            if (PictureAt(x, y, &px, &py)) BrushLine(px, py);
        }
        sE.dirtyDraw = true;
    }
    else if (in->touchActive)
    {
        if (sE.pencilHold && !sE.sizeSlider && ++sE.pencilHold > SIZE_HOLD_FRAMES)
        {
            /* Held: the size's slider opens beside it, the stylus on it. */
            sE.sizeSlider = true;
            sE.slider = SLIDER_SIZE;
            sE.dirtyDraw = true;
        }
        if (sE.slider != SLIDER_NONE)
            SliderAt(x, y);
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
            /* The top screen follows once the stylus lifts (EndStroke ->
             * Changed): composing the stack again mid-stroke restarted the
             * whole texture each time, lagging both screens. */
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
        /* The pencil let go before its slider opened: the next size. */
        if (sE.pencilHold && !sE.sizeSlider) CycleSize();
        sE.pencilHold = 0;
        sE.sizeSlider = false;
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
        sE.helpDirty = true;
        break;
    case O_CONTROLS: sE.controls = !sE.controls; CtrSettings_SetInt("edit_controls", sE.controls); break;
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
        if (in->touchX >= CF_X + 16 && in->touchX < CF_X + 96)
        {
            if (sE.screen == S_CONFIRM_FINAL) { ToggleFinal(); sE.screen = S_EDIT; }
            else OptionAct(O_RESET + 100, false);
        }
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
 * edited bright and saturated, the others dim and greyed, so the plane in
 * hand stands out), pixels without a plane hatched red; the grid as a
 * faint white or black over it, as a painting program's. */
static void DrawPicture(void)
{
    const uint16_t out = Rgb(5, 10, 5), bg0 = Rgb(7, 14, 7), bg1 = Rgb(6, 13, 7), red = Rgb(28, 6, 8),
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
            else if (k >= 20)
            {
                /* MAX: the plane's colour outright, the pixel's brightness
                 * shading it (the outline stays dark), the plane edited
                 * full and the others a little darker. */
                unsigned l = Luma(pic[i]) + 64; /* 64..320 */

                c = Mix(sPlaneColours[p], 0, l > 320 ? 320 : l, 320);
                if (p != sE.layer) c = Mix(out, c, 1, 4);
            }
            else if (p == sE.layer)
            {
                /* The plane edited: its colour at full strength, lifted. */
                c = Mix(sPlaneColours[p], Rgb565Of(pic[i]), k, 20);
                c = Mix(Rgb(31, 63, 31), c, 1, 8);
            }
            else
            {
                /* The others: the pixel greyed, a dim tint, half dark. */
                c = Mix(Muted(sPlaneColours[p]), Grey(Rgb565Of(pic[i])), k / 2, 20);
                c = Mix(out, c, 1, 2);
            }
            /* Under the eraser's stroke, waiting to be placed: their own
             * colour drained of the plane's tint, so they read as let go. */
            if (sE.stroking && sE.tool == T_ERASE && BitGet(sErase, i)) c = Mix(Grey(c), c, 3, 4);
            CanvasFill(sx, sy, s, s, c);
            if (sE.grid != GRID_OFF)
            {
                /* A quarter white, or a third black, over the pixel's own colour. */
                uint16_t line = sE.grid == GRID_WHITE ? Mix(gridInk, c, 1, 4) : Mix(gridInk, c, 1, 3);

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
}

/* An icon (3ds_edit_icons.h: Pixelarticons', `size` pixels square) at
 * (x, y), pixel for pixel in the game's dark grey, a faint shadow a pixel
 * down and right (a fifth black over what is there), as the game's text. */
#define INK_DARK Rgb(9, 19, 11)
#define INK_WHITE Rgb(31, 63, 31)
#define INK_OFF Rgb(17, 36, 19)
static inline bool GlyphInk(const uint32_t *rows, int size, int r, int c)
{
    return r >= 0 && r < size && c >= 0 && c < size && ((rows[r] >> (size - 1 - c)) & 1);
}

static void DrawGlyph(const uint32_t *rows, int size, int x, int y, uint16_t ink)
{
    for (int r = 0; r <= size; ++r)
        for (int c = 0; c <= size; ++c)
        {
            if (GlyphInk(rows, size, r, c)) Px(x + c, y + r, ink);
            else if (GlyphInk(rows, size, r - 1, c - 1) && x + c >= 0 && y + r >= 0 && x + c < W && y + r < H)
            {
                uint16_t *at = &sCanvas[(x + c) * H + (H - 1 - (y + r))];

                *at = Mix(Rgb(0, 0, 0), *at, 1, 5);
            }
        }
}

static void Icon(int icon, bool large, int x, int y, uint16_t ink)
{
    if (large) DrawGlyph(sIconLarge[icon], ICON_LARGE, x, y, ink);
    else DrawGlyph(sIconSmall[icon], ICON_SMALL, x, y, ink);
}

/* A colour sprite (3ds_edit_sprites.h: the user's own, 16 px) at (x, y),
 * pixel for pixel, with the icons' faint shadow a pixel down and right. */
static inline bool SpriteInk(const uint16_t *px, int r, int c)
{
    return r >= 0 && r < SPRITE_SIZE && c >= 0 && c < SPRITE_SIZE && px[r * SPRITE_SIZE + c] != SPRITE_CLEAR;
}

static void Sprite(int sprite, int x, int y)
{
    const uint16_t *px = sSprites[sprite];

    for (int r = 0; r <= SPRITE_SIZE; ++r)
        for (int c = 0; c <= SPRITE_SIZE; ++c)
        {
            if (SpriteInk(px, r, c)) Px(x + c, y + r, px[r * SPRITE_SIZE + c]);
            else if (SpriteInk(px, r - 1, c - 1) && x + c >= 0 && y + r >= 0 && x + c < W && y + r < H)
            {
                uint16_t *at = &sCanvas[(x + c) * H + (H - 1 - (y + r))];

                *at = Mix(Rgb(0, 0, 0), *at, 1, 5);
            }
        }
}

/* The ink an icon takes on a plate: the dark grey on a plain one, and on
 * a chosen (green) one too, where white did not read. */
static uint16_t InkOn(bool chosen) { (void)chosen; return INK_DARK; }

/* A round plate, in the plates' look: a dark rim, a light face with a
 * highlight along its top and a shade along its bottom; green when chosen. */
static void DrawRoundPlate(int cx, int cy, int r, bool chosen, bool pressed)
{
    const uint16_t rim = chosen ? Rgb(3, 22, 7) : Rgb(7, 18, 13);
    const uint16_t face = chosen ? Rgb(11, 48, 16) : Rgb(30, 60, 31);
    const uint16_t high = chosen ? Rgb(21, 60, 23) : Rgb(31, 63, 31);
    const uint16_t shade = chosen ? Rgb(7, 38, 12) : Rgb(25, 52, 28);

    for (int dy = -r; dy <= r; ++dy)
    {
        int half = (int)(sqrtf((float)(r * r + r - dy * dy)) + 0.5f);
        int inner = r * r + r - dy * dy - 2 * r > 0 ? (int)(sqrtf((float)(r * r + r - dy * dy - 2 * r)) + 0.5f) : 0;
        uint16_t body = !pressed && dy < -r + 3 ? high : dy > r - 4 ? shade : face;

        if (half <= 0) continue;
        CtrBottomDraw_Fill(cx - half, cy + dy, 2 * half + 1, 1, rim);
        if (inner > 0) CtrBottomDraw_Fill(cx - inner, cy + dy, 2 * inner + 1, 1, body);
    }
}

/* The tint's button: three circles overlapping, red, yellow and blue, each
 * laid over what is under it at the tint's strength, ringed in its colour. */
static void DrawTintCircles(int x, int y)
{
    static const uint16_t colours[3] = {0xF800, 0xFFE0, 0x201F};
    static const int8_t centres[3][2] = {{5, 6}, {10, 6}, {8, 11}};
    const int r = 4;

    for (int k = 0; k < 3; ++k)
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx)
            {
                int d = dx * dx + dy * dy, px = x + centres[k][0] + dx, py = y + centres[k][1] + dy;
                uint16_t *at;

                if (d > r * r + r || px < 0 || py < 0 || px >= W || py >= H) continue;
                at = &sCanvas[px * H + (H - 1 - py)];
                *at = d > r * r - r ? Mix(colours[k], Rgb(1, 2, 2), 2, 3) : Mix(colours[k], *at, sOpacities[sE.opacity], 20);
            }
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

/* A pixel laid over what is there, `ka` parts in `k` (k parts: opaque). */
static inline void PxMix(int x, int y, uint16_t c, unsigned ka, unsigned k)
{
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    if (ka >= k) sCanvas[x * H + (H - 1 - y)] = c;
    else sCanvas[x * H + (H - 1 - y)] = Mix(c, sCanvas[x * H + (H - 1 - y)], ka, k);
}

static void FillMix(int x, int y, int w, int h, uint16_t c, unsigned ka, unsigned k)
{
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx) PxMix(xx, yy, c, ka, k);
}

static void DrawDisc(int cx, int cy, int r, uint16_t c)
{
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            if (dx * dx + dy * dy <= r * r + r) Px(cx + dx, cy + dy, c);
}

static void DiscMix(int cx, int cy, int r, uint16_t c, unsigned ka, unsigned k)
{
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            if (dx * dx + dy * dy <= r * r + r) PxMix(cx + dx, cy + dy, c, ka, k);
}

/* A slider's track and steps from x0 to x1 at y, the steps up to the value
 * lit, laid on `ka` parts in `k` (see-through while nobody holds it); the
 * knob is the caller's. */
static void DrawTrack(int x0, int x1, int y, int value, int steps, unsigned ka, unsigned k)
{
    const uint16_t track = Rgb(26, 54, 28), edge = Rgb(1, 2, 2), lit = Rgb(31, 56, 8);

    FillMix(x0 - 1, y - 2, x1 - x0 + 3, 5, edge, ka, k);
    FillMix(x0, y - 1, x1 - x0 + 1, 3, track, ka, k);
    for (int st = 0; st < steps; ++st)
    {
        int tx = x0 + (x1 - x0) * st / (steps - 1);

        FillMix(tx - 1, y - 5, 3, 11, edge, ka, k);
        FillMix(tx, y - 4, 1, 9, st <= value ? lit : track, ka, k);
    }
}

static void DrawKnob(int cx, int cy, int r, unsigned ka, unsigned k)
{
    DiscMix(cx, cy, r + 1, Rgb(1, 2, 2), ka, k);
    DiscMix(cx, cy, r, Rgb(31, 63, 31), ka, k);
}

/* The zoom slider along the bottom, its knob the magnifier: half
 * see-through over the picture until it is held. */
static void DrawZoomSlider(void)
{
    int kx = ZOOM_X0 + (ZOOM_X1 - ZOOM_X0) * sE.zoom / (ZOOMS - 1);
    unsigned ka = sE.slider == SLIDER_ZOOM ? 2 : 1;

    DrawTrack(ZOOM_X0, ZOOM_X1, ZOOM_CY, sE.zoom, ZOOMS, ka, 2);
    DrawKnob(kx, ZOOM_CY, ZOOM_KNOB_R, ka, 2);
    if (ka == 2) Sprite(SPRITE_ZOOM, kx - SPRITE_SIZE / 2, ZOOM_CY - SPRITE_SIZE / 2);
    else
        for (int r = 0; r < SPRITE_SIZE; ++r)
            for (int c = 0; c < SPRITE_SIZE; ++c)
                if (sSprites[SPRITE_ZOOM][r * SPRITE_SIZE + c] != SPRITE_CLEAR)
                    PxMix(kx - SPRITE_SIZE / 2 + c, ZOOM_CY - SPRITE_SIZE / 2 + r, sSprites[SPRITE_ZOOM][r * SPRITE_SIZE + c], 1, 2);
}

/* The brush's dot: the plane's colour, larger for a larger brush, a stroke
 * of the colour's half-dark just inside its edge so it reads on white. */
static int BrushDotRadius(void) { return 3 + 2 * ((int)sE.size - 1); }

static void DrawBrushDot(int cx, int cy)
{
    int r = BrushDotRadius();

    DrawDisc(cx, cy, r + 1, Rgb(31, 63, 31));
    DrawDisc(cx, cy, r, Mix(sPlaneColours[sE.layer], Rgb(0, 0, 0), 1, 2));
    DrawDisc(cx, cy, r - 1, sPlaneColours[sE.layer]);
}

/* The drawer's handle beside the pencil: the four brush sizes as dots, a
 * pixel to four across, one over the other (the size in hand lit), so the
 * pencil reads as something that opens: a tap for the next size, a hold
 * for the slider. */
static void DrawSizeHandle(int x, int y)
{
    const uint16_t rim = Rgb(1, 2, 2);
    int cy = y + TOOL_H / 2 - 9;

    FillMix(x, y + TOOL_H / 2 - 12, 7, 24, rim, 1, 2);
    for (int s = 0; s < SIZES; ++s, cy += 6)
    {
        int w = s + 1;
        uint16_t c = s + 1 == sE.size ? Rgb(31, 63, 31) : Mix(sPlaneColours[sE.layer], Rgb(31, 63, 31), 1, 2);

        CtrBottomDraw_Fill(x + 3 - w / 2, cy - w / 2, w, w, c);
    }
}

/* The brush size's slider, open to the pencil's right while the pencil is
 * held: its knob the brush's dot. */
static void DrawSizeSlider(void)
{
    int x, y, cy, kx;

    ToolButton(sE.tool == T_ERASE ? T_ERASE : T_BRUSH, &x, &y);
    cy = y + TOOL_H / 2;
    kx = SIZE_X0 + (SIZE_X1 - SIZE_X0) * (sE.size - 1) / (SIZES - 1);
    DrawTrack(SIZE_X0, SIZE_X1, cy, sE.size - 1, SIZES, 1, 1);
    DrawKnob(kx, cy, KNOB_R + 3, 1, 1);
    DrawBrushDot(kx, cy);
}

/* The tools: round buttons down the left; the pencil's with the brush's
 * dot behind it; the grid's button at the bottom right. */
static void DrawTools(void)
{
    static const int sprites[T_COUNT] = {SPRITE_PENCIL, SPRITE_ERASER, SPRITE_LASSO, SPRITE_BUCKET, SPRITE_HAND};

    for (int t = 0; t < T_COUNT; ++t)
    {
        int x, y;
        bool on = sE.tool == t;

        ToolButton(t, &x, &y);
        DrawRoundPlate(x + TOOL_W / 2, y + TOOL_H / 2, TOOL_W / 2, on, on || sE.pressed == P_TOOL + t);
        if (t == T_BRUSH) DrawBrushDot(x + TOOL_W / 2, y + TOOL_H / 2);
        Sprite(sprites[t], x + (TOOL_W - SPRITE_SIZE) / 2, y + (TOOL_H - SPRITE_SIZE) / 2);
        if ((t == T_BRUSH || t == T_ERASE) && !sE.sizeSlider) DrawSizeHandle(x + TOOL_W + 1, y);
    }
    if (sE.sizeSlider) DrawSizeSlider();
    DrawZoomSlider();
}

/* The bar along the picture's top. */
static void DrawBar(void)
{
    const bool final = CtrStereoMon_EditMeta()->finalized, gridOn = sE.grid != GRID_OFF;

    /* The chooser: the game's Pokédex. */
    CtrBottomDraw_Plate(DEX_X, BAR_Y, DEX_W, BAR_H, 0, sE.pressed == P_DEX);
    CtrBottomDraw_GamePic(CTR_DRAW_PIC_DEX, DEX_X + DEX_W / 2, BAR_Y + BAR_H / 2);
    PlateIcon(MORE_X, BAR_Y, MORE_W, BAR_H, 0, sE.pressed == P_MORE, ICON_SETTINGS, false, INK_DARK);
    PlateIcon(UNDO_X, BAR_Y, UNDO_W, BAR_H, sUndoCount ? 0 : 2, sE.pressed == P_UNDO, ICON_UNDO, false,
              sUndoCount ? INK_DARK : INK_OFF);
    PlateIcon(REDO_X, BAR_Y, UNDO_W, BAR_H, sRedoCount ? 0 : 2, sE.pressed == P_REDO, ICON_REDO, false,
              sRedoCount ? INK_DARK : INK_OFF);
    /* The save: green when final, its dot green once saved, red while
     * changes wait for the card. */
    CtrBottomDraw_Plate(SAVE_X, BAR_Y, SAVE_W, BAR_H, final ? 1 : 0, final || sE.pressed == P_SAVE);
    Sprite(SPRITE_SAVE, SAVE_X + (SAVE_W - SPRITE_SIZE) / 2 - 1, BAR_Y + (BAR_H - SPRITE_SIZE) / 2);
    DrawDisc(SAVE_X + SAVE_W - 5, BAR_Y + 5, 4, Rgb(1, 2, 2));
    DrawDisc(SAVE_X + SAVE_W - 5, BAR_Y + 5, 3, sE.dirtyFile ? Rgb(31, 12, 8) : Rgb(8, 56, 10));
    /* The bottom right: the grid (off: struck through; white or black as
     * its lines are). */
    CtrBottomDraw_Plate(GRID_X, GRID_Y, SAVE_W, BAR_H, gridOn ? 1 : 0, gridOn || sE.pressed == P_RIGHT + B_GRID);
    if (sE.grid == GRID_WHITE) CtrBottomDraw_Fill(GRID_X + 6, GRID_Y + 5, 14, 14, Rgb(8, 30, 12));
    Icon(ICON_GRID, false, GRID_X + (SAVE_W - ICON_SMALL) / 2, GRID_Y + (BAR_H - ICON_SMALL) / 2,
         sE.grid == GRID_WHITE ? INK_WHITE : sE.grid == GRID_BLACK ? Rgb(0, 0, 0) : INK_DARK);
    if (!gridOn)
    {
        int x0 = GRID_X + (SAVE_W - ICON_SMALL) / 2 - 1, y0 = GRID_Y + (BAR_H - ICON_SMALL) / 2 + ICON_SMALL;

        for (int i = 0; i <= ICON_SMALL + 1; ++i)
        {
            Px(x0 + i - 1, y0 - i, Rgb(30, 60, 31));
            Px(x0 + i + 2, y0 - i, Rgb(30, 60, 31));
            Px(x0 + i, y0 - i, INK_DARK);
            Px(x0 + i + 1, y0 - i, INK_DARK);
        }
    }
}

static void DrawToast(void)
{
    if ((sE.heldKeys & CTR_KEY_L) || sE.toastFrames)
    {
        bool picking = sE.heldKeys & CTR_KEY_L;
        int x0 = FREE_X, w = RIGHT_X - 4 - x0;

        CtrBottomDraw_Plate(x0, BOT_Y - 26, w, 20, 0, false);
        CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, picking ? "TAP A PIXEL TO PICK ITS PLANE" : sE.toast, x0 + w / 2,
                                  BOT_Y - 23, picking ? CTR_DRAW_BLUE : CTR_DRAW_DARK);
    }
}

/* The planes' column: the planes nearest first, each framed in its colour
 * (bright for the plane being edited, muted for the others), then the
 * grid's three looks and the tint, the outline and MORE. */
/* A filled box with its corners rounded off by two pixels (the three
 * corner pixels left as they were). */
static void RoundFill(int x, int y, int w, int h, uint16_t c)
{
    static const int8_t cut[3][2] = {{0, 0}, {1, 0}, {0, 1}};
    uint16_t keep[4][3];

    for (int k = 0; k < 3; ++k)
    {
        keep[0][k] = sCanvas[(x + cut[k][0]) * H + (H - 1 - (y + cut[k][1]))];
        keep[1][k] = sCanvas[(x + w - 1 - cut[k][0]) * H + (H - 1 - (y + cut[k][1]))];
        keep[2][k] = sCanvas[(x + cut[k][0]) * H + (H - 1 - (y + h - 1 - cut[k][1]))];
        keep[3][k] = sCanvas[(x + w - 1 - cut[k][0]) * H + (H - 1 - (y + h - 1 - cut[k][1]))];
    }
    CtrBottomDraw_Fill(x, y, w, h, c);
    for (int k = 0; k < 3; ++k)
    {
        Px(x + cut[k][0], y + cut[k][1], keep[0][k]);
        Px(x + w - 1 - cut[k][0], y + cut[k][1], keep[1][k]);
        Px(x + cut[k][0], y + h - 1 - cut[k][1], keep[2][k]);
        Px(x + w - 1 - cut[k][0], y + h - 1 - cut[k][1], keep[3][k]);
    }
}

/* The front/back tabs along the column's top, as a folder's: the side
 * shown in front, light, open at its bottom onto the frames' buttons; the
 * other behind it, a shade darker and a pixel lower (greyed when the pack
 * has no other side). */
/* A tab across half the column: the side shown in front, in the column's
 * own green and open onto it; the other behind, a shade darker and a
 * pixel lower, a soft shadow line where the front one overlaps it. */
/* A box with only its top corners rounded off (RoundFill's cut), its
 * bottom edge straight: a tab, not a button. */
static void TabFill(int x, int y, int w, int h, uint16_t c)
{
    static const int8_t cut[3][2] = {{0, 0}, {1, 0}, {0, 1}};
    uint16_t keep[2][3];

    for (int k = 0; k < 3; ++k)
    {
        keep[0][k] = sCanvas[(x + cut[k][0]) * H + (H - 1 - (y + cut[k][1]))];
        keep[1][k] = sCanvas[(x + w - 1 - cut[k][0]) * H + (H - 1 - (y + cut[k][1]))];
    }
    CtrBottomDraw_Fill(x, y, w, h, c);
    for (int k = 0; k < 3; ++k)
    {
        Px(x + cut[k][0], y + cut[k][1], keep[0][k]);
        Px(x + w - 1 - cut[k][0], y + cut[k][1], keep[1][k]);
    }
}

static void DrawTab(int x, const char *label, bool front, bool other, uint16_t panel)
{
    /* The column's green in front; a darker green behind (duller still
     * when the pack has no other side). The bottoms are straight, as a
     * folder's: the front tab open onto the column below it, the back one
     * standing on the ledge. */
    const uint16_t rim = Rgb(1, 2, 2), dark = Rgb(10, 38, 14);
    const uint16_t face = front ? panel : other ? dark : Mix(dark, panel, 1, 2);
    int y = TAB_Y + (front ? 0 : 2), h = TAB_H - (front ? 0 : 2);

    TabFill(x, y, TAB_W, h + 1, rim);
    TabFill(x + 1, y + 1, TAB_W - 2, h, face);
    if (front) CtrBottomDraw_Fill(x + 1, y + h, TAB_W - 2, 3, face);
    CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, label, x + TAB_W / 2, y, front ? CTR_DRAW_DARK : CTR_DRAW_WHITE);
}

static void DrawTabs(void)
{
    const bool back = (sE.id & 0xC000) != 0, other = CtrStereoMon_Counterpart(sE.id) != 0;
    const uint16_t panel = sCanvas[(RIGHT_X + 2) * H + (H - 1 - (TAB_Y + TAB_H + 2))];
    const int x0 = RIGHT_X, fx = back ? x0 + TAB_W : x0;

    /* A ledge the tabs stand on, the front one drawn last so it overlaps;
     * a shadow a pixel wide along the front one's inner edge. */
    CtrBottomDraw_Fill(x0, TAB_Y + TAB_H, 2 * TAB_W, 1, Rgb(1, 2, 2));
    DrawTab(back ? x0 : x0 + TAB_W, back ? "FRT" : "BCK", false, other, panel);
    DrawTab(fx, back ? "BCK" : "FRT", true, true, panel);
    FillMix(back ? fx - 1 : fx + TAB_W, TAB_Y + 1, 1, TAB_H, Rgb(0, 0, 0), 1, 3);
}

static void DrawRight(void)
{
    char label[12];

    CtrBottomDraw_Panel(RIGHT_X, 0, W, H);
    /* The frames' switch over the planes: one rimmed group, the frame
     * shown green, the other white (grey with one frame only). */
    RoundFill(ROW_X - 1, FRAME_Y, 2 * FRAME_W + 2, FRAME_H, Rgb(1, 2, 2));
    for (unsigned f = 0; f < 2; ++f)
    {
        int x = ROW_X + (int)f * FRAME_W;
        bool on = sE.frame == f, off = f >= sE.frames;

        snprintf(label, sizeof(label), "F%u", f + 1);
        CtrBottomDraw_Fill(x, FRAME_Y + 1, FRAME_W, FRAME_H - 2, off ? Rgb(22, 44, 23) : on ? Rgb(11, 48, 16) : Rgb(30, 60, 31));
        CtrBottomDraw_Fill(x, FRAME_Y + 1, FRAME_W, 2, off ? Rgb(24, 48, 25) : on ? Rgb(21, 60, 23) : Rgb(31, 63, 31));
        CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, label, x + FRAME_W / 2, FRAME_Y + 2,
                                  off ? CTR_DRAW_OFF : on ? CTR_DRAW_WHITE : CTR_DRAW_DARK);
    }
    CtrBottomDraw_Fill(ROW_X + FRAME_W, FRAME_Y + 1, 1, FRAME_H - 2, Rgb(1, 2, 2));
    DrawTabs();
    /* The tint under the planes, its strength written on it. */
    CtrBottomDraw_Plate(TINT_X, TINT_Y, TINT_W, TINT_H, 0, sE.pressed == P_RIGHT + B_OPACITY);
    DrawTintCircles(TINT_X + 2, TINT_Y);
    snprintf(label, sizeof(label), "%s", sOpacityNames[sE.opacity]);
    CtrBottomDraw_Text(CTR_DRAW_FONT_SMALL, label, TINT_X + 20, TINT_Y + 2, CTR_DRAW_DARK);
    if (sOpacities[sE.opacity] < 20)
    {
        /* A per cent sign, the game's font having none. */
        int px = TINT_X + 20 + CtrBottomDraw_TextWidth(CTR_DRAW_FONT_SMALL, label) + 2, py = TINT_Y + 5;

        for (int i = 0; i < 7; ++i) Px(px + 6 - i, py + i, INK_DARK);
        CtrBottomDraw_Fill(px, py, 2, 2, INK_DARK);
        CtrBottomDraw_Fill(px + 5, py + 5, 2, 2, INK_DARK);
    }
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
            RoundFill(ROW_X - 3, y - 2, ROW_SIDE + 6, ROW_SIDE + 4, Rgb(1, 2, 2));
            RoundFill(ROW_X - 2, y - 1, ROW_SIDE + 4, ROW_SIDE + 2, Rgb(31, 63, 31));
        }
        RoundFill(ROW_X, y, ROW_SIDE, ROW_SIDE, colour);
        DrawPreview(plane, ROW_X + ROW_FRAME, y + ROW_FRAME, THUMB, !cur);
        CtrBottomDraw_Fill(ROW_X + ROW_FRAME, y + ROW_FRAME, 10, 13, colour);
        snprintf(label, sizeof(label), "%d", r + 1);
        CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, label, ROW_X + ROW_FRAME + 5, y + ROW_FRAME,
                                  light ? CTR_DRAW_DARK : CTR_DRAW_WHITE);
        if (grabbed) CtrBottomDraw_Fill(ROW_X + ROW_FRAME, y + ROW_FRAME, THUMB, 4, Rgb(31, 50, 10));
        if (sE.grabRow >= 0 && sE.grabTarget == r && !grabbed)
            CtrBottomDraw_Fill(ROW_X - 2, sE.grabTarget > sE.grabRow ? y + ROW_SIDE - 1 : y - 3, ROW_SIDE + 4, 3, Rgb(31, 50, 10));
    }
}

static void DrawConfirm(void)
{
    const bool final = sE.screen == S_CONFIRM_FINAL, isFinal = CtrStereoMon_EditMeta()->finalized;

    CtrBottomDraw_Box(0, CF_X, CF_Y, CF_W / 8, CF_H / 8, false);
    if (final)
    {
        CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, isFinal ? "THIS EDIT IS MARKED FINAL." : "SAVED. MARK THIS EDIT AS FINAL?",
                                  CF_X + CF_W / 2, CF_Y + 10, CTR_DRAW_DARK);
        CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, isFinal ? "TAKE THE MARK OFF AGAIN?" : "YOU CAN STILL CHANGE IT AFTER.",
                                  CF_X + CF_W / 2, CF_Y + 26, CTR_DRAW_BLUE);
    }
    else
    {
        CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, "BACK TO THE PACK'S PLANES?", CF_X + CF_W / 2, CF_Y + 10, CTR_DRAW_DARK);
        CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, "YOUR EDITS OF THIS ONE ARE LOST.", CF_X + CF_W / 2, CF_Y + 26, CTR_DRAW_RED);
    }
    CtrBottomDraw_Plate(CF_X + 16, CF_Y + 52, 80, 24, final ? 1 : 0, false);
    CtrBottomDraw_Plate(CF_X + 104, CF_Y + 52, 80, 24, final ? 0 : 1, false);
    CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, final ? (isFinal ? "REOPEN" : "FINALIZE") : "RESET", CF_X + 56, CF_Y + 57,
                              final ? CTR_DRAW_WHITE : CTR_DRAW_RED);
    CtrBottomDraw_TextCentred(CTR_DRAW_FONT_SMALL, final ? "CANCEL" : "KEEP", CF_X + 144, CF_Y + 57,
                              final ? CTR_DRAW_DARK : CTR_DRAW_WHITE);
}

static void DrawOptions(void)
{
    static const char *const names[O_COUNT] = {"3D DEPTH", "SHOW CONTROLS", "AUTHOR", "RESET TO PACK", "CLOSE"};
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
        else if (row == O_CONTROLS) snprintf(value, sizeof(value), "%s", sE.controls ? "ON" : "OFF");
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
    Icon(ICON_SEARCH, false, CH_SEARCH_X + 5, 6, InkOn(sE.query[0] != 0));
    CtrBottomDraw_Text(CTR_DRAW_FONT_SMALL, sE.query[0] ? sE.query : "SEARCH  Y", CH_SEARCH_X + 22, 5,
                       sE.query[0] ? CTR_DRAW_WHITE : CTR_DRAW_OFF);
    CtrBottomDraw_Plate(CH_SHOW_X, 2, CH_SHOW_W, 20, sE.onlyEdited ? 1 : 0, sE.onlyEdited);
    Icon(sE.onlyEdited ? ICON_EDIT : ICON_FILTER, false, CH_SHOW_X + 4, 6, InkOn(sE.onlyEdited));
    CtrBottomDraw_Text(CTR_DRAW_FONT_SMALL, sE.onlyEdited ? "EDITED" : "ALL", CH_SHOW_X + 19, 5,
                       sE.onlyEdited ? CTR_DRAW_WHITE : CTR_DRAW_DARK);
    CtrBottomDraw_Plate(CH_SORT_X, 2, CH_SORT_W, 20, sE.editedFirst ? 1 : 0, sE.editedFirst);
    Icon(ICON_SORT, false, CH_SORT_X + 4, 6, InkOn(sE.editedFirst));
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
        /* Edited (either side): a pencil; finished (both sides): a tick. */
        {
            uint16_t other = CtrStereoMon_Counterpart(id);

            if (CtrStereoMon_IsFinal(id) && (!other || CtrStereoMon_IsFinal(other)))
                Icon(ICON_CHECK, false, x + W / CH_COLS - 18, y + 3, Rgb(8, 58, 10));
            else if (CtrStereoMon_HasEdit(id) || (other && CtrStereoMon_HasEdit(other)))
                Icon(ICON_EDIT, false, x + W / CH_COLS - 18, y + 3, Rgb(31, 52, 8));
        }
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
    Icon(sE.kbFor == KB_SEARCH ? ICON_SEARCH : ICON_USER, false, 12, 21, INK_WHITE);
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
        if (wideIcons[k] >= 0) Icon(wideIcons[k], false, x + 6, KB_ROW2_Y + 8, InkOn(k == KB_OK));
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

/* The top screen's words, in the game's small font: each line drawn into
 * this canvas with the bottom screen's font, then handed to the viewer as
 * a cell of its help sheet (CtrStereoMon_HelpCell). The game font has no
 * percent sign, so the depth is "3D DEPTH 50". */
static void MakeHelpSheet(void)
{
    static const char *const fixed[] = {"NEXT/PICK PLANE", "OUTLINE", "PAN", "ZOOM", "UNDO/REDO", "ANIMATE", "VIEW",
                                        "FRAME", "SWITCH PKMN", "SAVE", "BACK", ""};
    static const char *const views[V_COUNT] = {"", "  LAYERS", "  BATTLE"};
    static uint8_t alpha[CTR_HELP_CELL_W * CTR_HELP_CELL_H];
    const CtrStereoEditMeta *meta = CtrStereoMon_EditMeta();
    char text[CTR_HELP_CELLS][40];
    unsigned number = PLANES - sE.layer;

    if ((sE.id & 0xC000) == 0) snprintf(text[CTR_HELP_NAME], 40, "No.%03u %s", sE.id, sE.name);
    else snprintf(text[CTR_HELP_NAME], 40, "%s", sE.name);
    if (sE.frames < 2) text[CTR_HELP_FRAME][0] = 0;
    else if (sE.animate) snprintf(text[CTR_HELP_FRAME], 40, "ANIMATING");
    else snprintf(text[CTR_HELP_FRAME], 40, "FRAME %u OF %u", sE.frame + 1, sE.frames);
    snprintf(text[CTR_HELP_PLANE], 40, "PLANE %u%s", number, number == 1 ? " FRONT" : number == PLANES ? " BACK" : "");
    snprintf(text[CTR_HELP_DEPTH], 40, "3D DEPTH %d%s", CtrStereoMon_Depth(), views[sE.view]);
    if (meta->revision) snprintf(text[CTR_HELP_REVISION], 40, "R%u %s", meta->revision, meta->author[0] ? meta->author : "?");
    else text[CTR_HELP_REVISION][0] = 0;
    snprintf(text[CTR_HELP_STATE], 40, "%s", meta->finalized ? "FINAL" : sE.dirtyFile || CtrStereoMon_HasEdit(sE.id) ? "EDITED" : "");
    for (unsigned k = 0; k < sizeof(fixed) / sizeof(fixed[0]); ++k) snprintf(text[CTR_HELP_L_PLANE + k], 40, "%s", fixed[k]);
    /* All the lines at once into the canvas, white on black. */
    CtrBottomDraw_Begin(sCanvas);
    CtrBottomDraw_Fill(0, 0, W, H, 0);
    for (unsigned cell = 0; cell < CTR_HELP_CELLS; ++cell)
        if (text[cell][0])
            CtrBottomDraw_Text(CTR_DRAW_FONT_SMALL, text[cell], (int)(cell % 2) * CTR_HELP_CELL_W + 1,
                               (int)(cell / 2) * CTR_HELP_CELL_H, CTR_DRAW_WHITE);
    CtrBottomDraw_End();
    for (unsigned cell = 0; cell < CTR_HELP_CELLS; ++cell)
    {
        int x0 = (int)(cell % 2) * CTR_HELP_CELL_W, y0 = (int)(cell / 2) * CTR_HELP_CELL_H;
        int width = text[cell][0] ? CtrBottomDraw_TextWidth(CTR_DRAW_FONT_SMALL, text[cell]) + 2 : 0;

        for (int y = 0; y < CTR_HELP_CELL_H; ++y)
            for (int x = 0; x < CTR_HELP_CELL_W; ++x)
            {
                /* The letters are white; their shadow (dark) and the black are not ink. */
                uint16_t c = sCanvas[(x0 + x) * H + (H - 1 - (y0 + y))];

                alpha[y * CTR_HELP_CELL_W + x] = ((c >> 11) & 31) > 20 ? 255 : 0;
            }
        CtrStereoMon_HelpCell(cell, alpha, (unsigned)(width < 0 ? 0 : width));
    }
    sE.dirtyDraw = true;
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
        DrawTools();
        DrawBar();
        DrawToast();
        DrawRight();
        if (sE.screen == S_OPTIONS) DrawOptions();
        if (sE.screen == S_CONFIRM_RESET || sE.screen == S_CONFIRM_FINAL) DrawConfirm();
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
#define BTN_TAP_X(b) (ROW_X - 1 + (ROW_SIDE + 2 - B_COUNT * BTN_STEP_X + 1) / 2 + (b) * BTN_STEP_X + BTN_W / 2)
#define BTN_TAP_Y(b) (BTN_Y0 + BTN_H / 2)
#define KEY_TAP_X(k) (KB_X + ((k) % KB_COLS) * KB_KEY_W + KB_KEY_W / 2)
#define KEY_TAP_Y(k) (KB_Y + ((k) / KB_COLS) * KB_KEY_H + KB_KEY_H / 2)
#define TOOL_TAP_X (TOOL_X + TOOL_W / 2)
#define TOOL_TAP_Y(t) (TOOL_Y0 + (t) * TOOL_STEP + TOOL_H / 2)
static const Step sScript[] = {
    {40, 41, TOOL_TAP_X, TOOL_TAP_Y(T_LASSO), 0},              /* LASSO */
    {60, 61, 160, H / 2, 0},                                   /* a tap on the body: its colour cell */
    {90, 91, ROW_X + 25, ROW_Y0 + 2 * ROW_STEP + 25, 0},       /* plane 3 */
    {120, 152, 0, 0, 0},                                       /* a loop (drawn below) */
    {165, 166, TINT_X + 20, TINT_Y + 12, 0},                   /* the tint lighter */
    {175, 176, TOOL_TAP_X, TOOL_TAP_Y(T_BRUSH), 0},            /* PENCIL */
    {180, 181, TOOL_TAP_X, TOOL_TAP_Y(T_BRUSH), 0},            /* its next size */
    {184, 214, TOOL_TAP_X, TOOL_TAP_Y(T_BRUSH), 0},            /* held: the size's slider */
    {222, 242, 0, 0, 0},                                       /* a stroke (drawn below) */
    {244, 245, TOOL_TAP_X, TOOL_TAP_Y(T_ERASE), 0},            /* ERASER */
    {246, 249, 0, 0, 0},                                       /* over the stroke's start (drawn below) */
    {250, 251, 0, 0, CTR_KEY_UP},                              /* zoom in */
    {256, 257, GRID_X + 12, GRID_Y + 12, 0},                   /* the grid's next look */
    {259, 260, GRID_X + 12, GRID_Y + 12, 0},                   /* and the next */
    {262, 263, 0, 0, CTR_KEY_X},                               /* the next view on the top screen */
    {266, 267, TOOL_TAP_X, H - 4 - TOOL_H / 2, 0},            /* HAND */
    {270, 285, 0, 0, 0},                                       /* a drag (drawn below) */
    {288, 289, UNDO_X + 12, BAR_Y + 12, 0},                    /* undo */
    {292, 293, TOOL_TAP_X, TOOL_TAP_Y(T_FILL), 0},             /* BUCKET */
    {296, 297, 150, H / 2 - 10, 0},                            /* a tap inside */
    {302, 303, MORE_X + 11, BAR_Y + 12, 0},                    /* MORE */
    {310, 311, 10, 60, 0},                                     /* (closes it: a tap outside) */
    {314, 315, 0, 0, CTR_KEY_R},                               /* the other frame */
    {318, 319, SAVE_X + 13, BAR_Y + 12, 0},                    /* save */
    {322, 323, SAVE_X + 13, BAR_Y + 12, 0},                    /* saved: final? */
    {330, 331, CF_X + 144, CF_Y + 64, 0},                      /* cancel */
    {336, 337, 0, 0, CTR_KEY_SELECT},                          /* the chooser */
    {342, 343, CH_SORT_X + 10, 10, 0},                         /* edited first */
    {348, 349, CH_SEARCH_X + 20, 10, 0},                       /* the search */
    {354, 355, KEY_TAP_X(1), KEY_TAP_Y(1), 0},                 /* B */
    {358, 359, KEY_TAP_X(20), KEY_TAP_Y(20), 0},               /* U */
    {362, 363, KEY_TAP_X(11), KEY_TAP_Y(11), 0},               /* L */
    {366, 367, 0, 0, CTR_KEY_START},                           /* OK */
    {372, 373, 0, 0, CTR_KEY_A},                               /* the first found */
    {390, 391, ROW_X - 1 + FRAME_W + 2 + 11, FRAME_Y + 10, 0}, /* F2 */
    {400, 401, RIGHT_X + TAB_W + 10, TAB_Y + 7, 0},        /* the back's tab */
    {440, 441, RIGHT_X + 12, TAB_Y + 7, 0},                    /* the front's tab */
    {480, 481, 0, 0, CTR_KEY_X},                               /* the battle view */
    {486, 487, 0, 0, CTR_KEY_Y},                               /* animated */
};
static const uint16_t sDumpTicks[] = {30, 75, 105, 160, 172, 183, 200, 219, 243, 249, 255, 265, 287, 291, 300, 308, 320, 326, 346, 352, 364, 369, 398, 420, 460, 520};

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
        else if (s->from == 184)
        {
            /* The pencil held, the stylus drawn along its slider. */
            x = TOOL_TAP_X + (int)(sE.tick - 184) * 3;
            y = TOOL_TAP_Y(T_BRUSH);
        }
        else if (s->from == 222)
        {
            x = CAN_X + 60 + (int)(sE.tick - 222) * 5;
            y = 150;
        }
        else if (s->from == 246)
        {
            /* Over the stroke where it crossed the body. */
            x = CAN_X + 120 + (int)(sE.tick - 246) * 5;
            y = 150;
        }
        else if (s->from == 270)
        {
            x = CAN_X + 100 - (int)(sE.tick - 270) * 3;
            y = 120 - (int)(sE.tick - 270) * 2;
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
    if (sE.inBattle != CtrStereoMonGame_TryBattleOn())
    {
        sE.inBattle = !sE.inBattle;
        sE.dirtyDraw = true;
        if (!sE.inBattle)
        {
            sE.screen = S_EDIT;
            Toast("BACK FROM THE BATTLE");
        }
    }
    if (sE.screen == S_CHOOSE) { TouchChoose(in); PreviewChosen(); }
    else if (sE.screen == S_KEYBOARD) TouchKeyboard(in);
    else if (sE.screen == S_OPTIONS) TouchOptions(in);
    else if (sE.screen == S_CONFIRM_RESET || sE.screen == S_CONFIRM_FINAL) TouchConfirm(in);
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
        CtrStereoMon_SetEditView(sE.frame, sE.animate, sE.outlineOn, sE.mark, sE.view, sE.controls);
    }
    /* Changes go to the card by themselves once the stylus has rested. */
    if (sE.dirtyFile && !sE.touching && sE.screen == S_EDIT && sE.tick - sE.changedAt > AUTOSAVE_FRAMES) Save(true);
    if (sE.helpDirty && sE.pl != NULL)
    {
        sE.helpDirty = false;
        MakeHelpSheet();
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
