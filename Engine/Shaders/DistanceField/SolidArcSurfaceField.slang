/*============================================================================================================================================
                                                        SOLIDARCSURFACEFIELD.SLANG
============================================================================================================================================*/
// 🧩 Exact GPU CAD B-Rep Signed Distance Field module matching CompiledSurfaceField.h 16B std430 structures byte-for-byte

module SolidArcSurfaceField;

//------------------------------------------------------------------------------------------------------------------------
//                                                    GPU STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

public struct GpuSolidArcSurface128
{
    public float4 BoundsLowCategory;     // xyz = AABB min [m], w = bitcast<float>(SurfacePrimitiveCategory)
    public float4 BoundsHighPatch;   // xyz = AABB max [m], w = bitcast<float>(BezierPatchOrdinal)
    public float4 OriginRadius0;     // xyz = Basis origin [m], w = Radius / OriginRadius / MajorRadius [m]
    public float4 AxisXRadius1;      // xyz = Basis X axis [-], w = MinorRadius [m] or RadialSlope [-]
    public float4 AxisYPeriodU;      // xyz = Basis Y axis [-], w = PeriodU [rad]
    public float4 AxisZPeriodV;      // xyz = Basis Z axis [-], w = PeriodV [rad]
    public float4 TrimSpanSenseBody; // x = FirstTrim, y = TrimCount, z = OutwardSense (+1/-1), w = BodyOrdinal
    public float4 ChartBounds;       // xy = ChartLow (uMin, vMin), zw = ChartHigh (uMax, vMax)
};

public struct GpuSolidArcTrimSegment32
{
    // Line2D:        Coords0 = (u0, v0, u1, v1),          Coords1 = (0, 0, 0, 0)
    // CircularArc2D: Coords0 = (centerU, centerV, R, t0), Coords1 = (sweepDelta, 1.0, 0, 0)
    public float4 Coords0;
    public float4 Coords1;
};

public struct GpuSolidArcBezierPatch256
{
    public float4 PolesWeighted[16]; // u-major 4x4 poles: xyz = Euclidean pole [m], w = weight > 0
};

public struct GpuSolidArcConicArc64
{
    public float4 CentreStartAngle; // xyz = Centre [m], w = IntervalLow [rad]
    public float4 CosineAxisSweep;  // xyz = Cosine semi-axis [m], w = SweepDelta [rad]
    public float4 SineAxisMajor;    // xyz = Sine semi-axis [m], w = MajorRadius [m]
    public float4 PlaneNormalMinor; // xyz = Unit plane normal [-], w = MinorRadius [m]
};

public struct GpuSolidArcEdge80
{
    public float4 StartPointCategory;     // xyz = Start 3D point [m], w = bitcast<float>(EdgeProjectionCategory)
    public float4 EndPointArcOrdinal; // xyz = End 3D point [m], w = bitcast<float>(ConicArcOrdinal)
    public float4 EdgePseudoNormal;   // xyz = Dihedral angle-weighted unit normal [-], w = DihedralRadians
    public float4 StartVertexNormal;  // xyz = Angle-weighted vertex normal at Start [-], w = BodyOrdinal
    public float4 EndVertexNormal;    // xyz = Angle-weighted vertex normal at End [-], w = NaiveFaceNormalSign
};

public struct GpuSolidArcMacroCell16
{
    public uint CenterDistanceBits; // IEEE-754 float32 signed distance at cell center [m]
    public uint FirstSurfaceEntry;  // Offset into CellSurfaceIndices
    public uint PackedCounts;       // low16 = SurfaceCount, high16 = EdgeCount (0 = outside narrow band)
    public uint FirstEdgeEntry;     // Offset into CellEdgeIndices
};

public struct GpuSolidArcFieldHeader
{
    public float4 GridOriginCell; // xyz = GridBoundsMin [m], w = EdgeTieTolerance [m]
    public float4 GridCellSize;   // xyz = CellSize [m], w = CellRadiusMeters [m]
    public uint4  GridDims;       // xyz = MacroDimensions, w = SurfaceCount
    public uint4  BufferCounts;   // x = EdgeCount, y = QueryCount, z = VolumeDims.x, w = VolumeDims.y
    public uint4  ExtraCounts;    // x = VolumeDims.z, y = OutputMode (0=Points, 1=VolumeGrid), zw = Reserved
    public float4 VolumeOrigin;   // xyz = VolumeOrigin [m], w = VoxelSpacing [m]
};

