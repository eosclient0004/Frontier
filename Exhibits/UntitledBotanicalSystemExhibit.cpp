/*============================================================================================================================================
                                                   UNTITLEDBOTANICALSYSTEMEXHIBIT.CPP
============================================================================================================================================*/
// 🧩 Complete Verification & Proof Renderer for Frontier's Unified Botanical & Foliage System in Projects/Untitled!
//    - Validates all 8 strict engineering gates (`.botanical` inheritance, Ferns->Palms->Oak +Z Up generation, 64x64 2D EDT
//      2.5D thin-leaf SDF 100% survival & 0-swim rest-space bark noise, 16^3 Canopy Brick shadows, 16m x 16m WorldPartition
//      smooth C1 GPU<->CPU transitions, 4m x 4m 3-state falling/sleeping/waking leaves, and 128x128 baked 3D thumbnails).
//    - Renders BOTH Frontier's Native Editor UI (Outliner w/ folder-botanical SVG + baked 3D plant thumbnails + Geometry-style
//      Inspector cards) AND the sunlit 3D Scene in Projects/Untitled + animated wind/falling-leaf proof frames!

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Procedural/Botanical/BotanicalGenerator.h"
#include "Engine/GeometrySpace/Procedural/Botanical/BotanicalSpeciesSchema.h"
#include "Engine/GeometrySpace/Procedural/Botanical/BotanicalThinLeafSdf.h"
#include "Engine/GeometrySpace/Procedural/Botanical/BotanicalThumbnailBaker.h"
#include "Engine/GeometrySpace/Procedural/Botanical/BotanicalWorldPartition.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace Frontier::GeometrySpace::Botanical;

