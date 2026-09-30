"""Project Aurora - 32x32 procedural textures v3.
1 block = 32 px. Height-field shading (light top-left), AO, hue-shifted ramps.
Rules from feedback: no near-black (darkest stop is lifted), mortar/crevices are dark grey not black,
ores cover ~25 % of the face (stone : ore = 3 : 1), battery drawn diagonally, aurora sword kept restrained.
Outputs name.png, name_n.png (normal), name_e.png / name_ec.png (emissive), name_s.png (R smooth, G metal, B emissive).
"""
import math, os
import numpy as np
from scipy import ndimage
from PIL import Image, ImageDraw, ImageFont

N = 32
D = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(D, 'tex32')
os.makedirs(OUT, exist_ok=True)
L = np.array([-0.55, -0.65, 0.52]); L /= np.linalg.norm(L)
H = (L + np.array([0, 0, 1.0])); H /= np.linalg.norm(H)       # half vector for specular


# ------------------------------------------------------------------ helpers
def vnoise(seed, fy, fx=None):
    fx = fx or fy
    g = np.random.default_rng(seed).random((fy, fx))
    return ndimage.zoom(g, (N / fy, N / fx), order=3, mode='grid-wrap', grid_mode=True)


def fbm(seed, base=4, octaves=3, pers=0.5, aniso=(1, 1)):
    tot, amp, s, f = np.zeros((N, N)), 1.0, 0.0, base
    for o in range(octaves):
        fy, fx = min(N, max(1, int(f * aniso[0]))), min(N, max(1, int(f * aniso[1])))
        tot += amp * vnoise(seed + o * 101, fy, fx); s += amp; amp *= pers; f *= 2
    tot /= s
    return (tot - tot.min()) / (tot.max() - tot.min() + 1e-9)


def worley(seed, n):
    r = np.random.default_rng(seed)
    pts = r.random((n, 2)) * N
    yy, xx = np.mgrid[0:N, 0:N] + 0.5
    dx = np.abs(xx[..., None] - pts[:, 0]); dx = np.minimum(dx, N - dx)
    dy = np.abs(yy[..., None] - pts[:, 1]); dy = np.minimum(dy, N - dy)
    d = np.sqrt(dx * dx + dy * dy)
    idx = np.argsort(d, axis=-1)
    f1 = np.take_along_axis(d, idx[..., :1], -1)[..., 0]
    f2 = np.take_along_axis(d, idx[..., 1:2], -1)[..., 0]
    return f1, f2, idx[..., 0], pts


def blur(a, s): return ndimage.gaussian_filter(a, s, mode='wrap')
def smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t)


def normals(h, k):
    gx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 0.5 * k
    gy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 0.5 * k
    n = np.dstack([-gx, -gy, np.ones_like(h)])
    return n / np.linalg.norm(n, axis=2, keepdims=True)


def diffuse(h, k):
    n = normals(h, k); return np.clip(n @ L, 0, 1), n


def ao(h, s=1.5, k=1.0): return np.clip(1 + k * (h - blur(h, s)), 0.55, 1.25)
def hx(s): s = s.lstrip('#'); return [int(s[i:i + 2], 16) for i in (0, 2, 4)]


def ramp(t, cols, levels=16):
    t = np.clip(t, 0, 1)
    if levels: t = np.round(t * (levels - 1)) / (levels - 1)
    c = np.array([hx(x) for x in cols], float); pos = np.linspace(0, 1, len(c))
    return np.dstack([np.interp(t, pos, c[:, i]) for i in range(3)])


def shade(h, base, k=4.0, amt=0.45, aok=1.0, aos=1.5):
    d, n = diffuse(h, k)
    t = base + amt * (d - L[2]) + 0.3 * (ao(h, aos, aok) - 1)
    return np.clip(t, 0, 1), n


# ------------------------------------------------------------------ ramps: darkest stop lifted (no near-black)
R = dict(
    stone=['#46454d', '#56555c', '#67666c', '#7a787c', '#8e8b8d', '#a4a09f', '#bdb8b2'],
    deep=['#2e2e38', '#383843', '#43434f', '#50505c', '#5e5e6b', '#71717d'],
    dirt=['#43301f', '#553d27', '#684b30', '#7c5a3a', '#916c47', '#a78259'],
    grass=['#2f4f2f', '#3a6135', '#48763b', '#588c43', '#6ba24c', '#84b75a', '#a2ca6c'],
    bark=['#3a2b1f', '#4a3827', '#5b4630', '#6d5539', '#826644', '#987a52'],
    wood=['#5a4027', '#6f502f', '#856137', '#9b7341', '#b0874f', '#c59d62', '#d6b47c'],
    mbark=['#2a2538', '#332d44', '#3e3752', '#4a4261', '#595072'],
    steel=['#3a4049', '#4b525d', '#5d6571', '#707986', '#86909c', '#9ea8b3', '#bcc5cd', '#e3e9ee'],
    black=['#26272d', '#2f3138', '#393c44', '#454951', '#53575f', '#656a73'],
    bronze=['#5a3a1a', '#76501f', '#936a29', '#b58838', '#d2a650', '#ecc97f'],
    coal=['#23252c', '#2c2f37', '#363a44', '#434855', '#575d6c', '#737a8a'],
    iron=['#6b4430', '#88573c', '#a66f4f', '#c38c69', '#dbab89', '#efcfb3'],
    gold=['#6e4a0f', '#946814', '#bb8a1f', '#dcab30', '#f1cc55', '#fde89c'],
    dia=['#1b5059', '#1f7580', '#2a9ea6', '#48c9cb', '#8eece6', '#dcfffb'],
    m_dull=['#2b4247', '#355259', '#42636a', '#50767c', '#648c90', '#80a6a8'],
    m_clear=['#1d5654', '#237373', '#2b928c', '#40b8ad', '#72dfd1', '#c6fff4'],
    m_pure=['#2f8a8a', '#44aaa8', '#63d0c8', '#93efe4', '#ccfff7', '#ffffff'],
    m_abyss=['#2a1a50', '#3a2270', '#512f98', '#6f43c4', '#9b72f2', '#d3c0ff'],
    leather=['#3a2418', '#4b2e1e', '#5e3a25', '#724a30', '#8a5d3d'],
    aurora=['#35516e', '#44698a', '#5885a4', '#71a2bd', '#91c0d3', '#bddfe9', '#eaf8fb'],
    brass=['#5c3f1c', '#7a5624', '#9a712f', '#bc8f3f', '#d8ae5a', '#f0d08a', '#fff0c8'],
    copper=['#5e2e18', '#7c3e20', '#9c522a', '#bd6a37', '#d98a52', '#f0b182', '#ffd9bd'],
    gunmetal=['#4a5260', '#5b6474', '#6e7888', '#85909f', '#a2acb9', '#c8d0da'],
    frame=['#7d949a', '#9ab0b5', '#b8cbcf', '#d6e6e8', '#f2fbfc'],
    glass=['#2a3b44', '#344a55', '#40596566', '#50707c', '#6d8f99'],
)
R['glass'] = ['#22313a', '#2b3d48', '#374d59', '#48636f', '#6a8c97']
TILES = {}


