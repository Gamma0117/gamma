// usage: node capture.mjs <clip> <outdir> [stride] [maxFrames]
import { chromium } from '/home/claude/.npm-global/lib/node_modules/playwright/index.mjs';
import fs from 'fs'; import path from 'path';
const [clip, outdir, strideS, maxS] = process.argv.slice(2);
const stride = +(strideS || 1);
const D = path.dirname(new URL(import.meta.url).pathname);
fs.mkdirSync(outdir, { recursive: true });
const b = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
const page = await b.newPage({ viewport: { width: 1280, height: 720 } });
page.on('console', m => { if (m.type() === 'error' || m.type() === 'warning') console.log('[page]', m.text().slice(0, 300)); });
page.on('pageerror', e => console.log('[pageerror]', e.message));
await page.route('https://cdn.jsdelivr.net/npm/three@0.170.0/**', r => {
  const rel = r.request().url().split('three@0.170.0/')[1];
  r.fulfill({ path: path.join(D, 'node_modules/three', rel), contentType: 'application/javascript' });
});
await page.route('https://fonts.googleapis.com/**', r => r.fulfill({ body: '', contentType: 'text/css' }));
await page.route('https://aurora.local/**', r => r.fulfill({ path: path.join(D, 'viewer_full.html'), contentType: 'text/html' }));
await page.addInitScript(() => { window.CAPTURE = true; });
await page.goto('https://aurora.local/viewer.html');
await page.waitForFunction(() => window.AUR_READY === true, null, { timeout: 60000 });
const len = await page.evaluate(k => { AUR.begin(k); return AUR.len(k); }, clip);
const total = Math.min(Math.round(len * 60), +(maxS || 1e9));
const t0 = Date.now();
let idx = 0;
for (let f = 0; f < total; f += stride) {
  const url = await page.evaluate(([s, first]) => { if (!first) AUR.step(s); return AUR.shot(); }, [stride, f === 0]);
  fs.writeFileSync(path.join(outdir, `f${String(idx++).padStart(4, '0')}.jpg`), Buffer.from(url.split(',')[1], 'base64'));
}
console.log(clip, 'frames', idx, 'len', len.toFixed(2), 'sec', ((Date.now() - t0) / 1000).toFixed(1));
await b.close();
