/* Frontier Texture Paint — stroke icon vocabulary (1.6px, round caps).
   Mirrors the Fluid glyph set; texture tools extend it. */

export const GlyphPaths = {
  brush: '<path d="m14 3 7 7-10 10H4v-7Zm-6 7 7 7M3 21h7"/>',
  eraser: '<path d="m7 21-4-4L14 6l5 5-6 6H8l-1 4Zm9-13 4 4"/>',
  smudge:
    '<path d="M12 3c3 4 6 7 6 11a6 6 0 0 1-12 0c0-2 1-3 2-4 0 2 1 3 3 3-1-3 0-7 1-10Z"/>',
  fill: '<path d="m5 11 9-9 9 9-7 7H8l-3-3v-4Zm-3 8c-2 1-2 4 0 4 3 0 4-2 4-4"/>',
  eyedropper:
    '<path d="m14 4 6 6-8 8-6-6Zm-8 8-3 7 7-3m4-12 3-3 4 4-3 3"/>',
  shape: '<rect x="3" y="3" width="10" height="10" rx="1"/><circle cx="16" cy="16" r="5"/>',
  move: '<path d="M12 2v20M2 12h20m-4-4 4 4-4 4m4-12 4 4-4 4M6 8 2 12l4 4"/>',
  pan: '<path d="M8 12V5a1 1 0 0 1 2 0v6V4a1 1 0 0 1 2 0v7V6a1 1 0 0 1 2 0v9c0 4-2 6-6 6-2 0-3-1-5-4l-2-3a1 1 0 0 1 2-1l2 3"/>',
  layers: '<path d="m12 3 10 5-10 5L2 8Zm-9 10 9 5 9-5M3 17l9 5 9-5"/>',
  plus: '<path d="M12 5v14M5 12h14"/>',
  search: '<circle cx="10" cy="10" r="6"/><path d="m15 15 6 6"/>',
  grid: '<rect x="3" y="3" width="7" height="7" rx="1"/><rect x="14" y="3" width="7" height="7" rx="1"/><rect x="3" y="14" width="7" height="7" rx="1"/><rect x="14" y="14" width="7" height="7" rx="1"/>',
  box: '<path d="m12 2 9 5v10l-9 5-9-5V7Zm0 10L3 7m9 5 9-5m-9 5v10M7 4.5 17 10"/>',
  link: '<path d="m10 14 4-4m-6 2-2 2a3.5 3.5 0 0 0 5 5l2-2m-2-10 2-2a3.5 3.5 0 0 1 5 5l-2 2"/>',
  download: '<path d="M12 3v12m-5-5 5 5 5-5M4 16v5h16v-5"/>',
  file: '<path d="M14 2H5v20h14V7Zm0 0v6h5M8 13h8m-8 4h6"/>',
  folder: '<path d="M3 6h7l2 3h9l-2 11H3V6Zm0 3h18"/>',
  help: '<circle cx="12" cy="12" r="9"/><path d="M9.5 9a2.5 2.5 0 1 1 4 2c-1.5 1-1.5 1.5-1.5 3m0 3h.01"/>',
  close: '<path d="m6 6 12 12M6 18 18 6"/>',
  undo: '<path d="M8 5 3 10l5 5M3 10h11a7 7 0 0 1 0 14h-3"/>',
  redo: '<path d="m16 5 5 5-5 5M21 10H10a7 7 0 0 0 0 14h3"/>',
  trash: '<path d="M4 7h16m-9-4h2l1 2h7l-1 14H5L4 5m6 0 1 2m4-2 1 2M7 11v7m4-7v7m4-7v7"/>',
  rotate: '<path d="M4 10a8 8 0 1 1 1 7M4 3v7h7"/>',
  focus:
    '<path d="M3 8V3h5m8 0h5v5m0 8v5h-5M8 21H3v-5"/><circle cx="12" cy="12" r="3"/>',
  maximize:
    '<path d="M8 3H3v5m0-5 7 7m6 11h5v-5m0 5-7-7M16 3h5v5m0-5-7 7M8 21H3v-5m0 5 7-7"/>',
  viewport:
    '<rect x="3" y="4" width="18" height="14" rx="2"/><path d="M8 22h8m-4-4v4"/>',
  clock: '<circle cx="12" cy="12" r="9"/><path d="M12 6v6l4 2"/>',
  warning: '<path d="m12 3 10 18H2Zm0 6v5m0 3h.01"/>',
  eye: '<path d="M2 12c5-9 15-9 20 0-5 9-15 9-20 0Z"/><circle cx="12" cy="12" r="3"/>',
  hidden: '<path d="m3 3 18 18M9 5c5-2 10 1 13 7l-4 5M6 6l-4 6c3 6 8 9 14 6"/>',
  lock: '<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V7a4 4 0 0 1 8 0v4"/>',
  unlock: '<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V7a4 4 0 0 1 7-2"/>',
  image:
    '<rect x="3" y="4" width="18" height="16" rx="2"/><circle cx="9" cy="10" r="2"/><path d="m3 18 5-5 4 4 3-3 6 6"/>',
  svg: '<path d="m8 6-6 6 6 6m8-12 6 6-6 6"/>',
  text: '<path d="M5 5h14M12 5v14m-3 0h6"/>',
  adjust:
    '<path d="M4 7h10m4 0h2M4 17h4m4 0h8"/><circle cx="16" cy="7" r="2.5"/><circle cx="10" cy="17" r="2.5"/>',
  "fill-layer": '<rect x="4" y="4" width="16" height="16" rx="3"/><path d="M4 15c4-1 5-6 9-6 3 0 4 2 7 2"/>',
  mask: '<circle cx="12" cy="12" r="9"/><path d="M12 3a9 9 0 0 1 0 18Z" fill="currentColor" stroke="none" opacity="0.45"/>',
  sphere:
    '<circle cx="12" cy="12" r="9"/><ellipse cx="12" cy="12" rx="4" ry="9"/><path d="M3 12h18"/>',
  cube: '<path d="m12 2 9 5v10l-9 5-9-5V7Zm0 10L3 7m9 5 9-5m-9 5v10"/>',
  sun: '<circle cx="12" cy="12" r="4"/><path d="M12 1v3m0 16v3M1 12h3m16 0h3M4 4l2 2m12 12 2 2M4 20l2-2M18 6l2-2"/>',
  check: '<path d="m4 12 5 5L20 6"/>',
  merge: '<path d="M12 3v10m0 0-4-4m4 4 4-4M4 13v7h16v-7"/>',
  swap: '<path d="M7 4 3 8l4 4M3 8h13m4 8 4-4-4-4m4 4H8"/>',
};

export const Icon = (Name) =>
  `<svg viewBox="0 0 24 24" aria-hidden="true">${GlyphPaths[Name] || GlyphPaths.box}</svg>`;

export function FillIcons(Root = document) {
  Root.querySelectorAll("[data-icon]").forEach((Element) => {
    if (Element.dataset.iconFilled) return;
    Element.innerHTML = Icon(Element.dataset.icon);
    Element.dataset.iconFilled = "1";
  });
}

export function SetIcon(Element, Name) {
  if (!Element) return;
  Element.innerHTML = Icon(Name);
  Element.dataset.icon = Name;
  Element.dataset.iconFilled = "1";
}
