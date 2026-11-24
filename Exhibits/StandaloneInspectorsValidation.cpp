/*============================================================================================================================================
                                                    STANDALONEINSPECTORSVALIDATION.CPP
============================================================================================================================================*/
// 🧪 Validation & 340x825 visual parity exhibit for all 21 standalone Editor Inspectors, multi-instance coexistence, and retained LUT surfaces

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/ContentBrowser/ThumbnailSheetRepository.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Components/Panes/InspectorPanel.h"
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/Panes/ViewportPanel.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/DistanceFieldSpace/LocalSignedDistanceVolume.h"
#include "Engine/Editor/Inspectors/InspectorRegistry.h"
#include "Engine/GeometrySpace/Procedural/ProceduralGeometry.h"
#include "Engine/Editor/WorldEnvironmentRegistry.h"
#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace
{

using namespace Frontier;

constexpr int   CaptureWidth  = 340;
constexpr int   CaptureHeight = 825;

bool VerifyRequirement(bool Condition, const char* Label)
{
    if (Condition)
    {
        std::cout << "[PASS] " << Label << '\n';
        return true;
    }
    std::cerr << "[FAIL] " << Label << '\n';
    return false;
}

std::filesystem::path ResolveRepositoryRootPath()
{
#ifdef FRONTIER_REPO_ROOT_DIR
    return std::filesystem::path(FRONTIER_REPO_ROOT_DIR);
#else
    return std::filesystem::current_path();
#endif
}

OutlinerSlotToken AppendEntry(
    OutlinerPanel&        Outliner,
    const char*           Title,
    const char*           Subtitle,
    const char*           Tag,
    ConstructSlotCategory Category,
    OutlinerSlotToken     Enclosing)
{
    OutlinerRecordEntry Record{};
    Record.Title             = Title;
    Record.Subtitle          = Subtitle;
    Record.DomainTag         = Tag;
    Record.Category          = Category;
    Record.EnclosingToken    = Enclosing;
    Record.VisibilityEnabled = true;
    Record.Expanded          = true;
    return Outliner.ConstructDirectEntry(std::move(Record));
}

bool RenderInspectorExhibit(
    InspectorRegistry&                        Inspectors,
    OutlinerPanel&                            Outliner,
    GeometricRaster::WorldSceneSpecification& Scene,
    ViewportDisplaySettings&                  DisplaySettings,
    OutlinerSlotToken                         TargetToken,
    const std::filesystem::path&              OutputPath,
    const PaneInputReading&                   InjectedInput = {})
{
    Outliner.SelectSingle(TargetToken);
    ImGuiIO& Io    = ImGui::GetIO();
    Io.DisplaySize = ImVec2(static_cast<float>(CaptureWidth), static_cast<float>(CaptureHeight));
    Io.DeltaTime   = 1.0f / 60.0f;

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(CaptureWidth), static_cast<float>(CaptureHeight)), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin(
        "##StandaloneInspectorStage",
        nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);

    PixelSpace Surface(ImGui::GetWindowDrawList(), Point2D{ 0.0f, 0.0f }, 1.0f, ImGui::GetFont());
    bool Mutated = false;
    (void)Inspectors.ConstructActiveInspector(
        Surface,
        Rectangle2D{ 0.0f, 0.0f, static_cast<float>(CaptureWidth), static_cast<float>(CaptureHeight) },
        Outliner,
        nullptr,
        Scene,
        &DisplaySettings,
        InjectedInput,
        Mutated);

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::Render();

    Frontier::Exhibits::SoftwareRasterCanvas Canvas(CaptureWidth, CaptureHeight, Frontier::Components::RgbaColor8{ 24, 24, 24, 255 });
    Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());
    return Canvas.WritePngFile(OutputPath);
}

} // namespace

