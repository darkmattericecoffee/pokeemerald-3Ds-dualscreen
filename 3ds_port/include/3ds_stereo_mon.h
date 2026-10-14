#ifndef CTR_STEREO_MON_H
#define CTR_STEREO_MON_H

/*
 * The stereoscopic Pokémon viewer: a Pokémon's front picture as a stack of
 * flat depth planes, drawn on the top screen with the 3D slider's parallax
 * in the Pokédex's info page and in a gallery of every
 * Pokémon that has planes (L+R+START in the field).
 *
 * The planes come from the Sprite Stereo Lab (scripts/gen_stereo_mons.py)
 * as romfs:/stereo/mons.bin, a port asset: per Pokémon, for each of the two
 * frames of its animated front picture, one plane number per pixel. The
 * picture itself is the game's own, decoded at run time on the game side
 * (3ds_stereo_mon_game.c); the native side (3ds_stereo_mon.c) composes the
 * planes into one texture and draws them with Citro2D. The decoding of the
 * file and the composition are pure (3ds_stereo_mon_decode.c) and host-tested.
 *
 * SDK-free: game translation units include this header.
 */

#include <stdbool.h>
#include <stdint.h>

#define CTR_STEREO_MON_FILE "stereo/mons.bin"
/* The battle's other pictures (scripts/gen_stereo_backs.py): Pokémon backs
 * (0x8000 | national), trainers' fronts (0x4000 | trainer pic), the
 * players' backs (0xC000 | back pic), each entry with frames of its own. */
#define CTR_STEREO_BACKS_FILE "stereo/backs.bin"
#define CTR_STEREO_ID_BACK 0x8000
#define CTR_STEREO_ID_TRAINER 0x4000
#define CTR_STEREO_ID_PLAYER 0xC000
#define CTR_STEREO_MON_MAGIC "EM3DSTM1"
#define CTR_STEREO_MON_DIM 64
/* Frames the viewer's picture buffers hold (the players' backs have five). */
#define CTR_STEREO_MON_MAX_FRAMES 5
#define CTR_STEREO_MON_FILE_MAX_FRAMES 5
#define CTR_STEREO_MON_MAX_PLANES 15
/* The nibble of a transparent pixel. */
#define CTR_STEREO_MON_NONE 15
#define CTR_STEREO_MON_NAME 16
/* Index entries a file may have, kept resident: the national dex, or the
 * backs file's Pokémon, trainers and players. */
#define CTR_STEREO_MON_MAX_COUNT 520
#define CTR_STEREO_MON_HEADER_BYTES 16
#define CTR_STEREO_MON_INDEX_BYTES 8

typedef struct
{
    uint16_t count;
    uint8_t planes, frames;
    uint16_t width, height;
    /* frames * width * height / 2: one entry's bytes. */
    uint32_t entryBytes;
} CtrStereoMonHeader;

/* ── Pure: the file and the composition (3ds_stereo_mon_decode.c) ─────── */

/* False for anything but a well-formed file of 64x64 planes. */
bool CtrStereoMon_ParseHeader(const uint8_t *file, uint32_t size, CtrStereoMonHeader *out);
/* Index entry `index`: its national dex number (or id) and where its planes
 * start. Only the header and the index need to be in `file` (its first
 * 16 + 8 * count bytes). */
bool CtrStereoMon_IndexEntry(const uint8_t *file, uint32_t size, const CtrStereoMonHeader *header,
                             unsigned index, uint16_t *national, uint32_t *offset);
/* The entry's own number of frames (the index's reserved word, when set),
 * else the file's; and the bytes of planes that many frames take. */
unsigned CtrStereoMon_EntryFrames(const uint8_t *file, uint32_t size, const CtrStereoMonHeader *header,
                                  unsigned index);
