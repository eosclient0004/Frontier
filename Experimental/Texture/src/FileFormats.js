// PNG and ZIP codecs. Canvas-based PNG encoding premultiplies alpha and is
// limited to 8 bits, which corrupts layer data and height maps, so pixels are
// encoded directly here (zlib via CompressionStream).

const CrcTable = (() => {
  const Table = new Uint32Array(256);
  for (let N = 0; N < 256; N++) {
    let C = N;
    for (let K = 0; K < 8; K++) C = C & 1 ? 0xedb88320 ^ (C >>> 1) : C >>> 1;
    Table[N] = C >>> 0;
  }
  return Table;
})();
export function Crc32(Bytes, Crc = 0) {
  Crc = ~Crc >>> 0;
  for (let K = 0; K < Bytes.length; K++) Crc = CrcTable[(Crc ^ Bytes[K]) & 0xff] ^ (Crc >>> 8);
  return ~Crc >>> 0;
}

async function Transform(Bytes, Stream) {
  const Response_ = new Response(new Blob([Bytes]).stream().pipeThrough(Stream));
  return new Uint8Array(await Response_.arrayBuffer());
}
const Deflate = (Bytes) => Transform(Bytes, new CompressionStream("deflate"));
const Inflate = (Bytes) => Transform(Bytes, new DecompressionStream("deflate"));

const WriteUint32 = (View, Offset, Value) => View.setUint32(Offset, Value >>> 0);

// Channels: 1 (gray), 3 (RGB) or 4 (RGBA). Data is a Uint8Array (8-bit) or
// Uint16Array (16-bit), row-major, top row first.
export async function EncodePng(Width, Height, Channels, Depth, Data) {
  const ColorType = { 1: 0, 2: 4, 3: 2, 4: 6 }[Channels];
  const BytesPerSample = Depth / 8;
  const Stride = Width * Channels * BytesPerSample;
  const Raw = new Uint8Array((Stride + 1) * Height);
  // Sub filter on every row compresses smooth maps substantially better.
  const Bpp = Channels * BytesPerSample;
  const Row = new Uint8Array(Stride);
  for (let Y = 0; Y < Height; Y++) {
    const Base = Y * Width * Channels;
    if (Depth === 8) Row.set(Data.subarray(Base, Base + Width * Channels));
    else
      for (let K = 0; K < Width * Channels; K++) {
        const V = Data[Base + K];
        Row[K * 2] = V >> 8;
        Row[K * 2 + 1] = V & 0xff;
      }
    const Offset = Y * (Stride + 1);
    Raw[Offset] = 1;
    for (let K = 0; K < Stride; K++) Raw[Offset + 1 + K] = (Row[K] - (K >= Bpp ? Row[K - Bpp] : 0)) & 0xff;
  }
  const Compressed = await Deflate(Raw);
  const Chunks = [];
  const Chunk = (Type, Payload) => {
    const Buffer_ = new Uint8Array(12 + Payload.length);
    const View = new DataView(Buffer_.buffer);
    WriteUint32(View, 0, Payload.length);
    for (let K = 0; K < 4; K++) Buffer_[4 + K] = Type.charCodeAt(K);
    Buffer_.set(Payload, 8);
    WriteUint32(View, 8 + Payload.length, Crc32(Buffer_.subarray(4, 8 + Payload.length)));
    Chunks.push(Buffer_);
  };
  const Header = new Uint8Array(13);
  const HeaderView = new DataView(Header.buffer);
  WriteUint32(HeaderView, 0, Width);
  WriteUint32(HeaderView, 4, Height);
  Header[8] = Depth;
  Header[9] = ColorType;
  Chunk("IHDR", Header);
  Chunk("IDAT", Compressed);
  Chunk("IEND", new Uint8Array(0));
  return new Blob([new Uint8Array([137, 80, 78, 71, 13, 10, 26, 10]), ...Chunks], { type: "image/png" });
}

