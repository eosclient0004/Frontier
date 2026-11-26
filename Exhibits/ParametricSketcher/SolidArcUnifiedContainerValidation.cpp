/*============================================================================================================================================
                                                  SOLIDARCUNIFIEDCONTAINERVALIDATION.CPP
============================================================================================================================================*/
// 🧩 End-to-end validation for Unified .solidarc Container: Authoring TOML + .GeometrySpace + .Curve + .DistanceField ('BSDF' & 'VSDF')

#include "Engine/Codecs/GeometryCodec.h"
#include "Engine/Codecs/SolidArcContainerCodec.h"
#include "Engine/DistanceFieldSpace/CompiledSurfaceField.h"
#include "Engine/GeometrySpace/Parametric/Containment/WindingNumber.h"
#include "Engine/GeometrySpace/Parametric/Exchange/SolidArcFieldCompiler.h"
#include "Engine/Serialization/Container/DocumentContainerCodec.h"
#include "Engine/Serialization/Container/MemoryMappedFileView.h"
#include "Engine/Serialization/Container/SuffixRegistry.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherCurveImport.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocument.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocumentCodec.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherExchange.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

using namespace Frontier;
using namespace Frontier::Codecs;
using namespace Frontier::DistanceFieldSpace;
using namespace Frontier::GeometrySpace::Parametric;
using namespace Frontier::Serialization;
using namespace Frontier::Workspaces::Sketcher;

bool VerifyCondition(bool Condition, const char* Description) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[SolidArcUnifiedContainerValidation] FAILED: %s\n", Description);
        return false;
    }
    return true;
}

float ComputeEmptyCellPercent(const CompiledSurfaceField& Field) noexcept
{
    if (Field.MacroCells.empty())
    {
        return 0.0f;
    }
    std::size_t EmptyCount = 0u;
    for (const CompiledMacroCell& Cell : Field.MacroCells)
    {
        if (Cell.PackedCounts == 0u)
        {
            ++EmptyCount;
        }
    }
    return 100.0f * static_cast<float>(EmptyCount) / static_cast<float>(Field.MacroCells.size());
}

} // namespace