uint32_t CtrStereoMon_FramesBytes(const CtrStereoMonHeader *header, unsigned frames);
/* The same with the planes themselves, which lie entirely inside `file`. */
bool CtrStereoMon_Entry(const uint8_t *file, uint32_t size, const CtrStereoMonHeader *header,
                        unsigned index, uint16_t *national, const uint8_t **planes);
/* The index of a national dex number's entry, or -1. */
int CtrStereoMon_Find(const uint8_t *file, uint32_t size, const CtrStereoMonHeader *header,
                      uint16_t national);
/* The plane of one pixel: 0 the farthest .. planes-1 the nearest, or
 * CTR_STEREO_MON_NONE. Frames the entry does not have repeat its last one. */
unsigned CtrStereoMon_Plane(const uint8_t *planes, const CtrStereoMonHeader *header,
                            unsigned frame, unsigned x, unsigned y);
/*
 * The file's planes merged into `groups` (2..planes): plane q belongs to
 * group q * groups / planes, so fewer groups mean fewer, thicker slices and
 * fewer tiles to keep.
 */
unsigned CtrStereoMon_Group(unsigned plane, unsigned planes, unsigned groups);
/*
 * One group of one frame as a 64x64 tile of a PICA texture `texWidth` texels
 * wide (Morton order, CtrVideo_Texel), at tile (tileX, tileY): the picture's
 * RGBA5551 pixels (rows top to bottom, 0 = transparent) where the group is,
 * transparent everywhere else - except a skirt of `skirt` pixels: pixels of
 * farther groups within that distance of the group's are on its tile too
 * (like the cardboard sides of the lab's planes), so that where the card
 * shifts away from the group behind it, the gap shows that group's own
 * colours rather than the scene through it. In a texture 64 texels wide a
 * tile is one contiguous run of 8 KiB, which is how the viewer stages it
 * into VRAM.
 */
void CtrStereoMon_ComposeTile(const uint16_t *picture, const uint8_t *planes,
                              const CtrStereoMonHeader *header, unsigned frame, unsigned groups,
                              unsigned group, uint16_t *texture, unsigned texWidth,
                              unsigned tileX, unsigned tileY, unsigned skirt);

/* ── Native: the viewer (3ds_stereo_mon.c) ────────────────────────────── */

/* Reads the planes file once. False when there is none: nothing to show. */
bool CtrStereoMon_Load(void);
unsigned CtrStereoMon_Count(void);
uint16_t CtrStereoMon_NationalAt(unsigned index);
bool CtrStereoMon_Has(uint16_t national);
/* The file's header once loaded (for CtrStereoMon_Plane and ComposeTile). */
const CtrStereoMonHeader *CtrStereoMon_Header(void);
/* One Pokémon's planes (header->entryBytes of them) into `out`, which holds
 * `size` bytes; false when the file has no such Pokémon or no room. */
bool CtrStereoMon_PlanesFor(uint16_t national, uint8_t *out, uint32_t size);
/* The same for the backs file (CTR_STEREO_BACKS_FILE), by id: its header,
 * whether it has the id, and the id's planes with their own number of
 * frames (`frames`, 1..CTR_STEREO_MON_FILE_MAX_FRAMES). */
const CtrStereoMonHeader *CtrStereoMon_BacksHeader(void);
bool CtrStereoMon_HasBack(uint16_t id);
bool CtrStereoMon_BackPlanesFor(uint16_t id, uint8_t *out, uint32_t size, unsigned *frames);
/* The SD card's edited planes of an id (CtrStereoMon_SaveEdit), when there
 * are any with the file's own number of planes: the battle shows them. */
bool CtrStereoMon_EditPlanesFor(uint16_t id, uint8_t *out, uint32_t size, unsigned *frames);
/* The gallery: every picture of both files, the fronts first (by national
 * number), then the backs, the trainers and the players. */