// Decodes non-interlaced PNGs (all filter types). Returns { Width, Height,
// Channels, Depth, Data } with Data top-row-first.
export async function DecodePng(Bytes) {
  const View = new DataView(Bytes.buffer, Bytes.byteOffset, Bytes.byteLength);
  if (View.getUint32(0) !== 0x89504e47) throw new Error("Not a PNG file.");
  let Offset = 8;
  let Width = 0, Height = 0, Depth = 8, ColorType = 6;
  const Idat = [];
  while (Offset < Bytes.length) {
    const Length = View.getUint32(Offset);
    const Type = String.fromCharCode(...Bytes.subarray(Offset + 4, Offset + 8));
    const Payload = Bytes.subarray(Offset + 8, Offset + 8 + Length);
    if (Type === "IHDR") {
      const H = new DataView(Payload.buffer, Payload.byteOffset, Payload.byteLength);
      Width = H.getUint32(0);
      Height = H.getUint32(4);
      Depth = Payload[8];
      ColorType = Payload[9];
      if (Payload[12] !== 0) throw new Error("Interlaced PNGs are not supported.");
    } else if (Type === "IDAT") Idat.push(Payload);
    else if (Type === "IEND") break;
    Offset += 12 + Length;
  }
  const Channels = { 0: 1, 2: 3, 4: 2, 6: 4 }[ColorType];
  if (!Channels || (Depth !== 8 && Depth !== 16)) throw new Error("Unsupported PNG layout.");
  const Joined = new Uint8Array(Idat.reduce((S, P) => S + P.length, 0));
  let Cursor = 0;
  for (const Part of Idat) {
    Joined.set(Part, Cursor);
    Cursor += Part.length;
  }
  const Raw = await Inflate(Joined);
  const Bpp = Channels * (Depth / 8);
  const Stride = Width * Bpp;
  const Out = new Uint8Array(Stride * Height);
  for (let Y = 0; Y < Height; Y++) {
    const Filter = Raw[Y * (Stride + 1)];
    const Source = Y * (Stride + 1) + 1;
    const Target = Y * Stride;
    for (let K = 0; K < Stride; K++) {
      const A = K >= Bpp ? Out[Target + K - Bpp] : 0;
      const B = Y > 0 ? Out[Target - Stride + K] : 0;
      const C = K >= Bpp && Y > 0 ? Out[Target - Stride + K - Bpp] : 0;
      let Value = Raw[Source + K];
      if (Filter === 1) Value += A;
      else if (Filter === 2) Value += B;
      else if (Filter === 3) Value += (A + B) >> 1;
      else if (Filter === 4) {
        const P = A + B - C;
        const PA = Math.abs(P - A), PB = Math.abs(P - B), PC = Math.abs(P - C);
        Value += PA <= PB && PA <= PC ? A : PB <= PC ? B : C;
      }
      Out[Target + K] = Value & 0xff;
    }
  }
  let Data = Out;
  if (Depth === 16) {
    Data = new Uint16Array(Width * Height * Channels);
    for (let K = 0; K < Data.length; K++) Data[K] = (Out[K * 2] << 8) | Out[K * 2 + 1];
  }
  return { Width, Height, Channels, Depth, Data };
}

