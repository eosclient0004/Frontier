/*============================================================================================================================================
                                                      BOTANICALWELDEDMANIFOLD.H
============================================================================================================================================*/
// 🧩 Exact C++20 Port of Frontier's Watertight Welded Quad Manifold Engine (`weldedManifold.ts`).
//    - Welds roots, trunk, limbs, branches, twigs & Y/k-way forks into ONE single closed genus-0 quad manifold:
//        V - E + F = 2, BoundaryEdges == 0, NonManifoldEdges == 0, InconsistentEdges == 0, Components == 1.
//    - Every root & side branch cuts a non-overlapping w x rows window in its parent ring grid, relocates/regrows to
//      exit dead-center through the window, mitres its first ring, and bridges with tangent-continuous Bezier collars!

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Botanical
{

using Vec3f = std::array<float, 3>;

//------------------------------------------------------------------------------------------------------------------------
//                                        WELDED MANIFOLD STEM & MESH STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct WeldedNode
{
    Vec3f Pos   { 0.0f, 0.0f, 0.0f };
    Vec3f Dir   { 0.0f, 0.0f, 1.0f };
    Vec3f Right { 1.0f, 0.0f, 0.0f };
    float S     = 0.0f; // Arc length from physical stem base [m]
};

enum class WeldedStemRole : uint8_t
{
    Shoot = 0u,
    Root  = 1u,
};

enum class WeldedAttachKind : uint8_t
{
    Root = 0u,
    Side = 1u,
    Fork = 2u,
};

struct WeldedVertexStyle
{
    Vec3f ColorLinear { 0.16f, 0.11f, 0.07f };
    float WindFlex    = 0.0f;
    float Phase       = 0.0f;
};

struct WeldedQuadMesh
{
    std::vector<Vec3f>                   Positions {};
    std::vector<Vec3f>                   Colors    {};
    std::vector<float>                   WindFlex  {};
    std::vector<float>                   Phase     {};
    std::vector<uint8_t>                 IsJunction{};
    std::vector<std::array<uint32_t, 4>> Quads     {};
    std::vector<std::array<uint32_t, 3>> Tris      {};

    uint32_t AddVertex(const Vec3f& pos, const WeldedVertexStyle& style, bool isJunction);
    void     AddQuad(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
    void     AddTri(uint32_t a, uint32_t b, uint32_t c);
};

struct WeldedStem
{
    uint32_t                                     Id            = 0u;
    uint32_t                                     Level         = 0u;
    WeldedStemRole                               Role          = WeldedStemRole::Shoot;
    WeldedStem*                                  Parent        = nullptr;
    WeldedAttachKind                             Attach        = WeldedAttachKind::Root;
    float                                        AttachS       = 0.0f;
    Vec3f                                        AttachDir     { 0.0f, 0.0f, 1.0f };
    std::vector<WeldedNode>                      Nodes         {};
    float                                        Length        = 0.0f;
    std::vector<std::shared_ptr<WeldedStem>>     Children      {};
    std::vector<std::shared_ptr<WeldedStem>>     Forks         {};
    bool                                         Dropped       = false;
    std::string                                  DropReason    {};
    float                                        RadiusDrop    = 1.0f;
    float                                        RingSpacing   = 0.0f; // 0 = auto
    uint32_t                                     MinRing       = 0u;   // 0 = use config minRadialSegments
    uint32_t                                     WindowCols    = 0u;   // 0 = auto
    std::vector<float>                           ExtraStations {};

    std::function<float(float)>                  BaseRadiusAt  {};
    std::function<float(float, float)>           ProfileFactor {};
    std::function<WeldedVertexStyle(float, float, const Vec3f&, const Vec3f&, bool)> StyleAt {};
    std::function<void(const Vec3f&, const Vec3f&)>                                  Regrow  {};
    std::function<void(const std::function<Vec3f(const Vec3f&)>&,
                       const std::function<Vec3f(const Vec3f&)>&)>                   OnTransform {};
};

struct WeldedMeshConfig
{
    uint32_t TrunkRadialSegments = 24u;
    uint32_t MinRadialSegments   = 6u;
    float    CollarScale         = 1.28f;
    float    CollarLength        = 1.18f;
    uint32_t CollarRings         = 2u;
    float    CollarFillet        = 0.78f;
    float    CollarMitre         = 0.52f;
    float    MinRadius           = 0.005f;
    bool     CapTips             = true;
};

struct WeldedBuildStats
{
    uint32_t Stems         = 0u;
    uint32_t DroppedStems  = 0u;
    uint32_t Junctions     = 0u;
    uint32_t Forks         = 0u;
    uint32_t MaxDepth      = 0u;
    uint32_t RootStems     = 0u;
    uint32_t DroppedRoots  = 0u;
};

struct WeldedManifoldReport
{
    uint32_t Vertices            = 0u;
    uint32_t Edges               = 0u;
    uint32_t Faces               = 0u;
    uint32_t BoundaryEdges       = 0u;
    uint32_t NonManifoldEdges    = 0u;
    uint32_t InconsistentEdges   = 0u;
    uint32_t IsolatedVertices    = 0u;
    uint32_t DegenerateFaces     = 0u;
    int32_t  EulerCharacteristic = 0;
    uint32_t Components          = 0u;
    bool     Closed              = false;
    bool     Manifold            = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                UTILITY & SOLVER API
//------------------------------------------------------------------------------------------------------------------------

struct SampledStemFrame
{
    Vec3f Pos   { 0.0f, 0.0f, 0.0f };
    Vec3f Dir   { 0.0f, 0.0f, 1.0f };
    Vec3f Right { 1.0f, 0.0f, 0.0f };
};

[[nodiscard]] SampledStemFrame SampleWeldedStem(const WeldedStem& stem, float s) noexcept;
[[nodiscard]] float            WeldedPipeFactor(const WeldedStem& stem, float s, float collarScale) noexcept;
[[nodiscard]] float            WeldedRadiusAt(const WeldedStem& stem, float s, const WeldedMeshConfig& cfg) noexcept;

void TransformWeldedSubtree(
    WeldedStem&                                 stem,
    const std::function<Vec3f(const Vec3f&)>&   mapPos,
    const std::function<Vec3f(const Vec3f&)>&   mapDir);
void TranslateWeldedSubtree(WeldedStem& stem, const Vec3f& delta);
void RotateWeldedSubtree(WeldedStem& stem, const Vec3f& origin, const Vec3f& axis, float angleRad);

class WeldedMesher
{
public:
    WeldedMesher(WeldedQuadMesh& outMesh, const std::vector<std::shared_ptr<WeldedStem>>& roots, const WeldedMeshConfig& cfg = {});

    [[nodiscard]] WeldedBuildStats Build();

private:
    struct Ring
    {
        std::vector<int32_t> Idx    {};
        float                S      = 0.0f;
        Vec3f                Center { 0.0f, 0.0f, 0.0f };
        Vec3f                Dir    { 0.0f, 0.0f, 1.0f };
        float                Radius = 0.01f;
    };

    struct HolePlan
    {
        std::shared_ptr<WeldedStem> Child {};
        float   S      = 0.0f;
        float   SLow   = 0.0f;
        float   SHigh  = 0.0f;
        int32_t J0     = 0;
        int32_t W      = 1;
        float   Hh     = 0.0f;
        int32_t Rows   = 1;
        int32_t Row0   = 0;
        int32_t Row1   = 0;
        float   Along  = 0.0f;
    };

    struct BaseInfo
    {
        WeldedAttachKind     Kind      = WeldedAttachKind::Root;
        std::vector<int32_t> Loop      {};
        Vec3f                Radial    { 1.0f, 0.0f, 0.0f };
        Vec3f                ParentDir { 0.0f, 0.0f, 1.0f };
        float                T1        = 0.0f;
    };

    [[nodiscard]] float   SurfaceRadius(const WeldedStem& stem, float s, float theta) const noexcept;
    [[nodiscard]] int32_t EmitVertex(const Vec3f& p, const WeldedStem& stem, float s, float theta, const Vec3f& dir, bool isJunc);
    [[nodiscard]] Vec3f   PosAt(int32_t vIdx) const noexcept;
    void                  MarkJunction(int32_t vIdx) noexcept;
    void                  DropStem(WeldedStem& stem, const char* reason);

    void                  MeshStem(WeldedStem& stem, const BaseInfo& base, uint32_t depth);
    [[nodiscard]] Ring    BuildRing(const WeldedStem& stem, float s, int32_t nCols, float phase,
                                    const Vec3f* tiltNormal, const std::function<bool(int32_t)>& skipFn);
    [[nodiscard]] float   FitPhase(const std::vector<int32_t>& loop, const Vec3f& center, const Vec3f& dir, const Vec3f& right) const noexcept;
    void                  BuildCollar(const WeldedStem& stem, const std::vector<int32_t>& loop, const Ring& first, const Vec3f& radial, float r0);
    void                  BridgeLoops(const std::vector<int32_t>& a, const std::vector<int32_t>& b, bool junction);
    void                  CapRing(const std::vector<int32_t>& ring, bool flip);
    [[nodiscard]] std::vector<HolePlan> PlanHoles(WeldedStem& stem, int32_t nCols, float phase, float sStart, float len);
    [[nodiscard]] std::vector<float>    BuildStations(const WeldedStem& stem, const std::vector<HolePlan>& holes,
                                                      float sStart, float len, WeldedAttachKind kind) const;
    [[nodiscard]] std::vector<int32_t>  HoleLoop(const std::vector<Ring>& rings, const HolePlan& h, int32_t nCols);
    void                  MeshFork(WeldedStem& stem, const Ring& endRing, uint32_t depth);

    WeldedQuadMesh&                          Mesh_;
    std::vector<std::shared_ptr<WeldedStem>> Roots_;
    WeldedMeshConfig                         Cfg_;
    WeldedBuildStats                         Stats_{};
};

/// Exact 2-manifold & Euler characteristic validator on a WeldedQuadMesh.
[[nodiscard]] WeldedManifoldReport ValidateWeldedManifold(const WeldedQuadMesh& mesh);

} // namespace Frontier::GeometrySpace::Botanical
