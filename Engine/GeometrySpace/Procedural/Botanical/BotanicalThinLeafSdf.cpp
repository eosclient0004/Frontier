/*============================================================================================================================================
                                                        BOTANICALTHINLEAFSDF.CPP
============================================================================================================================================*/
// 🧩 Implementation of exact 64x64 2D Felzenszwalb-Huttenlocher EDT Leaf Archetype SDF Atlas, Watertight Noisy Wood Capsules,
//    16^3 Canopy Brick Accelerator, and 2.5D Curved Thin-Sheet Shadow Raytracer with Automatic Cone-Width Beer-Lambert LOD.

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Procedural/Botanical/BotanicalThinLeafSdf.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Frontier::GeometrySpace::Botanical
{

namespace
{

using V3 = std::array<float, 3>;
constexpr float kPi = 3.14159265358979323846f;

[[nodiscard]] inline V3 Add3(const V3& a, const V3& b) noexcept { return { a[0] + b[0], a[1] + b[1], a[2] + b[2] }; }
[[nodiscard]] inline V3 Sub3(const V3& a, const V3& b) noexcept { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
[[nodiscard]] inline V3 Mul3(const V3& a, float s) noexcept     { return { a[0] * s, a[1] * s, a[2] * s }; }
[[nodiscard]] inline float Dot3(const V3& a, const V3& b) noexcept { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
[[nodiscard]] inline float Len3(const V3& a) noexcept { return std::sqrt(Dot3(a, a)); }

[[nodiscard]] inline float Fract(float x) noexcept { return x - std::floor(x); }

[[nodiscard]] inline float HashLattice3d(float ix, float iy, float iz) noexcept
{
    const float wx = ix - std::floor(ix * (1.0f / 289.0f)) * 289.0f;
    const float wy = iy - std::floor(iy * (1.0f / 289.0f)) * 289.0f;
    const float wz = iz - std::floor(iz * (1.0f / 289.0f)) * 289.0f;
    float px = Fract(wx * 0.1031f);
    float py = Fract(wy * 0.1030f);
    float pz = Fract(wz * 0.0973f);
    const float d = px * (py + 19.19f) + py * (pz + 19.19f) + pz * (px + 19.19f);
    px += d; py += d; pz += d;
    return Fract((px + py) * pz);
}

[[nodiscard]] inline float EvalSmoothNoise3d(float qx, float qy, float qz) noexcept
{
    const float ix = std::floor(qx), iy = std::floor(qy), iz = std::floor(qz);
    const float fx = qx - ix, fy = qy - iy, fz = qz - iz;
    const float ux = fx * fx * (3.0f - 2.0f * fx);
    const float uy = fy * fy * (3.0f - 2.0f * fy);
    const float uz = fz * fz * (3.0f - 2.0f * fz);

    const float n000 = HashLattice3d(ix,        iy,        iz);
    const float n100 = HashLattice3d(ix + 1.0f, iy,        iz);
    const float n010 = HashLattice3d(ix,        iy + 1.0f, iz);
    const float n110 = HashLattice3d(ix + 1.0f, iy + 1.0f, iz);
    const float n001 = HashLattice3d(ix,        iy,        iz + 1.0f);
    const float n101 = HashLattice3d(ix + 1.0f, iy,        iz + 1.0f);
    const float n011 = HashLattice3d(ix,        iy + 1.0f, iz + 1.0f);
    const float n111 = HashLattice3d(ix + 1.0f, iy + 1.0f, iz + 1.0f);

    const float nx00 = n000 + (n100 - n000) * ux;
    const float nx10 = n010 + (n110 - n010) * ux;
    const float nx01 = n001 + (n101 - n001) * ux;
    const float nx11 = n011 + (n111 - n011) * ux;
    const float nxy0 = nx00 + (nx10 - nx00) * uy;
    const float nxy1 = nx01 + (nx11 - nx01) * uy;
    return nxy0 + (nxy1 - nxy0) * uz;
}

/// Evaluates boolean silhouette occupancy on local [-1, +1]^2 coordinates (X = width, Z = length)
[[nodiscard]] bool TestLeafArchetypeInside(LeafSdfArchetype arch, float x, float z) noexcept
{
    const float ax = std::fabs(x);

    switch (arch)
    {
        case LeafSdfArchetype::OvateBeech:
        {
            if (z >= -0.96f && z <= -0.56f && ax <= 0.034f) return true;
            const float t = (z + 0.64f) / 1.56f;
            if (t < 0.0f || t > 1.0f) return false;
            const float env   = 0.62f * std::sin(kPi * std::pow(t, 0.78f)) * (1.0f - 0.18f * t);
            const float teeth = 0.042f * std::sin(t * kPi * 13.0f) * std::sin(kPi * t);
            return ax <= std::max(0.0f, env + teeth);
        }
        case LeafSdfArchetype::HeartLinden:
        {
            if (z >= -0.96f && z <= -0.56f && ax <= 0.034f) return true;
            const float dz = z + 0.22f;
            const float r  = std::hypot(ax * 1.12f, dz);
            if (r < 1.0e-5f) return true;
            const float th = std::atan2(ax * 1.12f, dz);
            const float hr = 0.72f * (1.0f + 0.26f * std::cos(th) - 0.42f * std::pow(std::sin(0.5f * th), 3.0f))
                           + 0.055f * std::exp(-std::pow(th / 0.24f, 2.0f));
            const float teeth = 0.024f * std::sin(th * 18.0f) * std::sin(th);
            return r <= hr + teeth && z <= 0.94f && z >= -0.78f;
        }
        case LeafSdfArchetype::LobedOak:
        {
            if (z >= -0.96f && z <= -0.56f && ax <= 0.036f) return true;
            const float t = (z + 0.66f) / 1.58f;
            if (t < 0.0f || t > 1.0f) return false;
            const float taper = std::sin(kPi * std::pow(t, 0.72f));
            const float lobes = 0.56f + 0.44f * std::cos((t - 0.14f) * kPi * 7.2f);
            return ax <= std::max(0.03f * (1.0f - t), 0.68f * taper * lobes);
        }
        case LeafSdfArchetype::PalmateMaple:
        {
            if (z >= -0.96f && z <= -0.56f && ax <= 0.034f) return true;
            const float oz = z + 0.42f;
            const float r  = std::hypot(ax, oz);
            if (r > 1.38f || z < -0.76f) return false;
            const float ang = std::atan2(ax, oz);
            constexpr float la[3] = { 0.0f, 0.72f, 1.46f };
            constexpr float ll[3] = { 1.28f, 1.08f, 0.70f };
            constexpr float ls[3] = { 0.27f, 0.25f, 0.26f };
            float maxR = 0.22f;
            for (int l = 0; l < 3; ++l)
            {
                const float da = (ang - la[l]) / ls[l];
                const float reach = ll[l] * std::exp(-0.55f * da * da) * (1.0f + 0.07f * std::cos(da * 5.2f));
                maxR = std::max(maxR, reach);
            }
            return r <= maxR;
        }
        case LeafSdfArchetype::SlenderWillow:
        {
            if (z >= -0.96f && z <= -0.74f && std::fabs(x + 0.02f) <= 0.025f) return true;
            const float t = (z + 0.78f) / 1.72f;
            if (t < 0.0f || t > 1.0f) return false;
            const float curve = 0.07f * std::sin(t * kPi * 1.15f);
            return std::fabs(x - curve) <= 0.22f * std::sin(kPi * std::pow(t, 0.80f));
        }
        case LeafSdfArchetype::FernPinna:
        {
            // Asymmetric sword-fern pinna with acroscopic basal auricle lobe + fine marginal serrations!
            const float t = (z + 0.90f) / 1.82f;
            if (t < 0.0f || t > 1.0f) return false;
            const float baseW   = 0.34f * std::pow(1.0f - std::pow(t, 1.35f), 0.72f) * (0.35f + 0.65f * std::min(1.0f, t * 7.0f));
            const float auricle = (x > 0.0f) ? (0.20f * std::exp(-std::pow((t - 0.14f) / 0.11f, 2.0f))) : 0.0f;
            const float teeth   = 0.032f * std::sin(t * kPi * 16.0f) * std::sin(kPi * t);
            return ax <= std::max(0.015f, baseW + auricle + teeth);
        }
        case LeafSdfArchetype::PalmLeaflet:
        {
            // Crisp linear-ensiform V-folded palm leaflet tapering smoothly to an acute tip!
            const float t = (z + 0.94f) / 1.88f;
            if (t < 0.0f || t > 1.0f) return false;
            const float w = 0.16f * std::sin(kPi * std::pow(t, 0.70f)) * (1.0f - 0.25f * t);
            return ax <= std::max(0.012f, w);
        }
        case LeafSdfArchetype::PineFascicle:
        default:
        {
            // Twin radiating pine needle cluster
            const float t = (z + 0.90f) / 1.80f;
            if (t < 0.0f || t > 1.0f) return false;
            const float spread = 0.22f * t;
            return (std::fabs(ax - spread) <= 0.045f) || (ax <= 0.045f);
        }
    }
}

/// Exact O(N^2) Felzenszwalb-Huttenlocher 2D Squared Euclidean Distance Transform
void ComputeExact2dEdt(const std::vector<uint8_t>& binary, int res, bool targetInside, std::vector<double>& outDistSq)
{
    constexpr double kInf = 1.0e12;
    const std::size_t n = static_cast<std::size_t>(res);
    outDistSq.assign(n * n, kInf);
    for (std::size_t i = 0u; i < n * n; ++i)
    {
        outDistSq[i] = ((binary[i] != 0u) == targetInside) ? 0.0 : kInf;
    }

    std::vector<int>    v(n, 0);
    std::vector<double> z(n + 1u, 0.0);
    std::vector<double> f(n, 0.0);

    auto edt1d = [&](const std::vector<double>& inLine, std::vector<double>& outLine)
    {
        int k = 0;
        v[0] = 0;
        z[0] = -kInf;
        z[1] = +kInf;
        for (int q = 1; q < res; ++q)
        {
            double s = 0.0;
            for (;;)
            {
                const int vk = v[static_cast<std::size_t>(k)];
                s = ((inLine[static_cast<std::size_t>(q)] + static_cast<double>(q * q))
                   - (inLine[static_cast<std::size_t>(vk)] + static_cast<double>(vk * vk)))
                   / static_cast<double>(2 * (q - vk));
                if (s > z[static_cast<std::size_t>(k)]) break;
                --k;
            }
            ++k;
            v[static_cast<std::size_t>(k)] = q;
            z[static_cast<std::size_t>(k)] = s;
            z[static_cast<std::size_t>(k + 1)] = +kInf;
        }
        k = 0;
        for (int q = 0; q < res; ++q)
        {
            while (z[static_cast<std::size_t>(k + 1)] < static_cast<double>(q)) ++k;
            const int vk = v[static_cast<std::size_t>(k)];
            const double d = static_cast<double>(q - vk);
            outLine[static_cast<std::size_t>(q)] = d * d + inLine[static_cast<std::size_t>(vk)];
        }
    };

    std::vector<double> tmp(n, 0.0);
    // Transform columns
    for (int x = 0; x < res; ++x)
    {
        for (int y = 0; y < res; ++y) f[static_cast<std::size_t>(y)] = outDistSq[static_cast<std::size_t>(y) * n + static_cast<std::size_t>(x)];
        edt1d(f, tmp);
        for (int y = 0; y < res; ++y) outDistSq[static_cast<std::size_t>(y) * n + static_cast<std::size_t>(x)] = tmp[static_cast<std::size_t>(y)];
    }
    // Transform rows
    for (int y = 0; y < res; ++y)
    {
        for (int x = 0; x < res; ++x) f[static_cast<std::size_t>(x)] = outDistSq[static_cast<std::size_t>(y) * n + static_cast<std::size_t>(x)];
        edt1d(f, tmp);
        for (int x = 0; x < res; ++x) outDistSq[static_cast<std::size_t>(y) * n + static_cast<std::size_t>(x)] = tmp[static_cast<std::size_t>(x)];
    }
}

[[nodiscard]] bool IntersectRayAabb(const V3& ro, const V3& rd, const V3& bMin, const V3& bMax, float& outNear, float& outFar) noexcept
{
    float tn = 0.0f, tf = 1.0e9f;
    for (int k = 0; k < 3; ++k)
    {
        if (std::fabs(rd[static_cast<std::size_t>(k)]) < 1.0e-7f)
        {
            if (ro[static_cast<std::size_t>(k)] < bMin[static_cast<std::size_t>(k)]
                || ro[static_cast<std::size_t>(k)] > bMax[static_cast<std::size_t>(k)])
            {
                return false;
            }
            continue;
        }
        const float inv = 1.0f / rd[static_cast<std::size_t>(k)];
        float t0 = (bMin[static_cast<std::size_t>(k)] - ro[static_cast<std::size_t>(k)]) * inv;
        float t1 = (bMax[static_cast<std::size_t>(k)] - ro[static_cast<std::size_t>(k)]) * inv;
        if (t0 > t1) std::swap(t0, t1);
        tn = std::max(tn, t0);
        tf = std::min(tf, t1);
        if (tn > tf) return false;
    }
    outNear = tn;
    outFar  = tf;
    return true;
}

} // namespace

LeafSdfArchetype ParseLeafArchetypeToken(const std::string_view token) noexcept
{
    if (token == "ovate_beech")    return LeafSdfArchetype::OvateBeech;
    if (token == "heart_linden")   return LeafSdfArchetype::HeartLinden;
    if (token == "lobed_oak")      return LeafSdfArchetype::LobedOak;
    if (token == "palmate_maple")  return LeafSdfArchetype::PalmateMaple;
    if (token == "slender_willow") return LeafSdfArchetype::SlenderWillow;
    if (token == "fern_pinna")     return LeafSdfArchetype::FernPinna;
    if (token == "palm_leaflet")   return LeafSdfArchetype::PalmLeaflet;
    if (token == "pine_fascicle")  return LeafSdfArchetype::PineFascicle;
    return LeafSdfArchetype::LobedOak;
}

float EvalBarkFbm3d(const std::array<float, 3>& p, const float freq) noexcept
{
    const float oct1 = EvalSmoothNoise3d(p[0] * freq, p[1] * freq, p[2] * freq * 0.46f) * 2.0f - 1.0f;
    const float f2   = freq * 2.13f;
    const float oct2 = EvalSmoothNoise3d(p[1] * f2 + 11.3f, p[0] * 0.62f * f2 + 7.1f, p[2] * f2 + 5.7f) * 2.0f - 1.0f;
    return oct1 * 0.68f + oct2 * 0.32f;
}

void LeafArchetypeSdfAtlas::BuildAllArchetypes()
{
    Resolution = kLeafSdf2dRes;
    LayerCount = static_cast<uint32_t>(LeafSdfArchetype::Count);
    SignedDist.assign(static_cast<std::size_t>(LayerCount) * Resolution * Resolution, 1.0f);

    constexpr int kHiRes = 256; // 4x super-resolution exact Felzenszwalb-Huttenlocher 2D EDT!
    const int block = kHiRes / static_cast<int>(Resolution);
    std::vector<uint8_t> bin(static_cast<std::size_t>(kHiRes * kHiRes), 0u);
    std::vector<double> dOutSq {}, dInSq {};

    for (uint32_t layer = 0u; layer < LayerCount; ++layer)
    {
        const auto arch = static_cast<LeafSdfArchetype>(layer);
        uint32_t insideCount = 0u;
        for (int y = 0; y < kHiRes; ++y)
        {
            const float z = -1.0f + 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(kHiRes);
            for (int x = 0; x < kHiRes; ++x)
            {
                const float u = -1.0f + 2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(kHiRes);
                const bool in = TestLeafArchetypeInside(arch, u, z);
                bin[static_cast<std::size_t>(y * kHiRes + x)] = in ? 1u : 0u;
                if (in) ++insideCount;
            }
        }
        AreaFraction[layer] = static_cast<float>(insideCount) / static_cast<float>(kHiRes * kHiRes);

        ComputeExact2dEdt(bin, kHiRes, true, dOutSq);
        ComputeExact2dEdt(bin, kHiRes, false, dInSq);

        const float pixelSpan = 2.0f / static_cast<float>(kHiRes);
        const std::size_t layerOff = static_cast<std::size_t>(layer) * Resolution * Resolution;
        for (uint32_t ry = 0u; ry < Resolution; ++ry)
        {
            for (uint32_t rx = 0u; rx < Resolution; ++rx)
            {
                double sum = 0.0;
                for (int by = 0; by < block; ++by)
                {
                    for (int bx = 0; bx < block; ++bx)
                    {
                        const std::size_t hi = static_cast<std::size_t>((static_cast<int>(ry) * block + by) * kHiRes
                                                                      + (static_cast<int>(rx) * block + bx));
                        const double sd = (bin[hi] != 0u)
                            ? -std::sqrt(std::max(0.0, dInSq[hi])) * pixelSpan
                            :  std::sqrt(std::max(0.0, dOutSq[hi])) * pixelSpan;
                        sum += sd;
                    }
                }
                SignedDist[layerOff + ry * Resolution + rx] = static_cast<float>(sum / static_cast<double>(block * block));
            }
        }
    }
}

float LeafArchetypeSdfAtlas::SampleBilinear(const float u, const float w, const LeafSdfArchetype arch) const noexcept
{
    if (SignedDist.empty()) return 1.0f;
    const uint32_t layer = std::min(static_cast<uint32_t>(arch), LayerCount - 1u);
    const float fu = std::clamp(u * 0.5f + 0.5f, 0.0f, 1.0f) * static_cast<float>(Resolution - 1u);
    const float fw = std::clamp(w * 0.5f + 0.5f, 0.0f, 1.0f) * static_cast<float>(Resolution - 1u);
    const uint32_t x0 = static_cast<uint32_t>(fu), y0 = static_cast<uint32_t>(fw);
    const uint32_t x1 = std::min(x0 + 1u, Resolution - 1u), y1 = std::min(y0 + 1u, Resolution - 1u);
    const float tx = fu - static_cast<float>(x0), ty = fw - static_cast<float>(y0);
    const std::size_t base = static_cast<std::size_t>(layer) * Resolution * Resolution;
    const float s00 = SignedDist[base + y0 * Resolution + x0];
    const float s10 = SignedDist[base + y0 * Resolution + x1];
    const float s01 = SignedDist[base + y1 * Resolution + x0];
    const float s11 = SignedDist[base + y1 * Resolution + x1];
    const float sx0 = s00 + (s10 - s00) * tx;
    const float sx1 = s01 + (s11 - s01) * tx;
    const float sampled = sx0 + (sx1 - sx0) * ty;

    const float overU = std::max(0.0f, std::fabs(u) - 1.0f);
    const float overW = std::max(0.0f, std::fabs(w) - 1.0f);
    if (overU > 0.0f || overW > 0.0f)
    {
        return std::max(sampled, 0.0f) + std::hypot(overU, overW);
    }
    return sampled;
}

float EvaluateNoisyWoodCapsuleSdf(
    const BotanicalWoodCapsule& cap,
    const std::array<float, 3>& queryPos,
    const float                 barkNoiseAmp,
    const float                 barkNoiseFreq) noexcept
{
    const V3 u = Sub3(cap.P1, cap.P0);
    const V3 w = Sub3(queryPos, cap.P0);
    const float uu = std::max(Dot3(u, u), 1.0e-8f);
    const float s = std::clamp(Dot3(w, u) / uu, 0.0f, 1.0f);
    const V3 axisPt = Add3(cap.P0, Mul3(u, s));
    const V3 diff   = Sub3(queryPos, axisPt);
    const float distAxis = Len3(diff);
    const float rSmooth  = cap.R0 + (cap.R1 - cap.R0) * s;

    if (barkNoiseAmp <= 1.0e-4f || distAxis > rSmooth * 1.45f)
    {
        return distAxis - rSmooth;
    }

    const V3 nHat = (distAxis > 1.0e-6f) ? Mul3(diff, 1.0f / distAxis) : V3{ 0.0f, 0.0f, 1.0f };
    // Evaluate coherent 3D bark FBM on REST-POSE skin projection so shared joints stay 0.000 mm watertight
    // AND bark noise NEVER swims during wind sway!
    const V3 restAxis = Add3(cap.RestP0, Mul3(Sub3(cap.RestP1, cap.RestP0), s));
    const V3 pSkinRest = Add3(restAxis, Mul3(nHat, rSmooth));
    const float fbm = EvalBarkFbm3d(pSkinRest, barkNoiseFreq);
    const float rBark = rSmooth * (1.0f + kBarkDispMaxFrac * barkNoiseAmp * fbm);
    return distAxis - rBark;
}

float EvaluateCurvedThinLeafSdf(
    const BotanicalLeafSdfInstance& leaf,
    const LeafArchetypeSdfAtlas&    atlas,
    const std::array<float, 3>&     queryPos) noexcept
{
    const float invS = 1.0f / std::max(leaf.HalfSpanMeters, 1.0e-4f);
    const V3 rel = Sub3(queryPos, leaf.CenterMeters);
    const float u = Dot3(rel, leaf.AxisX) * invS;
    const float y = Dot3(rel, leaf.AxisY) * invS;
    const float w = Dot3(rel, leaf.AxisZ) * invS;

    const float d2d = leaf.HalfSpanMeters * atlas.SampleBilinear(u, w, leaf.Archetype);
    const float camber = leaf.CamberX * u * u + leaf.CamberZ * w * w;
    const float dSheet = std::fabs(y - camber) * leaf.HalfSpanMeters - leaf.HalfThicknessM;

    if (d2d <= 0.0f) return std::max(d2d, dSheet);
    const float eSheet = std::max(0.0f, dSheet);
    return std::hypot(d2d, eSheet);
}

void BotanicalSdfVolume::BuildCanopyBricks(const LeafArchetypeSdfAtlas& atlas, const float swayPaddingMeters)
{
    BoundsMin = {  1.0e6f,  1.0e6f,  1.0e6f };
    BoundsMax = { -1.0e6f, -1.0e6f, -1.0e6f };

    auto expandPt = [&](const V3& p, float rad)
    {
        for (int k = 0; k < 3; ++k)
        {
            BoundsMin[static_cast<std::size_t>(k)] = std::min(BoundsMin[static_cast<std::size_t>(k)], p[static_cast<std::size_t>(k)] - rad);
            BoundsMax[static_cast<std::size_t>(k)] = std::max(BoundsMax[static_cast<std::size_t>(k)], p[static_cast<std::size_t>(k)] + rad);
        }
    };

    for (const auto& c : WoodCapsules)
    {
        expandPt(c.P0, c.R0 * 1.25f + swayPaddingMeters);
        expandPt(c.P1, c.R1 * 1.25f + swayPaddingMeters);
    }
    for (const auto& lf : Leaves)
    {
        if (!lf.Detached) expandPt(lf.CenterMeters, lf.HalfSpanMeters * 1.15f + swayPaddingMeters);
    }

    if (BoundsMin[0] > BoundsMax[0])
    {
        BoundsMin = { -1.0f, -1.0f, 0.0f };
        BoundsMax = {  1.0f,  1.0f, 2.0f };
    }

    BrickRes = std::clamp(BrickRes, 4u, 24u);
    const uint32_t totalBricks = BrickRes * BrickRes * BrickRes;
    Bricks.assign(totalBricks, CanopyBrickHeader{});
    BrickLeafRefs.clear();
    OccupiedBricks = 0u;

    const V3 span {
        std::max(BoundsMax[0] - BoundsMin[0], 1.0e-3f),
        std::max(BoundsMax[1] - BoundsMin[1], 1.0e-3f),
        std::max(BoundsMax[2] - BoundsMin[2], 1.0e-3f)
    };
    const V3 brickSize {
        span[0] / static_cast<float>(BrickRes),
        span[1] / static_cast<float>(BrickRes),
        span[2] / static_cast<float>(BrickRes)
    };
    const float brickVol = std::max(brickSize[0] * brickSize[1] * brickSize[2], 1.0e-6f);

    std::vector<std::vector<uint32_t>> perBrick(totalBricks);
    for (uint32_t i = 0u; i < static_cast<uint32_t>(Leaves.size()); ++i)
    {
        const auto& lf = Leaves[i];
        if (lf.Detached) continue;
        const float r = lf.HalfSpanMeters * 1.12f;
        const int x0 = std::clamp(static_cast<int>(std::floor((lf.CenterMeters[0] - r - BoundsMin[0]) / brickSize[0])), 0, static_cast<int>(BrickRes) - 1);
        const int y0 = std::clamp(static_cast<int>(std::floor((lf.CenterMeters[1] - r - BoundsMin[1]) / brickSize[1])), 0, static_cast<int>(BrickRes) - 1);
        const int z0 = std::clamp(static_cast<int>(std::floor((lf.CenterMeters[2] - r - BoundsMin[2]) / brickSize[2])), 0, static_cast<int>(BrickRes) - 1);
        const int x1 = std::clamp(static_cast<int>(std::floor((lf.CenterMeters[0] + r - BoundsMin[0]) / brickSize[0])), 0, static_cast<int>(BrickRes) - 1);
        const int y1 = std::clamp(static_cast<int>(std::floor((lf.CenterMeters[1] + r - BoundsMin[1]) / brickSize[1])), 0, static_cast<int>(BrickRes) - 1);
        const int z1 = std::clamp(static_cast<int>(std::floor((lf.CenterMeters[2] + r - BoundsMin[2]) / brickSize[2])), 0, static_cast<int>(BrickRes) - 1);
        for (int z = z0; z <= z1; ++z)
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x)
                    perBrick[static_cast<std::size_t>((z * static_cast<int>(BrickRes) + y) * static_cast<int>(BrickRes) + x)].push_back(i);
    }

    for (uint32_t b = 0u; b < totalBricks; ++b)
    {
        if (perBrick[b].empty()) continue;
        ++OccupiedBricks;
        Bricks[b].StartOffset   = static_cast<uint32_t>(BrickLeafRefs.size());
        Bricks[b].InstanceCount = static_cast<uint32_t>(perBrick[b].size());
        float areaX = 0.0f, areaY = 0.0f, areaZ = 0.0f;
        for (const uint32_t idx : perBrick[b])
        {
            BrickLeafRefs.push_back(idx);
            const auto& lf = Leaves[idx];
            const float areaFrac = atlas.AreaFraction[static_cast<std::size_t>(lf.Archetype)];
            const float bladeArea = (4.0f * lf.HalfSpanMeters * lf.HalfSpanMeters) * areaFrac;
            areaX += bladeArea * std::fabs(lf.AxisY[0]);
            areaY += bladeArea * std::fabs(lf.AxisY[1]);
            areaZ += bladeArea * std::fabs(lf.AxisY[2]);
        }
        Bricks[b].SigmaX = areaX / brickVol;
        Bricks[b].SigmaY = areaY / brickVol;
        Bricks[b].SigmaZ = areaZ / brickVol;
    }
}

void BotanicalSdfVolume::ApplyWindBend(
    const float bendAngleXRad,
    const float bendAngleYRad,
    const float gustTimeSec,
    const float rustleAmp) noexcept
{
    // Cantilever sway around +Z up: displaces X/Y proportional to WindFlex & drops Z gently to conserve arc length!
    auto bendPoint = [&](const V3& rest, float flex, float phase) -> V3
    {
        const float h = std::max(0.0f, rest[2]);
        const float rustle = rustleAmp * flex * std::sin(gustTimeSec * 6.5f + phase * 6.283185f) * 0.04f;
        const float dx = (bendAngleXRad * h + rustle) * flex;
        const float dy = (bendAngleYRad * h + rustle * 0.7f) * flex;
        const float dz = -0.35f * (dx * dx + dy * dy) / std::max(h, 0.35f);
        return { rest[0] + dx, rest[1] + dy, std::max(0.0f, rest[2] + dz) };
    };

    for (auto& cap : WoodCapsules)
    {
        cap.P0 = bendPoint(cap.RestP0, cap.WindFlex0, 0.0f);
        cap.P1 = bendPoint(cap.RestP1, cap.WindFlex1, 0.0f);
    }
    for (auto& lf : Leaves)
    {
        if (!lf.Detached)
        {
            lf.CenterMeters = bendPoint(lf.RestCenterM, lf.WindFlex, lf.Phase);
        }
    }
}

float BotanicalSdfVolume::EvaluateSignedDistance(
    const LeafArchetypeSdfAtlas& atlas,
    const std::array<float, 3>&  queryPos) const noexcept
{
    float best = 1.0e6f;
    for (const auto& cap : WoodCapsules)
    {
        best = std::min(best, EvaluateNoisyWoodCapsuleSdf(cap, queryPos, BarkNoiseAmp, BarkNoiseFreq));
    }

    if (OccupiedBricks > 0u)
    {
        const V3 span {
            std::max(BoundsMax[0] - BoundsMin[0], 1.0e-3f),
            std::max(BoundsMax[1] - BoundsMin[1], 1.0e-3f),
            std::max(BoundsMax[2] - BoundsMin[2], 1.0e-3f)
        };
        const int bx = static_cast<int>(std::floor(((queryPos[0] - BoundsMin[0]) / span[0]) * static_cast<float>(BrickRes)));
        const int by = static_cast<int>(std::floor(((queryPos[1] - BoundsMin[1]) / span[1]) * static_cast<float>(BrickRes)));
        const int bz = static_cast<int>(std::floor(((queryPos[2] - BoundsMin[2]) / span[2]) * static_cast<float>(BrickRes)));
        if (bx >= 0 && by >= 0 && bz >= 0
            && bx < static_cast<int>(BrickRes) && by < static_cast<int>(BrickRes) && bz < static_cast<int>(BrickRes))
        {
            const auto& br = Bricks[static_cast<std::size_t>((bz * static_cast<int>(BrickRes) + by) * static_cast<int>(BrickRes) + bx)];
            for (uint32_t k = 0u; k < br.InstanceCount; ++k)
            {
                const auto& lf = Leaves[BrickLeafRefs[br.StartOffset + k]];
                if (!lf.Detached) best = std::min(best, EvaluateCurvedThinLeafSdf(lf, atlas, queryPos));
            }
        }
    }
    return best;
}

float BotanicalSdfVolume::TraceShadowTransmittance(
    const LeafArchetypeSdfAtlas& atlas,
    const std::array<float, 3>&  rayOrigin,
    const std::array<float, 3>&  sunDirNorm,
    const float                  tanSunCone,
    const float                  lodConeBias) const noexcept
{
    float tEnter = 0.0f, tExit = 0.0f;
    if (!IntersectRayAabb(rayOrigin, sunDirNorm, BoundsMin, BoundsMax, tEnter, tExit))
    {
        return 1.0f;
    }

    float trans = 1.0f;

    // 1. Upfront Analytic Noisy Wood-Capsule Shadow Evaluation (100% limb/twig survival!)
    for (const auto& cap : WoodCapsules)
    {
        const V3 u = Sub3(cap.P1, cap.P0);
        const V3 w = Sub3(rayOrigin, cap.P0);
        const float B = Dot3(sunDirNorm, u);
        const float C = Dot3(u, u);
        const float D = Dot3(sunDirNorm, w);
        const float E = Dot3(u, w);
        const float denom = std::max(0.0f, C - B * B);

        float s = (denom > 1.0e-7f) ? std::clamp((E - B * D) / denom, 0.0f, 1.0f) : 0.0f;
        float t = std::clamp(B * s - D, tEnter, tExit);
        s = (C > 1.0e-7f) ? std::clamp((E + t * B) / C, 0.0f, 1.0f) : 0.0f;
        t = std::clamp(B * s - D, tEnter, tExit);

        // Also explicitly check start & end spherical caps (eliminates short-segment grazing dropouts!)
        auto evalAt = [&](float tc) -> float
        {
            const V3 p = Add3(rayOrigin, Mul3(sunDirNorm, tc));
            return EvaluateNoisyWoodCapsuleSdf(cap, p, BarkNoiseAmp, BarkNoiseFreq);
        };
        float bestD = evalAt(t);
        float bestT = t;
        const float t0 = std::clamp(-D, tEnter, tExit);
        const float d0 = evalAt(t0);
        if (d0 < bestD) { bestD = d0; bestT = t0; }
        const float t1 = std::clamp(B - D, tEnter, tExit);
        const float d1 = evalAt(t1);
        if (d1 < bestD) { bestD = d1; bestT = t1; }

        const float rCone = std::max(0.002f, bestT * tanSunCone);
        const float vis = std::clamp(0.5f + 0.5f * bestD / rCone, 0.0f, 1.0f);
        trans = std::min(trans, vis);
        if (trans <= 0.01f) return 0.0f;
    }

    // 2. Two-Level 16^3 Canopy Brick DDA + Curved 2.5D Thin-Sheet Zero-Crossing + Cone-Width Beer-Lambert LOD
    if (OccupiedBricks == 0u) return trans;

    const V3 span {
        std::max(BoundsMax[0] - BoundsMin[0], 1.0e-3f),
        std::max(BoundsMax[1] - BoundsMin[1], 1.0e-3f),
        std::max(BoundsMax[2] - BoundsMin[2], 1.0e-3f)
    };
    const V3 brickSize {
        span[0] / static_cast<float>(BrickRes),
        span[1] / static_cast<float>(BrickRes),
        span[2] / static_cast<float>(BrickRes)
    };
    const float stepLen = std::min({ brickSize[0], brickSize[1], brickSize[2] }) * 0.85f;
    int lastBrick = -1;

    for (float t = tEnter + 0.005f; t < tExit && trans > 0.015f; t += stepLen)
    {
        const V3 p = Add3(rayOrigin, Mul3(sunDirNorm, t));
        const int bx = std::clamp(static_cast<int>(std::floor((p[0] - BoundsMin[0]) / brickSize[0])), 0, static_cast<int>(BrickRes) - 1);
        const int by = std::clamp(static_cast<int>(std::floor((p[1] - BoundsMin[1]) / brickSize[1])), 0, static_cast<int>(BrickRes) - 1);
        const int bz = std::clamp(static_cast<int>(std::floor((p[2] - BoundsMin[2]) / brickSize[2])), 0, static_cast<int>(BrickRes) - 1);
        const int bIdx = (bz * static_cast<int>(BrickRes) + by) * static_cast<int>(BrickRes) + bx;
        if (bIdx == lastBrick) continue;
        lastBrick = bIdx;

        const auto& br = Bricks[static_cast<std::size_t>(bIdx)];
        if (br.InstanceCount == 0u) continue;

        const float tSeg0 = std::max(tEnter, t - 0.5f * stepLen);
        const float tSeg1 = std::min(tExit,  t + 0.75f * stepLen);
        const float rCone = std::max(0.0015f, t * tanSunCone * lodConeBias);

        // Compute Cone-Width LOD blend (0 = exact 2.5D curved sheet silhouettes, 1 = anisotropic Beer-Lambert extinction)
        const float avgSpan = Leaves[BrickLeafRefs[br.StartOffset]].HalfSpanMeters;
        const float lodBlend = std::clamp((rCone / std::max(avgSpan, 0.02f) - 0.35f) / 0.85f, 0.0f, 1.0f);

        float sheetTrans = 1.0f;
        if (lodBlend < 0.98f)
        {
            for (uint32_t k = 0u; k < br.InstanceCount; ++k)
            {
                const auto& lf = Leaves[BrickLeafRefs[br.StartOffset + k]];
                if (lf.Detached) continue;

                const float invS = 1.0f / std::max(lf.HalfSpanMeters, 1.0e-4f);
                auto evalPhi = [&](float tt, float& outU, float& outW) -> float
                {
                    const V3 rp = Sub3(Add3(rayOrigin, Mul3(sunDirNorm, tt)), lf.CenterMeters);
                    outU = Dot3(rp, lf.AxisX) * invS;
                    outW = Dot3(rp, lf.AxisZ) * invS;
                    const float y = Dot3(rp, lf.AxisY) * invS;
                    return y - (lf.CamberX * outU * outU + lf.CamberZ * outW * outW);
                };

                float u0 = 0.0f, w0 = 0.0f, u1 = 0.0f, w1 = 0.0f;
                const float phi0 = evalPhi(tSeg0, u0, w0);
                const float phi1 = evalPhi(tSeg1, u1, w1);

                // Unconditional curved thin-sheet sign-change crossing (works even at h = 0.6 mm!)
                if (phi0 * phi1 <= 0.0f || std::fabs(phi0) * lf.HalfSpanMeters <= lf.HalfThicknessM)
                {
                    const float denom = phi1 - phi0;
                    const float alpha = (std::fabs(denom) > 1.0e-6f) ? std::clamp(-phi0 / denom, 0.0f, 1.0f) : 0.5f;
                    const float tStar = tSeg0 + (tSeg1 - tSeg0) * alpha;
                    float uStar = 0.0f, wStar = 0.0f;
                    (void)evalPhi(tStar, uStar, wStar);
                    if (std::fabs(uStar) <= 1.25f && std::fabs(wStar) <= 1.25f)
                    {
                        const float d2dM = lf.HalfSpanMeters * atlas.SampleBilinear(uStar, wStar, lf.Archetype);
                        const float vis = std::clamp(0.5f + 0.5f * d2dM / rCone, 0.0f, 1.0f);
                        sheetTrans = std::min(sheetTrans, vis);
                        if (sheetTrans <= 0.01f) break;
                    }
                }
            }
        }

        const float sigmaDir = br.SigmaX * std::fabs(sunDirNorm[0])
                             + br.SigmaY * std::fabs(sunDirNorm[1])
                             + br.SigmaZ * std::fabs(sunDirNorm[2]);
        const float beerTrans = std::exp(-sigmaDir * stepLen);
        const float brickTrans = sheetTrans * (1.0f - lodBlend) + beerTrans * lodBlend;
        trans *= brickTrans;
    }

    return std::clamp(trans, 0.0f, 1.0f);
}

} // namespace Frontier::GeometrySpace::Botanical
