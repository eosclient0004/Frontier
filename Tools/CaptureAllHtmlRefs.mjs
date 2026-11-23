#!/usr/bin/env node
// Captures exact 340x825 HTML reference PNG + DOM layout JSON for EVERY inspector row & EVERY tab in ProjectZeroEditor.
import fs from "node:fs";
import path from "node:path";
import { spawn } from "node:child_process";
import { createRequire } from "node:module";

const require = createRequire("/tmp/browser-harness/package.json");
const { chromium } = require("playwright-core");

const OUT_DIR = "/home/user/Frontier/Exhibits/Images/VisualParity/HtmlRef";
const WIDTH = 340;
const HEIGHT = 825;
const PORT = 8097;

// Representative outliner rows covering ALL inspector classes in ProjectZeroEditor
const TARGET_ROWS = [
  "height-fog",
  "aerial-fog",
  "local-fog",
  "wind",
  "clouds",
  "local-cloud",
  "atmosphere",
  "sun",
  "moon",
  "stars",
  "camera",
  "motion-blur",
  "flare",
  "post",
  "environment-light",
  "reference-rim-point",
  "reference-key-spot",
  "reference-ece-low-beam",
  "reference-softbox",
  "reference-studio-tube",
  "reference-led",
  "reference-led-strip",
  "torus",
  "showcase",
];

async function waitPort(url, timeoutMs = 8000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      const res = await fetch(url);
      if (res.ok) return true;
    } catch {}
    await new Promise((r) => setTimeout(r, 150));
  }
  return false;
}

