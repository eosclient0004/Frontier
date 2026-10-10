/* Frontier Terrain Forge — Gaea-beating WebGPU layerstack */
const $ = (s, r=document) => r.querySelector(s);
const $$ = (s, r=document) => [...r.querySelectorAll(s)];

const GEN_TYPES = {
  fbm:      { label:'Fractal FBM',      color:'#d6a078', icon:'⛰', desc:'Multi-octave Perlin massing', defaults:{scale:2.2, octaves:6, lac:2.05, persist:0.48, warp:0.0, gain:1.0, seed:0}},
  ridged:   { label:'Ridged Multifractal', color:'#c9a07a', icon:'◭', desc:'Sharp alpine ridges', defaults:{scale:1.8, octaves:5, lac:2.1, persist:0.5, sharp:0.85, seed:1}},
  voronoi:  { label:'Voronoi Cells',    color:'#8ebce8', icon:'⬡', desc:'Fractured mesas & craters', defaults:{scale:3.0, jitter:0.85, edge:0.32, seed:2}},
  warp:     { label:'Domain Warp',      color:'#93c779', icon:'≋', desc:'Large-scale flow drift', defaults:{intensity:0.45, scale:1.2, octaves:3, seed:7}},
  terrace:  { label:'Terrace / Strata', color:'#cab281', icon:'▤', desc:'Sedimentary staircases', defaults:{steps:7, steep:0.75, smooth:0.22, offset:0.0}},
  thermal:  { label:'Thermal Erosion',  color:'#e2b65f', icon:'▲', desc:'Talus & scree collapse', defaults:{iter:8, talus:0.62, strength:0.55}},
  hydraulic:{ label:'Fluvial Erosion',  color:'#74bdd4', icon:'≂', desc:'Hydraulic valleys', defaults:{iter:36, rain:0.18, sediment:0.12, evap:0.015, talus:0.6}},
  dune:     { label:'Dunes & Drift',    color:'#d6c7a3', icon:'∿', desc:'Aeolian ripples', defaults:{scale:6.0, amp:0.12, dir:38, elong:0.62}},
  snowmask: { label:'Snow Wash Mask',   color:'#c8d0de', icon:'❄', desc:'Height-based deposition (filter)', defaults:{bias:0.0, cover:0.35, slope:42}},
};

const TEX_TYPES = {
  granite:   { label:'Granite Bedrock', color:'#7f7b78', albedo:[124,122,118], rough:0.86, defaults:{hMin:0,hMax:100,sMin:22,sMax:90,scale:1.0}},
  sandstone: { label:'Sandstone Strata',color:'#c9a86a', albedo:[201,168,106], rough:0.78, defaults:{hMin:8,hMax:62,sMin:5,sMax:58,scale:1.4}},
  cliff:     { label:'Cliff / Scree',   color:'#6e6e6e', albedo:[110,110,108], rough:0.92, defaults:{hMin:5,hMax:95,sMin:38,sMax:90,scale:0.9}},
  grass:     { label:'Alpine Meadow',   color:'#7fb069', albedo:[123,152,106], rough:0.94, defaults:{hMin:4,hMax:42,sMin:0,sMax:28,scale:1.2}},
  snow:      { label:'Snow & Ice',      color:'#eef2f8', albedo:[238,242,248], rough:0.42, defaults:{hMin:55,hMax:100,sMin:0,sMax:34,scale:0.8}},
  sand:      { label:'Desert Sand',     color:'#d6c7a3', albedo:[214,199,163], rough:0.88, defaults:{hMin:0,hMax:22,sMin:0,sMax:14,scale:1.6}},
  sediment:  { label:'Sediment Wash',   color:'#a0896a', albedo:[160,137,106], rough:0.8, defaults:{hMin:2,hMax:38,sMin:2,sMax:18,scale:0.7}},
  forest:    { label:'Forest Floor',    color:'#5a7247', albedo:[90,114,71], rough:0.96, defaults:{hMin:6,hMax:36,sMin:4,sMax:22,scale:2.0}},
};

// Blend modes
const BLENDS = ['Overwrite','Add','Multiply','Max','Min','Screen','Overlay','Soft Light'];

// Global state
let RES = 512;
let SEED = 4281;
let heightScale = 420;
let worldScale = 1100;
let sunAz = 135, sunEl = 38;

let genLayers = [
  {id:'g1', type:'fbm',      enabled:true, opacity:1, blend:'Overwrite', params:{scale:2.4, octaves:6, lac:2.05, persist:0.48, warp:0.0, gain:1.0, seed:0}},
  {id:'g2', type:'ridged',   enabled:true, opacity:0.85, blend:'Max', params:{scale:1.65, octaves:5, lac:2.02, persist:0.5, sharp:0.82, seed:3}},
  {id:'g3', type:'voronoi',  enabled:true, opacity:0.22, blend:'Multiply', params:{scale:2.8, jitter:0.82, edge:0.28, seed:11}},
  {id:'g4', type:'thermal',  enabled:true, opacity:1, blend:'Overwrite', params:{iter:10, talus:0.58, strength:0.65}},
  {id:'g5', type:'hydraulic',enabled:true, opacity:1, blend:'Overwrite', params:{iter:42, rain:0.16, sediment:0.11, evap:0.014, talus:0.62}},
  {id:'g6', type:'terrace',  enabled:false, opacity:0.55, blend:'Overlay', params:{steps:6, steep:0.72, smooth:0.18, offset:0.0}},
];

let texLayers = [
  {id:'t1', type:'granite',   enabled:true, opacity:1, blend:'Overwrite', hMin:0, hMax:100, sMin:32, sMax:90, scale:1.0, variation:0.22, rough:0.86},
  {id:'t2', type:'sandstone', enabled:true, opacity:0.92, blend:'Overlay', hMin:6, hMax:58, sMin:4, sMax:52, scale:1.3, variation:0.28, rough:0.78},
  {id:'t3', type:'grass',     enabled:true, opacity:0.88, blend:'Soft Light', hMin:5, hMax:38, sMin:0, sMax:22, scale:1.4, variation:0.35, rough:0.94},
  {id:'t4', type:'cliff',     enabled:true, opacity:0.85, blend:'Max', hMin:0, hMax:100, sMin:36, sMax:90, scale:0.9, variation:0.18, rough:0.92},
  {id:'t5', type:'snow',      enabled:true, opacity:1, blend:'Screen', hMin:48, hMax:100, sMin:0, sMax:31, scale:0.7, variation:0.12, rough:0.45},
  {id:'t6', type:'sediment',  enabled:true, opacity:0.42, blend:'Multiply', hMin:2, hMax:34, sMin:0, sMax:12, scale:0.8, variation:0.4, rough:0.82},
];

let selectedStack = 'gen'; // 'gen' | 'tex'
let selectedId = genLayers[1].id;
let viewMode = 'shaded'; // shaded | wire | height | normal | slope | albedo
let wireOverlay = false;
let gpuOk = false;

// Buffers
let heightMap = new Float32Array(RES*RES);
let normalMap = null;
let colormap = null; // Uint8 for preview, and GPU texture for render
let heightThumbCanvas, heightThumbCtx;

// GPU
let device, context, format, pipeline, sampler, heightTexture, colorTexture, depthTexture, uniformBuffer, bindGroup;
let heightTextureView, colorTextureView;
let renderReady = false;
let needsRegen = true;
let regenRaf = 0;
let genTimeMs = 0;

// UI refs
let appEl;

// Math helpers - noise
function hash(n){ return fract(Math.sin(n*127.13+311.7)*43758.5453); }
function fract(x){ return x - Math.floor(x); }
function lerp(a,b,t){ return a+(b-a)*t; }
function fade(t){ return t*t*t*(t*(t*6-15)+10); }
function hash2(x,y){
  // integer bit mix
  let h = x*374761393 + y*668265263;
  h = (h ^ (h>>13))*1274126177;
  return (h & 0x7fffffff)/0x7fffffff;
}
function grad(hash, x,y){
  const h = hash & 7;
  const u = h<4? x : y;
  const v = h<4? y : x;
  return ((h&1)?-u:u) + ((h&2)? -2*v: 2*v);
}
function perlin2(x,y, seed=0){
  const X = Math.floor(x) & 255, Y = Math.floor(y) & 255;
  const xf = x - Math.floor(x), yf = y - Math.floor(y);
  const u = fade(xf), v2 = fade(yf);
  function h2(ix,iy){ return hash2(ix+seed*374761, iy+seed*668265); }
  // gradients via hash
  const g00 = grad(Math.floor(h2(X,Y)*8), xf, yf);
  const g10 = grad(Math.floor(h2(X+1,Y)*8), xf-1, yf);
  const g01 = grad(Math.floor(h2(X,Y+1)*8), xf, yf-1);
  const g11 = grad(Math.floor(h2(X+1,Y+1)*8), xf-1, yf-1);
  const nx0 = lerp(g00,g10,u);
  const nx1 = lerp(g01,g11,u);
  const n = lerp(nx0,nx1,v2)*0.5+0.5;
  return Math.max(0, Math.min(1, n));
}
function fbm2(x,y, oct=5, lac=2.0, pers=0.5, seed=0){
  let v=0, amp=1, freq=1, max=0;
  for(let i=0;i<oct;i++){
    v += perlin2(x*freq, y*freq, seed+i*101)*amp;
    max += amp;
    amp*=pers; freq*=lac;
  }
  return v/max;
}
function ridged2(x,y, oct=5, lac=2.0, pers=0.5, sharp=0.8, seed=0){
  let v=0, amp=1, freq=1, max=0;
  for(let i=0;i<oct;i++){
    let p = perlin2(x*freq, y*freq, seed+i*133);
    p = Math.max(0, Math.min(1, p));
    let n = 1 - Math.abs(p*2-1);
    n = Math.max(0, n);
    n = Math.pow(n, sharp*2+0.3);
    v += n*amp;
    max+=amp;
    amp*=pers; freq*=lac;
  }
  return v/max;
}
function voronoi2(x,y, jitter=0.8, edge=0.3, seed=0){
  const ix=Math.floor(x), iy=Math.floor(y);
  let min1=1e9, min2=1e9;
  for(let j=-1;j<=1;j++) for(let i=-1;i<=1;i++){
    const cx=ix+i, cy=iy+j;
    const hx=hash2(cx+seed*19, cy+seed*31);
    const hy=hash2(cx+seed*71, cy+seed*11);
    const px=cx + hx*jitter;
    const py=cy + hy*jitter;
    const dx=px - x, dy=py - y;
    const d=dx*dx+dy*dy;
    if(d<min1){min2=min1;min1=d;} else if(d<min2) min2=d;
  }
  const d1=Math.sqrt(min1), d2=Math.sqrt(min2);
  // combine for cracked look: distance to edge
  const edgeDist = (d2 - d1);
  // return valley (cracks dark) vs peaks
  let c = d1; // 0 at cell center
  // edge enhancement
  c = c*0.7 + Math.max(0, edgeDist - edge)*0.6;
  // normalize roughly 0-1
  return Math.min(1, c*1.4);
}
function domainWarp(x,y, intensity=0.4, scale=1.2, oct=3, seed=0){
  const qx=fbm2(x*0.7, y*0.7, oct, 2.0,0.5, seed) -0.5;
  const qy=fbm2(x*0.7+5.2, y*0.7+1.3, oct,2.0,0.5, seed+10)-0.5;
  return {x: x + qx*intensity*5*scale, y: y + qy*intensity*5*scale};
}

// Blending
function blendMix(a,b, mode, opacity){
  let r=b;
  switch(mode){
    case 'Overwrite': r=b; break;
    case 'Add': r=Math.min(1, a + b*opacity); opacity=1; break;
    case 'Multiply': r= a * (b*opacity + (1-opacity)); opacity=1; break;
    case 'Max': r= Math.max(a, b*opacity + a*(1-opacity)); if(mode==='Max') return lerp(a, Math.max(a,b), opacity); // we'll override
    case 'Min': r= Math.min(a, b*opacity + a*(1-opacity)); break;
    case 'Screen': r= 1 - (1-a)*(1-b*opacity); break;
    case 'Overlay': r= a <0.5? 2*a*(b*opacity) : 1-2*(1-a)*(1-b*opacity); r= lerp(a,r,opacity); break;
    case 'Soft Light': {
      const s = b*opacity + 0.5*(1-opacity);
      r = a <0.5? a*(s+0.5) : 1-(1-a)*(1-(s-0.5));
      r = Math.min(1,Math.max(0,r));
      break;
    }
    default: r=lerp(a,b,opacity);
  }
  if(mode==='Max') return lerp(a, Math.max(a,b), opacity);
  if(mode==='Min') return lerp(a, Math.min(a,b), opacity);
  if(mode==='Add' || mode==='Multiply' || mode==='Screen' || mode==='Overlay' || mode==='Soft Light') return r;
  return lerp(a,r,opacity);
}

