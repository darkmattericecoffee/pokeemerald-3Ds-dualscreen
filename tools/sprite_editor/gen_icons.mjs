// Generates 3ds_port/src/3ds_edit_icons.h: 1-bit bitmaps of the plane editor's
// icons at two sizes, from Pixelarticons' SVGs (https://pixelarticons.com, MIT licence;
// the icon artwork is Pixelarticons', this script only re-samples it).
//
// Run it from a scratch directory, not from the repository:
//   mkdir /tmp/icons && cd /tmp/icons && npm init -y
//   npm install --save-exact pixelarticons@2.4.1 @resvg/resvg-js@2.6.2
//   node <repo>/tools/sprite_editor/gen_icons.mjs /tmp/icons/node_modules \
//       <repo>/3ds_port/src/3ds_edit_icons.h [preview.png]
// The optional preview shows every icon at both sizes, 4x, ink black on white.
//
// Pixelarticons are drawn on a 24x24 pixel grid out of whole-pixel rectangles, so
// LARGE (24 px) is the artwork 1:1: resvg renders it at 24x24 and the script checks
// that every pixel came out fully opaque or fully clear (nothing anti-aliased away).
// SMALL (12 px) halves that grid: strokes are mostly 2 px wide on even coordinates,
// so a 2x2 box filter gives a 1 px stroke. Icons drawn on odd coordinates are first
// shifted by a pixel (the shift with the fewest half-covered 2x2 blocks wins), and
// each icon's threshold (how many of a block's 4 pixels must be ink) was judged by eye.

import { readFileSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { join } from 'node:path';

const [modulesDir, outPath, previewPath] = process.argv.slice(2);
if (!modulesDir || !outPath) {
    console.error('usage: node gen_icons.mjs <node_modules dir> <out header> [preview.png]');
    process.exit(1);
}
const require = createRequire(join(modulesDir, 'noop.js'));
const { Resvg } = require('@resvg/resvg-js');

const LARGE = 24, SMALL = 12;

// Enum name -> Pixelarticons icon name [, SMALL overrides: { min: ink pixels of 4 needed
// (default 2), shift: [dx, dy] to force a grid shift instead of searching, or rows: the
// 12 px icon drawn by hand ('#' is ink) where no single shift works because the icon
// mixes odd and even coordinates; those are the icon's own parts, each halved exactly
// under its own shift, put back together }].
const ICONS = [
    // Ring from the icon's exact half, with a 3 px plus (the 24 px plus is on odd coordinates).
    ['ZOOM', 'zoom-in', { rows: [
        '............',
        '...####.....',
        '..#....#....',
        '.#..#...#...',
        '.#.###..#...',
        '.#..#...#...',
        '.#......#...',
        '..#....#....',
        '...####.#...',
        '.........#..',
        '..........#.',
        '............'] }],
    ['PENCIL', 'pencil'],
    ['LASSO', 'lasso'],
    ['PAN', 'hand'],
    ['GRID', 'grid-3x3'],
    ['GRID_OFF', 'square'],
    ['UNDO', 'undo'],
    ['REDO', 'redo'],
    ['SETTINGS', 'sliders'],
    ['OUTLINE', 'section'],
    // Three 2 px dots where the hollow 1 px diamonds the halving gives look like gears.
    ['MORE', 'more-horizontal', { rows: [
        '............',
        '............',
        '............',
        '............',
        '............',
        '.##..##..##.',
        '.##..##..##.',
        '............',
        '............',
        '............',
        '............',
        '............'] }],
    ['LIST', 'bulletlist'],
    ['SEARCH', 'search'],
    ['FILTER', 'filter'],
    ['SORT', 'sort-vertical'],
    // Box halved under shift (1, 1), pencil under shift (0, 0): each exact on its own.
    ['EDIT', 'pen-square', { rows: [
        '............',
        '........#...',
        '...###.#.#..',
        '..#...#...#.',
        '..#..#...#..',
        '..#.#...#...',
        '..#.##.#....',
        '..#.###...#.',
        '..#.......#.',
        '..#.......#.',
        '..#######...',
        '............'] }],
    ['CHECK', 'check'],
    ['BOX', 'checkbox'],
    // Box halved under shift (0, 0), check mark under shift (1, 0).
    ['BOX_CHECK', 'checkbox-on', { rows: [
        '............',
        '..########..',
        '.#........#.',
        '.#........#.',
        '.#......#.#.',
        '.#.....#..#.',
        '.#..#.#...#.',
        '.#...#....#.',
        '.#........#.',
        '.#........#.',
        '..########..',
        '............'] }],
    ['USER', 'user'],
    ['BATTLE', 'sword'],
    ['DELETE', 'delete'],
    ['ENTER', 'corner-down-left'],
    ['X', 'close'],
    ['CROSS', 'cross'],
    ['FLAG', 'flag'],
    ['SWAP', 'arrows-horizontal'],
    // The OPTIONS screen's ONLINE tab (3ds_bottom_ui.c), drawn at 24 px.
    ['REFRESH', 'refresh'],
    ['UPLOAD', 'upload'],
    ['DOWNLOAD', 'download'],
    ['LINK', 'link'],
    ['UNLINK', 'unlink'],
    ['SERVER', 'server'],
    ['HEART', 'heart'],
    // The editor's Pokédex: "yours" next to a picture the card has its own edit of.
    ['SMILE', 'smile'],
];

// The icon's 24x24 grid as an array of 0/1, straight from a 1:1 resvg render.
function grid24(svgText, name) {
    const svg = svgText
        .replace(/<!--[\s\S]*?-->/g, '')
        .replace(/\s(width|height)="[^"]*"/g, '')
        .replace(/<svg/, `<svg width="${LARGE}" height="${LARGE}"`)
        .replace(/fill="currentColor"/g, 'fill="#000"');
    const img = new Resvg(svg, { fitTo: { mode: 'original' }, font: { loadSystemFonts: false } }).render();
    if (img.width !== LARGE || img.height !== LARGE) throw new Error(`${name}: rendered ${img.width}x${img.height}`);
    const g = new Uint8Array(LARGE * LARGE);
    for (let i = 0; i < LARGE * LARGE; i++) {
        const a = img.pixels[i * 4 + 3];
        if (a !== 0 && a !== 255) throw new Error(`${name}: pixel ${i % LARGE},${Math.floor(i / LARGE)} is anti-aliased (alpha ${a}); not on the pixel grid`);
        g[i] = a ? 1 : 0;
    }
    return g;
}

// Halve a 24 grid: shift it by (dx, dy), then each 2x2 block is ink when at least
// `min` of its pixels are. Returns the 12x12 grid and how many blocks were partial.
function halve(g, dx, dy, min) {
    const h = new Uint8Array(SMALL * SMALL);
    let partial = 0;
    const at = (x, y) => (x >= 0 && y >= 0 && x < LARGE && y < LARGE ? g[y * LARGE + x] : 0);
    for (let r = 0; r < SMALL; r++) {
        for (let c = 0; c < SMALL; c++) {
            const x = 2 * c - dx, y = 2 * r - dy;
            const n = at(x, y) + at(x + 1, y) + at(x, y + 1) + at(x + 1, y + 1);
            if (n !== 0 && n !== 4) partial++;
            h[r * SMALL + c] = n >= min ? 1 : 0;
        }
    }
    return { h, partial };
}

function bestHalf(g, name, over = {}) {
    const min = over.min ?? 2;
    if (over.rows) {
        if (over.rows.length !== SMALL || over.rows.some((r) => r.length !== SMALL)) throw new Error(`${name}: hand-drawn rows must be ${SMALL}x${SMALL}`);
        return Uint8Array.from(over.rows.join(''), (ch) => (ch === '#' ? 1 : 0));
    }
    if (over.shift) return halve(g, over.shift[0], over.shift[1], min).h;
    // Ink must not fall off the shifted edge.
    const inkAt = (x, y) => g[y * LARGE + x];
    let best = null;
    for (const dy of [0, 1]) {
        for (const dx of [0, 1]) {
            let lost = false;
            for (let i = 0; i < LARGE && !lost; i++) lost = (dx && inkAt(LARGE - 1, i)) || (dy && inkAt(i, LARGE - 1));
            if (lost) continue;
            const { h, partial } = halve(g, dx, dy, min);
            if (!best || partial < best.partial) best = { h, partial, dx, dy };
        }
    }
    if (!best) throw new Error(`${name}: no shift keeps the ink inside`);
    return best.h;
}

const rowsOf = (g, px) => {
    const rows = [];
    for (let r = 0; r < px; r++) {
        let bits = 0;
        for (let c = 0; c < px; c++) if (g[r * px + c]) bits |= 1 << (px - 1 - c);
        rows.push(bits >>> 0);
    }
    return rows;
};

const bitmaps = { SMALL: [], LARGE: [] };
for (const [, name, over] of ICONS) {
    const svgText = readFileSync(join(modulesDir, 'pixelarticons', 'svg', `${name}.svg`), 'utf8');
    const g = grid24(svgText, name);
    bitmaps.LARGE.push(rowsOf(g, LARGE));
    bitmaps.SMALL.push(rowsOf(bestHalf(g, name, over), SMALL));
}

const hex = (v, px) => '0x' + v.toString(16).toUpperCase().padStart(Math.ceil(px / 4), '0');
const table = (key, px) => {
    const lines = bitmaps[key].map((rows, i) =>
        `    { ${rows.map((v) => hex(v, px)).join(', ')} }, /* ${ICONS[i][1]} */`);
    return `static const uint32_t sIcon${key[0]}${key.slice(1).toLowerCase()}[ICON_COUNT][ICON_${key}] = {\n${lines.join('\n')}\n};\n`;
};

const header = `/* Generated by tools/sprite_editor/gen_icons.mjs from Pixelarticons (pixelarticons.com, MIT licence): do not edit. */
#ifndef CTR_EDIT_ICONS_H
#define CTR_EDIT_ICONS_H
#include <stdint.h>
#define ICON_SMALL ${SMALL}
#define ICON_LARGE ${LARGE}
enum { ${ICONS.map(([e]) => `ICON_${e}, `).join('')}ICON_COUNT };
/* Row r of an icon: bit (size - 1 - c) set where column c is ink. */
${table('SMALL', SMALL)}${table('LARGE', LARGE)}#endif
`;
writeFileSync(outPath, header);
console.log(`wrote ${outPath}: ${ICONS.length} icons`);

if (previewPath) {
    // Grid of icons: one column per icon, small above large, 4x pixels.
    const S = 4, gap = 8, cell = LARGE * S + gap;
    const cols = 9, rowsOfIcons = Math.ceil(ICONS.length / cols);
    const blockH = (SMALL + LARGE) * S + 3 * gap;
    const W = cols * cell + gap, H = rowsOfIcons * blockH + gap;
    const rects = [`<rect width="${W}" height="${H}" fill="#fff"/>`];
    const draw = (rows, px, x0, y0) => {
        rects.push(`<rect x="${x0 - 1}" y="${y0 - 1}" width="${px * S + 2}" height="${px * S + 2}" fill="none" stroke="#9cf"/>`);
        for (let r = 0; r < px; r++)
            for (let c = 0; c < px; c++)
                if ((rows[r] >> (px - 1 - c)) & 1)
                    rects.push(`<rect x="${x0 + c * S}" y="${y0 + r * S}" width="${S}" height="${S}" fill="#000"/>`);
    };
    ICONS.forEach((_, i) => {
        const x = gap + (i % cols) * cell, y = gap + Math.floor(i / cols) * blockH;
        draw(bitmaps.SMALL[i], SMALL, x, y);
        draw(bitmaps.LARGE[i], LARGE, x, y + SMALL * S + gap);
    });
    const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}">${rects.join('')}</svg>`;
    writeFileSync(previewPath, new Resvg(svg, { shapeRendering: 0, font: { loadSystemFonts: false } }).render().asPng());
    console.log(`wrote ${previewPath}`);
}
