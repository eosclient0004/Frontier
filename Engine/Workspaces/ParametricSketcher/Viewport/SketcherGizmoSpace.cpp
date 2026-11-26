/*==========================================================================================================================================
                                                               GIZMOSPACE.CPP
==========================================================================================================================================*/
// 🧩 The gizmo's handle triangles (Slate GizmoPRO look), their pick and the drag maths.

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherGizmoSpace.h"
#include <numbers>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>

namespace Frontier
{

using GeometrySpace::Parametric::Cross;
using GeometrySpace::Parametric::Dot;
using GeometrySpace::Parametric::Length;
using GeometrySpace::Parametric::Normalize;
using GeometrySpace::Parametric::Vector3;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Unified with Frontier Editor's TransformGizmo (Slate GizmoPRO palette & proportions, L = 1).
constexpr uint32_t AxisColours[3]  = { 0xe01414u, 0x12d40au, 0x1560e0u };                    // [-]   - X red, Y green, Z blue (0xRRGGBB)
constexpr uint32_t PlaneColours[3] = { 0x1fc7c7u, 0xc81ec8u, 0xe0cd12u };                    // [-]   - YZ cyan, XZ magenta, XY yellow
constexpr uint32_t RingColour      = 0xffffffu;                                              // [-]   - Centre ring & axis stems
constexpr uint32_t GlowColour      = 0x555555u;                                              // [-]   - Hover emissive

constexpr double Tip          = 0.95;                                                        // [L]   - Cone centre along its axis
constexpr double ConeRadius   = 0.055;                                                       // [L]   - Cone base radius (matches TransformGizmo)
constexpr double ConeHeight   = 0.18;                                                        // [L]   - Cone height (centred on Tip)
constexpr double ScaleRadius  = 0.048;                                                       // [L]   - Scale cylinder radius (matches TransformGizmo)
constexpr double ScaleHeight  = 0.12;                                                        // [L]   - Scale cylinder length (matches TransformGizmo)
constexpr double ScaleCentre  = Tip - 0.28;                                                  // [L]   - Scale cylinder centre (0.67 L)
constexpr double StemRadius   = 0.0105;                                                      // [L]   - Pure white axis stem radius (matches TransformGizmo)
constexpr double RingOuterRim = 0.168;                                                       // [L]   - Outer rim of centre ring where axis stems start
constexpr double PlaneHalf    = 0.08;                                                        // [L]   - Plane quad half side
constexpr double PlaneCentre  = Tip - PlaneHalf;                                             // [L]   - Quad centre along u and v
constexpr double ArcRadius    = 0.62 * Tip;                                                  // [L]   - Rotate arc centre line
constexpr double ArcBand      = 0.034;                                                       // [L]   - Arc half width (matches TransformGizmo)
constexpr double DegreesToRadians(double Deg) noexcept { return Deg * (std::numbers::pi / 180.0); }
constexpr double ArcSweep     = DegreesToRadians(31.0);                                      // [rad] - Arc span
constexpr double RingRadius   = 0.16;                                                        // [L]   - Centre ring radius
constexpr double RingTube     = 0.008;                                                       // [L]   - Ring tube radius
constexpr double StrokePixels = 1.6;                                                         // [px]  - Quad edge stroke width
constexpr int    RoundSegments = 24;                                                         // [-]   - Cone / cylinder facets
constexpr int    ArcSegments   = 24;                                                         // [-]   - Arc pieces
constexpr int    RingSegments  = 64;                                                         // [-]   - Ring pieces

constexpr uint8_t QuadAlpha      = 71;                                                       // [-] - Opacity 0.28
constexpr uint8_t QuadHoverAlpha = 140;                                                      // [-] - Opacity 0.55 on hover
constexpr uint8_t SweepAlpha     = 76;                                                       // [-] - Opacity 0.30 (matches TransformGizmo sweep wedge)
constexpr uint8_t GuideLineAlpha = 153;                                                      // [-] - Opacity 0.60 (matches TransformGizmo travel guide)

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] Vector3 FromArray(const std::array<double, 3>& Values) { return { Values[0], Values[1], Values[2] }; }

// 0xRRGGBB (+ glow, clamped per channel) and alpha → RGBA8 with R in the low byte.
[[nodiscard]] uint32_t PackColour(uint32_t Rgb, bool GlowCondition, uint8_t Alpha = 255)
{
    uint32_t Channels[3] = { (Rgb >> 16) & 0xffu, (Rgb >> 8) & 0xffu, Rgb & 0xffu };
    if (GlowCondition)
        for (int Channel = 0; Channel < 3; ++Channel) Channels[Channel] = std::min(255u, Channels[Channel] + ((GlowColour >> (16 - 8 * Channel)) & 0xffu));
    return Channels[0] | (Channels[1] << 8) | (Channels[2] << 16) | (uint32_t(Alpha) << 24);
}

// Unit vector perpendicular to Direction (any).
[[nodiscard]] Vector3 Perpendicular(Vector3 Direction)
{
    const Vector3 ReferenceAxis = std::abs(Direction.X) < 0.9 ? Vector3{ 1.0, 0.0, 0.0 } : Vector3{ 0.0, 1.0, 0.0 };
    return Normalize(Cross(Direction, ReferenceAxis));
}

class GizmoBuilder
{
public:
    GizmoBuilder(const GizmoPose& Pose, std::vector<GizmoVertex>& Vertices) : Pose(Pose), Vertices(Vertices) {}