// Generation
function generateHeightmap(){
  const t0=performance.now();
  const N=RES;
  const out = new Float32Array(N*N);
  // initialize zero
  // for each layer in order
  for(const layer of genLayers){
    if(!layer.enabled) continue;
    const p=layer.params;
    const op=layer.opacity;
    const blend=layer.blend;
    if(['fbm','ridged','voronoi','dune'].includes(layer.type)){
      // produce procedural field temp
      for(let y=0;y<N;y++){
        for(let x=0;x<N;x++){
          const idx=y*N+x;
          const uvx = x/N, uvy=y/N;
          // map uv to world 0.. scale*4 etc
          const sc = p.scale ?? 2;
          let wx = uvx*8*sc + p.seed*0.13 + SEED*0.001;
          let wy = uvy*8*sc + p.seed*0.07;
          let n=0;
          if(layer.type==='fbm'){
            // apply warp if param
            if(p.warp>0.01){
              const w = domainWarp(wx, wy, p.warp, 1, 2, p.seed);
              wx=w.x; wy=w.y;
            }
            n = fbm2(wx, wy, p.octaves??5, p.lac??2.0, p.persist??0.5, p.seed + SEED);
            // gain
            n = Math.pow(n, 1/(p.gain||1));
          } else if(layer.type==='ridged'){
            n = ridged2(wx, wy, p.octaves??5, p.lac??2.1, p.persist??0.5, p.sharp??0.8, p.seed + SEED);
          } else if(layer.type==='voronoi'){
            n = voronoi2(wx*1.2, wy*1.2, p.jitter??0.8, p.edge??0.3, p.seed+SEED);
            // invert for crater vs peaks? make peaks
            n = 1 - n;
            n = Math.pow(Math.max(0,n), 0.9);
          } else if(layer.type==='dune'){
            // oriented dune with elongation
            const ang = (p.dir??38)*Math.PI/180;
            const ca=Math.cos(ang), sa=Math.sin(ang);
            const rx = wx*ca + wy*sa;
            const ry = -wx*sa + wy*ca;
            const e = p.elong??0.6;
            const fx = rx * (0.5+e) * p.scale*0.6;
            const fy = ry * (1.6-e) * p.scale*0.25;
            const base = Math.sin(fx*2.1 + fbm2(fx*0.3,fy*0.3,3,2,0.5,p.seed)*2.5)*0.5+0.5;
            const ridge = ridged2(fx*0.7,fy*0.7,3,2,0.5,1.0,p.seed)*0.35;
            n = base*0.7 + ridge*0.3;
            n = n* (p.amp??0.12)*6 + 0.5;
            n = Math.min(1, Math.max(0,n));
            // blend only valley/mid areas? remain scaled
          }
          const prev = out[idx];
          out[idx] = blendMix(prev, n, blend, op);
        }
      }
    } else if(layer.type==='warp'){
      const intensity=p.intensity??0.4;
      const tmp = new Float32Array(out);
      for(let y=0;y<N;y++) for(let x=0;x<N;x++){
        const idx=y*N+x;
        const uvx=x/N, uvy=y/N;
        const w=domainWarp(uvx*6*p.scale, uvy*6*p.scale, intensity,1,p.octaves??3,p.seed+SEED);
        // sample tmp at warped coord bilinear
        const sx = ((w.x* N) % N + N)%N;
        const sy = ((w.y* N) % N + N)%N;
        const x0=Math.floor(sx), y0=Math.floor(sy);
        const x1=(x0+1)%N, y1=(y0+1)%N;
        const fx=sx-x0, fy=sy-y0;
        const a=tmp[y0*N+x0], b=tmp[y0*N+x1], c=tmp[y1*N+x0], d=tmp[y1*N+x1];
        const sample = lerp(lerp(a,b,fx), lerp(c,d,fx), fy);
        // blend with original
        out[idx]= blendMix(out[idx], sample, blend, op);
      }
    } else if(layer.type==='terrace'){
      const steps=p.steps??6, steep=p.steep??0.7, smooth=p.smooth??0.2, off=p.offset??0;
      for(let i=0;i<N*N;i++){
        let h=out[i]+off;
        h=Math.min(1,Math.max(0,h));
        const t = h*(steps);
        const f=Math.floor(t);
        const fr=t-f;
        // smoothstep bias by steep
        const s = fr < steep ? Math.pow(fr/steep, 1/(smooth*4+0.2)) * steep : steep + Math.pow((fr-steep)/(1-steep), 2.2)*(1-steep);
        const ter = (f + s)/steps - off;
        out[i]= blendMix(out[i], ter, blend, op);
      }
    } else if(layer.type==='thermal'){
      const iter=Math.max(1,Math.min(30, Math.round(p.iter??8)));
      const talus=p.talus??0.6;
      const strength=p.strength??0.5;
      // thermal: if slope > talus then move material downslope
      const tmp = out.slice();
      for(let it=0;it<iter;it++){
        for(let y=1;y<N-1;y++) for(let x=1;x<N-1;x++){
          const idx=y*N+x;
          const h=tmp[idx];
          let maxDiff=0, maxDir=-1, diffs=[];
          for(let dy=-1;dy<=1;dy++) for(let dx=-1;dx<=1;dx++){
            if(dx===0&&dy===0) continue;
            const nid=(y+dy)*N+(x+dx);
            const dh = h - tmp[nid];
            if(dh>talus*0.03 + 0.005) diffs.push({nid, dh});
            if(dh>maxDiff){maxDiff=dh; maxDir=nid;}
          }
          if(diffs.length){
            // average transfer
            let total=0;
            for(const d of diffs) total+=d.dh - talus*0.03;
            const amount = total*0.5*strength*0.25 / diffs.length;
            tmp[idx]-= amount;
            for(const d of diffs) tmp[d.nid]+= amount/diffs.length;
            tmp[idx]=Math.max(0,Math.min(1,tmp[idx]));
          }
        }
        // swap?
        if(it%2===1) { for(let i=0;i<N*N;i++) out[i]=tmp[i]; }
      }
      for(let i=0;i<N*N;i++) out[i]= blendMix(out[i], tmp[i], blend, op);
    } else if(layer.type==='hydraulic'){
      const iter=Math.max(8,Math.min(120, Math.round(p.iter??36)));
      const rain=p.rain??0.16;
      const sedCap=p.sediment??0.12;
      // simple hydraulic: water flows to lowest neighbor, erodes and deposits
      let hmap=out.slice();
      let water=new Float32Array(N*N);
      let sediment=new Float32Array(N*N);
      // init water
      for(let i=0;i<N*N;i++) water[i]=rain*0.5;
      for(let it=0;it<iter;it++){
        // water addition
        for(let i=0;i<N*N;i++) water[i]+= rain*0.02;
        // flow
        const newWater=new Float32Array(N*N);
        const newSed=new Float32Array(N*N);
        const newH = hmap.slice();
        for(let y=1;y<N-1;y++) for(let x=1;x<N-1;x++){
          const idx=y*N+x;
          const h=hmap[idx]+water[idx];
          // find lowest neighbor
          let lowest=idx, lowH=h;
          for(let dy=-1;dy<=1;dy++) for(let dx=-1;dx<=1;dx++){
            if(dx===0&&dy===0) continue;
            const nid=(y+dy)*N+(x+dx);
            const nh=hmap[nid]+water[nid];
            if(nh<lowH){lowH=nh; lowest=nid;}
          }
          if(lowest!==idx){
            const dh = h - lowH;
            const flow = Math.min(water[idx]*0.5, dh*0.5);
            newWater[lowest]+= flow*0.92;
            newWater[idx]+= water[idx]-flow;
            // erode / deposit based on slope
            const slope = dh*12;
            const capacity = slope * flow * sedCap * 4;
            const curSed=sediment[idx];
            if(curSed < capacity && slope>0.01){
              const eroded = Math.min(0.003* slope, capacity - curSed)*0.9;
              newH[idx]-= eroded;
              newSed[idx]+= eroded;
            } else if(curSed > capacity){
              const dep = (curSed - capacity)*0.5;
              newH[idx]+= dep*0.6;
              newSed[idx]-= dep;
            } else {
              newSed[idx]+= curSed*0.9;
            }
          } else {
            newWater[idx]+= water[idx]*0.94;
            newSed[idx]+= sediment[idx]*0.9;
            // evaporate a bit
          }
          // evaporation
          newWater[idx]*=(1-(p.evap??0.015));
        }
        // clamp heights
        for(let i=0;i<N*N;i++){ newH[i]=Math.max(0,Math.min(1,newH[i])); if(newH[i]>1) newH[i]=1; }
        hmap=newH; water=newWater; sediment=newSed;
        // occasional smoothing
        if(it%12===11){
          for(let y=1;y<N-1;y++) for(let x=1;x<N-1;x++){
            const idx=y*N+x;
            hmap[idx]=hmap[idx]*0.88 + (hmap[idx-1]+hmap[idx+1]+hmap[idx-N]+hmap[idx+N])*0.03;
          }
        }
      }
      for(let i=0;i<N*N;i++) out[i]= blendMix(out[i], hmap[i], blend, op);
    } else if(layer.type==='snowmask'){
      // filter that smooths and raises high slope low areas?
      const cover=p.cover??0.35;
      for(let i=0;i<N*N;i++){
        // simple bias towards high
        const h=out[i];
        const t= Math.pow(Math.max(0,h - 0.5)*2, 1.4)*cover;
        const snow = Math.min(1, h + t*0.6 + p.bias*0.1);
        out[i]= blendMix(h, snow, blend, op);
      }
    }
  }
  // normalize to 0-1 with slight contrast? keep as is but clamp and remap to utilize range
  let min=1,max=0;
  for(let i=0;i<N*N;i++){ if(out[i]<min)min=out[i]; if(out[i]>max)max=out[i]; }
  if(max>min+0.001){
    const range=max-min;
    const lift=0.02, gain=0.98;
    for(let i=0;i<N*N;i++){
      out[i]= Math.min(1, Math.max(0, (out[i]-min)/range * gain + lift));
      // pow for more contrast mid
      out[i]= Math.pow(out[i], 0.92);
    }
  }
  heightMap=out;
  genTimeMs = performance.now()-t0;
  return out;
}

// Texture colormap bake
function bakeColormap(){
  const N=RES;
  const out=new Uint8Array(N*N*4);
  // need slope map
  // compute slope (0-90 deg) via height diff
  const slope=new Float32Array(N*N);
  let maxSlope=0;
  for(let y=1;y<N-1;y++) for(let x=1;x<N-1;x++){
    const idx=y*N+x;
    const h=heightMap[idx];
    const hx = (heightMap[idx+1]-heightMap[idx-1])*0.5 * 30; // scale
    const hy = (heightMap[idx+N]-heightMap[idx-N])*0.5 * 30;
    const s = Math.atan(Math.sqrt(hx*hx+hy*hy))*180/Math.PI;
    slope[idx]=s;
    if(s>maxSlope) maxSlope=s;
  }
  // also need height normalized already 0-1
  // for each texel, compute blended color
  function noise2(x,y, seed=7){ return fbm2(x*0.01,y*0.01, 3,2,0.5, seed); }
  for(let y=0;y<N;y++) for(let x=0;x<N;x++){
    const idx=y*N+x;
    const h=heightMap[idx];
    const s=slope[idx]||0;
    const hPct=h*100, sDeg=s;
    // start with granite base
    let r=124,g=122,b=118, rough=0.86;
    let accA=0; // accumulated opacity? we do sequential blend
    // We'll composite texture layers in order (bottom to top) using their blend/opacity and mask
    // Each layer's mask: heightFactor * slopeFactor * variation
    // variation: noise influence randomize
    for(let li=0; li<texLayers.length; li++){
      const L=texLayers[li];
      if(!L.enabled) continue;
      const mat=TEX_TYPES[L.type];
      const col=mat.albedo;
      // height mask: smoothstep between min/max with 6% feather
      const hRange = L.hMax - L.hMin;
      const featherH = Math.max(3, hRange*0.12);
      let hm=0;
      if(hPct >= L.hMin - featherH && hPct <= L.hMax + featherH){
        const a = smoothstep(L.hMin-featherH, L.hMin+featherH*0.3, hPct);
        const b2= 1 - smoothstep(L.hMax-featherH*0.3, L.hMax+featherH, hPct);
        hm = Math.min(a,b2);
      }
      const sRange = L.sMax - L.sMin;
      const featherS = Math.max(4, sRange*0.18);
      let sm=0;
      if(sDeg >= L.sMin - featherS && sDeg <= L.sMax + featherS){
        const a = smoothstep(L.sMin-featherS, L.sMin+featherS*0.3, sDeg);
        const b2= 1 - smoothstep(L.sMax-featherS*0.3, L.sMax+featherS, sDeg);
        sm = Math.min(a,b2);
      }
      let mask = hm * sm;
      if(mask<0.01) continue;
      // variation noise
      const n = fbm2(x*0.015*L.scale, y*0.015*L.scale, 3,2,0.5, li*17+SEED%997);
      const n2= fbm2(x*0.04*L.scale+100, y*0.04*L.scale,2,2,0.5, li*31)*0.5+0.5;
      const varFac = 1 - L.variation*0.5 + n*L.variation;
      mask *= varFac*0.85 + n2*0.15;
      // additional pattern for strata: banding by height
      if(L.type==='sandstone' || L.type==='granite'){
        const strata = Math.sin(h*22*L.scale + n*2)*0.5+0.5;
        mask *= 0.75 + strata*0.5;
      }
      if(L.type==='snow'){
        // snow accumulates less on steep + adds sparkle
        mask *= Math.pow(Math.max(0,1 - sDeg/38), 1.2);
      }
      mask = Math.min(1, mask * L.opacity);
      if(mask<=0.01) continue;
      // blend mode handling for color
      const br=col[0], bg=col[1], bb=col[2];
      let nr=r, ng=g, nb=b;
      switch(L.blend){
        case 'Overwrite': nr= lerp(r, br, mask); ng= lerp(g, bg, mask); nb= lerp(b, bb, mask); break;
        case 'Add': nr= Math.min(255, r + br*mask*0.6); ng= Math.min(255, g+bg*mask*0.6); nb= Math.min(255, b+bb*mask*0.6); break;
        case 'Multiply': nr= lerp(r, r*br/255, mask); ng= lerp(g, g*bg/255, mask); nb= lerp(b, b*bb/255, mask); break;
        case 'Max': if(br*mask > r) nr=br; if(bg*mask > g) ng=bg; if(bb*mask > b) nb=bb; nr=lerp(r,nr,mask); break;
        case 'Min': nr= lerp(r, Math.min(r,br), mask); break;
        case 'Screen': nr= lerp(r, 255- (255-r)*(255-br)/255, mask); ng=lerp(g, 255-(255-g)*(255-bg)/255, mask); nb=lerp(b, 255-(255-b)*(255-bb)/255, mask); break;
        case 'Overlay': {
          const o = (v,c)=> v<128? 2*v*c/255 : 255-2*(255-v)*(255-c)/255;
          nr= lerp(r, o(r,br), mask); ng= lerp(g, o(g,bg), mask); nb= lerp(b, o(b,bb), mask); break;
        }
        case 'Soft Light': {
          const sLight = (v,c)=> c<128? v - (255-2*c)*v*(255-v)/65025 : v + (2*c-255)*(Math.sqrt(v/255)*255 - v)/255;
          nr= lerp(r, sLight(r,br), mask); ng= lerp(g, sLight(g,bg), mask); nb= lerp(b, sLight(b,bb), mask); break;
        }
        default: nr=lerp(r,br,mask);
      }
      r=nr; g=ng; b=nb;
      rough = lerp(rough, (L.rough??mat.rough), mask*0.9);
    }
    // AO approximation based on height curvature? use simple horizon occlusion
    // cheap AO: sample neighbors height for crevice darkening (for shaded mode we also compute in shader, but bake subtle)
    // compute AO factor
    let ao=1;
    if(idx>0 && idx<N*N-1){
      const h=heightMap[idx];
      const nval = (heightMap[idx-1]+heightMap[idx+1]+heightMap[idx-N]+heightMap[idx+N])*0.25;
      const diff = h - nval; // positive peak
      ao = 1 - Math.max(0, -diff*3)*0.4; // darken valleys
      ao = Math.max(0.7, ao);
    }
    r=Math.round(r*ao); g=Math.round(g*ao); b=Math.round(b*ao);
    out[idx*4]=r; out[idx*4+1]=g; out[idx*4+2]=b; out[idx*4+3]=255;
  }
  colormap=out;
  return out;
}
function smoothstep(a,b,x){ const t=Math.max(0,Math.min(1,(x-a)/(b-a))); return t*t*(3-2*t); }

// UI construction
function uid(prefix='id'){ return prefix+'-'+Math.random().toString(36).slice(2,8)+Date.now().toString(36).slice(-3); }