def save(name, label, rgb, n, e=None, alpha=None, sm=0.2, mt=0.0, s_map=None):
    if e is None: e = np.zeros((N, N))
    TILES[name] = dict(label=label, rgb=np.clip(rgb, 0, 255).astype(np.uint8), n=n, e=np.clip(e, 0, 1), alpha=alpha, sm=sm, mt=mt, s_map=s_map)


# ------------------------------------------------------------------ natural blocks
def stone_h(seed):
    r = np.random.default_rng(seed)
    grain = blur(r.random((N, N)), 0.5)
    h = 0.45 * fbm(seed, 2, 4) + 0.35 * fbm(seed + 7, 4, 3, pers=0.6) + 0.2 * grain
    f1, f2, _, _ = worley(seed + 3, 5)
    crack = (f2 - f1 < 0.6) & (fbm(seed + 9, 2, 2) > 0.7)
    return h - 0.2 * crack, crack


def stone_base(seed):
    r = np.random.default_rng(seed + 40)
    f1, _, cid, _ = worley(seed + 41, 26)
    ct = r.random(26)
    speck = np.where(f1 < 1.0, (ct[cid] - 0.5) * 0.28, 0)
    return 0.52 + 0.24 * (fbm(seed + 20, 2, 3) - 0.5) + speck + 0.07 * (r.random((N, N)) - 0.5)


def t_stone(seed=1):
    h, crack = stone_h(seed)
    t, n = shade(h, stone_base(seed), k=5, amt=0.55)
    save('stone', '돌', ramp(t - 0.08 * crack, R['stone']), n)


def t_deepslate(seed=2):
    h = 0.6 * fbm(seed, 2, 4, aniso=(3, 0.5)) + 0.4 * fbm(seed + 5, 8, 2)
    lines = np.abs(np.sin((np.arange(N)[:, None] + 3 * fbm(seed + 8, 2, 2)) * math.pi / 4)) < 0.15
    h = h - 0.2 * lines * (fbm(seed + 9, 4, 2) > 0.45)
    t, n = shade(h, 0.5 + 0.18 * (fbm(seed + 11, 2, 2) - 0.5), k=4, amt=0.45)
    save('deepslate', '심층암', ramp(t, R['deep']), n)


def t_dirt(seed=3):
    h = 0.6 * fbm(seed, 2, 4) + 0.2 * np.random.default_rng(seed).random((N, N))
    f1, _, cid, _ = worley(seed + 1, 16)
    rr = np.random.default_rng(seed + 2).random(16)
    peb = (f1 < 0.9 + 0.8 * rr[cid]) & (rr[cid] > 0.5)
    h = h + np.where(peb, 0.3, 0)
    base = 0.5 + 0.16 * (fbm(seed + 5, 2, 3) - 0.5)
    t, n = shade(h, base, k=3, amt=0.45)
    rgb = ramp(t, R['dirt'])
    rgb = np.where((peb & (rr[cid] > 0.78))[..., None], ramp(t * 0.8 + 0.2, R['stone']), rgb)
    save('dirt', '흙', rgb, n)


def grass_field(seed, strokes=380):
    r = np.random.default_rng(seed)
    h = 0.35 * fbm(seed, 2, 3)
    tone = 0.45 + 0.22 * (fbm(seed + 1, 2, 2) - 0.5)
    for _ in range(strokes):
        x, y = r.random() * N, r.random() * N
        ang = -math.pi / 2 + r.normal(0, 0.5); ln = r.uniform(1.5, 3.2)
        for s in range(int(ln * 2)):
            u = s / (ln * 2)
            px, py = int(x + math.cos(ang) * u * ln) % N, int(y + math.sin(ang) * u * ln) % N
            h[py, px] = max(h[py, px], 0.35 + 0.65 * u); tone[py, px] = 0.4 + 0.5 * u + r.normal(0, 0.03)
    return h, tone


def t_grass_top(seed=4):
    h, tone = grass_field(seed)
    t, n = shade(h, tone, k=2.5, amt=0.35, aos=1.0)
    save('grass_top', '풀 윗면', ramp(t, R['grass']), n)


def t_grass_side(seed=5):
    dirt = TILES['dirt']['rgb'].astype(float)
    hg, tg = grass_field(seed)
    tgs, ng = shade(hg, tg, k=2.5, amt=0.35)
    r = np.random.default_rng(seed)
    edge = 6 + 3.5 * vnoise(seed + 3, 1, 6)[0] + 1.5 * (vnoise(seed + 4, 1, 16)[0] - 0.5)
    for _ in range(6):
        x = r.integers(0, N); L2 = r.integers(2, 6)
        edge[x] = max(edge[x], edge[x] + L2); edge[(x + 1) % N] = max(edge[(x + 1) % N], edge[x] - 1)
    yy = np.arange(N)[:, None]
    m = yy < edge[None, :]
    depth = np.clip((edge[None, :] - yy) / 2.5, 0, 1)
    grass = ramp(tgs * (0.8 + 0.2 * depth) - 0.08 * (1 - depth), R['grass'])
    sh = np.clip(1 - (yy - edge[None, :]) / 3, 0, 1) * (yy >= edge[None, :])
    rgb = np.where(m[..., None], grass, dirt * (1 - 0.22 * sh[..., None]))
    save('grass_side', '풀 옆면', rgb, np.where(m[..., None], ng, TILES['dirt']['n']))


