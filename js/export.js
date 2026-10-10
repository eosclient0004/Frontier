//============================================================================================================================================
//  EXPORT.JS — 16-BIT PNG HEIGHTMAP ENCODER, NORMAL MAP, SPLATMAP, ALBEDO SATMAP, WAVEFRONT OBJ & JSON RECIPE EXPORTERS
//============================================================================================================================================

function crc32(bytes) {
  let c = -1;
  for (let i = 0; i < bytes.length; i++) {
    c ^= bytes[i];
    for (let k = 0; k < 8; k++) {
      c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    }
  }
  return (c ^ -1) >>> 0;
}

function adler32(bytes) {
  let a = 1;
  let b = 0;
  const MOD = 65521;
  for (let i = 0; i < bytes.length; i++) {
    a = (a + bytes[i]) % MOD;
    b = (b + a) % MOD;
  }
  return ((b << 16) | a) >>> 0;
}

function writeU32BE(arr, offset, val) {
  arr[offset + 0] = (val >>> 24) & 0xff;
  arr[offset + 1] = (val >>> 16) & 0xff;
  arr[offset + 2] = (val >>> 8) & 0xff;
  arr[offset + 3] = val & 0xff;
}

function makePngChunk(typeStr, dataBytes) {
  const chunk = new Uint8Array(12 + dataBytes.length);
  writeU32BE(chunk, 0, dataBytes.length);
  for (let i = 0; i < 4; i++) chunk[4 + i] = typeStr.charCodeAt(i);
  chunk.set(dataBytes, 8);
  const crc = crc32(chunk.subarray(4, 8 + dataBytes.length));
  writeU32BE(chunk, 8 + dataBytes.length, crc);
  return chunk;
}

// Encode a genuine 16-bit Grayscale PNG (65,536 elevation levels)
export function encode16BitGrayscalePNG(width, height, floatHeights, minElev, maxElev) {
  const rowBytes = 1 + width * 2; // 1 filter byte (0) + 2 bytes per pixel (big-endian uint16)
  const raw = new Uint8Array(height * rowBytes);
  const span = Math.max(1e-5, maxElev - minElev);

  let ptr = 0;
  for (let y = 0; y < height; y++) {
    raw[ptr++] = 0; // Filter type 0 (None)
    for (let x = 0; x < width; x++) {
      const h = floatHeights[y * width + x];
      const norm = Math.max(0, Math.min(1, (h - minElev) / span));
      const u16 = Math.round(norm * 65535);
      raw[ptr++] = (u16 >>> 8) & 0xff;
      raw[ptr++] = u16 & 0xff;
    }
  }

  // Wrap raw scanlines in uncompressed zlib stored blocks (65535 bytes max per block)
  const maxBlock = 65535;
  const numBlocks = Math.ceil(raw.length / maxBlock);
  const zlibLen = 2 + raw.length + numBlocks * 5 + 4;
  const zlib = new Uint8Array(zlibLen);
  let zPtr = 0;
  zlib[zPtr++] = 0x78; // CMF
  zlib[zPtr++] = 0x01; // FLG (no compression)

  for (let b = 0; b < numBlocks; b++) {
    const start = b * maxBlock;
    const end = Math.min(raw.length, start + maxBlock);
    const len = end - start;
    const isLast = b === numBlocks - 1 ? 1 : 0;
    zlib[zPtr++] = isLast;
    zlib[zPtr++] = len & 0xff;
    zlib[zPtr++] = (len >>> 8) & 0xff;
    const nlen = ~len & 0xffff;
    zlib[zPtr++] = nlen & 0xff;
    zlib[zPtr++] = (nlen >>> 8) & 0xff;
    zlib.set(raw.subarray(start, end), zPtr);
    zPtr += len;
  }

  writeU32BE(zlib, zPtr, adler32(raw));

  const sig = new Uint8Array([137, 80, 78, 71, 13, 10, 26, 10]);
  const ihdr = new Uint8Array(13);
  writeU32BE(ihdr, 0, width);
  writeU32BE(ihdr, 4, height);
  ihdr[8] = 16; // 16-bit depth!
  ihdr[9] = 0;  // Grayscale
  ihdr[10] = 0;
  ihdr[11] = 0;
  ihdr[12] = 0;

  const cIHDR = makePngChunk("IHDR", ihdr);
  const cIDAT = makePngChunk("IDAT", zlib);
  const cIEND = makePngChunk("IEND", new Uint8Array(0));

  const out = new Uint8Array(sig.length + cIHDR.length + cIDAT.length + cIEND.length);
  let o = 0;
  out.set(sig, o); o += sig.length;
  out.set(cIHDR, o); o += cIHDR.length;
  out.set(cIDAT, o); o += cIDAT.length;
  out.set(cIEND, o);
  return new Blob([out], { type: "image/png" });
}

export function triggerDownload(blob, filename) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 2500);
}

export function exportHeightmap16Bit(engine, presetId = "terrain") {
  const N = engine.cpuSize;
  const blob = encode16BitGrayscalePNG(
    N,
    N,
    engine.cpuHeight,
    engine.stats.minElev,
    engine.stats.maxElev
  );
  triggerDownload(blob, `${presetId}_heightmap_16bit_${N}x${N}.png`);
}