unsigned CtrStereoMon_GalleryCount(void);
uint16_t CtrStereoMon_GalleryIdAt(unsigned index);
int CtrStereoMon_GalleryIndexOf(uint16_t id);
/* A front's back and a back's front, when both files have them; else 0. */
uint16_t CtrStereoMon_Counterpart(uint16_t id);
/*
 * Show a Pokémon: its national dex number, its front picture (`frames` of
 * 64x64 RGBA5551 pixels, rows top to bottom, 0 = transparent, copied here),
 * its name in ASCII, and whether the gallery's controls are shown.
 */
bool CtrStereoMon_Show(uint16_t national, const uint16_t *picture, unsigned frames,
                       const char *name, bool gallery);
/* The same for any id of either file (a back, a trainer, a player). */
bool CtrStereoMon_ShowId(uint16_t id, const uint16_t *picture, unsigned frames, const char *name, bool gallery);
void CtrStereoMon_Hide(void);
/* Whether the top screen is the viewer's this frame. */
bool CtrStereoMon_Active(void);
/* The viewer kept (its planes editable) but not drawn: the editor's battle
 * has the top screen. */
void CtrStereoMon_SetDrawn(bool drawn);
/* Whether it is the Pokédex's picture (else the gallery). */
bool CtrStereoMon_Hologram(void);
/* The gallery's own keys, pressed this frame (CTR_KEY_* of 3ds_input.h):
 * UP/DOWN the depth, L/R fewer or more planes, X the animation. */
void CtrStereoMon_Press(unsigned keys);
/*
 * Draws into the left eye's target, and the right eye's when it is given,
 * with the 3D slider at `slider` (0..1). Targets are C3D_RenderTarget
 * pointers; the caller sets up the 2D program and forgets its blend state.
 * Called inside the Citro3D frame, which is also where the textures are
 * filled (a few tiles per frame, within the compositor's upload budget) or
 * given back (Idle), after the GPU has finished with them.
 */
void CtrStereoMon_Draw(void *leftTarget, void *rightTarget, float slider);
/* Whether the next Draw would show anything different from the last (the
 * picture, its planes, the frame, the slider, the edit view, the words):
 * false, and the compositor can leave the top screen as it is. Counts the
 * frame's tick, so it is asked once a frame, before Draw. */
bool CtrStereoMon_NeedsFrame(float slider, bool stereo);
/*
 * The Pokédex's backdrop behind the Pokémon: a 256x128 picture (the species'
 * battle terrain, decoded on the game side) given row by row as RGBA5551
 * pixels, after Begin; shown once all 128 rows are in.
 */
#define CTR_STEREO_MON_BACKDROP_W 256
#define CTR_STEREO_MON_BACKDROP_H 128
void CtrStereoMon_BackdropBegin(void);
void CtrStereoMon_BackdropRow(unsigned y, const uint16_t *row);
void CtrStereoMon_Idle(void);

/* ── Native: the plane editor's side of the viewer (3ds_stereo_edit.c) ── */

/*
 * A Pokémon's planes can be edited on the device (3ds_stereo_edit.c, the
 * bottom screen): the viewer shows the planes it holds for the Pokémon on
 * show, and the editor changes them in place. Edited planes are kept on the
 * SD card, one file per Pokémon (CTR_STEREO_EDIT_DIR/NNN.bin: a 16-byte
 * header "EM3DSED1", u16 national, u8 planes, u8 frames, u16 width, u16
 * height, u32 reserved, then frames x 2048 bytes of nibbles as in the planes
 * file), and shown instead of the pack's whenever one exists; the PC-side
 * tools/sprite_editor/import_sd_edits.py folds them back into the pack.
 */
#define CTR_STEREO_EDIT_DIR "stereo"
#define CTR_STEREO_EDIT_MAGIC "EM3DSED1"
#define CTR_STEREO_EDIT_MAX_PLANES 14
/* The editor works on this many planes: a file with more is folded into
 * them, one with fewer spread over them, when it is opened. */
#define CTR_STEREO_EDIT_PLANES 4
/* The planes of the Pokémon on show (frames x 2048 bytes, writable), with
 * its number of planes and frames; NULL when nothing is shown. */