def t_cobble(seed=6):
    f1, f2, cid, _ = worley(seed, 11)
    r = np.random.default_rng(seed + 1)
    ch, ct = r.random(11), r.random(11)
    d = f2 - f1
    body = smooth(0.2, 3.0, d)
    h = body * (0.6 + 0.4 * ch[cid]) + 0.12 * fbm(seed + 3, 4, 2) * body
    mortar = d < 0.7
    base = np.where(mortar, 0.3, 0.42 + 0.3 * ct[cid] + 0.1 * (fbm(seed + 4, 4, 2) - 0.5))
    t, n = shade(h * 2, base, k=2.2, amt=0.5, aok=1.0, aos=1.2)
    save('cobble', '조약돌', ramp(np.maximum(t, 0.18), R['stone']), n)


def t_log_side(seed=7):
    h = 0.65 * fbm(seed, 2, 4, aniso=(0.5, 3)) + 0.35 * fbm(seed + 1, 8, 2, aniso=(0.25, 1))
    h = np.where(h < 0.38, h - 0.15, h)
    t, n = shade(h * 1.4, 0.45 + 0.3 * (h - 0.5), k=3.5, amt=0.5)
    save('log_side', '참나무 원목 옆면', ramp(t, R['bark']), n)


def t_log_top(seed=8):
    yy, xx = np.mgrid[0:N, 0:N] + 0.5
    dx, dy = xx - 16, yy - 16
    rr = np.sqrt(dx * dx + dy * dy) * 0.6 + np.maximum(np.abs(dx), np.abs(dy)) * 0.4 + 1.1 * (fbm(seed, 2, 2) - 0.5)
    ring = 0.5 + 0.5 * np.cos(rr * 2 * math.pi / 2.2)
    bark = rr > 14.2
    h = np.where(bark, 0.6 * fbm(seed + 3, 8, 2) + 0.4, 0.5 + 0.08 * ring)
    base = np.where(bark, 0.3 + 0.2 * fbm(seed + 4, 8, 2), 0.55 + 0.16 * ring - 0.08 * (rr / 14))
    t, n = shade(h, base, k=3, amt=0.3)
    save('log_top', '원목 윗면', np.where(bark[..., None], ramp(t, R['bark']), ramp(t, R['wood'])), n)


def t_planks(seed=9):
    yy, xx = np.mgrid[0:N, 0:N]
    board = yy // 8; ly = yy % 8
    tone = np.random.default_rng(seed).uniform(-0.07, 0.07, 4)
    joint = [(i * 11 + 5) % 32 for i in range(4)]
    grain = fbm(seed, 2, 3, aniso=(4, 0.25))
    ex = np.array([[min(abs(x - joint[b]), 32 - abs(x - joint[b])) for x in range(N)] for b in range(4)])[board, xx]
    e = np.minimum(np.minimum(ly + 0.5, 7.5 - ly), ex + 0.5)
    h = smooth(0, 1.5, e) * 0.8 + 0.12 * grain
    gap = (ly == 7) | (ex == 0)
    base = np.where(gap, 0.22, 0.56 + tone[board] + 0.14 * (grain - 0.5))
    t, n = shade(h * 2, base, k=2, amt=0.4)
    rgb = ramp(t, R['wood'])
    for b in range(4):
        x = (joint[b] + 2) % 32; rgb[b * 8 + 3, x] = hx('#4a4a52'); rgb[b * 8 + 4, x] = hx('#8a8a92')
    save('planks', '참나무 판자', rgb, n)


def t_mana_log(seed=10):
    h = 0.65 * fbm(seed, 2, 4, aniso=(0.5, 3)) + 0.35 * fbm(seed + 1, 8, 2, aniso=(0.25, 1))
    f1, f2, _, _ = worley(seed + 3, 5)
    v = np.clip(1 - (f2 - f1) / 1.0, 0, 1) * (fbm(seed + 6, 2, 2) > 0.4)
    t, n = shade(h * 1.3 + 0.3 * v, 0.45 + 0.3 * (h - 0.5), k=3.5, amt=0.45)
    rgb = ramp(t, R['mbark'])
    glow = np.clip(blur(v, 1.2) * 1.4, 0, 1)
    rgb = rgb * (1 - 0.3 * glow[..., None]) + np.array(hx('#3fe0d0')) * 0.3 * glow[..., None]
    rgb = np.where((v > 0.4)[..., None], ramp(0.4 + 0.6 * v, ['#2b928c', '#3fe0d0', '#b6fff6']), rgb)
    save('mana_log', '마력나무 원목', rgb, n, e=np.clip((v - 0.35) * 1.6, 0, 1))


def t_stone_bricks(seed=11):
    yy, xx = np.mgrid[0:N, 0:N]
    row = yy // 8; off = (row % 2) * 8
    lx = (xx + off) % 16; ly = yy % 8
    bid = row * 2 + ((xx + off) % 32) // 16
    btone = np.random.default_rng(seed).uniform(-0.08, 0.08, 8)
    e = np.minimum(np.minimum(lx - 0.5, 14.5 - lx), np.minimum(ly - 0.5, 6.5 - ly))
    e = e - 1.0 * (fbm(seed + 1, 4, 2) > 0.75) * (e < 1.2)
    mortar = e < 0
    det = fbm(seed + 2, 4, 3)
    h = np.where(mortar, 0.1, smooth(-0.5, 1.8, e) * (0.75 + 0.25 * det))
    base = np.where(mortar, 0.3 + 0.06 * det, 0.56 + btone[bid] + 0.12 * (det - 0.5))
    t, n = shade(h * 2, base, k=2, amt=0.4, aok=0.8)
    save('stone_bricks', '돌벽돌', ramp(np.maximum(t, 0.2), R['stone']), n)


