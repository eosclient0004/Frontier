/*============================================================================================================================================
                                                        COMPILEDSURFACEFIELD.CPP
============================================================================================================================================*/
// 🧩 Implementation of exact parametric B-rep SDF query ('BSDF'), embedded 3D curves ('CRV1') & fp16 volume ('VSDF') serialization

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/DistanceFieldSpace/CompiledSurfaceField.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace Frontier::DistanceFieldSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL ALGEBRA
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr float TwoPiFloat = 6.2831853071795864769f;
constexpr float PiFloat    = 3.1415926535897932384f;

[[nodiscard]] inline Mathematics::Axis3D LoadVec3(const std::array<float, 4u>& Source) noexcept
{
    return { Source[0], Source[1], Source[2] };
}

[[nodiscard]] inline Mathematics::Axis3D SubVec3(Mathematics::Axis3D Left, Mathematics::Axis3D Right) noexcept
{
    return { Left.X - Right.X, Left.Y - Right.Y, Left.Z - Right.Z };
}

[[nodiscard]] inline Mathematics::Axis3D AddVec3(Mathematics::Axis3D Left, Mathematics::Axis3D Right) noexcept
{
    return { Left.X + Right.X, Left.Y + Right.Y, Left.Z + Right.Z };
}

[[nodiscard]] inline Mathematics::Axis3D ScaleVec3(Mathematics::Axis3D ScalarInput, float Factor) noexcept
{
    return { ScalarInput.X * Factor, ScalarInput.Y * Factor, ScalarInput.Z * Factor };
}

[[nodiscard]] inline float DotVec3(Mathematics::Axis3D Left, Mathematics::Axis3D Right) noexcept
{
    return Left.X * Right.X + Left.Y * Right.Y + Left.Z * Right.Z;
}

[[nodiscard]] inline Mathematics::Axis3D CrossVec3(Mathematics::Axis3D Left, Mathematics::Axis3D Right) noexcept
{
    return {
        Left.Y * Right.Z - Left.Z * Right.Y,
        Left.Z * Right.X - Left.X * Right.Z,
        Left.X * Right.Y - Left.Y * Right.X
    };
}

[[nodiscard]] inline float LengthSquaredVec3(Mathematics::Axis3D ScalarInput) noexcept
{
    return DotVec3(ScalarInput, ScalarInput);
}

[[nodiscard]] inline float LengthVec3(Mathematics::Axis3D ScalarInput) noexcept
{
    return std::sqrt(LengthSquaredVec3(ScalarInput));
}

[[nodiscard]] inline Mathematics::Axis3D NormalizeSafeVec3(Mathematics::Axis3D ScalarInput) noexcept
{
    const float LenSq = LengthSquaredVec3(ScalarInput);
    if (LenSq <= 1.0e-24f)
    {
        return { 0.0f, 1.0f, 0.0f };
    }
    const float InvLen = 1.0f / std::sqrt(LenSq);
    return { ScalarInput.X * InvLen, ScalarInput.Y * InvLen, ScalarInput.Z * InvLen };
}

[[nodiscard]] inline float DistanceSquaredToBox(
    Mathematics::Axis3D Point,
    Mathematics::Axis3D BoundsLow,
    Mathematics::Axis3D BoundsHigh) noexcept
{
    const float Dx = std::max({ BoundsLow.X - Point.X, 0.0f, Point.X - BoundsHigh.X });
    const float Dy = std::max({ BoundsLow.Y - Point.Y, 0.0f, Point.Y - BoundsHigh.Y });
    const float Dz = std::max({ BoundsLow.Z - Point.Z, 0.0f, Point.Z - BoundsHigh.Z });
    return Dx * Dx + Dy * Dy + Dz * Dz;
}

[[nodiscard]] inline float WrapAngleSigned(float Radians) noexcept
{
    float Wrapped = std::fmod(Radians + PiFloat, TwoPiFloat);
    if (Wrapped < 0.0f)
    {
        Wrapped += TwoPiFloat;
    }
    return Wrapped - PiFloat;
}

[[nodiscard]] inline float AlignPeriodicCoordinate(float RawCoord, float Period, float WindowCenter) noexcept
{
    if (Period <= 0.0f)
    {
        return RawCoord;
    }
    const float Delta = RawCoord - WindowCenter;
    return RawCoord - Period * std::round(Delta / Period);
}

// 2D even-odd ray-crossing containment test supporting both straight segments and exact 2D circular arcs
[[nodiscard]] bool PointInsideParameterTrimSingle(
    float                                    ChartU,
    float                                    ChartV,
    std::span<const CompiledTrimSegment2D>   TrimSegments,
    uint32_t                                 FirstTrim,
    uint32_t                                 TrimCount) noexcept
{
    if (TrimCount == 0u)
    {
        return true;
    }

    bool InsideRegion = false;
    const uint32_t EndTrim = std::min<uint32_t>(FirstTrim + TrimCount, static_cast<uint32_t>(TrimSegments.size()));
    for (uint32_t Index = FirstTrim; Index < EndTrim; ++Index)
    {
        const CompiledTrimSegment2D& Seg = TrimSegments[Index];
        const bool ArcCondition = (Seg.Coords1[1] > 0.5f);
        if (!ArcCondition)
        {
            const float Ax = Seg.Coords0[0];
            const float Ay = Seg.Coords0[1];
            const float Bx = Seg.Coords0[2];
            const float By = Seg.Coords0[3];
            if ((Ay > ChartV) != (By > ChartV))
            {
                const float CrossU = Ax + (ChartV - Ay) * (Bx - Ax) / (By - Ay);
                if (ChartU < CrossU)
                {
                    InsideRegion = !InsideRegion;
                }
            }
        }
        else
        {
            const float CenterU  = Seg.Coords0[0];
            const float CenterV  = Seg.Coords0[1];
            const float Radius   = std::abs(Seg.Coords0[2]);
            const float StartRad = Seg.Coords0[3];
            const float SweepRad = Seg.Coords1[0];
            if (Radius <= 1.0e-12f || std::abs(SweepRad) <= 1.0e-7f)
            {
                continue;
            }
            // Full 360-degree circular hole/outer loop: exact 6-FLOP disk parity with zero vertex-junction ambiguity!
            if (std::abs(SweepRad) >= TwoPiFloat - 1.0e-4f)
            {
                const float Du = ChartU - CenterU;
                const float Dv = ChartV - CenterV;
                if ((Du * Du + Dv * Dv) < Radius * Radius)
                {
                    InsideRegion = !InsideRegion;
                }
                continue;
            }
            // Partial circular arc: strict half-open Jordan scanline rule (PrevV > ChartV) != (NextV > ChartV)
            float PrevU = CenterU + Radius * std::cos(StartRad);
            float PrevV = CenterV + Radius * std::sin(StartRad);
            for (int Step = 1; Step <= 16; ++Step)
            {
                const float Theta = StartRad + SweepRad * (static_cast<float>(Step) / 16.0f);
                const float NextU = CenterU + Radius * std::cos(Theta);
                const float NextV = CenterV + Radius * std::sin(Theta);
                if ((PrevV > ChartV) != (NextV > ChartV))
                {
                    const float CrossU = PrevU + (ChartV - PrevV) * (NextU - PrevU) / (NextV - PrevV);
                    if (ChartU < CrossU)
                    {
                        InsideRegion = !InsideRegion;
                    }
                }
                PrevU = NextU;
                PrevV = NextV;
            }
        }
    }
    return InsideRegion;
}

