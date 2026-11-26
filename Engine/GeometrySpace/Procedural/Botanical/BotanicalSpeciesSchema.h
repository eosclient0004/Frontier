/*============================================================================================================================================
                                                        BOTANICALSPECIESSCHEMA.H
============================================================================================================================================*/
// 🧩 Data-driven .botanical species schema & registry (TOML-serialized via toml++, never .toml suffix).
//    Supports ~500 species via ParentArchetype prototype inheritance across Ferns, Palms, Trees, Shrubs, and Grasses.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Frontier::GeometrySpace::Botanical
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   CONSTANTS & ENUMS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr std::string_view kBotanicalSpeciesSuffix = ".botanical";

enum class BotanicalGrowthForm : uint8_t
{
    Fern      = 0u,
    Palm      = 1u,
    Tree      = 2u,
    Shrub     = 3u,
    Grass     = 4u,
    Vine      = 5u,
};

enum class BotanicalMotionMode : uint8_t
{
    ForceField  = 0u, // Live analytic ForceField sway (wind / fan jet / vortex)
    Vat         = 1u, // Two-channel VertexMotionAtlas (1 rotated +X sway LUT + 1 seamless rustle VAT)
    HeroPhysics = 2u, // WorldPartition CPU-promoted deterministic spring/capsule physics
};

enum class BotanicalCollisionClass : uint8_t
{
    SolidWoodCapsules = 0u, // Smooth deterministic Jolt capsule chain (trees, palm trunks)
    CompliantSpring   = 1u, // Deterministic 120 Hz 2-DOF damped angular spring (ferns, bushes, tall grass)
    DecorativeNone    = 2u, // Canopy leaves / non-colliding ground cover
};

//------------------------------------------------------------------------------------------------------------------------
//                                             BOTANICAL SPECIES DEFINITION
//------------------------------------------------------------------------------------------------------------------------

struct BotanicalTrunkSection
{
    float HeightMeters      = 6.0f;
    float BaseRadiusMeters  = 0.25f;
    float TaperExponent     = 0.65f;
    float FlareRatio        = 1.40f;
    float GnarlAmplitude    = 0.25f;
    float BarkNoiseAmp      = 0.65f;
    float BarkNoiseFreq     = 9.5f;
    float BreakCollarHeight = 0.0f;
};

struct BotanicalRootsSection
{
    uint32_t Count          = 6u;
    float    SpreadMeters   = 1.5f;
    float    ButtressHeight = 0.5f;
};

struct BotanicalFernSection
{
    uint32_t DetailTier          = 3u;
    float    CrownRadiusMeters   = 0.065f;
    float    CaudexHeightMeters  = 0.04f;
    float    RamentaDensity      = 0.70f;
    uint32_t FrondCount          = 16u;
    float    FrondLengthMeters   = 0.82f;
    float    FrondLengthVar      = 0.16f;
    float    FrondWidthMeters    = 0.18f;
    float    StipeFraction       = 0.18f;
    float    StipeRadiusMeters   = 0.0045f;
    float    RosetteAngleDeg     = 38.0f;
    float    ArchDroopDeg        = 68.0f;
    float    ArchPower           = 1.55f;
    float    RachisTwistDeg      = 12.0f;
    float    RachisWaveDeg       = 7.0f;
    uint32_t CrosierCount        = 4u;
    float    CrosierHeightMeters = 0.22f;
    float    CrosierTurns        = 2.6f;
    uint32_t PinnaPairs          = 26u;
    uint32_t PinnulePairs        = 12u;
    float    PinnaAngleDeg       = 68.0f;
    float    Auricle             = 0.55f;
    float    SerrationDepth      = 0.09f;
    uint32_t SerrationCount      = 16u;
    float    LaminaThicknessMm   = 0.55f;
    float    MidribRelief        = 0.90f;
    float    VeinRelief          = 0.85f;
    uint32_t VeinCount           = 14u;
    float    Bullation           = 0.42f;
    float    LaminaCamber        = 0.28f;
    float    EdgeUndulation      = 0.32f;
    float    TipCurl             = 0.22f;
    float    SoriCoverage        = 0.65f;
    float    SoriSizeMm          = 1.40f;
    float    Senescence          = 0.18f;
};

struct BotanicalPalmSection
{
    uint32_t DetailTier        = 3u;
    float    TrunkLeanDeg      = 6.5f;
    float    TrunkCurveDeg     = 10.0f;
    float    BasalBole         = 0.36f;
    uint32_t RootButtresses    = 6u;
    float    TrunkRelief       = 0.80f;
    float    CrownshaftRatio   = 0.0f;
    uint32_t ForkLevels        = 0u;
    uint32_t FrondCount        = 28u;
    float    FrondLengthMeters = 3.1f;
    float    FrondWidthMeters  = 0.96f;
    float    FrondArchDeg      = 72.0f;
    uint32_t LeafletPairs      = 30u;
    float    PlumoseSplay      = 0.18f;
    float    SkirtFraction     = 0.0f;
    bool     ShowFruits        = true;
    uint32_t FruitBunches      = 6u;
    float    FruitAbundance    = 1.05f;
    float    FruitRipeness     = 0.58f;
};

