// GLSL ES 3.00 programs for the texture painter.
//
// Texture space conventions
//  · Position map  RGBA32F  object-space position, w = 1 inside UV islands
//  · Normal map    RGBA16F  object-space normal, w = signed mean curvature
//  · Accumulators  RGBA16F  A0 base color (sRGB) · A1 roughness/metal/height
//                           A2 emissive (linear HDR)
//  · Paint layer   T0 RGBA8  premultiplied color + color coverage
//                  T1 RGBA16F premultiplied roughness, metallic, height
//                  T2 RGBA8  coverage of roughness, metallic, height, emissive
//                  T3 RGBA8  premultiplied emissive color (scaled by 1/16)

const Header = `#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;
`;

export const FullscreenVertex = `#version 300 es
layout(location = 0) in vec2 a_Position;
out vec2 v_Uv;
void main() {
  v_Uv = a_Position * 0.5 + 0.5;
  gl_Position = vec4(a_Position, 0.0, 1.0);
}`;

const Noise = `
float Hash12(vec2 P) {
  vec3 P3 = fract(vec3(P.xyx) * 0.1031);
  P3 += dot(P3, P3.yzx + 33.33);
  return fract((P3.x + P3.y) * P3.z);
}
vec2 Hash22(vec2 P) {
  vec3 P3 = fract(vec3(P.xyx) * vec3(0.1031, 0.1030, 0.0973));
  P3 += dot(P3, P3.yzx + 33.33);
  return fract((P3.xx + P3.yz) * P3.zy);
}
float ValueNoise(vec2 P) {
  vec2 I = floor(P);
  vec2 F = fract(P);
  vec2 U = F * F * (3.0 - 2.0 * F);
  return mix(mix(Hash12(I), Hash12(I + vec2(1.0, 0.0)), U.x),
             mix(Hash12(I + vec2(0.0, 1.0)), Hash12(I + vec2(1.0, 1.0)), U.x), U.y);
}
float Fbm(vec2 P) {
  float Sum = 0.0;
  float Amplitude = 0.5;
  for (int Octave = 0; Octave < 5; Octave++) {
    Sum += Amplitude * ValueNoise(P);
    P = mat2(1.6, 1.2, -1.2, 1.6) * P + 3.1;
    Amplitude *= 0.5;
  }
  return Sum / 0.96875;
}
vec3 Voronoi(vec2 P) {
  vec2 N = floor(P);
  vec2 F = fract(P);
  float F1 = 8.0;
  float F2 = 8.0;
  float Id = 0.0;
  for (int J = -1; J <= 1; J++)
    for (int I = -1; I <= 1; I++) {
      vec2 G = vec2(float(I), float(J));
      vec2 R = G + Hash22(N + G) - F;
      float D = dot(R, R);
      if (D < F1) { F2 = F1; F1 = D; Id = Hash12(N + G + 17.0); }
      else if (D < F2) { F2 = D; }
    }
  return vec3(sqrt(F1), sqrt(F2), Id);
}
`;

