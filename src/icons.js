/* Frontier — icon library. Stroke-based 24x24 glyphs matching the Fluid theme.
   `applyIcons(root)` replaces any [data-icon="name"] element with an <svg>. */

const ICONS = {
  plus: '<path d="M12 5v14M5 12h14"/>',
  help: '<circle cx="12" cy="12" r="9"/><path d="M9.6 9.2a2.5 2.5 0 1 1 3.4 2.3c-.8.4-1 .9-1 1.7"/><path d="M12 17h.01"/>',
  download: '<path d="M12 3v12"/><path d="M7 11l5 5 5-5"/><path d="M5 20h14"/>',
  folder: '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>',
  file: '<path d="M6 3h8l4 4v14H6z"/><path d="M14 3v4h4"/>',
  search: '<circle cx="11" cy="11" r="7"/><path d="M21 21l-4.3-4.3"/>',
  grid: '<rect x="3" y="3" width="7" height="7" rx="1.4"/><rect x="14" y="3" width="7" height="7" rx="1.4"/><rect x="3" y="14" width="7" height="7" rx="1.4"/><rect x="14" y="14" width="7" height="7" rx="1.4"/>',
  viewport: '<rect x="3" y="4" width="18" height="16" rx="2.4"/><path d="M3 9.5h18"/>',
  eye: '<path d="M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7-10-7-10-7z"/><circle cx="12" cy="12" r="3"/>',
  "eye-off": '<path d="M3 3l18 18"/><path d="M10.5 5.2A10.6 10.6 0 0 1 12 5c6.4 0 10 7 10 7a17 17 0 0 1-3.1 3.9M6.6 6.7A17 17 0 0 0 2 12s3.6 7 10 7a10.6 10.6 0 0 0 4-.8"/><path d="M9.6 9.6a3 3 0 0 0 4.2 4.2"/>',
  brush: '<path d="M14.5 3.5l6 6-7.2 1.1-3-3z"/><path d="M11.3 11.4L4.5 18.2a1.8 1.8 0 0 0 2.5 0l4.3-4.3"/>',
  eraser: '<path d="M4 15.5l7-7 6 6-4 4H8z"/><path d="M9 20h11"/>',
  droplet: '<path d="M12 3s6 6.2 6 10.2A6 6 0 0 1 6 13.2C6 9.2 12 3 12 3z"/>',
  eyedropper: '<path d="M17 3l4 4-2.2 2.2-4-4z"/><path d="M14.8 5.2L5 15l-2.5 4.5L7 17l9.8-9.8"/>',
  text: '<path d="M5 6h14M12 6v13"/><path d="M9 19h6"/>',
  image: '<rect x="3" y="4" width="18" height="16" rx="2.4"/><circle cx="8.5" cy="9.5" r="1.6"/><path d="M21 16l-5-5L5 19"/>',
  link: '<path d="M9.5 14.5l5-5"/><path d="M10 6.5l1-1a4 4 0 0 1 5.7 5.7l-1 1"/><path d="M14 17.5l-1 1a4 4 0 0 1-5.7-5.7l1-1"/>',
  box: '<path d="M12 3l8.5 4.7v9.6L12 21.5 3.5 17.3V7.7z"/><path d="M12 3v18.5M3.5 7.7 12 12l8.5-4.3"/>',
  layers: '<path d="M12 3l9 5-9 5-9-5z"/><path d="M3 13l9 5 9-5"/>',
  trash: '<path d="M4 7h16M9 7V4.5h6V7M6 7l1 13h10l1-13"/><path d="M10 11v6M14 11v6"/>',
  "chevron-up": '<path d="M6 15l6-6 6 6"/>',
  "chevron-down": '<path d="M6 9l6 6 6-6"/>',
  move: '<path d="M12 3v18M3 12h18"/><path d="M9 6l3-3 3 3M9 18l3 3 3-3M6 9l-3 3 3 3M18 9l3 3-3 3"/>',
  shape: '<path d="M12 3l2.6 5.6L20 9.3l-4 4 1 6-5-2.8L7 19.3l1-6-4-4 5.4-.7z"/>',
  sun: '<circle cx="12" cy="12" r="4"/><path d="M12 2v2.5M12 19.5V22M2 12h2.5M19.5 12H22M4.9 4.9l1.8 1.8M17.3 17.3l1.8 1.8M19.1 4.9l-1.8 1.8M6.7 17.3l-1.8 1.8"/>',
  settings: '<circle cx="12" cy="12" r="3"/><path d="M12 2.5l1.4 2.3 2.6-.6.4 2.6 2.4 1-1 2.4 1 2.4-2.4 1-.4 2.6-2.6-.6L12 21.5 10.6 19.2 8 19.8l-.4-2.6L5.2 16l1-2.4-1-2.4 2.4-1 .4-2.6 2.6.6z"/>',
  paint: '<path d="M12 3a9 9 0 0 0 0 18c1 0 1.6-1 1.6-2.1 0-1.4 1-2.1 2.1-2.1H18a3 3 0 0 0 3-3c0-4.2-4.2-7.8-9-7.8z"/><circle cx="7.2" cy="10.5" r="1.1"/>',
  reset: '<path d="M3.5 12a8.5 8.5 0 1 0 2.6-6.1"/><path d="M3 4v5h5"/>',
  "zoom-in": '<circle cx="11" cy="11" r="7"/><path d="M21 21l-4.3-4.3M11 8v6M8 11h6"/>',
  "zoom-out": '<circle cx="11" cy="11" r="7"/><path d="M21 21l-4.3-4.3M8 11h6"/>',
  check: '<path d="M5 13l4 4 10-11"/>',
  logo: '<rect width="32" height="32" rx="8" fill="%2321252b"/><path d="M18 5c1 8-7 8-3 14-4-1-4-5-4-5-8 12 11 19 14 7 1-6-3-12-7-16" fill="%23efa56f"/>',
};

export function icon(name) {
  const glyph = ICONS[name] || ICONS.box;
  return `<svg viewBox="0 0 24 24" aria-hidden="true">${glyph}</svg>`;
}

export function applyIcons(root = document) {
  root.querySelectorAll("[data-icon]").forEach((node) => {
    const name = node.getAttribute("data-icon");
    if (node.childElementCount === 0 && ICONS[name]) {
      node.innerHTML = icon(name);
    }
  });
}
