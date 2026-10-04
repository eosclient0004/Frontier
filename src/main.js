import { TexturePanel } from "./TexturePanel.js";

window.addEventListener("DOMContentLoaded", () => {
  try {
    window.texturePanel = new TexturePanel();
  } catch (ErrorValue) {
    console.error(ErrorValue);
    const Status = document.querySelector("#status-ready");
    if (Status) Status.textContent = `Startup failed: ${ErrorValue.message}`;
  }
});
