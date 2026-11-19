//==========================================================================================================================================
//                                                  DOCKED COMPONENT VIEWER
//==========================================================================================================================================
// This is the original Component Viewer workspace, mounted in the World Mirror
// dock rather than a replacement summary/inspector.  Its own Components tree,
// Blueprint tabs, inspection viewport channels, construction graph, Details,
// compile action, simulation HUD, camera HUD, and compiled-data drawer remain
// intact.  The user requested that the old Content tab be removed; its source
// shell therefore exposes Components only.

import React, { useEffect, useRef } from "react";
import "./ComponentViewer.css";

const COMPONENT_VIEWER_URL = "../ComponentViewer/index.html";

export default function ComponentViewer({ Selected, Select }) {
  const Frame = useRef(null);

  useEffect(() => {
    const SyncSelection = () => {
      try {
        Frame.current?.contentWindow?.postMessage(
          { Type: "frontier-world-mirror-selection", Id: Selected },
          window.location.origin,
        );
      } catch {
        // The embedded editor can still be loading its own module graph.
      }
    };
    SyncSelection();
    const Retry = window.setTimeout(SyncSelection, 700);
    return () => window.clearTimeout(Retry);
  }, [Selected]);

  useEffect(() => {
    const ReceiveViewerSelection = (Event) => {
      if (
        Event.origin !== window.location.origin ||
        Event.source !== Frame.current?.contentWindow ||
        Event.data?.Type !== "frontier-component-viewer-selection"
      ) {
        return;
      }
      // BP_ControlVehicle's interior components all belong to the one World
      // Mirror mesh actor; its authored chase camera maps to the editor camera.
      Select?.(
        Event.data.ComponentName === "ChaseCamera"
          ? "camera"
          : "control-vehicle",
      );
    };
    window.addEventListener("message", ReceiveViewerSelection);
    return () => window.removeEventListener("message", ReceiveViewerSelection);
  }, [Select]);

  return (
    <div className="component-viewer-dock" aria-label="Component Viewer">
      <iframe
        ref={Frame}
        className="component-viewer-frame"
        title="Component Viewer"
        src={COMPONENT_VIEWER_URL}
        onLoad={() => {
          try {
            Frame.current?.contentWindow?.postMessage(
              { Type: "frontier-world-mirror-selection", Id: Selected },
              window.location.origin,
            );
          } catch {}
        }}
      />
    </div>
  );
}
