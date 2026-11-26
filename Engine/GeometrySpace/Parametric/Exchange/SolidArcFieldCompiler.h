/*============================================================================================================================================
                                                        SOLIDARCFIELDCOMPILER.H
============================================================================================================================================*/
// 🧩 Compiles SolidArc B-rep bodies into PolygonNetwork (.GeometrySpace), 3D curves (.Curve), exact B-rep SDF ('BSDF') & fp16 volume ('VSDF')

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/GeometryCodec.h"
#include "Engine/DistanceFieldSpace/CompiledSurfaceField.h"
#include "Engine/GeometrySpace/Parametric/Connectivity/ModelPartition.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct StandaloneCurveInput
{
    CurveGeometry Curve       = StraightLine{}; // [-]     - 3D sketch or reference curve carrier
    double        IntervalLow = 0.0;            // [m/rad] - Start parameter
    double        IntervalHigh = 1.0;           // [m/rad] - End parameter
};

struct SolidArcCompileSettings
{
    double   UnitScaleToMeters      = 1.0;    // [m/len] - Partition length -> SI meters (SolidArc internal unit = 1.0 m)
    double   TessellationSagRatio   = 1.0e-3; // [-]     - Chordal tolerance ratio for PolygonNetwork triangulation
    uint32_t TargetMacroGridBudget  = 8000u;  // [cells] - Target narrow-band macro-cell count (e.g. 20x20x20 or 28x16x16)
    uint32_t TargetVoxelBudget      = 65536u; // [vox]   - Target fp16 LocalSignedDistanceVolume voxel budget (e.g. 40^3 or 56x36x32)
    uint32_t AuditSampleCount       = 512u;   // [cnt]   - Validation samples against exact Generalized Winding Number
    bool     BuildSpatialStructures = true;   // [-]     - Build WideBoundingTree (BVH4) and ClusterPyramid on PolygonNetwork
    bool     BakeVoxelVolume        = true;   // [-]     - Bake ready-to-use fp16 LocalSignedDistanceVolume ('VSDF')
};

struct SolidArcFieldAuditMetrics
{
    uint32_t SampledPoints           = 0u;    // [cnt] - Total 3D validation queries evaluated
    float    MeanAbsoluteErrorMeters = 0.0f;  // [m]   - Mean |d_compiled - d_exact|
    float    Percentile95ErrorMeters = 0.0f;  // [m]   - 95th percentile |d_compiled - d_exact|
    float    Percentile99ErrorMeters = 0.0f;  // [m]   - 99th percentile |d_compiled - d_exact|
    float    SignAgreementPercent    = 100.0f;// [%]   - Percentage matching Generalized Winding Number sign
    double   CompileMilliseconds     = 0.0;   // [ms]  - Total wall-clock compilation & bake duration
};

struct SolidArcCompiledBundle
{
    Codecs::GeometrySpaceDocument              GeometryDocument {}; // [-] - PolygonNetwork + UVs + normals + BVH4 + ClusterPyramid
    DistanceFieldSpace::ParametricCurvePackage CurvePackage     {}; // [-] - Embedded .Curve ('CRV1') 3D analytic & Bezier curves
    DistanceFieldSpace::CompiledSurfaceField   SurfaceField     {}; // [-] - Embedded .DistanceField ('BSDF') exact GPU/CPU B-rep SDF
    DistanceFieldSpace::LocalSignedDistanceVolume BakedVolume   {}; // [-] - Embedded .DistanceField ('VSDF') ready fp16 volume
    SolidArcFieldAuditMetrics                  Audit            {}; // [-] - Accuracy & timing telemetry
    std::string                                Refusal          {}; // [-] - Empty on success; diagnostic reason otherwise
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] SolidArcCompiledBundle CompileSolidArcPartition(
    const ModelPartition&                   Partition,
    std::span<const BodyToken>              Bodies,
    std::span<const std::string>            BodyTitles          = {},
    std::span<const StandaloneCurveInput>   StandaloneCurves    = {},
    const SolidArcCompileSettings&          Settings            = {});

} // namespace Frontier::GeometrySpace::Parametric
