// Composes the product image at the top of the README (docs/screenshots/hero-light.webp / hero-dark.webp):
// two Torden windows in perspective over a thunderstorm above a fjord, with the logo and the key facts.
//
//   tools/screenshots/shoot.sh        # makes the raw page shots first, then calls this script
//   node hero.mjs                     # only the composition (raw shots in build/screenshots/raw)
//
// Requires Playwright (npm install in this folder) and cwebp.

import { chromium } from "playwright";
import { execFileSync } from "node:child_process";
import { mkdirSync, readdirSync, statSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const RAW_DIR = join(ROOT, "build/screenshots/raw");
const PNG_DIR = join(ROOT, "build/screenshots/png");
const OUT_DIR = join(ROOT, "docs/screenshots");
const FONTS = join(ROOT, "resources/fonts");

// Colours from Source/ui/Theme.h (storm tones derived from them)
const PALETTE = {
  light: { sky1: "#dfe7eb", sky2: "#eef3f4", cloud: "rgba(120,138,152,0.30)", glow: "rgba(232,160,74,0.30)",
           ridge1: "#b9c7cf", ridge2: "#9fb1bc", ridge3: "#8597a3", water: "#dbe4e8", text: "#15253a", dim: "#5f7383",
           accent: "#a8650a", bolt: "#e8a04a", core: "#fff4dc", edge: "rgba(0,0,0,0.08)", shadow: "rgba(15,40,60,0.30)" },
  dark:  { sky1: "#05080f", sky2: "#18233a", cloud: "rgba(52,68,98,0.55)", glow: "rgba(242,181,68,0.26)",
           ridge1: "#0d1626", ridge2: "#0a111d", ridge3: "#060a12", water: "#070c15", text: "#e2edf0", dim: "#8a9bb0",
           accent: "#f2b544", bolt: "#f2b544", core: "#fff8e6", edge: "rgba(255,255,255,0.08)", shadow: "rgba(0,0,0,0.65)" },
};

function presetCount() {
  let n = 0;
  const walk = (dir) => {
    for (const f of readdirSync(dir)) {
      const p = join(dir, f);
      if (statSync(p).isDirectory()) walk(p);
      else if (f.endsWith(".torden")) n++;
    }
  };
  walk(join(ROOT, "presets/Factory"));
  return n;
}

const img = (id, theme) => pathToFileURL(join(RAW_DIR, `${id}-${theme}.png`)).href;

// A lightning bolt with branches (fixed shape, so the image is reproducible)
function bolt(c) {
  const main = "M712,-10 L694,48 L703,60 L676,128 L688,140 L652,214 L640,262 L655,272 L622,340 L610,392 L621,402 L596,470 L588,520 L598,530 L578,600 L570,652";
  const branch1 = "M688,140 L662,172 L668,182 L636,222 L628,250";
  const branch2 = "M640,262 L668,300 L662,312 L690,352";
  const paths = [main, branch1, branch2];
  const glow = `filter: drop-shadow(0 0 4px ${c.bolt}) drop-shadow(0 0 14px ${c.bolt}) drop-shadow(0 0 40px ${c.bolt});`;
  return `<svg class="bolt" viewBox="0 0 1600 900" preserveAspectRatio="none">
    <g style="${glow}" transform="translate(-112 0)">
      ${paths.map((d, i) => `<path d="${d}" fill="none" stroke="${c.core}" stroke-width="${i ? 2.2 : 4}"
                                 stroke-linejoin="round" stroke-linecap="round"/>`).join("")}
    </g>
  </svg>`;
}

function ridges(c) {
  const h = 300;
  const layer = (points, fill) => `<polygon points="${points}" fill="${fill}"/>`;
  return `<svg class="ridges" viewBox="0 0 1600 ${h}" preserveAspectRatio="none" style="height:${h}px">
    ${layer(`0,${h * 0.3} 160,${h * 0.1} 320,${h * 0.36} 500,${h * 0.04} 720,${h * 0.34} 960,${h * 0.12} 1180,${h * 0.42} 1400,${h * 0.08} 1600,${h * 0.28} 1600,${h} 0,${h}`, c.ridge1)}
    ${layer(`0,${h * 0.55} 230,${h * 0.34} 460,${h * 0.62} 700,${h * 0.36} 940,${h * 0.66} 1160,${h * 0.4} 1400,${h * 0.64} 1600,${h * 0.46} 1600,${h} 0,${h}`, c.ridge2)}
    ${layer(`0,${h * 0.78} 1600,${h * 0.78} 1600,${h} 0,${h}`, c.water)}
    <rect x="160" y="${h * 0.84}" width="420" height="3" rx="1.5" fill="${c.accent}" opacity="0.55"/>
    <rect x="260" y="${h * 0.9}" width="240" height="3" rx="1.5" fill="${c.bolt}" opacity="0.4"/>
    <rect x="330" y="${h * 0.955}" width="110" height="3" rx="1.5" fill="#5b8fe6" opacity="0.5"/>
  </svg>`;
}

function hero(theme) {
  const c = PALETTE[theme];
  const other = theme === "light" ? "dark" : "light";
  return `<!doctype html><meta charset="utf-8"><style>
  @font-face { font-family: Manrope; font-weight: 400; src: url(${pathToFileURL(join(FONTS, "Manrope-Regular.ttf")).href}); }
  @font-face { font-family: Manrope; font-weight: 500; src: url(${pathToFileURL(join(FONTS, "Manrope-Medium.ttf")).href}); }
  @font-face { font-family: Manrope; font-weight: 600; src: url(${pathToFileURL(join(FONTS, "Manrope-SemiBold.ttf")).href}); }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  html, body { width: 100%; height: 100%; }
  body {
    font-family: Manrope, sans-serif; color: ${c.text}; overflow: hidden; position: relative;
    background:
      radial-gradient(520px 520px at 31% 42%, ${c.glow}, transparent 70%),
      radial-gradient(700px 420px at 70% 34%, ${c.glow}, transparent 75%),
      radial-gradient(520px 160px at 30% 12%, ${c.cloud}, transparent 70%),
      radial-gradient(640px 180px at 72% 6%, ${c.cloud}, transparent 70%),
      radial-gradient(480px 140px at 90% 22%, ${c.cloud}, transparent 70%),
      radial-gradient(420px 120px at 8% 26%, ${c.cloud}, transparent 70%),
      linear-gradient(180deg, ${c.sky1}, ${c.sky2});
  }
  .bolt { position: absolute; inset: 0; width: 100%; height: 100%; }
  .ridges { position: absolute; left: 0; bottom: 0; width: 100%; }
  .rain { position: absolute; inset: 0; background: repeating-linear-gradient(105deg, transparent 0 22px,
          ${theme === "dark" ? "rgba(160,180,204,0.05)" : "rgba(80,100,120,0.06)"} 22px 23px); }
  .copy { position: absolute; left: 96px; top: 238px; width: 500px; z-index: 2; }
  .logo { font-size: 104px; font-weight: 500; letter-spacing: -3px; line-height: 1; }
  .logo span { color: ${c.accent}; }
  .tag { margin-top: 26px; font-size: 27px; line-height: 1.35; font-weight: 500; }
  .facts { margin-top: 30px; font-size: 15px; letter-spacing: 2.2px; text-transform: uppercase; color: ${c.dim};
           line-height: 2.1; font-weight: 600; }
  .facts b { color: ${c.accent}; font-weight: 600; }
  .scene { position: absolute; left: 600px; top: 110px; perspective: 2600px; }
  .window { display: block; position: absolute; width: 1100px; border-radius: 14px; transform-origin: left center;
            box-shadow: 0 0 0 1px ${c.edge}, 0 50px 90px -30px ${c.shadow}, 0 18px 36px -18px ${c.shadow}; }
  .back  { top: -150px; left: 190px; transform: rotateY(-18deg) rotateX(4deg) scale(0.9); opacity: 0.97; }
  .front { top: 70px; left: 0; transform: rotateY(-18deg) rotateX(4deg); }
  </style>
  <div class="rain"></div>
  ${bolt(c)}
  ${ridges(c)}
  <div class="copy">
    <div class="logo">torden<span>.</span></div>
    <div class="tag">Kicks, built from thunder.<br>Drawn, shaped, synthesized.</div>
    <div class="facts">
      <b>4</b> layers · <b>7</b> drawable envelopes<br>
      <b>10</b> effects · step sequencer<br>
      <b>${presetCount()}</b> factory presets<br>
      <b>VST3</b> · <b>AU</b> · <b>LV2</b> · standalone
    </div>
  </div>
  <div class="scene">
    <img class="window back" src="${img("fx", other)}">
    <img class="window front" src="${img("kick", theme)}">
  </div>`;
}

mkdirSync(OUT_DIR, { recursive: true });
mkdirSync(PNG_DIR, { recursive: true });
const browser = await chromium.launch();
const page = await browser.newPage({ deviceScaleFactor: 1.5 });
try {
  for (const theme of ["light", "dark"]) {
    const htmlFile = join(RAW_DIR, `_hero-${theme}.html`);
    writeFileSync(htmlFile, hero(theme));
    await page.setViewportSize({ width: 1600, height: 900 });
    await page.goto(pathToFileURL(htmlFile).href);
    await page.evaluate(() => document.fonts.ready);
    await page.waitForFunction(() => [...document.images].every((i) => i.complete && i.naturalWidth > 0));
    const png = join(PNG_DIR, `hero-${theme}.png`);
    const out = join(OUT_DIR, `hero-${theme}.webp`);
    await page.screenshot({ path: png });
    execFileSync("cwebp", ["-quiet", "-q", "88", "-m", "6", png, "-o", out]);
    console.log(`hero  ${out.replace(ROOT + "/", "")} (${Math.round(statSync(out).size / 1024)} KB)`);
  }
} finally {
  await browser.close();
}
