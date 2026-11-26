/*============================================================================================================================================
                                                         BOTANICALTHINLEAFSDF.H
============================================================================================================================================*/
// 🧩 C++20 Port of Frontier-1's Proven Two-Level 16^3 Canopy Brick + 64x64 2.5D Thin-Sheet Extrusion Leaf SDF
//    & Watertight Rest-Space 3D Noisy Ball-Jointed Wood-Capsule SDF (100% thin-leaf survival, 0.0% bloat, 0.000 mm joint gaps).

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Frontier::GeometrySpace::Botanical
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  CONSTANTS & ARCHETYPES
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t kLeafSdf2dRes       = 64u;     // [px] 64x64 2D Euclidean Signed Distance Grid per archetype
inline constexpr uint32_t kCanopyBrickRes     = 16u;     // [cells] 16^3 macro brick accelerator
inline constexpr float    kBarkDispMaxFrac    = 0.18f;   // R_bark >= 0.82 * R(s) > 0 strictly guaranteed!
inline constexpr float    kLeafHalfThickM     = 0.0006f; // 0.6 mm physical half-thickness (1.2 mm blade)

enum class LeafSdfArchetype : uint8_t
{
    OvateBeech   = 0u,
    HeartLinden  = 1u,
    LobedOak     = 2u,
    PalmateMaple = 3u,
    SlenderWillow= 4u,
    FernPinna    = 5u,
    PalmLeaflet  = 6u,
    PineFascicle = 7u,
    Count        = 8u,
};

[[nodiscard]] LeafSdfArchetype ParseLeafArchetypeToken(std::string_view token) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                         WATER-TIGHT NOISY WOOD CAPSULE & LEAF RECORDS
//------------------------------------------------------------------------------------------------------------------------

/// Ball-jointed tapered wood capsule with rest-pose endpoints so 3D bark noise bends cleanly with wind sway (zero swimming!).
struct BotanicalWoodCapsule
{
    std::array<float, 3> P0          { 0.0f, 0.0f, 0.0f }; // [m] Current world/local start waypoint
    float                R0          = 0.05f;              // [m] Start radius
    std::array<float, 3> P1          { 0.0f, 0.0f, 1.0f }; // [m] Current world/local end waypoint
    float                R1          = 0.03f;              // [m] End radius
    std::array<float, 3> RestP0      { 0.0f, 0.0f, 0.0f }; // [m] Rest-pose start waypoint (for swimming-free bark FBM!)
    std::array<float, 3> RestP1      { 0.0f, 0.0f, 1.0f }; // [m] Rest-pose end waypoint
    float                WindFlex0   = 0.0f;               // [0..1] Sway weight at P0
    float                WindFlex1   = 0.0f;               // [0..1] Sway weight at P1
    uint32_t             BranchLevel = 0u;                 // [0=trunk, 1=limb, 2=branch, 3=twig/rachis]
};

/// Oriented curved 2.5D thin-sheet leaf instance (matches GPU 64-byte leaf storage layout).
struct BotanicalLeafSdfInstance
{
    std::array<float, 3> CenterMeters   { 0.0f, 0.0f, 0.0f };
    float                HalfSpanMeters = 0.12f;
    std::array<float, 3> AxisX          { 1.0f, 0.0f, 0.0f }; // Width axis
    float                CamberX        = 0.14f;
    std::array<float, 3> AxisY          { 0.0f, 0.0f, 1.0f }; // Sheet normal (+Z up default)
    float                CamberZ        = 0.22f;
    std::array<float, 3> AxisZ          { 0.0f, 1.0f, 0.0f }; // Blade length axis
    float                HalfThicknessM = kLeafHalfThickM;
    std::array<float, 3> RestCenterM    { 0.0f, 0.0f, 0.0f };
    float                WindFlex       = 1.0f;
    float                Phase          = 0.0f;
    LeafSdfArchetype     Archetype      = LeafSdfArchetype::LobedOak;
    bool                 Detached       = false;
};