[[nodiscard]] bool PointInsideParameterTrimPeriodic(
    float                                  RawU,
    float                                  RawV,
    const CompiledSurfacePrimitive&        Prim,
    std::span<const CompiledTrimSegment2D> TrimSegments) noexcept
{
    const uint32_t FirstTrim = static_cast<uint32_t>(Prim.TrimSpanSenseBody[0]);
    const uint32_t TrimCount = static_cast<uint32_t>(Prim.TrimSpanSenseBody[1]);
    if (TrimCount == 0u)
    {
        return true;
    }

    const float PeriodU = Prim.AxisYPeriodU[3];
    const float PeriodV = Prim.AxisZPeriodV[3];
    const float MidU    = 0.5f * (Prim.ChartBounds[0] + Prim.ChartBounds[2]);
    const float MidV    = 0.5f * (Prim.ChartBounds[1] + Prim.ChartBounds[3]);
    const float AlignedU   = AlignPeriodicCoordinate(RawU, PeriodU, MidU);
    const float AlignedV   = AlignPeriodicCoordinate(RawV, PeriodV, MidV);

    const int ShiftMinU = (PeriodU > 0.0f) ? -1 : 0;
    const int ShiftMaxU = (PeriodU > 0.0f) ?  1 : 0;
    const int ShiftMinV = (PeriodV > 0.0f) ? -1 : 0;
    const int ShiftMaxV = (PeriodV > 0.0f) ?  1 : 0;

    for (int Du = ShiftMinU; Du <= ShiftMaxU; ++Du)
    {
        const float TestU = AlignedU + static_cast<float>(Du) * PeriodU;
        if (TestU < Prim.ChartBounds[0] - 1.0e-3f || TestU > Prim.ChartBounds[2] + 1.0e-3f)
        {
            continue;
        }
        for (int Dv = ShiftMinV; Dv <= ShiftMaxV; ++Dv)
        {
            const float TestV = AlignedV + static_cast<float>(Dv) * PeriodV;
            if (TestV < Prim.ChartBounds[1] - 1.0e-3f || TestV > Prim.ChartBounds[3] + 1.0e-3f)
            {
                continue;
            }
            if (PointInsideParameterTrimSingle(TestU, TestV, TrimSegments, FirstTrim, TrimCount))
            {
                return true;
            }
        }
    }
    return false;
}

struct RationalBezierEval
{
    Mathematics::Axis3D Position    {};
    Mathematics::Axis3D DerivativeU {};
    Mathematics::Axis3D DerivativeV {};
};

[[nodiscard]] RationalBezierEval EvaluateRationalBezier4x4(
    const CompiledBezierPatch4x4& Patch,
    float                         LocalU,
    float                         LocalV) noexcept
{
    const float U  = std::clamp(LocalU, 0.0f, 1.0f);
    const float V  = std::clamp(LocalV, 0.0f, 1.0f);
    const float Mu = 1.0f - U;
    const float Mv = 1.0f - V;

    const float Bu[4]  = { Mu * Mu * Mu, 3.0f * Mu * Mu * U, 3.0f * Mu * U * U, U * U * U };
    const float Bv[4]  = { Mv * Mv * Mv, 3.0f * Mv * Mv * V, 3.0f * Mv * V * V, V * V * V };
    const float Dbu[4] = { -3.0f * Mu * Mu, 3.0f * Mu * (1.0f - 3.0f * U), 3.0f * U * (2.0f - 3.0f * U), 3.0f * U * U };
    const float Dbv[4] = { -3.0f * Mv * Mv, 3.0f * Mv * (1.0f - 3.0f * V), 3.0f * V * (2.0f - 3.0f * V), 3.0f * V * V };

    Mathematics::Axis3D NumPos {};
    Mathematics::Axis3D NumDu  {};
    Mathematics::Axis3D NumDv  {};
    float               DenW   = 0.0f;
    float               DenDu  = 0.0f;
    float               DenDv  = 0.0f;

    for (uint32_t Row = 0u; Row < 4u; ++Row)
    {
        for (uint32_t Col = 0u; Col < 4u; ++Col)
        {
            const std::array<float, 4u>& PoleW = Patch.PolesWeighted[Row * 4u + Col];
            const float Weight = PoleW[3];
            const float Wuv    = Bu[Row]  * Bv[Col]  * Weight;
            const float Wdu    = Dbu[Row] * Bv[Col]  * Weight;
            const float Wdv    = Bu[Row]  * Dbv[Col] * Weight;

            NumPos.X += Wuv * PoleW[0]; NumPos.Y += Wuv * PoleW[1]; NumPos.Z += Wuv * PoleW[2];
            NumDu.X  += Wdu * PoleW[0]; NumDu.Y  += Wdu * PoleW[1]; NumDu.Z  += Wdu * PoleW[2];
            NumDv.X  += Wdv * PoleW[0]; NumDv.Y  += Wdv * PoleW[1]; NumDv.Z  += Wdv * PoleW[2];

            DenW  += Wuv;
            DenDu += Wdu;
            DenDv += Wdv;
        }
    }

    const float InvW   = 1.0f / std::max(DenW, 1.0e-12f);
    const Mathematics::Axis3D Pos = ScaleVec3(NumPos, InvW);
    const Mathematics::Axis3D Du  = ScaleVec3(SubVec3(NumDu, ScaleVec3(Pos, DenDu)), InvW);
    const Mathematics::Axis3D Dv  = ScaleVec3(SubVec3(NumDv, ScaleVec3(Pos, DenDv)), InvW);
    return { Pos, Du, Dv };
}

struct ProjectedSurfaceFoot
{
    bool                ValidCondition = false;
    float               DistanceMeters = 1.0e30f;
    float               ChartU         = 0.0f;
    float               ChartV         = 0.0f;
    Mathematics::Axis3D FootPoint      {};
    Mathematics::Axis3D OutwardNormal  { 0.0f, 1.0f, 0.0f };
};