async function main() {
  const filterArg = process.argv.slice(2);
  const wantedRows = filterArg.length > 0 ? filterArg : TARGET_ROWS;
  fs.mkdirSync(OUT_DIR, { recursive: true });

  const baseUrl = `http://127.0.0.1:${PORT}/`;
  let serverProc = null;
  if (!(await waitPort(baseUrl, 500))) {
    serverProc = spawn("python3", ["/tmp/pze-latest/Experimental/ProjectZeroEditor/Serve.py", String(PORT)], {
      stdio: "ignore",
      detached: false,
    });
    if (!(await waitPort(baseUrl, 8000))) {
      console.error("Failed to start Serve.py");
      serverProc.kill("SIGKILL");
      process.exit(1);
    }
  }

  const browser = await chromium.launch({
    executablePath: "/tmp/chromium",
    headless: true,
    env: {
      ...process.env,
      LD_LIBRARY_PATH: `/tmp/al2023/lib:/tmp${process.env.LD_LIBRARY_PATH ? ":" + process.env.LD_LIBRARY_PATH : ""}`,
    },
    args: ["--no-sandbox", "--disable-setuid-sandbox", "--disable-dev-shm-usage", "--disable-gpu"],
  });

  const manifest = {};

  try {
    const page = await browser.newPage({
      viewport: { width: 1440, height: HEIGHT + 30 },
      deviceScaleFactor: 1,
    });
    await page.route("**/*", (route) => {
      const u = route.request().url();
      if (u.startsWith("http://127.0.0.1:") || u.startsWith("http://localhost:") || u.startsWith("data:") || u.startsWith("blob:")) {
        return route.continue();
      }
      return route.abort();
    });

    await page.goto(baseUrl, { waitUntil: "domcontentloaded" });
    await page.evaluate(() => localStorage.clear());
    await page.reload({ waitUntil: "domcontentloaded" });
    await page.locator(".outliner-row").first().waitFor({ timeout: 15000 });
    await page.waitForTimeout(250);

    // Disable CSS transitions and hide leaving pages immediately so tab switches settle cleanly with zero ghost overlap!
    await page.addStyleTag({ content: "*, *::before, *::after { transition: none !important; animation: none !important; scroll-behavior: auto !important; } .pc-leaving, [hidden] { display: none !important; visibility: hidden !important; opacity: 0 !important; }" });

    // Expand all nested Outliner folders/parents across React render ticks
    for (let pass = 0; pass < 5; pass++) {
      const clickedAny = await page.evaluate(() => {
        let count = 0;
        document.querySelectorAll(".outliner-row[aria-expanded='false']").forEach((row) => {
          const btn = row.querySelector("button");
          if (btn) { btn.click(); count++; }
        });
        return count;
      });
      if (!clickedAny) break;
      await page.waitForTimeout(100);
    }

    const cdp = await page.context().newCDPSession(page);

    for (const rowId of wantedRows) {
      const clicked = await page.evaluate((id) => {
        const el = document.querySelector(`.outliner-row[data-outliner-row="${id}"]`) ||
          Array.from(document.querySelectorAll(".outliner-row")).find((r) => (r.getAttribute("data-outliner-row") || "").endsWith(id));
        if (!el) return null;
        el.click();
        return el.getAttribute("data-outliner-row");
      }, rowId);

      if (!clicked) {
        console.log(`[SKIP] row not found in default scene: ${rowId}`);
        continue;
      }
      await page.waitForTimeout(120);

      // Lock inspector column width to exact 340px
      await page.evaluate((w) => {
        const content = document.querySelector(".specialized-inspector, .folder-inspector, .generic-inspector, .primitive-inspector, .inspector-scroll");
        if (!content) return;
        let panel = content.parentElement;
        while (panel && panel.parentElement && panel.getBoundingClientRect().height < 700) panel = panel.parentElement;
        if (panel) {
          panel.style.width = `${w}px`;
          panel.style.minWidth = `${w}px`;
          panel.style.maxWidth = `${w}px`;
        }
      }, WIDTH);
      await page.waitForTimeout(60);

      const tabs = await page.evaluate(() =>
        Array.from(document.querySelectorAll("[data-tab-for]")).map((el, idx) => ({
          index: idx,
          key: el.getAttribute("data-tab-for"),
          label: el.textContent?.replace(/\s+/g, " ").trim(),
        }))
      );

      manifest[clicked] = [];
      const tabCount = Math.max(1, tabs.length);

      for (let t = 0; t < tabCount; t++) {
        if (tabs.length > 0 && tabs[t]) {
          await page.evaluate((key) => {
            document.querySelector(`[data-tab-for="${key}"]`)?.click();
          }, tabs[t].key);
          await page.waitForTimeout(100);
        }

        const layout = await page.evaluate(({ w, h }) => {
          const scrollEl = document.querySelector(".inspector-scroll");
          const content = document.querySelector(".specialized-inspector, .folder-inspector, .generic-inspector, .primitive-inspector") || scrollEl;
          if (!content) return null;
          if (scrollEl) scrollEl.scrollTop = 0;
          const host = scrollEl || content;
          const rect = host.getBoundingClientRect();

          // Only collect elements that are actually inside the visible viewport horizontally [rect.left..rect.right]
          const elements = [];
          const sel = [
            ".breadcrumbs", ".inspector-heading", ".inspector-heading h1", ".inspector-heading .eyebrow",
            ".pc-strip", ".pc-pill",
            ".property-card", ".property-card h3", ".property-card > p",
            ".metric", ".readout", ".field", ".colour-field",
            ".quick-grid", ".quick-tile", ".quick-count",
            ".tag-chip", ".notes-card",
            "input[type='range']", "svg", "canvas", "button"
          ].join(", ");
          content.querySelectorAll(sel).forEach((el) => {
            const r = el.getBoundingClientRect();
            if (r.width <= 0 || r.height <= 0) return;
            // Skip off-screen carousel pages
            if (r.right < rect.left + 2 || r.left > rect.right - 2) return;
            const s = getComputedStyle(el);
            elements.push({
              tag: el.tagName.toLowerCase(),
              cls: typeof el.className === "string" ? el.className : (el.getAttribute("class") || ""),
              x: Math.round((r.left - rect.left) * 10) / 10,
              y: Math.round((r.top - rect.top) * 10) / 10,
              w: Math.round(r.width * 10) / 10,
              h: Math.round(r.height * 10) / 10,
              bg: s.backgroundColor,
              color: s.color,
              fontSize: s.fontSize,
              text: el.childElementCount === 0 ? el.textContent?.trim() : undefined,
            });
          });

          return {
            clipX: Math.round(rect.left),
            clipY: Math.round(rect.top),
            hostW: Math.round(rect.width),
            hostH: Math.round(rect.height),
            fullText: content.innerText,
            elements,
          };
        }, { w: WIDTH, h: HEIGHT });

        if (!layout) continue;
        const tabKey = (tabs[t]?.key || "default").replace(/[^a-zA-Z0-9_-]/g, "_");
        const safeRow = clicked.replace(/[^a-zA-Z0-9_-]/g, "_");
        const baseName = `${safeRow}__${t}-${tabKey}`;
        const pngPath = path.join(OUT_DIR, `${baseName}.png`);
        const jsonPath = path.join(OUT_DIR, `${baseName}.json`);

        const { data } = await cdp.send("Page.captureScreenshot", {
          format: "png",
          clip: { x: layout.clipX, y: layout.clipY, width: WIDTH, height: HEIGHT, scale: 1 },
        });
        fs.writeFileSync(pngPath, Buffer.from(data, "base64"));
        fs.writeFileSync(jsonPath, JSON.stringify(layout, null, 2));
        manifest[clicked].push({ index: t, key: tabKey, png: `${baseName}.png`, json: `${baseName}.json` });
      }
      console.log(`[CAPTURED] ${clicked}: ${manifest[clicked].length} tab(s)`);
    }
    fs.writeFileSync(path.join(OUT_DIR, "manifest.json"), JSON.stringify(manifest, null, 2));
  } finally {
    await browser.close();
    if (serverProc) serverProc.kill("SIGKILL");
  }
  process.exit(0);
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