def t_glass():
    yy, xx = np.mgrid[0:N, 0:N] + 0.5
    e = np.minimum(np.minimum(xx, N - xx), np.minimum(yy, N - yy))
    frame = e < 2
    t, n = shade(np.where(frame, smooth(0, 1, e), 0) * 2, np.where(frame, 0.65, 0.5), k=3, amt=0.6)
    glare = ((np.abs((xx - yy) - 5) < 1.1) | (np.abs((xx - yy) + 3) < 0.6)) & (xx + yy > 15) & (xx + yy < 35)
    alpha = np.where(frame, 255, np.where(glare, 150, 45 + 18 * (yy / N)))
    rgb = np.where(frame[..., None], ramp(t, R['frame']), np.where(glare[..., None], 255, ramp(0.75 + 0.2 * (1 - yy / N), R['frame'])))
    save('glass', '유리', rgb, n, alpha=alpha, sm=0.95)


# ------------------------------------------------------------------ ores: fill to ~25 % coverage
TARGET = 0.25


def nuggets(seed, rmin, rmax, squarish=0.5):
    r = np.random.default_rng(seed)
    hh = np.zeros((N, N)); mask = np.zeros((N, N), bool); tone = np.zeros((N, N))
    yy, xx = np.mgrid[0:N, 0:N] + 0.5
    for c in range(400):
        cx, cy = r.random() * N, r.random() * N
        for k in range(3):
            if mask.mean() >= TARGET: return hh, mask, tone
            x, y = (cx + r.normal(0, 2.2)) % N, (cy + r.normal(0, 2.2)) % N
            rad = r.uniform(rmin, rmax)
            dx = np.abs(xx - x); dx = np.minimum(dx, N - dx)
            dy = np.abs(yy - y); dy = np.minimum(dy, N - dy)
            d = np.maximum(dx, dy) * squarish + np.sqrt(dx * dx + dy * dy) * (1 - squarish)
            dome = np.sqrt(np.clip(rad * rad - d * d, 0, None)) / rad
            new = d < rad
            if (new & ndimage.binary_dilation(mask, iterations=1)).sum() > 0.12 * new.sum(): continue
            upd = dome > hh
            hh = np.where(upd, dome, hh); tone = np.where(upd, r.uniform(-0.08, 0.08), tone)
            mask |= d < rad
    return hh, mask, tone


def ore_compose(seed, mask, H_ore, t_ore, pal, sm=0.3, mt=0.0, emissive=None):
    h, crack = stone_h(seed)
    socket = (blur(mask.astype(float), 0.8) > 0.08) & ~mask      # darker ring so each ore reads on its own
    H = np.where(mask, H_ore, h * 0.6)
    t_s, n = shade(H, stone_base(seed) - 0.14 * socket, k=3.5, amt=0.45)
    rgb = np.where(mask[..., None], ramp(t_ore, R[pal]), ramp(t_s, R['stone']))
    s_map = np.dstack([np.where(mask, sm, 0.15), np.where(mask, mt, 0), emissive if emissive is not None else np.zeros((N, N))])
    return rgb, n, s_map


def t_ore(name, label, pal, seed, rmin, rmax, spec=False, sm=0.3, mt=0.0, sq=0.5):
    hh, mask, tone = nuggets(seed + 50, rmin, rmax, sq)
    H_ore = 0.35 + hh
    d, n = diffuse(H_ore * 1.6, 3)
    t = np.clip(0.48 + tone + 0.7 * (d - L[2]) + 0.15 * hh, 0.05, 1)
    rim = mask & ~ndimage.binary_erosion(mask)
    t = np.where(rim & (d < L[2]), t - 0.12, t)
    if spec:
        sp = np.clip(normals(H_ore * 1.6, 3) @ H, 0, 1) ** 18
        t = np.clip(t + 0.6 * sp, 0, 1)
    rgb, n, s_map = ore_compose(seed, mask, H_ore, t, pal, sm, mt)
    save(name, label, rgb, n, s_map=s_map)
    return mask.mean()


def facet_gems(seed, rmin, rmax):
    S = 8
    img = Image.new('L', (N * S, N * S), 0); dr = ImageDraw.Draw(img)
    r = np.random.default_rng(seed)
    m = np.zeros((N, N), bool)
    while m.mean() < TARGET:
        cx, cy = r.random() * N, r.random() * N
        rad = r.uniform(rmin, rmax); a = r.uniform(0, math.pi)
        yy_, xx_ = np.mgrid[0:N, 0:N] + 0.5
        ddx = np.abs(xx_ - cx); ddx = np.minimum(ddx, N - ddx); ddy = np.abs(yy_ - cy); ddy = np.minimum(ddy, N - ddy)
        if (np.hypot(ddx, ddy) < rad + 1.2)[m].any(): continue
        pts = [(cx + rad * math.cos(a + k * math.pi / 2) * (1 if k % 2 == 0 else 0.72),
                cy + rad * math.sin(a + k * math.pi / 2) * (1 if k % 2 == 0 else 0.72)) for k in range(4)]
        for ox in (-N, 0, N):
            for oy in (-N, 0, N):
                for k in range(4):
                    p0, p1 = pts[k], pts[(k + 1) % 4]
                    mx, my = (p0[0] + p1[0]) / 2 - cx, (p0[1] + p1[1]) / 2 - cy
                    lit = -(mx * L[0] + my * L[1]) / (math.hypot(mx, my) + 1e-6)
                    dr.polygon([((cx + ox) * S, (cy + oy) * S), ((p0[0] + ox) * S, (p0[1] + oy) * S), ((p1[0] + ox) * S, (p1[1] + oy) * S)], fill=int(120 + 115 * lit))
        m = np.array(img.resize((N, N), Image.NEAREST)) > 0
    a = np.array(img.resize((N, N), Image.NEAREST), float) / 255
    return a, a > 0