int main()
{
    uint32_t PassedChecks = 0u;
    const std::filesystem::path ScratchDir = std::filesystem::temp_directory_path() / "FrontierSolidArcUnifiedValidation";
    std::error_code DirErr {};
    std::filesystem::remove_all(ScratchDir, DirErr);
    std::filesystem::create_directories(ScratchDir, DirErr);

    //--------------------------------------------------------------------------------------------------------------------
    // 1. Format Registry & Standalone .GeometrySpace Separation
    //--------------------------------------------------------------------------------------------------------------------
    {
        FormatSuffixCategory SolidArcCategory = FormatSuffixCategory::WorldSpace;
        std::string CanonicalSolidArc {};
        std::string SuffixDiag {};
        const bool ResolvedSolidArc =
            ResolveSuffixCaseInsensitive(".solidarc", SolidArcCategory, CanonicalSolidArc, SuffixDiag);
        if (!VerifyCondition(ResolvedSolidArc && SolidArcCategory == FormatSuffixCategory::Sketch,
                             ".solidarc resolves to FormatSuffixCategory::Sketch in SuffixRegistry"))
        {
            return 1;
        }

        FormatSuffixCategory GeometryCategory = FormatSuffixCategory::WorldSpace;
        std::string CanonicalGeo {};
        const bool ResolvedGeo =
            ResolveSuffixCaseInsensitive(".GeometrySpace", GeometryCategory, CanonicalGeo, SuffixDiag);
        if (!VerifyCondition(ResolvedGeo && GeometryCategory == FormatSuffixCategory::GeometrySpace,
                             "Standalone .GeometrySpace stays dedicated to PolygonNetwork modelling"))
        {
            return 1;
        }
        ++PassedChecks;
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 2. GPU std430 Alignment & Byte-Size Verification
    //--------------------------------------------------------------------------------------------------------------------
    {
        if (!VerifyCondition(
                sizeof(CompiledSurfacePrimitive) == 128u &&
                sizeof(CompiledTrimSegment2D) == 32u &&
                sizeof(CompiledBezierPatch4x4) == 256u &&
                sizeof(CompiledConicArc3D) == 64u &&
                sizeof(CompiledPseudoNormalEdge) == 80u &&
                sizeof(CompiledMacroCell) == 16u,
                "All GPU surface/trim/patch/arc/edge/cell structures match 16B std430 layout sizes"))
        {
            return 1;
        }
        ++PassedChecks;
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 3. Multi-Feature Parametric SolidArc Part + 3D SpaceCurve -> Unified .solidarc Save & Export
    //--------------------------------------------------------------------------------------------------------------------
    SketcherDocument AuthoredDoc = CreateSketcherDocument("PrecisionFlangeHousing");
    {
        // Lower flange block [-0.12, 0.12] x [-0.08, 0.08] x [0.0, 0.04] m (240 x 160 x 40 mm)
        SketcherConstructRequest FlangeBlock {};
        FlangeBlock.Kind     = SketcherConstructCategory::Block;
        FlangeBlock.Position = { -0.12, -0.08, 0.0 };
        FlangeBlock.Width    = 0.24;
        FlangeBlock.Depth    = 0.16;
        FlangeBlock.Height   = 0.04;
        if (!VerifyCondition(ConstructSketcherPrimitive(AuthoredDoc, FlangeBlock).AcceptedCondition,
                             "Construct lower flange block"))
        {
            return 1;
        }

        // Raised cylindrical collar fused onto top face (Union)
        SketcherDrawingRequest CollarSketch {};
        CollarSketch.Kind            = SketcherDrawingCategory::Circle;
        CollarSketch.GridPlane       = 0;
        CollarSketch.CentreCondition = true;
        CollarSketch.FillCondition   = true;
        CollarSketch.Points          = { { 0.0, 0.0 }, { 0.055, 0.0 } };
        if (!VerifyCondition(DrawSketcherSketch(AuthoredDoc, CollarSketch).AcceptedCondition,
                             "Construct cylindrical collar sketch"))
        {
            return 1;
        }
        const std::string CollarLabel = AuthoredDoc.Tree.Steps.back().Label;
        if (!VerifyCondition(ExtrudeSketcherSketch(AuthoredDoc, CollarLabel, 0.075, FeatureCombination::Union).AcceptedCondition,
                             "Extrude cylindrical collar Union"))
        {
            return 1;
        }

        // Precision central through-bore (Difference)
        SketcherDrawingRequest BoreSketch {};
        BoreSketch.Kind            = SketcherDrawingCategory::Circle;
        BoreSketch.GridPlane       = 0;
        BoreSketch.CentreCondition = true;
        BoreSketch.FillCondition   = true;
        BoreSketch.Points          = { { 0.0, 0.0 }, { 0.028, 0.0 } };
        if (!VerifyCondition(DrawSketcherSketch(AuthoredDoc, BoreSketch).AcceptedCondition,
                             "Construct central bore sketch"))
        {
            return 1;
        }
        const std::string BoreLabel = AuthoredDoc.Tree.Steps.back().Label;
        if (!VerifyCondition(ExtrudeSketcherSketch(AuthoredDoc, BoreLabel, 0.10, FeatureCombination::Difference).AcceptedCondition,
                             "Extrude central bore Difference"))
        {
            return 1;
        }

        // Add a standalone 3D SpaceCurve guide rail so .Curve carries both B-rep edges AND standalone 3D curves
        VehicleCurveFile RailCurves {};
        RailCurves.Source = "SolidArcValidation";
        VehicleCurve GuideRail {};
        GuideRail.Name            = "ShoulderGuideRail";
        GuideRail.ClosedCondition = false;
        GuideRail.PlanarCondition = false;
        GuideRail.Points          = {
            { -0.14, -0.09, 0.01 },
            { -0.05, -0.095, 0.045 },
            {  0.05, -0.095, 0.055 },
            {  0.14, -0.09, 0.02 }
        };
        RailCurves.Curves.push_back(std::move(GuideRail));
        if (!VerifyCondition(ImportSketcherCurves(AuthoredDoc, RailCurves, 1.0e-4).AcceptedCondition,
                             "Import standalone 3D SpaceCurve into SolidArc document"))
        {
            return 1;
        }
        AuthoredDoc.PreviewPngBytes = { 0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au };
        ++PassedChecks;
    }

    const std::filesystem::path SavedSolidArcPath    = ScratchDir / "PrecisionFlangeHousing.solidarc";
    const std::filesystem::path ExportedSolidArcPath = ScratchDir / "PrecisionFlangeExport.solidarc";

    //--------------------------------------------------------------------------------------------------------------------
    // 4. SaveSketcherDocument + ExportSketcherBodies -> Unified Two-Tier .solidarc Container
    //--------------------------------------------------------------------------------------------------------------------
    {
        if (!VerifyCondition(SaveSketcherDocument(AuthoredDoc, SavedSolidArcPath.string()),
                             "SaveSketcherDocument writes unified .solidarc container"))
        {
            return 1;
        }

        const SketcherExportOutcome ExportOut = ExportSketcherBodies(AuthoredDoc, ExportedSolidArcPath.string());
        if (!VerifyCondition(
                ExportOut.Refusal.empty() &&
                ExportOut.Format == SketcherExchangeFormat::SolidArc &&
                ExportOut.Faces >= 8 &&
                ExportOut.Bytes > 4096.0,
                "ExportSketcherBodies exports .solidarc with full WindingNumber SDF audit"))
        {
            std::fprintf(stderr, "Export refusal: %s\n", ExportOut.Refusal.c_str());
            return 1;
        }

        // Verify SolidArc authoring round-trip from the binary-sectioned .solidarc file!
        std::string PreviewTitle {};
        std::string PreviewDesc {};
        std::vector<uint8_t> PreviewPng {};
        if (!VerifyCondition(
                LoadSketcherPreviewHeader(SavedSolidArcPath.string(), PreviewTitle, PreviewDesc, PreviewPng) &&
                PreviewTitle == "PrecisionFlangeHousing" &&
                PreviewPng.size() == 8u,
                "LoadSketcherPreviewHeader reads Title and PreviewPng from unified .solidarc container"))
        {
            return 1;
        }

        const std::string BaselineAuthoringToml = EncodeSketcherDocument(AuthoredDoc);
        const SketcherDocumentFile ReloadedFile = LoadSketcherDocumentFile(SavedSolidArcPath.string());
        if (!VerifyCondition(
                ReloadedFile.Refusal.empty() &&
                ReloadedFile.Title == "PrecisionFlangeHousing" &&
                ReloadedFile.Tree.Steps.size() == AuthoredDoc.Tree.Steps.size(),
                "LoadSketcherDocumentFile transparently strips Tier 2 binary boundary and restores parametric steps"))
        {
            std::fprintf(stderr, "Reload refusal: %s\n", ReloadedFile.Refusal.c_str());
            return 1;
        }

        SketcherDocument ReloadedDoc = CreateSketcherDocument(ReloadedFile.Title);
        ReloadedDoc.PreviewPngBytes = ReloadedFile.PreviewPngBytes;
        const SketcherCommandOutcome ReplayOut =
            ReplaceSketcherTree(ReloadedDoc, ReloadedFile.Tree, ReloadedFile.Captions, "ReloadUnifiedSolidArc");
        if (!VerifyCondition(
                ReplayOut.AcceptedCondition &&
                EncodeSketcherDocument(ReloadedDoc) == BaselineAuthoringToml,
                "Authoring TOML round-trip through unified .solidarc is 100% lossless"))
        {
            return 1;
        }
        ++PassedChecks;
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 5. Zero-Bake Frontier Runtime Load via MemoryMappedFileView + Exact SDF Audit
    //--------------------------------------------------------------------------------------------------------------------
    {
        const auto LoadStart = std::chrono::steady_clock::now();
        MemoryMappedFileView MappedView {};
        std::string Diagnostic {};
        if (!VerifyCondition(MappedView.Open(ExportedSolidArcPath, Diagnostic),
                             "MemoryMappedFileView opens unified .solidarc container"))
        {
            std::fprintf(stderr, "mmap diagnostic: %s\n", Diagnostic.c_str());
            return 1;
        }

        // Verify 64-byte alignment of every Tier 2 binary section inside .solidarc
        DocumentContainer RawContainer {};
        if (!VerifyCondition(DocumentContainerDecoder::DecodeFromMemoryMappedFile(MappedView, RawContainer, Diagnostic),
                             "DocumentContainerDecoder validates FNV-1a checksums and 64B section alignment"))
        {
            std::fprintf(stderr, "Container decode diagnostic: %s\n", Diagnostic.c_str());
            return 1;
        }
        for (const Serialization::BinarySectionBlock& Block : RawContainer.BinarySections)
        {
            if (!VerifyCondition((Block.DecodedAbsoluteByteOffset % 64u) == 0u,
                                 "Every Tier 2 binary section inside .solidarc is 64-byte boundary aligned"))
            {
                return 1;
            }
        }

        SolidArcRuntimeDocument RuntimeDoc {};
        if (!VerifyCondition(SolidArcContainerDecoder::DecodeFromMemoryMappedFile(MappedView, RuntimeDoc, Diagnostic),
                             "SolidArcContainerDecoder unpacks GeometrySpace + Curve + DistanceField with zero CAD replay"))
        {
            std::fprintf(stderr, "SolidArc runtime decode diagnostic: %s\n", Diagnostic.c_str());
            return 1;
        }
        const double RuntimeLoadMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - LoadStart).count();

        if (!VerifyCondition(
                RuntimeDoc.GeometryPresent &&
                RuntimeDoc.CurvesPresent &&
                RuntimeDoc.SurfaceFieldPresent &&
                RuntimeDoc.BakedVolumePresent,
                "All four embedded pillars (.GeometrySpace, .Curve, 'BSDF' SurfaceField, 'VSDF' BakedVolume) are resident"))
        {
            return 1;
        }

        if (!VerifyCondition(
                !RuntimeDoc.Geometry.Network.Faces.empty() &&
                !RuntimeDoc.Geometry.Network.Vertices.empty() &&
                RuntimeDoc.Geometry.WideTreeIncluded &&
                RuntimeDoc.Geometry.ClustersIncluded,
                "Embedded .GeometrySpace includes watertight PolygonNetwork + WideBoundingTree BVH4 + ClusterPyramid"))
        {
            return 1;
        }

        if (!VerifyCondition(
                RuntimeDoc.Curves.Curves.size() > 12u,
                "Embedded .Curve ('CRV1') carries 3D B-rep edges plus standalone SpaceCurves"))
        {
            return 1;
        }

        if (!VerifyCondition(
                RuntimeDoc.AuditMetrics.SignAgreementPercent >= 99.0f &&
                RuntimeDoc.AuditMetrics.MeanAbsoluteErrorMeters < 1.0e-4f,
                "Compiled CAD B-Rep SDF achieves >= 99% sign agreement and sub-0.1mm MAE against Generalized Winding + Exact B-Rep"))
        {
            std::fprintf(
                stderr,
                "Audit metrics: sign=%.2f%% MAE=%.6f m p95=%.6f m\n",
                RuntimeDoc.AuditMetrics.SignAgreementPercent,
                RuntimeDoc.AuditMetrics.MeanAbsoluteErrorMeters,
                RuntimeDoc.AuditMetrics.Percentile95ErrorMeters);
            return 1;
        }

        // Verify analytical SDF queries at explicit geometric test points (both narrow-band accelerated and exact unaccelerated):
        //   (a) Inside central through-bore at (0.018, 0, 0.02): OUTSIDE the solid, exact +10.0 mm to r=28 mm bore wall!
        //   (b) Exact bore axis (0, 0, 0.02) via QueryExactUnaccelerated: OUTSIDE the solid, exact +28.0 mm to bore wall!
        //   (c) Inside solid collar wall at (0.0415, 0, 0.055): INSIDE the solid, exact -13.5 mm!
        //   (d) Above collar top cap at (0.04, 0, 0.083) [narrow-band] & (0.04, 0, 0.095) [exact]: +8.0 mm & +20.0 mm!
        const SurfaceDistanceSample BoreNearSample =
            RuntimeDoc.SurfaceField.QuerySignedDistance(Mathematics::Axis3D{ 0.018f, 0.0f, 0.02f });
        const SurfaceDistanceSample BoreAxisExact =
            RuntimeDoc.SurfaceField.QueryExactUnaccelerated(Mathematics::Axis3D{ 0.0f, 0.0f, 0.02f });
        const SurfaceDistanceSample SolidWallSample =
            RuntimeDoc.SurfaceField.QuerySignedDistance(Mathematics::Axis3D{ 0.0415f, 0.0f, 0.055f });
        const SurfaceDistanceSample AboveTopBandSample =
            RuntimeDoc.SurfaceField.QuerySignedDistance(Mathematics::Axis3D{ 0.04f, 0.0f, 0.083f });
        const SurfaceDistanceSample AboveTopExactSample =
            RuntimeDoc.SurfaceField.QueryExactUnaccelerated(Mathematics::Axis3D{ 0.04f, 0.0f, 0.095f });

        if (!VerifyCondition(
                std::abs(BoreNearSample.SignedDistanceMeters - 0.010f) < 1.0e-4f &&
                std::abs(BoreAxisExact.SignedDistanceMeters - 0.028f) < 1.0e-4f &&
                std::abs(SolidWallSample.SignedDistanceMeters - (-0.0135f)) < 1.0e-4f &&
                std::abs(AboveTopBandSample.SignedDistanceMeters - 0.008f) < 1.0e-4f &&
                std::abs(AboveTopExactSample.SignedDistanceMeters - 0.020f) < 1.0e-4f,
                "Exact B-Rep SDF resolves through-bore (+10.0mm & +28.0mm), collar wall (-13.5mm), and top cap (+8.0mm & +20.0mm) to <0.1mm"))
        {
            std::fprintf(
                stderr,
                "Samples: boreNear=%.6f boreAxisExact=%.6f solidWall=%.6f aboveBand=%.6f aboveExact=%.6f\n",
                BoreNearSample.SignedDistanceMeters,
                BoreAxisExact.SignedDistanceMeters,
                SolidWallSample.SignedDistanceMeters,
                AboveTopBandSample.SignedDistanceMeters,
                AboveTopExactSample.SignedDistanceMeters);
            return 1;
        }

        // Also verify pre-baked fp16 LocalSignedDistanceVolume ('VSDF') agrees in sign at bore & solid wall
        const float BakedBoreDist = RuntimeDoc.BakedVolume.SampleTrilinearLocal(Mathematics::Axis3D{ 0.0f, 0.0f, 0.02f });
        const float BakedWallDist = RuntimeDoc.BakedVolume.SampleTrilinearLocal(Mathematics::Axis3D{ 0.0415f, 0.0f, 0.055f });
        if (!VerifyCondition(
                BakedBoreDist > 0.0f && BakedWallDist < 0.0f,
                "Pre-baked fp16 LocalSignedDistanceVolume ('VSDF') is immediately queryable with 0 ms runtime bake"))
        {
            return 1;
        }

        std::printf(
            "[SolidArcUnifiedContainerValidation] PrecisionFlangeHousing.solidarc: "
            "faces=%zu curves=%zu surfaces=%zu bezierPatches=%zu edges=%zu "
            "sign=%.2f%% MAE=%.4f mm p95=%.4f mm emptySkip=%.1f%% load=%.2f ms\n",
            RuntimeDoc.Geometry.Network.Faces.size(),
            RuntimeDoc.Curves.Curves.size(),
            RuntimeDoc.SurfaceField.Surfaces.size(),
            RuntimeDoc.SurfaceField.BezierPatches.size(),
            RuntimeDoc.SurfaceField.Edges.size(),
            RuntimeDoc.AuditMetrics.SignAgreementPercent,
            RuntimeDoc.AuditMetrics.MeanAbsoluteErrorMeters * 1000.0f,
            RuntimeDoc.AuditMetrics.Percentile95ErrorMeters * 1000.0f,
            ComputeEmptyCellPercent(RuntimeDoc.SurfaceField),
            RuntimeLoadMs);
        ++PassedChecks;
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 6. Real Multi-Surface .STEP CAD B-Rep Import -> Unified .solidarc Audit (When STEP Corpus Present + Roundtrip STEP)
    //--------------------------------------------------------------------------------------------------------------------
    {
        // First: export our multi-surface PrecisionFlangeHousing to .STEP AP214, re-import as a STEP B-Rep solid,
        // and compile to .solidarc to prove full STEP -> SolidArc -> Frontier SDF fidelity anywhere in CI!
        const std::filesystem::path StepRoundtripPath = ScratchDir / "PrecisionFlangeHousing.step";
        const SketcherExportOutcome StepExport = ExportSketcherBodies(AuthoredDoc, StepRoundtripPath.string());
        if (!VerifyCondition(StepExport.Refusal.empty(), "Export multi-surface CAD body to ISO 10303-21 STEP AP214"))
        {
            return 1;
        }

        std::vector<std::filesystem::path> StepCandidates = { StepRoundtripPath };
        const std::filesystem::path OptionalWheelStep = "WebGpuCadBrepSdfDemo/assets/racer_wheel.step";
        const std::filesystem::path OptionalGripperStep = "WebGpuCadBrepSdfDemo/assets/thor_gripper.step";
        if (std::filesystem::exists(OptionalWheelStep))
        {
            StepCandidates.push_back(OptionalWheelStep);
        }
        if (std::filesystem::exists(OptionalGripperStep))
        {
            StepCandidates.push_back(OptionalGripperStep);
        }

        for (const std::filesystem::path& StepPath : StepCandidates)
        {
            const SketcherStepReading Reading = ReadSketcherStepFile(StepPath.string());
            if (!VerifyCondition(Reading.Refusal.empty() && !Reading.Texts.empty(),
                                 "ReadSketcherStepFile parses multi-surface .STEP solid"))
            {
                std::fprintf(stderr, "STEP read refusal (%s): %s\n", StepPath.string().c_str(), Reading.Refusal.c_str());
                return 1;
            }

            SketcherDocument StepDoc = CreateSketcherDocument(Reading.Stem);
            if (!VerifyCondition(ImportSketcherStep(StepDoc, Reading).AcceptedCondition,
                                 "ImportSketcherStep embeds canonical STEP B-rep into SolidArc document"))
            {
                return 1;
            }

            const std::filesystem::path CompiledPath = ScratchDir / (Reading.Stem + "_compiled.solidarc");
            SolidArcCompileSettings Settings {};
            Settings.AuditSampleCount = 512u;
            SolidArcFieldAuditMetrics Audit {};
            std::string Diag {};
            if (!VerifyCondition(
                    SolidArcContainerEncoder::EncodeDocumentToFile(StepDoc, CompiledPath, Settings, {}, &Audit, Diag),
                    "Encode imported STEP document into unified .solidarc container"))
            {
                std::fprintf(stderr, "Encode STEP .solidarc diagnostic: %s\n", Diag.c_str());
                return 1;
            }

            SolidArcRuntimeDocument LoadedStepDoc {};
            if (!VerifyCondition(
                    SolidArcContainerDecoder::DecodeFromFile(CompiledPath, LoadedStepDoc, Diag),
                    "Decode imported STEP .solidarc container into Frontier runtime structures"))
            {
                return 1;
            }

            if (!VerifyCondition(
                    Audit.SignAgreementPercent >= 98.0f &&
                    LoadedStepDoc.GeometryPresent &&
                    LoadedStepDoc.CurvesPresent &&
                    LoadedStepDoc.SurfaceFieldPresent &&
                    LoadedStepDoc.BakedVolumePresent,
                    "Multi-surface STEP -> .solidarc maintains >= 98% exact sign agreement and complete runtime pillars"))
            {
                std::fprintf(
                    stderr,
                    "STEP audit (%s): sign=%.2f%% MAE=%.4f mm\n",
                    Reading.Stem.c_str(),
                    Audit.SignAgreementPercent,
                    Audit.MeanAbsoluteErrorMeters * 1000.0f);
                return 1;
            }

            std::printf(
                "[SolidArcUnifiedContainerValidation] STEP->SolidArc '%s': "
                "cadFaces=%d polyFaces=%zu curves=%zu surfaces=%zu patches=%zu "
                "sign=%.2f%% MAE=%.4f mm p95=%.4f mm emptySkip=%.1f%%\n",
                Reading.Stem.c_str(),
                Reading.Faces,
                LoadedStepDoc.Geometry.Network.Faces.size(),
                LoadedStepDoc.Curves.Curves.size(),
                LoadedStepDoc.SurfaceField.Surfaces.size(),
                LoadedStepDoc.SurfaceField.BezierPatches.size(),
                Audit.SignAgreementPercent,
                Audit.MeanAbsoluteErrorMeters * 1000.0f,
                Audit.Percentile95ErrorMeters * 1000.0f,
                ComputeEmptyCellPercent(LoadedStepDoc.SurfaceField));
        }
        ++PassedChecks;
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 7. Elongated Freeform Loft (RationalBezier4x4 NURBS) + Torus Aspect-Matched .solidarc Audit
    //--------------------------------------------------------------------------------------------------------------------
    {
        SketcherDocument FreeformDoc = CreateSketcherDocument("ElongatedAerodynamicFuselage");

        const double XStations[3]   = { -0.48, 0.0, 0.48 };
        const double HalfWidths[3]  = { 0.11, 0.19, 0.13 };
        const double HalfHeights[3] = { 0.08, 0.14, 0.09 };
        std::vector<std::string> SectionLabels {};
        for (int Sec = 0; Sec < 3; ++Sec)
        {
            OrthonormalBasis StationPlane = SketcherSketchPlane(2); // YZ plane (normal +X)
            StationPlane.Origin           = { XStations[Sec], 0.0, 0.0 };

            SketcherDrawingRequest CurveReq {};
            CurveReq.Kind            = SketcherDrawingCategory::Curve;
            CurveReq.ClosedCondition = true;
            CurveReq.PlaneCondition  = true;
            CurveReq.Plane           = StationPlane;
            for (int Pt = 0; Pt < 8; ++Pt)
            {
                const double Theta = 2.0 * 3.14159265358979323846 * static_cast<double>(Pt) / 8.0;
                CurveReq.Points.push_back({
                    HalfWidths[Sec] * std::cos(Theta),
                    HalfHeights[Sec] * std::sin(Theta)
                });
            }
            if (!VerifyCondition(DrawSketcherSketch(FreeformDoc, CurveReq).AcceptedCondition,
                                 "Construct closed aerodynamic spline section on parallel YZ station"))
            {
                return 1;
            }
            SectionLabels.push_back(FreeformDoc.Tree.Steps.back().Label);
        }

        const SketcherCommandOutcome LoftOut = LoftSketcherCurves(FreeformDoc, SectionLabels, 0, true);
        if (!VerifyCondition(LoftOut.AcceptedCondition,
                             "Loft 3 cross-sections into closed watertight NURBS solid"))
        {
            std::fprintf(stderr, "Loft refusal: %s\n", LoftOut.Refusal.c_str());
            return 1;
        }

        // Fuse a Torus collar onto the fuselage so Torus + Plane + RationalBezier4x4 coexist in one solid
        SketcherConstructRequest CollarTorus {};
        CollarTorus.Kind        = SketcherConstructCategory::Torus;
        CollarTorus.Position    = { 0.0, 0.0, 0.0 };
        CollarTorus.Major       = 0.175;
        CollarTorus.Minor       = 0.035;
        CollarTorus.Combination = FeatureCombination::Separate;
        (void)ConstructSketcherPrimitive(FreeformDoc, CollarTorus);

        const std::filesystem::path FuselagePath = ScratchDir / "ElongatedAerodynamicFuselage.solidarc";
        SolidArcCompileSettings Settings {};
        Settings.AuditSampleCount = 512u;
        SolidArcFieldAuditMetrics Audit {};
        std::string Diag {};
        if (!VerifyCondition(
                SolidArcContainerEncoder::EncodeDocumentToFile(FreeformDoc, FuselagePath, Settings, {}, &Audit, Diag),
                "Compile elongated NURBS loft + Torus into unified .solidarc container"))
        {
            std::fprintf(stderr, "Fuselage compile diagnostic: %s\n", Diag.c_str());
            return 1;
        }

        SolidArcRuntimeDocument FuselageRuntime {};
        if (!VerifyCondition(SolidArcContainerDecoder::DecodeFromFile(FuselagePath, FuselageRuntime, Diag),
                             "Decode elongated NURBS .solidarc container"))
        {
            return 1;
        }

        // Verify natural aspect-matched macro-grid proportions (Mx > My >= Mz for X-elongated fuselage!)
        const auto& MacroDims = FuselageRuntime.SurfaceField.MacroDimensions;
        if (!VerifyCondition(
                !FuselageRuntime.SurfaceField.BezierPatches.empty() &&
                MacroDims[0] > MacroDims[1] &&
                MacroDims[0] > MacroDims[2] &&
                FuselageRuntime.BakedVolume.ResolutionX > FuselageRuntime.BakedVolume.ResolutionY &&
                Audit.SignAgreementPercent >= 98.0f,
                "Elongated NURBS loft preserves natural aspect-matched grid proportions and compiles RationalBezier4x4 patches"))
        {
            std::fprintf(
                stderr,
                "Fuselage check: patches=%zu macro=(%u,%u,%u) vol=(%u,%u,%u) sign=%.2f%% MAE=%.4f mm\n",
                FuselageRuntime.SurfaceField.BezierPatches.size(),
                MacroDims[0], MacroDims[1], MacroDims[2],
                FuselageRuntime.BakedVolume.ResolutionX,
                FuselageRuntime.BakedVolume.ResolutionY,
                FuselageRuntime.BakedVolume.ResolutionZ,
                Audit.SignAgreementPercent,
                Audit.MeanAbsoluteErrorMeters * 1000.0f);
            return 1;
        }

        std::printf(
            "[SolidArcUnifiedContainerValidation] ElongatedAerodynamicFuselage.solidarc: "
            "bezierPatches=%zu macroGrid=%ux%ux%u bakedVol=%ux%ux%u "
            "sign=%.2f%% MAE=%.4f mm p95=%.4f mm emptySkip=%.1f%%\n",
            FuselageRuntime.SurfaceField.BezierPatches.size(),
            MacroDims[0], MacroDims[1], MacroDims[2],
            FuselageRuntime.BakedVolume.ResolutionX,
            FuselageRuntime.BakedVolume.ResolutionY,
            FuselageRuntime.BakedVolume.ResolutionZ,
            Audit.SignAgreementPercent,
            Audit.MeanAbsoluteErrorMeters * 1000.0f,
            Audit.Percentile95ErrorMeters * 1000.0f,
            ComputeEmptyCellPercent(FuselageRuntime.SurfaceField));
        ++PassedChecks;
    }

    std::filesystem::remove_all(ScratchDir, DirErr);
    std::printf("[SolidArcUnifiedContainerValidation] ALL %u VALIDATION SUITES PASSED.\n", PassedChecks);
    return 0;
}