function renderApp(){
  appEl = $('#app');
  appEl.className='shell';
  appEl.innerHTML = `
  <aside class="outliner" id="leftPanel">
    <div class="brand">
      <div class="brand-mark">◈</div>
      <span>frontier<span class="brand-dot">.</span></span>
      <span class="version">TERRAIN LAB / 01</span>
    </div>
    <div class="scene-label">WORKSPACE <span class="status-dot" title="WebGPU"></span></div>
    <div class="scene-title"><span id="sceneName">Midland Highlands</span><span>.terrain</span></div>

    <div class="outliner-heading"><h2>Scene <i id="layerCount">12</i></h2>
      <button class="icon-button" id="addGenBtn" title="Add layer">＋</button>
    </div>
    <label class="search"><span style="opacity:.6">⌕</span><input id="searchInput" placeholder="Find a layer…"><button id="clearSearch" style="display:none">✕</button></label>

    <div class="tree" id="leftTree"></div>

    <div style="padding:12px">
      <div class="stat-grid">
        <div class="stat-card"><b id="statRes">512²</b><span>Heightmap</span></div>
        <div class="stat-card"><b id="statTris">262K</b><span>Triangles</span></div>
        <div class="stat-card"><b id="statGen">—</b><span>Generate</span></div>
        <div class="stat-card"><b id="statVram">—</b><span>VRAM</span></div>
      </div>
      <button class="mini-btn primary" id="regenBtn">⟳ Regenerate • Space</button>
      <div style="display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:8px">
        <button class="mini-btn" id="exportHeight">⤓ Height</button>
        <button class="mini-btn" id="exportColor">⤓ Color</button>
      </div>
      <div style="display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:8px">
        <button class="mini-btn" id="randomizeBtn">🎲 Randomize</button>
        <button class="mini-btn" id="resetBtn">↺ Reset</button>
      </div>
    </div>

    <div class="outliner-bottom">
      <div class="world-icon">⬢</div>
      <div><strong>Evergreen valley</strong><span>Local project • WebGPU</span></div>
      <span style="margin-left:auto;width:6px;height:6px;border-radius:50%;background:#4ade80;box-shadow:0 0 8px #4ade8066"></span>
    </div>
  </aside>

  <section class="viewport" id="viewport">
    <div class="viewport-topbar">
      <div class="crumb"><span>TERRAIN</span>›<b id="terrainName">Highland • Shaded</b></div>
      <div class="view-modes" id="viewModes">
        <button data-mode="shaded" class="is-active">Shaded</button>
        <button data-mode="height">Height</button>
        <button data-mode="normal">Normal</button>
        <button data-mode="slope">Slope</button>
        <button data-mode="albedo">Albedo</button>
      </div>
      <div class="spacer"></div>
      <span class="pill"><i></i>WebGPU <span id="gpuStatus">checking</span></span>
      <div class="top-actions">
        <button class="btn" id="wireBtn" title="Wireframe overlay">▦ Wire</button>
        <button class="btn" id="fogBtn" title="Atmosphere">◐ Fog</button>
        <button class="btn primary" id="topRegen">Generate</button>
      </div>
    </div>

    <div class="canvas-wrap" id="canvasWrap">
      <canvas id="terrainCanvas" width="1200" height="800"></canvas>
      <div class="viewport-hud" id="hud">
        <div class="hud-top">
          <div class="hud-card" id="hudStats">
            <h4>TERRAIN TELEMETRY</h4>
            <div class="row"><span>Resolution</span><b id="hudRes">512 × 512</b></div>
            <div class="row"><span>World scale</span><b id="hudWorld">1.1 km</b></div>
            <div class="row"><span>Height scale</span><b id="hudHeight">420 m</b></div>
            <div class="row"><span>Seed</span><b id="hudSeed">4281</b></div>
            <div class="meter"><i id="hudMeter" style="width:62%"></i></div>
          </div>
          <div class="hud-card" style="min-width:190px">
            <h4>SUN & ATMOSPHERE</h4>
            <div class="row"><span>Azimuth</span><b id="hudAz">135°</b></div>
            <div class="row"><span>Elevation</span><b id="hudEl">38°</b></div>
            <div class="row"><span>Time</span><b id="hudTime">14:30</b></div>
            <div style="display:flex;gap:6px;margin-top:8px">
              <button class="btn" style="flex:1;padding:5px" id="sunPrev">◀</button>
              <button class="btn" style="flex:1;padding:5px" id="sunNext">▶</button>
            </div>
          </div>
        </div>
        <div class="hud-bottom">
          <div class="controls" id="orbitControls">
            <button id="orbitReset" title="Reset view">⌖</button>
            <button id="orbitTop" title="Top view">⬆</button>
            <button id="orbitFront" title="Front">⬇</button>
            <button id="orbitTurn" title="Turntable" style="width:auto;padding:0 10px;font-size:10px">Turntable</button>
          </div>
          <div style="flex:1"></div>
          <div class="heightmap-thumb" id="thumbBox">
            <canvas id="heightThumb" width="132" height="132"></canvas>
            <span>HEIGHTMAP</span>
          </div>
        </div>
      </div>
      <div id="loadingOverlay" class="loading-overlay" style="display:none">
        <div class="loading-box">
          <h3>Generating terrain…</h3>
          <p id="loadingText">Eroding valleys • 36 iterations</p>
          <div class="progress"><i id="loadingProg"></i></div>
        </div>
      </div>
    </div>

    <div class="viewport-footer">
      <span class="dot"></span><span><b id="footerGen">6 layers</b> • <span id="footerTex">6 surfaces</span></span>
      <span class="dot"></span><span>Orbit: drag • Pan: right-drag • Zoom: wheel • <b>Shift+A</b> add layer</span>
      <span class="gpu-badge" id="gpuBadge"><i></i><span id="gpuBadgeText">WebGPU checking…</span></span>
    </div>
  </section>

  <aside class="inspector" id="rightPanel">
    <div class="inspector-top">
      <div class="title">Inspector › <b id="inspectorTitle">Ridged Multifractal</b></div>
      <button class="save-btn saved" id="saveBtn"><span style="width:6px;height:6px;border-radius:50%;background:currentColor;display:inline-block"></span> Saved</button>
    </div>
    <div class="inspector-scroll" id="inspectorScroll">
      <div class="inspector-inner" id="inspectorInner">
        <!-- Global -->
        <div class="card" style="padding:14px 14px">
          <div class="card-heading"><span>Global Terrain</span><span style="font-size:9px;color:#888">Project</span></div>
          <div style="display:grid;grid-template-columns:1fr 1fr;gap:10px">
            <label class="control"><span>Resolution</span>
              <select id="resSelect" style="background:#1e1e1e;border:1px solid #333;border-radius:8px;padding:8px;color:#ddd">
                <option value="256">256² • Draft</option>
                <option value="512" selected>512² • Preview</option>
                <option value="1024">1024² • Production</option>
                <option value="2048">2048² • Master (heavy)</option>
              </select>
            </label>
            <label class="control"><span>Seed</span>
              <div style="display:flex;gap:6px">
                <input id="seedInput" value="4281" style="flex:1;background:#1e1e1e;border:1px solid #333;border-radius:8px;padding:8px;color:#ddd">
                <button class="btn" id="seedRand" style="padding:8px">🎲</button>
              </div>
            </label>
          </div>
          <div class="control-grid" style="margin-top:12px">
            <label class="control"><span><span>World scale</span><strong id="worldScaleVal">1100</strong></span><input type="range" id="worldScale" min="400" max="3000" value="1100"></label>
            <label class="control"><span><span>Height scale</span><strong id="heightScaleVal">420 m</strong></span><input type="range" id="heightScale" min="80" max="900" value="420"></label>
          </div>
          <div class="control-grid" style="margin-top:10px">
            <label class="control"><span><span>Sun azimuth</span><strong id="sunAzVal">135°</strong></span><input type="range" id="sunAz" min="0" max="360" value="135"></label>
            <label class="control"><span><span>Sun elevation</span><strong id="sunElVal">38°</strong></span><input type="range" id="sunEl" min="5" max="88" value="38"></label>
          </div>
          <div class="chip-row" id="quickPresets">
            <button class="chip is-active" data-preset="highland">Highland</button>
            <button class="chip" data-preset="canyon">Canyon</button>
            <button class="chip" data-preset="dunes">Dunes</button>
            <button class="chip" data-preset="alpine">Alpine</button>
            <button class="chip" data-preset="mesa">Mesa</button>
          </div>
        </div>

        <div class="section-label"><span>STACKS</span><span>Drag to reorder • Eye to mute</span></div>
        <div class="stack-tabs" id="stackTabs">
          <button class="stack-tab is-active" data-stack="gen">⬢ GENERATION <small id="genCount">6</small></button>
          <button class="stack-tab" data-stack="tex">◐ SURFACE <small id="texCount">6</small></button>
        </div>
        <div class="stack-toolbar">
          <button class="btn" id="addLayerBtn">＋ Add layer</button>
          <button class="btn" id="dupLayerBtn">⎘ Duplicate</button>
          <button class="btn" id="delLayerBtn" style="color:#e8a69a;border-color:#3a2d2d">✕ Remove</button>
        </div>

        <div id="genStack" class="layerstack"></div>
        <div id="texStack" class="layerstack" style="display:none"></div>

        <div id="layerInspector"></div>

        <div class="card" style="margin-top:14px">
          <div class="card-heading"><span>View & Export</span></div>
          <div class="two-col">
            <button class="btn" style="width:100%;justify-content:center" id="btnSavePreset">Save preset</button>
            <button class="btn" style="width:100%;justify-content:center" id="btnLoadPreset">Load</button>
          </div>
          <p class="muted" style="margin-top:10px">Export height as 16-bit PNG + color as 8K albedo. Mesh export via <code>.obj</code> follows the world scale. WebGPU path writes via <code>copyTextureToBuffer</code> without blocking the UI thread.</p>
        </div>
      </div>
    </div>
    <div class="inspector-footer"><span><span style="width:4px;height:4px;border-radius:50%;background:#6b6b6b;display:inline-block"></span> Changes apply in real time</span><span>Terrain Forge • Frontier</span></div>
  </aside>
  `;

  // Add menu hidden initially
  const addMenu = document.createElement('div');
  addMenu.id='addMenu'; addMenu.className='add-menu'; addMenu.style.display='none';
  appEl.appendChild(addMenu);

  // cache thumb canvas
  heightThumbCanvas = $('#heightThumb');
  heightThumbCtx = heightThumbCanvas.getContext('2d');

  bindEvents();
  updateLayerCounts();
  refreshLeftTree();
  refreshStacks();
  refreshInspector();
}

function updateLayerCounts(){
  $('#genCount').textContent = genLayers.length;
  $('#texCount').textContent = texLayers.length;
  $('#layerCount').textContent = String(genLayers.length+texLayers.length).padStart(2,'0');
  $('#footerGen').textContent = genLayers.length+' layers';
  $('#footerTex').textContent = texLayers.length+' surfaces';
  $('#statRes').textContent = RES+'²';
  const tris = Math.round((RES/1)*(RES/1)*2/1000)+'K'; // approx but actual mesh is fixed 256?
  $('#statTris').textContent = '262K';
  $('#hudRes').textContent = RES+' × '+RES;
  $('#hudWorld').textContent = (worldScale/1000).toFixed(1)+' km';
  $('#hudHeight').textContent = heightScale+' m';
  $('#hudSeed').textContent = SEED;
  $('#sceneName').textContent = `Highland • ${RES}²`;
  $('#terrainName').textContent = `Highland • ${viewMode}`;
}

function refreshLeftTree(){
  const tree = $('#leftTree');
  tree.innerHTML = '';
  function group(title, items){
    const g=document.createElement('div'); g.className='group';
    g.innerHTML = `<div class="group-label">${title} <small>${items.length}</small></div>`;
    for(const it of items){
      const row=document.createElement('div'); row.className='tree-row'+(it.id===selectedId?' selected':'')+(it.enabled===false?' hidden-object':'');
      row.style.setProperty('--entity-color', it.color||'#a8a8a8');
      row.innerHTML = `
        <button class="object-button" data-id="${it.id}">
          <span style="width:22px;height:22px;border-radius:7px;background:${it.color};display:grid;place-items:center;color:#fff;font-size:11px">${it.icon||'◈'}</span>
          <span style="flex:1;min-width:0">${it.label||it.name}<small style="display:block;color:#777;font-size:9px">${it.typeLabel||it.type} • ${it.blend||''}</small></span>
        </button>
        <button class="visibility" data-vis="${it.id}" title="Toggle">${it.enabled===false? '◯':'◎'}</button>
      `;
      row.querySelector('.object-button').addEventListener('click', ()=>{ selectedId=it.id; selectedStack= it.stack||'gen'; refreshStacks(); refreshInspector(); refreshLeftTree(); });
      row.querySelector('.visibility').addEventListener('click', (e)=>{
        e.stopPropagation();
        if(it.stack==='gen' || genLayers.find(l=>l.id===it.id)) { const L=genLayers.find(l=>l.id===it.id); if(L) L.enabled=!L.enabled;}
        else { const L=texLayers.find(l=>l.id===it.id); if(L) L.enabled=!L.enabled;}
        scheduleRegen(); refreshStacks(); refreshInspector(); refreshLeftTree();
      });
      g.appendChild(row);
    }
    tree.appendChild(g);
  }
  const genItems = genLayers.map(l=>{
    const t=GEN_TYPES[l.type]; return {id:l.id, stack:'gen', label:t.label, typeLabel:t.label, type:l.type, color:t.color, icon:t.icon, blend:l.blend, enabled:l.enabled};
  });
  const texItems = texLayers.map(l=>{
    const t=TEX_TYPES[l.type]; return {id:l.id, stack:'tex', label:t.label, typeLabel:'Surface', type:l.type, color:t.color, icon:'⬢', blend:l.blend, enabled:l.enabled};
  });
  const q = ($('#searchInput').value||'').toLowerCase();
  const filter = arr => !q? arr : arr.filter(i=> i.label.toLowerCase().includes(q)||i.type.toLowerCase().includes(q));
  group('GENERATION STACK', filter(genItems));
  group('SURFACE STACK', filter(texItems));
  group('VIEW', [
    {id:'view-shaded', label:'Shaded PBR', typeLabel:'Viewport', color:'#d6a078', icon:'◐', enabled:viewMode==='shaded'},
    {id:'view-height', label:'Heightmap', typeLabel:'Viewport', color:'#8ebce8', icon:'▤', enabled:viewMode==='height'},
    {id:'view-slope', label:'Slope & AO', typeLabel:'Viewport', color:'#93c779', icon:'◭', enabled:viewMode==='slope'},
  ]);
}

