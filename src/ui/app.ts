/**
 * Main Application Controller for Geological Cliff Generator
 */

import { CliffConfig, PipelineResult } from '../core/types';
import { GEOLOGICAL_PRESETS } from '../core/presets';
import { CliffPipeline } from '../core/pipeline';
import { MeshExporter } from '../core/exporter';
import { GeologicalViewer, RenderMode } from './viewer';

class GeologicalCliffApp {
  private viewer!: GeologicalViewer;
  private currentConfig: CliffConfig;
  private currentResult: PipelineResult | null = null;
  private selectedStage: number = 5;
  private isProcessing: boolean = false;

  constructor() {
    this.currentConfig = { ...GEOLOGICAL_PRESETS.sedimentary_canyon.config };
  }

  public init(): void {
    const viewportContainer = document.getElementById('viewport-container');
    if (!viewportContainer) throw new Error('Viewport container not found');

    this.viewer = new GeologicalViewer(viewportContainer);

    this.bindEvents();
    this.syncUIWithConfig();
    this.runPipeline();
  }

  private bindEvents(): void {
    // Preset buttons
    const presetButtons = document.querySelectorAll('.preset-btn');
    presetButtons.forEach((btn) => {
      btn.addEventListener('click', (e) => {
        const target = e.currentTarget as HTMLElement;
        const presetKey = target.dataset.preset;
        if (presetKey && GEOLOGICAL_PRESETS[presetKey]) {
          presetButtons.forEach((b) => b.classList.remove('active'));
          target.classList.add('active');
          this.currentConfig = { ...GEOLOGICAL_PRESETS[presetKey].config };
          this.syncUIWithConfig();
          this.runPipeline();
        }
      });
    });

    // Stage cards (1..5)
    const stageCards = document.querySelectorAll('.stage-card');
    stageCards.forEach((card) => {
      card.addEventListener('click', (e) => {
        const target = e.currentTarget as HTMLElement;
        const stageNum = parseInt(target.dataset.stage || '5', 10);
        this.selectedStage = stageNum;

        stageCards.forEach((c) => c.classList.remove('active'));
        target.classList.add('active');

        this.displaySelectedStage();
      });
    });

    // Render mode buttons
    const modeButtons = document.querySelectorAll('.mode-btn');
    modeButtons.forEach((btn) => {
      btn.addEventListener('click', (e) => {
        const target = e.currentTarget as HTMLElement;
        const mode = target.dataset.mode as RenderMode;
        if (mode) {
          modeButtons.forEach((b) => b.classList.remove('active'));
          target.classList.add('active');
          this.viewer.setRenderMode(mode);
        }
      });
    });

    // Sliders
    this.bindSlider('param-strataCount', 'val-strataCount', 'strataCount', (v) => Math.round(v));
    this.bindSlider('param-strataHardnessContrast', 'val-strataHardnessContrast', 'strataHardnessContrast', (v) => v.toFixed(2));
    this.bindSlider('param-overhangIntensity', 'val-overhangIntensity', 'overhangIntensity', (v) => v.toFixed(2));
    this.bindSlider('param-cutCount', 'val-cutCount', 'cutCount', (v) => Math.round(v));
    this.bindSlider('param-maxCutOffset', 'val-maxCutOffset', 'maxCutOffset', (v) => v.toFixed(2));
    this.bindSlider('param-jointSpacing', 'val-jointSpacing', 'jointSpacing', (v) => v.toFixed(1));
    this.bindSlider('param-jointDilation', 'val-jointDilation', 'jointDilation', (v) => v.toFixed(2));
    this.bindSlider('param-erosionStrength', 'val-erosionStrength', 'erosionStrength', (v) => v.toFixed(2));
    this.bindSlider('param-surfaceCrackDensity', 'val-surfaceCrackDensity', 'surfaceCrackDensity', (v) => v.toFixed(2));
    this.bindSlider('param-crackMaxDepthOffset', 'val-crackMaxDepthOffset', 'crackMaxDepthOffset', (v) => v.toFixed(2));

    // Recompute button
    document.getElementById('btn-recompute')?.addEventListener('click', () => {
      this.runPipeline();
    });

    // Randomize seed button
    document.getElementById('btn-randomize')?.addEventListener('click', () => {
      this.currentConfig.seed = Math.floor(Math.random() * 1000000);
      this.runPipeline();
    });

    // Export OBJ button
    document.getElementById('btn-export-obj')?.addEventListener('click', () => {
      this.exportModel('obj');
    });

    // Export PLY button
    document.getElementById('btn-export-ply')?.addEventListener('click', () => {
      this.exportModel('ply');
    });
  }