    // Gizmo units (L, along Pose.Axes) → world.
    [[nodiscard]] Vector3 Place(Vector3 Local) const
    {
        return Pose.Origin + (Pose.Axes[0] * Local.X + Pose.Axes[1] * Local.Y + Pose.Axes[2] * Local.Z) * Pose.Size;
    }
    [[nodiscard]] Vector3 Turn(Vector3 Local) const { return Pose.Axes[0] * Local.X + Pose.Axes[1] * Local.Y + Pose.Axes[2] * Local.Z; }

    void Triangle(Vector3 First, Vector3 Second, Vector3 Third, Vector3 Normal, uint32_t Colour, GizmoHandle Handle)
    {
        for (const Vector3& Corner : { First, Second, Third })
        {
            GizmoVertex Vertex;
            Vertex.Position[0] = float(Corner.X); Vertex.Position[1] = float(Corner.Y); Vertex.Position[2] = float(Corner.Z);
            Vertex.Normal[0]   = float(Normal.X); Vertex.Normal[1]   = float(Normal.Y); Vertex.Normal[2]   = float(Normal.Z);
            Vertex.Colour      = Colour;
            Vertex.Handle      = uint32_t(Handle);
            Vertices.push_back(Vertex);
        }
    }

    // A round solid along Axis (local unit vector): radius tapering from BaseRadius at Start to TopRadius at End.
    // Pass LitCondition = false for Frontier-style flat unlit white axis stems and drag travel guides.
    void Round(Vector3 Axis, double Start, double End, double BaseRadius, double TopRadius, uint32_t Colour, GizmoHandle Handle, bool LitCondition = true)
    {
        const Vector3 First  = Perpendicular(Axis);
        const Vector3 Second = Cross(Axis, First);
        const Vector3 Bottom = Place(Axis * Start), Top = Place(Axis * End);
        const Vector3 Up     = Turn(Axis);
        const double  Slope  = (BaseRadius - TopRadius) / (End - Start);                     // [-] - Side normal tilt
        for (int Index = 0; Index < RoundSegments; ++Index)
        {
            const double  Angle[2] = { 2.0 * std::numbers::pi * Index / RoundSegments, 2.0 * std::numbers::pi * (Index + 1) / RoundSegments };
            Vector3       Spoke[2], Low[2], High[2];
            for (int Side = 0; Side < 2; ++Side)
            {
                Spoke[Side] = First * std::cos(Angle[Side]) + Second * std::sin(Angle[Side]);
                Low[Side]   = Place(Axis * Start + Spoke[Side] * BaseRadius);
                High[Side]  = Place(Axis * End + Spoke[Side] * TopRadius);
            }
            const Vector3 Middle = LitCondition ? Normalize(Turn(Spoke[0] + Spoke[1]) + Up * Slope) : Vector3{};
            const Vector3 BotN   = LitCondition ? Up * -1.0 : Vector3{};
            const Vector3 TopN   = LitCondition ? Up : Vector3{};
            Triangle(Low[0], Low[1], High[1], Middle, Colour, Handle);
            if (TopRadius > 0.0) Triangle(Low[0], High[1], High[0], Middle, Colour, Handle);
            else                 Triangle(Low[0], High[1], Top, Middle, Colour, Handle);
            Triangle(Bottom, Low[1], Low[0], BotN, Colour, Handle);
            if (TopRadius > 0.0) Triangle(Top, High[0], High[1], TopN, Colour, Handle);
        }
    }

    // World-space unlit cylinder between WorldCentre - WorldAxis*(Len/2) and WorldCentre + WorldAxis*(Len/2).
    void WorldCylinder(Vector3 WorldCentre, Vector3 WorldAxis, double RadiusWorld, double LengthWorld, uint32_t Colour, GizmoHandle Handle)
    {
        const Vector3 Unit   = Normalize(WorldAxis);
        const Vector3 First  = Perpendicular(Unit);
        const Vector3 Second = Cross(Unit, First);
        const Vector3 Bottom = WorldCentre - Unit * (0.5 * LengthWorld);
        const Vector3 Top    = WorldCentre + Unit * (0.5 * LengthWorld);
        constexpr int Segs = 16;
        for (int Index = 0; Index < Segs; ++Index)
        {
            const double A0 = 2.0 * std::numbers::pi * Index / Segs;
            const double A1 = 2.0 * std::numbers::pi * (Index + 1) / Segs;
            const Vector3 R0 = (First * std::cos(A0) + Second * std::sin(A0)) * RadiusWorld;
            const Vector3 R1 = (First * std::cos(A1) + Second * std::sin(A1)) * RadiusWorld;
            Triangle(Bottom + R0, Bottom + R1, Top + R1, {}, Colour, Handle);
            Triangle(Bottom + R0, Top + R1, Top + R0, {}, Colour, Handle);
            Triangle(Bottom, Bottom + R1, Bottom + R0, {}, Colour, Handle);
            Triangle(Top, Top + R0, Top + R1, {}, Colour, Handle);
        }
    }

