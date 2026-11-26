/*============================================================================================================================================
                                                     BOTANICALWELDEDMANIFOLD.CPP
============================================================================================================================================*/
// 🧩 Exact C++20 Implementation of Watertight Closed Genus-0 Quad Manifold Welding (`WeldedMesher` + `ValidateWeldedManifold`).

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Procedural/Botanical/BotanicalWeldedManifold.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace Frontier::GeometrySpace::Botanical
{

namespace
{

constexpr float kPi  = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;

[[nodiscard]] inline Vec3f Add(const Vec3f& a, const Vec3f& b) noexcept { return { a[0] + b[0], a[1] + b[1], a[2] + b[2] }; }
[[nodiscard]] inline Vec3f Sub(const Vec3f& a, const Vec3f& b) noexcept { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
[[nodiscard]] inline Vec3f Scale(const Vec3f& a, float s) noexcept      { return { a[0] * s, a[1] * s, a[2] * s }; }
[[nodiscard]] inline Vec3f AddScaled(const Vec3f& a, const Vec3f& b, float s) noexcept
{
    return { a[0] + b[0] * s, a[1] + b[1] * s, a[2] + b[2] * s };
}
[[nodiscard]] inline Vec3f Lerp(const Vec3f& a, const Vec3f& b, float t) noexcept
{
    return { a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t };
}
[[nodiscard]] inline float Dot(const Vec3f& a, const Vec3f& b) noexcept { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
[[nodiscard]] inline float LenSq(const Vec3f& a) noexcept { return Dot(a, a); }
[[nodiscard]] inline float Len(const Vec3f& a) noexcept   { return std::sqrt(LenSq(a)); }
[[nodiscard]] inline Vec3f Cross(const Vec3f& a, const Vec3f& b) noexcept
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}
[[nodiscard]] inline Vec3f Normalize(const Vec3f& a, const Vec3f& fb = { 0.0f, 0.0f, 1.0f }) noexcept
{
    const float l = Len(a);
    return (l > 1.0e-7f) ? Scale(a, 1.0f / l) : fb;
}
[[nodiscard]] inline Vec3f ProjectOnPlane(const Vec3f& v, const Vec3f& n) noexcept
{
    return Sub(v, Scale(n, Dot(v, n)));
}
[[nodiscard]] inline Vec3f AnyPerpendicular(const Vec3f& n) noexcept
{
    const Vec3f ref = (std::fabs(n[2]) < 0.85f) ? Vec3f{ 0.0f, 0.0f, 1.0f } : Vec3f{ 1.0f, 0.0f, 0.0f };
    return Normalize(Cross(ref, n));
}
[[nodiscard]] inline Vec3f RotateAxis(const Vec3f& v, const Vec3f& axis, float rad) noexcept
{
    const Vec3f k = Normalize(axis);
    const float c = std::cos(rad), s = std::sin(rad);
    return Add(Add(Scale(v, c), Scale(Cross(k, v), s)), Scale(k, Dot(k, v) * (1.0f - c)));
}
[[nodiscard]] inline Vec3f RotateTowards(const Vec3f& from, const Vec3f& to, float maxRad) noexcept
{
    const float d = std::clamp(Dot(from, to), -1.0f, 1.0f);
    const float ang = std::acos(d);
    if (ang <= 1.0e-6f || maxRad <= 0.0f) return from;
    if (ang <= maxRad) return to;
    Vec3f ax = Cross(from, to);
    if (LenSq(ax) < 1.0e-10f) ax = AnyPerpendicular(from);
    return Normalize(RotateAxis(from, ax, maxRad));
}
[[nodiscard]] inline int32_t ModInt(int32_t a, int32_t n) noexcept
{
    const int32_t r = a % n;
    return r < 0 ? r + n : r;
}
[[nodiscard]] inline float ModFloat(float a, float n) noexcept
{
    const float r = std::fmod(a, n);
    return r < 0.0f ? r + n : r;
}

[[nodiscard]] inline float CollarStart(const WeldedStem& stem, const WeldedMeshConfig& cfg) noexcept
{
    const float r0 = WeldedRadiusAt(stem, 0.0f, cfg);
    return std::min(cfg.CollarLength * r0, 0.5f * stem.Length);
}

[[nodiscard]] inline int32_t NearestIndex(const std::vector<float>& sorted, float v) noexcept
{
    int32_t lo = 0;
    int32_t hi = static_cast<int32_t>(sorted.size()) - 1;
    while (hi - lo > 1)
    {
        const int32_t mid = (lo + hi) >> 1;
        if (sorted[static_cast<std::size_t>(mid)] <= v) lo = mid;
        else hi = mid;
    }
    return (std::fabs(sorted[static_cast<std::size_t>(lo)] - v) <= std::fabs(sorted[static_cast<std::size_t>(hi)] - v)) ? lo : hi;
}

[[nodiscard]] inline bool CircularOverlap(int32_t a, int32_t wa, int32_t b, int32_t wb, int32_t nCols) noexcept
{
    if (wa >= nCols || wb >= nCols) return true;
    a = ModInt(a, nCols);
    b = ModInt(b, nCols);
    if (ModInt(b - a, nCols) < wa) return true;
    return ModInt(a - b, nCols) < wb;
}

} // namespace

uint32_t WeldedQuadMesh::AddVertex(const Vec3f& pos, const WeldedVertexStyle& style, const bool isJunction)
{
    const uint32_t idx = static_cast<uint32_t>(Positions.size());
    Positions.push_back(pos);
    Colors.push_back(style.ColorLinear);
    WindFlex.push_back(style.WindFlex);
    Phase.push_back(style.Phase);
    IsJunction.push_back(isJunction ? 1u : 0u);
    return idx;
}

void WeldedQuadMesh::AddQuad(const uint32_t a, const uint32_t b, const uint32_t c, const uint32_t d)
{
    Quads.push_back({ a, b, c, d });
}

void WeldedQuadMesh::AddTri(const uint32_t a, const uint32_t b, const uint32_t c)
{
    Tris.push_back({ a, b, c });
}

SampledStemFrame SampleWeldedStem(const WeldedStem& stem, const float s) noexcept
{
    if (stem.Nodes.empty()) return {};
    if (s <= 0.0f || stem.Nodes.size() == 1u)
    {
        return { stem.Nodes.front().Pos, stem.Nodes.front().Dir, stem.Nodes.front().Right };
    }
    const auto& last = stem.Nodes.back();
    if (s >= last.S) return { last.Pos, last.Dir, last.Right };

    std::size_t lo = 0u, hi = stem.Nodes.size() - 1u;
    while (hi - lo > 1u)
    {
        const std::size_t mid = (lo + hi) >> 1u;
        if (stem.Nodes[mid].S <= s) lo = mid;
        else hi = mid;
    }
    const auto& a = stem.Nodes[lo];
    const auto& b = stem.Nodes[hi];
    const float t = (s - a.S) / std::max(1.0e-9f, b.S - a.S);
    const Vec3f pos = Lerp(a.Pos, b.Pos, t);
    const Vec3f dir = Normalize(Lerp(a.Dir, b.Dir, t));
    Vec3f right = ProjectOnPlane(Lerp(a.Right, b.Right, t), dir);
    right = (LenSq(right) < 1.0e-10f) ? AnyPerpendicular(dir) : Normalize(right);
    return { pos, dir, right };
}

float WeldedPipeFactor(const WeldedStem& stem, const float s, const float collarScale) noexcept
{
    float f = 1.0f;
    for (const auto& c : stem.Children)
    {
        if (!c || c->Dropped || c->RadiusDrop >= 1.0f) continue;
        const float rc0 = c->BaseRadiusAt ? c->BaseRadiusAt(0.0f) : 0.01f;
        const float hh = std::max(1.0e-6f, rc0 * collarScale);
        const float t = std::clamp((s - (c->AttachS - hh)) / (2.0f * hh), 0.0f, 1.0f);
        if (t <= 0.0f) continue;
        const float k = t * t * (3.0f - 2.0f * t);
        f *= 1.0f + (c->RadiusDrop - 1.0f) * k;
    }
    return f;
}

float WeldedRadiusAt(const WeldedStem& stem, const float s, const WeldedMeshConfig& cfg) noexcept
{
    float r = stem.BaseRadiusAt ? stem.BaseRadiusAt(s) : cfg.MinRadius;
    if (!stem.Children.empty()) r *= WeldedPipeFactor(stem, s, cfg.CollarScale);
    return std::max(cfg.MinRadius, r);
}

void TransformWeldedSubtree(
    WeldedStem&                               stem,
    const std::function<Vec3f(const Vec3f&)>& mapPos,
    const std::function<Vec3f(const Vec3f&)>& mapDir)
{
    std::vector<WeldedStem*> stack { &stem };
    while (!stack.empty())
    {
        WeldedStem* s = stack.back();
        stack.pop_back();
        for (auto& n : s->Nodes)
        {
            n.Pos   = mapPos(n.Pos);
            n.Dir   = mapDir(n.Dir);
            n.Right = mapDir(n.Right);
        }
        s->AttachDir = mapDir(s->AttachDir);
        if (s->OnTransform) s->OnTransform(mapPos, mapDir);
        for (auto& c : s->Children) if (c) stack.push_back(c.get());
        for (auto& f : s->Forks)    if (f) stack.push_back(f.get());
    }
}

void TranslateWeldedSubtree(WeldedStem& stem, const Vec3f& delta)
{
    TransformWeldedSubtree(stem,
        [&](const Vec3f& p) { return Add(p, delta); },
        [](const Vec3f& d)  { return d; });
}

void RotateWeldedSubtree(WeldedStem& stem, const Vec3f& origin, const Vec3f& axis, const float angleRad)
{
    TransformWeldedSubtree(stem,
        [&](const Vec3f& p) { return Add(origin, RotateAxis(Sub(p, origin), axis, angleRad)); },
        [&](const Vec3f& d) { return Normalize(RotateAxis(d, axis, angleRad)); });
}

WeldedMesher::WeldedMesher(
    WeldedQuadMesh&                                 outMesh,
    const std::vector<std::shared_ptr<WeldedStem>>& roots,
    const WeldedMeshConfig&                         cfg)
    : Mesh_(outMesh), Roots_(roots), Cfg_(cfg)
{}

WeldedBuildStats WeldedMesher::Build()
{
    for (auto& root : Roots_)
    {
        if (!root) continue;
        BaseInfo base {};
        base.Kind = WeldedAttachKind::Root;
        MeshStem(*root, base, 0u);
    }
    return Stats_;
}

float WeldedMesher::SurfaceRadius(const WeldedStem& stem, const float s, const float theta) const noexcept
{
    const float base = WeldedRadiusAt(stem, s, Cfg_);
    return stem.ProfileFactor ? (base * stem.ProfileFactor(s, theta)) : base;
}

int32_t WeldedMesher::EmitVertex(const Vec3f& p, const WeldedStem& stem, const float s, const float theta, const Vec3f& dir, const bool isJunc)
{
    const WeldedVertexStyle st = stem.StyleAt ? stem.StyleAt(s, theta, p, dir, isJunc) : WeldedVertexStyle{};
    return static_cast<int32_t>(Mesh_.AddVertex(p, st, isJunc));
}

Vec3f WeldedMesher::PosAt(const int32_t vIdx) const noexcept
{
    return Mesh_.Positions[static_cast<std::size_t>(vIdx)];
}

void WeldedMesher::MarkJunction(const int32_t vIdx) noexcept
{
    if (vIdx >= 0 && static_cast<std::size_t>(vIdx) < Mesh_.IsJunction.size())
    {
        Mesh_.IsJunction[static_cast<std::size_t>(vIdx)] = 1u;
    }
}

void WeldedMesher::DropStem(WeldedStem& stem, const char* reason)
{
    stem.Dropped = true;
    stem.DropReason = reason ? reason : "dropped";
    ++Stats_.DroppedStems;
    if (stem.Role == WeldedStemRole::Root) ++Stats_.DroppedRoots;
}

void WeldedMesher::MeshStem(WeldedStem& stem, const BaseInfo& base, const uint32_t depth)
{
    ++Stats_.Stems;
    if (stem.Role == WeldedStemRole::Root) ++Stats_.RootStems;
    if (depth > Stats_.MaxDepth) Stats_.MaxDepth = depth;

    const int32_t nCols = (base.Kind == WeldedAttachKind::Root)
        ? std::max<int32_t>(4, static_cast<int32_t>(std::lround(static_cast<float>(stem.MinRing ? stem.MinRing : Cfg_.TrunkRadialSegments) * 0.5f)) * 2)
        : static_cast<int32_t>(base.Loop.size());
    const float L  = stem.Length;
    const float r0 = WeldedRadiusAt(stem, 0.0f, Cfg_);

    float sStart = 0.0f;
    if (base.Kind == WeldedAttachKind::Side) sStart = CollarStart(stem, Cfg_);
    else if (base.Kind == WeldedAttachKind::Fork) sStart = std::min(base.T1, 0.7f * L);

    bool hasTilt = false;
    Vec3f tilt { 0.0f, 0.0f, 1.0f };
    if (base.Kind == WeldedAttachKind::Side && !stem.Nodes.empty())
    {
        const Vec3f a0 = stem.Nodes.front().Dir;
        const float theta = std::acos(std::clamp(Dot(a0, base.Radial), -1.0f, 1.0f));
        const float dScale = std::clamp(sStart / std::max(1.0e-6f, Cfg_.CollarLength * r0), 0.0f, 1.0f);
        const float alpha = theta * std::clamp(Cfg_.CollarMitre, 0.0f, 0.9f) * dScale;
        if (alpha > 1.0e-3f)
        {
            tilt = RotateTowards(a0, base.Radial, alpha);
            hasTilt = true;
        }
    }

    float phase = 0.0f;
    if (base.Kind == WeldedAttachKind::Side || base.Kind == WeldedAttachKind::Fork)
    {
        const auto smp = SampleWeldedStem(stem, sStart);
        phase = FitPhase(base.Loop, smp.Pos, smp.Dir, smp.Right);
    }

    auto holes = PlanHoles(stem, nCols, phase, sStart, L);
    const auto stations = BuildStations(stem, holes, sStart, L, base.Kind);
    const int32_t K = static_cast<int32_t>(stations.size());

    std::vector<uint8_t> occupied(static_cast<std::size_t>(std::max<int32_t>(1, K - 1) * nCols), 0u);
    std::vector<HolePlan> accepted {};

    for (auto& h : holes)
    {
        int32_t row0 = NearestIndex(stations, h.SLow);
        int32_t row1 = NearestIndex(stations, h.SHigh);
        if (row1 - row0 < h.Rows) row1 = row0 + h.Rows;
        if (row1 > K - 1)
        {
            row1 = K - 1;
            row0 = std::max<int32_t>(0, row1 - h.Rows);
        }
        if (row1 <= row0 || row0 < 0)
        {
            DropStem(*h.Child, "no room on parent");
            continue;
        }
        bool free = true;
        for (int32_t i = row0; i < row1 && free; ++i)
        {
            for (int32_t j = 0; j < h.W; ++j)
            {
                if (occupied[static_cast<std::size_t>(i * nCols + ModInt(h.J0 + j, nCols))] != 0u) free = false;
            }
        }
        if (!free)
        {
            DropStem(*h.Child, "window conflict");
            continue;
        }
        for (int32_t i = row0; i < row1; ++i)
        {
            for (int32_t j = 0; j < h.W; ++j)
            {
                occupied[static_cast<std::size_t>(i * nCols + ModInt(h.J0 + j, nCols))] = 1u;
            }
        }
        h.Row0 = row0;
        h.Row1 = row1;
        accepted.push_back(h);
    }

    auto cellOcc = [&](int32_t i, int32_t j) -> bool
    {
        return i >= 0 && i < K - 1 && occupied[static_cast<std::size_t>(i * nCols + ModInt(j, nCols))] != 0u;
    };
    auto interior = [&](int32_t i, int32_t j) -> bool
    {
        return cellOcc(i - 1, j - 1) && cellOcc(i - 1, j) && cellOcc(i, j - 1) && cellOcc(i, j);
    };

    std::vector<Ring> rings {};
    rings.reserve(static_cast<std::size_t>(K));
    for (int32_t i = 0; i < K; ++i)
    {
        rings.push_back(BuildRing(stem, stations[static_cast<std::size_t>(i)], nCols, phase,
            (i == 0 && hasTilt) ? &tilt : nullptr,
            [&](int32_t j) { return interior(i, j); }));
    }

    // Base cap / Bezier collar / Fork D-ring bridge
    if (base.Kind == WeldedAttachKind::Root)
    {
        CapRing(rings.front().Idx, true);
    }
    else if (base.Kind == WeldedAttachKind::Side)
    {
        BuildCollar(stem, base.Loop, rings.front(), base.Radial, r0);
        ++Stats_.Junctions;
    }
    else if (base.Kind == WeldedAttachKind::Fork)
    {
        const Ring& first = rings.front();
        const int32_t M = static_cast<int32_t>(base.Loop.size());
        std::vector<int32_t> mid(static_cast<std::size_t>(M));
        const Vec3f forkPt   = stem.Nodes.front().Pos;
        const Vec3f chordMid = Lerp(forkPt, first.Center, 0.5f);
        for (int32_t k = 0; k < M; ++k)
        {
            const Vec3f a = PosAt(base.Loop[static_cast<std::size_t>(k)]);
            const Vec3f b = PosAt(first.Idx[static_cast<std::size_t>(k)]);
            Vec3f p = Lerp(a, b, 0.5f);
            Vec3f out = ProjectOnPlane(Sub(p, chordMid), first.Dir);
            if (LenSq(out) > 1.0e-12f)
            {
                p = AddScaled(p, Normalize(out), first.Radius * 0.10f);
            }
            mid[static_cast<std::size_t>(k)] = EmitVertex(p, stem, sStart * 0.5f, (kTau * static_cast<float>(k)) / static_cast<float>(M), first.Dir, true);
        }
        BridgeLoops(base.Loop, mid, true);
        BridgeLoops(mid, first.Idx, true);
    }

    // Tube quads (skipping open window cells)
    for (int32_t i = 0; i < K - 1; ++i)
    {
        const auto& a = rings[static_cast<std::size_t>(i)].Idx;
        const auto& b = rings[static_cast<std::size_t>(i + 1)].Idx;
        for (int32_t j = 0; j < nCols; ++j)
        {
            if (occupied[static_cast<std::size_t>(i * nCols + j)] != 0u) continue;
            const int32_t j1 = (j + 1) % nCols;
            Mesh_.AddQuad(static_cast<uint32_t>(a[static_cast<std::size_t>(j)]),
                          static_cast<uint32_t>(a[static_cast<std::size_t>(j1)]),
                          static_cast<uint32_t>(b[static_cast<std::size_t>(j1)]),
                          static_cast<uint32_t>(b[static_cast<std::size_t>(j)]));
        }
    }

    // End: fork or ladder cap
    if (!stem.Forks.empty() && K >= 1)
    {
        MeshFork(stem, rings.back(), depth);
    }
    else if (K >= 1)
    {
        CapRing(rings.back().Idx, false);
    }

    // Recurse into every accepted welded child root/branch
    for (const auto& h : accepted)
    {
        const auto loopIdx = HoleLoop(rings, h, nCols);
        const auto ps = SampleWeldedStem(stem, h.Child->AttachS);
        Vec3f radial = ProjectOnPlane(Sub(h.Child->Nodes.front().Pos, ps.Pos), ps.Dir);
        radial = (LenSq(radial) < 1.0e-12f) ? AnyPerpendicular(ps.Dir) : Normalize(radial);
        BaseInfo childBase {};
        childBase.Kind      = WeldedAttachKind::Side;
        childBase.Loop      = loopIdx;
        childBase.Radial    = radial;
        childBase.ParentDir = ps.Dir;
        MeshStem(*h.Child, childBase, depth + 1u);
    }
}

WeldedMesher::Ring WeldedMesher::BuildRing(
    const WeldedStem&                 stem,
    const float                       s,
    const int32_t                     nCols,
    const float                       phase,
    const Vec3f*                      tiltNormal,
    const std::function<bool(int32_t)>& skipFn)
{
    const auto smp = SampleWeldedStem(stem, s);
    const float r  = WeldedRadiusAt(stem, s, Cfg_);
    const Vec3f up = Cross(smp.Dir, smp.Right);
    std::vector<int32_t> idx(static_cast<std::size_t>(nCols), -1);
    const float an = tiltNormal ? Dot(smp.Dir, *tiltNormal) : 1.0f;

    for (int32_t j = 0; j < nCols; ++j)
    {
        if (skipFn && skipFn(j)) continue;
        const float th = phase + (kTau * static_cast<float>(j)) / static_cast<float>(nCols);
        const float rr = stem.ProfileFactor ? (r * stem.ProfileFactor(s, th)) : r;
        const float c = std::cos(th), sn = std::sin(th);
        Vec3f off {
            rr * (c * smp.Right[0] + sn * up[0]),
            rr * (c * smp.Right[1] + sn * up[1]),
            rr * (c * smp.Right[2] + sn * up[2])
        };
        if (tiltNormal && an > 0.3f)
        {
            const float lam = -Dot(off, *tiltNormal) / an;
            off = AddScaled(off, smp.Dir, lam);
        }
        idx[static_cast<std::size_t>(j)] = EmitVertex(Add(smp.Pos, off), stem, s, th, smp.Dir, false);
    }
    return { idx, s, smp.Pos, smp.Dir, r };
}

float WeldedMesher::FitPhase(const std::vector<int32_t>& loop, const Vec3f& center, const Vec3f& dir, const Vec3f& right) const noexcept
{
    const Vec3f up = Cross(dir, right);
    const int32_t M = static_cast<int32_t>(loop.size());
    std::vector<float> xs(static_cast<std::size_t>(M)), ys(static_cast<std::size_t>(M));
    float cx = 0.0f, cy = 0.0f;
    for (int32_t k = 0; k < M; ++k)
    {
        const Vec3f v = Sub(PosAt(loop[static_cast<std::size_t>(k)]), center);
        xs[static_cast<std::size_t>(k)] = Dot(v, right);
        ys[static_cast<std::size_t>(k)] = Dot(v, up);
        cx += xs[static_cast<std::size_t>(k)];
        cy += ys[static_cast<std::size_t>(k)];
    }
    cx /= static_cast<float>(M);
    cy /= static_cast<float>(M);
    float sx = 0.0f, sy = 0.0f;
    for (int32_t k = 0; k < M; ++k)
    {
        const float a = std::atan2(ys[static_cast<std::size_t>(k)] - cy, xs[static_cast<std::size_t>(k)] - cx)
                      - (kTau * static_cast<float>(k)) / static_cast<float>(M);
        sx += std::cos(a);
        sy += std::sin(a);
    }
    return std::atan2(sy, sx);
}

void WeldedMesher::BuildCollar(const WeldedStem& stem, const std::vector<int32_t>& loop, const Ring& first, const Vec3f& radial, const float r0)
{
    const int32_t n = static_cast<int32_t>(Cfg_.CollarRings);
    const int32_t M = static_cast<int32_t>(loop.size());
    const Vec3f a0  = stem.Nodes.front().Dir;
    const float cosT = std::max(0.2f, Dot(a0, radial));
    const float fillet = std::clamp(Cfg_.CollarFillet, 0.0f, 1.0f);

    std::vector<int32_t> prev = loop;
    for (int32_t q = 0; q < n; ++q)
    {
        const float k = static_cast<float>(q + 1) / static_cast<float>(n + 1);
        std::vector<int32_t> mid(static_cast<std::size_t>(M));
        for (int32_t i = 0; i < M; ++i)
        {
            const Vec3f a = PosAt(loop[static_cast<std::size_t>(i)]);
            const Vec3f b = PosAt(first.Idx[static_cast<std::size_t>(i)]);
            const Vec3f chord = Lerp(a, b, k);
            const float mu = std::clamp(Dot(Sub(b, a), radial) / cosT, 0.0f, 4.0f * r0);
            const Vec3f c = AddScaled(b, a0, -mu);
            const float w0 = (1.0f - k) * (1.0f - k);
            const float w1 = 2.0f * k * (1.0f - k);
            const float w2 = k * k;
            const Vec3f bez {
                a[0] * w0 + c[0] * w1 + b[0] * w2,
                a[1] * w0 + c[1] * w1 + b[1] * w2,
                a[2] * w0 + c[2] * w1 + b[2] * w2
            };
            const Vec3f p = Lerp(chord, bez, fillet);
            mid[static_cast<std::size_t>(i)] = EmitVertex(p, stem, first.S * k, (kTau * static_cast<float>(i)) / static_cast<float>(M), first.Dir, true);
        }
        BridgeLoops(prev, mid, true);
        prev = std::move(mid);
    }
    BridgeLoops(prev, first.Idx, true);
}

void WeldedMesher::BridgeLoops(const std::vector<int32_t>& a, const std::vector<int32_t>& b, const bool junction)
{
    const int32_t M = static_cast<int32_t>(a.size());
    for (int32_t k = 0; k < M; ++k)
    {
        const int32_t k1 = (k + 1) % M;
        Mesh_.AddQuad(static_cast<uint32_t>(a[static_cast<std::size_t>(k)]),
                      static_cast<uint32_t>(a[static_cast<std::size_t>(k1)]),
                      static_cast<uint32_t>(b[static_cast<std::size_t>(k1)]),
                      static_cast<uint32_t>(b[static_cast<std::size_t>(k)]));
        if (junction)
        {
            MarkJunction(a[static_cast<std::size_t>(k)]);
            MarkJunction(b[static_cast<std::size_t>(k)]);
        }
    }
}

void WeldedMesher::CapRing(const std::vector<int32_t>& ring, const bool flip)
{
    if (!Cfg_.CapTips && !flip) return;
    const int32_t M = static_cast<int32_t>(ring.size());
    std::vector<int32_t> r = ring;
    if (flip) std::reverse(r.begin(), r.end());
    for (int32_t i = 0; 2 * i <= M - 3; ++i)
    {
        const uint32_t a = static_cast<uint32_t>(r[static_cast<std::size_t>(i)]);
        const uint32_t b = static_cast<uint32_t>(r[static_cast<std::size_t>(i + 1)]);
        const uint32_t c = static_cast<uint32_t>(r[static_cast<std::size_t>(M - 2 - i)]);
        const uint32_t d = static_cast<uint32_t>(r[static_cast<std::size_t>(M - 1 - i)]);
        if (b == c) Mesh_.AddTri(a, b, d);
        else        Mesh_.AddQuad(a, b, c, d);
    }
}

std::vector<WeldedMesher::HolePlan> WeldedMesher::PlanHoles(WeldedStem& stem, const int32_t nCols, const float phase, const float sStart, const float len)
{
    std::vector<std::shared_ptr<WeldedStem>> kids {};
    for (const auto& c : stem.Children) if (c && !c->Dropped && !c->Nodes.empty()) kids.push_back(c);
    std::sort(kids.begin(), kids.end(), [&](const auto& a, const auto& b) {
        return WeldedRadiusAt(*b, 0.0f, Cfg_) < WeldedRadiusAt(*a, 0.0f, Cfg_);
    });

    const float colStep = kTau / static_cast<float>(nCols);
    const int32_t wMax  = std::max<int32_t>(1, nCols / 2 - 1);
    std::vector<HolePlan> cands {};

    for (const auto& child : kids)
    {
        const int32_t minRing = std::max<int32_t>(4, static_cast<int32_t>(std::lround(static_cast<float>(child->MinRing ? child->MinRing : Cfg_.MinRadialSegments) * 0.5f)) * 2);
        const float sAttach = child->AttachS;
        const auto smp = SampleWeldedStem(stem, sAttach);
        const float rp = WeldedRadiusAt(stem, sAttach, Cfg_);
        const float rc = WeldedRadiusAt(*child, 0.0f, Cfg_);
        const float rho = rc * Cfg_.CollarScale;
        const float chord = (kTau * rp) / static_cast<float>(nCols);

        const Vec3f a0 = child->Nodes.front().Dir;
        const float d0 = CollarStart(*child, Cfg_);
        const float dp = Dot(a0, smp.Dir);
        const float along = d0 * dp;
        const float sinT = std::sqrt(std::max(0.0f, 1.0f - dp * dp));
        const float extentHalf = rc * std::max(0.6f, 1.0f / std::max(0.35f, sinT)) * 0.5f + rc * 0.5f;

        int32_t w = child->WindowCols
            ? std::clamp<int32_t>(static_cast<int32_t>(child->WindowCols), 1, wMax)
            : std::clamp<int32_t>(static_cast<int32_t>(std::lround((2.0f * rho) / std::max(1.0e-5f, chord))), 1, wMax);
        const int32_t hMin = std::max<int32_t>(1, static_cast<int32_t>(std::ceil(static_cast<float>(minRing) * 0.5f - static_cast<float>(w))));
        if (hMin > 2 && w < wMax && !child->WindowCols)
        {
            w = std::min(wMax, std::max(w, static_cast<int32_t>(std::ceil(static_cast<float>(minRing) * 0.5f)) - 2));
        }
        const float W = static_cast<float>(w) * chord;
        const int32_t hRows = std::max<int32_t>(1, static_cast<int32_t>(std::ceil(static_cast<float>(minRing) * 0.5f - static_cast<float>(w))));
        const float hh = std::max({ rho, extentHalf * Cfg_.CollarScale, W * 0.5f, ((static_cast<float>(hRows) * W) / (2.0f * static_cast<float>(w))) * 0.9f });
        const float s = sAttach + along;
        const Vec3f up = Cross(smp.Dir, smp.Right);
        Vec3f radial = ProjectOnPlane(Sub(child->Nodes.front().Pos, smp.Pos), smp.Dir);
        if (LenSq(radial) < 1.0e-12f) radial = ProjectOnPlane(child->AttachDir, smp.Dir);
        if (LenSq(radial) < 1.0e-12f) radial = smp.Right;
        radial = Normalize(radial);
        const float phi = std::atan2(Dot(radial, up), Dot(radial, smp.Right)) - phase;
        const float jc  = phi / colStep;
        const int32_t j0 = static_cast<int32_t>(std::lround(jc - static_cast<float>(w) * 0.5f));
        cands.push_back({ child, s, s - hh, s + hh, j0, w, hh, hRows, 0, 0, along });
    }

    std::vector<HolePlan> accepted {};
    const float r0   = WeldedRadiusAt(stem, 0.0f, Cfg_);
    const float sMin = sStart + (stem.Attach == WeldedAttachKind::Side ? 0.45f * r0 : 1.0e-4f);
    constexpr float maxTurn = kPi * 0.25f;

    for (auto& c : cands)
    {
        const float eps = c.Hh * 0.6f;
        auto conflicts = [&](float sLow, float sHigh, int32_t j0) -> bool
        {
            for (const auto& a : accepted)
            {
                if (!(sLow < a.SHigh + eps && a.SLow < sHigh + eps)) continue;
                if (CircularOverlap(j0 - 1, c.W + 2, a.J0, a.W, nCols)) return true;
            }
            return false;
        };
        auto fits = [&](float sLow, float sHigh) -> bool
        {
            return sLow >= sMin + c.Hh * 0.15f && sHigh <= len + 1.0e-6f && (sLow + c.Hh - c.Along) > 1.0e-4f;
        };

        struct Opt { float ds; int32_t dj; float cost; };
        std::vector<Opt> options {};
        const int32_t maxDj = std::max<int32_t>(1, std::min<int32_t>(c.W + 1, static_cast<int32_t>(std::floor(maxTurn / colStep))));
        const bool isRoot = (c.Child->Role == WeldedStemRole::Root);
        for (int32_t dj = -maxDj; dj <= maxDj; ++dj)
        {
            for (int32_t ks = -6; ks <= 6; ++ks)
            {
                const float ds = static_cast<float>(ks) * c.Hh;
                const float cost = isRoot
                    ? ((ks < 0 ? static_cast<float>(-ks) * 2.0f : static_cast<float>(ks) * 6.0f) + static_cast<float>(std::abs(dj)))
                    : (static_cast<float>(std::abs(ks)) + static_cast<float>(std::abs(dj)) * 1.5f);
                options.push_back({ ds, dj, cost });
            }
        }
        std::sort(options.begin(), options.end(), [](const Opt& a, const Opt& b) { return a.cost < b.cost; });

        struct Attempt { bool narrow; bool anyDs; };
        const std::vector<Attempt> attempts = c.Child->WindowCols
            ? std::vector<Attempt>{ { false, false }, { true, false }, { false, true } }
            : std::vector<Attempt>{ { false, true }, { true, true } };

        bool found = false;
        Opt placed { 0.0f, 0, 0.0f };
        for (const auto& att : attempts)
        {
            if (found) break;
            if (att.narrow)
            {
                if (c.W <= 1) continue;
                c.W -= 1;
                c.Rows += 1;
            }
            for (const auto& o : options)
            {
                if (!att.anyDs && std::fabs(o.ds) > 1.0e-7f) continue;
                const float sLow = c.SLow + o.ds;
                const float sHigh = c.SHigh + o.ds;
                if (!fits(sLow, sHigh)) continue;
                if (conflicts(sLow, sHigh, c.J0 + o.dj)) continue;
                placed = o;
                found = true;
                break;
            }
        }
        if (!found)
        {
            DropStem(*c.Child, "window conflict");
            continue;
        }

        // Relocate / regrow child subtree so it exits dead-center through its assigned window!
        const auto oldSmp = SampleWeldedStem(stem, c.Child->AttachS);
        const float newS = c.S + placed.ds;
        const float newAttach = newS - c.Along;
        const int32_t newJ0 = c.J0 + placed.dj;
        const float newCenterCol = static_cast<float>(newJ0) + static_cast<float>(c.W) * 0.5f;
        const Vec3f oldUp = Cross(oldSmp.Dir, oldSmp.Right);
        Vec3f oldRadProj = ProjectOnPlane(Sub(c.Child->Nodes.front().Pos, oldSmp.Pos), oldSmp.Dir);
        if (LenSq(oldRadProj) < 1.0e-12f) oldRadProj = ProjectOnPlane(c.Child->AttachDir, oldSmp.Dir);
        if (LenSq(oldRadProj) < 1.0e-12f) oldRadProj = oldSmp.Right;
        const Vec3f oldRadial = Normalize(oldRadProj);
        const float oldAz = std::atan2(Dot(oldRadial, oldUp), Dot(oldRadial, oldSmp.Right));
        const float newAz = phase + newCenterCol * colStep;
        float dAz = newAz - oldAz;
        while (dAz > kPi)  dAz -= kTau;
        while (dAz < -kPi) dAz += kTau;

        const auto newSmp = SampleWeldedStem(stem, newAttach);
        const Vec3f newUp = Cross(newSmp.Dir, newSmp.Right);
        const float rpNew = SurfaceRadius(stem, newAttach, newAz);
        const Vec3f radialNew = Add(Scale(newSmp.Right, std::cos(newAz)), Scale(newUp, std::sin(newAz)));
        const Vec3f target = AddScaled(newSmp.Pos, radialNew, rpNew);

        if (c.Child->Regrow)
        {
            const Vec3f delta = Sub(target, c.Child->Nodes.front().Pos);
            if (std::fabs(dAz) > 1.0e-6f || LenSq(delta) > 1.0e-14f)
            {
                const Vec3f newDir = Normalize(RotateAxis(c.Child->AttachDir, oldSmp.Dir, dAz));
                c.Child->AttachDir = newDir;
                c.Child->AttachS = newAttach;
                c.Child->Regrow(target, newDir);
            }
        }
        else
        {
            if (std::fabs(dAz) > 1.0e-6f) RotateWeldedSubtree(*c.Child, oldSmp.Pos, oldSmp.Dir, dAz);
            const Vec3f delta = Sub(target, c.Child->Nodes.front().Pos);
            if (LenSq(delta) > 1.0e-14f) TranslateWeldedSubtree(*c.Child, delta);
        }

        c.Child->AttachS = newAttach;
        c.S = newS;
        c.SLow += placed.ds;
        c.SHigh += placed.ds;
        c.J0 = ModInt(newJ0, nCols);
        accepted.push_back(c);
    }
    return accepted;
}

std::vector<float> WeldedMesher::BuildStations(
    const WeldedStem&            stem,
    const std::vector<HolePlan>& holes,
    const float                  sStart,
    const float                  len,
    const WeldedAttachKind       kind) const
{
    const float spacing = (stem.RingSpacing > 0.0f) ? stem.RingSpacing : std::max(0.03f, len / 18.0f);
    if (len - sStart < 1.0e-6f) return { sStart };

    struct St { float s; int pri; };
    std::vector<St> mand { { sStart, 3 }, { len, 3 } };
    float minHole = std::numeric_limits<float>::infinity();

    for (const auto& h : holes)
    {
        mand.push_back({ h.SLow, 2 });
        mand.push_back({ h.SHigh, 2 });
        for (int32_t r = 1; r < h.Rows; ++r)
        {
            mand.push_back({ h.SLow + ((h.SHigh - h.SLow) * static_cast<float>(r)) / static_cast<float>(h.Rows), 2 });
        }
        minHole = std::min(minHole, (h.Hh * 2.0f) / static_cast<float>(h.Rows));
    }
    for (const float es : stem.ExtraStations)
    {
        if (es > sStart && es < len) mand.push_back({ es, 1 });
    }
    if (kind == WeldedAttachKind::Side && stem.Role != WeldedStemRole::Root)
    {
        const float emergeSpan = std::min(len * 0.35f, std::max(spacing * 3.0f, CollarStart(stem, Cfg_) * 2.5f));
        for (const auto& nd : stem.Nodes)
        {
            if (nd.S <= sStart + 1.0e-6f) continue;
            if (nd.S > emergeSpan + 1.0e-6f || nd.S >= len) break;
            mand.push_back({ nd.S, 1 });
        }
    }

    const float eps = std::min(spacing * 0.3f, std::isfinite(minHole) ? minHole * 0.3f : std::numeric_limits<float>::infinity());
    std::sort(mand.begin(), mand.end(), [](const St& a, const St& b) {
        return (std::fabs(a.s - b.s) > 1.0e-7f) ? (a.s < b.s) : (a.pri > b.pri);
    });
    std::vector<St> kept {};
    for (const auto& st : mand)
    {
        if (!kept.empty() && st.s - kept.back().s < eps)
        {
            if (st.pri > kept.back().pri) kept.back() = st;
            continue;
        }
        kept.push_back(st);
    }
    kept.back() = { len, 3 };
    if (kept.size() >= 2u && kept.back().s - kept[kept.size() - 2u].s < eps * 0.5f)
    {
        kept.erase(kept.end() - 2);
    }

    std::vector<float> out {};
    for (std::size_t i = 0u; i < kept.size(); ++i)
    {
        out.push_back(kept[i].s);
        if (i + 1u == kept.size()) break;
        const float gap = kept[i + 1u].s - kept[i].s;
        const int n = static_cast<int>(std::floor(gap / spacing));
        if (n >= 1)
        {
            const float step = gap / static_cast<float>(n + 1);
            for (int k = 1; k <= n; ++k) out.push_back(kept[i].s + static_cast<float>(k) * step);
        }
    }
    return out;
}

std::vector<int32_t> WeldedMesher::HoleLoop(const std::vector<Ring>& rings, const HolePlan& h, const int32_t nCols)
{
    std::vector<int32_t> loop {};
    for (int32_t j = 0; j <= h.W; ++j)
        loop.push_back(rings[static_cast<std::size_t>(h.Row0)].Idx[static_cast<std::size_t>(ModInt(h.J0 + j, nCols))]);
    for (int32_t i = h.Row0 + 1; i <= h.Row1; ++i)
        loop.push_back(rings[static_cast<std::size_t>(i)].Idx[static_cast<std::size_t>(ModInt(h.J0 + h.W, nCols))]);
    for (int32_t j = h.W - 1; j >= 0; --j)
        loop.push_back(rings[static_cast<std::size_t>(h.Row1)].Idx[static_cast<std::size_t>(ModInt(h.J0 + j, nCols))]);
    for (int32_t i = h.Row1 - 1; i >= h.Row0 + 1; --i)
        loop.push_back(rings[static_cast<std::size_t>(i)].Idx[static_cast<std::size_t>(ModInt(h.J0, nCols))]);
    for (const int32_t v : loop) MarkJunction(v);
    return loop;
}

void WeldedMesher::MeshFork(WeldedStem& stem, const Ring& endRing, const uint32_t depth)
{
    const int32_t nCols = static_cast<int32_t>(endRing.Idx.size());
    std::vector<std::shared_ptr<WeldedStem>> kids {};
    for (const auto& k : stem.Forks) if (k && !k->Dropped && !k->Nodes.empty()) kids.push_back(k);
    if (kids.empty())
    {
        CapRing(endRing.Idx, false);
        return;
    }
    ++Stats_.Forks;

    const Vec3f F = endRing.Center;
    const Vec3f dirP = endRing.Dir;
    const Vec3f right = !endRing.Idx.empty()
        ? Normalize(ProjectOnPlane(Sub(PosAt(endRing.Idx.front()), F), dirP))
        : AnyPerpendicular(dirP);
    const Vec3f up = Cross(dirP, right);
    const float colStep = kTau / static_cast<float>(nCols);

    Vec3f meanDir { 0.0f, 0.0f, 0.0f };
    for (const auto& k : kids) meanDir = Add(meanDir, k->Nodes.front().Dir);
    meanDir = (LenSq(meanDir) < 1.0e-10f) ? dirP : Normalize(meanDir);

    struct KidInfo { std::shared_ptr<WeldedStem> stem; float col; bool hasCol; float r; float theta; float t1; Vec3f proj; };
    std::vector<KidInfo> infos {};
    for (const auto& k : kids)
    {
        const Vec3f d = k->Nodes.front().Dir;
        Vec3f proj = ProjectOnPlane(d, dirP);
        const float r = WeldedRadiusAt(*k, 0.0f, Cfg_);
        const float theta = std::max(0.05f, std::acos(std::clamp(Dot(d, meanDir), -1.0f, 1.0f)));
        const float sep = std::sin(std::min(kPi * 0.5f, 2.0f * theta));
        const float t1 = std::clamp((2.0f * r) / std::max(0.05f, sep), 1.5f * r, 6.0f * r);
        const bool okProj = LenSq(proj) > 1.0e-10f;
        float col = 0.0f;
        if (okProj)
        {
            const float az = std::atan2(Dot(proj, up), Dot(proj, right));
            col = ModFloat(az / colStep, static_cast<float>(nCols));
        }
        infos.push_back({ k, col, okProj, r, theta, t1, proj });
    }
    for (auto& info : infos)
    {
        if (info.hasCol) continue;
        float sx = 0.0f, sy = 0.0f;
        for (const auto& o : infos)
        {
            if (!o.hasCol) continue;
            sx += std::cos(o.col * colStep);
            sy += std::sin(o.col * colStep);
        }
        info.col = ModFloat(std::atan2(-sy, -sx) / colStep, static_cast<float>(nCols));
    }
    std::sort(infos.begin(), infos.end(), [](const KidInfo& a, const KidInfo& b) { return a.col < b.col; });
    const int32_t kCount = static_cast<int32_t>(infos.size());

    if (kCount == 1)
    {
        BaseInfo childBase {};
        childBase.Kind = WeldedAttachKind::Fork;
        childBase.Loop = endRing.Idx;
        childBase.T1   = std::min(infos[0].t1, infos[0].stem->Length * 0.5f);
        MeshStem(*infos[0].stem, childBase, depth + 1u);
        return;
    }

    std::vector<int32_t> splits(static_cast<std::size_t>(kCount));
    for (int32_t i = 0; i < kCount; ++i)
    {
        const auto& a = infos[static_cast<std::size_t>(i)];
        const auto& b = infos[static_cast<std::size_t>((i + 1) % kCount)];
        float gap = b.col - a.col;
        if (i == kCount - 1) gap += static_cast<float>(nCols);
        const float t = a.r / (a.r + b.r);
        splits[static_cast<std::size_t>(i)] = ModInt(static_cast<int32_t>(std::lround(a.col + gap * t)), nCols);
    }
    for (int32_t iter = 0; iter < nCols; ++iter)
    {
        bool ok = true;
        for (int32_t i = 0; i < kCount; ++i)
        {
            const int32_t a = splits[static_cast<std::size_t>((i + kCount - 1) % kCount)];
            const int32_t b = splits[static_cast<std::size_t>(i)];
            if (ModInt(b - a, nCols) == 0)
            {
                splits[static_cast<std::size_t>(i)] = ModInt(b + 1, nCols);
                ok = false;
            }
        }
        if (ok) break;
    }

    std::vector<int32_t> arcLen(static_cast<std::size_t>(kCount));
    int32_t aMin = nCols;
    for (int32_t i = 0; i < kCount; ++i)
    {
        const int32_t a = splits[static_cast<std::size_t>((i + kCount - 1) % kCount)];
        const int32_t b = splits[static_cast<std::size_t>(i)];
        arcLen[static_cast<std::size_t>(i)] = ModInt(b - a, nCols);
        aMin = std::min(aMin, arcLen[static_cast<std::size_t>(i)]);
    }

    float hc = 0.0f;
    for (const auto& info : infos) hc += info.t1 * std::cos(info.theta);
    hc = std::clamp(hc / static_cast<float>(kCount), 0.5f * endRing.Radius, 4.0f * endRing.Radius);
    const Vec3f H = AddScaled(F, meanDir, hc);
    const int32_t m = std::max<int32_t>(0, (aMin + 1) / 2 - 1);
    const int32_t hub = EmitVertex(H, stem, stem.Length, 0.0f, meanDir, true);

    std::vector<std::vector<int32_t>> half(static_cast<std::size_t>(kCount));
    for (int32_t i = 0; i < kCount; ++i)
    {
        const Vec3f S = PosAt(endRing.Idx[static_cast<std::size_t>(splits[static_cast<std::size_t>(i)])]);
        const Vec3f outward = Normalize(ProjectOnPlane(Sub(S, F), meanDir));
        for (int32_t q = 0; q < m; ++q)
        {
            const float u = static_cast<float>(q + 1) / static_cast<float>(m + 1);
            Vec3f p = AddScaled(Lerp(S, H, u), outward, std::sin(kPi * u) * endRing.Radius * 0.12f);
            half[static_cast<std::size_t>(i)].push_back(
                EmitVertex(p, stem, stem.Length, (static_cast<float>(splits[static_cast<std::size_t>(i)]) / static_cast<float>(nCols)) * kTau, meanDir, true));
        }
    }
    for (const int32_t v : endRing.Idx) MarkJunction(v);

    for (int32_t i = 0; i < kCount; ++i)
    {
        const int32_t sA = splits[static_cast<std::size_t>((i + kCount - 1) % kCount)];
        std::vector<int32_t> ring {};
        for (int32_t j = 0; j <= arcLen[static_cast<std::size_t>(i)]; ++j)
            ring.push_back(endRing.Idx[static_cast<std::size_t>(ModInt(sA + j, nCols))]);
        for (const int32_t v : half[static_cast<std::size_t>(i)]) ring.push_back(v);
        ring.push_back(hub);
        const auto& back = half[static_cast<std::size_t>((i + kCount - 1) % kCount)];
        for (int32_t q = static_cast<int32_t>(back.size()) - 1; q >= 0; --q) ring.push_back(back[static_cast<std::size_t>(q)]);

        BaseInfo childBase {};
        childBase.Kind = WeldedAttachKind::Fork;
        childBase.Loop = std::move(ring);
        childBase.T1   = std::min(infos[static_cast<std::size_t>(i)].t1, infos[static_cast<std::size_t>(i)].stem->Length * 0.7f);
        MeshStem(*infos[static_cast<std::size_t>(i)].stem, childBase, depth + 1u);
    }
}

WeldedManifoldReport ValidateWeldedManifold(const WeldedQuadMesh& mesh)
{
    WeldedManifoldReport rep {};
    const uint32_t V = static_cast<uint32_t>(mesh.Positions.size());
    rep.Vertices = V;
    rep.Faces    = static_cast<uint32_t>(mesh.Quads.size() + mesh.Tris.size());

    std::unordered_map<uint64_t, uint32_t> edgeCount {};
    std::unordered_map<uint64_t, int32_t>  edgeDir   {};
    std::vector<uint8_t> used(V, 0u);
    std::vector<uint32_t> uf(V);
    for (uint32_t i = 0u; i < V; ++i) uf[i] = i;

    auto findRoot = [&](uint32_t x) -> uint32_t
    {
        while (uf[x] != x) { uf[x] = uf[uf[x]]; x = uf[x]; }
        return x;
    };
    auto unite = [&](uint32_t a, uint32_t b)
    {
        const uint32_t ra = findRoot(a), rb = findRoot(b);
        if (ra != rb) uf[ra] = rb;
    };
    auto edgeKey = [](uint32_t a, uint32_t b) -> uint64_t
    {
        const uint32_t lo = std::min(a, b), hi = std::max(a, b);
        return (static_cast<uint64_t>(lo) << 32u) | static_cast<uint64_t>(hi);
    };

    for (const auto& q : mesh.Quads)
    {
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j)
                if (q[static_cast<std::size_t>(i)] == q[static_cast<std::size_t>(j)]) ++rep.DegenerateFaces;

        for (int i = 0; i < 4; ++i)
        {
            const uint32_t a = q[static_cast<std::size_t>(i)];
            const uint32_t b = q[static_cast<std::size_t>((i + 1) % 4)];
            used[a] = 1u;
            unite(a, b);
            const uint64_t k = edgeKey(a, b);
            edgeCount[k] += 1u;
            edgeDir[k]   += (a < b) ? 1 : -1;
        }
    }

    for (const auto& t : mesh.Tris)
    {
        if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2]) ++rep.DegenerateFaces;
        for (int i = 0; i < 3; ++i)
        {
            const uint32_t a = t[static_cast<std::size_t>(i)];
            const uint32_t b = t[static_cast<std::size_t>((i + 1) % 3)];
            used[a] = 1u;
            unite(a, b);
            const uint64_t k = edgeKey(a, b);
            edgeCount[k] += 1u;
            edgeDir[k]   += (a < b) ? 1 : -1;
        }
    }

    for (const auto& [k, c] : edgeCount)
    {
        if (c == 1u) ++rep.BoundaryEdges;
        else if (c > 2u) ++rep.NonManifoldEdges;
        else if (edgeDir[k] != 0) ++rep.InconsistentEdges;
    }

    std::unordered_set<uint32_t> compRoots {};
    for (uint32_t i = 0u; i < V; ++i)
    {
        if (used[i] == 0u) ++rep.IsolatedVertices;
        else compRoots.insert(findRoot(i));
    }

    rep.Edges               = static_cast<uint32_t>(edgeCount.size());
    rep.Components          = static_cast<uint32_t>(compRoots.size());
    rep.EulerCharacteristic = static_cast<int32_t>(V - rep.IsolatedVertices) - static_cast<int32_t>(rep.Edges) + static_cast<int32_t>(rep.Faces);
    rep.Closed              = (rep.BoundaryEdges == 0u);
    rep.Manifold            = (rep.NonManifoldEdges == 0u && rep.InconsistentEdges == 0u && rep.DegenerateFaces == 0u);
    return rep;
}

} // namespace Frontier::GeometrySpace::Botanical
