// Generates 3ds_port/src/3ds_edit_icons.h: 1-bit bitmaps of the plane editor's
// icons at two sizes, rasterised from Lucide's SVGs (https://lucide.dev, ISC licence;
// the icon artwork is Lucide's, this script only re-renders it).
//
// Run it from a scratch directory, not from the repository:
//   mkdir /tmp/icons && cd /tmp/icons && npm init -y
//   npm install --save-exact lucide-static@1.47.0 @resvg/resvg-js@2.6.2
//   node <repo>/tools/sprite_editor/gen_icons.mjs /tmp/icons/node_modules \
//       <repo>/3ds_port/src/3ds_edit_icons.h [preview.png]
// The optional preview shows every icon at both sizes, 4x, ink black and the
// 1 px outline the C code draws around it in grey.
//
// Lucide draws on a 24 grid with stroke-width 2, which is too thin at 20 px, so each
// size gets its own stroke width and alpha threshold (12 px has so few pixels that
// strokes stay near 1 px, else shapes close up; a few icons override). The drawing area
// (2..22 on the grid, widened by the extra stroke) maps to the bitmap minus a
// 1 px empty margin all round, which the C code's outline uses.

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

// Enum name -> Lucide icon name [, per-size { SIZE: [stroke, threshold, zoom] } overrides,
// picked by eye where the default merges or breaks lines; zoom < 1 draws it smaller].
const ICONS = [
    ['ZOOM', 'zoom-in'],
    ['BRUSH', 'paintbrush'],
    ['PAN', 'hand', { SMALL: [2, 0.4] }],
    ['FILL', 'paint-bucket', { SMALL: [2, 0.5, 0.9] }],
    ['GRID', 'grid-3x3'],
    ['GRID_OFF', 'square'],
    ['LAYERS', 'layers'],
    ['EYE', 'eye'],
    ['SEARCH', 'search'],
    ['LIST', 'list', { SMALL: [2.25, 0.45, 0.9] }],
    ['BATTLE', 'swords', { SMALL: [2, 0.5, 0.85] }],
    ['CHECK', 'check'],
    ['BOX', 'square'],
    ['BOX_CHECK', 'square-check'],
    ['USER', 'user'],
    ['FILM', 'film'],
    ['MORE', 'ellipsis', { SMALL: [2.5, 0.45, 0.9] }],
    ['UNDO', 'undo-2'],
    ['REDO', 'redo-2'],
    ['SAVE', 'save'],
    ['CONTRAST', 'contrast', { SMALL: [2, 0.5] }],
    ['OUTLINE', 'square-dashed'],
    ['SORT', 'arrow-down-wide-narrow'],
    ['FILTER', 'list-filter', { SMALL: [2.5, 0.5] }],
    ['PENCIL', 'pencil'],
    ['DELETE', 'delete', { SMALL: [2, 0.4] }],
    ['ENTER', 'corner-down-left'],
    ['X', 'x'],
    ['SWAP', 'arrow-left-right'],
    ['FLAG', 'flag', { SMALL: [2.5, 0.5] }],
];

// Per size: Lucide stroke width (24-grid units) and the alpha above which a pixel is ink.
const SIZES = {
    SMALL: { px: 12, stroke: 2.25, threshold: 0.45 },
    LARGE: { px: 20, stroke: 2.5, threshold: 0.45 },
};
// Sub-pixel shifts tried per icon (a crude hinting): the one whose 1-bit image is
// closest to the anti-aliased one wins, so strokes land on whole pixels. An icon
// whose ink spills into the 1 px margin at every shift is drawn slightly smaller.
const SHIFTS = [-0.5, -0.25, 0, 0.25, 0.5];
const ZOOMS = [1, 0.95, 0.9, 0.85, 0.8];

function coverage(svgText, name, { px, stroke }, zoom, dx, dy) {
    // Grid area the ink covers (2..22, widened by the extra stroke), mapped onto
    // px - 2 pixels with a 1 px margin.
    const span = (20 + (stroke - 2)) / zoom, c0 = 12 - span / 2;
    const unit = span / (px - 2);
    const vb = [c0 - unit - dx * unit, c0 - unit - dy * unit, span + 2 * unit, span + 2 * unit].join(' ');
    const svg = svgText
        .replace(/<!--[\s\S]*?-->/g, '')
        .replace(/\swidth="24"/, ` width="${px}"`)
        .replace(/\sheight="24"/, ` height="${px}"`)
        .replace(/viewBox="[^"]*"/, `viewBox="${vb}"`)
        .replace(/stroke-width="[^"]*"/, `stroke-width="${stroke}"`)
        .replace(/stroke="currentColor"/, 'stroke="#000"');
    const img = new Resvg(svg, { fitTo: { mode: 'original' }, shapeRendering: 2, font: { loadSystemFonts: false } }).render();
    if (img.width !== px || img.height !== px) throw new Error(`${name}: rendered ${img.width}x${img.height}`);
    const a = new Float32Array(px * px);
    for (let i = 0; i < px * px; i++) a[i] = img.pixels[i * 4 + 3] / 255;
    return a;
}