[[nodiscard]] ProjectedSurfaceFoot ProjectPointOntoSurfacePrimitive(
    Mathematics::Axis3D                       Position,
    const CompiledSurfacePrimitive&           Prim,
    std::span<const CompiledBezierPatch4x4>   BezierPatches,
    uint32_t                                  GaussNewtonIterations) noexcept
{
    ProjectedSurfaceFoot Out {};
    const uint32_t RawCategory = std::bit_cast<uint32_t>(Prim.BoundsLowCategory[3]);
    const auto     ProjCategory = static_cast<SurfacePrimitiveCategory>(RawCategory);

    const Mathematics::Axis3D Origin = LoadVec3(Prim.OriginRadius0);
    const Mathematics::Axis3D AxisX  = LoadVec3(Prim.AxisXRadius1);
    const Mathematics::Axis3D AxisY  = LoadVec3(Prim.AxisYPeriodU);
    const Mathematics::Axis3D AxisZ  = LoadVec3(Prim.AxisZPeriodV);
    const float               Sense  = Prim.TrimSpanSenseBody[2];

    const Mathematics::Axis3D Rel = SubVec3(Position, Origin);
    const float LocalX = DotVec3(Rel, AxisX);
    const float LocalY = DotVec3(Rel, AxisY);
    const float LocalZ = DotVec3(Rel, AxisZ);

    if (ProjCategory == SurfacePrimitiveCategory::Plane)
    {
        Out.ValidCondition = true;
        Out.ChartU         = LocalX;
        Out.ChartV         = LocalY;
        Out.FootPoint      = SubVec3(Position, ScaleVec3(AxisZ, LocalZ));
        Out.DistanceMeters = std::abs(LocalZ);
        Out.OutwardNormal  = ScaleVec3(AxisZ, Sense);
        return Out;
    }

    if (ProjCategory == SurfacePrimitiveCategory::Cylinder)
    {
        const float Radius = Prim.OriginRadius0[3];
        const float Rho    = std::hypot(LocalX, LocalY);
        const Mathematics::Axis3D RadialDir = (Rho > 1.0e-12f)
            ? AddVec3(ScaleVec3(AxisX, LocalX / Rho), ScaleVec3(AxisY, LocalY / Rho))
            : AxisX;
        Out.ValidCondition = true;
        Out.ChartU         = (Rho > 1.0e-12f) ? std::atan2(LocalY, LocalX) : 0.0f;
        Out.ChartV         = LocalZ;
        Out.FootPoint      = AddVec3(Origin, AddVec3(ScaleVec3(RadialDir, Radius), ScaleVec3(AxisZ, LocalZ)));
        Out.DistanceMeters = LengthVec3(SubVec3(Position, Out.FootPoint));
        Out.OutwardNormal  = ScaleVec3(RadialDir, Sense);
        return Out;
    }

    if (ProjCategory == SurfacePrimitiveCategory::Cone)
    {
        const float OriginRadius = Prim.OriginRadius0[3];
        const float RadialSlope  = Prim.AxisXRadius1[3];
        const float Rho          = std::hypot(LocalX, LocalY);
        const float ParamV       = ((Rho - OriginRadius) * RadialSlope + LocalZ) / (1.0f + RadialSlope * RadialSlope);
        const float ConeRadius   = OriginRadius + ParamV * RadialSlope;
        const Mathematics::Axis3D RadialDir = (Rho > 1.0e-12f)
            ? AddVec3(ScaleVec3(AxisX, LocalX / Rho), ScaleVec3(AxisY, LocalY / Rho))
            : AxisX;

        Out.ValidCondition = true;
        Out.ChartU         = (Rho > 1.0e-12f) ? std::atan2(LocalY, LocalX) : 0.0f;
        Out.ChartV         = ParamV;
        Out.FootPoint      = AddVec3(Origin, AddVec3(ScaleVec3(RadialDir, ConeRadius), ScaleVec3(AxisZ, ParamV)));
        Out.DistanceMeters = LengthVec3(SubVec3(Position, Out.FootPoint));
        const float OrientSign = (ConeRadius >= 0.0f) ? Sense : -Sense;
        Out.OutwardNormal = ScaleVec3(
            NormalizeSafeVec3(SubVec3(RadialDir, ScaleVec3(AxisZ, RadialSlope))),
            OrientSign);
        return Out;
    }

    if (ProjCategory == SurfacePrimitiveCategory::Sphere)
    {
        const float Radius = Prim.OriginRadius0[3];
        const float DistSq = LocalX * LocalX + LocalY * LocalY + LocalZ * LocalZ;
        const float Dist   = std::sqrt(DistSq);
        const Mathematics::Axis3D UnitDir = (Dist > 1.0e-12f) ? ScaleVec3(Rel, 1.0f / Dist) : AxisZ;
        const float ClampedSinV = std::clamp((Dist > 1.0e-12f) ? (LocalZ / Dist) : 1.0f, -1.0f, 1.0f);

        Out.ValidCondition = true;
        Out.ChartU         = (std::hypot(LocalX, LocalY) > 1.0e-12f) ? std::atan2(LocalY, LocalX) : 0.0f;
        Out.ChartV         = std::asin(ClampedSinV);
        Out.FootPoint      = AddVec3(Origin, ScaleVec3(UnitDir, Radius));
        Out.DistanceMeters = LengthVec3(SubVec3(Position, Out.FootPoint));
        Out.OutwardNormal  = ScaleVec3(UnitDir, Sense);
        return Out;
    }

    if (ProjCategory == SurfacePrimitiveCategory::Torus)
    {
        const float MajorRadius = Prim.OriginRadius0[3];
        const float MinorRadius = Prim.AxisXRadius1[3];
        const float Rho         = std::hypot(LocalX, LocalY);
        const Mathematics::Axis3D MajorDir = (Rho > 1.0e-12f)
            ? AddVec3(ScaleVec3(AxisX, LocalX / Rho), ScaleVec3(AxisY, LocalY / Rho))
            : AxisX;
        const Mathematics::Axis3D RingCenter = AddVec3(Origin, ScaleVec3(MajorDir, MajorRadius));
        const float TubeRho = Rho - MajorRadius;
        const float ParamV  = std::atan2(LocalZ, TubeRho);
        const Mathematics::Axis3D TubeDir = NormalizeSafeVec3(SubVec3(Position, RingCenter));

        Out.ValidCondition = true;
        Out.ChartU         = (Rho > 1.0e-12f) ? std::atan2(LocalY, LocalX) : 0.0f;
        Out.ChartV         = ParamV;
        Out.FootPoint      = AddVec3(RingCenter, ScaleVec3(TubeDir, MinorRadius));
        Out.DistanceMeters = LengthVec3(SubVec3(Position, Out.FootPoint));
        Out.OutwardNormal  = ScaleVec3(TubeDir, Sense);
        return Out;
    }

    // RationalBezier4x4: bilinear plane warm-start + 5-point guard + damped 2x2 Gauss-Newton
    const uint32_t PatchIndex = std::bit_cast<uint32_t>(Prim.BoundsHighPatch[3]);
    if (PatchIndex >= BezierPatches.size())
    {
        return Out;
    }
    const CompiledBezierPatch4x4& Patch = BezierPatches[PatchIndex];

    // Bilinear plane seed from the 4 corners P00, P30, P03, P33
    const Mathematics::Axis3D P00 = LoadVec3(Patch.PolesWeighted[0u]);
    const Mathematics::Axis3D P30 = LoadVec3(Patch.PolesWeighted[12u]);
    const Mathematics::Axis3D P03 = LoadVec3(Patch.PolesWeighted[3u]);
    const Mathematics::Axis3D P33 = LoadVec3(Patch.PolesWeighted[15u]);
    const Mathematics::Axis3D EdgeU = ScaleVec3(AddVec3(SubVec3(P30, P00), SubVec3(P33, P03)), 0.5f);
    const Mathematics::Axis3D EdgeV = ScaleVec3(AddVec3(SubVec3(P03, P00), SubVec3(P33, P30)), 0.5f);
    const Mathematics::Axis3D RelCorner = SubVec3(Position, P00);

    float BestU = std::clamp(DotVec3(RelCorner, EdgeU) / std::max(LengthSquaredVec3(EdgeU), 1.0e-12f), 0.05f, 0.95f);
    float BestV = std::clamp(DotVec3(RelCorner, EdgeV) / std::max(LengthSquaredVec3(EdgeV), 1.0e-12f), 0.05f, 0.95f);
    float BestDistSq = LengthSquaredVec3(SubVec3(EvaluateRationalBezier4x4(Patch, BestU, BestV).Position, Position));

    // Fast 4-quadrant interior guard for strongly curved blades / involute flanks
    constexpr float GuardSeeds[4][2] = { { 0.25f, 0.25f }, { 0.75f, 0.25f }, { 0.25f, 0.75f }, { 0.75f, 0.75f } };
    for (const auto& Seed : GuardSeeds)
    {
        const RationalBezierEval Sample = EvaluateRationalBezier4x4(Patch, Seed[0], Seed[1]);
        const float DistSq = LengthSquaredVec3(SubVec3(Sample.Position, Position));
        if (DistSq < BestDistSq)
        {
            BestDistSq = DistSq;
            BestU      = Seed[0];
            BestV      = Seed[1];
        }
    }

    float CurU = BestU;
    float CurV = BestV;
    for (uint32_t Iter = 0u; Iter < GaussNewtonIterations; ++Iter)
    {
        const RationalBezierEval Eval = EvaluateRationalBezier4x4(Patch, CurU, CurV);
        const Mathematics::Axis3D Diff = SubVec3(Eval.Position, Position);
        const float J11 = DotVec3(Eval.DerivativeU, Eval.DerivativeU) + 1.0e-8f;
        const float J12 = DotVec3(Eval.DerivativeU, Eval.DerivativeV);
        const float J22 = DotVec3(Eval.DerivativeV, Eval.DerivativeV) + 1.0e-8f;
        const float Gu  = DotVec3(Diff, Eval.DerivativeU);
        const float Gv  = DotVec3(Diff, Eval.DerivativeV);
        const float Det = J11 * J22 - J12 * J12;
        if (std::abs(Det) <= 1.0e-16f)
        {
            break;
        }
        const float StepU = std::clamp(-( J22 * Gu - J12 * Gv) / Det, -0.45f, 0.45f);
        const float StepV = std::clamp(-(-J12 * Gu + J11 * Gv) / Det, -0.45f, 0.45f);
        CurU = std::clamp(CurU + StepU, 0.0f, 1.0f);
        CurV = std::clamp(CurV + StepV, 0.0f, 1.0f);
    }

    const RationalBezierEval FinalEval = EvaluateRationalBezier4x4(Patch, CurU, CurV);
    Out.ValidCondition = true;
    Out.ChartU         = Prim.ChartBounds[0] + CurU * (Prim.ChartBounds[2] - Prim.ChartBounds[0]);
    Out.ChartV         = Prim.ChartBounds[1] + CurV * (Prim.ChartBounds[3] - Prim.ChartBounds[1]);
    Out.FootPoint      = FinalEval.Position;
    Out.DistanceMeters = LengthVec3(SubVec3(Position, FinalEval.Position));
    Out.OutwardNormal  = ScaleVec3(NormalizeSafeVec3(CrossVec3(FinalEval.DerivativeU, FinalEval.DerivativeV)), Sense);
    return Out;
}

