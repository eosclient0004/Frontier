#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherSurfaceTracing.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchProfile.h"
#include <algorithm>
#include <cmath>

namespace Frontier::Workspaces::Sketcher
{
using namespace GeometrySpace::Parametric;

namespace
{
// Closest point on 3D segment [A, B] to ray (Origin + t * Dir), clamped to u in [0, 1].
Vector3 ClosestPointOnSegmentToRay(const Vector3& A, const Vector3& B, const Vector3& Origin, const Vector3& Dir)
{
    const Vector3 Seg = B - A;
    const double  SegLenSq = Dot(Seg, Seg);
    if (!(SegLenSq > 1.0e-24)) return A;
    const Vector3 W = A - Origin;
    const double  Bdot = Dot(Seg, Dir);
    const double  Ddot = Dot(Seg, W);
    const double  Edot = Dot(Dir, W);
    const double  Denom = SegLenSq - Bdot * Bdot;
    double U = 0.0;
    if (Denom > 1.0e-18 * SegLenSq)
    {
        U = (Bdot * Edot - Ddot) / Denom;
    }
    else
    {
        U = -Ddot / SegLenSq;
    }
    U = std::clamp(U, 0.0, 1.0);
    return A + Seg * U;
}

double ScreenPointToSegment(double PX, double PY, double AX, double AY, double BX, double BY)
{
    const double DX = BX - AX;
    const double DY = BY - AY;
    const double LenSq = DX * DX + DY * DY;
    const double T = LenSq > 1.0e-12 ? std::clamp(((PX - AX) * DX + (PY - AY) * DY) / LenSq, 0.0, 1.0) : 0.0;
    return std::hypot(PX - (AX + T * DX), PY - (AY + T * DY));
}

SketcherSnap SnapReferenceMeshFeatures(
    const SketcherDocument& Document,
    const OrbitCameraBasis& Frame,
    double X,
    double Y,
    bool VertexSnap,
    bool EdgeSnap,
    double VertexReachPx,
    double EdgeReachPx)
{
    if ((!VertexSnap && !EdgeSnap) || Document.ReferenceSurfaces.empty()) return {};

    const OrbitRay Ray = CastOrbitRay(Frame, X, Y);
    const Vector3  Origin{ Ray.Origin[0], Ray.Origin[1], Ray.Origin[2] };
    const Vector3  Dir = Normalize(Vector3{ Ray.Direction[0], Ray.Direction[1], Ray.Direction[2] });
    const auto     RefHit = IntersectReferenceSurfaces(Document.ReferenceSurfaces, Origin, Dir);

    auto PartCaption = [&](uint32_t SurfIdx, uint32_t PartIdx) -> std::string
    {
        if (SurfIdx >= Document.ReferenceSurfaces.size()) return "Mesh";
        const auto& Surf = Document.ReferenceSurfaces[SurfIdx];
        if (PartIdx < Surf.Parts.size() && !Surf.Parts[PartIdx].Name.empty())
            return Surf.Name + " · " + Surf.Parts[PartIdx].Name;
        return Surf.Name.empty() ? std::string("Mesh") : Surf.Name;
    };

    bool        HaveVertex = false;
    double      BestVertexPx = VertexReachPx + 1.0e-6;
    double      BestVertexDepth = 1.0e300;
    Vector3     BestVertexPos {};
    Vector3     BestVertexNormal { 0.0, 0.0, 1.0 };
    std::string BestVertexSource;

    bool        HaveEdge = false;
    double      BestEdgeScore = 1.0e300;
    double      BestEdgePx = EdgeReachPx + 1.0e-6;
    Vector3     BestEdgePos {};
    Vector3     BestEdgeTangent {};
    Vector3     BestEdgeNormal { 0.0, 0.0, 1.0 };
    bool        BestEdgeCrease = false;
    std::string BestEdgeSource;

    auto ConsiderVertex = [&](const Vector3& Pos, const Vector3& Normal, const std::string& Label, double MaxDepth)
    {
        if (!VertexSnap) return;
        const double Depth = Dot(Pos - Origin, Dir);
        if (Depth <= 1.0e-6 || Depth > MaxDepth) return;
        double PX = 0.0, PY = 0.0;
        if (!ProjectSketcherPoint(Frame, Pos, PX, PY)) return;
        const double Gap = std::hypot(PX - X, PY - Y);
        if (Gap > VertexReachPx) return;
        if (!HaveVertex || Gap < BestVertexPx - 0.5 || (std::fabs(Gap - BestVertexPx) <= 0.5 && Depth < BestVertexDepth))
        {
            HaveVertex       = true;
            BestVertexPx     = Gap;
            BestVertexDepth  = Depth;
            BestVertexPos    = Pos;
            BestVertexNormal = Length(Normal) > 1.0e-9 ? Normalize(Normal) : Vector3{ 0.0, 0.0, 1.0 };
            BestVertexSource = "Mesh vertex · " + Label;
        }
    };

    auto ConsiderEdge = [&](const Vector3& A, const Vector3& B, const Vector3& Normal, bool IsCrease, const std::string& Label, double MaxDepth)
    {
        if (!EdgeSnap) return;
        const Vector3 Seg = B - A;
        const double SegLen = Length(Seg);
        if (!(SegLen > 1.0e-12)) return;
        double AX = 0.0, AY = 0.0, BX = 0.0, BY = 0.0;
        if (!ProjectSketcherPoint(Frame, A, AX, AY) || !ProjectSketcherPoint(Frame, B, BX, BY)) return;
        if (ScreenPointToSegment(X, Y, AX, AY, BX, BY) > EdgeReachPx * 1.35) return;

        const Vector3 Pt = ClosestPointOnSegmentToRay(A, B, Origin, Dir);
        const double Depth = Dot(Pt - Origin, Dir);
        if (Depth <= 1.0e-6 || Depth > MaxDepth) return;
        double EX = 0.0, EY = 0.0;
        if (!ProjectSketcherPoint(Frame, Pt, EX, EY)) return;
        const double Gap = std::hypot(EX - X, EY - Y);
        if (Gap > EdgeReachPx) return;

        // Slight preference for crease/feature edges over interior triangulation diagonals.
        const double Score = IsCrease ? Gap : (Gap + 2.25);
        if (!HaveEdge || Score < BestEdgeScore)
        {
            HaveEdge        = true;
            BestEdgeScore   = Score;
            BestEdgePx      = Gap;
            BestEdgePos     = Pt;
            BestEdgeTangent = Seg * (1.0 / SegLen);
            BestEdgeNormal  = Length(Normal) > 1.0e-9 ? Normalize(Normal) : Vector3{ 0.0, 0.0, 1.0 };
            BestEdgeCrease  = IsCrease;
            BestEdgeSource  = std::string(IsCrease ? "Mesh crease · " : "Mesh edge · ") + Label;
        }
    };

    if (RefHit)
    {
        // Compute exact world-space radius corresponding to screen reach at the surface hit depth.
        const double ReachPx = std::max(VertexReachPx, EdgeReachPx);
        double WorldRadius = std::max(1.0e-4, ReachPx * Frame.WorldPerPixel * 1.5);
        const OrbitRay RayX = CastOrbitRay(Frame, X + ReachPx, Y);
        const OrbitRay RayY = CastOrbitRay(Frame, X, Y + ReachPx);
        const auto ProjectOffset = [&](const OrbitRay& R) -> double
        {
            const Vector3 RO{ R.Origin[0], R.Origin[1], R.Origin[2] };
            const Vector3 RD = Normalize(Vector3{ R.Direction[0], R.Direction[1], R.Direction[2] });
            const double Den = Dot(RD, Dir);
            if (std::fabs(Den) < 1.0e-9) return 0.0;
            const double T = Dot(RefHit->Position - RO, Dir) / Den;
            if (T <= 0.0) return 0.0;
            return Distance(RO + RD * T, RefHit->Position);
        };
        WorldRadius = std::max({ WorldRadius, ProjectOffset(RayX) * 1.6, ProjectOffset(RayY) * 1.6 });

        const double MaxDepth = RefHit->Distance + WorldRadius * 1.75;
        std::vector<uint32_t> Triangles;
        Triangles.reserve(384u);

        for (uint32_t SurfIdx = 0u; SurfIdx < Document.ReferenceSurfaces.size(); ++SurfIdx)
        {
            const auto& Surf = Document.ReferenceSurfaces[SurfIdx];
            if (!Surf.VisibleCondition || !Surf.Geometry) continue;
            // Quick surface AABB check against query sphere.
            const double Dx = RefHit->Position.X < Surf.MinBound[0] ? (Surf.MinBound[0] - RefHit->Position.X) : (RefHit->Position.X > Surf.MaxBound[0] ? (RefHit->Position.X - Surf.MaxBound[0]) : 0.0);
            const double Dy = RefHit->Position.Y < Surf.MinBound[1] ? (Surf.MinBound[1] - RefHit->Position.Y) : (RefHit->Position.Y > Surf.MaxBound[1] ? (RefHit->Position.Y - Surf.MaxBound[1]) : 0.0);
            const double Dz = RefHit->Position.Z < Surf.MinBound[2] ? (Surf.MinBound[2] - RefHit->Position.Z) : (RefHit->Position.Z > Surf.MaxBound[2] ? (RefHit->Position.Z - Surf.MaxBound[2]) : 0.0);
            if (Dx * Dx + Dy * Dy + Dz * Dz > WorldRadius * WorldRadius) continue;

            // Gather triangles in O(log N) via BVH.
            Triangles.clear();
            GatherReferenceTrianglesInRadius(Surf, RefHit->Position, WorldRadius, Triangles, 384u);
            const auto& Corners = Surf.Geometry->Corners;
            for (uint32_t Tri : Triangles)
            {
                const auto& C0 = Corners[size_t(Tri) * 3u + 0u];
                const auto& C1 = Corners[size_t(Tri) * 3u + 1u];
                const auto& C2 = Corners[size_t(Tri) * 3u + 2u];
                const Vector3 V0{ C0.Position[0], C0.Position[1], C0.Position[2] };
                const Vector3 V1{ C1.Position[0], C1.Position[1], C1.Position[2] };
                const Vector3 V2{ C2.Position[0], C2.Position[1], C2.Position[2] };
                const Vector3 N0{ C0.Normal[0], C0.Normal[1], C0.Normal[2] };
                const Vector3 N1{ C1.Normal[0], C1.Normal[1], C1.Normal[2] };
                const Vector3 N2{ C2.Normal[0], C2.Normal[1], C2.Normal[2] };
                const Vector3 FaceN = Cross(V1 - V0, V2 - V0);
                const std::string Label = PartCaption(SurfIdx, C0.PartIndex);

                if (VertexSnap)
                {
                    ConsiderVertex(V0, N0, Label, MaxDepth);
                    ConsiderVertex(V1, N1, Label, MaxDepth);
                    ConsiderVertex(V2, N2, Label, MaxDepth);
                }
                if (EdgeSnap)
                {
                    ConsiderEdge(V0, V1, FaceN, (C0.CreaseBits != 0u && C1.CreaseBits != 0u), Label, MaxDepth);
                    ConsiderEdge(V1, V2, FaceN, (C1.CreaseBits != 0u && C2.CreaseBits != 0u), Label, MaxDepth);
                    ConsiderEdge(V2, V0, FaceN, (C2.CreaseBits != 0u && C0.CreaseBits != 0u), Label, MaxDepth);
                }
            }

            // Also inspect nearby explicit crease/feature segments (exact boundary & sharp feature edges).
            const double LoX = RefHit->Position.X - WorldRadius, HiX = RefHit->Position.X + WorldRadius;
            const double LoY = RefHit->Position.Y - WorldRadius, HiY = RefHit->Position.Y + WorldRadius;
            const double LoZ = RefHit->Position.Z - WorldRadius, HiZ = RefHit->Position.Z + WorldRadius;
            for (const auto& Seg : Surf.Geometry->CreaseSegments)
            {
                if (std::max(Seg.Start[0], Seg.End[0]) < LoX || std::min(Seg.Start[0], Seg.End[0]) > HiX ||
                    std::max(Seg.Start[1], Seg.End[1]) < LoY || std::min(Seg.Start[1], Seg.End[1]) > HiY ||
                    std::max(Seg.Start[2], Seg.End[2]) < LoZ || std::min(Seg.Start[2], Seg.End[2]) > HiZ) continue;
                if (Seg.PartIndex < Surf.Parts.size() && !Surf.Parts[Seg.PartIndex].VisibleCondition) continue;
                const Vector3 A{ Seg.Start[0], Seg.Start[1], Seg.Start[2] };
                const Vector3 B{ Seg.End[0], Seg.End[1], Seg.End[2] };
                const std::string Label = PartCaption(SurfIdx, Seg.PartIndex);
                if (VertexSnap)
                {
                    ConsiderVertex(A, RefHit->Normal, Label, MaxDepth);
                    ConsiderVertex(B, RefHit->Normal, Label, MaxDepth);
                }
                if (EdgeSnap)
                {
                    ConsiderEdge(A, B, RefHit->Normal, true, Label, MaxDepth);
                }
            }
        }
    }
    else
    {
        // Cursor just outside the mesh silhouette: still allow snapping to nearby feature/boundary crease vertices & edges.
        for (uint32_t SurfIdx = 0u; SurfIdx < Document.ReferenceSurfaces.size(); ++SurfIdx)
        {
            const auto& Surf = Document.ReferenceSurfaces[SurfIdx];
            if (!Surf.VisibleCondition || !Surf.Geometry || Surf.Geometry->CreaseSegments.size() > 65536u) continue;
            for (const auto& Seg : Surf.Geometry->CreaseSegments)
            {
                if (Seg.PartIndex < Surf.Parts.size() && !Surf.Parts[Seg.PartIndex].VisibleCondition) continue;
                const Vector3 A{ Seg.Start[0], Seg.Start[1], Seg.Start[2] };
                const Vector3 B{ Seg.End[0], Seg.End[1], Seg.End[2] };
                const std::string Label = PartCaption(SurfIdx, Seg.PartIndex);
                if (VertexSnap)
                {
                    ConsiderVertex(A, Vector3{ 0.0, 0.0, 1.0 }, Label, 1.0e29);
                    ConsiderVertex(B, Vector3{ 0.0, 0.0, 1.0 }, Label, 1.0e29);
                }
                if (EdgeSnap)
                {
                    ConsiderEdge(A, B, Vector3{ 0.0, 0.0, 1.0 }, true, Label, 1.0e29);
                }
            }
        }
    }

    if (HaveVertex) return { SketcherSnapCategory::Endpoint, BestVertexPos, {}, BestVertexNormal, BestVertexSource, BestVertexPx };
    if (HaveEdge)   return { SketcherSnapCategory::Curve, BestEdgePos, BestEdgeTangent, BestEdgeNormal, BestEdgeSource, BestEdgePx };
    return {};
}
} // namespace

SketcherSnap SnapSketcherTrace(SketcherSnapRepository& Catalogue, const SketcherDocument& Document,
    const SketcherScene& Scene, const OrbitCameraBasis& Frame, double X, double Y, bool CurveSnap,
    bool AutoCurves, bool VertexSnap, bool EdgeSnap)
{
    SketcherSnapQuery Query;
    Query.Frame = Frame;
    Query.PixelX = X;
    Query.PixelY = Y;
    Query.PointReach = 10.0;
    Query.CurveReach = 12.0;

    // 1) Explicit Vertex snap (mesh vertices + CAD body vertices + authored curve endpoints).
    if (VertexSnap)
    {
        SketcherSnap MeshVert = SnapReferenceMeshFeatures(Document, Frame, X, Y, true, false, 10.0, 9.0);
        SketcherSnapQuery VQ = Query;
        VQ.Filter = { true, false, false, false, false, false, false, false, false };
        VQ.FreeCurvesOnly = false;
        VQ.BodyCurvesOnly = false;
        VQ.OverlayCurvesVisible = true;
        SketcherSnap CadVert = FindSketcherSnap(Catalogue, Document, Scene, VQ);
        if (MeshVert.Kind != SketcherSnapCategory::None && CadVert.Kind != SketcherSnapCategory::None)
            return MeshVert.Pixels <= CadVert.Pixels ? MeshVert : CadVert;
        if (MeshVert.Kind != SketcherSnapCategory::None) return MeshVert;
        if (CadVert.Kind != SketcherSnapCategory::None)  return CadVert;
    }

    // 2) Explicit Edge snap (mesh edges/creases + CAD body edges + authored curves).
    if (EdgeSnap)
    {
        SketcherSnap MeshEdge = SnapReferenceMeshFeatures(Document, Frame, X, Y, false, true, 10.0, 9.0);
        SketcherSnapQuery EQ = Query;
        EQ.Filter = { false, false, false, false, false, false, false, true, false };
        EQ.FreeCurvesOnly = false;
        EQ.BodyCurvesOnly = false;
        EQ.OverlayCurvesVisible = true;
        SketcherSnap CadEdge = FindSketcherSnap(Catalogue, Document, Scene, EQ);
        if (MeshEdge.Kind != SketcherSnapCategory::None && CadEdge.Kind != SketcherSnapCategory::None)
            return MeshEdge.Pixels <= CadEdge.Pixels ? MeshEdge : CadEdge;
        if (MeshEdge.Kind != SketcherSnapCategory::None) return MeshEdge;
        if (CadEdge.Kind != SketcherSnapCategory::None)  return CadEdge;
    }

    const bool Curves = CurveSnap || AutoCurves;
    Query.Filter = { false, false, false, false, false, false, false, Curves, !Curves };
    Query.FreeCurvesOnly = Curves;
    Query.OverlayCurvesVisible = Curves; // Authored curves are visibly drawn over the reference.
    // Ctrl prioritises an authored curve, then a CAD edge, then the surface.
    SketcherSnap Snap;
    if (Curves) {
        // Exact endpoints beat a nearby interior foot; consumed profiles are excluded.
        Query.Filter.EndpointEnabled = true; Query.Filter.CurveEnabled = false; Query.PointReach=8;
        Snap=FindSketcherSnap(Catalogue,Document,Scene,Query);
        Query.Filter.EndpointEnabled = false; Query.Filter.CurveEnabled = true;
    }
    if(Snap.Kind==SketcherSnapCategory::None) Snap = FindSketcherSnap(Catalogue, Document, Scene, Query);
    if (CurveSnap && Snap.Kind == SketcherSnapCategory::None)
    {
        Query.FreeCurvesOnly = false;
        Query.BodyCurvesOnly = true;
        Snap = FindSketcherSnap(Catalogue, Document, Scene, Query);
    }
    if (Curves && Snap.Kind == SketcherSnapCategory::None)
    {
        Query.Filter.CurveEnabled = false;
        Query.Filter.FaceEnabled = true;
        Snap = FindSketcherSnap(Catalogue, Document, Scene, Query);
    }
    if (Snap.Kind == SketcherSnapCategory::Face)
    {
        double PX, PY;
        // Newton refinement at a silhouette can land on a different branch. Never
        // accept an off-ray foot as though it were the surface under the cursor.
        if (!ProjectSketcherPoint(Frame, Snap.Position, PX, PY) || std::hypot(PX - X, PY - Y) > 0.75) return {};
    }
    return Snap;
}

SketcherControlTrace MakeSketcherControlTrace(const std::vector<Vector3>& Controls,
    SketcherDrawingCategory Kind, SketchSplineForm Form, bool Closed)
{
    SketcherControlTrace Result;
    if (Controls.size() < 2) { Result.Refusal = "Place at least two controls"; return Result; }
    for (size_t I = 0; I < Controls.size(); ++I)
    {
        const auto P = Controls[I];
        if (!std::isfinite(P.X) || !std::isfinite(P.Y) || !std::isfinite(P.Z))
        { Result.Refusal = "A control is not finite"; return Result; }
        if (I && Distance(P, Controls[I-1]) <= 1e-12)
        { Result.Refusal = "The same control twice in a row"; return Result; }
    }
    if (Closed && (Controls.size() < 4 || Distance(Controls.front(), Controls.back()) > 1e-12))
    { Result.Refusal = "Close by explicitly placing the last control on the first (at least three distinct controls)"; return Result; }
    if (Kind == SketcherDrawingCategory::Arc)
    {
        if (Controls.size() != 3) { Result.Refusal = "Arc: place start, end, then a point on the arc"; return Result; }
        const auto X = Controls[1] - Controls[0], N = Cross(X, Controls[2] - Controls[0]);
        if (Length(N) < 1e-12) { Result.Refusal = "Arc points are collinear"; return Result; }
        SketcherDrawingRequest Request;
        Request.Kind = Kind; Request.PlaneCondition = true;
        Request.Plane.Origin = Controls[0]; Request.Plane.XAxis = Normalize(X);
        Request.Plane.ZAxis = Normalize(N); Request.Plane.YAxis = Cross(Request.Plane.ZAxis, Request.Plane.XAxis);
        for (auto P : Controls) { const auto D = P - Request.Plane.Origin; Request.Points.push_back({Dot(D,Request.Plane.XAxis),Dot(D,Request.Plane.YAxis)}); }
        SketchModel Model;
        Result.Refusal = ComposeSketcherSketch(Request, Model);
        if (!Result.Refusal.empty()) return Result;
        for (auto P : SampleSketchCurve(Model, Model.Curves.front()))
            Result.Samples.push_back(Request.Plane.Origin + Request.Plane.XAxis * P.X + Request.Plane.YAxis * P.Y);
        Result.Arc = Request;
        return Result;
    }
    if (Kind != SketcherDrawingCategory::Line && Kind != SketcherDrawingCategory::Curve)
    { Result.Refusal = "Use Line, Curve or Arc"; return Result; }
    if (Kind == SketcherDrawingCategory::Curve && Form != SketchSplineForm::Nurbs && Form != SketchSplineForm::Bezier)
    {
        const std::vector<Vector3> Seed = SampleThroughCurve(Controls, Closed, Form, 32);
        if (Seed.size() < 2) { Result.Refusal = "Curve form takes no curve through these controls"; return Result; }
        double Chord = 0.0;
        for (size_t I = 1; I < Seed.size(); ++I) Chord += Distance(Seed[I - 1], Seed[I]);
        CurveFitSettings Settings;
        Settings.Tolerance = std::max(1.0e-5, Chord * 2.5e-4);
        Settings.Fairing   = 1.0e-10;
        Settings.SpanLimit = std::max(48, int(Controls.size()) * 2);
        const CurveFitOutcome Fitted = FitSpaceCurve(Seed, Settings);
        if (!Fitted.Refusal.empty() || !Fitted.Curve.Definition)
        {
            Result.Refusal = Fitted.Refusal.empty() ? "Curve fit failed" : Fitted.Refusal;
            return Result;
        }
        NurbsCurveDefinition Exact = *Fitted.Curve.Definition;
        Exact.Poles.front() = Controls.front();
        Exact.Poles.back()  = Closed ? Controls.front() : Controls.back();
        Result.Curve.Definition = std::make_shared<NurbsCurveDefinition>(std::move(Exact));
        const int Samples = std::clamp(int(Controls.size()) * 24, 48, 512);
        for (int I = 0; I <= Samples; ++I)
            Result.Samples.push_back(EvaluateNurbsCurve(*Result.Curve.Definition, double(I) / Samples).Position);
        return Result;
    }
    const int Degree = Kind == SketcherDrawingCategory::Line ? 1 : Form == SketchSplineForm::Bezier ? int(Controls.size()) - 1 : std::min(3, int(Controls.size()) - 1);
    if (Degree > 15) { Result.Refusal = "Bezier supports at most 16 controls; use NURBS"; return Result; }
    NurbsCurveDefinition Definition;
    Definition.Degree = Degree;
    Definition.Poles = Controls; // Exact count, exact snapped XYZ values. No fitting or insertion.
    Definition.Weights.assign(Controls.size(), 1.0);
    Definition.Knots.assign(Degree + 1, 0.0);
    const int Spans = int(Controls.size()) - Degree;
    for (int I = 1; I < Spans; ++I) Definition.Knots.push_back(double(I) / Spans);
    Definition.Knots.insert(Definition.Knots.end(), Degree + 1, 1.0);
    Result.Curve.Definition = std::make_shared<NurbsCurveDefinition>(std::move(Definition));
    if (Degree <= 1)
    {
        Result.Samples = Controls;
    }
    else
    {
        const int Samples = std::clamp(int(Controls.size()) * 24, 48, 512);
        for (int I = 0; I <= Samples; ++I)
            Result.Samples.push_back(EvaluateNurbsCurve(*Result.Curve.Definition, double(I) / Samples).Position);
    }
    return Result;
}

SketcherControlTrace ConstructReferenceConformingTrace(const SketcherDocument& Document,
    const std::vector<Vector3>& Controls, SketcherDrawingCategory Kind, SketchSplineForm Form, bool Closed)
{
    SketcherControlTrace Fallback = MakeSketcherControlTrace(Controls, Kind, Form, Closed);
    if (!Fallback.Refusal.empty() || Fallback.Arc || Document.ReferenceSurfaces.empty() || !Fallback.Curve.Definition)
    {
        return Fallback;
    }

    double TotalChord = 0.0;
    for (size_t I = 1; I < Controls.size(); ++I) TotalChord += Distance(Controls[I - 1], Controls[I]);
    if (!(TotalChord > 1.0e-9)) return Fallback;

    const double SearchRadius = std::max(0.05, TotalChord * 0.25);

    // Estimate a consistent outward panel normal from the clicked controls so Solidify shells always hit the OUTER skin
    Vector3 RefCentroid{ 0.0, 0.0, 0.0 };
    int RefCount = 0;
    for (const SketcherReferenceSurface& Surf : Document.ReferenceSurfaces)
    {
        if (!Surf.VisibleCondition || !Surf.Geometry || Surf.Geometry->Corners.empty()) continue;
        RefCentroid = RefCentroid + Vector3{ 0.5 * (Surf.MinBound[0] + Surf.MaxBound[0]),
                                             0.5 * (Surf.MinBound[1] + Surf.MaxBound[1]),
                                             0.5 * (Surf.MinBound[2] + Surf.MaxBound[2]) };
        ++RefCount;
    }
    if (RefCount > 0) RefCentroid = RefCentroid * (1.0 / double(RefCount));

    Vector3 PanelNormal{ 0.0, 0.0, 0.0 };
    for (const Vector3& Ctrl : Controls)
    {
        if (const auto Hit = ProjectOntoReferenceSurfaces(Document.ReferenceSurfaces, Ctrl, SearchRadius * 0.2))
        {
            Vector3 N = Hit->Normal;
            if (Length(PanelNormal) > 0.5)
            {
                if (Dot(N, PanelNormal) < 0.0) N = N * -1.0;
            }
            else if (Dot(N, Ctrl - RefCentroid) < 0.0)
            {
                N = N * -1.0;
            }
            PanelNormal = PanelNormal + N;
        }
    }
    const bool PanelNormalReady = Length(PanelNormal) > 1.0e-6;
    if (PanelNormalReady) PanelNormal = Normalize(PanelNormal);

    auto ProjectOuterSkin = [&](const Vector3& QueryPt) -> std::optional<Vector3>
    {
        if (PanelNormalReady)
        {
            const double RayLift = std::max(0.05, TotalChord * 0.15);
            const Vector3 RayStart = QueryPt + PanelNormal * RayLift;
            const Vector3 RayDir   = PanelNormal * -1.0;
            if (const auto RayHit = IntersectReferenceSurfaces(Document.ReferenceSurfaces, RayStart, RayDir, RayLift * 2.5))
            {
                if (Distance(RayHit->Position, QueryPt) <= SearchRadius) return RayHit->Position;
            }
        }
        if (const auto ClosestHit = ProjectOntoReferenceSurfaces(Document.ReferenceSurfaces, QueryPt, SearchRadius))
        {
            return ClosestHit->Position;
        }
        return std::nullopt;
    };

    const int DenseCount = std::clamp(int(Controls.size()) * 16, 64, 512);
    std::vector<Vector3> Conformed;
    Conformed.reserve(size_t(DenseCount + 1));

    int HitSamples = 0;
    double MaxLift = 0.0;
    for (int Step = 0; Step <= DenseCount; ++Step)
    {
        const double Param = double(Step) / double(DenseCount);
        const Vector3 SeedPoint = EvaluateNurbsCurve(*Fallback.Curve.Definition, Param).Position;
        Vector3 Placed = SeedPoint;
        if (Step == 0)
        {
            Placed = Controls.front();
            ++HitSamples;
        }
        else if (Step == DenseCount)
        {
            Placed = Closed ? Controls.front() : Controls.back();
            ++HitSamples;
        }
        else if (const auto OuterPt = ProjectOuterSkin(SeedPoint))
        {
            MaxLift = std::max(MaxLift, Distance(SeedPoint, *OuterPt));
            Placed  = *OuterPt;
            ++HitSamples;
        }
        if (Conformed.empty() || Distance(Conformed.back(), Placed) > 1.0e-9 * std::max(1.0, TotalChord))
        {
            Conformed.push_back(Placed);
        }
    }
    if (HitSamples < DenseCount / 2 || Conformed.size() < 3) return Fallback;
    Conformed.front() = Controls.front();
    Conformed.back()  = Closed ? Controls.front() : Controls.back();
    if (MaxLift <= std::max(1.0e-5, TotalChord * 2.0e-4)) return Fallback;

    // 2-sweep gentle fairing + outer-skin re-projection keeps curves smooth and flush on the exact outer skin
    for (int Sweep = 0; Sweep < 2; ++Sweep)
    {
        std::vector<Vector3> Smoothed = Conformed;
        for (size_t I = 1; I + 1 < Conformed.size(); ++I)
        {
            const Vector3 Avg = (Conformed[I - 1] + Conformed[I] * 2.0 + Conformed[I + 1]) * 0.25;
            if (const auto OuterPt = ProjectOuterSkin(Avg)) Smoothed[I] = *OuterPt;
            else Smoothed[I] = Avg;
        }
        Conformed = std::move(Smoothed);
    }

    CurveFitSettings Settings;
    Settings.Tolerance = std::max(2.0e-4, TotalChord * 4.0e-4);
    Settings.Fairing   = 1.0e-7;
    Settings.SpanLimit = std::clamp(int(Controls.size()) * 3, 18, 96);
    CurveFitOutcome Fitted = FitSpaceCurve(Conformed, Settings);
    if (!Fitted.Refusal.empty() || !Fitted.Curve.Definition || Fitted.Curve.Definition->Poles.size() < 2)
    {
        Settings.Tolerance = std::max(1.0e-3, TotalChord * 2.0e-3);
        Settings.SpanLimit = std::clamp(int(Controls.size()) * 4, 28, 128);
        Fitted = FitSpaceCurve(Conformed, Settings);
    }
    if (!Fitted.Refusal.empty() || !Fitted.Curve.Definition || Fitted.Curve.Definition->Poles.size() < 2)
    {
        return Fallback;
    }

    NurbsCurveDefinition Exact = *Fitted.Curve.Definition;
    Exact.Poles.front() = Controls.front();
    Exact.Poles.back()  = Closed ? Controls.front() : Controls.back();

    SketcherControlTrace ConformedTrace;
    ConformedTrace.Curve.Definition = std::make_shared<NurbsCurveDefinition>(std::move(Exact));
    const int DisplaySamples = std::max(48, int(ConformedTrace.Curve.Definition->Poles.size()) * 12);
    ConformedTrace.Samples.reserve(static_cast<size_t>(DisplaySamples + 1));
    for (int I = 0; I <= DisplaySamples; ++I)
    {
        ConformedTrace.Samples.push_back(EvaluateNurbsCurve(*ConformedTrace.Curve.Definition, double(I) / DisplaySamples).Position);
    }
    return ConformedTrace;
}

SketcherSurfaceTrace ProjectSketcherTrace(SketcherSnapRepository& Catalogue, const SketcherDocument& Document,
    const SketcherScene& Scene, const OrbitCameraBasis& Frame, const std::vector<Vector3>& Controls,
    SketcherDrawingCategory Kind, SketchSplineForm Form, bool Closed, bool Fit, double Tolerance)
{
    SketcherSurfaceTrace Result;
    if (Controls.size() < 2 || Controls.size() > 65 || !(Tolerance > 0.0) || !std::isfinite(Tolerance))
    {
        Result.Refusal = "Surface trace: use 2–65 distinct controls";
        return Result;
    }
    if (Closed && Controls.size() < 3)
    { Result.Refusal = "Surface trace: a closed spline needs at least three controls"; return Result; }
    // Camera changes must never silently move a clicked control from the back
    // of a sheet onto its newly visible front. All controls must still be on
    // the visible source (body-edge controls may lie on its exact silhouette).
    for (Vector3 Control : Controls)
    {
        double X, Y;
        if (!ProjectSketcherPoint(Frame, Control, X, Y))
        { Result.Refusal = "Surface trace: a control left the view; restore the view or split the section"; return Result; }
        SketcherSnap Hit = SnapSketcherTrace(Catalogue, Document, Scene, Frame, X, Y, false);
        if (Hit.Kind == SketcherSnapCategory::None || Distance(Hit.Position, Control) > Tolerance)
            Hit = SnapSketcherTrace(Catalogue, Document, Scene, Frame, X, Y, true);
        if (Hit.Kind == SketcherSnapCategory::None || Distance(Hit.Position, Control) > Tolerance)
        { Result.Refusal = "Surface trace: a control is hidden / off the current surface; restore the view or split the section"; return Result; }
    }
    std::vector<Vector3> Seed;
    if (Kind == SketcherDrawingCategory::Arc)
    {
        if (Controls.size() != 3) { Result.Refusal = "Surface arc: click start, end, then a point on the arc"; return Result; }
        const Vector3 Along = Controls[1] - Controls[0];
        const Vector3 Normal = Cross(Along, Controls[2] - Controls[0]);
        if (Length(Along) < 1.0e-10 || Length(Normal) < 1.0e-12)
        { Result.Refusal = "Surface arc: the three points are collinear"; return Result; }
        OrthonormalBasis Plane;
        Plane.Origin = Controls[0]; Plane.XAxis = Normalize(Along); Plane.ZAxis = Normalize(Normal);
        Plane.YAxis = Cross(Plane.ZAxis, Plane.XAxis);
        SketcherDrawingRequest Request;
        Request.Kind = Kind;
        Request.FillCondition = false;
        for (Vector3 Point : Controls)
        {
            const Vector3 Delta = Point - Plane.Origin;
            Request.Points.push_back({ Dot(Delta, Plane.XAxis), Dot(Delta, Plane.YAxis) });
        }
        SketchModel Sketch;
        Result.Refusal = ComposeSketcherSketch(Request, Sketch);
        if (!Result.Refusal.empty()) return Result;
        const ProfileSegment Arc = ResolveSketchCurveSegment(Sketch, 0, 1.0e-10, Result.Refusal);
        if (!Result.Refusal.empty()) return Result;
        const int Steps = Fit ? 128 : 48;
        for (int Index = 0; Index <= Steps; ++Index)
        {
            const Vector2 Point = EvaluateProfileSegment(Arc, double(Index) / double(Steps));
            Seed.push_back(Plane.Origin + Plane.XAxis * Point.X + Plane.YAxis * Point.Y);
        }
    }
    else if (Kind == SketcherDrawingCategory::Line)
    {
        for (size_t Span = 1; Span < Controls.size(); ++Span)
            for (int Step = Span == 1 ? 0 : 1; Step <= 32; ++Step)
                Seed.push_back(Controls[Span - 1] * (1.0 - double(Step) / 32.0) + Controls[Span] * (double(Step) / 32.0));
        if (Closed) { Result.Refusal = "Surface polyline: finish open sections separately"; return Result; }
    }
    else if (Kind == SketcherDrawingCategory::Curve) Seed = SampleThroughCurve(Controls, Closed, Form, Fit ? 48 : 16);
    else { Result.Refusal = "Surface trace supports Curve, Arc and Line; turn Surface off for planar shapes"; return Result; }
    if (Seed.size() < 2) { Result.Refusal = "Surface trace: cannot form the seed curve"; return Result; }
    // Bounded work even with a long clicked path. Each finished fit also receives
    // an independent, denser surface check below; a sample-only fit is not enough.
    const size_t Count = std::min<size_t>(Fit ? 513 : 129, Seed.size());
    for (size_t Index = 0; Index < Count; ++Index)
    {
        const Vector3 Point = Seed[Index * (Seed.size() - 1) / (Count - 1)];
        double X, Y;
        if (!ProjectSketcherPoint(Frame, Point, X, Y))
        { Result.Refusal = "Surface trace leaves the view; draw a shorter section"; return Result; }
        SketcherSnap Hit = SnapSketcherTrace(Catalogue, Document, Scene, Frame, X, Y, false);
        if (Hit.Kind == SketcherSnapCategory::None && (Index == 0 || Index + 1 == Count))
        {
            // Exact boundary controls may project a fraction beyond the faceted
            // silhouette. Only an exact BODY edge at the same 3D point is valid.
            Hit = SnapSketcherTrace(Catalogue, Document, Scene, Frame, X, Y, true);
            if (Hit.Kind != SketcherSnapCategory::Curve || Distance(Hit.Position, Point) > Tolerance) Hit = {};
        }
        if (Hit.Kind == SketcherSnapCategory::None)
        { Result.Refusal = "Surface trace crosses a gap / silhouette; add controls or split the curve"; return Result; }
        if (Result.Samples.empty() || Distance(Result.Samples.back(), Hit.Position) > 1.0e-10) Result.Samples.push_back(Hit.Position);
    }
    if (Result.Samples.size() < 2) { Result.Refusal = "Surface trace: coincident points"; return Result; }
    if (Closed) Result.Samples.back() = Result.Samples.front();
    if (!Fit) return Result;
    CurveFitSettings Settings;
    Settings.Tolerance = Tolerance * 0.25;
    Settings.Fairing = 1.0e-10;
    Settings.SpanLimit = 64;
    for (int Attempt = 0; Attempt < 3; ++Attempt)
    {
        Result.Fit = FitSpaceCurve(Result.Samples, Settings);
        if (!Result.Fit.Refusal.empty()) { Result.Refusal = Result.Fit.Refusal; return Result; }
        Result.WorstSurfaceDeviation = 0.0;
        bool Miss = false;
        for (int Step = 0; Step <= 256; ++Step)
        {
            const Vector3 Point = EvaluateNurbsCurve(*Result.Fit.Curve.Definition, double(Step) / 256.0).Position;
            double X, Y;
            if (!ProjectSketcherPoint(Frame, Point, X, Y)) { Miss = true; break; }
            SketcherSnap Hit = SnapSketcherTrace(Catalogue, Document, Scene, Frame, X, Y, false);
            if (Hit.Kind == SketcherSnapCategory::None && (Step == 0 || Step == 256))
            {
                Hit = SnapSketcherTrace(Catalogue, Document, Scene, Frame, X, Y, true);
                if (Hit.Kind != SketcherSnapCategory::Curve || Distance(Hit.Position, Point) > Tolerance) Hit = {};
            }
            if (Hit.Kind == SketcherSnapCategory::None) { Miss = true; break; }
            Result.WorstSurfaceDeviation = std::max(Result.WorstSurfaceDeviation, Distance(Point, Hit.Position));
        }
        if (!Miss && Result.WorstSurfaceDeviation <= Tolerance) return Result;
        Settings.Tolerance *= 0.25;
    }
    Result.Refusal = "Surface trace cannot meet the 0.2 mm sampled surface check; add controls or draw a shorter section";
    return Result;
}
}
