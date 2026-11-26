/*============================================================================================================================================
                                                       SOLIDARCCONTAINERCODEC.CPP
============================================================================================================================================*/
// 🧩 Implementation of unified .solidarc container encoder & decoder (Authoring TOML + .GeometrySpace + .Curve + .DistanceField)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/SolidArcContainerCodec.h"

#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/Serialization/Container/DocumentContainerCodec.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocumentCodec.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL UTILITIES
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr std::string_view ContainerBoundaryMarker = "\n# === FRONTIER_BINARY_BOUNDARY ===\n";

void CollectDocumentGeometryInputs(
    const Workspaces::Sketcher::SketcherDocument&                   Document,
    std::span<const std::string>                                    OptionalBodyKeys,
    std::vector<GeometrySpace::Parametric::BodyToken>&              OutBodyTokens,
    std::vector<std::string>&                                       OutBodyTitles,
    std::vector<GeometrySpace::Parametric::StandaloneCurveInput>&   OutStandaloneCurves)
{
    OutBodyTokens.clear();
    OutBodyTitles.clear();
    OutStandaloneCurves.clear();

    if (Document.Partition)
    {
        if (!OptionalBodyKeys.empty())
        {
            for (const std::string& Key : OptionalBodyKeys)
            {
                const auto Found = std::find_if(
                    Document.Regeneration.Bodies.begin(),
                    Document.Regeneration.Bodies.end(),
                    [&](const GeometrySpace::Parametric::RegeneratedBody& Entry) { return Entry.Key == Key; });
                if (Found != Document.Regeneration.Bodies.end() && Found->Body.PresentCondition())
                {
                    OutBodyTokens.push_back(Found->Body);
                    OutBodyTitles.push_back(Found->Key);
                }
            }
        }
        else
        {
            for (const GeometrySpace::Parametric::RegeneratedBody& Entry : Document.Regeneration.Bodies)
            {
                if (!Entry.Body.PresentCondition())
                {
                    continue;
                }
                if (Document.Appearance.HiddenBodies.contains(Entry.Key) ||
                    Document.Appearance.LiveCutters.contains(Entry.Key))
                {
                    continue;
                }
                OutBodyTokens.push_back(Entry.Body);
                OutBodyTitles.push_back(Entry.Key);
            }
        }
    }

    // Include fitted 3D SpaceCurves from regeneration so standalone curves travel inside .solidarc too
    for (const auto& [Label, Fit] : Document.Regeneration.Curves)
    {
        if (!Fit.Curve.Definition || Fit.Curve.Definition->Poles.empty())
        {
            continue;
        }
        const GeometrySpace::Parametric::NurbsDomain Span =
            GeometrySpace::Parametric::MeasureNurbsCurveDomain(*Fit.Curve.Definition);
        GeometrySpace::Parametric::StandaloneCurveInput Input {};
        Input.Curve        = Fit.Curve;
        Input.IntervalLow  = Span.Low;
        Input.IntervalHigh = Span.High;
        OutStandaloneCurves.push_back(std::move(Input));
    }
}