def t_diamond(seed=40):
    fac, mask = facet_gems(seed + 1, 2.4, 3.6)
    rgb, n, s_map = ore_compose(seed, mask, 0.9 + 0 * fac, fac, 'dia', sm=0.9)
    save('diamond_ore', '다이아몬드 광석', rgb, n, s_map=s_map)
    return mask.mean()


def crystals(seed, lmin, lmax, wmin, wmax, spread=0.5):
    S = 8
    fac = Image.new('L', (N * S, N * S), 0); lng = Image.new('L', (N * S, N * S), 0)
    df, dl = ImageDraw.Draw(fac), ImageDraw.Draw(lng)
    r = np.random.default_rng(seed)
    m = np.zeros((N, N), bool); tries = 0
    items = []
    while m.mean() < TARGET and tries < 600:
        tries += 1
        cx, cy = r.random() * N, 6 + r.random() * 24
        ang = -math.pi / 2 + r.normal(0, spread)
        ln, w = r.uniform(lmin, lmax), r.uniform(wmin, wmax)
        ux, uy = math.cos(ang), math.sin(ang); vx, vy = -uy, ux
        def P(u, v): return (cx + ux * u + vx * v, cy + uy * u + vy * v)
        body = ln - w * 0.9
        tmp = Image.new('L', (N * S, N * S), 0); dt = ImageDraw.Draw(tmp)
        outline_poly = [P(0, -w / 2), P(body, -w / 2), P(ln, 0), P(body, w / 2), P(0, w / 2)]
        for ox in (-N, 0, N):
            for oy in (-N, 0, N):
                dt.polygon([((x + ox) * S, (y + oy) * S) for x, y in outline_poly], fill=255)
        mn = np.array(tmp.resize((N, N), Image.NEAREST)) > 0
        if (mn & ndimage.binary_dilation(m, iterations=1)).sum() > 0.1 * max(1, mn.sum()): continue
        for fi, (a0, a1) in enumerate([(-0.5, -0.17), (-0.17, 0.17), (0.17, 0.5)]):
            nrm = (vx * (fi - 1) * 0.8, vy * (fi - 1) * 0.8)
            lit = np.clip(0.55 - (nrm[0] * L[0] + nrm[1] * L[1]) * 0.9, 0.15, 1)
            poly = [P(0, a0 * w), P(body, a0 * w), P(body, a1 * w), P(0, a1 * w)]
            tip = [P(body, a0 * w), P(ln, 0), P(body, a1 * w)]
            for ox in (-N, 0, N):
                for oy in (-N, 0, N):
                    df.polygon([((x + ox) * S, (y + oy) * S) for x, y in poly], fill=int(lit * 200 + 30))
                    df.polygon([((x + ox) * S, (y + oy) * S) for x, y in tip], fill=int(min(1, lit + 0.25) * 200 + 40))
        for k in range(6):
            u0, u1 = ln * k / 6, ln * (k + 1) / 6
            strip = [P(u0, -w / 2), P(u1, -w / 2), P(u1, w / 2), P(u0, w / 2)]
            for ox in (-N, 0, N):
                for oy in (-N, 0, N):
                    dl.polygon([((x + ox) * S, (y + oy) * S) for x, y in strip], fill=int(40 + 215 * k / 5))
        m = np.array(fac.resize((N, N), Image.NEAREST)) > 0
    f = np.array(fac.resize((N, N), Image.NEAREST), float) / 255
    l = np.array(lng.resize((N, N), Image.NEAREST), float) / 255
    return f, f > 0, l


def t_mana(name, label, pal, seed, size, glow, deep=False, cloudy=0.0):
    f, m, lng = crystals(seed + 30, *size)
    if deep:
        h = 0.6 * fbm(seed, 2, 4, aniso=(3, 0.5)) + 0.4 * fbm(seed + 5, 8, 2)
        base, bramp = 0.5 + 0.18 * (fbm(seed + 11, 2, 2) - 0.5), R['deep']
    else:
        h, _ = stone_h(seed); base, bramp = stone_base(seed), R['stone']
    socket = (blur(m.astype(float), 0.8) > 0.08) & ~m
    t_s, n = shade(np.where(m, 0.8 + 0.4 * lng, h * 0.6), base - 0.14 * socket, k=3.5, amt=0.45)
    rgb_s = ramp(t_s, bramp)
    halo = np.clip(blur(m.astype(float), 1.8) * 1.3, 0, 1) * glow * (~m)
    gcol = np.array(hx(R[pal][3]))
    rgb_s = rgb_s * (1 - 0.35 * halo[..., None]) + gcol * 0.35 * halo[..., None]
    tc = np.clip(0.18 + 0.6 * f + 0.28 * lng, 0, 1)
    if cloudy: tc = tc * (1 - cloudy) + cloudy * (0.35 + 0.3 * fbm(seed + 70, 8, 2))
    rgb = np.where(m[..., None], ramp(tc, R[pal]), rgb_s)
    e = np.where(m, glow * (0.35 + 0.65 * lng), halo * 0.2)
    save(name, label, rgb, n, e=e, s_map=np.dstack([np.where(m, 0.9, 0.15), np.zeros((N, N)), e]))
    return m.mean()


# ------------------------------------------------------------------ metal plates
def rivet(h, cx, cy, rad=1.6):
    yy, xx = np.mgrid[0:N, 0:N] + 0.5
    d = np.hypot(xx - cx, yy - cy)
    return np.maximum(h, np.where(d < rad, 0.55 + np.sqrt(np.clip(rad * rad - d * d, 0, None)) / rad * 0.9, 0)), d < rad


