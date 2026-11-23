#!/usr/bin/env node
// Captures 1:1 340x825 HTML reference PNG + exact DOM layout blueprint for any ProjectZeroEditor inspector row & tab.
import fs from "node:fs";
import path from "node:path";
import { spawn } from "node:child_process";
import { createRequire } from "node:module";

const require = createRequire("/tmp/browser-harness/package.json");
const { chromium } = require("playwright-core");

function parseArgs(argv) {
  const args = {
    row: "",
    tab: "all",
    outDir: "/home/user/Frontier/Exhibits/Images/VisualParity/HtmlRef",
    width: 340,
    height: 825,
    port: 8097,
    scrollY: 0,
  };
  for (let i = 2; i < argv.length; i++) {
    const a = argv[i];
    if (a === "--row") args.row = argv[++i];
    else if (a === "--tab") args.tab = argv[++i];
    else if (a === "--out") args.outDir = argv[++i];
    else if (a === "--width") args.width = Number(argv[++i]);
    else if (a === "--height") args.height = Number(argv[++i]);
    else if (a === "--port") args.port = Number(argv[++i]);
    else if (a === "--scroll") args.scrollY = Number(argv[++i]);
  }
  if (!args.row) {
    console.error("Usage: CaptureHtmlInspectorRef.mjs --row <outliner-row-id> [--tab <index|all>] [--out <dir>] [--width 340] [--height 825]");
    process.exit(2);
  }
  return args;
}

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
  const opts = parseArgs(process.argv);
  fs.mkdirSync(opts.outDir, { recursive: true });

  const baseUrl = `http://127.0.0.1:${opts.port}/`;
  let serverProc = null;
  if (!(await waitPort(baseUrl, 500))) {
    serverProc = spawn("python3", ["/tmp/pze-latest/Experimental/ProjectZeroEditor/Serve.py", String(opts.port)], {
      stdio: "ignore",
      detached: false,
    });
    if (!(await waitPort(baseUrl, 8000))) {
      console.error("Failed to start ProjectZeroEditor Serve.py on port", opts.port);
      serverProc.kill();
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
    args: [
      "--no-sandbox",
      "--disable-setuid-sandbox",
      "--disable-dev-shm-usage",
      "--disable-gpu",
    ],
  });

  try {
    const page = await browser.newPage({
      viewport: { width: 1440, height: opts.height + 30 },
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

    // Expand all folders in outliner
    await page.evaluate(() => {
      for (let pass = 0; pass < 4; pass++) {
        document.querySelectorAll(".outliner-row").forEach((r) => {
          const b = r.querySelector("button, .chev, .toggle");
          if (b && b.getAttribute("aria-expanded") === "false") b.click();
        });
      }
    });
    await page.waitForTimeout(150);

    // Force inspector panel column to exact target width and height so coordinates match C++ 1:1
    await page.evaluate(({ w, h }) => {
      const pane = document.querySelector(".panel-inspector, .inspector-pane, aside.inspector") ||
        Array.from(document.querySelectorAll("aside, section, div")).find((el) => {
          const r = el.getBoundingClientRect();
          return r.x > 950 && r.width >= 300 && r.width <= 440 && r.height > 600 && el.querySelector(".specialized-inspector, .folder-inspector");
        });
      if (pane) {
        pane.style.width = `${w}px`;
        pane.style.minWidth = `${w}px`;
        pane.style.maxWidth = `${w}px`;
      }
      document.documentElement.style.setProperty("--inspector-width", `${w}px`);
    }, { w: opts.width, h: opts.height });

    // Click requested outliner row
    const clicked = await page.evaluate((rowId) => {
      const el = document.querySelector(`.outliner-row[data-outliner-row="${rowId}"]`) ||
        Array.from(document.querySelectorAll(".outliner-row")).find((r) => (r.getAttribute("data-outliner-row") || "").endsWith(rowId));
      if (!el) return null;
      el.click();
      return el.getAttribute("data-outliner-row");
    }, opts.row);

    if (!clicked) {
      const avail = await page.evaluate(() =>
        Array.from(document.querySelectorAll(".outliner-row")).map((r) => r.getAttribute("data-outliner-row"))
      );
      console.error(`Row '${opts.row}' not found. Available:`, avail.join(", "));
      process.exit(1);
    }
    await page.waitForTimeout(220);

    const tabs = await page.evaluate(() =>
      Array.from(document.querySelectorAll("[data-tab-for]")).map((el, idx) => ({
        index: idx,
        key: el.getAttribute("data-tab-for"),
        label: el.textContent?.replace(/\s+/g, " ").trim(),
      }))
    );

    const indices = opts.tab === "all"
      ? (tabs.length ? tabs.map((t) => t.index) : [0])
      : [Number(opts.tab)];

    const cdp = await page.context().newCDPSession(page);

    for (const tabIdx of indices) {
      if (tabs.length > 0 && tabs[tabIdx]) {
        await page.evaluate((key) => {
          document.querySelector(`[data-tab-for="${key}"]`)?.click();
        }, tabs[tabIdx].key);
        await page.waitForTimeout(450);
      }

      const layout = await page.evaluate(({ w, h, scrollY }) => {
        // Find the exact scroll container wrapping .specialized-inspector or .folder-inspector
        const content = document.querySelector(".specialized-inspector, .folder-inspector");
        if (!content) return null;
        let scroller = content.parentElement;
        while (scroller && scroller !== document.body) {
          const st = getComputedStyle(scroller);
          if (st.overflowY === "auto" || st.overflowY === "scroll" || scroller.classList.contains("panel-body")) break;
          scroller = scroller.parentElement;
        }
        const host = scroller || content;
        // Ensure exact width on panel container
        let panel = host;
        while (panel && panel.parentElement && panel.getBoundingClientRect().height < h - 40) {
          panel = panel.parentElement;
        }
        if (panel) {
          panel.style.width = `${w}px`;
          panel.style.minWidth = `${w}px`;
          panel.style.maxWidth = `${w}px`;
        }
        host.scrollTop = scrollY;
        const rect = host.getBoundingClientRect();

        // Collect every key element's exact geometry relative to host top-left
        const pickRect = (el) => {
          const r = el.getBoundingClientRect();
          const s = getComputedStyle(el);
          return {
            tag: el.tagName.toLowerCase(),
            cls: typeof el.className === "string" ? el.className : (el.getAttribute("class") || ""),
            x: Math.round((r.left - rect.left) * 10) / 10,
            y: Math.round((r.top - rect.top) * 10) / 10,
            w: Math.round(r.width * 10) / 10,
            h: Math.round(r.height * 10) / 10,
            bg: s.backgroundColor,
            bgImage: s.backgroundImage !== "none" ? s.backgroundImage : undefined,
            border: s.borderTopWidth !== "0px" ? `${s.borderTopWidth} ${s.borderTopColor}` : undefined,
            radius: s.borderRadius !== "0px" ? s.borderRadius : undefined,
            color: s.color,
            fontSize: s.fontSize,
            fontWeight: s.fontWeight,
            text: el.childElementCount === 0 ? el.textContent?.trim() : undefined,
          };
        };

        const elements = [];
        const sel = [
          ".breadcrumbs", ".inspector-heading", ".inspector-heading h1", ".inspector-heading .eyebrow",
          ".pc-strip", ".pc-step", ".pc-rail", ".pc-pill", ".pc-number", ".pc-count",
          ".property-card", ".property-card h3", ".property-card > p",
          ".metric-readout", ".metric-readout *",
          ".quick-grid", ".quick-tile", ".quick-count",
          ".tag-chip", ".tag-add", ".notes-card", ".notes-card textarea",
          ".control-row", ".pill-input", ".colour-row", "input[type='range']", "svg", "canvas",
          "button", ".hf-light-pair", ".hf-light-pair > *", ".hf-panel", ".hf-panel *"
        ].join(", ");
        content.querySelectorAll(sel).forEach((el) => {
          const r = el.getBoundingClientRect();
          if (r.width > 0 && r.height > 0) elements.push(pickRect(el));
        });

        // Also check footer bar right below host
        const footer = panel?.querySelector(".panel-footer, footer, .inspector-footer");
        const footerRect = footer ? footer.getBoundingClientRect() : null;

        return {
          clipX: Math.round(rect.left),
          clipY: Math.round(rect.top),
          hostW: Math.round(rect.width),
          hostH: Math.round(rect.height),
          hostBg: getComputedStyle(host).backgroundColor,
          footerText: footer?.innerText?.replace(/\s+/g, " ").trim() || "",
          footerH: footerRect ? Math.round(footerRect.height) : 0,
          fullText: content.innerText,
          elements,
        };
      }, { w: opts.width, h: opts.height, scrollY: opts.scrollY });

      if (!layout) {
        console.error("Could not locate .specialized-inspector / .folder-inspector");
        process.exit(1);
      }

      const tabKey = (tabs[tabIdx]?.key || "default").replace(/[^a-zA-Z0-9_-]/g, "_");
      const safeRow = clicked.replace(/[^a-zA-Z0-9_-]/g, "_");
      const baseName = `${safeRow}__${tabIdx}-${tabKey}`;
      const pngPath = path.join(opts.outDir, `${baseName}.png`);
      const jsonPath = path.join(opts.outDir, `${baseName}.json`);

      const { data } = await cdp.send("Page.captureScreenshot", {
        format: "png",
        clip: {
          x: layout.clipX,
          y: layout.clipY,
          width: opts.width,
          height: opts.height,
          scale: 1,
        },
      });
      fs.writeFileSync(pngPath, Buffer.from(data, "base64"));
      fs.writeFileSync(jsonPath, JSON.stringify(layout, null, 2));
      console.log(`[HTML-REF] ${baseName} -> ${pngPath} (host=${layout.hostW}x${layout.hostH}, bg=${layout.hostBg})`);
    }
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