// Material evaluation is specialised at compile time through MAT_PATTERN,
// MAT_MAPPING (0 triplanar · 1 uv), MAT_WARP, MAT_GRAIN and MAT_BITMAP. An uber
// shader holding every pattern took Direct3D shader compilers tens of seconds
// and could exceed the GPU watchdog, so every program that evaluates a
// material is compiled per variant (see TextureEngine.Variant).
const MaterialLibrary = `
struct Material {
  vec3 Color;
  vec3 Color2;
  vec2 Roughness;
  vec2 Metallic;
  float Height;
  float HeightAmount;
  vec3 Emissive;
  float EmissiveFromPattern;
  int Pattern;
  float Scale;
  float Rotation;
  float Contrast;
  float Balance;
  float Warp;
  float Seed;
  float Grain;
  int Mapping;
  int UseBitmap;
  float BitmapScale;
  float BitmapHeight;
};
struct Surface {
  vec3 Color;
  float Roughness;
  float Metallic;
  float Height;
  vec3 Emissive;
};

float RawPattern(vec2 P) {
#if MAT_PATTERN == 0
  return 0.0;

#elif MAT_PATTERN == 1
  return Fbm(P);
#elif MAT_PATTERN == 2
  {
    float N = Fbm(P * 0.7);
    float V = Voronoi(P * 1.3).x;
    return clamp(N * 1.25 - V * 0.4 + 0.08, 0.0, 1.0);
  }
#elif MAT_PATTERN == 3
  {
    vec3 V = Voronoi(P);
    return clamp((1.0 - V.x) * 0.75 + V.z * 0.25, 0.0, 1.0);
  }
#elif MAT_PATTERN == 4
  {
    vec3 V = Voronoi(P + (vec2(Fbm(P * 2.0), Fbm(P * 2.0 + 7.0)) - 0.5) * 0.35);
    return 1.0 - smoothstep(0.0, 0.07, V.y - V.x);
  }
#elif MAT_PATTERN == 5
  return Fbm(vec2(P.x * 0.06, P.y * 14.0)) * 0.8 + ValueNoise(vec2(P.x * 0.3, P.y * 60.0)) * 0.2;
#elif MAT_PATTERN == 6
  {
    float S = 0.0;
    for (int K = 0; K < 4; K++) {
      float Angle = Hash12(vec2(float(K), 3.7)) * 3.14159;
      vec2 R = mat2(cos(Angle), -sin(Angle), sin(Angle), cos(Angle)) * P * (1.5 + float(K) * 0.7);
      vec2 Cell = vec2(floor(R.x * 0.3), floor(R.y));
      float Offset = 0.15 + 0.7 * Hash12(Cell + float(K) * 13.0);
      float Line = 1.0 - smoothstep(0.0, 0.035, abs(fract(R.y) - Offset));
      float Present = step(0.62, Hash12(Cell * 1.7 + 5.0 + float(K)));
      float Along = fract(R.x * 0.3);
      float Fade = smoothstep(0.0, 0.35, Along) * smoothstep(1.0, 0.6, Along);
      S = max(S, Line * Present * Fade);
    }
    return S;
  }
#elif MAT_PATTERN == 7
  return 0.5 + 0.5 * sin(P.x * 6.2831853);
#elif MAT_PATTERN == 8
  { vec2 C = floor(P); return mod(C.x + C.y, 2.0); }
#elif MAT_PATTERN == 9
  {
    vec2 Q = vec2(P.x, P.y * 2.0);
    float Row = floor(Q.y);
    Q.x += mod(Row, 2.0) * 0.5;
    vec2 F = fract(Q);
    float D = min(min(F.x, 1.0 - F.x), min(F.y, 1.0 - F.y) * 0.5);
    float Brick = smoothstep(0.018, 0.034, D);
    return clamp(1.0 - Brick * (0.82 + 0.18 * Hash12(floor(Q))), 0.0, 1.0);
  }
#elif MAT_PATTERN == 10
  {
    vec2 S = vec2(1.0, 1.7320508);
    vec2 A = mod(P, S) - S * 0.5;
    vec2 B = mod(P - S * 0.5, S) - S * 0.5;
    vec2 G = dot(A, A) < dot(B, B) ? A : B;
    vec2 AG = abs(G);
    float D = max(dot(AG, vec2(0.8660254, 0.5)), AG.y);
    return smoothstep(0.42, 0.47, D);
  }
#elif MAT_PATTERN == 11
  {
    vec2 Q = vec2(P.x * 0.18, P.y);
    float Ring = fract(length(Q) * 3.0 + Fbm(P * vec2(0.4, 3.0)) * 0.7);
    return clamp(pow(Ring, 2.5) + (ValueNoise(P * vec2(2.0, 40.0)) - 0.5) * 0.25, 0.0, 1.0);
  }
#elif MAT_PATTERN == 12
  {
    vec2 C = floor(P);
    vec2 F = fract(P);
    bool Horizontal = mod(C.x - C.y, 4.0) < 2.0;
    float Across = Horizontal ? F.y : F.x;
    float Tow = sqrt(max(0.0, sin(Across * 3.14159)));
    return Horizontal ? 0.25 + 0.75 * Tow : 0.1 * Tow;
  }
#elif MAT_PATTERN == 13
  {
    vec2 C = floor(P);
    vec2 F = fract(P);
    float Thread = mod(C.x + C.y, 2.0) < 1.0 ? sin(F.y * 3.14159) : sin(F.x * 3.14159);
    return Thread * (0.75 + 0.25 * ValueNoise(P * 6.0));
  }
#elif MAT_PATTERN == 14
  return 1.0 - smoothstep(0.26, 0.3, length(fract(P) - 0.5));
#else
  return 0.0;
#endif
}

float PatternAt(Material M, vec2 P) {
  float Angle = radians(M.Rotation);
  P = mat2(cos(Angle), sin(Angle), -sin(Angle), cos(Angle)) * P;
  P += vec2(M.Seed * 7.31, M.Seed * 3.17);
#if MAT_WARP == 1
  P += (vec2(Fbm(P * 0.5 + 5.2), Fbm(P * 0.5 + 1.3)) - 0.5) * 2.0 * M.Warp;
#endif
  return RawPattern(P);
}

vec3 TriplanarWeights(vec3 N) {
  vec3 W = pow(abs(N), vec3(4.0));
  return W / max(1e-5, W.x + W.y + W.z);
}

Surface EvaluateMaterial(Material M, sampler2D Bitmap, vec2 Uv, vec3 Position, vec3 Normal) {
  float T = 0.0;
  float Grain = 0.5;
  vec3 W = TriplanarWeights(Normal);
#if MAT_PATTERN > 0
#if MAT_MAPPING == 1
  T = PatternAt(M, Uv * M.Scale);
#else
  vec3 PP = Position * M.Scale * 0.5;
  T = W.x * PatternAt(M, PP.zy) + W.y * PatternAt(M, PP.xz) + W.z * PatternAt(M, PP.xy);
#endif
  T = clamp((T - 0.5) * M.Contrast + 0.5 + M.Balance, 0.0, 1.0);
#endif
#if MAT_GRAIN == 1
#if MAT_MAPPING == 1
  Grain = ValueNoise(Uv * 900.0);
#else
  vec3 PG = Position * 220.0;
  Grain = W.x * ValueNoise(PG.zy) + W.y * ValueNoise(PG.xz) + W.z * ValueNoise(PG.xy);
#endif
#endif
  Surface S;
  S.Color = mix(M.Color, M.Color2, T);
  S.Roughness = mix(M.Roughness.x, M.Roughness.y, T) + (Grain - 0.5) * M.Grain * 0.5;
  S.Metallic = mix(M.Metallic.x, M.Metallic.y, T);
  S.Height = M.Height + T * M.HeightAmount;
  S.Color *= 1.0 + (Grain - 0.5) * M.Grain * 0.2;
#if MAT_BITMAP == 1
  vec3 Texel;
#if MAT_MAPPING == 1
  Texel = texture(Bitmap, Uv * M.BitmapScale).rgb;
#else
  vec3 PB = Position * M.BitmapScale * 0.5 + 0.5;
  Texel = W.x * texture(Bitmap, PB.zy).rgb + W.y * texture(Bitmap, PB.xz).rgb + W.z * texture(Bitmap, PB.xy).rgb;
#endif
  S.Color *= Texel;
  S.Height += (dot(Texel, vec3(0.299, 0.587, 0.114)) - 0.5) * M.BitmapHeight;
#endif
  S.Emissive = M.Emissive * (M.EmissiveFromPattern > 0.5 ? T : 1.0);
  S.Color = clamp(S.Color, 0.0, 1.0);
  S.Roughness = clamp(S.Roughness, 0.02, 1.0);
  S.Metallic = clamp(S.Metallic, 0.0, 1.0);
  return S;
}
`;