struct ProjectedEdgeFoot
{
    float               DistanceMeters = 1.0e30f;
    Mathematics::Axis3D FootPoint      {};
    Mathematics::Axis3D PseudoNormal   { 0.0f, 1.0f, 0.0f };
    uint32_t            BodyOrdinal    = 0u;
};

[[nodiscard]] ProjectedEdgeFoot ProjectPointOntoEdgePrimitive(
    Mathematics::Axis3D                     Position,
    const CompiledPseudoNormalEdge&         Edge,
    std::span<const CompiledConicArc3D>     ConicArcs,
    bool                                    EnablePseudoNormals) noexcept
{
    ProjectedEdgeFoot Out {};
    Out.BodyOrdinal = static_cast<uint32_t>(Edge.StartVertexNormal[3]);

    const uint32_t RawCategory = std::bit_cast<uint32_t>(Edge.StartPointCategory[3]);
    const auto     ProjCategory = static_cast<EdgeProjectionCategory>(RawCategory);

    const Mathematics::Axis3D PointA  = LoadVec3(Edge.StartPointCategory);
    const Mathematics::Axis3D PointB  = LoadVec3(Edge.EndPointArcOrdinal);
    const Mathematics::Axis3D NormE   = LoadVec3(Edge.EdgePseudoNormal);
    const Mathematics::Axis3D NormVA  = LoadVec3(Edge.StartVertexNormal);
    const Mathematics::Axis3D NormVB  = LoadVec3(Edge.EndVertexNormal);

    float NormalizedParam = 0.5f;

    if (ProjCategory == EdgeProjectionCategory::StraightSegment3D)
    {
        const Mathematics::Axis3D Seg = SubVec3(PointB, PointA);
        const float Denom = std::max(LengthSquaredVec3(Seg), 1.0e-18f);
        NormalizedParam = std::clamp(DotVec3(SubVec3(Position, PointA), Seg) / Denom, 0.0f, 1.0f);
        Out.FootPoint   = AddVec3(PointA, ScaleVec3(Seg, NormalizedParam));
    }
    else
    {
        const uint32_t ArcIdx = std::bit_cast<uint32_t>(Edge.EndPointArcOrdinal[3]);
        if (ArcIdx < ConicArcs.size())
        {
            const CompiledConicArc3D& Arc    = ConicArcs[ArcIdx];
            const Mathematics::Axis3D Centre = LoadVec3(Arc.CentreStartAngle);
            const Mathematics::Axis3D CosAx  = LoadVec3(Arc.CosineAxisSweep);
            const Mathematics::Axis3D SinAx  = LoadVec3(Arc.SineAxisMajor);
            const float StartAngle           = Arc.CentreStartAngle[3];
            const float SweepAngle           = Arc.CosineAxisSweep[3];
            const float MajorR               = std::max(Arc.SineAxisMajor[3], 1.0e-9f);
            const float MinorR               = std::max(Arc.PlaneNormalMinor[3], 1.0e-9f);

            const Mathematics::Axis3D Rel = SubVec3(Position, Centre);
            const float ProjX = DotVec3(Rel, CosAx) / (MajorR * MajorR);
            const float ProjY = DotVec3(Rel, SinAx) / (MinorR * MinorR);
            float Angle = std::atan2(ProjY, ProjX);

            // 2 Newton polish iterations when MajorR != MinorR (exact in 0 iterations for circles)
            if (std::abs(MajorR - MinorR) > 1.0e-6f * MajorR)
            {
                for (int Iter = 0; Iter < 2; ++Iter)
                {
                    const float Ca = std::cos(Angle);
                    const float Sa = std::sin(Angle);
                    const Mathematics::Axis3D CurvePt = AddVec3(Centre, AddVec3(ScaleVec3(CosAx, Ca), ScaleVec3(SinAx, Sa)));
                    const Mathematics::Axis3D Tang    = AddVec3(ScaleVec3(CosAx, -Sa), ScaleVec3(SinAx, Ca));
                    const Mathematics::Axis3D Accel   = AddVec3(ScaleVec3(CosAx, -Ca), ScaleVec3(SinAx, -Sa));
                    const Mathematics::Axis3D Diff    = SubVec3(CurvePt, Position);
                    const float Grad = DotVec3(Diff, Tang);
                    const float Hess = DotVec3(Tang, Tang) + DotVec3(Diff, Accel);
                    if (std::abs(Hess) > 1.0e-14f)
                    {
                        Angle -= std::clamp(Grad / Hess, -0.5f, 0.5f);
                    }
                }
            }

            if (std::abs(SweepAngle) >= TwoPiFloat - 1.0e-4f)
            {
                NormalizedParam = 0.5f;
            }
            else
            {
                const float MidAngle   = StartAngle + 0.5f * SweepAngle;
                const float HalfSweep  = 0.5f * std::abs(SweepAngle);
                const float RelWrapped = WrapAngleSigned(Angle - MidAngle);
                if (std::abs(RelWrapped) <= HalfSweep)
                {
                    const float SignedOffset = (SweepAngle >= 0.0f) ? (RelWrapped + HalfSweep) : (HalfSweep - RelWrapped);
                    NormalizedParam = std::clamp(SignedOffset / std::max(std::abs(SweepAngle), 1.0e-9f), 0.0f, 1.0f);
                }
                else
                {
                    const float DistA = LengthSquaredVec3(SubVec3(Position, PointA));
                    const float DistB = LengthSquaredVec3(SubVec3(Position, PointB));
                    if (DistA <= DistB)
                    {
                        NormalizedParam = 0.0f;
                        Angle           = StartAngle;
                    }
                    else
                    {
                        NormalizedParam = 1.0f;
                        Angle           = StartAngle + SweepAngle;
                    }
                }
            }

            Out.FootPoint = AddVec3(Centre, AddVec3(ScaleVec3(CosAx, std::cos(Angle)), ScaleVec3(SinAx, std::sin(Angle))));
        }
        else
        {
            Out.FootPoint = PointA;
        }
    }

    Out.DistanceMeters = LengthVec3(SubVec3(Position, Out.FootPoint));
    if (EnablePseudoNormals)
    {
        if (NormalizedParam <= 1.0e-3f)
        {
            Out.PseudoNormal = NormVA;
        }
        else if (NormalizedParam >= 0.999f)
        {
            Out.PseudoNormal = NormVB;
        }
        else
        {
            Out.PseudoNormal = NormE;
        }
    }
    else
    {
        // Deliberate naive single-face normal fallback for diagnostic stress comparisons
        Out.PseudoNormal = NormalizeSafeVec3({ NormE.X * Edge.EndVertexNormal[3], NormE.Y, NormE.Z });
    }
    return Out;
}

template <typename PodT>
void AppendPodBytes(std::vector<std::byte>& Buffer, const PodT& ScalarInput)
{
    const std::size_t OldSize = Buffer.size();
    Buffer.resize(OldSize + sizeof(PodT));
    std::memcpy(Buffer.data() + OldSize, &ScalarInput, sizeof(PodT));
}

template <typename PodT>
void AppendPodVector(std::vector<std::byte>& Buffer, std::span<const PodT> Elements)
{
    if (Elements.empty())
    {
        return;
    }
    const std::size_t ByteCount = Elements.size() * sizeof(PodT);
    const std::size_t OldSize   = Buffer.size();
    Buffer.resize(OldSize + ByteCount);
    std::memcpy(Buffer.data() + OldSize, Elements.data(), ByteCount);
}