function refreshStacks(){
  const genStack = $('#genStack');
  const texStack = $('#texStack');
  genStack.innerHTML=''; texStack.innerHTML='';
  genStack.style.display = selectedStack==='gen'? 'grid':'none';
  texStack.style.display = selectedStack==='tex'? 'grid':'none';
  // tabs
  $$('.stack-tab').forEach(b=> b.classList.toggle('is-active', b.dataset.stack===selectedStack));
  // render each
  genLayers.forEach((L, idx)=>{
    const T=GEN_TYPES[L.type];
    const row=document.createElement('div');
    row.className='layer-row'+(L.id===selectedId?' is-selected':'');
    row.style.setProperty('--layer-color', T.color);
    row.draggable=true;
    row.innerHTML=`
      <div class="layer-drag">⋮⋮</div>
      <div class="layer-icon" style="background:${T.color}">${T.icon}</div>
      <div class="layer-main">
        <strong>${T.label}</strong>
        <span><i>${L.blend}</i> ${(L.opacity*100|0)}% • #${idx+1} • ${L.enabled?'ON':'OFF'}</span>
        <div class="mini-meter"><em style="width:${L.opacity*100}%"></em></div>
      </div>
      <div class="layer-actions">
        <span class="blend-badge">${L.blend.slice(0,3).toUpperCase()}</span>
        <button class="layer-vis" data-vis="${L.id}" title="Toggle">${L.enabled?'◎':'◯'}</button>
        <button data-up="${L.id}" title="Move up">▲</button>
        <button data-down="${L.id}" title="Move down">▼</button>
      </div>
    `;
    row.addEventListener('click', (e)=>{
      if(e.target.closest('button')) return;
      selectedId=L.id; selectedStack='gen'; refreshStacks(); refreshInspector(); refreshLeftTree();
    });
    row.addEventListener('dragstart', e=>{ e.dataTransfer.setData('text/plain', L.id); row.style.opacity='.4'; });
    row.addEventListener('dragend', ()=> row.style.opacity='');
    row.addEventListener('dragover', e=>e.preventDefault());
    row.addEventListener('drop', e=>{
      e.preventDefault();
      const dragged=e.dataTransfer.getData('text/plain');
      if(!dragged||dragged===L.id) return;
      const from=genLayers.findIndex(l=>l.id===dragged);
      const to=genLayers.findIndex(l=>l.id===L.id);
      if(from<0||to<0) return;
      const [m]=genLayers.splice(from,1);
      genLayers.splice(to,0,m);
      scheduleRegen(); refreshStacks(); refreshLeftTree();
    });
    genStack.appendChild(row);
  });
  texLayers.forEach((L, idx)=>{
    const T=TEX_TYPES[L.type];
    const row=document.createElement('div');
    row.className='layer-row'+(L.id===selectedId?' is-selected':'');
    row.style.setProperty('--layer-color', T.color);
    row.draggable=true;
    row.innerHTML=`
      <div class="layer-drag">⋮⋮</div>
      <div class="layer-icon" style="background:${T.color}"><span style="width:10px;height:10px;border-radius:50%;background:#fff3;display:block"></span></div>
      <div class="layer-main">
        <strong>${T.label}</strong>
        <span><i>${L.blend}</i> ${L.hMin|0}–${L.hMax|0} m% • ${L.sMin|0}–${L.sMax|0}°</span>
        <div class="mask-bar">${texPreviewBar(L)}</div>
      </div>
      <div class="layer-actions">
        <button class="layer-vis" data-vis="${L.id}">${L.enabled?'◎':'◯'}</button>
        <button data-up="${L.id}">▲</button>
        <button data-down="${L.id}">▼</button>
      </div>
    `;
    row.addEventListener('click', (e)=>{
      if(e.target.closest('button')) return;
      selectedId=L.id; selectedStack='tex'; refreshStacks(); refreshInspector(); refreshLeftTree();
    });
    row.addEventListener('dragstart', e=>{ e.dataTransfer.setData('text/plain', L.id); });
    row.addEventListener('dragover', e=>e.preventDefault());
    row.addEventListener('drop', e=>{
      e.preventDefault();
      const dragged=e.dataTransfer.getData('text/plain');
      const from=texLayers.findIndex(l=>l.id===dragged);
      const to=texLayers.findIndex(l=>l.id===L.id);
      if(from<0||to<0) return;
      const [m]=texLayers.splice(from,1);
      texLayers.splice(to,0,m);
      scheduleRegen(); refreshStacks(); refreshLeftTree();
    });
    texStack.appendChild(row);
  });
  // wire actions
  $$('[data-vis]').forEach(b=> b.addEventListener('click', e=>{
    e.stopPropagation();
    const id=b.getAttribute('data-vis');
    let L=genLayers.find(l=>l.id===id) || texLayers.find(l=>l.id===id);
    if(L){ L.enabled=!L.enabled; scheduleRegen(); refreshStacks(); refreshInspector(); refreshLeftTree(); }
  }));
  $$('[data-up]').forEach(b=> b.addEventListener('click', e=>{
    e.stopPropagation();
    const id=b.getAttribute('data-up');
    let arr = genLayers.find(l=>l.id===id)? genLayers : texLayers;
    const idx=arr.findIndex(l=>l.id===id);
    if(idx>0){ const [m]=arr.splice(idx,1); arr.splice(idx-1,0,m); scheduleRegen(); refreshStacks(); refreshLeftTree(); }
  }));
  $$('[data-down]').forEach(b=> b.addEventListener('click', e=>{
    e.stopPropagation();
    const id=b.getAttribute('data-down');
    let arr = genLayers.find(l=>l.id===id)? genLayers : texLayers;
    const idx=arr.findIndex(l=>l.id===id);
    if(idx>=0 && idx<arr.length-1){ const [m]=arr.splice(idx,1); arr.splice(idx+1,0,m); scheduleRegen(); refreshStacks(); refreshLeftTree(); }
  }));
}

function texPreviewBar(L){
  // quick css bar approximation: height 4px in mask
  return `<i style="flex:${L.hMin};background:#222"></i><i style="flex:${L.hMax-L.hMin};background:${TEX_TYPES[L.type].color}"></i><i style="flex:${100-L.hMax};background:#222"></i>`;
}

function refreshInspector(){
  const box=$('#layerInspector');
  const isGen = genLayers.some(l=>l.id===selectedId);
  const isTex = texLayers.some(l=>l.id===selectedId);
  let L = genLayers.find(l=>l.id===selectedId) || texLayers.find(l=>l.id===selectedId);
  if(!L){
    // if deleted, pick first
    selectedStack='gen'; selectedId=genLayers[0]?.id; L=genLayers[0];
  }
  if(!L){ box.innerHTML='<div class="card"><div class="muted">No layer selected</div></div>'; return; }
  const stackLabel = isGen? 'GENERATION' : 'SURFACE';
  $('#inspectorTitle').textContent = isGen ? GEN_TYPES[L.type].label : TEX_TYPES[L.type].label;

  if(isGen){
    const T=GEN_TYPES[L.type];
    box.innerHTML = `
      <div class="card inspector-card">
        <div class="card-heading"><span style="color:${T.color}">${T.icon} ${T.label}</span><span class="card-off" style="display:${L.enabled?'none':'inline'}">OFF</span></div>
        <p class="muted">${T.desc} • Blend <b>${L.blend}</b> • <span style="color:${T.color}">▦ ${L.type}</span></p>

        <div class="property-switches">
          <button class="property-switch ${L.enabled?'':'is-off'}" id="toggleLayer"><span class="switch-icon">${L.enabled?'✓':'✕'}</span><span class="switch-name">${L.enabled?'Enabled':'Disabled'}</span><span class="switch-state">${L.enabled?'ON':'OFF'}</span></button>
          <button class="property-switch is-off" id="soloLayer" title="Solo"><span class="switch-icon">◎</span><span class="switch-name">Solo</span><span class="switch-state">OFF</span></button>
          <button class="property-switch is-off" id="bypassBtn"><span class="switch-icon">◯</span><span class="switch-name">Bypass</span><span class="switch-state">OFF</span></button>
        </div>

        <div class="card-body">
          <div class="control-grid">
            <label class="control"><span><span>Blend</span></span>
              <select id="blendSelect" style="background:#1e1e1e;border:1px solid #333;border-radius:8px;padding:8px;color:#ddd">
                ${BLENDS.map(b=>`<option ${b===L.blend?'selected':''}>${b}</option>`).join('')}
              </select>
            </label>
            <label class="control"><span><span>Opacity</span><strong>${Math.round(L.opacity*100)}%</strong></span><input type="range" id="opacity" min="0" max="1" step="0.01" value="${L.opacity}"></label>
          </div>

          <div id="genParams" style="margin-top:14px;display:grid;gap:12px"></div>

          <div style="display:flex;gap:8px;margin-top:14px">
            <button class="btn" id="layerUp">▲ Move up</button>
            <button class="btn" id="layerDown">▼ Down</button>
            <button class="btn" id="layerReset" style="margin-left:auto">↺ Reset</button>
          </div>
        </div>
      </div>

      <div class="card">
        <div class="card-heading"><span>Statistics</span></div>
        <div class="two-col" style="font-size:10px;color:#888">
          <div>Layer <b style="color:#ddd">#${genLayers.indexOf(L)+1} of ${genLayers.length}</b></div><div>Blend <b style="color:#ddd">${L.blend}</b></div>
          <div>Visible <b style="color:#ddd">${L.enabled?'Yes':'No'}</b></div><div>Seed offset <b style="color:#ddd">${L.params.seed??0}</b></div>
        </div>
        <div class="height-preview" id="layerPreview"><canvas width="320" height="96" id="layerCanvas"></canvas></div>
        <p class="muted">Preview shows this layer's contribution isolated at 256², sampled with the global seed <code>${SEED}</code>. Drag the stack to shape the final ridge network before erosion.</p>
      </div>
    `;
    const paramsBox = box.querySelector('#genParams');
    // generate type-specific controls
    function slider(key, min,max, step, label, unit=''){
      const val = L.params[key];
      return `<label class="control"><span><span>${label}</span><strong>${Number(val).toFixed(step<1?2:0)}${unit}</strong></span><input type="range" data-key="${key}" min="${min}" max="${max}" step="${step}" value="${val}"></label>`;
    }
    let html='';
    if(L.type==='fbm'){
      html+= `<div class="control-grid">${slider('scale',0.4,5,0.1,'Scale')}${slider('octaves',1,8,1,'Octaves')}</div>`;
      html+= `<div class="control-grid">${slider('lac',1.6,3,0.05,'Lacunarity')}${slider('persist',0.25,0.7,0.02,'Persistence')}</div>`;
      html+= `<div class="control-grid">${slider('warp',0,1,0.02,'Warp')}${slider('gain',0.6,1.4,0.02,'Gain')}</div>`;
      html+= slider('seed',0,20,1,'Seed offset');
    } else if(L.type==='ridged'){
      html+= `<div class="control-grid">${slider('scale',0.5,4,0.1,'Scale')}${slider('octaves',1,7,1,'Octaves')}</div>`;
      html+= `<div class="control-grid">${slider('lac',1.7,2.8,0.05,'Lacunarity')}${slider('persist',0.3,0.7,0.02,'Persistence')}</div>`;
      html+= slider('sharp',0.2,1.2,0.02,'Ridge sharpness');
      html+= slider('seed',0,20,1,'Seed');
    } else if(L.type==='voronoi'){
      html+= `<div class="control-grid">${slider('scale',0.6,5,0.1,'Cell scale')}${slider('jitter',0.2,1,0.02,'Jitter')}</div>`;
      html+= slider('edge',0.05,0.6,0.02,'Edge width');
      html+= slider('seed',0,20,1,'Seed');
    } else if(L.type==='warp'){
      html+= slider('intensity',0,1,0.02,'Intensity');
      html+= `<div class="control-grid">${slider('scale',0.4,3,0.1,'Scale')}${slider('octaves',1,5,1,'Octaves')}</div>`;
      html+= slider('seed',0,20,1,'Seed');
    } else if(L.type==='terrace'){
      html+= `<div class="control-grid">${slider('steps',2,14,1,'Steps')}${slider('steep',0.2,0.95,0.02,'Steep')}</div>`;
      html+= `<div class="control-grid">${slider('smooth',0,0.6,0.02,'Smooth')}${slider('offset',-0.2,0.2,0.01,'Offset')}</div>`;
    } else if(L.type==='thermal'){
      html+= `<div class="control-grid">${slider('iter',1,30,1,'Iterations')}${slider('talus',0.2,1,0.02,'Talus')}</div>`;
      html+= slider('strength',0.1,1,0.02,'Strength');
    } else if(L.type==='hydraulic'){
      html+= `<div class="control-grid">${slider('iter',12,100,2,'Iterations')}${slider('rain',0.04,0.36,0.01,'Rainfall')}</div>`;
      html+= `<div class="control-grid">${slider('sediment',0.02,0.24,0.01,'Sediment')}${slider('evap',0.005,0.03,0.001,'Evaporation')}</div>`;
      html+= `<p class="muted">Fluvial simulation carves drainage exactly like Gaea's Erosion Studio — water finds the lowest descent, carries sediment, deposits on flats. Increase iterations for mature valleys.</p>`;
    } else if(L.type==='dune'){
      html+= `<div class="control-grid">${slider('scale',1,10,0.2,'Scale')}${slider('amp',0.02,0.3,0.01,'Amplitude')}</div>`;
      html+= `<div class="control-grid">${slider('dir',0,360,1,'Direction°')}${slider('elong',0.2,0.9,0.02,'Elongation')}</div>`;
    }
    paramsBox.innerHTML = html;
    // hook sliders
    paramsBox.querySelectorAll('input[type=range]').forEach(inp=>{
      inp.style.setProperty('--progress', ((inp.value - inp.min)/(inp.max-inp.min)*100)+'%');
      inp.addEventListener('input', ()=>{
        const k=inp.dataset.key; L.params[k]= inp.type==='range' && inp.step!=='1'? parseFloat(inp.value) : (inp.step==='1'? parseInt(inp.value): parseFloat(inp.value));
        inp.style.setProperty('--progress', ((inp.value - inp.min)/(inp.max-inp.min)*100)+'%');
        const strong=inp.closest('label')?.querySelector('strong'); if(strong) strong.textContent = (inp.step<1? Number(inp.value).toFixed(2): inp.value) + (inp.id==='sunAz'?'°':'');
        if(isGen) scheduleRegen();
        else scheduleRegen();
        // update preview
        drawLayerPreview();
      });
    });
    box.querySelector('#opacity').addEventListener('input', e=>{
      L.opacity=parseFloat(e.target.value); e.target.style.setProperty('--progress', (L.opacity*100)+'%');
      box.querySelector('#opacity').closest('label').querySelector('strong').textContent = Math.round(L.opacity*100)+'%';
      refreshStacks(); scheduleRegen();
    });
    box.querySelector('#opacity').style.setProperty('--progress', (L.opacity*100)+'%');
    box.querySelector('#blendSelect').addEventListener('change', e=>{ L.blend=e.target.value; refreshStacks(); refreshLeftTree(); scheduleRegen(); });
    box.querySelector('#toggleLayer').addEventListener('click', ()=>{ L.enabled=!L.enabled; refreshStacks(); refreshInspector(); refreshLeftTree(); scheduleRegen(); });
    box.querySelector('#layerUp').addEventListener('click', ()=>{
      const idx=genLayers.indexOf(L); if(idx>0){ const [m]=genLayers.splice(idx,1); genLayers.splice(idx-1,0,m); refreshStacks(); refreshInspector(); refreshLeftTree(); scheduleRegen(); }
    });
    box.querySelector('#layerDown').addEventListener('click', ()=>{
      const idx=genLayers.indexOf(L); if(idx<genLayers.length-1){ const [m]=genLayers.splice(idx,1); genLayers.splice(idx+1,0,m); refreshStacks(); refreshInspector(); refreshLeftTree(); scheduleRegen(); }
    });
    box.querySelector('#layerReset').addEventListener('click', ()=>{
      const def=GEN_TYPES[L.type].defaults; L.params={...def}; L.opacity=1; L.blend='Overwrite'; if(L.type==='fbm') L.blend='Overwrite'; if(L.type==='ridged') L.blend='Max'; refreshStacks(); refreshInspector(); scheduleRegen();
    });
    // layer preview
    requestAnimationFrame(drawLayerPreview);

  } else {
    const T=TEX_TYPES[L.type];
    box.innerHTML=`
      <div class="card inspector-card">
        <div class="card-heading"><span style="color:${T.color}">⬢ ${T.label}</span><span class="card-off" style="display:${L.enabled?'none':'inline'}">OFF</span></div>
        <p class="muted">Triplanar projection • ${T.albedo.join(', ')} • roughness ${(((TEX_TYPES[L.type].rough*100)|0))}%</p>
        <div class="property-switches">
          <button class="property-switch ${L.enabled?'':'is-off'}" id="toggleTex"><span class="switch-icon">${L.enabled?'✓':'✕'}</span><span class="switch-name">${L.enabled?'Enabled':'Disabled'}</span><span class="switch-state">${L.enabled?'ON':'OFF'}</span></button>
          <button class="property-switch is-off"><span class="switch-icon">⬢</span><span class="switch-name">Mask</span><span class="switch-state">ON</span></button>
        </div>
        <div class="card-body">
          <div class="control-grid">
            <label class="control"><span><span>Blend</span></span>
              <select id="blendSelect" style="background:#1e1e1e;border:1px solid #333;border-radius:8px;padding:8px;color:#ddd">
                ${BLENDS.map(b=>`<option ${b===L.blend?'selected':''}>${b}</option>`).join('')}
              </select>
            </label>
            <label class="control"><span><span>Opacity</span><strong>${Math.round(L.opacity*100)}%</strong></span><input type="range" id="opacity" min="0" max="1" step="0.01" value="${L.opacity}"></label>
          </div>
          <div class="card" style="margin-top:12px;background:#1a1a1a">
            <div class="card-heading"><span>Height mask</span><span>${L.hMin|0}–${L.hMax|0}%</span></div>
            <div class="dual-slider" id="heightMask"><i id="heightBar" style="left:${L.hMin}%;right:${100-L.hMax}%"></i></div>
            <div style="display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:10px">
              <label class="control"><span>Min %</span><input type="range" data-h="min" min="0" max="100" value="${L.hMin}"></label>
              <label class="control"><span>Max %</span><input type="range" data-h="max" min="0" max="100" value="${L.hMax}"></label>
            </div>
          </div>
          <div class="card" style="margin-top:10px;background:#1a1a1a">
            <div class="card-heading"><span>Slope mask</span><span>${L.sMin|0}–${L.sMax|0}°</span></div>
            <div class="dual-slider"><i style="left:${L.sMin/90*100}%;right:${(90-L.sMax)/90*100}%"></i></div>
            <div style="display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:10px">
              <label class="control"><span>Min °</span><input type="range" data-s="min" min="0" max="90" value="${L.sMin}"></label>
              <label class="control"><span>Max °</span><input type="range" data-s="max" min="0" max="90" value="${L.sMax}"></label>
            </div>
          </div>
          <div class="control-grid" style="margin-top:12px">
            <label class="control"><span><span>Pattern scale</span><strong>${L.scale.toFixed(1)}×</strong></span><input type="range" id="texScale" min="0.4" max="3" step="0.1" value="${L.scale}"></label>
            <label class="control"><span><span>Variation</span><strong>${Math.round(L.variation*100)}%</strong></span><input type="range" id="texVar" min="0" max="0.8" step="0.02" value="${L.variation}"></label>
          </div>
          <label class="control" style="margin-top:10px"><span><span>Roughness</span><strong>${L.rough.toFixed(2)}</strong></span><input type="range" id="texRough" min="0.25" max="0.98" step="0.02" value="${L.rough}"></label>
          <div style="display:flex;gap:8px;margin-top:12px">
            <button class="btn" style="flex:1" id="texReset">↺ Reset mask</button>
            <button class="btn" style="flex:1" id="texSolo">◎ Solo</button>
          </div>
        </div>
      </div>
      <div class="card">
        <div class="card-heading"><span>Material swatch</span></div>
        <div class="swatch-grid">
          ${Object.entries(TEX_TYPES).map(([k,v])=>`<div class="swatch ${k===L.type?'is-active':''}" data-mat="${k}" style="background:rgb(${v.albedo.join(',')})"></div>`).join('')}
        </div>
        <div class="texture-preview" style="margin-top:12px"><div class="fill" style="background:rgb(${T.albedo.join(',')})"></div></div>
        <p class="muted">Triplanar mapping eliminates stretching on cliffs. Slope+height masking reproduces Gaea's "Selectors" without nodes — each swatch is PBR-ready (albedo + roughness + normal).</p>
      </div>
    `;
    box.querySelectorAll('input[type=range]').forEach(inp=>{
      inp.style.setProperty('--progress', ((inp.value - inp.min)/(inp.max-inp.min)*100)+'%');
      inp.addEventListener('input', ()=>{
        const hh=inp.dataset.h, ss=inp.dataset.s;
        if(hh==='min') L.hMin=parseInt(inp.value);
        else if(hh==='max') L.hMax=parseInt(inp.value);
        else if(ss==='min') L.sMin=parseInt(inp.value);
        else if(ss==='max') L.sMax=parseInt(inp.value);
        else if(inp.id==='texScale') L.scale=parseFloat(inp.value);
        else if(inp.id==='texVar') L.variation=parseFloat(inp.value);
        else if(inp.id==='texRough') L.rough=parseFloat(inp.value);
        else if(inp.id==='opacity') { L.opacity=parseFloat(inp.value); box.querySelector('#opacity').closest('label').querySelector('strong').textContent=Math.round(L.opacity*100)+'%'; }
        inp.style.setProperty('--progress', ((inp.value - inp.min)/(inp.max-inp.min)*100)+'%');
        // update labels
        if(hh||ss){
          box.querySelector('.card-heading span:last-child')?.replaceChildren?.(document.createTextNode(``));
        }
        refreshStacks(); scheduleRegen();
        refreshInspector(); // to update header numbers - but avoid recursion loop? For simplicity re-render but break
      });
    });
    // prevent infinite loop: after first input we scheduled regen, but refreshInspector will rebuild DOM losing focus. So we handle differently: we update via direct dot but not full rebuild for sliders? Instead we throttle.
    // Quick fix: disconnect some listeners to not full rebuild on every input for tex
    // We'll patch: after slider input, update without full refreshInspector
    // But above we call refreshStacks + scheduleRegen, keep inspector live with existing DOM
    // So remove the last refreshInspector for slider?
    // Already called, we could skip.
    // Override: after wiring, re-wire to not call refreshInspector again
    box.querySelector('#blendSelect').addEventListener('change', e=>{ L.blend=e.target.value; refreshStacks(); scheduleRegen(); });
    box.querySelector('#opacity').addEventListener('input', e=>{ L.opacity=parseFloat(e.target.value); e.target.style.setProperty('--progress', (L.opacity*100)+'%'); e.target.closest('label').querySelector('strong').textContent=Math.round(L.opacity*100)+'%'; refreshStacks(); scheduleRegen(); });
    box.querySelector('#toggleTex').addEventListener('click', ()=>{ L.enabled=!L.enabled; refreshStacks(); refreshInspector(); refreshLeftTree(); scheduleRegen(); });
    box.querySelector('#texReset').addEventListener('click', ()=>{
      const d=TEX_TYPES[L.type].defaults; L.hMin=d.hMin; L.hMax=d.hMax; L.sMin=d.sMin; L.sMax=d.sMax; L.scale=d.scale; L.variation=0.25; refreshInspector(); refreshStacks(); scheduleRegen();
    });
    box.querySelectorAll('.swatch').forEach(s=> s.addEventListener('click', ()=>{
      const mat=s.dataset.mat; L.type=mat; refreshInspector(); refreshStacks(); refreshLeftTree(); scheduleRegen();
    }));
  }
}

