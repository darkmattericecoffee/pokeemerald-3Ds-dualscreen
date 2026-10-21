#ifndef CTR_STEREO_STRIP_H
#define CTR_STEREO_STRIP_H

/*
 * The voting view's top screen (3ds_stereo_strip.c): a Pokémon's server
 * versions side by side as a filmstrip, in 3D. Each column is one upload:
 * a front version above a back version (either half may be empty), each a
 * stack of its own planes at the battle's size (64 x CTR_BATTLE_ZOOM),
 * three columns across the 400 pixels, scrolled smoothly.
 *
 * The editor (3ds_stereo_edit.c) owns the columns and what they say; this
 * side fetches nothing itself but each version's planes from the online
 * cache (CtrOnline_FileCopy) and composes them into the viewer's plane
 * texture, lent to it while the view is open (the editor's picture is not
 * on the top screen then). The texture holds CtrStereoStrip_Slots()
 * columns at once (4, or fewer after a battle took the big one): column c
 * goes in slot c % slots.
 *
 * The words are the editor's, drawn with the bottom screen's font and
 * handed over as cells of a label sheet: the fixed ones, then three per
 * slot (the author over the column, the front's line, the back's).
 *
 * SDK-free: game translation units include it.
 */
#include <stdbool.h>
#include <stdint.h>
#include "3ds_stereo_mon.h"

#define CTR_STRIP_VISIBLE 3
#define CTR_STRIP_MAX_COLUMNS 32
#define CTR_STRIP_SLOTS_MAX 4

typedef struct
{
    uint32_t version; /* 0: this half is empty */
    bool voted, final, mine, web;
} CtrStripCell;

/* Open lends the viewer's texture to the strip (front and back are the
 * pictures' ids; back 0 for a trainer's one picture); Close gives it back. */
void CtrStereoStrip_Open(uint16_t front, uint16_t back);
void CtrStereoStrip_Close(void);
bool CtrStereoStrip_IsOpen(void);
unsigned CtrStereoStrip_Slots(void);
/* The columns: how many, each one's halves (side 0 the front). */
void CtrStereoStrip_SetCount(unsigned count);
void CtrStereoStrip_SetCell(unsigned column, unsigned side, const CtrStripCell *cell);
/* The first column shown, in columns (2.5: half of the third on the left
 * edge); the cursor's cell (it gets a frame). */
void CtrStereoStrip_SetScroll(float first);
void CtrStereoStrip_SetCursor(unsigned column, unsigned side, bool shown);

/* The label sheet: 128x14 cells, one byte a pixel (0 clear, else ink). */
#define CTR_STRIP_LABEL_W 128
#define CTR_STRIP_LABEL_H 14
enum
{
    CTR_STRIP_TITLE,   /* the Pokémon, top left */
    CTR_STRIP_PAGE,    /* which columns of how many, top right */
    CTR_STRIP_FINAL,   /* "FINAL" */
    CTR_STRIP_WEB,     /* "WEB": a version the web app picked */
    CTR_STRIP_EMPTY,   /* a half nobody sent */
    CTR_STRIP_LOADING, /* "LOADING" */
    CTR_STRIP_FIXED,
    /* Then per slot: the author, the front's line, the back's. */
};
#define CTR_STRIP_LABELS (CTR_STRIP_FIXED + 3 * CTR_STRIP_SLOTS_MAX)
#define CTR_STRIP_SLOT_LABEL(slot, k) (CTR_STRIP_FIXED + 3 * (slot) + (k))
/* A cell's ink; `column` is the column a slot's cell speaks of (ignored
 * for the fixed ones): it is shown only while that column is in the slot. */
void CtrStereoStrip_Label(unsigned cell, const uint8_t *alpha, unsigned width, unsigned column);

/* ── The native side (3ds_stereo_mon.c calls these) ──────────────────── */

/* Per frame before drawing: the next stacks into the texture. */
void CtrStereoStrip_Compose(void);
/* Whether Compose has work waiting (a column shown but not composed, a
 * file or a picture come since): the viewer draws a frame for it then. */
bool CtrStereoStrip_Pending(void);
/* One eye of the top screen (sign +1 the left eye, slider 0..1). */
void CtrStereoStrip_DrawEye(float sign, float slider);
/* Moves on with anything that changes what the strip shows. */
unsigned CtrStereoStrip_Version(void);
/* BIG: the cursor's column alone, its front and back side by side at 2x
 * (as the Pokédex shows one's own), instead of the three-column strip. */
void CtrStereoStrip_SetBig(bool big);

/* What 3ds_stereo_mon.c lends: its plane texture (a C3D_Tex, its width;
 * NULL when there is none), lent while `lend`; the pictures (frame 0,
 * RGBA5551) of the picture on show and of its other side, NULL for
 * others or while not decoded; a picture's planes file header. */
void *CtrStereoMon_LendTexture(bool lend, unsigned *width, unsigned *height);
const uint16_t *CtrStereoMon_SidePicture(uint16_t id);
/* The top screen drawn again on the next frame (a held one is not). */
void CtrStereoMon_Invalidate(void);

#endif