// Stroke application against premultiplied paint layer storage.
const StrokeLibrary = `
void ApplyStroke(inout vec4 T0, inout vec4 T1, inout vec4 T2, inout vec4 T3, float A, Surface S, vec4 Channels, float EmissiveChannel, bool Erase) {
  float AC = A * Channels.x;
  float AR = A * Channels.y;
  float AM = A * Channels.z;
  float AH = A * Channels.w;
  float AE = A * EmissiveChannel;
  if (Erase) {
    T0 *= 1.0 - AC;
    T1.x *= 1.0 - AR; T2.x *= 1.0 - AR;
    T1.y *= 1.0 - AM; T2.y *= 1.0 - AM;
    T1.z *= 1.0 - AH; T2.z *= 1.0 - AH;
    T3.rgb *= 1.0 - AE; T2.w *= 1.0 - AE;
    return;
  }
  T0 = T0 * (1.0 - AC) + vec4(S.Color, 1.0) * AC;
  T1.x = T1.x * (1.0 - AR) + S.Roughness * AR; T2.x = T2.x * (1.0 - AR) + AR;
  T1.y = T1.y * (1.0 - AM) + S.Metallic * AM;  T2.y = T2.y * (1.0 - AM) + AM;
  T1.z = T1.z * (1.0 - AH) + S.Height * AH;    T2.z = T2.z * (1.0 - AH) + AH;
  T3.rgb = T3.rgb * (1.0 - AE) + clamp(S.Emissive / 16.0, 0.0, 1.0) * AE; T2.w = T2.w * (1.0 - AE) + AE;
}
`;

// Renders the mesh in UV space to bake object-space position/normal maps.
export const MapsVertex = `#version 300 es
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_Uv;
out vec3 v_Position;
out vec3 v_Normal;
void main() {
  v_Position = a_Position;
  v_Normal = a_Normal;
  gl_Position = vec4(a_Uv * 2.0 - 1.0, 0.0, 1.0);
}`;
export const MapsFragment = `${Header}
in vec3 v_Position;
in vec3 v_Normal;
layout(location = 0) out vec4 o_Position;
layout(location = 1) out vec4 o_Normal;
void main() {
  o_Position = vec4(v_Position, 1.0);
  o_Normal = vec4(normalize(v_Normal), 0.0);
}`;

// Mean curvature estimate from neighbouring texels at several radii.
export const CurvatureFragment = `${Header}
uniform sampler2D u_Position;
uniform sampler2D u_Normal;
in vec2 v_Uv;
out vec4 o_Normal;
void main() {
  ivec2 Texel = ivec2(gl_FragCoord.xy);
  ivec2 Size = textureSize(u_Position, 0);
  vec4 P = texelFetch(u_Position, Texel, 0);
  vec4 N = texelFetch(u_Normal, Texel, 0);
  if (P.w < 0.5) { o_Normal = vec4(0.0, 0.0, 1.0, 0.0); return; }
  float Sum = 0.0;
  float Weight = 0.0;
  for (int Level = 0; Level < 4; Level++) {
    int Radius = 1 << (Level + Size.x / 1024);
    for (int Direction = 0; Direction < 8; Direction++) {
      float Angle = float(Direction) * 0.7853982;
      ivec2 Offset = ivec2(round(vec2(cos(Angle), sin(Angle)) * float(Radius)));
      ivec2 Q = clamp(Texel + Offset, ivec2(0), Size - 1);
      vec4 PN = texelFetch(u_Position, Q, 0);
      if (PN.w < 0.5) continue;
      vec3 DP = PN.xyz - P.xyz;
      float L2 = dot(DP, DP);
      if (L2 < 1e-10 || L2 > 0.04) continue;
      vec3 DN = texelFetch(u_Normal, Q, 0).xyz - N.xyz;
      Sum += dot(DN, DP) / L2;
      Weight += 1.0;
    }
  }
  o_Normal = vec4(N.xyz, Weight > 0.0 ? Sum / Weight : 0.0);
}`;

// Screen-space pick buffer: UV + view depth, object position, object normal.
export const PickVertex = `#version 300 es
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_Uv;
uniform mat4 u_ViewProjection;
uniform mat4 u_View;
out vec2 v_Uv;
out vec3 v_Position;
out vec3 v_Normal;
out float v_Depth;
void main() {
  v_Uv = a_Uv;
  v_Position = a_Position;
  v_Normal = a_Normal;
  v_Depth = -(u_View * vec4(a_Position, 1.0)).z;
  gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}`;
export const PickFragment = `${Header}
in vec2 v_Uv;
in vec3 v_Position;
in vec3 v_Normal;
in float v_Depth;
layout(location = 0) out vec4 o_Pick;
layout(location = 1) out vec4 o_Position;
layout(location = 2) out vec4 o_Normal;
void main() {
  o_Pick = vec4(v_Uv, v_Depth, 1.0);
  o_Position = vec4(v_Position, 1.0);
  o_Normal = vec4(normalize(v_Normal) * (gl_FrontFacing ? 1.0 : -1.0), 1.0);
}`;

// Accumulates brush dabs into a single-channel stroke mask in texture space.
// Mode 0 projects every texel to the screen (camera projection painting);
// mode 1 paints directly in UV space.
export const StrokeFragment = `${Header}
${Noise}
uniform sampler2D u_Position;
uniform sampler2D u_Normal;
uniform sampler2D u_Pick;
uniform int u_Mode;
uniform mat4 u_ViewProjection;
uniform mat4 u_View;
uniform vec3 u_Camera;
uniform vec2 u_Viewport;
uniform int u_DabCount;
uniform vec4 u_Dabs[32];
uniform vec2 u_DabExtra[32];
uniform int u_Tip;
uniform float u_Hardness;
uniform float u_Roundness;
uniform int u_Backface;
uniform vec2 u_Resolution;
out vec4 o_Stroke;

float Tip(vec2 D, float Radius, float Seed) {
  float Hardness = min(u_Hardness, 1.0 - 1.5 / max(Radius, 1.6));
  float R = u_Tip == 1 ? max(abs(D.x), abs(D.y)) : length(D);
  float Falloff = 1.0 - smoothstep(Hardness, 1.0, R);
  if (u_Tip == 2) Falloff *= smoothstep(0.38, 0.62, Fbm(D * 2.5 + Seed * 31.0));
  if (u_Tip == 3) {
    vec3 V = Voronoi(D * 3.5 + Seed * 17.0);
    Falloff = (1.0 - smoothstep(0.18, 0.24, V.x)) * step(V.z, 0.75) * (1.0 - smoothstep(0.75, 1.0, length(D)));
  }
  return Falloff;
}

void main() {
  vec2 Screen;
  float Visibility = 1.0;
  float Scale = 1.0;
  if (u_Mode == 0) {
    ivec2 Texel = ivec2(gl_FragCoord.xy);
    vec4 P = texelFetch(u_Position, Texel, 0);
    if (P.w < 0.5) discard;
    vec3 N = normalize(texelFetch(u_Normal, Texel, 0).xyz);
    vec4 Clip = u_ViewProjection * vec4(P.xyz, 1.0);
    if (Clip.w <= 0.0) discard;
    vec3 Ndc = Clip.xyz / Clip.w;
    if (any(greaterThan(abs(Ndc.xy), vec2(1.02)))) discard;
    Screen = (Ndc.xy * 0.5 + 0.5) * u_Viewport;
    vec4 Pick = texelFetch(u_Pick, ivec2(clamp(Screen, vec2(0.0), u_Viewport - 1.0)), 0);
    float Depth = -(u_View * vec4(P.xyz, 1.0)).z;
    if (Pick.w > 0.5 && Depth > Pick.z + max(0.012, Depth * 0.006)) discard;
    float Facing = dot(N, normalize(u_Camera - P.xyz));
    if (u_Backface == 1 && Facing <= 0.0) discard;
    Visibility = u_Backface == 1 ? smoothstep(0.0, 0.2, Facing) : 1.0;
  } else {
    Screen = gl_FragCoord.xy / u_Resolution;
  }
  float Remaining = 1.0;
  for (int K = 0; K < 32; K++) {
    if (K >= u_DabCount) break;
    vec4 Dab = u_Dabs[K];
    vec2 D = Screen - Dab.xy;
    float Radius = Dab.z;
    if (dot(D, D) > Radius * Radius * 2.1) continue;
    float Angle = u_DabExtra[K].x;
    D = mat2(cos(Angle), -sin(Angle), sin(Angle), cos(Angle)) * D / Radius;
    D.y /= max(0.05, u_Roundness);
    float PixelRadius = u_Mode == 0 ? Radius : Radius * u_Resolution.x;
    Remaining *= 1.0 - clamp(Tip(D, PixelRadius, u_DabExtra[K].y) * Dab.w, 0.0, 1.0);
  }
  float Value = (1.0 - Remaining) * Visibility;
  if (Value <= 0.0) discard;
  o_Stroke = vec4(Value, 0.0, 0.0, 1.0);
}`;