def t_plate(name, label, pal, seed, panels=1, rivets=(), trim=None, engrave=False):
    yy, xx = np.mgrid[0:N, 0:N] + 0.5
    step = N // panels
    lx, ly = xx % step, yy % step
    e = np.minimum(np.minimum(lx, step - lx), np.minimum(ly, step - ly))
    brushed = 0.6 * fbm(seed, 1, 3, aniso=(1, 0.5)) + 0.4 * vnoise(seed + 9, 32, 2)
    h = smooth(0, 1.6, e) * 0.55 + 0.05 * brushed
    grime = smooth(0.6, 0.85, fbm(seed + 3, 2, 2)) * (1 - smooth(0, 3, e))
    if trim:
        te = np.minimum(np.minimum(xx, N - xx), np.minimum(yy, N - yy)); tm = (te > 1.5) & (te < 4.2)
        h = np.where(tm, 0.8 + 0.3 * smooth(1.5, 2.3, te) - 0.3 * smooth(3.4, 4.2, te), h)
    if engrave:
        dd = np.abs(xx - 16) + np.abs(yy - 16)
        h = h - 0.4 * ((np.abs(dd - 7) < 0.6) | (np.abs(dd - 3.5) < 0.5))
    rm = np.zeros((N, N), bool)
    for cx, cy in rivets:
        h, m = rivet(h, cx, cy); rm |= m
    t, n = shade(h * 2, 0.56 + 0.1 * (brushed - 0.5) - 0.18 * grime, k=2.2, amt=0.7, aok=1.0)
    sp = np.clip(n @ H, 0, 1) ** 20
    t = np.clip(t + 0.35 * sp + 0.12 * ((e < 0.9) & (fbm(seed + 8, 8, 2) > 0.45)), 0, 1)
    rgb = ramp(t, R[pal])
    if trim:
        te = np.minimum(np.minimum(xx, N - xx), np.minimum(yy, N - yy)); tm = (te > 1.5) & (te < 4.2)
        rgb = np.where(tm[..., None], ramp(t, R[trim]), rgb)
    save(name, label, rgb, n, sm=0.6, mt=1.0, s_map=np.dstack([0.6 - 0.3 * grime, np.ones((N, N)), np.zeros((N, N))]))


# ------------------------------------------------------------------ items (alpha, tinted outline)
def outline(rgb, mask, col):
    edge = ndimage.binary_dilation(mask) & ~mask
    rgb = np.where(edge[..., None], np.array(hx(col)), rgb)
    return rgb, np.where(mask | edge, 255, 0)


def diag_frame(x0=2.0, y0=30.0):
    yy, xx = np.mgrid[0:N, 0:N] + 0.5
    ux, uy = 1 / math.sqrt(2), -1 / math.sqrt(2)
    u = (xx - x0) * ux + (yy - y0) * uy
    v = (xx - x0) * (-uy) + (yy - y0) * ux
    return u, v, np.array([-uy, ux])        # v axis (perpendicular, towards bottom-right)


def cyl_light(nv, pdir, spec_k=24):
    """shading of a cylinder whose cross-section coordinate is nv in [-1,1] along image direction pdir."""
    nz = np.sqrt(np.clip(1 - nv ** 2, 0, 1))
    n = np.dstack([pdir[0] * nv, pdir[1] * nv, nz])
    dif = np.clip(n @ L, 0, 1)
    spc = np.clip(n @ H, 0, 1) ** spec_k
    rimv = (1 - nz) ** 3
    return dif, spc, rimv, n


def sword(name, label, blade, guard, grip, pommel, fuller_glow=None, gem=None, tips=None):
    u, v, pdir = diag_frame(2.2, 29.8)
    Lb0, Lt = 10.5, 37.2
    hw = np.where(u < Lt - 5, 1.55 - 0.25 * (u - Lb0) / (Lt - Lb0), 1.35 * np.clip((Lt - u) / 5, 0, 1))
    bl = (u > Lb0) & (u < Lt) & (np.abs(v) < hw + 0.05)
    gd = (u > 8.6) & (u < 10.6) & (np.abs(v) < 4.6)
    gp = (u > 3.2) & (u <= 8.6) & (np.abs(v) < 0.95)
    pm = np.hypot(u - 2.2, v) < 1.6
    mask = bl | gd | gp | pm
    # blade: two bevel facets, a narrow edge highlight on the lit side
    nv = np.clip(v / np.maximum(hw, 0.1), -1, 1)
    side = np.where(nv < 0, -1.0, 1.0)
    n_b = np.dstack([pdir[0] * side * 0.55, pdir[1] * side * 0.55, np.ones_like(nv)])
    n_b /= np.linalg.norm(n_b, axis=2, keepdims=True)
    tb = np.clip(0.36 + 0.95 * ((n_b @ L) - L[2]) + 0.1 * (u - Lb0) / (Lt - Lb0), 0, 1)
    tb = np.where((np.abs(np.abs(v) - hw) < 0.55) & (v < 0), np.minimum(1, tb + 0.35), tb)
    rgb = np.zeros((N, N, 3))
    rgb = np.where(bl[..., None], ramp(tb, R[blade]), rgb)
    e = np.zeros((N, N))
    if fuller_glow:
        fl = bl & (np.abs(v) < 0.45) & (u > Lb0 + 1.5) & (u < Lt - 8)
        rgb = np.where(fl[..., None], ramp(0.55 + 0.3 * (u - Lb0) / (Lt - Lb0), R[fuller_glow]), rgb)
        e = np.where(fl, 0.6, 0)
    dif, spc, rimv, _ = cyl_light(np.clip((u - 9.6) / 1.0, -1, 1) * 0 + np.clip(v / 4.6, -1, 1) * 0.3, pdir)
    tg = np.clip(0.45 + 0.8 * (dif - L[2]) + 0.4 * spc - 0.18 * (np.abs(v) > 3.2), 0, 1)
    rgb = np.where(gd[..., None], ramp(tg, R[guard]), rgb)
    if tips:
        tp = gd & (np.abs(v) > 3.3)
        rgb = np.where(tp[..., None], ramp(np.clip(tg + 0.1, 0, 1), R[tips]), rgb)
    if gem:
        gm = gd & (np.abs(v) < 0.9)
        rgb = np.where(gm[..., None], ramp(0.55 + 0.3 * (v < 0), R[gem]), rgb); e = np.where(gm, 0.5, e)
    wrap = ((u + v * 0.9) % 1.6) < 0.7
    difg, spcg, _, _ = cyl_light(np.clip(v / 0.95, -1, 1), pdir, 10)
    rgb = np.where(gp[..., None], ramp(np.clip(0.35 + 0.6 * (difg - L[2]) - 0.12 * wrap + 0.2 * spcg, 0, 1), R[grip]), rgb)
    rgb = np.where(pm[..., None], ramp(np.clip(0.55 + 0.6 * (difg - L[2]) + 0.4 * spcg, 0, 1), R[pommel]), rgb)
    rgb, alpha = outline(rgb, mask, '#262a33')
    Hh = np.where(mask, 0.5, 0)
    save(name, label, rgb, normals(Hh, 2), e=e, alpha=alpha, sm=0.7, mt=1.0)