    // World-space unlit annular band in plane (WorldU, WorldV) centred at WorldPivot.
    void WorldBand(Vector3 WorldPivot, Vector3 WorldU, Vector3 WorldV, double InnerWorld, double OuterWorld,
                   double LowAngle, double HighAngle, int Segments, uint32_t Colour, GizmoHandle Handle)
    {
        for (int Index = 0; Index < Segments; ++Index)
        {
            const double A0 = LowAngle + (HighAngle - LowAngle) * Index / Segments;
            const double A1 = LowAngle + (HighAngle - LowAngle) * (Index + 1) / Segments;
            const Vector3 D0 = WorldU * std::cos(A0) + WorldV * std::sin(A0);
            const Vector3 D1 = WorldU * std::cos(A1) + WorldV * std::sin(A1);
            Triangle(WorldPivot + D0 * InnerWorld, WorldPivot + D0 * OuterWorld, WorldPivot + D1 * OuterWorld, {}, Colour, Handle);
            Triangle(WorldPivot + D0 * InnerWorld, WorldPivot + D1 * OuterWorld, WorldPivot + D1 * InnerWorld, {}, Colour, Handle);
        }
    }

    // Flat band (u, v local): radii Inner … Outer over angles Low … High, unlit.
    void Band(Vector3 U, Vector3 V, double Inner, double Outer, double Low, double High, int Segments, uint32_t Colour, GizmoHandle Handle)
    {
        for (int Index = 0; Index < Segments; ++Index)
        {
            const double First = Low + (High - Low) * Index / Segments, Second = Low + (High - Low) * (Index + 1) / Segments;
            const Vector3 InnerFirst  = Place((U * std::cos(First) + V * std::sin(First)) * Inner);
            const Vector3 OuterFirst  = Place((U * std::cos(First) + V * std::sin(First)) * Outer);
            const Vector3 InnerSecond = Place((U * std::cos(Second) + V * std::sin(Second)) * Inner);
            const Vector3 OuterSecond = Place((U * std::cos(Second) + V * std::sin(Second)) * Outer);
            Triangle(InnerFirst, OuterFirst, OuterSecond, {}, Colour, Handle);
            Triangle(InnerFirst, OuterSecond, InnerSecond, {}, Colour, Handle);
        }
    }

    // World stroke From → To of width Width (world), facing the eye (Toward = unit eye direction).
    void Stroke(Vector3 From, Vector3 To, double Width, Vector3 Toward, uint32_t Colour, GizmoHandle Handle)
    {
        Vector3 Side = Cross(To - From, Toward);
        if (Length(Side) < 1.0e-12) return;
        Side = Normalize(Side) * (0.5 * Width);
        Triangle(From - Side, From + Side, To + Side, {}, Colour, Handle);
        Triangle(From - Side, To + Side, To - Side, {}, Colour, Handle);
    }

    const GizmoPose&           Pose;
    std::vector<GizmoVertex>&  Vertices;
};

