/*============================================================================================================================================
                                                       SOLIDARCFIELDCOMPILER.CPP
============================================================================================================================================*/
// 🧩 Implementation of SolidArc B-rep -> PolygonNetwork (.GeometrySpace), Curves (.Curve), Exact SDF ('BSDF') & Baked Volume ('VSDF')

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Exchange/SolidArcFieldCompiler.h"

#include "Engine/GeometrySpace/Parametric/Containment/FaceDomain.h"
#include "Engine/GeometrySpace/Parametric/Containment/WindingNumber.h"
#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Tessellation/BodyTessellation.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <numbers>
#include <unordered_map>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL UTILITIES
//------------------------------------------------------------------------------------------------------------------------

namespace
{

using DistanceFieldSpace::CompiledBezierPatch4x4;
using DistanceFieldSpace::CompiledConicArc3D;
using DistanceFieldSpace::CompiledMacroCell;
using DistanceFieldSpace::CompiledPseudoNormalEdge;
using DistanceFieldSpace::CompiledSurfaceField;
using DistanceFieldSpace::CompiledSurfacePrimitive;
using DistanceFieldSpace::CompiledTrimSegment2D;
using DistanceFieldSpace::EdgeProjectionCategory;
using DistanceFieldSpace::PackFloat32ToBinary16;
using DistanceFieldSpace::ParametricCurveCategory;
using DistanceFieldSpace::ParametricCurveEntry;
using DistanceFieldSpace::ParametricCurvePackage;
using DistanceFieldSpace::SurfaceFieldQuerySettings;
using DistanceFieldSpace::SurfacePrimitiveCategory;

[[nodiscard]] inline Mathematics::Axis3D ToMetersAxis3D(Vector3 Source, double Scale) noexcept
{
    return {
        static_cast<float>(Source.X * Scale),
        static_cast<float>(Source.Y * Scale),
        static_cast<float>(Source.Z * Scale)
    };
}

[[nodiscard]] inline Mathematics::Axis3D ToUnitAxis3D(Vector3 Source) noexcept
{
    const double Len = Length(Source);
    if (Len <= 1.0e-15)
    {
        return { 0.0f, 1.0f, 0.0f };
    }
    return {
        static_cast<float>(Source.X / Len),
        static_cast<float>(Source.Y / Len),
        static_cast<float>(Source.Z / Len)
    };
}

[[nodiscard]] inline Vector3 SafeNormalizeDouble(Vector3 InputVec) noexcept
{
    const double Len = Length(InputVec);
    return (Len > 1.0e-15) ? (InputVec * (1.0 / Len)) : Vector3{ 0.0, 1.0, 0.0 };
}

[[nodiscard]] inline float BoxToBoxDistanceSquared(
    Mathematics::Axis3D LowA,
    Mathematics::Axis3D HighA,
    Mathematics::Axis3D LowB,
    Mathematics::Axis3D HighB) noexcept
{
    const float Dx = std::max({ LowA.X - HighB.X, LowB.X - HighA.X, 0.0f });
    const float Dy = std::max({ LowA.Y - HighB.Y, LowB.Y - HighA.Y, 0.0f });
    const float Dz = std::max({ LowA.Z - HighB.Z, LowB.Z - HighA.Z, 0.0f });
    return Dx * Dx + Dy * Dy + Dz * Dz;
}

// Computes aspect-matched grid dimensions (Nx, Ny, Nz) targeting TotalCells while preserving natural proportions
[[nodiscard]] std::array<uint32_t, 3u> DeriveAspectGridDimensions(
    Mathematics::Axis3D BoundsMin,
    Mathematics::Axis3D BoundsMax,
    uint32_t            TargetBudget,
    uint32_t            MinimumAxis,
    uint32_t            MaximumAxis) noexcept
{
    const double SpanX = std::max<double>(BoundsMax.X - BoundsMin.X, 1.0e-4);
    const double SpanY = std::max<double>(BoundsMax.Y - BoundsMin.Y, 1.0e-4);
    const double SpanZ = std::max<double>(BoundsMax.Z - BoundsMin.Z, 1.0e-4);
    const double Volume = SpanX * SpanY * SpanZ;
    const double Pitch  = std::cbrt(Volume / static_cast<double>(std::max(TargetBudget, 64u)));

    const auto ClampDim = [&](double Span) -> uint32_t
    {
        const auto Raw = static_cast<uint32_t>(std::llround(Span / std::max(Pitch, 1.0e-6)));
        return std::clamp(Raw, MinimumAxis, MaximumAxis);
    };
    return { ClampDim(SpanX), ClampDim(SpanY), ClampDim(SpanZ) };
}

// Appends closed 2D chart loops (including seam/pole virtual closures) using exact Line2D and CircularArc2D primitives
void AppendFaceTrimLoops(
    const FaceDomain&                     FaceRegion,
    std::vector<CompiledTrimSegment2D>&   OutTrimSegments,
    uint32_t&                             OutFirstTrim,
    uint32_t&                             OutTrimCount)
{
    OutFirstTrim = static_cast<uint32_t>(OutTrimSegments.size());

    for (const std::vector<DomainSegment>& Loop : FaceRegion.Loops)
    {
        for (const DomainSegment& Seg : Loop)
        {
            if (std::holds_alternative<ParameterLine>(Seg.Curve))
            {
                const Vector2 StartUV = EvaluateParameterCurve(Seg.Curve, Seg.Start).Position;
                const Vector2 EndUV   = EvaluateParameterCurve(Seg.Curve, Seg.End).Position;
                if (Length(EndUV - StartUV) <= 1.0e-12)
                {
                    continue;
                }
                CompiledTrimSegment2D Prim {};
                Prim.Coords0 = {
                    static_cast<float>(StartUV.X),
                    static_cast<float>(StartUV.Y),
                    static_cast<float>(EndUV.X),
                    static_cast<float>(EndUV.Y)
                };
                Prim.Coords1 = { 0.0f, 0.0f, 0.0f, 0.0f };
                OutTrimSegments.push_back(Prim);
                continue;
            }

            if (const auto* Ellipse = std::get_if<ParameterEllipse>(&Seg.Curve))
            {
                const double LenA   = Length(Ellipse->FirstAxis);
                const double LenB   = Length(Ellipse->SecondAxis);
                const double Ortho  = std::abs(Dot(Ellipse->FirstAxis, Ellipse->SecondAxis));
                const double DetAB  = Cross(Ellipse->FirstAxis, Ellipse->SecondAxis);
                const double Radius = Ellipse->Radius * LenA;

                // Exact 2D circular arc whenever FirstAxis and SecondAxis form an isotropic circle (either handedness)
                if (Radius > 1.0e-9 &&
                    std::abs(LenA - LenB) <= 1.0e-6 * std::max(LenA, 1.0) &&
                    Ortho <= 1.0e-6 * LenA * LenB &&
                    std::abs(DetAB) > 1.0e-18)
                {
                    const double HandedSign  = (DetAB >= 0.0) ? 1.0 : -1.0;
                    const double PhaseOffset = std::atan2(Ellipse->FirstAxis.Y, Ellipse->FirstAxis.X);
                    const double SignedSweep = HandedSign * (Seg.End - Seg.Start);
                    CompiledTrimSegment2D ArcPrim {};
                    ArcPrim.Coords0 = {
                        static_cast<float>(Ellipse->Centre.X),
                        static_cast<float>(Ellipse->Centre.Y),
                        static_cast<float>(Radius),
                        static_cast<float>(PhaseOffset + HandedSign * Seg.Start)
                    };
                    ArcPrim.Coords1 = { static_cast<float>(SignedSweep), 1.0f, 0.0f, 0.0f };
                    OutTrimSegments.push_back(ArcPrim);
                    continue;
                }
            }

            // General anisotropic ParameterEllipse or ParameterProjection: adaptive high-density 2D chords
            const double SweepMag = std::abs(Seg.End - Seg.Start);
            const int    Subdivs  = std::clamp(static_cast<int>(std::ceil(SweepMag * 14.0)), 16, 48);
            Vector2 PrevUV = EvaluateParameterCurve(Seg.Curve, Seg.Start).Position;
            for (int Step = 1; Step <= Subdivs; ++Step)
            {
                const double Param = Seg.Start + (Seg.End - Seg.Start) * (static_cast<double>(Step) / Subdivs);
                const Vector2 NextUV = EvaluateParameterCurve(Seg.Curve, Param).Position;
                if (Length(NextUV - PrevUV) > 1.0e-12)
                {
                    CompiledTrimSegment2D LinePrim {};
                    LinePrim.Coords0 = {
                        static_cast<float>(PrevUV.X),
                        static_cast<float>(PrevUV.Y),
                        static_cast<float>(NextUV.X),
                        static_cast<float>(NextUV.Y)
                    };
                    OutTrimSegments.push_back(LinePrim);
                    PrevUV = NextUV;
                }
            }
        }
    }

    OutTrimCount = static_cast<uint32_t>(OutTrimSegments.size()) - OutFirstTrim;
}

// Exact 4x4 inverse Bernstein coefficients at nodes t = {0, 1/3, 2/3, 1}
// Row0 = [1, 0, 0, 0], Row1 = [-5, 18, -9, 2]/6, Row2 = [2, -9, 18, -5]/6, Row3 = [0, 0, 0, 1]
constexpr double InverseBernstein4x4[4][4] = {
    {  1.0,         0.0,         0.0,         0.0        },
    { -5.0 / 6.0,   3.0,        -1.5,         1.0 / 3.0  },
    {  1.0 / 3.0,  -1.5,         3.0,        -5.0 / 6.0  },
    {  0.0,         0.0,         0.0,         1.0        }
};

// Evaluates homogeneous (w*Px, w*Py, w*Pz, w) directly on a NURBS surface definition
[[nodiscard]] std::array<double, 4u> EvaluateNurbsHomogeneous(
    const NurbsSurfaceDefinition& Def,
    double                        ParamU,
    double                        ParamV)
{
    const NurbsBasisSample BasisU = EvaluateNurbsBasis(Def.KnotsU, Def.DegreeU, Def.CountU, ParamU);
    const NurbsBasisSample BasisV = EvaluateNurbsBasis(Def.KnotsV, Def.DegreeV, Def.CountV, ParamV);
    std::array<double, 4u> Hom { 0.0, 0.0, 0.0, 0.0 };

    for (int Ru = 0; Ru <= Def.DegreeU; ++Ru)
    {
        const int PoleU = std::clamp(BasisU.First + Ru, 0, Def.CountU - 1);
        const double Nu = BasisU.Values[0][static_cast<std::size_t>(Ru)];
        for (int Rv = 0; Rv <= Def.DegreeV; ++Rv)
        {
            const int PoleV  = std::clamp(BasisV.First + Rv, 0, Def.CountV - 1);
            const double Nv  = BasisV.Values[0][static_cast<std::size_t>(Rv)];
            const std::size_t Flat = static_cast<std::size_t>(PoleU * Def.CountV + PoleV);
            const double Weight = Def.Weights.empty() ? 1.0 : Def.Weights[Flat];
            const double BasisW = Nu * Nv * Weight;
            const Vector3& Pole = Def.Poles[Flat];
            Hom[0] += BasisW * Pole.X;
            Hom[1] += BasisW * Pole.Y;
            Hom[2] += BasisW * Pole.Z;
            Hom[3] += BasisW;
        }
    }
    return Hom;
}

// Fits one C-infinity chart sub-window [u0, u1] x [v0, v1] into a 4x4 RationalBezierPatch4x4 + tight AABB
void AppendBezierSubPatch(
    const SurfaceGeometry&                  Carrier,
    double                                  U0,
    double                                  U1,
    double                                  V0,
    double                                  V1,
    float                                   OutwardSense,
    uint32_t                                BodyOrdinal,
    uint32_t                                FirstTrim,
    uint32_t                                TrimCount,
    const SurfacePeriodicity&               Periodicity,
    double                                  ScaleMeters,
    std::vector<CompiledBezierPatch4x4>&    OutPatches,
    std::vector<CompiledSurfacePrimitive>&  OutSurfaces)
{
    const auto* NurbsPtr = std::get_if<NurbsSurface>(&Carrier);
    const bool UseExactHomogeneous =
        (NurbsPtr != nullptr && NurbsPtr->Definition != nullptr &&
         NurbsPtr->Definition->DegreeU <= 3 && NurbsPtr->Definition->DegreeV <= 3);

    std::array<std::array<double, 4u>, 16u> GridHom {};
    for (int Row = 0; Row < 4; ++Row)
    {
        const double SampleU = U0 + (U1 - U0) * (static_cast<double>(Row) / 3.0);
        for (int Col = 0; Col < 4; ++Col)
        {
            const double SampleV = V0 + (V1 - V0) * (static_cast<double>(Col) / 3.0);
            if (UseExactHomogeneous)
            {
                std::array<double, 4u> Hom = EvaluateNurbsHomogeneous(*NurbsPtr->Definition, SampleU, SampleV);
                Hom[0] *= ScaleMeters;
                Hom[1] *= ScaleMeters;
                Hom[2] *= ScaleMeters;
                GridHom[static_cast<std::size_t>(Row * 4 + Col)] = Hom;
            }
            else
            {
                const Vector3 Pos = EvaluateSurface(Carrier, { SampleU, SampleV }).Position * ScaleMeters;
                GridHom[static_cast<std::size_t>(Row * 4 + Col)] = { Pos.X, Pos.Y, Pos.Z, 1.0 };
            }
        }
    }

    // Apply M^-1 along U then along V in homogeneous 4D space
    std::array<std::array<double, 4u>, 16u> TempHom {};
    for (int Row = 0; Row < 4; ++Row)
    {
        for (int Col = 0; Col < 4; ++Col)
        {
            std::array<double, 4u> Sum { 0.0, 0.0, 0.0, 0.0 };
            for (int K = 0; K < 4; ++K)
            {
                const double Coeff = InverseBernstein4x4[Row][K];
                const auto& Src    = GridHom[static_cast<std::size_t>(K * 4 + Col)];
                for (std::size_t D = 0; D < 4u; ++D) Sum[D] += Coeff * Src[D];
            }
            TempHom[static_cast<std::size_t>(Row * 4 + Col)] = Sum;
        }
    }

    std::array<std::array<double, 4u>, 16u> BezHom {};
    bool WeightsValid = true;
    for (int Row = 0; Row < 4; ++Row)
    {
        for (int Col = 0; Col < 4; ++Col)
        {
            std::array<double, 4u> Sum { 0.0, 0.0, 0.0, 0.0 };
            for (int K = 0; K < 4; ++K)
            {
                const double Coeff = InverseBernstein4x4[Col][K];
                const auto& Src    = TempHom[static_cast<std::size_t>(Row * 4 + K)];
                for (std::size_t D = 0; D < 4u; ++D) Sum[D] += Coeff * Src[D];
            }
            if (Sum[3] <= 1.0e-4)
            {
                WeightsValid = false;
            }
            BezHom[static_cast<std::size_t>(Row * 4 + Col)] = Sum;
        }
    }

    // Fallback to Euclidean non-rational poles if a degenerate span produced non-positive weights
    if (!WeightsValid && UseExactHomogeneous)
    {
        for (int Row = 0; Row < 4; ++Row)
        {
            const double SampleU = U0 + (U1 - U0) * (static_cast<double>(Row) / 3.0);
            for (int Col = 0; Col < 4; ++Col)
            {
                const double SampleV = V0 + (V1 - V0) * (static_cast<double>(Col) / 3.0);
                const Vector3 Pos = EvaluateSurface(Carrier, { SampleU, SampleV }).Position * ScaleMeters;
                GridHom[static_cast<std::size_t>(Row * 4 + Col)] = { Pos.X, Pos.Y, Pos.Z, 1.0 };
            }
        }
        for (int Row = 0; Row < 4; ++Row)
        {
            for (int Col = 0; Col < 4; ++Col)
            {
                std::array<double, 4u> Sum { 0.0, 0.0, 0.0, 0.0 };
                for (int K = 0; K < 4; ++K)
                {
                    const double Coeff = InverseBernstein4x4[Row][K];
                    const auto& Src    = GridHom[static_cast<std::size_t>(K * 4 + Col)];
                    for (std::size_t D = 0; D < 4u; ++D) Sum[D] += Coeff * Src[D];
                }
                TempHom[static_cast<std::size_t>(Row * 4 + Col)] = Sum;
            }
        }
        for (int Row = 0; Row < 4; ++Row)
        {
            for (int Col = 0; Col < 4; ++Col)
            {
                std::array<double, 4u> Sum { 0.0, 0.0, 0.0, 0.0 };
                for (int K = 0; K < 4; ++K)
                {
                    const double Coeff = InverseBernstein4x4[Col][K];
                    const auto& Src    = TempHom[static_cast<std::size_t>(Row * 4 + K)];
                    for (std::size_t D = 0; D < 4u; ++D) Sum[D] += Coeff * Src[D];
                }
                Sum[3] = 1.0;
                BezHom[static_cast<std::size_t>(Row * 4 + Col)] = Sum;
            }
        }
    }

    CompiledBezierPatch4x4 Patch {};
    Mathematics::Axis3D Low  {  1.0e30f,  1.0e30f,  1.0e30f };
    Mathematics::Axis3D High { -1.0e30f, -1.0e30f, -1.0e30f };

    // Convex-hull property of positive-weight rational Bezier guarantees all surface points lie inside pole AABB!
    for (std::size_t Idx = 0u; Idx < 16u; ++Idx)
    {
        const double W  = std::max(BezHom[Idx][3], 1.0e-8);
        const auto   Px = static_cast<float>(BezHom[Idx][0] / W);
        const auto   Py = static_cast<float>(BezHom[Idx][1] / W);
        const auto   Pz = static_cast<float>(BezHom[Idx][2] / W);
        const auto   Wf = static_cast<float>(W);
        Patch.PolesWeighted[Idx] = { Px, Py, Pz, Wf };

        Low.X  = std::min(Low.X, Px);  Low.Y  = std::min(Low.Y, Py);  Low.Z  = std::min(Low.Z, Pz);
        High.X = std::max(High.X, Px); High.Y = std::max(High.Y, Py); High.Z = std::max(High.Z, Pz);
    }

    const float Pad = 2.0e-4f;
    Low.X -= Pad; Low.Y -= Pad; Low.Z -= Pad;
    High.X += Pad; High.Y += Pad; High.Z += Pad;

    const auto PatchOrdinal = static_cast<uint32_t>(OutPatches.size());
    OutPatches.push_back(Patch);

    CompiledSurfacePrimitive Prim {};
    Prim.BoundsLowCategory = {
        Low.X, Low.Y, Low.Z,
        std::bit_cast<float>(static_cast<uint32_t>(SurfacePrimitiveCategory::RationalBezier4x4))
    };
    Prim.BoundsHighPatch = {
        High.X, High.Y, High.Z,
        std::bit_cast<float>(PatchOrdinal)
    };
    Prim.AxisYPeriodU = { 0.0f, 1.0f, 0.0f, static_cast<float>(Periodicity.PeriodU) };
    Prim.AxisZPeriodV = { 0.0f, 0.0f, 1.0f, static_cast<float>(Periodicity.PeriodV) };
    Prim.TrimSpanSenseBody = {
        static_cast<float>(FirstTrim),
        static_cast<float>(TrimCount),
        OutwardSense,
        static_cast<float>(BodyOrdinal)
    };
    Prim.ChartBounds = {
        static_cast<float>(U0),
        static_cast<float>(V0),
        static_cast<float>(U1),
        static_cast<float>(V1)
    };
    OutSurfaces.push_back(Prim);
}

// Builds sorted knot/curvature split coordinates over [Low, High]
[[nodiscard]] std::vector<double> BuildPatchSplits(
    const std::vector<double>& InternalKnots,
    double                     Low,
    double                     High,
    int                        MinPiecesPerSpan)
{
    std::vector<double> Knots;
    Knots.push_back(Low);
    for (double K : InternalKnots)
    {
        if (K > Low + 1.0e-9 && K < High - 1.0e-9)
        {
            Knots.push_back(K);
        }
    }
    Knots.push_back(High);
    std::sort(Knots.begin(), Knots.end());

    std::vector<double> Refined;
    Refined.push_back(Knots.front());
    for (std::size_t Index = 0u; Index + 1u < Knots.size(); ++Index)
    {
        const double A = Knots[Index];
        const double B = Knots[Index + 1u];
        if (B - A <= 1.0e-9)
        {
            continue;
        }
        const int Sub = std::max(1, MinPiecesPerSpan);
        for (int Step = 1; Step <= Sub; ++Step)
        {
            Refined.push_back(A + (B - A) * (static_cast<double>(Step) / Sub));
        }
    }
    return Refined;
}

void CompileSingleFace(
    const FaceDomain&                       FaceRegion,
    uint32_t                                BodyOrdinal,
    double                                  ScaleMeters,
    std::vector<CompiledTrimSegment2D>&     OutTrims,
    std::vector<CompiledBezierPatch4x4>&    OutPatches,
    std::vector<CompiledSurfacePrimitive>&  OutSurfaces)
{
    uint32_t FirstTrim = 0u;
    uint32_t TrimCount = 0u;
    AppendFaceTrimLoops(FaceRegion, OutTrims, FirstTrim, TrimCount);

    const float OutwardSense = (FaceRegion.Sense == Orientation::Forward) ? 1.0f : -1.0f;
    const SurfacePeriodicity Periodicity = ResolvePeriodicity(FaceRegion.Surface);

    const Mathematics::Axis3D BoxLow  = ToMetersAxis3D(FaceRegion.BoundsLow, ScaleMeters);
    const Mathematics::Axis3D BoxHigh = ToMetersAxis3D(FaceRegion.BoundsHigh, ScaleMeters);
    const float Pad = 2.0e-4f;

    const auto PopulateSurfaceBounds = [&](CompiledSurfacePrimitive& Prim, SurfacePrimitiveCategory Kind, const OrthonormalBasis& Basis)
    {
        Prim.BoundsLowCategory = {
            BoxLow.X - Pad, BoxLow.Y - Pad, BoxLow.Z - Pad,
            std::bit_cast<float>(static_cast<uint32_t>(Kind))
        };
        Prim.BoundsHighPatch = {
            BoxHigh.X + Pad, BoxHigh.Y + Pad, BoxHigh.Z + Pad,
            std::bit_cast<float>(0u)
        };
        const Mathematics::Axis3D Origin = ToMetersAxis3D(Basis.Origin, ScaleMeters);
        const Mathematics::Axis3D AxisX  = ToUnitAxis3D(Basis.XAxis);
        const Mathematics::Axis3D AxisY  = ToUnitAxis3D(Basis.YAxis);
        const Mathematics::Axis3D AxisZ  = ToUnitAxis3D(Basis.ZAxis);

        Prim.OriginRadius0     = { Origin.X, Origin.Y, Origin.Z, 0.0f };
        Prim.AxisXRadius1      = { AxisX.X,  AxisX.Y,  AxisX.Z,  0.0f };
        Prim.AxisYPeriodU      = { AxisY.X,  AxisY.Y,  AxisY.Z,  static_cast<float>(Periodicity.PeriodU) };
        Prim.AxisZPeriodV      = { AxisZ.X,  AxisZ.Y,  AxisZ.Z,  static_cast<float>(Periodicity.PeriodV) };
        Prim.TrimSpanSenseBody = {
            static_cast<float>(FirstTrim),
            static_cast<float>(TrimCount),
            OutwardSense,
            static_cast<float>(BodyOrdinal)
        };
        Prim.ChartBounds = {
            static_cast<float>(FaceRegion.Window.Low.X),
            static_cast<float>(FaceRegion.Window.Low.Y),
            static_cast<float>(FaceRegion.Window.High.X),
            static_cast<float>(FaceRegion.Window.High.Y)
        };
    };

    if (const auto* Plane = std::get_if<PlaneSurface>(&FaceRegion.Surface))
    {
        CompiledSurfacePrimitive Prim {};
        PopulateSurfaceBounds(Prim, SurfacePrimitiveCategory::Plane, Plane->Frame);
        OutSurfaces.push_back(Prim);
        return;
    }

    if (const auto* Cyl = std::get_if<CylinderSurface>(&FaceRegion.Surface))
    {
        CompiledSurfacePrimitive Prim {};
        PopulateSurfaceBounds(Prim, SurfacePrimitiveCategory::Cylinder, Cyl->Frame);
        Prim.OriginRadius0[3] = static_cast<float>(Cyl->Radius * ScaleMeters);
        OutSurfaces.push_back(Prim);
        return;
    }

    if (const auto* Cone = std::get_if<ConeSurface>(&FaceRegion.Surface))
    {
        CompiledSurfacePrimitive Prim {};
        PopulateSurfaceBounds(Prim, SurfacePrimitiveCategory::Cone, Cone->Frame);
        Prim.OriginRadius0[3] = static_cast<float>(Cone->OriginRadius * ScaleMeters);
        Prim.AxisXRadius1[3]  = static_cast<float>(Cone->RadialSlope);
        OutSurfaces.push_back(Prim);
        return;
    }

    if (const auto* Sph = std::get_if<SphereSurface>(&FaceRegion.Surface))
    {
        CompiledSurfacePrimitive Prim {};
        PopulateSurfaceBounds(Prim, SurfacePrimitiveCategory::Sphere, Sph->Frame);
        Prim.OriginRadius0[3] = static_cast<float>(Sph->Radius * ScaleMeters);
        OutSurfaces.push_back(Prim);
        return;
    }

    if (const auto* Tor = std::get_if<TorusSurface>(&FaceRegion.Surface))
    {
        CompiledSurfacePrimitive Prim {};
        PopulateSurfaceBounds(Prim, SurfacePrimitiveCategory::Torus, Tor->Frame);
        Prim.OriginRadius0[3] = static_cast<float>(Tor->MajorRadius * ScaleMeters);
        Prim.AxisXRadius1[3]  = static_cast<float>(Tor->MinorRadius * ScaleMeters);
        OutSurfaces.push_back(Prim);
        return;
    }

    // Freeform NURBS, RollingBallSurface, or ParallelSurface -> knot-split 4x4 RationalBezier4x4 patches
    const double LowU  = FaceRegion.Window.Low.X;
    const double HighU = FaceRegion.Window.High.X;
    const double LowV  = FaceRegion.Window.Low.Y;
    const double HighV = FaceRegion.Window.High.Y;

    const std::vector<double> KnotsU = CollectSurfaceBreakpoints(FaceRegion.Surface, LowU, HighU);
    const std::vector<double> KnotsV = CollectSurfaceBreakpointsAlongV(FaceRegion.Surface, LowV, HighV);

    int SubU = 1;
    int SubV = 1;
    if (const auto* NurbsPtr = std::get_if<NurbsSurface>(&FaceRegion.Surface); NurbsPtr && NurbsPtr->Definition)
    {
        if (NurbsPtr->Definition->DegreeU > 3) SubU = 2;
        if (NurbsPtr->Definition->DegreeV > 3) SubV = 2;
    }
    else
    {
        // Rolling-ball & parallel carriers: ensure angular tube / spine sweep <= pi/4 per cubic Bezier patch
        SubU = std::max(1, static_cast<int>(std::ceil((HighU - LowU) / 0.8)));
        SubV = std::max(1, static_cast<int>(std::ceil((HighV - LowV) / 0.8)));
    }

    const std::vector<double> SplitsU = BuildPatchSplits(KnotsU, LowU, HighU, SubU);
    const std::vector<double> SplitsV = BuildPatchSplits(KnotsV, LowV, HighV, SubV);

    for (std::size_t Iu = 0u; Iu + 1u < SplitsU.size(); ++Iu)
    {
        for (std::size_t Iv = 0u; Iv + 1u < SplitsV.size(); ++Iv)
        {
            AppendBezierSubPatch(
                FaceRegion.Surface,
                SplitsU[Iu], SplitsU[Iu + 1u],
                SplitsV[Iv], SplitsV[Iv + 1u],
                OutwardSense,
                BodyOrdinal,
                FirstTrim,
                TrimCount,
                Periodicity,
                ScaleMeters,
                OutPatches,
                OutSurfaces);
        }
    }
}

[[nodiscard]] Vector3 EvaluateOutwardFaceNormalAt3DPoint(
    const ModelPartition& Partition,
    FaceToken             Face,
    Vector3               Point3D) noexcept
{
    if (!Face.PresentCondition() || !Partition.Faces.Authenticate(Face))
    {
        return { 0.0, 1.0, 0.0 };
    }
    const FaceProfile& Profile = Partition.Faces.Resolve(Face);
    if (!Profile.Surface.PresentCondition() || !Partition.Surfaces.Authenticate(Profile.Surface))
    {
        return { 0.0, 1.0, 0.0 };
    }
    const SurfaceGeometry& Surf = Partition.Surfaces.Resolve(Profile.Surface);
    const Vector2 FootUV = ProjectOntoSurface(Surf, Point3D, { 0.0, 0.0 });
    const Vector3 NatN   = EvaluateSurfaceNormal(Surf, FootUV);
    return (Profile.Sense == Orientation::Forward) ? NatN : (-NatN);
}

// Collects distinct faces incident to Edge around its radial fin ring
[[nodiscard]] std::vector<FaceToken> CollectIncidentEdgeFaces(
    const ModelPartition& Partition,
    EdgeToken             Edge)
{
    std::vector<FaceToken> Result;
    if (!Edge.PresentCondition() || !Partition.Edges.Authenticate(Edge))
    {
        return Result;
    }
    const FinToken StartFin = Partition.Edges.Resolve(Edge).RadialFin;
    if (!StartFin.PresentCondition() || !Partition.Fins.Authenticate(StartFin))
    {
        return Result;
    }
    FinToken Cur = StartFin;
    for (int Guard = 0; Guard < 32; ++Guard)
    {
        const FinProfile& FinRec = Partition.Fins.Resolve(Cur);
        if (FinRec.Loop.PresentCondition() && Partition.Loops.Authenticate(FinRec.Loop))
        {
            const FaceToken IncFace = Partition.Loops.Resolve(FinRec.Loop).Face;
            if (IncFace.PresentCondition() && std::find(Result.begin(), Result.end(), IncFace) == Result.end())
            {
                Result.push_back(IncFace);
            }
        }
        Cur = FinRec.RadialSuccessor;
        if (!Cur.PresentCondition() || Cur == StartFin || !Partition.Fins.Authenticate(Cur))
        {
            break;
        }
    }
    return Result;
}

// Computes exact Baerentzen-Aanaes angle-weighted vertex pseudo-normals across all corners of a body
[[nodiscard]] std::unordered_map<uint32_t, Vector3> ComputeVertexPseudoNormals(
    const ModelPartition& Partition,
    const BodyInventory&  Inventory)
{
    std::unordered_map<uint32_t, Vector3> AccumNormals;
    for (LoopToken Loop : Inventory.Loops)
    {
        if (!Partition.Loops.Authenticate(Loop))
        {
            continue;
        }
        const FaceToken Face = Partition.Loops.Resolve(Loop).Face;
        Partition.EnumerateLoopFins(
            Loop,
            [&](FinToken Fin)
            {
                const VertexToken CornerVert = Partition.FinStart(Fin);
                if (!CornerVert.PresentCondition() || !Partition.Vertices.Authenticate(CornerVert))
                {
                    return;
                }
                const Vector3 CornerPos = Partition.Vertices.Resolve(CornerVert).Position;
                const Vector3 FaceNorm  = EvaluateOutwardFaceNormalAt3DPoint(Partition, Face, CornerPos);

                // Tangent of incoming fin vs outgoing fin at CornerVert
                const FinProfile& OutFin = Partition.Fins.Resolve(Fin);
                Vector3 TangOut { 1.0, 0.0, 0.0 };
                if (OutFin.Edge.PresentCondition() && Partition.Edges.Authenticate(OutFin.Edge))
                {
                    const EdgeProfile& EProf = Partition.Edges.Resolve(OutFin.Edge);
                    if (EProf.Curve.PresentCondition() && Partition.Curves.Authenticate(EProf.Curve))
                    {
                        const double Param = (OutFin.Sense == Orientation::Forward) ? EProf.IntervalLow : EProf.IntervalHigh;
                        const Vector3 Deriv = EvaluateCurve(Partition.Curves.Resolve(EProf.Curve), Param).Derivative;
                        TangOut = SafeNormalizeDouble((OutFin.Sense == Orientation::Forward) ? Deriv : (-Deriv));
                    }
                }

                Vector3 TangIn = TangOut;
                const FinToken PrevToken = OutFin.Predecessor;
                if (PrevToken.PresentCondition() && Partition.Fins.Authenticate(PrevToken))
                {
                    const FinProfile& InFin = Partition.Fins.Resolve(PrevToken);
                    if (InFin.Edge.PresentCondition() && Partition.Edges.Authenticate(InFin.Edge))
                    {
                        const EdgeProfile& EProf = Partition.Edges.Resolve(InFin.Edge);
                        if (EProf.Curve.PresentCondition() && Partition.Curves.Authenticate(EProf.Curve))
                        {
                            const double Param = (InFin.Sense == Orientation::Forward) ? EProf.IntervalHigh : EProf.IntervalLow;
                            const Vector3 Deriv = EvaluateCurve(Partition.Curves.Resolve(EProf.Curve), Param).Derivative;
                            TangIn = SafeNormalizeDouble((InFin.Sense == Orientation::Forward) ? Deriv : (-Deriv));
                        }
                    }
                }

                const double CosAngle = std::clamp(Dot(-TangIn, TangOut), -1.0, 1.0);
                const double CornerAngle = std::max(std::acos(CosAngle), 0.05);
                AccumNormals[CornerVert.Slot] = AccumNormals[CornerVert.Slot] + FaceNorm * CornerAngle;
            });
    }

    for (auto& [SlotIdx, Vec] : AccumNormals)
    {
        Vec = SafeNormalizeDouble(Vec);
    }
    return AccumNormals;
}

void AppendCurveToPackage(
    const CurveGeometry&     Curve,
    double                   Low,
    double                   High,
    uint32_t                 BodyOrdinal,
    float                    DihedralRadians,
    double                   ScaleMeters,
    ParametricCurvePackage&  OutPackage)
{
    if (const auto* Line = std::get_if<StraightLine>(&Curve))
    {
        ParametricCurveEntry Entry {};
        Entry.Category         = ParametricCurveCategory::StraightLine3D;
        Entry.BodyOrdinal      = BodyOrdinal;
        Entry.OriginOrCentre   = ToMetersAxis3D(Line->Origin, ScaleMeters);
        Entry.AxisXOrDirection = ToUnitAxis3D(Line->Direction);
        Entry.IntervalLow      = static_cast<float>(Low * ScaleMeters);
        Entry.IntervalHigh     = static_cast<float>(High * ScaleMeters);
        Entry.DihedralRadians  = DihedralRadians;
        Entry.ClosedCondition  = 0u;
        OutPackage.Curves.push_back(Entry);
        return;
    }

    if (const auto* Arc = std::get_if<CircularArc>(&Curve))
    {
        ParametricCurveEntry Entry {};
        Entry.Category          = ParametricCurveCategory::CircularArc3D;
        Entry.BodyOrdinal       = BodyOrdinal;
        Entry.OriginOrCentre    = ToMetersAxis3D(Arc->Frame.Origin, ScaleMeters);
        Entry.AxisXOrDirection  = ToUnitAxis3D(Arc->Frame.XAxis);
        Entry.AxisY             = ToUnitAxis3D(Arc->Frame.YAxis);
        Entry.MajorRadiusMeters = static_cast<float>(Arc->Radius * ScaleMeters);
        Entry.MinorRadiusMeters = Entry.MajorRadiusMeters;
        Entry.IntervalLow       = static_cast<float>(Low);
        Entry.IntervalHigh      = static_cast<float>(High);
        Entry.DihedralRadians   = DihedralRadians;
        Entry.ClosedCondition   = (std::abs(High - Low) >= 2.0 * std::numbers::pi - 1.0e-5) ? 1u : 0u;
        OutPackage.Curves.push_back(Entry);
        return;
    }

    if (const auto* Ell = std::get_if<EllipticalArc>(&Curve))
    {
        ParametricCurveEntry Entry {};
        Entry.Category          = ParametricCurveCategory::EllipticalArc3D;
        Entry.BodyOrdinal       = BodyOrdinal;
        Entry.OriginOrCentre    = ToMetersAxis3D(Ell->Frame.Origin, ScaleMeters);
        Entry.AxisXOrDirection  = ToUnitAxis3D(Ell->Frame.XAxis);
        Entry.AxisY             = ToUnitAxis3D(Ell->Frame.YAxis);
        Entry.MajorRadiusMeters = static_cast<float>(Ell->MajorRadius * ScaleMeters);
        Entry.MinorRadiusMeters = static_cast<float>(Ell->MinorRadius * ScaleMeters);
        Entry.IntervalLow       = static_cast<float>(Low);
        Entry.IntervalHigh      = static_cast<float>(High);
        Entry.DihedralRadians   = DihedralRadians;
        Entry.ClosedCondition   = (std::abs(High - Low) >= 2.0 * std::numbers::pi - 1.0e-5) ? 1u : 0u;
        OutPackage.Curves.push_back(Entry);
        return;
    }

    if (const auto* NurbsC = std::get_if<NurbsCurve>(&Curve); NurbsC && NurbsC->Definition)
    {
        const std::vector<NurbsBezierPiece> Pieces = DecomposeNurbsCurve(*NurbsC->Definition, Low, High);
        for (const NurbsBezierPiece& Piece : Pieces)
        {
            ParametricCurveEntry Entry {};
            Entry.Category        = ParametricCurveCategory::RationalBezierSpan3D;
            Entry.BodyOrdinal     = BodyOrdinal;
            Entry.FirstPoleIndex  = static_cast<uint32_t>(OutPackage.BezierPolesWeighted.size());
            Entry.PoleCount       = static_cast<uint32_t>(Piece.Poles.size());
            Entry.IntervalLow     = static_cast<float>(Piece.PieceLow);
            Entry.IntervalHigh    = static_cast<float>(Piece.PieceHigh);
            Entry.DihedralRadians = DihedralRadians;
            for (std::size_t Idx = 0u; Idx < Piece.Poles.size(); ++Idx)
            {
                const Mathematics::Axis3D Pm = ToMetersAxis3D(Piece.Poles[Idx], ScaleMeters);
                const float Wf = Piece.Weights.empty() ? 1.0f : static_cast<float>(Piece.Weights[Idx]);
                OutPackage.BezierPolesWeighted.push_back({ Pm.X, Pm.Y, Pm.Z, Wf });
            }
            OutPackage.Curves.push_back(Entry);
        }
        return;
    }

    // IntersectionCurve / ContactCurve -> fit cubic Bezier spans at breakpoints
    const int Spans = std::clamp(static_cast<int>(std::ceil(std::abs(High - Low) / 0.8)), 2, 12);
    for (int SpanIdx = 0; SpanIdx < Spans; ++SpanIdx)
    {
        const double T0 = Low + (High - Low) * (static_cast<double>(SpanIdx) / Spans);
        const double T1 = Low + (High - Low) * (static_cast<double>(SpanIdx + 1) / Spans);
        std::array<Vector3, 4u> Samples {};
        for (int K = 0; K < 4; ++K)
        {
            Samples[static_cast<std::size_t>(K)] =
                EvaluateCurve(Curve, T0 + (T1 - T0) * (static_cast<double>(K) / 3.0)).Position * ScaleMeters;
        }

        ParametricCurveEntry Entry {};
        Entry.Category        = ParametricCurveCategory::RationalBezierSpan3D;
        Entry.BodyOrdinal     = BodyOrdinal;
        Entry.FirstPoleIndex  = static_cast<uint32_t>(OutPackage.BezierPolesWeighted.size());
        Entry.PoleCount       = 4u;
        Entry.IntervalLow     = static_cast<float>(T0);
        Entry.IntervalHigh    = static_cast<float>(T1);
        Entry.DihedralRadians = DihedralRadians;

        for (int Row = 0; Row < 4; ++Row)
        {
            Vector3 Pole { 0.0, 0.0, 0.0 };
            for (int K = 0; K < 4; ++K)
            {
                Pole = Pole + Samples[static_cast<std::size_t>(K)] * InverseBernstein4x4[Row][K];
            }
            OutPackage.BezierPolesWeighted.push_back({
                static_cast<float>(Pole.X),
                static_cast<float>(Pole.Y),
                static_cast<float>(Pole.Z),
                1.0f
            });
        }
        OutPackage.Curves.push_back(Entry);
    }
}

void CompileBodyBoundaryEdges(
    const ModelPartition&                       Partition,
    const BodyInventory&                        Inventory,
    const std::unordered_map<uint32_t, Vector3>& VertexNormals,
    uint32_t                                    BodyOrdinal,
    double                                      ScaleMeters,
    ParametricCurvePackage&                     OutCurves,
    std::vector<CompiledConicArc3D>&            OutConicArcs,
    std::vector<CompiledPseudoNormalEdge>&      OutEdges)
{
    for (EdgeToken Edge : Inventory.Edges)
    {
        if (!Partition.Edges.Authenticate(Edge))
        {
            continue;
        }
        const EdgeProfile& EProf = Partition.Edges.Resolve(Edge);
        if (!EProf.Curve.PresentCondition() || !Partition.Curves.Authenticate(EProf.Curve))
        {
            continue;
        }

        const CurveGeometry& Curve = Partition.Curves.Resolve(EProf.Curve);
        const std::vector<FaceToken> IncidentFaces = CollectIncidentEdgeFaces(Partition, Edge);

        const auto EvalEdgePseudoNormalAt = [&](Vector3 Point3D, float& OutDihedral) -> Vector3
        {
            OutDihedral = 0.0f;
            if (IncidentFaces.empty())
            {
                return { 0.0, 1.0, 0.0 };
            }
            if (IncidentFaces.size() == 1u)
            {
                return EvaluateOutwardFaceNormalAt3DPoint(Partition, IncidentFaces[0], Point3D);
            }
            const Vector3 N0 = EvaluateOutwardFaceNormalAt3DPoint(Partition, IncidentFaces[0], Point3D);
            const Vector3 N1 = EvaluateOutwardFaceNormalAt3DPoint(Partition, IncidentFaces[1], Point3D);
            OutDihedral = static_cast<float>(std::acos(std::clamp(Dot(N0, N1), -1.0, 1.0)));
            const Vector3 Sum = N0 + N1;
            return (Length(Sum) > 1.0e-9) ? SafeNormalizeDouble(Sum) : N0;
        };

        const Vector3 MidPos = EvaluateCurve(Curve, 0.5 * (EProf.IntervalLow + EProf.IntervalHigh)).Position;
        float MidDihedral = 0.0f;
        const Vector3 MidEdgeNorm = EvalEdgePseudoNormalAt(MidPos, MidDihedral);

        AppendCurveToPackage(Curve, EProf.IntervalLow, EProf.IntervalHigh, BodyOrdinal, MidDihedral, ScaleMeters, OutCurves);

        const auto ResolveEndpointNormal = [&](VertexToken Vert, Vector3 FallbackPos) -> Vector3
        {
            if (Vert.PresentCondition())
            {
                if (const auto It = VertexNormals.find(Vert.Slot); It != VertexNormals.end())
                {
                    return It->second;
                }
            }
            float DummyAngle = 0.0f;
            return EvalEdgePseudoNormalAt(FallbackPos, DummyAngle);
        };

        // Case 1: Exact 3D StraightLine -> 1 StraightSegment3D
        if (std::holds_alternative<StraightLine>(Curve))
        {
            const Vector3 PosA = EvaluateCurve(Curve, EProf.IntervalLow).Position;
            const Vector3 PosB = EvaluateCurve(Curve, EProf.IntervalHigh).Position;
            if (Length(PosB - PosA) <= 1.0e-12)
            {
                continue;
            }
            const Vector3 NormVA = ResolveEndpointNormal(EProf.Start, PosA);
            const Vector3 NormVB = ResolveEndpointNormal(EProf.End,   PosB);

            CompiledPseudoNormalEdge Prim {};
            const Mathematics::Axis3D Am = ToMetersAxis3D(PosA, ScaleMeters);
            const Mathematics::Axis3D Bm = ToMetersAxis3D(PosB, ScaleMeters);
            const Mathematics::Axis3D Ne = ToUnitAxis3D(MidEdgeNorm);
            const Mathematics::Axis3D Na = ToUnitAxis3D(NormVA);
            const Mathematics::Axis3D Nb = ToUnitAxis3D(NormVB);

            Prim.StartPointCategory     = { Am.X, Am.Y, Am.Z, std::bit_cast<float>(static_cast<uint32_t>(EdgeProjectionCategory::StraightSegment3D)) };
            Prim.EndPointArcOrdinal = { Bm.X, Bm.Y, Bm.Z, std::bit_cast<float>(0u) };
            Prim.EdgePseudoNormal   = { Ne.X, Ne.Y, Ne.Z, MidDihedral };
            Prim.StartVertexNormal  = { Na.X, Na.Y, Na.Z, static_cast<float>(BodyOrdinal) };
            Prim.EndVertexNormal    = { Nb.X, Nb.Y, Nb.Z, -1.0f };
            OutEdges.push_back(Prim);
            continue;
        }

        // Case 2: Exact 3D CircularArc / EllipticalArc -> exact ConicArc3D sub-spans (0 sagitta error, <= 18 deg normal turn)
        if (const std::optional<ConicForm> Conic = ResolveConicForm(Curve))
        {
            const double SweepTotal = EProf.IntervalHigh - EProf.IntervalLow;
            const int SubArcs = std::clamp(static_cast<int>(std::ceil(std::abs(SweepTotal) / 0.32)), 1, 20);
            const double MajorM = Length(Conic->CosineAxis) * ScaleMeters;
            const double MinorM = Length(Conic->SineAxis)   * ScaleMeters;
            const Mathematics::Axis3D CentreM = ToMetersAxis3D(Conic->Centre, ScaleMeters);
            const Mathematics::Axis3D CosAxM  = ToMetersAxis3D(Conic->CosineAxis, ScaleMeters);
            const Mathematics::Axis3D SinAxM  = ToMetersAxis3D(Conic->SineAxis, ScaleMeters);
            const Mathematics::Axis3D PlaneN  = ToUnitAxis3D(Conic->Normal);
            const bool ClosedLoop = (std::abs(SweepTotal) >= 2.0 * std::numbers::pi - 1.0e-5);

            for (int Sub = 0; Sub < SubArcs; ++Sub)
            {
                const double A0  = EProf.IntervalLow + SweepTotal * (static_cast<double>(Sub) / SubArcs);
                const double A1  = EProf.IntervalLow + SweepTotal * (static_cast<double>(Sub + 1) / SubArcs);
                const double Am0 = 0.5 * (A0 + A1);

                const Vector3 Pos0 = EvaluateCurve(Curve, A0).Position;
                const Vector3 Pos1 = EvaluateCurve(Curve, A1).Position;
                const Vector3 PosM = EvaluateCurve(Curve, Am0).Position;

                float SubDihedral = 0.0f;
                const Vector3 NormMid = EvalEdgePseudoNormalAt(PosM, SubDihedral);
                float Dummy0 = 0.0f;
                float Dummy1 = 0.0f;
                const Vector3 Norm0 = (!ClosedLoop && Sub == 0)
                    ? ResolveEndpointNormal(EProf.Start, Pos0)
                    : EvalEdgePseudoNormalAt(Pos0, Dummy0);
                const Vector3 Norm1 = (!ClosedLoop && Sub + 1 == SubArcs)
                    ? ResolveEndpointNormal(EProf.End, Pos1)
                    : EvalEdgePseudoNormalAt(Pos1, Dummy1);

                const auto ArcOrdinal = static_cast<uint32_t>(OutConicArcs.size());
                CompiledConicArc3D ArcGpu {};
                ArcGpu.CentreStartAngle = { CentreM.X, CentreM.Y, CentreM.Z, static_cast<float>(A0) };
                ArcGpu.CosineAxisSweep  = { CosAxM.X,  CosAxM.Y,  CosAxM.Z,  static_cast<float>(A1 - A0) };
                ArcGpu.SineAxisMajor    = { SinAxM.X,  SinAxM.Y,  SinAxM.Z,  static_cast<float>(MajorM) };
                ArcGpu.PlaneNormalMinor = { PlaneN.X,  PlaneN.Y,  PlaneN.Z,  static_cast<float>(MinorM) };
                OutConicArcs.push_back(ArcGpu);

                const Mathematics::Axis3D P0m = ToMetersAxis3D(Pos0, ScaleMeters);
                const Mathematics::Axis3D P1m = ToMetersAxis3D(Pos1, ScaleMeters);
                const Mathematics::Axis3D Nem = ToUnitAxis3D(NormMid);
                const Mathematics::Axis3D N0m = ToUnitAxis3D(Norm0);
                const Mathematics::Axis3D N1m = ToUnitAxis3D(Norm1);

                CompiledPseudoNormalEdge Prim {};
                Prim.StartPointCategory     = { P0m.X, P0m.Y, P0m.Z, std::bit_cast<float>(static_cast<uint32_t>(EdgeProjectionCategory::ConicArc3D)) };
                Prim.EndPointArcOrdinal = { P1m.X, P1m.Y, P1m.Z, std::bit_cast<float>(ArcOrdinal) };
                Prim.EdgePseudoNormal   = { Nem.X, Nem.Y, Nem.Z, SubDihedral };
                Prim.StartVertexNormal  = { N0m.X, N0m.Y, N0m.Z, static_cast<float>(BodyOrdinal) };
                Prim.EndVertexNormal    = { N1m.X, N1m.Y, N1m.Z, -1.0f };
                OutEdges.push_back(Prim);
            }
            continue;
        }

        // Case 3: Freeform NURBS / TracedIntersection / ContactCurve -> adaptive 3D chords with exact pseudo-normals
        const int Chords = std::clamp(static_cast<int>(std::ceil(std::abs(EProf.IntervalHigh - EProf.IntervalLow) * 16.0)), 12, 48);
        const bool ClosedFreeform = PeriodicCondition(Curve) &&
            Length(EvaluateCurve(Curve, EProf.IntervalLow).Position - EvaluateCurve(Curve, EProf.IntervalHigh).Position) < 1.0e-7;

        for (int Step = 0; Step < Chords; ++Step)
        {
            const double T0 = EProf.IntervalLow + (EProf.IntervalHigh - EProf.IntervalLow) * (static_cast<double>(Step) / Chords);
            const double T1 = EProf.IntervalLow + (EProf.IntervalHigh - EProf.IntervalLow) * (static_cast<double>(Step + 1) / Chords);
            const Vector3 Pos0 = EvaluateCurve(Curve, T0).Position;
            const Vector3 Pos1 = EvaluateCurve(Curve, T1).Position;
            if (Length(Pos1 - Pos0) <= 1.0e-12)
            {
                continue;
            }
            const Vector3 PosM = EvaluateCurve(Curve, 0.5 * (T0 + T1)).Position;
            float SubDihedral = 0.0f;
            float DummyA = 0.0f;
            float DummyB = 0.0f;
            const Vector3 NormM = EvalEdgePseudoNormalAt(PosM, SubDihedral);
            const Vector3 Norm0 = (!ClosedFreeform && Step == 0) ? ResolveEndpointNormal(EProf.Start, Pos0) : EvalEdgePseudoNormalAt(Pos0, DummyA);
            const Vector3 Norm1 = (!ClosedFreeform && Step + 1 == Chords) ? ResolveEndpointNormal(EProf.End, Pos1) : EvalEdgePseudoNormalAt(Pos1, DummyB);

            const Mathematics::Axis3D P0m = ToMetersAxis3D(Pos0, ScaleMeters);
            const Mathematics::Axis3D P1m = ToMetersAxis3D(Pos1, ScaleMeters);
            const Mathematics::Axis3D Nem = ToUnitAxis3D(NormM);
            const Mathematics::Axis3D N0m = ToUnitAxis3D(Norm0);
            const Mathematics::Axis3D N1m = ToUnitAxis3D(Norm1);

            CompiledPseudoNormalEdge Prim {};
            Prim.StartPointCategory     = { P0m.X, P0m.Y, P0m.Z, std::bit_cast<float>(static_cast<uint32_t>(EdgeProjectionCategory::StraightSegment3D)) };
            Prim.EndPointArcOrdinal = { P1m.X, P1m.Y, P1m.Z, std::bit_cast<float>(0u) };
            Prim.EdgePseudoNormal   = { Nem.X, Nem.Y, Nem.Z, SubDihedral };
            Prim.StartVertexNormal  = { N0m.X, N0m.Y, N0m.Z, static_cast<float>(BodyOrdinal) };
            Prim.EndVertexNormal    = { N1m.X, N1m.Y, N1m.Z, -1.0f };
            OutEdges.push_back(Prim);
        }
    }
}

void BuildMacroCellIndex(
    CompiledSurfaceField& Field,
    uint32_t              TargetMacroBudget)
{
    if (Field.Surfaces.empty())
    {
        return;
    }

    Mathematics::Axis3D RawMin {  1.0e30f,  1.0e30f,  1.0e30f };
    Mathematics::Axis3D RawMax { -1.0e30f, -1.0e30f, -1.0e30f };
    for (const CompiledSurfacePrimitive& Surf : Field.Surfaces)
    {
        RawMin.X = std::min(RawMin.X, Surf.BoundsLowCategory[0]);
        RawMin.Y = std::min(RawMin.Y, Surf.BoundsLowCategory[1]);
        RawMin.Z = std::min(RawMin.Z, Surf.BoundsLowCategory[2]);
        RawMax.X = std::max(RawMax.X, Surf.BoundsHighPatch[0]);
        RawMax.Y = std::max(RawMax.Y, Surf.BoundsHighPatch[1]);
        RawMax.Z = std::max(RawMax.Z, Surf.BoundsHighPatch[2]);
    }

    const float MaxSpan = std::max({ RawMax.X - RawMin.X, RawMax.Y - RawMin.Y, RawMax.Z - RawMin.Z, 1.0e-3f });
    const float Margin  = 0.08f * MaxSpan;
    Field.GridBoundsMin = { RawMin.X - Margin, RawMin.Y - Margin, RawMin.Z - Margin };
    Field.GridBoundsMax = { RawMax.X + Margin, RawMax.Y + Margin, RawMax.Z + Margin };
    Field.EdgeTieTolerance = std::max(MaxSpan * 3.0e-5f, 1.0e-6f);

    Field.MacroDimensions = DeriveAspectGridDimensions(
        Field.GridBoundsMin,
        Field.GridBoundsMax,
        TargetMacroBudget,
        8u,
        32u);

    const uint32_t Mx = Field.MacroDimensions[0];
    const uint32_t My = Field.MacroDimensions[1];
    const uint32_t Mz = Field.MacroDimensions[2];

    const float CellSizeX = (Field.GridBoundsMax.X - Field.GridBoundsMin.X) / static_cast<float>(Mx);
    const float CellSizeY = (Field.GridBoundsMax.Y - Field.GridBoundsMin.Y) / static_cast<float>(My);
    const float CellSizeZ = (Field.GridBoundsMax.Z - Field.GridBoundsMin.Z) / static_cast<float>(Mz);
    Field.CellRadiusMeters = 0.5f * std::sqrt(CellSizeX * CellSizeX + CellSizeY * CellSizeY + CellSizeZ * CellSizeZ);

    const float NarrowBandLimit = 1.42f * Field.CellRadiusMeters;

    // Precompute edge AABBs for fast per-cell culling
    struct EdgeBox { Mathematics::Axis3D Low {}; Mathematics::Axis3D High {}; };
    std::vector<EdgeBox> EdgeBounds(Field.Edges.size());
    for (std::size_t Idx = 0u; Idx < Field.Edges.size(); ++Idx)
    {
        const CompiledPseudoNormalEdge& E = Field.Edges[Idx];
        const auto ProjCategory = static_cast<EdgeProjectionCategory>(std::bit_cast<uint32_t>(E.StartPointCategory[3]));
        if (ProjCategory == EdgeProjectionCategory::StraightSegment3D)
        {
            EdgeBounds[Idx].Low = {
                std::min(E.StartPointCategory[0], E.EndPointArcOrdinal[0]),
                std::min(E.StartPointCategory[1], E.EndPointArcOrdinal[1]),
                std::min(E.StartPointCategory[2], E.EndPointArcOrdinal[2])
            };
            EdgeBounds[Idx].High = {
                std::max(E.StartPointCategory[0], E.EndPointArcOrdinal[0]),
                std::max(E.StartPointCategory[1], E.EndPointArcOrdinal[1]),
                std::max(E.StartPointCategory[2], E.EndPointArcOrdinal[2])
            };
        }
        else
        {
            const uint32_t ArcIdx = std::bit_cast<uint32_t>(E.EndPointArcOrdinal[3]);
            const CompiledConicArc3D& Arc = Field.ConicArcs3D[ArcIdx];
            const float RadBound = std::max(Arc.SineAxisMajor[3], Arc.PlaneNormalMinor[3]) + 1.0e-4f;
            EdgeBounds[Idx].Low  = { Arc.CentreStartAngle[0] - RadBound, Arc.CentreStartAngle[1] - RadBound, Arc.CentreStartAngle[2] - RadBound };
            EdgeBounds[Idx].High = { Arc.CentreStartAngle[0] + RadBound, Arc.CentreStartAngle[1] + RadBound, Arc.CentreStartAngle[2] + RadBound };
        }
    }

    SurfaceFieldQuerySettings ExactSettings {};
    ExactSettings.EnableMacroCellSkip   = false;
    ExactSettings.GaussNewtonIterations = 3u;

    Field.MacroCells.resize(static_cast<std::size_t>(Mx) * My * Mz);
    Field.CellSurfaceIndices.clear();
    Field.CellEdgeIndices.clear();

    for (uint32_t Iz = 0u; Iz < Mz; ++Iz)
    {
        for (uint32_t Iy = 0u; Iy < My; ++Iy)
        {
            for (uint32_t Ix = 0u; Ix < Mx; ++Ix)
            {
                const Mathematics::Axis3D CellLow {
                    Field.GridBoundsMin.X + static_cast<float>(Ix) * CellSizeX,
                    Field.GridBoundsMin.Y + static_cast<float>(Iy) * CellSizeY,
                    Field.GridBoundsMin.Z + static_cast<float>(Iz) * CellSizeZ
                };
                const Mathematics::Axis3D CellHigh {
                    CellLow.X + CellSizeX,
                    CellLow.Y + CellSizeY,
                    CellLow.Z + CellSizeZ
                };
                const Mathematics::Axis3D Center {
                    0.5f * (CellLow.X + CellHigh.X),
                    0.5f * (CellLow.Y + CellHigh.Y),
                    0.5f * (CellLow.Z + CellHigh.Z)
                };

                const float CenterDist = Field.QueryExactUnaccelerated(Center, ExactSettings).SignedDistanceMeters;
                CompiledMacroCell Cell {};
                Cell.CenterDistanceBits = std::bit_cast<uint32_t>(CenterDist);

                if (std::abs(CenterDist) <= NarrowBandLimit)
                {
                    const float Reach   = std::abs(CenterDist) + 2.05f * Field.CellRadiusMeters;
                    const float ReachSq = Reach * Reach;

                    Cell.FirstSurfaceEntry = static_cast<uint32_t>(Field.CellSurfaceIndices.size());
                    uint32_t SurfCount = 0u;
                    for (uint32_t Si = 0u; Si < Field.Surfaces.size() && SurfCount < 0xFFFFu; ++Si)
                    {
                        const CompiledSurfacePrimitive& Surf = Field.Surfaces[Si];
                        const Mathematics::Axis3D SLow  { Surf.BoundsLowCategory[0], Surf.BoundsLowCategory[1], Surf.BoundsLowCategory[2] };
                        const Mathematics::Axis3D SHigh { Surf.BoundsHighPatch[0], Surf.BoundsHighPatch[1], Surf.BoundsHighPatch[2] };
                        if (BoxToBoxDistanceSquared(CellLow, CellHigh, SLow, SHigh) <= ReachSq)
                        {
                            Field.CellSurfaceIndices.push_back(Si);
                            ++SurfCount;
                        }
                    }

                    Cell.FirstEdgeEntry = static_cast<uint32_t>(Field.CellEdgeIndices.size());
                    uint32_t EdgeCount = 0u;
                    for (uint32_t Ei = 0u; Ei < Field.Edges.size() && EdgeCount < 0xFFFFu; ++Ei)
                    {
                        if (BoxToBoxDistanceSquared(CellLow, CellHigh, EdgeBounds[Ei].Low, EdgeBounds[Ei].High) <= ReachSq)
                        {
                            Field.CellEdgeIndices.push_back(Ei);
                            ++EdgeCount;
                        }
                    }

                    Cell.PackedCounts = (SurfCount & 0xFFFFu) | ((EdgeCount & 0xFFFFu) << 16u);
                }

                Field.MacroCells[(static_cast<std::size_t>(Iz) * My + Iy) * Mx + Ix] = Cell;
            }
        }
    }
}

void BakeLocalDistanceVolumeFromField(
    const CompiledSurfaceField&                   Field,
    uint32_t                                      TargetVoxelBudget,
    DistanceFieldSpace::LocalSignedDistanceVolume& OutVolume)
{
    OutVolume.Reset();
    if (Field.EmptyCondition())
    {
        return;
    }

    const std::array<uint32_t, 3u> Dims = DeriveAspectGridDimensions(
        Field.GridBoundsMin,
        Field.GridBoundsMax,
        TargetVoxelBudget,
        16u,
        96u);

    OutVolume.ResolutionX = Dims[0];
    OutVolume.ResolutionY = Dims[1];
    OutVolume.ResolutionZ = Dims[2];
    OutVolume.LocalMin    = Field.GridBoundsMin;
    OutVolume.LocalMax    = Field.GridBoundsMax;
    OutVolume.VoxelSpacing = {
        (Field.GridBoundsMax.X - Field.GridBoundsMin.X) / static_cast<float>(std::max(1u, Dims[0] - 1u)),
        (Field.GridBoundsMax.Y - Field.GridBoundsMin.Y) / static_cast<float>(std::max(1u, Dims[1] - 1u)),
        (Field.GridBoundsMax.Z - Field.GridBoundsMin.Z) / static_cast<float>(std::max(1u, Dims[2] - 1u))
    };

    const std::size_t TotalVoxels = static_cast<std::size_t>(Dims[0]) * Dims[1] * Dims[2];
    OutVolume.HalfDistanceSamples.resize(TotalVoxels);
    OutVolume.DistanceSamples.resize(TotalVoxels);

    SurfaceFieldQuerySettings BakeQuery {};
    BakeQuery.EnableMacroCellSkip   = false;
    BakeQuery.GaussNewtonIterations = 3u;

    for (uint32_t Iz = 0u; Iz < Dims[2]; ++Iz)
    {
        const float Pz = OutVolume.LocalMin.Z + static_cast<float>(Iz) * OutVolume.VoxelSpacing.Z;
        for (uint32_t Iy = 0u; Iy < Dims[1]; ++Iy)
        {
            const float Py = OutVolume.LocalMin.Y + static_cast<float>(Iy) * OutVolume.VoxelSpacing.Y;
            for (uint32_t Ix = 0u; Ix < Dims[0]; ++Ix)
            {
                const float Px = OutVolume.LocalMin.X + static_cast<float>(Ix) * OutVolume.VoxelSpacing.X;
                const float SignedD = Field.QueryExactUnaccelerated({ Px, Py, Pz }, BakeQuery).SignedDistanceMeters;
                const std::size_t Flat = (static_cast<std::size_t>(Iz) * Dims[1] + Iy) * Dims[0] + Ix;
                OutVolume.HalfDistanceSamples[Flat] = PackFloat32ToBinary16(SignedD);
                OutVolume.DistanceSamples[Flat]     = DistanceFieldSpace::UnpackBinary16ToFloat32(OutVolume.HalfDistanceSamples[Flat]);
            }
        }
    }
}

void AuditCompiledSurfaceField(
    const ModelPartition&          Partition,
    std::span<const BodyToken>     Bodies,
    const CompiledSurfaceField&    Field,
    double                         ScaleMeters,
    uint32_t                       SampleCount,
    SolidArcFieldAuditMetrics&     OutMetrics)
{
    if (SampleCount == 0u || Field.EmptyCondition() || Bodies.empty())
    {
        return;
    }

    std::vector<BodyContainment> BodyContainments;
    BodyContainments.reserve(Bodies.size());
    for (BodyToken Body : Bodies)
    {
        BodyContainments.push_back(PrepareBodyContainment(Partition, Body));
    }

    uint32_t RngSeed = 0x5A17C9E3u;
    const auto NextUniform = [&]() -> double
    {
        RngSeed = RngSeed * 1664525u + 1013904223u;
        return static_cast<double>(RngSeed) / 4294967296.0;
    };

    std::vector<float> AbsErrors;
    AbsErrors.reserve(SampleCount);
    uint32_t SignMatches = 0u;
    uint32_t ValidCount  = 0u;

    SurfaceFieldQuerySettings EvalSettings {};
    EvalSettings.EnableMacroCellSkip   = false;
    EvalSettings.GaussNewtonIterations = 3u;

    for (uint32_t SampleIdx = 0u; SampleIdx < SampleCount; ++SampleIdx)
    {
        // Sample inside narrow-band & interior/exterior of GridBounds
        const Mathematics::Axis3D PosM {
            static_cast<float>(Field.GridBoundsMin.X + NextUniform() * (Field.GridBoundsMax.X - Field.GridBoundsMin.X)),
            static_cast<float>(Field.GridBoundsMin.Y + NextUniform() * (Field.GridBoundsMax.Y - Field.GridBoundsMin.Y)),
            static_cast<float>(Field.GridBoundsMin.Z + NextUniform() * (Field.GridBoundsMax.Z - Field.GridBoundsMin.Z))
        };
        const Vector3 PosPart {
            static_cast<double>(PosM.X) / ScaleMeters,
            static_cast<double>(PosM.Y) / ScaleMeters,
            static_cast<double>(PosM.Z) / ScaleMeters
        };

        bool InsideAnyBody = false;
        bool OnBoundary    = false;
        for (const BodyContainment& Cont : BodyContainments)
        {
            const SolidPointClass PtClass = ClassifySolidPointScreened(Cont, PosPart);
            if (PtClass.Membership == PointMembership::Boundary)
            {
                OnBoundary = true;
                break;
            }
            if (PtClass.Membership == PointMembership::Inside)
            {
                InsideAnyBody = true;
            }
        }
        if (OnBoundary)
        {
            continue;
        }

        const float CompiledSigned = Field.QueryExactUnaccelerated(PosM, EvalSettings).SignedDistanceMeters;
        if (std::abs(CompiledSigned) < 2.0e-5f)
        {
            continue;
        }

        // Also verify narrow-band accelerated query sign parity near the surface
        const float AccelSigned = Field.QuerySignedDistance(PosM).SignedDistanceMeters;
        const bool ExpectedInside = InsideAnyBody;
        const bool ActualInside   = (AccelSigned < 0.0f);
        if (ExpectedInside == ActualInside)
        {
            ++SignMatches;
        }

        // Exact per-body signed reference distance combined via min_b SDF_b(p)
        double BestSignedExactMeters = 1.0e30;
        for (const BodyContainment& Cont : BodyContainments)
        {
            const SolidPointClass BodyClass = ClassifySolidPointScreened(Cont, PosPart);
            const bool BodyInside = (BodyClass.Membership == PointMembership::Inside);
            double BodyUnsignedMeters = 1.0e30;
            for (const FaceDomain& Dom : Cont.Faces)
            {
                const Vector2 RefUV {
                    0.5 * (Dom.Window.Low.X + Dom.Window.High.X),
                    0.5 * (Dom.Window.Low.Y + Dom.Window.High.Y)
                };
                const Vector2 FootUV = ProjectOntoSurface(Dom.Surface, PosPart, RefUV);
                if (ClassifyParameterPoint(Dom, FootUV) != PointMembership::Outside)
                {
                    const double DistM = Distance(EvaluateSurface(Dom.Surface, FootUV).Position, PosPart) * ScaleMeters;
                    BodyUnsignedMeters = std::min(BodyUnsignedMeters, DistM);
                }
                for (const DomainBoundaryCurve& BCurve : Dom.BoundaryCurves)
                {
                    const double Tau = ProjectOntoCurve(BCurve.Curve, PosPart, BCurve.Low);
                    double EdgeDist = std::min(
                        Distance(EvaluateCurve(BCurve.Curve, BCurve.Low).Position, PosPart),
                        Distance(EvaluateCurve(BCurve.Curve, BCurve.High).Position, PosPart));
                    if (Tau >= BCurve.Low && Tau <= BCurve.High)
                    {
                        EdgeDist = std::min(EdgeDist, Distance(EvaluateCurve(BCurve.Curve, Tau).Position, PosPart));
                    }
                    BodyUnsignedMeters = std::min(BodyUnsignedMeters, EdgeDist * ScaleMeters);
                }
            }
            if (BodyUnsignedMeters < 1.0e20)
            {
                const double BodySigned = (BodyInside ? -1.0 : 1.0) * BodyUnsignedMeters;
                BestSignedExactMeters = std::min(BestSignedExactMeters, BodySigned);
            }
        }

        if (BestSignedExactMeters < 1.0e20)
        {
            const float ExactSigned = static_cast<float>(BestSignedExactMeters);
            AbsErrors.push_back(std::abs(CompiledSigned - ExactSigned));
        }
        ++ValidCount;
    }

    if (ValidCount > 0u)
    {
        OutMetrics.SampledPoints        = ValidCount;
        OutMetrics.SignAgreementPercent = 100.0f * static_cast<float>(SignMatches) / static_cast<float>(ValidCount);
    }
    if (!AbsErrors.empty())
    {
        std::sort(AbsErrors.begin(), AbsErrors.end());
        double Sum = 0.0;
        for (float Err : AbsErrors) Sum += static_cast<double>(Err);
        OutMetrics.MeanAbsoluteErrorMeters = static_cast<float>(Sum / static_cast<double>(AbsErrors.size()));
        OutMetrics.Percentile95ErrorMeters = AbsErrors[std::min(AbsErrors.size() - 1u, (AbsErrors.size() * 95u) / 100u)];
        OutMetrics.Percentile99ErrorMeters = AbsErrors[std::min(AbsErrors.size() - 1u, (AbsErrors.size() * 99u) / 100u)];
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  PUBLIC COMPILER ENTRY
//------------------------------------------------------------------------------------------------------------------------

SolidArcCompiledBundle CompileSolidArcPartition(
    const ModelPartition&                   Partition,
    std::span<const BodyToken>              Bodies,
    std::span<const std::string>            BodyTitles,
    std::span<const StandaloneCurveInput>   StandaloneCurves,
    const SolidArcCompileSettings&          Settings)
{
    const auto StartClock = std::chrono::steady_clock::now();
    SolidArcCompiledBundle Bundle {};

    const double Scale = (Settings.UnitScaleToMeters > 0.0) ? Settings.UnitScaleToMeters : 1.0;

    // ① Standalone 3D curves (unconsumed sketches / construction curves) -> Embedded .Curve ('CRV1')
    for (const StandaloneCurveInput& Standalone : StandaloneCurves)
    {
        AppendCurveToPackage(
            Standalone.Curve,
            Standalone.IntervalLow,
            Standalone.IntervalHigh,
            0xFFFFu,
            0.0f,
            Scale,
            Bundle.CurvePackage);
    }

    if (Bodies.empty())
    {
        Bundle.Audit.CompileMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - StartClock).count();
        return Bundle;
    }

    GeometrySpace::PolygonNetwork& Net = Bundle.GeometryDocument.Network;
    Net.Reset();
    Net.NetworkTitle = (!BodyTitles.empty() && !BodyTitles[0].empty()) ? BodyTitles[0] : std::string("SolidArcCompiledBodies");

    // ② Tessellate each body into PolygonNetwork (.GeometrySpace) AND compile exact surfaces/trims/edges ('BSDF' + 'CRV1')
    std::vector<BodyToken> CompiledSolidBodies;
    for (std::size_t BodyIdx = 0u; BodyIdx < Bodies.size(); ++BodyIdx)
    {
        const BodyToken Body = Bodies[BodyIdx];
        if (!Body.PresentCondition() || !Partition.Bodies.Authenticate(Body))
        {
            continue;
        }

        const auto BodyOrd = static_cast<uint32_t>(BodyIdx);
        Codecs::GeometrySurfaceSlotLink Slot {};
        Slot.PartGroupOrdinal = BodyOrd;
        Slot.SlotTitle = (BodyIdx < BodyTitles.size() && !BodyTitles[BodyIdx].empty())
            ? BodyTitles[BodyIdx]
            : ("Body" + std::to_string(BodyIdx + 1u));
        Bundle.GeometryDocument.SurfaceSlots.push_back(Slot);

        // 2a. Watertight CDT tessellation -> PolygonNetwork + analytical corner normals + normalized chart UVs
        TessellationSettings TessCfg {};
        TessCfg.SagRatio = Settings.TessellationSagRatio;
        const TessellationOutcome Tess = TessellateBody(Partition, Body, TessCfg);
        if (!Tess.Refusal.empty())
        {
            Bundle.Refusal = "Body tessellation failed: " + Tess.Refusal;
            return Bundle;
        }

        const auto AlignedVertexOffset = static_cast<uint32_t>(Net.Vertices.size());
        for (const Vector3& Pos : Tess.Complex.Positions)
        {
            (void)Net.AppendVertex(ToMetersAxis3D(Pos, Scale));
        }

        // Precompute chart UV normalization windows per tessellated face
        std::vector<ParameterWindow> FaceWindows(Tess.Complex.Faces.size());
        for (std::size_t Fi = 0u; Fi < Tess.Complex.Faces.size(); ++Fi)
        {
            const PolygonNetFace& NetFace = Tess.Complex.Faces[Fi];
            Vector2 MinUV {  1.0e30,  1.0e30 };
            Vector2 MaxUV { -1.0e30, -1.0e30 };
            for (int Ti = 0; Ti < NetFace.TriangleCount; ++Ti)
            {
                const auto& Charts = Tess.Complex.TriangleCharts[static_cast<std::size_t>(NetFace.FirstTriangle + Ti)];
                for (const Vector2& C : Charts)
                {
                    MinUV.X = std::min(MinUV.X, C.X); MinUV.Y = std::min(MinUV.Y, C.Y);
                    MaxUV.X = std::max(MaxUV.X, C.X); MaxUV.Y = std::max(MaxUV.Y, C.Y);
                }
            }
            FaceWindows[Fi] = { MinUV, MaxUV };
        }

        GeometrySpace::FaceShadingAssignment DefaultShade {};
        for (std::size_t TriIdx = 0u; TriIdx < Tess.Complex.Triangles.size(); ++TriIdx)
        {
            const std::array<int, 3>& Tri = Tess.Complex.Triangles[TriIdx];
            const std::array<uint32_t, 3u> LoopVerts = {
                AlignedVertexOffset + static_cast<uint32_t>(Tri[0]),
                AlignedVertexOffset + static_cast<uint32_t>(Tri[1]),
                AlignedVertexOffset + static_cast<uint32_t>(Tri[2])
            };

            const int FaceSpanIdx = Tess.Complex.TriangleFaces[TriIdx];
            const PolygonNetFace& SpanFace = Tess.Complex.Faces[static_cast<std::size_t>(FaceSpanIdx)];
            const ParameterWindow& Win     = FaceWindows[static_cast<std::size_t>(FaceSpanIdx)];
            const double SpanU = std::max(Win.High.X - Win.Low.X, 1.0e-6);
            const double SpanV = std::max(Win.High.Y - Win.Low.Y, 1.0e-6);

            const auto& Charts = Tess.Complex.TriangleCharts[TriIdx];
            std::array<Mathematics::UVAxes, 3u> CornerUVs {};
            for (std::size_t C = 0u; C < 3u; ++C)
            {
                CornerUVs[C] = {
                    static_cast<float>((Charts[C].X - Win.Low.X) / SpanU),
                    static_cast<float>((Charts[C].Y - Win.Low.Y) / SpanV)
                };
            }

            (void)Net.AppendFace(LoopVerts, CornerUVs, DefaultShade, 0u, static_cast<uint16_t>(BodyOrd));

            // Analytical CAD outward normals per corner from exact SurfaceGeometry
            const FaceProfile& FProf = Partition.Faces.Resolve(SpanFace.Face);
            const SurfaceGeometry& Carrier = Partition.Surfaces.Resolve(FProf.Surface);
            const double SenseSign = (FProf.Sense == Orientation::Forward) ? 1.0 : -1.0;
            for (std::size_t C = 0u; C < 3u; ++C)
            {
                const Vector3 ExactN = EvaluateSurfaceNormal(Carrier, Charts[C]) * SenseSign;
                const Mathematics::Axis3D UnitN = ToUnitAxis3D(ExactN);
                Bundle.GeometryDocument.FaceCornerNormals.push_back(UnitN.X);
                Bundle.GeometryDocument.FaceCornerNormals.push_back(UnitN.Y);
                Bundle.GeometryDocument.FaceCornerNormals.push_back(UnitN.Z);
            }
        }

        // 2b. Exact CAD B-Rep SDF compilation for solid/closed bodies
        const BodyInventory Inventory = Partition.CollectInventory(Body);
        const std::unordered_map<uint32_t, Vector3> VertNormals = ComputeVertexPseudoNormals(Partition, Inventory);

        for (FaceToken Face : Inventory.Faces)
        {
            const FaceDomain FaceRegion = DescribeFaceDomain(Partition, Face);
            CompileSingleFace(
                FaceRegion,
                BodyOrd,
                Scale,
                Bundle.SurfaceField.TrimSegments2D,
                Bundle.SurfaceField.BezierPatches,
                Bundle.SurfaceField.Surfaces);
        }

        CompileBodyBoundaryEdges(
            Partition,
            Inventory,
            VertNormals,
            BodyOrd,
            Scale,
            Bundle.CurvePackage,
            Bundle.SurfaceField.ConicArcs3D,
            Bundle.SurfaceField.Edges);

        if (Partition.Bodies.Resolve(Body).Category == BodyCategory::Solid)
        {
            CompiledSolidBodies.push_back(Body);
        }
    }

    if (!Net.Faces.empty())
    {
        Net.LinkOppositeEdges(false);
        Net.RecomputeNormals();
        Net.MarkDihedralSharpSeams(35.0f, false);

        if (Settings.BuildSpatialStructures)
        {
            Bundle.GeometryDocument.WideTreeIncluded =
                Bundle.GeometryDocument.RayAcceleration.BuildFromPolygonNetwork(Net);
            Bundle.GeometryDocument.ClustersIncluded =
                Bundle.GeometryDocument.ClusterHierarchy.BuildFromPolygonNetwork(Net, 4u);
        }
    }

    // ③ Narrow-band macro-cell index + ready-to-use fp16 LocalSignedDistanceVolume ('VSDF') + WindingNumber audit
    if (!Bundle.SurfaceField.EmptyCondition())
    {
        BuildMacroCellIndex(Bundle.SurfaceField, Settings.TargetMacroGridBudget);

        if (Settings.BakeVoxelVolume)
        {
            BakeLocalDistanceVolumeFromField(Bundle.SurfaceField, Settings.TargetVoxelBudget, Bundle.BakedVolume);
        }

        if (Settings.AuditSampleCount > 0u && !CompiledSolidBodies.empty())
        {
            AuditCompiledSurfaceField(
                Partition,
                CompiledSolidBodies,
                Bundle.SurfaceField,
                Scale,
                Settings.AuditSampleCount,
                Bundle.Audit);
        }
    }

    Bundle.Audit.CompileMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - StartClock).count();
    return Bundle;
}

} // namespace Frontier::GeometrySpace::Parametric