// Writes a committed stroke into a paint layer (MRT, ping-pong).
export const CommitFragment = `${Header}
${Noise}
${MaterialLibrary}
${StrokeLibrary}
uniform sampler2D u_T0;
uniform sampler2D u_T1;
uniform sampler2D u_T2;
uniform sampler2D u_T3;
uniform sampler2D u_Stroke;
uniform sampler2D u_Position;
uniform sampler2D u_Normal;
uniform sampler2D u_Bitmap1;
uniform Material u_Material[2];
uniform vec4 u_BrushChannels;
uniform float u_BrushEmissive;
uniform float u_BrushOpacity;
uniform int u_Erase;
in vec2 v_Uv;
layout(location = 0) out vec4 o_T0;
layout(location = 1) out vec4 o_T1;
layout(location = 2) out vec4 o_T2;
layout(location = 3) out vec4 o_T3;
void main() {
  ivec2 Texel = ivec2(gl_FragCoord.xy);
  vec4 T0 = texelFetch(u_T0, Texel, 0);
  vec4 T1 = texelFetch(u_T1, Texel, 0);
  vec4 T2 = texelFetch(u_T2, Texel, 0);
  vec4 T3 = texelFetch(u_T3, Texel, 0);
  float A = clamp(texelFetch(u_Stroke, Texel, 0).r, 0.0, 1.0) * u_BrushOpacity;
  Surface S = EvaluateMaterial(u_Material[1], u_Bitmap1, v_Uv, texelFetch(u_Position, Texel, 0).xyz, normalize(texelFetch(u_Normal, Texel, 0).xyz + vec3(0.0, 1e-4, 0.0)));
  ApplyStroke(T0, T1, T2, T3, A, S, u_BrushChannels, u_BrushEmissive, u_Erase == 1);
  o_T0 = T0; o_T1 = T1; o_T2 = T2; o_T3 = T3;
}`;

export const MaskCommitFragment = `${Header}
uniform sampler2D u_Mask;
uniform sampler2D u_Stroke;
uniform float u_Value;
uniform float u_BrushOpacity;
out vec4 o_Mask;
void main() {
  ivec2 Texel = ivec2(gl_FragCoord.xy);
  float Mask = texelFetch(u_Mask, Texel, 0).r;
  float A = clamp(texelFetch(u_Stroke, Texel, 0).r, 0.0, 1.0) * u_BrushOpacity;
  o_Mask = vec4(vec3(mix(Mask, u_Value, A)), 1.0);
}`;

