# Trees of the General tileset

Original art for the voxel mode's trees, drawn for Emerald 3DS:

- `tree_trunk.png`: 32×32, opaque trunk and ground, on a 2×2-cell footprint.
- `tree_crown.png`: 32×36 RGBA, crown on a transparent background.
- `tree_small_trunk.png`: 16×16, trunk of the small tree on a single cell.
- `tree_small_crown.png`: 16×32 RGBA, crown of the small tree on a
  transparent background.
- `tree_2_small_trunk.png`, `tree_2_small_crown.png`: the same pair for
  Dewford Town's small tree, trunk on sand.
- `tree_small_trunk_jungle.png`, `tree_small_trunk_grass_road.png`,
  `tree_small_trunk_volcan.png`: 16×16, the small tree's trunk on Fortree's
  jungle floor, Route 111's pale grass and Route 112's volcanic soil. Their
  crown is `tree_small_crown.png`.
- `tree_crown_base_floor.png`: 32×32, opaque ground of the secret base's
  tree, on a 2×2-cell footprint.
- `tree_crown_base.png`, `tree_crown_base_open.png`: 32×32 RGBA, its crown
  closed and with the base's entrance opened (the same upper half).

`scripts/gen_voxel_trees.py` packs them, unscaled, into
`voxel/trees.rgba5551`: a 128×128 RGBA5551 texture in PICA200 order (large
crown at 0,0; large trunk at 32,0; small crown at 32,32; small trunk at
48,32; Dewford's small crown at 64,0 and its trunk at 80,0; the jungle,
pale-grass and volcanic trunks at 48,48, 80,16 and 64,32; the secret base
tree's floor at 96,0, its crown at 0,64 and the opened one at 32,64). It is an engine file and ships inside the 3DSX. The build regenerates
it whenever a source changes; it needs Pillow like the port's other graphics
tools.

The crown is a fixed plane tilted 50° from the ground, high end to the north.
The tilt and the anchor point are in `src/voxel/voxel_tree.c`. The plane's
base sits at −0.10 cells: the transparent margin sinks into the ground and the
visible leaves overlap the trunk. The renderer uses alpha test: transparent
pixels write neither colour nor depth.

They replace the regular metatiles `1D4–1D7`, `1DC–1DF`, `1E4–1E7`, `1F2–1F3`
(the lower row of a staggered wood, under the next tree's canopy) and
`1FC–1FD` (the upper row in front of a fence) of the **General** tileset, also in its repeated border. The tops `1C6–1C7` and
`1CE–1CF` get their background grass back. Special trees keep their art. Each
cell emits its own part of the trunk and the crown, so a tree can cross a
chunk or map boundary without being duplicated or cut when rebuilt.

Where a hedge, a roof, a fence or a ledge of the secondary tileset draws over
one row of a large tree (Petalburg, Oldale, Lilycove, Route 104's flower
shop, Route 116, the Safari Zone), that cell is not a tree part: it keeps its
own ground and model, and the tree's row in the next cell adds the missing
row of the crown over it, so the crown is not cut in two. Inside the map
only; across its edge the neighbour's own cells answer.

The small tree works the same way: the trunk cell (`016–017`, forest edge;
`0C6–0C7`, interior, where the next crown overlaps the trunk; `1F4–1F5`)
becomes the new trunk and raises the whole crown above it, a 1×2-cell plane at
50°, base at −0.10 and 0.825 cells from the north edge. The upper cells lose
the old crown and get their background back: `00E–00F` and `040` grass, `01D`
ledge edge, `025` tall grass, `02D` reflective water, `035`/`193` water, `0CE`
rock. `1EC–1ED` are the bottom row of a large tree. The secondary tilesets'
variants keep their art.

A small tree at the foot of a mountain (its crown's cell backed by a cliff or
shelf, two cells north of the trunk) stands up steeper, the plane's top at the
trunk cell's north edge: leant back at 50° it ran into the rock and came out of
it where the rock falls away, a corner's slope (Route 110, by New Mauville's
entrance). So does one whose north neighbour stands higher than its trunk (a
terrace's edge, a wood climbing terraces on Route 112 and 119): the higher
ground's top cut the leant crown across.

Dewford Town's small tree (its secondary tileset, so the ids only mean a tree
there) is replaced the same way with its own art: the trunk cells `23A` (on
the sand) and `243` (inside the wood, where the next crown overlaps it, also
the repeated border) become the trunk and carry the whole crown; the crown top
`239` gets the sand `124` back.

The General's small tree drawn by a secondary tileset over its own ground is
replaced the same way, with the General's crown and a trunk on that ground:
Fortree's `253` (jungle floor), Mauville's `226` (Route 111's pale grass) and
Lavaridge's `218` (Route 112 and Lavaridge Town), with `21A`, `3B8` and
`203` inside its woods, where the next crown overlaps the trunk (the
General's `0C6`, on the soil and on the dark rock). Fortree's `22A` is that
`0C6` with the crown drawn on both layers, round its tree houses: the
General's trunk on grass. The old crown its tileset
draws over the cell north of the trunk (over its ground, a mountain's rock or
anything else) is cut away by the relief (`scripts/voxel_terraces.py`,
`canopy_mask`), the ground it was drawn over laid under it. On maps the
relief does not read that way (Verdanturf, the Battle Frontier, Southern
Island, Ever Grande, Faraway Island) and over Route 112's dark rock,
`VoxelTree_GroundMetatile` swaps the tile for the plain ground it is drawn
over, the same tile without the crown.

The secret base's tree (`01E–01F` over `026–027`, `036–037` once
`secret_base.c` opens it) is a 2×2 tree like the large one, with its own
floor and a 32×32 crown, two cells long at the same 50°; its lower row takes
the opened crown when the base is open. Both are General ids, so it is
replaced on every map.