def battery():
    """diagonal mana battery: brass caps (knurled), glass tube with glowing liquid, specular highlights."""
    u, v, pdir = diag_frame(4.2, 27.8)
    rad = 5.2
    Ltot = 32.5
    inside = (np.abs(v) < rad) & (u > 1.5) & (u < Ltot - 2.2)
    cap_a = inside & (u < 7.5)
    cap_b = inside & (u > Ltot - 8.0)
    tube = inside & ~cap_a & ~cap_b & (np.abs(v) < rad - 0.9)
    term = (np.abs(v) < 1.8) & (u >= Ltot - 2.2) & (u < Ltot)
    foot = (np.abs(v) < 2.6) & (u > 0) & (u <= 1.5)
    mask = cap_a | cap_b | tube | term | foot
    nv = np.clip(v / rad, -1, 1)
    dif, spc, rimv, _ = cyl_light(nv, pdir, 28)
    # knurled caps: ridges along the axis + bevelled rims
    ridge = (np.floor(u * 1.4) % 2 == 0)
    bev = np.minimum(np.abs(u - 1.5), np.abs(u - 7.5))
    bevb = np.minimum(np.abs(u - (Ltot - 8.0)), np.abs(u - (Ltot - 2.2)))
    tcap = 0.36 + 0.95 * (dif - L[2]) + 0.4 * spc - 0.16 * ridge - 0.12 * (nv > 0.55) + 0.08 * rimv * (nv < 0)
    tcap = np.where(cap_a & (bev < 0.7), tcap + 0.15, tcap)
    tcap = np.where(cap_b & (bevb < 0.7), tcap + 0.15, tcap)
    rgb = np.zeros((N, N, 3))
    rgb = np.where((cap_a | cap_b)[..., None], ramp(np.clip(tcap, 0, 1), R['brass']), rgb)
    difs, spcs, _, _ = cyl_light(np.clip(v / 1.8, -1, 1), pdir, 16)
    rgb = np.where((term | foot)[..., None], ramp(np.clip(0.45 + 0.8 * (difs - L[2]) + 0.5 * spcs, 0, 1), R['copper']), rgb)
    # glass tube: dark tinted glass, glowing liquid core, sharp specular streak + soft secondary reflection
    liq = tube & (np.abs(v) < rad - 2.6) & (u > 9.2)
    lv = np.clip(v / (rad - 2.6), -1, 1)
    difl, spcl, _, _ = cyl_light(lv, pdir, 12)
    t_glass = 0.22 + 0.4 * (dif - L[2]) + 0.55 * rimv
    rgb = np.where(tube[..., None], ramp(np.clip(t_glass, 0, 1), R['glass']), rgb)
    wave = 9.2 + 0.5 * np.sin(v * 1.7)
    liq &= u > wave
    t_liq = np.clip(0.3 + 0.55 * (1 - np.abs(lv)) ** 1.5 + 0.15 * difl + 0.06 * np.sin(u * 1.3 + v), 0, 1)
    rgb = np.where(liq[..., None], ramp(t_liq, R['m_clear']), rgb)
    r = np.random.default_rng(5)
    bub = np.zeros((N, N), bool)
    for _ in range(5):
        bu, bvv = r.uniform(11, 23), r.uniform(-1.8, 1.8)
        bub |= np.hypot(u - bu, v - bvv) < r.uniform(0.5, 0.9)
    rgb = np.where((bub & liq)[..., None], np.array(hx('#d8fff8')), rgb)
    streak = tube & (np.abs(nv + 0.55) < 0.12)
    rgb = np.where(streak[..., None], np.array(hx('#f4ffff')), rgb)
    soft = tube & (np.abs(nv - 0.62) < 0.1)
    rgb = np.where(soft[..., None], rgb * 0.6 + np.array(hx('#b8d8e0')) * 0.4, rgb)
    # contact shadow of caps on the glass
    cs = tube & ((np.abs(u - 7.5) < 0.9) | (np.abs(u - (Ltot - 8.0)) < 0.9))
    rgb = np.where(cs[..., None], rgb * 0.72, rgb)
    rgb, alpha = outline(rgb, mask, '#2d2a2a')
    e = np.where(liq, 0.85, 0) + np.where(bub & liq, 0.15, 0)
    save('mana_battery', '마력 배터리', rgb, normals(np.where(mask, np.sqrt(1 - nv ** 2), 0), 2), e=e, alpha=alpha, sm=0.7, mt=0.6)