// One layer of the stack. Reads the accumulated channels below and writes
// the result of blending this layer on top.
// Specialised per layer through the defines LAYER_TYPE (1 paint · 2 fill ·
// 3 decal), STROKE_MODE (0 none · 1 paint · 2 mask), OUTPUT_MASK, MASK_ENABLED,
// MASK_PAINTED, MASK_GEN, MASK_BREAKUP and DECAL_MAPPING, plus the MAT_*
// material defines. Small variants compile quickly and never execute
// patterns or branches the layer does not use.
export const CompositeFragment = `${Header}
${Noise}
${MaterialLibrary}
${StrokeLibrary}
uniform sampler2D u_Prev0;
uniform sampler2D u_Prev1;
uniform sampler2D u_Prev2;
uniform sampler2D u_Position;
uniform sampler2D u_Normal;
uniform sampler2D u_T0;
uniform sampler2D u_T1;
uniform sampler2D u_T2;
uniform sampler2D u_T3;
uniform sampler2D u_Mask;
uniform sampler2D u_Decal;
uniform sampler2D u_Stroke;
uniform sampler2D u_Bitmap0;
uniform sampler2D u_Bitmap1;
uniform Material u_Material[2];

uniform float u_Opacity;
uniform int u_Blend;
uniform int u_HeightBlend;
uniform vec4 u_Channels;
uniform float u_EmissiveChannel;

uniform vec4 u_MaskParams;   // scale, contrast, offset, seed
uniform vec2 u_MaskExtra;    // invert, breakup
uniform vec2 u_HeightRange;  // object min/max y

uniform vec3 u_DecalOrigin;
uniform vec3 u_DecalRight;
uniform vec3 u_DecalUp;
uniform vec3 u_DecalNormal;
uniform vec2 u_DecalAngle;   // cos(limit), fade width
uniform vec2 u_DecalCenter;
uniform mat2 u_DecalInverse;
uniform int u_DecalSourceColor;
uniform int u_DecalReady;
uniform int u_DecalTwoSided;

uniform vec4 u_BrushChannels;
uniform float u_BrushEmissive;
uniform float u_BrushOpacity;
uniform int u_Erase;
uniform float u_MaskValue;

in vec2 v_Uv;
layout(location = 0) out vec4 o_A0;
layout(location = 1) out vec4 o_A1;
layout(location = 2) out vec4 o_A2;

vec3 Overlay(vec3 A, vec3 B) {
  return mix(2.0 * A * B, 1.0 - 2.0 * (1.0 - A) * (1.0 - B), step(0.5, A));
}
vec3 SoftLight(vec3 A, vec3 B) {
  return mix(A - (1.0 - 2.0 * B) * A * (1.0 - A), A + (2.0 * B - 1.0) * (sqrt(A) - A), step(0.5, B));
}
vec3 Hue(vec3 A, vec3 B) {
  float LA = dot(A, vec3(0.299, 0.587, 0.114));
  float LB = dot(B, vec3(0.299, 0.587, 0.114));
  return clamp(B + (LA - LB), 0.0, 1.0);
}
vec3 BlendColor(int Mode, vec3 A, vec3 B) {
  if (Mode == 1) return A * B;
  if (Mode == 2) return 1.0 - (1.0 - A) * (1.0 - B);
  if (Mode == 3) return Overlay(A, B);
  if (Mode == 4) return SoftLight(A, B);
  if (Mode == 5) return min(A + B, 1.0);
  if (Mode == 6) return max(A - B, 0.0);
  if (Mode == 7) return min(A, B);
  if (Mode == 8) return max(A, B);
  if (Mode == 9) return Hue(A, B);
  return B;
}
float BlendHeight(int Mode, float A, float B, float C) {
  if (Mode == 1) return A + B * C;
  if (Mode == 2) return A - B * C;
  if (Mode == 3) return mix(A, max(A, B), C);
  if (Mode == 4) return mix(A, min(A, B), C);
  return mix(A, B, C);
}

float TriplanarFbm(vec3 P, vec3 N) {
  vec3 W = TriplanarWeights(N);
  return W.x * Fbm(P.zy) + W.y * Fbm(P.xz) + W.z * Fbm(P.xy);
}

float LayerMask(ivec2 Texel, vec3 P, vec3 N, float Curvature) {
#if MASK_ENABLED == 0
  return 1.0;
#else
  float Mask = 1.0;
#if MASK_PAINTED == 1
  Mask = texelFetch(u_Mask, Texel, 0).r;
#if STROKE_MODE == 2
  float A = clamp(texelFetch(u_Stroke, Texel, 0).r, 0.0, 1.0) * u_BrushOpacity;
  Mask = mix(Mask, u_MaskValue, A);
#endif
#endif
#if MASK_GEN > 0
  float Scale = u_MaskParams.x;
  vec3 Seed = vec3(u_MaskParams.w * 3.7, u_MaskParams.w * 1.3, u_MaskParams.w * 5.1);
  float G = 0.0;
#if MASK_GEN == 1
  G = TriplanarFbm(P * Scale + Seed, N);
#elif MASK_GEN == 2
  G = (P.y - u_HeightRange.x) / max(1e-4, u_HeightRange.y - u_HeightRange.x);
#elif MASK_GEN == 3
  G = N.y * 0.5 + 0.5;
#elif MASK_GEN == 4
  G = clamp(Curvature * 0.08 * Scale, 0.0, 1.0);
#elif MASK_GEN == 5
  G = clamp(-Curvature * 0.08 * Scale, 0.0, 1.0);
#endif
#if MASK_GEN > 1 && MASK_BREAKUP == 1
  G += (TriplanarFbm(P * 7.0 + Seed, N) - 0.5) * u_MaskExtra.y * 0.5;
#endif
  G = clamp((G + u_MaskParams.z - 0.5) * u_MaskParams.y + 0.5, 0.0, 1.0);
  if (u_MaskExtra.x > 0.5) G = 1.0 - G;
  Mask *= G;
#endif
  return Mask;
#endif
}

void main() {
  ivec2 Texel = ivec2(gl_FragCoord.xy);
  vec4 P = texelFetch(u_Position, Texel, 0);
  vec4 NC = texelFetch(u_Normal, Texel, 0);
  vec3 N = normalize(NC.xyz + vec3(0.0, 1e-4, 0.0));
  float Mask = LayerMask(Texel, P.xyz, N, NC.w);
#if OUTPUT_MASK == 1
  o_A0 = vec4(vec3(Mask), 1.0);
  o_A1 = vec4(0.0);
  o_A2 = vec4(0.0);
#else
  vec4 Prev0 = texelFetch(u_Prev0, Texel, 0);
  vec4 Prev1 = texelFetch(u_Prev1, Texel, 0);
  vec4 Prev2 = texelFetch(u_Prev2, Texel, 0);

  Surface S;
  vec4 Coverage = vec4(0.0);   // color, roughness, metallic, height
  float EmissiveCoverage = 0.0;

#if LAYER_TYPE == 1
  {
    vec4 T0 = texelFetch(u_T0, Texel, 0);
    vec4 T1 = texelFetch(u_T1, Texel, 0);
    vec4 T2 = texelFetch(u_T2, Texel, 0);
    vec4 T3 = texelFetch(u_T3, Texel, 0);
#if STROKE_MODE == 1
    float A = clamp(texelFetch(u_Stroke, Texel, 0).r, 0.0, 1.0) * u_BrushOpacity;
    Surface B = EvaluateMaterial(u_Material[1], u_Bitmap1, v_Uv, P.xyz, N);
    ApplyStroke(T0, T1, T2, T3, A, B, u_BrushChannels, u_BrushEmissive, u_Erase == 1);
#endif
    Coverage = vec4(T0.a, T2.x, T2.y, T2.z);
    EmissiveCoverage = T2.w;
    S.Color = T0.rgb / max(T0.a, 1e-4);
    S.Roughness = T1.x / max(T2.x, 1e-4);
    S.Metallic = T1.y / max(T2.y, 1e-4);
    S.Height = T1.z / max(T2.z, 1e-4);
    S.Emissive = T3.rgb * 16.0 / max(T2.w, 1e-4);
  }
#elif LAYER_TYPE == 2
  S = EvaluateMaterial(u_Material[0], u_Bitmap0, v_Uv, P.xyz, N);
  Coverage = vec4(1.0);
  EmissiveCoverage = 1.0;
#else
  {
    vec2 Local;
    float Inside = 0.0;
#if DECAL_MAPPING == 0
    {
      vec3 D = P.xyz - u_DecalOrigin;
      Local = vec2(dot(D, u_DecalRight), dot(D, u_DecalUp));
      float Depth = dot(D, u_DecalNormal);
      float Facing = dot(N, normalize(u_DecalNormal));
      if (u_DecalTwoSided == 1) Facing = abs(Facing);
      float Angular = smoothstep(u_DecalAngle.x - u_DecalAngle.y, u_DecalAngle.x + u_DecalAngle.y, Facing);
      Inside = step(abs(Depth), 1.0) * Angular * P.w;
    }
#else
    Local = u_DecalInverse * (v_Uv - u_DecalCenter);
    Inside = 1.0;
#endif
    vec2 DecalUv = Local + 0.5;
    Inside *= step(0.0, DecalUv.x) * step(DecalUv.x, 1.0) * step(0.0, DecalUv.y) * step(DecalUv.y, 1.0);
    vec4 Texel4 = texture(u_Decal, DecalUv) * float(u_DecalReady);
    float Alpha = Texel4.a * Inside;
    S = EvaluateMaterial(u_Material[0], u_Bitmap0, DecalUv, P.xyz, N);
    if (u_DecalSourceColor == 1) S.Color = Texel4.rgb;
    Coverage = vec4(Alpha);
    EmissiveCoverage = Alpha;
  }
#endif

  float Strength = u_Opacity * Mask;
  Coverage *= u_Channels * Strength;
  EmissiveCoverage *= u_EmissiveChannel * Strength;

  vec3 Blended = BlendColor(u_Blend, Prev0.rgb, S.Color);
  vec3 Color = mix(Prev0.rgb, Blended, clamp(Coverage.x, 0.0, 1.0));
  float Roughness = mix(Prev1.x, S.Roughness, clamp(Coverage.y, 0.0, 1.0));
  float Metallic = mix(Prev1.y, S.Metallic, clamp(Coverage.z, 0.0, 1.0));
  float Height = BlendHeight(u_HeightBlend, Prev1.z, S.Height, clamp(Coverage.w, 0.0, 1.0));
  vec3 Emissive = mix(Prev2.rgb, S.Emissive, clamp(EmissiveCoverage, 0.0, 1.0));
  o_A0 = vec4(Color, 1.0);
  o_A1 = vec4(Roughness, Metallic, Height, 1.0);
  o_A2 = vec4(Emissive, 1.0);
#endif
}`;

