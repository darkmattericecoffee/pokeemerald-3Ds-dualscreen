/*
 * The menu's plates and its text styles (included by 3ds_bottom_ui.c).
 *
 * A plate is the battle menus' plate (scripts/gen_battle_art.py, plate()) at
 * any size and in any colour: a soft shadow under it, an ink outline with its
 * corners cut, a face shaded top to bottom with a darker band across its
 * lower part, a gloss over its top half, a thin light rim inside the edge and
 * the screen's faint scanlines. It may have a cream end on the left (slanted,
 * with three light stripes past it and a faint Poké Ball in it) and a light
 * head under a coloured top (the move plates' look). Focused, it glows
 * orange round its edge; pressed, it sinks SINK pixels and goes a step
 * darker.
 *
 * The script draws at four times the size and shrinks the result; here each
 * shape's edge coverage is worked out from its distance to the pixel instead
 * (a chamfered rectangle is the intersection of a few half-planes, so its
 * distance is the largest of theirs), and a Gaussian blur of an edge from
 * the normal distribution's integral: the same layers, in the same order,
 * at one sample a pixel.
 *
 * A plate is made once and kept (sPlates): each pixel as what is left of
 * whatever lies under it (t, 0..255) and what the plate adds over that (c,
 * RGB565), so drawing it again is a multiply and an add a pixel.
 */

#define PLATE_SINK 2
/* Room round a plate for its ring and shadow: left, top, right, bottom. */
#define PLATE_ML 6
#define PLATE_MT 6
#define PLATE_MR 6
#define PLATE_MB 8

enum { PLATE_STATE_NORMAL, PLATE_STATE_FOCUS, PLATE_STATE_PRESSED };

typedef struct
{
    s16 w, h;
    s16 cream;          /* width of the cream end on the left, 0 for none */
    u8 cut;             /* the corners' cut */
    u8 slope;           /* the cream end's slant, in hundredths */
    u8 head;            /* rows of coloured top over a light face, 0 for none */
    u8 state;           /* PLATE_STATE_* */
    Rgb base;
} PlateSpec;

static const Rgb sPlateInk = {22, 28, 38};
static const Rgb sPlateRing = {248, 104, 48};

/* The normal distribution's integral, Phi(z), at z = i / 16 - 4. */
#define PHI_STEPS 129
static float sPhi[PHI_STEPS];

static void BuildPhi(void)
{
    for (int i = 0; i < PHI_STEPS; ++i)
        sPhi[i] = 0.5f * erfcf(-((float)i / 16.0f - 4.0f) / 1.41421356f);
}

/* A blurred edge's coverage at signed distance d (negative inside), blur sigma. */
static inline float Blurred(float d, float sigma)
{
    float z = -d / sigma, f;
    int i;

    if (z <= -4.0f)
        return 0.0f;
    if (z >= 4.0f)
        return 1.0f;
    f = (z + 4.0f) * 16.0f;
    i = (int)f;
    return sPhi[i] + (sPhi[i + 1] - sPhi[i]) * (f - i);
}