uint8_t *CtrStereoMon_EditPlanes(unsigned *planes, unsigned *frames);
/* Its picture: one frame of 64x64 RGBA5551 pixels (0 = transparent). */
const uint16_t *CtrStereoMon_Picture(unsigned frame);
uint16_t CtrStereoMon_Shown(void);
/* Its number of planes after an add or a merge (2..CTR_STEREO_EDIT_MAX_PLANES). */
void CtrStereoMon_SetPlaneCount(unsigned planes);
/* The planes changed: composed again over the next frames. */
void CtrStereoMon_Recompose(void);
/* The editor is open on the Pokémon: no turn, every plane drawn on its own,
 * the plane being edited named on the screen. */
void CtrStereoMon_SetEditing(bool editing, unsigned plane);
/* The planes' colours (RGB565), the farthest first: four clashing,
 * saturated hues, like a UV checker map's, so that neighbouring planes never
 * look alike (blue, yellow, cyan, red: the nearest red). */
/* Four hues a quarter turn apart, fully saturated, far to near: red,
 * yellow, cyan, magenta. Each stands out from the others and from any
 * Pokémon's own colours; a green and a blue read alike over a green one,
 * and two blues alike anywhere. */
#define CTR_STEREO_EDIT_COLOURS {0xF800, 0xFFE0, 0x07FF, 0xF81F}
/* What the top screen shows while editing: the frame being edited (or the
 * animation, every frame for as long), the plane being edited outlined in
 * `mark` (RGB565), and the view: the colours, every pixel tinted with its
 * plane's colour, or the picture in its place in a battle. */
enum { CTR_STEREO_VIEW_COLOURS, CTR_STEREO_VIEW_LAYERS, CTR_STEREO_VIEW_BATTLE };
void CtrStereoMon_SetEditView(unsigned frame, bool animate, bool outline, uint16_t mark, unsigned view, bool controls);
/* The other side of the picture being edited (a front's back, a back's
 * front) for the battle view: `frames` of 64x64 RGBA5551 pixels, copied
 * here, drawn flat in its own battle place; NULL for none. */
void CtrStereoMon_SetCompanion(const uint16_t *picture, unsigned frames);
/* The editor's 3D depth (percent, 0..CTR_STEREO_EDIT_DEPTH_MAX): its own
 * setting, capped at half the gallery's usual depth to spare the eyes over
 * a long session; the gallery and the Pokédex keep theirs. */
#define CTR_STEREO_EDIT_DEPTH_MAX 50
#define CTR_STEREO_EDIT_DEPTH_STEP 10
int CtrStereoMon_Depth(void);
void CtrStereoMon_SetDepth(int percent);
/*
 * The editor's words on the top screen in the game's own small font: the
 * editor renders each line with the bottom screen's font and hands it over
 * as a cell of the help sheet (CTR_HELP_CELLS cells of CTR_HELP_CELL_W x
 * CTR_HELP_CELL_H, one byte a pixel, 0 clear .. 255 ink); the viewer
 * draws the cells by number.
 */
#define CTR_HELP_CELL_W 128
#define CTR_HELP_CELL_H 14
#define CTR_HELP_CELLS 18
enum
{
    CTR_HELP_NAME, CTR_HELP_FRAME, CTR_HELP_PLANE, CTR_HELP_DEPTH, CTR_HELP_REVISION, CTR_HELP_STATE,
    CTR_HELP_L_PLANE, CTR_HELP_B_OUTLINE, CTR_HELP_L_PAN, CTR_HELP_L_ZOOM, CTR_HELP_L_UNDO,
    CTR_HELP_R_ANIMATE, CTR_HELP_R_LAYERS, CTR_HELP_R_FRAME, CTR_HELP_R_MON, CTR_HELP_R_SAVE, CTR_HELP_R_BACK,
    CTR_HELP_HOLD
};
void CtrStereoMon_HelpCell(unsigned cell, const uint8_t *alpha, unsigned width);
/*
 * Who edited a picture, and how far: an edit file ends with a 32-byte
 * trailer (older files have none): "EM3DSMD1", u8 flags (bit 0 finalized),
 * u8 0, u16 revision (one more on every save), u32 seconds since 2000-01-01
 * of the last save (0 unknown), char author[16] (ASCII, NUL-padded). The
 * author is the console's user name unless CTR_STEREO_EDIT_DIR/author.txt
 * names someone else (CtrStereoMon_SetAuthor writes it).
 */