// Store-only ZIP (PNG payloads are already compressed).
export function CreateZip(Files) {
  const Encoder = new TextEncoder();
  const Locals = [];
  const Central = [];
  let Offset = 0;
  const Now = new Date();
  const DosTime = (Now.getHours() << 11) | (Now.getMinutes() << 5) | (Now.getSeconds() >> 1);
  const DosDate = ((Now.getFullYear() - 1980) << 9) | ((Now.getMonth() + 1) << 5) | Now.getDate();
  for (const File_ of Files) {
    const Name = Encoder.encode(File_.Name);
    const Data = File_.Data;
    const Crc = Crc32(Data);
    const Local = new Uint8Array(30 + Name.length);
    const L = new DataView(Local.buffer);
    L.setUint32(0, 0x04034b50, true);
    L.setUint16(4, 20, true);
    L.setUint16(6, 0x0800, true);
    L.setUint16(8, 0, true);
    L.setUint16(10, DosTime, true);
    L.setUint16(12, DosDate, true);
    L.setUint32(14, Crc, true);
    L.setUint32(18, Data.length, true);
    L.setUint32(22, Data.length, true);
    L.setUint16(26, Name.length, true);
    Local.set(Name, 30);
    Locals.push(Local, Data);
    const Entry = new Uint8Array(46 + Name.length);
    const C = new DataView(Entry.buffer);
    C.setUint32(0, 0x02014b50, true);
    C.setUint16(4, 20, true);
    C.setUint16(6, 20, true);
    C.setUint16(8, 0x0800, true);
    C.setUint16(12, DosTime, true);
    C.setUint16(14, DosDate, true);
    C.setUint32(16, Crc, true);
    C.setUint32(20, Data.length, true);
    C.setUint32(24, Data.length, true);
    C.setUint16(28, Name.length, true);
    C.setUint32(42, Offset, true);
    Entry.set(Name, 46);
    Central.push(Entry);
    Offset += Local.length + Data.length;
  }
  const CentralSize = Central.reduce((S, E) => S + E.length, 0);
  const End = new Uint8Array(22);
  const E = new DataView(End.buffer);
  E.setUint32(0, 0x06054b50, true);
  E.setUint16(8, Files.length, true);
  E.setUint16(10, Files.length, true);
  E.setUint32(12, CentralSize, true);
  E.setUint32(16, Offset, true);
  return new Blob([...Locals, ...Central, End], { type: "application/zip" });
}

export async function ReadZip(Bytes) {
  const View = new DataView(Bytes.buffer, Bytes.byteOffset, Bytes.byteLength);
  let End = -1;
  for (let K = Bytes.length - 22; K >= Math.max(0, Bytes.length - 65557); K--)
    if (View.getUint32(K, true) === 0x06054b50) { End = K; break; }
  if (End < 0) throw new Error("Not a valid project archive.");
  const Count = View.getUint16(End + 10, true);
  let Cursor = View.getUint32(End + 16, true);
  const Decoder = new TextDecoder();
  const Files = new Map();
  for (let K = 0; K < Count; K++) {
    if (View.getUint32(Cursor, true) !== 0x02014b50) throw new Error("Corrupt project archive.");
    const Method = View.getUint16(Cursor + 10, true);
    const CompressedSize = View.getUint32(Cursor + 20, true);
    const NameLength = View.getUint16(Cursor + 28, true);
    const ExtraLength = View.getUint16(Cursor + 30, true);
    const CommentLength = View.getUint16(Cursor + 32, true);
    const LocalOffset = View.getUint32(Cursor + 42, true);
    const Name = Decoder.decode(Bytes.subarray(Cursor + 46, Cursor + 46 + NameLength));
    const LocalNameLength = View.getUint16(LocalOffset + 26, true);
    const LocalExtraLength = View.getUint16(LocalOffset + 28, true);
    const Start = LocalOffset + 30 + LocalNameLength + LocalExtraLength;
    let Data = Bytes.subarray(Start, Start + CompressedSize);
    if (Method === 8) Data = await Transform(Data, new DecompressionStream("deflate-raw"));
    else if (Method !== 0) throw new Error(`Unsupported compression in ${Name}.`);
    Files.set(Name, Data);
    Cursor += 46 + NameLength + ExtraLength + CommentLength;
  }
  return Files;
}

export function DownloadBlob(Blob_, Name) {
  const Url = URL.createObjectURL(Blob_);
  const Link = document.createElement("a");
  Link.href = Url;
  Link.download = Name;
  document.body.append(Link);
  Link.click();
  Link.remove();
  setTimeout(() => URL.revokeObjectURL(Url), 4000);
}