template <typename PodT>
[[nodiscard]] bool ReadPodBytes(std::span<const std::byte>& Cursor, PodT& OutDecoded) noexcept
{
    if (Cursor.size() < sizeof(PodT))
    {
        return false;
    }
    std::memcpy(&OutDecoded, Cursor.data(), sizeof(PodT));
    Cursor = Cursor.subspan(sizeof(PodT));
    return true;
}

template <typename PodT>
[[nodiscard]] bool ReadPodVector(std::span<const std::byte>& Cursor, uint32_t Count, std::vector<PodT>& OutVec)
{
    const std::size_t ByteCount = static_cast<std::size_t>(Count) * sizeof(PodT);
    if (Cursor.size() < ByteCount)
    {
        return false;
    }
    OutVec.resize(Count);
    if (ByteCount > 0u)
    {
        std::memcpy(OutVec.data(), Cursor.data(), ByteCount);
        Cursor = Cursor.subspan(ByteCount);
    }
    return true;
}

[[nodiscard]] SurfaceDistanceSample FinalizeHollowWallThickness(
    SurfaceDistanceSample            Sample,
    const SurfaceFieldQuerySettings& Settings) noexcept
{
    if (Settings.HollowWallMeters > 1.0e-7f)
    {
        const float OriginalSign = (Sample.SignedDistanceMeters < 0.0f) ? -1.0f : 1.0f;
        Sample.SignedDistanceMeters = std::abs(Sample.SignedDistanceMeters) - 0.5f * Settings.HollowWallMeters;
        Sample.OutwardGradient      = ScaleVec3(Sample.OutwardGradient, OriginalSign);
    }
    return Sample;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                             IEEE-754 BINARY16 CONVERSIONS
//------------------------------------------------------------------------------------------------------------------------

uint16_t PackFloat32ToBinary16(float ScalarInput) noexcept
{
    const uint32_t Bits     = std::bit_cast<uint32_t>(ScalarInput);
    const uint32_t Sign     = (Bits >> 16u) & 0x8000u;
    const int32_t  Exponent = static_cast<int32_t>((Bits >> 23u) & 0xFFu) - 127 + 15;
    uint32_t       Mantissa = Bits & 0x007FFFFFu;

    if (Exponent <= 0)
    {
        if (Exponent < -10)
        {
            return static_cast<uint16_t>(Sign);
        }
        Mantissa = (Mantissa | 0x00800000u) >> static_cast<uint32_t>(1 - Exponent);
        return static_cast<uint16_t>(Sign | ((Mantissa + 0x00001000u) >> 13u));
    }
    if (Exponent >= 31)
    {
        return static_cast<uint16_t>(Sign | 0x7BFFu);
    }
    return static_cast<uint16_t>(Sign | (static_cast<uint32_t>(Exponent) << 10u) | ((Mantissa + 0x00001000u) >> 13u));
}

float UnpackBinary16ToFloat32(uint16_t HalfWord) noexcept
{
    const uint32_t Sign     = (static_cast<uint32_t>(HalfWord & 0x8000u)) << 16u;
    const uint32_t Exponent = (static_cast<uint32_t>(HalfWord >> 10u)) & 0x1Fu;
    uint32_t       Mantissa = static_cast<uint32_t>(HalfWord & 0x03FFu);

    if (Exponent == 0u)
    {
        if (Mantissa == 0u)
        {
            return std::bit_cast<float>(Sign);
        }
        uint32_t ExpShift = 127u - 14u;
        while ((Mantissa & 0x0400u) == 0u)
        {
            Mantissa <<= 1u;
            --ExpShift;
        }
        Mantissa &= 0x03FFu;
        return std::bit_cast<float>(Sign | (ExpShift << 23u) | (Mantissa << 13u));
    }
    if (Exponent == 31u)
    {
        return std::bit_cast<float>(Sign | 0x7F800000u | (Mantissa << 13u));
    }
    return std::bit_cast<float>(Sign | ((Exponent + 127u - 15u) << 23u) | (Mantissa << 13u));
}

//------------------------------------------------------------------------------------------------------------------------
//                                           PARAMETRIC CURVE PACKAGE METHODS
//------------------------------------------------------------------------------------------------------------------------

void ParametricCurvePackage::Reset() noexcept
{
    Curves.clear();
    BezierPolesWeighted.clear();
}

std::size_t ParametricCurvePackage::QueryMemoryBytes() const noexcept
{
    return Curves.size() * sizeof(ParametricCurveEntry) +
           BezierPolesWeighted.size() * sizeof(std::array<float, 4u>);
}

//------------------------------------------------------------------------------------------------------------------------
//                                             COMPILED SURFACE FIELD METHODS
//------------------------------------------------------------------------------------------------------------------------

void CompiledSurfaceField::Reset() noexcept
{
    GridBoundsMin   = {};
    GridBoundsMax   = {};
    MacroDimensions = { 1u, 1u, 1u };
    CellRadiusMeters = 0.0f;
    EdgeTieTolerance = 1.0e-5f;
    Surfaces.clear();
    TrimSegments2D.clear();
    BezierPatches.clear();
    ConicArcs3D.clear();
    Edges.clear();
    MacroCells.clear();
    CellSurfaceIndices.clear();
    CellEdgeIndices.clear();
}

std::size_t CompiledSurfaceField::QueryGpuPayloadBytes() const noexcept
{
    return Surfaces.size()           * sizeof(CompiledSurfacePrimitive) +
           TrimSegments2D.size()     * sizeof(CompiledTrimSegment2D) +
           BezierPatches.size()      * sizeof(CompiledBezierPatch4x4) +
           ConicArcs3D.size()        * sizeof(CompiledConicArc3D) +
           Edges.size()              * sizeof(CompiledPseudoNormalEdge) +
           MacroCells.size()         * sizeof(CompiledMacroCell) +
           CellSurfaceIndices.size() * sizeof(uint32_t) +
           CellEdgeIndices.size()    * sizeof(uint32_t);
}

struct PerBodyCandidateSlot
{
    uint32_t                 BodyOrdinal   = 0xFFFFFFFFu;
    float                    BestFaceDist  = 1.0e30f;
    Mathematics::Axis3D      BestFaceFoot  {};
    Mathematics::Axis3D      BestFaceNorm  { 0.0f, 1.0f, 0.0f };
    SurfacePrimitiveCategory BestFaceCategory  = SurfacePrimitiveCategory::Plane;
    float                    BestEdgeDist  = 1.0e30f;
    Mathematics::Axis3D      BestEdgeFoot  {};
    Mathematics::Axis3D      BestEdgeNorm  { 0.0f, 1.0f, 0.0f };
};

struct PerBodyCandidateAccumulator
{
    static constexpr std::size_t MaxTrackedBodies = 16u;
    std::array<PerBodyCandidateSlot, MaxTrackedBodies> Slots {};
    std::size_t ActiveCount = 0u;

    PerBodyCandidateSlot& AcquireSlot(uint32_t BodyOrdinal) noexcept
    {
        for (std::size_t Idx = 0u; Idx < ActiveCount; ++Idx)
        {
            if (Slots[Idx].BodyOrdinal == BodyOrdinal)
            {
                return Slots[Idx];
            }
        }
        if (ActiveCount < MaxTrackedBodies)
        {
            PerBodyCandidateSlot& Fresh = Slots[ActiveCount++];
            Fresh.BodyOrdinal = BodyOrdinal;
            return Fresh;
        }
        return Slots[MaxTrackedBodies - 1u];
    }

    [[nodiscard]] bool EmptyCondition() const noexcept
    {
        for (std::size_t Idx = 0u; Idx < ActiveCount; ++Idx)
        {
            if (Slots[Idx].BestFaceDist < 1.0e20f || Slots[Idx].BestEdgeDist < 1.0e20f)
            {
                return false;
            }
        }
        return true;
    }

    void ResolveExactUnion(
        Mathematics::Axis3D    PositionMeters,
        float                  EdgeTieTolerance,
        SurfaceDistanceSample& InOutResult) const noexcept
    {
        float BestSigned = 1.0e30f;
        for (std::size_t Idx = 0u; Idx < ActiveCount; ++Idx)
        {
            const PerBodyCandidateSlot& Slot = Slots[Idx];
            if (Slot.BestFaceDist >= 1.0e20f && Slot.BestEdgeDist >= 1.0e20f)
            {
                continue;
            }
            const bool ChooseFace = (Slot.BestFaceDist <= Slot.BestEdgeDist + EdgeTieTolerance);
            const float UnsignedD = std::min(Slot.BestFaceDist, Slot.BestEdgeDist);
            const Mathematics::Axis3D Foot = ChooseFace ? Slot.BestFaceFoot : Slot.BestEdgeFoot;
            const Mathematics::Axis3D Norm = ChooseFace ? Slot.BestFaceNorm : Slot.BestEdgeNorm;
            const Mathematics::Axis3D Delta = SubVec3(PositionMeters, Foot);
            const float SignVal = (DotVec3(Delta, Norm) < 0.0f) ? -1.0f : 1.0f;
            const float SignedD = SignVal * UnsignedD;
            if (SignedD < BestSigned)
            {
                BestSigned                     = SignedD;
                InOutResult.SignedDistanceMeters = SignedD;
                InOutResult.ClosestSurfacePoint  = Foot;
                InOutResult.WinningCategory      = Slot.BestFaceCategory;
                InOutResult.WinningBodyOrdinal   = Slot.BodyOrdinal;
                InOutResult.OutwardGradient      = (UnsignedD > 1.0e-7f)
                    ? ScaleVec3(NormalizeSafeVec3(Delta), SignVal)
                    : Norm;
            }
        }
    }
};

SurfaceDistanceSample CompiledSurfaceField::QueryExactUnaccelerated(
    Mathematics::Axis3D              PositionMeters,
    const SurfaceFieldQuerySettings& Settings) const noexcept
{
    SurfaceDistanceSample Result {};
    if (Surfaces.empty())
    {
        return Result;
    }

    PerBodyCandidateAccumulator BodyAccum {};

    for (const CompiledSurfacePrimitive& Prim : Surfaces)
    {
        const uint32_t BodyOrd = static_cast<uint32_t>(Prim.TrimSpanSenseBody[3] + 0.5f);
        PerBodyCandidateSlot& Slot = BodyAccum.AcquireSlot(BodyOrd);

        const Mathematics::Axis3D Low  = LoadVec3(Prim.BoundsLowCategory);
        const Mathematics::Axis3D High = LoadVec3(Prim.BoundsHighPatch);
        if (DistanceSquaredToBox(PositionMeters, Low, High) >= Slot.BestFaceDist * Slot.BestFaceDist)
        {
            continue;
        }

        ++Result.TestedPrimitives;
        const ProjectedSurfaceFoot Proj = ProjectPointOntoSurfacePrimitive(
            PositionMeters,
            Prim,
            BezierPatches,
            Settings.GaussNewtonIterations);
        if (!Proj.ValidCondition || Proj.DistanceMeters >= Slot.BestFaceDist)
        {
            continue;
        }
        if (!Settings.EnableParameterTrim || PointInsideParameterTrimPeriodic(Proj.ChartU, Proj.ChartV, Prim, TrimSegments2D))
        {
            Slot.BestFaceDist = Proj.DistanceMeters;
            Slot.BestFaceFoot = Proj.FootPoint;
            Slot.BestFaceNorm = Proj.OutwardNormal;
            Slot.BestFaceCategory = static_cast<SurfacePrimitiveCategory>(std::bit_cast<uint32_t>(Prim.BoundsLowCategory[3]));
        }
    }

    for (const CompiledPseudoNormalEdge& Edge : Edges)
    {
        const uint32_t BodyOrd = static_cast<uint32_t>(Edge.StartVertexNormal[3] + 0.5f);
        PerBodyCandidateSlot& Slot = BodyAccum.AcquireSlot(BodyOrd);
        ++Result.TestedPrimitives;
        const ProjectedEdgeFoot EdgeProj = ProjectPointOntoEdgePrimitive(
            PositionMeters,
            Edge,
            ConicArcs3D,
            Settings.EnablePseudoNormals);
        if (EdgeProj.DistanceMeters < Slot.BestEdgeDist)
        {
            Slot.BestEdgeDist = EdgeProj.DistanceMeters;
            Slot.BestEdgeFoot = EdgeProj.FootPoint;
            Slot.BestEdgeNorm = EdgeProj.PseudoNormal;
        }
    }

    BodyAccum.ResolveExactUnion(PositionMeters, EdgeTieTolerance, Result);
    return FinalizeHollowWallThickness(Result, Settings);
}

SurfaceDistanceSample CompiledSurfaceField::QuerySignedDistance(
    Mathematics::Axis3D              PositionMeters,
    const SurfaceFieldQuerySettings& Settings) const noexcept
{
    if (!Settings.EnableMacroCellSkip || MacroCells.empty())
    {
        return QueryExactUnaccelerated(PositionMeters, Settings);
    }

    const float BoxDistSq = DistanceSquaredToBox(PositionMeters, GridBoundsMin, GridBoundsMax);
    if (BoxDistSq > 0.0f)
    {
        SurfaceDistanceSample Exterior {};
        Exterior.SignedDistanceMeters = std::sqrt(BoxDistSq) + 0.005f;
        const Mathematics::Axis3D Clamped {
            std::clamp(PositionMeters.X, GridBoundsMin.X, GridBoundsMax.X),
            std::clamp(PositionMeters.Y, GridBoundsMin.Y, GridBoundsMax.Y),
            std::clamp(PositionMeters.Z, GridBoundsMin.Z, GridBoundsMax.Z)
        };
        Exterior.ClosestSurfacePoint = Clamped;
        Exterior.OutwardGradient     = NormalizeSafeVec3(SubVec3(PositionMeters, Clamped));
        return FinalizeHollowWallThickness(Exterior, Settings);
    }

    const Mathematics::Axis3D Span = SubVec3(GridBoundsMax, GridBoundsMin);
    const float RelX = std::clamp((PositionMeters.X - GridBoundsMin.X) / std::max(Span.X, 1.0e-9f), 0.0f, 0.999999f);
    const float RelY = std::clamp((PositionMeters.Y - GridBoundsMin.Y) / std::max(Span.Y, 1.0e-9f), 0.0f, 0.999999f);
    const float RelZ = std::clamp((PositionMeters.Z - GridBoundsMin.Z) / std::max(Span.Z, 1.0e-9f), 0.0f, 0.999999f);

    const uint32_t Mx = MacroDimensions[0];
    const uint32_t My = MacroDimensions[1];
    const uint32_t Mz = MacroDimensions[2];
    const uint32_t Ix = std::min(static_cast<uint32_t>(RelX * static_cast<float>(Mx)), Mx - 1u);
    const uint32_t Iy = std::min(static_cast<uint32_t>(RelY * static_cast<float>(My)), My - 1u);
    const uint32_t Iz = std::min(static_cast<uint32_t>(RelZ * static_cast<float>(Mz)), Mz - 1u);
    const uint32_t CellOrdinal = (Iz * My + Iy) * Mx + Ix;

    const CompiledMacroCell& Cell = MacroCells[CellOrdinal];
    if (Cell.PackedCounts == 0u)
    {
        const float CenterDist = std::bit_cast<float>(Cell.CenterDistanceBits);
        const float SignVal    = (CenterDist < 0.0f) ? -1.0f : 1.0f;
        const float SafeBound  = std::max(std::abs(CenterDist) - CellRadiusMeters, CellRadiusMeters * 0.30f);
        SurfaceDistanceSample SkipSample {};
        SkipSample.SignedDistanceMeters = SignVal * SafeBound;
        return FinalizeHollowWallThickness(SkipSample, Settings);
    }

    const uint32_t SurfaceStart = Cell.FirstSurfaceEntry;
    const uint32_t SurfaceCount = Cell.PackedCounts & 0xFFFFu;
    const uint32_t EdgeStart    = Cell.FirstEdgeEntry;
    const uint32_t EdgeCount    = Cell.PackedCounts >> 16u;

    SurfaceDistanceSample Result {};
    PerBodyCandidateAccumulator BodyAccum {};

    for (uint32_t Offset = 0u; Offset < SurfaceCount; ++Offset)
    {
        const uint32_t PrimIndex = CellSurfaceIndices[SurfaceStart + Offset];
        const CompiledSurfacePrimitive& Prim = Surfaces[PrimIndex];
        const uint32_t BodyOrd = static_cast<uint32_t>(Prim.TrimSpanSenseBody[3] + 0.5f);
        PerBodyCandidateSlot& Slot = BodyAccum.AcquireSlot(BodyOrd);

        const Mathematics::Axis3D Low  = LoadVec3(Prim.BoundsLowCategory);
        const Mathematics::Axis3D High = LoadVec3(Prim.BoundsHighPatch);
        if (DistanceSquaredToBox(PositionMeters, Low, High) >= Slot.BestFaceDist * Slot.BestFaceDist)
        {
            continue;
        }

        ++Result.TestedPrimitives;
        const ProjectedSurfaceFoot Proj = ProjectPointOntoSurfacePrimitive(
            PositionMeters,
            Prim,
            BezierPatches,
            Settings.GaussNewtonIterations);
        if (!Proj.ValidCondition || Proj.DistanceMeters >= Slot.BestFaceDist)
        {
            continue;
        }
        if (!Settings.EnableParameterTrim || PointInsideParameterTrimPeriodic(Proj.ChartU, Proj.ChartV, Prim, TrimSegments2D))
        {
            Slot.BestFaceDist = Proj.DistanceMeters;
            Slot.BestFaceFoot = Proj.FootPoint;
            Slot.BestFaceNorm = Proj.OutwardNormal;
            Slot.BestFaceCategory = static_cast<SurfacePrimitiveCategory>(std::bit_cast<uint32_t>(Prim.BoundsLowCategory[3]));
        }
    }

    for (uint32_t Offset = 0u; Offset < EdgeCount; ++Offset)
    {
        const uint32_t EdgeIndex = CellEdgeIndices[EdgeStart + Offset];
        const uint32_t BodyOrd = static_cast<uint32_t>(Edges[EdgeIndex].StartVertexNormal[3] + 0.5f);
        PerBodyCandidateSlot& Slot = BodyAccum.AcquireSlot(BodyOrd);
        ++Result.TestedPrimitives;
        const ProjectedEdgeFoot EdgeProj = ProjectPointOntoEdgePrimitive(
            PositionMeters,
            Edges[EdgeIndex],
            ConicArcs3D,
            Settings.EnablePseudoNormals);
        if (EdgeProj.DistanceMeters < Slot.BestEdgeDist)
        {
            Slot.BestEdgeDist = EdgeProj.DistanceMeters;
            Slot.BestEdgeFoot = EdgeProj.FootPoint;
            Slot.BestEdgeNorm = EdgeProj.PseudoNormal;
        }
    }

    if (BodyAccum.EmptyCondition())
    {
        const float CenterDist = std::bit_cast<float>(Cell.CenterDistanceBits);
        Result.SignedDistanceMeters = CenterDist;
        return FinalizeHollowWallThickness(Result, Settings);
    }

    BodyAccum.ResolveExactUnion(PositionMeters, EdgeTieTolerance, Result);
    return FinalizeHollowWallThickness(Result, Settings);
}

//------------------------------------------------------------------------------------------------------------------------
//                                            BINARY SECTION ENCODERS / DECODERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

struct CompiledSurfaceFieldBinaryHeader
{
    uint32_t            Magic              = CompiledSurfaceFieldMagic;
    uint32_t            Version            = CompiledSurfaceFieldVersion;
    Mathematics::Axis3D GridBoundsMin      {};
    Mathematics::Axis3D GridBoundsMax      {};
    uint32_t            MacroDimX          = 1u;
    uint32_t            MacroDimY          = 1u;
    uint32_t            MacroDimZ          = 1u;
    float               CellRadiusMeters   = 0.0f;
    float               EdgeTieTolerance   = 1.0e-5f;
    uint32_t            SurfaceCount       = 0u;
    uint32_t            TrimSegmentCount   = 0u;
    uint32_t            BezierPatchCount   = 0u;
    uint32_t            ConicArcCount      = 0u;
    uint32_t            EdgeCount          = 0u;
    uint32_t            MacroCellCount     = 0u;
    uint32_t            CellSurfaceCount   = 0u;
    uint32_t            CellEdgeCount      = 0u;
    uint32_t            ReservedZero       = 0u;
};

struct ParametricCurveBinaryHeader
{
    uint32_t Magic        = ParametricCurvePackageMagic;
    uint32_t Version      = ParametricCurvePackageVersion;
    uint32_t CurveCount   = 0u;
    uint32_t BezierPoles  = 0u;
};

struct BakedVolumeBinaryHeader
{
    uint32_t            Magic       = BakedVolumeSectionMagic;
    uint32_t            Version     = BakedVolumeSectionVersion;
    uint32_t            ResolutionX = 0u;
    uint32_t            ResolutionY = 0u;
    uint32_t            ResolutionZ = 0u;
    uint32_t            SampleCount = 0u;
    Mathematics::Axis3D LocalMin    {};
    Mathematics::Axis3D LocalMax    {};
    Mathematics::Axis3D Spacing     {};
    uint32_t            Reserved    = 0u;
};

} // namespace