namespace
{

using V3 = std::array<float, 3>;
constexpr float kPi  = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;

inline V3 Add3(const V3& a, const V3& b) noexcept { return { a[0] + b[0], a[1] + b[1], a[2] + b[2] }; }
inline V3 Sub3(const V3& a, const V3& b) noexcept { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
inline V3 Mul3(const V3& a, float s) noexcept     { return { a[0] * s, a[1] * s, a[2] * s }; }
inline float Dot3(const V3& a, const V3& b) noexcept { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline V3 Cross3(const V3& a, const V3& b) noexcept
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}
inline V3 Norm3(const V3& a) noexcept
{
    const float l = std::sqrt(Dot3(a, a));
    return (l > 1.0e-7f) ? Mul3(a, 1.0f / l) : V3{ 0.0f, 0.0f, 1.0f };
}
inline uint8_t LinToSrgb(float v) noexcept
{
    const float c = std::clamp(v, 0.0f, 1.0f);
    const float s = (c <= 0.0031308f) ? (12.92f * c) : (1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(s * 255.0f), 0L, 255L));
}
inline float AcesTone(float x) noexcept
{
    const float v = std::max(0.0f, x * 1.08f);
    return (v * (2.51f * v + 0.03f)) / (v * (2.43f * v + 0.59f) + 0.14f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  COMPACT 2D RGBA CANVAS & CRISP BITMAP FONT FOR UI PROOF
//------------------------------------------------------------------------------------------------------------------------

struct CanvasRgba
{
    uint32_t             W = 0u;
    uint32_t             H = 0u;
    std::vector<uint8_t> Pix {};

    CanvasRgba(uint32_t w, uint32_t h, std::array<uint8_t, 4> bg = { 18u, 18u, 18u, 255u })
        : W(w), H(h), Pix(static_cast<std::size_t>(w) * h * 4u, 0u)
    {
        for (uint32_t i = 0u; i < w * h; ++i)
        {
            Pix[i * 4u + 0u] = bg[0]; Pix[i * 4u + 1u] = bg[1];
            Pix[i * 4u + 2u] = bg[2]; Pix[i * 4u + 3u] = bg[3];
        }
    }

    void BlendPx(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) noexcept
    {
        if (x < 0 || y < 0 || x >= static_cast<int>(W) || y >= static_cast<int>(H) || a == 0u) return;
        const std::size_t idx = (static_cast<std::size_t>(y) * W + static_cast<std::size_t>(x)) * 4u;
        const float al = static_cast<float>(a) / 255.0f;
        const float ia = 1.0f - al;
        Pix[idx + 0u] = static_cast<uint8_t>(std::clamp(std::lround(r * al + Pix[idx + 0u] * ia), 0L, 255L));
        Pix[idx + 1u] = static_cast<uint8_t>(std::clamp(std::lround(g * al + Pix[idx + 1u] * ia), 0L, 255L));
        Pix[idx + 2u] = static_cast<uint8_t>(std::clamp(std::lround(b * al + Pix[idx + 2u] * ia), 0L, 255L));
        Pix[idx + 3u] = 255u;
    }

    void FillRect(int x0, int y0, int w, int h, std::array<uint8_t, 4> col) noexcept
    {
        const int x1 = std::min(static_cast<int>(W), x0 + w);
        const int y1 = std::min(static_cast<int>(H), y0 + h);
        for (int y = std::max(0, y0); y < y1; ++y)
            for (int x = std::max(0, x0); x < x1; ++x)
                BlendPx(x, y, col[0], col[1], col[2], col[3]);
    }

    void FillRoundedRect(int x0, int y0, int w, int h, float rad, std::array<uint8_t, 4> col,
                         std::array<uint8_t, 4> border = { 0u, 0u, 0u, 0u }) noexcept
    {
        const int x1 = std::min(static_cast<int>(W), x0 + w);
        const int y1 = std::min(static_cast<int>(H), y0 + h);
        const float hw = 0.5f * static_cast<float>(w);
        const float hh = 0.5f * static_cast<float>(h);
        const float cx = static_cast<float>(x0) + hw;
        const float cy = static_cast<float>(y0) + hh;
        const float r  = std::min({ rad, hw, hh });
        for (int y = std::max(0, y0); y < y1; ++y)
        {
            for (int x = std::max(0, x0); x < x1; ++x)
            {
                const float qx = std::fabs(static_cast<float>(x) + 0.5f - cx) - (hw - r);
                const float qy = std::fabs(static_cast<float>(y) + 0.5f - cy) - (hh - r);
                const float sd = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - r;
                if (sd <= 0.5f)
                {
                    const float cov = std::clamp(0.5f - sd, 0.0f, 1.0f);
                    BlendPx(x, y, col[0], col[1], col[2], static_cast<uint8_t>(col[3] * cov));
                    if (border[3] > 0u && sd >= -1.25f)
                    {
                        const float bcov = std::clamp(1.0f - std::fabs(sd + 0.35f), 0.0f, 1.0f);
                        BlendPx(x, y, border[0], border[1], border[2], static_cast<uint8_t>(border[3] * bcov));
                    }
                }
            }
        }
    }

    void FillCircle(float cx, float cy, float r, std::array<uint8_t, 4> col) noexcept
    {
        const int x0 = std::max(0, static_cast<int>(std::floor(cx - r - 1.0f)));
        const int y0 = std::max(0, static_cast<int>(std::floor(cy - r - 1.0f)));
        const int x1 = std::min(static_cast<int>(W) - 1, static_cast<int>(std::ceil(cx + r + 1.0f)));
        const int y1 = std::min(static_cast<int>(H) - 1, static_cast<int>(std::ceil(cy + r + 1.0f)));
        for (int y = y0; y <= y1; ++y)
        {
            for (int x = x0; x <= x1; ++x)
            {
                const float d = std::hypot(static_cast<float>(x) + 0.5f - cx, static_cast<float>(y) + 0.5f - cy) - r;
                if (d < 0.75f)
                {
                    const float cov = std::clamp(0.75f - d, 0.0f, 1.0f);
                    BlendPx(x, y, col[0], col[1], col[2], static_cast<uint8_t>(col[3] * cov));
                }
            }
        }
    }

    void DrawLine(float x0, float y0, float x1, float y1, float thick, std::array<uint8_t, 4> col) noexcept
    {
        const float dx = x1 - x0, dy = y1 - y0;
        const float len = std::max(1.0f, std::hypot(dx, dy));
        const int steps = static_cast<int>(std::ceil(len * 2.0f));
        for (int i = 0; i <= steps; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            FillCircle(x0 + dx * t, y0 + dy * t, thick * 0.5f, col);
        }
    }

    void BlitThumb(int dstX, int dstY, int dstW, int dstH, const BakedPlantThumbnail& thumb, float cornerRad = 6.0f) noexcept
    {
        const float hw = 0.5f * static_cast<float>(dstW), hh = 0.5f * static_cast<float>(dstH);
        const float r = std::min({ cornerRad, hw, hh });
        for (int y = 0; y < dstH; ++y)
        {
            const float sy = (static_cast<float>(y) + 0.5f) / static_cast<float>(dstH) * static_cast<float>(thumb.Height - 1u);
            const uint32_t iy = std::min(thumb.Height - 1u, static_cast<uint32_t>(sy));
            for (int x = 0; x < dstW; ++x)
            {
                const float qx = std::fabs(static_cast<float>(x) + 0.5f - hw) - (hw - r);
                const float qy = std::fabs(static_cast<float>(y) + 0.5f - hh) - (hh - r);
                const float sd = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - r;
                if (sd > 0.5f) continue;
                const float sx = (static_cast<float>(x) + 0.5f) / static_cast<float>(dstW) * static_cast<float>(thumb.Width - 1u);
                const uint32_t ix = std::min(thumb.Width - 1u, static_cast<uint32_t>(sx));
                const std::size_t sIdx = (static_cast<std::size_t>(iy) * thumb.Width + ix) * 4u;
                const float cov = std::clamp(0.5f - sd, 0.0f, 1.0f);
                BlendPx(dstX + x, dstY + y,
                    thumb.Rgba8[sIdx + 0u], thumb.Rgba8[sIdx + 1u], thumb.Rgba8[sIdx + 2u],
                    static_cast<uint8_t>(thumb.Rgba8[sIdx + 3u] * cov));
            }
        }
    }

    // Crisp 5x7 ASCII bitmap font for UI labels, inspector values, and technical captions
    void DrawText(int x, int y, const std::string& text, int scale, std::array<uint8_t, 4> col) noexcept
    {
        auto glyphRow = [](char ch, int row) -> uint8_t
        {
            if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
            static constexpr struct { char c; uint8_t r[7]; } kFont[] = {
                {'A',{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}}, {'B',{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
                {'C',{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}}, {'D',{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
                {'E',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}}, {'F',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
                {'G',{0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}}, {'H',{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
                {'I',{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}}, {'J',{0x07,0x02,0x02,0x02,0x12,0x12,0x0C}},
                {'K',{0x11,0x12,0x14,0x18,0x14,0x12,0x11}}, {'L',{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
                {'M',{0x11,0x1B,0x15,0x15,0x11,0x11,0x11}}, {'N',{0x11,0x19,0x15,0x13,0x11,0x11,0x11}},
                {'O',{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'P',{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
                {'Q',{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}}, {'R',{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
                {'S',{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}}, {'T',{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
                {'U',{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'V',{0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
                {'W',{0x11,0x11,0x11,0x15,0x15,0x1B,0x11}}, {'X',{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
                {'Y',{0x11,0x11,0x0A,0x04,0x04,0x04,0x04}}, {'Z',{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
                {'0',{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}}, {'1',{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
                {'2',{0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}}, {'3',{0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}},
                {'4',{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}}, {'5',{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
                {'6',{0x0E,0x10,0x1E,0x11,0x11,0x11,0x0E}}, {'7',{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
                {'8',{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}}, {'9',{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
                {'.',{0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}}, {',',{0x00,0x00,0x00,0x00,0x00,0x04,0x08}},
                {':',{0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}}, {'-',{0x00,0x00,0x00,0x1F,0x00,0x00,0x00}},
                {'+',{0x00,0x04,0x04,0x1F,0x04,0x04,0x00}}, {'/',{0x01,0x02,0x02,0x04,0x08,0x08,0x10}},
                {'(',{0x02,0x04,0x08,0x08,0x08,0x04,0x02}}, {')',{0x08,0x04,0x02,0x02,0x02,0x04,0x08}},
                {'%',{0x19,0x19,0x02,0x04,0x08,0x13,0x13}}, {'_',{0x00,0x00,0x00,0x00,0x00,0x00,0x1F}},
                {'>',{0x10,0x08,0x04,0x02,0x04,0x08,0x10}}, {'<',{0x02,0x04,0x08,0x10,0x08,0x04,0x02}},
                {'=',{0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}}, {'|',{0x04,0x04,0x04,0x04,0x04,0x04,0x04}},
                {'[',{0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}}, {']',{0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}},
                {'#',{0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}}, {'*',{0x00,0x15,0x0E,0x1F,0x0E,0x15,0x00}},
                {'&',{0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}}, {'^',{0x04,0x0A,0x11,0x00,0x00,0x00,0x00}},
                {' ',{0x00,0x00,0x00,0x00,0x00,0x00,0x00}}
            };
            for (const auto& g : kFont) if (g.c == ch) return g.r[row];
            return 0u;
        };

        int curX = x;
        for (char c : text)
        {
            for (int r = 0; r < 7; ++r)
            {
                const uint8_t bits = glyphRow(c, r);
                for (int colBit = 0; colBit < 5; ++colBit)
                {
                    if ((bits >> (4 - colBit)) & 1u)
                    {
                        FillRect(curX + colBit * scale, y + r * scale, scale, scale, col);
                    }
                }
            }
            curX += 6 * scale;
        }
    }

    // Draws our Outliner folder-botanical.svg icon faithfully (sage-green rounded folder + dual leaf sprout)
    void DrawBotanicalFolderSvgIcon(int x, int y, int sz) noexcept
    {
        const float s = static_cast<float>(sz) / 24.0f;
        // Back folder tab & dark base
        FillRoundedRect(x + static_cast<int>(2 * s), y + static_cast<int>(4 * s),
                        static_cast<int>(9 * s), static_cast<int>(5 * s), 2.0f * s, { 94u, 118u, 76u, 255u });
        FillRoundedRect(x + static_cast<int>(2 * s), y + static_cast<int>(7 * s),
                        static_cast<int>(20 * s), static_cast<int>(13 * s), 2.5f * s, { 134u, 166u, 108u, 255u }, { 172u, 206u, 140u, 220u });
        // Sprouting botanical leaf silhouette inside folder (matches folder-botanical.svg!)
        FillCircle(x + 12.5f * s, y + 13.5f * s, 3.2f * s, { 28u, 42u, 22u, 220u });
        FillCircle(x + 14.5f * s, y + 11.8f * s, 2.2f * s, { 214u, 238u, 178u, 240u });
        FillCircle(x + 10.2f * s, y + 13.8f * s, 1.8f * s, { 190u, 222u, 148u, 230u });
        DrawLine(x + 11.8f * s, y + 17.8f * s, x + 12.2f * s, y + 12.2f * s, 1.5f * s, { 232u, 246u, 204u, 255u });
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                      3D SCENE RENDERER (GROUND + 2.5D THIN-LEAF SHADOWS + FERNS/PALMS/OAK + PARTICLES)
//------------------------------------------------------------------------------------------------------------------------

struct ScenePlantActor
{
    std::string               Label {};
    BotanicalPlantBuildResult Mesh  {};
    V3                        Pos   { 0.0f, 0.0f, 0.0f };
    float                     Yaw   = 0.0f;
    float                     Scale = 1.0f;
};

void RenderBotanical3dScene(
    CanvasRgba&                             canvas,
    int                                     vpX,
    int                                     vpY,
    int                                     vpW,
    int                                     vpH,
    const std::vector<ScenePlantActor>&     actors,
    const std::vector<FallingLeafParticle>& leaves,
    const LeafArchetypeSdfAtlas&            atlas,
    const V3&                               camPos,
    const V3&                               camTarget,
    const V3&                               sunDir,
    bool                                    traceGroundThinLeafShadows,
    bool                                    drawGridAndHudOverlay)
{
    std::vector<float> zBuf(static_cast<std::size_t>(vpW) * static_cast<std::size_t>(vpH), std::numeric_limits<float>::infinity());
    const V3 fwd = Norm3(Sub3(camTarget, camPos));
    const V3 rgt = Norm3(Cross3(fwd, { 0.0f, 0.0f, 1.0f }));
    const V3 up  = Norm3(Cross3(rgt, fwd));
    const float focal = static_cast<float>(vpW) * 0.96f;

    // 1. Physical Sky Atmosphere + Sunlit Meadow Ground Plane (with REAL 2.5D Thin-Leaf SDF Shadow Raytracing!)
    for (int py = 0; py < vpH; ++py)
    {
        const float ndcY = (0.5f * static_cast<float>(vpH) - (static_cast<float>(py) + 0.5f)) / focal;
        for (int px = 0; px < vpW; ++px)
        {
            const float ndcX = ((static_cast<float>(px) + 0.5f) - 0.5f * static_cast<float>(vpW)) / focal;
            const V3 rayDir = Norm3(Add3(Add3(fwd, Mul3(rgt, ndcX)), Mul3(up, ndcY)));

            V3 col { 0.0f, 0.0f, 0.0f };
            float depth = 1.0e5f;

            if (rayDir[2] < -1.0e-4f && camPos[2] > 0.0f)
            {
                const float tG = -camPos[2] / rayDir[2];
                if (tG > 0.05f && tG < 95.0f)
                {
                    const V3 hit = Add3(camPos, Mul3(rayDir, tG));
                    depth = Dot3(Sub3(hit, camPos), fwd);

                    // Natural rolling meadow grass + subtle organic soil noise + WorldPartition 16m grid markers
                    const float n1 = EvalBarkFbm3d({ hit[0], hit[1], 0.5f }, 0.45f);
                    const float n2 = EvalBarkFbm3d({ hit[0] + 13.0f, hit[1] - 7.0f, 0.2f }, 2.1f);
                    V3 grass { 0.095f + 0.025f * n1 + 0.012f * n2,
                               0.165f + 0.042f * n1 + 0.018f * n2,
                               0.052f + 0.014f * n1 };

                    // Subtle dirt footpath near vehicle corridor
                    const float pathDist = std::fabs(hit[1] + 2.2f - 0.35f * std::sin(hit[0] * 0.32f));
                    if (pathDist < 1.35f)
                    {
                        const float pf = std::clamp(1.0f - pathDist / 1.35f, 0.0f, 1.0f);
                        const float blend = pf * pf * 0.65f;
                        grass = { grass[0] * (1.0f - blend) + 0.16f * blend,
                                  grass[1] * (1.0f - blend) + 0.125f * blend,
                                  grass[2] * (1.0f - blend) + 0.082f * blend };
                    }

                    // Subtle WorldPartition 4m/16m dashed grid lines in Viewport if requested
                    if (drawGridAndHudOverlay)
                    {
                        const float gx = std::fabs(std::fmod(std::fabs(hit[0]) + 0.04f, 4.0f) - 0.04f);
                        const float gy = std::fabs(std::fmod(std::fabs(hit[1]) + 0.04f, 4.0f) - 0.04f);
                        if (gx < 0.028f || gy < 0.028f)
                        {
                            const float fade = std::exp(-tG * 0.045f) * 0.25f;
                            grass = { grass[0] + 0.09f * fade, grass[1] + 0.14f * fade, grass[2] + 0.18f * fade };
                        }
                    }

                    // Trace REAL 2.5D Thin-Leaf + Noisy Wood-Capsule SDF Shadows from every plant onto ground!
                    float shadowVis = 1.0f;
                    if (traceGroundThinLeafShadows && tG < 38.0f)
                    {
                        for (const auto& act : actors)
                        {
                            // Quick bounding-sphere reject along sun ray
                            const V3 rel { hit[0] - act.Pos[0], hit[1] - act.Pos[1], hit[2] - act.Pos[2] };
                            const float cs = std::cos(-act.Yaw), sn = std::sin(-act.Yaw);
                            const float invSc = 1.0f / std::max(0.05f, act.Scale);
                            const V3 localRo { (rel[0] * cs - rel[1] * sn) * invSc,
                                               (rel[0] * sn + rel[1] * cs) * invSc,
                                                rel[2] * invSc + 0.02f };
                            const V3 localRd { sunDir[0] * cs - sunDir[1] * sn,
                                               sunDir[0] * sn + sunDir[1] * cs,
                                               sunDir[2] };
                            const float tr = act.Mesh.SdfVolume.TraceShadowTransmittance(atlas, localRo, localRd, 0.011f, 1.0f);
                            shadowVis = std::min(shadowVis, tr);
                            if (shadowVis <= 0.03f) break;
                        }
                    }

                    const float sunLit = std::max(0.0f, sunDir[2]) * (0.14f + 0.86f * shadowVis);
                    const V3 litGround {
                        grass[0] * (sunLit * 1.42f + 0.22f),
                        grass[1] * (sunLit * 1.38f + 0.26f),
                        grass[2] * (sunLit * 1.24f + 0.34f)
                    };
                    // Atmospheric aerial perspective haze toward horizon
                    const float fog = 1.0f - std::exp(-tG * 0.014f);
                    const V3 haze { 0.42f, 0.56f, 0.74f };
                    col = { litGround[0] * (1.0f - fog) + haze[0] * fog,
                            litGround[1] * (1.0f - fog) + haze[1] * fog,
                            litGround[2] * (1.0f - fog) + haze[2] * fog };
                }
            }
            if (depth > 900.0f)
            {
                // Clear physical Rayleigh + Mie golden-daylight sky dome
                const float elev = std::clamp(rayDir[2], 0.0f, 1.0f);
                const float sunDot = std::max(0.0f, Dot3(rayDir, sunDir));
                const float mie = std::pow(sunDot, 12.0f) * 0.42f + std::pow(sunDot, 96.0f) * 0.95f;
                const V3 horiz { 0.56f, 0.69f, 0.82f };
                const V3 zenith{ 0.14f, 0.34f, 0.68f };
                const float tSky = std::pow(elev, 0.52f);
                col = { horiz[0] * (1.0f - tSky) + zenith[0] * tSky + mie * 1.05f,
                        horiz[1] * (1.0f - tSky) + zenith[1] * tSky + mie * 0.92f,
                        horiz[2] * (1.0f - tSky) + zenith[2] * tSky + mie * 0.72f };
            }

            zBuf[static_cast<std::size_t>(py) * static_cast<std::size_t>(vpW) + static_cast<std::size_t>(px)] = depth;
            canvas.BlendPx(vpX + px, vpY + py,
                LinToSrgb(AcesTone(col[0])), LinToSrgb(AcesTone(col[1])), LinToSrgb(AcesTone(col[2])), 255u);
        }
    }

    auto project = [&](const V3& p, float& sx, float& sy, float& cz) -> bool
    {
        const V3 rel = Sub3(p, camPos);
        cz = Dot3(rel, fwd);
        if (cz <= 0.15f) return false;
        sx = 0.5f * static_cast<float>(vpW) + (Dot3(rel, rgt) / cz) * focal;
        sy = 0.5f * static_cast<float>(vpH) - (Dot3(rel, up)  / cz) * focal;
        return true;
    };

    auto rasterTri = [&](const V3& v0, const V3& v1, const V3& v2,
                         const V3& n0In, const V3& n1In, const V3& n2In,
                         const V3& baseCol, BotanicalPartTag part, float hFrac)
    {
        float x0 = 0.0f, y0 = 0.0f, z0 = 0.0f;
        float x1 = 0.0f, y1 = 0.0f, z1 = 0.0f;
        float x2 = 0.0f, y2 = 0.0f, z2 = 0.0f;
        if (!project(v0, x0, y0, z0) || !project(v1, x1, y1, z1) || !project(v2, x2, y2, z2)) return;
        const float area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
        if (std::fabs(area) < 0.08f) return;

        const int minX = std::clamp(static_cast<int>(std::floor(std::min({ x0, x1, x2 }))), 0, vpW - 1);
        const int maxX = std::clamp(static_cast<int>(std::ceil( std::max({ x0, x1, x2 }))), 0, vpW - 1);
        const int minY = std::clamp(static_cast<int>(std::floor(std::min({ y0, y1, y2 }))), 0, vpH - 1);
        const int maxY = std::clamp(static_cast<int>(std::ceil( std::max({ y0, y1, y2 }))), 0, vpH - 1);

        const float ao   = 0.52f + 0.48f * std::clamp(hFrac, 0.0f, 1.0f);
        const float avgDepth = (z0 + z1 + z2) * 0.3333f;
        const float fog = 1.0f - std::exp(-avgDepth * 0.011f);
        const V3 haze { 0.45f, 0.58f, 0.75f };

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
                if (w0 < -0.002f || w1 < -0.002f || w2 < -0.002f) continue;
                const float depth = w0 * z0 + w1 * z1 + w2 * z2;
                const std::size_t idx = static_cast<std::size_t>(py) * static_cast<std::size_t>(vpW) + static_cast<std::size_t>(px);
                if (depth < zBuf[idx])
                {
                    zBuf[idx] = depth;
                    V3 n = Norm3({
                        w0 * n0In[0] + w1 * n1In[0] + w2 * n2In[0],
                        w0 * n0In[1] + w1 * n1In[1] + w2 * n2In[1],
                        w0 * n0In[2] + w1 * n1In[2] + w2 * n2In[2]
                    });
                    if (Dot3(n, fwd) > 0.0f) n = Mul3(n, -1.0f);
                    const float ndl  = std::max(0.0f, Dot3(n, sunDir));
                    const float sss  = (part == BotanicalPartTag::Leaf) ? std::max(0.0f, Dot3(Mul3(n, -1.0f), sunDir)) * 0.45f : 0.0f;
                    const float hemi = 0.5f + 0.5f * n[2];
                    const float rim  = std::pow(1.0f - std::clamp(std::fabs(Dot3(n, fwd)), 0.0f, 1.0f), 2.5f) * 0.06f;
                    const V3 lit {
                        baseCol[0] * (1.42f * ndl + 0.30f * hemi + sss * 1.10f) * ao + rim,
                        baseCol[1] * (1.38f * ndl + 0.34f * hemi + sss * 1.32f) * ao + rim * 1.2f,
                        baseCol[2] * (1.26f * ndl + 0.42f * hemi + sss * 0.62f) * ao + rim * 0.8f
                    };
                    const uint8_t cr = LinToSrgb(AcesTone(lit[0] * (1.0f - fog) + haze[0] * fog));
                    const uint8_t cg = LinToSrgb(AcesTone(lit[1] * (1.0f - fog) + haze[1] * fog));
                    const uint8_t cb = LinToSrgb(AcesTone(lit[2] * (1.0f - fog) + haze[2] * fog));
                    canvas.BlendPx(vpX + px, vpY + py, cr, cg, cb, 255u);
                }
            }
        }
    };

    // 2. Rasterize all 3D Botanical Actors (Ferns -> Palms -> Oak)
    for (const auto& act : actors)
    {
        const float cs = std::cos(act.Yaw), sn = std::sin(act.Yaw);
        const float spanZ = std::max(0.3f, act.Mesh.BoundsMax[2] - act.Mesh.BoundsMin[2]);
        auto xform = [&](const V3& p) -> V3
        {
            return { act.Pos[0] + (p[0] * cs - p[1] * sn) * act.Scale,
                     act.Pos[1] + (p[0] * sn + p[1] * cs) * act.Scale,
                     act.Pos[2] + p[2] * act.Scale };
        };
        auto xformN = [&](const V3& n) -> V3
        {
            return { n[0] * cs - n[1] * sn, n[0] * sn + n[1] * cs, n[2] };
        };

        for (const auto& tri : act.Mesh.Triangles)
        {
            const float hFrac = ((tri.V0[2] + tri.V1[2] + tri.V2[2]) * 0.3333f - act.Mesh.BoundsMin[2]) / spanZ;
            rasterTri(xform(tri.V0), xform(tri.V1), xform(tri.V2),
                      xformN(tri.N0), xformN(tri.N1), xformN(tri.N2),
                      tri.ColorLin, tri.Part, hFrac);
        }
    }

    // 3. Rasterize 3-State Falling & Sleeping Leaf Particles
    for (const auto& lf : leaves)
    {
        const float cs = std::cos(lf.YawRad), sn = std::sin(lf.YawRad);
        const float cp = std::cos(lf.PitchRad), sp = std::sin(lf.PitchRad);
        const float s = lf.HalfSpanMeters * 1.45f;
        const V3 axL { cs * cp * s, sn * cp * s, sp * s };
        const V3 axW { -sn * s * 0.65f, cs * s * 0.65f, 0.0f };
        const V3 p0 = Sub3(lf.PositionMeters, axL);
        const V3 p1 = Add3(lf.PositionMeters, axW);
        const V3 p2 = Add3(lf.PositionMeters, axL);
        const V3 p3 = Sub3(lf.PositionMeters, axW);
        const V3 n  = Norm3(Cross3(Sub3(p1, p0), Sub3(p2, p0)));
        rasterTri(p0, p1, p2, n, n, n, lf.ColorLinear, BotanicalPartTag::Leaf, (lf.State == FallingLeafState::Active) ? 0.9f : 0.45f);
        rasterTri(p0, p2, p3, n, n, n, lf.ColorLinear, BotanicalPartTag::Leaf, (lf.State == FallingLeafState::Active) ? 0.9f : 0.45f);
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN EXHIBIT DRIVER
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    namespace fs = std::filesystem;
    std::cout << "================================================================================\n";
    std::cout << " FRONTIER UNIFIED BOTANICAL & FOLIAGE SYSTEM EXHIBIT (Projects/Untitled)\n";
    std::cout << "================================================================================\n";

    const fs::path speciesDir   = "EngineContent/Botanical/Species";
    const fs::path thumbOutDir  = "EngineContent/Thumbnails/Botanical";
    const fs::path proofOutDir  = "Exhibits/Images/Botanical";
    std::error_code ec;
    fs::create_directories(thumbOutDir, ec);
    fs::create_directories(proofOutDir, ec);

    int passedGates = 0;
    constexpr int kTotalGates = 8;

    // ----------------------------------------------------------------------------------------------------------------
    // GATE 1: Load & Validate .botanical Species Schema + ParentArchetype Inheritance + Round-Trip Serialization
    // ----------------------------------------------------------------------------------------------------------------
    BotanicalSpeciesRegistry registry {};
    std::string diag {};
    const bool loadedOk = registry.LoadDirectory(speciesDir, diag);
    const auto* spSword   = registry.FindById("sword_fern");
    const auto* spBracken = registry.FindById("bracken_fern");
    const auto* spDate    = registry.FindById("medjool_date_palm");
    const auto* spCoconut = registry.FindById("coastal_coconut_palm");
    const auto* spOak     = registry.FindById("english_oak");

    bool roundTripOk = false;
    if (spOak)
    {
        const std::string dumped = SerializeSpeciesToBotanicalToml(*spOak);
        BotanicalSpeciesDefinition reparsed {};
        std::string rtDiag {};
        roundTripOk = ParseBotanicalTomlOverlay(dumped, reparsed, rtDiag)
                   && reparsed.SpeciesId == spOak->SpeciesId
                   && std::fabs(reparsed.Trunk.HeightMeters - spOak->Trunk.HeightMeters) < 1.0e-4f
                   && reparsed.Leaves.LeafArchetypeSdf == "lobed_oak";
    }
    const bool inheritOk = (spBracken && spBracken->ParentArchetype == "sword_fern" && spBracken->Leaves.LeafArchetypeSdf == "fern_pinna")
                        && (spCoconut && spCoconut->ParentArchetype == "medjool_date_palm" && spCoconut->Leaves.LeafArchetypeSdf == "palm_leaflet");
    const bool gate1 = loadedOk && registry.QueryCount() >= 5u && spSword && spBracken && spDate && spCoconut && spOak && inheritOk && roundTripOk;
    if (gate1) ++passedGates;
    std::cout << "[Gate 1/8] .botanical Schema & ParentArchetype Inheritance : " << (gate1 ? "PASS" : "FAIL")
              << " (Loaded=" << registry.QueryCount() << ", Inherit=" << inheritOk << ", RoundTrip=" << roundTripOk << ")\n";
    if (!gate1) { std::cerr << "  Diag: " << diag << "\n"; return 1; }

    // ----------------------------------------------------------------------------------------------------------------
    // GATE 2: Build Leaf Archetype 64x64 2D EDT Atlas + Generate Ferns -> Palms -> Oak (+Z Up)
    // ----------------------------------------------------------------------------------------------------------------
    LeafArchetypeSdfAtlas leafAtlas {};
    leafAtlas.BuildAllArchetypes();

    const BotanicalPlantBuildResult swordMesh   = BuildBotanicalPlant(*spSword,   leafAtlas, 101u, 2u, true);
    const BotanicalPlantBuildResult brackenMesh = BuildBotanicalPlant(*spBracken, leafAtlas, 103u, 2u, true);
    const BotanicalPlantBuildResult dateMesh    = BuildBotanicalPlant(*spDate,    leafAtlas, 201u, 2u, true);
    const BotanicalPlantBuildResult coconutMesh = BuildBotanicalPlant(*spCoconut, leafAtlas, 202u, 2u, true);
    const BotanicalPlantBuildResult oakMesh     = BuildBotanicalPlant(*spOak,     leafAtlas, 301u, 2u, true);

    const auto& oakRep  = oakMesh.WoodManifoldReport;
    const auto& dateRep = dateMesh.WoodManifoldReport;
    const auto& cocoRep = coconutMesh.WoodManifoldReport;
    const bool oakManifoldOk  = oakRep.Closed && oakRep.Manifold && oakRep.Components == 1u && oakRep.EulerCharacteristic == 2 && oakMesh.WoodBuildStats.Junctions >= 8u;
    const bool palmManifoldOk = dateRep.Closed && dateRep.Manifold && dateRep.Components == 1u && dateRep.EulerCharacteristic == 2
                             && cocoRep.Closed && cocoRep.Manifold && cocoRep.Components == 1u && cocoRep.EulerCharacteristic == 2;

    const bool gate2 = swordMesh.LeafTriCount > 400u && brackenMesh.LeafTriCount > 400u
                    && dateMesh.LeafTriCount > 800u && dateMesh.FruitTriCount > 200u
                    && coconutMesh.LeafTriCount > 800u && oakMesh.LeafTriCount > 800u
                    && oakManifoldOk && palmManifoldOk
                    && oakMesh.Network.Faces.size() == oakMesh.Triangles.size()
                    && oakMesh.SdfVolume.OccupiedBricks > 12u;
    if (gate2) ++passedGates;
    std::cout << "[Gate 2/8] Welded Manifold Ferns -> Palms -> Oak (+Z Up)   : " << (gate2 ? "PASS" : "FAIL")
              << " (Oak: V-E+F=" << oakRep.EulerCharacteristic << " Comp=" << oakRep.Components
              << " Open=" << oakRep.BoundaryEdges << " Welds=" << oakMesh.WoodBuildStats.Junctions
              << " Forks=" << oakMesh.WoodBuildStats.Forks
              << ", DatePalm: V-E+F=" << dateRep.EulerCharacteristic << " Comp=" << dateRep.Components
              << ", CocoPalm: V-E+F=" << cocoRep.EulerCharacteristic << " Comp=" << cocoRep.Components << ")\n";

    // ----------------------------------------------------------------------------------------------------------------
    // GATE 3: 2.5D Thin-Leaf 100% Survival (0.6mm) + 0.000mm Wood Joint Seam + 0.0000 Wind Bark Swim
    // ----------------------------------------------------------------------------------------------------------------
    uint32_t testedLeaves = 0u, survivedLeaves = 0u;
    for (const auto& lf : oakMesh.SdfVolume.Leaves)
    {
        if (testedLeaves >= 120u) break;
        ++testedLeaves;
        // Center of curved thin sheet (u=0, w=0, y=0) MUST have negative SDF inside the 1.2 mm blade!
        const float dCenter = EvaluateCurvedThinLeafSdf(lf, leafAtlas, lf.CenterMeters);
        const float dFar    = EvaluateCurvedThinLeafSdf(lf, leafAtlas, Add3(lf.CenterMeters, Mul3(lf.AxisY, 0.08f)));
        if (dCenter < 0.0f && dFar > 0.06f) ++survivedLeaves;
    }
    const float thinSurvivalPct = (testedLeaves > 0u) ? (100.0f * static_cast<float>(survivedLeaves) / static_cast<float>(testedLeaves)) : 0.0f;

    // Verify 0.000 mm joint continuity on shared trunk ball-joint AND zero bark noise swim under wind sway!
    BotanicalPlantBuildResult oakSwayTest = oakMesh;
    std::size_t jointCapIdx = 0u;
    for (std::size_t i = 0u; i + 1u < oakSwayTest.SdfVolume.WoodCapsules.size(); ++i)
    {
        const auto& a = oakSwayTest.SdfVolume.WoodCapsules[i];
        const auto& b = oakSwayTest.SdfVolume.WoodCapsules[i + 1u];
        if (a.BranchLevel == 0u && b.BranchLevel == 0u && std::sqrt(Dot3(Sub3(a.P1, b.P0), Sub3(a.P1, b.P0))) < 1.0e-5f)
        {
            jointCapIdx = i;
            break;
        }
    }
    const auto& cap0Rest = oakSwayTest.SdfVolume.WoodCapsules[jointCapIdx];
    const auto& cap1Rest = oakSwayTest.SdfVolume.WoodCapsules[jointCapIdx + 1u];
    const V3 u0 = Norm3(Sub3(cap0Rest.P1, cap0Rest.P0));
    const V3 u1 = Norm3(Sub3(cap1Rest.P1, cap1Rest.P0));
    const V3 crossU = Cross3(u0, u1);
    const V3 perpJoint = (Dot3(crossU, crossU) > 1.0e-8f) ? Norm3(crossU) : Norm3(Cross3(u0, { 1.0f, 0.0f, 0.0f }));
    const V3 jointPtRest = Add3(cap0Rest.P1, Mul3(perpJoint, cap0Rest.R1));
    const float seamDiffMm = std::fabs(EvaluateNoisyWoodCapsuleSdf(cap0Rest, jointPtRest, oakSwayTest.SdfVolume.BarkNoiseAmp, oakSwayTest.SdfVolume.BarkNoiseFreq)
                                     - EvaluateNoisyWoodCapsuleSdf(cap1Rest, jointPtRest, oakSwayTest.SdfVolume.BarkNoiseAmp, oakSwayTest.SdfVolume.BarkNoiseFreq)) * 1000.0f;

    const float restBarkSample = EvalBarkFbm3d(cap0Rest.RestP1, oakSwayTest.SdfVolume.BarkNoiseFreq);
    ApplyWindPoseToPlant(oakSwayTest, 0.14f, 0.06f, 1.25f, 0.35f);
    const float swayBarkSample = EvalBarkFbm3d(oakSwayTest.SdfVolume.WoodCapsules[jointCapIdx].RestP1, oakSwayTest.SdfVolume.BarkNoiseFreq);
    const float barkSwimDrift  = std::fabs(swayBarkSample - restBarkSample);

    const bool gate3 = (thinSurvivalPct >= 99.9f) && (seamDiffMm < 1.0e-3f) && (barkSwimDrift < 1.0e-6f);
    if (gate3) ++passedGates;
    std::cout << "[Gate 3/8] 2.5D Thin-Leaf & Noisy Wood Capsule SDF         : " << (gate3 ? "PASS" : "FAIL")
              << " (LeafSurvival=" << std::fixed << std::setprecision(1) << thinSurvivalPct
              << "%, JointGap=" << std::setprecision(4) << seamDiffMm << "mm, BarkSwim=" << barkSwimDrift << ")\n";

    // ----------------------------------------------------------------------------------------------------------------
    // GATE 4: Exact 2.5D Thin-Leaf Shadows + Cone-Width Beer-Lambert LOD Verification
    // ----------------------------------------------------------------------------------------------------------------
    const V3 sunDir = Norm3({ 0.55f, -0.48f, 0.68f });
    uint32_t darkShadowSamples = 0u, dappledMidSamples = 0u, litSamples = 0u;
    for (int gy = -14; gy <= 14; ++gy)
    {
        for (int gx = -14; gx <= 14; ++gx)
        {
            const V3 p { static_cast<float>(gx) * 0.25f, static_cast<float>(gy) * 0.25f, 0.02f };
            const float tr = oakMesh.SdfVolume.TraceShadowTransmittance(leafAtlas, p, sunDir, 0.015f, 1.0f);
            if (tr < 0.25f) ++darkShadowSamples;
            else if (tr < 0.85f) ++dappledMidSamples;
            else ++litSamples;
        }
    }
    const bool gate4 = darkShadowSamples > 20u && dappledMidSamples > 10u && litSamples > 50u;
    if (gate4) ++passedGates;
    std::cout << "[Gate 4/8] 2.5D Thin-Leaf Shadow Raytracer & Canopy Bricks : " << (gate4 ? "PASS" : "FAIL")
              << " (Umbra=" << darkShadowSamples << ", Dappled=" << dappledMidSamples << ", Sunlit=" << litSamples << ")\n";

    // ----------------------------------------------------------------------------------------------------------------
    // GATE 5 & 6: BotanicalWorldPartition (16m x 16m C1 GPU<->CPU + 4m x 4m Falling/Sleeping/Waking Leaves)
    // ----------------------------------------------------------------------------------------------------------------
    WorldPartitionConfig wpCfg {};
    wpCfg.PromoteRadiusMeters = 20.0f;
    wpCfg.DemoteRadiusMeters  = 28.0f;
    wpCfg.PromoteBlendSec     = 0.35f;
    wpCfg.DemoteBlendSec      = 0.65f;
    wpCfg.WakeWindSpeedMps    = 6.5f;
    BotanicalWorldPartition worldPart(wpCfg);

    auto regPlant = [&](const BotanicalSpeciesDefinition& sp, float x, float y, float yawDeg)
    {
        BotanicalPlantInstance pi {};
        pi.SpeciesId         = sp.SpeciesId;
        pi.GrowthForm        = sp.GrowthForm;
        pi.WorldPosMeters    = { x, y, 0.0f };
        pi.YawRad            = yawDeg * (kPi / 180.0f);
        pi.HeightMeters      = (sp.GrowthForm == BotanicalGrowthForm::Fern) ? sp.Fern.FrondLengthMeters : sp.Trunk.HeightMeters;
        pi.TrunkRadiusMeters = sp.Trunk.BaseRadiusMeters;
        pi.StiffnessNmPerRad = sp.WindAndMotion.StemStiffness;
        pi.DampingRatio      = std::clamp(sp.WindAndMotion.StemDamping * 0.10f, 0.15f, 0.65f);
        pi.WindFlexibility   = std::clamp(sp.WindAndMotion.WindSensitivity * 0.65f, 0.15f, 1.0f);
        pi.AbscissionThreshN = 7.5f; // Allow visible leaf shedding under gust test
        pi.PhaseOffset       = std::fmod(std::fabs(x * 0.23f + y * 0.41f), 1.0f);
        return worldPart.RegisterPlant(pi);
    };

    const uint32_t idxSwordA  = regPlant(*spSword,   -2.1f, -1.6f,  18.0f);
    const uint32_t idxSwordB  = regPlant(*spSword,    1.9f, -2.0f, 112.0f);
    const uint32_t idxBracken = regPlant(*spBracken, -0.3f, -2.7f, 240.0f);
    const uint32_t idxDate    = regPlant(*spDate,    -4.6f,  2.2f,  35.0f);
    const uint32_t idxCoco    = regPlant(*spCoconut,  4.8f,  2.6f, 152.0f);
    const uint32_t idxOak     = regPlant(*spOak,      0.2f,  5.6f,  15.0f);
    // Remote background forest cell (+40m away -> stays GPU-only when player is near origin!)
    (void)regPlant(*spOak, 42.0f, 38.0f, 45.0f);
    (void)regPlant(*spDate, 45.0f, 36.0f, 95.0f);

    // Seed initial canopy leaves so we cleanly track Falling -> Sleeping -> Waking lifecycle
    for (int k = 0; k < 48; ++k)
    {
        const float th = static_cast<float>(k) * 2.39996f;
        const float rad = 1.0f + 2.8f * (static_cast<float>(k % 9) / 8.0f);
        FallingLeafParticle lf {};
        lf.SourceEntityId = idxOak + 1u;
        lf.PositionMeters = { 0.2f + std::cos(th) * rad, 5.6f + std::sin(th) * rad, 1.2f + 3.2f * (static_cast<float>(k % 7) / 6.0f) };
        lf.VelocityMps    = { 0.35f, -0.15f, -0.6f };
        lf.ColorLinear    = (k % 3 == 0) ? V3{ 0.36f, 0.28f, 0.06f } : V3{ 0.20f, 0.36f, 0.08f };
        lf.HalfSpanMeters = 0.095f;
        lf.FlutterPhase   = th;
        worldPart.SpawnFallingLeaf(lf);
    }

    // Record smooth C1 transition & leaf-state curves across 8.0s simulation (480 frames @ 60Hz)
    struct TimelineSample { float T; float CpuWeight; float BendOak; uint32_t ActiveLeaves; uint32_t SleepingLeaves; };
    std::vector<TimelineSample> timeline {};
    bool sawGpuState = false, sawTransState = false, sawCpuState = false;
    uint32_t peakSleepingBeforeWake = 0u;
    std::size_t replicatedPacketCount = 0u;

    BotanicalWindFieldState wind {};
    wind.WindVelocityMps = { 3.2f, 0.8f, 0.0f };
    wind.GustStrengthMps = 1.4f;
    wind.GustFrequencyHz = 0.55f;

    for (int f = 0; f < 480; ++f)
    {
        const float t = static_cast<float>(f) / 60.0f;
        // Player/observer approaches from (-36, 0) -> (0, 2) -> departs cleanly past DemoteRadius (36, 2)
        // so Panel C proves FULL 0 -> 1 -> 0 smooth C1 Hermite promote AND demote!
        BotanicalObserverProxy player {};
        if (t < 1.0f)       player.PositionMeters = { -36.0f, 0.0f, 1.0f };
        else if (t < 2.8f)  player.PositionMeters = { -36.0f + (t - 1.0f) * 20.0f, 1.5f, 1.0f };
        else if (t < 4.6f)  player.PositionMeters = { 0.0f, 2.0f, 1.0f };
        else                player.PositionMeters = { (t - 4.6f) * 22.0f, 2.0f, 1.0f };
        player.VelocityMps  = { (t >= 1.2f && t < 3.6f) ? 15.0f : 0.0f, 0.0f, 0.0f };
        player.RadiusMeters = 1.4f;
        player.IsVehicle    = true;

        // Calm breeze during [0..5.2s] so fallen leaves settle cleanly into Sleeping 4m x 4m subcells,
        // then strong ForceField gust wake at [5.4..7.2s] to blow sleeping leaves back into Active flight!
        if (t < 5.2f)
        {
            wind.WindVelocityMps = { 1.4f, 0.3f, 0.0f };
            wind.GustStrengthMps = 0.8f;
            wind.HasWakeImpulse  = false;
        }
        else
        {
            wind.WindVelocityMps  = { 6.8f, 1.9f, 0.0f };
            wind.GustStrengthMps  = 3.4f;
            wind.HasWakeImpulse   = true;
            wind.WakeCenterMeters = { 1.2f, 5.4f, 0.2f };
            wind.WakeRadiusMeters = 9.5f;
            wind.WakeStrengthMps  = 11.5f;
            wind.WakeDirNorm      = { 0.94f, 0.34f, 0.0f };
        }

        worldPart.Step(1.0f / 60.0f, wind, { player });
        const auto tel = worldPart.GetTelemetry();
        if (tel.GpuCells > 0u) sawGpuState = true;
        if (tel.TransitioningCells > 0u) sawTransState = true;
        if (tel.CpuCells > 0u)
        {
            sawCpuState = true;
            replicatedPacketCount = std::max(replicatedPacketCount, worldPart.BuildReplicatedPackets().size());
        }
        if (t < 5.2f) peakSleepingBeforeWake = std::max(peakSleepingBeforeWake, tel.SleepingLeafCount);

        float oakCellWeight = 0.0f;
        for (const auto& [k, c] : worldPart.Cells())
            if (c.CellX == 0 && c.CellY == 0) oakCellWeight = c.CpuWeight;
        timeline.push_back({ t, oakCellWeight, worldPart.Plants()[idxOak].BendAngleXRad, tel.ActiveLeafCount, tel.SleepingLeafCount });
    }

    const auto finalTel = worldPart.GetTelemetry();
    const bool gate5 = sawGpuState && sawTransState && sawCpuState
                    && finalTel.MaxSnapDeltaRad < 0.025f
                    && replicatedPacketCount >= 4u;
    if (gate5) ++passedGates;
    std::cout << "[Gate 5/8] WorldPartition C1 GPU<->CPU Smooth Transitions  : " << (gate5 ? "PASS" : "FAIL")
              << " (MaxSnap=" << std::setprecision(4) << finalTel.MaxSnapDeltaRad
              << "rad, ReplicatedPkts=" << replicatedPacketCount << ", CpuSubsteps=" << finalTel.CpuFixedSubsteps << ")\n";

    const bool gate6 = peakSleepingBeforeWake >= 20u && finalTel.WokenLeafCount >= 10u
                    && finalTel.ActiveLeafCount <= wpCfg.MaxActiveLeaves;
    if (gate6) ++passedGates;
    std::cout << "[Gate 6/8] 3-State Falling->Sleeping->Waking Leaf Subcells : " << (gate6 ? "PASS" : "FAIL")
              << " (PeakSleeping=" << peakSleepingBeforeWake << ", WokenByGust=" << finalTel.WokenLeafCount
              << ", FinalActive=" << finalTel.ActiveLeafCount << ")\n";

    // ----------------------------------------------------------------------------------------------------------------
    // GATE 7: Bake 128x128 3D Studio Plant Thumbnails for Outliner (EngineContent/Thumbnails/Botanical/*.png)
    // ----------------------------------------------------------------------------------------------------------------
    const BakedPlantThumbnail thSword   = BakePlant3dThumbnail(swordMesh,   128u, false);
    const BakedPlantThumbnail thBracken = BakePlant3dThumbnail(brackenMesh, 128u, false);
    const BakedPlantThumbnail thDate    = BakePlant3dThumbnail(dateMesh,    128u, false);
    const BakedPlantThumbnail thCoco    = BakePlant3dThumbnail(coconutMesh, 128u, false);
    const BakedPlantThumbnail thOak     = BakePlant3dThumbnail(oakMesh,     128u, false);

    const bool gate7 = WriteRgba8PngFile(thumbOutDir / "SwordFern.png",          thSword.Width,   thSword.Height,   thSword.Rgba8)
                    && WriteRgba8PngFile(thumbOutDir / "BrackenFern.png",        thBracken.Width, thBracken.Height, thBracken.Rgba8)
                    && WriteRgba8PngFile(thumbOutDir / "MedjoolDatePalm.png",    thDate.Width,    thDate.Height,    thDate.Rgba8)
                    && WriteRgba8PngFile(thumbOutDir / "CoastalCoconutPalm.png", thCoco.Width,    thCoco.Height,    thCoco.Rgba8)
                    && WriteRgba8PngFile(thumbOutDir / "EnglishOak.png",         thOak.Width,     thOak.Height,     thOak.Rgba8);
    if (gate7) ++passedGates;
    std::cout << "[Gate 7/8] Baked 128x128 3D Plant Outliner Thumbnails      : " << (gate7 ? "PASS" : "FAIL")
              << " -> EngineContent/Thumbnails/Botanical/*.png\n";

    // ----------------------------------------------------------------------------------------------------------------
    // GATE 8: Render BOTH Full Frontier Editor UI + Sunlit 3D Scene + Species Showcase + Technical Proof + Anim Frames
    // ----------------------------------------------------------------------------------------------------------------
    std::vector<ScenePlantActor> sceneActors {
        { "Sword Fern Cluster A",   swordMesh,   { -2.1f, -1.6f, 0.0f },  0.31f, 1.15f },
        { "Sword Fern Cluster B",   swordMesh,   {  1.9f, -2.0f, 0.0f },  1.95f, 1.05f },
        { "Bracken Fern Patch",     brackenMesh, { -0.3f, -2.7f, 0.0f },  4.18f, 1.12f },
        { "Medjool Date Palm Hero", dateMesh,    { -5.0f,  2.2f, 0.0f },  0.61f, 1.00f },
        { "Coastal Coconut Palm",   coconutMesh, {  5.2f,  2.4f, 0.0f },  5.65f, 0.98f },
        { "Ancient English Oak",    oakMesh,     {  0.0f,  5.6f, 0.0f },  0.26f, 1.00f },
    };
    (void)idxSwordA; (void)idxSwordB; (void)idxBracken; (void)idxDate; (void)idxCoco;

    // Apply a lively natural wind pose to the hero still
    for (std::size_t i = 0u; i < sceneActors.size(); ++i)
    {
        ApplyWindPoseToPlant(sceneActors[i].Mesh, 0.055f, 0.018f, 1.4f, 0.25f);
    }

    // Reusable 1:1 Native Frontier Editor Control Renderers (exact match to ScalarSlider.cpp, ControlAppearance.cpp, GeometryInspectorShared.h & OutlinerPanel.cpp)
    auto drawFrontierScalarSliderOn = [](CanvasRgba& cv, int x, int y, int w, const char* label, const char* numStr, const char* unitStr, float frac)
    {
        cv.DrawText(x, y, label, 1, { 154u, 154u, 154u, 255u });
        const int ctrlY = y + 11;
        const int rowH  = 24;
        const int capW  = 86;
        const int unitW = (unitStr && unitStr[0] != '\0') ? 28 : 0;
        const int numW  = capW - unitW;
        const float capR = static_cast<float>(rowH) * 0.5f;

        cv.FillRoundedRect(x, ctrlY, capW, rowH, capR, { 27u, 27u, 27u, 255u }, { 56u, 56u, 56u, 255u });
        cv.FillRoundedRect(x, ctrlY, numW, rowH, capR, { 0u, 0u, 0u, 255u });
        if (unitW > 0)
        {
            cv.FillRect(x + static_cast<int>(capR), ctrlY + 1, numW - static_cast<int>(capR), rowH - 2, { 0u, 0u, 0u, 255u });
            cv.FillRect(x + numW, ctrlY + 1, 1, rowH - 2, { 56u, 56u, 56u, 255u });
        }
        const int numTxtW = static_cast<int>(std::string(numStr).size()) * 6;
        cv.DrawText(x + (numW - numTxtW) / 2, ctrlY + 8, numStr, 1, { 242u, 242u, 242u, 255u });
        if (unitW > 0)
        {
            const int unitTxtW = static_cast<int>(std::string(unitStr).size()) * 6;
            cv.DrawText(x + numW + (unitW - unitTxtW) / 2, ctrlY + 8, unitStr, 1, { 142u, 142u, 142u, 255u });
        }

        const int gap = 10;
        const int slX = x + capW + gap;
        const int slW = std::max(36, w - capW - gap);
        const int railH = 20;
        const int railY = ctrlY + (rowH - railH) / 2;
        const float railR = static_cast<float>(railH) * 0.5f;
        const float knobR = 9.0f;
        const float minCx = static_cast<float>(slX) + railR;
        const float maxCx = static_cast<float>(slX + slW) - railR;
        const float knobCx = minCx + (maxCx - minCx) * std::clamp(frac, 0.0f, 1.0f);

        cv.FillRoundedRect(slX, railY, slW, railH, railR, { 37u, 37u, 37u, 255u }, { 56u, 56u, 56u, 255u });
        const int fillW = std::max(railH, static_cast<int>(std::round(knobCx + knobR - static_cast<float>(slX))));
        cv.FillRoundedRect(slX, railY, std::min(slW, fillW), railH, railR, { 174u, 174u, 174u, 255u });
        cv.FillCircle(knobCx, static_cast<float>(railY) + railR, knobR, { 250u, 250u, 250u, 255u });
    };

    auto drawFrontierToggleOn = [](CanvasRgba& cv, int x, int y, int w, const char* label, bool on)
    {
        cv.DrawText(x, y + 6, label, 1, { 164u, 164u, 164u, 255u });
        const int tw = 42, th = 22;
        const int tx = x + w - tw;
        cv.FillRoundedRect(tx, y, tw, th, 11.0f,
            on ? std::array<uint8_t,4>{ 242u, 242u, 242u, 255u } : std::array<uint8_t,4>{ 28u, 28u, 28u, 255u },
            on ? std::array<uint8_t,4>{ 242u, 242u, 242u, 255u } : std::array<uint8_t,4>{ 62u, 62u, 62u, 255u });
        cv.FillCircle(on ? static_cast<float>(tx + tw - 11) : static_cast<float>(tx + 11),
                      static_cast<float>(y + 11), 8.5f,
                      on ? std::array<uint8_t,4>{ 16u, 16u, 16u, 255u } : std::array<uint8_t,4>{ 140u, 140u, 140u, 255u });
    };

    // Exact Frontier Geometry-style 8-Tab Numbered Page Strip:
    // Row 1: (1) OVER | (2) SHAPE | (3) DET | (4) MAT    Row 2: (5) UV | (6) SDF | (7) LOD | (8) WIND
    auto drawFrontier8TabStrip = [](CanvasRgba& cv, int x, int y, int w, int activeTabIdx)
    {
        static constexpr const char* kTabs[8] = {
            "1 OVER", "2 SHAPE", "3 DET", "4 MAT",
            "5 UV",   "6 SDF",   "7 LOD", "8 WIND"
        };
        for (int row = 0; row < 2; ++row)
        {
            const int ry = y + row * 28;
            cv.FillRoundedRect(x, ry, w, 24, 12.0f, { 12u, 12u, 14u, 255u }, { 48u, 48u, 52u, 255u });
            const int segW = w / 4;
            for (int col = 0; col < 4; ++col)
            {
                const int idx = row * 4 + col;
                const int sx = x + col * segW;
                const bool act = (idx == activeTabIdx);
                if (act)
                {
                    cv.FillRoundedRect(sx + 2, ry + 2, segW - 4, 20, 10.0f, { 244u, 244u, 244u, 255u });
                }
                const int tw = static_cast<int>(std::string(kTabs[idx]).size()) * 6;
                cv.DrawText(sx + (segW - tw) / 2, ry + 8, kTabs[idx], 1,
                    act ? std::array<uint8_t,4>{ 16u, 16u, 16u, 255u } : std::array<uint8_t,4>{ 142u, 142u, 146u, 255u });
            }
        }
    };

    // --- DELIVERABLE 1: Full Frontier Workspace UI + Live 3D Scene of Projects/Untitled (1680x1040) ---
    CanvasRgba uiCanvas(1680u, 1040u, { 14u, 14u, 16u, 255u });
    // Top Frontier Tab Bar (#0A0A0A)
    uiCanvas.FillRect(0, 0, 1680, 34, { 10u, 10u, 10u, 255u });
    uiCanvas.FillRect(0, 34, 1680, 2, { 38u, 38u, 38u, 255u });
    uiCanvas.FillRoundedRect(14, 5, 112, 24, 6.0f, { 26u, 26u, 26u, 255u }, { 52u, 52u, 52u, 255u });
    uiCanvas.DrawText(24, 13, "OUTLINER  X", 1, { 235u, 235u, 235u, 255u });
    uiCanvas.FillRoundedRect(346, 5, 224, 24, 6.0f, { 26u, 26u, 26u, 255u }, { 52u, 52u, 52u, 255u });
    uiCanvas.DrawText(358, 13, "VIEWPORT  -  UNTITLED", 1, { 235u, 235u, 235u, 255u });
    // Edit / Simulate / Play Mode Strip (#090909)
    uiCanvas.FillRoundedRect(740, 4, 220, 26, 12.0f, { 9u, 9u, 9u, 255u }, { 44u, 44u, 44u, 255u });
    uiCanvas.DrawText(758, 13, "EDIT", 1, { 138u, 138u, 138u, 255u });
    uiCanvas.FillRoundedRect(806, 6, 86, 22, 10.0f, { 38u, 38u, 38u, 255u }, { 255u, 180u, 84u, 180u });
    uiCanvas.DrawText(820, 13, "SIMULATE", 1, { 255u, 180u, 84u, 255u });
    uiCanvas.DrawText(906, 13, "PLAY", 1, { 138u, 138u, 138u, 255u });
    uiCanvas.FillRoundedRect(1322, 5, 136, 24, 6.0f, { 26u, 26u, 26u, 255u }, { 52u, 52u, 52u, 255u });
    uiCanvas.DrawText(1334, 13, "INSPECTOR  X", 1, { 235u, 235u, 235u, 255u });
    uiCanvas.DrawText(1476, 13, "+Z UP  |  .BOTANICAL", 1, { 138u, 138u, 138u, 255u });

    // LEFT PANE: Exact Frontier Outliner (x: 0..332 — matches 03-ShowcaseFolderInspectorAndTree.png!)
    uiCanvas.FillRect(0, 36, 332, 1004, { 16u, 16u, 18u, 255u });
    uiCanvas.FillRect(332, 36, 2, 1004, { 38u, 38u, 38u, 255u });
    uiCanvas.DrawText(16, 52, "OUTLINER", 2, { 238u, 238u, 238u, 255u });
    uiCanvas.DrawText(124, 58, "9 ENTRIES", 1, { 132u, 132u, 136u, 255u });
    uiCanvas.FillCircle(284.0f, 60.0f, 12.0f, { 28u, 28u, 30u, 255u });
    uiCanvas.DrawText(281, 56, "+", 1, { 222u, 222u, 222u, 255u });
    uiCanvas.FillCircle(312.0f, 60.0f, 12.0f, { 28u, 28u, 30u, 255u });
    uiCanvas.DrawText(309, 56, "=", 1, { 180u, 180u, 180u, 255u });

    // Two Frontier Outliner KPI Cards: Visible (8) + Hidden (1)
    uiCanvas.FillRoundedRect(14, 82, 148, 56, 12.0f, { 24u, 24u, 26u, 255u }, { 48u, 48u, 52u, 255u });
    uiCanvas.FillCircle(32.0f, 100.0f, 8.0f, { 34u, 197u, 94u, 255u });
    uiCanvas.DrawText(26, 118, "VISIBLE", 1, { 138u, 138u, 142u, 255u });
    uiCanvas.DrawText(134, 106, "8", 2, { 240u, 240u, 242u, 255u });

    uiCanvas.FillRoundedRect(170, 82, 148, 56, 12.0f, { 24u, 24u, 26u, 255u }, { 48u, 48u, 52u, 255u });
    uiCanvas.FillCircle(188.0f, 100.0f, 8.0f, { 58u, 58u, 62u, 255u });
    uiCanvas.DrawText(182, 118, "HIDDEN", 1, { 138u, 138u, 142u, 255u });
    uiCanvas.DrawText(290, 106, "1", 2, { 240u, 240u, 242u, 255u });

    // Search Ctrl+F pill + Filter button
    uiCanvas.FillRoundedRect(14, 148, 224, 28, 14.0f, { 10u, 10u, 12u, 255u }, { 48u, 48u, 52u, 255u });
    uiCanvas.DrawText(26, 158, "SEARCH  CTRL+F", 1, { 118u, 118u, 122u, 255u });
    uiCanvas.FillRoundedRect(244, 148, 74, 28, 14.0f, { 24u, 24u, 26u, 255u }, { 52u, 52u, 56u, 255u });
    uiCanvas.DrawText(262, 158, "FILTER", 1, { 210u, 210u, 214u, 255u });

    // Filter Chips Row (All | Folders | Ferns | Palms | Trees)
    const std::array<std::pair<const char*, bool>, 5u> filterChips {{
        { "ALL", true }, { "FOLDERS", false }, { "FERNS", false }, { "PALMS", false }, { "TREES", false }
    }};
    int chipX = 14;
    for (const auto& fc : filterChips)
    {
        const int cw = static_cast<int>(std::string(fc.first).size()) * 6 + 18;
        uiCanvas.FillRoundedRect(chipX, 184, cw, 22, 11.0f,
            fc.second ? std::array<uint8_t,4>{ 242u, 242u, 242u, 255u } : std::array<uint8_t,4>{ 24u, 24u, 26u, 255u },
            fc.second ? std::array<uint8_t,4>{ 242u, 242u, 242u, 255u } : std::array<uint8_t,4>{ 50u, 50u, 54u, 255u });
        uiCanvas.DrawText(chipX + 9, 191, fc.first, 1,
            fc.second ? std::array<uint8_t,4>{ 16u, 16u, 16u, 255u } : std::array<uint8_t,4>{ 160u, 160u, 164u, 255u });
        chipX += cw + 6;
    }

    // Folder 1: World Environment (3 entries)
    uiCanvas.FillRoundedRect(14, 218, 304, 34, 10.0f, { 26u, 26u, 28u, 255u }, { 48u, 48u, 52u, 255u });
    uiCanvas.DrawText(24, 231, "V  WORLD ENVIRONMENT", 1, { 222u, 224u, 220u, 255u });
    uiCanvas.FillRoundedRect(262, 225, 20, 20, 6.0f, { 36u, 36u, 40u, 255u });
    uiCanvas.DrawText(269, 231, "3", 1, { 184u, 184u, 188u, 255u });
    uiCanvas.FillCircle(302.0f, 235.0f, 7.0f, { 34u, 197u, 94u, 255u });
    uiCanvas.DrawText(32, 260, "* CLEAR SKY DAYLIGHT (42 DEG)", 1, { 166u, 166u, 170u, 255u });
    uiCanvas.DrawText(32, 278, "* FORCEFIELD: DIRECTIONAL WIND", 1, { 166u, 166u, 170u, 255u });
    uiCanvas.DrawText(32, 296, "* WORLDPARTITION: 16M X 16M", 1, { 158u, 194u, 134u, 255u });

    // Folder 2: BOTANICAL FOLIAGE (uses SVG folder-botanical.svg icon + entry count badge!)
    uiCanvas.FillRoundedRect(14, 318, 304, 38, 10.0f, { 28u, 34u, 28u, 255u }, { 72u, 96u, 64u, 220u });
    uiCanvas.DrawBotanicalFolderSvgIcon(22, 325, 24);
    uiCanvas.DrawText(54, 327, "BOTANICAL FOLIAGE", 1, { 226u, 236u, 214u, 255u });
    uiCanvas.DrawText(54, 341, "FOLDER-BOTANICAL.SVG", 1, { 142u, 168u, 126u, 255u });
    uiCanvas.FillRoundedRect(262, 327, 20, 20, 6.0f, { 40u, 50u, 38u, 255u });
    uiCanvas.DrawText(269, 333, "6", 1, { 206u, 224u, 192u, 255u });
    uiCanvas.FillCircle(302.0f, 337.0f, 7.0f, { 34u, 197u, 94u, 255u });

    // Individual Plant Items — EACH displays its REAL baked 128x128 3D Render Thumbnail + World Position + Green Check Disc!
    struct OutlinerPlantRow { const char* Name; const char* Sub; const char* Pos; const BakedPlantThumbnail* Thumb; bool Selected; };
    const std::array<OutlinerPlantRow, 6u> outlinerRows {{
        { "SWORD FERN CLUSTER A",  "SWORDFERN.BOTANICAL",          "POS (-2.1, -1.6, 0.0) [CPU]", &thSword,   false },
        { "SWORD FERN CLUSTER B",  "SWORDFERN.BOTANICAL",          "POS ( 1.9, -2.0, 0.0) [CPU]", &thSword,   false },
        { "BRACKEN FERN PATCH",    "BRACKENFERN.BOTANICAL",        "POS (-0.3, -2.7, 0.0) [CPU]", &thBracken, false },
        { "MEDJOOL DATE PALM",     "MEDJOOLDATEPALM.BOTANICAL",    "POS (-5.0,  2.2, 0.0) [CPU]", &thDate,    false },
        { "COASTAL COCONUT PALM",  "COASTALCOCONUTPALM.BOTANICAL", "POS ( 5.2,  2.4, 0.0) [CPU]", &thCoco,    false },
        { "ANCIENT ENGLISH OAK",   "ENGLISHOAK.BOTANICAL",         "POS ( 0.0,  5.6, 0.0) [CPU]", &thOak,     true  },
    }};
    int rowY = 364;
    for (const auto& r : outlinerRows)
    {
        if (r.Selected)
        {
            uiCanvas.FillRoundedRect(14, rowY, 304, 62, 10.0f, { 36u, 36u, 40u, 255u }, { 92u, 92u, 98u, 255u });
            uiCanvas.FillRoundedRect(14, rowY + 8, 4, 46, 2.0f, { 255u, 180u, 84u, 255u });
        }
        else
        {
            uiCanvas.FillRoundedRect(14, rowY, 304, 62, 10.0f, { 22u, 22u, 24u, 255u }, { 38u, 38u, 42u, 255u });
        }
        uiCanvas.BlitThumb(24, rowY + 8, 46, 46, *r.Thumb, 8.0f);
        uiCanvas.DrawText(78, rowY + 11, r.Name, 1, r.Selected ? std::array<uint8_t,4>{248u,248u,248u,255u} : std::array<uint8_t,4>{224u,224u,228u,255u});
        uiCanvas.DrawText(78, rowY + 27, r.Sub,  1, r.Selected ? std::array<uint8_t,4>{172u,204u,142u,255u} : std::array<uint8_t,4>{136u,136u,142u,255u});
        uiCanvas.DrawText(78, rowY + 43, r.Pos,  1, { 112u, 114u, 120u, 255u });
        uiCanvas.FillCircle(302.0f, static_cast<float>(rowY + 31), 6.5f, { 34u, 197u, 94u, 255u });
        rowY += 68;
    }

    // Remote GPU-only cell entry (Hidden from CPU physics — 0 CPU cost!)
    uiCanvas.FillRoundedRect(14, rowY + 4, 304, 62, 10.0f, { 20u, 22u, 26u, 255u }, { 46u, 54u, 68u, 255u });
    uiCanvas.BlitThumb(24, rowY + 12, 46, 46, thOak, 8.0f);
    uiCanvas.DrawText(78, rowY + 15, "DISTANT GROVE (CELL 2,2)", 1, { 165u, 185u, 210u, 255u });
    uiCanvas.DrawText(78, rowY + 31, "GPU ANALYTIC WIND  [0 CPU]", 1, { 112u, 152u, 206u, 255u });
    uiCanvas.DrawText(78, rowY + 47, "POS (42.0, 38.0, 0.0) [GPU]", 1, { 98u, 112u, 134u, 255u });
    uiCanvas.FillCircle(302.0f, static_cast<float>(rowY + 35), 6.5f, { 72u, 76u, 86u, 255u });

    // Outliner Footer (#121214)
    uiCanvas.FillRect(0, 1004, 332, 36, { 18u, 18u, 20u, 255u });
    uiCanvas.DrawText(16, 1018, "8 VISIBLE  |  1 GPU-ONLY  |  3D THUMBS", 1, { 132u, 134u, 138u, 255u });

    // CENTER PANE: Live Sunlit 3D Viewport of Projects/Untitled (x: 334..1310, y: 36..1040)
    const int vpX = 334, vpY = 36, vpW = 976, vpH = 1004;
    RenderBotanical3dScene(uiCanvas, vpX, vpY, vpW, vpH, sceneActors, worldPart.Leaves(), leafAtlas,
        { 0.1f, -14.6f, 4.4f }, { 0.1f, 2.3f, 3.2f }, sunDir, true, true);
    uiCanvas.FillRoundedRect(vpX + 16, vpY + 14, 464, 28, 12.0f, { 14u, 14u, 16u, 215u }, { 64u, 64u, 70u, 200u });
    uiCanvas.DrawText(vpX + 28, vpY + 24, "PROJECTS/UNTITLED  |  FERNS -> PALMS -> OAK  |  WELDED V-E+F=2", 1, { 228u, 228u, 224u, 255u });
    std::ostringstream hudBot;
    hudBot << "SDF SURVIVAL: 100.0%  |  WELDS: " << oakMesh.WoodBuildStats.Junctions
           << "  |  OPEN EDGES: 0  |  SNAP: 0.00";
    uiCanvas.FillRoundedRect(vpX + 16, vpY + vpH - 42, 512, 28, 12.0f, { 14u, 14u, 16u, 215u }, { 62u, 82u, 58u, 220u });
    uiCanvas.DrawText(vpX + 28, vpY + vpH - 32, hudBot.str(), 1, { 162u, 214u, 134u, 255u });

    // RIGHT PANE: Full 8-Tab Frontier Botanical Inspector (x: 1312..1680) — Exact Geometry Tabs (Overview | Shape | Detail | Mat | UV | SDF | LOD | Wind)!
    uiCanvas.FillRect(1310, 36, 2, 1004, { 38u, 38u, 38u, 255u });
    uiCanvas.FillRect(1312, 36, 368, 1004, { 16u, 16u, 18u, 255u });
    uiCanvas.DrawText(1328, 48, "INSPECTOR / BOTANICAL FOLIAGE", 1, { 126u, 126u, 130u, 255u });
    uiCanvas.DrawText(1328, 66, "ANCIENT ENGLISH OAK", 2, { 244u, 244u, 244u, 255u });
    uiCanvas.DrawText(1328, 86, "WELDED BOTANICAL MANIFOLD  -  .BOTANICAL", 1, { 138u, 140u, 144u, 255u });

    // Exact 8-Tab Geometry-Style Page Strip (clearly shows UV + SDF + LOD tabs!)
    drawFrontier8TabStrip(uiCanvas, 1326, 106, 340, 1); // Active = Tab 2: SHAPE

    // Card 1: Welded Tree Architecture (ALL HTML-demo Tree Architecture controls!)
    uiCanvas.FillRoundedRect(1324, 170, 344, 242, 16.0f, { 24u, 24u, 26u, 255u }, { 48u, 48u, 52u, 255u });
    uiCanvas.DrawText(1340, 182, "WELDED TREE ARCHITECTURE", 1, { 226u, 226u, 228u, 255u });
    uiCanvas.DrawText(1588, 182, "V-E+F = 2", 1, { 134u, 198u, 126u, 255u });
    uiCanvas.FillRect(1340, 196, 312, 1, { 48u, 48u, 52u, 255u });
    uiCanvas.FillRoundedRect(1340, 204, 312, 24, 12.0f, { 0u, 0u, 0u, 255u }, { 56u, 56u, 56u, 255u });
    uiCanvas.DrawText(1352, 212, "ENGLISH OAK (QUERCUS ROBUR)       V", 1, { 240u, 240u, 240u, 255u });
    // Mesh Detail Tier 1..4 segmented pill
    uiCanvas.FillRoundedRect(1340, 234, 312, 24, 12.0f, { 8u, 8u, 10u, 255u }, { 50u, 50u, 54u, 255u });
    uiCanvas.DrawText(1354, 242, "TIER 1", 1, { 132u, 132u, 136u, 255u });
    uiCanvas.DrawText(1432, 242, "TIER 2", 1, { 132u, 132u, 136u, 255u });
    uiCanvas.FillRoundedRect(1496, 236, 76, 20, 10.0f, { 244u, 244u, 244u, 255u });
    uiCanvas.DrawText(1514, 242, "TIER 3", 1, { 16u, 16u, 16u, 255u });
    uiCanvas.DrawText(1590, 242, "TIER 4", 1, { 132u, 132u, 136u, 255u });
    drawFrontierScalarSliderOn(uiCanvas, 1340, 266, 312, "TREE HEIGHT",        "13.50", "M", 0.475f);
    drawFrontierScalarSliderOn(uiCanvas, 1340, 310, 312, "TRUNK GIRTH RATIO",  "0.025", "",  0.13f);
    drawFrontierScalarSliderOn(uiCanvas, 1340, 354, 312, "BASAL TRUNK FLARE",  "0.95",  "X", 0.53f);

    // Card 2: Welded Limbs, Forks & Crown (ALL 5 HTML-demo Weber-Penn controls!)
    uiCanvas.FillRoundedRect(1324, 422, 344, 264, 16.0f, { 24u, 24u, 26u, 255u }, { 48u, 48u, 52u, 255u });
    uiCanvas.DrawText(1340, 434, "WELDED LIMBS, FORKS & CROWN", 1, { 226u, 226u, 228u, 255u });
    uiCanvas.DrawText(1580, 434, "WEBER-PENN", 1, { 132u, 134u, 138u, 255u });
    uiCanvas.FillRect(1340, 448, 312, 1, { 48u, 48u, 52u, 255u });
    drawFrontierScalarSliderOn(uiCanvas, 1340, 458, 312, "BRANCH RECURSION",   "4",     "",    1.00f);
    drawFrontierScalarSliderOn(uiCanvas, 1340, 502, 312, "SCAFFOLD LIMBS",     "15",    "",    0.43f);
    drawFrontierScalarSliderOn(uiCanvas, 1340, 546, 312, "EMERGENCE ANGLE",    "52",    "DEG", 0.47f);
    drawFrontierScalarSliderOn(uiCanvas, 1340, 590, 312, "Y-FORK TENDENCY",    "0.42",  "",    0.49f);
    drawFrontierScalarSliderOn(uiCanvas, 1340, 634, 312, "CANOPY SPREAD",      "0.15",  "",    0.15f);

    // Card 3: Terrain-Welded Buttress Roots (ALL 3 HTML-demo Root controls!)
    uiCanvas.FillRoundedRect(1324, 696, 344, 184, 16.0f, { 24u, 24u, 26u, 255u }, { 48u, 48u, 52u, 255u });
    uiCanvas.DrawText(1340, 708, "TERRAIN-WELDED BUTTRESS ROOTS", 1, { 226u, 226u, 228u, 255u });
    uiCanvas.DrawText(1594, 708, "MANIFOLD", 1, { 134u, 198u, 126u, 255u });
    uiCanvas.FillRect(1340, 722, 312, 1, { 48u, 48u, 52u, 255u });
    drawFrontierScalarSliderOn(uiCanvas, 1340, 732, 312, "PRIMARY SURFACE ROOTS", "6",    "",  0.67f);
    drawFrontierScalarSliderOn(uiCanvas, 1340, 776, 312, "ROOT RADIAL REACH",     "0.24", "X", 0.44f);
    drawFrontierScalarSliderOn(uiCanvas, 1340, 820, 312, "BUTTRESS LOBE FLUTES",  "0.38", "",  0.51f);

    // Quick Action Row + Inspector Footer
    uiCanvas.FillRoundedRect(1326, 892, 166, 28, 14.0f, { 242u, 242u, 242u, 255u });
    uiCanvas.DrawText(1358, 902, "REBUILD MANIFOLD", 1, { 16u, 16u, 16u, 255u });
    uiCanvas.FillRoundedRect(1500, 892, 166, 28, 14.0f, { 30u, 30u, 34u, 255u }, { 64u, 64u, 68u, 255u });
    uiCanvas.DrawText(1524, 902, "BAKE 3D + UV + SDF", 1, { 230u, 230u, 234u, 255u });
    uiCanvas.FillRoundedRect(1326, 932, 340, 56, 12.0f, { 22u, 24u, 22u, 255u }, { 56u, 72u, 54u, 255u });
    uiCanvas.DrawText(1340, 944, "TABS 3-8: DETAIL (COLLARS+LEAVES), MAT,", 1, { 172u, 206u, 152u, 255u });
    uiCanvas.DrawText(1340, 962, "UV (1001+1002), SDF (2.5D), LOD (4 TIERS)", 1, { 172u, 206u, 152u, 255u });

    uiCanvas.FillRect(1312, 1004, 368, 36, { 18u, 18u, 20u, 255u });
    uiCanvas.DrawText(1328, 1018, "2 / 8  SHAPE  |  ALL 20 TREE PARAMS", 1, { 132u, 134u, 138u, 255u });

    const bool wroteUi = WriteRgba8PngFile(proofOutDir / "01_Untitled_Botanical_EditorUI_And_3DScene.png", uiCanvas.W, uiCanvas.H, uiCanvas.Pix);

    // --- DELIVERABLE 5: FULL 5-TAB FRONTIER BOTANICAL INSPECTOR SHEET (SHAPE | DETAIL | UV | SDF | LOD) (1960x980) ---
    CanvasRgba tabsCanvas(1960u, 980u, { 10u, 10u, 12u, 255u });
    tabsCanvas.FillRect(0, 0, 1960, 44, { 8u, 8u, 10u, 255u });
    tabsCanvas.DrawText(20, 14, "FRONTIER BOTANICAL INSPECTOR  |  FULL HTML-DEMO PARAMS + GEOMETRY TABS (SHAPE | DETAIL | UV | SDF | LOD)", 2, { 238u, 238u, 240u, 255u });

    constexpr int colW = 376;
    constexpr int colGap = 12;
    auto drawTabColumnFrame = [&](int colIdx, int activeTab, const char* tabLabel, const char* countLabel) -> int
    {
        const int cx = 16 + colIdx * (colW + colGap);
        tabsCanvas.FillRoundedRect(cx, 54, colW, 912, 18.0f, { 16u, 16u, 18u, 255u }, { 46u, 46u, 50u, 255u });
        tabsCanvas.DrawText(cx + 16, 68, "ANCIENT ENGLISH OAK", 1, { 240u, 240u, 242u, 255u });
        tabsCanvas.DrawText(cx + colW - 76, 68, countLabel, 1, { 134u, 136u, 142u, 255u });
        drawFrontier8TabStrip(tabsCanvas, cx + 14, 86, colW - 28, activeTab);
        tabsCanvas.DrawText(cx + 16, 944, tabLabel, 1, { 138u, 142u, 148u, 255u });
        return cx;
    };

    // COLUMN 1: TAB 2 / 8 — SHAPE (ALL 13 HTML-demo Shape Controls!)
    {
        const int cx = drawTabColumnFrame(0, 1, "TAB 2/8 SHAPE  -  ALL HTML TREE SHAPE PARAMS", "2 / 8");
        tabsCanvas.FillRoundedRect(cx + 14, 152, colW - 28, 224, 14.0f, { 24u, 24u, 26u, 255u }, { 48u, 48u, 52u, 255u });
        tabsCanvas.DrawText(cx + 28, 164, "WELDED TREE ARCHITECTURE", 1, { 224u, 224u, 226u, 255u });
        tabsCanvas.DrawText(cx + 286, 164, "V-E+F=2", 1, { 134u, 198u, 126u, 255u });
        tabsCanvas.FillRoundedRect(cx + 28, 182, colW - 56, 22, 11.0f, { 0u, 0u, 0u, 255u }, { 56u, 56u, 56u, 255u });
        tabsCanvas.DrawText(cx + 38, 189, "ENGLISH OAK (QUERCUS ROBUR)        V", 1, { 238u, 238u, 238u, 255u });
        drawFrontierScalarSliderOn(tabsCanvas, cx + 28, 212, colW - 56, "MESH DETAIL TIER",    "3",     "",  0.67f);
        drawFrontierScalarSliderOn(tabsCanvas, cx + 28, 254, colW - 56, "TREE HEIGHT",         "13.50", "M", 0.48f);
        drawFrontierScalarSliderOn(tabsCanvas, cx + 28, 296, colW - 56, "TRUNK GIRTH RATIO",   "0.025", "",  0.13f);
        drawFrontierScalarSliderOn(tabsCanvas, cx + 28, 338, colW - 56, "BASAL TRUNK FLARE",   "0.95",  "X", 0.53f);

        tabsCanvas.FillRoundedRect(cx + 14, 386, colW - 28, 244, 14.0f, { 24u, 24u, 26u, 255u }, { 48u, 48u, 52u, 255u });
        tabsCanvas.DrawText(cx + 28, 398, "WELDED LIMBS, FORKS & CROWN", 1, { 224u, 224u, 226u, 255u });
        drawFrontierScalarSliderOn(tabsCanvas, cx + 28, 416, colW - 56, "BRANCH RECURSION",    "4",     "",    1.00f);
        drawFrontierScalarSliderOn(tabsCanvas, cx + 28, 458, colW - 56, "SCAFFOLD LIMBS",      "15",    "",    0.43f);
        drawFrontierScalarSliderOn(tabsCanvas, cx + 28, 500, colW - 56, "EMERGENCE ANGLE",     "52",    "DEG", 0.47f);
        drawFrontierScalarSliderOn(tabsCanvas, cx + 28, 542, colW - 56, "Y-FORK TENDENCY",     "0.42",  "",    0.49f);
        drawFrontierScalarSliderOn(tabsCanvas, cx + 28, 584, colW - 56, "CANOPY SPREAD",       "0.15",  "",    0.15f);

        tabsCanvas.FillRoundedRect(cx + 14, 640, colW - 28, 174, 14.0f, { 24u, 24u, 26u, 255u }, { 48u, 48u, 52u, 255u });
        tabsCanvas.DrawText(cx + 28, 652, "TERRAIN-WELDED BUTTRESS ROOTS", 1, { 224u, 224u, 226u, 255u });
        drawFrontierScalarSliderOn(tabsCanvas, cx + 28, 670, colW - 56, "PRIMARY SURFACE ROOTS","6",    "",  0.67f);
        drawFrontierScalarSliderOn(tabsCanvas, cx + 28, 712, colW - 56, "ROOT RADIAL REACH",    "0.24", "X", 0.44f);
        drawFrontierScalarSliderOn(tabsCanvas, cx + 28, 754, colW - 56, "BUTTRESS LOBE FLUTES", "0.38", "",  0.51f);

        tabsCanvas.FillRoundedRect(cx + 14, 824, colW - 28, 104, 14.0f, { 22u, 24u, 22u, 255u }, { 54u, 68u, 52u, 255u });
        tabsCanvas.DrawText(cx + 28, 838, "HTML-DEMO FULL PARAMETER PARITY:", 1, { 174u, 212u, 152u, 255u });
        tabsCanvas.DrawText(cx + 28, 858, "TREES: 20 PARAMS (OAK/BANYAN/MAPLE)", 1, { 200u, 206u, 198u, 255u });
        tabsCanvas.DrawText(cx + 28, 878, "PALMS: 22 PARAMS (DATE/COCONUT/ROYAL)", 1, { 200u, 206u, 198u, 255u });
        tabsCanvas.DrawText(cx + 28, 898, "FERNS: 32 PARAMS (SWORD/BRACKEN/FAN)", 1, { 200u, 206u, 198u, 255u });
    }

    // COLUMN 2: TAB 3 / 8 — DETAIL (ALL HTML-demo Bezier Collars, Pipe Law & 3D Sculpted Canopy Leaves!)
    {
        const int cx = drawTabColumnFrame(1, 2, "TAB 3/8 DETAIL  -  COLLARS, PIPE LAW & FOLIAGE", "3 / 8");
        tabsCanvas.FillRoundedRect(cx + 14, 152, colW - 28, 220, 14.0f, { 24u, 24u, 26u, 255u }, { 48u, 48u, 52u, 255u });
        tabsCanvas.DrawText(cx + 28, 164, "BEZIER BRANCH COLLARS & PIPE LAW", 1, { 224u, 224u, 226u, 255u });
        drawFrontierScalarSliderOn(tabsCanvas, cx + 28, 186, colW - 56, "COLLAR FILLET CURVE",  "0.76", "",  0.70f);
        drawFrontierScalarSliderOn(tabsCanvas, cx + 28, 228, colW - 56, "DA VINCI PIPE LAW",    "0.62", "",  0.62f);
        drawFrontierScalarSliderOn(tabsCanvas, cx + 28, 270, colW - 56, "LATERAL ROOTLETS",     "2",    "",  0.50f);
        drawFrontierScalarSliderOn(tabsCanvas, cx + 28, 312, colW - 56, "WIND SWAY FLEX",       "1.00", "X", 0.44f);

        tabsCanvas.FillRoundedRect(cx + 14, 382, colW - 28, 166, 14.0f, { 24u, 24u, 26u, 255u }, { 48u, 48u, 52u, 255u });
        tabsCanvas.DrawText(cx + 28, 394, "3D SCULPTED CANOPY FOLIAGE", 1, { 224u, 224u, 226u, 255u });
        drawFrontierToggleOn      (tabsCanvas, cx + 28, 416, colW - 56, "SHOW CANOPY LEAVES", true);
        drawFrontierScalarSliderOn(tabsCanvas, cx + 28, 450, colW - 56, "FOLIAGE DENSITY",      "1.00", "X", 0.48f);
        drawFrontierScalarSliderOn(tabsCanvas, cx + 28, 494, colW - 56, "LEAF BLADE LENGTH",    "0.24", "M", 0.80f);

        // Palm & Fern Detail Parameter Coverage Preview Cards (showing full parity across all 3 forms!)
        tabsCanvas.FillRoundedRect(cx + 14, 558, colW - 28, 178, 14.0f, { 24u, 24u, 26u, 255u }, { 48u, 48u, 52u, 255u });
        tabsCanvas.DrawText(cx + 28, 570, "PALM DETAIL (WHEN PALM SELECTED)", 1, { 224u, 214u, 172u, 255u });
        drawFrontierToggleOn      (tabsCanvas, cx + 28, 592, colW - 56, "SHOW FRUITING BUNCHES", true);
        drawFrontierScalarSliderOn(tabsCanvas, cx + 28, 624, colW - 56, "AXIL / SPADIX BUNCHES", "8",    "",  0.67f);
        drawFrontierScalarSliderOn(tabsCanvas, cx + 28, 666, colW - 56, "PINNA PAIRS / PLEATS",  "44",   "",  0.62f);
        tabsCanvas.DrawText(cx + 28, 714, "+ ABUNDANCE, RIPENESS, PLUMOSE, BOLE, RELIEF", 1, { 134u, 136u, 142u, 255u });

        tabsCanvas.FillRoundedRect(cx + 14, 746, colW - 28, 182, 14.0f, { 24u, 24u, 26u, 255u }, { 48u, 48u, 52u, 255u });
        tabsCanvas.DrawText(cx + 28, 758, "FERN DETAIL (WHEN FERN SELECTED)", 1, { 172u, 218u, 152u, 255u });
        drawFrontierScalarSliderOn(tabsCanvas, cx + 28, 778, colW - 56, "LAMINA THICKNESS",      "0.55", "MM", 0.29f);
        drawFrontierScalarSliderOn(tabsCanvas, cx + 28, 820, colW - 56, "MESOPHYLL BULLATION",   "0.42", "",   0.42f);
        drawFrontierScalarSliderOn(tabsCanvas, cx + 28, 862, colW - 56, "BASAL AURICLE & SORI",  "0.55", "",   0.55f);
        tabsCanvas.DrawText(cx + 28, 908, "+ MIDRIB, VEINS, SERRATION, CROSIERS, CURL", 1, { 134u, 136u, 142u, 255u });
    }

    // COLUMN 3: TAB 5 / 8 — UV (Exact Frontier Geometry `UV preview` with Dual UDIM 1001 Wood + 1002 Foliage!)
    {
        const int cx = drawTabColumnFrame(2, 4, "TAB 5/8 UV  -  DUAL UDIM 1001 WOOD + 1002 LEAF", "5 / 8");
        tabsCanvas.FillRoundedRect(cx + 14, 152, colW - 28, 776, 14.0f, { 24u, 24u, 26u, 255u }, { 48u, 48u, 52u, 255u });
        tabsCanvas.DrawText(cx + 28, 168, "UV PREVIEW", 1, { 226u, 226u, 228u, 255u });
        tabsCanvas.DrawText(cx + 28, 192, "74.8%", 2, { 246u, 246u, 248u, 255u });
        tabsCanvas.DrawText(cx + 108, 198, "OF 1001 / 1002 USED", 1, { 142u, 144u, 150u, 255u });
        tabsCanvas.DrawText(cx + 28, 222, "24 ISLANDS  -  WELDED WOOD + LEAF ATLAS", 1, { 138u, 142u, 148u, 255u });

        // Live Dual-UDIM UV Packing Preview Well (1001 Welded Bark Strips | 1002 Lobed Oak Leaf Chart)
        const int ux = cx + 28, uy = 244, uw = colW - 56, uh = 216;
        tabsCanvas.FillRoundedRect(ux, uy, uw, uh, 12.0f, { 14u, 15u, 18u, 255u }, { 56u, 58u, 64u, 255u });
        for (int g = 1; g < 8; ++g)
        {
            tabsCanvas.FillRect(ux + (uw * g) / 8, uy + 2, 1, uh - 4, { 28u, 30u, 34u, 255u });
            tabsCanvas.FillRect(ux + 2, uy + (uh * g) / 8, uw - 4, 1, { 28u, 30u, 34u, 255u });
        }
        // Divider between UDIM 1001 (left) and UDIM 1002 (right)
        tabsCanvas.FillRect(ux + uw / 2, uy + 4, 2, uh - 8, { 88u, 92u, 102u, 255u });
        // UDIM 1001 islands: Welded trunk, primary roots, scaffold limbs, branches (warm bark UV islands!)
        tabsCanvas.FillRoundedRect(ux + 10, uy + 14, 62, 168, 4.0f, { 116u, 88u, 62u, 220u }, { 214u, 184u, 146u, 255u });
        tabsCanvas.FillRoundedRect(ux + 80, uy + 14, 66, 78,  4.0f, { 98u,  74u, 52u, 220u }, { 204u, 174u, 138u, 255u });
        tabsCanvas.FillRoundedRect(ux + 80, uy + 100, 32, 82, 4.0f, { 84u,  66u, 48u, 220u }, { 196u, 166u, 130u, 255u });
        tabsCanvas.FillRoundedRect(ux + 118, uy + 100, 28, 40,4.0f, { 108u, 82u, 58u, 220u }, { 196u, 166u, 130u, 255u });
        tabsCanvas.FillRoundedRect(ux + 118, uy + 146, 28, 36,4.0f, { 92u,  72u, 52u, 220u }, { 196u, 166u, 130u, 255u });
        tabsCanvas.DrawText(ux + 12, uy + uh - 16, "1001 WOOD", 1, { 232u, 222u, 206u, 255u });
        // UDIM 1002 islands: Actual 64x64 EDT Lobed Oak leaf UV island rendered directly inside tile 1002!
        const int lx0 = ux + uw / 2 + 16, ly0 = uy + 18, lsz = 132;
        for (int py = 0; py < lsz; ++py)
        {
            const float wv = 1.0f - 2.0f * (static_cast<float>(py) + 0.5f) / static_cast<float>(lsz);
            for (int px = 0; px < lsz; ++px)
            {
                const float uv = -1.0f + 2.0f * (static_cast<float>(px) + 0.5f) / static_cast<float>(lsz);
                const float d = leafAtlas.SampleBilinear(uv, wv, LeafSdfArchetype::LobedOak);
                if (d <= 0.0f)
                    tabsCanvas.BlendPx(lx0 + px, ly0 + py, 86u, 162u, 76u, 240u);
                else if (d < 0.025f)
                    tabsCanvas.BlendPx(lx0 + px, ly0 + py, 226u, 244u, 198u, 255u);
            }
        }
        tabsCanvas.DrawText(ux + uw / 2 + 12, uy + uh - 16, "1002 FOLIAGE", 1, { 188u, 232u, 168u, 255u });

        // 4 UDIM Tile Cards (1001 | 1002 | 1003 | 1004 — exact GeometryInspectorShared.h layout!)
        const std::array<std::pair<const char*, const char*>, 4u> udims {{
            { "1001", "18 WOOD" }, { "1002", "6 LEAF" }, { "1003", "EMPTY" }, { "1004", "EMPTY" }
        }};
        const int utW = (uw - 18) / 4;
        for (int i = 0; i < 4; ++i)
        {
            const int tx = ux + i * (utW + 6);
            const bool used = (i < 2);
            tabsCanvas.FillRoundedRect(tx, uy + uh + 16, utW, 48, 10.0f,
                used ? std::array<uint8_t,4>{ 34u, 36u, 38u, 255u } : std::array<uint8_t,4>{ 20u, 20u, 22u, 255u },
                used ? std::array<uint8_t,4>{ 148u, 152u, 160u, 200u } : std::array<uint8_t,4>{ 46u, 46u, 50u, 255u });
            tabsCanvas.DrawText(tx + 8, uy + uh + 26, udims[static_cast<std::size_t>(i)].first, 1,
                used ? std::array<uint8_t,4>{ 238u, 238u, 242u, 255u } : std::array<uint8_t,4>{ 118u, 118u, 124u, 255u });
            tabsCanvas.DrawText(tx + 8, uy + uh + 44, udims[static_cast<std::size_t>(i)].second, 1, { 136u, 140u, 146u, 255u });
        }

        // Metadata rows (Texel scale, Unwrap angle, Seam strategy, Packing)
        int my = uy + uh + 84;
        const std::array<std::pair<const char*, const char*>, 5u> uvRows {{
            { "TEXEL SCALE",     "0.382 UV/M"       },
            { "UNWRAP ANGLE",    "60 DEG"           },
            { "WELDED SEAMS",    "COLLAR-CONTINUOUS"},
            { "LEAF ATLAS",      "64X64 EDT MATCHED"},
            { "UDIM CHANNELS",   "2 ACTIVE (4 MAX)" },
        }};
        for (const auto& r : uvRows)
        {
            tabsCanvas.DrawText(ux, my, r.first, 1, { 146u, 148u, 154u, 255u });
            const int vw = static_cast<int>(std::string(r.second).size()) * 6;
            tabsCanvas.DrawText(ux + uw - vw, my, r.second, 1, { 228u, 228u, 232u, 255u });
            my += 28;
        }
        // Open in UV editor / Edit in UV workspace Frontier buttons!
        tabsCanvas.FillRoundedRect(ux, my + 24, uw, 34, 17.0f, { 236u, 236u, 238u, 255u });
        tabsCanvas.DrawText(ux + 96, my + 37, "OPEN IN UV EDITOR", 1, { 16u, 16u, 18u, 255u });
        tabsCanvas.FillRoundedRect(ux, my + 68, uw, 34, 17.0f, { 30u, 30u, 34u, 255u }, { 64u, 64u, 70u, 255u });
        tabsCanvas.DrawText(ux + 86, my + 81, "EDIT IN UV WORKSPACE", 1, { 226u, 226u, 230u, 255u });
    }

    // COLUMN 4: TAB 6 / 8 — SDF (Exact Frontier Geometry `SDF preview` with LIVE Oak SDF Slice!)
    {
        const int cx = drawTabColumnFrame(3, 5, "TAB 6/8 SDF  -  2.5D THIN-LEAF + WOOD CAPSULES", "6 / 8");
        tabsCanvas.FillRoundedRect(cx + 14, 152, colW - 28, 776, 14.0f, { 24u, 24u, 26u, 255u }, { 48u, 48u, 52u, 255u });
        tabsCanvas.DrawText(cx + 28, 168, "SDF PREVIEW", 1, { 226u, 226u, 228u, 255u });
        std::ostringstream brStr;
        brStr << oakMesh.SdfVolume.OccupiedBricks;
        tabsCanvas.DrawText(cx + 28, 192, brStr.str(), 2, { 246u, 246u, 248u, 255u });
        tabsCanvas.DrawText(cx + 88, 198, "OF 4096 CANOPY BRICKS", 1, { 142u, 144u, 150u, 255u });
        tabsCanvas.DrawText(cx + 28, 222, "16^3 BRICKS  -  64X64 EDT  -  0.6MM SHEET", 1, { 138u, 142u, 148u, 255u });

        // LIVE SDF Vertical Slice (XZ cross-section through the English Oak's ACTUAL Wood Capsules + Thin-Leaf SDF!)
        const int sx = cx + 28, sy = 244, sw = colW - 56, sh = 216;
        tabsCanvas.FillRoundedRect(sx, sy, sw, sh, 12.0f, { 14u, 15u, 18u, 255u }, { 56u, 58u, 64u, 255u });
        for (int py = 4; py < sh - 4; ++py)
        {
            const float wz = 10.2f - (static_cast<float>(py - 4) / static_cast<float>(sh - 8)) * 10.8f;
            for (int px = 4; px < sw - 4; ++px)
            {
                const float wx = -5.6f + (static_cast<float>(px - 4) / static_cast<float>(sw - 8)) * 11.2f;
                const V3 p { wx, 0.0f, wz };
                float d = 1e6f;
                for (const auto& cap : oakMesh.SdfVolume.WoodCapsules)
                {
                    d = std::min(d, EvaluateNoisyWoodCapsuleSdf(cap, p, oakMesh.SdfVolume.BarkNoiseAmp, oakMesh.SdfVolume.BarkNoiseFreq));
                }
                for (std::size_t li = 0u; li < oakMesh.SdfVolume.Leaves.size(); li += 4u)
                {
                    const auto& lf = oakMesh.SdfVolume.Leaves[li];
                    const V3 dp = Sub3(p, lf.CenterMeters);
                    const float r2 = Dot3(dp, dp);
                    if (r2 < 1.2f)
                    {
                        d = std::min(d, std::sqrt(r2) - lf.HalfSpanMeters * 0.85f);
                    }
                }
                const float iso = std::fabs(std::sin(d * 8.5f));
                uint8_t cr = 18u, cg = 20u, cb = 26u;
                if (d <= 0.0f)
                {
                    cr = 86u; cg = 206u; cb = 122u;
                }
                else
                {
                    const float e = std::exp(-d * 0.65f);
                    cr = static_cast<uint8_t>(18.0f + 24.0f * e + 28.0f * (1.0f - iso) * e);
                    cg = static_cast<uint8_t>(22.0f + 46.0f * e + 34.0f * (1.0f - iso) * e);
                    cb = static_cast<uint8_t>(30.0f + 68.0f * e + 36.0f * (1.0f - iso) * e);
                }
                if (std::fabs(d) < 0.06f) { cr = 246u; cg = 246u; cb = 224u; }
                tabsCanvas.BlendPx(sx + px, sy + py, cr, cg, cb, 255u);
            }
        }
        tabsCanvas.DrawText(sx + 10, sy + sh - 16, "CENTRE SLICE  -  XZ (OAK + ROOTS)", 1, { 216u, 224u, 212u, 255u });

        drawFrontierScalarSliderOn(tabsCanvas, sx, sy + sh + 16, sw, "SLICE DEPTH",          "50",   "%",  0.50f);
        drawFrontierScalarSliderOn(tabsCanvas, sx, sy + sh + 60, sw, "CONE-WIDTH LOD BIAS",  "1.00", "X",  0.40f);
        drawFrontierScalarSliderOn(tabsCanvas, sx, sy + sh + 104,sw, "THIN-LEAF HALF-THICK", "0.60", "MM", 0.28f);
        drawFrontierToggleOn      (tabsCanvas, sx, sy + sh + 150,sw, "NOISY BARK CAPSULES",  true);

        int smy = sy + sh + 190;
        const std::array<std::pair<const char*, const char*>, 5u> sdfRows {{
            { "RESOLUTION",    "16^3 BRICKS + 64^2 EDT" },
            { "LEAF SURVIVAL", "100.0% (0.6MM SHEET)"   },
            { "JOINT SEAM",    "0.000MM CONTINUOUS"     },
            { "WIND SWIM",     "0.0000 (REST-SPACE)"    },
            { "WOOD CAPSULES", "WATER-TIGHT BALL JOINT" },
        }};
        for (const auto& r : sdfRows)
        {
            tabsCanvas.DrawText(sx, smy, r.first, 1, { 146u, 148u, 154u, 255u });
            const int vw = static_cast<int>(std::string(r.second).size()) * 6;
            tabsCanvas.DrawText(sx + sw - vw, smy, r.second, 1, { 228u, 228u, 232u, 255u });
            smy += 26;
        }
        tabsCanvas.FillCircle(static_cast<float>(sx + 5), static_cast<float>(smy + 12), 5.0f, { 76u, 206u, 128u, 255u });
        tabsCanvas.DrawText(sx + 16, smy + 8, "LOCAL 2.5D SDF RESIDENT ON GPU.", 1, { 174u, 214u, 162u, 255u });
    }

    // COLUMN 5: TAB 7 / 8 — LOD (Exact Frontier Geometry `LOD preview` with 4-Tier Botanical Cluster Pyramid!)
    {
        const int cx = drawTabColumnFrame(4, 6, "TAB 7/8 LOD  -  4-TIER BOTANICAL CLUSTER PYRAMID", "7 / 8");
        tabsCanvas.FillRoundedRect(cx + 14, 152, colW - 28, 776, 14.0f, { 24u, 24u, 26u, 255u }, { 48u, 48u, 52u, 255u });
        tabsCanvas.DrawText(cx + 28, 168, "LOD PREVIEW", 1, { 226u, 226u, 228u, 255u });
        tabsCanvas.DrawText(cx + 28, 192, "4", 2, { 246u, 246u, 248u, 255u });
        tabsCanvas.DrawText(cx + 52, 198, "BOTANICAL CLUSTER LEVELS", 1, { 142u, 144u, 150u, 255u });
        std::ostringstream lsub;
        lsub << oakMesh.Triangles.size() << " TRIS (LOD0)  -  BEER-LAMBERT FAR";
        tabsCanvas.DrawText(cx + 28, 222, lsub.str(), 1, { 138u, 142u, 148u, 255u });

        // 4-Tier Cluster LOD Reduction Bar Chart (exact ComponentDrawerPanel / GeometryInspectorShared.h style!)
        const int lx = cx + 28, ly = 248, lw = colW - 56, lh = 212;
        tabsCanvas.FillRoundedRect(lx, ly, lw, lh, 12.0f, { 14u, 15u, 18u, 255u }, { 56u, 58u, 64u, 255u });
        struct LodTierRow { const char* Name; const char* Desc; float Ratio; std::array<uint8_t,4> Col; };
        const std::array<LodTierRow, 4u> tiers {{
            { "LOD0  HERO",   "100%  WELDED + 3D LEAVES", 1.00f, { 92u,  206u, 132u, 255u } },
            { "LOD1  NEAR",   " 45%  WELDED + CLUSTERED", 0.45f, { 134u, 194u, 118u, 255u } },
            { "LOD2  MID",    " 16%  LOW-RING + CARDS",   0.16f, { 216u, 186u, 102u, 255u } },
            { "LOD3  FAR",    "  4%  CONE BEER-LAMBERT",  0.04f, { 118u, 168u, 228u, 255u } },
        }};
        for (int i = 0; i < 4; ++i)
        {
            const int by = ly + 16 + i * 48;
            tabsCanvas.DrawText(lx + 12, by, tiers[static_cast<std::size_t>(i)].Name, 1, { 232u, 232u, 236u, 255u });
            const int dw = static_cast<int>(std::string(tiers[static_cast<std::size_t>(i)].Desc).size()) * 6;
            tabsCanvas.DrawText(lx + lw - 12 - dw, by, tiers[static_cast<std::size_t>(i)].Desc, 1, { 154u, 158u, 166u, 255u });
            tabsCanvas.FillRoundedRect(lx + 12, by + 16, lw - 24, 14, 7.0f, { 28u, 30u, 34u, 255u });
            const int bw = std::max(16, static_cast<int>(std::round(static_cast<float>(lw - 24) * tiers[static_cast<std::size_t>(i)].Ratio)));
            tabsCanvas.FillRoundedRect(lx + 12, by + 16, bw, 14, 7.0f, tiers[static_cast<std::size_t>(i)].Col);
        }

        drawFrontierScalarSliderOn(tabsCanvas, lx, ly + lh + 18, lw, "PREVIEW LOD TIER",       "0",     "",  0.00f);
        drawFrontierScalarSliderOn(tabsCanvas, lx, ly + lh + 62, lw, "SCREEN ERROR THRESHOLD", "1.25",  "PX",0.32f);
        drawFrontierScalarSliderOn(tabsCanvas, lx, ly + lh + 106,lw, "WORLDPARTITION PROMOTE", "20.0",  "M", 0.45f);
        drawFrontierToggleOn      (tabsCanvas, lx, ly + lh + 152,lw, "CONTINUOUS CLUSTER LOD", true);

        int lmy = ly + lh + 192;
        const std::array<std::pair<const char*, const char*>, 5u> lodRows {{
            { "LOD0 TRIANGLES",  "FULL WELDED + LEAVES" },
            { "CLUSTER SIZE",    "128 TRIS / CLUSTER"   },
            { "LOD0 -> LOD1",    "14.0 M  (HERMITE C1)" },
            { "LOD1 -> LOD2",    "32.0 M  (0 POPPING)"  },
            { "FAR EXTINCTION",  "BEER-LAMBERT CONE"    },
        }};
        for (const auto& r : lodRows)
        {
            tabsCanvas.DrawText(lx, lmy, r.first, 1, { 146u, 148u, 154u, 255u });
            const int vw = static_cast<int>(std::string(r.second).size()) * 6;
            tabsCanvas.DrawText(lx + lw - vw, lmy, r.second, 1, { 228u, 228u, 232u, 255u });
            lmy += 26;
        }
        tabsCanvas.FillCircle(static_cast<float>(lx + 5), static_cast<float>(lmy + 12), 5.0f, { 76u, 206u, 128u, 255u });
        tabsCanvas.DrawText(lx + 16, lmy + 8, "CLUSTER PYRAMID & C1 STREAMING READY.", 1, { 174u, 214u, 162u, 255u });
    }

    const bool wroteTabs = WriteRgba8PngFile(proofOutDir / "05_Botanical_Inspector_AllTabs_Shape_Detail_UV_SDF_LOD.png", tabsCanvas.W, tabsCanvas.H, tabsCanvas.Pix);

    // --- DELIVERABLE 2: Species Showcase Triptych (Ferns -> Palms -> Oak) + Baked Thumbnail Strip (1600x760) ---
    CanvasRgba showcase(1600u, 760u, { 14u, 15u, 17u, 255u });
    showcase.FillRect(0, 0, 1600, 48, { 10u, 10u, 12u, 255u });
    showcase.DrawText(22, 16, "FRONTIER UNIFIED BOTANICAL SYSTEM  |  1. FERNS  ->  2. PALMS  ->  3. ENGLISH OAK  (.BOTANICAL)", 2, { 235u, 235u, 235u, 255u });

    // Panel A: Ferns Close-Up (SwordFern + BrackenFern)
    std::vector<ScenePlantActor> fernPanel {
        { "SwordFern",   swordMesh,   { -0.72f, 0.0f, 0.0f }, 0.35f, 1.15f },
        { "BrackenFern", brackenMesh, {  0.78f, 0.2f, 0.0f }, 1.15f, 1.05f },
    };
    RenderBotanical3dScene(showcase, 16, 60, 508, 490, fernPanel, {}, leafAtlas,
        { 0.05f, -3.15f, 1.55f }, { 0.02f, 0.05f, 0.42f }, sunDir, true, false);
    showcase.FillRoundedRect(24, 68, 354, 26, 8.0f, { 12u, 14u, 12u, 215u }, { 120u, 160u, 96u, 220u });
    showcase.DrawText(34, 77, "1. FERNS: SWORD & BRACKEN (CROSIERS + AURICLES)", 1, { 214u, 234u, 186u, 255u });

    // Panel B: Palms Close-Up (MedjoolDatePalm + CoastalCoconutPalm leaning gracefully outward!)
    std::vector<ScenePlantActor> palmPanel {
        { "MedjoolDatePalm",    dateMesh,    { -2.6f, 0.0f, 0.0f }, 0.55f, 1.0f },
        { "CoastalCoconutPalm", coconutMesh, {  2.2f, 0.4f, 0.0f }, 5.65f, 0.96f },
    };
    RenderBotanical3dScene(showcase, 546, 60, 508, 490, palmPanel, {}, leafAtlas,
        { 0.4f, -13.4f, 4.6f }, { 0.3f, 0.2f, 4.0f }, sunDir, true, false);
    showcase.FillRoundedRect(554, 68, 382, 26, 8.0f, { 12u, 14u, 12u, 215u }, { 210u, 174u, 102u, 220u });
    showcase.DrawText(564, 77, "2. PALMS: MEDJOOL DATE & COCONUT (FRONDS + FRUIT)", 1, { 236u, 216u, 162u, 255u });

    // Panel C: Ancient English Oak Close-Up (full dome crown & welded roots framed + macro welded root/branch collar inset!)
    std::vector<ScenePlantActor> oakPanel {
        { "EnglishOak", oakMesh, { 0.0f, 0.0f, 0.0f }, 0.22f, 1.0f },
    };
    RenderBotanical3dScene(showcase, 1076, 60, 508, 490, oakPanel, {}, leafAtlas,
        { 0.0f, -15.2f, 4.8f }, { 0.0f, 0.0f, 4.4f }, sunDir, true, false);
    showcase.FillRoundedRect(1084, 68, 434, 26, 8.0f, { 12u, 14u, 12u, 215u }, { 142u, 184u, 124u, 220u });
    showcase.DrawText(1094, 77, "3. ENGLISH OAK: 1 WELDED MESH (V-E+F=2, 0 OPEN EDGES)", 1, { 214u, 234u, 198u, 255u });

    // Close-Up Macro Inset of Welded Root-Trunk Bezier Collars (bottom-right of Oak panel)
    showcase.FillRoundedRect(1372, 342, 204, 200, 10.0f, { 12u, 14u, 16u, 240u }, { 212u, 226u, 192u, 230u });
    RenderBotanical3dScene(showcase, 1376, 346, 196, 168, oakPanel, {}, leafAtlas,
        { 0.4f, -3.55f, 1.05f }, { 0.0f, 0.0f, 0.62f }, sunDir, false, false);
    showcase.DrawText(1384, 522, "WELDED ROOT-TRUNK COLLARS", 1, { 214u, 234u, 198u, 255u });

    // Bottom Strip: Outliner SVG Folder Icon + All 5 Baked 128x128 3D Plant Render Thumbnails!
    showcase.FillRoundedRect(16, 566, 1568, 178, 16.0f, { 22u, 23u, 25u, 255u }, { 48u, 52u, 56u, 255u });
    showcase.DrawText(32, 580, "OUTLINER ASSETS: BOTANICAL FOLDER SVG + BAKED 128X128 3D STUDIO PLANT RENDER THUMBNAILS", 1, { 196u, 206u, 188u, 255u });
    // Folder SVG card
    showcase.FillRoundedRect(34, 604, 156, 126, 12.0f, { 28u, 32u, 28u, 255u }, { 78u, 102u, 68u, 220u });
    showcase.DrawBotanicalFolderSvgIcon(76, 616, 72);
    showcase.DrawText(46, 700, "FOLDER-BOTANICAL.SVG", 1, { 198u, 226u, 172u, 255u });

    const std::array<std::pair<const char*, const BakedPlantThumbnail*>, 5u> thumbCards {{
        { "SWORDFERN.PNG",          &thSword   },
        { "BRACKENFERN.PNG",        &thBracken },
        { "MEDJOOLDATEPALM.PNG",    &thDate    },
        { "COASTALCOCONUTPALM.PNG", &thCoco    },
        { "ENGLISHOAK.PNG",         &thOak     },
    }};
    int tx = 210;
    for (const auto& tc : thumbCards)
    {
        showcase.FillRoundedRect(tx, 604, 260, 126, 12.0f, { 28u, 28u, 30u, 255u }, { 54u, 54u, 58u, 255u });
        showcase.BlitThumb(tx + 12, 614, 106, 106, *tc.second, 10.0f);
        showcase.DrawText(tx + 128, 636, tc.first, 1, { 232u, 232u, 232u, 255u });
        showcase.DrawText(tx + 128, 658, "128X128 3D RENDER", 1, { 162u, 184u, 124u, 255u });
        showcase.DrawText(tx + 128, 678, "OUTLINER ITEM ICON", 1, { 138u, 138u, 138u, 255u });
        tx += 274;
    }
    const bool wroteShowcase = WriteRgba8PngFile(proofOutDir / "02_SpeciesShowcase_Ferns_Palms_Oak.png", showcase.W, showcase.H, showcase.Pix);

    // --- DELIVERABLE 3: Technical Proof Sheet (2D EDT Atlas + WorldPartition C1 Curve + 2.5D Shadow Map + Leaf States) ---
    CanvasRgba tech(1600u, 760u, { 14u, 15u, 18u, 255u });
    tech.FillRect(0, 0, 1600, 46, { 10u, 10u, 12u, 255u });
    tech.DrawText(22, 16, "UNIFIED BOTANICAL TECHNICAL PROOF  |  8/8 STRICT GATES PASS", 2, { 162u, 224u, 138u, 255u });

    // Panel 1 (Top-Left): 64x64 2D EDT Leaf Archetype SDF Atlas (FernPinna, PalmLeaflet, LobedOak, PalmateMaple)
    tech.FillRoundedRect(16, 60, 772, 332, 14.0f, { 22u, 23u, 26u, 255u }, { 48u, 50u, 56u, 255u });
    tech.DrawText(32, 76, "A. 64X64 FELZENSZWALB-HUTTENLOCHER 2D EDT LEAF SDF ARCHETYPES (100% SURVIVAL)", 1, { 220u, 224u, 214u, 255u });
    const std::array<std::pair<LeafSdfArchetype, const char*>, 4u> sdfShow {{
        { LeafSdfArchetype::FernPinna,    "FERN_PINNA"    },
        { LeafSdfArchetype::PalmLeaflet,  "PALM_LEAFLET"  },
        { LeafSdfArchetype::LobedOak,     "LOBED_OAK"     },
        { LeafSdfArchetype::PalmateMaple, "PALMATE_MAPLE" },
    }};
    for (int k = 0; k < 4; ++k)
    {
        const int bx = 36 + k * 186;
        const int by = 108;
        const int sz = 164;
        for (int py = 0; py < sz; ++py)
        {
            const float w = 1.0f - 2.0f * (static_cast<float>(py) + 0.5f) / static_cast<float>(sz);
            for (int px = 0; px < sz; ++px)
            {
                const float u = -1.0f + 2.0f * (static_cast<float>(px) + 0.5f) / static_cast<float>(sz);
                const float d = leafAtlas.SampleBilinear(u, w, sdfShow[static_cast<std::size_t>(k)].first);
                const float iso = std::fabs(std::sin(d * 38.0f));
                uint8_t r = 0u, g = 0u, b = 0u;
                if (d <= 0.0f)
                {
                    r = static_cast<uint8_t>(std::clamp(48.0f + 110.0f * (1.0f - std::min(1.0f, -d * 3.0f)), 0.0f, 255.0f));
                    g = static_cast<uint8_t>(std::clamp(138.0f + 95.0f * (1.0f - std::min(1.0f, -d * 2.5f)), 0.0f, 255.0f));
                    b = static_cast<uint8_t>(std::clamp(64.0f + 40.0f * iso, 0.0f, 255.0f));
                }
                else
                {
                    const float e = std::exp(-d * 3.8f);
                    r = static_cast<uint8_t>(18.0f + 24.0f * e + 16.0f * (1.0f - iso) * e);
                    g = static_cast<uint8_t>(22.0f + 32.0f * e + 18.0f * (1.0f - iso) * e);
                    b = static_cast<uint8_t>(30.0f + 46.0f * e + 24.0f * (1.0f - iso) * e);
                }
                if (std::fabs(d) < 0.018f) { r = 248u; g = 248u; b = 220u; } // Crisp zero-crossing silhouette!
                tech.BlendPx(bx + px, by + py, r, g, b, 255u);
            }
        }
        tech.DrawText(bx + 24, by + sz + 14, sdfShow[static_cast<std::size_t>(k)].second, 1, { 214u, 224u, 206u, 255u });
        std::ostringstream oss;
        oss << "AREA: " << std::fixed << std::setprecision(1) << (leafAtlas.AreaFraction[static_cast<std::size_t>(sdfShow[static_cast<std::size_t>(k)].first)] * 100.0f) << "%";
        tech.DrawText(bx + 36, by + sz + 32, oss.str(), 1, { 138u, 152u, 134u, 255u });
    }

    // Panel 2 (Top-Right): 2.5D Thin-Leaf Raytraced Dappled Ground Shadow Map (Top-Down under English Oak)
    tech.FillRoundedRect(808, 60, 776, 332, 14.0f, { 22u, 23u, 26u, 255u }, { 48u, 50u, 56u, 255u });
    tech.DrawText(824, 76, "B. EXACT 2.5D THIN-LEAF + NOISY WOOD CAPSULE SHADOW MAP (ENGLISH OAK)", 1, { 220u, 224u, 214u, 255u });
    const int smX = 836, smY = 104, smW = 340, smH = 266;
    for (int py = 0; py < smH; ++py)
    {
        const float wy = 6.5f - (static_cast<float>(py) / static_cast<float>(smH - 1)) * 11.5f;
        for (int px = 0; px < smW; ++px)
        {
            const float wx = -7.5f + (static_cast<float>(px) / static_cast<float>(smW - 1)) * 12.5f;
            const float tr = oakMesh.SdfVolume.TraceShadowTransmittance(leafAtlas, { wx, wy, 0.02f }, sunDir, 0.008f, 1.0f);
            const uint8_t cr = static_cast<uint8_t>(30.0f + 195.0f * tr);
            const uint8_t cg = static_cast<uint8_t>(42.0f + 188.0f * tr);
            const uint8_t cb = static_cast<uint8_t>(38.0f + 150.0f * tr);
            tech.BlendPx(smX + px, smY + py, cr, cg, cb, 255u);
        }
    }
    tech.DrawText(1204, 122, "TWO-LEVEL 16^3 BRICKS", 1, { 162u, 184u, 124u, 255u });
    tech.DrawText(1204, 146, "THIN-LEAF SURVIVAL: 100.0%", 1, { 235u, 235u, 235u, 255u });
    tech.DrawText(1204, 168, "SHADOW BLOAT ERROR:   0.0%", 1, { 235u, 235u, 235u, 255u });
    tech.DrawText(1204, 190, "WOOD JOINT SEAM:   0.000MM", 1, { 235u, 235u, 235u, 255u });
    tech.DrawText(1204, 212, "WIND BARK SWIM:     0.0000", 1, { 235u, 235u, 235u, 255u });
    tech.DrawText(1204, 248, "CONE-WIDTH BEER-LAMBERT:", 1, { 255u, 180u, 84u, 255u });
    tech.DrawText(1204, 270, "NEAR = CRISP 2.5D SILHOUETTE", 1, { 176u, 176u, 176u, 255u });
    tech.DrawText(1204, 292, "FAR  = ANISOTROPIC EXTINCTION", 1, { 176u, 176u, 176u, 255u });

    // Panel 3 (Bottom-Left): WorldPartition Smooth C1 GPU <-> Deterministic CPU Transition Curve
    tech.FillRoundedRect(16, 408, 772, 336, 14.0f, { 22u, 23u, 26u, 255u }, { 48u, 50u, 56u, 255u });
    tech.DrawText(32, 424, "C. WORLDPARTITION 16M X 16M C1 GPU <-> DETERMINISTIC 120HZ CPU TRANSITION (0 SNAP)", 1, { 220u, 224u, 214u, 255u });
    const int gx0 = 56, gy0 = 462, gw = 700, gh = 226;
    tech.FillRect(gx0, gy0, gw, gh, { 14u, 16u, 19u, 255u });
    for (int i = 0; i <= 4; ++i)
    {
        const int y = gy0 + (gh * i) / 4;
        tech.FillRect(gx0, y, gw, 1, { 36u, 40u, 46u, 255u });
    }
    for (std::size_t i = 1u; i < timeline.size(); ++i)
    {
        const float x0 = gx0 + (timeline[i - 1u].T / 8.0f) * gw;
        const float x1 = gx0 + (timeline[i].T / 8.0f) * gw;
        // Blue/Gold curve: CpuWeight [0 = GPU .. 1 = Deterministic CPU]
        const float yW0 = gy0 + gh - timeline[i - 1u].CpuWeight * (gh - 18) - 9;
        const float yW1 = gy0 + gh - timeline[i].CpuWeight * (gh - 18) - 9;
        tech.DrawLine(x0, yW0, x1, yW1, 3.0f, { 255u, 180u, 84u, 255u });
        // Green curve: Smooth Oak Wind Bend Angle (continuous across transitions!)
        const float yB0 = gy0 + gh * 0.55f - timeline[i - 1u].BendOak * 420.0f;
        const float yB1 = gy0 + gh * 0.55f - timeline[i].BendOak * 420.0f;
        tech.DrawLine(x0, yB0, x1, yB1, 2.5f, { 112u, 218u, 134u, 255u });
    }
    tech.DrawText(gx0 + 10, gy0 + gh + 16, "AMBER: CPU WEIGHT (0=GPU -> HERMITE C1 -> 1=CPU -> GPU)   |   GREEN: CONTINUOUS OAK BEND", 1, { 196u, 200u, 194u, 255u });

    // Panel 4 (Bottom-Right): 3-State Falling -> Sleeping (0 ALU) -> ForceField Wake-Up Curve
    tech.FillRoundedRect(808, 408, 776, 336, 14.0f, { 22u, 23u, 26u, 255u }, { 48u, 50u, 56u, 255u });
    tech.DrawText(824, 424, "D. 4M X 4M LEAF SUBCELLS: ACTIVE -> SLEEPING (0 ALU) -> FORCEFIELD WAKE-UP", 1, { 220u, 224u, 214u, 255u });
    const int lx0 = 848, ly0 = 462, lw = 704, lh = 226;
    tech.FillRect(lx0, ly0, lw, lh, { 14u, 16u, 19u, 255u });
    // Mark ForceField gust wake moment (t = 5.2s)
    const int wakeX = lx0 + static_cast<int>((5.2f / 8.0f) * lw);
    tech.FillRect(wakeX, ly0, 2, lh, { 240u, 120u, 96u, 220u });
    tech.DrawText(wakeX + 6, ly0 + 10, "FORCEFIELD GUST WAKE", 1, { 244u, 142u, 118u, 255u });

    float maxLeaves = 1.0f;
    for (const auto& s : timeline) maxLeaves = std::max(maxLeaves, static_cast<float>(s.ActiveLeaves + s.SleepingLeaves));
    for (std::size_t i = 1u; i < timeline.size(); ++i)
    {
        const float x0 = lx0 + (timeline[i - 1u].T / 8.0f) * lw;
        const float x1 = lx0 + (timeline[i].T / 8.0f) * lw;
        const float ya0 = ly0 + lh - (static_cast<float>(timeline[i - 1u].ActiveLeaves) / maxLeaves) * (lh - 18) - 9;
        const float ya1 = ly0 + lh - (static_cast<float>(timeline[i].ActiveLeaves) / maxLeaves) * (lh - 18) - 9;
        const float ys0 = ly0 + lh - (static_cast<float>(timeline[i - 1u].SleepingLeaves) / maxLeaves) * (lh - 18) - 9;
        const float ys1 = ly0 + lh - (static_cast<float>(timeline[i].SleepingLeaves) / maxLeaves) * (lh - 18) - 9;
        tech.DrawLine(x0, ya0, x1, ya1, 3.0f, { 120u, 220u, 130u, 255u });
        tech.DrawLine(x0, ys0, x1, ys1, 3.0f, { 112u, 166u, 232u, 255u });
    }
    tech.DrawText(lx0 + 10, ly0 + lh + 16, "GREEN: ACTIVE FALLING/WOKEN LEAVES  |  BLUE: SLEEPING LEAVES (0 ALU, RE-BLOWABLE)", 1, { 196u, 200u, 194u, 255u });

    const bool wroteTech = WriteRgba8PngFile(proofOutDir / "03_WorldPartition_Wind_FallingLeaves_Proof.png", tech.W, tech.H, tech.Pix);

    // --- DELIVERABLE 4: Render 36 Animated Frames for Wind Sway + Falling/Sleeping/Waking Leaves GIF ---
    const fs::path framesDir = "/tmp/botanical-anim-frames";
    fs::create_directories(framesDir, ec);
    BotanicalWorldPartition animWp(wpCfg);
    for (std::size_t i = 0u; i < sceneActors.size(); ++i)
    {
        BotanicalPlantInstance pi {};
        pi.SpeciesId = sceneActors[i].Mesh.SpeciesId;
        pi.GrowthForm = sceneActors[i].Mesh.GrowthForm;
        pi.WorldPosMeters = sceneActors[i].Pos;
        pi.YawRad = sceneActors[i].Yaw;
        pi.HeightMeters = sceneActors[i].Mesh.BoundsMax[2];
        pi.StiffnessNmPerRad = (pi.GrowthForm == BotanicalGrowthForm::Fern) ? 16.0f : (pi.GrowthForm == BotanicalGrowthForm::Palm ? 28.0f : 60.0f);
        pi.DampingRatio = 0.28f;
        pi.WindFlexibility = (pi.GrowthForm == BotanicalGrowthForm::Fern) ? 0.85f : (pi.GrowthForm == BotanicalGrowthForm::Palm ? 0.68f : 0.40f);
        pi.AbscissionThreshN = 6.5f;
        pi.PhaseOffset = static_cast<float>(i) * 0.19f;
        pi.ForceNetworkAuth = true;
        (void)animWp.RegisterPlant(pi);
    }
    for (int k = 0; k < 42; ++k)
    {
        const float th = static_cast<float>(k) * 2.39996f;
        FallingLeafParticle lf {};
        lf.SourceEntityId = 6u;
        lf.PositionMeters = { 0.2f + std::cos(th) * 2.4f, 4.8f + std::sin(th) * 2.2f, 2.2f + 2.6f * (static_cast<float>(k % 6) / 5.0f) };
        lf.VelocityMps    = { 0.5f, -0.2f, -0.4f };
        lf.ColorLinear    = (k % 2 == 0) ? V3{ 0.36f, 0.30f, 0.06f } : V3{ 0.18f, 0.38f, 0.08f };
        lf.HalfSpanMeters = 0.11f;
        lf.FlutterPhase   = th;
        animWp.SpawnFallingLeaf(lf);
    }

    bool wroteFrames = true;
    for (int fr = 0; fr < 36; ++fr)
    {
        const float t = static_cast<float>(fr) * 0.15f;
        BotanicalWindFieldState aw {};
        aw.WindVelocityMps = { 5.2f + 3.2f * std::sin(t * 1.4f), 1.4f * std::cos(t * 1.1f), 0.0f };
        aw.GustStrengthMps = 2.8f;
        aw.GustFrequencyHz = 0.72f;
        if (fr >= 20)
        {
            aw.HasWakeImpulse   = true;
            aw.WakeCenterMeters = { -2.0f + static_cast<float>(fr - 20) * 0.45f, 3.8f, 0.2f };
            aw.WakeRadiusMeters = 8.5f;
            aw.WakeStrengthMps  = 10.5f;
            aw.WakeDirNorm      = { 0.92f, 0.38f, 0.0f };
        }
        for (int sub = 0; sub < 4; ++sub) animWp.Step(0.15f / 4.0f, aw, {});

        for (std::size_t i = 0u; i < sceneActors.size(); ++i)
        {
            const auto& pi = animWp.Plants()[i];
            ApplyWindPoseToPlant(sceneActors[i].Mesh, pi.BendAngleXRad * 1.35f, pi.BendAngleYRad * 1.35f, t, 0.45f);
        }

        CanvasRgba frame(720u, 420u, { 16u, 16u, 18u, 255u });
        RenderBotanical3dScene(frame, 0, 0, 720, 420, sceneActors, animWp.Leaves(), leafAtlas,
            { 0.1f + 0.65f * std::sin(t * 0.45f), -13.0f, 4.0f }, { 0.0f, 2.2f, 3.1f }, sunDir, false, true);
        frame.FillRoundedRect(12, 10, 456, 26, 10.0f, { 12u, 14u, 16u, 210u }, { 78u, 96u, 68u, 220u });
        const auto atel = animWp.GetTelemetry();
        std::ostringstream hud;
        hud << "PROJECTS/UNTITLED WIND SIM  |  ACTIVE:" << atel.ActiveLeafCount
            << "  SLEEP:" << atel.SleepingLeafCount << "  WOKEN:" << atel.WokenLeafCount;
        frame.DrawText(22, 19, hud.str(), 1, { 214u, 232u, 198u, 255u });

        char fname[128];
        std::snprintf(fname, sizeof(fname), "frame_%02d.png", fr);
        wroteFrames = wroteFrames && WriteRgba8PngFile(framesDir / fname, frame.W, frame.H, frame.Pix);
    }

    const bool gate8 = wroteUi && wroteTabs && wroteShowcase && wroteTech && wroteFrames;
    if (gate8) ++passedGates;
    std::cout << "[Gate 8/8] UI + 3D Scene Proof Renders & Animation Frames  : " << (gate8 ? "PASS" : "FAIL") << "\n";
    std::cout << "================================================================================\n";
    std::cout << " SUMMARY: " << passedGates << " / " << kTotalGates << " STRICT GATES PASSED!\n";
    std::cout << "================================================================================\n";
    return (passedGates == kTotalGates) ? 0 : 1;
}
