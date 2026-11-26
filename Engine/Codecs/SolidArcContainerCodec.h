/*============================================================================================================================================
                                                        SOLIDARCCONTAINERCODEC.H
============================================================================================================================================*/
// 🧩 Unified .solidarc container encoder & decoder bundling Authoring TOML + .GeometrySpace + .Curve + .DistanceField ('BSDF' & 'VSDF')

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/GeometryCodec.h"
#include "Engine/DistanceFieldSpace/CompiledSurfaceField.h"
#include "Engine/GeometrySpace/Parametric/Exchange/SolidArcFieldCompiler.h"
#include "Engine/Serialization/Container/MemoryMappedFileView.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocument.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr std::string_view SolidArcSectionGeometrySpace    = "EmbeddedGeometrySpace";
inline constexpr std::string_view SolidArcSectionParametricCurves = "EmbeddedParametricCurves";
inline constexpr std::string_view SolidArcSectionSurfaceField     = "CompiledSurfaceDistanceField";
inline constexpr std::string_view SolidArcSectionBakedVolume      = "BakedVoxelDistanceVolume";

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SolidArcRuntimeDocument
{
    std::string                                        DocumentTitle          {};    // [-] - Human-readable document title
    std::string                                        StableIdentifier       {};    // [-] - Persistent UUID string
    GeometrySpaceDocument                              Geometry               {};    // [-] - Embedded .GeometrySpace (triangles/quads/n-gons + UVs + BVH4)
    DistanceFieldSpace::ParametricCurvePackage         Curves                 {};    // [-] - Embedded .Curve ('CRV1') 3D analytic & Bezier curves
    DistanceFieldSpace::CompiledSurfaceField           SurfaceField           {};    // [-] - Embedded .DistanceField ('BSDF') exact CAD B-rep SDF
    DistanceFieldSpace::LocalSignedDistanceVolume      BakedVolume            {};    // [-] - Embedded .DistanceField ('VSDF') ready fp16 volume
    GeometrySpace::Parametric::SolidArcFieldAuditMetrics AuditMetrics         {};    // [-] - Verified accuracy & compile metrics
    bool                                               GeometryPresent        = false; // [-] - True when EmbeddedGeometrySpace was decoded
    bool                                               CurvesPresent          = false; // [-] - True when EmbeddedParametricCurves was decoded
    bool                                               SurfaceFieldPresent    = false; // [-] - True when CompiledSurfaceDistanceField was decoded
    bool                                               BakedVolumePresent     = false; // [-] - True when BakedVoxelDistanceVolume was decoded
};

struct SolidArcContainerDecodeOptions
{
    bool ReplayLegacyTomlWhenUnbaked = true; // [-] - Replay parametric steps if opening an older text-only .solidarc without Tier 2 sections
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  ENCODER & DECODER
//------------------------------------------------------------------------------------------------------------------------

class SolidArcContainerEncoder
{
public:
    [[nodiscard]] static bool EncodeDocumentToBytes(
        const Workspaces::Sketcher::SketcherDocument&              Document,
        const GeometrySpace::Parametric::SolidArcCompileSettings&  CompileSettings,
        std::span<const std::string>                               OptionalBodyKeys,
        std::vector<std::byte>&                                    OutContainerBytes,
        GeometrySpace::Parametric::SolidArcFieldAuditMetrics*      OutAuditMetrics,
        std::string&                                               OutDiagnostic);

    [[nodiscard]] static bool EncodeDocumentToFile(
        const Workspaces::Sketcher::SketcherDocument&              Document,
        const std::filesystem::path&                               TargetPath,
        const GeometrySpace::Parametric::SolidArcCompileSettings&  CompileSettings,
        std::span<const std::string>                               OptionalBodyKeys,
        GeometrySpace::Parametric::SolidArcFieldAuditMetrics*      OutAuditMetrics,
        std::string&                                               OutDiagnostic);
};

class SolidArcContainerDecoder
{
public:
    [[nodiscard]] static bool DecodeFromBytes(
        std::span<const std::byte>            InputBytes,
        SolidArcRuntimeDocument&              OutRuntimeDocument,
        std::string&                          OutDiagnostic,
        const SolidArcContainerDecodeOptions& Options = {});

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path&          SourcePath,
        SolidArcRuntimeDocument&              OutRuntimeDocument,
        std::string&                          OutDiagnostic,
        const SolidArcContainerDecodeOptions& Options = {});

    [[nodiscard]] static bool DecodeFromMemoryMappedFile(
        const Serialization::MemoryMappedFileView& MappedView,
        SolidArcRuntimeDocument&                   OutRuntimeDocument,
        std::string&                               OutDiagnostic);
};

} // namespace Frontier::Codecs