// Tangent-space normal map from the composited height (OpenGL, +Y up).
export const NormalFragment = `${Header}
uniform sampler2D u_Height;
uniform sampler2D u_Position;
uniform float u_Strength;
out vec4 o_Normal;
float HeightAt(ivec2 Texel, ivec2 Size, float Fallback) {
  Texel = clamp(Texel, ivec2(0), Size - 1);
  return texelFetch(u_Position, Texel, 0).w > 0.5 ? texelFetch(u_Height, Texel, 0).z : Fallback;
}
void main() {
  ivec2 Texel = ivec2(gl_FragCoord.xy);
  ivec2 Size = textureSize(u_Height, 0);
  float H = texelFetch(u_Height, Texel, 0).z;
  float L = HeightAt(Texel - ivec2(1, 0), Size, H);
  float R = HeightAt(Texel + ivec2(1, 0), Size, H);
  float D = HeightAt(Texel - ivec2(0, 1), Size, H);
  float U = HeightAt(Texel + ivec2(0, 1), Size, H);
  float Scale = u_Strength * float(Size.x) / 64.0;
  vec3 N = normalize(vec3((L - R) * Scale, (D - U) * Scale, 1.0));
  o_Normal = vec4(N * 0.5 + 0.5, 1.0);
}`;

// Pushes island-edge texels outward so filtering and mipmaps don't bleed
// background values across UV seams.
export const DilateFragment = `${Header}
uniform sampler2D u_Source0;
uniform sampler2D u_Source1;
uniform sampler2D u_Source2;
uniform sampler2D u_Source3;
uniform sampler2D u_Position;
uniform int u_Radius;
layout(location = 0) out vec4 o_0;
layout(location = 1) out vec4 o_1;
layout(location = 2) out vec4 o_2;
layout(location = 3) out vec4 o_3;
void main() {
  ivec2 Texel = ivec2(gl_FragCoord.xy);
  ivec2 Size = textureSize(u_Position, 0);
  ivec2 Source = Texel;
  if (texelFetch(u_Position, Texel, 0).w < 0.5) {
    bool Found = false;
    for (int Step = 1; Step <= 16; Step++) {
      if (Step > u_Radius || Found) break;
      for (int Direction = 0; Direction < 8; Direction++) {
        float Angle = float(Direction) * 0.7853982;
        ivec2 Q = clamp(Texel + ivec2(round(vec2(cos(Angle), sin(Angle)) * float(Step))), ivec2(0), Size - 1);
        if (texelFetch(u_Position, Q, 0).w > 0.5) { Source = Q; Found = true; break; }
      }
    }
  }
  o_0 = texelFetch(u_Source0, Source, 0);
  o_1 = texelFetch(u_Source1, Source, 0);
  o_2 = texelFetch(u_Source2, Source, 0);
  o_3 = texelFetch(u_Source3, Source, 0);
}`;

