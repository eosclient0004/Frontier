/*============================================================================================================================================
                                                       BOTANICALSPECIESSCHEMA.CPP
============================================================================================================================================*/
// 🧩 Implementation of .botanical species TOML parser (toml++), ParentArchetype resolver, and BotanicalSpeciesRegistry.

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Procedural/Botanical/BotanicalSpeciesSchema.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace Frontier::GeometrySpace::Botanical
{

namespace
{

[[nodiscard]] float SrgbChannelToLinear(float c) noexcept
{
    c = std::clamp(c, 0.0f, 1.0f);
    return (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] int HexDigit(char ch) noexcept
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
    if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
    return -1;
}

[[nodiscard]] BotanicalGrowthForm ParseGrowthForm(std::string_view tok, BotanicalGrowthForm fallback) noexcept
{
    if (tok == "Fern")  return BotanicalGrowthForm::Fern;
    if (tok == "Palm")  return BotanicalGrowthForm::Palm;
    if (tok == "Tree")  return BotanicalGrowthForm::Tree;
    if (tok == "Shrub") return BotanicalGrowthForm::Shrub;
    if (tok == "Grass") return BotanicalGrowthForm::Grass;
    if (tok == "Vine")  return BotanicalGrowthForm::Vine;
    return fallback;
}

[[nodiscard]] BotanicalMotionMode ParseMotionMode(std::string_view tok, BotanicalMotionMode fallback) noexcept
{
    if (tok == "ForceField")  return BotanicalMotionMode::ForceField;
    if (tok == "Vat")         return BotanicalMotionMode::Vat;
    if (tok == "HeroPhysics") return BotanicalMotionMode::HeroPhysics;
    return fallback;
}

[[nodiscard]] BotanicalCollisionClass ParseCollisionClass(std::string_view tok, BotanicalCollisionClass fallback) noexcept
{
    if (tok == "SolidWoodCapsules") return BotanicalCollisionClass::SolidWoodCapsules;
    if (tok == "CompliantSpring")   return BotanicalCollisionClass::CompliantSpring;
    if (tok == "DecorativeNone")    return BotanicalCollisionClass::DecorativeNone;
    return fallback;
}

template <typename T>
void ReadScalar(const toml::table* tbl, std::string_view key, T& dst) noexcept
{
    if (!tbl) return;
    if (const auto v = (*tbl)[key].value<double>(); v.has_value())
    {
        if constexpr (std::is_floating_point_v<T>)      dst = static_cast<T>(*v);
        else if constexpr (std::is_integral_v<T>)       dst = static_cast<T>(std::max(0.0, *v));
    }
    else if (const auto vi = (*tbl)[key].value<int64_t>(); vi.has_value())
    {
        if constexpr (std::is_floating_point_v<T>)      dst = static_cast<T>(*vi);
        else if constexpr (std::is_integral_v<T>)       dst = static_cast<T>(std::max<int64_t>(0, *vi));
    }
}

void ReadBool(const toml::table* tbl, std::string_view key, bool& dst) noexcept
{
    if (!tbl) return;
    if (const auto v = (*tbl)[key].value<bool>(); v.has_value()) dst = *v;
}

void ReadStr(const toml::table* tbl, std::string_view key, std::string& dst)
{
    if (!tbl) return;
    if (const auto v = (*tbl)[key].value<std::string>(); v.has_value()) dst = *v;
}

} // namespace

std::array<float, 3> ParseHexSrgbToLinearRgb(std::string_view hex, const std::array<float, 3>& fallback) noexcept
{
    if (!hex.empty() && hex.front() == '#') hex.remove_prefix(1u);
    if (hex.size() != 6u) return fallback;
    const int d0 = HexDigit(hex[0]), d1 = HexDigit(hex[1]);
    const int d2 = HexDigit(hex[2]), d3 = HexDigit(hex[3]);
    const int d4 = HexDigit(hex[4]), d5 = HexDigit(hex[5]);
    if (d0 < 0 || d1 < 0 || d2 < 0 || d3 < 0 || d4 < 0 || d5 < 0) return fallback;
    const float r = static_cast<float>((d0 << 4) | d1) / 255.0f;
    const float g = static_cast<float>((d2 << 4) | d3) / 255.0f;
    const float b = static_cast<float>((d4 << 4) | d5) / 255.0f;
    return { SrgbChannelToLinear(r), SrgbChannelToLinear(g), SrgbChannelToLinear(b) };
}

std::string_view QueryGrowthFormToken(BotanicalGrowthForm form) noexcept
{
    switch (form)
    {
        case BotanicalGrowthForm::Fern:  return "Fern";
        case BotanicalGrowthForm::Palm:  return "Palm";
        case BotanicalGrowthForm::Tree:  return "Tree";
        case BotanicalGrowthForm::Shrub: return "Shrub";
        case BotanicalGrowthForm::Grass: return "Grass";
        case BotanicalGrowthForm::Vine:  return "Vine";
    }
    return "Fern";
}

std::string_view QueryMotionModeToken(BotanicalMotionMode mode) noexcept
{
    switch (mode)
    {
        case BotanicalMotionMode::ForceField:  return "ForceField";
        case BotanicalMotionMode::Vat:         return "Vat";
        case BotanicalMotionMode::HeroPhysics: return "HeroPhysics";
    }
    return "ForceField";
}

std::string_view QueryCollisionClassToken(BotanicalCollisionClass cls) noexcept
{
    switch (cls)
    {
        case BotanicalCollisionClass::SolidWoodCapsules: return "SolidWoodCapsules";
        case BotanicalCollisionClass::CompliantSpring:   return "CompliantSpring";
        case BotanicalCollisionClass::DecorativeNone:    return "DecorativeNone";
    }
    return "CompliantSpring";
}

bool ParseBotanicalTomlOverlay(
    const std::string_view      tomlText,
    BotanicalSpeciesDefinition& sp,
    std::string&                outError)
{
    toml::table root {};
    try
    {
        root = toml::parse(tomlText);
    }
    catch (const toml::parse_error& err)
    {
        outError = std::string("Botanical TOML parse error: ") + std::string(err.description());
        return false;
    }

    if (const auto* c = root["Container"].as_table())
    {
        ReadScalar(c, "SchemaVersion", sp.SchemaVersion);
        ReadStr(c, "SpeciesId", sp.SpeciesId);
        ReadStr(c, "CommonName", sp.CommonName);
        ReadStr(c, "BotanicalName", sp.BotanicalName);
        ReadStr(c, "Architecture", sp.Architecture);
        ReadStr(c, "ParentArchetype", sp.ParentArchetype);
        ReadStr(c, "ThumbnailAsset", sp.ThumbnailAsset);
        std::string gf {};
        ReadStr(c, "GrowthForm", gf);
        if (!gf.empty()) sp.GrowthForm = ParseGrowthForm(gf, sp.GrowthForm);
    }

    if (const auto* t = root["Trunk"].as_table())
    {
        ReadScalar(t, "HeightMeters", sp.Trunk.HeightMeters);
        ReadScalar(t, "BaseRadiusMeters", sp.Trunk.BaseRadiusMeters);
        ReadScalar(t, "TaperExponent", sp.Trunk.TaperExponent);
        ReadScalar(t, "FlareRatio", sp.Trunk.FlareRatio);
        ReadScalar(t, "GnarlAmplitude", sp.Trunk.GnarlAmplitude);
        ReadScalar(t, "BarkNoiseAmp", sp.Trunk.BarkNoiseAmp);
        ReadScalar(t, "BarkNoiseFreq", sp.Trunk.BarkNoiseFreq);
        ReadScalar(t, "BreakCollarHeight", sp.Trunk.BreakCollarHeight);
    }

    if (const auto* r = root["Roots"].as_table())
    {
        ReadScalar(r, "Count", sp.Roots.Count);
        ReadScalar(r, "SpreadMeters", sp.Roots.SpreadMeters);
        ReadScalar(r, "ButtressHeight", sp.Roots.ButtressHeight);
    }

    if (const auto* f = root["Fern"].as_table())
    {
        ReadScalar(f, "DetailTier", sp.Fern.DetailTier);
        ReadScalar(f, "CrownRadiusMeters", sp.Fern.CrownRadiusMeters);
        ReadScalar(f, "CaudexHeightMeters", sp.Fern.CaudexHeightMeters);
        ReadScalar(f, "RamentaDensity", sp.Fern.RamentaDensity);
        ReadScalar(f, "FrondCount", sp.Fern.FrondCount);
        ReadScalar(f, "FrondLengthMeters", sp.Fern.FrondLengthMeters);
        ReadScalar(f, "FrondLengthVar", sp.Fern.FrondLengthVar);
        ReadScalar(f, "FrondWidthMeters", sp.Fern.FrondWidthMeters);
        ReadScalar(f, "StipeFraction", sp.Fern.StipeFraction);
        ReadScalar(f, "StipeRadiusMeters", sp.Fern.StipeRadiusMeters);
        ReadScalar(f, "RosetteAngleDeg", sp.Fern.RosetteAngleDeg);
        ReadScalar(f, "ArchDroopDeg", sp.Fern.ArchDroopDeg);
        ReadScalar(f, "ArchPower", sp.Fern.ArchPower);
        ReadScalar(f, "RachisTwistDeg", sp.Fern.RachisTwistDeg);
        ReadScalar(f, "RachisWaveDeg", sp.Fern.RachisWaveDeg);
        ReadScalar(f, "CrosierCount", sp.Fern.CrosierCount);
        ReadScalar(f, "CrosierHeightMeters", sp.Fern.CrosierHeightMeters);
        ReadScalar(f, "CrosierTurns", sp.Fern.CrosierTurns);
        ReadScalar(f, "PinnaPairs", sp.Fern.PinnaPairs);
        ReadScalar(f, "PinnulePairs", sp.Fern.PinnulePairs);
        ReadScalar(f, "PinnaAngleDeg", sp.Fern.PinnaAngleDeg);
        ReadScalar(f, "Auricle", sp.Fern.Auricle);
        ReadScalar(f, "SerrationDepth", sp.Fern.SerrationDepth);
        ReadScalar(f, "SerrationCount", sp.Fern.SerrationCount);
        ReadScalar(f, "LaminaThicknessMm", sp.Fern.LaminaThicknessMm);
        ReadScalar(f, "MidribRelief", sp.Fern.MidribRelief);
        ReadScalar(f, "VeinRelief", sp.Fern.VeinRelief);
        ReadScalar(f, "VeinCount", sp.Fern.VeinCount);
        ReadScalar(f, "Bullation", sp.Fern.Bullation);
        ReadScalar(f, "LaminaCamber", sp.Fern.LaminaCamber);
        ReadScalar(f, "EdgeUndulation", sp.Fern.EdgeUndulation);
        ReadScalar(f, "TipCurl", sp.Fern.TipCurl);
        ReadScalar(f, "SoriCoverage", sp.Fern.SoriCoverage);
        ReadScalar(f, "SoriSizeMm", sp.Fern.SoriSizeMm);
        ReadScalar(f, "Senescence", sp.Fern.Senescence);
    }

    if (const auto* p = root["Palm"].as_table())
    {
        ReadScalar(p, "DetailTier", sp.Palm.DetailTier);
        ReadScalar(p, "TrunkLeanDeg", sp.Palm.TrunkLeanDeg);
        ReadScalar(p, "TrunkCurveDeg", sp.Palm.TrunkCurveDeg);
        ReadScalar(p, "BasalBole", sp.Palm.BasalBole);
        ReadScalar(p, "RootButtresses", sp.Palm.RootButtresses);
        ReadScalar(p, "TrunkRelief", sp.Palm.TrunkRelief);
        ReadScalar(p, "CrownshaftRatio", sp.Palm.CrownshaftRatio);
        ReadScalar(p, "ForkLevels", sp.Palm.ForkLevels);
        ReadScalar(p, "FrondCount", sp.Palm.FrondCount);
        ReadScalar(p, "FrondLengthMeters", sp.Palm.FrondLengthMeters);
        ReadScalar(p, "FrondWidthMeters", sp.Palm.FrondWidthMeters);
        ReadScalar(p, "FrondArchDeg", sp.Palm.FrondArchDeg);
        ReadScalar(p, "SkirtFraction", sp.Palm.SkirtFraction);
        ReadScalar(p, "LeafletPairs", sp.Palm.LeafletPairs);
        ReadScalar(p, "PlumoseSplay", sp.Palm.PlumoseSplay);
        ReadBool(p, "ShowFruits", sp.Palm.ShowFruits);
        ReadScalar(p, "FruitBunches", sp.Palm.FruitBunches);
        ReadScalar(p, "FruitAbundance", sp.Palm.FruitAbundance);
        ReadScalar(p, "FruitRipeness", sp.Palm.FruitRipeness);
    }

    if (const auto* tr = root["Tree"].as_table())
    {
        ReadScalar(tr, "DetailTier", sp.Tree.DetailTier);
        ReadScalar(tr, "BranchLevels", sp.Tree.BranchLevels);
        ReadScalar(tr, "LimbCount", sp.Tree.LimbCount);
        ReadScalar(tr, "BranchAngleDeg", sp.Tree.BranchAngleDeg);
        ReadScalar(tr, "ForkTendency", sp.Tree.ForkTendency);
        ReadScalar(tr, "CanopyFlatten", sp.Tree.CanopyFlatten);
        ReadScalar(tr, "ButtressRelief", sp.Tree.ButtressRelief);
        ReadScalar(tr, "RootLaterals", sp.Tree.RootLaterals);
        ReadScalar(tr, "CollarFillet", sp.Tree.CollarFillet);
        ReadScalar(tr, "PipeConservation", sp.Tree.PipeConservation);
        ReadBool(tr, "ShowLeaves", sp.Tree.ShowLeaves);
        ReadScalar(tr, "LeafDensity", sp.Tree.LeafDensity);
        ReadScalar(tr, "LeafSizeMeters", sp.Tree.LeafSizeMeters);
    }

    if (const auto* lv = root["Leaves"].as_table())
    {
        ReadStr(lv, "LeafArchetypeSdf", sp.Leaves.LeafArchetypeSdf);
        ReadScalar(lv, "BladeScaleMeters", sp.Leaves.BladeScaleMeters);
        ReadScalar(lv, "BladeAspect", sp.Leaves.BladeAspect);
        ReadScalar(lv, "HalfThicknessMm", sp.Leaves.HalfThicknessMm);
        ReadScalar(lv, "CamberX", sp.Leaves.CamberX);
        ReadScalar(lv, "CamberZ", sp.Leaves.CamberZ);
        ReadBool(lv, "CanDetach", sp.Leaves.CanDetach);
        ReadScalar(lv, "DetachWindMps", sp.Leaves.DetachWindMps);
    }

    if (const auto* m = root["Materials"].as_table())
    {
        ReadStr(m, "WoodHueHex", sp.Materials.WoodHueHex);
        ReadStr(m, "LeafBaseHueHex", sp.Materials.LeafBaseHueHex);
        ReadStr(m, "LeafYoungHueHex", sp.Materials.LeafYoungHueHex);
        ReadStr(m, "FruitHueHex", sp.Materials.FruitHueHex);
        ReadScalar(m, "Translucency", sp.Materials.Translucency);
    }
    sp.Materials.WoodColorLinear      = ParseHexSrgbToLinearRgb(sp.Materials.WoodHueHex,      sp.Materials.WoodColorLinear);
    sp.Materials.LeafBaseColorLinear  = ParseHexSrgbToLinearRgb(sp.Materials.LeafBaseHueHex,  sp.Materials.LeafBaseColorLinear);
    sp.Materials.LeafYoungColorLinear = ParseHexSrgbToLinearRgb(sp.Materials.LeafYoungHueHex, sp.Materials.LeafYoungColorLinear);
    sp.Materials.FruitColorLinear     = ParseHexSrgbToLinearRgb(sp.Materials.FruitHueHex,     sp.Materials.FruitColorLinear);

    if (const auto* wm = root["WindAndMotion"].as_table())
    {
        std::string mm {};
        ReadStr(wm, "DefaultMotionMode", mm);
        if (!mm.empty()) sp.WindAndMotion.DefaultMotionMode = ParseMotionMode(mm, sp.WindAndMotion.DefaultMotionMode);
        ReadScalar(wm, "WindSensitivity", sp.WindAndMotion.WindSensitivity);
        ReadScalar(wm, "StemStiffness", sp.WindAndMotion.StemStiffness);
        ReadScalar(wm, "StemDamping", sp.WindAndMotion.StemDamping);
        ReadScalar(wm, "RustleFrequencyHz", sp.WindAndMotion.RustleFrequencyHz);
    }

    if (const auto* col = root["Collision"].as_table())
    {
        std::string cc {};
        ReadStr(col, "CollisionClass", cc);
        if (!cc.empty()) sp.Collision.CollisionClass = ParseCollisionClass(cc, sp.Collision.CollisionClass);
        ReadScalar(col, "CapsuleCount", sp.Collision.CapsuleCount);
        ReadScalar(col, "BendLimitDeg", sp.Collision.BendLimitDeg);
        ReadScalar(col, "VehiclePushGain", sp.Collision.VehiclePushGain);
        ReadBool(col, "ReplicateOverNet", sp.Collision.ReplicateOverNet);
    }

    if (const auto* sdf = root["Sdf"].as_table())
    {
        ReadBool(sdf, "UseNoisyCapsules", sp.Sdf.UseNoisyCapsules);
        ReadScalar(sdf, "CanopyBrickRes", sp.Sdf.CanopyBrickRes);
        ReadScalar(sdf, "LodConeBias", sp.Sdf.LodConeBias);
    }

    return true;
}

std::string SerializeSpeciesToBotanicalToml(const BotanicalSpeciesDefinition& sp)
{
    std::ostringstream os {};
    os << "# Frontier Engine — Botanical Species Asset (.botanical)\n";
    os << "[Container]\n";
    os << "FormatSuffix = \".botanical\"\n";
    os << "SchemaVersion = " << sp.SchemaVersion << "\n";
    os << "SpeciesId = \"" << sp.SpeciesId << "\"\n";
    os << "CommonName = \"" << sp.CommonName << "\"\n";
    os << "BotanicalName = \"" << sp.BotanicalName << "\"\n";
    os << "GrowthForm = \"" << QueryGrowthFormToken(sp.GrowthForm) << "\"\n";
    os << "Architecture = \"" << sp.Architecture << "\"\n";
    if (!sp.ParentArchetype.empty()) os << "ParentArchetype = \"" << sp.ParentArchetype << "\"\n";
    if (!sp.ThumbnailAsset.empty())  os << "ThumbnailAsset = \"" << sp.ThumbnailAsset << "\"\n";
    os << "\n[Trunk]\n";
    os << "HeightMeters = " << sp.Trunk.HeightMeters << "\n";
    os << "BaseRadiusMeters = " << sp.Trunk.BaseRadiusMeters << "\n";
    os << "BarkNoiseAmp = " << sp.Trunk.BarkNoiseAmp << "\n";
    os << "BarkNoiseFreq = " << sp.Trunk.BarkNoiseFreq << "\n";
    os << "\n[Leaves]\n";
    os << "LeafArchetypeSdf = \"" << sp.Leaves.LeafArchetypeSdf << "\"\n";
    os << "BladeScaleMeters = " << sp.Leaves.BladeScaleMeters << "\n";
    os << "HalfThicknessMm = " << sp.Leaves.HalfThicknessMm << "\n";
    os << "CanDetach = " << (sp.Leaves.CanDetach ? "true" : "false") << "\n";
    os << "DetachWindMps = " << sp.Leaves.DetachWindMps << "\n";
    os << "\n[Materials]\n";
    os << "WoodHueHex = \"" << sp.Materials.WoodHueHex << "\"\n";
    os << "LeafBaseHueHex = \"" << sp.Materials.LeafBaseHueHex << "\"\n";
    os << "LeafYoungHueHex = \"" << sp.Materials.LeafYoungHueHex << "\"\n";
    os << "FruitHueHex = \"" << sp.Materials.FruitHueHex << "\"\n";
    os << "\n[WindAndMotion]\n";
    os << "DefaultMotionMode = \"" << QueryMotionModeToken(sp.WindAndMotion.DefaultMotionMode) << "\"\n";
    os << "WindSensitivity = " << sp.WindAndMotion.WindSensitivity << "\n";
    os << "StemStiffness = " << sp.WindAndMotion.StemStiffness << "\n";
    os << "StemDamping = " << sp.WindAndMotion.StemDamping << "\n";
    os << "\n[Collision]\n";
    os << "CollisionClass = \"" << QueryCollisionClassToken(sp.Collision.CollisionClass) << "\"\n";
    os << "ReplicateOverNet = " << (sp.Collision.ReplicateOverNet ? "true" : "false") << "\n";
    os << "\n[Sdf]\n";
    os << "UseNoisyCapsules = " << (sp.Sdf.UseNoisyCapsules ? "true" : "false") << "\n";
    os << "CanopyBrickRes = " << sp.Sdf.CanopyBrickRes << "\n";
    return os.str();
}

void BotanicalSpeciesRegistry::Clear() noexcept
{
    OrderedSpecies_.clear();
    IndexById_.clear();
}

bool BotanicalSpeciesRegistry::LoadDirectory(const std::filesystem::path& speciesDirectory, std::string& outError)
{
    Clear();
    std::error_code ec {};
    if (!std::filesystem::exists(speciesDirectory, ec) || !std::filesystem::is_directory(speciesDirectory, ec))
    {
        outError = "Botanical species directory not found: " + speciesDirectory.string();
        return false;
    }

    struct RawEntry
    {
        std::filesystem::path Path {};
        std::string           Text {};
        std::string           SpeciesId {};
        std::string           ParentId {};
    };
    std::vector<RawEntry> raws {};

    for (const auto& ent : std::filesystem::directory_iterator(speciesDirectory, ec))
    {
        if (!ent.is_regular_file()) continue;
        if (ent.path().extension() != kBotanicalSpeciesSuffix) continue;
        std::ifstream f(ent.path(), std::ios::binary);
        if (!f) continue;
        std::ostringstream ss {};
        ss << f.rdbuf();
        RawEntry re {};
        re.Path = ent.path();
        re.Text = ss.str();

        BotanicalSpeciesDefinition probe {};
        std::string err {};
        if (!ParseBotanicalTomlOverlay(re.Text, probe, err))
        {
            outError = ent.path().filename().string() + ": " + err;
            return false;
        }
        re.SpeciesId = probe.SpeciesId.empty() ? ent.path().stem().string() : probe.SpeciesId;
        re.ParentId  = probe.ParentArchetype;
        raws.push_back(std::move(re));
    }

    std::sort(raws.begin(), raws.end(), [](const RawEntry& a, const RawEntry& b) { return a.SpeciesId < b.SpeciesId; });

    std::unordered_map<std::string, std::size_t> rawIdx {};
    for (std::size_t i = 0u; i < raws.size(); ++i) rawIdx[raws[i].SpeciesId] = i;

    // Resolve inheritance chains (depth <= 8 prevents cycles cleanly)
    for (const auto& re : raws)
    {
        std::vector<std::size_t> chain {};
        std::string cur = re.SpeciesId;
        for (int depth = 0; depth < 8; ++depth)
        {
            const auto it = rawIdx.find(cur);
            if (it == rawIdx.end()) break;
            chain.push_back(it->second);
            if (raws[it->second].ParentId.empty()) break;
            cur = raws[it->second].ParentId;
        }
        std::reverse(chain.begin(), chain.end());

        BotanicalSpeciesDefinition resolved {};
        for (const std::size_t idx : chain)
        {
            std::string err {};
            if (!ParseBotanicalTomlOverlay(raws[idx].Text, resolved, err))
            {
                outError = raws[idx].Path.filename().string() + ": " + err;
                return false;
            }
        }
        if (resolved.SpeciesId.empty()) resolved.SpeciesId = re.SpeciesId;
        resolved.SourcePath = re.Path;
        IndexById_[resolved.SpeciesId] = OrderedSpecies_.size();
        OrderedSpecies_.push_back(std::move(resolved));
    }

    return !OrderedSpecies_.empty();
}

const BotanicalSpeciesDefinition* BotanicalSpeciesRegistry::FindById(const std::string_view speciesId) const noexcept
{
    const auto it = IndexById_.find(std::string(speciesId));
    return (it != IndexById_.end()) ? &OrderedSpecies_[it->second] : nullptr;
}

} // namespace Frontier::GeometrySpace::Botanical
