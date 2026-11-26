/*====================================================================================================================================
                                                         SOLIDARCOUTLINER.CPP
====================================================================================================================================*/
// 🧩 Outliner panel, a 1:1 port of the HTML Sketcher Outliner (see header). Measurements are WorkspacePanel.css values;
//    each block names its selector so the port can be checked line by line against the style sheet.

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherOutliner.h"

#include "Engine/Components/RetainedCanvasCache.h"
#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherInterface.h"
#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherStyleSheet.h"

#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherUiKit.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace Frontier::Workspaces::Sketcher
{

using namespace Frontier::GeometrySpace::Parametric;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr uint32_t TextInk       = 0xeeeeee;                                                 // :root color
constexpr uint32_t MutedInk      = 0x858585;                                                 // --muted
constexpr uint32_t AmberInk      = 0xe5b369;                                                 // --amber (bodies)
constexpr uint32_t CyanInk       = 0x84bdc2;                                                 // --cyan (curves)
constexpr uint32_t BlueInk       = 0x7ba9d9;                                                 // --blue (surfaces)
constexpr uint32_t VioletInk     = 0xb8a3e0;                                                 // Image planes (reference pictures)
constexpr uint32_t EmeraldInk    = 0x6fd6b8;                                                 // GPU Blender reference surfaces
constexpr uint32_t ActiveInk     = 0x33ff73;                                                 // .scene-row.active-selection
constexpr uint32_t SelectedInk   = 0x4099ff;                                                 // .scene-row.selected:not(.active-selection)
constexpr uint32_t HoverFill     = 0x292929;                                                 // button:hover
constexpr float    RowHeight     = 39.0f;                                                    // [px] - .scene-row
constexpr float    CompactHeight = 32.0f;                                                    // [px] - .compact .scene-row

//------------------------------------------------------------------------------------------------------------------------
//                                                    HELPERS
//------------------------------------------------------------------------------------------------------------------------

ColorQuad Ink(uint32_t Rgb, float Opacity = 1.0f)
{
    return SketcherInk(Rgb, Opacity);
}

ColorQuad CategoryInk(SketcherFigureCategory Category)
{
    switch (Category)
    {
    case SketcherFigureCategory::Curve:     return Ink(CyanInk);                             // .colour-0
    case SketcherFigureCategory::Surface:   return Ink(BlueInk);                             // .colour-1
    case SketcherFigureCategory::Image:     return Ink(VioletInk);
    case SketcherFigureCategory::Reference: return Ink(EmeraldInk);
    default:                                return Ink(AmberInk);                            // .colour-2
    }
}

const char* CategoryTag(SketcherFigureCategory Category)
{
    static const char* const Tags[] = { "CURVE", "SURFACE", "BODY", "IMAGE", "REF" };
    return Tags[static_cast<int>(Category)];
}

std::string Lowered(std::string Text)
{
    for (char& Character : Text) Character = char(std::tolower(static_cast<unsigned char>(Character)));
    return Text;
}

std::string CaptionOfStep(const SketcherDocument& Document, const std::string& Label)
{
    const auto Found = Document.Captions.find(Label);
    return Found != Document.Captions.end() && !Found->second.empty() ? Found->second : Label;
}

// WorkspacePanel.js FigureIcon: the glyph of a sketch follows its tool/caption and curves.
IconSymbol SketchIcon(const SketchModel& Sketch, const std::string& Caption = {})
{
    auto StartsWithCI = [&](const char* Prefix) -> bool
    {
        size_t I = 0;
        for (; Prefix[I] != '\0' && I < Caption.size(); ++I)
        {
            if (std::tolower(static_cast<unsigned char>(Caption[I])) != std::tolower(static_cast<unsigned char>(Prefix[I]))) return false;
        }
        return Prefix[I] == '\0';
    };
    if (StartsWithCI("Polyline"))  return IconSymbol::SketcherPolyline;
    if (StartsWithCI("Line"))      return IconSymbol::SketcherEdge;
    if (StartsWithCI("Slot"))      return IconSymbol::SketcherSlot;
    if (StartsWithCI("Polygon"))   return IconSymbol::SketcherPolygon;
    if (StartsWithCI("Rectangle")) return IconSymbol::SketcherRectangle;
    if (StartsWithCI("Centre rect")) return IconSymbol::SketcherCentrerect;
    if (StartsWithCI("Circle"))    return IconSymbol::SketcherCircle;
    if (StartsWithCI("Arc"))       return IconSymbol::SketcherArc;
    if (StartsWithCI("Hermite"))   return IconSymbol::SketcherHermite;
    if (StartsWithCI("Bezier"))    return IconSymbol::SketcherBezier;
    if (StartsWithCI("NURBS"))     return IconSymbol::SketcherNurbs;
    if (StartsWithCI("Cubic") || StartsWithCI("Spline")) return IconSymbol::SketcherCurve;

    int Lines = 0;
    int Arcs  = 0;
    for (const SketchCurve& Curve : Sketch.Curves)
    {
        if (Curve.Construction) continue;
        switch (Curve.Kind)
        {
        case SketchCurveCategory::Circle: return IconSymbol::SketcherCircle;
        case SketchCurveCategory::Arc:    ++Arcs; break;
        case SketchCurveCategory::Spline:
            switch (Curve.Form)
            {
            case SketchSplineForm::Hermite: return IconSymbol::SketcherHermite;
            case SketchSplineForm::Bezier:  return IconSymbol::SketcherBezier;
            case SketchSplineForm::Nurbs:   return IconSymbol::SketcherNurbs;
            default:                        return IconSymbol::SketcherCurve;
            }
        case SketchCurveCategory::Line: ++Lines; break;
        }
    }
    if (Arcs == 2 && Lines == 2) return IconSymbol::SketcherSlot;
    if (Arcs > 0) return IconSymbol::SketcherArc;
    if (Lines == 4 && Sketch.Points.size() == 4) return IconSymbol::SketcherRectangle;
    return Lines > 1 ? IconSymbol::SketcherPolyline : IconSymbol::SketcherEdge;
}

IconSymbol SpaceCurveIcon(const SketcherDocument& Document, const FeatureStep& Step, const std::string& Caption)
{
    auto StartsWithCI = [&](const char* Prefix) -> bool
    {
        size_t I = 0;
        for (; Prefix[I] != '\0' && I < Caption.size(); ++I)
        {
            if (std::tolower(static_cast<unsigned char>(Caption[I])) != std::tolower(static_cast<unsigned char>(Prefix[I]))) return false;
        }
        return Prefix[I] == '\0';
    };
    if (StartsWithCI("Polyline")) return IconSymbol::SketcherPolyline;
    if (StartsWithCI("Line"))     return IconSymbol::SketcherEdge;
    if (StartsWithCI("Hermite"))  return IconSymbol::SketcherHermite;
    if (StartsWithCI("Bezier"))   return IconSymbol::SketcherBezier;
    if (StartsWithCI("NURBS"))    return IconSymbol::SketcherNurbs;
    if (StartsWithCI("Spline") || StartsWithCI("Cubic")) return IconSymbol::SketcherCurve;
    if (StartsWithCI("Arc"))      return IconSymbol::SketcherArc;
    if (StartsWithCI("Circle"))   return IconSymbol::SketcherCircle;
    if (StartsWithCI("Rectangle")) return IconSymbol::SketcherRectangle;
    if (StartsWithCI("Polygon"))  return IconSymbol::SketcherPolygon;
    if (StartsWithCI("Slot"))     return IconSymbol::SketcherSlot;

    const auto FitIt = Document.Regeneration.Curves.find(Step.Label);
    if (FitIt != Document.Regeneration.Curves.end() && FitIt->second.Curve.Definition)
    {
        const auto& Def = *FitIt->second.Curve.Definition;
        if (Def.Degree <= 1)
        {
            return Def.Poles.size() <= 2 ? IconSymbol::SketcherEdge : IconSymbol::SketcherPolyline;
        }
    }
    return IconSymbol::SketcherNurbs;
}

// .icon-button: 28 px circle, #888 glyph; hover #292929 / #fff.
bool IconButton(SketcherWorkspaceStatus& State, SketcherSheet& Sheet, const std::string& Key, const PlaneExtent& Extent, IconSymbol Symbol,
                float GlyphSize, ColorQuad Border = { 0.0f, 0.0f, 0.0f, 0.0f })
{
    const SketcherPress Press = PressSketcherRegion(State.Widgets, Key, Extent);
    Sheet.Box(Extent, Press.HoveredCondition ? Ink(HoverFill) : ColorQuad{ 0.0f, 0.0f, 0.0f, 0.0f }, 0.5f * Extent.Width(), Border);
    Sheet.Icon(Symbol, 0.5f * (Extent.MinimumX + Extent.MaximumX - GlyphSize), 0.5f * (Extent.MinimumY + Extent.MaximumY - GlyphSize), GlyphSize,
               Press.HoveredCondition ? Ink(0xffffff) : Ink(0x888888));
    return Press.ClickedCondition;
}

// The lock of a row (no Sketcher icon carries one): a 9 × 6 body under its shackle; unlocked, the shackle lifts off
// its left leg.
void LockGlyph(SketcherSheet& Sheet, float CentreX, float CentreY, bool LockedCondition, ColorQuad Ink)
{
    Sheet.Box(Spanning(CentreX - 4.5f, CentreY - 0.5f, 9.0f, 6.0f), Ink, 1.5f);
    const float Lift = LockedCondition ? 0.0f : 2.0f;
    const float Top  = CentreY - 3.5f - Lift;                                                // Arc centre height
    constexpr int Steps = 8;
    for (int Index = 0; Index < Steps; ++Index)
    {
        const float From = 3.14159265f * (1.0f + float(Index) / Steps), To = 3.14159265f * (1.0f + float(Index + 1) / Steps);
        Sheet.Line(CentreX + 3.0f * std::cos(From), Top + 3.0f * std::sin(From), CentreX + 3.0f * std::cos(To), Top + 3.0f * std::sin(To), Ink, 1.2f);
    }
    Sheet.Line(CentreX + 3.0f, Top, CentreX + 3.0f, CentreY - 0.5f, Ink, 1.2f);
    Sheet.Line(CentreX - 3.0f, Top, CentreX - 3.0f, LockedCondition ? CentreY - 0.5f : Top + 1.0f, Ink, 1.2f);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::vector<SketcherFigure> CollectSketcherFigures(const SketcherWorkspaceStatus& State)
{
    const SketcherDocument&  Document = State.Document;
    const SketcherSelection& Picked   = Document.Selection;
    std::vector<SketcherFigure> Figures;
    for (const SketcherBodyFigures& Body : Document.Summary.Bodies)
    {
        SketcherFigure Figure;
        Figure.Key             = Body.Key;
        Figure.Name            = Body.Caption;
        Figure.Category        = Body.SheetCondition ? SketcherFigureCategory::Surface : SketcherFigureCategory::Body;
        Figure.Source          = SketcherFigureSource::Body;
        Figure.Icon            = Body.SheetCondition ? IconSymbol::SketcherSurface : IconSymbol::SketcherBody;
        Figure.HiddenCondition     = Document.Appearance.HiddenBodies.contains(Body.Key);
        Figure.LockedCondition     = SketcherLockedCondition(Document, Body.Key);
        Figure.LiveCutterCondition = Document.Appearance.LiveCutters.contains(Body.Key);
        const auto Order = std::find(Picked.Bodies.begin(), Picked.Bodies.end(), Body.Key);
        Figure.Rank = Order == Picked.Bodies.end() ? -1 : int(Order - Picked.Bodies.begin());
        Figure.SelectedCondition = Figure.Rank >= 0;
        for (const std::string& Face : Picked.Faces)
            if (!Figure.SelectedCondition && SketcherBodyOfName(Document, SketcherSelectionCategory::Face, Face) == Body.Key) Figure.SelectedCondition = true;
        for (const std::string& Edge : Picked.Edges)
            if (!Figure.SelectedCondition && SketcherBodyOfName(Document, SketcherSelectionCategory::Edge, Edge) == Body.Key) Figure.SelectedCondition = true;
        Figures.push_back(std::move(Figure));
    }
    for (const FeatureStep& Step : Document.Tree.Steps)
    {
        if (Document.Appearance.RemovedCurves.contains(Step.Label)) continue;                // Deleted (X), kept for a later step
        SketcherFigure Figure;
        Figure.Key      = Step.Label;
        Figure.Name     = CaptionOfStep(Document, Step.Label);
        Figure.Category = SketcherFigureCategory::Curve;
        Figure.HiddenCondition = SketcherCurveHiddenCondition(Document, Step.Label);
        Figure.LockedCondition = SketcherLockedCondition(Document, Step.Label);
        if (Step.Kind == FeatureStepCategory::Sketch)
        {
            // Extruded / revolved sketches stay listed, so their leftover lines can be hidden or locked too.
            const auto Solved = Document.Regeneration.Sketches.find(Step.Label);
            const std::string RawCaption = Figure.Name;
            if (SketcherSketchConsumedCondition(Document, Step.Label)) Figure.Name += " sketch";
            const auto Order         = std::find(Picked.Curves.begin(), Picked.Curves.end(), Step.Label);
            Figure.Source            = SketcherFigureSource::Sketch;
            Figure.Icon              = SketchIcon(Solved != Document.Regeneration.Sketches.end() ? Solved->second : Step.Sketch, RawCaption);
            Figure.Rank              = Order == Picked.Curves.end() ? -1 : int(Order - Picked.Curves.begin());
            Figure.SelectedCondition = Figure.Rank >= 0
                                    || (State.FocusedStep == Step.Label && Picked.Bodies.empty() && Picked.Faces.empty() && Picked.Edges.empty());
            if (Figure.SelectedCondition && Figure.Rank < 0) Figure.Rank = 0;
        }
        else if (Step.Kind == FeatureStepCategory::SpaceCurve)
        {
            const auto Order = std::find(Picked.Curves.begin(), Picked.Curves.end(), Step.Label);
            Figure.Source            = SketcherFigureSource::SpaceCurve;
            Figure.Icon              = SpaceCurveIcon(Document, Step, Figure.Name);
            Figure.Rank              = Order == Picked.Curves.end() ? -1 : int(Order - Picked.Curves.begin());
            Figure.SelectedCondition = Figure.Rank >= 0;
        }
        else
        {
            continue;
        }
        Figures.push_back(std::move(Figure));
    }
    // Image planes: the eye shows / hides the picture, the lock freezes its placement, a pick opens its Inspector rows.
    for (const SketcherImagePlane& Plane : Document.ImagePlanes)
    {
        SketcherFigure Figure;
        Figure.Key               = Plane.Name;
        Figure.Name              = Plane.Name;
        Figure.Category          = SketcherFigureCategory::Image;
        Figure.Source            = SketcherFigureSource::Image;
        Figure.Icon              = IconSymbol::SketcherImage;
        Figure.HiddenCondition   = !Plane.VisibleCondition || !State.ImagePlanesEnabled;
        Figure.LockedCondition   = Plane.LockCondition;
        Figure.SelectedCondition = State.FocusedImage == Plane.Name;
        Figure.Rank              = Figure.SelectedCondition ? 0 : -1;
        Figures.push_back(std::move(Figure));
    }
    // GPU-resident Blender reference surfaces (high-poly vehicle retopology guides)
    for (const SketcherReferenceSurface& Reference : Document.ReferenceSurfaces)
    {
        SketcherFigure Figure;
        Figure.Key               = Reference.Name;
        Figure.Name              = Reference.Name;
        Figure.Category          = SketcherFigureCategory::Reference;
        Figure.Source            = SketcherFigureSource::Reference;
        Figure.Icon              = IconSymbol::SketcherSurface;
        Figure.HiddenCondition   = !Reference.VisibleCondition;
        Figure.LockedCondition   = Reference.LockedCondition;
        Figure.SelectedCondition = State.FocusedReference == Reference.Name;
        Figure.Rank              = Figure.SelectedCondition ? 0 : -1;
        Figures.push_back(std::move(Figure));
    }
    return Figures;
}

namespace
{

std::atomic<uint64_t> GlobalOutlinerFigureHits     { 0ull };
std::atomic<uint64_t> GlobalOutlinerFigureRebuilds { 0ull };
std::atomic<uint64_t> GlobalOutlinerCanvasReplays  { 0ull };
std::atomic<uint64_t> GlobalOutlinerCanvasCaptures { 0ull };

[[nodiscard]] uint64_t MixString(uint64_t Seed, const std::string& Text) noexcept
{
    uint64_t H = 14695981039346656037ull;
    for (unsigned char Ch : Text)
    {
        H ^= static_cast<uint64_t>(Ch);
        H *= 1099511628211ull;
    }
    return RetainedCanvasCache::MixHash(Seed, H);
}

[[nodiscard]] uint64_t HashOutlinerFiguresState(const SketcherWorkspaceStatus& State) noexcept
{
    const SketcherDocument& Doc = State.Document;
    uint64_t Sig = RetainedCanvasCache::MixHash(0x5011DAC00711E401ull, reinterpret_cast<uintptr_t>(&State));
    Sig = RetainedCanvasCache::MixHash(Sig, Doc.Revision);
    Sig = RetainedCanvasCache::MixHash(Sig, Doc.ReferenceRevision);
    Sig = RetainedCanvasCache::MixHash(Sig, Doc.Regeneration.Bodies.size());
    Sig = RetainedCanvasCache::MixHash(Sig, Doc.Tree.Steps.size());
    Sig = RetainedCanvasCache::MixHash(Sig, Doc.ImagePlanes.size());
    Sig = RetainedCanvasCache::MixHash(Sig, Doc.ReferenceSurfaces.size());
    Sig = RetainedCanvasCache::MixHash(Sig, State.ImagePlanesEnabled ? 1ull : 0ull);
    Sig = MixString(Sig, State.FocusedStep);
    Sig = MixString(Sig, State.FocusedImage);
    Sig = MixString(Sig, State.FocusedReference);

    const SketcherSelection& Sel = Doc.Selection;
    Sig = RetainedCanvasCache::MixHash(Sig, Sel.Bodies.size());
    for (const std::string& S : Sel.Bodies) Sig = MixString(Sig, S);
    Sig = RetainedCanvasCache::MixHash(Sig, Sel.Faces.size());
    for (const std::string& S : Sel.Faces) Sig = MixString(Sig, S);
    Sig = RetainedCanvasCache::MixHash(Sig, Sel.Edges.size());
    for (const std::string& S : Sel.Edges) Sig = MixString(Sig, S);
    Sig = RetainedCanvasCache::MixHash(Sig, Sel.Curves.size());
    for (const std::string& S : Sel.Curves) Sig = MixString(Sig, S);

    const SketcherAppearance& App = Doc.Appearance;
    Sig = RetainedCanvasCache::MixHash(Sig, App.HiddenBodies.size());
    for (const std::string& S : App.HiddenBodies) Sig = MixString(Sig, S);
    Sig = RetainedCanvasCache::MixHash(Sig, App.HiddenCurves.size());
    for (const std::string& S : App.HiddenCurves) Sig = MixString(Sig, S);
    Sig = RetainedCanvasCache::MixHash(Sig, App.LockedFigures.size());
    for (const std::string& S : App.LockedFigures) Sig = MixString(Sig, S);
    Sig = RetainedCanvasCache::MixHash(Sig, App.RemovedCurves.size());
    for (const std::string& S : App.RemovedCurves) Sig = MixString(Sig, S);
    Sig = RetainedCanvasCache::MixHash(Sig, App.LiveCutters.size());
    for (const std::string& S : App.LiveCutters) Sig = MixString(Sig, S);
    Sig = RetainedCanvasCache::MixHash(Sig, Doc.Captions.size());
    for (const auto& [K, V] : Doc.Captions) { Sig = MixString(Sig, K); Sig = MixString(Sig, V); }
    for (const SketcherImagePlane& P : Doc.ImagePlanes)
    {
        Sig = MixString(Sig, P.Name);
        Sig = RetainedCanvasCache::MixHash(Sig, (P.VisibleCondition ? 1ull : 0ull) | (P.LockCondition ? 2ull : 0ull));
    }
    for (const SketcherReferenceSurface& R : Doc.ReferenceSurfaces)
    {
        Sig = MixString(Sig, R.Name);
        Sig = RetainedCanvasCache::MixHash(Sig, (R.VisibleCondition ? 1ull : 0ull) | (R.LockedCondition ? 2ull : 0ull));
    }
    return Sig == 0ull ? 1ull : Sig;
}

[[nodiscard]] const std::vector<SketcherFigure>& CollectSketcherFiguresCached(const SketcherWorkspaceStatus& State, uint64_t& OutFiguresSig)
{
    thread_local uint64_t                    CachedSig = 0ull;
    thread_local std::vector<SketcherFigure> CachedFigures {};
    const uint64_t Sig = HashOutlinerFiguresState(State);
    OutFiguresSig = Sig;
    if (Sig == CachedSig)
    {
        GlobalOutlinerFigureHits.fetch_add(1ull, std::memory_order_relaxed);
        return CachedFigures;
    }
    GlobalOutlinerFigureRebuilds.fetch_add(1ull, std::memory_order_relaxed);
    CachedFigures = CollectSketcherFigures(State);
    CachedSig     = Sig;
    return CachedFigures;
}

} // namespace

void QuerySketcherOutlinerCacheStats(
    uint64_t& OutFigureHits,
    uint64_t& OutFigureRebuilds,
    uint64_t& OutCanvasReplays,
    uint64_t& OutCanvasCaptures) noexcept
{
    OutFigureHits     = GlobalOutlinerFigureHits.load(std::memory_order_relaxed);
    OutFigureRebuilds = GlobalOutlinerFigureRebuilds.load(std::memory_order_relaxed);
    OutCanvasReplays  = GlobalOutlinerCanvasReplays.load(std::memory_order_relaxed);
    OutCanvasCaptures = GlobalOutlinerCanvasCaptures.load(std::memory_order_relaxed);
}

SketcherOutlinerIntent ConstructSketcherOutliner(SketcherWorkspaceStatus& State, const PlaneExtent& Panel)
{
    SketcherOutlinerIntent Intent;
    SketcherSheet          Sheet;
    State.Outliner.HoveredCondition = false;                                                 // Re-found below while drawn
    if (!Sheet.Begin()) return Intent;
    SketcherOutlinerMode& Outliner = State.Outliner;
    uint64_t FiguresSig = 0ull;
    const std::vector<SketcherFigure>& Figures = CollectSketcherFiguresCached(State, FiguresSig);
    const ColorQuad                   Clear    = { 0.0f, 0.0f, 0.0f, 0.0f };

    // .panel — border 1 px #ffffff08, --panel, radius 23; children sit in the content box.
    Sheet.Box(Panel, Ink(0x121212), SketcherStyleTokens::PanelRadius, SketcherWash(0x08));
    Sheet.Surface().PushClip(Panel);
    const float Left  = Panel.MinimumX + 1.0f;
    const float Top   = Panel.MinimumY + 1.0f;
    const float Width = Panel.Width() - 2.0f;
    const float Right = Left + Width;

    // .panel-heading — padding 20 19 16, min-height 64, gap 10: h2 18 px (−0.6 letter spacing), small 10 px, rows button.
    const float HeadingCentre = Top + 20.0f + 14.0f;
    float       Cursor        = Left + 19.0f;
    Cursor += Sheet.Text(Cursor, HeadingCentre, 18.0f, Ink(TextInk), "Outliner", SketcherWeight::Light, -0.6f) + 10.0f;
    char Count[64];
    std::snprintf(Count, sizeof(Count), "Scene · %d figures", int(Figures.size()));
    Sheet.Text(Cursor, HeadingCentre, 10.0f, Ink(MutedInk), Count);
    // ☰ opens the file menu (new / open / save / import / export / examples, then compact rows): the app header is gone.
    const PlaneExtent MenuButton = Spanning(Right - 19.0f - 28.0f, HeadingCentre - 14.0f, 28.0f, 28.0f);
    if (IconButton(State, Sheet, "OutlinerMenu", MenuButton, IconSymbol::SketcherRows, 17.0f))
    {
        State.FileMenuAnchor = { MenuButton.MinimumX, MenuButton.MaximumY + 6.0f };
        State.PopupRequest   = "FileMenu";
    }
    if (ImGui::IsMouseHoveringRect(ImVec2(MenuButton.MinimumX, MenuButton.MinimumY), ImVec2(MenuButton.MaximumX, MenuButton.MaximumY))
        && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        ImGui::SetTooltip("File: new, open, save, import, export, examples (Ctrl+N / O / S / I / E) · compact rows");
    float Y = Top + 64.0f;

    // .stat-tiles — 2 columns, gap 10, margin 0 12 12; tile 64 px, #1c1c1c, hairline, radius 18, padding 11 14.
    const float TileWidth = 0.5f * (Width - 24.0f - 10.0f);
    int Bodies = 0, Sketches = 0;
    for (const SketcherFigure& Figure : Figures)
    {
        Bodies   += Figure.Category == SketcherFigureCategory::Body;
        Sketches += Figure.Category == SketcherFigureCategory::Curve;
    }
    const struct { IconSymbol Icon; uint32_t Tint; const char* Label; int Value; } Tiles[2] = {
        { IconSymbol::SketcherBody, AmberInk, "Bodies", Bodies }, { IconSymbol::SketcherCurve, CyanInk, "Sketches", Sketches } };
    for (int Index = 0; Index < 2; ++Index)
    {
        const PlaneExtent Tile = Spanning(Left + 12.0f + float(Index) * (TileWidth + 10.0f), Y, TileWidth, 64.0f);
        Sheet.Box(Tile, Ink(0x1c1c1c), 18.0f, SketcherWash(0x0c));
        Sheet.Icon(Tiles[Index].Icon, Tile.MinimumX + 15.0f, Tile.MinimumY + 12.0f, 18.0f, Ink(Tiles[Index].Tint));
        Sheet.Text(Tile.MinimumX + 15.0f, Tile.MinimumY + 12.0f + 18.0f + 4.0f + 0.5f * 11.0f * SketcherStyleTokens::FontScale, 11.0f, Ink(0x939393),
                   Tiles[Index].Label);
        char Value[16];
        std::snprintf(Value, sizeof(Value), "%d", Tiles[Index].Value);
        Sheet.TextRight(Tile.MaximumX - 1.0f - 15.0f, Tile.MaximumY - 1.0f - 9.0f - 0.5f * 26.0f * SketcherStyleTokens::FontScale, 26.0f, Ink(TextInk), Value);
    }
    Y += 64.0f + 12.0f;

    // .search — 40 px pill, #222, #ffffff10 border, padding 0 12, gap 9: glyph 14, input 11 px #ccc, kbd 9 px #606060.
    const PlaneExtent Search = Spanning(Left + 12.0f, Y, Width - 24.0f, 40.0f);
    const float       SearchCentre = Y + 20.0f;
    Sheet.Box(Search, Ink(0x222222), 20.0f, SketcherWash(0x10));
    Sheet.Icon(IconSymbol::SketcherSearch, Search.MinimumX + 13.0f, SearchCentre - 7.0f, 14.0f, Ink(0x7c7c7c));
    const float KeyWidth = Sheet.TextRight(Search.MaximumX - 13.0f, SearchCentre, 9.0f, Ink(0x606060), "Ctrl F");
    {
        const float InputLeft  = Search.MinimumX + 13.0f + 14.0f + 9.0f;
        const float InputWidth = Search.MaximumX - 13.0f - KeyWidth - 9.0f - InputLeft;
        const float FontSize   = 11.0f * SketcherStyleTokens::FontScale;
        ImGui::PushFont(static_cast<ImFont*>(QuerySketcherTypefaces().Regular), FontSize);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));                // #ccc
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0x8b / 255.0f, 0x8b / 255.0f, 0x8b / 255.0f, 1.0f)); // dark-scheme placeholder
        const ImVec2 Restore = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(InputLeft, SearchCentre - 0.5f * FontSize));
        ImGui::SetNextItemWidth(InputWidth);
        if (Outliner.SearchFocusRequest)
        {
            ImGui::SetKeyboardFocusHere();
            Outliner.SearchFocusRequest = false;
        }
        ImGui::InputTextWithHint("##SceneSearch", "Search scene…", Outliner.Search.data(), Outliner.Search.size());
        State.Widgets.Record("SceneSearch", ImVec2(Search.MinimumX, Search.MinimumY), ImVec2(Search.MaximumX, Search.MaximumY));
        ImGui::SetCursorScreenPos(Restore);
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(2);
        ImGui::PopFont();
    }
    Y += 40.0f;

    // .filter-chips — margin 10 12 11, gap 5; chip 10 px, padding 5 9, radius 14, #ffffff0d border; wraps cleanly when narrow.
    Y += 10.0f;
    const float ChipHeight = 10.0f * SketcherStyleTokens::FontScale + 10.0f + 2.0f;
    const struct { const char* Label; int Filter; } Chips[] = { { "All", -1 }, { "Bodies", 2 }, { "Sketches", 0 }, { "Surfaces", 1 }, { "Images", 3 }, { "Refs", 4 } };
    Cursor = Left + 12.0f;
    const float ChipMaxRight = Right - 12.0f;
    for (const auto& Chip : Chips)
    {
        if (Chip.Filter == 3 && State.Document.ImagePlanes.empty()) continue;                // Only when the document has pictures
        if (Chip.Filter == 4 && State.Document.ReferenceSurfaces.empty()) continue;          // Only when the document has Blender references
        const bool  Active    = Outliner.Filter == Chip.Filter;
        const auto  ChipWgt   = Active ? SketcherWeight::Regular : SketcherWeight::Light;
        const float ChipWidth = Sheet.Measure(Chip.Label, 10.0f, SketcherWeight::Regular) + 18.0f + 2.0f;
        if (Cursor > Left + 12.0f + 1.0f && Cursor + ChipWidth > ChipMaxRight)
        {
            Cursor = Left + 12.0f;
            Y += ChipHeight + 5.0f;
        }
        const PlaneExtent Extent    = Spanning(Cursor, Y, std::min(ChipWidth, ChipMaxRight - Cursor), ChipHeight);
        const SketcherPress Press   = PressSketcherRegion(State.Widgets, std::string("Filter") + Chip.Label, Extent);
        Sheet.Box(Extent, Press.HoveredCondition ? Ink(HoverFill) : Active ? Ink(0x262626) : Clear, 14.0f, SketcherWash(0x0d));
        Sheet.Text(Cursor + 10.0f, Y + 0.5f * ChipHeight, 10.0f, Press.HoveredCondition ? Ink(0xffffff) : Active ? Ink(0xeeeeee) : Ink(0x9a9a9a), Chip.Label, ChipWgt);
        if (Press.ClickedCondition) Outliner.Filter = Chip.Filter;
        Cursor += Extent.Width() + 5.0f;
    }
    Y += ChipHeight + 11.0f;

    // .collection — margin 0 12 8, 11 px: fold text-button (⌄, folder glyph 13, caption) and the 24 px + button.
    const float CollectionHeight = 11.0f * SketcherStyleTokens::FontScale + 12.0f;
    const float CollectionCentre = Y + 0.5f * CollectionHeight;
    {
        const float ChevronWidth = 6.0f + Sheet.Measure(" \xC2\xA0", 11.0f, SketcherWeight::Regular);
        const float CaptionWidth = Sheet.Measure("Geometry collection", 11.0f, SketcherWeight::Regular);
        const PlaneExtent Fold   = Spanning(Left + 12.0f, Y, 8.0f + ChevronWidth + 8.0f + 13.0f + 8.0f + CaptionWidth + 8.0f, CollectionHeight);
        const SketcherPress Press = PressSketcherRegion(State.Widgets, "OutlinerFold", Fold);
        const ColorQuad   Tint   = Press.HoveredCondition ? Ink(0xffffff) : Ink(0xa8a8a8);
        Sheet.Box(Fold, Press.HoveredCondition ? Ink(HoverFill) : Clear, 16.0f);
        float FoldCursor = Fold.MinimumX + 8.0f;
        // DM Sans has no ⌄ / ›: the chevron is two strokes in the advance of the glyph and its trailing spaces.
        const float ChevronX = FoldCursor + 1.0f;
        if (Outliner.FoldedEnabled)
        {
            Sheet.Line(ChevronX + 1.5f, CollectionCentre - 3.0f, ChevronX + 4.5f, CollectionCentre, Tint, 1.2f);
            Sheet.Line(ChevronX + 4.5f, CollectionCentre, ChevronX + 1.5f, CollectionCentre + 3.0f, Tint, 1.2f);
        }
        else
        {
            Sheet.Line(ChevronX, CollectionCentre - 1.5f, ChevronX + 3.0f, CollectionCentre + 1.5f, Tint, 1.2f);
            Sheet.Line(ChevronX + 3.0f, CollectionCentre + 1.5f, ChevronX + 6.0f, CollectionCentre - 1.5f, Tint, 1.2f);
        }
        FoldCursor += ChevronWidth + 8.0f;
        Sheet.Icon(IconSymbol::SketcherFolder, FoldCursor, CollectionCentre - 6.5f, 13.0f, Tint);
        Sheet.Text(FoldCursor + 13.0f + 8.0f, CollectionCentre, 11.0f, Tint, "Geometry collection", SketcherWeight::Regular);
        if (Press.ClickedCondition) Outliner.FoldedEnabled = !Outliner.FoldedEnabled;
        const PlaneExtent AddSpan = Spanning(Right - 12.0f - 24.0f, CollectionCentre - 12.0f, 24.0f, 24.0f);
        if (IconButton(State, Sheet, "OutlinerAdd", AddSpan, IconSymbol::SketcherPlus, 13.0f, SketcherWash(0x08)))
            Intent.Verb = SketcherOutlinerVerb::Construct;
        // Alias legacy @ConstructLauncher script target to the collection + button now that the bottom card is removed.
        State.Widgets.Record("ConstructLauncher", ImVec2(AddSpan.MinimumX, AddSpan.MinimumY), ImVec2(AddSpan.MaximumX, AddSpan.MaximumY));
    }
    Y += CollectionHeight + 8.0f;

    // #scene-rows — fills available panel space above caption + footer, scrolls; .scene-row 39 px (compact 32).
    const float FooterTop     = Panel.MaximumY - 1.0f - 32.0f;
    const float CaptionHeight = 16.0f + 12.0f + 16.0f;
    const std::string Query   = Lowered(Outliner.Search.data());
    std::vector<const SketcherFigure*> Shown;
    for (const SketcherFigure& Figure : Figures)
        if ((Outliner.Filter < 0 || static_cast<int>(Figure.Category) == Outliner.Filter) && Lowered(Figure.Name).find(Query) != std::string::npos)
            Shown.push_back(&Figure);
    const float Pitch      = Outliner.CompactEnabled ? CompactHeight : RowHeight;
    const float Limit      = std::max(80.0f, FooterTop - CaptionHeight - Y);
    const float EmptyBlock = 18.0f + 2.0f * 18.0f + 18.0f;                                   // .empty-note: padding 18, two 1.8 em lines
    const float TotalRowsHeight = std::max(Shown.empty() ? EmptyBlock : Pitch * float(Shown.size()), 1.0f);
    const float RowsHeight = Outliner.FoldedEnabled ? 0.0f : std::clamp(Shown.empty() ? EmptyBlock : Pitch * float(Shown.size()), 80.0f, Limit);
    if (RowsHeight > 0.0f)
    {
        const ImVec2 Restore = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(Left, Y));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(1, 1, 1, 0.12f));
        ImGui::SetNextWindowContentSize(ImVec2(0.0f, TotalRowsHeight));
        ImGui::BeginChild("SceneRows", ImVec2(Width, RowsHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav);
        ImDrawList* const RowsDrawList = ImGui::GetWindowDrawList();
        const float RowsTop = ImGui::GetCursorScreenPos().y;
        const float ScrollY = ImGui::GetScrollY();
        ImGui::Dummy(ImVec2(1.0f, TotalRowsHeight));

        // Mirror Frontier Editor's OutlinerPanel RetainedCanvasCache: when the cursor is outside the rows viewport
        // and content/scroll/layout haven't changed, replay the cached ImDrawList in ~3 us with zero layout/shaping!
        thread_local RetainedCanvasCache RowsCanvasCache {};
        thread_local float               LastObservedScrollY = -1.0f;
        const Rectangle2D RowsBounds { Left, Y, Width, RowsHeight };
        const ImVec2 MousePos = ImGui::GetIO().MousePos;
        const Point2D MousePt { MousePos.x, MousePos.y };
        const bool PointerInRows =
            MousePos.x >= Left && MousePos.x <= (Left + Width) &&
            MousePos.y >= Y    && MousePos.y <= (Y + RowsHeight);
        const bool ScrollingNow = std::fabs(ScrollY - LastObservedScrollY) > 0.01f || std::fabs(ImGui::GetIO().MouseWheel) > 1.0e-4f;
        LastObservedScrollY = ScrollY;
        const bool RowsWindowHovered =
            ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
            !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        uint64_t RowsSig = RetainedCanvasCache::MixBounds(0x5011DAC00711E402ull, RowsBounds, 1.0f);
        RowsSig = RetainedCanvasCache::MixHash(RowsSig, FiguresSig);
        RowsSig = RetainedCanvasCache::MixHash(RowsSig, static_cast<uint64_t>(Outliner.Filter + 16));
        RowsSig = RetainedCanvasCache::MixHash(RowsSig, Outliner.CompactEnabled ? 1ull : 0ull);
        RowsSig = RetainedCanvasCache::MixHash(RowsSig, static_cast<uint64_t>(std::llround(ScrollY * 4.0f)));
        RowsSig = MixString(RowsSig, Query);

        const bool ForceRowsRebuild = PointerInRows || RowsWindowHovered || ScrollingNow || ImGui::IsAnyMouseDown();
        const bool ReplayedRows =
            RowsDrawList != nullptr &&
            RowsCanvasCache.QueryReusableStationary(RowsSig, RowsBounds, MousePt, ForceRowsRebuild) &&
            RowsCanvasCache.ReplayInto(RowsDrawList, MousePt);
        if (ReplayedRows)
        {
            GlobalOutlinerCanvasReplays.fetch_add(1ull, std::memory_order_relaxed);
            // Keep automated widget-region lookup tables in sync without re-drawing any SketcherSheet geometry.
            const float ViewMinY = Y - Pitch;
            const float ViewMaxY = Y + RowsHeight + Pitch;
            for (size_t Index = 0; Index < Shown.size(); ++Index)
            {
                const float RowTop = RowsTop + float(Index) * Pitch;
                if (RowTop + Pitch < ViewMinY || RowTop > ViewMaxY) continue;
                const SketcherFigure& Figure = *Shown[Index];
                const PlaneExtent     Row    = Spanning(Left + 10.0f, RowTop, Width - 20.0f, Pitch);
                const float           Centre = 0.5f * (Row.MinimumY + Row.MaximumY);
                const PlaneExtent     Eye    = Spanning(Row.MaximumX - 1.0f - 9.0f - 23.0f, Centre - 11.5f, 23.0f, 23.0f);
                const PlaneExtent     Lock   = Spanning(Eye.MinimumX - 2.0f - 23.0f, Centre - 11.5f, 23.0f, 23.0f);
                State.Widgets.Record("Figure:" + Figure.Key, ImVec2(Row.MinimumX, Row.MinimumY), ImVec2(Lock.MinimumX - 4.0f, Row.MaximumY));
                State.Widgets.Record("Visibility:" + Figure.Key, ImVec2(Eye.MinimumX, Eye.MinimumY), ImVec2(Eye.MaximumX, Eye.MaximumY));
                State.Widgets.Record("Lock:" + Figure.Key, ImVec2(Lock.MinimumX, Lock.MinimumY), ImVec2(Lock.MaximumX, Lock.MaximumY));
            }
        }
        else
        {
            const bool CaptureNow = RowsDrawList != nullptr && !PointerInRows && !RowsWindowHovered && !ScrollingNow && !ImGui::IsAnyMouseDown();
            if (CaptureNow)
            {
                RowsCanvasCache.BeginCapture(RowsDrawList);
            }
            SketcherSheet Rows;
            Rows.Begin();
            if (Shown.empty())
            {
                Rows.Text(Left + 18.0f, RowsTop + 18.0f + 9.0f, 10.0f, Ink(MutedInk), "No matching geometry.");
                Rows.Text(Left + 18.0f, RowsTop + 18.0f + 27.0f, 10.0f, Ink(MutedInk), "Construct a shape or open a document.");
            }
            const bool CanHitRow = RowsWindowHovered && MousePos.x < (Left + Width - 8.0f) && MousePos.y >= Y && MousePos.y <= (Y + RowsHeight);
            const bool LeftClicked = CanHitRow && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            auto HitExtent = [&](const std::string& Key, const PlaneExtent& Extent) -> SketcherPress
            {
                State.Widgets.Record(Key, ImVec2(Extent.MinimumX, Extent.MinimumY), ImVec2(Extent.MaximumX, Extent.MaximumY));
                SketcherPress Press {};
                if (CanHitRow &&
                    MousePos.x >= Extent.MinimumX && MousePos.x <= Extent.MaximumX &&
                    MousePos.y >= Extent.MinimumY && MousePos.y <= Extent.MaximumY)
                {
                    Press.HoveredCondition = true;
                    Press.ClickedCondition = LeftClicked;
                    Press.HeldCondition    = ImGui::IsMouseDown(ImGuiMouseButton_Left);
                    Press.DoubleCondition  = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                }
                return Press;
            };
            const float ViewMinY = Y - Pitch;
            const float ViewMaxY = Y + RowsHeight + Pitch;
            for (size_t Index = 0; Index < Shown.size(); ++Index)
            {
                const float RowTop = RowsTop + float(Index) * Pitch;
                if (RowTop + Pitch < ViewMinY || RowTop > ViewMaxY) continue;
                const SketcherFigure& Figure  = *Shown[Index];
                const PlaneExtent     Row     = Spanning(Left + 10.0f, RowTop, Width - 20.0f, Pitch);
                const float           Centre  = 0.5f * (Row.MinimumY + Row.MaximumY);
                const float           Opacity = Figure.HiddenCondition ? 0.45f : 1.0f;            // .hidden-figure
                const PlaneExtent     Eye     = Spanning(Row.MaximumX - 1.0f - 9.0f - 23.0f, Centre - 11.5f, 23.0f, 23.0f);
                const PlaneExtent     Lock    = Spanning(Eye.MinimumX - 2.0f - 23.0f, Centre - 11.5f, 23.0f, 23.0f);
                const SketcherPress   Press   = HitExtent("Figure:" + Figure.Key, PlaneExtent{ Row.MinimumX, Row.MinimumY, Lock.MinimumX - 4.0f, Row.MaximumY });
                const bool            Active  = Figure.SelectedCondition && Figure.Rank == 0;
                const ColorQuad       Accent  = Active ? Ink(ActiveInk) : Ink(SelectedInk);
                if (Figure.SelectedCondition)
                {
                    Rows.Box(Row, ControlKit::Faded(SketcherWash(0x16), Opacity), 2.0f, ControlKit::Faded(SketcherWash(0x20), Opacity));
                    Rows.Surface().FillRectangle(Spanning(Row.MinimumX, Row.MinimumY, 3.0f, Pitch), ControlKit::Faded(Accent, Opacity));   // :before rail
                }
                else if (Press.HoveredCondition)
                {
                    Rows.Box(Row, SketcherWash(0x04), 2.0f);
                }
                float RowCursor = Row.MinimumX + 1.0f + 14.0f;
                Rows.Icon(Figure.Icon, RowCursor + 4.5f, Centre - 8.5f, 17.0f, ControlKit::Faded(Figure.SelectedCondition ? Accent : CategoryInk(Figure.Category), Opacity));
                RowCursor += 26.0f + 9.0f;
                char Tag[32];
                if (Figure.Rank >= 0) std::snprintf(Tag, sizeof(Tag), "%d %s", Figure.Rank + 1, Active ? "ACTIVE" : "SELECTED");
                else if (Figure.LiveCutterCondition) std::snprintf(Tag, sizeof(Tag), "CUTTER");
                else std::snprintf(Tag, sizeof(Tag), "%s", CategoryTag(Figure.Category));
                const float TagWidth = Rows.TextRight(Lock.MinimumX - 9.0f, Centre, 8.0f, ControlKit::Faded(Figure.SelectedCondition ? Accent : Ink(0x787878), Opacity), Tag,
                                                      SketcherWeight::Light, 0.5f);
                Rows.TextClipped(RowCursor, Centre, Lock.MinimumX - 9.0f - TagWidth - 9.0f - RowCursor, 11.0f, ControlKit::Faded(Ink(TextInk), Opacity), Figure.Name.c_str(),
                                 SketcherWeight::Regular);
                const SketcherPress Toggle = HitExtent("Visibility:" + Figure.Key, Eye);
                if (Toggle.HoveredCondition) Rows.Box(Eye, Ink(HoverFill), 11.5f);
                const ColorQuad EyeInk = Toggle.HoveredCondition ? Ink(0xffffff) : Ink(0x858585);
                Rows.Icon(Figure.HiddenCondition ? IconSymbol::SketcherHidden : IconSymbol::SketcherEye, Eye.MinimumX + 4.5f, Eye.MinimumY + 4.5f, 14.0f,
                          ControlKit::Faded(EyeInk, Opacity));
                const SketcherPress Latch = HitExtent("Lock:" + Figure.Key, Lock);
                if (Latch.HoveredCondition) Rows.Box(Lock, Ink(HoverFill), 11.5f);
                const ColorQuad LockInk = Latch.HoveredCondition ? Ink(0xffffff) : Figure.LockedCondition ? Ink(0xffd24d) : Ink(0x858585, 0.55f);
                LockGlyph(Rows, 0.5f * (Lock.MinimumX + Lock.MaximumX), 0.5f * (Lock.MinimumY + Lock.MaximumY), Figure.LockedCondition, ControlKit::Faded(LockInk, Opacity));
                if (Press.HoveredCondition || Toggle.HoveredCondition || Latch.HoveredCondition)
                {
                    Outliner.HoveredCondition = true;
                    Outliner.Hovered          = Figure;
                }
                if (Toggle.ClickedCondition)
                {
                    Intent.Verb   = SketcherOutlinerVerb::ToggleVisibility;
                    Intent.Figure = Figure;
                }
                else if (Latch.ClickedCondition)
                {
                    Intent.Verb   = SketcherOutlinerVerb::ToggleLock;
                    Intent.Figure = Figure;
                }
                else if (Press.ClickedCondition)
                {
                    Intent.Verb              = SketcherOutlinerVerb::Pick;
                    Intent.Figure            = Figure;
                    Intent.ShiftCondition    = ImGui::GetIO().KeyShift;
                    Intent.CtrlCondition     = ImGui::GetIO().KeyCtrl;
                    Intent.AdditiveCondition = Intent.ShiftCondition || Intent.CtrlCondition;
                    if (Intent.ShiftCondition && Figure.Source != SketcherFigureSource::Image)
                    {
                        std::string Anchor = Outliner.AnchorKey;
                        if (Anchor.empty())
                        {
                            for (const SketcherFigure* Candidate : Shown)
                                if (Candidate->SelectedCondition && Candidate->Source != SketcherFigureSource::Image) { Anchor = Candidate->Key; break; }
                        }
                        ptrdiff_t AnchorIndex = -1;
                        for (size_t Scan = 0; Scan < Shown.size(); ++Scan)
                            if (Shown[Scan]->Key == Anchor) { AnchorIndex = static_cast<ptrdiff_t>(Scan); break; }
                        if (AnchorIndex >= 0)
                        {
                            const ptrdiff_t ClickIndex = static_cast<ptrdiff_t>(Index);
                            const ptrdiff_t StepDir    = AnchorIndex <= ClickIndex ? 1 : -1;
                            for (ptrdiff_t CursorIdx = AnchorIndex; ; CursorIdx += StepDir)
                            {
                                const SketcherFigure& RowFigure = *Shown[static_cast<size_t>(CursorIdx)];
                                if (RowFigure.Source != SketcherFigureSource::Image && !RowFigure.LockedCondition
                                    && !(RowFigure.HiddenCondition && RowFigure.Source != SketcherFigureSource::Body))
                                {
                                    Intent.RangeFigures.push_back(RowFigure);
                                }
                                if (CursorIdx == ClickIndex) break;
                            }
                        }
                    }
                }
            }
            if (CaptureNow)
            {
                RowsCanvasCache.EndCapture(RowsDrawList, RowsSig, RowsBounds, MousePt);
                GlobalOutlinerCanvasCaptures.fetch_add(1ull, std::memory_order_relaxed);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        ImGui::SetCursorScreenPos(Restore);
    }
    Y += RowsHeight;

    // .outliner-caption — padding 16 21, gap 6, 9 px #6d6d6d, link glyph 12.
    Sheet.Icon(IconSymbol::SketcherLink, Left + 21.0f, Y + 16.0f, 12.0f, Ink(0x6d6d6d));
    Sheet.Text(Left + 21.0f + 12.0f + 6.0f, Y + 16.0f + 6.0f, 9.0f, Ink(0x6d6d6d), "Native B-rep · editable construction");

    // .panel-footer — min-height 32 at the bottom, #ffffff0a top border, padding 8 13, 9 px #737373, shield glyph 11.
    Sheet.Line(Left, FooterTop + 0.5f, Right, FooterTop + 0.5f, SketcherWash(0x0a));
    Sheet.Icon(IconSymbol::SketcherShield, Left + 13.0f, FooterTop + 16.0f - 5.5f, 11.0f, Ink(0x737373));
    Sheet.Text(Left + 13.0f + 11.0f + 6.0f, FooterTop + 16.0f, 9.0f, Ink(0x737373), "Native geometry · Vulkan viewport");
    Sheet.Surface().PopClip();
    return Intent;
}

} // namespace Frontier::Workspaces::Sketcher