struct BotanicalTreeSection
{
    uint32_t DetailTier       = 3u;
    uint32_t BranchLevels     = 4u;
    uint32_t LimbCount        = 15u;
    float    BranchAngleDeg   = 52.0f;
    float    ForkTendency     = 0.42f;
    float    CanopyFlatten    = 0.15f;
    float    ButtressRelief   = 0.38f;
    uint32_t RootLaterals     = 2u;
    float    CollarFillet     = 0.76f;
    float    PipeConservation = 0.62f;
    bool     ShowLeaves       = true;
    float    LeafDensity      = 1.0f;
    float    LeafSizeMeters   = 0.24f;
};

struct BotanicalLeavesSection
{
    std::string LeafArchetypeSdf { "lobed_oak" };
    float       BladeScaleMeters = 0.18f;
    float       BladeAspect      = 0.55f;
    float       HalfThicknessMm  = 0.60f;
    float       CamberX          = 0.15f;
    float       CamberZ          = 0.22f;
    bool        CanDetach        = true;
    float       DetachWindMps    = 12.0f;
};

struct BotanicalMaterialsSection
{
    std::array<float, 3> WoodColorLinear      { 0.14f, 0.09f, 0.06f };
    std::array<float, 3> LeafBaseColorLinear  { 0.05f, 0.17f, 0.03f };
    std::array<float, 3> LeafYoungColorLinear { 0.21f, 0.46f, 0.06f };
    std::array<float, 3> FruitColorLinear     { 0.46f, 0.12f, 0.02f };
    std::string          WoodHueHex           { "#5c4633" };
    std::string          LeafBaseHueHex       { "#2f6b20" };
    std::string          LeafYoungHueHex      { "#74bd34" };
    std::string          FruitHueHex          { "#b85214" };
    float                Translucency         = 0.45f;
};

struct BotanicalWindMotionSection
{
    BotanicalMotionMode DefaultMotionMode = BotanicalMotionMode::ForceField;
    float               WindSensitivity   = 1.0f;
    float               StemStiffness     = 24.0f;
    float               StemDamping       = 2.8f;
    float               RustleFrequencyHz = 2.6f;
};

struct BotanicalCollisionSection
{
    BotanicalCollisionClass CollisionClass   = BotanicalCollisionClass::CompliantSpring;
    uint32_t                CapsuleCount     = 0u;
    float                   BendLimitDeg     = 55.0f;
    float                   VehiclePushGain  = 1.0f;
    bool                    ReplicateOverNet = true;
};

struct BotanicalSdfSection
{
    bool     UseNoisyCapsules = true;
    uint32_t CanopyBrickRes   = 16u;
    float    LodConeBias      = 1.0f;
};

struct BotanicalSpeciesDefinition
{
    uint32_t                   SchemaVersion   = 1u;
    std::string                SpeciesId       {};
    std::string                CommonName      {};
    std::string                BotanicalName   {};
    BotanicalGrowthForm        GrowthForm      = BotanicalGrowthForm::Fern;
    std::string                Architecture    {};
    std::string                ParentArchetype {};
    std::string                ThumbnailAsset  {};
    std::filesystem::path      SourcePath      {};

    BotanicalTrunkSection      Trunk           {};
    BotanicalRootsSection      Roots           {};
    BotanicalFernSection       Fern            {};
    BotanicalPalmSection       Palm            {};
    BotanicalTreeSection       Tree            {};
    BotanicalLeavesSection     Leaves          {};
    BotanicalMaterialsSection  Materials       {};
    BotanicalWindMotionSection WindAndMotion   {};
    BotanicalCollisionSection  Collision       {};
    BotanicalSdfSection        Sdf             {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                           PARSING, SERIALIZATION & REGISTRY
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::array<float, 3> ParseHexSrgbToLinearRgb(std::string_view hex, const std::array<float, 3>& fallback) noexcept;
[[nodiscard]] std::string_view     QueryGrowthFormToken(BotanicalGrowthForm form) noexcept;
[[nodiscard]] std::string_view     QueryMotionModeToken(BotanicalMotionMode mode) noexcept;
[[nodiscard]] std::string_view     QueryCollisionClassToken(BotanicalCollisionClass cls) noexcept;

/// Parses a .botanical TOML payload on top of an existing base species (enabling ParentArchetype inheritance).
[[nodiscard]] bool ParseBotanicalTomlOverlay(
    std::string_view            tomlText,
    BotanicalSpeciesDefinition& inOutSpecies,
    std::string&                outError);

/// Serializes a BotanicalSpeciesDefinition back to canonical .botanical TOML text.
[[nodiscard]] std::string SerializeSpeciesToBotanicalToml(const BotanicalSpeciesDefinition& species);

class BotanicalSpeciesRegistry
{
public:
    BotanicalSpeciesRegistry() = default;

    /// Loads all *.botanical files in speciesDirectory, resolving ParentArchetype inheritance chains.
    [[nodiscard]] bool LoadDirectory(const std::filesystem::path& speciesDirectory, std::string& outError);

    void Clear() noexcept;

    [[nodiscard]] const BotanicalSpeciesDefinition* FindById(std::string_view speciesId) const noexcept;
    [[nodiscard]] const std::vector<BotanicalSpeciesDefinition>& QueryAllSpecies() const noexcept { return OrderedSpecies_; }
    [[nodiscard]] std::size_t QueryCount() const noexcept { return OrderedSpecies_.size(); }

private:
    std::vector<BotanicalSpeciesDefinition>       OrderedSpecies_ {};
    std::unordered_map<std::string, std::size_t>  IndexById_      {};
};

} // namespace Frontier::GeometrySpace::Botanical
