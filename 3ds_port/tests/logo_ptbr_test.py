"""The Portuguese title logo: DrawLogo (src/3ds_icons_ptbr.c), built on the host,
writes the table sLogo into the title screen logo's tiles, pixel for pixel."""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <stdio.h>
void Port_TransformAsset(const char *path, unsigned char *data, unsigned size);
const unsigned char *Port_LatinGlyph(unsigned font, unsigned char code, int *width)
{
    (void)font;
    (void)code;
    *width = 0;
    return NULL;
}
int main(void)
{
    static unsigned char d[4096];
    Port_TransformAsset("graphics/title_screen/emerald_version.8bpp", d, sizeof(d));
    fwrite(d, 1, sizeof(d), stdout);
    return 0;
}
'''


def tile_offset(x, y):
    """Two 64x32 sprites of 8bpp tiles (gbagfx -mwidth 8 -mheight 4)."""
    return ((x // 64) * 32 + (y // 8) * 8 + (x % 64) // 8) * 64 + (y % 8) * 8 + x % 8


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', default='cc')
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('logo_ptbr', ROOT.parent / 'tools/logo_ptbr.py')
    logo_ptbr = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(logo_ptbr)
    want = logo_ptbr.table(ROOT / 'src/3ds_icons_ptbr.c')

    with tempfile.TemporaryDirectory() as tmp:
        harness = Path(tmp) / 'harness.c'
        harness.write_text(HARNESS)
        exe = Path(tmp) / 'logo_test'
        subprocess.run([args.cc, '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror', '-DCTR_LANG_PT_BR=1',
                        '-I' + str(ROOT / 'include'), str(harness), str(ROOT / 'src/3ds_icons_ptbr.c'),
                        '-lm', '-o', str(exe)], check=True)
        out = subprocess.run([str(exe)], capture_output=True, check=True).stdout

    wrong = [(x, y) for y in range(32) for x in range(128) if out[tile_offset(x, y)] != want[y][x]]
    if wrong:
        sys.exit('logo_ptbr_test: DrawLogo differs from sLogo at %d pixels, e.g. %s' % (len(wrong), wrong[:5]))
    print('PASS logo pt_br: DrawLogo writes sLogo into the logo tiles pixel for pixel')


if __name__ == '__main__':
    main()
