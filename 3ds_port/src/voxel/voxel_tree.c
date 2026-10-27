#include <stddef.h>
#include "voxel_tree.h"
#include "voxel_relief.h"
#include "voxel_regions.h"

struct Tileset;
extern const struct Tileset gTileset_Dewford;
extern const struct Tileset gTileset_Fortree;
extern const struct Tileset gTileset_Mauville;
extern const struct Tileset gTileset_Lavaridge;
extern const struct Tileset gTileset_Rustboro;
extern const struct Tileset gTileset_Lilycove;
extern const struct Tileset gTileset_EverGrande;
extern const struct Tileset gTileset_BattleFrontierOutsideWest;

/* Where gen_voxel_trees.py packs each tree, in texels. */
#define TEX_U(px) ((float)(px) / VOXEL_TREE_TEXTURE_DIM)
#define TEX_V(py) (1.0f - (float)(py) / VOXEL_TREE_TEXTURE_DIM)

/* Ids from 0x200 are the secondary tileset's, the same number meaning a
 * different drawing on every map. */
#define SECONDARY_ID 0x200

/* Where a small trunk's ground is packed: the General one's at 48,32,
 * Dewford's at 80,0, and the trunks a secondary tileset draws on its own
 * ground (3: the General's own grass). -1 for any other metatile. */
static int SmallTrunkGround(const void *secondaryTileset, int metatileId)
{
    if (secondaryTileset == &gTileset_Fortree && metatileId == 0x253)
        return 0; /* tree_small_trunk_jungle.png at 48,48 */
    /* 22A: the General's 0C6 with the crown drawn on both layers, in the
     * woods round Fortree's tree houses */
    if (secondaryTileset == &gTileset_Fortree && metatileId == 0x22A)
        return 3;
    if (secondaryTileset == &gTileset_Mauville && metatileId == 0x226)
        return 1; /* tree_small_trunk_grass_road.png at 80,16 */
    /* 21A, 3B8 and 203 inside a wood, where the next crown overlaps the
     * trunk (the General's 0C6), on the soil and on the dark rock */
    if (secondaryTileset == &gTileset_Lavaridge
     && (metatileId == 0x218 || metatileId == 0x21A || metatileId == 0x3B8 || metatileId == 0x203))
        return 2; /* tree_small_trunk_volcan.png at 64,32 */
    return -1;
}

int VoxelTree_Part(const void *secondaryTileset, int metatileId)
{
    if (metatileId >= SECONDARY_ID)
    {
        /* Dewford's small tree: the trunk on the sand (23A), and inside the
         * wood (243), where the next crown overlaps it. */
        if (secondaryTileset == &gTileset_Dewford
         && (metatileId == 0x23A || metatileId == 0x243))
            return VOXEL_TREE_SMALL;
        /* The General's small tree drawn over another ground: Fortree's
         * jungle floor, Route 111's pale grass, Route 112's volcanic soil. */
        if (SmallTrunkGround(secondaryTileset, metatileId) >= 0)
            return VOXEL_TREE_SMALL;
        return -1;
    }
    switch (metatileId)
    {
    /* The secret base's tree: its upper row, and the lower closed (026-027)
     * or opened (036-037, set by secret_base.c). */
    case 0x01E: return 0;
    case 0x01F: return 1;
    case 0x026: case 0x036: return 2;
    case 0x027: case 0x037: return 3;
    case 0x1D4: case 0x1D6: return 0; /* upper left, including forest edge */
    case 0x1D5: case 0x1D7: return 1;
    case 0x1DC: case 0x1DE: case 0x1E4: case 0x1E6: return 2;
    case 0x1DD: case 0x1DF: case 0x1E5: case 0x1E7: return 3;
    /* A large tree's lower row under a small tree's canopy top. */
    case 0x1EC: return 2;
    case 0x1ED: return 3;
    /* A lower row under the canopy top of a tree one column to the side, as
     * in staggered woods: 1F3 is the left half, 1F2 the right. */
    case 0x1F3: return 2;
    case 0x1F2: return 3;
    /* An upper row in front of a fence or rock (the Safari Zone's). */
    case 0x1FC: return 0;
    case 0x1FD: return 1;
    /* Small trees: the edge of a wood, and inside it where the next crown
     * overlaps the trunk. 1F4-1F5 also carry a neighbour's leaves. */
    case 0x016: case 0x017: case 0x0C6: case 0x0C7:
    case 0x1F4: case 0x1F5: return VOXEL_TREE_SMALL;
    default: return -1;
    }
}