public struct GpuSolidArcDistanceSample
{
    public float  SignedDistance;
    public float3 OutwardGradient;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 ANALYTIC & TRIM SOLVERS
//------------------------------------------------------------------------------------------------------------------------

static const float SolidArcTwoPi = 6.283185307179586f;

float WrapAnglePositive(float Angle)
{
    float Wrapped = Angle - floor(Angle / SolidArcTwoPi) * SolidArcTwoPi;
    return (Wrapped < 0.0f) ? (Wrapped + SolidArcTwoPi) : Wrapped;
}

float WrapChartCoordinate(float RawCoord, float Low, float High, float Period)
{
    if (Period <= 0.0f)
    {
        return RawCoord;
    }
    float Mid     = 0.5f * (Low + High);
    float Shifted = RawCoord - Mid;
    Shifted      -= Period * round(Shifted / Period);
    float Cand    = Mid + Shifted;
    if (Cand < Low && (Cand + Period) <= High + 1.0e-5f)
    {
        Cand += Period;
    }
    else if (Cand > High && (Cand - Period) >= Low - 1.0e-5f)
    {
        Cand -= Period;
    }
    return Cand;
}

float BoxDistanceSq(float3 P, float3 BMin, float3 BMax)
{
    float3 Clamped = clamp(P, BMin, BMax);
    float3 Diff    = P - Clamped;
    return dot(Diff, Diff);
}

bool PointInsideTrimLoop2D(
    float2                                    Uv,
    uint                                      TrimStart,
    uint                                      TrimCount,
    StructuredBuffer<GpuSolidArcTrimSegment32> TrimSegments)
{
    if (TrimCount == 0u)
    {
        return true;
    }
    int Crossings = 0;
    for (uint Offset = 0u; Offset < TrimCount; ++Offset)
    {
        GpuSolidArcTrimSegment32 Seg = TrimSegments[TrimStart + Offset];
        bool ArcCondition = (Seg.Coords1.y > 0.5f);
        if (!ArcCondition)
        {
            float2 A = Seg.Coords0.xy;
            float2 B = Seg.Coords0.zw;
            if ((A.y > Uv.y) != (B.y > Uv.y))
            {
                float XHit = A.x + (Uv.y - A.y) * (B.x - A.x) / (B.y - A.y);
                if (Uv.x < XHit)
                {
                    Crossings += 1;
                }
            }
        }
        else
        {
            float2 Center = Seg.Coords0.xy;
            float  Radius = abs(Seg.Coords0.z);
            float  StartA = Seg.Coords0.w;
            float  Sweep  = Seg.Coords1.x;
            if (abs(Sweep) >= SolidArcTwoPi - 1.0e-4f)
            {
                float2 Diff = Uv - Center;
                if (dot(Diff, Diff) < Radius * Radius)
                {
                    Crossings += 1;
                }
                continue;
            }
            float2 Prev = Center + float2(Radius * cos(StartA), Radius * sin(StartA));
            for (int Step = 1; Step <= 16; ++Step)
            {
                float Theta = StartA + Sweep * (float(Step) / 16.0f);
                float2 Next = Center + float2(Radius * cos(Theta), Radius * sin(Theta));
                if ((Prev.y > Uv.y) != (Next.y > Uv.y))
                {
                    float XHit = Prev.x + (Uv.y - Prev.y) * (Next.x - Prev.x) / (Next.y - Prev.y);
                    if (Uv.x < XHit)
                    {
                        Crossings += 1;
                    }
                }
                Prev = Next;
            }
        }
    }
    return (Crossings & 1) != 0;
}

void EvaluateRationalBezier4x4(
    GpuSolidArcBezierPatch256 Patch,
    float                     U,
    float                     V,
    out float3                OutPos,
    out float3                OutDu,
    out float3                OutDv)
{
    float U1 = 1.0f - U;
    float V1 = 1.0f - V;

    float Bu[4]  = { U1 * U1 * U1, 3.0f * U * U1 * U1, 3.0f * U * U * U1, U * U * U };
    float Bv[4]  = { V1 * V1 * V1, 3.0f * V * V1 * V1, 3.0f * V * V * V1, V * V * V };
    float dBu[4] = { -3.0f * U1 * U1, 3.0f * U1 * (1.0f - 3.0f * U), 3.0f * U * (2.0f - 3.0f * U), 3.0f * U * U };
    float dBv[4] = { -3.0f * V1 * V1, 3.0f * V1 * (1.0f - 3.0f * V), 3.0f * V * (2.0f - 3.0f * V), 3.0f * V * V };

    float4 H   = float4(0.0f);
    float4 dHu = float4(0.0f);
    float4 dHv = float4(0.0f);

    [unroll]
    for (int Row = 0; Row < 4; ++Row)
    {
        [unroll]
        for (int Col = 0; Col < 4; ++Col)
        {
            float4 Cp = Patch.PolesWeighted[Row * 4 + Col];
            float  W  = max(Cp.w, 1.0e-8f);
            float4 Hw = float4(Cp.xyz * W, W);
            H   += (Bu[Row] * Bv[Col]) * Hw;
            dHu += (dBu[Row] * Bv[Col]) * Hw;
            dHv += (Bu[Row] * dBv[Col]) * Hw;
        }
    }

    float InvW = 1.0f / max(H.w, 1.0e-12f);
    OutPos = H.xyz * InvW;
    OutDu  = (dHu.xyz - dHu.w * OutPos) * InvW;
    OutDv  = (dHv.xyz - dHv.w * OutPos) * InvW;
}

bool ProjectUntrimmedSurfaceGpu(
    float3                                     P,
    GpuSolidArcSurface128                      Surf,
    StructuredBuffer<GpuSolidArcBezierPatch256> BezierPatches,
    out float3                                 OutFoot,
    out float3                                 OutOutwardNormal,
    out float2                                 OutUv)
{
    uint   Cat     = asuint(Surf.BoundsLowCategory.w);
    float  Orient  = (Surf.TrimSpanSenseBody.z < 0.0f) ? -1.0f : 1.0f;
    float3 Orig    = Surf.OriginRadius0.xyz;
    float3 Ax      = Surf.AxisXRadius1.xyz;
    float3 Ay      = Surf.AxisYPeriodU.xyz;
    float3 Az      = Surf.AxisZPeriodV.xyz;
    float  PeriodU = Surf.AxisYPeriodU.w;
    float  PeriodV = Surf.AxisZPeriodV.w;
    float  UMin    = Surf.ChartBounds.x;
    float  VMin    = Surf.ChartBounds.y;
    float  UMax    = Surf.ChartBounds.z;
    float  VMax    = Surf.ChartBounds.w;

    OutFoot          = Orig;
    OutOutwardNormal = float3(0.0f, 1.0f, 0.0f);
    OutUv            = float2(0.0f, 0.0f);

    if (Cat == 0u) // Plane
    {
        float3 Rel  = P - Orig;
        float  U    = dot(Rel, Ax);
        float  V    = dot(Rel, Ay);
        float  Dist = dot(Rel, Az);
        if (U < UMin - 1.0e-5f || U > UMax + 1.0e-5f || V < VMin - 1.0e-5f || V > VMax + 1.0e-5f)
        {
            return false;
        }
        OutFoot          = P - Dist * Az;
        OutOutwardNormal = normalize(Orient * Az);
        OutUv            = float2(U, V);
        return true;
    }
    if (Cat == 1u) // Cylinder
    {
        float  Radius = Surf.OriginRadius0.w;
        float3 Rel    = P - Orig;
        float  V      = dot(Rel, Az);
        if (V < VMin - 1.0e-5f || V > VMax + 1.0e-5f)
        {
            return false;
        }
        float  X   = dot(Rel, Ax);
        float  Y   = dot(Rel, Ay);
        float  Rho = sqrt(X * X + Y * Y);
        float  Phi = (Rho > 1.0e-12f) ? atan2(Y, X) : UMin;
        float  U   = WrapChartCoordinate(Phi, UMin, UMax, PeriodU);
        if (U < UMin - 1.0e-5f || U > UMax + 1.0e-5f)
        {
            return false;
        }
        float3 Radial    = cos(U) * Ax + sin(U) * Ay;
        OutFoot          = Orig + Radius * Radial + V * Az;
        OutOutwardNormal = normalize(Orient * Radial);
        OutUv            = float2(U, V);
        return true;
    }
    if (Cat == 2u) // Cone
    {
        float  OriginRadius = Surf.OriginRadius0.w;
        float  Slope      = Surf.AxisXRadius1.w;
        float3 Rel        = P - Orig;
        float  X          = dot(Rel, Ax);
        float  Y          = dot(Rel, Ay);
        float  Z          = dot(Rel, Az);
        float  Rho        = sqrt(X * X + Y * Y);
        float  Phi        = (Rho > 1.0e-12f) ? atan2(Y, X) : UMin;
        float  U          = WrapChartCoordinate(Phi, UMin, UMax, PeriodU);
        if (U < UMin - 1.0e-5f || U > UMax + 1.0e-5f)
        {
            return false;
        }
        float Denom = 1.0f + Slope * Slope;
        float V     = (Z + (Rho - OriginRadius) * Slope) / Denom;
        if (V < VMin - 1.0e-5f || V > VMax + 1.0e-5f)
        {
            return false;
        }
        float  RAtV      = max(0.0f, OriginRadius + Slope * V);
        float3 Radial    = cos(U) * Ax + sin(U) * Ay;
        OutFoot          = Orig + RAtV * Radial + V * Az;
        OutOutwardNormal = normalize(Orient * (Radial - Slope * Az));
        OutUv            = float2(U, V);
        return true;
    }
    if (Cat == 3u) // Sphere
    {
        float  Radius = Surf.OriginRadius0.w;
        float3 Rel    = P - Orig;
        float  Len    = length(Rel);
        float3 Dir    = (Len > 1.0e-12f) ? (Rel / Len) : Az;
        float  SinV   = clamp(dot(Dir, Az), -1.0f, 1.0f);
        float  V      = asin(SinV);
        if (V < VMin - 1.0e-5f || V > VMax + 1.0e-5f)
        {
            return false;
        }
        float X   = dot(Dir, Ax);
        float Y   = dot(Dir, Ay);
        float Phi = (X * X + Y * Y > 1.0e-12f) ? atan2(Y, X) : UMin;
        float U   = WrapChartCoordinate(Phi, UMin, UMax, PeriodU);
        if (U < UMin - 1.0e-5f || U > UMax + 1.0e-5f)
        {
            return false;
        }
        float  CosV      = cos(V);
        float3 Radial    = CosV * (cos(U) * Ax + sin(U) * Ay) + sin(V) * Az;
        OutFoot          = Orig + Radius * Radial;
        OutOutwardNormal = normalize(Orient * Radial);
        OutUv            = float2(U, V);
        return true;
    }
    if (Cat == 4u) // Torus
    {
        float  MajorR = Surf.OriginRadius0.w;
        float  MinorR = Surf.AxisXRadius1.w;
        float3 Rel    = P - Orig;
        float  X      = dot(Rel, Ax);
        float  Y      = dot(Rel, Ay);
        float  Z      = dot(Rel, Az);
        float  Rho    = sqrt(X * X + Y * Y);
        float  PhiU   = (Rho > 1.0e-12f) ? atan2(Y, X) : UMin;
        float  U      = WrapChartCoordinate(PhiU, UMin, UMax, PeriodU);
        if (U < UMin - 1.0e-5f || U > UMax + 1.0e-5f)
        {
            return false;
        }
        float Dr   = Rho - MajorR;
        float PhiV = atan2(Z, Dr);
        float V    = WrapChartCoordinate(PhiV, VMin, VMax, PeriodV);
        if (V < VMin - 1.0e-5f || V > VMax + 1.0e-5f)
        {
            return false;
        }
        float3 RingRadial = cos(U) * Ax + sin(U) * Ay;
        float3 TubeNormal = cos(V) * RingRadial + sin(V) * Az;
        OutFoot           = Orig + (MajorR + MinorR * cos(V)) * RingRadial + (MinorR * sin(V)) * Az;
        OutOutwardNormal  = normalize(Orient * TubeNormal);
        OutUv             = float2(U, V);
        return true;
    }
    if (Cat == 5u) // RationalBezier4x4
    {
        uint PatchOrdinal = asuint(Surf.BoundsHighPatch.w);
        GpuSolidArcBezierPatch256 Patch = BezierPatches[PatchOrdinal];

        float3 C00   = Patch.PolesWeighted[0].xyz;
        float3 C10   = Patch.PolesWeighted[12].xyz;
        float3 C01   = Patch.PolesWeighted[3].xyz;
        float3 EdgeU = C10 - C00;
        float3 EdgeV = C01 - C00;
        float  BestU = clamp(dot(P - C00, EdgeU) / max(dot(EdgeU, EdgeU), 1.0e-12f), 0.05f, 0.95f);
        float  BestV = clamp(dot(P - C00, EdgeV) / max(dot(EdgeV, EdgeV), 1.0e-12f), 0.05f, 0.95f);

        float3 S0, Du0, Dv0;
        EvaluateRationalBezier4x4(Patch, BestU, BestV, S0, Du0, Dv0);
        float BestDistSq = dot(P - S0, P - S0);

        const float2 Seeds[4] = { float2(0.25f, 0.25f), float2(0.75f, 0.25f), float2(0.25f, 0.75f), float2(0.75f, 0.75f) };
        [unroll]
        for (int SeedIdx = 0; SeedIdx < 4; ++SeedIdx)
        {
            float3 Sp, Sdu, Sdv;
            EvaluateRationalBezier4x4(Patch, Seeds[SeedIdx].x, Seeds[SeedIdx].y, Sp, Sdu, Sdv);
            float Dsq = dot(P - Sp, P - Sp);
            if (Dsq < BestDistSq)
            {
                BestDistSq = Dsq;
                BestU      = Seeds[SeedIdx].x;
                BestV      = Seeds[SeedIdx].y;
            }
        }

        float U = BestU;
        float V = BestV;
        [unroll]
        for (int Iter = 0; Iter < 4; ++Iter)
        {
            float3 S, Du, Dv;
            EvaluateRationalBezier4x4(Patch, U, V, S, Du, Dv);
            float3 Diff = S - P;
            float  Gu   = dot(Diff, Du);
            float  Gv   = dot(Diff, Dv);
            float  Juu  = dot(Du, Du) + 1.0e-6f;
            float  Juv  = dot(Du, Dv);
            float  Jvv  = dot(Dv, Dv) + 1.0e-6f;
            float  Det  = Juu * Jvv - Juv * Juv;
            if (abs(Det) < 1.0e-16f)
            {
                break;
            }
            float InvDet = 1.0f / Det;
            U = clamp(U - (Jvv * Gu - Juv * Gv) * InvDet, 0.0f, 1.0f);
            V = clamp(V - (-Juv * Gu + Juu * Gv) * InvDet, 0.0f, 1.0f);
        }

        if (U <= 1.0e-4f || U >= 1.0f - 1.0e-4f || V <= 1.0e-4f || V >= 1.0f - 1.0e-4f)
        {
            return false;
        }

        float3 FinalS, FinalDu, FinalDv;
        EvaluateRationalBezier4x4(Patch, U, V, FinalS, FinalDu, FinalDv);
        float3 RawNormal = cross(FinalDu, FinalDv);
        OutFoot          = FinalS;
        OutOutwardNormal = normalize(Orient * RawNormal);
        OutUv            = float2(UMin + U * (UMax - UMin), VMin + V * (VMax - VMin));
        return true;
    }
    return false;
}

void EvaluateEdgeCandidateGpu(
    float3                                  P,
    GpuSolidArcEdge80                       Edge,
    StructuredBuffer<GpuSolidArcConicArc64> ConicArcs,
    float                                   TieTolerance,
    inout float                             BestDistSq,
    inout float3                            BestFoot,
    inout float3                            BestPseudoNormal)
{
    uint   CurveCat = asuint(Edge.StartPointCategory.w);
    float3 Foot     = Edge.StartPointCategory.xyz;
    float  ParamT   = 0.0f;

    if (CurveCat == 0u) // StraightSegment3D
    {
        float3 A     = Edge.StartPointCategory.xyz;
        float3 B     = Edge.EndPointArcOrdinal.xyz;
        float3 Ab    = B - A;
        float  LenSq = dot(Ab, Ab);
        ParamT       = (LenSq > 1.0e-20f) ? clamp(dot(P - A, Ab) / LenSq, 0.0f, 1.0f) : 0.0f;
        Foot         = A + ParamT * Ab;
    }
    else // ConicArc3D
    {
        uint ArcIdx = asuint(Edge.EndPointArcOrdinal.w);
        GpuSolidArcConicArc64 Arc = ConicArcs[ArcIdx];
        float3 Center     = Arc.CentreStartAngle.xyz;
        float  StartAngle = Arc.CentreStartAngle.w;
        float3 CosVec     = Arc.CosineAxisSweep.xyz;
        float  SweepAngle = Arc.CosineAxisSweep.w;
        float3 SinVec     = Arc.SineAxisMajor.xyz;
        float  MajorR     = Arc.SineAxisMajor.w;
        float  MinorR     = Arc.PlaneNormalMinor.w;

        float3 Au = CosVec / max(MajorR, 1.0e-9f);
        float3 Av = SinVec / max(MinorR, 1.0e-9f);

        if (abs(MajorR - MinorR) <= 1.0e-5f * max(1.0f, MajorR))
        {
            float3 Rel = P - Center;
            float  X   = dot(Rel, Au);
            float  Y   = dot(Rel, Av);
            float  Ang = atan2(Y, X);
            if (abs(SweepAngle) >= SolidArcTwoPi - 1.0e-4f)
            {
                ParamT = 0.5f;
                Foot   = Center + cos(Ang) * CosVec + sin(Ang) * SinVec;
            }
            else
            {
                float Delta = (SweepAngle >= 0.0f) ? WrapAnglePositive(Ang - StartAngle) : WrapAnglePositive(StartAngle - Ang);
                float Span  = abs(SweepAngle);
                if (Delta <= Span)
                {
                    ParamT = (Span > 1.0e-9f) ? (Delta / Span) : 0.0f;
                    Foot   = Center + cos(Ang) * CosVec + sin(Ang) * SinVec;
                }
                else
                {
                    float3 Pa = Edge.StartPointCategory.xyz;
                    float3 Pb = Edge.EndPointArcOrdinal.xyz;
                    if (dot(P - Pa, P - Pa) <= dot(P - Pb, P - Pb))
                    {
                        ParamT = 0.0f;
                        Foot   = Pa;
                    }
                    else
                    {
                        ParamT = 1.0f;
                        Foot   = Pb;
                    }
                }
            }
        }
        else
        {
            float  BestArcSq = 1.0e30f;
            float  BestTheta = StartAngle;
            for (int Step = 0; Step <= 8; ++Step)
            {
                float  Frac = float(Step) / 8.0f;
                float  Th   = StartAngle + SweepAngle * Frac;
                float3 Cand = Center + cos(Th) * CosVec + sin(Th) * SinVec;
                float  Dsq  = dot(P - Cand, P - Cand);
                if (Dsq < BestArcSq)
                {
                    BestArcSq = Dsq;
                    BestTheta = Th;
                }
            }
            float Lo = min(StartAngle, StartAngle + SweepAngle);
            float Hi = max(StartAngle, StartAngle + SweepAngle);
            float Th = BestTheta;
            [unroll]
            for (int Iter = 0; Iter < 4; ++Iter)
            {
                float  C   = cos(Th);
                float  S   = sin(Th);
                float3 Pos = Center + C * CosVec + S * SinVec;
                float3 d1  = -S * CosVec + C * SinVec;
                float3 d2  = -C * CosVec - S * SinVec;
                float  F1  = dot(Pos - P, d1);
                float  F2  = dot(d1, d1) + dot(Pos - P, d2);
                if (abs(F2) > 1.0e-14f)
                {
                    Th = clamp(Th - F1 / F2, Lo, Hi);
                }
            }
            ParamT = (abs(SweepAngle) > 1.0e-9f) ? clamp((Th - StartAngle) / SweepAngle, 0.0f, 1.0f) : 0.0f;
            Foot   = Center + cos(Th) * CosVec + sin(Th) * SinVec;
        }
    }

    float DistSq   = dot(P - Foot, P - Foot);
    float Dist     = sqrt(max(0.0f, DistSq));
    float BestDist = sqrt(max(0.0f, BestDistSq));
    if (Dist <= BestDist + TieTolerance)
    {
        if (DistSq < BestDistSq)
        {
            BestDistSq = DistSq;
            BestFoot   = Foot;
        }
        if (ParamT <= 0.001f)
        {
            BestPseudoNormal = Edge.StartVertexNormal.xyz;
        }
        else if (ParamT >= 0.999f)
        {
            BestPseudoNormal = Edge.EndVertexNormal.xyz;
        }
        else
        {
            BestPseudoNormal = Edge.EdgePseudoNormal.xyz;
        }
    }
}

public GpuSolidArcDistanceSample QuerySolidArcFieldGpu(
    float3                                      P,
    GpuSolidArcFieldHeader                      Header,
    StructuredBuffer<GpuSolidArcSurface128>     Surfaces,
    StructuredBuffer<GpuSolidArcTrimSegment32>  TrimSegments,
    StructuredBuffer<GpuSolidArcBezierPatch256> BezierPatches,
    StructuredBuffer<GpuSolidArcConicArc64>     ConicArcs,
    StructuredBuffer<GpuSolidArcEdge80>         Edges,
    StructuredBuffer<GpuSolidArcMacroCell16>    MacroCells,
    StructuredBuffer<uint>                      CellSurfaceIndices,
    StructuredBuffer<uint>                      CellEdgeIndices)
{
    GpuSolidArcDistanceSample Result;
    Result.SignedDistance  = Header.GridCellSize.w;
    Result.OutwardGradient = float3(0.0f, 1.0f, 0.0f);

    uint3 GridDims = Header.GridDims.xyz;
    if (GridDims.x == 0u || GridDims.y == 0u || GridDims.z == 0u)
    {
        return Result;
    }

    float3 RelCell = (P - Header.GridOriginCell.xyz) / Header.GridCellSize.xyz;
    int3   Coord   = clamp(int3(floor(RelCell)), int3(0), int3(GridDims) - int3(1));
    uint   FlatIdx = (uint(Coord.z) * GridDims.y + uint(Coord.y)) * GridDims.x + uint(Coord.x);

    GpuSolidArcMacroCell16 Cell = MacroCells[FlatIdx];
    float CenterDist = asfloat(Cell.CenterDistanceBits);
    uint  SurfOffset = Cell.FirstSurfaceEntry;
    uint  SurfCount  = Cell.PackedCounts & 0xFFFFu;
    uint  EdgeCount  = (Cell.PackedCounts >> 16u) & 0xFFFFu;
    uint  EdgeOffset = Cell.FirstEdgeEntry;

    if (SurfCount == 0u && EdgeCount == 0u)
    {
        Result.SignedDistance = CenterDist;
        return Result;
    }

    float  BestDistSq = 1.0e30f;
    float3 BestFoot   = P;
    float3 BestNormal = float3(0.0f, 1.0f, 0.0f);

    for (uint Slot = 0u; Slot < SurfCount; ++Slot)
    {
        uint SurfIdx = CellSurfaceIndices[SurfOffset + Slot];
        GpuSolidArcSurface128 Surf = Surfaces[SurfIdx];
        if (BoxDistanceSq(P, Surf.BoundsLowCategory.xyz, Surf.BoundsHighPatch.xyz) >= BestDistSq)
        {
            continue;
        }
        float3 Foot;
        float3 OutNormal;
        float2 Uv;
        if (!ProjectUntrimmedSurfaceGpu(P, Surf, BezierPatches, Foot, OutNormal, Uv))
        {
            continue;
        }
        float DistSq = dot(P - Foot, P - Foot);
        if (DistSq >= BestDistSq)
        {
            continue;
        }
        uint TrimStart = uint(Surf.TrimSpanSenseBody.x + 0.5f);
        uint TrimCount = uint(Surf.TrimSpanSenseBody.y + 0.5f);
        if (!PointInsideTrimLoop2D(Uv, TrimStart, TrimCount, TrimSegments))
        {
            continue;
        }
        BestDistSq = DistSq;
        BestFoot   = Foot;
        BestNormal = OutNormal;
    }

    float TieTol = Header.GridOriginCell.w;
    for (uint Slot = 0u; Slot < EdgeCount; ++Slot)
    {
        uint EdgeIdx = CellEdgeIndices[EdgeOffset + Slot];
        EvaluateEdgeCandidateGpu(P, Edges[EdgeIdx], ConicArcs, TieTol, BestDistSq, BestFoot, BestNormal);
    }

    if (BestDistSq >= 1.0e29f)
    {
        Result.SignedDistance = CenterDist;
        return Result;
    }

    float3 Diff     = P - BestFoot;
    float  Unsigned = sqrt(max(0.0f, BestDistSq));
    float  Orient   = dot(Diff, BestNormal);
    float  SignVal  = (abs(Orient) > 1.0e-9f) ? ((Orient >= 0.0f) ? 1.0f : -1.0f) : ((CenterDist >= 0.0f) ? 1.0f : -1.0f);
    Result.SignedDistance  = SignVal * Unsigned;
    Result.OutwardGradient = (Unsigned > 1.0e-8f) ? ((SignVal / Unsigned) * Diff) : normalize(BestNormal);
    return Result;
}
