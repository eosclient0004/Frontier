// Declarative inspector controls rendered with the Fluid editor's markup
// (property groups, slider pills, switches, pill selects).

import { Icon } from "./Icons.js";
import { CHANNELS } from "./MaterialSpecification.js";

export const Escape = (Text) =>
  String(Text).replace(/[&<>"']/g, (C) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[C]);

export const Slider = (Bind, Label, Min, Max, Step, Unit = "—", Extra = {}) => ({ Kind: "slider", Bind, Label, Min, Max, Step, Unit, Display: 1, ...Extra });
export const Percent = (Bind, Label, Extra = {}) => Slider(Bind, Label, 0, 1, 0.01, "%", { Display: 100, ...Extra });
export const ColorField = (Bind, Label) => ({ Kind: "color", Bind, Label });
export const Toggle = (Bind, Label, Extra = {}) => ({ Kind: "toggle", Bind, Label, ...Extra });
export const Choice = (Bind, Label, Options, Extra = {}) => ({ Kind: "select", Bind, Label, Options, ...Extra });
export const Segmented = (Bind, Label, Options) => ({ Kind: "segmented", Bind, Label, Options });
export const Buttons = (Items) => ({ Kind: "buttons", Items });
export const Hint = (Text) => ({ Kind: "hint", Text });
export const Channels = (Bind, Label = "Channels") => ({ Kind: "channels", Bind, Label });
export const TextArea = (Bind, Label, Rows = 3) => ({ Kind: "textarea", Bind, Label, Rows });
export const Custom = (Html) => ({ Kind: "custom", Html });

export const Decimals = (Step) => (Step >= 1 ? 0 : Step >= 0.1 ? 1 : Step >= 0.01 ? 2 : 3);
export const FormatValue = (Value, Control) =>
  (Number(Value) * Control.Display).toFixed(Decimals(Control.Step * Control.Display));

let Counter = 0;
export function RenderControl(Control, Get) {
  const Id = `control-${Counter++}`;
  const Label = Escape(Control.Label || "");
  const Value = Control.Bind ? Get(Control.Bind) : undefined;
  switch (Control.Kind) {
    case "slider": {
      const Fraction = (Number(Value) - Control.Min) / (Control.Max - Control.Min || 1);
      return `<div class="property-row slider-row"><label class="property-label" for="${Id}">${Label}</label><div class="slider-control"><div class="value-pill"><input id="${Id}" data-bind="${Control.Bind}" data-kind="number" type="number" value="${FormatValue(Value, Control)}" min="${Control.Min * Control.Display}" max="${Control.Max * Control.Display}" step="${Control.Step * Control.Display}" data-display="${Control.Display}" data-step="${Control.Step}" data-min="${Control.Min}" data-max="${Control.Max}"/><span class="unit-cell" aria-hidden="true">${Escape(Control.Unit)}</span></div><input type="range" aria-label="${Label} slider" data-bind="${Control.Bind}" data-kind="range" min="${Control.Min}" max="${Control.Max}" step="${Control.Step}" value="${Value}" style="--fraction:${Math.min(1, Math.max(0, Fraction)).toFixed(4)}"/></div></div>`;
    }
    case "color":
      return `<div class="property-row color-row"><label class="property-label" for="${Id}">${Label}</label><div class="color-control"><label class="color-swatch" style="--swatch:${Value}"><input id="${Id}" type="color" data-bind="${Control.Bind}" data-kind="color" value="${Value}"/></label><input type="text" class="hex-input" data-bind="${Control.Bind}" data-kind="hex" value="${Escape(String(Value).toUpperCase())}" maxlength="7" spellcheck="false" aria-label="${Label} hex"/></div></div>`;
    case "toggle":
      return `<div class="property-row toggle-row"><label class="property-label" for="${Id}">${Label}</label><label class="switch"><input id="${Id}" type="checkbox" data-bind="${Control.Bind}" data-kind="toggle" ${Value ? "checked" : ""}/><span></span></label></div>`;
    case "select":
      return `<div class="property-row select-row"><label class="property-label" for="${Id}">${Label}</label><select id="${Id}" data-bind="${Control.Bind}" data-kind="select">${Control.Options.map((O) => `<option value="${Escape(O.id)}" ${String(O.id) === String(Value) ? "selected" : ""}>${Escape(O.label)}</option>`).join("")}</select></div>`;
    case "segmented":
      return `<div class="property-row segmented-row">${Label ? `<span class="property-label">${Label}</span>` : ""}<div class="segmented wide" role="radiogroup">${Control.Options.map((O) => `<button type="button" data-bind="${Control.Bind}" data-kind="segment" data-value="${Escape(O.id)}" class="${String(O.id) === String(Value) ? "active" : ""}" role="radio" aria-checked="${String(O.id) === String(Value)}">${O.icon ? Icon(O.icon) : ""}${Escape(O.label)}</button>`).join("")}</div></div>`;
    case "buttons":
      return `<div class="property-row button-row">${Control.Items.map((B) => `<button type="button" class="inspector-button ${B.Danger ? "danger" : ""} ${B.Primary ? "primary" : ""}" data-action="${B.Action}" ${B.Disabled ? "disabled" : ""} title="${Escape(B.Title || B.Label)}">${B.Icon ? Icon(B.Icon) : ""}<span>${Escape(B.Label)}</span></button>`).join("")}</div>`;
    case "hint":
      return `<p class="property-hint">${Control.Text}</p>`;
    case "channels":
      return `<div class="property-row channels-row"><span class="property-label">${Label}</span><div class="channel-chips">${CHANNELS.map((C) => `<button type="button" class="channel-chip ${Value[C.id] ? "active" : ""}" data-bind="${Control.Bind}.${C.id}" data-kind="chip" aria-pressed="${Boolean(Value[C.id])}" title="${C.label}"><i data-channel="${C.id}"></i>${C.short}</button>`).join("")}</div></div>`;
    case "textarea":
      return `<div class="property-row textarea-row"><label class="property-label" for="${Id}">${Label}</label><textarea id="${Id}" rows="${Control.Rows}" data-bind="${Control.Bind}" data-kind="text" spellcheck="false">${Escape(Value ?? "")}</textarea></div>`;
    case "custom":
      return Control.Html;
  }
  return "";
}

export function RenderGroup(Group, Get, OpenState) {
  const Open = OpenState[Group.Id] ?? Group.Open ?? true;
  return `<details class="property-group" data-group="${Group.Id}" ${Open ? "open" : ""}><summary>${Escape(Group.Title)}<span class="section-badge">${Escape(Group.Badge || "")}</span></summary><div class="group-content">${Group.Hint ? `<p class="property-hint">${Group.Hint}</p>` : ""}${Group.Controls.filter(Boolean).map((C) => RenderControl(C, Get)).join("")}</div></details>`;
}