#define CTR_STEREO_EDIT_AUTHOR 16
typedef struct
{
    bool finalized;
    uint16_t revision;
    uint32_t saved;
    char author[CTR_STEREO_EDIT_AUTHOR + 1];
} CtrStereoEditMeta;
/* The shown picture's: what its file says, or empty without one. */
const CtrStereoEditMeta *CtrStereoMon_EditMeta(void);
/* Marks the shown picture finished (or not); kept with its next save. */
void CtrStereoMon_SetFinalized(bool finalized);
/* Whether an id's edit file is marked finished (scanned with EditScan). */
bool CtrStereoMon_IsFinal(uint16_t id);
/* The name saves are signed with (ASCII capitals), and changing it. */
const char *CtrStereoMon_Author(void);
void CtrStereoMon_SetAuthor(const char *name);
/* The planes on show to the SD card (signed, its revision one more), or
 * that file removed and the pack's planes read back. */
bool CtrStereoMon_SaveEdit(void);
bool CtrStereoMon_ResetEdit(void);
/* Which Pokémon have edit files: scanned once (Scan) and then answered from
 * memory, for the chooser's marks. */
void CtrStereoMon_EditScan(void);
bool CtrStereoMon_HasEdit(uint16_t id);
/* Appends a line to the Pokémon's edit log (CTR_STEREO_EDIT_DIR/NNN.log). */
void CtrStereoMon_LogEdit(const char *line);

/* ── Native: the battle (3ds_stereo_battle.c) ─────────────────────────── */

/*
 * The battle's pictures drawn as stacks of planes instead of sprites: in
 * each of the four battler positions (B_POSITION_*: 0 and 2 the player's
 * side, 1 and 3 the opponent's) a Pokémon's front or back, a trainer's
 * front during the intro, or the player's back with its throw. The game
 * side names each position every frame of a battle (CtrStereoBattle_SetSlot):
 * the tile its sprite's picture is at in OBJ VRAM, the position's picture
 * buffer (the game's decoded front or back picture, or the trainer's front,
 * `bufferFrames` of 2048 bytes of 4bpp tiles), the ids of the planes the
 * buffer could be showing (the Pokémon's, the trainer's), and on the
 * player's side the raw sheet of the player's back (`backFrames` frames) with
 * its id; 0 for a slot with no sprite. The compositor finds the sprite in
 * OAM each frame, tells which picture and frame OBJ VRAM holds (so a
 * Substitute, a Transform or a ball's shrink fall back to the sprite),
 * composes the planes with the sprite's own palette as it stands in palette
 * RAM (its fades and tints come for free), leaves the sprite out of the
 * frame and draws the planes where it was.
 */
#define CTR_STEREO_SLOTS 4
#define CTR_STEREO_BATTLE_FRAME_BYTES 2048
#define CTR_STEREO_BUFFER_FRAMES 2
#define CTR_STEREO_BACK_FRAMES 5
void CtrStereoBattle_SetSlot(unsigned position, unsigned tileNum, const uint8_t *buffer, unsigned bufferFrames,
                             uint16_t monId, uint16_t trainerId, uint16_t playerId, const uint8_t *backSheet,
                             unsigned backFrames);
void CtrStereoBattle_ClearBattlers(void);
/* The planes read for the battle are forgotten: the next battle reads them
 * again (the editor saved new ones). */
