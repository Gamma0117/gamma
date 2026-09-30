"""Character skin at 32 px per block (2 px per model unit). Head is 7 units (14 px faces).
Also dummy, crack stages (32 px) and particle colours."""
import json, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from scipy import ndimage
from PIL import Image
from tex32 import ramp, hx, R, OUT as TEXDIR

D = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(D, 'skin32'); os.makedirs(OUT, exist_ok=True)
RNG = np.random.default_rng(7)
R2 = dict(
    skin=['#6a4030', '#8e5a42', '#b0785a', '#cc9570', '#e3b28c', '#f3cfae', '#fde6cf'],
    hair=['#2e1d14', '#3d281b', '#503523', '#65452e', '#7c583a', '#946d4a'],
    tunic=['#233049', '#2b3a58', '#34476a', '#40567e', '#4e6893', '#6481aa', '#819bc0'],
    trim=['#5a4218', '#7a5b20', '#9c7a2e', '#c29c45', '#e2c173'],
    pants=['#3a332c', '#463e35', '#524940', '#60564b', '#6f6457', '#827666'],
    leather=R['leather'] + ['#9a6b45'],
    boot=['#33241b', '#3f2c21', '#4c3628', '#5a4030', '#6a4d3a', '#7e5d46'],
    steel=R['steel'],
    burlap=['#5a4526', '#72592f', '#8a6d3b', '#a3854c', '#bb9c60', '#d2b879'],
    red=['#5a1a1a', '#7a2020', '#9c2a2a', '#c23a34', '#dc5a4a'],
)


def nz(h, w, s=0.6): return ndimage.gaussian_filter(RNG.random((h, w)), s)


def vol(h, w, top=0.1, bottom=-0.12, side=-0.07):
    yy, xx = np.mgrid[0:h, 0:w]
    ex = np.minimum(xx, w - 1 - xx)
    return top + (bottom - top) * yy / max(1, h - 1) + side * (ex < 1)


def save(name, rgb): Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8)).save(f'{OUT}/{name}.png')


def quilt(h, w, ox=0):
    yy, xx = np.mgrid[0:h, 0:w]
    a, b = (xx + ox + yy) % 5, (xx + ox - yy) % 5
    line = (a == 0) | (b == 0)
    return 0.54 - 0.14 * line + 0.05 * (nz(h, w) - 0.5)


def tunic(w, h, face):
    yy, xx = np.mgrid[0:h, 0:w]
    rgb = ramp(quilt(h, w, 2 if face in ('left', 'right') else 0) + vol(h, w), R2['tunic'])
    rgb = np.where(((yy < 1) | (yy >= h - 2))[..., None], ramp(0.6 - 0.2 * (yy == h - 1) + 0 * xx, R2['trim']), rgb)
    belt = (yy >= 16) & (yy <= 18)
    rgb = np.where(belt[..., None], ramp(0.5 + 0.15 * (yy == 16) - 0.15 * (yy == 18) + 0 * xx, R2['leather']), rgb)
    if face == 'front':
        bk = (np.abs(xx - w / 2 + 0.5) <= 1.5) & (yy >= 15) & (yy <= 19)
        rgb = np.where(bk[..., None], ramp(0.65 + 0.2 * (xx < w / 2) - 0.15 * (yy == 19), R2['trim']), rgb)
    if face in ('front', 'back'):
        sg = 1 if face == 'front' else -1
        dd = (xx - (w * 0.2 if sg > 0 else w * 0.8)) * sg - yy * 0.72
        strap = (np.abs(dd) < 1.4) & (yy > 0) & (yy < 16)
        rgb = np.where(strap[..., None], ramp(0.55 + 0.2 * (dd < -0.6) - 0.2 * (dd > 0.6) + 0 * xx, R2['leather']), rgb)
    return rgb


def arm(w, h, face):
    yy, xx = np.mgrid[0:h, 0:w]
    rgb = ramp(quilt(h, w, 1) + vol(h, w, 0.06, -0.04), R2['tunic'])
    edge = 6 + np.cos((xx - w / 2 + 0.5) / w * math.pi)
    pl = yy < edge
    tp = 0.62 + 0.22 * (xx < w * 0.35) - 0.18 * (xx > w * 0.7) - 0.25 * (np.abs(yy - edge) < 0.8) + 0.18 * (yy < 1)
    rgb = np.where(pl[..., None], ramp(tp, R2['steel']), rgb)
    rgb = np.where((yy == 11)[..., None], ramp(0.6 + 0 * xx, R2['trim']), rgb)
    br = (yy >= 12) & (yy <= 19)
    rgb = np.where(br[..., None], ramp(0.5 + 0.06 * nz(h, w) + vol(h, w, 0.04, -0.04), R2['leather']), rgb)
    rgb = np.where(((yy == 13) | (yy == 18))[..., None], ramp(0.62 + 0.2 * (xx < w / 2), R2['steel']), rgb)
    gl = yy >= 20
    tg = 0.4 + 0.06 * nz(h, w) + 0.08 * (yy == 20) - 0.08 * (yy >= 23)
    if face == 'front': tg = tg - 0.14 * (((xx % 2) == 0) & (yy >= 21))
    return np.where(gl[..., None], ramp(tg, R2['leather']), rgb)