bool EncodeCompiledSurfaceFieldBinary(
    const CompiledSurfaceField& SourceField,
    std::vector<std::byte>&     OutBytes,
    std::string&                OutDiagnostic)
{
    OutBytes.clear();
    OutDiagnostic.clear();

    CompiledSurfaceFieldBinaryHeader Header {};
    Header.GridBoundsMin    = SourceField.GridBoundsMin;
    Header.GridBoundsMax    = SourceField.GridBoundsMax;
    Header.MacroDimX        = SourceField.MacroDimensions[0];
    Header.MacroDimY        = SourceField.MacroDimensions[1];
    Header.MacroDimZ        = SourceField.MacroDimensions[2];
    Header.CellRadiusMeters = SourceField.CellRadiusMeters;
    Header.EdgeTieTolerance = SourceField.EdgeTieTolerance;
    Header.SurfaceCount     = static_cast<uint32_t>(SourceField.Surfaces.size());
    Header.TrimSegmentCount = static_cast<uint32_t>(SourceField.TrimSegments2D.size());
    Header.BezierPatchCount = static_cast<uint32_t>(SourceField.BezierPatches.size());
    Header.ConicArcCount    = static_cast<uint32_t>(SourceField.ConicArcs3D.size());
    Header.EdgeCount        = static_cast<uint32_t>(SourceField.Edges.size());
    Header.MacroCellCount   = static_cast<uint32_t>(SourceField.MacroCells.size());
    Header.CellSurfaceCount = static_cast<uint32_t>(SourceField.CellSurfaceIndices.size());
    Header.CellEdgeCount    = static_cast<uint32_t>(SourceField.CellEdgeIndices.size());

    OutBytes.reserve(sizeof(Header) + SourceField.QueryGpuPayloadBytes());
    AppendPodBytes(OutBytes, Header);
    AppendPodVector<CompiledSurfacePrimitive>(OutBytes, SourceField.Surfaces);
    AppendPodVector<CompiledTrimSegment2D>(OutBytes, SourceField.TrimSegments2D);
    AppendPodVector<CompiledBezierPatch4x4>(OutBytes, SourceField.BezierPatches);
    AppendPodVector<CompiledConicArc3D>(OutBytes, SourceField.ConicArcs3D);
    AppendPodVector<CompiledPseudoNormalEdge>(OutBytes, SourceField.Edges);
    AppendPodVector<CompiledMacroCell>(OutBytes, SourceField.MacroCells);
    AppendPodVector<uint32_t>(OutBytes, SourceField.CellSurfaceIndices);
    AppendPodVector<uint32_t>(OutBytes, SourceField.CellEdgeIndices);
    return true;
}