static inline float Clamp01(float v)
{
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

/* A pixel's coverage by a shape whose edge is at signed distance d. */
static inline float Cover(float d)
{
    return Clamp01(0.5f - d);
}

/* Distance from (u, v) to the chamfered rectangle (x, y, w, h, cut). */
static inline float ChamferDist(float u, float v, float x, float y, float w, float h, float c)
{
    float hw = w * 0.5f, hh = h * 0.5f;
    float du = fabsf(u - (x + hw)), dv = fabsf(v - (y + hh));
    float a = du - hw, b = dv - hh, k = (du + dv - (hw + hh - c)) * 0.70710678f;

    if (b > a) a = b;
    return k > a ? k : a;
}

typedef struct { float r, g, b; } Rgbf;

static inline Rgbf Mixf(Rgbf a, Rgbf b, float t)
{
    return (Rgbf){a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

static inline Rgbf Fl(Rgb c)
{
    return (Rgbf){c.r, c.g, c.b};
}

/* Through colour stops (t ascending) at t. */
static Rgbf Ramp(const float *ts, const Rgbf *cs, int n, float t)
{
    if (t <= ts[0])
        return cs[0];
    for (int k = 0; k + 1 < n; ++k)
        if (t <= ts[k + 1])
            return Mixf(cs[k], cs[k + 1], (t - ts[k]) / (ts[k + 1] - ts[k] > 1e-6f ? ts[k + 1] - ts[k] : 1e-6f));
    return cs[n - 1];
}

/* One layer over the pixel so far: t what is left of the backdrop, c what is added. */
typedef struct { float t, r, g, b; } Px;

static inline void Paint(Px *p, Rgbf c, float a)
{
    if (a <= 0.0f)
        return;
    if (a > 1.0f)
        a = 1.0f;
    p->t *= 1.0f - a;
    p->r += (c.r - p->r) * a;
    p->g += (c.g - p->g) * a;
    p->b += (c.b - p->b) * a;
}

/* The faint Poké Ball's coverage at distance (dx, dy) from its centre. */
static float FaintBall(float dx, float dy, float r)
{
    float d = sqrtf(dx * dx + dy * dy);
    float ring = Cover(d - r) - Cover(d - (r - 2.5f));
    float band = Clamp01(0.5f - (fabsf(dy) - r * 0.11f)) * Cover(fabsf(dx) - r);
    float disc = Cover(d - r * 0.42f);
    float hole = Cover(d - r * 0.24f);
    float m = ring > band ? ring : band;

    if (disc > m)
        m = disc;
    return m * (1.0f - hole);
}

/*
 * The plate's picture, (w + PLATE_ML + PLATE_MR) x (h + PLATE_MT + PLATE_MB),
 * column by column, its top-left PLATE_ML, PLATE_MT out from the plate's. `oddRow`
 * says whether its first row is an odd one on screen (the scanlines).
 */
static void RenderPlate(const PlateSpec *s, u8 *outT, u16 *outC, bool8 oddRow)
{
    static const Rgbf white = {255, 255, 255}, black = {0, 0, 0};
    const Rgbf base = Fl(s->base), ink = Fl(sPlateInk), ring = Fl(sPlateRing);
    const Rgbf creamTop = {252, 245, 234}, creamLow = {236, 224, 208}, creamBall = {222, 204, 188};
    const Rgbf shadowColour = {10, 50, 44};
    bool8 pressed = s->state == PLATE_STATE_PRESSED, focus = s->state == PLATE_STATE_FOCUS;
    int pw = s->w + PLATE_ML + PLATE_MR, ph = s->h + PLATE_MT + PLATE_MB;
    float w = s->w, h = s->h, c = s->cut, slope = s->slope / 100.0f;
    float oy = pressed ? PLATE_SINK : 0, hh = h - (pressed ? PLATE_SINK : 0);
    float shadowY = pressed ? PLATE_SINK - 0.5f : 3.5f;
    Rgbf top = Mixf(base, white, 0.22f), mid = base, low = Mixf(base, black, 0.16f), deep = Mixf(base, black, 0.30f);
    Rgbf faceStops[3] = {Mixf(base, white, 0.88f), Mixf(base, white, 0.84f), Mixf(base, white, 0.72f)};
    static const float faceTs[3] = {0, 0.6f, 1};
    static const float fadeTs[4] = {0, 0.55f, 0.8f, 1};
    static const Rgbf fadeCs[4] = {{255, 255, 255}, {90, 90, 90}, {40, 40, 40}, {170, 170, 170}};

    if (focus)
    {
        top = Mixf(top, white, 0.15f);
        mid = Mixf(base, white, 0.12f);
    }
    if (pressed)
    {
        top = mid;
        mid = low;
        low = deep;
    }
    for (int j = 0; j < ph; ++j)
    {
        float v = j - PLATE_MT + 0.5f, tRow = (v - oy) / hh;
        float creamEdge = s->cream + (oy + hh * 0.5f - v) * slope;
        float bandEdge0 = oy + hh * 0.64f, bandSlope = -hh * 0.06f / w;
        float gloss = 0.16f * (1.0f - Clamp01((v - oy) / (hh * 0.5f)));
        float fade = Ramp(fadeTs, fadeCs, 4, tRow).r / 255.0f;
        bool8 scan = ((j & 1) != 0) == (oddRow != 0);
        static const float gradTs[3] = {0, 0.45f, 1};
        Rgbf grad[3] = {top, mid, low};
        Rgbf face = Ramp(gradTs, grad, 3, tRow);
        Rgbf cream = tRow <= 0.6f ? creamTop : Mixf(creamTop, creamLow, Clamp01((tRow - 0.6f) / 0.4f));
        Rgbf headFace = s->head ? Ramp(faceTs, faceStops, 3, (v - (oy + s->head)) / (hh - s->head)) : black;
        float headCover = s->head ? Clamp01(v + 0.5f - (oy + s->head)) : 0.0f;
        float sep = s->head ? Clamp01((v + 0.5f < oy + s->head + 0.6f ? v + 0.5f : oy + s->head + 0.6f)
                                      - (v - 0.5f > oy + s->head - 0.6f ? v - 0.5f : oy + s->head - 0.6f)) : 0.0f;

        for (int i = 0; i < pw; ++i)
        {
            float u = i - PLATE_ML + 0.5f;
            float d = ChamferDist(u, v, 0, oy, w, hh, c);
            float covI = Cover(d + 1.2f);
            Px p = {1.0f, 0, 0, 0};

            Paint(&p, shadowColour, 0.55f * Blurred(ChamferDist(u, v, 0, shadowY, w, h - (pressed ? PLATE_SINK : 0), c), 1.23f));
            if (focus)
            {
                float dr = ChamferDist(u, v, -2.5f, oy - 2.5f, w + 5, hh + 5, c + 1);

                Paint(&p, ring, 0.9f * Blurred(dr, 1.62f));
                Paint(&p, ring, Cover(dr));
            }
            if (d < 1.0f)
            {
                Paint(&p, ink, Cover(d));
                if (covI > 0.0f)
                {
                    float creamCover = s->cream ? Clamp01(creamEdge - u + 0.5f) * covI : 0.0f;

                    Paint(&p, face, covI);
                    Paint(&p, deep, 0.45f * covI * Clamp01(v + 0.5f - (bandEdge0 + bandSlope * u)));
                    if (s->cream)
                    {
                        static const float stripes[3][3] = {{1.0f, 2.0f, 0.85f}, {5, 9, 0.22f}, {17, 4, 0.14f}};

                        Paint(&p, cream, creamCover);
                        for (int k = 0; k < 3; ++k)
                        {
                            float a = creamEdge + stripes[k][0], b = a + stripes[k][1];
                            float cov = Clamp01(u + 0.5f - a) - Clamp01(u + 0.5f - b);

                            Paint(&p, white, stripes[k][2] * cov * covI);
                        }
                        Paint(&p, creamBall, 0.85f * creamCover * FaintBall(u - 13.0f, v - (oy + 16.0f), 12.0f));
                    }
                    if (s->head)
                    {
                        Paint(&p, headFace, headCover * covI);
                        Paint(&p, ink, 0.9f * sep * covI);
                        if (v < oy + s->head + 0.5f)
                        {
                            static const float stripes[2][3] = {{0, 10, 0.20f}, {14, 3, 0.16f}};
                            float rows = Clamp01(oy + s->head - (v - 0.5f)) * Clamp01(v + 0.5f - oy);

                            for (int k = 0; k < 2; ++k)
                            {
                                float a = w - 46 + stripes[k][0] - (v - oy) * 0.5f, b = a + stripes[k][1];
                                float cov = Clamp01(u + 0.5f - a) - Clamp01(u + 0.5f - b);

                                Paint(&p, white, stripes[k][2] * cov * rows * covI);
                            }
                        }
                    }
                    Paint(&p, white, gloss * covI);
                    {
                        float rim = 0.875f - fabsf(d + 1.9f);

                        if (rim > 0.75f) rim = 0.75f;
                        if (rim > 0.0f)
                            Paint(&p, white, 0.75f * rim * fade);
                    }
                    if (scan)
                        Paint(&p, black, 0.045f * covI);
                }
            }
            {
                /* Each layer lerps towards its colour: what it adds is premultiplied already. */
                int t = (int)(p.t * 255.0f + 0.5f), r = (int)(p.r + 0.5f), g = (int)(p.g + 0.5f), b = (int)(p.b + 0.5f);

                outT[i * ph + j] = (u8)(t > 255 ? 255 : t);
                outC[i * ph + j] = (r > 255 ? 31 : (r * 31 + 127) / 255) << 11 | (g > 255 ? 63 : (g * 63 + 127) / 255) << 5
                                 | (b > 255 ? 31 : (b * 31 + 127) / 255);
            }
        }
    }
}

/*
 * The plates made so far, the least recently drawn given up first once they
 * take more than PLATE_BUDGET bytes. A screen's few dozen plates are a few
 * hundred KiB; the big ones (a list's panel) are drawn into the screen's own
 * backdrop cache and seldom again.
 */
#define PLATE_PICS 64
#define PLATE_BUDGET (1536u * 1024u)
typedef struct
{
    PlateSpec spec;
    bool8 odd, valid;
    u16 pw, ph;
    u32 used;
    u8 *t;
    u16 *c;
} PlatePic;
static PlatePic sPlates[PLATE_PICS];
static u32 sPlateClock;
static size_t sPlateBytes;
/* Plates made, for the slow-redraw log. */
static u32 sPlatesMade;

static void FreePlate(PlatePic *p)
{
    if (!p->valid)
        return;
    sPlateBytes -= (size_t)p->pw * p->ph * 3;
    free(p->t);
    free(p->c);
    p->t = NULL;
    p->c = NULL;
    p->valid = FALSE;
}

static const PlatePic *PlatePicture(const PlateSpec *s, bool8 odd)
{
    PlatePic *slot = NULL;
    int pw = s->w + PLATE_ML + PLATE_MR, ph = s->h + PLATE_MT + PLATE_MB;
    size_t bytes = (size_t)pw * ph * 3;

    for (int i = 0; i < PLATE_PICS; ++i)
        if (sPlates[i].valid && sPlates[i].odd == odd && memcmp(&sPlates[i].spec, s, sizeof(*s)) == 0)
        {
            sPlates[i].used = ++sPlateClock;
            return &sPlates[i];
        }
    /* Room: a free slot, then the oldest ones while over budget. */
    for (;;)
    {
        PlatePic *oldest = NULL;

        slot = NULL;
        for (int i = 0; i < PLATE_PICS; ++i)
        {
            if (!sPlates[i].valid && !slot)
                slot = &sPlates[i];
            if (sPlates[i].valid && (!oldest || sPlates[i].used < oldest->used))
                oldest = &sPlates[i];
        }
        if (slot && sPlateBytes + bytes <= PLATE_BUDGET)
            break;
        if (!oldest)
            break;
        FreePlate(oldest);
    }
    if (!slot)
        return NULL;
    slot->t = malloc((size_t)pw * ph);
    slot->c = malloc((size_t)pw * ph * sizeof(u16));
    if (!slot->t || !slot->c)
    {
        free(slot->t);
        free(slot->c);
        slot->t = NULL;
        slot->c = NULL;
        return NULL;
    }
    RenderPlate(s, slot->t, slot->c, odd);
    slot->spec = *s;
    slot->odd = odd;
    slot->pw = pw;
    slot->ph = ph;
    slot->used = ++sPlateClock;
    slot->valid = TRUE;
    sPlateBytes += bytes;
    ++sPlatesMade;
    return slot;
}

/* x * t / 255 for one RGB565 pixel, t 0..255. */
static inline u16 Scale565(u16 p, int t)
{
    int r = p >> 11, g = (p >> 5) & 63, b = p & 31;

    return (u16)(Div255(r * t) << 11 | Div255(g * t) << 5 | Div255(b * t));
}

/* Two RGB565 pixels added, each channel saturating. */
static inline u16 Add565(u16 a, u16 b)
{
    int r = (a >> 11) + (b >> 11), g = ((a >> 5) & 63) + ((b >> 5) & 63), bl = (a & 31) + (b & 31);

    return (u16)((r > 31 ? 31 : r) << 11 | (g > 63 ? 63 : g) << 5 | (bl > 31 ? 31 : bl));
}

/* A plate with its top-left at (x, y); returns the top of its face (lower when pressed). */
static int DrawPlateSpec(int x, int y, const PlateSpec *s)
{
    int x0 = x - PLATE_ML + sOX, y0 = y - PLATE_MT;
    const PlatePic *p;
    int i0, i1, j0, j1;

    if (x0 >= sClipX1 || y0 >= sClipY1 || x0 + s->w + PLATE_ML + PLATE_MR <= sClipX0
     || y0 + s->h + PLATE_MT + PLATE_MB <= sClipY0)
        return y + (s->state == PLATE_STATE_PRESSED ? PLATE_SINK : 0);
    p = PlatePicture(s, (y0 & 1) != 0);
    if (p)
    {
        i0 = sClipX0 - x0 > 0 ? sClipX0 - x0 : 0;
        j0 = sClipY0 - y0 > 0 ? sClipY0 - y0 : 0;
        i1 = sClipX1 - x0 < p->pw ? sClipX1 - x0 : p->pw;
        j1 = sClipY1 - y0 < p->ph ? sClipY1 - y0 : p->ph;
        for (int i = i0; i < i1; ++i)
        {
            u16 *dst = sDst + (x0 + i) * H + (H - 1 - (y0 + j0));
            const u8 *t = p->t + i * p->ph + j0;
            const u16 *c = p->c + i * p->ph + j0;

            for (int j = j0; j < j1; ++j, --dst, ++t, ++c)
            {
                if (*t == 255)
                {
                    if (*c)
                        *dst = Add565(*dst, *c);
                }
                else if (*t == 0)
                    *dst = *c;
                else
                    *dst = Add565(Scale565(*dst, *t), *c);
            }
        }
    }
    else
    {
        /* No memory for its picture: a flat face, so the screen still works. */
        u16 face = PackRgb(s->base.r, s->base.g, s->base.b);

        FillRect(x + 1, y, s->w - 2, s->h, face);
        FillRect(x, y + 1, s->w, s->h - 2, face);
    }
    return y + (s->state == PLATE_STATE_PRESSED ? PLATE_SINK : 0);
}

/* The mockups' plate: corners cut by a fifth of the height (3 to 5), a cream
 * end `cream` wide slanted `slope` hundredths. */
static int PlateCut(int h)
{
    return h / 5 < 3 ? 3 : h / 5 > 5 ? 5 : h / 5;
}

static int PlateEx(int x, int y, int w, int h, Rgb base, u8 state, int cream, int slope, int cut, int head)
{
    PlateSpec s;

    memset(&s, 0, sizeof(s));
    s.w = w;
    s.h = h;
    s.cream = cream;
    s.slope = slope;
    s.cut = cut;
    s.head = head;
    s.state = state;
    s.base = base;
    return DrawPlateSpec(x, y, &s);
}

static int Plate(int x, int y, int w, int h, Rgb base, u8 state)
{
    return PlateEx(x, y, w, h, base, state, 0, 30, PlateCut(h), 0);
}

/* The plate's room on screen, ring and shadow included: for partial redraws. */
static void PlateBounds(int x, int y, int w, int h, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = x - PLATE_ML;
    *y0 = y - PLATE_MT;
    *x1 = x + w + PLATE_MR;
    *y1 = y + h + PLATE_MB;
}

/* ------------------------------------------------------------------------ */
/* Text styles                                                              */
/* ------------------------------------------------------------------------ */

/*
 * The mockups' four: white with an ink outline (on coloured plates), ink
 * with a warm shadow (on cream and light ones), green for values, grey for
 * labels. A string too wide for its room takes the game's narrow cut of its
 * font (FitFont); a label that still does not fit is cut at its room's edge.
 */
enum { STYLE_WHITE, STYLE_INK, STYLE_GREEN, STYLE_GRAY, STYLE_RED, STYLE_SOFT, STYLE_MALE, STYLE_FEMALE, STYLE_COUNT };
typedef struct { u16 fg, shadow; bool8 outlined; } TextStyle;
static TextStyle sStyle[STYLE_COUNT];

static void InitStyles(void)
{
    u16 ink = PackRgb(22, 28, 38);

    sStyle[STYLE_WHITE] = (TextStyle){PackRgb(255, 255, 255), ink, TRUE};
    sStyle[STYLE_INK] = (TextStyle){PackRgb(56, 56, 64), PackRgb(208, 200, 188), FALSE};
    sStyle[STYLE_GREEN] = (TextStyle){PackRgb(40, 128, 80), PackRgb(200, 232, 208), FALSE};
    sStyle[STYLE_GRAY] = (TextStyle){PackRgb(132, 128, 124), PackRgb(228, 220, 206), FALSE};
    sStyle[STYLE_RED] = (TextStyle){PackRgb(208, 48, 40), PackRgb(248, 184, 168), FALSE};
    /* White on the move plates' light faces' neighbours: a pale blue, outlined. */
    sStyle[STYLE_SOFT] = (TextStyle){PackRgb(208, 224, 240), ink, TRUE};
    /* The gender signs, the game's blue and red. */
    sStyle[STYLE_MALE] = (TextStyle){PackRgb(64, 136, 248), PackRgb(200, 216, 248), FALSE};
    sStyle[STYLE_FEMALE] = (TextStyle){PackRgb(248, 96, 96), PackRgb(248, 200, 200), FALSE};
    BuildPhi();
}

static bool8 IsSmall(const Font *font)
{
    return font->height < 16;
}

/* The narrow cut of a font, when the string is wider than `avail`. */
static const Font *FitFont(const Font *font, const u8 *str, int avail)
{
    const Font *narrow = IsSmall(font) ? &sSmallNarrow : &sNarrow;

    if (font == &sSmallNarrow || font == &sNarrow || StrWidth(font, str) <= avail || !narrow->glyphs)
        return font;
    return narrow;
}

/* Floor division, as the mockups' layout divides. */
static int FloorDiv(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

/* The row to draw one line at, centred on a plate [py, py + ph) by its capitals. */
static int TextY(int py, int ph, const Font *font)
{
    int k = IsSmall(font) ? FloorDiv(ph - 8, 2) - 4 : FloorDiv(ph - 10, 2) - 3;

    return py + (k > 1 ? k : 1);
}

/* One pass over a string's glyph pixels: its ink outlined (all eight
 * neighbours), or its ink and, unless outlined, its shadow. */
static int StyledPass(const Font *font, const u8 *str, int x, int y, u16 colour, u16 shadow, int pass)
{
    int left = x;

    for (u16 g; (g = NextGlyph(&str)) != 0xFFFF;)
    {
        const u16 *base;
        int width;

        if (g == 0xFFFE)
        {
            x = left;
            y += font->lineHeight;
            continue;
        }
        base = font->glyphs + g * 0x20;
        width = font->widths[g] > 16 ? 16 : font->widths[g];
        for (int row = 0; row < font->height; ++row)
            for (int px = 0; px < width; ++px)
            {
                u16 bits = base[(row >= 8 ? 0x10 : 0) + (px / 8) * 8 + (row & 7)];
                u8 byte = (px & 7) < 4 ? bits >> 8 : bits & 0xFF;
                u8 v = (byte >> (6 - 2 * (px & 3))) & 3;

                if (pass == 0 && v == 1)
                {
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                            Put(x + px + dx, y + row + dy, colour);
                }
                else if (pass == 1 && v == 1)
                    Put(x + px, y + row, colour);
                else if (pass == 2 && v == 2)
                    Put(x + px, y + row, shadow);
            }
        x += font->widths[g];
    }
    return x;
}

/* A string in a style; returns where it ended. */
static int StyledStr(const Font *font, const u8 *str, int x, int y, u8 style)
{
    const TextStyle *st = &sStyle[style];

    if (!font->glyphs || !str)
        return x;
    if (st->outlined)
        StyledPass(font, str, x, y, st->shadow, 0, 0);
    else
        StyledPass(font, str, x, y, 0, st->shadow, 2);
    return StyledPass(font, str, x, y, st->fg, 0, 1);
}

static int StyledRight(const Font *font, const u8 *str, int right, int y, u8 style)
{
    return StyledStr(font, str, right - StrWidth(font, str), y, style);
}

static int StyledCentre(const Font *font, const u8 *str, int cx, int y, u8 style)
{
    return StyledStr(font, str, cx - StrWidth(font, str) / 2, y, style);
}

/* The mockups' tin(): one line on a plate [x, x + w) x [y, y + h), `pad` in
 * from its sides, aligned 'l', 'c' or 'r', in the narrow cut if it must. */
static int TextIn(const u8 *str, int x, int y, int w, int h, const Font *font, char align, u8 style, int pad)
{
    const Font *f = FitFont(font, str, w - 2 * pad);
    int ty = TextY(y, h, f);

    if (align == 'c')
        return StyledCentre(f, str, x + w / 2, ty, style);
    if (align == 'r')
        return StyledRight(f, str, x + w - pad, ty, style);
    return StyledStr(f, str, x + pad, ty, style);
}

/*
 * A text over up to `max` lines of [x, x + w), `lineH` apart: its own line
 * breaks kept, and a line too wide even in the narrow cut wrapped at its
 * last space that fits. Draws only with `draw`; returns the lines.
 */
static int TextLines(const u8 *str, int x, int y, int w, int lineH, int max, const Font *font, u8 style, bool8 draw)
{
    const Font *narrow = IsSmall(font) ? &sSmallNarrow : &sNarrow;
    u8 line[256];
    int lines = 0;

    if (!narrow->glyphs)
        narrow = font;
    while (str && *str != EOS && lines < max)
    {
        const u8 *p = str, *end = str, *next = str, *space = NULL;
        int thin = 0, n;

        for (;;)
        {
            const u8 *at = p;
            u16 g = NextGlyph(&p);

            if (g == 0xFFFF || g == 0xFFFE)
            {
                end = at;
                next = p;
                break;
            }
            if (g == CHAR_SPACE)
                space = at;
            thin += narrow->widths[g];
            if (thin > w && space)
            {
                end = space;
                next = space + 1;
                break;
            }
        }
        n = (int)(end - str);
        if (n > (int)sizeof(line) - 1)
            n = sizeof(line) - 1;
        memcpy(line, str, n);
        line[n] = EOS;
        if (draw)
            StyledStr(FitFont(font, line, w), line, x, y + lines * lineH, style);
        ++lines;
        str = next;
    }
    return lines;
}
