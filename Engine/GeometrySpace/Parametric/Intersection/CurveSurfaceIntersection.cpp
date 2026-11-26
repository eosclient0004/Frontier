/*====================================================================================================================================
                                                 CURVESURFACEINTERSECTION.CPP
====================================================================================================================================*/
// 🧩 Bézier clipping candidates, signed-distance Newton polishing, tolerant tangency merge

#include "Engine/GeometrySpace/Parametric/Intersection/CurveSurfaceIntersection.h"
#include "Engine/GeometrySpace/Parametric/Intersection/ImplicitSurface.h"
#include "Engine/GeometrySpace/Parametric/Intersection/IntersectionCurve.h"
#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/RollingBallCarrier.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr int    NewtonLimit        = 40;                                                // [-] - Polishing iterations
    constexpr double TangentSine        = 1.0e-7;                                            // [-] - |N·Ĉ′| below this is a touch
    constexpr double CoincidentFraction = 1.0e-13;                                           // [-] - Coefficients ≤ this × L^d vanish

    struct DistanceQuery
    {
        double Distance = 0.0;                                                               // [len] - d(τ)
        double Slope    = 0.0;                                                               // [len] - d′(τ) = N·C′(τ)
        double Speed    = 0.0;                                                               // [len] - |C′(τ)|
    };

    [[nodiscard]] DistanceQuery ProbeDistance(const CurveGeometry& Curve, const SurfaceGeometry& Surface, double τ)
    {
        const CurveSample     Sample  = EvaluateCurve(Curve, τ);
        const SurfaceDistance Measure = MeasureSurfaceDistance(Surface, Sample.Position);
        return { Measure.Distance, Dot(Measure.Normal, Sample.Derivative), Length(Sample.Derivative) };
    }

    [[nodiscard]] DistanceQuery ProbeDistanceNear(const CurveGeometry& Curve, const SurfaceGeometry& Surface, double τ, Vector2& Hint)
    {
        const CurveSample     Sample  = EvaluateCurve(Curve, τ);
        const SurfaceDistance Measure = MeasureSurfaceDistanceNear(Surface, Sample.Position, Hint);
        return { Measure.Distance, Dot(Measure.Normal, Sample.Derivative), Length(Sample.Derivative) };
    }

    // Newton on d(τ) = 0 — quadratic at transversal roots. Moves are confined to [Low, High].
    [[nodiscard]] double PolishTransversal(const CurveGeometry& Curve, const SurfaceGeometry& Surface, double τ, double Low, double High, Vector2& Hint)
    {
        for (int Iteration = 0; Iteration < NewtonLimit; ++Iteration)
        {
            const DistanceQuery Probe = ProbeDistanceNear(Curve, Surface, τ, Hint);
            if (std::fabs(Probe.Slope) <= 1.0e-14 * Probe.Speed) break;
            const double Step = Probe.Distance / Probe.Slope;                                // [-] - Newton correction
            if (!std::isfinite(Step)) break;                                                 // Probe left the carrier's range
            const double Previous = τ;                                                       // [-] - τ before the move
            τ = std::clamp(τ - Step, Low, High);
            // 💡 Pinned at a bound: every later iteration repeats this one exactly (deterministic probes), so stopping
            //    changes no bit — it saves the remaining NewtonLimit probes (a clipped edge ending on the face).
            if (τ == Previous) break;
            if (std::fabs(Step) <= 1.0e-16 * (1.0 + std::fabs(τ))) break;
        }
        return τ;
    }

    // Newton on d′(τ) = 0 (central-difference d″): the extremum of the distance, where a touch lives.
    [[nodiscard]] double PolishExtremum(const CurveGeometry& Curve, const SurfaceGeometry& Surface, double τ, double Low, double High, Vector2& Hint)
    {
        const double Increment = 1.0e-6 * std::max(1.0, High - Low);                         // [-] - Difference step
        for (int Iteration = 0; Iteration < NewtonLimit; ++Iteration)
        {
            const double Slope     = ProbeDistanceNear(Curve, Surface, τ, Hint).Slope;
            Vector2      PlusHint  = Hint;
            Vector2      MinusHint = Hint;
            const double Curvature = (ProbeDistanceNear(Curve, Surface, τ + Increment, PlusHint).Slope - ProbeDistanceNear(Curve, Surface, τ - Increment, MinusHint).Slope) / (2.0 * Increment);
            if (std::fabs(Curvature) <= 1.0e-300) break;
            const double Step = Slope / Curvature;                                           // [-] - Newton correction
            if (!std::isfinite(Step)) break;                                                 // A difference probe left the range
            const double Previous = τ;                                                       // [-] - τ before the move
            τ = std::clamp(τ - Step, Low, High);
            if (τ == Previous) break;                                                        // Pinned: see PolishTransversal
            if (std::fabs(Step) <= 1.0e-15 * (1.0 + std::fabs(τ))) break;
        }
        return τ;
    }

    // Curve parameter of a point produced by a rational segment (arcs fold into the segment's own interval).
    [[nodiscard]] double SegmentParameter(const CurveGeometry& Curve, const RationalSegment& Segment, double 𝑡)
    {
        // Lines and Bézier pieces of a NURBS curve are linear in 𝑡 (extraction keeps the parameterisation).
        if (std::holds_alternative<StraightLine>(Curve) || std::holds_alternative<NurbsCurve>(Curve)) return Segment.CurveLow + 𝑡 * (Segment.CurveHigh - Segment.CurveLow);
        return ProjectOntoCurve(Curve, EvaluateRationalSegment(Segment, 𝑡), Segment.CurveLow - 1.0e-9);
    }

    struct Candidate
    {
        double  τ              = 0.0;                                                        // [-] - Curve parameter
        bool    TouchCondition = false;                                                      // [-] - From a root of F′
        Vector2 SurfaceHint    = { NAN, NAN };                                               // [-] - Warm chart parameter
    };

    void CollectScannedCandidates(const CurveGeometry& Curve, double Low, double High, int Count, double Chord,
                                  const SurfaceGeometry& Surface, const CurveSurfaceSettings& Settings,
                                  std::vector<CoincidentInterval>& Coincidences, std::vector<Candidate>& Candidates)
    {
        std::vector<double>  SignedDistances(Count + 1);
        std::vector<Vector3> SamplePoints(Count + 1);
        std::vector<Vector2> SurfaceHints(Count + 1, Vector2{ NAN, NAN });
        Vector2 ScanHint = { NAN, NAN };
        for (int Index = 0; Index <= Count; ++Index)
        {
            if ((Index & 7) == 0) ScanHint = { NAN, NAN };
            const double τ        = Low + (High - Low) * Index / Count;
            SamplePoints[Index]   = EvaluateCurve(Curve, τ).Position;
            SignedDistances[Index] = MeasureSurfaceDistanceNear(Surface, SamplePoints[Index], ScanHint).Distance;
            SurfaceHints[Index]   = ScanHint;
        }

        const double CoincidentThreshold = std::min(Settings.Precision, 1.0e-10 * std::max(1.0, Chord));
        for (int Index = 0; Index <= Count; ++Index)
        {
            const double τ = Low + (High - Low) * Index / Count;
            if (Index < Count && SignedDistances[Index] * SignedDistances[Index + 1] < 0.0)
            {
                Candidates.push_back({ τ + 0.5 * (High - Low) / Count, false, SurfaceHints[Index] });
            }
        }

        for (int Index = 0; Index <= Count; ++Index)
        {
            if (std::fabs(SignedDistances[Index]) <= CoincidentThreshold)
            {
                int RunEnd = Index;
                while (RunEnd < Count && std::fabs(SignedDistances[RunEnd + 1]) <= CoincidentThreshold) ++RunEnd;
                if (RunEnd - Index >= 4)
                {
                    const double StartTau = Low + (High - Low) * Index / Count;
                    const double EndTau   = Low + (High - Low) * RunEnd / Count;
                    bool VerifiedCondition = true;
                    Vector2 MidHint = SurfaceHints[(Index + RunEnd) / 2];
                    for (int Check = 1; Check <= 3 && VerifiedCondition; ++Check)
                    {
                        const double MidTau = StartTau + (EndTau - StartTau) * (0.25 * Check);
                        const double Gap    = std::fabs(MeasureSurfaceDistanceNear(Surface, EvaluateCurve(Curve, MidTau).Position, MidHint).Distance);
                        VerifiedCondition   = Gap <= Settings.Precision;
                    }
                    if (VerifiedCondition)
                    {
                        if (!Coincidences.empty() && std::fabs(Coincidences.back().High - StartTau) <= 1.0e-12) Coincidences.back().High = EndTau;
                        else Coincidences.push_back({ StartTau, EndTau });
                        Index = RunEnd;
                        continue;
                    }
                }
            }

            const double AbsHere = std::fabs(SignedDistances[Index]);
            int PlateauEnd = Index;
            while (PlateauEnd < Count && std::fabs(std::fabs(SignedDistances[PlateauEnd + 1]) - AbsHere) <= 1.0e-13 * std::max(1.0, AbsHere))
            {
                ++PlateauEnd;
            }
            const bool LeftDownCondition  = Index == 0 || AbsHere < std::fabs(SignedDistances[Index - 1]);
            const bool RightUpCondition   = PlateauEnd == Count || AbsHere < std::fabs(SignedDistances[PlateauEnd + 1]);
            if (LeftDownCondition && RightUpCondition && !(Index == 0 && PlateauEnd == Count && AbsHere > Settings.Precision))
            {
                const int    MidIndex = (Index + PlateauEnd) / 2;
                const double StepPrev = MidIndex > 0 ? Length(SamplePoints[MidIndex] - SamplePoints[MidIndex - 1]) : 0.0;
                const double StepNext = MidIndex < Count ? Length(SamplePoints[MidIndex + 1] - SamplePoints[MidIndex]) : 0.0;
                const double MaxStep  = std::max({ StepPrev, StepNext, Chord / std::max(Count, 1) });
                // Euclidean distance is 1-Lipschitz in arc length: |d| > 2.5·MaxStep + 16·Precision cannot reach Precision on the adjacent intervals.
                if (AbsHere <= 2.5 * MaxStep + 16.0 * Settings.Precision)
                {
                    const double MidTau = Low + (High - Low) * (0.5 * static_cast<double>(Index + PlateauEnd)) / Count;
                    Candidates.push_back({ MidTau, true, SurfaceHints[MidIndex] });
                }
            }
            else if ((Index == 0 || PlateauEnd == Count) && AbsHere <= 2.5 * (Chord / std::max(Count, 1)) + 16.0 * Settings.Precision)
            {
                const int    EndIndex = Index == 0 ? 0 : Count;
                const double EndTau   = Low + (High - Low) * EndIndex / Count;
                Candidates.push_back({ EndTau, true, SurfaceHints[EndIndex] });
            }
            Index = PlateauEnd;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

CurveSurfaceOutcome IntersectCurveSurface(const CurveGeometry& Curve, double Low, double High, const SurfaceGeometry& Surface, const CurveSurfaceSettings& Settings)
{
    CurveSurfaceOutcome    Outcome;
    std::vector<Candidate> Candidates;
    // 💡 No rational form on one side (traced curve, contact rail) or no implicit form on the other (rolling-ball or
    //    NURBS carrier): scan the exact signed distance instead of composing a polynomial.
    const bool ScanCondition = std::holds_alternative<IntersectionCurve>(Curve) || std::holds_alternative<ContactCurve>(Curve)
                            || ProceduralSurfaceCondition(Surface);
    if (ScanCondition && !std::holds_alternative<IntersectionCurve>(Curve))
    {
        // 📝 Spacing ≤ r/10 on a rolling-ball carrier (d is near-linear across a fraction of the tube radius) and ≤ 1/256
        //    of the curve's chord length; rails also take 16 samples per spine guide knot.
        double Chord = 0.0;                                                                  // [len] - Chord length of the interval
        Vector3 Previous = EvaluateCurve(Curve, Low).Position;
        for (int Index = 1; Index <= 64; ++Index)
        {
            const Vector3 Next = EvaluateCurve(Curve, Low + (High - Low) * Index / 64).Position;
            Chord += Length(Next - Previous);
            Previous = Next;
        }
        double Spacing = Chord / 256.0;                                                      // [len] - Target sample spacing
        if (const auto* Carrier = std::get_if<RollingBallSurface>(&Surface)) Spacing = std::min(Spacing, 0.1 * Carrier->Definition->Radius);
        // 📝 On a NURBS carrier the distance is near-linear across a fraction of the smallest curvature radius.
        if (const NurbsSurface* Spline = ResolveNurbsChart(Surface)) Spacing = std::min(Spacing, 1.0e-2 * Spline->Definition->Extent);
        const int Knots = static_cast<int>(CollectCurveBreakpoints(Curve, Low, High).size());
        const int Floor = Settings.ScanSpacing > 0.0 ? 8 : 64;                               // [-] - Fewest samples
        if (Settings.ScanSpacing > 0.0) Spacing = Settings.ScanSpacing;
        const int Count = std::clamp(std::max(static_cast<int>(std::ceil(Chord / std::max(Spacing, 1.0e-300))), 16 * Knots), Floor, 16384);
        CollectScannedCandidates(Curve, Low, High, Count, Chord, Surface, Settings, Outcome.Coincidences, Candidates);
    }
    if (const auto* Traced = std::get_if<IntersectionCurve>(&Curve))
    {
        // 📝 An intersection curve has no rational form: the exact signed distance is scanned at 16 samples per guide
        //    segment (the guide turns ≤ 0.1 rad per segment, so d is near-linear between samples). Sign changes become
        //    transversal candidates and local minima of |d| touch candidates; ③ polishes both on the exact distance.
        const TracedIntersection& Trace = *Traced->Trace;
        double Chord = 0.0;
        Vector3 Previous = EvaluateCurve(Curve, Low).Position;
        for (int Index = 1; Index <= 64; ++Index)
        {
            const Vector3 Next = EvaluateCurve(Curve, Low + (High - Low) * Index / 64).Position;
            Chord += Length(Next - Previous);
            Previous = Next;
        }
        const double KnotSpan = Trace.Knots.back() - Trace.Knots.front();                    // [-] - τ per guide
        const int    Count    = std::clamp(static_cast<int>(16.0 * static_cast<double>(Trace.Knots.size()) * (High - Low) / std::max(KnotSpan, 1.0e-300)), 64, 8192);
        CollectScannedCandidates(Curve, Low, High, Count, Chord, Surface, Settings, Outcome.Coincidences, Candidates);
    }
    for (const RationalSegment& Segment : ScanCondition ? std::vector<RationalSegment>{} : ConvertToRationalSegments(Curve, Low, High))
    {
        // ---- ① Composed implicit equation; an identically vanishing one means the segment lies in the surface.
        const BernsteinPolynomial Equation  = ComposeImplicitEquation(Surface, Segment);
        const double              Magnitude = ImplicitMagnitude(Surface, Segment);           // [len^d] - L^d
        double Largest = 0.0;                                                                // [len^d] - max |cᵢ|
        for (double Coefficient : Equation.Coefficients) Largest = std::max(Largest, std::fabs(Coefficient));
        if (Largest <= CoincidentFraction * Magnitude)
        {
            if (!Outcome.Coincidences.empty() && std::fabs(Outcome.Coincidences.back().High - Segment.CurveLow) <= 1.0e-12) Outcome.Coincidences.back().High = Segment.CurveHigh;
            else Outcome.Coincidences.push_back({ Segment.CurveLow, Segment.CurveHigh });
            continue;
        }

        // ---- ② Sign changes of F by Bézier clipping; extrema of F (roots of F′) as touch candidates.
        const RootIsolation Crossings = IsolateBernsteinRoots(Equation, Settings.ParameterTolerance);
        // ⚠️ A constant F (line parallel to a cylinder axis, circle coaxial with a surface of revolution) has a derivative
        //    made of rounding noise; clipping it would chase noise, so its extremum search is skipped.
        const BernsteinPolynomial Slope = DifferentiateBernstein(Equation);
        double SlopeLargest = 0.0;                                                           // [len^d] - max |c′ᵢ|
        for (double Coefficient : Slope.Coefficients) SlopeLargest = std::max(SlopeLargest, std::fabs(Coefficient));
        const RootIsolation Extrema   = SlopeLargest <= CoincidentFraction * Magnitude ? RootIsolation{} : IsolateBernsteinRoots(Slope, Settings.ParameterTolerance);
        Outcome.ClipSteps    += Crossings.ClipSteps + Extrema.ClipSteps;
        Outcome.Subdivisions += Crossings.Subdivisions + Extrema.Subdivisions;
        for (double Root : Crossings.Roots) Candidates.push_back({ SegmentParameter(Curve, Segment, Root), false });
        for (double Root : Extrema.Roots)   Candidates.push_back({ SegmentParameter(Curve, Segment, Root), true });
        // 📝 Segment end points are touch candidates too: a touch exactly at a joint has F′ ≠ 0 on either side.
        Candidates.push_back({ Segment.CurveLow, true });
        Candidates.push_back({ Segment.CurveHigh, true });
    }
    Outcome.Candidates = static_cast<int>(Candidates.size());

    // ---- ③ Polish on the exact signed distance and accept within precision.
    std::vector<CurveSurfaceCrossing> Transversals;
    std::vector<CurveSurfaceCrossing> Touches;
    const auto Accept = [&](double τ, bool TouchCondition, Vector2& Hint)
    {
        const CurveSample     Sample  = EvaluateCurve(Curve, τ);
        const SurfaceDistance Measure = MeasureSurfaceDistanceNear(Surface, Sample.Position, Hint);
        if (!(std::fabs(Measure.Distance) <= Settings.Precision)) return;                    // NaN-safe: never accept a non-finite probe
        for (const CoincidentInterval& Interval : Outcome.Coincidences) if (τ >= Interval.Low - 1.0e-12 && τ <= Interval.High + 1.0e-12) return;
        const double  Sine             = std::fabs(Dot(Measure.Normal, Sample.Derivative)) / std::max(Length(Sample.Derivative), 1.0e-300);
        const Vector2 SurfaceParameter = (std::isfinite(Hint.X) && std::isfinite(Hint.Y)) ? ProjectOntoSurfaceNear(Surface, Measure.Foot, Hint) : ProjectOntoSurface(Surface, Measure.Foot, { 0.0, 0.0 });
        CurveSurfaceCrossing Crossing{ τ, SurfaceParameter, Sample.Position,
                                       TouchCondition || Sine <= TangentSine ? CrossingCategory::Tangent : CrossingCategory::Transversal, std::fabs(Measure.Distance) };
        (Crossing.Category == CrossingCategory::Tangent ? Touches : Transversals).push_back(Crossing);
    };
    for (const Candidate& Entry : Candidates)
    {
        Vector2 Hint = Entry.SurfaceHint;
        if (Entry.TouchCondition) Accept(PolishExtremum(Curve, Surface, Entry.τ, Low, High, Hint), true, Hint);
        else                      Accept(PolishTransversal(Curve, Surface, Entry.τ, Low, High, Hint), false, Hint);
    }

    // ---- ④ Merge: duplicates collapse; crossings joined to a touch by a stretch within precision become that touch.
    const auto ByParameter = [](const CurveSurfaceCrossing& First, const CurveSurfaceCrossing& Second) { return First.CurveParameter < Second.CurveParameter; };
    std::sort(Transversals.begin(), Transversals.end(), ByParameter);
    std::sort(Touches.begin(), Touches.end(), ByParameter);
    const auto StretchWithinPrecision = [&](double From, double To)
    {
        for (int Sample = 1; Sample < 8; ++Sample)
        {
            if (std::fabs(ProbeDistance(Curve, Surface, From + (To - From) * Sample / 8.0).Distance) > Settings.Precision) return false;
        }
        return true;
    };
    std::vector<CurveSurfaceCrossing> Merged;
    for (const CurveSurfaceCrossing& Touch : Touches)
    {
        if (!Merged.empty() && (Distance(Merged.back().Position, Touch.Position) <= 2.0 * Settings.Precision || StretchWithinPrecision(Merged.back().CurveParameter, Touch.CurveParameter)))
        {
            // ⚠️ At a touch |d| is flat to second order: a point 5·10⁻⁹ along a line tangent to a cylinder still reads
            //    |d| ≈ 3·10⁻¹⁷, so comparing residuals picks by rounding. The extremum condition d′ = 0 is first order —
            //    keep the duplicate with the smaller |d′| / |C′| (it moved a tangent cap foot by 4.9·10⁻⁹).
            const auto Flatness = [&](const CurveSurfaceCrossing& Crossing)
            {
                const DistanceQuery Probe = ProbeDistance(Curve, Surface, Crossing.CurveParameter);
                return std::fabs(Probe.Slope) / std::max(Probe.Speed, 1.0e-300);
            };
            if (Flatness(Touch) < Flatness(Merged.back())) Merged.back() = Touch;
            continue;
        }
        Merged.push_back(Touch);
    }
    // 📝 A genuine touch has an extremum of |d| below precision; reject "touches" that sit on a transversal crossing
    //    (the extremum of F′ near a steep crossing polishes onto the crossing itself) — keep the transversal.
    std::vector<CurveSurfaceCrossing> Accepted;
    for (const CurveSurfaceCrossing& Crossing : Transversals)
    {
        if (!Accepted.empty() && Distance(Accepted.back().Position, Crossing.Position) <= 2.0 * Settings.Precision) continue;
        Accepted.push_back(Crossing);
    }
    for (const CurveSurfaceCrossing& Touch : Merged)
    {
        const double Slope = std::fabs(ProbeDistance(Curve, Surface, Touch.CurveParameter).Slope) / std::max(ProbeDistance(Curve, Surface, Touch.CurveParameter).Speed, 1.0e-300);
        bool Absorbed = false;                                                               // [-] - Touch joins nearby crossings
        for (auto Crossing = Accepted.begin(); Crossing != Accepted.end();)
        {
            const bool Near = Distance(Crossing->Position, Touch.Position) <= 2.0 * Settings.Precision;
            if (Near && Slope > TangentSine) { Absorbed = true; ++Crossing; continue; }      // Steep: it IS the crossing
            if (Near || StretchWithinPrecision(Crossing->CurveParameter, Touch.CurveParameter)) { Crossing = Accepted.erase(Crossing); continue; }
            ++Crossing;
        }
        if (!Absorbed) Accepted.push_back(Touch);
    }
    std::sort(Accepted.begin(), Accepted.end(), ByParameter);
    Outcome.Crossings = std::move(Accepted);
    return Outcome;
}

} // namespace Frontier::GeometrySpace::Parametric