export function exportAlbedoPNG(engine, presetId = "terrain") {
  const N = engine.cpuSize;
  const c = document.createElement("canvas");
  c.width = N;
  c.height = N;
  const ctx = c.getContext("2d");
  const img = ctx.createImageData(N, N);
  img.data.set(engine.cpuAlbedo);
  ctx.putImageData(img, 0, 0);
  c.toBlob((blob) => {
    if (blob) triggerDownload(blob, `${presetId}_albedo_satmap_${N}x${N}.png`);
  }, "image/png");
}

export function exportNormalMapPNG(engine, presetId = "terrain") {
  const N = engine.cpuSize;
  const c = document.createElement("canvas");
  c.width = N;
  c.height = N;
  const ctx = c.getContext("2d");
  const img = ctx.createImageData(N, N);
  const d = img.data;
  for (let i = 0; i < N * N; i++) {
    const nx = engine.cpuNormX[i];
    const nz = engine.cpuNormZ[i];
    const ny = Math.sqrt(Math.max(0.01, 1 - nx * nx - nz * nz));
    d[i * 4 + 0] = Math.round((nx * 0.5 + 0.5) * 255);
    d[i * 4 + 1] = Math.round((-nz * 0.5 + 0.5) * 255);
    d[i * 4 + 2] = Math.round((ny * 0.5 + 0.5) * 255);
    d[i * 4 + 3] = 255;
  }
  ctx.putImageData(img, 0, 0);
  c.toBlob((blob) => {
    if (blob) triggerDownload(blob, `${presetId}_normal_${N}x${N}.png`);
  }, "image/png");
}

export function exportSplatMapPNG(engine, presetId = "terrain") {
  const N = engine.cpuSize;
  const c = document.createElement("canvas");
  c.width = N;
  c.height = N;
  const ctx = c.getContext("2d");
  const img = ctx.createImageData(N, N);
  const d = img.data;
  for (let i = 0; i < N * N; i++) {
    const sed = Math.min(1, engine.cpuSediment[i] / 18);
    const tal = Math.min(1, engine.cpuTalus[i] / 15);
    const flw = Math.min(1, engine.cpuFlow[i] / 2.2);
    const snw = Math.min(1, engine.cpuSnow[i]);
    const bedrock = Math.max(0, 1 - Math.max(sed, tal, snw));
    d[i * 4 + 0] = Math.round(bedrock * 255);
    d[i * 4 + 1] = Math.round(Math.max(sed, flw * 0.6) * 255);
    d[i * 4 + 2] = Math.round(tal * 255);
    d[i * 4 + 3] = Math.round(Math.max(0.15, snw) * 255);
  }
  ctx.putImageData(img, 0, 0);
  c.toBlob((blob) => {
    if (blob) triggerDownload(blob, `${presetId}_splat_rgba_${N}x${N}.png`);
  }, "image/png");
}

export function exportWavefrontOBJ(engine, state, presetId = "terrain") {
  const N = 128; // Clean 128x128 OBJ export for DCC tools
  const C = engine.cpuSize;
  const domainM = (state.env.domainSizeKm || 5.0) * 1000;
  const lines = [
    `# Frontier Terrain Studio — Wavefront OBJ Export`,
    `# Domain: ${state.env.domainSizeKm} km x ${state.env.domainSizeKm} km`,
  ];

  for (let z = 0; z < N; z++) {
    const v = z / (N - 1);
    const cz = Math.min(C - 1, Math.round(v * (C - 1)));
    for (let x = 0; x < N; x++) {
      const u = x / (N - 1);
      const cx = Math.min(C - 1, Math.round(u * (C - 1)));
      const idx = cz * C + cx;
      const wx = ((u - 0.5) * domainM).toFixed(2);
      const wy = engine.cpuHeight[idx].toFixed(2);
      const wz = ((v - 0.5) * domainM).toFixed(2);
      lines.push(`v ${wx} ${wy} ${wz}`);
      lines.push(`vt ${u.toFixed(4)} ${(1 - v).toFixed(4)}`);
    }
  }

  for (let z = 0; z < N - 1; z++) {
    for (let x = 0; x < N - 1; x++) {
      const i00 = z * N + x + 1;
      const i10 = i00 + 1;
      const i01 = (z + 1) * N + x + 1;
      const i11 = i01 + 1;
      lines.push(`f ${i00}/${i00} ${i01}/${i01} ${i10}/${i10}`);
      lines.push(`f ${i10}/${i10} ${i01}/${i01} ${i11}/${i11}`);
    }
  }

  const blob = new Blob([lines.join("\n")], { type: "text/plain" });
  triggerDownload(blob, `${presetId}_mesh_${N}x${N}.obj`);
}

export function exportProjectJSON(state) {
  const payload = {
    version: "1.0",
    presetId: state.presetId,
    env: state.env,
    terrainStack: state.terrainStack,
    textureStack: state.textureStack,
  };
  const blob = new Blob([JSON.stringify(payload, null, 2)], { type: "application/json" });
  triggerDownload(blob, `${state.presetId || "frontier"}_terrain_recipe.json`);
}
