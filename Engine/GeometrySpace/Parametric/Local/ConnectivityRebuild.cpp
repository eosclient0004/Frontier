/*====================================================================================================================================
                                                      TOPOLOGYREBUILD.CPP
====================================================================================================================================*/
// 🧩 Topology-changing face verbs by direct blueprint rebuild

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Local/ConnectivityRebuild.h"

#include "Engine/GeometrySpace/Parametric/Construction/BodyArrangement.h"
#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/ProjectedCurve.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchSpline.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/CurveNetwork.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <tuple>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PRIVATE HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double τPeriod = 2.0 * std::numbers::pi;                                        // [-] - Period of conic carriers

    // 📝 A body re-expressed as a blueprint in inventory order (vertex / edge / face indices = inventory positions), the
    //    common starting point of every rebuild verb.
    struct BodyDescription
    {
        BodyBlueprint           Blueprint   = {};                                            // [-] - Faithful copy
        BodyInventory           Inventory   = {};                                            // [-] - Source records
        std::map<uint32_t, int> VertexIndex = {};                                            // [-] - Vertex slot → index
        std::map<uint32_t, int> EdgeIndex   = {};                                            // [-] - Edge slot → index
        std::map<uint32_t, int> FaceIndex   = {};                                            // [-] - Face slot → index
        double                  Scale       = 1.0;                                           // [len] - Bounding diagonal
    };

    [[nodiscard]] std::string DescribeBody(const ModelPartition& Partition, BodyToken Body, BodyDescription& Description)
    {
        Description.Inventory = Partition.CollectInventory(Body);
        Vector3 Low{ 1e300, 1e300, 1e300 }, High{ -1e300, -1e300, -1e300 };
        for (size_t Index = 0; Index < Description.Inventory.Vertices.size(); ++Index)
        {
            const Vector3 Position = Partition.Vertices.Resolve(Description.Inventory.Vertices[Index]).Position;
            Description.VertexIndex[Description.Inventory.Vertices[Index].Slot] = static_cast<int>(Index);
            Description.Blueprint.Vertices.push_back({ Position });
            Low  = { std::min(Low.X, Position.X), std::min(Low.Y, Position.Y), std::min(Low.Z, Position.Z) };
            High = { std::max(High.X, Position.X), std::max(High.Y, Position.Y), std::max(High.Z, Position.Z) };
        }
        Description.Scale = std::max(1.0e-300, Distance(Low, High));
        for (size_t Index = 0; Index < Description.Inventory.Edges.size(); ++Index)
        {
            const EdgeProfile& Profile = Partition.Edges.Resolve(Description.Inventory.Edges[Index]);
            Description.EdgeIndex[Description.Inventory.Edges[Index].Slot] = static_cast<int>(Index);
            Description.Blueprint.Edges.push_back({ Partition.Curves.Resolve(Profile.Curve), Profile.IntervalLow, Profile.IntervalHigh,
                                                    Description.VertexIndex.at(Profile.Start.Slot), Description.VertexIndex.at(Profile.End.Slot) });
        }
        for (size_t Index = 0; Index < Description.Inventory.Faces.size(); ++Index)
        {
            const FaceProfile& Profile = Partition.Faces.Resolve(Description.Inventory.Faces[Index]);
            Description.FaceIndex[Description.Inventory.Faces[Index].Slot] = static_cast<int>(Index);
            BlueprintFace Planned{ Partition.Surfaces.Resolve(Profile.Surface), Profile.Sense, {} };
            std::string   Refusal;
            for (LoopToken Loop : Profile.Loops)
            {
                std::vector<BlueprintUse> Uses;
                Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
                {
                    const FinProfile& Use = Partition.Fins.Resolve(Fin);
                    if (!Use.SurfaceCurve) { Refusal = "fin without an SP-curve"; return; }
                    Uses.push_back({ Description.EdgeIndex.at(Use.Edge.Slot), Use.Sense, *Use.SurfaceCurve });
                });
                if (!Refusal.empty()) return Refusal;
                Planned.Loops.push_back(std::move(Uses));
            }
            Description.Blueprint.Faces.push_back(std::move(Planned));
        }
        Description.Blueprint.Category = Partition.Bodies.Resolve(Body).Category;
        return {};
    }

    // 📝 SP-curves for a whole loop on a (possibly periodic) carrier: the chart image is tracked continuously along the
    //    loop (each projection referenced to the previous one), then shifted by whole periods so the loop starts in the
    //    principal window. Seams get u = 0 on one use and u = 2π on the other by construction.
    void FitLoopSurfaceCurves(const SurfaceGeometry& Surface, const BodyBlueprint& Blueprint, std::vector<BlueprintUse>& Loop)
    {
        constexpr int        Steps   = 24;
        std::vector<Vector2> Anchors;
        Vector2              Cursor  = {};
        bool                 Started = false;
        Vector2              Least{ 1e300, 1e300 };
        for (const BlueprintUse& Use : Loop)
        {
            const BlueprintEdge& Edge  = Blueprint.Edges[Use.Edge];
            const double         From  = Use.Sense == Orientation::Forward ? Edge.Low : Edge.High;
            const double         Until = Use.Sense == Orientation::Forward ? Edge.High : Edge.Low;
            Vector2              Middle{};
            for (int Step = 0; Step <= Steps; ++Step)
            {
                const Vector3 Point = EvaluateCurve(Edge.Curve, From + (Until - From) * Step / Steps).Position;
                Cursor  = ProjectOntoSurface(Surface, Point, Started ? Cursor : Vector2{});
                Started = true;
                Least   = { std::min(Least.X, Cursor.X), std::min(Least.Y, Cursor.Y) };
                if (Step == Steps / 2) Middle = Cursor;
            }
            Anchors.push_back(Middle);
        }
        const SurfacePeriodicity Period = ResolvePeriodicity(Surface);
        Vector2 Shift{};
        if (Period.PeriodU > 0.0) Shift.X = -std::floor((Least.X + 1.0e-9) / Period.PeriodU) * Period.PeriodU;
        if (Period.PeriodV > 0.0) Shift.Y = -std::floor((Least.Y + 1.0e-9) / Period.PeriodV) * Period.PeriodV;
        for (size_t Index = 0; Index < Loop.size(); ++Index)
        {
            const BlueprintEdge& Edge   = Blueprint.Edges[Loop[Index].Edge];
            const Vector2        Anchor = Anchors[Index] + Shift;
            Loop[Index].SurfaceCurve = FitSurfaceCurve(Surface, Edge.Curve, Edge.Low, Edge.High, { Anchor - Vector2{ 1.0, 1.0 }, Anchor + Vector2{ 1.0, 1.0 } }, Anchor);
        }
    }

    [[nodiscard]] double SignOfSense(Orientation Sense) { return Sense == Orientation::Forward ? 1.0 : -1.0; }

    // Assemble, audit, histories; the shared tail of every rebuild verb.
    [[nodiscard]] std::string Conclude(ModelPartition& Partition, const BodyBlueprint& Blueprint, const std::string& Label, const std::vector<FaceChangeHistory>& Planned,
                                       BodyToken& Result, std::vector<FaceChangeHistory>& Histories, AuditReport& Audit)
    {
        const ArrangementOutcome Assembly = AssembleBody(Partition, Blueprint, Label);
        if (!Assembly.Refusal.empty()) return Assembly.Refusal;
        Audit = AuditBody(Partition, Assembly.Body);
        if (!Audit.SoundCondition()) return "result fails audit " + std::string(Audit.Findings.front().Code) + " on " + Audit.Findings.front().Subject + ": " + Audit.Findings.front().Detail;
        if (std::string Clash = DetectSelfClash(Partition, Assembly.Body); !Clash.empty()) return Clash;
        Histories = Planned;
        for (size_t Index = 0; Index < Histories.size(); ++Index) Histories[Index].Result = Assembly.Faces[Index];
        Result = Assembly.Body;
        return {};
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                SURFACE EQUIVALENCE (HEAL)
    //--------------------------------------------------------------------------------------------------------------------

    // Same point set and same outward side: planes and coaxial cylinders (the kept face's chart is re-fitted over the
    // merged boundary by FitLoopSurfaceCurves); cones, spheres and tori are 🚧.
    [[nodiscard]] bool SameOutwardCarrier(const BlueprintFace& First, const BlueprintFace& Second, double Tolerance)
    {
        if (const auto* One = std::get_if<PlaneSurface>(&First.Surface))
        {
            const auto* Two = std::get_if<PlaneSurface>(&Second.Surface);
            if (!Two) return false;
            const Vector3 OneNormal = One->Frame.ZAxis * SignOfSense(First.Sense);
            const Vector3 TwoNormal = Two->Frame.ZAxis * SignOfSense(Second.Sense);
            return Dot(OneNormal, TwoNormal) > 1.0 - 1.0e-12 && std::fabs(Dot(Two->Frame.Origin - One->Frame.Origin, OneNormal)) <= Tolerance;
        }
        if (const auto* One = std::get_if<CylinderSurface>(&First.Surface))
        {
            const auto* Two = std::get_if<CylinderSurface>(&Second.Surface);
            if (!Two || First.Sense != Second.Sense || std::fabs(One->Radius - Two->Radius) > Tolerance) return false;
            const Vector3 Gap = Two->Frame.Origin - One->Frame.Origin;
            return std::fabs(Dot(One->Frame.ZAxis, Two->Frame.ZAxis)) > 1.0 - 1.0e-12 && Length(Gap - One->Frame.ZAxis * Dot(Gap, One->Frame.ZAxis)) <= Tolerance;
        }
        return false;
    }

    [[nodiscard]] bool SameCarrierCurve(const CurveGeometry& First, const CurveGeometry& Second, double Tolerance)
    {
        if (const auto* One = std::get_if<StraightLine>(&First))
        {
            const auto* Two = std::get_if<StraightLine>(&Second);
            if (!Two || Length(Cross(One->Direction, Two->Direction)) > 1.0e-12) return false;
            const Vector3 Gap = Two->Origin - One->Origin;
            return Length(Gap - One->Direction * Dot(Gap, One->Direction)) <= Tolerance;
        }
        if (const auto* One = std::get_if<CircularArc>(&First))
        {
            const auto* Two = std::get_if<CircularArc>(&Second);
            return Two && Distance(One->Frame.Origin, Two->Frame.Origin) <= Tolerance && std::fabs(One->Radius - Two->Radius) <= Tolerance &&
                   std::fabs(Dot(One->Frame.ZAxis, Two->Frame.ZAxis)) > 1.0 - 1.0e-12;
        }
        return false;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    EXTRUDE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Corpus E "extrudefaces": each selected planar face moves by Distance along its outward normal n (carrier translated,
//    same chart, so its SP-curves carry over) and a ring of side faces closes the gap: per boundary fin A → B (direction t)
//    a side face on the plane / cylinder swept by the edge along n, outward t × n, loop [edge, rise at B, top edge
//    reversed, rise at A reversed]. A closed circle reuses its single rise as the cylinder seam. The old neighbours are
//    untouched, so side faces meet them coplanar (Heal merges them).
std::string ExtrudePlanarFaces(ModelPartition& Partition, BodyToken Body, const std::vector<FaceToken>& Faces, double Distance, BodyToken& Result,
                               std::vector<FaceChangeHistory>& Histories, AuditReport& Audit, std::string& Method)
{
    if (Faces.empty()) return "no faces selected";
    if (!(Distance > 0.0)) return "extrusion distance must be positive (use push / offset to cut in)";
    BodyDescription Description;
    if (std::string Refusal = DescribeBody(Partition, Body, Description); !Refusal.empty()) return Refusal;
    BodyBlueprint&                 Blueprint = Description.Blueprint;
    std::vector<FaceChangeHistory> Planned;
    for (size_t Index = 0; Index < Blueprint.Faces.size(); ++Index) Planned.push_back({ {}, FaceChangeOrigin::Unchanged, Description.Inventory.Faces[Index], {} });
    std::set<int> Selected;
    for (FaceToken Face : Faces) Selected.insert(Description.FaceIndex.at(Face.Slot));
    std::map<int, std::set<int>> EdgeUsers;                                                  // Edge → selected faces using it
    for (int Face : Selected)
    {
        if (!std::holds_alternative<PlaneSurface>(Blueprint.Faces[Face].Surface)) return LabelToken("F", Description.Inventory.Faces[Face]) + " is not planar";
        for (const auto& Loop : Blueprint.Faces[Face].Loops) for (const BlueprintUse& Use : Loop) EdgeUsers[Use.Edge].insert(Face);
        for (const auto& Loop : Blueprint.Faces[Face].Loops)
        {
            for (const BlueprintUse& Use : Loop)
            {
                if (std::holds_alternative<ContactCurve>(Blueprint.Edges[Use.Edge].Curve)) return LabelToken("F", Description.Inventory.Faces[Face]) + " is bounded by a rolling-ball contact rail (🚧 ruled sides over procedural edges)";
                if (std::holds_alternative<IntersectionCurve>(Blueprint.Edges[Use.Edge].Curve)) return LabelToken("F", Description.Inventory.Faces[Face]) + " is bounded by an intersection curve (🚧 ruled sides over traced edges)";
            }
        }
    }
    for (const auto& [Edge, Users] : EdgeUsers) if (Users.size() > 1) return "two selected faces share an edge (🚧 joint extrusion)";

    int SideFaces = 0;
    for (int Face : Selected)
    {
        BlueprintFace& Top   = Blueprint.Faces[Face];
        const auto*    Plane = std::get_if<PlaneSurface>(&Top.Surface);
        if (!Plane) return LabelToken("F", Description.Inventory.Faces[Face]) + " is not planar";
        const Vector3 Normal = Plane->Frame.ZAxis * SignOfSense(Top.Sense);
        const Vector3 Lift   = Normal * Distance;
        PlaneSurface  Raised = *Plane;
        Raised.Frame.Origin  = Plane->Frame.Origin + Lift;
        Top.Surface          = Raised;
        Planned[Face].Origin = FaceChangeOrigin::Replaced;
        std::map<int, int> Raise, Rise;                                                      // Vertex → raised vertex, rise edge
        const auto RaisedVertex = [&](int Vertex)
        {
            if (!Raise.count(Vertex))
            {
                Raise[Vertex] = static_cast<int>(Blueprint.Vertices.size());
                Blueprint.Vertices.push_back({ Blueprint.Vertices[Vertex].Position + Lift });
                Rise[Vertex] = static_cast<int>(Blueprint.Edges.size());
                Blueprint.Edges.push_back({ StraightLine{ Blueprint.Vertices[Vertex].Position, Normal }, 0.0, Distance, Vertex, Raise[Vertex] });
            }
            return Raise[Vertex];
        };
        for (auto& Loop : Top.Loops)
        {
            for (BlueprintUse& Use : Loop)
            {
                const BlueprintEdge Lower = Blueprint.Edges[Use.Edge];
                CurveGeometry Lifted = Lower.Curve;
                std::visit([&](auto& Carrier)
                {
                    using Type = std::decay_t<decltype(Carrier)>;
                    if constexpr (std::is_same_v<Type, StraightLine>) Carrier.Origin = Carrier.Origin + Lift;
                    else if constexpr (std::is_same_v<Type, IntersectionCurve> || std::is_same_v<Type, ContactCurve>) {}   // Refused above
                    else if constexpr (std::is_same_v<Type, NurbsCurve>) Carrier = MapNurbsCurve(*Carrier.Definition, [&](Vector3 Point) { return Point + Lift; });
                    else Carrier.Frame.Origin = Carrier.Frame.Origin + Lift;
                }, Lifted);
                const int UpperStart = RaisedVertex(Lower.Start);                             // Issues rise edges first
                const int UpperEnd   = RaisedVertex(Lower.End);
                const int Upper      = static_cast<int>(Blueprint.Edges.size());
                Blueprint.Edges.push_back({ Lifted, Lower.Low, Lower.High, UpperStart, UpperEnd });

                // Side face: plane through a line, or the cylinder over a circle whose axis is the normal.
                const int     From      = Use.Sense == Orientation::Forward ? Lower.Start : Lower.End;
                const int     Until     = Use.Sense == Orientation::Forward ? Lower.End : Lower.Start;
                const double  Middle    = 0.5 * (Lower.Low + Lower.High);
                const Vector3 Tangent   = Normalize(EvaluateCurve(Lower.Curve, Middle).Derivative) * SignOfSense(Use.Sense);
                const Vector3 Outward   = Cross(Tangent, Normal);
                BlueprintFace Side;
                if (const auto* Line = std::get_if<StraightLine>(&Lower.Curve))
                {
                    Side.Surface = PlaneSurface{ ConstructFrame(Line->Origin, Outward, Tangent) };
                    Side.Sense   = Orientation::Forward;
                }
                else if (const auto* Circle = std::get_if<CircularArc>(&Lower.Curve))
                {
                    OrthonormalBasis Axis = ConstructFrame(Circle->Frame.Origin, Normal, Circle->Frame.XAxis);
                    Side.Surface = CylinderSurface{ Axis, Circle->Radius };
                    const Vector3 Point  = EvaluateCurve(Lower.Curve, Middle).Position;
                    const Vector3 Radial = Point - Circle->Frame.Origin;
                    Side.Sense   = Dot(Radial, Outward) > 0.0 ? Orientation::Forward : Orientation::Reversed;
                }
                else
                {
                    return "boundary edge of " + LabelToken("F", Description.Inventory.Faces[Face]) + " is an ellipse (side carrier 🚧)";
                }
                const Orientation Back = Use.Sense == Orientation::Forward ? Orientation::Reversed : Orientation::Forward;
                Side.Loops.push_back({ { Use.Edge, Use.Sense, ParameterLine{} }, { Rise[Until], Orientation::Forward, ParameterLine{} },
                                       { Upper, Back, ParameterLine{} }, { Rise[From], Orientation::Reversed, ParameterLine{} } });
                FitLoopSurfaceCurves(Side.Surface, Blueprint, Side.Loops.back());
                Blueprint.Faces.push_back(std::move(Side));
                Planned.push_back({ {}, FaceChangeOrigin::Generated, Description.Inventory.Faces[Face], Description.Inventory.Edges[Use.Edge] });
                ++SideFaces;
                Use.Edge = Upper;                                                            // Same τ, translated chart: SP-curve unchanged
            }
        }
    }
    Method = "direct rebuild: " + std::to_string(Selected.size()) + " face(s) raised, " + std::to_string(SideFaces) + " side face(s)";
    return Conclude(Partition, Blueprint, "ExtrudeFaces", Planned, Result, Histories, Audit);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSET
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 One boundary edge offset into its face, in the face plane's chart (x, y): a line shifted along the inward
    //    normal, or a concentric circle whose radius grows or shrinks by the distance.
    struct InsetCarrier
    {
        bool    CircleCondition = false;                                                     // [-]   - Circle (else line)
        Vector2 Origin          = {};                                                        // [len] - Line point · circle centre
        Vector2 Direction       = {};                                                        // [-]   - Line direction (traversal)
        double  Radius          = 0.0;                                                       // [len] - Circle radius
    };

    [[nodiscard]] Vector2 ChartPoint(const OrthonormalBasis& Plane, Vector3 Point)
    {
        return { Dot(Point - Plane.Origin, Plane.XAxis), Dot(Point - Plane.Origin, Plane.YAxis) };
    }

    [[nodiscard]] Vector3 SpacePoint(const OrthonormalBasis& Plane, Vector2 Point)
    {
        return Plane.Origin + Plane.XAxis * Point.X + Plane.YAxis * Point.Y;
    }

    [[nodiscard]] double ChartCross(Vector2 First, Vector2 Second) { return First.X * Second.Y - First.Y * Second.X; }

    // Where two offset carriers meet: every crossing (0, 1 or 2 points).
    [[nodiscard]] std::vector<Vector2> CrossInsetCarriers(const InsetCarrier& First, const InsetCarrier& Second)
    {
        std::vector<Vector2> Points;
        if (!First.CircleCondition && !Second.CircleCondition)
        {
            const double Denominator = ChartCross(First.Direction, Second.Direction);
            if (std::fabs(Denominator) < 1.0e-14) return Points;
            const double Along = ChartCross(Second.Origin - First.Origin, Second.Direction) / Denominator;
            Points.push_back(First.Origin + First.Direction * Along);
            return Points;
        }
        if (First.CircleCondition != Second.CircleCondition)
        {
            const InsetCarrier& Line   = First.CircleCondition ? Second : First;
            const InsetCarrier& Circle = First.CircleCondition ? First : Second;
            const Vector2 Gap     = Line.Origin - Circle.Origin;
            const double  Half    = Dot(Gap, Line.Direction);
            const double  Discriminant = Half * Half - (Dot(Gap, Gap) - Circle.Radius * Circle.Radius);
            if (Discriminant < -1.0e-12) return Points;
            const double Root = std::sqrt(std::max(0.0, Discriminant));
            Points.push_back(Line.Origin + Line.Direction * (-Half - Root));
            Points.push_back(Line.Origin + Line.Direction * (-Half + Root));
            return Points;
        }
        const Vector2 Gap  = Second.Origin - First.Origin;
        const double  Span = Length(Gap);
        if (Span < 1.0e-14) return Points;
        const double Along  = (Span * Span + First.Radius * First.Radius - Second.Radius * Second.Radius) / (2.0 * Span);
        const double Square = First.Radius * First.Radius - Along * Along;
        if (Square < -1.0e-12) return Points;
        const double  Height = std::sqrt(std::max(0.0, Square));
        const Vector2 Unit   = Gap * (1.0 / Span);
        const Vector2 Across{ -Unit.Y, Unit.X };
        Points.push_back(First.Origin + Unit * Along + Across * Height);
        Points.push_back(First.Origin + Unit * Along - Across * Height);
        return Points;
    }

    // Proper crossing of two chart segments (shared end points do not count).
    [[nodiscard]] bool SegmentsCross(Vector2 A, Vector2 B, Vector2 C, Vector2 D)
    {
        const double One   = ChartCross(B - A, C - A), Two = ChartCross(B - A, D - A);
        const double Three = ChartCross(D - C, A - C), Four = ChartCross(D - C, B - C);
        return One * Two < -1.0e-18 && Three * Four < -1.0e-18;
    }
}

// 📝 Plasticity offset-curve on a face / Blender I: the boundary loop of each selected planar face is offset into the
//    face by Distance; the face keeps its outer boundary and gains the offset loop as a hole (the border ring), and a new
//    inner face fills the loop. With Depth ≠ 0 the inner face then moves along the outward normal (+ boss, − pocket)
//    and a ring of side walls (planes over lines, cylinders over arcs) joins it to the border, exactly like extrude.
//    Corners: tangent joints move along the common normal; other joints take the nearest crossing of the two offset
//    carriers. An edge that would reverse or a loop that would cross itself is refused (inset too large).
std::string InsetPlanarFaces(ModelPartition& Partition, BodyToken Body, const std::vector<FaceToken>& Faces, double Distance, double Depth, BodyToken& Result,
                             std::vector<FaceChangeHistory>& Histories, AuditReport& Audit, std::string& Method)
{
    if (Faces.empty()) return "no faces selected";
    if (!(Distance > 0.0)) return "inset distance must be positive";
    BodyDescription Description;
    if (std::string Refusal = DescribeBody(Partition, Body, Description); !Refusal.empty()) return Refusal;
    BodyBlueprint&                 Blueprint = Description.Blueprint;
    const double                   Tolerance = 1.0e-9 * Description.Scale;
    std::vector<FaceChangeHistory> Planned;
    for (size_t Index = 0; Index < Blueprint.Faces.size(); ++Index) Planned.push_back({ {}, FaceChangeOrigin::Unchanged, Description.Inventory.Faces[Index], {} });
    std::set<int> Selected;
    for (FaceToken Face : Faces) Selected.insert(Description.FaceIndex.at(Face.Slot));

    int Walls = 0;
    for (int Face : Selected)
    {
        const std::string Name  = LabelToken("F", Description.Inventory.Faces[Face]);
        const auto*       Plane = std::get_if<PlaneSurface>(&Blueprint.Faces[Face].Surface);
        if (!Plane) return Name + " is not planar";
        if (Blueprint.Faces[Face].Loops.size() != 1) return Name + " has holes (🚧 inset of hole loops)";
        const OrthonormalBasis Chart  = Plane->Frame;
        const Vector3          Normal = Plane->Frame.ZAxis * SignOfSense(Blueprint.Faces[Face].Sense);
        const std::vector<BlueprintUse> Loop = Blueprint.Faces[Face].Loops.front();
        const size_t Count = Loop.size();

        // ① Offset carriers, traversal tangents at both ends of every use.
        std::vector<InsetCarrier> Carriers(Count);
        std::vector<Vector3>      StartTangent(Count), EndTangent(Count);
        std::vector<int>          FromVertex(Count), UntilVertex(Count);
        for (size_t Index = 0; Index < Count; ++Index)
        {
            const BlueprintUse&  Use   = Loop[Index];
            const BlueprintEdge& Edge  = Blueprint.Edges[Use.Edge];
            const double         Sign  = SignOfSense(Use.Sense);
            const double         From  = Use.Sense == Orientation::Forward ? Edge.Low : Edge.High;
            const double         Until = Use.Sense == Orientation::Forward ? Edge.High : Edge.Low;
            FromVertex[Index]   = Use.Sense == Orientation::Forward ? Edge.Start : Edge.End;
            UntilVertex[Index]  = Use.Sense == Orientation::Forward ? Edge.End : Edge.Start;
            StartTangent[Index] = Normalize(EvaluateCurve(Edge.Curve, From).Derivative) * Sign;
            EndTangent[Index]   = Normalize(EvaluateCurve(Edge.Curve, Until).Derivative) * Sign;
            if (const auto* Line = std::get_if<StraightLine>(&Edge.Curve))
            {
                const Vector3 Inward = Cross(Normal, StartTangent[Index]);
                Carriers[Index].Origin    = ChartPoint(Chart, Line->Origin + Inward * Distance);
                const Vector3 Ahead = Line->Direction * Sign;
                Carriers[Index].Direction = { Dot(Ahead, Chart.XAxis), Dot(Ahead, Chart.YAxis) };
            }
            else if (const auto* Circle = std::get_if<CircularArc>(&Edge.Curve))
            {
                if (std::fabs(std::fabs(Dot(Circle->Frame.ZAxis, Normal)) - 1.0) > 1.0e-9) return "boundary arc of " + Name + " does not lie in its plane";
                const Vector3 Point  = EvaluateCurve(Edge.Curve, From).Position;
                const Vector3 Radial = Normalize(Point - Circle->Frame.Origin);
                const double  Side   = Dot(Cross(Normal, StartTangent[Index]), Radial) > 0.0 ? 1.0 : -1.0;
                Carriers[Index].CircleCondition = true;
                Carriers[Index].Origin          = ChartPoint(Chart, Circle->Frame.Origin);
                Carriers[Index].Radius          = Circle->Radius + Side * Distance;
                if (Carriers[Index].Radius <= Tolerance) return "inset " + std::to_string(Distance) + " swallows a boundary arc of " + Name + " (radius " + std::to_string(Circle->Radius) + ")";
            }
            else return "boundary edge of " + Name + " is not a line or an arc (🚧 inset of curved boundaries)";
        }

        // ② Inset vertex at each junction (use Index ends where Index + 1 starts).
        std::map<int, int>     Inset;                                                        // Vertex → inset vertex
        std::map<int, Vector2> InsetChart;
        for (size_t Index = 0; Index < Count; ++Index)
        {
            const size_t  Next   = (Index + 1) % Count;
            const int     Vertex = UntilVertex[Index];
            const Vector3 Corner = Blueprint.Vertices[Vertex].Position;
            const Vector3 Inward = Cross(Normal, EndTangent[Index]);
            Vector2       Placed = ChartPoint(Chart, Corner + Inward * Distance);
            const bool    Smooth = Count == 1 || (Length(Cross(EndTangent[Index], StartTangent[Next])) < 1.0e-9 && Dot(EndTangent[Index], StartTangent[Next]) > 0.0);
            if (!Smooth)
            {
                const std::vector<Vector2> Crossings = CrossInsetCarriers(Carriers[Index], Carriers[Next]);
                if (Crossings.empty()) return "inset " + std::to_string(Distance) + " is too large for " + Name + " (offset edges no longer meet)";
                const Vector2 Origin = ChartPoint(Chart, Corner);
                Placed = *std::min_element(Crossings.begin(), Crossings.end(), [&](Vector2 First, Vector2 Second) { return Length(First - Origin) < Length(Second - Origin); });
            }
            Inset[Vertex]      = static_cast<int>(Blueprint.Vertices.size());
            InsetChart[Vertex] = Placed;
            Blueprint.Vertices.push_back({ SpacePoint(Chart, Placed) });
        }

        // ③ Inset edges on the offset carriers, same orientation as their source edges; reversal = inset too large.
        std::vector<int> InsetEdges(Count);
        std::vector<Vector2> Outline;                                                       // Sampled inset loop (chart)
        for (size_t Index = 0; Index < Count; ++Index)
        {
            const BlueprintUse   Use  = Loop[Index];
            const BlueprintEdge  Edge = Blueprint.Edges[Use.Edge];
            BlueprintEdge        Offset;
            Offset.Start = Inset.at(Edge.Start);
            Offset.End   = Inset.at(Edge.End);
            const Vector3 StartPoint = Blueprint.Vertices[Offset.Start].Position;
            const Vector3 EndPoint   = Blueprint.Vertices[Offset.End].Position;
            if (const auto* Line = std::get_if<StraightLine>(&Edge.Curve))
            {
                const Vector3 Origin = SpacePoint(Chart, Carriers[Index].Origin);
                Offset.Curve = StraightLine{ Origin, Line->Direction };
                Offset.Low   = Dot(StartPoint - Origin, Line->Direction);
                Offset.High  = Dot(EndPoint - Origin, Line->Direction);
                if (Offset.High - Offset.Low <= Tolerance) return "inset " + std::to_string(Distance) + " is too large for " + Name + " (an edge would vanish or reverse)";
            }
            else
            {
                CircularArc Circle = std::get<CircularArc>(Edge.Curve);
                Circle.Radius = Carriers[Index].Radius;
                const auto Angle = [&](Vector3 Point, double Near)
                {
                    const Vector3 Gap = Point - Circle.Frame.Origin;
                    double θ = std::atan2(Dot(Gap, Circle.Frame.YAxis), Dot(Gap, Circle.Frame.XAxis));
                    θ += τPeriod * std::round((Near - θ) / τPeriod);
                    return θ;
                };
                Offset.Curve = Circle;
                Offset.Low   = Angle(StartPoint, Edge.Low);
                Offset.High  = Edge.Start == Edge.End ? Offset.Low + (Edge.High - Edge.Low) : Angle(EndPoint, Edge.High);
                if (Offset.High - Offset.Low <= Tolerance || Offset.High - Offset.Low > τPeriod + 1.0e-9) return "inset " + std::to_string(Distance) + " is too large for " + Name + " (an arc would vanish or reverse)";
            }
            InsetEdges[Index] = static_cast<int>(Blueprint.Edges.size());
            Blueprint.Edges.push_back(Offset);
            const double From  = Use.Sense == Orientation::Forward ? Offset.Low : Offset.High;
            const double Until = Use.Sense == Orientation::Forward ? Offset.High : Offset.Low;
            for (int Step = 0; Step < 16; ++Step) Outline.push_back(ChartPoint(Chart, EvaluateCurve(Offset.Curve, From + (Until - From) * Step / 16.0).Position));
        }
        double Area = 0.0;
        for (size_t Index = 0; Index < Outline.size(); ++Index) Area += ChartCross(Outline[Index], Outline[(Index + 1) % Outline.size()]);
        if (Area * Dot(Chart.ZAxis, Normal) <= 0.0) return "inset " + std::to_string(Distance) + " is too large for " + Name + " (the loop turns inside out)";
        for (size_t First = 0; First < Outline.size(); ++First)
            for (size_t Second = First + 2; Second < Outline.size(); ++Second)
            {
                if (First == 0 && Second == Outline.size() - 1) continue;
                if (SegmentsCross(Outline[First], Outline[(First + 1) % Outline.size()], Outline[Second], Outline[(Second + 1) % Outline.size()]))
                    return "inset " + std::to_string(Distance) + " is too large for " + Name + " (the offset loop crosses itself)";
            }

        // ④ Border ring = the face with the inset loop as a hole; the inner face fills the loop.
        std::vector<BlueprintUse> Hole, Inner;
        for (size_t Index = Count; Index-- > 0;) Hole.push_back({ InsetEdges[Index], Loop[Index].Sense == Orientation::Forward ? Orientation::Reversed : Orientation::Forward, ParameterLine{} });
        for (size_t Index = 0; Index < Count; ++Index) Inner.push_back({ InsetEdges[Index], Loop[Index].Sense, ParameterLine{} });
        FitLoopSurfaceCurves(Blueprint.Faces[Face].Surface, Blueprint, Hole);
        Blueprint.Faces[Face].Loops.push_back(std::move(Hole));
        Planned[Face].Origin = FaceChangeOrigin::Trimmed;
        BlueprintFace Filling{ Blueprint.Faces[Face].Surface, Blueprint.Faces[Face].Sense, {} };

        // ⑤ Depth: the inner face moves along ± normal, side walls over each inset edge (extrude construction).
        if (std::fabs(Depth) > Tolerance)
        {
            const Vector3 Rise   = Normal * (Depth > 0.0 ? 1.0 : -1.0);
            const Vector3 Lift   = Normal * Depth;
            PlaneSurface  Moved  = *std::get_if<PlaneSurface>(&Filling.Surface);
            Moved.Frame.Origin   = Moved.Frame.Origin + Lift;
            Filling.Surface      = Moved;
            std::map<int, int> Raised, Riser;
            const auto RaisedVertex = [&](int Vertex)
            {
                if (!Raised.count(Vertex))
                {
                    Raised[Vertex] = static_cast<int>(Blueprint.Vertices.size());
                    Blueprint.Vertices.push_back({ Blueprint.Vertices[Vertex].Position + Lift });
                    Riser[Vertex] = static_cast<int>(Blueprint.Edges.size());
                    Blueprint.Edges.push_back({ StraightLine{ Blueprint.Vertices[Vertex].Position, Rise }, 0.0, std::fabs(Depth), Vertex, Raised[Vertex] });
                }
                return Raised[Vertex];
            };
            for (size_t Index = 0; Index < Count; ++Index)
            {
                BlueprintUse&       Use   = Inner[Index];
                const BlueprintEdge Lower = Blueprint.Edges[Use.Edge];
                CurveGeometry       Upper = Lower.Curve;
                if (auto* Line = std::get_if<StraightLine>(&Upper)) Line->Origin = Line->Origin + Lift;
                else std::get<CircularArc>(Upper).Frame.Origin = std::get<CircularArc>(Upper).Frame.Origin + Lift;
                const int UpperStart = RaisedVertex(Lower.Start);
                const int UpperEnd   = RaisedVertex(Lower.End);
                const int Top        = static_cast<int>(Blueprint.Edges.size());
                Blueprint.Edges.push_back({ Upper, Lower.Low, Lower.High, UpperStart, UpperEnd });
                const int     From    = Use.Sense == Orientation::Forward ? Lower.Start : Lower.End;
                const int     Until   = Use.Sense == Orientation::Forward ? Lower.End : Lower.Start;
                const double  Middle  = 0.5 * (Lower.Low + Lower.High);
                const Vector3 Tangent = Normalize(EvaluateCurve(Lower.Curve, Middle).Derivative) * SignOfSense(Use.Sense);
                const Vector3 Outward = Cross(Tangent, Rise);
                BlueprintFace Wall;
                if (const auto* Line = std::get_if<StraightLine>(&Lower.Curve))
                {
                    Wall.Surface = PlaneSurface{ ConstructFrame(Line->Origin, Outward, Tangent) };
                    Wall.Sense   = Orientation::Forward;
                }
                else
                {
                    const CircularArc& Circle = std::get<CircularArc>(Lower.Curve);
                    Wall.Surface = CylinderSurface{ ConstructFrame(Circle.Frame.Origin, Normal, Circle.Frame.XAxis), Circle.Radius };
                    const Vector3 Radial = EvaluateCurve(Lower.Curve, Middle).Position - Circle.Frame.Origin;
                    Wall.Sense   = Dot(Radial, Outward) > 0.0 ? Orientation::Forward : Orientation::Reversed;
                }
                const Orientation Back = Use.Sense == Orientation::Forward ? Orientation::Reversed : Orientation::Forward;
                Wall.Loops.push_back({ { Use.Edge, Use.Sense, ParameterLine{} }, { Riser[Until], Orientation::Forward, ParameterLine{} },
                                       { Top, Back, ParameterLine{} }, { Riser[From], Orientation::Reversed, ParameterLine{} } });
                FitLoopSurfaceCurves(Wall.Surface, Blueprint, Wall.Loops.back());
                Blueprint.Faces.push_back(std::move(Wall));
                Planned.push_back({ {}, FaceChangeOrigin::Generated, Description.Inventory.Faces[Face], Description.Inventory.Edges[Loop[Index].Edge] });
                ++Walls;
                Use.Edge = Top;
            }
        }
        FitLoopSurfaceCurves(Filling.Surface, Blueprint, Inner);
        Filling.Loops.push_back(std::move(Inner));
        Blueprint.Faces.push_back(std::move(Filling));
        Planned.push_back({ {}, FaceChangeOrigin::Generated, Description.Inventory.Faces[Face], {} });
    }
    Method = "direct rebuild: " + std::to_string(Selected.size()) + " face(s) inset " + std::to_string(Distance) + ", depth " + std::to_string(Depth) + ", " + std::to_string(Walls) + " side wall(s)";
    return Conclude(Partition, Blueprint, "InsetFaces", Planned, Result, Histories, Audit);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DELETE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Corpus E "deleteface": ① hole heal — when every loop of a neighbour that touches the deleted faces is a hole loop made
//    only of their edges (a bore wall), those loops are dropped and the body stays solid; ② otherwise the faces are
//    removed and the remainder is an open sheet body (laminar edges where the faces were). Neighbour growth to close a
//    gap (Parasolid "heal by growing") is 🚧.
std::string DeleteBodyFaces(ModelPartition& Partition, BodyToken Body, const std::vector<FaceToken>& Faces, BodyToken& Result, BodyCategory& Category,
                            std::vector<FaceChangeHistory>& Histories, AuditReport& Audit, std::string& Method)
{
    if (Faces.empty()) return "no faces selected";
    BodyDescription Description;
    if (std::string Refusal = DescribeBody(Partition, Body, Description); !Refusal.empty()) return Refusal;
    const BodyBlueprint& Source = Description.Blueprint;
    std::set<int> Deleted, DeletedEdges;
    for (FaceToken Face : Faces) Deleted.insert(Description.FaceIndex.at(Face.Slot));
    if (Deleted.size() == Source.Faces.size()) return "every face would be deleted";
    for (int Face : Deleted) for (const auto& Loop : Source.Faces[Face].Loops) for (const BlueprintUse& Use : Loop) DeletedEdges.insert(Use.Edge);

    bool Healable = true;
    for (size_t Face = 0; Face < Source.Faces.size() && Healable; ++Face)
    {
        if (Deleted.count(static_cast<int>(Face))) continue;
        for (size_t Loop = 0; Loop < Source.Faces[Face].Loops.size(); ++Loop)
        {
            const auto& Uses     = Source.Faces[Face].Loops[Loop];
            const auto  Touching = std::count_if(Uses.begin(), Uses.end(), [&](const BlueprintUse& Use) { return DeletedEdges.count(Use.Edge) > 0; });
            if (Touching > 0 && (Loop == 0 || Touching != static_cast<long>(Uses.size()))) Healable = false;
        }
    }

    BodyBlueprint                  Blueprint;
    std::vector<FaceChangeHistory> Planned;
    Blueprint.Vertices = Source.Vertices;
    Blueprint.Edges    = Source.Edges;
    Blueprint.Category = Healable ? BodyCategory::Solid : BodyCategory::Sheet;
    int DroppedLoops = 0;
    for (size_t Face = 0; Face < Source.Faces.size(); ++Face)
    {
        if (Deleted.count(static_cast<int>(Face))) continue;
        BlueprintFace Kept = Source.Faces[Face];
        bool Trimmed = false;
        if (Healable)
        {
            const auto Before = Kept.Loops.size();
            std::erase_if(Kept.Loops, [&](const std::vector<BlueprintUse>& Uses) { return !Uses.empty() && DeletedEdges.count(Uses.front().Edge) > 0; });
            Trimmed       = Kept.Loops.size() != Before;
            DroppedLoops += static_cast<int>(Before - Kept.Loops.size());
        }
        Blueprint.Faces.push_back(std::move(Kept));
        Planned.push_back({ {}, Trimmed ? FaceChangeOrigin::Trimmed : FaceChangeOrigin::Unchanged, Description.Inventory.Faces[Face], {} });
    }
    Category = Blueprint.Category;
    Method   = Healable ? "hole heal: " + std::to_string(Deleted.size()) + " face(s) and " + std::to_string(DroppedLoops) + " hole loop(s) removed, body stays solid"
                        : "open sheet: " + std::to_string(Deleted.size()) + " face(s) removed, their edges become laminar";
    return Conclude(Partition, Blueprint, "DeleteFace", Planned, Result, Histories, Audit);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    HEAL
//------------------------------------------------------------------------------------------------------------------------

// 📝 Corpus E "heal": ① faces on one carrier (plane, coaxial cylinder) with the same outward side, adjacent through an
//    edge, merge (union-find); their shared edges vanish and the loops are spliced (next = successor; across a vanished
//    edge, continue from its mate's successor), outer loop = largest |area| in the chart;
//    an edge used twice by one planar face (a slit) vanishes the same way; ② at a vertex left with exactly two edges on
//    one carrier (collinear lines, one circle) the edges join. A body with nothing to merge is returned as an equal copy.
std::string HealBody(ModelPartition& Partition, BodyToken Body, BodyToken& Result, std::vector<FaceChangeHistory>& Histories, AuditReport& Audit, std::string& Method,
                     const std::vector<EdgeToken>& OnlyEdges, const std::vector<VertexToken>& OnlyVertices)
{
    BodyDescription Description;
    if (std::string Refusal = DescribeBody(Partition, Body, Description); !Refusal.empty()) return Refusal;
    BodyBlueprint& Source    = Description.Blueprint;
    const double   Tolerance = 1.0e-9 * Description.Scale;
    const int      FaceCount = static_cast<int>(Source.Faces.size());

    // ⓪ Restricted (Delete on edges / vertices): only the listed edges dissolve, joins only at the listed vertices and at
    //    the ends of dissolved edges; every listed one must go, else the heal is refused (nothing else changes).
    const bool    RestrictedCondition = !OnlyEdges.empty() || !OnlyVertices.empty();
    std::set<int> Requested, Joinable, Corners;
    for (EdgeToken Edge : OnlyEdges)
    {
        const auto Found = Description.EdgeIndex.find(Edge.Slot);
        if (Found == Description.EdgeIndex.end()) return "Heal: the edge is not on this body";
        Requested.insert(Found->second);
        Joinable.insert(Source.Edges[Found->second].Start);
        Joinable.insert(Source.Edges[Found->second].End);
    }
    for (VertexToken Vertex : OnlyVertices)
    {
        const auto Found = Description.VertexIndex.find(Vertex.Slot);
        if (Found == Description.VertexIndex.end()) return "Heal: the vertex is not on this body";
        Joinable.insert(Found->second);
        Corners.insert(Found->second);
    }

    // ① Union-find over faces through edges whose two uses lie on one outward plane.
    std::vector<int> Root(FaceCount);
    for (int Face = 0; Face < FaceCount; ++Face) Root[Face] = Face;
    const std::function<int(int)> Find = [&](int Face) { return Root[Face] == Face ? Face : Root[Face] = Find(Root[Face]); };
    std::map<int, std::vector<int>> EdgeFaces;
    for (int Face = 0; Face < FaceCount; ++Face) for (const auto& Loop : Source.Faces[Face].Loops) for (const BlueprintUse& Use : Loop) EdgeFaces[Use.Edge].push_back(Face);
    std::set<int> Internal;
    for (const auto& [Edge, Users] : EdgeFaces)
    {
        if (RestrictedCondition && !Requested.contains(Edge)) continue;
        if (Users.size() == 2 && Users[0] != Users[1] && SameOutwardCarrier(Source.Faces[Users[0]], Source.Faces[Users[1]], Tolerance))
        {
            Root[Find(Users[0])] = Find(Users[1]);
            Internal.insert(Edge);
        }
    }
    if (RestrictedCondition)                                                                 // Two faces made one: every edge between them goes
    {
        for (const auto& [Edge, Users] : EdgeFaces)
        {
            if (Users.size() != 2 || Users[0] == Users[1] || Internal.contains(Edge) || Find(Users[0]) != Find(Users[1])) continue;
            Internal.insert(Edge);
            Joinable.insert(Source.Edges[Edge].Start);
            Joinable.insert(Source.Edges[Edge].End);
        }
    }
    for (auto Edge = Internal.begin(); Edge != Internal.end();)                              // Only edges inside one merged group vanish
    {
        const auto& Users = EdgeFaces[*Edge];
        Edge = Find(Users[0]) == Find(Users[1]) ? std::next(Edge) : Internal.erase(Edge);
    }
    std::set<int> Slit;                                                                      // Faces with an edge used twice by themselves
    for (const auto& [Edge, Users] : EdgeFaces)
    {
        if (RestrictedCondition && !Requested.contains(Edge)) continue;
        if (Users.size() == 2 && Users[0] == Users[1] && std::holds_alternative<PlaneSurface>(Source.Faces[Users[0]].Surface))
        {
            Internal.insert(Edge);                                                           // A slit on a plane is not a seam
            Slit.insert(Users[0]);
        }
    }
    for (int Edge : Requested)
        if (!Internal.contains(Edge)) return "the edge joins two different surfaces: removing it cannot leave a closed solid (only an edge between faces on one surface dissolves)";

    // ② Merged faces: surviving uses re-traced into loops by vertex chaining.
    BodyBlueprint                  Blueprint;
    std::vector<FaceChangeHistory> Planned;
    Blueprint.Vertices = Source.Vertices;
    Blueprint.Edges    = Source.Edges;
    Blueprint.Category = Source.Category;
    int Merged = 0, Slits = 0;
    for (int Face = 0; Face < FaceCount; ++Face)
    {
        if (Find(Face) != Face) continue;
        std::vector<int> Members;
        for (int Other = 0; Other < FaceCount; ++Other) if (Find(Other) == Face) Members.push_back(Other);
        if (Members.size() == 1 && !Slit.count(Face))
        {
            Blueprint.Faces.push_back(Source.Faces[Face]);
            Planned.push_back({ {}, FaceChangeOrigin::Unchanged, Description.Inventory.Faces[Face], {} });
            continue;
        }
        Merged += static_cast<int>(Members.size()) - 1;
        Slits  += Slit.count(Face) ? 1 : 0;
        // Fin splicing over (member, loop, use) positions.
        struct UsePlace { int Member; size_t Loop; size_t Use; };
        std::vector<UsePlace>              Places;
        std::map<int, std::vector<size_t>> EdgePlaces;                                      // Edge → places using it
        std::map<std::tuple<int, size_t, size_t>, size_t> PlaceIndex;
        for (int Member : Members)
        {
            for (size_t Loop = 0; Loop < Source.Faces[Member].Loops.size(); ++Loop)
            {
                for (size_t Use = 0; Use < Source.Faces[Member].Loops[Loop].size(); ++Use)
                {
                    PlaceIndex[{ Member, Loop, Use }] = Places.size();
                    EdgePlaces[Source.Faces[Member].Loops[Loop][Use].Edge].push_back(Places.size());
                    Places.push_back({ Member, Loop, Use });
                }
            }
        }
        const auto UseAt     = [&](size_t Place) -> const BlueprintUse& { return Source.Faces[Places[Place].Member].Loops[Places[Place].Loop][Places[Place].Use]; };
        const auto Successor = [&](size_t Place)
        {
            const UsePlace& Here = Places[Place];
            return PlaceIndex.at({ Here.Member, Here.Loop, (Here.Use + 1) % Source.Faces[Here.Member].Loops[Here.Loop].size() });
        };
        const auto Mate = [&](size_t Place) { const auto& Both = EdgePlaces.at(UseAt(Place).Edge); return Both[0] == Place ? Both[1] : Both[0]; };
        BlueprintFace Combined{ Source.Faces[Face].Surface, Source.Faces[Face].Sense, {} };
        std::vector<bool> Taken(Places.size(), false);
        for (size_t Seed = 0; Seed < Places.size(); ++Seed)
        {
            if (Taken[Seed] || Internal.count(UseAt(Seed).Edge)) continue;
            std::vector<BlueprintUse> Loop;
            size_t Cursor = Seed;
            for (size_t Guard = 0; Guard <= Places.size(); ++Guard)
            {
                Taken[Cursor] = true;
                Loop.push_back(UseAt(Cursor));
                Cursor = Successor(Cursor);
                for (size_t Hop = 0; Internal.count(UseAt(Cursor).Edge) && Hop <= Places.size(); ++Hop) Cursor = Successor(Mate(Cursor));
                if (Cursor == Seed) break;
                if (Taken[Cursor]) return "merged face boundary does not close";
            }
            if (Cursor != Seed) return "merged face boundary does not close";
            FitLoopSurfaceCurves(Combined.Surface, Blueprint, Loop);
            Combined.Loops.push_back(std::move(Loop));
        }
        // Outer loop first: largest |signed area| of the chart polygon.
        const auto Area = [&](const std::vector<BlueprintUse>& Loop)
        {
            double Sum = 0.0;
            for (const BlueprintUse& Use : Loop)
            {
                const BlueprintEdge& Edge = Blueprint.Edges[Use.Edge];
                for (int Step = 0; Step < 16; ++Step)
                {
                    const double  From = Use.Sense == Orientation::Forward ? Edge.Low : Edge.High;
                    const double  Span = (Use.Sense == Orientation::Forward ? 1.0 : -1.0) * (Edge.High - Edge.Low);
                    const Vector2 One  = EvaluateParameterCurve(Use.SurfaceCurve, From + Span * Step / 16.0).Position;
                    const Vector2 Two  = EvaluateParameterCurve(Use.SurfaceCurve, From + Span * (Step + 1) / 16.0).Position;
                    Sum += One.X * Two.Y - Two.X * One.Y;
                }
            }
            return std::fabs(Sum);
        };
        std::stable_sort(Combined.Loops.begin(), Combined.Loops.end(), [&](const auto& One, const auto& Two) { return Area(One) > Area(Two); });
        Blueprint.Faces.push_back(std::move(Combined));
        Planned.push_back({ {}, FaceChangeOrigin::Trimmed, Description.Inventory.Faces[Face], {} });
    }

    // ③ Edge joins at vertices of degree two whose edges share a carrier.
    int Joined = 0;
    for (bool Progress = true; Progress;)
    {
        Progress = false;
        std::map<int, std::vector<int>> VertexEdges;
        std::set<int> Used;
        for (const BlueprintFace& Target : Blueprint.Faces) for (const auto& Loop : Target.Loops) for (const BlueprintUse& Use : Loop) Used.insert(Use.Edge);
        for (int Edge : Used)
        {
            VertexEdges[Blueprint.Edges[Edge].Start].push_back(Edge);
            if (Blueprint.Edges[Edge].End != Blueprint.Edges[Edge].Start) VertexEdges[Blueprint.Edges[Edge].End].push_back(Edge);
        }
        for (const auto& [Vertex, Edges] : VertexEdges)
        {
            if (Edges.size() != 2 || Edges[0] == Edges[1]) continue;
            if (RestrictedCondition && !Joinable.contains(Vertex)) continue;
            const BlueprintEdge& One = Blueprint.Edges[Edges[0]];
            const BlueprintEdge& Two = Blueprint.Edges[Edges[1]];
            if (!SameCarrierCurve(One.Curve, Two.Curve, Tolerance)) continue;
            // Orient the join First → Vertex → Last along the first edge's direction.
            const bool OneEndsHere = One.End == Vertex;
            const int  First       = OneEndsHere ? One.Start : One.End;
            const int  Last        = Two.Start == Vertex ? Two.End : Two.Start;
            if (First == Vertex || Last == Vertex) continue;
            BlueprintEdge Join;
            if (std::holds_alternative<StraightLine>(One.Curve))
            {
                const Vector3 Begin = Blueprint.Vertices[First].Position, Finish = Blueprint.Vertices[Last].Position;
                Join = { StraightLine{ Begin, Normalize(Finish - Begin) }, 0.0, Distance(Begin, Finish), First, Last };
            }
            else
            {
                const Vector3 Begin = Blueprint.Vertices[First].Position, Finish = Blueprint.Vertices[Last].Position, Through = Blueprint.Vertices[Vertex].Position;
                const double  Low   = ProjectOntoCurve(One.Curve, Begin, 0.0);
                double        Pivot = ProjectOntoCurve(One.Curve, Through, Low);
                double        High  = First == Last ? Low + τPeriod : ProjectOntoCurve(One.Curve, Finish, Low);
                if (Pivot <= Low) Pivot += τPeriod;
                if (High <= Low) High += τPeriod;
                if (!(Pivot < High)) continue;                                               // Circle runs the other way: leave it
                Join = { One.Curve, Low, High, First, Last };
            }
            const int Fresh = static_cast<int>(Blueprint.Edges.size());
            Blueprint.Edges.push_back(Join);
            // Each loop that traverses Edges[0] and Edges[1] consecutively (either order) gets one use of the join.
            for (BlueprintFace& Target : Blueprint.Faces)
            {
                for (auto& Loop : Target.Loops)
                {
                    for (size_t Index = 0; Index < Loop.size() && Loop.size() >= 2; ++Index)  // A seam pair occurs twice
                    {
                        const size_t Next = (Index + 1) % Loop.size();
                        const bool   Pair = (Loop[Index].Edge == Edges[0] && Loop[Next].Edge == Edges[1]) || (Loop[Index].Edge == Edges[1] && Loop[Next].Edge == Edges[0]);
                        if (!Pair) continue;
                        const BlueprintEdge& Here  = Blueprint.Edges[Loop[Index].Edge];
                        const int            Begin = Loop[Index].Sense == Orientation::Forward ? Here.Start : Here.End;
                        Loop[Index] = { Fresh, Begin == First ? Orientation::Forward : Orientation::Reversed, ParameterLine{} };
                        Loop.erase(Loop.begin() + static_cast<long>(Next));
                        if (Next < Index) --Index;
                    }
                }
            }
            ++Joined;
            Corners.erase(Vertex);
            Progress = true;
            break;
        }
    }
    if (!Corners.empty()) return "the vertex is a corner: only a point that splits one straight or circular edge dissolves";
    if (Joined > 0) for (BlueprintFace& Target : Blueprint.Faces) for (auto& Loop : Target.Loops) FitLoopSurfaceCurves(Target.Surface, Blueprint, Loop);
    Method = Merged + Joined + Slits == 0 ? "nothing to merge: equal copy"
                                          : "merged " + std::to_string(Merged) + " face(s), removed " + std::to_string(Slits) + " slit(s), joined " + std::to_string(Joined) + " edge pair(s)";
    return Conclude(Partition, Blueprint, "Heal", Planned, Result, Histories, Audit);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THICKEN
//------------------------------------------------------------------------------------------------------------------------

// 📝 Sheet → solid of thickness Distance on the side opposite the face normals (Parasolid thicken): the inner skin is the
//    face change "every face offset by −Distance" of the sheet (laminar edges re-solved on their rims), stitched to the
//    outer skin by one rim face per laminar edge — loop [edge reversed, rise at X, inner edge, rise at Y reversed] for a
//    fin X → Y, outward t × n. Thicken(DeleteFace(F), t) is the closed-form twin of Hollow(t, F).
std::string ThickenSheet(ModelPartition& Partition, BodyToken Body, double Thickness, BodyToken& Result, std::vector<FaceChangeHistory>& Histories, AuditReport& Audit, std::string& Method)
{
    if (Partition.Bodies.Resolve(Body).Category != BodyCategory::Sheet) return "body is not a sheet (delete a face first)";
    if (!(Thickness > 0.0)) return "thickness must be positive";
    BodyDescription Description;
    if (std::string Refusal = DescribeBody(Partition, Body, Description); !Refusal.empty()) return Refusal;
    const BodyBlueprint& Outer = Description.Blueprint;
    std::vector<FaceReplacement> Replacements;
    for (FaceToken Face : Description.Inventory.Faces)
    {
        const FaceProfile& Profile = Partition.Faces.Resolve(Face);
        const std::optional<SurfaceGeometry> Offset = OffsetSurface(Partition.Surfaces.Resolve(Profile.Surface), -Thickness * SignOfSense(Profile.Sense));
        if (!Offset) return LabelToken("F", Face) + ": thickness exceeds the curvature radius";
        Replacements.push_back({ Face, *Offset, -Thickness });
    }
    BodyBlueprint                  Inner;
    std::vector<FaceChangeHistory> Provenance;
    FaceChangeStatistics           Statistics;
    if (std::string Refusal = PlanFaceChange(Partition, Body, Replacements, Inner, Provenance, Statistics); !Refusal.empty()) return "inner skin: " + Refusal;

    const int     VertexShift = static_cast<int>(Outer.Vertices.size());
    const int     EdgeShift   = static_cast<int>(Outer.Edges.size());
    BodyBlueprint Blueprint   = Outer;
    Blueprint.Category        = BodyCategory::Solid;
    std::vector<FaceChangeHistory> Planned;
    for (FaceToken Face : Description.Inventory.Faces) Planned.push_back({ {}, FaceChangeOrigin::Unchanged, Face, {} });
    for (const BlueprintVertex& Vertex : Inner.Vertices) Blueprint.Vertices.push_back(Vertex);
    for (BlueprintEdge Edge : Inner.Edges)
    {
        Edge.Start += VertexShift;
        Edge.End   += VertexShift;
        Blueprint.Edges.push_back(Edge);
    }
    for (size_t Face = 0; Face < Inner.Faces.size(); ++Face)
    {
        BlueprintFace Flipped{ Inner.Faces[Face].Surface, Inner.Faces[Face].Sense == Orientation::Forward ? Orientation::Reversed : Orientation::Forward, {} };
        for (const auto& Loop : Inner.Faces[Face].Loops)
        {
            std::vector<BlueprintUse> Reversed;
            for (auto Use = Loop.rbegin(); Use != Loop.rend(); ++Use)
            {
                Reversed.push_back({ Use->Edge + EdgeShift, Use->Sense == Orientation::Forward ? Orientation::Reversed : Orientation::Forward, Use->SurfaceCurve });
            }
            Flipped.Loops.push_back(std::move(Reversed));
        }
        Blueprint.Faces.push_back(std::move(Flipped));
        Planned.push_back({ {}, FaceChangeOrigin::Generated, Description.Inventory.Faces[Face], {} });
    }

    // Rim faces over the laminar edges.
    std::map<int, std::vector<std::pair<int, BlueprintUse>>> Uses;                            // Edge → (face, use)
    for (size_t Face = 0; Face < Outer.Faces.size(); ++Face) for (const auto& Loop : Outer.Faces[Face].Loops) for (const BlueprintUse& Use : Loop) Uses[Use.Edge].push_back({ static_cast<int>(Face), Use });
    std::map<int, int> Rise;                                                                 // Vertex → rise edge
    const auto RiseEdge = [&](int Vertex) -> int
    {
        if (Rise.count(Vertex)) return Rise[Vertex];
        const Vector3 From = Blueprint.Vertices[Vertex].Position, Until = Blueprint.Vertices[Vertex + VertexShift].Position;
        if (Distance(From, Until) <= 1.0e-12 * Description.Scale) return -1;
        Blueprint.Edges.push_back({ StraightLine{ From, Normalize(Until - From) }, 0.0, Distance(From, Until), Vertex, Vertex + VertexShift });
        return Rise[Vertex] = static_cast<int>(Blueprint.Edges.size()) - 1;
    };
    int RimFaces = 0;
    for (const auto& [Edge, Users] : Uses)
    {
        if (Users.size() != 1) continue;
        const auto& [Face, Use] = Users.front();
        const BlueprintEdge& SkinEdge = Outer.Edges[Edge];
        const std::optional<SurfaceGeometry> Rim = ConstructRimSurface(Partition, Description.Inventory.Edges[Edge], Description.Inventory.Faces[Face]);
        if (!Rim) return "laminar edge " + LabelToken("E", Description.Inventory.Edges[Edge]) + " has no rim surface (🚧)";
        const int From  = Use.Sense == Orientation::Forward ? SkinEdge.Start : SkinEdge.End;
        const int Until = Use.Sense == Orientation::Forward ? SkinEdge.End : SkinEdge.Start;
        const int AtFrom = RiseEdge(From), AtUntil = RiseEdge(Until);
        if (AtFrom < 0 || AtUntil < 0) return "a boundary vertex does not move (degenerate rim)";
        const Orientation Back = Use.Sense == Orientation::Forward ? Orientation::Reversed : Orientation::Forward;
        BlueprintFace Side{ *Rim, Orientation::Forward, {} };
        Side.Loops.push_back({ { Edge, Back, ParameterLine{} }, { AtFrom, Orientation::Forward, ParameterLine{} },
                               { Edge + EdgeShift, Use.Sense, ParameterLine{} }, { AtUntil, Orientation::Reversed, ParameterLine{} } });
        const double  Middle  = 0.5 * (SkinEdge.Low + SkinEdge.High);
        const CurveSample Sample = EvaluateCurve(SkinEdge.Curve, Middle);
        const Vector3 Normal  = EvaluateOutwardNormal(Partition, Description.Inventory.Faces[Face], Sample.Position);
        const Vector3 Outward = Cross(Normalize(Sample.Derivative) * SignOfSense(Use.Sense), Normal);
        if (Dot(EvaluateSurfaceNormal(*Rim, ProjectOntoSurface(*Rim, Sample.Position, {})), Outward) < 0.0) Side.Sense = Orientation::Reversed;
        FitLoopSurfaceCurves(Side.Surface, Blueprint, Side.Loops.back());
        Blueprint.Faces.push_back(std::move(Side));
        Planned.push_back({ {}, FaceChangeOrigin::Generated, Description.Inventory.Faces[Face], Description.Inventory.Edges[Edge] });
        ++RimFaces;
    }
    if (RimFaces == 0) return "sheet has no boundary (closed sheet 🚧)";
    Method = "outer skin + inner skin (" + std::to_string(Inner.Faces.size()) + " offsets) + " + std::to_string(RimFaces) + " rim face(s)";
    return Conclude(Partition, Blueprint, "Thicken", Planned, Result, Histories, Audit);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MANIFOLD OUTLINE
//------------------------------------------------------------------------------------------------------------------------

std::string DescribePlanarFaceProfile(const ModelPartition& Partition, BodyToken Body, FaceToken Face, SketchProfile& Profile, OrthonormalBasis& Plane,
                                      bool& IslandCondition)
{
    Profile         = {};
    IslandCondition = false;
    BodyDescription Description;
    if (std::string Refusal = DescribeBody(Partition, Body, Description); !Refusal.empty()) return Refusal;
    const BodyBlueprint& Blueprint = Description.Blueprint;
    const auto           Found     = Description.FaceIndex.find(Face.Slot);
    if (Found == Description.FaceIndex.end()) return LabelToken("F", Face) + " is not on the body";
    const int            Index     = Found->second;
    const std::string    Name      = LabelToken("F", Face);
    const BlueprintFace& Top       = Blueprint.Faces[Index];
    const auto*          Surface   = std::get_if<PlaneSurface>(&Top.Surface);
    if (!Surface) return Name + " is not planar";
    const Vector3 Normal    = Surface->Frame.ZAxis * SignOfSense(Top.Sense);
    const double  Tolerance = 1.0e-9 * Description.Scale;                                     // [len] - Coplanar distance
    Plane = ConstructFrame(Surface->Frame.Origin, Normal, Surface->Frame.XAxis);
    const auto Flatten = [&](Vector3 Point) { const Vector3 Offset = Point - Plane.Origin; return Vector2{ Dot(Offset, Plane.XAxis), Dot(Offset, Plane.YAxis) }; };

    std::map<int, std::vector<int>> EdgeFaces;                                               // Edge → faces using it
    for (size_t Other = 0; Other < Blueprint.Faces.size(); ++Other)
        for (const auto& Loop : Blueprint.Faces[Other].Loops) for (const BlueprintUse& Use : Loop) EdgeFaces[Use.Edge].push_back(static_cast<int>(Other));
    bool Island = true;
    Profile.Scale = Description.Scale;
    for (const auto& Loop : Top.Loops)
    {
        ProfileLoop Planned;
        for (const BlueprintUse& Use : Loop)
        {
            const BlueprintEdge& Edge = Blueprint.Edges[Use.Edge];
            for (int Other : EdgeFaces[Use.Edge])
            {
                if (Other == Index) continue;
                const auto* Neighbour = std::get_if<PlaneSurface>(&Blueprint.Faces[Other].Surface);
                if (!Neighbour || Dot(Neighbour->Frame.ZAxis * SignOfSense(Blueprint.Faces[Other].Sense), Normal) < 1.0 - 1.0e-12 ||
                    std::fabs(Dot(Neighbour->Frame.Origin - Plane.Origin, Normal)) > Tolerance)
                    Island = false;
            }
            const bool     Forward = Use.Sense == Orientation::Forward;
            ProfileSegment Segment;
            Segment.Name  = LabelToken("E", Description.Inventory.Edges[Use.Edge]);
            Segment.Start = Flatten(Blueprint.Vertices[Forward ? Edge.Start : Edge.End].Position);
            Segment.End   = Flatten(Blueprint.Vertices[Forward ? Edge.End : Edge.Start].Position);
            if (std::holds_alternative<StraightLine>(Edge.Curve)) Segment.Kind = ProfileSegmentCategory::Line;
            else if (const auto* Arc = std::get_if<CircularArc>(&Edge.Curve))
            {
                // Plane angle θ = Phase + Turn·τ (Turn −1 when the arc's axis opposes the normal).
                const double Turn  = Dot(Arc->Frame.ZAxis, Normal) > 0.0 ? 1.0 : -1.0;
                const double Phase = std::atan2(Dot(Arc->Frame.XAxis, Plane.YAxis), Dot(Arc->Frame.XAxis, Plane.XAxis));
                const double Span  = Edge.High - Edge.Low;                                   // [rad]
                Segment.Centre = Flatten(Arc->Frame.Origin);
                Segment.Radius = Arc->Radius;
                if (Loop.size() == 1 && Span >= 2.0 * std::numbers::pi - 1.0e-9)
                {
                    Segment.Kind       = ProfileSegmentCategory::Circle;
                    Segment.StartAngle = 0.0;
                    Segment.Sweep      = 2.0 * std::numbers::pi;
                    Segment.Start      = Segment.End = Segment.Centre + Vector2{ Arc->Radius, 0.0 };
                }
                else
                {
                    Segment.Kind       = ProfileSegmentCategory::Arc;
                    Segment.StartAngle = Phase + Turn * (Forward ? Edge.Low : Edge.High);
                    Segment.Sweep      = Turn * (Forward ? Span : -Span);
                }
            }
            else if (const auto* Curve = std::get_if<NurbsCurve>(&Edge.Curve); Curve && Curve->Definition)
            {
                const NurbsCurveDefinition& Definition = *Curve->Definition;
                const bool Rational = std::any_of(Definition.Weights.begin(), Definition.Weights.end(), [](double Weight) { return std::fabs(Weight - 1.0) > 1.0e-12; });
                if (Rational || Definition.Period > 0.0 || Definition.Knots.empty() || std::fabs(Edge.Low - Definition.Knots.front()) > 1.0e-12 ||
                    std::fabs(Edge.High - Definition.Knots.back()) > 1.0e-12)
                    return Name + " is bounded by a rational, periodic or trimmed spline " + Segment.Name + " (🚧 manifold extrusion over it)";
                Segment.Kind          = ProfileSegmentCategory::Spline;
                Segment.Spline.Degree = Definition.Degree;
                Segment.Spline.Knots  = Definition.Knots;
                for (const Vector3& Pole : Definition.Poles) Segment.Spline.Poles.push_back(Flatten(Pole));
                if (!Forward) ReverseSketchSpline(Segment.Spline);
            }
            else return Name + " is bounded by " + Segment.Name + ", an ellipse or traced curve (🚧 manifold extrusion over it)";
            Planned.Segments.push_back(std::move(Segment));
        }
        Profile.Loops.push_back(std::move(Planned));
    }
    if (Profile.Loops.empty()) return Name + " has no boundary";
    ArrangeProfileLoops(Profile);
    IslandCondition = Island;
    return Profile.Refusal.empty() ? std::string{} : Name + ": " + Profile.Refusal;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SLIVER BRIDGES
//------------------------------------------------------------------------------------------------------------------------

int OpenSliverBridges(const ModelPartition& Partition, BodyToken Body, const OrthonormalBasis& Plane, double CutSign, const FaceToken* Region, SketchProfile& Profile)
{
    constexpr double BridgeFraction = 1.0e-2;                                                // [-]   - Thinnest strip kept, per outline span
    constexpr double LeadFraction   = 1.0e-3;                                                // [-]   - Past the rim, per profile scale (cutter lead)
    constexpr double TurnFloor      = 5.0e-2;                                                // [-]   - |sin| between a moved side and its neighbours
    BodyDescription Description;
    if (!DescribeBody(Partition, Body, Description).empty()) return 0;
    const BodyBlueprint& Blueprint = Description.Blueprint;
    const double  Tolerance = 1.0e-9 * Description.Scale;                                     // [len] - Coplanar / parallel distance
    const Vector3 Normal    = Plane.ZAxis * (CutSign < 0.0 ? 1.0 : -1.0);                    // [-]   - Outward normal of the faces cut into
    const auto    Flatten   = [&](Vector3 Point) { const Vector3 Offset = Point - Plane.Origin; return Vector2{ Dot(Offset, Plane.XAxis), Dot(Offset, Plane.YAxis) }; };
    const auto    Facing    = [&](int Face) { const auto* Surface = std::get_if<PlaneSurface>(&Blueprint.Faces[Face].Surface);
                                              return Surface ? std::optional<Vector3>(Surface->Frame.ZAxis * SignOfSense(Blueprint.Faces[Face].Sense)) : std::nullopt; };

    // ① Rims: straight edges of the coplanar faces facing Normal (the region's own edges excluded) whose neighbour is a
    //    plane at 90° or sharper — the in-plane outward t × n of the rim leaves the material.
    int RegionIndex = -1;
    if (Region)
        if (const auto Found = Description.FaceIndex.find(Region->Slot); Found != Description.FaceIndex.end()) RegionIndex = Found->second;
    std::set<int> RegionEdges;
    if (RegionIndex >= 0) for (const auto& Loop : Blueprint.Faces[RegionIndex].Loops) for (const BlueprintUse& Use : Loop) RegionEdges.insert(Use.Edge);
    std::map<int, std::vector<int>> EdgeFaces;                                               // Edge → faces using it
    for (size_t Face = 0; Face < Blueprint.Faces.size(); ++Face)
        for (const auto& Loop : Blueprint.Faces[Face].Loops) for (const BlueprintUse& Use : Loop) EdgeFaces[Use.Edge].push_back(static_cast<int>(Face));
    struct Rim { Vector2 From, To, Outward; };
    std::vector<Rim> Rims;
    for (int Face = 0; Face < static_cast<int>(Blueprint.Faces.size()); ++Face)
    {
        const std::optional<Vector3> Own = Facing(Face);
        if (Face == RegionIndex || !Own || Dot(*Own, Normal) < 1.0 - 1.0e-12) continue;
        if (std::fabs(Dot(std::get<PlaneSurface>(Blueprint.Faces[Face].Surface).Frame.Origin - Plane.Origin, Normal)) > Tolerance) continue;
        for (const auto& Loop : Blueprint.Faces[Face].Loops)
        {
            for (const BlueprintUse& Use : Loop)
            {
                const BlueprintEdge& Edge = Blueprint.Edges[Use.Edge];
                if (RegionEdges.contains(Use.Edge) || !std::holds_alternative<StraightLine>(Edge.Curve)) continue;
                const bool    Forward = Use.Sense == Orientation::Forward;
                const Vector3 From    = Blueprint.Vertices[Forward ? Edge.Start : Edge.End].Position;
                const Vector3 To      = Blueprint.Vertices[Forward ? Edge.End : Edge.Start].Position;
                if (!(Length(To - From) > Tolerance)) continue;
                const Vector3 Outward = Cross(Normalize(To - From), Normal);                 // [-] - Leaves the face across this rim
                bool Convex = false;
                for (const int Other : EdgeFaces[Use.Edge])
                {
                    const std::optional<Vector3> Beyond = Other == Face ? std::nullopt : Facing(Other);
                    if (Beyond && Dot(*Beyond, Outward) > 1.0e-6 && Dot(*Beyond, Normal) <= 1.0e-9) Convex = true;
                }
                if (Convex) Rims.push_back({ Flatten(From), Flatten(To), Normalize(Vector2{ Dot(Outward, Plane.XAxis), Dot(Outward, Plane.YAxis) }) });
            }
        }
    }
    if (Rims.empty()) return 0;

    // ② The outline's span sets the thinnest strip worth keeping.
    Vector2 Low{ 1.0e300, 1.0e300 }, High{ -1.0e300, -1.0e300 };
    const auto Include = [&](Vector2 Point) { Low = { std::min(Low.X, Point.X), std::min(Low.Y, Point.Y) }; High = { std::max(High.X, Point.X), std::max(High.Y, Point.Y) }; };
    for (const ProfileLoop& Loop : Profile.Loops)
        for (const ProfileSegment& Segment : Loop.Segments)
        {
            Include(Segment.Start);
            Include(Segment.End);
            if (Segment.Kind == ProfileSegmentCategory::Arc || Segment.Kind == ProfileSegmentCategory::Circle)
            {
                Include(Segment.Centre - Vector2{ Segment.Radius, Segment.Radius });
                Include(Segment.Centre + Vector2{ Segment.Radius, Segment.Radius });
            }
            for (const Vector2& Pole : Segment.Spline.Poles) Include(Pole);
        }
    const double Bridge = BridgeFraction * Length(High - Low);                               // [len] - Strips up to this thin open
    const double Lead   = LeadFraction * Profile.Scale;                                      // [len] - Past the rim

    // ③ Outer loops: a straight side between straight neighbours, parallel to a rim, facing out across it, within its
    //    extent and at most Bridge inside it moves out to Lead past it; the corners are re-intersected.
    const SketchProfile Original = Profile;
    int Opened = 0;
    for (size_t LoopIndex = 0; LoopIndex < Profile.Loops.size(); ++LoopIndex)
    {
        ProfileLoop&       Loop  = Profile.Loops[LoopIndex];
        const ProfileLoop& Was   = Original.Loops[LoopIndex];
        const size_t       Count = Loop.Segments.size();
        if (!(Loop.SignedArea > 0.0) || Count < 3) continue;
        std::vector<double>  Shift(Count, 0.0);
        std::vector<Vector2> Along(Count), Origin(Count);
        bool Moved = false;
        for (size_t Index = 0; Index < Count; ++Index)
        {
            const ProfileSegment& Side = Loop.Segments[Index];
            const double          Span = Distance(Side.Start, Side.End);
            if (Side.Kind != ProfileSegmentCategory::Line || !(Span > Tolerance)) continue;
            Along[Index]  = (Side.End - Side.Start) * (1.0 / Span);
            Origin[Index] = Side.Start;
            const ProfileSegment& Before = Loop.Segments[(Index + Count - 1) % Count];
            const ProfileSegment& After  = Loop.Segments[(Index + 1) % Count];
            if (Before.Kind != ProfileSegmentCategory::Line || After.Kind != ProfileSegmentCategory::Line) continue;
            const Vector2 Out = { Along[Index].Y, -Along[Index].X };                         // [-] - CCW outer loop: material of the cut on the left
            for (const Rim& Edge : Rims)
            {
                if (Dot(Out, Edge.Outward) < 1.0 - 1.0e-12) continue;
                const double Gap   = Dot(Edge.From - Side.Start, Out);                       // [len] - Strip left between the side and the rim
                const double Skew  = Dot(Edge.From - Side.End, Out) - Gap;
                if (std::fabs(Skew) > Tolerance || Gap < -Tolerance || Gap > Bridge) continue;
                const double  Reach = Distance(Edge.From, Edge.To);
                const Vector2 Rail  = (Edge.To - Edge.From) * (1.0 / Reach);
                const double  First = Dot(Side.Start - Edge.From, Rail), Last = Dot(Side.End - Edge.From, Rail);
                if (std::min(First, Last) < -Tolerance || std::max(First, Last) > Reach + Tolerance) continue;
                Shift[Index] = Gap + Lead;
                Moved        = true;
                break;
            }
        }
        if (!Moved) continue;
        bool Usable = true;
        for (size_t Index = 0; Index < Count && Usable; ++Index)
        {
            const size_t Previous = (Index + Count - 1) % Count;
            if (Shift[Index] == 0.0 && Shift[Previous] == 0.0) continue;
            const Vector2 OutPrevious = { Along[Previous].Y, -Along[Previous].X }, OutIndex = { Along[Index].Y, -Along[Index].X };
            const Vector2 Base        = Origin[Previous] + OutPrevious * Shift[Previous];
            const Vector2 Other       = Origin[Index] + OutIndex * Shift[Index];
            const double  Turn        = Cross(Along[Previous], Along[Index]);
            if (std::fabs(Turn) < TurnFloor) { Usable = false; break; }
            const Vector2 Corner = Base + Along[Previous] * (Cross(Other - Base, Along[Index]) / Turn);
            Loop.Segments[Previous].End = Corner;
            Loop.Segments[Index].Start  = Corner;
        }
        for (size_t Index = 0; Index < Count && Usable; ++Index)                             // Every side keeps its direction
        {
            const ProfileSegment& Side = Loop.Segments[Index];
            if (Side.Kind == ProfileSegmentCategory::Line && Dot(Side.End - Side.Start, Was.Segments[Index].End - Was.Segments[Index].Start) <= 0.0) Usable = false;
        }
        if (!Usable) { Profile = Original; return 0; }
        for (const double Each : Shift) Opened += Each != 0.0 ? 1 : 0;
    }
    if (Opened == 0) return 0;
    ArrangeProfileLoops(Profile);
    if (!Profile.Refusal.empty()) { Profile = Original; return 0; }
    return Opened;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RESHAPE (VERTICES / EDGES)
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    [[nodiscard]] double ReshapeCross2D(Vector2 First, Vector2 Second) noexcept
    {
        return First.X * Second.Y - First.Y * Second.X;
    }

    [[nodiscard]] double ReshapeSegmentPointDistance2D(Vector2 Point, Vector2 Start, Vector2 End) noexcept
    {
        const Vector2 Chord  = End - Start;
        const double  LengthSq = Chord.X * Chord.X + Chord.Y * Chord.Y;
        if (LengthSq <= 1.0e-24) return std::hypot(Point.X - Start.X, Point.Y - Start.Y);
        const double Share = std::clamp(((Point.X - Start.X) * Chord.X + (Point.Y - Start.Y) * Chord.Y) / LengthSq, 0.0, 1.0);
        return std::hypot(Point.X - (Start.X + Chord.X * Share), Point.Y - (Start.Y + Chord.Y * Share));
    }

    [[nodiscard]] bool ReshapeSegmentsCross2D(Vector2 A0, Vector2 A1, Vector2 B0, Vector2 B1) noexcept
    {
        constexpr double Eps = 1.0e-11;
        const auto Orient = [](Vector2 P, Vector2 Q, Vector2 R) noexcept { return ReshapeCross2D(Q - P, R - P); };
        const auto OnSpan = [&](Vector2 P, Vector2 Q, Vector2 R) noexcept
        {
            return R.X >= std::min(P.X, Q.X) - Eps && R.X <= std::max(P.X, Q.X) + Eps &&
                   R.Y >= std::min(P.Y, Q.Y) - Eps && R.Y <= std::max(P.Y, Q.Y) + Eps;
        };
        const double O1 = Orient(A0, A1, B0);
        const double O2 = Orient(A0, A1, B1);
        const double O3 = Orient(B0, B1, A0);
        const double O4 = Orient(B0, B1, A1);
        if (((O1 > Eps && O2 < -Eps) || (O1 < -Eps && O2 > Eps)) &&
            ((O3 > Eps && O4 < -Eps) || (O3 < -Eps && O4 > Eps)))
            return true;
        if (std::fabs(O1) <= Eps && OnSpan(A0, A1, B0)) return true;
        if (std::fabs(O2) <= Eps && OnSpan(A0, A1, B1)) return true;
        if (std::fabs(O3) <= Eps && OnSpan(B0, B1, A0)) return true;
        if (std::fabs(O4) <= Eps && OnSpan(B0, B1, A1)) return true;
        return false;
    }

    [[nodiscard]] bool ReshapePointInPolygon2D(Vector2 Point, const std::vector<Vector2>& Polygon) noexcept
    {
        bool Inside = false;
        const size_t Count = Polygon.size();
        for (size_t Curr = 0, Prev = Count - 1; Curr < Count; Prev = Curr++)
        {
            const Vector2 A = Polygon[Prev];
            const Vector2 B = Polygon[Curr];
            if (((A.Y > Point.Y) != (B.Y > Point.Y)) &&
                (Point.X < (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y) + A.X))
                Inside = !Inside;
        }
        return Inside;
    }

    [[nodiscard]] bool ReshapePointInTriangle2D(Vector2 Point, Vector2 A, Vector2 B, Vector2 C) noexcept
    {
        constexpr double Eps = -1.0e-11;
        return ReshapeCross2D(B - A, Point - A) >= Eps &&
               ReshapeCross2D(C - B, Point - B) >= Eps &&
               ReshapeCross2D(A - C, Point - C) >= Eps;
    }

    [[nodiscard]] NurbsCurve NormalizeOrientedNurbsSpan(const BlueprintEdge& Edge, Orientation Sense)
    {
        std::optional<NurbsCurve> Converted = ConvertCurveToNurbs(Edge.Curve, Edge.Low, Edge.High);
        NurbsCurveDefinition      Def       = Converted && Converted->Definition
                                            ? *Converted->Definition
                                            : *ConvertLineToNurbs(StraightLine{ EvaluateCurve(Edge.Curve, Edge.Low).Position,
                                                                                Normalize(EvaluateCurve(Edge.Curve, Edge.High).Position - EvaluateCurve(Edge.Curve, Edge.Low).Position) },
                                                                  0.0, std::max(1.0e-9, Distance(EvaluateCurve(Edge.Curve, Edge.Low).Position, EvaluateCurve(Edge.Curve, Edge.High).Position))).Definition;
        if (Sense == Orientation::Reversed)
            Def = *ReverseNurbsCurve(Def).Definition;
        const NurbsDomain Dom  = MeasureNurbsCurveDomain(Def);
        const double      Span = std::max(1.0e-12, Dom.High - Dom.Low);
        for (double& Knot : Def.Knots)
            Knot = std::clamp((Knot - Dom.Low) / Span, 0.0, 1.0);
        for (int K = 0; K <= Def.Degree && K < static_cast<int>(Def.Knots.size()); ++K)
        {
            Def.Knots[K] = 0.0;
            Def.Knots[Def.Knots.size() - 1 - K] = 1.0;
        }
        Def.Period = 0.0;
        return NurbsCurve{ std::make_shared<NurbsCurveDefinition>(std::move(Def)) };
    }

    // Exact ruled rational NURBS surface over a 4-sided loop with two opposite straight edges and two compatible rail edges
    // (such as an extruded circular-arc wheel arch or spline wall when one or more corners move).
    [[nodiscard]] std::optional<NurbsSurface> BuildRuledQuadCarrier(const BodyBlueprint& Blueprint, const std::vector<BlueprintUse>& Loop)
    {
        if (Loop.size() != 4) return std::nullopt;
        int RailSlot = -1;
        if (std::holds_alternative<StraightLine>(Blueprint.Edges[Loop[1].Edge].Curve) &&
            std::holds_alternative<StraightLine>(Blueprint.Edges[Loop[3].Edge].Curve))
        {
            RailSlot = 0;
        }
        else if (std::holds_alternative<StraightLine>(Blueprint.Edges[Loop[0].Edge].Curve) &&
                 std::holds_alternative<StraightLine>(Blueprint.Edges[Loop[2].Edge].Curve))
        {
            RailSlot = 1;
        }
        if (RailSlot < 0) return std::nullopt;

        const BlueprintUse& LowerUse = Loop[RailSlot];
        const BlueprintUse& UpperUse = Loop[(RailSlot + 2) % 4];
        const Orientation   UpperOpposite = UpperUse.Sense == Orientation::Forward ? Orientation::Reversed : Orientation::Forward;
        const NurbsCurve    LowerCurve = NormalizeOrientedNurbsSpan(Blueprint.Edges[LowerUse.Edge], LowerUse.Sense);
        const NurbsCurve    UpperCurve = NormalizeOrientedNurbsSpan(Blueprint.Edges[UpperUse.Edge], UpperOpposite);
        if (!LowerCurve.Definition || !UpperCurve.Definition) return std::nullopt;
        const NurbsCurveDefinition& A = *LowerCurve.Definition;
        const NurbsCurveDefinition& B = *UpperCurve.Definition;
        if (A.Degree != B.Degree || A.Poles.size() != B.Poles.size() || A.Knots.size() != B.Knots.size())
            return std::nullopt;
        for (size_t K = 0; K < A.Knots.size(); ++K)
            if (std::fabs(A.Knots[K] - B.Knots[K]) > 1.0e-6) return std::nullopt;
        for (size_t P = 0; P < A.Weights.size(); ++P)
            if (std::fabs(A.Weights[P] - B.Weights[P]) > 1.0e-6) return std::nullopt;

        const int CountU = static_cast<int>(A.Poles.size());
        std::vector<Vector3> Poles(static_cast<size_t>(CountU) * 2);
        std::vector<double>  Weights(static_cast<size_t>(CountU) * 2);
        for (int I = 0; I < CountU; ++I)
        {
            Poles[static_cast<size_t>(I) * 2 + 0]   = A.Poles[I];
            Poles[static_cast<size_t>(I) * 2 + 1]   = B.Poles[I];
            const double SharedWeight               = 0.5 * (A.Weights[I] + B.Weights[I]);
            Weights[static_cast<size_t>(I) * 2 + 0] = SharedWeight;
            Weights[static_cast<size_t>(I) * 2 + 1] = SharedWeight;
        }
        return ConstructNurbsSurface(A.Degree, 1, CountU, 2, A.Knots, { 0.0, 0.0, 1.0, 1.0 }, std::move(Poles), std::move(Weights));
    }
}

std::string ReshapeBodyVertices(ModelPartition& Partition, BodyToken Body, const std::vector<VertexToken>& Vertices, const AffineMapping& Map,
                                BodyToken& Result, std::vector<FaceChangeHistory>& Histories, AuditReport& Audit, std::string& Method)
{
    BodyDescription Description;
    if (std::string Refusal = DescribeBody(Partition, Body, Description); !Refusal.empty()) return "Reshape: " + Refusal;
    BodyBlueprint& Blueprint = Description.Blueprint;
    if (Blueprint.Category != BodyCategory::Solid && Blueprint.Category != BodyCategory::Sheet)
        return "Reshape: unsupported body category";
    if (Vertices.empty()) return "Reshape: pick vertices or edges first";
    const double Tolerance = 1.0e-8 * std::max(1.0, Description.Scale);                     // [len] - Coplanarity / travel floor (Accept)
    const auto   Carry     = [&](Vector3 Point) { return Map.ColumnX * Point.X + Map.ColumnY * Point.Y + Map.ColumnZ * Point.Z + Map.Shift; };

    // ① New positions for the picked vertices (recording pre-move positions so already-bent NURBS faces and N-gon planar
    //    decompositions keep their pre-move chart orientation and visibility).
    const size_t         VertexCount = Blueprint.Vertices.size();
    std::vector<Vector3> OriginalPositions(VertexCount);
    for (size_t Index = 0; Index < VertexCount; ++Index) OriginalPositions[Index] = Blueprint.Vertices[Index].Position;
    std::vector<char>    Moved(VertexCount, 0);
    for (VertexToken Vertex : Vertices)
    {
        const auto Found = Description.VertexIndex.find(Vertex.Slot);
        if (Found == Description.VertexIndex.end()) return "Reshape: " + LabelToken("V", Vertex) + " is not a vertex of the body";
        Moved[Found->second] = 1;
    }
    double Travel = 0.0;                                                                     // [len] - Longest vertex move
    for (size_t Index = 0; Index < VertexCount; ++Index)
    {
        if (!Moved[Index]) continue;
        const Vector3 Was = Blueprint.Vertices[Index].Position;
        Blueprint.Vertices[Index].Position = Carry(Was);
        Travel = std::max(Travel, Distance(Was, Blueprint.Vertices[Index].Position));
    }
    if (Travel <= Tolerance) return "Reshape: the move is zero";

    // ② Incident edges follow their moved endpoints: straight lines re-draw between their new ends, in-plane circular arcs
    //    preserve their sweep angle, and off-plane / freeform curves deform smoothly along their clamped NURBS poles.
    const size_t      OriginalEdgeCount = Blueprint.Edges.size();
    std::vector<char> EdgeChanged(OriginalEdgeCount, 0);
    int               ChangedEdges = 0;
    for (size_t Index = 0; Index < OriginalEdgeCount; ++Index)
    {
        BlueprintEdge& Edge = Blueprint.Edges[Index];
        if (!Moved[Edge.Start] && !Moved[Edge.End]) continue;
        const std::string Name   = LabelToken("E", Description.Inventory.Edges[Index]);
        const Vector3     From   = Blueprint.Vertices[Edge.Start].Position;
        const Vector3     Until  = Blueprint.Vertices[Edge.End].Position;
        const double      Extent = Distance(From, Until);                                    // [len]
        if (Edge.Start != Edge.End && Extent <= 1.0e-6 * Description.Scale)
            return "Reshape: edge " + Name + " would shrink to a point";

        if (std::holds_alternative<StraightLine>(Edge.Curve))
        {
            Edge.Curve = StraightLine{ From, (Until - From) * (1.0 / Extent) };
            Edge.Low   = 0.0;
            Edge.High  = Extent;
        }
        else if (const auto* Arc = std::get_if<CircularArc>(&Edge.Curve);
                 Arc && Edge.Start != Edge.End &&
                 std::fabs(Dot(Until - From, Arc->Frame.ZAxis)) <= Tolerance &&
                 (Edge.High - Edge.Low) > 1.0e-6 && (Edge.High - Edge.Low) < 2.0 * std::numbers::pi - 1.0e-6)
        {
            const double  Sweep       = Edge.High - Edge.Low;
            const Vector3 Chord       = Until - From;
            const double  ChordLength = Length(Chord);
            const Vector3 Perp        = Normalize(Cross(Arc->Frame.ZAxis, Chord));
            const double  HalfAngle   = 0.5 * Sweep;
            const double  NewRadius   = ChordLength / (2.0 * std::sin(HalfAngle));
            const Vector3 NewOrigin   = (From + Until) * 0.5 + Perp * (0.5 * ChordLength / std::tan(HalfAngle));
            const Vector3 NewXAxis    = Normalize(From - NewOrigin);
            const Vector3 NewYAxis    = Cross(Arc->Frame.ZAxis, NewXAxis);
            Edge.Curve = CircularArc{ { NewOrigin, NewXAxis, NewYAxis, Arc->Frame.ZAxis }, NewRadius };
            Edge.Low   = 0.0;
            Edge.High  = Sweep;
        }
        else
        {
            std::optional<NurbsCurve> Converted = ConvertCurveToNurbs(Edge.Curve, Edge.Low, Edge.High);
            if (!Converted || !Converted->Definition)
                return "Reshape: edge " + Name + " (" + CurveCategoryLabel(Edge.Curve) + ") cannot deform with a moved vertex";
            NurbsCurveDefinition Def = *Converted->Definition;
            const NurbsDomain    Dom = MeasureNurbsCurveDomain(Def);
            const double         DomSpan = std::max(1.0e-12, Dom.High - Dom.Low);
            const Vector3        ShiftStart = From - OriginalPositions[Edge.Start];
            const Vector3        ShiftEnd   = Until - OriginalPositions[Edge.End];
            const int            PoleCount  = static_cast<int>(Def.Poles.size());
            for (int PoleIdx = 0; PoleIdx < PoleCount; ++PoleIdx)
            {
                double Greville = 0.0;
                for (int K = 1; K <= Def.Degree; ++K) Greville += Def.Knots[PoleIdx + K];
                Greville /= std::max(1, Def.Degree);
                const double Share = PoleIdx == 0 ? 0.0 : (PoleIdx == PoleCount - 1 ? 1.0 : std::clamp((Greville - Dom.Low) / DomSpan, 0.0, 1.0));
                Def.Poles[PoleIdx] = Def.Poles[PoleIdx] + ShiftStart * (1.0 - Share) + ShiftEnd * Share;
            }
            if (Edge.Start != Edge.End && !Def.Poles.empty())
            {
                Def.Poles.front() = From;
                Def.Poles.back()  = Until;
            }
            Edge.Curve = NurbsCurve{ std::make_shared<NurbsCurveDefinition>(std::move(Def)) };
            Edge.Low   = Dom.Low;
            Edge.High  = Dom.High;
        }
        EdgeChanged[Index] = 1;
        ++ChangedEdges;
    }

    // ③ Faces at a moved vertex: plane re-fit (including concave N-gons and faces with curved wheel arches/holes),
    //    smooth bilinear / ruled NURBS patch on 4-sided faces, or minimal planar chord decomposition on off-plane N-gons.
    const size_t                   OriginalFaceCount = Blueprint.Faces.size();
    std::vector<FaceChangeHistory> Planned(OriginalFaceCount);
    int    PlanarFaces = 0, BentFaces = 0, SplitFacets = 0;
    double Bow         = 0.0;                                                                // [len] - Worst off-plane corner among bent faces

    auto ConstructFittedPlane = [](Vector3 Centre, Vector3 Normal, Vector3 RefXAxis, Vector3 RefYAxis) -> PlaneSurface
    {
        Vector3 Along = RefXAxis - Normal * Dot(RefXAxis, Normal);
        if (Length(Along) < 1.0e-6) Along = RefYAxis - Normal * Dot(RefYAxis, Normal);
        if (Length(Along) < 1.0e-6)
        {
            const Vector3 Seed = std::fabs(Normal.X) < 0.9 ? Vector3{ 1.0, 0.0, 0.0 } : Vector3{ 0.0, 1.0, 0.0 };
            Along = Seed - Normal * Dot(Seed, Normal);
        }
        Along = Along * (1.0 / Length(Along));
        PlaneSurface Fitted;
        Fitted.Frame = { Centre, Along, Cross(Normal, Along), Normal };
        return Fitted;
    };

    for (size_t Index = 0; Index < OriginalFaceCount; ++Index)
    {
        const FaceToken Token = Description.Inventory.Faces[Index];
        Planned[Index]        = { {}, FaceChangeOrigin::Unchanged, Token, {} };
        bool Touched          = false;
        for (const std::vector<BlueprintUse>& Loop : Blueprint.Faces[Index].Loops)
            for (const BlueprintUse& Use : Loop)
                if (static_cast<size_t>(Use.Edge) < OriginalEdgeCount && EdgeChanged[Use.Edge]) Touched = true;
        if (!Touched) continue;

        const std::string Name  = LabelToken("F", Token);
        const auto*       Plane = std::get_if<PlaneSurface>(&Blueprint.Faces[Index].Surface);
        const auto*       Nurbs = std::get_if<NurbsSurface>(&Blueprint.Faces[Index].Surface);

        // Pre-move outward orientation and local chart axes.
        Vector3 Outward{};
        Vector3 RefOrigin{};
        Vector3 RefXAxis{ 1.0, 0.0, 0.0 }, RefYAxis{ 0.0, 1.0, 0.0 };
        if (Plane)
        {
            Outward   = Plane->Frame.ZAxis * SignOfSense(Blueprint.Faces[Index].Sense);
            RefOrigin = Plane->Frame.Origin;
            RefXAxis  = Plane->Frame.XAxis;
            RefYAxis  = Cross(Outward, RefXAxis);
        }
        else
        {
            Vector3 PreNewell{}, PreSum{};
            int     PreCount = 0;
            for (const std::vector<BlueprintUse>& Loop : Blueprint.Faces[Index].Loops)
            {
                std::vector<Vector3> PreRing;
                for (const BlueprintUse& Use : Loop)
                {
                    const BlueprintEdge& Edge = Blueprint.Edges[Use.Edge];
                    PreRing.push_back(OriginalPositions[Use.Sense == Orientation::Forward ? Edge.Start : Edge.End]);
                    PreSum = PreSum + PreRing.back();
                    ++PreCount;
                }
                for (size_t Corner = 0; Corner < PreRing.size(); ++Corner)
                    PreNewell = PreNewell + Cross(PreRing[Corner], PreRing[(Corner + 1) % PreRing.size()]);
            }
            if (PreCount > 0) RefOrigin = PreSum * (1.0 / PreCount);
            if (Length(PreNewell) > 1.0e-12 * Description.Scale * Description.Scale)
            {
                Outward = Normalize(PreNewell);
            }
            else if (Nurbs && Nurbs->Definition)
            {
                const NurbsDomain DomU = MeasureNurbsSurfaceDomain(*Nurbs->Definition, true);
                const NurbsDomain DomV = MeasureNurbsSurfaceDomain(*Nurbs->Definition, false);
                Outward = EvaluateNurbsNormal(*Nurbs, { 0.5 * (DomU.Low + DomU.High), 0.5 * (DomV.Low + DomV.High) }) * SignOfSense(Blueprint.Faces[Index].Sense);
            }
            else
            {
                Vector2 MidUV{};
                if (!Blueprint.Faces[Index].Loops.empty() && !Blueprint.Faces[Index].Loops.front().empty())
                {
                    const BlueprintUse&  FirstUse  = Blueprint.Faces[Index].Loops.front().front();
                    const BlueprintEdge& FirstEdge = Blueprint.Edges[FirstUse.Edge];
                    MidUV = EvaluateParameterCurve(FirstUse.SurfaceCurve, 0.5 * (FirstEdge.Low + FirstEdge.High)).Position;
                }
                Outward = EvaluateSurfaceNormal(Blueprint.Faces[Index].Surface, MidUV) * SignOfSense(Blueprint.Faces[Index].Sense);
            }
            const Vector3 Seed = std::fabs(Outward.X) < 0.9 ? Vector3{ 1.0, 0.0, 0.0 } : Vector3{ 0.0, 1.0, 0.0 };
            RefXAxis = Normalize(Seed - Outward * Dot(Seed, Outward));
            RefYAxis = Cross(Outward, RefXAxis);
        }

        // Gather post-move corner rings AND dense boundary samples (so curved edges contribute to Newell area & coplanarity).
        std::vector<std::vector<Vector3>> Corners;
        std::vector<std::vector<Vector3>> DenseRings;
        bool    AllEdgesStraight = true;
        Vector3 Newell{}, Sum{};
        int     SampleCount = 0;
        for (const std::vector<BlueprintUse>& Loop : Blueprint.Faces[Index].Loops)
        {
            std::vector<Vector3> Ring;
            std::vector<Vector3> Dense;
            for (const BlueprintUse& Use : Loop)
            {
                const BlueprintEdge& Edge    = Blueprint.Edges[Use.Edge];
                const bool           Forward = Use.Sense == Orientation::Forward;
                const Vector3        StartPt = Blueprint.Vertices[Forward ? Edge.Start : Edge.End].Position;
                Ring.push_back(StartPt);
                if (std::holds_alternative<StraightLine>(Edge.Curve))
                {
                    Dense.push_back(StartPt);
                    Sum = Sum + StartPt;
                    ++SampleCount;
                }
                else
                {
                    AllEdgesStraight = false;
                    constexpr int CurveSteps = 12;
                    const double  FromParam  = Forward ? Edge.Low : Edge.High;
                    const double  UntilParam = Forward ? Edge.High : Edge.Low;
                    for (int Step = 0; Step < CurveSteps; ++Step)
                    {
                        const double  Param = FromParam + (UntilParam - FromParam) * (static_cast<double>(Step) / CurveSteps);
                        const Vector3 Pt    = Step == 0 ? StartPt : EvaluateCurve(Edge.Curve, Param).Position;
                        Dense.push_back(Pt);
                        Sum = Sum + Pt;
                        ++SampleCount;
                    }
                }
            }
            for (size_t K = 0; K < Dense.size(); ++K)
                Newell = Newell + Cross(Dense[K], Dense[(K + 1) % Dense.size()]);
            Corners.push_back(std::move(Ring));
            DenseRings.push_back(std::move(Dense));
        }

        if (Length(Newell) <= 1.0e-12 * Description.Scale * Description.Scale)
            return "Reshape: face " + Name + " would collapse";
        const Vector3 Normal = Newell * (1.0 / Length(Newell));
        if (Dot(Normal, Outward) <= 0.0)
            return "Reshape: face " + Name + " would turn inside out";

        const Vector3 Centre   = Sum * (1.0 / std::max(1, SampleCount));
        double        Residual = 0.0;
        for (const std::vector<Vector3>& Dense : DenseRings)
            for (const Vector3& Pt : Dense)
                Residual = std::max(Residual, std::fabs(Dot(Pt - Centre, Normal)));

        // ③a Coplanar post-move face (any side count, concave or convex, straight or curved edges, with or without holes):
        //    verify no boundary ring self-intersects in the fitted plane, then re-fit an exact PlaneSurface.
        if (Residual <= Tolerance)
        {
            const PlaneSurface Fitted = ConstructFittedPlane(Centre, Normal, RefXAxis, RefYAxis);
            for (const std::vector<Vector3>& Dense : DenseRings)
            {
                std::vector<Vector2> Poly2D;
                Poly2D.reserve(Dense.size());
                for (const Vector3& Pt : Dense)
                {
                    const Vector3 Rel = Pt - Fitted.Frame.Origin;
                    Poly2D.push_back({ Dot(Rel, Fitted.Frame.XAxis), Dot(Rel, Fitted.Frame.YAxis) });
                }
                const size_t N = Poly2D.size();
                for (size_t A = 0; A < N; ++A)
                {
                    const size_t A1 = (A + 1) % N;
                    for (size_t B = A + 2; B < N; ++B)
                    {
                        if (A == 0 && B + 1 == N) continue;
                        const size_t B1 = (B + 1) % N;
                        if (ReshapeSegmentsCross2D(Poly2D[A], Poly2D[A1], Poly2D[B], Poly2D[B1]))
                            return "Reshape: face " + Name + " boundary would cross itself";
                    }
                }
            }
            BlueprintFace& Face = Blueprint.Faces[Index];
            Face.Surface        = Fitted;
            Face.Sense          = Orientation::Forward;
            for (std::vector<BlueprintUse>& Loop : Face.Loops)
                FitLoopSurfaceCurves(Face.Surface, Blueprint, Loop);
            Planned[Index].Origin = FaceChangeOrigin::Replaced;
            ++PlanarFaces;
            continue;
        }

        // ③b Bent 4-sided single-loop face: try exact bilinear NURBS patch (all straight edges) or exact ruled rational NURBS
        //    patch (two opposite straight edges + two compatible curved rails, e.g. an extruded wheel arch cylinder).
        if (Blueprint.Faces[Index].Loops.size() == 1 && Corners.front().size() == 4)
        {
            if (AllEdgesStraight)
            {
                const std::vector<Vector3>& Ring = Corners.front();
                const Vector3 A = Ring[0], B = Ring[1], C = Ring[2], D = Ring[3];
                const Vector3 CornerNormals[] = { Cross(B - A, D - A), Cross(B - A, C - B), Cross(C - D, D - A), Cross(C - D, C - B) };
                bool UnfoldedCondition = true;
                for (const Vector3& CornerNormal : CornerNormals)
                    if (Dot(CornerNormal, Normal) <= 1.0e-12 * Description.Scale * Description.Scale) UnfoldedCondition = false;
                if (UnfoldedCondition)
                {
                    if (std::optional<NurbsSurface> Patch = ConstructNurbsSurface(1, 1, 2, 2, { 0.0, 0.0, 1.0, 1.0 }, { 0.0, 0.0, 1.0, 1.0 }, { A, D, B, C }, { 1.0, 1.0, 1.0, 1.0 }))
                    {
                        BlueprintFace& Face = Blueprint.Faces[Index];
                        Face.Surface        = *Patch;
                        Face.Sense          = Orientation::Forward;
                        for (std::vector<BlueprintUse>& Loop : Face.Loops)
                            FitLoopSurfaceCurves(Face.Surface, Blueprint, Loop);
                        Planned[Index].Origin = FaceChangeOrigin::Replaced;
                        Bow                   = std::max(Bow, Residual);
                        ++BentFaces;
                        continue;
                    }
                }
                // Twisted/folded quad falls through to ③c to split cleanly along its non-folding diagonal into two planar triangles.
            }
            else if (std::optional<NurbsSurface> Ruled = BuildRuledQuadCarrier(Blueprint, Blueprint.Faces[Index].Loops.front()))
            {
                BlueprintFace&      Face = Blueprint.Faces[Index];
                const NurbsDomain   DomU = MeasureNurbsSurfaceDomain(*Ruled->Definition, true);
                const NurbsDomain   DomV = MeasureNurbsSurfaceDomain(*Ruled->Definition, false);
                const Vector3       MidN = EvaluateNurbsNormal(*Ruled, { 0.5 * (DomU.Low + DomU.High), 0.5 * (DomV.Low + DomV.High) });
                Face.Surface             = *Ruled;
                Face.Sense               = Dot(MidN, Normal) >= 0.0 ? Orientation::Forward : Orientation::Reversed;
                for (std::vector<BlueprintUse>& Loop : Face.Loops)
                    FitLoopSurfaceCurves(Face.Surface, Blueprint, Loop);
                Planned[Index].Origin = FaceChangeOrigin::Replaced;
                Bow                   = std::max(Bow, Residual);
                ++BentFaces;
                continue;
            }
            else
            {
                std::vector<NurbsCurve> EdgeCurves;
                EdgeCurves.reserve(4);
                for (const BlueprintUse& Use : Blueprint.Faces[Index].Loops.front())
                    EdgeCurves.push_back(NormalizeOrientedNurbsSpan(Blueprint.Edges[Use.Edge], Use.Sense));
                const ClosedLoopSurfaceOutcome Coons = ConstructClosedLoopPanels(EdgeCurves, 1.0e-5 * std::max(1.0, Description.Scale));
                if (Coons.Refusal.empty() && Coons.Panels.size() == 1)
                {
                    BlueprintFace& Face = Blueprint.Faces[Index];
                    const Vector3  MidN = EvaluateNurbsNormal(Coons.Panels[0].Surface, { 0.5, 0.5 });
                    Face.Surface        = Coons.Panels[0].Surface;
                    Face.Sense          = Dot(MidN, Normal) >= 0.0 ? Orientation::Forward : Orientation::Reversed;
                    for (std::vector<BlueprintUse>& Loop : Face.Loops)
                        FitLoopSurfaceCurves(Face.Surface, Blueprint, Loop);
                    Planned[Index].Origin = FaceChangeOrigin::Replaced;
                    Bow                   = std::max(Bow, Residual);
                    ++BentFaces;
                    continue;
                }
            }
        }

        // ③c General single-loop polygon / N-gon bent off its plane (e.g. moving a vertex along X on the 17-sided concave
        //    car side cap, or a quad whose bilinear patch would fold, or a 3-sided curved tent):
        //    triangulate in the pre-move 2D chart (isolating unmoved flat regions first), then greedily merge coplanar
        //    adjacent triangles so ONLY the chords needed around the off-plane vertex(es) remain.
        if (Blueprint.Faces[Index].Loops.size() != 1)
            return "Reshape: face " + Name + " has inner hole loops and bends off its plane (move in the face plane instead)";

        const std::vector<BlueprintUse> OriginalLoop = Blueprint.Faces[Index].Loops.front();
        const size_t                    LoopCount    = OriginalLoop.size();
        if (LoopCount == 3 && !AllEdgesStraight)
        {
            std::vector<NurbsCurve> EdgeCurves;
            EdgeCurves.reserve(3);
            for (const BlueprintUse& Use : OriginalLoop)
                EdgeCurves.push_back(NormalizeOrientedNurbsSpan(Blueprint.Edges[Use.Edge], Use.Sense));
            const ClosedLoopSurfaceOutcome Coons = ConstructClosedLoopPanels(EdgeCurves, 1.0e-5 * std::max(1.0, Description.Scale));
            if (Coons.Refusal.empty() && Coons.Panels.size() == 1)
            {
                BlueprintFace& Face = Blueprint.Faces[Index];
                const Vector3  MidN = EvaluateNurbsNormal(Coons.Panels[0].Surface, { 0.5, 0.5 });
                Face.Surface        = Coons.Panels[0].Surface;
                Face.Sense          = Dot(MidN, Normal) >= 0.0 ? Orientation::Forward : Orientation::Reversed;
                for (std::vector<BlueprintUse>& Loop : Face.Loops)
                    FitLoopSurfaceCurves(Face.Surface, Blueprint, Loop);
                Planned[Index].Origin = FaceChangeOrigin::Replaced;
                Bow                   = std::max(Bow, Residual);
                ++BentFaces;
                continue;
            }
        }

        std::vector<int>     LoopVertices(LoopCount);
        std::vector<Vector2> Pre2D(LoopCount);
        std::vector<char>    OffPlane(LoopCount, 0);
        std::vector<Vector2> PreDense2D;
        std::vector<size_t>  DenseStartIdx(LoopCount);
        auto ProjectPre2D = [&](Vector3 WorldPt) -> Vector2
        {
            const Vector3 Rel = WorldPt - RefOrigin;
            return { Dot(Rel, RefXAxis), Dot(Rel, RefYAxis) };
        };
        for (size_t K = 0; K < LoopCount; ++K)
        {
            const BlueprintUse&  Use     = OriginalLoop[K];
            const BlueprintEdge& Edge    = Blueprint.Edges[Use.Edge];
            const bool           Forward = Use.Sense == Orientation::Forward;
            const int            VIdx    = Forward ? Edge.Start : Edge.End;
            LoopVertices[K]              = VIdx;
            Pre2D[K]                     = ProjectPre2D(OriginalPositions[VIdx]);
            OffPlane[K]                  = std::fabs(Dot(Blueprint.Vertices[VIdx].Position - RefOrigin, Outward)) > Tolerance ? 1 : 0;
            DenseStartIdx[K]             = PreDense2D.size();
            if (std::holds_alternative<StraightLine>(Edge.Curve) || (static_cast<size_t>(Use.Edge) < OriginalEdgeCount && EdgeChanged[Use.Edge]))
            {
                PreDense2D.push_back(Pre2D[K]);
            }
            else
            {
                constexpr int ArcSteps   = 12;
                const double  FromParam  = Forward ? Edge.Low : Edge.High;
                const double  UntilParam = Forward ? Edge.High : Edge.Low;
                for (int Step = 0; Step < ArcSteps; ++Step)
                {
                    const double  Param = FromParam + (UntilParam - FromParam) * (static_cast<double>(Step) / ArcSteps);
                    const Vector3 Pt    = Step == 0 ? OriginalPositions[VIdx] : EvaluateCurve(Edge.Curve, Param).Position;
                    PreDense2D.push_back(ProjectPre2D(Pt));
                }
            }
        }

        double SignedArea2D = 0.0;
        for (size_t K = 0; K < PreDense2D.size(); ++K)
            SignedArea2D += ReshapeCross2D(PreDense2D[K], PreDense2D[(K + 1) % PreDense2D.size()]);
        if (SignedArea2D < 0.0)
        {
            for (Vector2& Pt : Pre2D)      Pt.Y = -Pt.Y;
            for (Vector2& Pt : PreDense2D) Pt.Y = -Pt.Y;
            RefYAxis = RefYAxis * -1.0;
        }

        auto ValidInternalChord2D = [&](size_t U, size_t V) -> bool
        {
            if (U == V || (U + 1) % LoopCount == V || (V + 1) % LoopCount == U) return false;
            const Vector2 A = Pre2D[U];
            const Vector2 B = Pre2D[V];
            if (std::hypot(B.X - A.X, B.Y - A.Y) <= 1.0e-8 * Description.Scale) return false;
            const size_t DenseN = PreDense2D.size();
            const size_t DU     = DenseStartIdx[U];
            const size_t DV     = DenseStartIdx[V];
            for (size_t S = 0; S < DenseN; ++S)
            {
                const size_t SNext = (S + 1) % DenseN;
                if (S == DU || SNext == DU || S == DV || SNext == DV) continue;
                if (ReshapeSegmentsCross2D(A, B, PreDense2D[S], PreDense2D[SNext])) return false;
                if (ReshapeSegmentPointDistance2D((A + B) * 0.5, PreDense2D[S], PreDense2D[SNext]) <= 1.0e-9 * Description.Scale) return false;
            }
            return ReshapePointInPolygon2D((A + B) * 0.5, PreDense2D);
        };

        struct SubSide
        {
            int  StartLoop      = 0;                                                         // [-] - Index in [0 .. LoopCount-1]
            int  EndLoop        = 0;                                                         // [-] - Index in [0 .. LoopCount-1]
            int  OriginalUseIdx = -1;                                                        // [-] - >= 0 for original boundary use, -1 for internal chord
            int  ChordId        = -1;                                                        // [-] - Shared chord identifier when OriginalUseIdx < 0
            bool ChordForward   = true;                                                      // [-] - True on (ChordStart -> ChordEnd)
        };
        struct PendingChord
        {
            int StartVert = 0;                                                               // [-] - Blueprint vertex index
            int EndVert   = 0;                                                               // [-] - Blueprint vertex index
        };

        std::vector<PendingChord>       Chords;
        std::map<std::pair<int, int>, std::pair<int, bool>> ChordLookup;
        auto AcquireChord = [&](int LoopU, int LoopV) -> std::pair<int, bool>
        {
            const int VA = LoopVertices[LoopU];
            const int VB = LoopVertices[LoopV];
            const std::pair<int, int> Key = VA < VB ? std::make_pair(VA, VB) : std::make_pair(VB, VA);
            const bool Forward = (VA == Key.first);
            auto Found = ChordLookup.find(Key);
            if (Found != ChordLookup.end()) return { Found->second.first, Forward };
            const int Id = static_cast<int>(Chords.size());
            Chords.push_back({ Key.first, Key.second });
            ChordLookup[Key] = { Id, true };
            return { Id, Forward };
        };

        auto BuildSubSide = [&](int LoopU, int LoopV) -> SubSide
        {
            if ((static_cast<size_t>(LoopU) + 1) % LoopCount == static_cast<size_t>(LoopV))
                return SubSide{ LoopU, LoopV, LoopU, -1, true };
            const auto [Id, Forward] = AcquireChord(LoopU, LoopV);
            return SubSide{ LoopU, LoopV, -1, Id, Forward };
        };

        std::vector<std::vector<SubSide>> SubPolys;
        std::vector<int>                  Active(LoopCount);
        for (size_t K = 0; K < LoopCount; ++K) Active[K] = static_cast<int>(K);

        bool TriangulatedCondition = true;
        while (Active.size() > 3)
        {
            const size_t Rem     = Active.size();
            int          BestPos = -1;
            int          BestTier = 99;
            double       BestScore = -1.0e300;
            for (size_t Pos = 0; Pos < Rem; ++Pos)
            {
                const int IPrev = Active[(Pos + Rem - 1) % Rem];
                const int ICurr = Active[Pos];
                const int INext = Active[(Pos + 1) % Rem];
                const Vector2 A2 = Pre2D[IPrev], B2 = Pre2D[ICurr], C2 = Pre2D[INext];
                const double CrossVal = ReshapeCross2D(B2 - A2, C2 - B2);
                if (CrossVal <= 1.0e-11 * Description.Scale * Description.Scale) continue;

                bool ContainsOther = false;
                for (size_t Other = 0; Other < Rem; ++Other)
                {
                    if (Other == Pos || Other == (Pos + Rem - 1) % Rem || Other == (Pos + 1) % Rem) continue;
                    const int IOther = Active[Other];
                    if (ReshapePointInTriangle2D(Pre2D[IOther], A2, B2, C2)) { ContainsOther = true; break; }
                }
                if (ContainsOther || !ValidInternalChord2D(static_cast<size_t>(IPrev), static_cast<size_t>(INext)))
                    continue;

                // Post-move 3D triangle must remain non-degenerate and avoid turning inside out relative to Outward.
                const Vector3 P0 = Blueprint.Vertices[LoopVertices[IPrev]].Position;
                const Vector3 P1 = Blueprint.Vertices[LoopVertices[ICurr]].Position;
                const Vector3 P2 = Blueprint.Vertices[LoopVertices[INext]].Position;
                const Vector3 TriCross = Cross(P1 - P0, P2 - P0);
                if (Length(TriCross) <= 1.0e-11 * Description.Scale * Description.Scale) continue;
                if (Dot(Normalize(TriCross), Outward) <= 1.0e-4) continue;

                // Prefer clipping flat unmoved ears first (especially around unmoved curved edges like wheel arches)
                // so curved boundary spans stay inside the flat region and never end up on a tilted triangle.
                const bool HasCurvedSpan = ((static_cast<size_t>(IPrev) + 1) % LoopCount == static_cast<size_t>(ICurr) &&
                                            !std::holds_alternative<StraightLine>(Blueprint.Edges[OriginalLoop[IPrev].Edge].Curve)) ||
                                           ((static_cast<size_t>(ICurr) + 1) % LoopCount == static_cast<size_t>(INext) &&
                                            !std::holds_alternative<StraightLine>(Blueprint.Edges[OriginalLoop[ICurr].Edge].Curve));
                const bool TouchesOffPlane = OffPlane[IPrev] || OffPlane[ICurr] || OffPlane[INext];
                if (HasCurvedSpan && TouchesOffPlane) continue;

                const int    Tier  = (!TouchesOffPlane && HasCurvedSpan) ? 0 : (!TouchesOffPlane ? 1 : 2);
                const double LPrev = std::max(1.0e-12, std::hypot(A2.X - B2.X, A2.Y - B2.Y));
                const double LNext = std::max(1.0e-12, std::hypot(C2.X - B2.X, C2.Y - B2.Y));
                const double CosEar = ((A2.X - B2.X) * (C2.X - B2.X) + (A2.Y - B2.Y) * (C2.Y - B2.Y)) / (LPrev * LNext);
                if (Tier < BestTier || (Tier == BestTier && CosEar > BestScore))
                {
                    BestTier  = Tier;
                    BestScore = CosEar;
                    BestPos   = static_cast<int>(Pos);
                }
            }
            if (BestPos < 0) { TriangulatedCondition = false; break; }
            const int IPrev = Active[(BestPos + Rem - 1) % Rem];
            const int ICurr = Active[BestPos];
            const int INext = Active[(BestPos + 1) % Rem];
            SubPolys.push_back({ BuildSubSide(IPrev, ICurr), BuildSubSide(ICurr, INext), BuildSubSide(INext, IPrev) });
            Active.erase(Active.begin() + BestPos);
        }
        if (!TriangulatedCondition || Active.size() != 3)
            return "Reshape: face " + Name + " cannot split into valid planar facets without folding";
        SubPolys.push_back({ BuildSubSide(Active[0], Active[1]), BuildSubSide(Active[1], Active[2]), BuildSubSide(Active[2], Active[0]) });

        struct SubPlaneFit
        {
            bool    Valid  = false;
            Vector3 Centre = {};
            Vector3 Normal = {};
            double  Area   = 0.0;
        };
        auto EvaluateSubPoly = [&](const std::vector<SubSide>& Poly) -> SubPlaneFit
        {
            std::vector<Vector3> Samples;
            Vector3              LocalSum{};
            for (const SubSide& Side : Poly)
            {
                const Vector3 StartPt = Blueprint.Vertices[LoopVertices[Side.StartLoop]].Position;
                if (Side.OriginalUseIdx >= 0 && !std::holds_alternative<StraightLine>(Blueprint.Edges[OriginalLoop[Side.OriginalUseIdx].Edge].Curve))
                {
                    const BlueprintUse&  Use        = OriginalLoop[Side.OriginalUseIdx];
                    const BlueprintEdge& Edge       = Blueprint.Edges[Use.Edge];
                    const bool           Forward    = Use.Sense == Orientation::Forward;
                    const double         FromParam  = Forward ? Edge.Low : Edge.High;
                    const double         UntilParam = Forward ? Edge.High : Edge.Low;
                    constexpr int        Steps      = 12;
                    for (int S = 0; S < Steps; ++S)
                    {
                        const double  Param = FromParam + (UntilParam - FromParam) * (static_cast<double>(S) / Steps);
                        const Vector3 Pt    = S == 0 ? StartPt : EvaluateCurve(Edge.Curve, Param).Position;
                        Samples.push_back(Pt);
                        LocalSum = LocalSum + Pt;
                    }
                }
                else
                {
                    Samples.push_back(StartPt);
                    LocalSum = LocalSum + StartPt;
                }
            }
            if (Samples.size() < 3) return {};
            Vector3 LocalNewell{};
            for (size_t K = 0; K < Samples.size(); ++K)
                LocalNewell = LocalNewell + Cross(Samples[K], Samples[(K + 1) % Samples.size()]);
            const double TwiceArea = Length(LocalNewell);
            if (TwiceArea <= 1.0e-12 * Description.Scale * Description.Scale) return {};
            const Vector3 LocalNormal = LocalNewell * (1.0 / TwiceArea);
            if (Dot(LocalNormal, Outward) <= 0.0) return {};
            const Vector3 LocalCentre = LocalSum * (1.0 / static_cast<double>(Samples.size()));
            for (const Vector3& Pt : Samples)
                if (std::fabs(Dot(Pt - LocalCentre, LocalNormal)) > Tolerance) return {};
            return { true, LocalCentre, LocalNormal, 0.5 * TwiceArea };
        };

        for (const std::vector<SubSide>& Poly : SubPolys)
            if (!EvaluateSubPoly(Poly).Valid)
                return "Reshape: face " + Name + " would fold or warp a curved edge off its plane";

        // Greedily merge coplanar adjacent sub-polygons so flat unmoved regions stay a single clean face.
        for (bool MergedAny = true; MergedAny;)
        {
            MergedAny = false;
            for (size_t P = 0; P < SubPolys.size() && !MergedAny; ++P)
            {
                const SubPlaneFit FitP = EvaluateSubPoly(SubPolys[P]);
                if (!FitP.Valid) continue;
                for (size_t Q = P + 1; Q < SubPolys.size() && !MergedAny; ++Q)
                {
                    const SubPlaneFit FitQ = EvaluateSubPoly(SubPolys[Q]);
                    if (!FitQ.Valid) continue;
                    if (Dot(FitP.Normal, FitQ.Normal) < 1.0 - 1.0e-10) continue;
                    if (std::fabs(Dot(FitQ.Centre - FitP.Centre, FitP.Normal)) > Tolerance) continue;

                    int SharedCount = 0;
                    int PosP = -1, PosQ = -1;
                    for (size_t IP = 0; IP < SubPolys[P].size(); ++IP)
                    {
                        if (SubPolys[P][IP].ChordId < 0) continue;
                        for (size_t IQ = 0; IQ < SubPolys[Q].size(); ++IQ)
                        {
                            if (SubPolys[P][IP].ChordId == SubPolys[Q][IQ].ChordId &&
                                SubPolys[P][IP].ChordForward != SubPolys[Q][IQ].ChordForward)
                            {
                                ++SharedCount;
                                PosP = static_cast<int>(IP);
                                PosQ = static_cast<int>(IQ);
                            }
                        }
                    }
                    if (SharedCount != 1 || PosP < 0 || PosQ < 0) continue;

                    std::vector<SubSide> Combined;
                    Combined.reserve(SubPolys[P].size() + SubPolys[Q].size() - 2);
                    for (int I = 0; I < PosP; ++I) Combined.push_back(SubPolys[P][I]);
                    for (size_t I = static_cast<size_t>(PosQ) + 1; I < SubPolys[Q].size(); ++I) Combined.push_back(SubPolys[Q][I]);
                    for (int I = 0; I < PosQ; ++I) Combined.push_back(SubPolys[Q][I]);
                    for (size_t I = static_cast<size_t>(PosP) + 1; I < SubPolys[P].size(); ++I) Combined.push_back(SubPolys[P][I]);

                    // Forbid figure-8 vertex pinches in the merged polygon cycle.
                    std::set<int> SeenVerts;
                    bool          SimpleCycle = true;
                    for (const SubSide& Side : Combined)
                        if (!SeenVerts.insert(LoopVertices[Side.StartLoop]).second) { SimpleCycle = false; break; }
                    if (!SimpleCycle || !EvaluateSubPoly(Combined).Valid) continue;

                    SubPolys[P] = std::move(Combined);
                    SubPolys.erase(SubPolys.begin() + static_cast<std::ptrdiff_t>(Q));
                    MergedAny = true;
                }
            }
        }

        // Keep the largest sub-face in Blueprint.Faces[Index] (so the primary face name stays stable) and append split facets.
        size_t PrimaryIdx = 0;
        double PrimaryArea = -1.0;
        for (size_t P = 0; P < SubPolys.size(); ++P)
        {
            const SubPlaneFit Fit = EvaluateSubPoly(SubPolys[P]);
            if (Fit.Area > PrimaryArea) { PrimaryArea = Fit.Area; PrimaryIdx = P; }
        }
        if (PrimaryIdx != 0) std::swap(SubPolys[0], SubPolys[PrimaryIdx]);

        // Materialize ONLY the chords that survived coplanar merging so no orphan edges enter Blueprint.Edges.
        std::map<int, int> MaterializedChordEdge;
        for (const std::vector<SubSide>& Poly : SubPolys)
        {
            for (const SubSide& Side : Poly)
            {
                if (Side.ChordId < 0 || MaterializedChordEdge.count(Side.ChordId)) continue;
                const PendingChord& Chord = Chords[Side.ChordId];
                const Vector3       From  = Blueprint.Vertices[Chord.StartVert].Position;
                const Vector3       Until = Blueprint.Vertices[Chord.EndVert].Position;
                const double        Len   = Distance(From, Until);
                if (Len <= 1.0e-6 * Description.Scale)
                    return "Reshape: internal split chord on face " + Name + " would collapse";
                const int NewEdgeIdx = static_cast<int>(Blueprint.Edges.size());
                Blueprint.Edges.push_back({ StraightLine{ From, (Until - From) * (1.0 / Len) }, 0.0, Len, Chord.StartVert, Chord.EndVert });
                MaterializedChordEdge[Side.ChordId] = NewEdgeIdx;
                ++ChangedEdges;
            }
        }

        for (size_t P = 0; P < SubPolys.size(); ++P)
        {
            const SubPlaneFit Fit = EvaluateSubPoly(SubPolys[P]);
            BlueprintFace     BuiltFace;
            BuiltFace.Surface = ConstructFittedPlane(Fit.Centre, Fit.Normal, RefXAxis, RefYAxis);
            BuiltFace.Sense   = Orientation::Forward;
            std::vector<BlueprintUse> BuiltLoop;
            BuiltLoop.reserve(SubPolys[P].size());
            for (const SubSide& Side : SubPolys[P])
            {
                if (Side.OriginalUseIdx >= 0)
                {
                    BuiltLoop.push_back(OriginalLoop[Side.OriginalUseIdx]);
                }
                else
                {
                    BuiltLoop.push_back({ MaterializedChordEdge.at(Side.ChordId),
                                          Side.ChordForward ? Orientation::Forward : Orientation::Reversed,
                                          ParameterLine{} });
                }
            }
            FitLoopSurfaceCurves(BuiltFace.Surface, Blueprint, BuiltLoop);
            BuiltFace.Loops.push_back(std::move(BuiltLoop));

            if (P == 0)
            {
                Blueprint.Faces[Index] = std::move(BuiltFace);
                Planned[Index].Origin  = FaceChangeOrigin::Replaced;
                ++PlanarFaces;
            }
            else
            {
                Blueprint.Faces.push_back(std::move(BuiltFace));
                Planned.push_back({ {}, FaceChangeOrigin::Replaced, Token, {} });
                ++SplitFacets;
            }
        }
        Bow = std::max(Bow, Residual);
    }

    // ④ Assemble, audit, clash check (Conclude), provenance per face.
    if (std::string Refusal = Conclude(Partition, Blueprint, "Reshape", Planned, Result, Histories, Audit); !Refusal.empty())
        return Refusal.rfind("Reshape", 0) == 0 ? Refusal : "Reshape: " + Refusal;
    int MovedCount = 0;
    for (char Each : Moved) MovedCount += Each;
    char Text[320];
    std::snprintf(Text, sizeof(Text),
                  "reshape: %d vertex(es) moved %.4g, %d edge(s) re-drawn, %d flat face(s) re-fitted, %d face(s) bent, %d facet(s) split (bow %.3g)",
                  MovedCount, Travel, ChangedEdges, PlanarFaces, BentFaces, SplitFacets, Bow);
    Method = Text;
    return {};
}

} // namespace Frontier::GeometrySpace::Parametric