[[nodiscard]] bool HiddenCondition(const GizmoPose& Pose, GizmoHandle Handle)
{
    return (Pose.Hidden >> uint32_t(Handle)) & 1u;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

OrbitCameraBasis ResolveGizmoCamera(const std::array<double, 3>& Eye, const std::array<double, 3>& Forward, const std::array<double, 3>& Up,
                                   double VerticalFieldOfView, double Width, double Height)
{
    // 📝 Vulkan clip space as the Sketcher camera writes it: x right, y down, depth in [0, 1] between Near and Far.
    constexpr double Near = 0.05, Far = 1.0e5;                                               // [len]
    const Vector3 Ahead   = Normalize(FromArray(Forward));
    const Vector3 Across  = Normalize(Cross(Ahead, FromArray(Up)));
    const Vector3 Upward  = Cross(Across, Ahead);
    const Vector3 Station = FromArray(Eye);
    const double  Tangent = std::tan(0.5 * VerticalFieldOfView);
    const double  Aspect  = Height > 0.0 ? Width / Height : 1.0;
    const double  Depth   = Far / (Far - Near);
    const double  Rows[4][4] = {
        { Across.X / (Tangent * Aspect), Across.Y / (Tangent * Aspect), Across.Z / (Tangent * Aspect), -Dot(Across, Station) / (Tangent * Aspect) },
        { -Upward.X / Tangent, -Upward.Y / Tangent, -Upward.Z / Tangent, Dot(Upward, Station) / Tangent },
        { Ahead.X * Depth, Ahead.Y * Depth, Ahead.Z * Depth, -Dot(Ahead, Station) * Depth - Near * Depth },
        { Ahead.X, Ahead.Y, Ahead.Z, -Dot(Ahead, Station) },
    };
    OrbitCameraBasis Camera;
    for (int Row = 0; Row < 4; ++Row)
        for (int Column = 0; Column < 4; ++Column) Camera.ClipFromWorld[Column * 4 + Row] = Rows[Row][Column];
    Camera.Eye           = Eye;
    Camera.Right         = { Across.X, Across.Y, Across.Z };
    Camera.Up            = { Upward.X, Upward.Y, Upward.Z };
    Camera.Forward       = { Ahead.X, Ahead.Y, Ahead.Z };
    Camera.WorldPerPixel = 2.0 * Tangent / std::max(1.0, Height);                            // [len] - At unit depth
    Camera.Width         = Width;
    Camera.Height        = Height;
    return Camera;
}

double MeasureGizmoSize(const OrbitCameraBasis& Frame, Vector3 Origin, double Pixels)
{
    // Project Origin and Origin + Right: their pixel distance is the screen scale at Origin's depth.
    const auto Project = [&](Vector3 Point, double& X, double& Y)
    {
        double Clip[4] = {};
        for (int Row = 0; Row < 4; ++Row)
            Clip[Row] = Frame.ClipFromWorld[0 * 4 + Row] * Point.X + Frame.ClipFromWorld[1 * 4 + Row] * Point.Y + Frame.ClipFromWorld[2 * 4 + Row] * Point.Z + Frame.ClipFromWorld[3 * 4 + Row];
        if (Clip[3] <= 1.0e-12) return false;
        X = (Clip[0] / Clip[3] * 0.5 + 0.5) * Frame.Width;
        Y = (Clip[1] / Clip[3] * 0.5 + 0.5) * Frame.Height;
        return true;
    };
    double FirstX = 0.0, FirstY = 0.0, SecondX = 0.0, SecondY = 0.0;
    if (!Project(Origin, FirstX, FirstY) || !Project(Origin + FromArray(Frame.Right), SecondX, SecondY)) return Pixels * Frame.WorldPerPixel;
    const double PerUnit = std::hypot(SecondX - FirstX, SecondY - FirstY);                   // [px/len]
    return PerUnit > 1.0e-9 ? Pixels / PerUnit : Pixels * Frame.WorldPerPixel;
}

namespace
{
    std::atomic<uint64_t> GlobalGizmoCacheHits     { 0ull };
    std::atomic<uint64_t> GlobalGizmoCacheRebuilds { 0ull };

    [[nodiscard]] uint64_t HashGizmoRequest(const GizmoPose& Pose, const OrbitCameraBasis& Frame, GizmoHandle Hover, GizmoHandle Grabbed) noexcept
    {
        auto Mix = [](uint64_t H, uint64_t V) noexcept -> uint64_t
        {
            H ^= V;
            H *= 1099511628211ull;
            return H;
        };
        auto Bits = [](double V) noexcept -> uint64_t
        {
            if (V == 0.0) V = 0.0; // normalize -0.0
            uint64_t Raw = 0ull;
            std::memcpy(&Raw, &V, sizeof(Raw));
            return Raw;
        };
        uint64_t H = 14695981039346656037ull;
        H = Mix(H, Bits(Pose.Origin.X));
        H = Mix(H, Bits(Pose.Origin.Y));
        H = Mix(H, Bits(Pose.Origin.Z));
        for (int A = 0; A < 3; ++A)
        {
            H = Mix(H, Bits(Pose.Axes[A].X));
            H = Mix(H, Bits(Pose.Axes[A].Y));
            H = Mix(H, Bits(Pose.Axes[A].Z));
        }
        H = Mix(H, Bits(Pose.Size));
        H = Mix(H, static_cast<uint64_t>(Pose.Modes));
        H = Mix(H, static_cast<uint64_t>(Pose.Hidden));
        for (int A = 0; A < 3; ++A)
        {
            H = Mix(H, Bits(Frame.Right[A]));
            H = Mix(H, Bits(Frame.Up[A]));
            H = Mix(H, Bits(Frame.Forward[A]));
        }
        H = Mix(H, static_cast<uint64_t>(Hover));
        H = Mix(H, static_cast<uint64_t>(Grabbed));
        H = Mix(H, Pose.Guide.Active ? 1ull : 0ull);
        if (Pose.Guide.Active)
        {
            H = Mix(H, Bits(Pose.Guide.StartOrigin.X));
            H = Mix(H, Bits(Pose.Guide.StartOrigin.Y));
            H = Mix(H, Bits(Pose.Guide.StartOrigin.Z));
            H = Mix(H, Bits(Pose.Guide.StartAngleRad));
            H = Mix(H, Bits(Pose.Guide.DeltaAngleRad));
        }
        return H == 0ull ? 1ull : H;
    }
} // namespace

void QueryGizmoGeometryCacheStats(uint64_t& OutHits, uint64_t& OutRebuilds) noexcept
{
    OutHits     = GlobalGizmoCacheHits.load(std::memory_order_relaxed);
    OutRebuilds = GlobalGizmoCacheRebuilds.load(std::memory_order_relaxed);
}

GizmoGeometry BuildGizmoGeometry(const GizmoPose& Pose, const OrbitCameraBasis& Frame, GizmoHandle Hover, GizmoHandle Grabbed)
{
    GizmoGeometry Geometry;
    if (Pose.Modes == 0) return Geometry;

    // 4-slot thread-local LRU cache covers both Single and Quad split-view panes with zero per-frame trig/realloc.
    struct CacheSlot
    {
        uint64_t      Key   = 0ull;
        uint64_t      Stamp = 0ull;
        GizmoGeometry Value {};
    };
    thread_local std::array<CacheSlot, 4u> Cache {};
    thread_local uint64_t                  StampCounter = 0ull;
    const uint64_t Key = HashGizmoRequest(Pose, Frame, Hover, Grabbed);
    size_t Victim = 0u;
    uint64_t Oldest = UINT64_MAX;
    for (size_t Slot = 0u; Slot < Cache.size(); ++Slot)
    {
        if (Cache[Slot].Key == Key)
        {
            Cache[Slot].Stamp = ++StampCounter;
            GlobalGizmoCacheHits.fetch_add(1ull, std::memory_order_relaxed);
            return Cache[Slot].Value;
        }
        if (Cache[Slot].Stamp < Oldest)
        {
            Oldest = Cache[Slot].Stamp;
            Victim = Slot;
        }
    }

    GlobalGizmoCacheRebuilds.fetch_add(1ull, std::memory_order_relaxed);
    Geometry.Vertices.reserve(4096u);
    GizmoBuilder  Builder(Pose, Geometry.Vertices);
    const Vector3 Units[3] = { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
    const Vector3 Right    = FromArray(Frame.Right);
    const Vector3 Up       = FromArray(Frame.Up);
    const Vector3 Toward   = FromArray(Frame.Forward) * -1.0;                                 // [-] - Towards the eye
    const auto Glow = [&](GizmoHandle Handle) { return Handle == Hover || Handle == Grabbed; };
    const auto Shown = [&](GizmoHandle Handle) { return !HiddenCondition(Pose, Handle); };

    // ① Frontier Editor unified opaque handles: pure white axis stems + cones + scale cylinders + rotate arcs + quad edges + centre ring
    const bool EmitTranslate = (Pose.Modes & GizmoTranslate) != 0u;
    const bool EmitScale     = (Pose.Modes & GizmoScale) != 0u;
    const bool EmitRotate    = (Pose.Modes & GizmoRotate) != 0u;

    for (int Axis = 0; Axis < 3; ++Axis)
    {
        const Vector3 OtherU = Axis == 0 ? Units[1] : Units[0];                              // [-] - Slate OTHERS[axis].u
        const Vector3 OtherV = Axis == 2 ? Units[1] : Units[2];                              // [-] - Slate OTHERS[axis].v
        const auto Translate = GizmoHandle(uint8_t(GizmoHandle::TranslateX) + Axis);
        const auto Rotate    = GizmoHandle(uint8_t(GizmoHandle::RotateX) + Axis);
        const auto Scale     = GizmoHandle(uint8_t(GizmoHandle::ScaleX) + Axis);
        const auto Plane     = GizmoHandle(uint8_t(GizmoHandle::PlaneYZ) + Axis);

        const bool ShowT = EmitTranslate && Shown(Translate);
        const bool ShowS = EmitScale && Shown(Scale);

        // Pure white axis stem matching Frontier's TransformGizmo: starts strictly at the centre ring's outer rim
        // (never entering the circle) and stops cleanly before the scale collar (or cone base when translate-only).
        if (ShowT || ShowS)
        {
            const Vector3 WorldAxis  = Builder.Turn(Units[Axis]);
            const double  ProjX      = Dot(WorldAxis, Right);
            const double  ProjY      = Dot(WorldAxis, Up);
            const double  ScreenProj = std::hypot(ProjX, ProjY);
            const double  StemFinish = ShowS ? (ScaleCentre - 0.062) : (Tip - 0.092);
            const double  RimStart   = (ScreenProj > 0.20) ? (RingOuterRim / ScreenProj) : StemFinish;
            if (StemFinish > RimStart + 0.015)
            {
                const GizmoHandle StemOwner = ShowT ? Translate : Scale;
                Builder.Round(Units[Axis], RimStart, StemFinish, StemRadius, StemRadius, PackColour(RingColour, false), StemOwner, false);
            }
        }

        if (ShowT)
            Builder.Round(Units[Axis], Tip - 0.5 * ConeHeight, Tip + 0.5 * ConeHeight, ConeRadius, 0.0, PackColour(AxisColours[Axis], Glow(Translate)), Translate);
        if (ShowS)
            Builder.Round(Units[Axis], ScaleCentre - 0.5 * ScaleHeight, ScaleCentre + 0.5 * ScaleHeight, ScaleRadius, ScaleRadius, PackColour(AxisColours[Axis], Glow(Scale)), Scale);
        if (EmitRotate && Shown(Rotate))
        {
            const double Centre = 0.25 * std::numbers::pi;                                   // [rad] - Bisector of u and v
            Builder.Band(OtherU, OtherV, ArcRadius - ArcBand, ArcRadius + ArcBand, Centre - 0.5 * ArcSweep, Centre + 0.5 * ArcSweep, ArcSegments,
                         PackColour(AxisColours[Axis], Glow(Rotate)), Rotate);
        }
        if (EmitTranslate && Shown(Plane))
        {
            const double  Width = StrokePixels / GizmoPixels * Pose.Size;                    // [len]
            const Vector3 Outer = Builder.Place((OtherU + OtherV) * (PlaneCentre + PlaneHalf));
            Builder.Stroke(Builder.Place(OtherU * (PlaneCentre - PlaneHalf) + OtherV * (PlaneCentre + PlaneHalf)), Outer, Width, Toward, PackColour(PlaneColours[Axis], Glow(Plane)), Plane);
            Builder.Stroke(Builder.Place(OtherV * (PlaneCentre - PlaneHalf) + OtherU * (PlaneCentre + PlaneHalf)), Outer, Width, Toward, PackColour(PlaneColours[Axis], Glow(Plane)), Plane);
        }
    }
    // Centre ring: a band facing the eye (Slate billboards a thin torus).
    if (Shown(GizmoHandle::Centre))
    {
        const Vector3 LocalRight{ Dot(Right, Pose.Axes[0]), Dot(Right, Pose.Axes[1]), Dot(Right, Pose.Axes[2]) };
        const Vector3 LocalUp{ Dot(Up, Pose.Axes[0]), Dot(Up, Pose.Axes[1]), Dot(Up, Pose.Axes[2]) };
        const double  Tube = std::max(RingTube, 0.75 * StrokePixels / GizmoPixels);          // [L] - At least a visible stroke
        Builder.Band(LocalRight, LocalUp, RingRadius - Tube, RingRadius + Tube, 0.0, 2.0 * std::numbers::pi, RingSegments,
                     PackColour(RingColour, Glow(GizmoHandle::Centre)), GizmoHandle::Centre);
    }
    Geometry.OpaqueCount = static_cast<uint32_t>(Geometry.Vertices.size());

    // ② translucent plane quads (drawn last, blended)
    if (EmitTranslate)
    {
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            const auto Plane = GizmoHandle(uint8_t(GizmoHandle::PlaneYZ) + Axis);
            if (!Shown(Plane)) continue;
            const Vector3 OtherU = Axis == 0 ? Units[1] : Units[0];
            const Vector3 OtherV = Axis == 2 ? Units[1] : Units[2];
            const Vector3 Centre = (OtherU + OtherV) * PlaneCentre;
            const Vector3 Corners[4] = { Builder.Place(Centre - OtherU * PlaneHalf - OtherV * PlaneHalf), Builder.Place(Centre + OtherU * PlaneHalf - OtherV * PlaneHalf),
                                         Builder.Place(Centre + OtherU * PlaneHalf + OtherV * PlaneHalf), Builder.Place(Centre - OtherU * PlaneHalf + OtherV * PlaneHalf) };
            const uint32_t Colour = PackColour(PlaneColours[Axis], false, Glow(Plane) ? QuadHoverAlpha : QuadAlpha);
            Builder.Triangle(Corners[0], Corners[1], Corners[2], {}, Colour, Plane);
            Builder.Triangle(Corners[0], Corners[2], Corners[3], {}, Colour, Plane);
        }
    }

    // Record pickable range & tight 3D world AABB before appending visual-only Frontier drag guides.
    Geometry.PickableCount = static_cast<uint32_t>(Geometry.Vertices.size());
    for (uint32_t I = 0u; I < Geometry.PickableCount; ++I)
    {
        const float* P = Geometry.Vertices[I].Position;
        for (int A = 0; A < 3; ++A)
        {
            Geometry.BoundsLow[A]  = std::min(Geometry.BoundsLow[A],  double(P[A]));
            Geometry.BoundsHigh[A] = std::max(Geometry.BoundsHigh[A], double(P[A]));
        }
    }

    // ③ Frontier active-drag visual guides (360° rotation circle + swept wedge + spokes, or extended axis travel line + origin tick)
    if (Pose.Guide.Active && Grabbed != GizmoHandle::None)
    {
        const int DragAxis = GizmoHandleAxis(Grabbed);
        if (DragAxis >= 0 && DragAxis < 3)
        {
            const double L = Pose.Size;
            const Vector3 StartOrigin = Pose.Guide.StartOrigin;
            if (Grabbed == GizmoHandle::RotateX || Grabbed == GizmoHandle::RotateY || Grabbed == GizmoHandle::RotateZ)
            {
                const Vector3 OtherU = DragAxis == 0 ? Units[1] : Units[0];
                const Vector3 OtherV = DragAxis == 2 ? Units[1] : Units[2];
                const Vector3 WorldU = Normalize(Builder.Turn(OtherU));
                const Vector3 WorldV = Normalize(Builder.Turn(OtherV));
                const double  RingR  = ArcRadius * L;
                const double  HalfB  = 0.015 * L;
                const double  InnerR = std::max(0.01 * L, RingR - HalfB);
                const double  OuterR = RingR + HalfB;
                const double  WedgeR = 0.175 * L;
                const uint32_t SolidCol = PackColour(AxisColours[DragAxis], false, 255);
                const uint32_t WedgeCol = PackColour(AxisColours[DragAxis], false, SweepAlpha);

                Builder.WorldBand(StartOrigin, WorldU, WorldV, InnerR, OuterR, 0.0, 2.0 * std::numbers::pi, 64, SolidCol, Grabbed);
                const double AbsRad = std::abs(Pose.Guide.DeltaAngleRad);
                if (AbsRad > 0.005)
                {
                    const int SweepSegs = std::clamp(int(std::ceil(AbsRad / DegreesToRadians(5.0))), 2, 64);
                    Builder.WorldBand(StartOrigin, WorldU, WorldV, WedgeR, InnerR,
                                      Pose.Guide.StartAngleRad, Pose.Guide.StartAngleRad + Pose.Guide.DeltaAngleRad,
                                      SweepSegs, WedgeCol, Grabbed);
                }
                const auto AddSpoke = [&](double AngleRad, uint32_t SpokeCol)
                {
                    const Vector3 Dir = WorldU * std::cos(AngleRad) + WorldV * std::sin(AngleRad);
                    const double SpokeLen = std::max(0.01 * L, InnerR - WedgeR);
                    const double SpokeMid = 0.5 * (WedgeR + InnerR);
                    Builder.WorldCylinder(StartOrigin + Dir * SpokeMid, Dir, HalfB * 0.55, SpokeLen, SpokeCol, Grabbed);
                };
                AddSpoke(Pose.Guide.StartAngleRad, PackColour(RingColour, false, 255));
                AddSpoke(Pose.Guide.StartAngleRad + Pose.Guide.DeltaAngleRad, SolidCol);
            }
            else if (Grabbed == GizmoHandle::TranslateX || Grabbed == GizmoHandle::TranslateY || Grabbed == GizmoHandle::TranslateZ ||
                     Grabbed == GizmoHandle::ScaleX     || Grabbed == GizmoHandle::ScaleY     || Grabbed == GizmoHandle::ScaleZ)
            {
                const Vector3 WorldAxis = Normalize(Builder.Turn(Units[DragAxis]));
                Builder.WorldCylinder(StartOrigin, WorldAxis, 0.0060 * L, 32.0 * L, PackColour(AxisColours[DragAxis], false, GuideLineAlpha), Grabbed);
                Builder.WorldCylinder(StartOrigin, WorldAxis, 0.026 * L,  0.014 * L, PackColour(RingColour, false, 255), Grabbed);
            }
        }
    }

    Cache[Victim].Key   = Key;
    Cache[Victim].Stamp = ++StampCounter;
    Cache[Victim].Value = Geometry;
    return Geometry;
}

GizmoHandle PickGizmoHandle(const GizmoGeometry& Geometry, const OrbitCameraBasis& Frame, double PixelX, double PixelY, double Reach)
{
    const size_t PickVerts = Geometry.PickableCount > 0u
        ? std::min<size_t>(Geometry.PickableCount, Geometry.Vertices.size())
        : Geometry.Vertices.size();
    if (PickVerts < 3u) return GizmoHandle::None;

    struct Projected { double X = 0.0, Y = 0.0, Depth = 0.0; bool Valid = false; };
    const auto ProjectPoint = [&](double Px, double Py, double Pz)
    {
        Projected Point;
        double Clip[4] = {};
        for (int Row = 0; Row < 4; ++Row)
            Clip[Row] = Frame.ClipFromWorld[0 * 4 + Row] * Px + Frame.ClipFromWorld[1 * 4 + Row] * Py
                      + Frame.ClipFromWorld[2 * 4 + Row] * Pz + Frame.ClipFromWorld[3 * 4 + Row];
        if (Clip[3] <= 1.0e-12) return Point;
        Point.X = (Clip[0] / Clip[3] * 0.5 + 0.5) * Frame.Width;
        Point.Y = (Clip[1] / Clip[3] * 0.5 + 0.5) * Frame.Height;
        Point.Depth = Clip[2] / Clip[3];
        Point.Valid = true;
        return Point;
    };

    // O(1) 8-corner world-AABB projected screen reject: skips all ~3000+ triangles when cursor is outside the gizmo!
    if (Geometry.BoundsLow[0] <= Geometry.BoundsHigh[0])
    {
        double BoxMinX = 1e30, BoxMaxX = -1e30, BoxMinY = 1e30, BoxMaxY = -1e30;
        int ValidCorners = 0;
        for (int CornerIdx = 0; CornerIdx < 8; ++CornerIdx)
        {
            const double X = (CornerIdx & 1) ? Geometry.BoundsHigh[0] : Geometry.BoundsLow[0];
            const double Y = (CornerIdx & 2) ? Geometry.BoundsHigh[1] : Geometry.BoundsLow[1];
            const double Z = (CornerIdx & 4) ? Geometry.BoundsHigh[2] : Geometry.BoundsLow[2];
            const Projected P = ProjectPoint(X, Y, Z);
            if (!P.Valid) { ValidCorners = -1; break; }
            BoxMinX = std::min(BoxMinX, P.X);
            BoxMaxX = std::max(BoxMaxX, P.X);
            BoxMinY = std::min(BoxMinY, P.Y);
            BoxMaxY = std::max(BoxMaxY, P.Y);
            ++ValidCorners;
        }
        if (ValidCorners == 8 && (PixelX < BoxMinX - Reach || PixelX > BoxMaxX + Reach || PixelY < BoxMinY - Reach || PixelY > BoxMaxY + Reach))
            return GizmoHandle::None;
    }

    const auto Project = [&](const GizmoVertex& Vertex)
    {
        return ProjectPoint(Vertex.Position[0], Vertex.Position[1], Vertex.Position[2]);
    };
    // Segment distance in pixels.
    const auto Gap = [&](const Projected& From, const Projected& To)
    {
        const double AlongX = To.X - From.X, AlongY = To.Y - From.Y, Square = AlongX * AlongX + AlongY * AlongY;
        const double Share  = Square > 0.0 ? std::clamp(((PixelX - From.X) * AlongX + (PixelY - From.Y) * AlongY) / Square, 0.0, 1.0) : 0.0;
        return std::hypot(From.X + AlongX * Share - PixelX, From.Y + AlongY * Share - PixelY);
    };
    GizmoHandle Best            = GizmoHandle::None;
    double              BestGap         = Reach;
    bool                BestTranslucent = true;
    double              BestDepth       = 1e300;
    for (size_t Index = 0; Index + 2 < PickVerts; Index += 3)
    {
        const Projected Corner[3] = { Project(Geometry.Vertices[Index]), Project(Geometry.Vertices[Index + 1]), Project(Geometry.Vertices[Index + 2]) };
        if (!Corner[0].Valid || !Corner[1].Valid || !Corner[2].Valid) continue;
        // Fast 2D screen-space AABB reject skips barycentric + 3x std::hypot for >95% of triangles.
        const double MinX = std::min({ Corner[0].X, Corner[1].X, Corner[2].X });
        const double MaxX = std::max({ Corner[0].X, Corner[1].X, Corner[2].X });
        if (PixelX < MinX - BestGap || PixelX > MaxX + BestGap) continue;
        const double MinY = std::min({ Corner[0].Y, Corner[1].Y, Corner[2].Y });
        const double MaxY = std::max({ Corner[0].Y, Corner[1].Y, Corner[2].Y });
        if (PixelY < MinY - BestGap || PixelY > MaxY + BestGap) continue;
        // Barycentric inside test (either winding)
        const double Area = (Corner[1].X - Corner[0].X) * (Corner[2].Y - Corner[0].Y) - (Corner[2].X - Corner[0].X) * (Corner[1].Y - Corner[0].Y);
        double Distance = 0.0, Depth = (Corner[0].Depth + Corner[1].Depth + Corner[2].Depth) / 3.0;
        if (std::abs(Area) > 1.0e-12)
        {
            const double First  = ((Corner[1].X - PixelX) * (Corner[2].Y - PixelY) - (Corner[2].X - PixelX) * (Corner[1].Y - PixelY)) / Area;
            const double Second = ((Corner[2].X - PixelX) * (Corner[0].Y - PixelY) - (Corner[0].X - PixelX) * (Corner[2].Y - PixelY)) / Area;
            const double Third  = 1.0 - First - Second;
            if (First >= 0.0 && Second >= 0.0 && Third >= 0.0) Depth = First * Corner[0].Depth + Second * Corner[1].Depth + Third * Corner[2].Depth;
            else Distance = std::min({ Gap(Corner[0], Corner[1]), Gap(Corner[1], Corner[2]), Gap(Corner[2], Corner[0]) });
        }
        else Distance = std::min({ Gap(Corner[0], Corner[1]), Gap(Corner[1], Corner[2]), Gap(Corner[2], Corner[0]) });
        if (Distance > BestGap) continue;
        // 📝 Ranking: inside beats near (smaller pixel gap first); then an opaque handle beats a see-through plane quad
        //    (the cone seen through a quad stays grabbable); then the nearest depth.
        const bool Translucent = Index >= Geometry.OpaqueCount;
        const bool Better = Distance < BestGap || (Distance == BestGap && ((!Translucent && BestTranslucent) || (Translucent == BestTranslucent && Depth < BestDepth)));
        if (Better || Best == GizmoHandle::None)
        {
            BestGap         = Distance;
            BestTranslucent = Translucent;
            BestDepth       = Depth;
            Best            = GizmoHandle(Geometry.Vertices[Index].Handle);
        }
    }
    return Best;
}

int GizmoHandleAxis(GizmoHandle Handle) noexcept
{
    const int Code = int(Handle);
    if (Code >= 1 && Code <= 12) return (Code - 1) % 3;
    return -1;
}

const char* GizmoHandleLabel(GizmoHandle Handle) noexcept
{
    switch (Handle)
    {
    case GizmoHandle::None:       return "none";
    case GizmoHandle::TranslateX: return "X move";
    case GizmoHandle::TranslateY: return "Y move";
    case GizmoHandle::TranslateZ: return "Z move";
    case GizmoHandle::PlaneYZ:    return "X-plane move";
    case GizmoHandle::PlaneXZ:    return "Y-plane move";
    case GizmoHandle::PlaneXY:    return "Z-plane move";
    case GizmoHandle::RotateX:    return "X rotate";
    case GizmoHandle::RotateY:    return "Y rotate";
    case GizmoHandle::RotateZ:    return "Z rotate";
    case GizmoHandle::ScaleX:     return "X scale";
    case GizmoHandle::ScaleY:     return "Y scale";
    case GizmoHandle::ScaleZ:     return "Z scale";
    case GizmoHandle::Centre:     return "centre";
    }
    return "?";
}

std::optional<double> MeasureAxisParameter(const OrbitRay& Ray, Vector3 Origin, Vector3 Axis)
{
    // Closest points of the skew lines R(s) = O + s·d and A(t) = P + t·a: solve the 2 × 2 normal equations for t.
    const Vector3 RayOrigin = FromArray(Ray.Origin), Direction = FromArray(Ray.Direction);
    const Vector3 Unit      = Normalize(Axis);
    const Vector3 Between   = Origin - RayOrigin;
    const double  Facing    = Dot(Direction, Unit);
    const double  Determinant = 1.0 - Facing * Facing;
    if (Determinant < 1.0e-9) return std::nullopt;                                           // Ray along the axis
    const double Along = (Facing * Dot(Between, Direction) - Dot(Between, Unit)) / Determinant;
    return Along;
}

std::optional<Vector3> IntersectGizmoPlane(const OrbitRay& Ray, Vector3 Origin, Vector3 Normal)
{
    const Vector3 RayOrigin = FromArray(Ray.Origin), Direction = FromArray(Ray.Direction);
    const double  Facing    = Dot(Direction, Normal);
    if (std::abs(Facing) < 1.0e-9 * std::max(1.0, Length(Normal))) return std::nullopt;
    const double Distance = Dot(Origin - RayOrigin, Normal) / Facing;
    if (!(Distance > 0.0)) return std::nullopt;
    return RayOrigin + Direction * Distance;
}

std::optional<double> MeasureGizmoAngle(const OrbitRay& Ray, Vector3 Origin, Vector3 Axis, Vector3 Reference)
{
    const Vector3 Unit = Normalize(Axis);
    const std::optional<Vector3> Hit = IntersectGizmoPlane(Ray, Origin, Unit);
    if (!Hit) return std::nullopt;
    const Vector3 U = Normalize(Reference - Unit * Dot(Reference, Unit));
    const Vector3 V = Cross(Unit, U);
    const Vector3 Offset = *Hit - Origin;
    if (Length(Offset) < 1.0e-12) return std::nullopt;
    return std::atan2(Dot(Offset, V), Dot(Offset, U));
}

} // namespace Frontier