function rasterise(name, size) {
    const { px, threshold, zoom: maxZoom = 1 } = size;
    const svgText = readFileSync(join(modulesDir, 'lucide-static', 'icons', `${name}.svg`), 'utf8');
    let best = null;
    for (const zoom of ZOOMS.map((z) => z * maxZoom)) {
        for (const dy of SHIFTS) {
            for (const dx of SHIFTS) {
                const a = coverage(svgText, name, size, zoom, dx, dy);
                let err = 0.05 * (Math.abs(dx) + Math.abs(dy)) * px; // prefer staying centred
                let spill = false;
                for (let r = 0; r < px; r++) {
                    for (let c = 0; c < px; c++) {
                        const v = a[r * px + c], ink = v > threshold;
                        err += Math.abs(v - (ink ? 1 : 0));
                        if (ink && (r === 0 || c === 0 || r === px - 1 || c === px - 1)) spill = true;
                    }
                }
                if (!spill && (!best || err < best.err)) best = { err, a };
            }
        }
        if (best) break;
    }
    if (!best) throw new Error(`${name}: ink reaches the margin at every zoom`);
    const rows = [];
    for (let r = 0; r < px; r++) {
        let bits = 0;
        for (let c = 0; c < px; c++) if (best.a[r * px + c] > threshold) bits |= 1 << (px - 1 - c);
        rows.push(bits >>> 0);
    }
    return rows;
}

const bitmaps = {};
for (const key of Object.keys(SIZES)) {
    bitmaps[key] = ICONS.map(([, name, over]) => {
        const o = over && over[key];
        return rasterise(name, o ? { ...SIZES[key], stroke: o[0], threshold: o[1], zoom: o[2] } : SIZES[key]);
    });
}

const hex = (v, px) => '0x' + v.toString(16).toUpperCase().padStart(Math.ceil(px / 4), '0');
const table = (key) => {
    const { px } = SIZES[key];
    const lines = bitmaps[key].map((rows, i) =>
        `    { ${rows.map((v) => hex(v, px)).join(', ')} }, /* ${ICONS[i][1]} */`);
    return `static const uint32_t sIcon${key[0]}${key.slice(1).toLowerCase()}[ICON_COUNT][ICON_${key}] = {\n${lines.join('\n')}\n};\n`;
};

const header = `/* Generated by tools/sprite_editor/gen_icons.mjs from Lucide (lucide.dev, ISC licence): do not edit. */
#ifndef CTR_EDIT_ICONS_H
#define CTR_EDIT_ICONS_H
#include <stdint.h>
#define ICON_SMALL ${SIZES.SMALL.px}
#define ICON_LARGE ${SIZES.LARGE.px}
enum { ${ICONS.map(([e]) => `ICON_${e}, `).join('')}ICON_COUNT };
/* Row r of an icon: bit (size - 1 - c) set where column c is ink. */
${table('SMALL')}${table('LARGE')}#endif
`;
writeFileSync(outPath, header);
console.log(`wrote ${outPath}: ${ICONS.length} icons`);

if (previewPath) {
    // Grid of icons: one column per icon, small row above large row, 4x pixels.
    const S = 4, gap = 8, cell = SIZES.LARGE.px * S + gap;
    const cols = 10, rowsOfIcons = Math.ceil(ICONS.length / cols);
    const blockH = (SIZES.SMALL.px + SIZES.LARGE.px) * S + 3 * gap;
    const W = cols * cell + gap, H = rowsOfIcons * blockH + gap;
    const rects = [`<rect width="${W}" height="${H}" fill="#fff"/>`];
    const draw = (rows, px, x0, y0) => {
        const ink = (r, c) => r >= 0 && r < px && c >= 0 && c < px && (rows[r] >> (px - 1 - c)) & 1;
        rects.push(`<rect x="${x0 - 1}" y="${y0 - 1}" width="${px * S + 2}" height="${px * S + 2}" fill="none" stroke="#9cf"/>`);
        for (let r = 0; r < px; r++) {
            for (let c = 0; c < px; c++) {
                let fill = null;
                if (ink(r, c)) fill = '#000';
                else {
                    for (let dr = -1; dr <= 1 && !fill; dr++)
                        for (let dc = -1; dc <= 1 && !fill; dc++) if (ink(r + dr, c + dc)) fill = '#aaa';
                }
                if (fill) rects.push(`<rect x="${x0 + c * S}" y="${y0 + r * S}" width="${S}" height="${S}" fill="${fill}"/>`);
            }
        }
    };
    ICONS.forEach((_, i) => {
        const x = gap + (i % cols) * cell, y = gap + Math.floor(i / cols) * blockH;
        draw(bitmaps.SMALL[i], SIZES.SMALL.px, x, y);
        draw(bitmaps.LARGE[i], SIZES.LARGE.px, x, y + SIZES.SMALL.px * S + gap);
    });
    const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}">${rects.join('')}</svg>`;
    writeFileSync(previewPath, new Resvg(svg, { shapeRendering: 0, font: { loadSystemFonts: false } }).render().asPng());
    console.log(`wrote ${previewPath}`);
}
