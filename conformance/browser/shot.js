// Real-browser snapshot driver for the AffineUI conformance harness.
//
// Loads a test's HTML at a fixed viewport, replays the SAME ordered steps the
// AffineUI side runs (click / hover / wait / snapshot), and writes a PNG at
// each `snapshot` marker. Pairs with tools/conformance (conformance_test) +
// conformance/diff.py. Coordinates are CSS pixels (browser & AffineUI agree).
//
//   node shot.js --html <path> --out-dir <dir> --name <test>
//                [--width W] [--height H] [--dpi S] [--channel chrome|chromium]
//                [--steps '<json>']
//
// steps JSON: array of {"click":[x,y]} | {"hover":[x,y]} | {"wait_ms":N}
//             | {"snapshot":"name"}.  No snapshot marker => one named "default".

import { chromium } from 'playwright';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

function parseArgs(argv) {
  const a = { width: 1024, height: 768, dpi: 1, channel: '', steps: [], outDir: '.', name: 'test' };
  for (let i = 2; i < argv.length; i++) {
    const k = argv[i], v = () => argv[++i];
    if (k === '--html') a.html = v();
    else if (k === '--out-dir') a.outDir = v();
    else if (k === '--name') a.name = v();
    else if (k === '--width') a.width = parseInt(v(), 10);
    else if (k === '--height') a.height = parseInt(v(), 10);
    else if (k === '--dpi') a.dpi = parseFloat(v());
    else if (k === '--channel') a.channel = v();
    else if (k === '--steps') a.steps = JSON.parse(v());
    else { console.error(`unknown option ${k}`); process.exit(2); }
  }
  if (!a.html) { console.error('usage: node shot.js --html <path> --out-dir <dir> --name <test> [...]'); process.exit(2); }
  return a;
}

async function launch(channel) {
  if (channel) return chromium.launch({ channel });
  try { return await chromium.launch(); }
  catch { return chromium.launch({ channel: 'chrome' }); }
}

const args = parseArgs(process.argv);
const browser = await launch(args.channel);
try {
  const ctx = await browser.newContext({
    viewport: { width: args.width, height: args.height },
    deviceScaleFactor: args.dpi,
    reducedMotion: 'reduce',
  });
  const page = await ctx.newPage();
  await page.goto(pathToFileURL(path.resolve(args.html)).href, { waitUntil: 'load' });

  const shot = async (snap) => {
    const out = path.join(args.outDir, `${args.name}.browser.${snap}.png`);
    await page.screenshot({ path: out, clip: { x: 0, y: 0, width: args.width, height: args.height } });
    console.error(`wrote ${out}`);
  };

  let took = false;
  for (const step of args.steps) {
    if (step.click) await page.mouse.click(step.click[0], step.click[1]);
    else if (step.hover) await page.mouse.move(step.hover[0], step.hover[1]);
    else if (step.wait_ms != null) await page.waitForTimeout(step.wait_ms);
    else if (step.snapshot != null) { await shot(step.snapshot); took = true; }
  }
  if (!took) await shot('default');
} finally {
  await browser.close();
}
