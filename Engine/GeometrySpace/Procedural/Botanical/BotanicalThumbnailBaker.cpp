/*============================================================================================================================================
                                                      BOTANICALTHUMBNAILBAKER.CPP
============================================================================================================================================*/
// 🧩 Implementation of 3D Studio Plant Thumbnail Baker (Software Z-Buffer + 2x2 SSAA + Rim/Subsurface Foliage Shading + Zero-Dep PNG Writer).

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Procedural/Botanical/BotanicalThumbnailBaker.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>

namespace Frontier::GeometrySpace::Botanical
{

namespace
{

using V3 = std::array<float, 3>;

[[nodiscard]] inline V3 Add3(const V3& a, const V3& b) noexcept { return { a[0] + b[0], a[1] + b[1], a[2] + b[2] }; }
[[nodiscard]] inline V3 Sub3(const V3& a, const V3& b) noexcept { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
[[nodiscard]] inline V3 Mul3(const V3& a, float s) noexcept     { return { a[0] * s, a[1] * s, a[2] * s }; }
[[nodiscard]] inline float Dot3(const V3& a, const V3& b) noexcept { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
[[nodiscard]] inline V3 Cross3(const V3& a, const V3& b) noexcept
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}
[[nodiscard]] inline V3 Norm3(const V3& a) noexcept
{
    const float l = std::sqrt(Dot3(a, a));
    return (l > 1.0e-7f) ? Mul3(a, 1.0f / l) : V3{ 0.0f, 0.0f, 1.0f };
}
[[nodiscard]] inline uint8_t LinearToSrgbU8(float v) noexcept
{
    const float c = std::clamp(v, 0.0f, 1.0f);
    const float s = (c <= 0.0031308f) ? (12.92f * c) : (1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(s * 255.0f), 0L, 255L));
}

void AppendBe32(std::vector<uint8_t>& buf, uint32_t v)
{
    buf.push_back(static_cast<uint8_t>((v >> 24u) & 0xFFu));
    buf.push_back(static_cast<uint8_t>((v >> 16u) & 0xFFu));
    buf.push_back(static_cast<uint8_t>((v >>  8u) & 0xFFu));
    buf.push_back(static_cast<uint8_t>( v         & 0xFFu));
}

[[nodiscard]] uint32_t ComputeCrc32(const uint8_t* data, std::size_t len) noexcept
{
    uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0u; i < len; ++i)
    {
        crc ^= static_cast<uint32_t>(data[i]);
        for (int b = 0; b < 8; ++b)
        {
            crc = (crc & 1u) ? ((crc >> 1u) ^ 0xEDB88320u) : (crc >> 1u);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

void AppendPngChunk(std::vector<uint8_t>& out, const char tag[4], const std::vector<uint8_t>& payload)
{
    AppendBe32(out, static_cast<uint32_t>(payload.size()));
    const std::size_t tagStart = out.size();
    out.insert(out.end(), tag, tag + 4);
    out.insert(out.end(), payload.begin(), payload.end());
    const uint32_t crc = ComputeCrc32(out.data() + tagStart, out.size() - tagStart);
    AppendBe32(out, crc);
}

} // namespace

BakedPlantThumbnail BakePlant3dThumbnail(
    const BotanicalPlantBuildResult& plant,
    const uint32_t                   sizePx,
    const bool                       transparentBackdrop)
{
    BakedPlantThumbnail thumb {};
    thumb.Width  = std::clamp(sizePx, 32u, 512u);
    thumb.Height = thumb.Width;
    thumb.Rgba8.assign(static_cast<std::size_t>(thumb.Width) * thumb.Height * 4u, 0u);

    // 2x2 super-sampled software studio rasterizer for crisp, anti-aliased thin leaves & pinnae!
    const uint32_t ssW = thumb.Width * 2u;
    const uint32_t ssH = thumb.Height * 2u;
    std::vector<std::array<float, 4>> colorBuf(static_cast<std::size_t>(ssW) * ssH, { 0.0f, 0.0f, 0.0f, 0.0f });
    std::vector<float>                depthBuf(static_cast<std::size_t>(ssW) * ssH, std::numeric_limits<float>::infinity());

    // Studio rounded-card backdrop gradient (matches Frontier Outliner dark slate tile!)
    for (uint32_t y = 0u; y < ssH; ++y)
    {
        const float vy = static_cast<float>(y) / static_cast<float>(ssH - 1u);
        for (uint32_t x = 0u; x < ssW; ++x)
        {
            const float vx = static_cast<float>(x) / static_cast<float>(ssW - 1u);
            const float dx = (vx - 0.5f) * 2.0f;
            const float dy = (vy - 0.55f) * 2.0f;
            const float r2 = dx * dx + dy * dy;
            if (!transparentBackdrop)
            {
                // Soft dark studio radial vignette with warm-green subtle studio key reflection
                const float glow = std::exp(-r2 * 1.45f);
                const float bgR = 0.032f + 0.022f * glow + 0.012f * (1.0f - vy);
                const float bgG = 0.038f + 0.028f * glow + 0.015f * (1.0f - vy);
                const float bgB = 0.046f + 0.026f * glow + 0.018f * (1.0f - vy);
                // Soft ground contact shadow disc beneath plant base
                const float sd = std::exp(-((vx - 0.5f) * (vx - 0.5f) * 14.0f + (vy - 0.86f) * (vy - 0.86f) * 85.0f));
                const float occ = 1.0f - 0.55f * sd;
                colorBuf[static_cast<std::size_t>(y) * ssW + x] = { bgR * occ, bgG * occ, bgB * occ, 1.0f };
            }
        }
    }

    // Auto-frame camera around plant bounding box in Frontier +Z Up
    const V3 bMin = plant.BoundsMin;
    const V3 bMax = plant.BoundsMax;
    const V3 center {
        0.5f * (bMin[0] + bMax[0]),
        0.5f * (bMin[1] + bMax[1]),
        0.48f * (bMin[2] + bMax[2])
    };
    const float spanX = std::max(0.25f, bMax[0] - bMin[0]);
    const float spanY = std::max(0.25f, bMax[1] - bMin[1]);
    const float spanZ = std::max(0.25f, bMax[2] - bMin[2]);
    const float radius = 0.5f * std::sqrt(spanX * spanX + spanY * spanY + spanZ * spanZ);

    const float elev = (plant.GrowthForm == BotanicalGrowthForm::Fern) ? 0.52f : 0.24f;
    const float az   = 0.62f;
    const float dist = radius * 2.55f;
    const V3 camPos {
        center[0] + dist * std::cos(elev) * std::cos(az),
        center[1] + dist * std::cos(elev) * std::sin(az),
        center[2] + dist * std::sin(elev)
    };
    const V3 camFwd = Norm3(Sub3(center, camPos));
    const V3 camRgt = Norm3(Cross3(camFwd, { 0.0f, 0.0f, 1.0f }));
    const V3 camUp  = Norm3(Cross3(camRgt, camFwd));
    const float focal = static_cast<float>(ssW) * 1.18f;

    // Three-point studio key + sky hemisphere + warm foliage rim/translucency
    const V3 keyDir = Norm3({ 0.58f, 0.36f, 0.73f });
    const V3 rimDir = Norm3({ -0.68f, -0.45f, 0.58f });

    auto project = [&](const V3& p, float& sx, float& sy, float& cz) -> bool
    {
        const V3 rel = Sub3(p, camPos);
        cz = Dot3(rel, camFwd);
        if (cz <= 0.05f) return false;
        const float cx = Dot3(rel, camRgt);
        const float cy = Dot3(rel, camUp);
        sx = 0.5f * static_cast<float>(ssW) + (cx / cz) * focal;
        sy = 0.52f * static_cast<float>(ssH) - (cy / cz) * focal;
        return true;
    };

    for (const auto& tri : plant.Triangles)
    {
        float x0 = 0.0f, y0 = 0.0f, z0 = 0.0f;
        float x1 = 0.0f, y1 = 0.0f, z1 = 0.0f;
        float x2 = 0.0f, y2 = 0.0f, z2 = 0.0f;
        if (!project(tri.V0, x0, y0, z0) || !project(tri.V1, x1, y1, z1) || !project(tri.V2, x2, y2, z2)) continue;

        const float area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
        if (std::fabs(area) < 1.0e-4f) continue;

        const int minX = std::clamp(static_cast<int>(std::floor(std::min({ x0, x1, x2 }))), 0, static_cast<int>(ssW) - 1);
        const int maxX = std::clamp(static_cast<int>(std::ceil( std::max({ x0, x1, x2 }))), 0, static_cast<int>(ssW) - 1);
        const int minY = std::clamp(static_cast<int>(std::floor(std::min({ y0, y1, y2 }))), 0, static_cast<int>(ssH) - 1);
        const int maxY = std::clamp(static_cast<int>(std::ceil( std::max({ y0, y1, y2 }))), 0, static_cast<int>(ssH) - 1);

        const float avgZ = (tri.V0[2] + tri.V1[2] + tri.V2[2]) * 0.3333f;
        const float heightAo = 0.55f + 0.45f * std::clamp((avgZ - bMin[2]) / std::max(0.2f, spanZ), 0.0f, 1.0f);

        const float invArea = 1.0f / area;
        for (int py = minY; py <= maxY; ++py)
        {
            const float fy = static_cast<float>(py) + 0.5f;
            for (int px = minX; px <= maxX; ++px)
            {
                const float fx = static_cast<float>(px) + 0.5f;
                const float w0 = ((x1 - fx) * (y2 - fy) - (y1 - fy) * (x2 - fx)) * invArea;
                const float w1 = ((x2 - fx) * (y0 - fy) - (y2 - fy) * (x0 - fx)) * invArea;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < -0.001f || w1 < -0.001f || w2 < -0.001f) continue;

                const float depth = w0 * z0 + w1 * z1 + w2 * z2;
                const std::size_t idx = static_cast<std::size_t>(py) * ssW + static_cast<std::size_t>(px);
                if (depth < depthBuf[idx])
                {
                    depthBuf[idx] = depth;
                    V3 n = Norm3({
                        w0 * tri.N0[0] + w1 * tri.N1[0] + w2 * tri.N2[0],
                        w0 * tri.N0[1] + w1 * tri.N1[1] + w2 * tri.N2[1],
                        w0 * tri.N0[2] + w1 * tri.N1[2] + w2 * tri.N2[2]
                    });
                    if (Dot3(n, camFwd) > 0.0f) n = Mul3(n, -1.0f);
                    const float ndl  = std::max(0.0f, Dot3(n, keyDir));
                    const float sss  = (tri.Part == BotanicalPartTag::Leaf) ? std::max(0.0f, Dot3(Mul3(n, -1.0f), keyDir)) * 0.42f : 0.0f;
                    const float rim  = std::pow(1.0f - std::clamp(std::fabs(Dot3(n, camFwd)), 0.0f, 1.0f), 2.2f) * std::max(0.25f, Dot3(n, rimDir) * 0.5f + 0.5f);
                    const float hemi = 0.5f + 0.5f * n[2];
                    const float litR = tri.ColorLin[0] * (1.35f * ndl + 0.34f * hemi + sss * 1.15f) * heightAo + rim * 0.09f;
                    const float litG = tri.ColorLin[1] * (1.35f * ndl + 0.38f * hemi + sss * 1.35f) * heightAo + rim * 0.12f;
                    const float litB = tri.ColorLin[2] * (1.28f * ndl + 0.44f * hemi + sss * 0.65f) * heightAo + rim * 0.08f;
                    colorBuf[idx] = { litR, litG, litB, 1.0f };
                }
            }
        }
    }

    // 2x2 Box Downsample + ACES Filmic Tone-Map -> sRGB RGBA8
    auto aces = [](float x) noexcept -> float
    {
        const float v = std::max(0.0f, x * 1.18f);
        return (v * (2.51f * v + 0.03f)) / (v * (2.43f * v + 0.59f) + 0.14f);
    };

    for (uint32_t y = 0u; y < thumb.Height; ++y)
    {
        for (uint32_t x = 0u; x < thumb.Width; ++x)
        {
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
            for (uint32_t sy = 0u; sy < 2u; ++sy)
            {
                for (uint32_t sx = 0u; sx < 2u; ++sx)
                {
                    const auto& c = colorBuf[static_cast<std::size_t>(y * 2u + sy) * ssW + (x * 2u + sx)];
                    r += c[0]; g += c[1]; b += c[2]; a += c[3];
                }
            }
            const std::size_t dst = (static_cast<std::size_t>(y) * thumb.Width + x) * 4u;
            thumb.Rgba8[dst + 0u] = LinearToSrgbU8(aces(r * 0.25f));
            thumb.Rgba8[dst + 1u] = LinearToSrgbU8(aces(g * 0.25f));
            thumb.Rgba8[dst + 2u] = LinearToSrgbU8(aces(b * 0.25f));
            thumb.Rgba8[dst + 3u] = static_cast<uint8_t>(std::clamp(std::lround((a * 0.25f) * 255.0f), 0L, 255L));
        }
    }

    return thumb;
}

bool WriteRgba8PngFile(
    const std::filesystem::path& filePath,
    const uint32_t               width,
    const uint32_t               height,
    const std::vector<uint8_t>&  rgba8)
{
    if (width == 0u || height == 0u || rgba8.size() < static_cast<std::size_t>(width) * height * 4u) return false;

    std::error_code ec;
    if (filePath.has_parent_path()) std::filesystem::create_directories(filePath.parent_path(), ec);

    // Build raw filtered scanlines (filter type 0 = None)
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4u;
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (1u + rowBytes));
    for (uint32_t y = 0u; y < height; ++y)
    {
        raw.push_back(0u);
        const std::size_t rowOff = static_cast<std::size_t>(y) * rowBytes;
        raw.insert(raw.end(), rgba8.begin() + static_cast<std::ptrdiff_t>(rowOff),
                              rgba8.begin() + static_cast<std::ptrdiff_t>(rowOff + rowBytes));
    }