void CtrStereoBattle_ForgetPlanes(void);
/* The editor's planes of `id` (frames x 2048 bytes, the editor's buffer,
 * read whenever LiveChanged says they changed) shown in the battle instead
 * of the file's, so a stroke shows there as it is made; NULL ends it. */
void CtrStereoBattle_SetLive(uint16_t id, const uint8_t *planes, unsigned frames);
void CtrStereoBattle_LiveChanged(void);
/* Whether the battle draws its Pokémon in 3D at all (OPTIONS' 3D BATTLE). */
bool CtrStereoBattle_Enabled(void);
/*
 * For the compositor (3ds_video.c), inside the Citro3D frame before anything
 * is drawn: finds this frame's Pokémon in OAM and composes their planes.
 * Sets the bit of each OAM entry drawn as planes in `skip` (128 bits, as
 * OAM is indexed); returns how many. `memory` is the CtrVideoMemory bound.
 */
unsigned CtrStereoBattle_Frame(const void *memory, bool battle, uint32_t skip[4]);
/* The sprite of OAM entry `oam` drawn as planes this frame: its GBA
 * position (the GBA's own wrapping) and its priority. */
bool CtrStereoBattle_Oam(unsigned oam, int *x, int *y, unsigned *priority);
/* Clears the bit of `oam` in what Frame found: drawn as a sprite after all. */
void CtrStereoBattle_Veto(unsigned oam, uint32_t skip[4]);
/*
 * Draws OAM entry `oam`'s planes with Citro2D into the current target and
 * view, the sprite's top-left corner at (x, y) in view units, `scale` view
 * units per sprite pixel. `eyeParallax` is the eye's slider (+ for the left
 * eye, - for the right, 0 flat) and `shiftScale` the view units per screen
 * pixel of displacement. `smooth` filters the planes bilinearly (for a draw
 * at a fractional scale; the texels are premultiplied, so the caller blends
 * ONE, ONE_MINUS_SRC_ALPHA), else nearest. The caller has set the blend
 * state it wants and forgets it afterwards.
 */
void CtrStereoBattle_Draw(unsigned oam, float x, float y, float scale, float eyeParallax, float shiftScale,
                          bool smooth);

/* ── Game side (3ds_stereo_mon_game.c) ────────────────────────────────── */

/* The wild Pokémon to meet instead of `species` when CHEATS' 3D WILD ONLY is
 * on: one that has planes (so the 3D battle can be tried anywhere); else
 * `species` itself. Called by CreateWildMon (patch 0042). */
uint16_t CtrStereoMonGame_WildSpecies(uint16_t species);

/* Once per frame, after input is scanned and before the game reads its
 * keys (next to CtrBottom_Frame). */
void CtrStereoMonGame_Frame(void);
/* Whether the gallery takes the player's keys from the game this frame. */
bool CtrStereoMonGame_TakesKeys(void);
/* Opens the gallery with the plane editor (OPTIONS' 3D SPRITE EDITOR). */
void CtrStereoMonGame_OpenEditor(void);
/* For the editor: the gallery's Pokémon by index, and leaving the gallery. */
unsigned CtrStereoMonGame_GalleryIndex(void);
void CtrStereoMonGame_SelectIndex(unsigned index);
void CtrStereoMonGame_CloseGallery(void);

/* ── Game side: the plane editor (3ds_stereo_edit.c) ──────────────────── */

bool CtrStereoEdit_Active(void);
/* Opens on the Pokémon the gallery shows (once the viewer has it); from
 * the gallery itself, B goes back to it, else B leaves both. */
void CtrStereoEdit_Open(bool fromGallery);
/* The editor's keys this frame (CTR_KEY_* down and held). */
void CtrStereoEdit_Keys(unsigned down, unsigned held);
/* The bottom screen's frame while the editor is open: its touches and its
 * drawing. False when the editor is not open (the bottom UI goes on). */
bool CtrStereoEdit_BottomFrame(void);

#endif