int VoxelTree_GroundMetatile(const void *secondaryTileset, int metatileId)
{
    if (metatileId >= SECONDARY_ID)
    {
        /* Dewford's crown top over the sand. */
        if (secondaryTileset == &gTileset_Dewford && metatileId == 0x239)
            return 0x124;
        /* A secondary tileset's own small crown top over its ground, north
         * of a trunk, on the maps the relief does not clear it on (its
         * CANOPY record): the ground the crown is drawn over. */
        if ((secondaryTileset == &gTileset_Rustboro && metatileId == 0x2C0)
         || (secondaryTileset == &gTileset_Lilycove && metatileId == 0x277))
            return 0x079; /* rock (Southern Island, Route 123) */
        /* Route 112's crowns over its dark rock, where no slope of the
         * relief draws the rock instead */
        if (secondaryTileset == &gTileset_Lavaridge && (metatileId == 0x211 || metatileId == 0x20B))
            return metatileId == 0x211 ? 0x079 : 0x07D;
        if (secondaryTileset == &gTileset_BattleFrontierOutsideWest && metatileId == 0x3F8)
            return 0x204;
        if (secondaryTileset == &gTileset_Mauville && metatileId == 0x2C1)
            return 0x205; /* Verdanturf's grass */
        if (secondaryTileset == &gTileset_EverGrande && metatileId == 0x262)
            return 0x222;
        if (secondaryTileset == &gTileset_Fortree && (metatileId == 0x316 || metatileId == 0x317))
            return 0x001; /* grass (Faraway Island) */
        return metatileId;
    }
    switch (metatileId)
    {
    case 0x1C6: case 0x1C7: return 0x00D; /* tall grass without canopy */
    case 0x1CE: case 0x1CF: return 0x001; /* ordinary grass */
    /* A small tree's canopy top, over whatever it stood in front of. The
     * fence feet under 040 have no metatile of their own: grass. */
    case 0x00E: case 0x00F: case 0x040: return 0x001;
    case 0x01D: return 0x002; /* ledge edge */
    case 0x025: return 0x00D; /* tall grass */
    case 0x02D: return 0x0A1; /* reflective water */
    case 0x035: case 0x193: return 0x170; /* calm water */
    case 0x0CE: return 0x091; /* rock wall, sand base */
    default: return metatileId;
    }
}

/*
 * The trunk's ground. Where the cell's relief is level, one plate at its
 * height. On a piece of slope one level tall (a band, a face, a corner: a
 * wood on a mountain's rock, Route 112's trees on the crags' bands) the
 * relief itself, the trunk's drawing laid on it: a plate at the centre's
 * height stood off the slope round it, half a level over its foot and
 * under its top, the void seen either side. The tree stands at the
 * centre's height either way. Anything taller is still a plate (Route
 * 120's wood, its cells rising two levels along their south edge: drawn,
 * more of the void showed past them). Past the last map, no relief.
 */
static bool OneLevelSlope(const int16_t *g)
{
    int lo, hi;

    if (g == NULL)
        return false;
    lo = hi = g[0];
    for (unsigned i = 1; i < VOXEL_RELIEF_SIDE * VOXEL_RELIEF_SIDE; ++i)
    {
        lo = g[i] < lo ? g[i] : lo;
        hi = g[i] > hi ? g[i] : hi;
    }
    return hi - lo == 16;
}

static void EmitGround(VoxelBuilder *builder, const VoxelMapInstance *inst, int x, int y,
                       float u0, float v0, float u1, float v1)
{
    float lift = builder->lift, shift = builder->shift;

    /* on a mountain's rock the ground pass lays the rock (EmitTreeStep) */
    if (inst != NULL && VoxelMesh_TreeOnRock(inst, x, y))
        return;
    if (inst == NULL || !OneLevelSlope(VoxelRelief_Cell(inst, x, y)))
    {
        VoxelMesh_Top(builder, (float)x, (float)y, 0.0f, 0.0f, u0, v0, u1, v1, 1.0f);
        return;
    }
    builder->lift = 0.0f;
    builder->shift = 0.0f;
    VoxelMesh_ReliefGround(builder, inst, x, y, u0, v0, u1, v1);
    builder->lift = lift;
    builder->shift = shift;
}

/* The small crown is 16:32: width 1, length 2 tiles, at the same 50 degrees
 * and sunk the same way as the large one, standing on its one-cell trunk.
 * Dewford's own small tree has its art at 64,0, the General one at 32,32;
 * a trunk on a secondary tileset's ground keeps the General's crown. */
