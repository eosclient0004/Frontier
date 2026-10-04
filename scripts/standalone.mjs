/* Build a single-file standalone page for static hosts (e.g. raw.githack.com).
   Usage: npm run build:standalone
   Reads dist/ (vite build output) and inlines JS + CSS into texture-paint.html
   at the repo root. Google Fonts stay remote with system-font fallback. */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const DIST = path.join(ROOT, "dist");
const OUT = path.join(ROOT, "texture-paint.html");

const htmlPath = path.join(DIST, "index.html");
if (!fs.existsSync(htmlPath)) {
  console.error("dist/index.html not found — run `vite build` first.");
  process.exit(1);
}
let html = fs.readFileSync(htmlPath, "utf8");

// Inline module scripts with relative sources.
html = html.replace(/<script([^>]*)\ssrc="(\.\/[^"]+)"([^>]*)><\/script>/g, (match, before, src) => {
  const file = path.join(DIST, src);
  if (!fs.existsSync(file)) {
    console.error(`missing script asset: ${src}`);
    process.exit(1);
  }
  let code = fs.readFileSync(file, "utf8");
  code = code.replace(/<\/script/gi, "<\\/script"); // keep the inline block intact
  const typeModule = /type="module"/.test(before) ? ' type="module"' : "";
  console.log(`inlined ${src} (${(code.length / 1024).toFixed(1)} KB)`);
  return `<script${typeModule}>\n${code}\n</script>`;
});

// Inline stylesheets with relative sources.
html = html.replace(/<link([^>]*)\srel="stylesheet"([^>]*)\shref="(\.\/[^"]+)"([^>]*)\/?>/g, (match, _a, _b, href) => {
  const file = path.join(DIST, href);
  if (!fs.existsSync(file)) {
    console.error(`missing stylesheet asset: ${href}`);
    process.exit(1);
  }
  const css = fs.readFileSync(file, "utf8");
  if (/<\/style/i.test(css)) {
    console.error(`unsafe CSS in ${href}: contains </style>`);
    process.exit(1);
  }
  console.log(`inlined ${href} (${(css.length / 1024).toFixed(1)} KB)`);
  return `<style>\n${css}\n</style>`;
});

if (/\.\/assets\//.test(html)) {
  console.error("unresolved relative asset references remain in the output.");
  process.exit(1);
}

html = html.replace(
  "</title>",
  "</title>\n    <!-- Standalone single-file build — open directly or via https://raw.githack.com/ -->",
);
fs.writeFileSync(OUT, html);
console.log(`wrote ${path.relative(ROOT, OUT)} (${(html.length / 1024).toFixed(1)} KB)`);