# ------------------------------------------------------------------ build / write / sheets
def build():
    t_stone(); t_deepslate(); t_dirt(); t_grass_top(); t_grass_side(); t_cobble()
    t_log_side(); t_log_top(); t_planks(); t_mana_log(); t_stone_bricks(); t_glass()
    cov = {}
    cov['coal'] = t_ore('coal_ore', '석탄 광석', 'coal', 31, 1.8, 2.8, sq=0.35)
    cov['iron'] = t_ore('iron_ore', '철 광석', 'iron', 32, 1.7, 2.6, sq=0.55)
    cov['gold'] = t_ore('gold_ore', '금 광석', 'gold', 33, 1.6, 2.4, spec=True, sm=0.8, mt=1.0, sq=0.6)
    cov['dia'] = t_diamond()
    cov['m_dull'] = t_mana('mana_dull', '탁한 마석', 'm_dull', 51, (5, 8, 2.6, 3.6), 0.3, cloudy=0.2)
    cov['m_clear'] = t_mana('mana_clear', '맑은 마석', 'm_clear', 52, (6, 10, 2.8, 3.8), 0.7)
    cov['m_pure'] = t_mana('mana_pure', '순수 마석', 'm_pure', 53, (7, 12, 3.0, 4.2), 1.0)
    cov['m_abyss'] = t_mana('mana_abyss', '심연 마석', 'm_abyss', 54, (6, 11, 2.8, 4.0), 0.8, deep=True)
    t_plate('steel_plate', '강철판', 'steel', 61)
    t_plate('rivet_plate', '리벳 강판', 'steel', 62, panels=2, rivets=[(x, y) for x in (3, 13, 19, 29) for y in (3, 13, 19, 29)])
    t_plate('blackiron_plate', '흑철판', 'black', 63, trim='bronze', engrave=True, rivets=[(3.5, 3.5), (28.5, 3.5), (3.5, 28.5), (28.5, 28.5)])
    sword('steel_sword', '강철 검', 'steel', 'steel', 'leather', 'steel')
    sword('aurora_sword', '오로라 합금 검', 'aurora', 'gunmetal', 'leather', 'brass', fuller_glow='m_clear', gem='m_abyss', tips='brass')
    battery()
    return cov


def write():
    for k, t in TILES.items():
        rgb = t['rgb']
        img = Image.fromarray(np.dstack([rgb, t['alpha'].astype(np.uint8)]), 'RGBA') if t['alpha'] is not None else Image.fromarray(rgb, 'RGB')
        img.save(f'{OUT}/{k}.png')
        n = t['n']
        Image.fromarray(((np.dstack([n[..., 0], -n[..., 1], n[..., 2]]) * 0.5 + 0.5) * 255).astype(np.uint8)).save(f'{OUT}/{k}_n.png')
        if t['e'].max() > 0:
            Image.fromarray((t['e'] * 255).astype(np.uint8), 'L').save(f'{OUT}/{k}_e.png')
            Image.fromarray((rgb.astype(float) * np.clip(t['e'] * 1.4, 0, 1)[..., None]).astype(np.uint8)).save(f'{OUT}/{k}_ec.png')
        s = t['s_map'] if t['s_map'] is not None else np.dstack([np.full((N, N), t['sm']), np.full((N, N), t['mt']), t['e']])
        Image.fromarray((np.clip(s, 0, 1) * 255).astype(np.uint8)).save(f'{OUT}/{k}_s.png')


FONT = '/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc'
FONTB = '/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc'


def checker(w, h, c=16):
    a = Image.new('RGB', (w, h), (58, 60, 66)); d = ImageDraw.Draw(a)
    for y in range(0, h, c):
        for x in range(0, w, c):
            if (x // c + y // c) % 2: d.rectangle((x, y, x + c - 1, y + c - 1), fill=(70, 72, 78))
    return a


def sheet(scale=8, cols=6):
    keys = list(TILES); cell = N * scale; pad, lab = 28, 34
    rows = (len(keys) + cols - 1) // cols
    im = Image.new('RGB', (pad + cols * (cell + pad), 90 + rows * (cell + lab + pad)), (30, 31, 35))
    d = ImageDraw.Draw(im)
    d.text((pad, 24), '프로젝트 오로라 — 텍스처 샘플 v3 (32×32, 8배 확대)', font=ImageFont.truetype(FONTB, 30), fill=(232, 230, 223))
    f = ImageFont.truetype(FONT, 20)
    for i, k in enumerate(keys):
        t = TILES[k]; x = pad + (i % cols) * (cell + pad); y = 90 + (i // cols) * (cell + lab + pad)
        src = Image.open(f'{OUT}/{k}.png').convert('RGBA').resize((cell, cell), Image.NEAREST)
        bg = checker(cell, cell) if t['alpha'] is not None else Image.new('RGB', (cell, cell)); bg.paste(src, (0, 0), src)
        im.paste(bg, (x, y))
        tw = d.textlength(t['label'], font=f); d.text((x + (cell - tw) / 2, y + cell + 6), t['label'], font=f, fill=(200, 202, 210))
    im.save(f'{D}/out/texture_sheet_32.png')


def tiling(names=('stone', 'cobble', 'stone_bricks', 'dirt', 'grass_top', 'planks', 'deepslate', 'iron_ore', 'mana_clear', 'rivet_plate'), scale=4):
    cols = 5; cell = N * 3 * scale; pad = 20; rows = (len(names) + cols - 1) // cols
    im = Image.new('RGB', (pad + cols * (cell + pad), 70 + rows * (cell + pad + 30)), (30, 31, 35)); d = ImageDraw.Draw(im)
    d.text((pad, 18), '3×3 타일링 검사 (4배)', font=ImageFont.truetype(FONTB, 26), fill=(232, 230, 223))
    f = ImageFont.truetype(FONT, 18)
    for i, k in enumerate(names):
        src = Image.open(f'{OUT}/{k}.png').convert('RGB'); big = Image.new('RGB', (N * 3, N * 3))
        for a in range(3):
            for b in range(3): big.paste(src, (a * N, b * N))
        x = pad + (i % cols) * (cell + pad); y = 70 + (i // cols) * (cell + pad + 30)
        im.paste(big.resize((cell, cell), Image.NEAREST), (x, y)); d.text((x, y + cell + 4), TILES[k]['label'], font=f, fill=(200, 202, 210))
    im.save(f'{D}/out/tiling_check_32.png')


if __name__ == '__main__':
    os.makedirs(os.path.join(D, 'out'), exist_ok=True)
    cov = build(); write(); sheet(); tiling()
    darkest = {k: int(np.min(np.array(Image.open(f'{OUT}/{k}.png').convert('L')))) for k in ('stone', 'cobble', 'stone_bricks', 'dirt', 'deepslate', 'coal_ore')}
    print('ore coverage', {k: round(v, 3) for k, v in cov.items()})
    print('darkest luma', darkest)