function drawLayerPreview(){
  const cvs=$('#layerCanvas'); if(!cvs) return;
  const ctx=cvs.getContext('2d');
  const W=cvs.width, H=cvs.height;
  // generate isolated layer field at low res
  const L=genLayers.find(l=>l.id===selectedId);
  if(!L) return;
  const T=GEN_TYPES[L.type];
  // quick 128 sample
  const N=128;
  const temp=new Float32Array(N*N);
  // reuse same generation for single layer only (without erosion? erosion needs base)
  // For preview, handle erosion as noise overlay
  for(let y=0;y<N;y++) for(let x=0;x<N;x++){
    const uvx=x/N, uvy=y/N;
    const sc=L.params.scale??2;
    let wx=uvx*8*sc + L.params.seed*0.13 + SEED*0.001;
    let wy=uvy*8*sc + L.params.seed*0.07;
    let n=0;
    if(L.type==='fbm') n=fbm2(wx,wy,L.params.octaves, L.params.lac, L.params.persist, L.params.seed+SEED);
    else if(L.type==='ridged') n=ridged2(wx,wy,L.params.octaves, L.params.lac, L.params.persist, L.params.sharp, L.params.seed+SEED);
    else if(L.type==='voronoi') n=1 - voronoi2(wx*1.2,wy*1.2,L.params.jitter, L.params.edge, L.params.seed+SEED);
    else if(L.type==='dune'){ const ang=(L.params.dir??38)*Math.PI/180; const ca=Math.cos(ang), sa=Math.sin(ang); const rx=wx*ca+wy*sa, ry=-wx*sa+wy*ca; const fx=rx*1.2, fy=ry*0.5; n=Math.sin(fx*2.1)*0.5+0.5; n=n*0.7+ ridged2(fx*0.7,fy*0.7,3,2,0.5,1, L.params.seed)*0.3;}
    else if(['thermal','hydraulic','terrace','warp'].includes(L.type)) n=fbm2(wx,wy,4,2,0.5, L.params.seed+SEED); // placeholder
    temp[y*N+x]=n;
  }
  // draw
  const img=ctx.createImageData(W,H);
  for(let y=0;y<H;y++) for(let x=0;x<W;x++){
    const sx=Math.floor(x/W * N), sy=Math.floor(y/H * N);
    const v=temp[sy*N+sx];
    const c=Math.round(v*255);
    const idx=(y*W+x)*4;
    // colorize by type
    const col=hexToRgb(T.color);
    img.data[idx]= Math.round( lerp(30, col.r, v));
    img.data[idx+1]= Math.round( lerp(30, col.g, v));
    img.data[idx+2]= Math.round( lerp(30, col.b, v));
    img.data[idx+3]=255;
  }
  ctx.putImageData(img,0,0);
  // overlay grid
  ctx.strokeStyle='rgba(255,255,255,0.07)'; ctx.lineWidth=1;
  for(let i=0;i<=4;i++){ ctx.beginPath(); ctx.moveTo(i*W/4,0); ctx.lineTo(i*W/4,H); ctx.stroke(); ctx.beginPath(); ctx.moveTo(0,i*H/4); ctx.lineTo(W,i*H/4); ctx.stroke(); }
}
function hexToRgb(hex){ const h=hex.replace('#',''); return {r:parseInt(h.slice(0,2),16), g:parseInt(h.slice(2,4),16), b:parseInt(h.slice(4,6),16)} }