def leg(w, h, face):
    yy, xx = np.mgrid[0:h, 0:w]
    t = 0.5 + 0.06 * nz(h, w) + vol(h, w, 0.05, -0.08) - 0.1 * (xx == (w // 2 if face in ('left', 'right') else 0))
    rgb = ramp(t, R2['pants'])
    if face == 'front':
        knee = (yy >= 9) & (yy <= 12) & (np.abs(xx - w / 2 + 0.5) <= 2.5)
        rgb = np.where(knee[..., None], ramp(0.55 - 0.15 * ((yy == 9) | (yy == 12)) + 0 * xx, R2['leather']), rgb)
    boot = yy >= 13
    tb = 0.5 + 0.05 * nz(h, w) + vol(h, w, 0.1, -0.1) - 0.12 * ((yy == 15) & ((xx + yy) % 3 < 2))
    rgb = np.where(boot[..., None], ramp(tb, R2['boot']), rgb)
    rgb = np.where((yy == 13)[..., None], ramp(0.62 + 0 * xx, R2['boot']), rgb)
    rgb = np.where((yy == 17)[..., None], ramp(0.35 + 0 * xx, R2['leather']), rgb)
    if face in ('left', 'right'): rgb[17, w // 2] = hx(R2['trim'][3])
    rgb = np.where((yy >= 22)[..., None], ramp(0.18 + 0.1 * (yy == 22) + 0 * xx, R2['boot']), rgb)
    if face == 'front': rgb = np.where(((yy >= 19) & (yy <= 21))[..., None], ramp(0.62 + 0.08 * (yy == 19) - 0.1 * (xx > w * 0.7), R2['boot']), rgb)
    return rgb


def hair(h, w, dirn='v'):
    s = ndimage.gaussian_filter(RNG.random((h, w)), (1.2, 0.3) if dirn == 'v' else (0.3, 1.2))
    return 0.48 + 0.8 * (s - 0.5) + vol(h, w, 0.08, -0.08)


def head(face, S=14):
    yy, xx = np.mgrid[0:S, 0:S]
    rgb = ramp(0.62 + 0.03 * nz(S, S, 0.8) + vol(S, S, 0.04, -0.1, -0.05), R2['skin'])
    hr = ramp(hair(S, S), R2['hair'])
    if face == 'front':
        fringe = 3 + np.array([1, 1, 0, 1, 2, 1, 0, 0, 1, 2, 1, 0, 1, 1])
        hm = (yy < fringe[None, :]) | (((xx < 1) | (xx > 12)) & (yy < 7))
        rgb = np.where(((yy >= fringe[None, :]) & (yy < fringe[None, :] + 1))[..., None], rgb * 0.88, rgb)
        for x0 in (2, 9):
            rgb[5, x0:x0 + 3] = ramp(np.array([[0.3, 0.25, 0.35]]), R2['hair'])[0]
        for x0, ix in ((2, 3), (9, 9)):
            rgb[6:8, x0:x0 + 3] = hx('#eef0f2')
            rgb[6:8, ix:ix + 2] = hx('#2f5f8a'); rgb[7, ix:ix + 2] = hx('#1d3b5c')
            rgb[6, ix] = hx('#ffffff') if ix == 3 else hx('#4a7aa5')
        rgb[8:10, 7] = ramp(np.full((2, 1), 0.45), R2['skin'])[:, 0]
        rgb[8, 6] = ramp(np.full((1, 1), 0.72), R2['skin'])[0, 0]
        rgb[11, 5:9] = hx('#8a4a38'); rgb[11, 6:8] = hx('#6e3326')
        rgb = np.where(hm[..., None], hr, rgb)
    elif face in ('left', 'right'):
        bx = xx if face == 'right' else (S - 1 - xx)
        hm = (yy < 5) | ((bx < 6) & (yy < 11)) | ((bx < 8) & (yy < 7))
        ear = (np.abs((S - 1 - bx) - 7) < 1.2) & (yy >= 7) & (yy <= 9)
        rgb = np.where(ear[..., None], ramp(0.5 + 0.1 * (yy == 7) + 0 * xx, R2['skin']), rgb)
        rgb = np.where(hm[..., None], hr, rgb)
    elif face == 'back':
        rgb = np.where((yy < 12)[..., None], hr, rgb)
    elif face == 'top':
        rgb = ramp(hair(S, S, 'h') + 0.06, R2['hair'])
    elif face == 'bottom':
        rgb = ramp(np.full((S, S), 0.38), R2['skin'])
    return rgb


def dummy():
    for face, (w, h) in dict(front=(16, 20), back=(16, 20), left=(12, 20), right=(12, 20), top=(16, 12), bottom=(16, 12)).items():
        yy, xx = np.mgrid[0:h, 0:w]
        weave = ((xx % 2 == 0) ^ (yy % 2 == 0)).astype(float)
        rgb = ramp(0.5 + 0.1 * weave + 0.06 * (nz(h, w) - 0.5) + vol(h, w, 0.06, -0.1), R2['burlap'])
        rope = (yy == 2) | (yy == h - 3)
        rgb = np.where(rope[..., None], ramp(0.62 - 0.2 * ((xx + yy) % 2) + 0 * xx, R2['burlap']), rgb)
        if face == 'front':
            d = np.hypot(xx - w / 2 + 0.5, yy - h / 2 + 0.5)
            rgb = np.where((((d > 4.5) & (d < 6)) | (d < 2))[..., None], ramp(0.55 + 0.1 * weave, R2['red']), rgb)
        save(f'dummy_{face}', rgb)


def cracks():
    S = 32; rng = np.random.default_rng(11); segs = []
    def branch(x, y, ang, ln, depth, t0):
        for i in range(int(ln)):
            x, y = x + math.cos(ang), y + math.sin(ang)
            segs.append((t0 + i / 30, int(x) % S, int(y) % S)); ang += rng.normal(0, 0.28)
            if depth < 2 and rng.random() < 0.08: branch(x, y, ang + rng.choice([-1, 1]) * rng.uniform(0.6, 1.2), ln * 0.55, depth + 1, t0 + i / 30)
    for k in range(6): branch(16, 16, k * math.pi / 3 + rng.normal(0, 0.3), 15, 0, 0)
    for st in range(10):
        a = np.zeros((S, S, 4), np.uint8); lim = (st + 1) / 10 * 0.62
        for t, x, y in segs:
            if t <= lim: a[y, x] = (40, 36, 42, 210)
        Image.fromarray(a, 'RGBA').save(f'{OUT}/destroy_{st}.png')


if __name__ == '__main__':
    for f in ('front', 'back', 'left', 'right', 'top', 'bottom'): save(f'head_{f}', head(f))
    for f, (w, h) in dict(front=(16, 24), back=(16, 24), left=(8, 24), right=(8, 24), top=(16, 8), bottom=(16, 8)).items():
        save(f'torso_{f}', ramp(0.45 + 0.04 * nz(h, w), R2['tunic']) if f in ('top', 'bottom') else tunic(w, h, f))
    for f in ('front', 'back', 'left', 'right'):
        save(f'arm_{f}', arm(8, 24, f)); save(f'leg_{f}', leg(8, 24, f))
    save('arm_top', ramp(0.7 + 0.04 * nz(8, 8), R2['steel'])); save('arm_bottom', ramp(0.32 + 0.04 * nz(8, 8), R2['leather']))
    save('leg_top', ramp(0.42 + 0.04 * nz(8, 8), R2['pants'])); save('leg_bottom', ramp(0.2 + 0.04 * nz(8, 8), R2['boot']))
    dummy(); cracks()
    parts = [('head_front', 14, 14), ('torso_front', 16, 24), ('arm_front', 8, 24), ('leg_front', 8, 24), ('head_right', 14, 14), ('head_back', 14, 14), ('torso_back', 16, 24)]
    W = sum(w for _, w, _ in parts) + 4 * len(parts)
    pv = Image.new('RGB', (W, 28), (40, 40, 46)); x = 2
    for n_, w, h in parts: pv.paste(Image.open(f'{OUT}/{n_}.png'), (x, 2)); x += w + 4
    pv.resize((W * 10, 280), Image.NEAREST).save(os.path.join(D, 'out', 'skin_preview.png'))
    print('ok')
