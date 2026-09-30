"""Build the standalone motion viewer (viewer.html) with textures from game/assets inlined.
Usage (from this folder):  python build.py      -> viewer.html / viewer_full.html
Reference only: this is a three.js preview of the animation rules, not game code."""
import base64, json, os

D = os.path.dirname(os.path.abspath(__file__))
TEX = os.path.normpath(os.path.join(D, '..', '..', 'game', 'assets', 'aurora', 'textures'))

BLOCKS = {  # viewer key -> file stem in textures/block
    'grass_top': 'grass_block_top', 'grass_side': 'grass_block_side', 'dirt': 'dirt', 'stone': 'stone',
    'cobble': 'cobblestone', 'stone_bricks': 'stone_bricks', 'planks': 'oak_planks', 'log_side': 'oak_log_side',
    'log_top': 'oak_log_top', 'mana_log': 'mana_log_side', 'mana_clear': 'mana_ore_clear', 'mana_pure': 'mana_ore_pure',
}


def uri(p):
    return 'data:image/png;base64,' + base64.b64encode(open(p, 'rb').read()).decode()


assets = {}
for key, stem in BLOCKS.items():
    for suf in ('', '_n', '_ec'):
        p = os.path.join(TEX, 'block', stem + suf + '.png')
        if os.path.exists(p): assets[key + suf] = uri(p)
for f in os.listdir(os.path.join(TEX, 'entity', 'player')):
    assets[f[:-4]] = uri(os.path.join(TEX, 'entity', 'player', f))
for f in os.listdir(os.path.join(TEX, 'entity', 'training_dummy')):
    assets['dummy_' + f[:-4]] = uri(os.path.join(TEX, 'entity', 'training_dummy', f))
for i in range(10):
    assets[f'destroy_{i}'] = uri(os.path.join(TEX, 'block', f'destroy_stage_{i}.png'))

src = open(os.path.join(D, 'viewer_src.html'), encoding='utf-8').read()
out = src.replace('/*__ASSETS__*/{}', json.dumps(assets))
open(os.path.join(D, 'viewer.html'), 'w', encoding='utf-8').write(out)
full = ('<!doctype html><html lang="ko"><head><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width,initial-scale=1"></head><body style="margin:0">' + out + '</body></html>')
open(os.path.join(D, 'viewer_full.html'), 'w', encoding='utf-8').write(full)
print('assets', len(assets), '-> viewer_full.html (open in a browser)')