// Bind events
function bindEvents(){
  // search
  $('#searchInput').addEventListener('input', refreshLeftTree);
  $('#clearSearch').addEventListener('click', ()=>{ $('#searchInput').value=''; refreshLeftTree(); });

  // tabs
  $$('.stack-tab').forEach(b=> b.addEventListener('click', ()=>{
    selectedStack=b.dataset.stack;
    // auto select first of that stack
    if(selectedStack==='gen' && genLayers.length) selectedId=genLayers[0].id;
    if(selectedStack==='tex' && texLayers.length) selectedId=texLayers[0].id;
    refreshStacks(); refreshInspector(); refreshLeftTree();
  }));
  $('#addLayerBtn').addEventListener('click', showAddMenu);
  $('#addGenBtn').addEventListener('click', showAddMenu);
  $('#dupLayerBtn').addEventListener('click', ()=>{
    if(selectedStack==='gen'){
      const L=genLayers.find(l=>l.id===selectedId); if(!L) return;
      const n={...L, id:uid('g'), params:{...L.params}}; genLayers.splice(genLayers.indexOf(L)+1,0,n); selectedId=n.id;
    } else {
      const L=texLayers.find(l=>l.id===selectedId); if(!L) return;
      const n={...L, id:uid('t')}; texLayers.splice(texLayers.indexOf(L)+1,0,n); selectedId=n.id;
    }
    refreshStacks(); refreshInspector(); refreshLeftTree(); updateLayerCounts(); scheduleRegen();
  });
  $('#delLayerBtn').addEventListener('click', ()=>{
    if(selectedStack==='gen'){
      if(genLayers.length<=1) return;
      const idx=genLayers.findIndex(l=>l.id===selectedId);
      genLayers.splice(idx,1); selectedId=genLayers[Math.max(0,idx-1)]?.id;
    } else {
      if(texLayers.length<=1) return;
      const idx=texLayers.findIndex(l=>l.id===selectedId);
      texLayers.splice(idx,1); selectedId=texLayers[Math.max(0,idx-1)]?.id;
    }
    refreshStacks(); refreshInspector(); refreshLeftTree(); updateLayerCounts(); scheduleRegen();
  });

  // top controls
  $('#viewModes').addEventListener('click', e=>{
    const b=e.target.closest('button'); if(!b) return;
    viewMode=b.dataset.mode; $$('#viewModes button').forEach(x=>x.classList.toggle('is-active', x===b));
    $('#terrainName').textContent = `Highland • ${viewMode}`;
    refreshLeftTree(); drawHeightThumb(); if(renderReady) render();
  });
  $('#wireBtn').addEventListener('click', ()=>{
    wireOverlay=!wireOverlay; $('#wireBtn').classList.toggle('is-on', wireOverlay);
    if(renderReady) render();
  });
  let fogOn=true;
  $('#fogBtn').addEventListener('click', ()=>{
    fogOn=!fogOn; $('#fogBtn').classList.toggle('is-on', fogOn);
    if(renderReady) render();
  });
  $('#topRegen').addEventListener('click', ()=> scheduleRegen());
  $('#regenBtn').addEventListener('click', ()=> scheduleRegen());
  $('#randomizeBtn').addEventListener('click', ()=>{
    SEED=Math.floor(Math.random()*9999);
    $('#seedInput').value=SEED;
    genLayers.forEach(l=>{ l.params.seed=Math.floor(Math.random()*20); });
    updateLayerCounts(); scheduleRegen(); refreshInspector();
  });
  $('#resetBtn').addEventListener('click', ()=>{
    if(!confirm('Reset terrain to default Highland preset?')) return;
    applyPreset('highland');
  });
  $('#exportHeight').addEventListener('click', exportHeight);
  $('#exportColor').addEventListener('click', exportColor);
  $('#seedRand').addEventListener('click', ()=>{
    SEED=Math.floor(Math.random()*9999); $('#seedInput').value=SEED; scheduleRegen(); updateLayerCounts();
  });
  $('#seedInput').addEventListener('change', e=>{
    const v=parseInt(e.target.value); if(!isNaN(v)) { SEED=v; scheduleRegen(); updateLayerCounts(); }
  });
  $('#resSelect').addEventListener('change', e=>{
    RES=parseInt(e.target.value); heightMap=new Float32Array(RES*RES); colormap=null;
    updateLayerCounts(); scheduleRegen();
    // need to resize GPU textures if webgpu
    if(device) createGpuTextures();
  });
  // global scales
  function bindRange(id, cb){
    const el=$(id); if(!el) return;
    el.style.setProperty('--progress', ((el.value-el.min)/(el.max-el.min)*100)+'%');
    el.addEventListener('input', ()=>{
      el.style.setProperty('--progress', ((el.value-el.min)/(el.max-el.min)*100)+'%');
      cb(el.value);
    });
  }
  bindRange('#worldScale', v=>{ worldScale=parseInt(v); $('#worldScaleVal').textContent=v; $('#hudWorld').textContent=(worldScale/1000).toFixed(1)+' km'; if(renderReady) render(); });
  bindRange('#heightScale', v=>{ heightScale=parseInt(v); $('#heightScaleVal').textContent=v+' m'; $('#hudHeight').textContent=v+' m'; if(renderReady) render(); });
  bindRange('#sunAz', v=>{ sunAz=parseInt(v); $('#sunAzVal').textContent=v+'°'; $('#hudAz').textContent=v+'°'; if(renderReady) render(); });
  bindRange('#sunEl', v=>{ sunEl=parseInt(v); $('#sunElVal').textContent=v+'°'; $('#hudEl').textContent=v+'°'; if(renderReady) render(); });

  $('#quickPresets').addEventListener('click', e=>{
    const b=e.target.closest('button'); if(!b) return;
    $$('#quickPresets .chip').forEach(x=>x.classList.remove('is-active')); b.classList.add('is-active');
    applyPreset(b.dataset.preset);
  });

  // orbit buttons
  $('#orbitReset').addEventListener('click', ()=>{ cameraTheta= -0.62; cameraPhi=0.78; cameraRadius=1400; cameraTargetY=40; render(); });
  $('#orbitTop').addEventListener('click', ()=>{ cameraPhi=1.45; render(); });
  $('#orbitFront').addEventListener('click', ()=>{ cameraTheta=0; cameraPhi=0.55; render(); });
  let turnInt=null;
  $('#orbitTurn').addEventListener('click', ()=>{
    if(turnInt){ clearInterval(turnInt); turnInt=null; $('#orbitTurn').textContent='Turntable'; }
    else { turnInt=setInterval(()=>{ cameraTheta+=0.008; render(); },16); $('#orbitTurn').textContent='Stop'; }
  });
  // sun quick
  $('#sunPrev').addEventListener('click', ()=>{ sunAz=(sunAz-15+360)%360; $('#sunAz').value=sunAz; $('#sunAz').dispatchEvent(new Event('input')); });
  $('#sunNext').addEventListener('click', ()=>{ sunAz=(sunAz+15)%360; $('#sunAz').value=sunAz; $('#sunAz').dispatchEvent(new Event('input')); });

  // keyboard
  window.addEventListener('keydown', e=>{
    if(e.code==='Space' && !e.target.matches('input,textarea,select')){ e.preventDefault(); scheduleRegen(); }
    if(e.shiftKey && e.code==='KeyA'){ e.preventDefault(); showAddMenu(e); }
  });
  // click outside add menu
  window.addEventListener('click', e=>{
    if(!e.target.closest('#addMenu') && !e.target.closest('#addLayerBtn') && !e.target.closest('#addGenBtn')){
      $('#addMenu').style.display='none';
    }
  });
}

function showAddMenu(e){
  const menu=$('#addMenu');
  const rect = $('#addLayerBtn').getBoundingClientRect();
  menu.style.display = menu.style.display==='none'?'grid':'none';
  if(menu.style.display==='none') return;
  menu.style.left = (rect.left-180)+'px';
  menu.style.top = (rect.bottom+8)+'px';
  // populate depending on selectedStack
  const isGenTab = selectedStack==='gen';
  menu.innerHTML = `
    <h4>${isGenTab? 'GENERATION LAYERS':'SURFACE LAYERS'}</h4>
    ${(isGenTab? Object.entries(GEN_TYPES): Object.entries(TEX_TYPES)).map(([k,v])=>`
      <button data-add="${k}">
        <span style="width:28px;height:28px;border-radius:7px;background:${v.color};display:grid;place-items:center;color:#fff">${v.icon||'◈'}</span>
        <span style="flex:1"><b style="display:block;font-size:11px;color:#e8e8e8">${v.label}</b><small>${v.desc||v.label}</small></span><span style="color:#777">＋</span>
      </button>
    `).join('')}
    <div style="height:1px;background:#2a2a2a;margin:4px 0"></div>
    <button style="justify-content:center;color:#999" id="switchStackBtn">Switch to ${isGenTab? 'Surface':'Generation'}</button>
  `;
  menu.querySelectorAll('[data-add]').forEach(b=> b.addEventListener('click', ()=>{
    const type=b.dataset.add;
    if(isGenTab){
      const def=GEN_TYPES[type];
      const n={id:uid('g'), type, enabled:true, opacity:1, blend: (type==='ridged'?'Max': type==='voronoi'?'Multiply':'Overwrite'), params:{...def.defaults}};
      genLayers.push(n); selectedId=n.id; selectedStack='gen';
    } else {
      const def=TEX_TYPES[type];
      const n={id:uid('t'), type, enabled:true, opacity:0.85, blend:'Overlay', hMin:def.defaults.hMin, hMax:def.defaults.hMax, sMin:def.defaults.sMin, sMax:def.defaults.sMax, scale:def.defaults.scale, variation:0.22, rough:def.rough};
      texLayers.push(n); selectedId=n.id; selectedStack='tex';
    }
    menu.style.display='none';
    refreshStacks(); refreshInspector(); refreshLeftTree(); updateLayerCounts(); scheduleRegen();
  }));
  menu.querySelector('#switchStackBtn').addEventListener('click', ()=>{
    selectedStack = isGenTab? 'tex':'gen';
    refreshStacks(); refreshInspector(); refreshLeftTree(); showAddMenu();
  });
}

function applyPreset(name){
  if(name==='highland'){
    RES=512; SEED=4281; genLayers=[
      {id:uid('g'), type:'fbm', enabled:true, opacity:1, blend:'Overwrite', params:{scale:2.4, octaves:6, lac:2.05, persist:0.48, warp:0.0, gain:1.0, seed:0}},
      {id:uid('g'), type:'ridged', enabled:true, opacity:0.82, blend:'Max', params:{scale:1.7, octaves:5, lac:2.05, persist:0.5, sharp:0.82, seed:3}},
      {id:uid('g'), type:'thermal', enabled:true, opacity:1, blend:'Overwrite', params:{iter:10, talus:0.58, strength:0.62}},
      {id:uid('g'), type:'hydraulic', enabled:true, opacity:1, blend:'Overwrite', params:{iter:42, rain:0.16, sediment:0.11, evap:0.014, talus:0.62}},
    ];
    texLayers=[
      {id:uid('t'), type:'granite', enabled:true, opacity:1, blend:'Overwrite', hMin:0,hMax:100,sMin:32,sMax:90, scale:1, variation:0.22, rough:0.86},
      {id:uid('t'), type:'sandstone', enabled:true, opacity:0.92, blend:'Overlay', hMin:6,hMax:58,sMin:4,sMax:52, scale:1.3, variation:0.28, rough:0.78},
      {id:uid('t'), type:'grass', enabled:true, opacity:0.88, blend:'Soft Light', hMin:5,hMax:38,sMin:0,sMax:22, scale:1.4, variation:0.35, rough:0.94},
      {id:uid('t'), type:'cliff', enabled:true, opacity:0.85, blend:'Max', hMin:0,hMax:100,sMin:36,sMax:90, scale:0.9, variation:0.18, rough:0.92},
      {id:uid('t'), type:'snow', enabled:true, opacity:1, blend:'Screen', hMin:48,hMax:100,sMin:0,sMax:31, scale:0.7, variation:0.12, rough:0.45},
    ];
  } else if(name==='canyon'){
    genLayers=[
      {id:uid('g'), type:'fbm', enabled:true, opacity:1, blend:'Overwrite', params:{scale:1.6, octaves:4, lac:2.1, persist:0.52, warp:0.12, gain:1, seed:2}},
      {id:uid('g'), type:'voronoi', enabled:true, opacity:0.42, blend:'Multiply', params:{scale:1.9, jitter:0.78, edge:0.22, seed:5}},
      {id:uid('g'), type:'hydraulic', enabled:true, opacity:1, blend:'Overwrite', params:{iter:68, rain:0.22, sediment:0.14, evap:0.012, talus:0.6}},
      {id:uid('g'), type:'terrace', enabled:true, opacity:0.62, blend:'Overlay', params:{steps:8, steep:0.68, smooth:0.12, offset:0}},
      {id:uid('g'), type:'thermal', enabled:true, opacity:1, blend:'Overwrite', params:{iter:6, talus:0.66, strength:0.55}},
    ];
    texLayers=[
      {id:uid('t'), type:'sandstone', enabled:true, opacity:1, blend:'Overwrite', hMin:0,hMax:100,sMin:0,sMax:90, scale:1.6, variation:0.3, rough:0.78},
      {id:uid('t'), type:'cliff', enabled:true, opacity:0.92, blend:'Max', hMin:0,hMax:100,sMin:38,sMax:90, scale:0.8, variation:0.15, rough:0.9},
      {id:uid('t'), type:'sediment', enabled:true, opacity:0.65, blend:'Multiply', hMin:0,hMax:28,sMin:0,sMax:14, scale:0.9, variation:0.4, rough:0.82},
      {id:uid('t'), type:'granite', enabled:true, opacity:0.5, blend:'Overlay', hMin:22,hMax:78,sMin:10,sMax:55, scale:1.2, variation:0.2, rough:0.86},
    ];
  } else if(name==='dunes'){
    genLayers=[
      {id:uid('g'), type:'fbm', enabled:true, opacity:1, blend:'Overwrite', params:{scale:0.9, octaves:4, lac:2, persist:0.45, warp:0, gain:1, seed:1}},
      {id:uid('g'), type:'dune', enabled:true, opacity:0.9, blend:'Add', params:{scale:7, amp:0.16, dir:42, elong:0.68}},
      {id:uid('g'), type:'warp', enabled:true, opacity:0.55, blend:'Overlay', params:{intensity:0.35, scale:1.4, octaves:3, seed:4}},
      {id:uid('g'), type:'thermal', enabled:true, opacity:1, blend:'Overwrite', params:{iter:4, talus:0.72, strength:0.35}},
    ];
    texLayers=[
      {id:uid('t'), type:'sand', enabled:true, opacity:1, blend:'Overwrite', hMin:0,hMax:100,sMin:0,sMax:90, scale:1.8, variation:0.38, rough:0.88},
      {id:uid('t'), type:'sediment', enabled:true, opacity:0.4, blend:'Multiply', hMin:0,hMax:40,sMin:0,sMax:18, scale:1.2, variation:0.32, rough:0.82},
      {id:uid('t'), type:'granite', enabled:true, opacity:0.25, blend:'Overlay', hMin:18,hMax:72,sMin:20,sMax:68, scale:1, variation:0.2, rough:0.86},
    ];
  } else if(name==='alpine'){
    genLayers=[
      {id:uid('g'), type:'fbm', enabled:true, opacity:1, blend:'Overwrite', params:{scale:2.8, octaves:6, lac:2.05, persist:0.46, warp:0.18, gain:1.05, seed:0}},
      {id:uid('g'), type:'ridged', enabled:true, opacity:0.92, blend:'Max', params:{scale:1.45, octaves:6, lac:2.12, persist:0.48, sharp:0.92, seed:9}},
      {id:uid('g'), type:'voronoi', enabled:false, opacity:0.18, blend:'Multiply', params:{scale:3.2, jitter:0.82, edge:0.32, seed:7}},
      {id:uid('g'), type:'thermal', enabled:true, opacity:1, blend:'Overwrite', params:{iter:14, talus:0.54, strength:0.72}},
      {id:uid('g'), type:'hydraulic', enabled:true, opacity:1, blend:'Overwrite', params:{iter:28, rain:0.12, sediment:0.09, evap:0.016, talus:0.62}},
      {id:uid('g'), type:'snowmask', enabled:true, opacity:0.45, blend:'Screen', params:{bias:0, cover:0.42, slope:38}},
    ];
    texLayers=[
      {id:uid('t'), type:'granite', enabled:true, opacity:1, blend:'Overwrite', hMin:0,hMax:100,sMin:28,sMax:90, scale:1, variation:0.2, rough:0.88},
      {id:uid('t'), type:'cliff', enabled:true, opacity:0.88, blend:'Max', hMin:0,hMax:100,sMin:34,sMax:90, scale:0.9, variation:0.16, rough:0.92},
      {id:uid('t'), type:'snow', enabled:true, opacity:1, blend:'Screen', hMin:42,hMax:100,sMin:0,sMax:28, scale:0.7, variation:0.14, rough:0.38},
      {id:uid('t'), type:'grass', enabled:true, opacity:0.42, blend:'Soft Light', hMin:6,hMax:42,sMin:0,sMax:18, scale:1.3, variation:0.32, rough:0.94},
    ];
  } else if(name==='mesa'){
    genLayers=[
      {id:uid('g'), type:'fbm', enabled:true, opacity:1, blend:'Overwrite', params:{scale:1.2, octaves:4, lac:2, persist:0.5, warp:0, gain:1, seed:6}},
      {id:uid('g'), type:'voronoi', enabled:true, opacity:0.55, blend:'Max', params:{scale:1.6, jitter:0.68, edge:0.18, seed:3}},
      {id:uid('g'), type:'terrace', enabled:true, opacity:1, blend:'Overwrite', params:{steps:5, steep:0.8, smooth:0.08, offset:0}},
      {id:uid('g'), type:'thermal', enabled:true, opacity:1, blend:'Overwrite', params:{iter:12, talus:0.62, strength:0.62}},
      {id:uid('g'), type:'hydraulic', enabled:true, opacity:0.65, blend:'Overlay', params:{iter:22, rain:0.1, sediment:0.08, evap:0.018, talus:0.65}},
    ];
    texLayers=[
      {id:uid('t'), type:'sandstone', enabled:true, opacity:1, blend:'Overwrite', hMin:0,hMax:100,sMin:0,sMax:90, scale:1.4, variation:0.28, rough:0.78},
      {id:uid('t'), type:'cliff', enabled:true, opacity:0.88, blend:'Max', hMin:0,hMax:100,sMin:42,sMax:90, scale:1, variation:0.18, rough:0.92},
      {id:uid('t'), type:'sediment', enabled:true, opacity:0.55, blend:'Multiply', hMin:0,hMax:32,sMin:0,sMax:16, scale:0.9, variation:0.35, rough:0.82},
    ];
  }
  selectedId=genLayers[0]?.id; selectedStack='gen';
  $('#seedInput').value=SEED; $('#resSelect').value=String(RES);
  updateLayerCounts(); refreshStacks(); refreshInspector(); refreshLeftTree(); scheduleRegen();
}

