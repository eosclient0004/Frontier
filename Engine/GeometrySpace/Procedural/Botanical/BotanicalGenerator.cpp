/*============================================================================================================================================
                                                         BOTANICALGENERATOR.CPP
============================================================================================================================================*/
// 🧩 Exact C++20 Port of Frontier's Procedural Botanical Generators (FernMesher -> PalmMesher -> TreeMesher) in Right-Hand +Z Up.
//    - English Oak & Palms use `WeldedMesher` (`BotanicalWeldedManifold`) so roots + trunk + limbs + branches + forks merge
//      cleanly into ONE single watertight genus-0 quad manifold (`V - E + F = 2`, `BoundaryEdges == 0`, `Components == 1`)!
//    - Ferns port `FernMesher` faithfully: fibrous rhizome + papery ramenta scales + coiled crosiers + U-sulcus grooved rachis
//      + veined, serrated, auricled 3D pinnae (1-pinnate SwordFern & 2-pinnate lacy BrackenFern).

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Procedural/Botanical/BotanicalGenerator.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Frontier::GeometrySpace::Botanical
{

namespace
{

using V3 = std::array<float, 3>;
constexpr float kPi      = 3.14159265358979323846f;
constexpr float kTau     = 6.28318530717958647692f;
constexpr float kDeg2Rad = kPi / 180.0f;
constexpr float kGolden  = 2.39996322972865332f;
constexpr V3    kUpZ     { 0.0f, 0.0f, 1.0f };

[[nodiscard]] inline V3 Add3(const V3& a, const V3& b) noexcept { return { a[0] + b[0], a[1] + b[1], a[2] + b[2] }; }
[[nodiscard]] inline V3 Sub3(const V3& a, const V3& b) noexcept { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
[[nodiscard]] inline V3 Mul3(const V3& a, float s) noexcept     { return { a[0] * s, a[1] * s, a[2] * s }; }
[[nodiscard]] inline V3 AddScaled3(const V3& a, const V3& b, float s) noexcept { return { a[0] + b[0] * s, a[1] + b[1] * s, a[2] + b[2] * s }; }
[[nodiscard]] inline float Dot3(const V3& a, const V3& b) noexcept { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
[[nodiscard]] inline V3 Cross3(const V3& a, const V3& b) noexcept
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}
[[nodiscard]] inline float LenSq3(const V3& a) noexcept { return Dot3(a, a); }
[[nodiscard]] inline float Len3(const V3& a) noexcept   { return std::sqrt(LenSq3(a)); }
[[nodiscard]] inline V3 Norm3(const V3& a, const V3& fb = kUpZ) noexcept
{
    const float l = Len3(a);
    return (l > 1.0e-7f) ? Mul3(a, 1.0f / l) : fb;
}
[[nodiscard]] inline V3 Mix3(const V3& a, const V3& b, float t) noexcept
{
    return { a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t };
}
[[nodiscard]] inline V3 ProjectOnPlane3(const V3& v, const V3& n) noexcept { return Sub3(v, Mul3(n, Dot3(v, n))); }
[[nodiscard]] inline V3 AnyPerp3(const V3& n) noexcept
{
    const V3 ref = (std::fabs(n[2]) < 0.85f) ? kUpZ : V3{ 1.0f, 0.0f, 0.0f };
    return Norm3(Cross3(ref, n));
}
[[nodiscard]] inline float SmoothStep(float e0, float e1, float x) noexcept
{
    const float t = std::clamp((x - e0) / std::max(e1 - e0, 1.0e-5f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
[[nodiscard]] inline V3 RotateAxis(const V3& v, const V3& axis, float rad) noexcept
{
    const V3 k = Norm3(axis);
    const float c = std::cos(rad), s = std::sin(rad);
    return Add3(Add3(Mul3(v, c), Mul3(Cross3(k, v), s)), Mul3(k, Dot3(k, v) * (1.0f - c)));
}
[[nodiscard]] inline V3 RotateTowards(const V3& from, const V3& to, float maxRad) noexcept
{
    const float ang = std::acos(std::clamp(Dot3(from, to), -1.0f, 1.0f));
    if (ang <= 1.0e-6f || maxRad <= 0.0f) return from;
    if (ang <= maxRad) return to;
    V3 ax = Cross3(from, to);
    if (LenSq3(ax) < 1.0e-10f) ax = AnyPerp3(from);
    return Norm3(RotateAxis(from, ax, maxRad));
}
[[nodiscard]] inline float AngleBetween(const V3& a, const V3& b) noexcept
{
    return std::acos(std::clamp(Dot3(a, b) / std::max(1.0e-7f, Len3(a) * Len3(b)), -1.0f, 1.0f));
}

class DetRng
{
public:
    explicit DetRng(uint32_t seed) noexcept : State_(seed == 0u ? 0x9E3779B9u : seed) {}
    [[nodiscard]] uint32_t NextU32() noexcept
    {
        uint32_t x = State_;
        x ^= x << 13u; x ^= x >> 17u; x ^= x << 5u;
        State_ = x;
        return x;
    }
    [[nodiscard]] float Next01() noexcept { return static_cast<float>(NextU32() & 0x00FFFFFFu) * (1.0f / 16777216.0f); }
    [[nodiscard]] float Uniform() noexcept { return Next01() * 2.0f - 1.0f; }
    [[nodiscard]] float Range(float lo, float hi) noexcept { return lo + (hi - lo) * Next01(); }
    [[nodiscard]] float Vary(float base, float spread) noexcept { return base + Uniform() * spread; }
    [[nodiscard]] DetRng Fork(uint32_t salt) noexcept { return DetRng((NextU32() ^ salt) | 1u); }
private:
    uint32_t State_ = 1u;
};

void EmitTriSmooth(
    BotanicalPlantBuildResult& out,
    const V3& v0, const V3& v1, const V3& v2,
    const V3& n0, const V3& n1, const V3& n2,
    const V3& col, float windFlex, float phase, BotanicalPartTag part)
{
    const V3 fn = Cross3(Sub3(v1, v0), Sub3(v2, v0));
    if (Dot3(fn, fn) < 1.0e-12f) return;
    BotanicalTriangle t {};
    t.V0 = v0; t.V1 = v1; t.V2 = v2;
    t.RestV0 = v0; t.RestV1 = v1; t.RestV2 = v2;
    t.Normal = Norm3(fn);
    t.N0 = (LenSq3(n0) > 1.0e-6f) ? Norm3(n0) : t.Normal;
    t.N1 = (LenSq3(n1) > 1.0e-6f) ? Norm3(n1) : t.Normal;
    t.N2 = (LenSq3(n2) > 1.0e-6f) ? Norm3(n2) : t.Normal;
    t.ColorLin = col;
    t.WindFlex = windFlex;
    t.Phase    = phase;
    t.Part     = part;
    out.Triangles.push_back(t);
    if (part == BotanicalPartTag::Wood)       ++out.WoodTriCount;
    else if (part == BotanicalPartTag::Leaf)  ++out.LeafTriCount;
    else                                      ++out.FruitTriCount;
}

void EmitTri(
    BotanicalPlantBuildResult& out,
    const V3& v0, const V3& v1, const V3& v2,
    const V3& col, float windFlex, float phase, BotanicalPartTag part)
{
    const V3 fn = Norm3(Cross3(Sub3(v1, v0), Sub3(v2, v0)));
    EmitTriSmooth(out, v0, v1, v2, fn, fn, fn, col, windFlex, phase, part);
}

void EmitQuad(
    BotanicalPlantBuildResult& out,
    const V3& a, const V3& b, const V3& c, const V3& d,
    const V3& col, float windFlex, float phase, BotanicalPartTag part)
{
    EmitTri(out, a, b, c, col, windFlex, phase, part);
    EmitTri(out, a, c, d, col, windFlex, phase, part);
}

/// Converts a WeldedQuadMesh into smooth area-weighted shaded triangles + emits watertight SDF capsules along stems!
void EmitWeldedQuadMeshToPlant(
    BotanicalPlantBuildResult& out,
    const WeldedQuadMesh&      wm)
{
    const std::size_t nV = wm.Positions.size();
    std::vector<V3> smoothNormals(nV, { 0.0f, 0.0f, 0.0f });

    for (const auto& q : wm.Quads)
    {
        const V3& p0 = wm.Positions[q[0]];
        const V3& p1 = wm.Positions[q[1]];
        const V3& p2 = wm.Positions[q[2]];
        const V3& p3 = wm.Positions[q[3]];
        const V3 nA  = Cross3(Sub3(p1, p0), Sub3(p2, p0));
        const V3 nB  = Cross3(Sub3(p2, p0), Sub3(p3, p0));
        const V3 fn  = Add3(nA, nB);
        for (int k = 0; k < 4; ++k)
        {
            smoothNormals[q[static_cast<std::size_t>(k)]] = Add3(smoothNormals[q[static_cast<std::size_t>(k)]], fn);
        }
    }
    for (const auto& t : wm.Tris)
    {
        const V3 fn = Cross3(Sub3(wm.Positions[t[1]], wm.Positions[t[0]]), Sub3(wm.Positions[t[2]], wm.Positions[t[0]]));
        for (int k = 0; k < 3; ++k)
        {
            smoothNormals[t[static_cast<std::size_t>(k)]] = Add3(smoothNormals[t[static_cast<std::size_t>(k)]], fn);
        }
    }
    for (auto& n : smoothNormals) n = Norm3(n, kUpZ);

    for (const auto& q : wm.Quads)
    {
        const V3& p0 = wm.Positions[q[0]];
        const V3& p1 = wm.Positions[q[1]];
        const V3& p2 = wm.Positions[q[2]];
        const V3& p3 = wm.Positions[q[3]];
        const V3 cAvg {
            0.25f * (wm.Colors[q[0]][0] + wm.Colors[q[1]][0] + wm.Colors[q[2]][0] + wm.Colors[q[3]][0]),
            0.25f * (wm.Colors[q[0]][1] + wm.Colors[q[1]][1] + wm.Colors[q[2]][1] + wm.Colors[q[3]][1]),
            0.25f * (wm.Colors[q[0]][2] + wm.Colors[q[1]][2] + wm.Colors[q[2]][2] + wm.Colors[q[3]][2])
        };
        const float flex  = 0.25f * (wm.WindFlex[q[0]] + wm.WindFlex[q[1]] + wm.WindFlex[q[2]] + wm.WindFlex[q[3]]);
        const float phase = wm.Phase[q[0]];
        EmitTriSmooth(out, p0, p1, p2, smoothNormals[q[0]], smoothNormals[q[1]], smoothNormals[q[2]], cAvg, flex, phase, BotanicalPartTag::Wood);
        EmitTriSmooth(out, p0, p2, p3, smoothNormals[q[0]], smoothNormals[q[2]], smoothNormals[q[3]], cAvg, flex, phase, BotanicalPartTag::Wood);
    }
    for (const auto& t : wm.Tris)
    {
        const V3 cAvg {
            (wm.Colors[t[0]][0] + wm.Colors[t[1]][0] + wm.Colors[t[2]][0]) / 3.0f,
            (wm.Colors[t[0]][1] + wm.Colors[t[1]][1] + wm.Colors[t[2]][1]) / 3.0f,
            (wm.Colors[t[0]][2] + wm.Colors[t[1]][2] + wm.Colors[t[2]][2]) / 3.0f
        };
        const float flex = (wm.WindFlex[t[0]] + wm.WindFlex[t[1]] + wm.WindFlex[t[2]]) / 3.0f;
        EmitTriSmooth(out, wm.Positions[t[0]], wm.Positions[t[1]], wm.Positions[t[2]],
                      smoothNormals[t[0]], smoothNormals[t[1]], smoothNormals[t[2]], cAvg, flex, wm.Phase[t[0]], BotanicalPartTag::Wood);
    }
}

/// Emits watertight ball-jointed BotanicalWoodCapsules directly from the final welded stem skeleton!
void EmitWoodCapsulesFromWeldedHierarchy(
    BotanicalPlantBuildResult& out,
    const WeldedStem&          stem,
    const WeldedMeshConfig&    cfg,
    float                      heightEst)
{
    if (stem.Dropped || stem.Nodes.size() < 2u) return;
    if (stem.Level <= 1u || stem.Role == WeldedStemRole::Root)
    {
        const uint32_t segs = (stem.Level == 0u) ? 8u : 4u;
        for (uint32_t i = 0u; i < segs; ++i)
        {
            const float s0 = (static_cast<float>(i) / static_cast<float>(segs)) * stem.Length;
            const float s1 = (static_cast<float>(i + 1u) / static_cast<float>(segs)) * stem.Length;
            const auto f0 = SampleWeldedStem(stem, s0);
            const auto f1 = SampleWeldedStem(stem, s1);
            BotanicalWoodCapsule cap {};
            cap.P0 = f0.Pos;  cap.R0 = WeldedRadiusAt(stem, s0, cfg);
            cap.P1 = f1.Pos;  cap.R1 = WeldedRadiusAt(stem, s1, cfg);
            cap.RestP0 = cap.P0; cap.RestP1 = cap.P1;
            cap.WindFlex0 = (stem.Role == WeldedStemRole::Root) ? 0.0f : std::pow(std::clamp(f0.Pos[2] / std::max(1.0f, heightEst), 0.0f, 1.0f), 1.4f);
            cap.WindFlex1 = (stem.Role == WeldedStemRole::Root) ? 0.0f : std::pow(std::clamp(f1.Pos[2] / std::max(1.0f, heightEst), 0.0f, 1.0f), 1.4f);
            cap.BranchLevel = (stem.Role == WeldedStemRole::Root) ? 0u : stem.Level;
            out.SdfVolume.WoodCapsules.push_back(cap);
        }
    }
    for (const auto& c : stem.Children) if (c) EmitWoodCapsulesFromWeldedHierarchy(out, *c, cfg, heightEst);
    for (const auto& f : stem.Forks)    if (f) EmitWoodCapsulesFromWeldedHierarchy(out, *f, cfg, heightEst);
}

void EmitOvoid(
    BotanicalPlantBuildResult& out,
    const V3& center, float rx, float ry, float rz,
    const V3& col, float windFlex, float phase, BotanicalPartTag part)
{
    constexpr int rings = 4, segs = 6;
    for (int i = 0; i < rings; ++i)
    {
        const float p0 = -0.5f * kPi + kPi * (static_cast<float>(i) / rings);
        const float p1 = -0.5f * kPi + kPi * (static_cast<float>(i + 1) / rings);
        for (int j = 0; j < segs; ++j)
        {
            const float t0 = kTau * (static_cast<float>(j) / segs);
            const float t1 = kTau * (static_cast<float>(j + 1) / segs);
            auto pt = [&](float phi, float th) -> V3 {
                return { center[0] + rx * std::cos(phi) * std::cos(th),
                         center[1] + ry * std::cos(phi) * std::sin(th),
                         center[2] + rz * std::sin(phi) };
            };
            EmitQuad(out, pt(p0, t0), pt(p0, t1), pt(p1, t1), pt(p1, t0), col, windFlex, phase, part);
        }
    }
}

struct CurveFrame3
{
    V3 Pos   { 0.0f, 0.0f, 0.0f };
    V3 Dir   { 0.0f, 0.0f, 1.0f };
    V3 Right { 1.0f, 0.0f, 0.0f };
    V3 Up    { 0.0f, 1.0f, 0.0f };
    float T  = 0.0f;
};

[[nodiscard]] CurveFrame3 SampleCurveFrames(const std::vector<CurveFrame3>& frs, float t) noexcept
{
    if (frs.empty()) return {};
    const float tc = std::clamp(t, 0.0f, 1.0f) * static_cast<float>(frs.size() - 1u);
    const std::size_t i0 = std::min(static_cast<std::size_t>(tc), frs.size() - 2u);
    const float a = tc - static_cast<float>(i0);
    const V3 pos = Mix3(frs[i0].Pos, frs[i0 + 1u].Pos, a);
    const V3 dir = Norm3(Mix3(frs[i0].Dir, frs[i0 + 1u].Dir, a));
    V3 rgt = ProjectOnPlane3(Mix3(frs[i0].Right, frs[i0 + 1u].Right, a), dir);
    rgt = (LenSq3(rgt) < 1.0e-8f) ? AnyPerp3(dir) : Norm3(rgt);
    const V3 up = Norm3(Cross3(dir, rgt));
    return { pos, dir, rgt, up, t };
}

/// Grooved adaxial U-sulcus stipe/rachis tube (exact port of FernMesher.meshGroovedRachis!)
void EmitGroovedRachis(
    BotanicalPlantBuildResult&      out,
    const std::vector<CurveFrame3>& frames,
    float                           rStart,
    float                           rEnd,
    uint32_t                        sides,
    const V3&                       colBase,
    const V3&                       colTip,
    float                           phase,
    float                           baseFlex,
    float                           flexSpan,
    bool                            emitCapsules)
{
    if (frames.size() < 2u) return;
    std::vector<std::vector<V3>> rings(frames.size(), std::vector<V3>(sides));
    std::vector<std::vector<V3>> norms(frames.size(), std::vector<V3>(sides));

    for (std::size_t i = 0u; i < frames.size(); ++i)
    {
        const auto& fr = frames[i];
        const float r = rStart + (rEnd - rStart) * std::pow(fr.T, 0.85f);
        for (uint32_t j = 0u; j < sides; ++j)
        {
            const float th = (static_cast<float>(j) / static_cast<float>(sides)) * kTau;
            const float c = std::cos(th), s = std::sin(th);
            // Adaxial U-sulcus groove indentation along upper (+Up) face of fern rachis!
            const float sulcus = (s > 0.25f) ? (1.0f - 0.34f * std::pow((s - 0.25f) / 0.75f, 1.5f)) : 1.0f;
            const V3 rad = Add3(Mul3(fr.Right, c * r * 1.06f), Mul3(fr.Up, s * r * sulcus));
            rings[i][j] = Add3(fr.Pos, rad);
            norms[i][j] = Norm3(rad);
        }
    }
    for (std::size_t i = 0u; i + 1u < frames.size(); ++i)
    {
        const float tm = 0.5f * (frames[i].T + frames[i + 1u].T);
        const V3 col = Mix3(colBase, colTip, SmoothStep(0.05f, 0.65f, tm));
        const float fl = baseFlex + flexSpan * std::pow(tm, 1.35f);
        for (uint32_t j = 0u; j < sides; ++j)
        {
            const uint32_t j1 = (j + 1u) % sides;
            EmitTriSmooth(out, rings[i][j], rings[i][j1], rings[i + 1u][j1],
                          norms[i][j], norms[i][j1], norms[i + 1u][j1], col, fl, phase, BotanicalPartTag::Wood);
            EmitTriSmooth(out, rings[i][j], rings[i + 1u][j1], rings[i + 1u][j],
                          norms[i][j], norms[i + 1u][j1], norms[i + 1u][j], col, fl, phase, BotanicalPartTag::Wood);
        }
    }
    if (emitCapsules)
    {
        const std::size_t stride = std::max<std::size_t>(1u, frames.size() / 4u);
        for (std::size_t i = 0u; i + 1u < frames.size(); i += stride)
        {
            const std::size_t i1 = std::min(i + stride, frames.size() - 1u);
            BotanicalWoodCapsule cap {};
            cap.P0 = frames[i].Pos;  cap.R0 = (rStart + (rEnd - rStart) * frames[i].T) * 1.25f;
            cap.P1 = frames[i1].Pos; cap.R1 = (rStart + (rEnd - rStart) * frames[i1].T) * 1.25f;
            cap.RestP0 = cap.P0; cap.RestP1 = cap.P1;
            cap.WindFlex0 = baseFlex + flexSpan * std::pow(frames[i].T, 1.35f);
            cap.WindFlex1 = baseFlex + flexSpan * std::pow(frames[i1].T, 1.35f);
            cap.BranchLevel = 1u;
            out.SdfVolume.WoodCapsules.push_back(cap);
        }
    }
}

/// Sculpted 3D veined, serrated, auricled & cupped lamina blade + 2.5D Thin-Sheet SDF instance!
void EmitSculptedBladeAndSdf(
    BotanicalPlantBuildResult& out,
    const V3&                  origin,
    const V3&                  axisLen,
    const V3&                  axisWid,
    const V3&                  axisNorm,
    float                      lengthM,
    float                      halfWidthM,
    float                      camberX,
    float                      camberZ,
    float                      auricle,
    float                      serrationDepth,
    int                        rachisSide,
    const V3&                  colBase,
    const V3&                  colTip,
    float                      windFlex,
    float                      phase,
    LeafSdfArchetype           arch,
    int                        lengthSegs = 5)
{
    std::vector<std::array<V3, 3u>> rows(static_cast<std::size_t>(lengthSegs + 1));
    std::vector<std::array<V3, 3u>> nRows(static_cast<std::size_t>(lengthSegs + 1));

    for (int i = 0; i <= lengthSegs; ++i)
    {
        const float u = static_cast<float>(i) / static_cast<float>(lengthSegs);
        float env = std::sin(kPi * std::pow(std::clamp(u, 0.01f, 0.99f), 0.72f));
        if (arch == LeafSdfArchetype::LobedOak)
        {
            env *= (0.74f + 0.26f * std::fabs(std::sin(u * kPi * 3.5f)));
        }
        else if (arch == LeafSdfArchetype::FernPinna)
        {
            env = std::pow(1.0f - std::pow(u, 1.35f), 0.72f) * (0.35f + 0.65f * std::min(1.0f, u * 5.0f));
            env *= (1.0f + serrationDepth * 0.45f * std::sin(u * kPi * 12.0f) * std::sin(kPi * u));
        }
        const float aurLobe = auricle * 0.55f * std::exp(-std::pow((u - 0.14f) / 0.11f, 2.0f));
        const float wL = std::max(lengthM * 0.025f, halfWidthM * (env + (rachisSide < 0 ? aurLobe : 0.0f)));
        const float wR = std::max(lengthM * 0.025f, halfWidthM * (env + (rachisSide > 0 ? aurLobe : 0.0f)));
        const float droopZ   = -std::pow(u, 1.65f) * lengthM * (0.16f + camberZ * 0.45f);
        const float cupLift  = halfWidthM * std::max(0.14f, camberX * 0.72f) * env;
        const V3 mid = Add3(AddScaled3(origin, axisLen, u * lengthM), Mul3(axisNorm, droopZ));
        rows[static_cast<std::size_t>(i)][0u] = Add3(AddScaled3(mid, axisWid, -wL), Mul3(axisNorm, cupLift));
        rows[static_cast<std::size_t>(i)][1u] = mid;
        rows[static_cast<std::size_t>(i)][2u] = Add3(AddScaled3(mid, axisWid, +wR), Mul3(axisNorm, cupLift));

        nRows[static_cast<std::size_t>(i)][0u] = Norm3(AddScaled3(axisNorm, axisWid, +0.26f));
        nRows[static_cast<std::size_t>(i)][1u] = axisNorm;
        nRows[static_cast<std::size_t>(i)][2u] = Norm3(AddScaled3(axisNorm, axisWid, -0.26f));
    }

    for (int i = 0; i < lengthSegs; ++i)
    {
        const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(lengthSegs);
        const V3 c = Mix3(colBase, colTip, SmoothStep(0.08f, 0.88f, u));
        const auto& r0 = rows[static_cast<std::size_t>(i)];
        const auto& r1 = rows[static_cast<std::size_t>(i + 1)];
        const auto& n0 = nRows[static_cast<std::size_t>(i)];
        const auto& n1 = nRows[static_cast<std::size_t>(i + 1)];
        EmitTriSmooth(out, r0[0], r0[1], r1[1], n0[0], n0[1], n1[1], c, windFlex, phase, BotanicalPartTag::Leaf);
        EmitTriSmooth(out, r0[0], r1[1], r1[0], n0[0], n1[1], n1[0], c, windFlex, phase, BotanicalPartTag::Leaf);
        EmitTriSmooth(out, r0[1], r0[2], r1[2], n0[1], n0[2], n1[2], c, windFlex, phase, BotanicalPartTag::Leaf);
        EmitTriSmooth(out, r0[1], r1[2], r1[1], n0[1], n1[2], n1[1], c, windFlex, phase, BotanicalPartTag::Leaf);
    }

    BotanicalLeafSdfInstance lf {};
    lf.CenterMeters   = AddScaled3(origin, axisLen, 0.5f * lengthM);
    lf.RestCenterM    = lf.CenterMeters;
    lf.HalfSpanMeters = std::max(0.02f, 0.52f * lengthM);
    lf.AxisX          = Norm3(axisWid);
    lf.AxisY          = Norm3(axisNorm);
    lf.AxisZ          = Norm3(axisLen);
    lf.CamberX        = camberX;
    lf.CamberZ        = camberZ;
    lf.HalfThicknessM = kLeafHalfThickM;
    lf.WindFlex       = windFlex;
    lf.Phase          = phase;
    lf.Archetype      = arch;
    out.SdfVolume.Leaves.push_back(lf);
}

//========================================================================================================================
//                        1. EXACT PORT OF FERNMESHER.TS (+Z UP, SWORD FERN & BIPINNATE BRACKEN FERN)
//========================================================================================================================

void BuildFernGeometry(const BotanicalSpeciesDefinition& sp, uint32_t seed, uint32_t tier, BotanicalPlantBuildResult& out)
{
    DetRng rng(seed ^ 0x4F3EA913u);
    const auto& fp  = sp.Fern;
    const auto& mat = sp.Materials;
    const V3 colStipe = mat.WoodColorLinear;
    const V3 colBase  = mat.LeafBaseColorLinear;
    const V3 colYoung = mat.LeafYoungColorLinear;
    const V3 colScale = Mix3(colStipe, { 0.36f, 0.22f, 0.12f }, 0.5f);
    const LeafSdfArchetype arch = ParseLeafArchetypeToken(sp.Leaves.LeafArchetypeSdf);
    const bool bipinnate = (sp.Architecture == "bipinnate_lacy");

    // 1. Fibrous Rhizome Crown + Papery Ramenta Scales
    EmitOvoid(out, { 0.0f, 0.0f, fp.CaudexHeightMeters * 0.45f },
        fp.CrownRadiusMeters * 1.15f, fp.CrownRadiusMeters * 1.15f, fp.CaudexHeightMeters * 1.15f,
        colStipe, 0.0f, 0.0f, BotanicalPartTag::Wood);

    // 2. Coiled Fiddlehead Crosiers (Archimedean logarithmic spiral + embryonic buds!)
    for (uint32_t c = 0u; c < fp.CrosierCount; ++c)
    {
        const float az = static_cast<float>(c) * kGolden + rng.Range(0.0f, 0.45f);
        const V3 outH { std::cos(az), std::sin(az), 0.0f };
        const V3 rgt  = Norm3(Cross3(kUpZ, outH));
        const int steps = 18;
        std::vector<CurveFrame3> frs(static_cast<std::size_t>(steps + 1));
        V3 cur { outH[0] * fp.CrownRadiusMeters * 0.32f, outH[1] * fp.CrownRadiusMeters * 0.32f, fp.CaudexHeightMeters * 0.72f };
        V3 dir = Norm3(AddScaled3(kUpZ, outH, 0.22f));
        const float segL = fp.CrosierHeightMeters / static_cast<float>(steps);
        for (int s = 0; s <= steps; ++s)
        {
            const float t = static_cast<float>(s) / static_cast<float>(steps);
            frs[static_cast<std::size_t>(s)] = { cur, dir, rgt, Norm3(Cross3(dir, rgt)), t };
            const float curlRate = (t > 0.40f) ? (fp.CrosierTurns * kTau / static_cast<float>(steps)) * 1.28f : 0.07f;
            dir = Norm3(RotateAxis(dir, rgt, -curlRate));
            cur = AddScaled3(cur, dir, segL * (1.0f - 0.48f * t));
        }
        EmitGroovedRachis(out, frs, fp.StipeRadiusMeters * 1.15f, fp.StipeRadiusMeters * 0.22f, 6u,
            colStipe, colYoung, static_cast<float>(c) * 0.23f, 0.0f, 0.45f, false);
    }

    // 3. Golden-Angle Spiral Rosette of Arching Grooved Fronds + Veined/Auricled Pinnae (or Bipinnate Lacy Pinnules!)
    const uint32_t frondCount  = std::max(6u, fp.FrondCount);
    const uint32_t rachisSteps = (tier >= 2u) ? 20u : 14u;
    const uint32_t nPairs      = std::max(8u, (tier >= 2u) ? fp.PinnaPairs : (fp.PinnaPairs * 3u) / 4u);

    for (uint32_t fi = 0u; fi < frondCount; ++fi)
    {
        const float age01 = static_cast<float>(fi) / static_cast<float>(std::max(1u, frondCount - 1u));
        const float az    = static_cast<float>(fi) * kGolden + rng.Vary(0.0f, 0.12f);
        const V3 outH { std::cos(az), std::sin(az), 0.0f };
        const V3 rgt0 = Norm3(Cross3(kUpZ, outH));

        const float tiltRad  = std::clamp(fp.RosetteAngleDeg * (0.52f + 0.76f * age01) + rng.Vary(0.0f, 4.5f), 6.0f, 84.0f) * kDeg2Rad;
        const float L        = fp.FrondLengthMeters * (0.76f + 0.30f * std::sin(age01 * kPi * 0.72f + 0.25f)) * (1.0f + rng.Vary(0.0f, fp.FrondLengthVar));
        const float droopRad = (fp.ArchDroopDeg * (0.58f + 0.66f * age01) + rng.Vary(0.0f, 6.5f)) * kDeg2Rad;
        const float twistRad = rng.Vary(0.0f, fp.RachisTwistDeg) * kDeg2Rad;
        const float waveRad  = fp.RachisWaveDeg * kDeg2Rad;
        const float phase    = rng.Next01();

        V3 cur { outH[0] * fp.CrownRadiusMeters * (0.38f + 0.62f * age01),
                 outH[1] * fp.CrownRadiusMeters * (0.38f + 0.62f * age01),
                 fp.CaudexHeightMeters * (0.92f - 0.32f * age01) };
        V3 dir = Norm3(Add3(Mul3(kUpZ, std::cos(tiltRad)), Mul3(outH, std::sin(tiltRad))));
        V3 rgt = rgt0;

        std::vector<CurveFrame3> frs(rachisSteps + 1u);
        const float stepL = L / static_cast<float>(rachisSteps);
        for (uint32_t s = 0u; s <= rachisSteps; ++s)
        {
            const float t = static_cast<float>(s) / static_cast<float>(rachisSteps);
            const V3 up = Norm3(Cross3(dir, rgt));
            frs[s] = { cur, dir, rgt, up, t };
            if (s < rachisSteps)
            {
                const float t1 = static_cast<float>(s + 1u) / static_cast<float>(rachisSteps);
                float dPitch = droopRad * (std::pow(t1, fp.ArchPower) - std::pow(t, fp.ArchPower));
                if (cur[2] < 0.05f && t > 0.42f) dPitch *= 0.32f;
                const float dYaw  = (std::sin(t1 * kPi * 2.2f + phase * kTau) - std::sin(t * kPi * 2.2f + phase * kTau)) * waveRad;
                const float dRoll = (t1 - t) * twistRad;
                dir = Norm3(RotateAxis(dir, rgt, dPitch));
                dir = Norm3(RotateAxis(dir, up, dYaw));
                rgt = Norm3(RotateAxis(rgt, dir, dRoll));
                cur = AddScaled3(cur, dir, stepL);
            }
        }

        const float stipeR = fp.StipeRadiusMeters * (0.82f + 0.28f * age01);
        const V3 frondCol  = Mix3(colYoung, colBase, std::clamp(age01 * 0.78f, 0.0f, 1.0f));
        EmitGroovedRachis(out, frs, stipeR, stipeR * 0.16f, 6u, colStipe, frondCol, phase, 0.0f, 1.0f, true);

        // Chaffy papery ramenta scales on lower stipe
        for (int sc = 0; sc < 6; ++sc)
        {
            const float ts = (static_cast<float>(sc) / 6.0f) * fp.StipeFraction * 1.1f;
            const auto sf = SampleCurveFrames(frs, ts);
            const float th = static_cast<float>(sc) * kGolden;
            const V3 rad = Norm3(Add3(Mul3(sf.Right, std::cos(th)), Mul3(sf.Up, std::sin(th))));
            const V3 scDir = Norm3(Add3(Mul3(sf.Dir, 0.75f), Mul3(rad, 0.65f)));
            const V3 scWid = Norm3(Cross3(sf.Dir, rad));
            const float sL = stipeR * 3.4f * (1.0f - ts);
            const V3 p0 = AddScaled3(sf.Pos, rad, stipeR * 0.9f);
            EmitTri(out, AddScaled3(p0, scWid, -sL * 0.18f), AddScaled3(p0, scWid, +sL * 0.18f), AddScaled3(p0, scDir, sL),
                    colScale, ts * 0.3f, phase, BotanicalPartTag::Wood);
        }

        const float s0 = std::clamp(fp.StipeFraction, 0.05f, 0.55f);
        for (uint32_t pi = 0u; pi < nPairs; ++pi)
        {
            for (int side : { -1, 1 })
            {
                const float uLeaf = std::clamp((static_cast<float>(pi) + 0.45f) / static_cast<float>(nPairs)
                                             + (side > 0 ? 0.16f : -0.16f) / static_cast<float>(nPairs), 0.01f, 0.985f);
                const float tR = s0 + (1.0f - s0) * uLeaf;
                const auto rf = SampleCurveFrames(frs, tR);
                const float env = bipinnate
                    ? std::max(0.06f, std::pow(std::sin(std::pow(uLeaf, 0.68f) * kPi), 0.78f))
                    : std::max(0.06f, (0.42f + 0.58f * SmoothStep(0.0f, 0.22f, uLeaf)) * std::pow(1.0f - std::pow(uLeaf, 1.65f), 0.68f));

                const float pLen = std::max(0.014f, fp.FrondWidthMeters * (bipinnate ? 0.56f : 0.52f) * env * (1.0f + rng.Vary(0.0f, 0.06f)));
                const float insRad = (fp.PinnaAngleDeg * (1.06f - 0.50f * uLeaf) + rng.Vary(0.0f, 3.0f)) * kDeg2Rad;
                const float dihRad = (12.0f - 14.0f * age01 + 6.0f * (1.0f - uLeaf)) * kDeg2Rad;
                V3 pDir = Norm3(Add3(Mul3(rf.Dir, std::cos(insRad)), Mul3(rf.Right, static_cast<float>(side) * std::sin(insRad))));
                pDir = Norm3(RotateAxis(pDir, rf.Dir, static_cast<float>(side) * dihRad));
                const V3 pWidAx = Norm3(Cross3(rf.Up, pDir));
                const V3 pNorm  = Norm3(Cross3(pDir, pWidAx));

                if (!bipinnate)
                {
                    const float pWid = pLen * std::clamp(sp.Leaves.BladeAspect, 0.18f, 0.45f);
                    EmitSculptedBladeAndSdf(out, rf.Pos, pDir, pWidAx, pNorm, pLen, pWid * 0.5f,
                        fp.LaminaCamber, sp.Leaves.CamberZ, fp.Auricle, fp.SerrationDepth, side,
                        frondCol, colYoung, std::pow(tR, 1.25f), phase + uLeaf * 0.37f, arch, 4);
                }
                else
                {
                    // Bipinnate lacy BrackenFern: secondary costa + paired delicate pinnules!
                    const int nSub = std::max(3, static_cast<int>(std::round(8.0f * (0.35f + 0.65f * env))));
                    for (int si = 0; si < nSub; ++si)
                    {
                        const float su = (static_cast<float>(si) + 0.4f) / static_cast<float>(nSub);
                        const V3 sPos = AddScaled3(rf.Pos, pDir, su * pLen);
                        const float sEnv = std::sin(kPi * std::pow(std::clamp(su, 0.05f, 0.96f), 0.70f));
                        const float subLen = std::max(0.012f, pLen * 0.32f * std::max(0.25f, sEnv));
                        const float subWid = subLen * 0.42f;
                        for (int sSide : { -1, 1 })
                        {
                            const V3 subDir = Norm3(Add3(Mul3(pDir, 0.58f), Mul3(pWidAx, static_cast<float>(sSide) * 0.81f)));
                            const V3 subW   = Norm3(Cross3(pNorm, subDir));
                            const V3 subN   = Norm3(Cross3(subDir, subW));
                            EmitSculptedBladeAndSdf(out, sPos, subDir, subW, subN, subLen, subWid * 0.5f,
                                fp.LaminaCamber, 0.18f, 0.0f, 0.15f, sSide,
                                frondCol, colYoung, std::pow(tR, 1.25f), phase + su * 0.25f, arch, 3);
                        }
                    }
                }
            }
        }
    }
}

//========================================================================================================================
//             2. EXACT PORT OF PALMMESHER.TS (+Z UP, WELDED GENUS-0 TRUNK + ROOTS + BOOTS + FRONDS + FRUITS)
//========================================================================================================================

void BuildPalmGeometry(const BotanicalSpeciesDefinition& sp, uint32_t seed, uint32_t tier, BotanicalPlantBuildResult& out)
{
    DetRng rng(seed ^ 0x9A1B5EEDu);
    const auto& pp  = sp.Palm;
    const auto& mat = sp.Materials;
    const V3 colBark      = mat.WoodColorLinear;
    const V3 colBarkDark  = Mix3(colBark, { 0.015f, 0.010f, 0.006f }, 0.52f);
    const V3 colWeathered = Mix3(colBark, { 0.38f, 0.34f, 0.28f }, 0.38f);
    const V3 colLeafBase  = mat.LeafBaseColorLinear;
    const V3 colLeafYoung = mat.LeafYoungColorLinear;
    const V3 colFruit     = mat.FruitColorLinear;
    const LeafSdfArchetype arch = ParseLeafArchetypeToken(sp.Leaves.LeafArchetypeSdf);
    const bool isDatePalm = (sp.Architecture == "date_palm");

    const float H  = std::max(2.5f, sp.Trunk.HeightMeters);
    const float R0 = std::max(0.10f, sp.Trunk.BaseRadiusMeters);
    const float topR = isDatePalm ? (R0 * 0.84f) : (R0 * 0.64f);
    const float totalEstH = H + pp.FrondLengthMeters * 0.65f;
    const uint32_t lobes = std::max(4u, pp.RootButtresses);
    const float lobePhase = static_cast<float>(seed % 360u) * kDeg2Rad;
    const float palmPhase = rng.Next01();

    WeldedMeshConfig trunkCfg {};
    trunkCfg.TrunkRadialSegments = (tier >= 2u) ? 24u : 18u;
    trunkCfg.MinRadialSegments   = 6u;
    trunkCfg.CollarScale         = 1.25f;
    trunkCfg.CollarLength        = 1.15f;
    trunkCfg.CollarRings         = 2u;
    trunkCfg.CollarFillet        = 0.78f;
    trunkCfg.CollarMitre         = 0.50f;
    trunkCfg.MinRadius           = 0.008f;
    trunkCfg.CapTips             = true;

    // 1. Build curved phototropic trunk WeldedStem in +Z Up
    const float leanAz = rng.Range(0.0f, kTau);
    const V3 leanHoriz { std::cos(leanAz), std::sin(leanAz), 0.0f };
    const float leanRad  = pp.TrunkLeanDeg * kDeg2Rad;
    const float curveRad = pp.TrunkCurveDeg * kDeg2Rad;
    V3 dir = Norm3(Add3(Mul3(kUpZ, std::cos(leanRad)), Mul3(leanHoriz, std::sin(leanRad))));
    V3 rgt = Norm3(Cross3(kUpZ, leanHoriz));
    V3 pos { 0.0f, 0.0f, 0.0f };

    auto trunk = std::make_shared<WeldedStem>();
    trunk->Id          = 1u;
    trunk->Level       = 0u;
    trunk->Role        = WeldedStemRole::Shoot;
    trunk->Attach      = WeldedAttachKind::Root;
    trunk->AttachDir   = dir;
    trunk->RingSpacing = std::clamp(R0 * 0.34f, 0.045f, 0.14f);

    const int nSteps = 24;
    const float stepLen = H / static_cast<float>(nSteps);
    float sAcc = 0.0f;
    trunk->Nodes.push_back({ pos, dir, rgt, 0.0f });
    for (int i = 1; i <= nSteps; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(nSteps);
        const float pitchUp = (curveRad / static_cast<float>(nSteps)) * (0.4f + 1.2f * t);
        const V3 rotAx = Cross3(dir, kUpZ);
        if (LenSq3(rotAx) > 1.0e-8f && std::fabs(pitchUp) > 1.0e-5f)
        {
            dir = Norm3(RotateAxis(dir, Norm3(rotAx), -pitchUp));
        }
        rgt = ProjectOnPlane3(rgt, dir);
        rgt = (LenSq3(rgt) < 1.0e-8f) ? AnyPerp3(dir) : Norm3(rgt);
        pos = AddScaled3(pos, dir, stepLen);
        sAcc += stepLen;
        trunk->Nodes.push_back({ pos, dir, rgt, sAcc });
    }
    trunk->Length = sAcc;

    // Extend trunk cleanly below Z = 0 so welded root collars sit flush with soil!
    const float rcBase = std::max(0.035f, R0 * 0.26f);
    const float stubDepth = rcBase * 2.4f + 0.08f;
    {
        const V3 d0 = Norm3(trunk->Nodes.front().Dir);
        const V3 r0v = trunk->Nodes.front().Right;
        const V3 baseP = AddScaled3(trunk->Nodes.front().Pos, d0, -stubDepth);
        for (auto& nd : trunk->Nodes) nd.S += stubDepth;
        trunk->Nodes.insert(trunk->Nodes.begin(), {
            { baseP, d0, r0v, 0.0f },
            { AddScaled3(baseP, d0, stubDepth * 0.5f), d0, r0v, stubDepth * 0.5f }
        });
        trunk->Length += stubDepth;
    }

    const float trunkPhysLen = trunk->Length;
    trunk->BaseRadiusAt = [=](float sVal) -> float
    {
        const float z = std::clamp((sVal - stubDepth) / std::max(1.0e-4f, trunkPhysLen - stubDepth), 0.0f, 1.0f);
        float r = R0 + (topR - R0) * std::pow(z, 0.88f);
        r *= (1.0f + pp.BasalBole * std::exp(-std::max(0.0f, z) * 6.8f));
        return std::max(trunkCfg.MinRadius, r);
    };
    trunk->ProfileFactor = [=](float sVal, float th) -> float
    {
        const float z = std::clamp((sVal - stubDepth) / std::max(1.0e-4f, trunkPhysLen - stubDepth), 0.0f, 1.0f);
        float mult = 1.0f;
        if (z < 0.14f && pp.RootButtresses > 0u)
        {
            const float fall = std::clamp(1.0f - z / 0.14f, 0.0f, 1.0f);
            mult *= 1.0f + 0.18f * fall * fall * (0.5f + 0.5f * std::cos(static_cast<float>(lobes) * th + lobePhase));
        }
        const float relief = std::clamp(pp.TrunkRelief, 0.0f, 1.0f);
        if (isDatePalm)
        {
            // Authentic Fibonacci 8 & 13 spiral diamond leaf-base boots!
            const float s1 = std::sin(8.0f * th + z * H * 11.5f);
            const float s2 = std::sin(5.0f * th - z * H * 11.5f);
            mult *= 1.0f + relief * 0.115f * std::pow(std::max(0.0f, s1 * s2), 0.65f);
        }
        else
        {
            // Crisp horizontal annular leaf-scar rings (Coconut Palm!)
            const float ring = 0.5f + 0.5f * std::cos(z * H * 18.5f + std::sin(th * 2.0f) * 0.15f);
            mult *= 1.0f + relief * 0.055f * std::pow(ring, 2.2f);
        }
        return mult;
    };
    trunk->StyleAt = [=](float sVal, float th, const Vec3f& p, const Vec3f&, bool) -> WeldedVertexStyle
    {
        const float z = std::clamp((sVal - stubDepth) / std::max(1.0e-4f, trunkPhysLen - stubDepth), 0.0f, 1.0f);
        const float h01 = std::clamp(p[2] / totalEstH, 0.0f, 1.0f);
        V3 bCol = colBark;
        if (isDatePalm)
        {
            const float s1 = std::sin(8.0f * th + z * H * 11.5f);
            const float s2 = std::sin(5.0f * th - z * H * 11.5f);
            const float diamond = std::max(0.0f, s1 * s2);
            bCol = Mix3(colBarkDark, Mix3(colBark, colWeathered, z * 0.45f), std::pow(diamond, 0.45f));
        }
        else
        {
            const float scar = std::pow(0.5f + 0.5f * std::cos(z * H * 18.5f), 2.4f);
            bCol = Mix3(colBark, colWeathered, scar * 0.65f);
            bCol = Mix3(colBarkDark, bCol, SmoothStep(0.0f, 0.14f, z) * 0.65f + 0.35f);
        }
        return { bCol, std::pow(h01, 1.65f) * 0.48f, palmPhase };
    };

    // 2. Attach WELDED basal root buttresses into trunk base (ONE closed quad manifold!)
    const uint32_t rootCount = std::min(lobes, trunkCfg.TrunkRadialSegments / 2u - 1u);
    for (uint32_t k = 0u; k < rootCount; ++k)
    {
        const float colStep = kTau / static_cast<float>(trunkCfg.TrunkRadialSegments);
        const uint32_t col = static_cast<uint32_t>(std::lround((static_cast<float>(k) / static_cast<float>(rootCount)) * static_cast<float>(trunkCfg.TrunkRadialSegments))) % trunkCfg.TrunkRadialSegments;
        const float theta = static_cast<float>(col + 1u) * colStep;
        const float attachS = stubDepth + rcBase * 0.55f;
        const auto smp = SampleWeldedStem(*trunk, attachS);
        const V3 up = Cross3(smp.Dir, smp.Right);
        const V3 radial = Norm3(Add3(Mul3(smp.Right, std::cos(theta)), Mul3(up, std::sin(theta))));
        const float rSurf = trunk->BaseRadiusAt(attachS) * trunk->ProfileFactor(attachS, theta);
        const V3 origin = AddScaled3(smp.Pos, radial, rSurf);
        const V3 horizAx = Norm3(Cross3(radial, kUpZ));
        const V3 emergeDir = Norm3(RotateAxis(radial, horizAx, -28.0f * kDeg2Rad));
        const float rootLen = R0 * 2.8f * rng.Vary(1.0f, 0.14f);
        const float rRoot0  = rcBase * rng.Vary(1.0f, 0.10f);

        auto rStem = std::make_shared<WeldedStem>();
        rStem->Id          = 10u + k;
        rStem->Level       = 4u;
        rStem->Role        = WeldedStemRole::Root;
        rStem->Parent      = trunk.get();
        rStem->Attach      = WeldedAttachKind::Side;
        rStem->AttachS     = attachS;
        rStem->AttachDir   = emergeDir;
        rStem->WindowCols  = 2u;
        rStem->MinRing     = 6u;
        rStem->RingSpacing = std::clamp(rRoot0 * 0.55f, 0.025f, 0.09f);
        rStem->BaseRadiusAt = [=](float sVal) -> float
        {
            const float z = std::clamp(sVal / std::max(1.0e-4f, rootLen), 0.0f, 1.0f);
            return std::max(trunkCfg.MinRadius, rRoot0 * std::pow(std::max(0.0f, 1.0f - z), 0.75f));
        };
        rStem->StyleAt = [=](float, float, const Vec3f&, const Vec3f&, bool) -> WeldedVertexStyle
        {
            return { Mix3(colBarkDark, colBark, 0.45f), 0.0f, 0.0f };
        };

        WeldedStem* rRaw = rStem.get();
        auto growRoot = [=](const V3& o, const V3& d)
        {
            rRaw->Nodes.clear();
            constexpr int steps = 9;
            V3 cur = o;
            V3 curD = Norm3(d);
            V3 curR = ProjectOnPlane3(Cross3(kUpZ, curD), curD);
            curR = (LenSq3(curR) < 1.0e-8f) ? AnyPerp3(curD) : Norm3(curR);
            rRaw->Nodes.push_back({ cur, curD, curR, 0.0f });
            float s = 0.0f;
            const float stp = rootLen / static_cast<float>(steps);
            const V3 hDir = Norm3({ curD[0], curD[1], 0.0f }, { 1.0f, 0.0f, 0.0f });
            for (int i = 1; i <= steps; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(steps);
                const float nx = cur[0] + hDir[0] * stp;
                const float ny = cur[1] + hDir[1] * stp;
                const float dive = SmoothStep(0.65f, 1.0f, t) * (rRoot0 * 2.0f + 0.08f);
                const float targetZ = rRoot0 * (1.0f - t) * 0.35f - dive;
                const float nz = (cur[2] + curD[2] * stp) * (1.0f - SmoothStep(0.08f, 0.40f, t) * 0.65f)
                               + targetZ * (SmoothStep(0.08f, 0.40f, t) * 0.65f);
                const V3 np { nx, ny, nz };
                curD = Norm3(Sub3(np, cur));
                curR = Norm3(ProjectOnPlane3(curR, curD));
                s += Len3(Sub3(np, cur));
                cur = np;
                rRaw->Nodes.push_back({ cur, curD, curR, s });
            }
            rRaw->Length = s;
        };
        rStem->Regrow = growRoot;
        growRoot(origin, emergeDir);
        trunk->Children.push_back(rStem);
    }

    // 3. Emit ONE single closed genus-0 quad manifold for the palm trunk + welded root buttresses!
    WeldedQuadMesh weldedTrunk {};
    WeldedMesher mesher(weldedTrunk, { trunk }, trunkCfg);
    out.WoodBuildStats     = mesher.Build();
    out.WoodManifoldReport = ValidateWeldedManifold(weldedTrunk);
    EmitWeldedQuadMeshToPlant(out, weldedTrunk);
    EmitWoodCapsulesFromWeldedHierarchy(out, *trunk, trunkCfg, totalEstH);

    // 4. Spiral Crown of Clasping Boots, Arching Pinnate Fronds, V-Ranked Leaflets & 3D Fruits!
    const auto endSmp = SampleWeldedStem(*trunk, trunk->Length);
    const V3 crownApex = endSmp.Pos;
    const float crownR = trunk->BaseRadiusAt(trunk->Length);
    const uint32_t nFronds = std::max(10u, pp.FrondCount);
    const uint32_t nPairs  = std::max(14u, (tier >= 2u) ? pp.LeafletPairs : (pp.LeafletPairs * 2u) / 3u);
    const uint32_t fSteps  = 12u;

    for (uint32_t fi = 0u; fi < nFronds; ++fi)
    {
        const float age01 = static_cast<float>(fi) / static_cast<float>(std::max(1u, nFronds - 1u));
        const float az    = static_cast<float>(fi) * kGolden + rng.Vary(0.0f, 0.12f);
        const V3 radDir { std::cos(az), std::sin(az), 0.0f };
        const V3 rgt0   = Norm3(Cross3(kUpZ, radDir));
        const float elevRad = (70.0f - 82.0f * age01 + rng.Vary(0.0f, 5.5f)) * kDeg2Rad;
        const float archRad = (pp.FrondArchDeg * (0.65f + 0.55f * age01)) * kDeg2Rad;
        const float fLen    = pp.FrondLengthMeters * (0.82f + 0.24f * std::sin(age01 * kPi * 0.8f + 0.2f));
        const float phase   = rng.Next01();

        V3 fCur = Add3(crownApex, Add3(Mul3(radDir, crownR * 0.62f), { 0.0f, 0.0f, 0.22f * (1.0f - age01) - 0.16f }));
        V3 fDir = Norm3({ radDir[0] * std::cos(elevRad), radDir[1] * std::cos(elevRad), std::sin(elevRad) });
        V3 fRgt = rgt0;

        std::vector<CurveFrame3> frs(fSteps + 1u);
        const float stepL = fLen / static_cast<float>(fSteps);
        for (uint32_t s = 0u; s <= fSteps; ++s)
        {
            const float t = static_cast<float>(s) / static_cast<float>(fSteps);
            frs[s] = { fCur, fDir, fRgt, Norm3(Cross3(fDir, fRgt)), t };
            if (s < fSteps)
            {
                const float dt = 1.0f / static_cast<float>(fSteps);
                fDir = Norm3(RotateAxis(fDir, fRgt, dt * archRad * (0.45f + 1.15f * t)));
                fCur = AddScaled3(fCur, fDir, stepL);
            }
        }
        const V3 frondCol = Mix3(colLeafYoung, colLeafBase, std::clamp(0.24f + 0.76f * age01, 0.0f, 1.0f));
        EmitGroovedRachis(out, frs, std::max(0.022f, crownR * 0.24f), 0.005f, 5u, colBark, frondCol, phase, 0.45f, 0.55f, true);

        for (uint32_t pi = 0u; pi < nPairs; ++pi)
        {
            const float u = 0.14f + 0.84f * (static_cast<float>(pi) / static_cast<float>(std::max(1u, nPairs - 1u)));
            const auto rf = SampleCurveFrames(frs, u);
            const float env = std::sin(kPi * std::pow(u, 0.72f));
            const float lLen = pp.FrondWidthMeters * 0.56f * std::max(0.18f, env);
            const float lWid = lLen * 0.13f;

            for (int side : { -1, 1 })
            {
                const float sweepRad = (56.0f - 28.0f * u) * kDeg2Rad;
                const float vTiltRad = (24.0f + pp.PlumoseSplay * rng.Vary(0.0f, 38.0f)) * kDeg2Rad;
                V3 lDir = Norm3(Add3(Mul3(rf.Dir, std::cos(sweepRad)), Mul3(rf.Right, static_cast<float>(side) * std::sin(sweepRad))));
                lDir = Norm3(RotateAxis(lDir, rf.Dir, static_cast<float>(side) * vTiltRad));
                const V3 lWidAx = Norm3(Cross3(rf.Up, lDir));
                const V3 lNorm  = Norm3(Cross3(lDir, lWidAx));
                EmitSculptedBladeAndSdf(out, rf.Pos, lDir, lWidAx, lNorm, lLen, lWid * 0.5f,
                    0.22f, 0.24f, 0.0f, 0.0f, side, frondCol, colLeafYoung, 0.55f + 0.45f * u, phase + u * 0.41f, arch, 3);
            }
        }
    }

    if (pp.ShowFruits && pp.FruitBunches > 0u)
    {
        const bool isCoconut = !isDatePalm;
        for (uint32_t b = 0u; b < pp.FruitBunches; ++b)
        {
            const float az = (static_cast<float>(b) / static_cast<float>(pp.FruitBunches)) * kTau + 0.35f;
            const V3 base = Add3(crownApex, { std::cos(az) * crownR * 1.15f, std::sin(az) * crownR * 1.15f, -0.34f });
            const int nFruits = isCoconut ? 5 : 16;
            for (int k = 0; k < nFruits; ++k)
            {
                const float th = static_cast<float>(k) * kGolden;
                const float rOff = isCoconut ? 0.15f : (0.10f + 0.14f * rng.Next01());
                const float zOff = -0.10f - (isCoconut ? 0.16f : 0.52f) * rng.Next01();
                const V3 fPos = Add3(base, { std::cos(th) * rOff, std::sin(th) * rOff, zOff });
                const float fr = isCoconut ? 0.105f : 0.038f;
                EmitOvoid(out, fPos, fr, fr, fr * 1.32f, colFruit, 0.48f, static_cast<float>(b) * 0.2f, BotanicalPartTag::Fruit);
            }
        }
    }
}

//========================================================================================================================
//             3. EXACT PORT OF TREEMESHER.TS (+Z UP, WEBER-PENN + WELDED GENUS-0 ROOTS/TRUNK/BRANCHES/FORKS!)
//========================================================================================================================

void BuildTreeGeometry(const BotanicalSpeciesDefinition& sp, uint32_t seed, uint32_t tier, BotanicalPlantBuildResult& out)
{
    DetRng rng(seed ^ 0x77EE5EEDu);
    DetRng leafRng(seed ^ 0x1EAFC0DEu);
    const auto& tp  = sp.Tree;
    const auto& mat = sp.Materials;
    const V3 colBark      = mat.WoodColorLinear;
    const V3 colBarkDeep  = Mix3(colBark, { 0.010f, 0.006f, 0.004f }, 0.48f);
    const V3 colBarkUpper = Mix3(colBark, { 0.21f, 0.16f, 0.11f }, 0.28f);
    const V3 colLeafBase  = mat.LeafBaseColorLinear;
    const V3 colLeafTip   = mat.LeafYoungColorLinear;
    const V3 colPetiole   = Mix3(colLeafBase, colBarkUpper, 0.45f);
    const LeafSdfArchetype arch = ParseLeafArchetypeToken(sp.Leaves.LeafArchetypeSdf);

    // Exact English Oak SpeciesProfile from treeMesher.ts
    constexpr float baseSize0 = 0.25f;
    constexpr float profLength[4]     = { 1.0f, 0.56f, 0.46f, 0.32f };
    constexpr float profTaper[4]      = { 0.96f, 1.0f, 1.0f, 1.0f };
    constexpr int   profCurveRes[4]   = { 9, 9, 6, 3 };
    constexpr float profCurve[4]      = { 0.0f, 24.0f, 10.0f, 0.0f };
    constexpr float profCurveBack[4]  = { 0.0f, -32.0f, 0.0f, 0.0f };
    constexpr float profCurveV[4]     = { 50.0f, 125.0f, 110.0f, 70.0f };
    constexpr float profBendV[4]      = { 18.0f, 82.0f, 55.0f, 0.0f };
    constexpr float profSplitAngle[4] = { 38.0f, 40.0f, 38.0f, 0.0f };
    const     float profDownAngle[4]  = { 0.0f, tp.BranchAngleDeg, 45.0f, 44.0f };
    constexpr float profDownAngleV[4] = { 0.0f, -28.0f, 18.0f, 16.0f };
    constexpr float profRotate[4]     = { 0.0f, 126.0f, 138.0f, 137.5f };
    const     float profBranches[4]   = { 1.0f, static_cast<float>(tp.LimbCount), 12.0f, 8.0f };
    constexpr int   kSubsteps[4]      = { 6, 4, 3, 2 };
    constexpr float kAttractionUp     = 0.34f;
    constexpr float kLeavesPerTwig    = 20.0f;
    constexpr float kMaxSideRatio     = 0.66f;
    constexpr float kMaxEmergeDeg     = 40.0f;
    constexpr float kMinForkDeg       = 16.0f;

    WeldedMeshConfig meshCfg {};
    meshCfg.TrunkRadialSegments = (tier >= 2u) ? 24u : 18u;
    meshCfg.MinRadialSegments   = (tier >= 2u) ? 6u : 4u;
    meshCfg.CollarScale         = 1.28f;
    meshCfg.CollarLength        = 1.18f;
    meshCfg.CollarRings         = 2u;
    meshCfg.CollarFillet        = std::clamp(tp.CollarFillet, 0.20f, 0.98f);
    meshCfg.CollarMitre         = 0.52f;
    meshCfg.MinRadius           = 0.006f;
    meshCfg.CapTips             = true;

    struct Turtle { V3 Pos; V3 Dir; V3 Right; };
    auto pitchDown = [](Turtle& t, float deg) { t.Dir = Norm3(RotateAxis(t.Dir, t.Right, -deg * kDeg2Rad)); };
    auto turnLeft  = [](Turtle& t, float deg) {
        const V3 ax = Norm3(Cross3(t.Dir, t.Right));
        t.Dir   = Norm3(RotateAxis(t.Dir, ax, -deg * kDeg2Rad));
        t.Right = Norm3(RotateAxis(t.Right, ax, -deg * kDeg2Rad));
    };
    auto rollRight = [](Turtle& t, float deg) { t.Right = Norm3(RotateAxis(t.Right, t.Dir, deg * kDeg2Rad)); };
    auto rotUp     = [](Turtle& t, float deg) {
        t.Dir   = Norm3(RotateAxis(t.Dir, kUpZ, deg * kDeg2Rad));
        t.Right = Norm3(RotateAxis(t.Right, kUpZ, deg * kDeg2Rad));
    };
    auto setDir = [](Turtle& t, const V3& nd) {
        const V3 ax = Cross3(t.Dir, nd);
        const float s = Len3(ax);
        const float c = std::clamp(Dot3(t.Dir, nd), -1.0f, 1.0f);
        if (s < 1.0e-7f)
        {
            t.Dir = nd;
            t.Right = Norm3(ProjectOnPlane3(t.Right, t.Dir), AnyPerp3(t.Dir));
            return;
        }
        const float ang = std::atan2(s, c);
        const V3 k = Mul3(ax, 1.0f / s);
        t.Dir   = Norm3(RotateAxis(t.Dir, k, ang));
        t.Right = Norm3(ProjectOnPlane3(RotateAxis(t.Right, k, ang), t.Dir), AnyPerp3(t.Dir));
    };

    struct ShootMeta
    {
        float LogicalLength  = 1.0f;
        float LogicalStart   = 0.0f;
        float LogicalRadius  = 0.1f;
        float RadiusFactor   = 1.0f;
        float Taper          = 0.96f;
        float LengthChildMax = 0.56f;
        float Phase          = 0.0f;
        float DistFromLimb   = 0.0f;
        float LimbReach      = 1.0f;
        uint32_t LimbRootId  = 0u;
    };
    struct LeafSpot
    {
        std::shared_ptr<WeldedStem> Stem {};
        float SLocal    = 0.0f;
        float RAngleDeg = 0.0f;
        float DAngleDeg = 42.0f;
        float LeafScale = 0.25f;
        float HueJitter = 0.0f;
    };

    uint32_t nextId = 1u;
    std::unordered_map<uint32_t, ShootMeta> metaMap {};
    std::vector<std::shared_ptr<WeldedStem>> allShoots {};
    std::vector<LeafSpot> leafSpots {};
    std::array<float, 5> splitErr { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    uint32_t lobes = std::max(3u, sp.Roots.Count);
    float lobePhase = static_cast<float>(seed % 360u) * kDeg2Rad;
    float treeHeightEst = std::max(4.0f, sp.Trunk.HeightMeters);
    const float basalFlare = std::max(0.25f, sp.Trunk.FlareRatio - 1.0f);
    const float buttressRelief = std::clamp(tp.ButtressRelief, 0.0f, 0.85f);
    const float forkTendency = std::clamp(tp.ForkTendency, 0.0f, 0.85f);

    auto evalShootRadius = [&](uint32_t stemId, uint32_t level, float s) -> float
    {
        const auto& m = metaMap[stemId];
        const float z = std::clamp(m.LogicalStart + s / std::max(1.0e-6f, m.LogicalLength), 0.0f, 1.0f);
        float r = m.LogicalRadius * (1.0f - std::clamp(m.Taper, 0.1f, 1.0f) * z * 0.94f);
        if (level == 0u)
        {
            const float yFlare = std::max(0.0f, 1.0f - 7.5f * std::max(0.0f, z));
            r *= 1.0f + basalFlare * ((std::pow(80.0f, yFlare) - 1.0f) / 80.0f);
        }
        r *= m.RadiusFactor;
        return std::max(meshCfg.MinRadius, r);
    };

    auto evalButtressProfile = [&](uint32_t stemId, uint32_t level, bool isRootParent, float s, float th) -> float
    {
        if (level != 0u || !isRootParent || lobes == 0u) return 1.0f;
        const auto& m = metaMap[stemId];
        const float lobeSpan = 0.14f * m.LogicalLength;
        const float sAbove = s + m.LogicalStart * m.LogicalLength;
        const float fall = std::clamp(1.0f - sAbove / std::max(1.0e-6f, lobeSpan), 0.0f, 1.0f);
        if (fall <= 0.0f) return 1.0f;
        const float wave = 0.5f + 0.5f * std::cos(static_cast<float>(lobes) * th + lobePhase);
        return 1.0f + buttressRelief * (fall * fall) * wave;
    };

    auto createShoot = [&](uint32_t level, WeldedStem* parent, WeldedAttachKind attach) -> std::shared_ptr<WeldedStem>
    {
        auto st = std::make_shared<WeldedStem>();
        st->Id     = nextId++;
        st->Level  = level;
        st->Role   = WeldedStemRole::Shoot;
        st->Parent = parent;
        st->Attach = attach;
        const uint32_t id = st->Id;
        const bool isRootTrunk = (parent == nullptr);

        ShootMeta m {};
        m.Taper      = profTaper[std::min(level, 3u)];
        m.Phase      = rng.Next01();
        m.LimbRootId = id;
        metaMap[id]  = m;

        st->BaseRadiusAt = [&, id, level](float s) { return evalShootRadius(id, level, s); };
        st->ProfileFactor = [&, id, level, isRootTrunk](float s, float th) { return evalButtressProfile(id, level, isRootTrunk, s, th); };
        st->StyleAt = [&, id, level](float s, float th, const Vec3f& p, const Vec3f&, bool) -> WeldedVertexStyle
        {
            const auto& mm = metaMap[id];
            const float fbm = EvalBarkFbm3d(p, sp.Trunk.BarkNoiseFreq * 0.25f);
            const float furrow = 0.84f + 0.16f * std::sin(th * (level == 0u ? 11.0f : 6.0f) + s * 4.5f + fbm * 2.8f);
            const V3 col = Mix3(Mix3(colBarkDeep, colBark, furrow), colBarkUpper, std::clamp(static_cast<float>(level) * 0.25f, 0.0f, 0.72f));
            const float h01 = std::clamp(p[2] / std::max(1.0f, treeHeightEst), 0.0f, 1.0f);
            const float flex = std::pow(h01, 1.55f) * 0.45f + (level > 0u ? 0.18f * std::clamp((mm.DistFromLimb + s) / std::max(0.5f, mm.LimbReach), 0.0f, 1.0f) : 0.0f);
            return { col, std::clamp(flex, 0.0f, 1.0f), mm.Phase };
        };
        allShoots.push_back(st);
        return st;
    };

    auto crownEnvelope = [](float ratio) -> float
    {
        return 0.25f + 0.75f * std::sin(0.5f * kPi * std::clamp(ratio, 0.0f, 1.0f));
    };

    const uint32_t maxLevels = std::clamp(tp.BranchLevels + 1u, 3u, 4u);

    auto recordLeaves = [&](const std::shared_ptr<WeldedStem>& st, int seg, int curveRes, float segLen, int startSeg, int count, float& prevRot)
    {
        const auto& m = metaMap[st->Id];
        const float startOff = static_cast<float>(startSeg) * segLen;
        for (int b = 0; b < count; ++b)
        {
            const float off = (static_cast<float>(b) + 0.5f) / static_cast<float>(std::max(1, count));
            const float stemOff = ((static_cast<float>(seg - 1) + off) / static_cast<float>(curveRes)) * m.LogicalLength;
            const float sLoc = stemOff - startOff;
            if (sLoc <= 0.0f || sLoc > st->Length) continue;
            prevRot = std::fmod(prevRot + 137.5f + leafRng.Uniform() * 18.0f + 360.0f, 360.0f);
            leafSpots.push_back({ st, sLoc, prevRot, 38.0f + leafRng.Uniform() * 22.0f,
                                  tp.LeafSizeMeters * leafRng.Range(1.55f, 2.25f), leafRng.Uniform() });
        }
    };

    std::function<void(Turtle&, const std::shared_ptr<WeldedStem>&, int, float, float, float, const V3*)> growShoot;
    growShoot = [&](Turtle& turtle, const std::shared_ptr<WeldedStem>& stem, int startSeg, float splitCorr,
                    float branchesFactor, float cloneProb, const V3* emergeTarget)
    {
        const uint32_t level = stem->Level;
        const uint32_t lp = std::min(level + 1u, 3u);
        const int curveRes = std::max(2, profCurveRes[std::min(level, 3u)]);
        const float logLen = metaMap[stem->Id].LogicalLength;
        const float segLen = logLen / static_cast<float>(curveRes);
        const int baseSegIdx = static_cast<int>(std::ceil(baseSize0 * static_cast<float>(curveRes)));
        const int substeps = kSubsteps[std::min(level, 3u)];
        const bool isLastLevel = (level >= maxLevels - 1u);

        constexpr float rps[4] = { 2.0f, 2.0f, 1.0f, 1.0f };
        stem->RingSpacing = std::max(0.035f, logLen / static_cast<float>(curveRes) / rps[std::min(level, 3u)]);

        const float segSplits = (level == 0u) ? (forkTendency * 0.65f) : ((level == 1u) ? (forkTendency * 0.72f) : 0.0f);
        float branchCount = 0.0f, leafCount = 0.0f;
        if (isLastLevel)
        {
            leafCount = kLeavesPerTwig * tp.LeafDensity * (1.0f - static_cast<float>(startSeg) / static_cast<float>(curveRes));
        }
        else
        {
            const float nBr = (level == 0u) ? profBranches[lp] : profBranches[lp] * (0.55f + 0.45f * std::clamp(logLen / 4.0f, 0.2f, 1.2f));
            branchCount = nBr * (1.0f - static_cast<float>(startSeg) / static_cast<float>(curveRes)) * branchesFactor;
        }
        float perSegBr = branchCount / static_cast<float>(curveRes);
        const float perSegLf = leafCount / static_cast<float>(curveRes);
        float brErr = 0.0f, lfErr = 0.0f;
        float prevRot = rng.Range(0.0f, 360.0f);

        stem->Nodes.push_back({ turtle.Pos, turtle.Dir, turtle.Right, 0.0f });
        float s = 0.0f, pendingPitch = 0.0f, pendingTurn = 0.0f;
        const float emergeAng = emergeTarget ? AngleBetween(turtle.Dir, *emergeTarget) : 0.0f;

        for (int seg = startSeg + 1; seg <= curveRes; ++seg)
        {
            const int remSegs = curveRes + 1 - seg;
            const float stepL = segLen / static_cast<float>(substeps);
            for (int k = 0; k < substeps; ++k)
            {
                if (pendingPitch != 0.0f) pitchDown(turtle, pendingPitch / static_cast<float>(substeps));
                if (pendingTurn  != 0.0f) turnLeft(turtle, pendingTurn / static_cast<float>(substeps));
                if (emergeTarget && emergeAng > 1.0e-4f && seg == startSeg + 1)
                {
                    setDir(turtle, RotateTowards(turtle.Dir, *emergeTarget, emergeAng / static_cast<float>(substeps)));
                }
                if (level >= 1u)
                {
                    const V3 hxt = Cross3(turtle.Dir, { 0.0f, 0.0f, kAttractionUp });
                    const float mag = Len3(hxt);
                    if (mag > 1.0e-6f)
                    {
                        const float alpha = (10.0f * mag * kDeg2Rad) / static_cast<float>(substeps);
                        const V3 ax = Mul3(hxt, 1.0f / mag);
                        turtle.Dir   = Norm3(RotateAxis(turtle.Dir, ax, alpha));
                        turtle.Right = Norm3(RotateAxis(turtle.Right, ax, alpha));
                    }
                }
                turtle.Pos = AddScaled3(turtle.Pos, turtle.Dir, stepL);
                s += stepL;
                stem->Nodes.push_back({ turtle.Pos, turtle.Dir, turtle.Right, s });
            }
            stem->Length = s;

            int numSplits = 0;
            if (seg < curveRes && segSplits > 0.0f && (level > 0u || seg > baseSegIdx))
            {
                if (rng.Next01() <= cloneProb)
                {
                    numSplits = static_cast<int>(std::floor(segSplits + splitErr[level]));
                    splitErr[level] -= static_cast<float>(numSplits) - segSplits;
                    if (numSplits > 0)
                    {
                        cloneProb /= static_cast<float>(numSplits + 1);
                        branchesFactor = std::max(0.78f, branchesFactor / static_cast<float>(numSplits + 1));
                        branchCount *= branchesFactor;
                        perSegBr = branchCount / static_cast<float>(curveRes);
                    }
                }
            }

            if (branchCount > 0.0f && !isLastLevel)
            {
                const int onSeg = static_cast<int>(std::floor(perSegBr + brErr));
                brErr -= static_cast<float>(onSeg) - perSegBr;
                if (onSeg > 0)
                {
                    const float baseLen = (level == 0u) ? (logLen * baseSize0) : (logLen * 0.05f);
                    const float startOff = static_cast<float>(startSeg) * segLen;
                    for (int b = 0; b < onSeg; ++b)
                    {
                        const float off = 0.10f + (static_cast<float>(b) / static_cast<float>(std::max(1, onSeg))) * 0.80f;
                        const float stemOff = ((static_cast<float>(seg - 1) + off) / static_cast<float>(curveRes)) * logLen;
                        if (stemOff <= baseLen) continue;
                        prevRot = std::fmod(prevRot + profRotate[lp] + rng.Uniform() * 24.0f + 360.0f, 360.0f);
                        const float sLocal = stemOff - startOff;
                        if (sLocal <= 0.0f || sLocal >= stem->Length) continue;

                        const auto smp = SampleWeldedStem(*stem, sLocal);
                        const V3 childRgt = Norm3(RotateAxis(smp.Right, smp.Dir, prevRot * kDeg2Rad));
                        const float d = profDownAngle[lp], dv = profDownAngleV[lp];
                        float dAng = d + rng.Uniform() * dv;
                        if (dv < 0.0f)
                        {
                            const float denom = std::max(1.0e-5f, logLen * (1.0f - (level == 0u ? baseSize0 : 0.04f)));
                            const float rat = std::clamp((logLen - stemOff) / denom, 0.0f, 1.0f);
                            dAng = d + dv * (1.0f - 2.0f * (0.2f + 0.8f * rat));
                        }
                        const V3 childDir = Norm3(RotateAxis(smp.Dir, childRgt, -dAng * kDeg2Rad));
                        const float rat = std::clamp((logLen - stemOff) / std::max(1.0e-5f, logLen - (level == 0u ? logLen * baseSize0 : 0.0f)), 0.0f, 1.0f);
                        const float cLen = (level == 0u)
                            ? (logLen * metaMap[stem->Id].LengthChildMax * crownEnvelope(rat))
                            : (metaMap[stem->Id].LengthChildMax * (logLen - 0.65f * stemOff));
                        if (cLen < meshCfg.MinRadius * 6.0f) continue;

                        const float rLim = stem->BaseRadiusAt(sLocal);
                        const float cRad = std::clamp(metaMap[stem->Id].LogicalRadius * std::pow(cLen / std::max(1.0e-5f, logLen), 1.28f),
                                                      meshCfg.MinRadius * 1.35f, rLim * kMaxSideRatio);

                        auto child = createShoot(level + 1u, stem.get(), WeldedAttachKind::Side);
                        auto& cm = metaMap[child->Id];
                        cm.LengthChildMax = profLength[std::min(level + 2u, 3u)] * rng.Vary(1.0f, 0.08f);
                        cm.LogicalLength  = cLen;
                        cm.LogicalRadius  = cRad;
                        cm.LimbRootId     = (level == 0u) ? child->Id : metaMap[stem->Id].LimbRootId;
                        cm.DistFromLimb   = (level == 0u) ? 0.0f : (metaMap[stem->Id].DistFromLimb + sLocal);

                        const float share = (cRad / std::max(1.0e-6f, rLim)) * (cRad / std::max(1.0e-6f, rLim));
                        child->RadiusDrop = std::clamp(std::sqrt(std::max(0.0f, 1.0f - tp.PipeConservation * share)), 0.56f, 1.0f);
                        child->AttachS    = sLocal;
                        child->AttachDir  = childDir;

                        V3 rad = ProjectOnPlane3(childDir, smp.Dir);
                        rad = (LenSq3(rad) < 1.0e-8f) ? childRgt : Norm3(rad);
                        const V3 cOrigin = AddScaled3(smp.Pos, rad, rLim);
                        const V3 emerge  = RotateTowards(rad, childDir, kMaxEmergeDeg * kDeg2Rad);
                        V3 cRgt = ProjectOnPlane3(childRgt, emerge);
                        cRgt = (LenSq3(cRgt) < 1.0e-8f) ? AnyPerp3(emerge) : Norm3(cRgt);
                        Turtle ct { cOrigin, emerge, cRgt };
                        stem->Children.push_back(child);
                        growShoot(ct, child, 0, 0.0f, 1.0f, 1.0f, &childDir);
                    }
                }
                if (level == maxLevels - 2u && seg >= static_cast<int>(std::ceil(static_cast<float>(curveRes) * 0.40f)))
                {
                    recordLeaves(stem, seg, curveRes, segLen, startSeg,
                        std::max(3, static_cast<int>(std::lround((kLeavesPerTwig * tp.LeafDensity) / static_cast<float>(curveRes) * 0.75f))), prevRot);
                }
            }
            else if (leafCount > 0.0f)
            {
                const int onSeg = static_cast<int>(std::floor(perSegLf + lfErr));
                lfErr -= static_cast<float>(onSeg) - perSegLf;
                if (onSeg > 0) recordLeaves(stem, seg, curveRes, segLen, startSeg, onSeg, prevRot);
            }

            if (numSplits > 0)
            {
                const float declDeg = std::acos(std::clamp(turtle.Dir[2], -1.0f, 1.0f)) * (180.0f / kPi);
                const float splAng  = std::max(14.0f, std::fabs(profSplitAngle[std::min(level, 3u)]) + rng.Uniform() * 8.0f - declDeg * 0.45f);
                const float nextSplitCorr = splAng / static_cast<float>(std::max(1, remSegs));
                const float sprAng  = -(24.0f + 0.65f * (28.0f + std::fabs(declDeg - 90.0f) * rng.Next01()));

                std::vector<Turtle> ts {};
                Turtle orig = turtle;
                pitchDown(orig, splAng * 0.5f);
                if (numSplits == 1) rotUp(orig, -sprAng * 0.5f);
                ts.push_back(orig);
                for (int i = 0; i < numSplits; ++i)
                {
                    Turtle t = turtle;
                    pitchDown(t, splAng * 0.5f);
                    rotUp(t, (i == 0) ? (sprAng * 0.5f) : (-sprAng * 0.5f));
                    ts.push_back(t);
                }
                const float minAng = kMinForkDeg * kDeg2Rad;
                for (int iter = 0; iter < 6; ++iter)
                {
                    for (std::size_t i = 0u; i < ts.size(); ++i)
                        for (std::size_t j = i + 1u; j < ts.size(); ++j)
                        {
                            const float ang = AngleBetween(ts[i].Dir, ts[j].Dir);
                            if (ang >= minAng) continue;
                            V3 ax = Cross3(ts[i].Dir, ts[j].Dir);
                            ax = (LenSq3(ax) < 1.0e-10f) ? AnyPerp3(ts[i].Dir) : Norm3(ax);
                            const float delta = (minAng - ang) * 0.5f + 1.0e-3f;
                            setDir(ts[i], Norm3(RotateAxis(ts[i].Dir, ax, -delta)));
                            setDir(ts[j], Norm3(RotateAxis(ts[j].Dir, ax, +delta)));
                        }
                }
                const float factor = std::pow(1.0f / static_cast<float>(ts.size()), 0.5f * std::clamp(tp.PipeConservation, 0.0f, 1.0f));
                const ShootMeta parentM = metaMap[stem->Id];
                for (auto& ft : ts)
                {
                    auto fc = createShoot(level, stem.get(), WeldedAttachKind::Fork);
                    auto& fm = metaMap[fc->Id];
                    fm.LogicalLength  = parentM.LogicalLength;
                    fm.LogicalStart   = static_cast<float>(seg) / static_cast<float>(curveRes);
                    fm.LogicalRadius  = parentM.LogicalRadius;
                    fm.RadiusFactor   = parentM.RadiusFactor * factor;
                    fm.Taper          = parentM.Taper;
                    fm.LengthChildMax = parentM.LengthChildMax;
                    fm.LimbRootId     = parentM.LimbRootId;
                    fm.DistFromLimb   = parentM.DistFromLimb + stem->Length;
                    fc->AttachDir     = ft.Dir;
                    stem->Forks.push_back(fc);
                    growShoot(ft, fc, seg, nextSplitCorr, branchesFactor, cloneProb, nullptr);
                }
                return;
            }

            if (seg < curveRes)
            {
                pendingTurn = (rng.Uniform() * profBendV[std::min(level, 3u)]) / static_cast<float>(curveRes);
                const float c0 = profCurve[std::min(level, 3u)], cb = profCurveBack[std::min(level, 3u)], cv = profCurveV[std::min(level, 3u)];
                const float baseA = (cb == 0.0f) ? (c0 / static_cast<float>(curveRes))
                    : ((seg < curveRes / 2) ? (c0 / (static_cast<float>(curveRes) * 0.5f)) : (cb / (static_cast<float>(curveRes) * 0.5f)));
                pendingPitch = baseA + (rng.Uniform() * cv) / static_cast<float>(curveRes) - splitCorr;
            }
        }
        stem->Length = s;
        if (!isLastLevel && stem->Forks.empty())
        {
            recordLeaves(stem, curveRes, curveRes, segLen, startSeg, std::max(4, static_cast<int>(std::lround(kLeavesPerTwig * tp.LeafDensity * 0.5f))), prevRot);
        }
    };

    // 1. Grow Weber-Penn trunk + limbs + branches + twigs + Y-forks
    Turtle rootTurtle { { 0.0f, 0.0f, 0.0f }, kUpZ, { 1.0f, 0.0f, 0.0f } };
    rollRight(rootTurtle, rng.Range(0.0f, 360.0f));
    auto trunk = createShoot(0u, nullptr, WeldedAttachKind::Root);
    metaMap[trunk->Id].LengthChildMax = profLength[1];
    metaMap[trunk->Id].LogicalLength  = std::max(3.5f, sp.Trunk.HeightMeters);
    metaMap[trunk->Id].LogicalRadius  = std::max(0.18f, sp.Trunk.BaseRadiusMeters);
    metaMap[trunk->Id].Taper          = profTaper[0];
    growShoot(rootTurtle, trunk, 0, 0.0f, 1.0f, 1.0f, nullptr);

    float maxZ = 1.0f;
    for (const auto& st : allShoots)
    {
        for (const auto& nd : st->Nodes) maxZ = std::max(maxZ, nd.Pos[2]);
        const auto& m = metaMap[st->Id];
        metaMap[m.LimbRootId].LimbReach = std::max(metaMap[m.LimbRootId].LimbReach, m.DistFromLimb + st->Length);
    }
    treeHeightEst = maxZ;

    // 2. Build WELDED terrain-following primary, forked & lateral surface roots merged cleanly into trunk base!
    auto makeRootStem = [&](WeldedStem* parent, float baseRad, float totalLen, uint32_t winCols) -> std::shared_ptr<WeldedStem>
    {
        auto rs = std::make_shared<WeldedStem>();
        rs->Id          = nextId++;
        rs->Level       = 4u;
        rs->Role        = WeldedStemRole::Root;
        rs->Parent      = parent;
        rs->Attach      = WeldedAttachKind::Side;
        rs->WindowCols  = winCols;
        rs->MinRing     = 8u;
        rs->RingSpacing = std::clamp(0.55f * baseRad, 0.025f, 0.18f);
        rs->BaseRadiusAt = [=, minR = meshCfg.MinRadius](float sVal) -> float
        {
            const float z = std::clamp(sVal / std::max(1.0e-5f, totalLen), 0.0f, 1.0f);
            return std::max(minR, baseRad * std::pow(std::max(0.0f, 1.0f - z), 0.78f));
        };
        rs->StyleAt = [=](float sVal, float th, const Vec3f& p, const Vec3f&, bool) -> WeldedVertexStyle
        {
            const float furrow = 0.84f + 0.16f * std::sin(th * 7.0f + sVal * 5.0f);
            const float moss = SmoothStep(0.35f, -0.05f, p[2]);
            const V3 bark = Mix3(colBarkDeep, colBark, furrow);
            return { Mix3(bark, { 0.04f, 0.08f, 0.02f }, moss * 0.22f), 0.0f, 0.0f };
        };
        return rs;
    };

    std::function<void(WeldedStem&, const V3&, const V3&, float, float, uint32_t, bool)> growTerrainRoot;
    growTerrainRoot = [&](WeldedStem& rs, const V3& origin, const V3& dir0, float r0Root, float totalLen, uint32_t rSeed, bool isPrimary)
    {
        DetRng lr(rSeed);
        const int nSteps = std::max(8, static_cast<int>(std::ceil(totalLen / std::clamp(0.24f * r0Root, 0.03f, 0.14f))));
        const float stepLen = totalLen / static_cast<float>(nSteps);
        V3 cur = origin;
        V3 dir = Norm3(dir0);
        V3 rgt = ProjectOnPlane3(Cross3(kUpZ, dir), dir);
        rgt = (LenSq3(rgt) < 1.0e-8f) ? AnyPerp3(dir) : Norm3(rgt);
        V3 horiz = Norm3({ dir[0], dir[1], 0.0f }, { 1.0f, 0.0f, 0.0f });
        const V3 goalHoriz = horiz;

        rs.Nodes.push_back({ cur, dir, rgt, 0.0f });
        float s = 0.0f;
        const bool willFork = isPrimary && (lr.Next01() < 0.48f);
        const int forkStep  = willFork ? static_cast<int>(std::floor(static_cast<float>(nSteps) * lr.Range(0.46f, 0.60f))) : -1;

        for (int i = 1; i <= nSteps; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(nSteps);
            const float rLoc = r0Root * std::pow(std::max(0.02f, 1.0f - t), 0.78f);
            const float wanderYaw = EvalBarkFbm3d({ cur[0] * 1.4f + static_cast<float>(rSeed & 255u) * 0.05f, cur[1] * 1.4f, 0.0f }, 1.0f) * 0.095f;
            horiz = Norm3(Mix3(Norm3(RotateAxis(horiz, kUpZ, wanderYaw)), goalHoriz, 0.16f));

            const float nx = cur[0] + horiz[0] * stepLen;
            const float ny = cur[1] + horiz[1] * stepLen;
            const float dive = SmoothStep(0.74f, 1.0f, t) * (r0Root * 2.2f + 0.12f);
            const float targetZ = rLoc * 0.36f - dive;
            const float blend = SmoothStep(0.06f, 0.34f, t) * 0.55f;
            const float nz = (cur[2] + dir[2] * stepLen) * (1.0f - blend) + targetZ * blend;
            const V3 np { nx, ny, nz };
            dir = Norm3(Sub3(np, cur));
            rgt = ProjectOnPlane3(rgt, dir);
            rgt = (LenSq3(rgt) < 1.0e-8f) ? AnyPerp3(dir) : Norm3(rgt);
            s += Len3(Sub3(np, cur));
            cur = np;
            rs.Nodes.push_back({ cur, dir, rgt, s });
            rs.Length = s;

            if (i == forkStep && nSteps - i >= 4)
            {
                const float remLen = totalLen - s;
                for (int side : { -1, 1 })
                {
                    const V3 fDir = Norm3(RotateAxis(dir, kUpZ, static_cast<float>(side) * lr.Range(20.0f, 32.0f) * kDeg2Rad));
                    auto fc = makeRootStem(&rs, rLoc * 0.76f, remLen, 0u);
                    fc->Attach = WeldedAttachKind::Fork;
                    fc->AttachDir = fDir;
                    growTerrainRoot(*fc, cur, fDir, rLoc * 0.76f, remLen, lr.NextU32(), false);
                    rs.Forks.push_back(fc);
                }
                break;
            }
        }
    };

    if (sp.Roots.Count > 0u)
    {
        DetRng rRng = rng.Fork(0x726F6F74u);
        const int32_t N = static_cast<int32_t>(meshCfg.TrunkRadialSegments);
        const float colStep = kTau / static_cast<float>(N);
        const float rNom = metaMap[trunk->Id].LogicalRadius * (1.0f + basalFlare * 0.45f);
        const int32_t maxSlots = std::min<int32_t>(static_cast<int32_t>(sp.Roots.Count), N / 2 - 1);

        struct RootSlot { float c; int32_t w; int32_t u; float radius; float len; uint32_t seed; float theta; };
        std::vector<RootSlot> slots {};
        for (int32_t k = 0; k < maxSlots; ++k)
        {
            const float th = (kTau * static_cast<float>(k)) / static_cast<float>(maxSlots) + rRng.Uniform() * 0.1f * (kTau / static_cast<float>(maxSlots));
            const float rad = std::max(meshCfg.MinRadius * 4.0f, rNom * 0.34f * rRng.Vary(1.0f, 0.12f));
            const float rLen = std::max(rad * 6.5f, sp.Roots.SpreadMeters * rRng.Vary(1.0f, 0.16f));
            float c = std::fmod(th / colStep, static_cast<float>(N));
            if (c < 0.0f) c += static_cast<float>(N);
            const float chord = (kTau * trunk->BaseRadiusAt(0.05f)) / static_cast<float>(N);
            const int32_t wEst = std::clamp<int32_t>(static_cast<int32_t>(std::lround((2.0f * rad * meshCfg.CollarScale) / std::max(1.0e-4f, chord))), 1, 3);
            slots.push_back({ c, wEst, 0, rad, rLen, rRng.NextU32(), th });
        }
        std::sort(slots.begin(), slots.end(), [](const RootSlot& a, const RootSlot& b) { return a.c < b.c; });
        int32_t need = 0;
        for (const auto& sl : slots) need += sl.w + 1;
        while (need > N)
        {
            auto it = std::max_element(slots.begin(), slots.end(), [](const RootSlot& a, const RootSlot& b) { return a.w < b.w; });
            if (it == slots.end() || it->w <= 1) break;
            --it->w; --need;
        }
        const int32_t K = static_cast<int32_t>(slots.size());
        std::vector<int32_t> uArr(static_cast<std::size_t>(K));
        for (int32_t i = 0; i < K; ++i) uArr[static_cast<std::size_t>(i)] = static_cast<int32_t>(std::lround(slots[static_cast<std::size_t>(i)].c - static_cast<float>(slots[static_cast<std::size_t>(i)].w) * 0.5f));
        for (int32_t i = 1; i < K; ++i) uArr[static_cast<std::size_t>(i)] = std::max(uArr[static_cast<std::size_t>(i)], uArr[static_cast<std::size_t>(i - 1)] + slots[static_cast<std::size_t>(i - 1)].w + 1);
        int32_t bound = uArr[0] + N;
        for (int32_t i = K - 1; i >= 1; --i) { uArr[static_cast<std::size_t>(i)] = std::min(uArr[static_cast<std::size_t>(i)], bound - slots[static_cast<std::size_t>(i)].w - 1); bound = uArr[static_cast<std::size_t>(i)]; }
        float sx = 0.0f, sy = 0.0f;
        for (int32_t i = 0; i < K; ++i)
        {
            int32_t v = uArr[static_cast<std::size_t>(i)] % N;
            if (v < 0) v += N;
            slots[static_cast<std::size_t>(i)].u = v;
            slots[static_cast<std::size_t>(i)].theta = (static_cast<float>(v) + static_cast<float>(slots[static_cast<std::size_t>(i)].w) * 0.5f) * colStep;
            sx += std::cos(static_cast<float>(lobes) * slots[static_cast<std::size_t>(i)].theta);
            sy += std::sin(static_cast<float>(lobes) * slots[static_cast<std::size_t>(i)].theta);
        }
        if (sx * sx + sy * sy > 1.0e-8f) lobePhase = -std::atan2(sy, sx);

        // Extend trunk cleanly below ground Z < 0 so ground-level root collars sit flush with soil!
        float maxRc = 0.08f;
        for (const auto& sl : slots) maxRc = std::max(maxRc, sl.radius);
        const float depth = std::min(trunk->Length * 0.25f, maxRc * 2.8f + 0.08f);
        {
            const V3 d0  = Norm3(trunk->Nodes.front().Dir);
            const V3 r0v = trunk->Nodes.front().Right;
            const V3 base = AddScaled3(trunk->Nodes.front().Pos, d0, -depth);
            for (auto& nd : trunk->Nodes) nd.S += depth;
            for (auto& c : trunk->Children) c->AttachS += depth;
            const int kSub = std::max(2, static_cast<int>(std::ceil(depth / std::max(0.05f, 0.35f * metaMap[trunk->Id].LogicalRadius))));
            std::vector<WeldedNode> stub {};
            for (int i = 0; i < kSub; ++i)
            {
                const float sStub = (static_cast<float>(i) / static_cast<float>(kSub)) * depth;
                stub.push_back({ AddScaled3(base, d0, sStub), d0, r0v, sStub });
            }
            trunk->Nodes.insert(trunk->Nodes.begin(), stub.begin(), stub.end());
            trunk->Length += depth;
            metaMap[trunk->Id].LogicalStart = -depth / metaMap[trunk->Id].LogicalLength;
            trunk->ExtraStations = {
                depth * 0.45f, depth,
                depth + 0.02f * metaMap[trunk->Id].LogicalLength,
                depth + 0.05f * metaMap[trunk->Id].LogicalLength,
                depth + 0.09f * metaMap[trunk->Id].LogicalLength,
                depth + 0.15f * metaMap[trunk->Id].LogicalLength
            };
        }

        for (const auto& sl : slots)
        {
            const float attachS = depth + sl.radius * 0.62f;
            const auto smp = SampleWeldedStem(*trunk, attachS);
            const V3 up = Cross3(smp.Dir, smp.Right);
            const V3 radial = Norm3(Add3(Mul3(smp.Right, std::cos(sl.theta)), Mul3(up, std::sin(sl.theta))));
            const float rSurf = trunk->BaseRadiusAt(attachS) * trunk->ProfileFactor(attachS, sl.theta);
            const V3 origin = AddScaled3(smp.Pos, radial, rSurf);
            const V3 horizAx = Norm3(Cross3(radial, kUpZ));
            const V3 emergeDir = Norm3(RotateAxis(radial, horizAx, -24.0f * kDeg2Rad));

            auto rootStem = makeRootStem(trunk.get(), sl.radius, sl.len, static_cast<uint32_t>(sl.w));
            rootStem->AttachS   = attachS;
            rootStem->AttachDir = emergeDir;
            WeldedStem* rRaw = rootStem.get();
            rootStem->Regrow = [&, rRaw, rRad = sl.radius, rLen = sl.len, rSeed = sl.seed](const V3& o, const V3& d)
            {
                rRaw->Children.clear(); rRaw->Forks.clear(); rRaw->Nodes.clear(); rRaw->Length = 0.0f;
                growTerrainRoot(*rRaw, o, d, rRad, rLen, rSeed, true);
            };
            rootStem->Regrow(origin, emergeDir);
            trunk->Children.push_back(rootStem);
        }
    }

    // 3. Emit ONE SINGLE CLOSED GENUS-0 QUAD MANIFOLD via WeldedMesher (V - E + F = 2, BoundaryEdges == 0, Components == 1!)
    WeldedQuadMesh weldedWood {};
    WeldedMesher mesher(weldedWood, { trunk }, meshCfg);
    out.WoodBuildStats     = mesher.Build();
    out.WoodManifoldReport = ValidateWeldedManifold(weldedWood);
    EmitWeldedQuadMeshToPlant(out, weldedWood);
    EmitWoodCapsulesFromWeldedHierarchy(out, *trunk, meshCfg, treeHeightEst);

    // 4. Emit sculpted 3D cupped & lobed oak leaves directly onto post-relocation twigs!
    auto isDroppedChain = [](const WeldedStem* st) -> bool
    {
        for (const WeldedStem* c = st; c != nullptr; c = c->Parent) if (c->Dropped) return true;
        return false;
    };

    if (tp.ShowLeaves)
    for (const auto& lp : leafSpots)
    {
        if (!lp.Stem || isDroppedChain(lp.Stem.get()) || lp.Stem->Nodes.empty()) continue;
        const float s = std::clamp(lp.SLocal, 0.0f, lp.Stem->Length);
        const auto smp = SampleWeldedStem(*lp.Stem, s);
        const V3 rgt = Norm3(RotateAxis(smp.Right, smp.Dir, lp.RAngleDeg * kDeg2Rad));
        const V3 dir = Norm3(RotateAxis(smp.Dir, rgt, -lp.DAngleDeg * kDeg2Rad));
        V3 rad = ProjectOnPlane3(dir, smp.Dir);
        rad = (LenSq3(rad) < 1.0e-8f) ? rgt : Norm3(rad);

        const float rTwig = lp.Stem->BaseRadiusAt(s);
        const V3 basePos = AddScaled3(smp.Pos, rad, rTwig * 0.88f);
        V3 leafRgt = Cross3(kUpZ, dir);
        leafRgt = (LenSq3(leafRgt) < 1.0e-6f) ? rgt : Norm3(leafRgt);
        const V3 normal = Norm3(Cross3(dir, leafRgt));

        const float h01 = std::clamp(basePos[2] / std::max(1.0f, treeHeightEst), 0.0f, 1.0f);
        const V3 leafTint = Mix3(colLeafBase, colLeafTip, std::clamp(0.45f + lp.HueJitter * 0.35f + h01 * 0.20f, 0.08f, 0.96f));
        const float L = lp.LeafScale;
        const float halfW = L * 0.74f * 0.5f;
        EmitSculptedBladeAndSdf(out, basePos, dir, leafRgt, normal, L, halfW,
            sp.Leaves.CamberX, sp.Leaves.CamberZ, 0.0f, 0.0f, 1,
            Mix3(colPetiole, colLeafBase, 0.65f), leafTint,
            std::clamp(0.35f + 0.65f * h01, 0.0f, 1.0f), metaMap[lp.Stem->Id].Phase, arch, 4);
    }
}

void PopulatePolygonNetwork(BotanicalPlantBuildResult& out)
{
    out.Network = {};
    out.Network.Vertices.reserve(out.Triangles.size() * 3u);
    out.Network.Faces.reserve(out.Triangles.size());
    out.Network.Edges.reserve(out.Triangles.size() * 3u);

    for (const auto& t : out.Triangles)
    {
        const uint32_t vBase = static_cast<uint32_t>(out.Network.Vertices.size());
        const uint32_t eBase = static_cast<uint32_t>(out.Network.Edges.size());
        const uint32_t fIdx  = static_cast<uint32_t>(out.Network.Faces.size());

        const std::array<V3, 3u> pts { t.V0, t.V1, t.V2 };
        const std::array<V3, 3u> nrm { t.N0, t.N1, t.N2 };
        for (uint32_t k = 0u; k < 3u; ++k)
        {
            Vertex v {};
            v.Axes = { pts[k][0], pts[k][1], pts[k][2] };
            v.Normal = { nrm[k][0], nrm[k][1], nrm[k][2] };
            v.OutgoingEdgeIndex = eBase + k;
            out.Network.Vertices.push_back(v);
        }
        for (uint32_t k = 0u; k < 3u; ++k)
        {
            DirectedEdge de {};
            de.OriginVertexIndex  = vBase + k;
            de.NextEdgeIndex      = eBase + ((k + 1u) % 3u);
            de.PreviousEdgeIndex  = eBase + ((k + 2u) % 3u);
            de.EnclosingFaceIndex = fIdx;
            out.Network.Edges.push_back(de);
        }
        Face f {};
        f.BoundaryLoopEdgeIndex   = eBase;
        f.GeometricNormal         = { t.Normal[0], t.Normal[1], t.Normal[2] };
        f.CornerCount             = 3u;
        f.PartGroupOrdinal        = static_cast<uint16_t>(t.Part);
        f.Shading.AlbedoLinearRGB = { t.ColorLin[0], t.ColorLin[1], t.ColorLin[2] };
        f.Shading.Roughness       = (t.Part == BotanicalPartTag::Wood) ? 0.82f : 0.44f;
        f.Shading.Metallic        = 0.0f;
        out.Network.Faces.push_back(f);
    }
}

} // namespace

BotanicalPlantBuildResult BuildBotanicalPlant(
    const BotanicalSpeciesDefinition& species,
    const LeafArchetypeSdfAtlas&      leafAtlas,
    const uint32_t                    seed,
    const uint32_t                    detailTier,
    const bool                        buildPolygonNetwork)
{
    BotanicalPlantBuildResult out {};
    out.SpeciesId  = species.SpeciesId;
    out.GrowthForm = species.GrowthForm;
    out.Seed       = seed;
    out.SdfVolume.BrickRes      = species.Sdf.CanopyBrickRes;
    out.SdfVolume.BarkNoiseAmp  = species.Sdf.UseNoisyCapsules ? species.Trunk.BarkNoiseAmp : 0.0f;
    out.SdfVolume.BarkNoiseFreq = species.Trunk.BarkNoiseFreq;

    if (species.GrowthForm == BotanicalGrowthForm::Fern)
    {
        BuildFernGeometry(species, seed, detailTier, out);
    }
    else if (species.GrowthForm == BotanicalGrowthForm::Palm)
    {
        BuildPalmGeometry(species, seed, detailTier, out);
    }
    else
    {
        BuildTreeGeometry(species, seed, detailTier, out);
    }

    out.BoundsMin = {  1.0e6f,  1.0e6f,  1.0e6f };
    out.BoundsMax = { -1.0e6f, -1.0e6f, -1.0e6f };
    for (const auto& t : out.Triangles)
    {
        for (const auto* p : { &t.V0, &t.V1, &t.V2 })
        {
            for (int k = 0; k < 3; ++k)
            {
                out.BoundsMin[static_cast<std::size_t>(k)] = std::min(out.BoundsMin[static_cast<std::size_t>(k)], (*p)[static_cast<std::size_t>(k)]);
                out.BoundsMax[static_cast<std::size_t>(k)] = std::max(out.BoundsMax[static_cast<std::size_t>(k)], (*p)[static_cast<std::size_t>(k)]);
            }
        }
    }

    const float spanH = std::max(0.5f, out.BoundsMax[2] - out.BoundsMin[2]);
    out.SdfVolume.BuildCanopyBricks(leafAtlas, std::min(0.65f, spanH * 0.12f));

    if (buildPolygonNetwork)
    {
        PopulatePolygonNetwork(out);
    }

    return out;
}

void ApplyWindPoseToPlant(
    BotanicalPlantBuildResult& plant,
    const float                bendAngleXRad,
    const float                bendAngleYRad,
    const float                gustTimeSec,
    const float                rustleAmp) noexcept
{
    auto bendPt = [&](const V3& rest, float flex, float phase) -> V3
    {
        if (rest[2] <= 0.0f || flex <= 1.0e-5f) return rest; // Keep sub-ground welded roots rock-solid!
        const float h = rest[2];
        const float rustle = rustleAmp * flex * std::sin(gustTimeSec * 6.5f + phase * kTau) * 0.04f;
        const float dx = (bendAngleXRad * h + rustle) * flex;
        const float dy = (bendAngleYRad * h + rustle * 0.7f) * flex;
        const float dz = -0.35f * (dx * dx + dy * dy) / std::max(h, 0.35f);
        return { rest[0] + dx, rest[1] + dy, std::max(0.0f, rest[2] + dz) };
    };

    for (auto& t : plant.Triangles)
    {
        t.V0 = bendPt(t.RestV0, t.WindFlex, t.Phase);
        t.V1 = bendPt(t.RestV1, t.WindFlex, t.Phase);
        t.V2 = bendPt(t.RestV2, t.WindFlex, t.Phase);
        t.Normal = Norm3(Cross3(Sub3(t.V1, t.V0), Sub3(t.V2, t.V0)), t.Normal);
    }

    plant.SdfVolume.ApplyWindBend(bendAngleXRad, bendAngleYRad, gustTimeSec, rustleAmp);
}

} // namespace Frontier::GeometrySpace::Botanical