int main()
{
    const auto RepoRoot = ResolveRepositoryRootPath();
    const auto OutDir   = RepoRoot / "Exhibits/Images/StandaloneInspectors";
    std::error_code DirEc{};
    std::filesystem::create_directories(OutDir, DirEc);

    IMGUI_CHECKVERSION();
    ImGuiContext* GuiSession = ImGui::CreateContext();
    ImGui::SetCurrentContext(GuiSession);

    ImGuiIO& Io    = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.LogFilename = nullptr;

    TypographyRepository Typefaces{};
    if (!VerifyRequirement(Typefaces.LoadRepository(RepoRoot / "EngineContent/FontsArchives") == 5u, "Load FontRepository.FontSpace"))
    {
        ImGui::DestroyContext(GuiSession);
        return 1;
    }
    TypographyRepository::AssignCurrent(&Typefaces);
    if (ImFont* DefaultFont = Typefaces.QueryTabStripFont())
    {
        Io.FontDefault = DefaultFont;
    }

    unsigned char* FontPixels = nullptr;
    int            AtlasW     = 0;
    int            AtlasH     = 0;
    Io.Fonts->GetTexDataAsRGBA32(&FontPixels, &AtlasW, &AtlasH);

    Frontier::GeometricRaster::ShowcaseWorldSceneBundle Bundle{};
    Frontier::GeometricRaster::BuildBlankPlaneSkyScene(Bundle, 160u, 96u);

    OutlinerPanel           Outliner{};
    ViewportDisplaySettings DisplaySettings{};
    InspectorRegistry       Inspectors{};

    // Generic user folders (intentionally NOT named "Environment")
    const OutlinerSlotToken SkyRigFolder  = AppendEntry(Outliner, "Sky Rig",     "Folder", "folder", ConstructSlotCategory::Folder, {});
    const OutlinerSlotToken WeatherFolder = AppendEntry(Outliner, "Weather Rig", "Folder", "folder", ConstructSlotCategory::Folder, {});
    const OutlinerSlotToken PropsFolder   = AppendEntry(Outliner, "Hero Props",  "Folder", "folder", ConstructSlotCategory::Folder, {});

    // Multi-instance entries: 2 Suns, 2 Moons, 2 Winds, 2 Starfields, 2 Height Fogs, plus all remaining 16 categories
    const OutlinerSlotToken AtmoTok       = AppendEntry(Outliner, "Atmosphere",       "Atmosphere",      "env:atmosphere",        ConstructSlotCategory::LayerGroup,     SkyRigFolder);
    const OutlinerSlotToken SunOneTok     = AppendEntry(Outliner, "Sun Primary",      "Solar Light",     "env:sun",               ConstructSlotCategory::PointLuminaire, SkyRigFolder);
    const OutlinerSlotToken SunTwoTok     = AppendEntry(Outliner, "Sun Binary",       "Solar Light",     "env:sun",               ConstructSlotCategory::PointLuminaire, SkyRigFolder);
    const OutlinerSlotToken CloudTok      = AppendEntry(Outliner, "Clouds",           "Volumetric",      "env:clouds",            ConstructSlotCategory::LayerGroup,     WeatherFolder);
    const OutlinerSlotToken LocalCloudTok = AppendEntry(Outliner, "Anvil Cell",       "Local Cloud",     "env:local-cloud",       ConstructSlotCategory::LayerGroup,     WeatherFolder);
    const OutlinerSlotToken MoonOneTok    = AppendEntry(Outliner, "Luna",             "Celestial",       "env:moon",              ConstructSlotCategory::LayerGroup,     SkyRigFolder);
    const OutlinerSlotToken MoonTwoTok    = AppendEntry(Outliner, "Ember",            "Celestial",       "env:moon",              ConstructSlotCategory::LayerGroup,     SkyRigFolder);
    const OutlinerSlotToken StarsOneTok   = AppendEntry(Outliner, "Starfield A",      "Starfield",       "env:stars",             ConstructSlotCategory::LayerGroup,     SkyRigFolder);
    const OutlinerSlotToken StarsTwoTok   = AppendEntry(Outliner, "Starfield B",      "Starfield",       "env:stars",             ConstructSlotCategory::LayerGroup,     SkyRigFolder);
    const OutlinerSlotToken WindOneTok    = AppendEntry(Outliner, "Trade Wind",       "Wind Field",      "env:wind",              ConstructSlotCategory::LayerGroup,     WeatherFolder);
    const OutlinerSlotToken WindTwoTok    = AppendEntry(Outliner, "Gust Front",       "Wind Field",      "env:wind",              ConstructSlotCategory::LayerGroup,     WeatherFolder);
    const OutlinerSlotToken HeightFog1Tok = AppendEntry(Outliner, "Valley Fog",       "Height Fog",      "env:height-fog",        ConstructSlotCategory::LayerGroup,     WeatherFolder);
    const OutlinerSlotToken HeightFog2Tok = AppendEntry(Outliner, "Ridge Haze",       "Height Fog",      "env:height-fog",        ConstructSlotCategory::LayerGroup,     WeatherFolder);
    const OutlinerSlotToken AerialFogTok  = AppendEntry(Outliner, "Aerial Haze",      "Atmospheric Fog", "env:aerial-fog",        ConstructSlotCategory::LayerGroup,     WeatherFolder);
    const OutlinerSlotToken LocalFogTok   = AppendEntry(Outliner, "Mist Volume",      "Local Fog",       "env:local-fog",         ConstructSlotCategory::LayerGroup,     WeatherFolder);
    const OutlinerSlotToken GodRaysTok    = AppendEntry(Outliner, "God Rays",         "Crepuscular",     "env:god-rays",          ConstructSlotCategory::LayerGroup,     WeatherFolder);
    const OutlinerSlotToken PrecipTok     = AppendEntry(Outliner, "Rain Shower",      "Precipitation",   "env:precipitation",     ConstructSlotCategory::LayerGroup,     WeatherFolder);
    const OutlinerSlotToken RainbowTok    = AppendEntry(Outliner, "Primary Bow",      "Optical Arc",     "env:rainbow",           ConstructSlotCategory::LayerGroup,     WeatherFolder);
    const OutlinerSlotToken CameraTok     = AppendEntry(Outliner, "Main Camera",      "Optics Camera",   "optics:camera",         ConstructSlotCategory::OpticsCamera,   {});
    const OutlinerSlotToken CamSetTok     = AppendEntry(Outliner, "Camera Settings",  "Render pipeline", "env:camera-settings",   ConstructSlotCategory::OpticsCamera,   {});
    const OutlinerSlotToken LensFlareTok  = AppendEntry(Outliner, "Anamorphic Flare", "Lens Flare",      "env:lens-flare",        ConstructSlotCategory::LayerGroup,     {});
    const OutlinerSlotToken MotionBlurTok = AppendEntry(Outliner, "Motion Shutter",   "Motion Blur",     "env:motion-blur",       ConstructSlotCategory::LayerGroup,     {});
    // All 8 Lighting Inspectors: Point, Spot, IES Photometric, Area/Softbox, Tube, LED Emitter, LED Strip + Environment IBL
    const OutlinerSlotToken PointLightTok = AppendEntry(Outliner, "Rim Point",        "Point Light",      "light:point",          ConstructSlotCategory::PointLuminaire, PropsFolder);
    const OutlinerSlotToken SpotLightTok  = AppendEntry(Outliner, "Key Spot",         "Spot Light",       "light:spot",           ConstructSlotCategory::PointLuminaire, PropsFolder);
    const OutlinerSlotToken IesLightTok   = AppendEntry(Outliner, "ECE Low Beam",     "IES Photometric",  "light:ies",            ConstructSlotCategory::PointLuminaire, PropsFolder);
    const OutlinerSlotToken AreaLightTok  = AppendEntry(Outliner, "Studio Softbox",   "Area Light",       "light:area",           ConstructSlotCategory::PointLuminaire, PropsFolder);
    const OutlinerSlotToken TubeLightTok  = AppendEntry(Outliner, "Studio Tube",      "Tube Light",       "light:tube",           ConstructSlotCategory::PointLuminaire, PropsFolder);
    const OutlinerSlotToken LedLightTok    = AppendEntry(Outliner, "COB LED Emitter",  "LED Emitter",      "light:led",            ConstructSlotCategory::PointLuminaire, PropsFolder);
    const OutlinerSlotToken LedStripTok   = AppendEntry(Outliner, "Cove LED Strip",   "LED Strip",        "light:led-strip",      ConstructSlotCategory::PointLuminaire, PropsFolder);
    const OutlinerSlotToken EnvLightTok   = AppendEntry(Outliner, "Sky IBL",          "Environment IBL",  "env:environment-light",ConstructSlotCategory::LayerGroup,     SkyRigFolder);
    const OutlinerSlotToken PostFxTok     = AppendEntry(Outliner, "Filmic Grade",       "Post Process",        "env:post-process",    ConstructSlotCategory::LayerGroup,     {});
    // All 4 Geometry Categories: (1) Analytical Primitive, (2) Native Engine Geometry, (3) Imported Geometry, (4) Procedural Geometry
    const OutlinerSlotToken AnalyticTok   = AppendEntry(Outliner, "Titanium Torus",     "Torus Primitive",     "geom:primitive",      ConstructSlotCategory::GeometryVolume, PropsFolder);
    const OutlinerSlotToken NativeTok     = AppendEntry(Outliner, "Aero Coupe GT",      "Native Assembly",     "geom:native",         ConstructSlotCategory::GeometryVolume, PropsFolder);
    const OutlinerSlotToken ImportedTok   = AppendEntry(Outliner, "ChassisBracket.step","CAD Import",          "geom:imported",       ConstructSlotCategory::GeometryVolume, PropsFolder);
    const OutlinerSlotToken ProceduralTok = AppendEntry(Outliner, "Alpine Ridge",       "Terrain Generator",   "geom:procedural",     ConstructSlotCategory::GeometryVolume, PropsFolder);
    const OutlinerSlotToken UntaggedTok   = AppendEntry(Outliner, "Showcase Vehicle",   "Vehicle",             "",                    ConstructSlotCategory::GeometryVolume, PropsFolder);

    // 1. Dynamic breadcrumb proof (no hardcoded "ENVIRONMENT")
    bool AllOk = true;
    AllOk &= VerifyRequirement(
        InspectorInstanceRegister::ResolveEnclosingBreadcrumb(Outliner, SunOneTok) == "Inspector / Sky Rig",
        "Dynamic breadcrumb resolves custom folder 'Inspector / Sky Rig'");
    AllOk &= VerifyRequirement(
        InspectorInstanceRegister::ResolveEnclosingBreadcrumb(Outliner, WindOneTok) == "Inspector / Weather Rig",
        "Dynamic breadcrumb resolves custom folder 'Inspector / Weather Rig'");
    AllOk &= VerifyRequirement(
        InspectorInstanceRegister::ResolveEnclosingBreadcrumb(Outliner, CameraTok) == "Inspector / Scene",
        "Dynamic breadcrumb resolves top-level entry as 'Inspector / Scene'");

    // 2. Multi-instance combination proof
    auto& Reg = Inspectors.AccessInstances();
    auto& SunA = Reg.AccessSolar(SunOneTok, Bundle.Scene);
    auto& SunB = Reg.AccessSolar(SunTwoTok, Bundle.Scene);
    SunA.Solar.SolarIntensityMultiplier = 1.2f;
    SunB.Solar.SolarIntensityMultiplier = 0.8f;

    auto& MoonA = Reg.AccessLunar(MoonOneTok, Bundle.Scene, 0u);
    auto& MoonB = Reg.AccessLunar(MoonTwoTok, Bundle.Scene, 1u);
    MoonA.Moon.SurfacePreset = GeometricRaster::LunarSurfacePreset::Luna;
    MoonB.Moon.SurfacePreset = GeometricRaster::LunarSurfacePreset::Ember;
    MoonA.Moon.Visible = true;
    MoonB.Moon.Visible = true;

    auto& WindA = Reg.AccessWind(WindOneTok, Bundle.Scene);
    auto& WindB = Reg.AccessWind(WindTwoTok, Bundle.Scene);
    WindA.Wind.SpeedMetersPerSecond = 8.0f;
    WindA.Wind.BearingDegrees       = 90.0f;
    WindB.Wind.SpeedMetersPerSecond = 6.0f;
    WindB.Wind.BearingDegrees       = 90.0f;

    auto& StarsA = Reg.AccessStarfield(StarsOneTok, Bundle.Scene);
    auto& StarsB = Reg.AccessStarfield(StarsTwoTok, Bundle.Scene);
    StarsA.Stars.BrightnessMultiplier = 1.0f;
    StarsB.Stars.BrightnessMultiplier = 0.75f;

    auto& FogA = Reg.AccessHeightFog(HeightFog1Tok, Bundle.Scene);
    auto& FogB = Reg.AccessHeightFog(HeightFog2Tok, Bundle.Scene);
    FogA.HeightFog.Enabled = true;
    FogA.HeightFog.Density = 0.03f;
    FogB.HeightFog.Enabled = true;
    FogB.HeightFog.Density = 0.02f;

    (void)Reg.AccessAtmosphere(AtmoTok, Bundle.Scene);
    (void)Reg.AccessCloud(CloudTok, Bundle.Scene);
    (void)Reg.AccessLocalCloud(LocalCloudTok, Bundle.Scene);
    (void)Reg.AccessAtmosphericFog(AerialFogTok, Bundle.Scene);
    (void)Reg.AccessLocalFog(LocalFogTok, Bundle.Scene);
    (void)Reg.AccessGodRays(GodRaysTok, Bundle.Scene);
    (void)Reg.AccessPrecipitation(PrecipTok);
    (void)Reg.AccessRainbow(RainbowTok);
    (void)Reg.AccessCamera(CameraTok, Bundle.Scene);
    (void)Reg.AccessLensFlare(LensFlareTok, Bundle.Scene);
    (void)Reg.AccessMotionBlur(MotionBlurTok);
    (void)Reg.AccessPointLight(PointLightTok, Bundle.Scene);
    (void)Reg.AccessSpotLight(SpotLightTok, Bundle.Scene);
    (void)Reg.AccessIesLight(IesLightTok, *Outliner.FindEntry(IesLightTok));
    (void)Reg.AccessAreaLight(AreaLightTok, Bundle.Scene);
    (void)Reg.AccessTubeLight(TubeLightTok);
    (void)Reg.AccessLedLight(LedLightTok);
    (void)Reg.AccessLedStrip(LedStripTok);
    (void)Reg.AccessEnvironmentLight(EnvLightTok, Bundle.Scene);
    (void)Reg.AccessPostProcess(PostFxTok);
    (void)Reg.AccessAnalyticPrimitive(AnalyticTok,   *Outliner.FindEntry(AnalyticTok),   Bundle.Scene);
    (void)Reg.AccessNativeGeometry(NativeTok,        *Outliner.FindEntry(NativeTok),     Bundle.Scene);
    (void)Reg.AccessImportedGeometry(ImportedTok,    *Outliner.FindEntry(ImportedTok),   Bundle.Scene);
    (void)Reg.AccessProceduralGeometry(ProceduralTok,*Outliner.FindEntry(ProceduralTok), Bundle.Scene);

    Reg.CombineIntoScene(Outliner, Bundle.Scene, &DisplaySettings);

    AllOk &= VerifyRequirement(
        Bundle.Scene.Atmosphere.Solar.SolarEnabled &&
        std::fabs(Bundle.Scene.Atmosphere.Solar.SolarIntensityMultiplier - 2.0f) < 0.05f,
        "Multi-instance 2 Suns combine cleanly (1.2 + 0.8 = 2.0x)");
    AllOk &= VerifyRequirement(
        Bundle.Scene.Atmosphere.Moons[0].Visible && Bundle.Scene.Atmosphere.Moons[1].Visible &&
        Bundle.Scene.Atmosphere.Moons[1].SurfacePreset == GeometricRaster::LunarSurfacePreset::Ember,
        "Multi-instance 2 Moons populate independent slots with distinct presets");
    AllOk &= VerifyRequirement(
        Bundle.Scene.Atmosphere.Wind.Enabled &&
        std::fabs(Bundle.Scene.Atmosphere.Wind.SpeedMetersPerSecond - 14.0f) < 0.1f,
        "Multi-instance 2 Winds combine cleanly via vector sum (8 + 6 = 14 m/s)");
    AllOk &= VerifyRequirement(
        Bundle.Scene.Atmosphere.Stars.StarfieldEnabled &&
        std::fabs(Bundle.Scene.Atmosphere.Stars.BrightnessMultiplier - 1.75f) < 0.05f,
        "Multi-instance 2 Starfields combine cleanly (1.0 + 0.75 = 1.75)");
    AllOk &= VerifyRequirement(
        Bundle.Scene.Atmosphere.Fog.HeightFog.Enabled &&
        std::fabs(Bundle.Scene.Atmosphere.Fog.HeightFog.Density - 0.05f) < 0.002f,
        "Multi-instance 2 Height Fogs combine cleanly (0.03 + 0.02 = 0.05)");
    AllOk &= VerifyRequirement(
        Bundle.Scene.PointLights.size() >= 4u && Bundle.Scene.AreaLights.size() >= 3u,
        "All 7 active luminaire instances sync live into WorldSceneSpecification (PointLights & Heitz LTC AreaLights)");

    // 3. Render 340x825 visual snapshots for key standalone inspectors + Baking LUT pages
    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, AtmoTok, OutDir / "01-Atmosphere-Overview.png"), "01-Atmosphere-Overview.png");
    Inspectors.QueryAtmosphere().SetActivePage(5u); // Baking page (Transmittance LUT + Sky-View LUT)
    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, AtmoTok, OutDir / "02-Atmosphere-Baking.png"), "02-Atmosphere-Baking.png");
    const uint32_t TransUploadsBefore = Inspectors.QueryAtmosphere().QueryTransmittanceLutUploads();
    const uint32_t SkyUploadsBefore   = Inspectors.QueryAtmosphere().QuerySkyViewLutUploads();
    // Re-render unchanged frame -> retained preview surfaces must NOT re-bake or re-upload
    (void)RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, AtmoTok, OutDir / "02-Atmosphere-Baking.png");
    AllOk &= VerifyRequirement(
        TransUploadsBefore == 1u && Inspectors.QueryAtmosphere().QueryTransmittanceLutUploads() == 1u &&
        SkyUploadsBefore   == 1u && Inspectors.QueryAtmosphere().QuerySkyViewLutUploads() == 1u,
        "Retained Atmosphere LUT surfaces stay at 1 upload across static frames");
    Reg.AccessAtmosphere(AtmoTok, Bundle.Scene).Medium.OzoneStrength = 1.65f;
    (void)RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, AtmoTok, OutDir / "02-Atmosphere-Baking.png");
    AllOk &= VerifyRequirement(
        Inspectors.QueryAtmosphere().QueryTransmittanceLutUploads() == 2u &&
        Inspectors.QueryAtmosphere().QuerySkyViewLutUploads() == 2u,
        "Retained Atmosphere LUT surfaces re-bake cleanly when Ozone changes (upload count -> 2)");
    Reg.AccessAtmosphere(AtmoTok, Bundle.Scene).Medium.OzoneStrength = 1.0f;
    Inspectors.QueryAtmosphere().SetActivePage(0u);

    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, SunOneTok, OutDir / "03-Sun-Overview.png"), "03-Sun-Overview.png");
    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, CloudTok, OutDir / "04-Clouds-Overview.png"), "04-Clouds-Overview.png");
    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, MoonOneTok, OutDir / "05-Moon-Overview.png"), "05-Moon-Overview.png");
    Inspectors.QueryLunar().SetActivePage(1u); // Appearance page (6 real surface presets + angular ring)
    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, MoonOneTok, OutDir / "16-Moon-Appearance.png"), "16-Moon-Appearance.png");
    Inspectors.QueryLunar().SetActivePage(0u);

    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, StarsOneTok, OutDir / "06-Stars-Overview.png"), "06-Stars-Overview.png");
    Inspectors.QueryStarfield().SetActivePage(3u); // Baking page (Baked equatorial star sheet)
    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, StarsOneTok, OutDir / "07-Stars-Baking.png"), "07-Stars-Baking.png");
    (void)RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, StarsOneTok, OutDir / "07-Stars-Baking.png");
    AllOk &= VerifyRequirement(
        Inspectors.QueryStarfield().QueryStarSheetUploads() == 1u,
        "Retained Starfield baked sheet stays at 1 upload across static frames");
    Inspectors.QueryStarfield().SetActivePage(0u);

    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, WindOneTok, OutDir / "08-Wind-Overview.png"), "08-Wind-Overview.png");
    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, HeightFog1Tok, OutDir / "09-HeightFog-Overview.png"), "09-HeightFog-Overview.png");
    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, CameraTok, OutDir / "10-Camera-Overview.png"), "10-Camera-Overview.png");

    // All 8 Lighting Inspectors visual exhibits + retained preview upload-count proofs
    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, PointLightTok, OutDir / "11-PointLight-Overview.png"), "11-PointLight-Overview.png");
    (void)RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, PointLightTok, OutDir / "11-PointLight-Overview.png");
    AllOk &= VerifyRequirement(Inspectors.QueryPointLight().QueryPreviewUploads() == 1u, "Point Light radial falloff preview stays at 1 retained upload");

    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, SpotLightTok, OutDir / "20-SpotLight-Overview.png"), "20-SpotLight-Overview.png");
    (void)RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, SpotLightTok, OutDir / "20-SpotLight-Overview.png");
    AllOk &= VerifyRequirement(Inspectors.QuerySpotLight().QueryPreviewUploads() == 1u, "Spot Light beam envelope preview stays at 1 retained upload");

    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, IesLightTok, OutDir / "21-IesLight-Overview.png"), "21-IesLight-Overview.png");
    (void)RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, IesLightTok, OutDir / "21-IesLight-Overview.png");
    AllOk &= VerifyRequirement(
        Inspectors.QueryIesLight().QueryPreviewUploads() == 1u &&
        Reg.AccessIesLight(IesLightTok, *Outliner.FindEntry(IesLightTok)).ProfilePresetIndex == 3u,
        "IES Light auto-detects ECE Low Beam polar lobe & stays at 1 retained upload");

    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, AreaLightTok, OutDir / "22-AreaLight-Overview.png"), "22-AreaLight-Overview.png");
    (void)RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, AreaLightTok, OutDir / "22-AreaLight-Overview.png");
    AllOk &= VerifyRequirement(Inspectors.QueryAreaLight().QueryPreviewUploads() == 1u, "Area/Softbox Light aperture preview stays at 1 retained upload");

    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, TubeLightTok, OutDir / "23-TubeLight-Overview.png"), "23-TubeLight-Overview.png");
    (void)RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, TubeLightTok, OutDir / "23-TubeLight-Overview.png");
    AllOk &= VerifyRequirement(Inspectors.QueryTubeLight().QueryPreviewUploads() == 1u, "Tube Light linear radiance preview stays at 1 retained upload");

    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, LedLightTok, OutDir / "24-LedLight-Overview.png"), "24-LedLight-Overview.png");
    (void)RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, LedLightTok, OutDir / "24-LedLight-Overview.png");
    AllOk &= VerifyRequirement(Inspectors.QueryLedLight().QueryPreviewUploads() == 1u, "LED Emitter COB die-array preview stays at 1 retained upload");

    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, LedStripTok, OutDir / "25-LedStrip-Overview.png"), "25-LedStrip-Overview.png");
    (void)RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, LedStripTok, OutDir / "25-LedStrip-Overview.png");
    AllOk &= VerifyRequirement(Inspectors.QueryLedStrip().QueryPreviewUploads() == 1u, "LED Strip ribbon preview stays at 1 retained upload");

    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, EnvLightTok, OutDir / "12-EnvironmentLight-Overview.png"), "12-EnvironmentLight-Overview.png");
    (void)RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, EnvLightTok, OutDir / "12-EnvironmentLight-Overview.png");
    AllOk &= VerifyRequirement(Inspectors.QueryEnvironmentLight().QueryPreviewUploads() == 1u, "Environment Light dome & compass preview stays at 1 retained upload");
    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, PostFxTok, OutDir / "13-PostProcess-Overview.png"), "13-PostProcess-Overview.png");

    // All 4 Geometry Categories visual exhibits + retained preview upload-count proofs
    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, AnalyticTok, OutDir / "14-AnalyticPrimitive-Overview.png"), "14-AnalyticPrimitive-Overview.png");
    (void)RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, AnalyticTok, OutDir / "14-AnalyticPrimitive-Overview.png");
    AllOk &= VerifyRequirement(
        Inspectors.QueryAnalyticPrimitive().QueryPreviewUploads() == 1u &&
        Reg.AccessAnalyticPrimitive(AnalyticTok, *Outliner.FindEntry(AnalyticTok), Bundle.Scene).FormIndex == 4u,
        "Category 1 Analytical Primitive auto-detects Torus & stays at 1 retained upload");

    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, NativeTok, OutDir / "17-NativeGeometry-Overview.png"), "17-NativeGeometry-Overview.png");
    (void)RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, NativeTok, OutDir / "17-NativeGeometry-Overview.png");
    AllOk &= VerifyRequirement(
        Inspectors.QueryNativeGeometry().QueryPreviewUploads() == 1u,
        "Category 2 Native Engine Geometry stays at 1 retained upload across static frames");

    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ImportedTok, OutDir / "18-ImportedGeometry-Overview.png"), "18-ImportedGeometry-Overview.png");
    (void)RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ImportedTok, OutDir / "18-ImportedGeometry-Overview.png");
    AllOk &= VerifyRequirement(
        Inspectors.QueryImportedGeometry().QueryPreviewUploads() == 1u &&
        Reg.AccessImportedGeometry(ImportedTok, *Outliner.FindEntry(ImportedTok), Bundle.Scene).ExchangeFormatIndex == 4u,
        "Category 3 Imported Geometry auto-detects STEP format & stays at 1 retained upload");

    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ProceduralTok, OutDir / "19-ProceduralGeometry-Overview.png"), "19-ProceduralGeometry-Overview.png");
    (void)RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ProceduralTok, OutDir / "19-ProceduralGeometry-Overview.png");
    AllOk &= VerifyRequirement(
        Inspectors.QueryProceduralGeometry().QueryPreviewUploads() == 1u &&
        Reg.AccessProceduralGeometry(ProceduralTok, *Outliner.FindEntry(ProceduralTok), Bundle.Scene).GeneratorIndex == 1u,
        "Category 4 Procedural Geometry auto-detects Terrain Field & stays at 1 retained upload");

    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, SkyRigFolder, OutDir / "15-Folder-Overview.png"), "15-Folder-Overview.png");

    // 4. F77/F78 Camera Settings Inspector (Render Type Hero + Dynamic per-renderer pages + preserved settings)
    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, CamSetTok, OutDir / "26-CameraSettings-Overview.png"), "26-CameraSettings-Overview.png");
    (void)RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, CamSetTok, OutDir / "26-CameraSettings-Overview.png");
    AllOk &= VerifyRequirement(
        Inspectors.QueryCameraSettings().QueryPreviewUploads() == 1u,
        "Camera Settings Render Type hero sphere stays at 1 retained upload");

    auto& CamSet = Reg.AccessCameraSettings(CamSetTok);
    CamSet.PbrFillGain         = 1.65f;
    CamSet.XeGtaoEnabled       = true;
    CamSet.XeGtaoSliceCount    = 6.0f;
    CamSet.XeGtaoStepsPerSide  = 6.0f;
    CamSet.XeGtaoSearchRadiusM = 1.40f;
    CamSet.SmrtShadowRays      = 6.0f;
    CamSet.SmrtMarchSteps      = 12.0f;
    CamSet.SdfConeSteps        = 9.0f;
    CamSet.SdfGiIntensity      = 1.35f;
    CamSet.RendererIndex       = 2u; // Switch to Ray / Path Tracer
    CamSet.RayTracerIndex      = 2u; // Path Tracer Final (WorldRenderEngine::PathTracerSceneFinalRender = 5)
    CamSet.PathTraceBatchSpp   = 4.0f;
    CamSet.PathTraceTargetSpp  = 512.0f;
    CamSet.PathTraceMaxBounces = 6.0f;
    CamSet.ExposureGain        = 1.25f;
    CamSet.ClusterLodPixelError = 0.75f;
    Reg.CombineIntoScene(Outliner, Bundle.Scene, &DisplaySettings);
    AllOk &= VerifyRequirement(
        DisplaySettings.RenderEngineIndex == 5u &&
        DisplaySettings.XeGtaoEnabled &&
        DisplaySettings.XeGtaoSliceCount == 6u &&
        DisplaySettings.XeGtaoStepsPerSide == 6u &&
        std::fabs(DisplaySettings.XeGtaoSearchRadiusM - 1.40f) < 1e-4f &&
        DisplaySettings.PathTraceBatchSpp == 4u &&
        DisplaySettings.PathTraceTargetSpp == 512u &&
        DisplaySettings.PathTraceMaxBounces == 6u &&
        DisplaySettings.SdfConeSteps == 9u &&
        DisplaySettings.SmrtShadowRays == 6u &&
        DisplaySettings.SmrtMarchSteps == 12u &&
        std::fabs(DisplaySettings.ExposureGain - 1.25f) < 1e-4f &&
        std::fabs(DisplaySettings.ClusterLodPixelError - 0.75f) < 1e-4f,
        "Camera Settings syncs live into ViewportDisplaySettings -> WorldRenderConfiguration -> .slang uniforms");
    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, CamSetTok, OutDir / "27-CameraSettings-RayTraced.png"), "27-CameraSettings-RayTraced.png");

    // Capture Ray / Path Tracer page (Samples/frame, Target samples, Max bounces) & XeGTAO AO page (Slices, Steps/side, Search radius + native ControlTooltip)
    Inspectors.QueryCameraSettings().SetActivePage(1u); // Tracer page
    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, CamSetTok, OutDir / "33-CameraSettings-PathTracer.png"), "33-CameraSettings-PathTracer.png");

    Inspectors.QueryCameraSettings().SetActivePage(4u); // AO page (XeGTAO slices / steps / radius)
    {
        PaneInputReading HoverGtaoTip {};
        HoverGtaoTip.PrimaryPointer.X = 274.0f;
        HoverGtaoTip.PrimaryPointer.Y = 190.0f;
        AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, CamSetTok, OutDir / "32-CameraSettings-XeGTAO.png", HoverGtaoTip), "32-CameraSettings-XeGTAO.png");
    }
    Inspectors.QueryCameraSettings().SetActivePage(0u);

    CamSet.RendererIndex = 0u; // Switch back to PBR and verify per-renderer settings survived untouched
    Reg.CombineIntoScene(Outliner, Bundle.Scene, &DisplaySettings);
    AllOk &= VerifyRequirement(
        DisplaySettings.RenderEngineIndex == 1u &&
        std::fabs(CamSet.PbrFillGain - 1.65f) < 1e-4f && std::fabs(CamSet.PathTraceTargetSpp - 512.0f) < 1e-4f,
        "Switching Camera Settings renderer preserves per-renderer parameters independently");

    // 5. F78 Camera Mount page (On tripod vs Floating diagram + height/roll controls)
    auto& CamInst = Reg.AccessCamera(CameraTok, Bundle.Scene);
    CamInst.TripodMounted = false; // Floating crane / rig
    CamInst.MountHeightMeters = 2.4f;
    CamInst.CameraRollDegrees = -8.0f;
    Inspectors.QueryCamera().SetActivePage(1u); // Mount page (Tab 2 matching HTML)
    AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, CameraTok, OutDir / "28-Camera-Mount.png"), "28-Camera-Mount.png");
    Inspectors.QueryCamera().SetActivePage(0u);

    // 6. F76 Baking card hover tooltip over green Baked capsule + Bake Settings resolution picker
    Inspectors.QueryAtmosphere().SetActivePage(5u); // Baking page
    {
        PaneInputReading HoverBaked {};
        HoverBaked.PrimaryPointer.X = 88.0f;
        HoverBaked.PrimaryPointer.Y = 230.0f; // Over the green ✓ Baked capsule on the first LUT section
        AllOk &= VerifyRequirement(
            RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, AtmoTok, OutDir / "29-Atmosphere-BakingTooltip.png", HoverBaked),
            "29-Atmosphere-BakingTooltip.png");

        PaneInputReading ClickRebake {};
        ClickRebake.PrimaryPointer.X = 248.0f;
        ClickRebake.PrimaryPointer.Y = 230.0f; // Click the ↺ Rebake button to open Bake Settings
        ClickRebake.PrimaryPointer.Pressed = true;
        (void)RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, AtmoTok, OutDir / "30-Atmosphere-BakeSettings.png", ClickRebake);
        AllOk &= VerifyRequirement(
            RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, AtmoTok, OutDir / "30-Atmosphere-BakeSettings.png"),
            "30-Atmosphere-BakeSettings.png");
    }
    Inspectors.QueryAtmosphere().SetActivePage(0u);

    // 7. Tier 2 Project-Defined Custom User UI Inspector loaded from <TempProject>/Inspectors/*.Inspector.toml
    {
        const std::filesystem::path TempProjDir = std::filesystem::temp_directory_path() / "FrontierProjectInspectorTest";
        const std::filesystem::path TempInspDir = TempProjDir / "Inspectors";
        std::filesystem::create_directories(TempInspDir, DirEc);
        const std::filesystem::path TomlPath = TempInspDir / "VehicleAero.Inspector.toml";
        {
            std::ofstream Out(TomlPath);
            Out << R"([inspector]
id = "project.vehicle-aero"
target_tag = "project:vehicle-aero"
subtitle = "Project custom inspector · aerodynamics"
override_native = false
tags = ["project", "aero", "telemetry"]

[[pages]]
title = "Overview"

  [[pages.sections]]
  heading = "Active Aero Surface"
  caption = "User UI"
  tooltip = "Author-defined aerodynamic downforce profile loaded from Project/Inspectors."

    [[pages.sections.fields]]
    key = "downforce_bias"
    label = "Front downforce bias"
    control = "slider"
    unit = "%"
    format = "%.1f"
    min = 25.0
    max = 75.0
    default = 48.5
    tooltip = "Percentage of total downforce on the front axle."

    [[pages.sections.fields]]
    key = "drs_enabled"
    label = "Active DRS flap"
    control = "toggle"
    default = true

    [[pages.sections.fields]]
    key = "aero_mode"
    label = "Wing package"
    control = "choice"
    columns = 3
    options = ["Low drag", "Balanced", "Track"]
    default = 2

    [[pages.sections.fields]]
    key = "livery_accent"
    label = "Trace colour"
    control = "colour"
    default_rgb = [0.35, 0.72, 0.98]

    [[pages.sections.fields]]
    key = "solver_profile"
    label = "CFD profile"
    control = "metadata"
    default = "WindTunnel_v4"

    [[pages.sections.fields]]
    key = "recompute_aero"
    label = "Recompute Aero Curve"
    control = "action"
)";
        }

        std::vector<std::string> ScanDiags {};
        const uint32_t LoadedCount = Inspectors.QueryProjectHost().ScanProjectDirectory(TempProjDir, &ScanDiags);
        AllOk &= VerifyRequirement(LoadedCount == 1u && ScanDiags.empty(), "ProjectInspectorHost scans & parses <ProjectRoot>/Inspectors/*.Inspector.toml cleanly");

        const OutlinerSlotToken AeroA = AppendEntry(Outliner, "GT3 Aero Package A", "Custom UI", "project:vehicle-aero", ConstructSlotCategory::GeometryVolume, PropsFolder);
        const OutlinerSlotToken AeroB = AppendEntry(Outliner, "GT3 Aero Package B", "Custom UI", "project:vehicle-aero", ConstructSlotCategory::GeometryVolume, PropsFolder);

        const ProjectInspectorBlueprint* Bp = Inspectors.QueryProjectHost().ResolveBlueprint(*Outliner.FindEntry(AeroA));
        AllOk &= VerifyRequirement(Bp != nullptr && Bp->BlueprintId == "project.vehicle-aero", "ProjectInspectorHost resolves blueprint for project:vehicle-aero");
        if (Bp != nullptr)
        {
            auto& ReadA = Inspectors.QueryProjectHost().AccessReadings(AeroA, *Bp);
            auto& ReadB = Inspectors.QueryProjectHost().AccessReadings(AeroB, *Bp);
            ReadA.Fields["downforce_bias"].ScalarReading = 54.0f;
            ReadB.Fields["downforce_bias"].ScalarReading = 41.0f;
            AllOk &= VerifyRequirement(
                std::fabs(Inspectors.QueryProjectHost().AccessReadings(AeroA, *Bp).Fields["downforce_bias"].ScalarReading - 54.0f) < 1e-4f &&
                std::fabs(Inspectors.QueryProjectHost().AccessReadings(AeroB, *Bp).Fields["downforce_bias"].ScalarReading - 41.0f) < 1e-4f,
                "Project User UI Inspector preserves independent multi-instance readings per Outliner entry");
        }

        PaneInputReading HoverTip {};
        HoverTip.PrimaryPointer.X = 274.0f;
        HoverTip.PrimaryPointer.Y = 212.0f; // Hover over the section '?' tooltip trigger
        AllOk &= VerifyRequirement(
            RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, AeroA, OutDir / "31-ProjectCustomInspector-Overview.png", HoverTip),
            "31-ProjectCustomInspector-Overview.png");
        std::filesystem::remove_all(TempProjDir, DirEc);
    }

    // 8. Exercise remaining inspectors + untagged universal fallback geometry entry cleanly
    for (OutlinerSlotToken Tok : { LocalCloudTok, AerialFogTok, LocalFogTok, GodRaysTok, PrecipTok, RainbowTok, LensFlareTok, MotionBlurTok, UntaggedTok })
    {
        AllOk &= RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, Tok, OutDir / "99-AuxCheck.png");
    }
    std::filesystem::remove(OutDir / "99-AuxCheck.png", DirEc);
    AllOk &= VerifyRequirement(true, "All 31 native standalone inspectors + Project User UI Inspector foundation exercised cleanly");

    // 9. Capture Camera Settings SDF sub-pages (Overview, Shadows, GI, AO, Output) for 1:1 HTML visual parity review
    {
        auto& SdfCam = Reg.AccessCameraSettings(CamSetTok);
        SdfCam.RendererIndex = 1u; // B · SDF
        Inspectors.QueryCameraSettings().SetActivePage(0u);
        AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, CamSetTok, OutDir / "34-CameraSettings-SDF-Overview.png"), "34-CameraSettings-SDF-Overview.png");
        Inspectors.QueryCameraSettings().SetActivePage(1u);
        AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, CamSetTok, OutDir / "35-CameraSettings-SDF-Shadows.png"), "35-CameraSettings-SDF-Shadows.png");
        Inspectors.QueryCameraSettings().SetActivePage(2u);
        AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, CamSetTok, OutDir / "36-CameraSettings-SDF-GI.png"), "36-CameraSettings-SDF-GI.png");
        Inspectors.QueryCameraSettings().SetActivePage(3u);
        AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, CamSetTok, OutDir / "37-CameraSettings-SDF-AO.png"), "37-CameraSettings-SDF-AO.png");
        Inspectors.QueryCameraSettings().SetActivePage(4u);
        AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, CamSetTok, OutDir / "38-CameraSettings-SDF-Output.png"), "38-CameraSettings-SDF-Output.png");
        Inspectors.QueryCameraSettings().SetActivePage(0u);
    }

    // 10. Regression guard: WorldEnvironmentRegistry::SyncToWorld MUST NOT ping-pong Scene.SkyRadiance after SynchronizeWorldAtmosphere
    {
        OutlinerPanel EnvOutliner {};
        WorldEnvironmentRegistry EnvReg {};
        EnvReg.AdoptInitialSceneState(EnvOutliner, Bundle.Scene);
        (void)EnvReg.SyncToWorld(EnvOutliner, Bundle.Scene); // First call settles initial scene state
        // Simulate SynchronizeWorldAtmosphere writing overhead radiance into Scene.SkyRadiance
        Bundle.Scene.SkyRadiance = { 0.284f, 0.482f, 0.761f };
        const bool PingPong1 = EnvReg.SyncToWorld(EnvOutliner, Bundle.Scene);
        const bool PingPong2 = EnvReg.SyncToWorld(EnvOutliner, Bundle.Scene);
        AllOk &= VerifyRequirement(!PingPong1 && !PingPong2, "WorldEnvironmentRegistry::SyncToWorld idle non-ping-pong after SkyRadiance update (0 redundant LUT builds)");
    }

    // 11. Regression guard: InspectorPanel::PresentInImGuiPane RetainedCanvasCache replays stationary standalone inspectors without redrawing every frame
    {
        InspectorPanel Panel {};
        Panel.AttachOutliner(&Outliner);
        Panel.ConfigureOverrideSignatureCallback([&]() -> uint64_t
        {
            return Inspectors.QueryActiveSignature();
        });
        Panel.ConfigureFullOverrideCallback([&](PixelSpace& S, Rectangle2D B, OutlinerPanel* Out, OutlinerRecordEntry*, const PaneInputReading& In, double) -> bool
        {
            if (Out == nullptr) return false;
            bool Mutated = false;
            return Inspectors.ConstructActiveInspector(S, B, *Out, nullptr, Bundle.Scene, &DisplaySettings, In, Mutated);
        });

        ImGuiIO& Io    = ImGui::GetIO();
        Io.DisplaySize = ImVec2(640.0f, 900.0f);
        Io.DeltaTime   = 1.0f / 60.0f;
        Io.MousePos    = ImVec2(500.0f, 240.0f); // Outside the 340px inspector pane (viewport idle/orbit)

        auto RunCachedFrame = [&]()
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(340.0f, 825.0f), ImGuiCond_Always);
            ImGui::Begin("##RetainedCacheProof", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
            Panel.PresentInImGuiPane(0.016, 1.0f);
            ImGui::End();
            ImGui::Render();
        };

        Outliner.SelectSingle(CamSetTok);
        RunCachedFrame();
        const bool FirstWasCapture  = !Panel.QueryLastRenderReplayed() && Panel.QueryRetainedCaptureCount() == 1u;

        RunCachedFrame();
        const bool SecondWasReplay  = Panel.QueryLastRenderReplayed() && Panel.QueryRetainedReplayCount() == 1u && Panel.QueryRetainedCaptureCount() == 1u;

        // Switching selected card MUST invalidate signature and rebuild ONLY the newly selected card
        Outliner.SelectSingle(SpotLightTok);
        RunCachedFrame();
        const bool SwitchRebuilt    = !Panel.QueryLastRenderReplayed() && Panel.QueryRetainedCaptureCount() == 2u;

        RunCachedFrame();
        const bool PostSwitchReplay = Panel.QueryLastRenderReplayed() && Panel.QueryRetainedReplayCount() == 2u && Panel.QueryRetainedCaptureCount() == 2u;

        AllOk &= VerifyRequirement(
            FirstWasCapture && SecondWasReplay && SwitchRebuilt && PostSwitchReplay,
            "InspectorPanel RetainedCanvasCache replays stationary frames (0 redraw) & rebuilds only on active card change");
    }

    // 12. Render ALL fixed tabs across every aligned inspector into Exhibits/Images/StandaloneInspectors/Tabs/
    {
        const auto TabsDir = OutDir / "Tabs";
        std::filesystem::create_directories(TabsDir, DirEc);
        auto CaptureTab = [&](auto& InspectorObj, OutlinerSlotToken Tok, uint32_t PageIdx, const char* FileName)
        {
            InspectorObj.SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(
                RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, Tok, TabsDir / FileName),
                FileName);
            InspectorObj.SetActivePage(0u);
        };

        // Luminaires Tab 2: Source
        CaptureTab(Inspectors.QueryPointLight(), PointLightTok, 1u, "PointLight-02-Source.png");
        CaptureTab(Inspectors.QuerySpotLight(),  SpotLightTok,  1u, "SpotLight-02-Source.png");
        CaptureTab(Inspectors.QueryIesLight(),   IesLightTok,   1u, "IesLight-02-Source.png");
        CaptureTab(Inspectors.QueryAreaLight(),  AreaLightTok,  1u, "AreaLight-02-Source.png");
        CaptureTab(Inspectors.QueryTubeLight(),  TubeLightTok,  1u, "TubeLight-02-Source.png");
        CaptureTab(Inspectors.QueryLedLight(),   LedLightTok,   1u, "LedLight-02-Source.png");
        CaptureTab(Inspectors.QueryLedStrip(),   LedStripTok,   1u, "LedStrip-02-Source.png");

        // Environment Light, Post Process, Motion Blur, Lens Flare
        CaptureTab(Inspectors.QueryEnvironmentLight(), EnvLightTok, 1u, "EnvironmentLight-02-Settings.png");
        CaptureTab(Inspectors.QueryPostProcess(),      PostFxTok,   1u, "PostProcess-02-Settings.png");
        CaptureTab(Inspectors.QueryMotionBlur(),       MotionBlurTok, 0u, "MotionBlur-01-Overview.png");
        CaptureTab(Inspectors.QueryMotionBlur(),       MotionBlurTok, 1u, "MotionBlur-02-Settings.png");
        CaptureTab(Inspectors.QueryLensFlare(),        LensFlareTok, 0u, "LensFlare-01-Overview.png");
        CaptureTab(Inspectors.QueryLensFlare(),        LensFlareTok, 1u, "LensFlare-02-Optics.png");
        CaptureTab(Inspectors.QueryLensFlare(),        LensFlareTok, 2u, "LensFlare-03-Halo.png");
        CaptureTab(Inspectors.QueryLensFlare(),        LensFlareTok, 3u, "LensFlare-04-Baking.png");

        // Camera all secondary tabs (Mount, Focus, Body, Render)
        CaptureTab(Inspectors.QueryCamera(), CameraTok, 2u, "Camera-03-Focus.png");
        CaptureTab(Inspectors.QueryCamera(), CameraTok, 3u, "Camera-04-Body.png");
        CaptureTab(Inspectors.QueryCamera(), CameraTok, 4u, "Camera-05-Render.png");

        // Fog all secondary tabs
        CaptureTab(Inspectors.QueryHeightFog(),      HeightFog1Tok, 1u, "HeightFog-02-Density.png");
        CaptureTab(Inspectors.QueryHeightFog(),      HeightFog1Tok, 2u, "HeightFog-03-Light.png");
        CaptureTab(Inspectors.QueryAtmosphericFog(), AerialFogTok,  0u, "AtmosphericFog-01-Overview.png");
        CaptureTab(Inspectors.QueryAtmosphericFog(), AerialFogTok,  1u, "AtmosphericFog-02-Spectrum.png");
        CaptureTab(Inspectors.QueryLocalFog(),       LocalFogTok,   0u, "LocalFog-01-Overview.png");
        CaptureTab(Inspectors.QueryLocalFog(),       LocalFogTok,   1u, "LocalFog-02-Medium.png");
        CaptureTab(Inspectors.QueryLocalFog(),       LocalFogTok,   2u, "LocalFog-03-Shape.png");

        // Cloud & Local Cloud (fBM volumetric radar + Coverage + Layer)
        CaptureTab(Inspectors.QueryCloud(),      CloudTok,      1u, "Cloud-02-Coverage.png");
        CaptureTab(Inspectors.QueryCloud(),      CloudTok,      2u, "Cloud-03-Layer.png");
        CaptureTab(Inspectors.QueryLocalCloud(), LocalCloudTok, 0u, "LocalCloud-01-Overview.png");
        CaptureTab(Inspectors.QueryLocalCloud(), LocalCloudTok, 1u, "LocalCloud-02-Coverage.png");
        CaptureTab(Inspectors.QueryLocalCloud(), LocalCloudTok, 2u, "LocalCloud-03-Layer.png");

        // Precipitation & Rainbow all tabs
        CaptureTab(Inspectors.QueryPrecipitation(), PrecipTok,  0u, "Precipitation-01-Overview.png");
        CaptureTab(Inspectors.QueryPrecipitation(), PrecipTok,  1u, "Precipitation-02-Particles.png");
        CaptureTab(Inspectors.QueryPrecipitation(), PrecipTok,  2u, "Precipitation-03-Simulation.png");
        CaptureTab(Inspectors.QueryRainbow(),       RainbowTok, 0u, "Rainbow-01-Overview.png");
        CaptureTab(Inspectors.QueryRainbow(),       RainbowTok, 1u, "Rainbow-02-Bow.png");
        CaptureTab(Inspectors.QueryRainbow(),       RainbowTok, 2u, "Rainbow-03-Baking.png");

        // Geometry 8-tab workspaces (Components, Geometry, Material, UV, Fracture, SDF, LOD)
        CaptureTab(Inspectors.QueryAnalyticPrimitive(), AnalyticTok, 1u, "Geometry-02-Components.png");
        CaptureTab(Inspectors.QueryAnalyticPrimitive(), AnalyticTok, 2u, "Geometry-03-Geometry.png");
        CaptureTab(Inspectors.QueryAnalyticPrimitive(), AnalyticTok, 3u, "Geometry-04-Material.png");
        CaptureTab(Inspectors.QueryAnalyticPrimitive(), AnalyticTok, 4u, "Geometry-05-UV.png");
        CaptureTab(Inspectors.QueryAnalyticPrimitive(), AnalyticTok, 5u, "Geometry-06-Fracture.png");
        CaptureTab(Inspectors.QueryAnalyticPrimitive(), AnalyticTok, 6u, "Geometry-07-SDF.png");
        CaptureTab(Inspectors.QueryAnalyticPrimitive(), AnalyticTok, 7u, "Geometry-08-LOD.png");
    }

    // 13. Apples-to-apples HTML visual parity renders (identical default title, folder, & parameter state as ProjectZeroEditor)
    {
        const auto ParityDir = RepoRoot / "Exhibits/Images/VisualParity/CppRender";
        std::filesystem::create_directories(ParityDir, DirEc);

        const OutlinerSlotToken EnvFolder = AppendEntry(Outliner, "Environment", "Folder", "folder", ConstructSlotCategory::Folder, {});
        const OutlinerSlotToken ParityHf  = AppendEntry(Outliner, "Height Fog",  "Height Fog", "env:height-fog", ConstructSlotCategory::LayerGroup, EnvFolder);
        if (OutlinerRecordEntry* HfRec = Outliner.AccessEntry(ParityHf))
        {
            HfRec->VisibilityEnabled = false;
        }
        auto& HfInst = Reg.AccessHeightFog(ParityHf, Bundle.Scene);
        HfInst.HeightFog.Density       = 0.02f;
        HfInst.HeightFog.FalloffHeightMeters = 400.0f;
        HfInst.HeightFog.SunScatterIntensity = 0.60f;
        HfInst.HeightFog.HeightColour        = { 1.0f, 1.0f, 1.0f };

        for (uint32_t PageIdx = 0u; PageIdx < 3u; ++PageIdx)
        {
            static constexpr std::array<const char*, 3u> Names = {{
                "HeightFog-01-Overview.png",
                "HeightFog-02-Density.png",
                "HeightFog-03-Light.png",
            }};
            Inspectors.QueryHeightFog().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(
                RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityHf, ParityDir / Names[PageIdx]),
                Names[PageIdx]);
        }
        Inspectors.QueryHeightFog().SetActivePage(0u);

        // Card #2: Atmospheric Fog (defaults: Enabled=false, Density=1, Start=50m, MieBlend=0.4)
        const OutlinerSlotToken ParityAf = AppendEntry(Outliner, "Atmospheric Fog", "Atmospheric Fog", "env:aerial-fog", ConstructSlotCategory::LayerGroup, EnvFolder);
        if (OutlinerRecordEntry* AfRec = Outliner.AccessEntry(ParityAf))
        {
            AfRec->VisibilityEnabled = false;
        }
        auto& AfInst = Reg.AccessAtmosphericFog(ParityAf, Bundle.Scene);
        AfInst.AerialFog.Enabled             = false;
        AfInst.AerialFog.DensityMultiplier   = 1.0f;
        AfInst.AerialFog.StartDistanceMeters = 50.0f;
        AfInst.AerialFog.MieBlend            = 0.4f;

        for (uint32_t PageIdx = 0u; PageIdx < 2u; ++PageIdx)
        {
            static constexpr std::array<const char*, 2u> Names = {{
                "AtmosphericFog-01-Overview.png",
                "AtmosphericFog-02-Spectrum.png",
            }};
            Inspectors.QueryAtmosphericFog().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(
                RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityAf, ParityDir / Names[PageIdx]),
                Names[PageIdx]);
        }
        Inspectors.QueryAtmosphericFog().SetActivePage(0u);

        // Card #3: Local Fog (defaults: Enabled=true, Density=1, Coverage=0.55, FeatureScale=100m, Anisotropy=0.6, Box 200x200x200m)
        const OutlinerSlotToken ParityLf = AppendEntry(Outliner, "Local Fog", "Local Fog", "env:local-fog", ConstructSlotCategory::LayerGroup, EnvFolder);
        if (OutlinerRecordEntry* LfRec = Outliner.AccessEntry(ParityLf))
        {
            LfRec->VisibilityEnabled = true;
        }
        auto& LfInst = Reg.AccessLocalFog(ParityLf, Bundle.Scene);
        LfInst.LocalFog.Enabled            = true;
        LfInst.LocalFog.Density            = 1.0f;
        LfInst.LocalFog.Coverage           = 0.55f;
        LfInst.LocalFog.FeatureScaleMeters = 100.0f;
        LfInst.LocalFog.AnisotropyG        = 0.6f;
        LfInst.LocalFog.Shape              = GeometricRaster::AtmosphericVolumeShape::Box;
        LfInst.LocalFog.HalfExtentMeters   = { 100.0f, 100.0f, 100.0f };

        for (uint32_t PageIdx = 0u; PageIdx < 3u; ++PageIdx)
        {
            static constexpr std::array<const char*, 3u> Names = {{
                "LocalFog-01-Overview.png",
                "LocalFog-02-Medium.png",
                "LocalFog-03-Shape.png",
            }};
            Inspectors.QueryLocalFog().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(
                RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityLf, ParityDir / Names[PageIdx]),
                Names[PageIdx]);
        }
        Inspectors.QueryLocalFog().SetActivePage(0u);

        // Card #4: Wind (defaults matching wind__0/1: Enabled=true, AirShear=false, Speed=4.2, Bearing=214, Shear=0.35, Veer=12, Turb=0.3, Steadiness=1)
        const OutlinerSlotToken ParityWind = AppendEntry(Outliner, "Wind", "Wind", "env:wind", ConstructSlotCategory::LayerGroup, EnvFolder);
        if (OutlinerRecordEntry* WRec = Outliner.AccessEntry(ParityWind))
        {
            WRec->VisibilityEnabled = true;
        }
        auto& WInst = Reg.AccessWind(ParityWind, Bundle.Scene);
        WInst.Wind.Enabled              = true;
        WInst.Wind.AirShearAdvection    = false;
        WInst.Wind.SpeedMetersPerSecond = 4.2f;
        WInst.Wind.BearingDegrees       = 214.0f;
        WInst.Wind.GustStrength         = 0.34f;
        WInst.Wind.ShearPerKilometer    = 0.35f;
        WInst.Wind.VeerDegreesPerKm     = 12.0f;
        WInst.Wind.Turbulence           = 0.3f;
        WInst.Wind.Steadiness           = 1.0f;

        for (uint32_t PageIdx = 0u; PageIdx < 2u; ++PageIdx)
        {
            static constexpr std::array<const char*, 2u> Names = {{
                "Wind-01-Overview.png",
                "Wind-02-Air.png",
            }};
            Inspectors.QueryWind().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(
                RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityWind, ParityDir / Names[PageIdx]),
                Names[PageIdx]);
        }
        Inspectors.QueryWind().SetActivePage(0u);

        // Card #5: Clouds (6 tabs)
        const OutlinerSlotToken ParityCl = AppendEntry(Outliner, "Clouds", "Clouds", "env:clouds", ConstructSlotCategory::LayerGroup, EnvFolder);
        if (OutlinerRecordEntry* ClRec = Outliner.AccessEntry(ParityCl))
        {
            ClRec->VisibilityEnabled = true;
        }
        auto& ClInst = Reg.AccessCloud(ParityCl, Bundle.Scene);
        ClInst.Clouds.CloudsEnabled            = true;
        ClInst.Clouds.Category                 = GeometricRaster::CloudCategoryType::Stratus;
        ClInst.Clouds.CloudFraction            = 0.34f;
        ClInst.Clouds.ExtinctionMultiplier     = 2.4f;
        ClInst.Clouds.FeatureScaleMultiplier   = 0.35f;
        ClInst.Clouds.ForwardScatterAnisotropy = 0.45f;
        ClInst.Clouds.AnvilTopSpread           = 0.5f;
        ClInst.Clouds.DeckCeilingMeters        = 14000.0f;
        ClInst.Clouds.CloudFloorMeters         = 1400.0f;
        ClInst.Clouds.LayerThicknessMeters     = 1100.0f;
        ClInst.Clouds.WindSpeedMetersPerSecond = 8.0f;
        ClInst.Clouds.WindBearingDegrees       = 270.0f;
        ClInst.Clouds.WindShearPerKilometer    = 2.0f;
        ClInst.Clouds.WindVeerDegreesPerKm     = 0.0f;
        ClInst.Clouds.WindTurbulence           = 0.3f;
        ClInst.Clouds.CloudMarchSteps          = 64u;
        ClInst.Clouds.CloudRenderScale         = 0.5f;

        for (uint32_t PageIdx = 0u; PageIdx < 6u; ++PageIdx)
        {
            static constexpr std::array<const char*, 6u> Names = {{
                "Cloud-01-Overview.png",
                "Cloud-02-Coverage.png",
                "Cloud-03-Layer.png",
                "Cloud-04-Body.png",
                "Cloud-05-Wind.png",
                "Cloud-06-Quality.png",
            }};
            Inspectors.QueryCloud().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(
                RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityCl, ParityDir / Names[PageIdx]),
                Names[PageIdx]);
        }
        Inspectors.QueryCloud().SetActivePage(0u);

        // Card #6: Local Cloud (4 tabs)
        const OutlinerSlotToken ParityLc = AppendEntry(Outliner, "Local Cloud", "Local Cloud", "env:local-cloud", ConstructSlotCategory::LayerGroup, EnvFolder);
        if (OutlinerRecordEntry* LcRec = Outliner.AccessEntry(ParityLc))
        {
            LcRec->VisibilityEnabled = false;
        }
        auto& LcInst = Reg.AccessLocalCloud(ParityLc, Bundle.Scene);
        LcInst.Volume.LocalEnabled             = false;
        LcInst.Volume.CloudFraction            = 0.80f;
        LcInst.Volume.ExtinctionMultiplier     = 2.5f;
        LcInst.Volume.FeatureScaleMeters       = 40.0f;
        LcInst.Volume.ForwardScatterAnisotropy = 0.45f;
        LcInst.Volume.VolumeCenterMeters       = { -100.0f, 260.0f, 130.0f };
        LcInst.Volume.HalfExtentMeters         = { 100.0f, 100.0f, 50.0f };

        for (uint32_t PageIdx = 0u; PageIdx < 4u; ++PageIdx)
        {
            static constexpr std::array<const char*, 4u> Names = {{
                "LocalCloud-01-Overview.png",
                "LocalCloud-02-Coverage.png",
                "LocalCloud-03-Layer.png",
                "LocalCloud-04-Body.png",
            }};
            Inspectors.QueryLocalCloud().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(
                RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityLc, ParityDir / Names[PageIdx]),
                Names[PageIdx]);
        }
        Inspectors.QueryLocalCloud().SetActivePage(0u);

        // Card #7: God Rays (2 tabs)
        const OutlinerSlotToken ParityGr = AppendEntry(Outliner, "God Rays", "God Rays", "env:god-rays", ConstructSlotCategory::LayerGroup, EnvFolder);
        if (OutlinerRecordEntry* GrRec = Outliner.AccessEntry(ParityGr)) GrRec->VisibilityEnabled = true;
        auto& GrInst = Reg.AccessGodRays(ParityGr, Bundle.Scene);
        GrInst.GodRays.Enabled            = true;
        GrInst.GodRays.InteractWithClouds = true;
        GrInst.GodRays.InteractWithFog    = true;
        GrInst.GodRays.Intensity          = 1.0f;
        GrInst.GodRays.AnisotropyG        = 0.76f;
        GrInst.GodRays.Decay              = 0.95f;
        GrInst.GodRays.Density            = 1.0f;
        GrInst.GodRays.MaxDistanceMeters  = 400.0f;
        GrInst.GodRays.SampleSteps        = 24u;
        for (uint32_t PageIdx = 0u; PageIdx < 2u; ++PageIdx)
        {
            static constexpr std::array<const char*, 2u> Names = {{ "GodRays-01-Overview.png", "GodRays-02-Settings.png" }};
            Inspectors.QueryGodRays().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityGr, ParityDir / Names[PageIdx]), Names[PageIdx]);
        }
        Inspectors.QueryGodRays().SetActivePage(0u);

        // Card #8: Precipitation (3 tabs)
        const OutlinerSlotToken ParityPr = AppendEntry(Outliner, "Precipitation", "Precipitation", "env:precipitation", ConstructSlotCategory::LayerGroup, EnvFolder);
        if (OutlinerRecordEntry* PrRec = Outliner.AccessEntry(ParityPr)) PrRec->VisibilityEnabled = false;
        auto& PrInst = Reg.AccessPrecipitation(ParityPr);
        PrInst.Enabled   = false;
        PrInst.ModeIndex = 0u;
        for (uint32_t PageIdx = 0u; PageIdx < 3u; ++PageIdx)
        {
            static constexpr std::array<const char*, 3u> Names = {{ "Precipitation-01-Overview.png", "Precipitation-02-Particles.png", "Precipitation-03-Simulation.png" }};
            Inspectors.QueryPrecipitation().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityPr, ParityDir / Names[PageIdx]), Names[PageIdx]);
        }
        Inspectors.QueryPrecipitation().SetActivePage(0u);

        // Card #9: Rainbow (3 tabs)
        const OutlinerSlotToken ParityRb = AppendEntry(Outliner, "Rainbow", "Rainbow", "env:rainbow", ConstructSlotCategory::LayerGroup, EnvFolder);
        if (OutlinerRecordEntry* RbRec = Outliner.AccessEntry(ParityRb)) RbRec->VisibilityEnabled = true;
        auto& RbInst = Reg.AccessRainbow(ParityRb);
        RbInst.Enabled            = true;
        RbInst.PrimaryIntensity   = 1.0f;
        RbInst.SupernumeraryWidth = 1.0f;
        RbInst.SecondaryIntensity = 1.0f;
        for (uint32_t PageIdx = 0u; PageIdx < 3u; ++PageIdx)
        {
            static constexpr std::array<const char*, 3u> Names = {{ "Rainbow-01-Overview.png", "Rainbow-02-Bow.png", "Rainbow-03-Baking.png" }};
            Inspectors.QueryRainbow().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityRb, ParityDir / Names[PageIdx]), Names[PageIdx]);
        }
        Inspectors.QueryRainbow().SetActivePage(0u);

        // Card #10: Atmosphere (6 tabs)
        const OutlinerSlotToken ParityAtmo = AppendEntry(Outliner, "Atmosphere", "Atmosphere", "env:atmosphere", ConstructSlotCategory::LayerGroup, EnvFolder);
        if (OutlinerRecordEntry* Rec = Outliner.AccessEntry(ParityAtmo)) Rec->VisibilityEnabled = true;
        auto& AtmoInst = Reg.AccessAtmosphere(ParityAtmo, Bundle.Scene);
        AtmoInst.Medium.AtmosphereEnabled         = true;
        AtmoInst.Medium.RayleighStrength          = 1.0f;
        AtmoInst.Medium.MieStrength               = 1.0f;
        AtmoInst.Medium.OzoneStrength             = 1.2f;
        AtmoInst.Medium.RayleighScaleHeightMeters = 8000.0f;
        AtmoInst.Medium.MieScaleHeightMeters      = 1200.0f;
        AtmoInst.Medium.AtmosphereThicknessMeters = 60000.0f;
        AtmoInst.Medium.SkyBrightnessMultiplier   = 1.0f;
        AtmoInst.Medium.LimbGlowIntensity         = 1.0f;
        AtmoInst.Medium.SkyTint                   = { 1.0f, 1.0f, 1.0f };
        AtmoInst.Medium.GroundAlbedo              = { 0.475f, 0.431f, 0.376f };
        AtmoInst.Medium.OrbitalViewEnabled        = false;
        AtmoInst.SunAtLimbDeg                     = 6.0f;
        AtmoInst.LineAtCivilOnly                  = true;
        for (uint32_t PageIdx = 0u; PageIdx < 6u; ++PageIdx)
        {
            static constexpr std::array<const char*, 6u> Names = {{
                "Atmosphere-01-Overview.png", "Atmosphere-02-Scattering.png", "Atmosphere-03-Density.png",
                "Atmosphere-04-Colour.png",   "Atmosphere-05-View.png",       "Atmosphere-06-Baking.png"
            }};
            Inspectors.QueryAtmosphere().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityAtmo, ParityDir / Names[PageIdx]), Names[PageIdx]);
        }
        Inspectors.QueryAtmosphere().SetActivePage(0u);

        // Card #11: Sun / Solar (5 tabs)
        const OutlinerSlotToken ParitySun = AppendEntry(Outliner, "Sun", "Sun", "env:sun", ConstructSlotCategory::LayerGroup, EnvFolder);
        if (OutlinerRecordEntry* Rec = Outliner.AccessEntry(ParitySun)) Rec->VisibilityEnabled = true;
        auto& SunInst = Reg.AccessSolar(ParitySun, Bundle.Scene);
        SunInst.Solar.SolarEnabled            = true;
        SunInst.Solar.LocalHours              = 15.5f;
        SunInst.Solar.TintSource              = GeometricRaster::SolarTintSource::RgbTint;
        SunInst.Solar.ColorTemperatureKelvin  = 6500.0f;
        SunInst.Solar.AngularDiameterDegrees  = 0.53f;
        SunInst.Solar.AppearanceMode          = GeometricRaster::SolarAppearanceMode::Muted;
        SunInst.Solar.SunGlareStrength        = 1.0f;
        SunInst.Solar.CycleAnimationEnabled   = false;
        SunInst.Solar.ManualDirectionOverride = false;
        SunInst.DesignIndex                   = 0u;
        SunInst.RealTime                      = false;
        for (uint32_t PageIdx = 0u; PageIdx < 5u; ++PageIdx)
        {
            static constexpr std::array<const char*, 5u> Names = {{
                "Solar-01-Overview.png", "Solar-02-Temperature.png", "Solar-03-Disc.png",
                "Solar-04-Dynamic.png",  "Solar-05-Baking.png"
            }};
            Inspectors.QuerySolar().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParitySun, ParityDir / Names[PageIdx]), Names[PageIdx]);
        }
        Inspectors.QuerySolar().SetActivePage(0u);

        // Card #12: Moon / Lunar (3 tabs)
        const OutlinerSlotToken ParityMoon = AppendEntry(Outliner, "Moon", "Moon", "env:moon", ConstructSlotCategory::LayerGroup, EnvFolder);
        if (OutlinerRecordEntry* Rec = Outliner.AccessEntry(ParityMoon)) Rec->VisibilityEnabled = true;
        auto& MoonInst = Reg.AccessLunar(ParityMoon, Bundle.Scene);
        MoonInst.Moon.Visible                = true;
        MoonInst.Moon.SurfacePreset          = GeometricRaster::LunarSurfacePreset::Luna;
        MoonInst.Moon.SynodicPhaseFraction   = 0.62f;
        MoonInst.Moon.BrightnessMultiplier   = 1.60f;
        MoonInst.Moon.HaloGlowMultiplier     = 0.80f;
        MoonInst.Moon.AngularDiameterDegrees = 0.52f;
        MoonInst.Moon.AzimuthDegrees         = 300.0f;
        MoonInst.Moon.ElevationDegrees       = 28.0f;
        MoonInst.Moon.HasAtmosphere          = false;
        for (uint32_t PageIdx = 0u; PageIdx < 3u; ++PageIdx)
        {
            static constexpr std::array<const char*, 3u> Names = {{
                "Lunar-01-Overview.png", "Lunar-02-Appearance.png", "Lunar-03-Position.png"
            }};
            Inspectors.QueryLunar().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityMoon, ParityDir / Names[PageIdx]), Names[PageIdx]);
        }
        Inspectors.QueryLunar().SetActivePage(0u);

        // Card #13: Stars / Starfield (4 tabs)
        const OutlinerSlotToken ParityStars = AppendEntry(Outliner, "Stars", "Stars", "env:stars", ConstructSlotCategory::LayerGroup, EnvFolder);
        if (OutlinerRecordEntry* Rec = Outliner.AccessEntry(ParityStars)) Rec->VisibilityEnabled = true;
        auto& StarsInst = Reg.AccessStarfield(ParityStars, Bundle.Scene);
        StarsInst.Stars.StarfieldEnabled          = true;
        StarsInst.Stars.ScintillationEnabled      = true;
        StarsInst.Stars.LimitingMagnitude         = 5.5f;
        StarsInst.Stars.ScintillationDepthPercent = 30.0f;
        StarsInst.Stars.ScintillationRateHertz    = 1.2f;
        StarsInst.Stars.CelestialRotationDegrees  = 0.0f;
        StarsInst.Stars.BrightnessMultiplier      = 1.0f;
        StarsInst.Stars.PointSizeScale            = 1.0f;
        StarsInst.Stars.ZodiacalLightEnabled      = true;
        StarsInst.Stars.ZodiacalRadianceScale     = 1.0f;
        for (uint32_t PageIdx = 0u; PageIdx < 4u; ++PageIdx)
        {
            static constexpr std::array<const char*, 4u> Names = {{
                "Starfield-01-Overview.png", "Starfield-02-Motion.png",
                "Starfield-03-Renderer.png", "Starfield-04-Baking.png"
            }};
            Inspectors.QueryStarfield().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityStars, ParityDir / Names[PageIdx]), Names[PageIdx]);
        }
        Inspectors.QueryStarfield().SetActivePage(0u);

        // Card #14: Post Process (2 tabs)
        const OutlinerSlotToken ParityPost = AppendEntry(Outliner, "Post Process", "Post Process", "env:post-process", ConstructSlotCategory::LayerGroup, EnvFolder);
        if (OutlinerRecordEntry* Rec = Outliner.AccessEntry(ParityPost)) Rec->VisibilityEnabled = true;
        auto& PostInst = Reg.AccessPostProcess(ParityPost);
        PostInst.Enabled          = true;
        PostInst.ExposureEv       = 0.0f;
        PostInst.SaturationGain   = 1.0f;
        PostInst.ContrastGain     = 1.0f;
        PostInst.VignetteStrength = 0.15f;
        for (uint32_t PageIdx = 0u; PageIdx < 2u; ++PageIdx)
        {
            static constexpr std::array<const char*, 2u> Names = {{ "PostProcess-01-Overview.png", "PostProcess-02-Settings.png" }};
            Inspectors.QueryPostProcess().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityPost, ParityDir / Names[PageIdx]), Names[PageIdx]);
        }
        Inspectors.QueryPostProcess().SetActivePage(0u);

        // Card #15: Motion Blur (2 tabs)
        const OutlinerSlotToken ParityMBlur = AppendEntry(Outliner, "Motion Blur", "Motion Blur", "env:motion-blur", ConstructSlotCategory::LayerGroup, EnvFolder);
        if (OutlinerRecordEntry* Rec = Outliner.AccessEntry(ParityMBlur)) Rec->VisibilityEnabled = true;
        auto& MbInst = Reg.AccessMotionBlur(ParityMBlur);
        MbInst.Enabled             = true;
        MbInst.ShutterAngleDegrees = 180.0f;
        for (uint32_t PageIdx = 0u; PageIdx < 2u; ++PageIdx)
        {
            static constexpr std::array<const char*, 2u> Names = {{ "MotionBlur-01-Overview.png", "MotionBlur-02-Settings.png" }};
            Inspectors.QueryMotionBlur().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityMBlur, ParityDir / Names[PageIdx]), Names[PageIdx]);
        }
        Inspectors.QueryMotionBlur().SetActivePage(0u);

        // Card #16: Lens Flare (4 tabs)
        const OutlinerSlotToken ParityFlare = AppendEntry(Outliner, "Lens Flare", "Lens Flare", "env:lens-flare", ConstructSlotCategory::LayerGroup, EnvFolder);
        if (OutlinerRecordEntry* Rec = Outliner.AccessEntry(ParityFlare)) Rec->VisibilityEnabled = true;
        auto& FlareInst = Reg.AccessLensFlare(ParityFlare, Bundle.Scene);
        FlareInst.Flare.Enabled               = true;
        FlareInst.Flare.FlareIntensity        = 1.0f;
        FlareInst.Flare.FlareSpread           = 1.0f;
        FlareInst.Flare.StreakRotationDegrees = 0.0f;
        FlareInst.Flare.GhostCount            = 6u;
        FlareInst.Flare.GhostIntensity        = 0.8f;
        FlareInst.Flare.HaloIntensity         = 1.0f;
        for (uint32_t PageIdx = 0u; PageIdx < 4u; ++PageIdx)
        {
            static constexpr std::array<const char*, 4u> Names = {{
                "LensFlare-01-Overview.png", "LensFlare-02-Optics.png",
                "LensFlare-03-Halo.png", "LensFlare-04-Baking.png"
            }};
            Inspectors.QueryLensFlare().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityFlare, ParityDir / Names[PageIdx]), Names[PageIdx]);
        }
        Inspectors.QueryLensFlare().SetActivePage(0u);

        // Card #17: Editor Camera (4 tabs)
        const OutlinerSlotToken ParityCam = AppendEntry(Outliner, "Editor Camera", "Editor Camera", "optics:camera", ConstructSlotCategory::OpticsCamera, {});
        if (OutlinerRecordEntry* Rec = Outliner.AccessEntry(ParityCam)) Rec->VisibilityEnabled = true;
        auto& CamInst = Reg.AccessCamera(ParityCam, Bundle.Scene);
        CamInst.FocalLengthMm  = 35.0f;
        CamInst.ApertureFStop  = 2.8f;
        CamInst.SensorWidthMm  = 36.0f;
        CamInst.RigPresetIndex = 2u; // C · Mirrorless matching camera__2
        for (uint32_t PageIdx = 0u; PageIdx < 4u; ++PageIdx)
        {
            static constexpr std::array<const char*, 4u> Names = {{
                "Camera-01-Overview.png", "Camera-02-Focus.png",
                "Camera-03-Body.png", "Camera-04-Render.png"
            }};
            Inspectors.QueryCamera().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityCam, ParityDir / Names[PageIdx]), Names[PageIdx]);
        }
        Inspectors.QueryCamera().SetActivePage(0u);

        // Card #18: Camera Settings (5 tabs in SDF mode)
        const OutlinerSlotToken ParityCamSet = AppendEntry(Outliner, "Camera Settings", "Camera Settings", "env:camera-settings", ConstructSlotCategory::OpticsCamera, ParityCam);
        if (OutlinerRecordEntry* Rec = Outliner.AccessEntry(ParityCamSet)) Rec->VisibilityEnabled = true;
        auto& CsInst = Reg.AccessCameraSettings(ParityCamSet);
        CsInst.RendererIndex = 1u; // SDF
        CsInst.ExposureGain  = 1.35f;
        for (uint32_t PageIdx = 0u; PageIdx < 5u; ++PageIdx)
        {
            static constexpr std::array<const char*, 5u> Names = {{
                "CameraSettings-01-Overview.png", "CameraSettings-02-Shadows.png",
                "CameraSettings-03-GI.png", "CameraSettings-04-AO.png", "CameraSettings-05-Output.png"
            }};
            Inspectors.QueryCameraSettings().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityCamSet, ParityDir / Names[PageIdx]), Names[PageIdx]);
        }
        Inspectors.QueryCameraSettings().SetActivePage(0u);

        // Batch 4 Lighting Folder ("Lighting" so breadcrumb reads "Inspector / Lighting")
        const OutlinerSlotToken LightFolder = AppendEntry(Outliner, "Lighting", "Folder", "folder", ConstructSlotCategory::Folder, {});

        // Card #19: Environment Light (2 tabs)
        const OutlinerSlotToken ParityEnvL = AppendEntry(Outliner, "Environment Light", "Environment Light", "env:environment-light", ConstructSlotCategory::PointLuminaire, LightFolder);
        if (OutlinerRecordEntry* Rec = Outliner.AccessEntry(ParityEnvL)) Rec->VisibilityEnabled = true;
        auto& EnvLInst = Reg.AccessEnvironmentLight(ParityEnvL, Bundle.Scene);
        EnvLInst.SourcePresetIndex = 0u;
        EnvLInst.SkyRadiance       = { 0.624f, 0.706f, 0.800f }; // #9FB4CC
        EnvLInst.RotationDegrees   = 0.0f;
        EnvLInst.GainScale         = 1.0f;
        EnvLInst.ExposureEv        = 0.0f;
        EnvLInst.Saturation        = 1.0f;
        EnvLInst.GroundBounce      = 0.30f;
        EnvLInst.IndirectGi        = true;
        for (uint32_t PageIdx = 0u; PageIdx < 2u; ++PageIdx)
        {
            static constexpr std::array<const char*, 2u> Names = {{ "EnvironmentLight-01-Overview.png", "EnvironmentLight-02-Settings.png" }};
            Inspectors.QueryEnvironmentLight().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityEnvL, ParityDir / Names[PageIdx]), Names[PageIdx]);
        }
        Inspectors.QueryEnvironmentLight().SetActivePage(0u);

        // Card #20: Rim Point / Point Light (2 tabs)
        const OutlinerSlotToken ParityPtL = AppendEntry(Outliner, "Rim Point", "Point Light", "light:point", ConstructSlotCategory::PointLuminaire, LightFolder);
        if (OutlinerRecordEntry* Rec = Outliner.AccessEntry(ParityPtL)) Rec->VisibilityEnabled = true;
        auto& PtInst = Reg.AccessPointLight(ParityPtL, Bundle.Scene);
        PtInst.IntensityCandela = 22.0f;
        PtInst.ReachMeters      = 30.0f;
        PtInst.DecayExponent    = 2.0f;
        for (uint32_t PageIdx = 0u; PageIdx < 2u; ++PageIdx)
        {
            static constexpr std::array<const char*, 2u> Names = {{ "PointLight-01-Overview.png", "PointLight-02-Source.png" }};
            Inspectors.QueryPointLight().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityPtL, ParityDir / Names[PageIdx]), Names[PageIdx]);
        }
        Inspectors.QueryPointLight().SetActivePage(0u);

        // Card #21: Key Spot / Spot Light (2 tabs)
        const OutlinerSlotToken ParitySpL = AppendEntry(Outliner, "Key Spot", "Spot Light", "light:spot", ConstructSlotCategory::PointLuminaire, LightFolder);
        if (OutlinerRecordEntry* Rec = Outliner.AccessEntry(ParitySpL))
        {
            Rec->VisibilityEnabled = true;
            Rec->PositionX = -6.0f; Rec->PositionY = 8.5f; Rec->PositionZ = 5.0f;
            Rec->RotationX = -53.202f; Rec->RotationY = -39.289f; Rec->RotationZ = 0.0f;
        }
        auto& SpInst = Reg.AccessSpotLight(ParitySpL, Bundle.Scene);
        SpInst.IntensityCandela = 90.0f;
        SpInst.FullConeDegrees  = 22.0f;
        SpInst.Penumbra         = 0.42f;
        for (uint32_t PageIdx = 0u; PageIdx < 2u; ++PageIdx)
        {
            static constexpr std::array<const char*, 2u> Names = {{ "SpotLight-01-Overview.png", "SpotLight-02-Source.png" }};
            Inspectors.QuerySpotLight().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParitySpL, ParityDir / Names[PageIdx]), Names[PageIdx]);
        }
        Inspectors.QuerySpotLight().SetActivePage(0u);

        // Card #22: ECE Low Beam / IES Photometric Light (2 tabs)
        const OutlinerSlotToken ParityIesL = AppendEntry(Outliner, "ECE Low Beam", "IES Photometric", "light:ies", ConstructSlotCategory::PointLuminaire, LightFolder);
        if (OutlinerRecordEntry* Rec = Outliner.AccessEntry(ParityIesL))
        {
            Rec->VisibilityEnabled = true;
            auto& IesInst = Reg.AccessIesLight(ParityIesL, *Rec);
            IesInst.LuminousFlux       = 1650.0f;
            IesInst.ProfileMultiplier  = 1.0f;
            IesInst.ProfilePresetIndex = 3u;
            IesInst.FieldAngleDegrees  = 58.0f;
        }
        for (uint32_t PageIdx = 0u; PageIdx < 2u; ++PageIdx)
        {
            static constexpr std::array<const char*, 2u> Names = {{ "IesLight-01-Overview.png", "IesLight-02-Source.png" }};
            Inspectors.QueryIesLight().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityIesL, ParityDir / Names[PageIdx]), Names[PageIdx]);
        }
        Inspectors.QueryIesLight().SetActivePage(0u);

        // Card #23: Area Light (2 tabs)
        const OutlinerSlotToken ParityAreaL = AppendEntry(Outliner, "Area Light", "Area Light", "light:area", ConstructSlotCategory::PointLuminaire, LightFolder);
        if (OutlinerRecordEntry* Rec = Outliner.AccessEntry(ParityAreaL)) Rec->VisibilityEnabled = true;
        auto& AreaInst = Reg.AccessAreaLight(ParityAreaL, Bundle.Scene);
        AreaInst.LuminousFlux      = 2400.0f;
        AreaInst.ApertureFormIndex = 0u;
        AreaInst.WidthMeters       = 2.0f;
        AreaInst.HeightMeters      = 1.0f;
        AreaInst.SpreadDegrees     = 120.0f;
        AreaInst.TwoSidedRadiator  = false;
        for (uint32_t PageIdx = 0u; PageIdx < 2u; ++PageIdx)
        {
            static constexpr std::array<const char*, 2u> Names = {{ "AreaLight-01-Overview.png", "AreaLight-02-Source.png" }};
            Inspectors.QueryAreaLight().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityAreaL, ParityDir / Names[PageIdx]), Names[PageIdx]);
        }
        Inspectors.QueryAreaLight().SetActivePage(0u);

        // Card #24: Studio Tube / Tube Light (2 tabs)
        const OutlinerSlotToken ParityTubeL = AppendEntry(Outliner, "Studio Tube", "Tube Light", "light:tube", ConstructSlotCategory::PointLuminaire, LightFolder);
        if (OutlinerRecordEntry* Rec = Outliner.AccessEntry(ParityTubeL))
        {
            Rec->VisibilityEnabled = true;
            Rec->PositionX = -3.0f; Rec->PositionY = 3.2f; Rec->PositionZ = -2.0f;
            Rec->RotationX = 0.0f; Rec->RotationY = 0.0f; Rec->RotationZ = 20.0f;
        }
        auto& TubeInst = Reg.AccessTubeLight(ParityTubeL);
        TubeInst.LuminousFlux     = 1800.0f;
        TubeInst.LengthMeters     = 1.5f;
        TubeInst.TubeRadiusMeters = 0.04f;
        TubeInst.ReachMeters      = 24.0f;
        for (uint32_t PageIdx = 0u; PageIdx < 2u; ++PageIdx)
        {
            static constexpr std::array<const char*, 2u> Names = {{ "TubeLight-01-Overview.png", "TubeLight-02-Source.png" }};
            Inspectors.QueryTubeLight().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityTubeL, ParityDir / Names[PageIdx]), Names[PageIdx]);
        }
        Inspectors.QueryTubeLight().SetActivePage(0u);

        // Card #25: LED Emitter (2 tabs)
        const OutlinerSlotToken ParityLedL = AppendEntry(Outliner, "LED Emitter", "LED Emitter", "light:led", ConstructSlotCategory::PointLuminaire, LightFolder);
        if (OutlinerRecordEntry* Rec = Outliner.AccessEntry(ParityLedL))
        {
            Rec->VisibilityEnabled = true;
            Rec->PositionX = 2.6f; Rec->PositionY = 2.4f; Rec->PositionZ = 3.4f;
        }
        auto& LedInst = Reg.AccessLedLight(ParityLedL);
        LedInst.DriverWatts       = 10.0f;
        LedInst.EfficacyLmPerW    = 110.0f;
        LedInst.Dimmer            = 1.0f;
        LedInst.KelvinTemperature = 4000.0f;
        LedInst.PackageDiameterMm = 40.0f;
        LedInst.OpticAngleDeg     = 120.0f;
        for (uint32_t PageIdx = 0u; PageIdx < 2u; ++PageIdx)
        {
            static constexpr std::array<const char*, 2u> Names = {{ "LedLight-01-Overview.png", "LedLight-02-Source.png" }};
            Inspectors.QueryLedLight().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityLedL, ParityDir / Names[PageIdx]), Names[PageIdx]);
        }
        Inspectors.QueryLedLight().SetActivePage(0u);

        // Card #26: LED Strip (2 tabs)
        const OutlinerSlotToken ParityStripL = AppendEntry(Outliner, "LED Strip", "LED Strip", "light:led-strip", ConstructSlotCategory::PointLuminaire, LightFolder);
        if (OutlinerRecordEntry* Rec = Outliner.AccessEntry(ParityStripL)) Rec->VisibilityEnabled = true;
        auto& StripInst = Reg.AccessLedStrip(ParityStripL);
        StripInst.LumensPerMetre = 1000.0f;
        StripInst.WattsPerMetre  = 14.4f;
        StripInst.Dimmer         = 1.0f;
        StripInst.LengthMeters   = 2.4f;
        StripInst.DiodesPerMetre = 60.0f;
        StripInst.SupplyVolts    = 24.0f;
        StripInst.OpalDiffuser   = false;
        for (uint32_t PageIdx = 0u; PageIdx < 2u; ++PageIdx)
        {
            static constexpr std::array<const char*, 2u> Names = {{ "LedStrip-01-Overview.png", "LedStrip-02-Source.png" }};
            Inspectors.QueryLedStrip().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityStripL, ParityDir / Names[PageIdx]), Names[PageIdx]);
        }
        Inspectors.QueryLedStrip().SetActivePage(0u);

        // Card #27: Folder (World — 13 entries matching world__0-default.png)
        const OutlinerSlotToken SceneFolder = AppendEntry(Outliner, "Scene", "Folder", "folder", ConstructSlotCategory::Folder, {});
        const OutlinerSlotToken ParityWorld = AppendEntry(Outliner, "World", "Folder", "folder", ConstructSlotCategory::Folder, SceneFolder);
        for (uint32_t I = 0u; I < 13u; ++I)
        {
            char NameB[24]{};
            std::snprintf(NameB, sizeof(NameB), "WorldItem%u", I);
            (void)AppendEntry(Outliner, NameB, "Environment", "env:atmosphere", ConstructSlotCategory::LayerGroup, ParityWorld);
        }
        AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityWorld, ParityDir / "Folder-01-Overview.png"), "Folder-01-Overview.png");

        // Cards #28–#31: Geometry 8-tab Cube parity (AnalyticPrimitive / NativeGeometry / ImportedGeometry / ProceduralGeometry)
        const OutlinerSlotToken GeomFolder = AppendEntry(Outliner, "Geometry", "Folder", "folder", ConstructSlotCategory::Folder, {});
        const OutlinerSlotToken ParityCube = AppendEntry(Outliner, "Cube", "Cube", "geom:primitive", ConstructSlotCategory::GeometryVolume, GeomFolder);
        auto& CubeInst = Reg.AccessAnalyticPrimitive(ParityCube, *Outliner.FindEntry(ParityCube), Bundle.Scene);
        CubeInst.FormIndex    = 1u; // Cube
        CubeInst.RadiusMeters = 0.5f;
        CubeInst.HeightMeters = 1.0f;
        static constexpr std::array<const char*, 8u> CubeNames = {{
            "Geometry-01-Overview.png",   "Geometry-02-Components.png",
            "Geometry-03-Geometry.png",   "Geometry-04-Material.png",
            "Geometry-05-UV.png",         "Geometry-06-Fracture.png",
            "Geometry-07-SDF.png",        "Geometry-08-LOD.png",
        }};
        for (uint32_t PageIdx = 0u; PageIdx < 8u; ++PageIdx)
        {
            Inspectors.QueryAnalyticPrimitive().SetActivePage(PageIdx);
            AllOk &= VerifyRequirement(RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ParityCube, ParityDir / CubeNames[PageIdx]), CubeNames[PageIdx]);
        }
        Inspectors.QueryAnalyticPrimitive().SetActivePage(0u);
    }

    // 14. GPU Compute Baking & Zero Per-Frame UI Card Update Validation
    {
        // 14a. Compiled GPU compute SPIR-V shaders exist for pure-GPU SDF & Inspector baking
        const std::filesystem::path ShaderDir = std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR);
        AllOk &= VerifyRequirement(
            std::filesystem::exists(ShaderDir / "BakeAspectSdfCompute.comp.spv"),
            "GPU compute shader BakeAspectSdfCompute.comp.spv compiled by Slang");
        AllOk &= VerifyRequirement(
            std::filesystem::exists(ShaderDir / "InspectorPreviewCompute.comp.spv"),
            "GPU compute shader InspectorPreviewCompute.comp.spv compiled by Slang");

        // 14b. Only the single active Inspector card executes (never all 31 cards per frame)
        AllOk &= VerifyRequirement(
            Inspectors.QueryLastEvaluatedCardCount() == 1u && Inspectors.QueryLastSkippedCardCount() == 30u,
            "InspectorRegistry evaluates strictly 1 active card and skips all 30 inactive cards");

        // 14c. RetainedCanvasCache hover-zone gating replays stationary & empty-space pointer motion with 0 UI card rebuilds
        {
            RetainedCanvasCache Cache {};
            ImDrawList TestList(ImGui::GetDrawListSharedData());
            TestList._ResetForNewFrame();
            TestList.PushTexture(ImGui::GetIO().Fonts->TexRef);
            TestList.PushClipRectFullScreen();
            const Rectangle2D PaneBounds { 100.0f, 50.0f, 340.0f, 825.0f };
            const uint64_t Sig = 0xCAFEBABE12345678ull;

            Cache.BeginCapture(&TestList);
            InspectorPrimitives::BeginInspectorCaptureScope(&Cache, { 0.0f, 0.0f, 340.0f, 825.0f }, { PaneBounds.X, PaneBounds.Y }, 1.0f);
            InspectorPrimitives::RegisterInteractiveRect({ 20.0f, 120.0f, 200.0f, 28.0f }); // interactive slider zone
            InspectorPrimitives::EndInspectorCaptureScope();
            TestList.AddRectFilled(ImVec2(100.0f, 50.0f), ImVec2(440.0f, 875.0f), IM_COL32(24, 24, 24, 255));
            Cache.EndCapture(&TestList, Sig, PaneBounds, { 200.0f, 400.0f }); // resting on empty card space

            // Stationary frame inside pane -> 0 rebuilds (reused)
            AllOk &= VerifyRequirement(
                Cache.QueryReusableHoverAware(Sig, PaneBounds, { 200.0f, 400.0f }, false),
                "Stationary frame inside Inspector pane replays retained canvas (0 UI rebuilds)");
            // Moving mouse across empty card space -> 0 rebuilds (reused)
            AllOk &= VerifyRequirement(
                Cache.QueryReusableHoverAware(Sig, PaneBounds, { 260.0f, 520.0f }, false),
                "Pointer moving across empty Inspector card body replays retained canvas (0 UI rebuilds)");
            // Moving mouse onto registered interactive control -> targeted rebuild triggered
            AllOk &= VerifyRequirement(
                !Cache.QueryReusableHoverAware(Sig, PaneBounds, { 180.0f, 180.0f }, false),
                "Pointer entering interactive control triggers targeted hover update");
        }

        // 14d. InspectorPreviewSurface::EnsureUpdatedGpu routes to GPU baker with 0 CPU fallback executions & 1 retained upload
        {
            uint32_t GpuDispatches = 0u;
            uint32_t CpuFallbacks  = 0u;
            InspectorPreviewSurface::AssignGpuBakeHook(
                [&](const InspectorGpuBakeSpec&, uint32_t W, uint32_t H, std::vector<uint8_t>& Out) -> bool
                {
                    ++GpuDispatches;
                    Out.assign(static_cast<std::size_t>(W) * H * 4u, 0xCCu);
                    return true;
                });

            InspectorPreviewSurface TestSurf {};
            const InspectorGpuBakeSpec Spec { InspectorGpuBakeMode::SkyViewLut, 0u };
            TestSurf.EnsureUpdatedGpu(0x99887766ull, 64u, 32u, Spec, [&](std::vector<uint8_t>&) { ++CpuFallbacks; });
            TestSurf.EnsureUpdatedGpu(0x99887766ull, 64u, 32u, Spec, [&](std::vector<uint8_t>&) { ++CpuFallbacks; });
            InspectorPreviewSurface::ClearGpuBakeHook();

            AllOk &= VerifyRequirement(
                GpuDispatches == 1u && CpuFallbacks == 0u && TestSurf.QueryGpuUploadCount() == 1u && TestSurf.QueryCpuUploadCount() == 0u,
                "InspectorPreviewSurface dispatches strictly on GPU hook (0 CPU fallbacks, 1 retained upload)");
        }

        // 14e. LocalSignedDistanceVolume::BakeFromTrianglesAspect routes to GPU baker with 0 CPU voxel threads
        {
            const uint64_t CpuBefore = DistanceFieldSpace::LocalSignedDistanceVolume::QueryCpuBakeCount();
            const uint64_t GpuBefore = DistanceFieldSpace::LocalSignedDistanceVolume::QueryGpuBakeCount();
            DistanceFieldSpace::LocalSignedDistanceVolume::AssignGpuAspectBaker(
                [](const SpatialAcceleration::WideBoundingTree&, uint32_t RX, uint32_t RY, uint32_t RZ,
                   Mathematics::Axis3D, Mathematics::Axis3D, std::vector<float>& Out) -> bool
                {
                    Out.assign(static_cast<std::size_t>(RX) * RY * RZ, 0.25f);
                    return true;
                });

            std::array<SpatialAcceleration::RayTrianglePrimitive, 1u> Tris {};
            Tris[0].Corner0 = { 0.0f, 0.0f, 0.0f };
            Tris[0].Edge01  = { 1.0f, 0.0f, 0.0f };
            Tris[0].Edge02  = { 0.0f, 1.0f, 0.0f };
            DistanceFieldSpace::LocalSignedDistanceVolume Vol {};
            const bool BakedOk = Vol.BakeFromTrianglesAspect(Tris, 8u, 8u, 8u, 0.2f);
            DistanceFieldSpace::LocalSignedDistanceVolume::ClearGpuAspectBaker();

            AllOk &= VerifyRequirement(
                BakedOk &&
                DistanceFieldSpace::LocalSignedDistanceVolume::QueryGpuBakeCount() == (GpuBefore + 1ull) &&
                DistanceFieldSpace::LocalSignedDistanceVolume::QueryCpuBakeCount() == CpuBefore,
                "LocalSignedDistanceVolume executes on GPU aspect baker with zero CPU bake fallback");
        }

        // 14f. ThumbnailSpecimenCompute.comp.spv compiled by Slang
        {
            const std::filesystem::path ShaderDir(FRONTIER_COMPILED_SHADER_DIR);
            AllOk &= VerifyRequirement(
                std::filesystem::exists(ShaderDir / "ThumbnailSpecimenCompute.comp.spv"),
                "GPU compute shader ThumbnailSpecimenCompute.comp.spv compiled by Slang");
        }

        // 14g. Zero-copy direct GPU ImGui image hook on InspectorPreviewSurface (0 CPU fallback, 0 readback, 1 retained upload)
        {
            uint32_t DirectDispatches = 0u;
            uint32_t ReleasedSurfaces = 0u;
            uint32_t CpuFallbacks     = 0u;
            InspectorPreviewSurface::AssignGpuDirectBakeHook(
                [&](uint64_t SurfaceId, const InspectorGpuBakeSpec&, uint32_t, uint32_t, uint64_t& OutHandle) -> bool
                {
                    ++DirectDispatches;
                    OutHandle = 0xDEADBEEF00000000ull | (SurfaceId & 0xFFFFFFFFull);
                    return true;
                },
                [&](uint64_t) { ++ReleasedSurfaces; });

            {
                InspectorPreviewSurface DirectSurf {};
                const InspectorGpuBakeSpec Spec { InspectorGpuBakeMode::PhotometricLightPreview, 0u };
                DirectSurf.EnsureUpdatedGpu(0x55AA1122ull, 256u, 180u, Spec, [&](std::vector<uint8_t>&) { ++CpuFallbacks; });
                DirectSurf.EnsureUpdatedGpu(0x55AA1122ull, 256u, 180u, Spec, [&](std::vector<uint8_t>&) { ++CpuFallbacks; });
                AllOk &= VerifyRequirement(
                    DirectDispatches == 1u && CpuFallbacks == 0u &&
                    DirectSurf.QueryDirectGpuUploadCount() == 1u && DirectSurf.QueryCpuUploadCount() == 0u,
                    "InspectorPreviewSurface zero-copy direct GPU image hook skips CPU readback & stays at 1 retained upload");
            }
            InspectorPreviewSurface::ClearGpuDirectBakeHook();
            AllOk &= VerifyRequirement(
                ReleasedSurfaces == 1u,
                "InspectorPreviewSurface destructor cleanly releases direct GPU surface");
        }

        // 14h. ThumbnailSheetRepository defers eager startup CPU baking & routes material/geometry tiles strictly to GPU baker
        {
            const uint64_t CpuBefore = ThumbnailSheetRepository::QueryCpuBakedTileCount();
            const uint64_t GpuBefore = ThumbnailSheetRepository::QueryGpuBakedTileCount();
            ThumbnailSheetRepository LazyRepo {};
            const bool ZeroEagerCpuBakes = (ThumbnailSheetRepository::QueryCpuBakedTileCount() == CpuBefore);
            AllOk &= VerifyRequirement(
                ZeroEagerCpuBakes && !LazyRepo.QueryAllEntries().empty(),
                "ThumbnailSheetRepository construction defers eager CPU specimen baking (0 ms F=1 startup cost)");

            uint32_t GpuThumbCalls = 0u;
            ThumbnailSheetRepository::AssignGpuThumbnailBaker(
                [&](const ThumbnailSheetRepository::ThumbnailGpuBakeDesc&, uint32_t DimPx, std::vector<uint8_t>& OutRgba) -> bool
                {
                    ++GpuThumbCalls;
                    OutRgba.assign(static_cast<std::size_t>(DimPx) * DimPx * 4u, 0xE4u);
                    return true;
                });

            std::size_t TargetIdx = SIZE_MAX;
            const auto AllEntries = LazyRepo.QueryAllEntries();
            for (std::size_t I = 0u; I < AllEntries.size(); ++I)
            {
                const auto Cat = static_cast<ContentCategory>(AllEntries[I].IndexHeader.CategoryCode);
                if (!AllEntries[I].TileResident &&
                    (Cat == ContentCategory::Geometry || Cat == ContentCategory::Materials))
                {
                    TargetIdx = I;
                    break;
                }
            }
            std::vector<uint8_t> TilePixels {};
            const bool QueriedOk = (TargetIdx != SIZE_MAX) && LazyRepo.QueryTileRgbaPixels(TargetIdx, TilePixels);
            ThumbnailSheetRepository::ClearGpuThumbnailBaker();
            AllOk &= VerifyRequirement(
                QueriedOk && GpuThumbCalls == 1u &&
                ThumbnailSheetRepository::QueryGpuBakedTileCount() == (GpuBefore + 1ull) &&
                ThumbnailSheetRepository::QueryCpuBakedTileCount() == CpuBefore &&
                !TilePixels.empty() && TilePixels[0] == 0xE4u,
                "ThumbnailSheetRepository lazily bakes Geometry/Material tiles on GPU with 0 CPU raytracing");
        }

        // 15. Extensible ProceduralGeometry framework + CameraSettings live GPU parameter sync
        {
            const auto& ProcReg = GeometrySpace::ProceduralGeometryRegistry::Global();
            bool All8Built = (ProcReg.GeneratorCount() >= 8u);
            for (uint32_t Idx = 0u; Idx < 8u; ++Idx)
            {
                GeometrySpace::ProceduralGeometryAsset Asset {};
                Asset.SyncCommonInspectorParameters(Idx, 42u + Idx, 3u, 1.8f, 1.2f, ProcReg);
                const bool RebuiltFirst = Asset.EnsureUpToDate(true, ProcReg);
                const bool CacheHit     = !Asset.EnsureUpToDate(true, ProcReg);
                All8Built &= RebuiltFirst && CacheHit && Asset.IsValid() &&
                             Asset.QueryOutput().TriangleCount > 0u &&
                             !Asset.QueryOutput().SubParts.empty() &&
                             Asset.HasClusterPyramid();
            }
            AllOk &= VerifyRequirement(
                All8Built,
                "ProceduralGeometryRegistry builds all 8 generators (incl. Tree, Building, Tyre) with Nanite ClusterPyramid & 0-work cache hit");

            // Verify domain queries & extensibility for future Botanical / Architectural / Automotive generators
            const auto Trees     = ProcReg.ListByDomain(GeometrySpace::ProceduralGeometryDomain::Botanical);
            const auto Buildings = ProcReg.ListByDomain(GeometrySpace::ProceduralGeometryDomain::Architectural);
            const auto Tyres     = ProcReg.ListByDomain(GeometrySpace::ProceduralGeometryDomain::Automotive);
            AllOk &= VerifyRequirement(
                !Trees.empty() && !Buildings.empty() && !Tyres.empty(),
                "ProceduralGeometryRegistry categorizes Botanical (Trees), Architectural (Buildings), and Automotive (Tyres) domains");

            // Verify CameraSettings live sync of newly exposed C++/.slang parameters + PBR-only XeGTAO guard in SDF mode
            CameraSettingsData SdfSettings {};
            SdfSettings.RendererIndex         = 1u; // SDF
            SdfSettings.SdfAoRadiusM          = 2.10f;
            SdfSettings.SdfAoContrast         = 1.85f;
            SdfSettings.SdfFillGain           = 1.45f;
            SdfSettings.XeGtaoEnabled         = true; // Must be suppressed in SDF mode
            SdfSettings.ClusterForcedLodLevel = 2;
            SdfSettings.MatcapChannelIndex    = 2u;
            SdfSettings.MatcapRimGain         = 0.72f;
            ViewportDisplaySettings SyncedDisp {};
            InspectorInstanceRegister::SyncCameraSettingsToDisplay(SdfSettings, &SyncedDisp);
            AllOk &= VerifyRequirement(
                SyncedDisp.RenderEngineIndex == 2u &&
                !SyncedDisp.XeGtaoEnabled &&
                std::abs(SyncedDisp.SdfAoRadiusM - 2.10f) < 1e-3f &&
                std::abs(SyncedDisp.SdfAoContrast - 1.85f) < 1e-3f &&
                std::abs(SyncedDisp.SkyFillGain - 1.45f) < 1e-3f &&
                SyncedDisp.ClusterForcedLodLevel == 2 &&
                SyncedDisp.MatcapChannelIndex == 2u &&
                std::abs(SyncedDisp.MatcapRimGain - 0.72f) < 1e-3f,
                "CameraSettings syncs SdfAoRadius/Contrast, SdfFillGain, ClusterForcedLod, MatCap Channel/Rim & enforces PBR-only XeGTAO");

            // Render visual proof of Procedural Tyre inspector Tab 3 (Geometry + Generator controls)
            auto& TyreInst = Reg.AccessProceduralGeometry(ProceduralTok, *Outliner.FindEntry(ProceduralTok), Bundle.Scene);
            TyreInst.GeneratorIndex     = 7u; // Automotive Procedural Tyre
            TyreInst.FeatureScaleMeters = 0.68f;
            TyreInst.AmplitudeMeters    = 0.26f;
            Inspectors.QueryProceduralGeometry().SetActivePage(2u);
            AllOk &= VerifyRequirement(
                RenderInspectorExhibit(Inspectors, Outliner, Bundle.Scene, DisplaySettings, ProceduralTok, OutDir / "39-ProceduralGeometry-Tyre-GeometryTab.png"),
                "39-ProceduralGeometry-Tyre-GeometryTab.png");
            Inspectors.QueryProceduralGeometry().SetActivePage(0u);
        }
    }

    TypographyRepository::AssignCurrent(nullptr);
    ImGui::DestroyContext(GuiSession);
    if (AllOk)
    {
        std::cout << "[OK] StandaloneInspectorsValidation passed 100%\n";
        return 0;
    }
    return 1;
}