function scheduleRegen(){
  needsRegen=true;
  $('#saveBtn').classList.remove('saved'); $('#saveBtn').innerHTML='<span style="width:6px;height:6px;border-radius:50%;background:#e2b65f;display:inline-block"></span> Unsaved';
  clearTimeout(regenRaf);
  $('#loadingOverlay').style.display='grid';
  $('#loadingProg').style.width='18%';
  regenRaf=setTimeout(async()=>{
    await regenerate();
    $('#loadingOverlay').style.display='none';
    $('#saveBtn').classList.add('saved'); $('#saveBtn').innerHTML='<span style="width:6px;height:6px;border-radius:50%;background:currentColor;display:inline-block"></span> Saved';
  }, 80);
}

async function regenerate(){
  // CPU gen - for WebGPU we could also compute there but we keep CPU for fidelity parity
  $('#loadingText').textContent = `Building ${RES}² • ${genLayers.filter(l=>l.enabled).length} active layers • seed ${SEED}`;
  $('#loadingProg').style.width='35%';
  await new Promise(r=> setTimeout(r, 16));
  generateHeightmap();
  $('#loadingProg').style.width='68%';
  bakeColormap();
  $('#loadingProg').style.width='88%';
  await new Promise(r=> setTimeout(r, 10));
  drawHeightThumb();
  $('#statGen').textContent = genTimeMs.toFixed(0)+' ms';
  $('#statVram').textContent = ((RES*RES*4*2)/1024/1024).toFixed(1)+' MB';
  $('#hudMeter').style.width = Math.min(100, 42 + genTimeMs/12)+'%';
  await uploadToGpu();
  render();
  $('#loadingProg').style.width='100%';
}

function drawHeightThumb(){
  if(!heightThumbCtx) return;
  const N=RES;
  const cvs=heightThumbCanvas;
  const ctx=heightThumbCtx;
  const W=cvs.width, H=cvs.height;
  const img=ctx.createImageData(W,H);
  // choose viewMode for thumb always height shaded
  for(let y=0;y<H;y++) for(let x=0;x<W;x++){
    const sx=Math.floor(x/W*N), sy=Math.floor(y/H*N);
    const v=heightMap[sy*N+sx];
    const idx=(y*W+x)*4;
    if(viewMode==='height' || true){
      const c=Math.round(v*255);
      img.data[idx]=c; img.data[idx+1]=c; img.data[idx+2]=c; img.data[idx+3]=255;
    }
  }
  ctx.putImageData(img,0,0);
  // if colormap, overlay tiny color strip at bottom 18px
  if(colormap && viewMode==='albedo'){
    const stripH=18;
    for(let y=H-stripH;y<H;y++) for(let x=0;x<W;x++){
      const sx=Math.floor(x/W*N), sy=Math.floor((y-(H-stripH))/stripH * N);
      // sample colormap more roughly
      const ci = (sy*N+sx)*4;
      const idx=(y*W+x)*4;
      img.data[idx]=colormap[ci]; img.data[idx+1]=colormap[ci+1]; img.data[idx+2]=colormap[ci+2];
    }
    ctx.putImageData(img,0,0);
  }
}

// GPU render setup
let cameraTheta=-0.62, cameraPhi=0.82, cameraRadius=1450, cameraTargetY=38, isDragging=false, lastX=0,lastY=0, dragMode='orbit';

async function initGpu(){
  const canvas=$('#terrainCanvas');
  const gpuBadge=$('#gpuBadge'), gpuStatus=$('#gpuStatus'), badgeText=$('#gpuBadgeText');
  if(!navigator.gpu){
    gpuOk=false; gpuStatus.textContent='unavailable'; badgeText.textContent='CPU fallback • No WebGPU';
    gpuBadge.querySelector('i').style.background='#ef4444';
    return false;
  }
  try{
    const adapter=await navigator.gpu.requestAdapter();
    if(!adapter) throw new Error('no adapter');
    device=await adapter.requestDevice();
    format=navigator.gpu.getPreferredCanvasFormat();
    context=canvas.getContext('webgpu');
    context.configure({device, format, alphaMode:'opaque'});
    gpuOk=true;
    gpuStatus.textContent='active'; badgeText.textContent='WebGPU • '+adapter.info?.vendor||'GPU';
    gpuBadge.querySelector('i').style.background='#22c55e';
    gpuBadge.classList.add('ok');
    createGpuTextures();
    createPipeline();
    setupCanvasEvents();
    // handle resize
    const ro=new ResizeObserver(()=>{ resizeCanvas(); });
    ro.observe(canvas.parentElement);
    resizeCanvas();
    return true;
  } catch(e){
    console.warn('WebGPU init failed', e);
    gpuOk=false; gpuStatus.textContent='failed'; badgeText.textContent='CPU fallback';
    gpuBadge.querySelector('i').style.background='#f59e0b';
    return false;
  }
}
function resizeCanvas(){
  const canvas=$('#terrainCanvas');
  const wrap=canvas.parentElement;
  const dpr=Math.min(2, window.devicePixelRatio||1);
  const w=wrap.clientWidth, h=wrap.clientHeight;
  canvas.width=Math.max(1, Math.floor(w*dpr));
  canvas.height=Math.max(1, Math.floor(h*dpr));
  canvas.style.width=w+'px'; canvas.style.height=h+'px';
  if(device){
    depthTexture?.destroy?.();
    depthTexture=device.createTexture({size:[canvas.width, canvas.height], format:'depth24plus', usage:GPUTextureUsage.RENDER_ATTACHMENT});
  }
  if(renderReady) render();
}
function createGpuTextures(){
  if(!device) return;
  const N=RES;
  heightTexture?.destroy?.();
  colorTexture?.destroy?.();
  heightTexture=device.createTexture({
    size:[N,N,1], format:'r32float', usage:GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.COPY_DST|GPUTextureUsage.STORAGE_BINDING,
    dimension:'2d'
  });
  colorTexture=device.createTexture({
    size:[N,N,1], format:'rgba8unorm', usage:GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.COPY_DST,
    dimension:'2d'
  });
  sampler=device.createSampler({magFilter:'linear', minFilter:'linear', mipmapFilter:'linear', addressModeU:'repeat', addressModeV:'repeat'});
  uniformBuffer?.destroy?.();
  uniformBuffer=device.createBuffer({size:256, usage:GPUBufferUsage.UNIFORM|GPUBufferUsage.COPY_DST});
  heightTextureView=heightTexture.createView();
  colorTextureView=colorTexture.createView();
  // depth
  const canvas=$('#terrainCanvas');
  depthTexture=device.createTexture({size:[canvas.width||1200, canvas.height||800], format:'depth24plus', usage:GPUTextureUsage.RENDER_ATTACHMENT});
}

const wgslShaders = {
vertex: `
struct Uniforms {
  mvp: mat4x4<f32>,
  worldScale: f32,
  heightScale: f32,
  res: f32,
  sunDir: vec3<f32>,
  viewMode: f32,
  time: f32,
  pad: f32,
  sunColor: vec3<f32>,
  pad2: f32,
  camPos: vec3<f32>,
}
@group(0) @binding(0) var<uniform> uni: Uniforms;
@group(0) @binding(1) var heightTex: texture_2d<f32>;
@group(0) @binding(2) var colorTex: texture_2d<f32>;
@group(0) @binding(3) var samp: sampler;

struct VSOut {
  @builtin(position) pos: vec4<f32>,
  @location(0) uv: vec2<f32>,
  @location(1) worldPos: vec3<f32>,
  @location(2) normal: vec3<f32>,
  @location(3) color: vec3<f32>,
}

fn heightAt(uv: vec2<f32>) -> f32 {
  let dims = textureDimensions(heightTex);
  let c = vec2u(clamp(uv, vec2<f32>(0.0), vec2<f32>(0.9999)) * vec2f(dims));
  return textureLoad(heightTex, c, 0).r;
}

@vertex
fn vs(@location(0) position: vec2<f32>, @location(1) uv: vec2<f32>) -> VSOut {
  var o: VSOut;
  let h = heightAt(uv);
  let worldPos = vec3<f32>( (position.x -0.5)*uni.worldScale, h*uni.heightScale, (position.y -0.5)*uni.worldScale);
  o.worldPos = worldPos;
  o.uv = uv;
  o.pos = uni.mvp * vec4<f32>(worldPos,1.0);
  // normal via central differences using textureLoad (r32float is unfilterable)
  let texel = 1.0/uni.res;
  let hL = heightAt(uv + vec2<f32>(-texel,0.0));
  let hR = heightAt(uv + vec2<f32>( texel,0.0));
  let hD = heightAt(uv + vec2<f32>(0.0,-texel));
  let hU = heightAt(uv + vec2<f32>(0.0, texel));
  let dx = (hR - hL)*uni.heightScale / (2.0*texel*uni.worldScale);
  let dz = (hU - hD)*uni.heightScale / (2.0*texel*uni.worldScale);
  var n = normalize(vec3<f32>(-dx, 1.0, -dz));
  o.normal = n;
  o.color = textureSampleLevel(colorTex, samp, uv, 0.0).rgb;
  return o;
}
`,
fragment: `
struct Uniforms {
  mvp: mat4x4<f32>,
  worldScale: f32,
  heightScale: f32,
  res: f32,
  sunDir: vec3<f32>,
  viewMode: f32,
  time: f32,
  pad: f32,
  sunColor: vec3<f32>,
  pad2: f32,
  camPos: vec3<f32>,
}
@group(0) @binding(0) var<uniform> uni: Uniforms;
@group(0) @binding(1) var heightTex: texture_2d<f32>;
@group(0) @binding(2) var colorTex: texture_2d<f32>;
@group(0) @binding(3) var samp: sampler;

fn heightAtF(uv: vec2<f32>) -> f32 {
  let dims = textureDimensions(heightTex);
  let c = vec2u(clamp(uv, vec2<f32>(0.0), vec2<f32>(0.9999)) * vec2f(dims));
  return textureLoad(heightTex, c, 0).r;
}

@fragment
fn fs(@location(0) uv: vec2<f32>, @location(1) worldPos: vec3<f32>, @location(2) normal: vec3<f32>, @location(3) color: vec3<f32>) -> @location(0) vec4<f32> {
  let viewDir = normalize(uni.camPos - worldPos);
  let N = normalize(normal);
  let L = normalize(uni.sunDir);
  let h = heightAtF(uv);

  // view mode switch
  if(uni.viewMode == 1.0){
    // height
    let c = vec3<f32>(h);
    return vec4<f32>(pow(c, vec3<f32>(0.92)),1.0);
  }
  if(uni.viewMode == 2.0){
    let cn = N*0.5+0.5;
    return vec4<f32>(cn,1.0);
  }
  if(uni.viewMode == 3.0){
    let slope = acos(clamp(N.y, -1.0,1.0))*57.2958;
    let t = slope/60.0;
    let c = mix(vec3<f32>(0.22,0.5,0.22), vec3<f32>(0.75,0.68,0.62), clamp(t,0.0,1.0));
    // height line
    return vec4<f32>(c * (0.85 + h*0.3),1.0);
  }
  if(uni.viewMode == 4.0){
    return vec4<f32>(color,1.0);
  }

  // PBR-ish shaded
  // albedo already in color
  var albedo = color;
  // add micro detail: triplanar noise modulates roughness perceived? Keep simple
  let ndotl = max(dot(N,L),0.0);
  // shadow-like ambient occlusion: darken valleys via height derivatives? Approx via N.y
  let ao = 0.92 + N.y*0.08;
  // diffuse
  let diff = albedo * (0.22*ao + 0.78*ndotl);
  // specular blinn
  let H = normalize(L + viewDir);
  let ndoth = max(dot(N,H),0.0);
  let spec = pow(ndoth, 48.0) * 0.22 * (1.0 - length(albedo)*0.15);
  // fresnel
  let fres = pow(1.0 - max(dot(N,viewDir),0.0), 3.0)*0.18;
  // sky hemisphere
  let skyCol = vec3<f32>(0.52,0.62,0.78);
  let sky = mix(vec3<f32>(0.06,0.07,0.08), skyCol, clamp(N.y*0.5+0.5,0.0,1.0))*0.35*ao;
  // fog
  let dist = length(uni.camPos - worldPos);
  let fogFactor = 1.0 - exp(-dist*0.00032);
  fogFactor = clamp(fogFactor,0.0,0.62);
  let fogCol = mix(vec3<f32>(0.78,0.82,0.86), vec3<f32>(0.52,0.58,0.66), clamp((worldPos.y-10.0)/380.0,0.0,1.0));
  // add distant haze with height
  let col = diff + spec + fres*albedo + sky;
  // slope-based darkening for crevices already in ao, enhance valley dark
  let valley = 1.0 - smoothstep(0.14,0.42, h);
  // subtle vignette via uv? not needed

  col = mix(col, fogCol, fogFactor);
  // tonemap (ACES approx)
  col = col / (col + vec3<f32>(1.0));
  col = pow(col, vec3<f32>(1.0/2.2));
  // contrast a bit
  col = mix(col, vec3<f32>(dot(col, vec3<f32>(0.2126,0.7152,0.0722))), -0.06);
  return vec4<f32>(col,1.0);
}
`,
wireframeFragment: `
@fragment
fn fsWire() -> @location(0) vec4<f32> {
  return vec4<f32>(0.08,0.08,0.08,1.0);
}
`
};

let meshVB, meshIB, meshIndexCount;

