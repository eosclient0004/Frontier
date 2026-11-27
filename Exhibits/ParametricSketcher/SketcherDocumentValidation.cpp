/*============================================================================================================================================
                                                      SKETCHERDOCUMENTVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation exhibit for Phase 2 Parametric Sketcher Document, Codec, Scene, DrawingSheet & MatCap layer

#include "Engine/GeometrySpace/Parametric/Audit/ModelAudit.h"
#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"
#include "Engine/GeometrySpace/Parametric/Intersection/CurveSurfaceIntersection.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/CurveFitting.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocument.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocumentCodec.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDrawingSheet.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherImagePlane.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherMatcap.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherScene.h"
#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherSurfaceTracing.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <string>

namespace
{

using namespace Frontier;
using namespace Frontier::GeometrySpace::Parametric;
using namespace Frontier::Workspaces::Sketcher;

bool VerifyCheck(bool Condition, const char* Message) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[SketcherDocumentValidation] FAILED: %s\n", Message);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    uint32_t Passed = 0u;

    // 1. Construct primitive + sketch + extrude + scene tessellation & CPU pick
    SketcherDocument Doc = CreateSketcherDocument("Bracket");
    {
        SketcherConstructRequest BoxReq {};
        BoxReq.Kind     = SketcherConstructCategory::Block;
        BoxReq.Position = { -1.5, -1.0, 0.0 };
        BoxReq.Width    = 3.0;
        BoxReq.Depth    = 2.0;
        BoxReq.Height   = 1.2;
        const SketcherCommandOutcome BoxOut = ConstructSketcherPrimitive(Doc, BoxReq);
        if (!VerifyCheck(BoxOut.AcceptedCondition && Doc.Summary.Bodies.size() == 1u,
                         "Construct Block primitive in SketcherDocument"))
        {
            return 1;
        }

        SketcherDrawingRequest CircleReq {};
        CircleReq.Kind            = SketcherDrawingCategory::Circle;
        CircleReq.GridPlane       = 0;
        CircleReq.CentreCondition = true;
        CircleReq.FillCondition   = true;
        CircleReq.Points          = { { 0.0, 0.0 }, { 0.45, 0.0 } };
        const SketcherCommandOutcome SketchOut = DrawSketcherSketch(Doc, CircleReq);
        if (!VerifyCheck(SketchOut.AcceptedCondition, "Draw filled circle sketch"))
        {
            return 1;
        }

        const std::string SketchLabel = Doc.Tree.Steps.back().Label;
        const SketcherCommandOutcome CutOut = ExtrudeSketcherSketch(
            Doc,
            SketchLabel,
            2.0,
            FeatureCombination::Difference);
        if (!VerifyCheck(CutOut.AcceptedCondition && Doc.Summary.Bodies.size() == 1u,
                         "Extrude circle Difference cut"))
        {
            return 1;
        }

        const SketcherScene Scene = BuildSketcherScene(Doc);
        if (!VerifyCheck(!Scene.Corners.empty() && !Scene.Segments.empty() && !Scene.Faces.empty(),
                         "BuildSketcherScene produces renderable face corners and edge segments"))
        {
            return 1;
        }

        const OrbitCamera Cam {};
        const OrbitCameraBasis Frame = FrameOrbitCamera(Cam, 1280.0, 720.0);
        const OrbitRay CenterRay = CastOrbitRay(Frame, 640.0, 360.0);
        const auto Hit = PickSketcherScene(Scene, CenterRay, 0.05);
        if (!VerifyCheck(Hit.has_value(), "PickSketcherScene hits extruded bracket"))
        {
            return 1;
        }
        ++Passed;
    }

    // 2. Deterministic TOML save -> load -> save roundtrip
    {
        const std::string Encoded1 = EncodeSketcherDocument(Doc);
        const SketcherDocumentFile Parsed = DecodeSketcherDocument(Encoded1);
        if (!VerifyCheck(Parsed.Refusal.empty() && Parsed.Title == "Bracket" && !Parsed.Tree.Steps.empty(),
                         "DecodeSketcherDocument parses saved document cleanly"))
        {
            return 1;
        }
        SketcherDocument Reloaded = CreateSketcherDocument(Parsed.Title);
        const auto ReplaceOut = ReplaceSketcherTree(Reloaded, Parsed.Tree, Parsed.Captions, "Reload");
        if (!VerifyCheck(ReplaceOut.AcceptedCondition, "ReplaceSketcherTree regenerates loaded document"))
        {
            return 1;
        }
        const std::string Encoded2 = EncodeSketcherDocument(Reloaded);
        if (!VerifyCheck(Encoded1 == Encoded2, "EncodeSketcherDocument roundtrip is byte-identical"))
        {
            return 1;
        }
        ++Passed;
    }

    // 3. Undo / Redo + Built-in MatCap Library + Drawing Sheet Composition
    {
        if (!VerifyCheck(!Doc.UndoEntries.empty(), "Document records undo history"))
        {
            return 1;
        }
        const auto& Matcaps = SketcherBuiltInMatcaps();
        const SketcherMatcapLibrary Library = AssembleSketcherMatcaps(Doc.Appearance);
        const auto Chrome = std::find(Library.Names.begin(), Library.Names.end(), "ClassAChrome");
        const size_t ChromeLayer = size_t(Chrome - Library.Names.begin());
        if (!VerifyCheck(Matcaps.size() >= 29 && Chrome != Library.Names.end(), "MatCap library includes shared baked studio set") ||
            !VerifyCheck(ChromeLayer > 0 && ChromeLayer - 1 < Library.Pictures.size() && Library.Pictures[ChromeLayer - 1].Pixels.size() == size_t(256 * 256 * 4)
                         && Library.Refusals[ChromeLayer].empty(), "Baked MatCap atlas preview is resident"))
        {
            return 1;
        }
        SketcherDocument VertexDoc = CloneSketcherDocument(Doc);
        const SketcherScene VertexScene = BuildSketcherScene(VertexDoc);
        const std::vector<std::string> VertexEdges = VertexScene.Vertices.empty() ? std::vector<std::string>{}
                                                    : ResolveSketcherVertexBlendEdges(VertexDoc, { VertexScene.Vertices.front().Name });
        VertexDoc.Selection = {};
        VertexDoc.Selection.Edges = VertexEdges;
        if (!VerifyCheck(VertexEdges.size() >= 2 && BlendSketcherSelection(VertexDoc, BlendProfile::Fillet, 0.02).AcceptedCondition,
                         "Selected body vertex resolves to an exact CAD corner bevel"))
        {
            return 1;
        }
        const SketcherDrawingSheet Sheet = ComposeSketcherDrawingSheet(Doc, Doc.DrawingSheet);
        if (!VerifyCheck(Sheet.Refusal.empty() && Sheet.Bodies == 1, "ComposeSketcherDrawingSheet succeeds for bracket"))
        {
            return 1;
        }
        ++Passed;
    }

    // 4. .ImageSpace export -> decode roundtrip & SketcherImagePlane validation
    {
        SketcherPicture Source {};
        Source.Width  = 4u;
        Source.Height = 4u;
        Source.Pixels.resize(4u * 4u * 4u, 0u);
        for (uint32_t Y = 0u; Y < Source.Height; ++Y)
        {
            for (uint32_t X = 0u; X < Source.Width; ++X)
            {
                const size_t Base = (size_t(Y) * Source.Width + X) * 4u;
                Source.Pixels[Base + 0u] = static_cast<uint8_t>((X * 64u) + 32u);
                Source.Pixels[Base + 1u] = static_cast<uint8_t>((Y * 64u) + 16u);
                Source.Pixels[Base + 2u] = 200u;
                Source.Pixels[Base + 3u] = 255u;
            }
        }

        const std::filesystem::path TempImageSpace =
            std::filesystem::temp_directory_path() / "SketcherDocTestRef.ImageSpace";
        std::string ImgDiag {};
        if (!VerifyCheck(ExportSketcherPictureAsImageSpace(Source, TempImageSpace, ImgDiag, "DocRefPlane"),
                         "ExportSketcherPictureAsImageSpace writes .ImageSpace"))
        {
            std::fprintf(stderr, "ImageSpace export diag: %s\n", ImgDiag.c_str());
            return 1;
        }
        const SketcherPicture Decoded = DecodeSketcherPicture(TempImageSpace.string());
        std::filesystem::remove(TempImageSpace);
        if (!VerifyCheck(Decoded.Refusal.empty() && Decoded.Width == 4u && Decoded.Height == 4u &&
                             Decoded.Pixels.size() == Source.Pixels.size(),
                         "DecodeSketcherPicture reads .ImageSpace back cleanly"))
        {
            return 1;
        }
        for (size_t Idx = 0u; Idx < Source.Pixels.size(); ++Idx)
        {
            if (!VerifyCheck(std::abs(int(Decoded.Pixels[Idx]) - int(Source.Pixels[Idx])) <= 2,
                             ".ImageSpace sRGB<->Linear pixel roundtrip matches within 2 LSB"))
            {
                return 1;
            }
        }
        SketcherImagePlane Plane {};
        Plane.Name   = "Blueprint";
        Plane.Path   = TempImageSpace.string();
        Plane.Corner = { -2.0, 0.0, 1.0 };
        Plane.Right  = { 4.0, 0.0, 0.0 };
        Plane.Down   = { 0.0, 0.0, -2.0 };
        if (!VerifyCheck(ExamineSketcherImagePlane(Plane).empty(), "ExamineSketcherImagePlane accepts valid plane"))
        {
            return 1;
        }
        ++Passed;
    }

    // 5. Curved-surface crossing curves -> split + auto-surface (Triangle, Square, Rectangle, Multi-loop Cage, Fill & Loft)
    {
        auto SpherePoint = [](double Radius, double Theta, double Phi) -> Vector3
        {
            const double CosPhi = std::cos(Phi);
            return { Radius * CosPhi * std::cos(Theta), Radius * CosPhi * std::sin(Theta), Radius * std::sin(Phi) };
        };
        auto SampleSphereArc = [&](double Radius, double Theta0, double Phi0, double Theta1, double Phi1, int Count = 9) -> NurbsCurve
        {
            std::vector<Vector3> Pts;
            Pts.reserve(size_t(Count));
            for (int I = 0; I < Count; ++I)
            {
                const double U = double(I) / double(Count - 1);
                Pts.push_back(SpherePoint(Radius, Theta0 + (Theta1 - Theta0) * U, Phi0 + (Phi1 - Phi0) * U));
            }
            return FitSpaceCurve(Pts, {}).Curve;
        };

        // 5a. Triangle on curved sphere (3 crossing curves with overhanging tails, auto-split + auto-surface)
        {
            SketcherDocument RetopoDoc = CreateSketcherDocument();
            SketcherConstructRequest RefSphere {};
            RefSphere.Kind   = SketcherConstructCategory::Sphere;
            RefSphere.Radius = 2.0;
            const auto RefOut = ConstructSketcherPrimitive(RetopoDoc, RefSphere);
            if (!VerifyCheck(RefOut.AcceptedCondition && RetopoDoc.Summary.Bodies.size() == 1u, "Construct curved reference sphere"))
            {
                return 1;
            }
            const std::string RefBody = RetopoDoc.Summary.Bodies.front().Key;
            (void)ToggleSketcherLock(RetopoDoc, RefBody);

            const double R = 2.0;
            // Base curve (phi = 0.20, theta in [-0.35, 0.55])
            // Left slanted curve (theta: -0.25 -> 0.15, phi: 0.10 -> 0.80)
            // Right slanted curve (theta: 0.45 -> 0.05, phi: 0.10 -> 0.80)
            const auto C1 = SampleSphereArc(R, -0.35, 0.20, 0.55, 0.20);
            const auto C2 = SampleSphereArc(R, -0.25, 0.10, 0.15, 0.80);
            const auto C3 = SampleSphereArc(R,  0.45, 0.10, 0.05, 0.80);
            const auto O1 = DrawSketcherProjectedCurve(RetopoDoc, C1, false, true, 0.001, true);
            const auto O2 = DrawSketcherProjectedCurve(RetopoDoc, C2, false, true, 0.001, true);
            const auto O3 = DrawSketcherProjectedCurve(RetopoDoc, C3, false, true, 0.001, true);
            if (!VerifyCheck(O1.AcceptedCondition && O2.AcceptedCondition && O3.AcceptedCondition,
                             "Curved triangle crossing curves accepted"))
            {
                std::fprintf(stderr, "Refusal: %s / %s / %s\n", O1.Refusal.c_str(), O2.Refusal.c_str(), O3.Refusal.c_str());
                return 1;
            }
            if (!VerifyCheck(RetopoDoc.Summary.Bodies.size() == 2u,
                             "Auto-split + Auto-surface builds triangular NURBS surface on curved sphere"))
            {
                std::fprintf(stderr, "Bodies=%zu Curves=%zu Steps=%zu\n",
                             RetopoDoc.Summary.Bodies.size(), RetopoDoc.Regeneration.Curves.size(), RetopoDoc.Tree.Steps.size());
                return 1;
            }
            const SketcherScene Scene = BuildSketcherScene(RetopoDoc);
            bool VerifiedPatch = false;
            for (const auto& Face : Scene.Faces)
            {
                if (Scene.Bodies[Face.Body].Key == RefBody) continue;
                VerifiedPatch = Face.CornerCount >= 3u;
                for (uint32_t K = 0u; K < Face.CornerCount; ++K)
                {
                    const auto& P = Scene.Corners[Face.FirstCorner + K].Position;
                    const double RSurf = std::sqrt(P[0] * P[0] + P[1] * P[1] + P[2] * P[2]);
                    if (!VerifyCheck(std::abs(RSurf - R) < 0.08, "Curved triangle patch conforms to sphere curvature"))
                    {
                        std::fprintf(stderr, "RSurf=%.4f expected ~%.4f\n", RSurf, R);
                        return 1;
                    }
                }
            }
            if (!VerifyCheck(VerifiedPatch, "Curved triangle patch produces valid tessellated faces"))
            {
                return 1;
            }
            ++Passed;
        }

        // 5b. Square + Rectangle + Multi-cell cage on curved sphere
        {
            SketcherDocument CageDoc = CreateSketcherDocument();
            SketcherConstructRequest RefSphere {};
            RefSphere.Kind   = SketcherConstructCategory::Sphere;
            RefSphere.Radius = 2.0;
            const auto RefOut = ConstructSketcherPrimitive(CageDoc, RefSphere);
            if (!VerifyCheck(RefOut.AcceptedCondition, "Construct curved reference sphere for multi-cell cage")) return 1;
            const std::string RefBody = CageDoc.Summary.Bodies.front().Key;
            (void)ToggleSketcherLock(CageDoc, RefBody);

            const double R = 2.0;
            // Grid of 2 latitudinal + 3 meridional crossing curves -> creates BOTH a curved Square AND an adjacent curved Rectangle!
            const auto Lat1 = SampleSphereArc(R, -0.35, 0.15, 0.95, 0.15);
            const auto Lat2 = SampleSphereArc(R, -0.35, 0.55, 0.95, 0.55);
            const auto Mer1 = SampleSphereArc(R, -0.20, 0.00, -0.20, 0.70);
            const auto Mer2 = SampleSphereArc(R,  0.20, 0.00,  0.20, 0.70);
            const auto Mer3 = SampleSphereArc(R,  0.80, 0.00,  0.80, 0.70);
            if (!VerifyCheck(DrawSketcherProjectedCurve(CageDoc, Lat1, false, true, 0.001, true).AcceptedCondition &&
                             DrawSketcherProjectedCurve(CageDoc, Lat2, false, true, 0.001, true).AcceptedCondition &&
                             DrawSketcherProjectedCurve(CageDoc, Mer1, false, true, 0.001, true).AcceptedCondition &&
                             DrawSketcherProjectedCurve(CageDoc, Mer2, false, true, 0.001, true).AcceptedCondition &&
                             DrawSketcherProjectedCurve(CageDoc, Mer3, false, true, 0.001, true).AcceptedCondition,
                             "Crossing curves for square + rectangle cage accepted"))
            {
                return 1;
            }
            int RetopoFaces = 0;
            for (const auto& B : CageDoc.Summary.Bodies)
            {
                if (B.Key != RefBody) RetopoFaces += B.Faces;
            }
            if (!VerifyCheck(RetopoFaces >= 2,
                             "Multi-cell crossing cage builds both square and rectangle CAD surfaces"))
            {
                std::fprintf(stderr, "CageDoc Bodies=%zu RetopoFaces=%d Curves=%zu Steps=%zu\n",
                             CageDoc.Summary.Bodies.size(), RetopoFaces, CageDoc.Regeneration.Curves.size(), CageDoc.Tree.Steps.size());
                return 1;
            }
            ++Passed;
        }

        // 5c. Fill hover/apply + Loft on 1-curve closed & 4-curve rectangle loops without AutoSurface
        {
            SketcherDocument ManualDoc = CreateSketcherDocument();
            const double R = 2.0;
            const auto Lat1 = SampleSphereArc(R, -0.45, 0.15, 0.45, 0.15);
            const auto Lat2 = SampleSphereArc(R, -0.45, 0.50, 0.45, 0.50);
            const auto Mer1 = SampleSphereArc(R, -0.30, 0.00, -0.30, 0.65);
            const auto Mer2 = SampleSphereArc(R,  0.30, 0.00,  0.30, 0.65);
            (void)DrawSketcherProjectedCurve(ManualDoc, Lat1, false, true, 0.001, false);
            (void)DrawSketcherProjectedCurve(ManualDoc, Lat2, false, true, 0.001, false);
            (void)DrawSketcherProjectedCurve(ManualDoc, Mer1, false, true, 0.001, false);
            (void)DrawSketcherProjectedCurve(ManualDoc, Mer2, false, true, 0.001, false);
            if (!VerifyCheck(ManualDoc.Summary.Bodies.empty(), "AutoSurface=false leaves cage unsurfaced until Fill or Build"))
            {
                return 1;
            }
            // Ray aimed straight at center of curved rectangle (theta=0, phi=0.325)
            const Vector3 Target = SpherePoint(R, 0.0, 0.325);
            const Vector3 Eye    = Target * 2.5;
            const Vector3 Dir    = Normalize(Target - Eye);
            const auto Preview   = PreviewSketcherSpaceLoop(ManualDoc, Eye, Dir, 0.001);
            if (!VerifyCheck(Preview.Refusal.empty() && Preview.Labels.size() == 4u,
                             "PreviewSketcherSpaceLoop detects 4-edge curved rectangle under pointer"))
            {
                std::fprintf(stderr, "Preview refusal: %s labels=%zu\n", Preview.Refusal.c_str(), Preview.Labels.size());
                return 1;
            }
            const auto FillOut = FillSketcherSpaceLoop(ManualDoc, Preview.Labels, 0.001);
            if (!VerifyCheck(FillOut.AcceptedCondition && ManualDoc.Summary.Bodies.size() == 1u,
                             "FillSketcherSpaceLoop surfaces hovered curved rectangle loop"))
            {
                std::fprintf(stderr, "Fill refusal: %s\n", FillOut.Refusal.c_str());
                return 1;
            }

            // Single closed smooth 3D curve on sphere -> LoftSketcherCurves
            std::vector<Vector3> LoopPts;
            for (int I = 0; I <= 24; ++I)
            {
                const double Ang = 2.0 * 3.14159265358979323846 * (double(I) / 24.0);
                LoopPts.push_back(SpherePoint(R, 0.25 * std::cos(Ang), -0.40 + 0.18 * std::sin(Ang)));
            }
            const auto ClosedFit = FitSpaceCurve(LoopPts, {});
            if (!VerifyCheck(ClosedFit.Refusal.empty() && ClosedFit.Curve.Definition != nullptr,
                             "Fit smooth closed 3D curve on sphere"))
            {
                std::fprintf(stderr, "Fit refusal: %s\n", ClosedFit.Refusal.c_str());
                return 1;
            }
            const auto ClosedCurveOut = DrawSketcherProjectedCurve(ManualDoc, ClosedFit.Curve, true, false, 0.001, false);
            if (!VerifyCheck(ClosedCurveOut.AcceptedCondition, "Draw single closed 3D curve"))
            {
                std::fprintf(stderr, "ClosedCurve refusal: %s\n", ClosedCurveOut.Refusal.c_str());
                return 1;
            }
            const std::string ClosedLabel = ManualDoc.Tree.Steps.back().Label;
            const auto LoftOut = LoftSketcherCurves(ManualDoc, { ClosedLabel }, 0, false);
            int TotalManualFaces = 0;
            for (const auto& B : ManualDoc.Summary.Bodies) TotalManualFaces += B.Faces;
            if (!VerifyCheck(LoftOut.AcceptedCondition && TotalManualFaces == 2,
                             "LoftSketcherCurves surfaces single closed 3D curve"))
            {
                std::fprintf(stderr, "Loft refusal: %s bodies=%zu faces=%d\n",
                             LoftOut.Refusal.c_str(), ManualDoc.Summary.Bodies.size(), TotalManualFaces);
                return 1;
            }
            ++Passed;
        }
    }

    // 8. Concave box-car closed-loop auto-surface (exact trimmed PlaneSurface), sheet extrusion & coplanar Boolean/intersection safety
    {
        const std::vector<Vector3> UpperTracePts = {
            { 0.0, -3.0, -0.8 }, { 0.0, -3.0,  0.1 }, { 0.0, -1.8,  0.2 },
            { 0.0, -1.1,  1.1 }, { 0.0,  1.0,  1.1 }, { 0.0,  1.6,  0.35 },
            { 0.0,  2.8,  0.35 }, { 0.0,  3.0,  0.2 }, { 0.0,  3.0, -0.8 }
        };
        const std::vector<Vector3> LowerTracePts = {
            { 0.0,  3.0, -0.8 }, { 0.0,  2.3, -0.8 }, { 0.0,  2.1, -0.35 },
            { 0.0,  1.4, -0.35 }, { 0.0,  1.2, -0.8 }, { 0.0, -1.2, -0.8 },
            { 0.0, -1.4, -0.35 }, { 0.0, -2.1, -0.35 }, { 0.0, -2.3, -0.8 }, { 0.0, -3.0, -0.8 }
        };

        SketcherDocument CarDoc = CreateSketcherDocument("BoxCarProfile");
        const auto UpperTrace = MakeSketcherControlTrace(UpperTracePts, SketcherDrawingCategory::Line, SketchSplineForm{}, false);
        const auto LowerTrace = MakeSketcherControlTrace(LowerTracePts, SketcherDrawingCategory::Line, SketchSplineForm{}, false);
        const auto OutA = DrawSketcherProjectedCurve(CarDoc, UpperTrace.Curve, false, true, 0.001, true);
        const auto OutB = DrawSketcherProjectedCurve(CarDoc, LowerTrace.Curve, false, true, 0.001, true);
        const SketcherScene CarScene = BuildSketcherScene(CarDoc);

        int PositiveXCount = 0;
        int NegativeXCount = 0;
        for (size_t CornerIdx = 0; CornerIdx + 2 < CarScene.Corners.size(); CornerIdx += 3)
        {
            const auto& P0 = CarScene.Corners[CornerIdx].Position;
            const auto& P1 = CarScene.Corners[CornerIdx + 1].Position;
            const auto& P2 = CarScene.Corners[CornerIdx + 2].Position;
            const double NormalX = double(P1[1] - P0[1]) * double(P2[2] - P0[2]) - double(P1[2] - P0[2]) * double(P2[1] - P0[1]);
            if (NormalX > 1.0e-7) ++PositiveXCount;
            else if (NormalX < -1.0e-7) ++NegativeXCount;
        }
        if (!VerifyCheck(OutA.AcceptedCondition && OutB.AcceptedCondition && CarDoc.Summary.SoundCondition &&
                         CarScene.Faces.size() == 1u && CarScene.Faces[0].Surface == "Plane" &&
                         CarScene.Edges.size() == 17u && PositiveXCount > 0 && NegativeXCount == 0 &&
                         std::abs(CarScene.Faces[0].Area - 7.845) < 1.0e-4,
                         "Concave 17-edge box-car closed loop builds exact 1-face Plane sheet with zero fold-overs"))
        {
            return 1;
        }

        // Direct positive & negative Face Extrude on the concave planar sheet face -> closed Solid car body
        SketcherDocument ExtrudePosDoc = CloneSketcherDocument(CarDoc);
        ExtrudePosDoc.Selection.Faces  = { CarScene.Faces[0].Name };
        const auto FaceExPos = ChangeSketcherSelection(ExtrudePosDoc, FaceOperationCategory::Extrude, 1.8);
        SketcherDocument ExtrudeNegDoc = CloneSketcherDocument(CarDoc);
        ExtrudeNegDoc.Selection.Faces  = { CarScene.Faces[0].Name };
        const auto FaceExNeg = ChangeSketcherSelection(ExtrudeNegDoc, FaceOperationCategory::Extrude, -1.8);
        if (!VerifyCheck(FaceExPos.AcceptedCondition && FaceExNeg.AcceptedCondition &&
                         ExtrudePosDoc.Summary.Bodies.size() == 1u && !ExtrudePosDoc.Summary.Bodies[0].SheetCondition &&
                         ExtrudePosDoc.Summary.Bodies[0].SoundCondition && ExtrudePosDoc.Summary.Bodies[0].Faces == 19 &&
                         std::abs(ExtrudePosDoc.Summary.Bodies[0].Volume - 7.845 * 1.8) < 1.0e-4 &&
                         ExtrudeNegDoc.Summary.Bodies.size() == 1u && !ExtrudeNegDoc.Summary.Bodies[0].SheetCondition &&
                         ExtrudeNegDoc.Summary.Bodies[0].SoundCondition &&
                         std::abs(ExtrudeNegDoc.Summary.Bodies[0].Volume - 7.845 * 1.8) < 1.0e-4,
                         "Extruding concave car Network sheet face (+1.8 and -1.8) builds watertight 19-face Solid"))
        {
            return 1;
        }

        // Sketch Extrude with Union while an open Network sheet body is active (regression for UI freeze)
        SketcherDrawingRequest CarSketch {};
        CarSketch.Kind            = SketcherDrawingCategory::Line;
        CarSketch.GridPlane       = 2;
        CarSketch.ClosedCondition = true;
        CarSketch.FillCondition   = true;
        for (size_t Index = 0; Index + 1 < UpperTracePts.size(); ++Index) CarSketch.Points.push_back({ UpperTracePts[Index].Y, UpperTracePts[Index].Z });
        for (size_t Index = 0; Index + 1 < LowerTracePts.size(); ++Index) CarSketch.Points.push_back({ LowerTracePts[Index].Y, LowerTracePts[Index].Z });
        if (!VerifyCheck(DrawSketcherSketch(CarDoc, CarSketch).AcceptedCondition, "Draw concave car Sketch on YZ")) return 1;
        const std::string CarSketchLabel = CarDoc.Tree.Steps.back().Label;
        const auto ExtrudeStart = std::chrono::steady_clock::now();
        const auto SketchExOut  = ExtrudeSketcherSketch(CarDoc, CarSketchLabel, 1.8, FeatureCombination::Union, "", 0.0, true);
        const double ExtrudeMs  = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ExtrudeStart).count();
        if (!VerifyCheck(SketchExOut.AcceptedCondition && CarDoc.Summary.SoundCondition && ExtrudeMs < 250.0,
                         "ExtrudeSketcherSketch with active Network sheet completes without freeze (< 250 ms)"))
        {
            return 1;
        }

        // 3D curved tumblehome concave car loop -> convex ear-clipped NURBS panels with zero fold-overs
        std::vector<Vector3> CurvedUpper = UpperTracePts;
        std::vector<Vector3> CurvedLower = LowerTracePts;
        for (Vector3& Pt : CurvedUpper) Pt.X = 0.08 * (Pt.Z + 0.8) * (1.1 - Pt.Z);
        for (Vector3& Pt : CurvedLower) Pt.X = 0.08 * (Pt.Z + 0.8) * (1.1 - Pt.Z);
        SketcherDocument CurvedCarDoc = CreateSketcherDocument("CurvedCarLoop");
        const auto CurvedA = MakeSketcherControlTrace(CurvedUpper, SketcherDrawingCategory::Line, SketchSplineForm{}, false);
        const auto CurvedB = MakeSketcherControlTrace(CurvedLower, SketcherDrawingCategory::Line, SketchSplineForm{}, false);
        (void)DrawSketcherProjectedCurve(CurvedCarDoc, CurvedA.Curve, false, true, 0.001, true);
        const auto CurvedOutB = DrawSketcherProjectedCurve(CurvedCarDoc, CurvedB.Curve, false, true, 0.001, true);
        const SketcherScene CurvedScene = BuildSketcherScene(CurvedCarDoc);
        int CurvedPosX = 0, CurvedNegX = 0;
        for (size_t CornerIdx = 0; CornerIdx + 2 < CurvedScene.Corners.size(); CornerIdx += 3)
        {
            const auto& P0 = CurvedScene.Corners[CornerIdx].Position;
            const auto& P1 = CurvedScene.Corners[CornerIdx + 1].Position;
            const auto& P2 = CurvedScene.Corners[CornerIdx + 2].Position;
            const double NormalX = double(P1[1] - P0[1]) * double(P2[2] - P0[2]) - double(P1[2] - P0[2]) * double(P2[1] - P0[1]);
            if (NormalX > 1.0e-7) ++CurvedPosX;
            else if (NormalX < -1.0e-7) ++CurvedNegX;
        }
        if (!VerifyCheck(CurvedOutB.AcceptedCondition && CurvedPosX > 0 && CurvedNegX == 0,
                         "3D non-planar concave car loop decomposes into convex panels with zero fold-overs"))
        {
            return 1;
        }

        // Coplanar StraightLine on NurbsSurface -> Coincidences recorded in < 50 ms (no cold-grid extremum storm)
        const auto FlatNurbs = ConstructNurbsSurface(
            1, 1, 2, 2, { 0.0, 0.0, 1.0, 1.0 }, { 0.0, 0.0, 1.0, 1.0 },
            { { 0.0, -3.0, -1.0 }, { 0.0, -3.0, 1.0 }, { 0.0, 3.0, -1.0 }, { 0.0, 3.0, 1.0 } },
            { 1.0, 1.0, 1.0, 1.0 });
        const auto ScanStart = std::chrono::steady_clock::now();
        const CurveSurfaceOutcome CoincidentOut = IntersectCurveSurface(
            StraightLine{ { 0.0, -2.5, 0.0 }, { 0.0, 1.0, 0.0 } }, 0.0, 5.0, SurfaceGeometry{ *FlatNurbs });
        const double ScanMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ScanStart).count();
        if (!VerifyCheck(!CoincidentOut.Coincidences.empty() && ScanMs < 50.0,
                         "Coplanar line on NurbsSurface detects CoincidentInterval rapidly without extremum storm"))
        {
            return 1;
        }
        ++Passed;

        // 9. B-Rep vertex reshape on concave N-gon car solid & curved wheel-arch cylinder (in-plane, off-plane X, slide, chained moves, sheet)
        {
            auto FindVertexByPosition = [](const SketcherScene& TargetScene, Vector3 Expected) -> std::string
            {
                double      BestGap = 1.0e-3;
                std::string Found   = {};
                for (const SketcherSceneVertex& Vert : TargetScene.Vertices)
                {
                    const double Gap = Distance(Vector3{ Vert.Position[0], Vert.Position[1], Vert.Position[2] }, Expected);
                    if (Gap < BestGap) { BestGap = Gap; Found = Vert.Name; }
                }
                return Found;
            };

            // (a) Sheet vertex move on unextruded concave car profile
            SketcherDocument SheetMoveDoc = CloneSketcherDocument(CarDoc);
            const SketcherScene SheetScene0 = BuildSketcherScene(SheetMoveDoc);
            const std::string SheetHoodVert = FindVertexByPosition(SheetScene0, { 0.0, -1.8, 0.2 });
            SheetMoveDoc.Selection = {};
            SheetMoveDoc.Selection.Vertices = { SheetHoodVert };
            const auto SheetMoveOut = ReshapeSketcherSelection(SheetMoveDoc, TranslationMap({ 0.0, 0.12, 0.14 }), "Move");
            if (!VerifyCheck(!SheetHoodVert.empty() && SheetMoveOut.AcceptedCondition && SheetMoveDoc.Summary.SoundCondition,
                             "ReshapeSketcherSelection moves vertex on concave 17-edge Sheet body cleanly"))
            {
                std::fprintf(stderr, "  SheetMove refusal: %s\n", SheetMoveOut.Refusal.c_str());
                return 1;
            }

            // (b) Slide hood-windshield A-pillar corner (0, -1.8, 0.2) 40% along the hood edge on the 19-face extruded solid
            const SketcherScene CarSolidScene = BuildSketcherScene(ExtrudePosDoc);
            const std::string   HoodWindshieldVert = FindVertexByPosition(CarSolidScene, { 0.0, -1.8, 0.2 });
            SketcherDocument    SlideHoodDoc       = CloneSketcherDocument(ExtrudePosDoc);
            SlideHoodDoc.Selection                 = {};
            SlideHoodDoc.Selection.Vertices        = { HoodWindshieldVert };
            const Vector3 HoodDelta                = (Vector3{ 0.0, -3.0, 0.1 } - Vector3{ 0.0, -1.8, 0.2 }) * 0.4;
            const auto    SlideHoodOut             = ReshapeSketcherSelection(SlideHoodDoc, TranslationMap(HoodDelta), "Move");
            const SketcherScene SlideHoodScene     = BuildSketcherScene(SlideHoodDoc);
            if (!VerifyCheck(!HoodWindshieldVert.empty() && SlideHoodOut.AcceptedCondition &&
                             SlideHoodDoc.Summary.Bodies[0].SoundCondition && SlideHoodDoc.Summary.Bodies[0].Faces == 19 &&
                             !FindVertexByPosition(SlideHoodScene, Vector3{ 0.0, -1.8, 0.2 } + HoodDelta).empty(),
                             "Sliding hood-windshield vertex along hood edge keeps 19-face solid sound"))
            {
                std::fprintf(stderr, "  SlideHood refusal: %s\n", SlideHoodOut.Refusal.c_str());
                return 1;
            }

            // (c) In-plane YZ move (bends hood & windshield into bilinear NURBS patches while 17-sided cap stays a single Plane)
            SketcherDocument InPlaneDoc = CloneSketcherDocument(ExtrudePosDoc);
            InPlaneDoc.Selection          = {};
            InPlaneDoc.Selection.Vertices = { HoodWindshieldVert };
            const auto InPlaneOut = ReshapeSketcherSelection(InPlaneDoc, TranslationMap({ 0.0, 0.14, 0.18 }), "Move");
            if (!VerifyCheck(InPlaneOut.AcceptedCondition && InPlaneDoc.Summary.Bodies[0].SoundCondition &&
                             InPlaneDoc.Summary.Bodies[0].Faces == 19,
                             "In-plane YZ vertex move on 17-sided car cap bends adjacent quads without refusal"))
            {
                std::fprintf(stderr, "  InPlane refusal: %s\n", InPlaneOut.Refusal.c_str());
                return 1;
            }

            // (d) Off-plane 3D / X move on 17-sided concave car cap + chained second move via auto-rebound Selection.Vertices
            SketcherDocument OffPlaneDoc = CloneSketcherDocument(ExtrudePosDoc);
            OffPlaneDoc.Selection          = {};
            OffPlaneDoc.Selection.Vertices = { HoodWindshieldVert };
            const auto OffPlaneOut1 = ReshapeSketcherSelection(OffPlaneDoc, TranslationMap({ -0.22, 0.08, 0.10 }), "Move");
            const bool ReboundOk    = OffPlaneOut1.AcceptedCondition && OffPlaneDoc.Selection.Vertices.size() == 1u;
            const auto OffPlaneOut2 = ReboundOk
                                    ? ReshapeSketcherSelection(OffPlaneDoc, TranslationMap({ 0.06, -0.04, 0.05 }), "Move")
                                    : SketcherCommandOutcome{};
            if (!VerifyCheck(OffPlaneOut1.AcceptedCondition && ReboundOk && OffPlaneOut2.AcceptedCondition &&
                             OffPlaneDoc.Summary.Bodies[0].SoundCondition,
                             "Off-plane X / 3D vertex move on 17-sided concave cap splits minimal planar facets & chains moves"))
            {
                std::fprintf(stderr, "  OffPlane1='%s' OffPlane2='%s'\n",
                             OffPlaneOut1.Refusal.c_str(), OffPlaneOut2.Refusal.c_str());
                return 1;
            }

            // (e) Concave car with CircularArc wheel well -> extruded CylinderSurface + 17-sided cap with CircularArc:
            //     verify moving BOTH the hood-windshield vertex AND the wheel-arch corner vertex (in-plane + off-plane) succeeds!
            SketcherDocument ArcCarDoc = CreateSketcherDocument("ArcWheelCar");
            SketcherDrawingRequest ArcSketch {};
            ArcSketch.Kind            = SketcherDrawingCategory::Line;
            ArcSketch.GridPlane       = 2;
            ArcSketch.ClosedCondition = false;
            ArcSketch.Points          = { { -2.4, -0.5 }, { -2.4, 0.2 }, { -1.4, 0.25 }, { -0.8, 0.9 }, { 1.2, 0.9 }, { 1.8, 0.25 }, { 1.8, -0.5 }, { 0.8, -0.5 } };
            (void)DrawSketcherSketch(ArcCarDoc, ArcSketch);
            const std::string ArcSketchLabel = ArcCarDoc.Tree.Steps.back().Label;
            // Close bottom with a CircularArc wheel well from (0.8, -0.5) over (0.0, -0.05) to (-0.8, -0.5), then line to (-2.4, -0.5)
            SketcherDrawingRequest WheelArcReq {};
            WheelArcReq.Kind      = SketcherDrawingCategory::Arc;
            WheelArcReq.GridPlane = 2;
            WheelArcReq.Points    = { { 0.8, -0.5 }, { -0.8, -0.5 }, { 0.0, -0.05 } };
            (void)DrawSketcherSketch(ArcCarDoc, WheelArcReq);
            const std::string WheelArcLabel = ArcCarDoc.Tree.Steps.back().Label;
            SketcherDrawingRequest FloorLineReq {};
            FloorLineReq.Kind      = SketcherDrawingCategory::Line;
            FloorLineReq.GridPlane = 2;
            FloorLineReq.Points    = { { -0.8, -0.5 }, { -2.4, -0.5 } };
            (void)DrawSketcherSketch(ArcCarDoc, FloorLineReq);
            const std::string FloorLineLabel = ArcCarDoc.Tree.Steps.back().Label;
            const auto JoinedOut = JoinSketcherSketches(ArcCarDoc, { ArcSketchLabel, WheelArcLabel, FloorLineLabel });
            const std::string JoinedLabel = ArcCarDoc.Tree.Steps.back().Label;
            const auto ArcExOut = ExtrudeSketcherSketch(ArcCarDoc, JoinedLabel, 1.5, FeatureCombination::New);
            const SketcherScene ArcCarScene = BuildSketcherScene(ArcCarDoc);
            const std::string ArcHoodVert  = FindVertexByPosition(ArcCarScene, { 0.0, -1.4, 0.25 });
            const std::string ArcWheelVert = FindVertexByPosition(ArcCarScene, { 0.0, -0.8, -0.5 });

            // Move hood vertex in YZ and in X while cap has a CircularArc wheel well
            SketcherDocument ArcHoodMoveDoc = CloneSketcherDocument(ArcCarDoc);
            ArcHoodMoveDoc.Selection = {};
            ArcHoodMoveDoc.Selection.Vertices = { ArcHoodVert };
            const auto ArcHoodYZ = ReshapeSketcherSelection(ArcHoodMoveDoc, TranslationMap({ 0.0, 0.12, 0.15 }), "Move");
            const auto ArcHoodX  = ArcHoodYZ.AcceptedCondition
                                 ? ReshapeSketcherSelection(ArcHoodMoveDoc, TranslationMap({ -0.18, 0.0, 0.0 }), "Move")
                                 : SketcherCommandOutcome{};

            // Move wheel-arch endpoint vertex (incident to CircularArc + CylinderSurface!) in-plane and off-plane
            SketcherDocument ArcWheelMoveDoc = CloneSketcherDocument(ArcCarDoc);
            ArcWheelMoveDoc.Selection = {};
            ArcWheelMoveDoc.Selection.Vertices = { ArcWheelVert };
            const auto ArcWheelInPlane  = ReshapeSketcherSelection(ArcWheelMoveDoc, TranslationMap({ 0.0, -0.10, 0.0 }), "Move");
            const auto ArcWheelOffPlane = ArcWheelInPlane.AcceptedCondition
                                        ? ReshapeSketcherSelection(ArcWheelMoveDoc, TranslationMap({ -0.12, 0.0, 0.0 }), "Move")
                                        : SketcherCommandOutcome{};

            if (!VerifyCheck(JoinedOut.AcceptedCondition && ArcExOut.AcceptedCondition &&
                             ArcHoodYZ.AcceptedCondition && ArcHoodX.AcceptedCondition && ArcHoodMoveDoc.Summary.SoundCondition &&
                             ArcWheelInPlane.AcceptedCondition && ArcWheelOffPlane.AcceptedCondition && ArcWheelMoveDoc.Summary.SoundCondition,
                             "Vertices on N-gon cap with CircularArc wheel well & CylinderSurface reshape cleanly in YZ and X"))
            {
                std::fprintf(stderr, "  ArcHoodYZ='%s' ArcHoodX='%s' ArcWheelIn='%s' ArcWheelOff='%s'\n",
                             ArcHoodYZ.Refusal.c_str(), ArcHoodX.Refusal.c_str(),
                             ArcWheelInPlane.Refusal.c_str(), ArcWheelOffPlane.Refusal.c_str());
                return 1;
            }
            ++Passed;
        }
    }

    // 10. Degree-4 vertex blends on beveled polygonal pockets & bosses (Multi-Face Cap + Degree-4 Mitre)
    {
        const auto VerifyManifoldSolid = [](const SketcherDocument& Candidate) -> bool
        {
            if (!Candidate.Partition || Candidate.Regeneration.Bodies.empty() || !Candidate.Summary.SoundCondition) return false;
            const auto& Body = Candidate.Regeneration.Bodies.front();
            const auto& Part = *Candidate.Partition;
            if (!AuditBody(Part, Body.Body).Findings.empty()) return false;
            std::map<uint32_t, int> EdgeRadialCounts;
            for (FaceToken Face : Part.CollectInventory(Body.Body).Faces)
            {
                for (LoopToken Loop : Part.Faces.Resolve(Face).Loops)
                {
                    const auto& LoopRec = Part.Loops.Resolve(Loop);
                    FinToken    Fin     = LoopRec.FirstFin;
                    for (int Guard = 0; Guard < 256 && Fin.PresentCondition(); ++Guard)
                    {
                        const auto& FinRec = Part.Fins.Resolve(Fin);
                        EdgeRadialCounts[FinRec.Edge.Slot]++;
                        Fin = FinRec.Successor;
                        if (Fin == LoopRec.FirstFin) break;
                    }
                }
            }
            for (const auto& [EdgeSlot, Count] : EdgeRadialCounts)
                if (Count != 2) return false;
            return true;
        };

        const auto ConstructPolygonalFeature = [](int SideCount, bool OutwardBoss, BlendProfile EndShape, double EndSize) -> SketcherDocument
        {
            SketcherDocument FeatureDoc = CreateSketcherDocument("PolygonalFeature");
            SketcherConstructRequest BlockReq {};
            BlockReq.Kind     = SketcherConstructCategory::Block;
            BlockReq.Position = { -2.0, -2.0, 0.0 };
            BlockReq.Width    = 4.0;
            BlockReq.Depth    = 4.0;
            BlockReq.Height   = 2.0;
            (void)ConstructSketcherPrimitive(FeatureDoc, BlockReq);
            const std::string BodyKey = FeatureDoc.Regeneration.Bodies.front().Key;

            SketcherDrawingRequest PolygonReq {};
            PolygonReq.Kind            = SketcherDrawingCategory::Polygon;
            PolygonReq.Sides           = SideCount;
            PolygonReq.CentreCondition = true;
            PolygonReq.FillCondition   = true;
            PolygonReq.PlaneCondition  = true;
            PolygonReq.Plane           = OrthonormalBasis{};
            PolygonReq.Plane.Origin    = { 0.0, 0.0, 2.0 };
            PolygonReq.Points          = { { 0.0, 0.0 }, { 0.8, 0.0 } };
            (void)DrawSketcherSketch(FeatureDoc, PolygonReq);
            const std::string SketchLabel = FeatureDoc.Tree.Steps.back().Label;
            (void)ImprintSketcherSketch(FeatureDoc, SketchLabel, BodyKey);

            const auto LocatePolygonalFace = [&](double TargetZ) -> std::string
            {
                const auto& Body = FeatureDoc.Regeneration.Bodies.front();
                const auto& Part = *FeatureDoc.Partition;
                for (FaceToken Face : Part.CollectInventory(Body.Body).Faces)
                {
                    const auto& FaceRec = Part.Faces.Resolve(Face);
                    if (FaceRec.Loops.size() != 1) continue;
                    const auto& LoopRec = Part.Loops.Resolve(FaceRec.Loops.front());
                    FinToken    Fin     = LoopRec.FirstFin;
                    int         Count   = 0;
                    double      SumZ    = 0.0;
                    for (int Guard = 0; Guard < 64 && Fin.PresentCondition(); ++Guard)
                    {
                        const auto& FinRec = Part.Fins.Resolve(Fin);
                        SumZ += Part.Vertices.Resolve(Part.Edges.Resolve(FinRec.Edge).Start).Position.Z;
                        ++Count;
                        Fin = FinRec.Successor;
                        if (Fin == LoopRec.FirstFin) break;
                    }
                    if (Count == SideCount && std::fabs(SumZ / Count - TargetZ) < 1.0e-3)
                    {
                        if (const auto Found = Body.Naming.Faces.find(Face.Slot); Found != Body.Naming.Faces.end())
                            return Found->second;
                    }
                }
                return {};
            };

            const std::string ImprintedFace = LocatePolygonalFace(2.0);
            FeatureDoc.Selection            = {};
            FeatureDoc.Selection.Faces      = { ImprintedFace };
            FeatureDoc.Selection.LastKind   = SketcherSelectionCategory::Face;
            FeatureDoc.Selection.PickedBody = BodyKey;
            (void)ChangeSketcherSelection(FeatureDoc, FaceOperationCategory::Extrude, OutwardBoss ? 0.8 : -0.8);

            const std::string EndFace = LocatePolygonalFace(OutwardBoss ? 2.8 : 1.2);
            std::vector<std::string> EndLoopEdges;
            {
                const auto& Body = FeatureDoc.Regeneration.Bodies.front();
                const auto& Part = *FeatureDoc.Partition;
                for (const auto& [Slot, Name] : Body.Naming.Faces)
                {
                    if (Name != EndFace) continue;
                    const auto& LoopRec = Part.Loops.Resolve(Part.Faces.Resolve(Part.Faces.TokenAt(Slot)).Loops.front());
                    FinToken    Fin     = LoopRec.FirstFin;
                    for (int Guard = 0; Guard < 64 && Fin.PresentCondition(); ++Guard)
                    {
                        const auto& FinRec = Part.Fins.Resolve(Fin);
                        if (const auto Found = Body.Naming.Edges.find(FinRec.Edge.Slot); Found != Body.Naming.Edges.end())
                            EndLoopEdges.push_back(Found->second);
                        Fin = FinRec.Successor;
                        if (Fin == LoopRec.FirstFin) break;
                    }
                    break;
                }
            }
            FeatureDoc.Selection            = {};
            FeatureDoc.Selection.Edges      = EndLoopEdges;
            FeatureDoc.Selection.LastKind   = SketcherSelectionCategory::Edge;
            FeatureDoc.Selection.PickedBody = BodyKey;
            SketcherBlendRequest EndReq {};
            EndReq.Shape = EndShape;
            EndReq.Size  = EndSize;
            (void)BlendSketcherSelection(FeatureDoc, EndReq);
            return FeatureDoc;
        };

        const auto VerifyAllFeatureRingsLoops = [&](const SketcherDocument& FeatureDoc, const char* Label) -> bool
        {
            if (!VerifyManifoldSolid(FeatureDoc)) return false;
            const auto& Body = FeatureDoc.Regeneration.Bodies.front();
            const auto& Part = *FeatureDoc.Partition;
            std::set<std::vector<std::string>> TestedSets;
            int VerifiedCount = 0;
            for (EdgeToken Edge : Part.CollectInventory(Body.Body).Edges)
            {
                const auto Found = Body.Naming.Edges.find(Edge.Slot);
                if (Found == Body.Naming.Edges.end()) continue;
                const auto& EdgeRec = Part.Edges.Resolve(Edge);
                const Vector3 P0    = Part.Vertices.Resolve(EdgeRec.Start).Position;
                const Vector3 P1    = Part.Vertices.Resolve(EdgeRec.End).Position;
                if (std::hypot(0.5 * (P0.X + P1.X), 0.5 * (P0.Y + P1.Y)) > 1.2) continue;

                for (const bool RingMode : { true, false })
                {
                    auto EdgeSet = RingMode ? SketcherEdgeRing(FeatureDoc, Found->second, Body.Key)
                                            : SketcherEdgeLoop(FeatureDoc, Found->second, Body.Key);
                    if (EdgeSet.size() < 2) continue;
                    auto SortedKey = EdgeSet;
                    std::sort(SortedKey.begin(), SortedKey.end());
                    if (!TestedSets.insert(SortedKey).second) continue;

                    for (const BlendProfile Profile : { BlendProfile::Fillet, BlendProfile::Chamfer })
                    {
                        for (const double Size : { 0.02, 0.05 })
                        {
                            SketcherDocument Candidate = CloneSketcherDocument(FeatureDoc);
                            Candidate.Selection            = {};
                            Candidate.Selection.Edges      = EdgeSet;
                            Candidate.Selection.LastKind   = SketcherSelectionCategory::Edge;
                            Candidate.Selection.PickedBody = Body.Key;
                            SketcherBlendRequest Req {};
                            Req.Shape = Profile;
                            Req.Size  = Size;
                            const auto Outcome = BlendSketcherSelection(Candidate, Req);
                            if (!Outcome.AcceptedCondition || !VerifyManifoldSolid(Candidate))
                            {
                                std::fprintf(stderr, "  [%s] %s %s count=%zu size=%.2f failed: %s\n",
                                             Label, RingMode ? "Ring" : "Loop",
                                             Profile == BlendProfile::Fillet ? "Fillet" : "Chamfer",
                                             EdgeSet.size(), Size, Outcome.Refusal.c_str());
                                return false;
                            }
                            ++VerifiedCount;
                        }
                    }
                }
            }
            return VerifiedCount >= 40;
        };

        // 10a. Concave pentagon pocket + end chamfer: all edge rings & edge loops (fillet + chamfer) + variable-size fillet
        const SketcherDocument PentagonPocket = ConstructPolygonalFeature(5, false, BlendProfile::Chamfer, 0.15);
        if (!VerifyCheck(VerifyAllFeatureRingsLoops(PentagonPocket, "PentagonPocketChamfer"),
                         "Concave pentagon pocket with end chamfer blends all edge rings and loops cleanly"))
            return 1;
        {
            const auto& Body = PentagonPocket.Regeneration.Bodies.front();
            const auto& Part = *PentagonPocket.Partition;
            std::string VerticalEdge;
            for (EdgeToken Edge : Part.CollectInventory(Body.Body).Edges)
            {
                const auto& EdgeRec = Part.Edges.Resolve(Edge);
                const Vector3 P0    = Part.Vertices.Resolve(EdgeRec.Start).Position;
                const Vector3 P1    = Part.Vertices.Resolve(EdgeRec.End).Position;
                if (std::hypot(P0.X, P0.Y) < 1.2 && std::hypot(P0.X - P1.X, P0.Y - P1.Y) < 1.0e-4 && std::fabs(P0.Z - P1.Z) > 0.2)
                {
                    VerticalEdge = Body.Naming.Edges.at(Edge.Slot);
                    break;
                }
            }
            auto VerticalRing = SketcherEdgeRing(PentagonPocket, VerticalEdge, Body.Key);
            SketcherDocument VariableDoc = CloneSketcherDocument(PentagonPocket);
            VariableDoc.Selection            = {};
            VariableDoc.Selection.Edges      = VerticalRing;
            VariableDoc.Selection.LastKind   = SketcherSelectionCategory::Edge;
            VariableDoc.Selection.PickedBody = Body.Key;
            SketcherBlendRequest VariableReq {};
            VariableReq.Shape   = BlendProfile::Fillet;
            VariableReq.Size    = 0.03;
            VariableReq.EndSize = 0.07;
            const auto VariableOut = BlendSketcherSelection(VariableDoc, VariableReq);
            if (!VerifyCheck(VerticalRing.size() == 5 && VariableOut.AcceptedCondition && VerifyManifoldSolid(VariableDoc),
                             "Variable-size fillet along pentagon vertical wall edge ring resolves degree-4 multi-face caps"))
                return 1;
        }

        // 10b. Concave pentagon pocket + end fillet: vertical wall ring (fillet & chamfer) + curved corner ring G1 propagation
        {
            const SketcherDocument FilletedPocket = ConstructPolygonalFeature(5, false, BlendProfile::Fillet, 0.15);
            const auto& Body = FilletedPocket.Regeneration.Bodies.front();
            const auto& Part = *FilletedPocket.Partition;
            std::string VerticalEdge;
            std::string CurvedCornerEdge;
            for (EdgeToken Edge : Part.CollectInventory(Body.Body).Edges)
            {
                const auto& EdgeRec = Part.Edges.Resolve(Edge);
                const Vector3 P0    = Part.Vertices.Resolve(EdgeRec.Start).Position;
                const Vector3 P1    = Part.Vertices.Resolve(EdgeRec.End).Position;
                if (std::hypot(0.5 * (P0.X + P1.X), 0.5 * (P0.Y + P1.Y)) > 1.2) continue;
                if (std::hypot(P0.X - P1.X, P0.Y - P1.Y) < 1.0e-4 && std::fabs(P0.Z - P1.Z) > 0.2)
                    VerticalEdge = Body.Naming.Edges.at(Edge.Slot);
                else if (std::min(P0.Z, P1.Z) < 1.25 && std::max(P0.Z, P1.Z) > 1.30)
                    CurvedCornerEdge = Body.Naming.Edges.at(Edge.Slot);
            }
            const auto VerticalRing = SketcherEdgeRing(FilletedPocket, VerticalEdge, Body.Key);
            const auto CurvedRing   = SketcherEdgeRing(FilletedPocket, CurvedCornerEdge, Body.Key);

            SketcherDocument VerticalFilletDoc = CloneSketcherDocument(FilletedPocket);
            VerticalFilletDoc.Selection            = {};
            VerticalFilletDoc.Selection.Edges      = VerticalRing;
            VerticalFilletDoc.Selection.LastKind   = SketcherSelectionCategory::Edge;
            VerticalFilletDoc.Selection.PickedBody = Body.Key;
            SketcherBlendRequest VertFilletReq {};
            VertFilletReq.Shape = BlendProfile::Fillet;
            VertFilletReq.Size  = 0.05;
            const auto VertFilletOut = BlendSketcherSelection(VerticalFilletDoc, VertFilletReq);

            SketcherDocument VerticalChamferDoc = CloneSketcherDocument(FilletedPocket);
            VerticalChamferDoc.Selection            = {};
            VerticalChamferDoc.Selection.Edges      = VerticalRing;
            VerticalChamferDoc.Selection.LastKind   = SketcherSelectionCategory::Edge;
            VerticalChamferDoc.Selection.PickedBody = Body.Key;
            SketcherBlendRequest VertChamferReq {};
            VertChamferReq.Shape = BlendProfile::Chamfer;
            VertChamferReq.Size  = 0.05;
            const auto VertChamferOut = BlendSketcherSelection(VerticalChamferDoc, VertChamferReq);

            SketcherDocument CurvedFilletDoc = CloneSketcherDocument(FilletedPocket);
            CurvedFilletDoc.Selection            = {};
            CurvedFilletDoc.Selection.Edges      = CurvedRing;
            CurvedFilletDoc.Selection.LastKind   = SketcherSelectionCategory::Edge;
            CurvedFilletDoc.Selection.PickedBody = Body.Key;
            SketcherBlendRequest CurvedReq {};
            CurvedReq.Shape = BlendProfile::Fillet;
            CurvedReq.Size  = 0.05;
            const auto CurvedOut = BlendSketcherSelection(CurvedFilletDoc, CurvedReq);

            if (!VerifyCheck(VerticalRing.size() == 5 && CurvedRing.size() == 5 &&
                             VertFilletOut.AcceptedCondition && VerifyManifoldSolid(VerticalFilletDoc) &&
                             VertChamferOut.AcceptedCondition && VerifyManifoldSolid(VerticalChamferDoc) &&
                             CurvedOut.AcceptedCondition && VerifyManifoldSolid(CurvedFilletDoc),
                             "Filleted pentagon pocket blends vertical edge ring (fillet/chamfer) and curved corner ring via G1 mitre"))
                return 1;
        }

        // 10c. Protruding convex pentagon boss + end chamfer & concave hexagon pocket + end chamfer
        const SketcherDocument ConvexBoss = ConstructPolygonalFeature(5, true, BlendProfile::Chamfer, 0.15);
        if (!VerifyCheck(VerifyAllFeatureRingsLoops(ConvexBoss, "ConvexPentagonBoss"),
                         "Convex protruding pentagon boss with end chamfer blends all edge rings and loops cleanly"))
            return 1;

        const SketcherDocument HexagonPocket = ConstructPolygonalFeature(6, false, BlendProfile::Chamfer, 0.15);
        if (!VerifyCheck(VerifyAllFeatureRingsLoops(HexagonPocket, "HexagonPocketChamfer"),
                         "Concave hexagon pocket with end chamfer blends all edge rings and loops cleanly"))
            return 1;

        ++Passed;
    }

    std::printf("[SketcherDocumentValidation] Passed=%u Failed=0\n", Passed);
    return 0;
}