  private bindSlider(
    sliderId: string,
    valLabelId: string,
    configKey: keyof CliffConfig,
    formatFn: (val: number) => string | number
  ): void {
    const slider = document.getElementById(sliderId) as HTMLInputElement;
    const label = document.getElementById(valLabelId);

    if (slider && label) {
      slider.addEventListener('input', () => {
        const val = parseFloat(slider.value);
        label.textContent = String(formatFn(val));
        (this.currentConfig as any)[configKey] = val;
      });

      slider.addEventListener('change', () => {
        this.runPipeline();
      });
    }
  }

  private syncUIWithConfig(): void {
    const setVal = (id: string, valId: string, val: number, fmt: (v: number) => string | number) => {
      const el = document.getElementById(id) as HTMLInputElement;
      const lbl = document.getElementById(valId);
      if (el) el.value = String(val);
      if (lbl) lbl.textContent = String(fmt(val));
    };

    setVal('param-strataCount', 'val-strataCount', this.currentConfig.strataCount, (v) => Math.round(v));
    setVal('param-strataHardnessContrast', 'val-strataHardnessContrast', this.currentConfig.strataHardnessContrast, (v) => v.toFixed(2));
    setVal('param-overhangIntensity', 'val-overhangIntensity', this.currentConfig.overhangIntensity, (v) => v.toFixed(2));
    setVal('param-cutCount', 'val-cutCount', this.currentConfig.cutCount, (v) => Math.round(v));
    setVal('param-maxCutOffset', 'val-maxCutOffset', this.currentConfig.maxCutOffset, (v) => v.toFixed(2));
    setVal('param-jointSpacing', 'val-jointSpacing', this.currentConfig.jointSpacing, (v) => v.toFixed(1));
    setVal('param-jointDilation', 'val-jointDilation', this.currentConfig.jointDilation, (v) => v.toFixed(2));
    setVal('param-erosionStrength', 'val-erosionStrength', this.currentConfig.erosionStrength, (v) => v.toFixed(2));
    setVal('param-surfaceCrackDensity', 'val-surfaceCrackDensity', this.currentConfig.surfaceCrackDensity, (v) => v.toFixed(2));
    setVal('param-crackMaxDepthOffset', 'val-crackMaxDepthOffset', this.currentConfig.crackMaxDepthOffset, (v) => v.toFixed(2));
  }

  private runPipeline(): void {
    if (this.isProcessing) return;
    this.isProcessing = true;

    const overlay = document.getElementById('loading-overlay');
    overlay?.classList.add('active');

    // Run asynchronously to allow UI render
    setTimeout(() => {
      try {
        this.currentResult = CliffPipeline.execute(this.currentConfig);
        this.displaySelectedStage();
      } catch (err) {
        console.error('Error running geological pipeline:', err);
      } finally {
        this.isProcessing = false;
        overlay?.classList.remove('active');
      }
    }, 20);
  }

  private displaySelectedStage(): void {
    if (!this.currentResult) return;

    const stageIdx = this.selectedStage - 1;
    const stage = this.currentResult.stages[stageIdx];
    if (!stage) return;

    // Display mesh in viewer
    this.viewer.displayStage(stage);

    // Update stats HUD
    const statVerts = document.getElementById('stat-vertices');
    const statTris = document.getElementById('stat-triangles');
    const statTime = document.getElementById('stat-time');
    const statManifold = document.getElementById('stat-manifold');

    if (statVerts) statVerts.textContent = stage.metrics.vertexCount.toLocaleString();
    if (statTris) statTris.textContent = stage.metrics.triangleCount.toLocaleString();
    if (statTime) statTime.textContent = `${this.currentResult.totalTimeMs} ms`;
    if (statManifold) statManifold.textContent = stage.metrics.isManifold ? 'Manifold' : 'Checked';

    // Update bottom banner
    const bannerTitle = document.getElementById('banner-title');
    const bannerDesc = document.getElementById('banner-desc');

    if (bannerTitle) bannerTitle.textContent = stage.stageName;
    if (bannerDesc) bannerDesc.textContent = stage.description;
  }

  private exportModel(format: 'obj' | 'ply'): void {
    if (!this.currentResult) return;

    const stageIdx = this.selectedStage - 1;
    const stage = this.currentResult.stages[stageIdx];
    const mesh = stage.mesh;

    let content = '';
    let filename = `Geological_Cliff_${this.currentConfig.formationType}_Stage${this.selectedStage}.${format}`;
    let mimeType = 'text/plain';

    if (format === 'obj') {
      content = MeshExporter.toOBJ(mesh, `Cliff_Stage_${this.selectedStage}`);
      mimeType = 'text/plain';
    } else {
      content = MeshExporter.toPLY(mesh);
      mimeType = 'text/plain';
    }

    const blob = new Blob([content], { type: mimeType });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = filename;
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);
  }
}

// Start application on DOM ready
window.addEventListener('DOMContentLoaded', () => {
  const app = new GeologicalCliffApp();
  app.init();
});