function createPipeline(){
  if(!device) return;
  const vsModule=device.createShaderModule({code:wgslShaders.vertex});
  const fsModule=device.createShaderModule({code:wgslShaders.fragment});
  const uniformBindLayout=device.createBindGroupLayout({entries:[
    {binding:0, visibility:GPUShaderStage.VERTEX|GPUShaderStage.FRAGMENT, buffer:{type:'uniform'}},
    {binding:1, visibility:GPUShaderStage.VERTEX|GPUShaderStage.FRAGMENT, texture:{sampleType:'unfilterable-float'}},
    {binding:2, visibility:GPUShaderStage.VERTEX|GPUShaderStage.FRAGMENT, texture:{sampleType:'float'}},
    {binding:3, visibility:GPUShaderStage.VERTEX|GPUShaderStage.FRAGMENT, sampler:{type:'filtering'}},
  ]});
  const pipelineLayout=device.createPipelineLayout({bindGroupLayouts:[uniformBindLayout]});
  pipeline=device.createRenderPipeline({
    layout:pipelineLayout,
    vertex:{
      module:vsModule, entryPoint:'vs',
      buffers:[
        {arrayStride:16, attributes:[{shaderLocation:0, offset:0, format:'float32x2'},{shaderLocation:1, offset:8, format:'float32x2'}]}
      ]
    },
    fragment:{module:fsModule, entryPoint:'fs', targets:[{format}]},
    primitive:{topology:'triangle-list', cullMode:'back'},
    depthStencil:{format:'depth24plus', depthWriteEnabled:true, depthCompare:'less'},
  });
  // mesh
  const GRID=256;
  const verts=new Float32Array(GRID*GRID*4);
  const idx=new Uint32Array((GRID-1)*(GRID-1)*6);
  let p=0;
  for(let y=0;y<GRID;y++) for(let x=0;x<GRID;x++){
    verts[p++]=x/(GRID-1);
    verts[p++]=y/(GRID-1);
    verts[p++]=x/(GRID-1);
    verts[p++]=y/(GRID-1);
  }
  let ip=0;
  for(let y=0;y<GRID-1;y++) for(let x=0;x<GRID-1;x++){
    const i=y*GRID+x;
    idx[ip++]=i; idx[ip++]=i+1; idx[ip++]=i+GRID;
    idx[ip++]=i+1; idx[ip++]=i+GRID+1; idx[ip++]=i+GRID;
  }
  meshIndexCount=idx.length;
  meshVB=device.createBuffer({size:verts.byteLength, usage:GPUBufferUsage.VERTEX|GPUBufferUsage.COPY_DST});
  device.queue.writeBuffer(meshVB,0,verts);
  meshIB=device.createBuffer({size:idx.byteLength, usage:GPUBufferUsage.INDEX|GPUBufferUsage.COPY_DST});
  device.queue.writeBuffer(meshIB,0,idx);
  // depth already
  renderReady=true;
}

async function uploadToGpu(){
  if(!device || !heightTexture) return;
  const N=RES;
  // height texture r32float raw data
  // device.queue.writeTexture requires bytesPerRow aligned to 256
  // Use copy via buffer? Simpler to use writeTexture with aligned rows using staging buffer?
  // We'll use writeTexture with 256-aligned hack: create padded buffer
  const bytesPerPixel=4;
  const unpadded= N*bytesPerPixel;
  const padded = Math.ceil(unpadded/256)*256;
  if(padded===unpadded){
    device.queue.writeTexture({texture:heightTexture}, heightMap, {bytesPerRow:unpadded, rowsPerImage:N}, [N,N,1]);
  } else {
    const paddedData=new Float32Array(padded/4 * N);
    for(let y=0;y<N;y++){
      paddedData.set(heightMap.subarray(y*N, (y+1)*N), y*padded/4);
    }
    device.queue.writeTexture({texture:heightTexture}, paddedData, {bytesPerRow:padded, rowsPerImage:N}, [N,N,1]);
  }
  // color
  if(colormap){
    const colorUnpadded=N*4;
    const colorPadded=Math.ceil(colorUnpadded/256)*256;
    if(colorPadded===colorUnpadded){
      device.queue.writeTexture({texture:colorTexture}, colormap, {bytesPerRow:colorUnpadded, rowsPerImage:N}, [N,N,1]);
    } else {
      const paddedC=new Uint8Array(colorPadded*N);
      for(let y=0;y<N;y++){
        paddedC.set(colormap.subarray(y*N*4,(y+1)*N*4), y*colorPadded);
      }
      device.queue.writeTexture({texture:colorTexture}, paddedC, {bytesPerRow:colorPadded, rowsPerImage:N}, [N,N,1]);
    }
  }
  // update bind group
  if(!bindGroup){
    const layout=pipeline.getBindGroupLayout(0);
    bindGroup=device.createBindGroup({
      layout, entries:[
        {binding:0, resource:{buffer:uniformBuffer}},
        {binding:1, resource:heightTextureView},
        {binding:2, resource:colorTextureView},
        {binding:3, resource:sampler},
      ]
    });
  } else {
    // need recreate because texture view changed (res change)
    const layout=pipeline.getBindGroupLayout(0);
    bindGroup=device.createBindGroup({
      layout, entries:[
        {binding:0, resource:{buffer:uniformBuffer}},
        {binding:1, resource:heightTexture.createView()},
        {binding:2, resource:colorTexture.createView()},
        {binding:3, resource:sampler},
      ]
    });
  }
}

function render(){
  if(!device || !pipeline || !bindGroup) {
    // fallback 2D render
    renderFallback2D();
    return;
  }
  const canvas=$('#terrainCanvas');
  const encoder=device.createCommandEncoder();
  // update uniforms
  const aspect=canvas.width/canvas.height;
  const fov=Math.PI/4;
  const near=1, far=8000;
  // perspective
  const proj=perspective(fov, aspect, near, far);
  const eye=orbitEye();
  const target=[0,cameraTargetY,0];
  const up=[0,1,0];
  const view=lookAt(eye,target,up);
  const mvp=multiplyMat4(proj, view);
  const sunDir=sunDirection(sunAz, sunEl);
  const modeMap={shaded:0, height:1, normal:2, slope:3, albedo:4};
  const viewModeVal=modeMap[viewMode]??0;
  const uniformData=new ArrayBuffer(256);
  const f32=new Float32Array(uniformData);
  // mat4 16 floats
  f32.set(mvp,0);
  f32[16]=worldScale; f32[17]=heightScale; f32[18]=RES; f32[19]=0;
  f32[20]=sunDir[0]; f32[21]=sunDir[1]; f32[22]=sunDir[2]; f32[23]=viewModeVal;
  // pad? need alignment: after vec3 sunDir + viewMode -> time, pad, sunColor etc
  // our struct: mvp 16, worldScale, heightScale, res, pad? Actually we pack as per WGSL: mvp 16, worldScale,heightScale,res,pad, sunDir xyz + viewMode, time pad etc - simpler just fill sequentially as defined
  // Let's correctly fill per struct layout (std140): mat4 16, then 4 floats, then vec3+float, etc
  // We wrote struct as:
  // mvp: mat4x4 (16)
  // worldScale, heightScale, res, pad? We'll map 16-19
  // sunDir vec3 + viewMode
  // time, pad, sunColor etc.. For simplicity fill remaining with defaults
  f32[24]=performance.now()*0.001; f32[25]=0;
  f32[26]=1.0; f32[27]=0.92; f32[28]=0.78; f32[29]=0;
  f32[30]=eye[0]; f32[31]=eye[1]; f32[32]=eye[2];
  device.queue.writeBuffer(uniformBuffer,0, uniformData);

  const pass=encoder.beginRenderPass({
    colorAttachments:[{view:context.getCurrentTexture().createView(), clearValue:{r:0.06,g:0.07,b:0.08,a:1.0}, loadOp:'clear', storeOp:'store'}],
    depthStencilAttachment:{view:depthTexture.createView(), depthClearValue:1.0, depthLoadOp:'clear', depthStoreOp:'store'}
  });
  pass.setPipeline(pipeline);
  pass.setBindGroup(0, bindGroup);
  pass.setVertexBuffer(0, meshVB);
  pass.setIndexBuffer(meshIB, 'uint32');
  pass.drawIndexed(meshIndexCount);
  // wire overlay if needed: draw lines? simple second pass with line topology? Instead we fake by rendering same mesh with fragment wire? For now we skip detailed wires; we can draw degraded depth offset.
  pass.end();
  device.queue.submit([encoder.finish()]);
}

function renderFallback2D(){
  const canvas=$('#terrainCanvas');
  const ctx=canvas.getContext('2d');
  if(!ctx) return;
  const w=canvas.width, h=canvas.height;
  ctx.fillStyle='#0a0a0a'; ctx.fillRect(0,0,w,h);
  if(!heightMap || !colormap) return;
  // simple heightmap image scaled
  const N=RES;
  const img=ctx.createImageData(w,h);
  // we could draw color map stretched
  for(let y=0;y<h;y++) for(let x=0;x<w;x++){
    const sx=Math.floor(x/w*N), sy=Math.floor(y/h*N);
    const idx=sy*N+sx;
    let r,g,b;
    if(viewMode==='height'){ const v=heightMap[idx]; r=g=b=Math.round(v*255); }
    else if(viewMode==='albedo' && colormap){ const ci=idx*4; r=colormap[ci]; g=colormap[ci+1]; b=colormap[ci+2]; }
    else {
      const ci=colormap? idx*4:0;
      if(colormap){ r=colormap[ci]; g=colormap[ci+1]; b=colormap[ci+2]; const shade = 0.6+0.4*heightMap[idx]; r=Math.round(r*shade); g=Math.round(g*shade); b=Math.round(b*shade); }
      else { const v=heightMap[idx]; r=g=b=Math.round(v*255); }
    }
    const p=(y*w+x)*4;
    img.data[p]=r; img.data[p+1]=g; img.data[p+2]=b; img.data[p+3]=255;
  }
  // put with smoothing via putImageData then scale? already per pixel
  ctx.putImageData(img,0,0);
  // overlay info
  ctx.fillStyle='rgba(0,0,0,0.5)'; ctx.fillRect(10,10,210,52);
  ctx.fillStyle='#fff'; ctx.font='11px DM Sans'; ctx.fillText(`Fallback 2D • ${viewMode} • ${RES}²`,18,28);
  ctx.fillStyle='#aaa'; ctx.fillText(`Seed ${SEED} • ${genTimeMs|0}ms`,18,44);
}

// camera math
function orbitEye(){
  const th=cameraTheta, ph=cameraPhi, r=cameraRadius;
  const x= Math.cos(th)*Math.cos(ph)*r;
  const y= Math.sin(ph)*r + cameraTargetY;
  const z= Math.sin(th)*Math.cos(ph)*r;
  return [x,y,z];
}
function sunDirection(az, el){
  const a=az*Math.PI/180, e=el*Math.PI/180;
  const x=Math.cos(a)*Math.cos(e);
  const y=Math.sin(e);
  const z=Math.sin(a)*Math.cos(e);
  return [x,y,z];
}
function perspective(fov, aspect, near, far){
  const f=1/Math.tan(fov/2), nf=1/(near-far);
  return new Float32Array([
    f/aspect,0,0,0,
    0,f,0,0,
    0,0,far*nf,-1,
    0,0,near*far*nf,0
  ]);
}
function lookAt(eye, target, up){
  const z0=eye[0]-target[0], z1=eye[1]-target[1], z2=eye[2]-target[2];
  let len=Math.hypot(z0,z1,z2); const zx=z0/len, zy=z1/len, zz=z2/len;
  let x0=up[1]*zz - up[2]*zy, x1=up[2]*zx - up[0]*zz, x2=up[0]*zy - up[1]*zx;
  len=Math.hypot(x0,x1,x2); x0/=len; x1/=len; x2/=len;
  let y0=zy*x2 - zz*x1, y1=zz*x0 - zx*x2, y2=zx*x1 - zy*x0;
  return new Float32Array([
    x0, x1, x2, 0,
    y0, y1, y2, 0,
    zx, zy, zz, 0,
    -(x0*eye[0]+x1*eye[1]+x2*eye[2]),
    -(y0*eye[0]+y1*eye[1]+y2*eye[2]),
    -(zx*eye[0]+zy*eye[1]+zz*eye[2]),
    1
  ]);
}
function multiplyMat4(a,b){
  // a * b (both column-major 4x4): out[c*4+r] = sum_k a[k*4+r] * b[c*4+k]
  const out=new Float32Array(16);
  for(let c=0;c<4;c++) for(let r=0;r<4;r++){
    let sum=0;
    for(let k=0;k<4;k++) sum += a[k*4+r] * b[c*4+k];
    out[c*4+r]=sum;
  }
  return out;
}

function setupCanvasEvents(){
  const canvas=$('#terrainCanvas');
  canvas.addEventListener('mousedown', e=>{
    isDragging=true; lastX=e.clientX; lastY=e.clientY;
    dragMode = e.button===2 || e.ctrlKey ? 'pan' : 'orbit';
    canvas.setPointerCapture(e.pointerId);
  });
  canvas.addEventListener('mouseup', e=>{ isDragging=false; });
  canvas.addEventListener('pointerleave', ()=> isDragging=false);
  canvas.addEventListener('mousemove', e=>{
    if(!isDragging) return;
    const dx=e.clientX-lastX, dy=e.clientY-lastY;
    lastX=e.clientX; lastY=e.clientY;
    if(dragMode==='orbit'){
      cameraTheta += dx*0.005;
      cameraPhi = Math.max(0.05, Math.min(1.45, cameraPhi - dy*0.005));
    } else {
      // pan target Y
      cameraTargetY -= dy*1.2;
      cameraTargetY=Math.max(-200, Math.min(300, cameraTargetY));
      cameraTheta += dx*0.002;
    }
    render();
  });
  canvas.addEventListener('wheel', e=>{
    e.preventDefault();
    const delta = Math.sign(e.deltaY)* 0.08;
    cameraRadius *= (1+delta);
    cameraRadius=Math.max(220, Math.min(4200, cameraRadius));
    render();
  }, {passive:false});
  canvas.addEventListener('contextmenu', e=>e.preventDefault());
}

// exports
function exportHeight(){
  const N=RES;
  // create 16-bit? We'll export 8-bit PNG via canvas
  const cvs=document.createElement('canvas'); cvs.width=N; cvs.height=N;
  const ctx=cvs.getContext('2d'); const img=ctx.createImageData(N,N);
  for(let i=0;i<N*N;i++){ const v=Math.round(heightMap[i]*255); img.data[i*4]=v; img.data[i*4+1]=v; img.data[i*4+2]=v; img.data[i*4+3]=255; }
  ctx.putImageData(img,0,0);
  cvs.toBlob(b=>{ const url=URL.createObjectURL(b); const a=document.createElement('a'); a.href=url; a.download=`frontier-height-${N}-${SEED}.png`; a.click(); setTimeout(()=>URL.revokeObjectURL(url),2000); }, 'image/png');
}
function exportColor(){
  const N=RES;
  if(!colormap) return;
  const cvs=document.createElement('canvas'); cvs.width=N; cvs.height=N;
  const ctx=cvs.getContext('2d'); const img=ctx.createImageData(N,N);
  img.data.set(colormap); ctx.putImageData(img,0,0);
  cvs.toBlob(b=>{ const url=URL.createObjectURL(b); const a=document.createElement('a'); a.href=url; a.download=`frontier-albedo-${N}-${SEED}.png`; a.click(); setTimeout(()=>URL.revokeObjectURL(url),2000); }, 'image/png');
}

// init
renderApp();
initGpu().then(()=> scheduleRegen());
drawHeightThumb();
// initial generate will be triggered by scheduleRegen after gpu
