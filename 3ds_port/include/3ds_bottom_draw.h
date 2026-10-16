#ifndef CTR_BOTTOM_DRAW_H
#define CTR_BOTTOM_DRAW_H

/*
 * The bottom screen's drawing, for a port screen of its own (the plane
 * editor, 3ds_stereo_edit.c): the game's fonts and box frames and the
 * port's plates and sections, drawn into a canvas the caller owns (320x240
 * RGB565 in the framebuffer's layout, 3ds_bottom.h) by 3ds_bottom_ui.c's
 * own routines. Between Begin and End every call draws into that canvas;
 * the caller presents it with CtrBottom_Blit.
 *
 * SDK-free: game translation units include it.
 */
#include <stdbool.h>
#include <stdint.h>

/* False until the bottom screen's resources are loaded (before the field). */
bool CtrBottomDraw_Ready(void);
void CtrBottomDraw_Begin(uint16_t *canvas);
void CtrBottomDraw_End(void);
/* The regular bottom screen draws itself whole on its next frame. */
void CtrBottom_ForceRedraw(void);
/* online_test.txt's aid: the bottom screen's canvas to a file (RGB565,
 * 320 columns of 240, each from the bottom up). */
void CtrBottom_TestShot(const char *path);
/* online_test.txt's aid: the OPTIONS screen on a tab (CTR_EXTRAS_*), even
 * from the title screen; page + 10 * n is its page n + 1. */
void CtrBottom_TestOptions(unsigned page);

uint16_t CtrBottomDraw_Rgb565(uint16_t bgr555);
void CtrBottomDraw_Pixel(int x, int y, uint16_t rgb565);
void CtrBottomDraw_Fill(int x, int y, int w, int h, uint16_t rgb565);
/* The light green panel with its frame lines (and its faint Poké Ball),
 * or the same without the ball. */
void CtrBottomDraw_Section(int x0, int y0, int x1, int y1);
void CtrBottomDraw_Panel(int x0, int y0, int x1, int y1);
/* The battle text box frames, in tiles: 0 the white menu box, 1 the teal
 * message box; dark for a pressed or chosen one. */
void CtrBottomDraw_Box(int kind, int x, int y, int wt, int ht, bool dark);
/* A button plate: 0 plain, 1 chosen (green), 2 off (grey). */
void CtrBottomDraw_Plate(int x, int y, int w, int h, int look, bool pressed);

/* Text in the game's font (0 small, 13px; 1 normal, 16px) from ASCII, in
 * a colour pair: 0 dark on light, 1 white on green, 2 red, 3 off (grey),
 * 4 blue. */
enum { CTR_DRAW_FONT_SMALL, CTR_DRAW_FONT_NORMAL };
enum { CTR_DRAW_DARK, CTR_DRAW_WHITE, CTR_DRAW_RED, CTR_DRAW_OFF, CTR_DRAW_BLUE };
void CtrBottomDraw_Text(int font, const char *ascii, int x, int y, int colour);
void CtrBottomDraw_TextCentred(int font, const char *ascii, int cx, int y, int colour);
int CtrBottomDraw_TextWidth(int font, const char *ascii);
/* A Pokémon's 32x32 icon (the party menu's) at (x, y) by national dex
 * number. One icon is read from RomFS per Begin: false when this one is
 * not in yet (draw again next frame). */
bool CtrBottomDraw_MonIcon(uint16_t national, int x, int y);
/* The game's own little pictures the bottom screen's column uses (the
 * Pokédex's, the Poké Ball's), their visible pixels centred on (cx, cy). */
enum { CTR_DRAW_PIC_DEX, CTR_DRAW_PIC_BALL };
void CtrBottomDraw_GamePic(int pic, int cx, int cy);

#endif