const Lighting = `
uniform vec3 u_Sky;
uniform vec3 u_Horizon;
uniform vec3 u_Ground;
uniform vec4 u_Lights[4];
uniform vec3 u_LightColors[4];
uniform float u_EnvRotation;
uniform float u_Exposure;

vec3 RotateY(vec3 D, float A) {
  return vec3(cos(A) * D.x + sin(A) * D.z, D.y, -sin(A) * D.x + cos(A) * D.z);
}
vec3 Environment(vec3 D, float Blur) {
  D = RotateY(D, u_EnvRotation);
  float Y = D.y;
  vec3 Color = mix(u_Ground, u_Horizon, smoothstep(-0.35 - Blur * 0.4, 0.02 + Blur * 0.2, Y));
  Color = mix(Color, u_Sky, smoothstep(0.0, 0.75 + Blur * 0.3, Y));
  for (int K = 0; K < 4; K++) {
    vec3 L = u_Lights[K].xyz;
    float Size = u_Lights[K].w;
    float S = dot(D, L);
    float Spread = Size + Blur;
    float W = smoothstep(cos(Spread), cos(max(0.0, Spread * 0.35 - Blur * 0.2)), S);
    Color += u_LightColors[K] * W * (Size * Size) / (Spread * Spread);
  }
  return Color;
}
vec3 EnvBRDFApprox(vec3 F0, float Roughness, float NoV) {
  const vec4 C0 = vec4(-1.0, -0.0275, -0.572, 0.022);
  const vec4 C1 = vec4(1.0, 0.0425, 1.04, -0.04);
  vec4 R = Roughness * C0 + C1;
  float A004 = min(R.x * R.x, exp2(-9.28 * NoV)) * R.x + R.y;
  vec2 AB = vec2(-1.04, 1.04) * A004 + R.zw;
  return F0 * AB.x + AB.y;
}
vec3 Aces(vec3 X) {
  return clamp((X * (2.51 * X + 0.03)) / (X * (2.43 * X + 0.59) + 0.14), 0.0, 1.0);
}
vec3 Shade(vec3 BaseSrgb, float Roughness, float Metallic, vec3 Emissive, vec3 N, vec3 V) {
  vec3 Base = pow(BaseSrgb, vec3(2.2));
  float NoV = clamp(dot(N, V), 1e-3, 1.0);
  vec3 F0 = mix(vec3(0.04), Base, Metallic);
  vec3 Diffuse = Base * (1.0 - Metallic) * Environment(N, 1.6) * 0.9;
  vec3 R = reflect(-V, N);
  vec3 Specular = Environment(R, Roughness * Roughness * 1.8) * EnvBRDFApprox(F0, Roughness, NoV);
  // Key light: GGX highlight from the brightest environment light.
  vec3 L = normalize(RotateY(u_Lights[0].xyz, -u_EnvRotation));
  vec3 H = normalize(L + V);
  float NoL = clamp(dot(N, L), 0.0, 1.0);
  float NoH = clamp(dot(N, H), 0.0, 1.0);
  float A = max(0.03, Roughness * Roughness);
  float A2 = A * A;
  float Denominator = NoH * NoH * (A2 - 1.0) + 1.0;
  float Distribution = A2 / (3.14159 * Denominator * Denominator);
  float K = (Roughness + 1.0) * (Roughness + 1.0) / 8.0;
  float Geometry = NoL / (NoL * (1.0 - K) + K) * NoV / (NoV * (1.0 - K) + K);
  vec3 Fresnel = F0 + (1.0 - F0) * pow(1.0 - clamp(dot(H, V), 0.0, 1.0), 5.0);
  vec3 Direct = (Distribution * Geometry * Fresnel / max(4.0 * NoL * NoV, 1e-3)) * NoL * u_LightColors[0] * 0.18;
  return (Diffuse + Specular + Direct) * u_Exposure + Emissive;
}
`;

export const ViewportVertex = `#version 300 es
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_Uv;
layout(location = 3) in vec4 a_Tangent;
uniform mat4 u_ViewProjection;
out vec3 v_Position;
out vec3 v_Normal;
out vec4 v_Tangent;
out vec2 v_Uv;
void main() {
  v_Position = a_Position;
  v_Normal = a_Normal;
  v_Tangent = a_Tangent;
  v_Uv = a_Uv;
  gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}`;
export const ViewportFragment = `${Header}
${Lighting}
uniform sampler2D u_Color;
uniform sampler2D u_Surface;
uniform sampler2D u_Emissive;
uniform sampler2D u_NormalMap;
uniform sampler2D u_MaskView;
uniform vec3 u_Camera;
uniform int u_Channel;
uniform float u_HeightView;
in vec3 v_Position;
in vec3 v_Normal;
in vec4 v_Tangent;
in vec2 v_Uv;
out vec4 o_Color;
void main() {
  vec3 Ng = normalize(v_Normal);
  if (!gl_FrontFacing) Ng = -Ng;
  vec3 T = normalize(v_Tangent.xyz - Ng * dot(Ng, v_Tangent.xyz));
  vec3 B = cross(Ng, T) * v_Tangent.w;
  vec3 Nt = texture(u_NormalMap, v_Uv).xyz * 2.0 - 1.0;
  vec3 N = normalize(T * Nt.x + B * Nt.y + Ng * Nt.z);
  vec3 V = normalize(u_Camera - v_Position);
  vec4 C = texture(u_Color, v_Uv);
  vec4 S = texture(u_Surface, v_Uv);
  vec3 E = texture(u_Emissive, v_Uv).rgb;
  vec3 Out;
  if (u_Channel == 0) {
    Out = Aces(Shade(C.rgb, clamp(S.x, 0.02, 1.0), clamp(S.y, 0.0, 1.0), E, N, V));
    Out = pow(Out, vec3(1.0 / 2.2));
  } else if (u_Channel == 1) Out = C.rgb;
  else if (u_Channel == 2) Out = vec3(S.x);
  else if (u_Channel == 3) Out = vec3(S.y);
  else if (u_Channel == 4) Out = vec3(clamp(S.z * u_HeightView + 0.5, 0.0, 1.0));
  else if (u_Channel == 5) Out = texture(u_NormalMap, v_Uv).rgb;
  else if (u_Channel == 6) Out = pow(Aces(E), vec3(1.0 / 2.2));
  else if (u_Channel == 7) {
    float M = texture(u_MaskView, v_Uv).r;
    float Lit = 0.55 + 0.45 * max(dot(N, V), 0.0);
    Out = mix(vec3(0.07), vec3(0.95), M) * Lit;
  } else {
    vec2 G = floor(v_Uv * 16.0);
    float Check = mod(G.x + G.y, 2.0);
    Out = mix(vec3(0.18), vec3(0.75), Check) * (0.55 + 0.45 * max(dot(N, V), 0.0));
  }
  o_Color = vec4(Out, 1.0);
}`;

export const LineVertex = `#version 300 es
layout(location = 0) in vec3 a_Position;
uniform mat4 u_ViewProjection;
void main() {
  gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}`;
