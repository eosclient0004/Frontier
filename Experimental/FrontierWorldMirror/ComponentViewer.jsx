//==========================================================================================================================================
//                                                     COMPONENTVIEWER.JSX
//==========================================================================================================================================
// Dockable in-editor component browser.  This intentionally has a single Components
// view: the obsolete standalone Content tab is not part of the World Mirror UI.

import React, { useMemo, useState } from "react";
import { Icon } from "./Inspectors.jsx";
import { ResolveMaterial } from "./MaterialSpecification.js";
import "./ComponentViewer.css";

const LabelForPanel = (Panel) =>
  ({
    group: "Collection",
    geometry: "Scene component",
    camera: "Camera component",
    "render-settings": "Camera Settings",
    sun: "Solar component",
    atmosphere: "Atmosphere component",
  })[Panel] || String(Panel || "component").replaceAll("-", " ");

const Vector = (Value, Digits = 2) =>
  Array.isArray(Value)
    ? Value.map((Axis) => Number(Axis || 0).toFixed(Digits)).join(", ")
    : "—";

export default function ComponentViewer({ Rows = [], Values = {}, Selected, Select }) {
  const [Query, SetQuery] = useState("");
  const Components = useMemo(() => {
    const Needle = Query.trim().toLowerCase();
    return Rows.filter((Row) =>
      !Needle || `${Row.Name} ${Row.Panel} ${Row.Description || ""}`.toLowerCase().includes(Needle),
    );
  }, [Rows, Query]);
  const Subject = Rows.find((Row) => Row.Id === Selected) || Components[0] || null;
  const ValuesForSubject = Subject ? Values[Subject.Id] || {} : {};
  const Material = Subject?.Panel === "geometry" ? ResolveMaterial(ValuesForSubject) : null;
  const Colour = Material?.Channels?.colour?.Fill || "#4c5563";

  return (
    <div className="component-viewer" aria-label="Component Viewer">
      <header className="component-viewer-heading">
        <div>
          <span className="component-viewer-kicker">DOCKED EDITOR TOOL</span>
          <h1>Component Viewer</h1>
          <p>Components in the active World Mirror scene. Asset Content has been removed.</p>
        </div>
        <div className="component-viewer-count">
          <b>{Rows.length}</b>
          <small>components</small>
        </div>
      </header>
      <div className="component-viewer-grid">
        <aside className="component-list" aria-label="Scene components">
          <label className="component-search">
            <span>⌕</span>
            <input
              aria-label="Search components"
              value={Query}
              onChange={(Event) => SetQuery(Event.target.value)}
              placeholder="Search components"
            />
          </label>
          <div className="component-list-scroll">
            {Components.map((Row) => (
              <button
                key={Row.Id}
                className={Row.Id === Subject?.Id ? "component-row active" : "component-row"}
                onClick={() => Select?.(Row.Id)}
                title={Row.Description}
              >
                <Icon Name={Row.Icon} Size={18} />
                <span>
                  <b>{Row.Name}</b>
                  <small>{LabelForPanel(Row.Panel)}</small>
                </span>
              </button>
            ))}
            {!Components.length && <p className="component-empty">No matching components.</p>}
          </div>
        </aside>
        <section className="component-detail" aria-live="polite">
          {Subject ? (
            <>
              <div className="component-detail-title">
                <span className="component-icon"><Icon Name={Subject.Icon} Size={25} /></span>
                <div>
                  <span>{LabelForPanel(Subject.Panel)}</span>
                  <h2>{Subject.Name}</h2>
                </div>
                <button onClick={() => Select?.(Subject.Id)}>Focus in Outliner</button>
              </div>
              <p className="component-description">{Subject.Description || "No component description."}</p>
              <div className="component-properties">
                <div><small>Component ID</small><code>{Subject.Id}</code></div>
                <div><small>Parent</small><b>{Subject.Parent || "World root"}</b></div>
                <div><small>Visible geometry</small><b>{Subject.Preview ? "Yes" : "No"}</b></div>
                {Subject.Panel === "geometry" && <>
                  <div><small>Position (m)</small><b>{Vector(ValuesForSubject.Position)}</b></div>
                  <div><small>Rotation (°)</small><b>{Vector(ValuesForSubject.Rotation, 1)}</b></div>
                  <div><small>Scale (m)</small><b>{Vector(ValuesForSubject.Scale)}</b></div>
                </>}
                {Subject.Panel === "sun" && <>
                  <div><small>Local time</small><b>{Number(ValuesForSubject["Local Hours"] ?? 0).toFixed(2)} h</b></div>
                  <div><small>Ephemeris site</small><b>{Number(ValuesForSubject.Latitude ?? 0).toFixed(2)}°, {Number(ValuesForSubject.Longitude ?? 0).toFixed(2)}°</b></div>
                </>}
                {Subject.Panel === "atmosphere" && <>
                  <div><small>SkyView LUT</small><b>256 × 128 C++ parity</b></div>
                  <div><small>Cloud layer</small><b>Not in this world</b></div>
                </>}
              </div>
              {Material && (
                <div className="component-material">
                  <span style={{ background: Colour }} />
                  <div><small>OpenPBR material</small><b>{Material.Type}</b></div>
                  <p>{Colour} · roughness {Number(Material.Channels?.roughness?.Fill ?? 0).toFixed(2)}</p>
                </div>
              )}
            </>
          ) : <p className="component-empty">Select a component in the Outliner.</p>}
        </section>
      </div>
    </div>
  );
}