static void EmitSmallCell(VoxelBuilder *builder, const VoxelMapInstance *inst, int x, int y,
                          const void *secondaryTileset, int metatileId, bool rock)
{
    static const uint8_t kTrunks[][2] = { { 48, 48 }, { 80, 16 }, { 64, 32 }, { 48, 32 } };
    int ground = SmallTrunkGround(secondaryTileset, metatileId);
    bool dewford = metatileId >= SECONDARY_ID && ground < 0;
    int cx = dewford ? 64 : 32, cy = dewford ? 0 : 32;
    int tx = ground >= 0 ? kTrunks[ground][0] : cx + 16, ty = ground >= 0 ? kTrunks[ground][1] : cy;
    float wx = (float)x, wz = (float)y;
    /* Against a mountain's face (its rock right behind the crown's cell,
     * Route 110's cave) or under higher ground right behind the trunk (a
     * terrace's edge) the crown stands up steeper, its top at the trunk
     * cell's back edge: leant back at 50 degrees it ran into the rock and
     * came out of it wherever the rock falls away, a corner's slope, and
     * the higher ground's top cut it across. */
    const float rise = 1.532089f, run = rock ? 0.8f : 1.285575f;
    const float baseHeight = -0.10f;
    /* 0.15 further forward than half the large tree's: any less and the
     * leaves stand through the ground behind the trunk. Dewford's another
     * 0.15, so its crown stands over the shadow drawn on its sand. */
    float baseZ = wz + (dewford ? 0.975f : 0.825f);

    EmitGround(builder, inst, x, y, TEX_U(tx), TEX_V(ty), TEX_U(tx + 16), TEX_V(ty + 16));
    builder->rounded = true;
    VoxelBuilder_Quad(builder,
        &(VoxelVertex){wx,        baseHeight + rise, baseZ - run, TEX_U(cx),      TEX_V(cy),      1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight + rise, baseZ - run, TEX_U(cx + 16), TEX_V(cy),      1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight,        baseZ,       TEX_U(cx + 16), TEX_V(cy + 32), 1.0f},
        &(VoxelVertex){wx,        baseHeight,        baseZ,       TEX_U(cx),      TEX_V(cy + 32), 1.0f});
    builder->rounded = false;
}

/* A 2x2 tree's art: its crown (rows of `rowPx` texels), its trunk's ground,
 * and how far into the trunk row the crown's foot stands (`foot`, cells
 * from its north edge): on its trunk's shadow. The General's large tree,
 * and the secret base's, closed or open: their upper rows are the same
 * drawing. The secret base's shadow is drawn further south and its crown
 * has no margin under the trunk: at the large tree's foot it stood on the
 * shadow's north rim, the tree floating over the ground. */
typedef struct
{
    uint8_t crownX, crownY, rowPx, groundX, groundY;
    float foot;
} LargeArt;

static const LargeArt kLargeTree = { 0, 0, 18, 32, 0, 0.35f };
static const LargeArt kBaseTree = { 0, 64, 16, 96, 0, 0.66f };
static const LargeArt kBaseTreeOpen = { 32, 64, 16, 96, 0, 0.66f };

static const LargeArt *LargeArtOf(int metatileId)
{
    switch (metatileId)
    {
    case 0x01E: case 0x01F: case 0x026: case 0x027: return &kBaseTree;
    case 0x036: case 0x037: return &kBaseTreeOpen;
    default: return &kLargeTree;
    }
}

/* One row of a large crown: the card that stands over the cell (x, y) as
 * the half of row `row`, column `col`, of a tree whose trunk row is
 * y - row + 1. */
static void EmitCrownCard(VoxelBuilder *builder, const LargeArt *art, int x, int y, int col, int row)
{
    float wx = (float)x, wz = (float)y;
    float u0 = (art->crownX + col * 16.0f) / VOXEL_TREE_TEXTURE_DIM;
    float u1 = u0 + 16.0f / VOXEL_TREE_TEXTURE_DIM;
    float v0 = 1.0f - (art->crownY + row * (float)art->rowPx) / VOXEL_TREE_TEXTURE_DIM;
    float v1 = v0 - (float)art->rowPx / VOXEL_TREE_TEXTURE_DIM;
    /* The crown keeps its aspect ratio: width 2, length 2.25 tiles for the
     * large tree's 32:36, 2 for the secret base's 32:32. sin/cos(50
     * degrees), fixed in the world rather than camera billboarding.
     * Lower the transparent bottom margin into the ground so the visible
     * leaves overlap the trunk instead of exposing a horizontal cut. */
    const float length = art->rowPx / 8.0f;
    const float rise = 0.766044f * length, run = 0.642788f * length;
    const float baseHeight = -0.10f;
    float baseZ = wz - row + 1.0f + art->foot;
    float top = 1.0f - row * 0.5f, bottom = top - 0.5f;

    /* A crown card: lit as the rounded crown it stands for. */
    builder->rounded = true;
    VoxelBuilder_Quad(builder,
        &(VoxelVertex){wx,        baseHeight + top * rise,    baseZ - top * run,    u0, v0, 1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight + top * rise,    baseZ - top * run,    u1, v0, 1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight + bottom * rise, baseZ - bottom * run, u1, v1, 1.0f},
        &(VoxelVertex){wx,        baseHeight + bottom * rise, baseZ - bottom * run, u0, v1, 1.0f});
    builder->rounded = false;
}