export const LineFragment = `${Header}
uniform vec4 u_Color;
out vec4 o_Color;
void main() { o_Color = u_Color; }`;

export const BackgroundFragment = `${Header}
${Lighting}
uniform mat4 u_InverseViewProjection;
uniform int u_ShowEnvironment;
in vec2 v_Uv;
out vec4 o_Color;
void main() {
  vec2 Ndc = v_Uv * 2.0 - 1.0;
  if (u_ShowEnvironment == 1) {
    vec4 Far = u_InverseViewProjection * vec4(Ndc, 1.0, 1.0);
    vec4 Near = u_InverseViewProjection * vec4(Ndc, -1.0, 1.0);
    vec3 D = normalize(Far.xyz / Far.w - Near.xyz / Near.w);
    vec3 C = Aces(Environment(D, 0.35) * u_Exposure * 0.6);
    o_Color = vec4(pow(C, vec3(1.0 / 2.2)), 1.0);
    return;
  }
  float R = length((v_Uv - vec2(0.5, 0.55)) * vec2(1.25, 1.0));
  vec3 C = mix(vec3(0.098), vec3(0.058), smoothstep(0.0, 0.9, R));
  o_Color = vec4(C, 1.0);
}`;

// UV editor: draws a channel inside a pannable rectangle over a checkerboard.
export const UvViewVertex = `#version 300 es
layout(location = 0) in vec2 a_Position;
uniform vec4 u_Rect;      // ndc x, y, width, height
out vec2 v_Uv;
void main() {
  v_Uv = a_Position * 0.5 + 0.5;
  gl_Position = vec4(u_Rect.xy + v_Uv * u_Rect.zw, 0.0, 1.0);
}`;
export const UvViewFragment = `${Header}
uniform sampler2D u_Color;
uniform sampler2D u_Surface;
uniform sampler2D u_Emissive;
uniform sampler2D u_NormalMap;
uniform sampler2D u_MaskView;
uniform sampler2D u_Position;
uniform int u_Channel;
uniform float u_HeightView;
uniform float u_Checker;
uniform int u_DimOutside;
in vec2 v_Uv;
out vec4 o_Color;
vec3 Aces(vec3 X) { return clamp((X * (2.51 * X + 0.03)) / (X * (2.43 * X + 0.59) + 0.14), 0.0, 1.0); }
void main() {
  vec4 C = texture(u_Color, v_Uv);
  vec4 S = texture(u_Surface, v_Uv);
  vec3 Out;
  if (u_Channel == 2) Out = vec3(S.x);
  else if (u_Channel == 3) Out = vec3(S.y);
  else if (u_Channel == 4) Out = vec3(clamp(S.z * u_HeightView + 0.5, 0.0, 1.0));
  else if (u_Channel == 5) Out = texture(u_NormalMap, v_Uv).rgb;
  else if (u_Channel == 6) Out = pow(Aces(texture(u_Emissive, v_Uv).rgb), vec3(1.0 / 2.2));
  else if (u_Channel == 7) Out = vec3(texture(u_MaskView, v_Uv).r);
  else if (u_Channel == 8) { vec2 G = floor(v_Uv * 16.0); Out = mix(vec3(0.18), vec3(0.75), mod(G.x + G.y, 2.0)); }
  else Out = C.rgb;
  if (u_DimOutside == 1) {
    float Inside = texture(u_Position, v_Uv).w;
    vec2 G = floor(gl_FragCoord.xy / u_Checker);
    vec3 Board = mix(vec3(0.075), vec3(0.095), mod(G.x + G.y, 2.0));
    Out = mix(mix(Board, Out, 0.28), Out, step(0.5, Inside));
  }
  o_Color = vec4(Out, 1.0);
}`;

export const UvLineVertex = `#version 300 es
layout(location = 0) in vec2 a_Uv;
uniform vec4 u_Rect;
void main() {
  gl_Position = vec4(u_Rect.xy + a_Uv * u_Rect.zw, 0.0, 1.0);
}`;

// Analytic sphere for material library thumbnails.
export const PreviewFragment = `${Header}
${Noise}
${MaterialLibrary}
${Lighting}
uniform Material u_Material[2];
uniform sampler2D u_Bitmap0;
in vec2 v_Uv;
out vec4 o_Color;
void main() {
  vec2 P = v_Uv * 2.0 - 1.0;
  P *= 1.08;
  float R2 = dot(P, P);
  if (R2 > 1.0) {
    float Edge = smoothstep(1.0, 1.02, R2);
    o_Color = vec4(0.0, 0.0, 0.0, 0.0);
    return;
  }
  vec3 N = vec3(P, sqrt(1.0 - R2));
  vec3 V = vec3(0.0, 0.0, 1.0);
  float Yaw = 0.6;
  vec3 O = vec3(cos(Yaw) * N.x + sin(Yaw) * N.z, N.y, -sin(Yaw) * N.x + cos(Yaw) * N.z);
  vec2 Uv = vec2(atan(O.x, O.z) / 6.2831853 + 0.5, acos(-O.y) / 3.14159);
  Surface S = EvaluateMaterial(u_Material[0], u_Bitmap0, Uv * vec2(2.0, 1.0), O, O);
  // Height as bump via finite difference of the material on the sphere.
  vec3 C = Shade(S.Color, S.Roughness, S.Metallic, S.Emissive, N, V);
  C = pow(Aces(C), vec3(1.0 / 2.2));
  float Alpha = 1.0 - smoothstep(0.985, 1.0, R2);
  o_Color = vec4(C * Alpha, Alpha);
}`;

// Format converters used by save/load and export.
export const DecodeFragment = `${Header}
uniform sampler2D u_Source;
uniform int u_Mode;      // 0 copy · 1 signed (x*2-1) for xyz
uniform vec4 u_Scale;
out vec4 o_Value;
void main() {
  vec4 V = texelFetch(u_Source, ivec2(gl_FragCoord.xy), 0);
  if (u_Mode == 1) V.xyz = V.xyz * 2.0 - 1.0;
  o_Value = V * u_Scale;
}`;

export const BlitFragment = `${Header}
uniform sampler2D u_Source;
in vec2 v_Uv;
out vec4 o_Value;
void main() { o_Value = texture(u_Source, v_Uv); }`;