    // Wrap raw scanlines in valid stored-block zlib stream (RFC 1950 + RFC 1951)
    std::vector<uint8_t> zlib;
    zlib.push_back(0x78u);
    zlib.push_back(0x01u);
    std::size_t pos = 0u;
    while (pos < raw.size())
    {
        const std::size_t chunk = std::min<std::size_t>(65535u, raw.size() - pos);
        const bool isFinal = (pos + chunk >= raw.size());
        zlib.push_back(isFinal ? 0x01u : 0x00u);
        const uint16_t len = static_cast<uint16_t>(chunk);
        const uint16_t nlen = static_cast<uint16_t>(~len);
        zlib.push_back(static_cast<uint8_t>(len & 0xFFu));
        zlib.push_back(static_cast<uint8_t>((len >> 8u) & 0xFFu));
        zlib.push_back(static_cast<uint8_t>(nlen & 0xFFu));
        zlib.push_back(static_cast<uint8_t>((nlen >> 8u) & 0xFFu));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                                raw.begin() + static_cast<std::ptrdiff_t>(pos + chunk));
        pos += chunk;
    }

    // Adler-32 checksum
    uint32_t s1 = 1u, s2 = 0u;
    for (const uint8_t byte : raw)
    {
        s1 = (s1 + byte) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    AppendBe32(zlib, (s2 << 16u) | s1);

    std::vector<uint8_t> png;
    static constexpr uint8_t kSig[8] = { 137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u };
    png.insert(png.end(), kSig, kSig + 8);

    std::vector<uint8_t> ihdr;
    AppendBe32(ihdr, width);
    AppendBe32(ihdr, height);
    ihdr.push_back(8u); // 8-bit depth
    ihdr.push_back(6u); // RGBA
    ihdr.push_back(0u); // Deflate
    ihdr.push_back(0u); // Adaptive filter
    ihdr.push_back(0u); // No interlace
    AppendPngChunk(png, "IHDR", ihdr);
    AppendPngChunk(png, "IDAT", zlib);
    AppendPngChunk(png, "IEND", {});

    std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return out.good();
}

} // namespace Frontier::GeometrySpace::Botanical