bool DecodeCompiledSurfaceFieldBinary(
    std::span<const std::byte> InputBytes,
    CompiledSurfaceField&      OutField,
    std::string&               OutDiagnostic)
{
    OutField.Reset();
    OutDiagnostic.clear();

    std::span<const std::byte> Cursor = InputBytes;
    CompiledSurfaceFieldBinaryHeader Header {};
    if (!ReadPodBytes(Cursor, Header))
    {
        OutDiagnostic = "CompiledSurfaceField binary truncated before header.";
        return false;
    }
    if (Header.Magic != CompiledSurfaceFieldMagic || Header.Version != CompiledSurfaceFieldVersion)
    {
        OutDiagnostic = "CompiledSurfaceField binary magic or version mismatch.";
        return false;
    }

    OutField.GridBoundsMin    = Header.GridBoundsMin;
    OutField.GridBoundsMax    = Header.GridBoundsMax;
    OutField.MacroDimensions  = { Header.MacroDimX, Header.MacroDimY, Header.MacroDimZ };
    OutField.CellRadiusMeters = Header.CellRadiusMeters;
    OutField.EdgeTieTolerance = Header.EdgeTieTolerance;

    if (!ReadPodVector(Cursor, Header.SurfaceCount,     OutField.Surfaces) ||
        !ReadPodVector(Cursor, Header.TrimSegmentCount, OutField.TrimSegments2D) ||
        !ReadPodVector(Cursor, Header.BezierPatchCount, OutField.BezierPatches) ||
        !ReadPodVector(Cursor, Header.ConicArcCount,    OutField.ConicArcs3D) ||
        !ReadPodVector(Cursor, Header.EdgeCount,        OutField.Edges) ||
        !ReadPodVector(Cursor, Header.MacroCellCount,   OutField.MacroCells) ||
        !ReadPodVector(Cursor, Header.CellSurfaceCount, OutField.CellSurfaceIndices) ||
        !ReadPodVector(Cursor, Header.CellEdgeCount,    OutField.CellEdgeIndices))
    {
        OutField.Reset();
        OutDiagnostic = "CompiledSurfaceField binary truncated inside payload arrays.";
        return false;
    }
    return true;
}