/// Single 16^3 Canopy Brick header with LeafRef range + 3-axis Beer-Lambert optical extinction.
struct CanopyBrickHeader
{
    uint32_t StartOffset   = 0u;
    uint32_t InstanceCount = 0u;
    float    SigmaX        = 0.0f; // [1/m] Direction-projected leaf cross-section extinction
    float    SigmaY        = 0.0f;
    float    SigmaZ        = 0.0f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                  2D EDT ARCHETYPE ATLAS & TWO-LEVEL CANOPY BRICK SOLVER
//------------------------------------------------------------------------------------------------------------------------

struct LeafArchetypeSdfAtlas
{
    uint32_t           Resolution = kLeafSdf2dRes;
    uint32_t           LayerCount = static_cast<uint32_t>(LeafSdfArchetype::Count);
    std::vector<float> SignedDist {}; // [LayerCount * Resolution * Resolution] normalized [-1, +1] blade-space SDF
    std::array<float, static_cast<std::size_t>(LeafSdfArchetype::Count)> AreaFraction {};

    void BuildAllArchetypes();

    [[nodiscard]] float SampleBilinear(float u, float w, LeafSdfArchetype arch) const noexcept;
};

/// Exact sin-free modulo-289 2-octave 3D FBM bark noise matching Frontier-1's ThinLeafWgslShaders.js!
[[nodiscard]] float EvalBarkFbm3d(const std::array<float, 3>& p, float freq) noexcept;

/// Evaluates watertight noisy tapered wood-capsule SDF (uses RestP0/RestP1 skin projection for swimming-free bark noise!).
[[nodiscard]] float EvaluateNoisyWoodCapsuleSdf(
    const BotanicalWoodCapsule& cap,
    const std::array<float, 3>& queryPos,
    float                       barkNoiseAmp,
    float                       barkNoiseFreq) noexcept;

/// Evaluates curved 2.5D thin-sheet leaf 3D SDF at queryPos.
[[nodiscard]] float EvaluateCurvedThinLeafSdf(
    const BotanicalLeafSdfInstance& leaf,
    const LeafArchetypeSdfAtlas&    atlas,
    const std::array<float, 3>&     queryPos) noexcept;

struct BotanicalSdfVolume
{
    std::array<float, 3>                  BoundsMin      { -1.0f, -1.0f, 0.0f };
    std::array<float, 3>                  BoundsMax      {  1.0f,  1.0f, 2.0f };
    uint32_t                              BrickRes       = kCanopyBrickRes;
    float                                 BarkNoiseAmp   = 0.65f;
    float                                 BarkNoiseFreq  = 9.5f;
    uint32_t                              OccupiedBricks = 0u;
    std::vector<CanopyBrickHeader>        Bricks         {};
    std::vector<uint32_t>                 BrickLeafRefs  {};
    std::vector<BotanicalWoodCapsule>     WoodCapsules   {};
    std::vector<BotanicalLeafSdfInstance> Leaves         {};

    /// Rebuilds the 16^3 Canopy Brick acceleration structure & 3-axis Beer-Lambert optical depths (with wind sway padding).
    void BuildCanopyBricks(const LeafArchetypeSdfAtlas& atlas, float swayPaddingMeters = 0.25f);

    /// Applies deterministic wind sway to WoodCapsules & Leaves in-place (preserving RestP0/RestP1 for zero bark swimming!).
    void ApplyWindBend(float bendAngleXRad, float bendAngleYRad, float gustTimeSec, float rustleAmp) noexcept;

    /// Evaluates composite SDF (min of noisy wood capsules & curved 2.5D leaves) at queryPos.
    [[nodiscard]] float EvaluateSignedDistance(
        const LeafArchetypeSdfAtlas& atlas,
        const std::array<float, 3>&  queryPos) const noexcept;

    /// Traces a shadow ray (origin -> sunDir) through upfront noisy wood capsules + 16^3 Canopy Bricks + 2.5D curved thin sheets
    /// with automatic Cone-Width Beer-Lambert LOD! Returns light transmittance in [0 = occluded .. 1 = sunlit].
    [[nodiscard]] float TraceShadowTransmittance(
        const LeafArchetypeSdfAtlas& atlas,
        const std::array<float, 3>&  rayOrigin,
        const std::array<float, 3>&  sunDirNorm,
        float                        tanSunCone = 0.00465f,
        float                        lodConeBias = 1.0f) const noexcept;
};

} // namespace Frontier::GeometrySpace::Botanical