[[nodiscard]] bool UnpackSectionsFromDocumentContainer(
    const Serialization::DocumentContainer& Container,
    SolidArcRuntimeDocument&                OutRuntime,
    std::string&                            OutDiagnostic)
{
    OutRuntime = SolidArcRuntimeDocument{};
    OutRuntime.DocumentTitle    = Container.DocumentTitle;
    OutRuntime.StableIdentifier = Container.StableIdentifier;

    if (const toml::table* Summary = Container.TomlHeader["SolidArcCompiledSummary"].as_table())
    {
        OutRuntime.AuditMetrics.SampledPoints =
            static_cast<uint32_t>(std::max<int64_t>(0, (*Summary)["AuditSampledPoints"].value_or(int64_t{ 0 })));
        OutRuntime.AuditMetrics.MeanAbsoluteErrorMeters =
            static_cast<float>((*Summary)["MeanAbsoluteErrorMeters"].value_or(0.0));
        OutRuntime.AuditMetrics.Percentile95ErrorMeters =
            static_cast<float>((*Summary)["Percentile95ErrorMeters"].value_or(0.0));
        OutRuntime.AuditMetrics.Percentile99ErrorMeters =
            static_cast<float>((*Summary)["Percentile99ErrorMeters"].value_or(0.0));
        OutRuntime.AuditMetrics.SignAgreementPercent =
            static_cast<float>((*Summary)["SignAgreementPercent"].value_or(100.0));
        OutRuntime.AuditMetrics.CompileMilliseconds =
            (*Summary)["CompileMilliseconds"].value_or(0.0);
    }

    if (const Serialization::BinarySectionBlock* GeoBlock =
            Serialization::DocumentContainerDecoder::FindBinarySection(Container, SolidArcSectionGeometrySpace))
    {
        if (!GeoBlock->PayloadBytes.empty())
        {
            if (!GeometryDecoder::DecodeFromBytes(GeoBlock->PayloadBytes, OutRuntime.Geometry, OutDiagnostic))
            {
                return false;
            }
            OutRuntime.GeometryPresent = true;
        }
    }

    if (const Serialization::BinarySectionBlock* CrvBlock =
            Serialization::DocumentContainerDecoder::FindBinarySection(Container, SolidArcSectionParametricCurves))
    {
        if (!CrvBlock->PayloadBytes.empty())
        {
            if (!DistanceFieldSpace::DecodeParametricCurvePackageBinary(CrvBlock->PayloadBytes, OutRuntime.Curves, OutDiagnostic))
            {
                return false;
            }
            OutRuntime.CurvesPresent = true;
        }
    }

    if (const Serialization::BinarySectionBlock* SdfBlock =
            Serialization::DocumentContainerDecoder::FindBinarySection(Container, SolidArcSectionSurfaceField))
    {
        if (!SdfBlock->PayloadBytes.empty())
        {
            if (!DistanceFieldSpace::DecodeCompiledSurfaceFieldBinary(SdfBlock->PayloadBytes, OutRuntime.SurfaceField, OutDiagnostic))
            {
                return false;
            }
            OutRuntime.SurfaceFieldPresent = true;
        }
    }

    if (const Serialization::BinarySectionBlock* VolBlock =
            Serialization::DocumentContainerDecoder::FindBinarySection(Container, SolidArcSectionBakedVolume))
    {
        if (!VolBlock->PayloadBytes.empty())
        {
            if (!DistanceFieldSpace::DecodeLocalDistanceVolumeBinary(VolBlock->PayloadBytes, OutRuntime.BakedVolume, OutDiagnostic))
            {
                return false;
            }
            OutRuntime.BakedVolumePresent = true;
        }
    }

    OutDiagnostic.clear();
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  SOLIDARC ENCODER
//------------------------------------------------------------------------------------------------------------------------

bool SolidArcContainerEncoder::EncodeDocumentToBytes(
    const Workspaces::Sketcher::SketcherDocument&              Document,
    const GeometrySpace::Parametric::SolidArcCompileSettings&  CompileSettings,
    std::span<const std::string>                               OptionalBodyKeys,
    std::vector<std::byte>&                                    OutContainerBytes,
    GeometrySpace::Parametric::SolidArcFieldAuditMetrics*      OutAuditMetrics,
    std::string&                                               OutDiagnostic)
{
    OutContainerBytes.clear();
    OutDiagnostic.clear();

    // ① Encode complete SolidArc parametric authoring TOML into Tier 1 table
    const std::string AuthoringTomlText = Workspaces::Sketcher::EncodeSketcherDocument(Document);
    toml::table ParsedAuthoringToml {};
    try
    {
        ParsedAuthoringToml = toml::parse(AuthoringTomlText);
    }
    catch (const toml::parse_error& Err)
    {
        OutDiagnostic = std::string("Failed to parse generated SolidArc authoring TOML: ") + std::string(Err.description());
        return false;
    }

    // ② Compile active B-rep bodies + SpaceCurves into PolygonNetwork + Curves + Exact B-Rep SDF + Baked fp16 Volume
    std::vector<GeometrySpace::Parametric::BodyToken>            BodyTokens;
    std::vector<std::string>                                     BodyTitles;
    std::vector<GeometrySpace::Parametric::StandaloneCurveInput> StandaloneCurves;
    CollectDocumentGeometryInputs(Document, OptionalBodyKeys, BodyTokens, BodyTitles, StandaloneCurves);

    GeometrySpace::Parametric::SolidArcCompiledBundle Compiled {};
    if (Document.Partition && (!BodyTokens.empty() || !StandaloneCurves.empty()))
    {
        Compiled = GeometrySpace::Parametric::CompileSolidArcPartition(
            *Document.Partition,
            BodyTokens,
            BodyTitles,
            StandaloneCurves,
            CompileSettings);
        if (!Compiled.Refusal.empty())
        {
            OutDiagnostic = Compiled.Refusal;
            return false;
        }
    }
    else if (!StandaloneCurves.empty())
    {
        GeometrySpace::Parametric::ModelPartition EmptyPartition {};
        Compiled = GeometrySpace::Parametric::CompileSolidArcPartition(
            EmptyPartition,
            {},
            {},
            StandaloneCurves,
            CompileSettings);
    }

    if (OutAuditMetrics != nullptr)
    {
        *OutAuditMetrics = Compiled.Audit;
    }

    // ③ Assemble Sectioned DocumentContainer (.Sketch / .solidarc)
    Serialization::DocumentContainer Container {};
    Container.SuffixCategory     = Serialization::FormatSuffixCategory::Sketch;
    Container.SchemaVersion      = 1u;
    Container.StableIdentifier   = "0193b5c0-7a1e-7000-8000-5011da4c0001";
    Container.DocumentTitle      = Document.Title.empty() ? std::string("Untitled") : Document.Title;
    Container.AuthoringWorkspace = "Frontier.SolidArc";
    Container.TomlHeader         = std::move(ParsedAuthoringToml);

    toml::table SummarySection {};
    SummarySection.insert("BodyCount",               static_cast<int64_t>(BodyTokens.size()));
    SummarySection.insert("PolygonFaceCount",        static_cast<int64_t>(Compiled.GeometryDocument.Network.Faces.size()));
    SummarySection.insert("ParametricCurveCount",    static_cast<int64_t>(Compiled.CurvePackage.Curves.size()));
    SummarySection.insert("SurfacePrimitiveCount",   static_cast<int64_t>(Compiled.SurfaceField.Surfaces.size()));
    SummarySection.insert("BezierPatchCount",        static_cast<int64_t>(Compiled.SurfaceField.BezierPatches.size()));
    SummarySection.insert("PseudoNormalEdgeCount",   static_cast<int64_t>(Compiled.SurfaceField.Edges.size()));
    SummarySection.insert("AuditSampledPoints",      static_cast<int64_t>(Compiled.Audit.SampledPoints));
    SummarySection.insert("MeanAbsoluteErrorMeters", static_cast<double>(Compiled.Audit.MeanAbsoluteErrorMeters));
    SummarySection.insert("Percentile95ErrorMeters", static_cast<double>(Compiled.Audit.Percentile95ErrorMeters));
    SummarySection.insert("Percentile99ErrorMeters", static_cast<double>(Compiled.Audit.Percentile99ErrorMeters));
    SummarySection.insert("SignAgreementPercent",    static_cast<double>(Compiled.Audit.SignAgreementPercent));
    SummarySection.insert("CompileMilliseconds",     Compiled.Audit.CompileMilliseconds);
    Container.TomlHeader.insert_or_assign("SolidArcCompiledSummary", std::move(SummarySection));

    // Section 1: Embedded .GeometrySpace (triangles/quads/n-gons + UVs + creased normals + BVH4 + ClusterPyramid)
    if (!Compiled.GeometryDocument.Network.Faces.empty())
    {
        std::vector<std::byte> GeoBytes;
        if (!GeometryEncoder::EncodeToBytes(Compiled.GeometryDocument, GeoBytes, OutDiagnostic))
        {
            return false;
        }
        Container.EmbeddedPillars.push_back({
            Serialization::FormatSuffixCategory::GeometrySpace,
            std::string(SolidArcSectionGeometrySpace),
            Serialization::EmbeddedResidencyMode::Tier2Binary,
            "PolygonNetwork + analytical CAD normals + chart UVs + BVH4 + ClusterPyramid"
        });
        Container.BinarySections.push_back({
            std::string(SolidArcSectionGeometrySpace),
            ".GeometrySpace",
            std::move(GeoBytes),
            0u,
            0u
        });
    }

    // Section 2: Embedded .Curve ('CRV1' 3D analytic lines, circles, ellipses & rational Bezier curves)
    if (!Compiled.CurvePackage.Curves.empty())
    {
        std::vector<std::byte> CurveBytes;
        if (!DistanceFieldSpace::EncodeParametricCurvePackageBinary(Compiled.CurvePackage, CurveBytes, OutDiagnostic))
        {
            return false;
        }
        Container.EmbeddedPillars.push_back({
            Serialization::FormatSuffixCategory::Curve,
            std::string(SolidArcSectionParametricCurves),
            Serialization::EmbeddedResidencyMode::Tier2Binary,
            "3D CAD B-rep & SpaceCurve analytic and rational Bezier curves"
        });
        Container.BinarySections.push_back({
            std::string(SolidArcSectionParametricCurves),
            ".Curve",
            std::move(CurveBytes),
            0u,
            0u
        });
    }

    // Section 3: Embedded .DistanceField ('BSDF' exact GPU-ready std430 CAD B-Rep SDF)
    if (!Compiled.SurfaceField.EmptyCondition())
    {
        std::vector<std::byte> SdfBytes;
        if (!DistanceFieldSpace::EncodeCompiledSurfaceFieldBinary(Compiled.SurfaceField, SdfBytes, OutDiagnostic))
        {
            return false;
        }
        Container.EmbeddedPillars.push_back({
            Serialization::FormatSuffixCategory::DistanceField,
            std::string(SolidArcSectionSurfaceField),
            Serialization::EmbeddedResidencyMode::Tier2Binary,
            "Exact compiled CAD B-rep SDF (Plane/Cylinder/Cone/Sphere/Torus + RationalBezier4x4 + UV Trims + PseudoNormals)"
        });
        Container.BinarySections.push_back({
            std::string(SolidArcSectionSurfaceField),
            ".DistanceField",
            std::move(SdfBytes),
            0u,
            0u
        });
    }

    // Section 4: Embedded .DistanceField ('VSDF' pre-baked fp16 LocalSignedDistanceVolume ready for zero-bake runtime)
    if (!Compiled.BakedVolume.HalfDistanceSamples.empty())
    {
        std::vector<std::byte> VolBytes;
        if (!DistanceFieldSpace::EncodeLocalDistanceVolumeBinary(Compiled.BakedVolume, VolBytes, OutDiagnostic))
        {
            return false;
        }
        Container.EmbeddedPillars.push_back({
            Serialization::FormatSuffixCategory::DistanceField,
            std::string(SolidArcSectionBakedVolume),
            Serialization::EmbeddedResidencyMode::Tier2Binary,
            "Pre-baked fp16 LocalSignedDistanceVolume evaluated from exact CAD B-rep SDF"
        });
        Container.BinarySections.push_back({
            std::string(SolidArcSectionBakedVolume),
            ".DistanceField",
            std::move(VolBytes),
            0u,
            0u
        });
    }

    return Serialization::DocumentContainerEncoder::EncodeToBytes(Container, OutContainerBytes, OutDiagnostic);
}

bool SolidArcContainerEncoder::EncodeDocumentToFile(
    const Workspaces::Sketcher::SketcherDocument&              Document,
    const std::filesystem::path&                               TargetPath,
    const GeometrySpace::Parametric::SolidArcCompileSettings&  CompileSettings,
    std::span<const std::string>                               OptionalBodyKeys,
    GeometrySpace::Parametric::SolidArcFieldAuditMetrics*      OutAuditMetrics,
    std::string&                                               OutDiagnostic)
{
    std::vector<std::byte> ContainerBytes;
    if (!EncodeDocumentToBytes(Document, CompileSettings, OptionalBodyKeys, ContainerBytes, OutAuditMetrics, OutDiagnostic))
    {
        return false;
    }

    std::error_code DirErr {};
    if (TargetPath.has_parent_path())
    {
        std::filesystem::create_directories(TargetPath.parent_path(), DirErr);
    }
    std::ofstream Output(TargetPath, std::ios::binary | std::ios::trunc);
    if (!Output)
    {
        OutDiagnostic = "Cannot open output path for writing: " + TargetPath.string();
        return false;
    }
    Output.write(reinterpret_cast<const char*>(ContainerBytes.data()), static_cast<std::streamsize>(ContainerBytes.size()));
    if (!Output)
    {
        OutDiagnostic = "Failed while flushing .solidarc container bytes to: " + TargetPath.string();
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SOLIDARC DECODER
//------------------------------------------------------------------------------------------------------------------------

bool SolidArcContainerDecoder::DecodeFromBytes(
    std::span<const std::byte>            InputBytes,
    SolidArcRuntimeDocument&              OutRuntimeDocument,
    std::string&                          OutDiagnostic,
    const SolidArcContainerDecodeOptions& Options)
{
    OutRuntimeDocument = SolidArcRuntimeDocument{};
    OutDiagnostic.clear();

    const std::string_view RawView(reinterpret_cast<const char*>(InputBytes.data()), InputBytes.size());
    if (RawView.find(ContainerBoundaryMarker) != std::string_view::npos)
    {
        Serialization::DocumentContainer Container {};
        if (!Serialization::DocumentContainerDecoder::DecodeFromBytes(InputBytes, Container, OutDiagnostic))
        {
            return false;
        }
        return UnpackSectionsFromDocumentContainer(Container, OutRuntimeDocument, OutDiagnostic);
    }

    // Legacy text-only .solidarc without Tier 2 binary boundary
    const Workspaces::Sketcher::SketcherDocumentFile Parsed =
        Workspaces::Sketcher::DecodeSketcherDocument(std::string(RawView));
    if (!Parsed.Refusal.empty())
    {
        OutDiagnostic = "Legacy .solidarc parse error: " + Parsed.Refusal;
        return false;
    }

    OutRuntimeDocument.DocumentTitle = Parsed.Title;
    if (!Options.ReplayLegacyTomlWhenUnbaked || Parsed.Tree.Steps.empty())
    {
        return true;
    }

    Workspaces::Sketcher::SketcherDocument Replayed = Workspaces::Sketcher::CreateSketcherDocument(Parsed.Title);
    (void)Workspaces::Sketcher::ReplaceSketcherTree(Replayed, Parsed.Tree, Parsed.Captions, "LegacyLoad");

    std::vector<GeometrySpace::Parametric::BodyToken>            BodyTokens;
    std::vector<std::string>                                     BodyTitles;
    std::vector<GeometrySpace::Parametric::StandaloneCurveInput> StandaloneCurves;
    CollectDocumentGeometryInputs(Replayed, {}, BodyTokens, BodyTitles, StandaloneCurves);

    if (Replayed.Partition && (!BodyTokens.empty() || !StandaloneCurves.empty()))
    {
        GeometrySpace::Parametric::SolidArcCompileSettings DefaultSettings {};
        DefaultSettings.AuditSampleCount = 0u;
        GeometrySpace::Parametric::SolidArcCompiledBundle Bundle =
            GeometrySpace::Parametric::CompileSolidArcPartition(
                *Replayed.Partition,
                BodyTokens,
                BodyTitles,
                StandaloneCurves,
                DefaultSettings);
        if (!Bundle.Refusal.empty())
        {
            OutDiagnostic = Bundle.Refusal;
            return false;
        }
        OutRuntimeDocument.Geometry            = std::move(Bundle.GeometryDocument);
        OutRuntimeDocument.Curves              = std::move(Bundle.CurvePackage);
        OutRuntimeDocument.SurfaceField        = std::move(Bundle.SurfaceField);
        OutRuntimeDocument.BakedVolume         = std::move(Bundle.BakedVolume);
        OutRuntimeDocument.GeometryPresent     = !OutRuntimeDocument.Geometry.Network.Faces.empty();
        OutRuntimeDocument.CurvesPresent       = !OutRuntimeDocument.Curves.Curves.empty();
        OutRuntimeDocument.SurfaceFieldPresent = !OutRuntimeDocument.SurfaceField.EmptyCondition();
        OutRuntimeDocument.BakedVolumePresent  = !OutRuntimeDocument.BakedVolume.HalfDistanceSamples.empty();
    }
    return true;
}

bool SolidArcContainerDecoder::DecodeFromFile(
    const std::filesystem::path&          SourcePath,
    SolidArcRuntimeDocument&              OutRuntimeDocument,
    std::string&                          OutDiagnostic,
    const SolidArcContainerDecodeOptions& Options)
{
    std::ifstream Stream(SourcePath, std::ios::binary | std::ios::ate);
    if (!Stream)
    {
        OutDiagnostic = "Cannot open .solidarc file: " + SourcePath.string();
        return false;
    }
    const std::streamsize FileSize = Stream.tellg();
    if (FileSize < 0)
    {
        OutDiagnostic = "Cannot query byte size of .solidarc file: " + SourcePath.string();
        return false;
    }
    Stream.seekg(0, std::ios::beg);
    std::vector<std::byte> Bytes(static_cast<std::size_t>(FileSize));
    if (FileSize > 0 && !Stream.read(reinterpret_cast<char*>(Bytes.data()), FileSize))
    {
        OutDiagnostic = "Failed reading .solidarc file: " + SourcePath.string();
        return false;
    }
    return DecodeFromBytes(Bytes, OutRuntimeDocument, OutDiagnostic, Options);
}

bool SolidArcContainerDecoder::DecodeFromMemoryMappedFile(
    const Serialization::MemoryMappedFileView& MappedView,
    SolidArcRuntimeDocument&                   OutRuntimeDocument,
    std::string&                               OutDiagnostic)
{
    Serialization::DocumentContainer Container {};
    if (!Serialization::DocumentContainerDecoder::DecodeFromMemoryMappedFile(MappedView, Container, OutDiagnostic))
    {
        return false;
    }
    return UnpackSectionsFromDocumentContainer(Container, OutRuntimeDocument, OutDiagnostic);
}

} // namespace Frontier::Codecs