bool EncodeParametricCurvePackageBinary(
    const ParametricCurvePackage& SourceCurves,
    std::vector<std::byte>&       OutBytes,
    std::string&                  OutDiagnostic)
{
    OutBytes.clear();
    OutDiagnostic.clear();

    ParametricCurveBinaryHeader Header {};
    Header.CurveCount  = static_cast<uint32_t>(SourceCurves.Curves.size());
    Header.BezierPoles = static_cast<uint32_t>(SourceCurves.BezierPolesWeighted.size());

    OutBytes.reserve(sizeof(Header) + SourceCurves.QueryMemoryBytes());
    AppendPodBytes(OutBytes, Header);
    AppendPodVector<ParametricCurveEntry>(OutBytes, SourceCurves.Curves);
    AppendPodVector<std::array<float, 4u>>(OutBytes, SourceCurves.BezierPolesWeighted);
    return true;
}

bool DecodeParametricCurvePackageBinary(
    std::span<const std::byte> InputBytes,
    ParametricCurvePackage&    OutCurves,
    std::string&               OutDiagnostic)
{
    OutCurves.Reset();
    OutDiagnostic.clear();

    std::span<const std::byte> Cursor = InputBytes;
    ParametricCurveBinaryHeader Header {};
    if (!ReadPodBytes(Cursor, Header))
    {
        OutDiagnostic = "ParametricCurvePackage binary truncated before header.";
        return false;
    }
    if (Header.Magic != ParametricCurvePackageMagic || Header.Version != ParametricCurvePackageVersion)
    {
        OutDiagnostic = "ParametricCurvePackage binary magic or version mismatch.";
        return false;
    }
    if (!ReadPodVector(Cursor, Header.CurveCount,  OutCurves.Curves) ||
        !ReadPodVector(Cursor, Header.BezierPoles, OutCurves.BezierPolesWeighted))
    {
        OutCurves.Reset();
        OutDiagnostic = "ParametricCurvePackage binary truncated inside payload.";
        return false;
    }
    return true;
}

bool EncodeLocalDistanceVolumeBinary(
    const LocalSignedDistanceVolume& SourceVolume,
    std::vector<std::byte>&          OutBytes,
    std::string&                     OutDiagnostic)
{
    OutBytes.clear();
    OutDiagnostic.clear();

    const uint64_t ExpectedCount = static_cast<uint64_t>(SourceVolume.ResolutionX) *
                                   static_cast<uint64_t>(SourceVolume.ResolutionY) *
                                   static_cast<uint64_t>(SourceVolume.ResolutionZ);
    if (SourceVolume.HalfDistanceSamples.size() != ExpectedCount)
    {
        OutDiagnostic = "LocalSignedDistanceVolume sample count does not match resolution product.";
        return false;
    }

    BakedVolumeBinaryHeader Header {};
    Header.ResolutionX = SourceVolume.ResolutionX;
    Header.ResolutionY = SourceVolume.ResolutionY;
    Header.ResolutionZ = SourceVolume.ResolutionZ;
    Header.SampleCount = static_cast<uint32_t>(ExpectedCount);
    Header.LocalMin    = SourceVolume.LocalMin;
    Header.LocalMax    = SourceVolume.LocalMax;
    Header.Spacing     = SourceVolume.VoxelSpacing;

    OutBytes.reserve(sizeof(Header) + SourceVolume.HalfDistanceSamples.size() * sizeof(uint16_t));
    AppendPodBytes(OutBytes, Header);
    AppendPodVector<uint16_t>(OutBytes, SourceVolume.HalfDistanceSamples);
    return true;
}

bool DecodeLocalDistanceVolumeBinary(
    std::span<const std::byte> InputBytes,
    LocalSignedDistanceVolume& OutVolume,
    std::string&               OutDiagnostic)
{
    OutVolume.Reset();
    OutDiagnostic.clear();

    std::span<const std::byte> Cursor = InputBytes;
    BakedVolumeBinaryHeader Header {};
    if (!ReadPodBytes(Cursor, Header))
    {
        OutDiagnostic = "BakedVolumeSection binary truncated before header.";
        return false;
    }
    if (Header.Magic != BakedVolumeSectionMagic || Header.Version != BakedVolumeSectionVersion)
    {
        OutDiagnostic = "BakedVolumeSection binary magic or version mismatch.";
        return false;
    }
    const uint64_t ExpectedCount = static_cast<uint64_t>(Header.ResolutionX) *
                                   static_cast<uint64_t>(Header.ResolutionY) *
                                   static_cast<uint64_t>(Header.ResolutionZ);
    if (Header.SampleCount != ExpectedCount)
    {
        OutDiagnostic = "BakedVolumeSection resolution product mismatch.";
        return false;
    }
    if (!ReadPodVector(Cursor, Header.SampleCount, OutVolume.HalfDistanceSamples))
    {
        OutVolume.Reset();
        OutDiagnostic = "BakedVolumeSection binary truncated inside fp16 samples.";
        return false;
    }

    OutVolume.ResolutionX  = Header.ResolutionX;
    OutVolume.ResolutionY  = Header.ResolutionY;
    OutVolume.ResolutionZ  = Header.ResolutionZ;
    OutVolume.LocalMin     = Header.LocalMin;
    OutVolume.LocalMax     = Header.LocalMax;
    OutVolume.VoxelSpacing = Header.Spacing;

    OutVolume.DistanceSamples.resize(OutVolume.HalfDistanceSamples.size());
    for (std::size_t Index = 0u; Index < OutVolume.HalfDistanceSamples.size(); ++Index)
    {
        OutVolume.DistanceSamples[Index] = UnpackBinary16ToFloat32(OutVolume.HalfDistanceSamples[Index]);
    }
    return true;
}

} // namespace Frontier::DistanceFieldSpace