static void EmitCell(VoxelBuilder *builder, const VoxelMapInstance *inst, const void *secondaryTileset,
                     int x, int y, int part, int metatileId, bool rock)
{
    const LargeArt *art = LargeArtOf(metatileId);
    int col = part & 1, row = part >> 1;
    float u0 = (art->groundX + col * 16.0f) / VOXEL_TREE_TEXTURE_DIM;
    float u1 = u0 + 16.0f / VOXEL_TREE_TEXTURE_DIM;
    float v0 = 1.0f - (art->groundY + row * 16.0f) / VOXEL_TREE_TEXTURE_DIM;
    float v1 = v0 - 16.0f / VOXEL_TREE_TEXTURE_DIM;

    if (part == VOXEL_TREE_SMALL)
    {
        EmitSmallCell(builder, inst, x, y, secondaryTileset, metatileId, rock);
        return;
    }
    EmitGround(builder, inst, x, y, u0, v0, u1, v1);
    EmitCrownCard(builder, art, x, y, col, row);
}

/*
 * The other row of a large tree whose half is drawn over by something else:
 * a hedge, a roof, a fence or a ledge of the secondary tileset, whose
 * metatile is not a tree part. The GBA draws the rest of the leaves in that
 * metatile's art; here its row of the crown is added over it, so the crown
 * is not cut in two. Its ground is left as it is. Only inside the map: across
 * its edge the neighbour's own cells answer.
 */
static void EmitMissingRow(VoxelBuilder *builder, const VoxelMapInstance *inst,
                           int x, int y, int part, int metatileId)
{
    int row = part >> 1;
    int other = row == 0 ? y + 1 : y - 1;

    if (part == VOXEL_TREE_SMALL)
        return;
    if (other < inst->originY || other >= inst->originY + inst->height)
        return;
    if (VoxelTree_Part(inst->secondaryTileset, VoxelWorld_GetMetatileId(x, other)) == (part ^ 2))
        return;
    EmitCrownCard(builder, LargeArtOf(metatileId), x, other, part & 1, row ^ 1);
}

void VoxelTree_EmitInstance(VoxelBuilder *builder, const VoxelMapInstance *inst,
                            int x0, int y0, int x1, int y1)
{
    if (!VoxelWorld_UsesTreeSprites(inst))
        return;
    if (x0 < inst->originX) x0 = inst->originX;
    if (y0 < inst->originY) y0 = inst->originY;
    if (x1 > inst->originX + inst->width) x1 = inst->originX + inst->width;
    if (y1 > inst->originY + inst->height) y1 = inst->originY + inst->height;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            int metatileId = VoxelWorld_GetMetatileId(x, y);
            int part = VoxelTree_Part(inst->secondaryTileset, metatileId);
            if (part >= 0)
            {
                /* what stands behind the crown's cell: a mountain's rock? */
                unsigned behind = VoxelRegions_RoleAt((unsigned)inst->layoutId, x - inst->originX,
                                                      y - 2 - inst->originY);
                float lift = VoxelRelief_CellLift(inst, x, y);
                /* or ground higher than the trunk's right behind it: the
                 * next terrace up, the next row of a wood climbing one
                 * (Route 112, 119) - its top cut the leant crown across */
                bool higher = y - 1 >= inst->originY
                           && VoxelRelief_CellLift(inst, x, y - 1) > lift + 0.25f;

                builder->lift = lift;
                builder->shift = VoxelRelief_CellShift(inst, x, y);
                EmitCell(builder, inst, inst->secondaryTileset, x, y, part, metatileId,
                         behind == VOXEL_ROLE_CLIFF || behind == VOXEL_ROLE_SHELF || higher);
                /* At the tree's own lift, so both rows meet. */
                EmitMissingRow(builder, inst, x, y, part, metatileId);
                builder->lift = 0.0f;
                builder->shift = 0.0f;
            }
        }
}

void VoxelTree_EmitBorder(VoxelBuilder *builder, int x0, int y0, int x1, int y1)
{
    const VoxelMapInstance *current = VoxelWorld_Instance(0);

    if (!VoxelWorld_UsesTreeSprites(current))
        return;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            int metatileId, part;
            if (VoxelWorld_GetInstanceAt(x, y) != NULL)
                continue;
            metatileId = VoxelWorld_BorderMetatile(x, y);
            part = VoxelTree_Part(current->secondaryTileset, metatileId);
            if (part >= 0)
            {
                /* at its map edge's level (VoxelMesh_BorderLevel) */
                float level, shift;

                VoxelMesh_BorderLevel(x, y, &level, &shift);
                builder->lift = level;
                builder->shift = shift;
                EmitCell(builder, NULL, current->secondaryTileset, x, y, part, metatileId, false);
                builder->lift = 0.0f;
                builder->shift = 0.0f;
            }
        }
}
