"""Run the generators and copy results into game/assets/aurora/textures with the game's file names.
Usage (from tools/texgen):  pip install -r requirements.txt  &&  python export.py"""
import glob, os, shutil, subprocess, sys

D = os.path.dirname(os.path.abspath(__file__))
B = os.path.normpath(os.path.join(D, '..', '..', 'game', 'assets', 'aurora', 'textures'))
os.makedirs(os.path.join(D, 'out'), exist_ok=True)
for s in ('tex32.py', 'skin32.py'):
    subprocess.run([sys.executable, os.path.join(D, s)], cwd=D, check=True)

REN = {'grass_top': 'grass_block_top', 'grass_side': 'grass_block_side', 'cobble': 'cobblestone', 'log_side': 'oak_log_side',
       'log_top': 'oak_log_top', 'planks': 'oak_planks', 'mana_log': 'mana_log_side', 'mana_dull': 'mana_ore_murky',
       'mana_clear': 'mana_ore_clear', 'mana_pure': 'mana_ore_pure', 'mana_abyss': 'mana_ore_abyss', 'rivet_plate': 'riveted_steel_plate'}
ITEMS = {'steel_sword', 'aurora_sword', 'mana_battery'}
for d in ('block', 'item', 'entity/player', 'entity/training_dummy'):
    os.makedirs(os.path.join(B, d), exist_ok=True)
for f in glob.glob(os.path.join(D, 'tex32', '*.png')):
    n = os.path.basename(f)[:-4]; base, suf = n, ''
    for s in ('_ec', '_n', '_e', '_s'):
        if n.endswith(s): base, suf = n[:-len(s)], s; break
    shutil.copy(f, os.path.join(B, 'item' if base in ITEMS else 'block', REN.get(base, base) + suf + '.png'))
for f in glob.glob(os.path.join(D, 'skin32', '*.png')):
    n = os.path.basename(f)
    if n.startswith('destroy_'): shutil.copy(f, os.path.join(B, 'block', 'destroy_stage_' + n[8:]))
    elif n.startswith('dummy_'): shutil.copy(f, os.path.join(B, 'entity', 'training_dummy', n[6:]))
    else: shutil.copy(f, os.path.join(B, 'entity', 'player', n))
print('exported to', B)
