/*============================================================================================================================================
                                                      STUDIOPROCEDURALSURFACE.CPP
============================================================================================================================================*/
// 🧩 Implementation of ProjectZeroEditor procedural studio surfaces & FCT1 vehicle surface transform into FrontierWorldZUp

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Source/Geometry/StudioProceduralSurface.h"

#include "Engine/ColourSpace/TransferFunction.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Engine/Mathematics/CoordinateSpace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace AutoSpace
{

namespace
{

using Axis3D    = Frontier::Mathematics::Axis3D;
using LinearRGB = Frontier::Mathematics::LinearRGB;
using UVAxes    = Frontier::Mathematics::UVAxes;

[[nodiscard]] Axis3D RotateHtmlYUpVector(Axis3D Pt, float PitchRad, float YawRad, float RollRad) noexcept
{
    // Apply Roll around Z -> Pitch around X -> Yaw around Y (matching StudioProceduralSurface.js AddBox/transform)
    float X = Pt.X;
    float Y = Pt.Y;
    float Z = Pt.Z;

    if (std::fabs(RollRad) > 1.0e-6f)
    {
        const float Cr = std::cos(RollRad);
        const float Sr = std::sin(RollRad);
        const float Nx = X * Cr - Y * Sr;
        const float Ny = X * Sr + Y * Cr;
        X = Nx;
        Y = Ny;
    }
    if (std::fabs(PitchRad) > 1.0e-6f)
    {
        const float Cp = std::cos(PitchRad);
        const float Sp = std::sin(PitchRad);
        const float Ny = Y * Cp - Z * Sp;
        const float Nz = Y * Sp + Z * Cp;
        Y = Ny;
        Z = Nz;
    }
    if (std::fabs(YawRad) > 1.0e-6f)
    {
        const float Cy = std::cos(YawRad);
        const float Sy = std::sin(YawRad);
        const float Nx = X * Cy + Z * Sy;
        const float Nz = -X * Sy + Z * Cy;
        X = Nx;
        Z = Nz;
    }
    return { X, Y, Z };
}

//------------------------------------------------------------------------------------------------------------------------
//                                        HTML Y-UP TO FRONTIERWORLDZUP MESH BUILDER
//------------------------------------------------------------------------------------------------------------------------

class HtmlStudioMeshBuilder
{
public:
    HtmlStudioMeshBuilder(
        std::string                  Title,
        const StudioMaterialPalette& InPalette,
        StudioMeshBakeTransform      InBakeTransform)
        : Palette(InPalette)
        , BakeTransform(InBakeTransform)
    {
        Built.Network.NetworkTitle = std::move(Title);
    }

    void PushTri(
        Axis3D             HtmlA,
        Axis3D             HtmlB,
        Axis3D             HtmlC,
        Axis3D             HtmlNa,
        Axis3D             HtmlNb,
        Axis3D             HtmlNc,
        StudioMaterialCode MatCode,
        UVAxes             UvA = { 0.0f, 0.0f },
        UVAxes             UvB = { 1.0f, 0.0f },
        UVAxes             UvC = { 0.5f, 1.0f })
    {
        const Axis3D Wa = ConvertHtmlYUpToWorldZUp(HtmlA, BakeTransform);
        const Axis3D Wb = ConvertHtmlYUpToWorldZUp(HtmlB, BakeTransform);
        const Axis3D Wc = ConvertHtmlYUpToWorldZUp(HtmlC, BakeTransform);

        const Axis3D Edge1 = Wb - Wa;
        const Axis3D Edge2 = Wc - Wa;
        const Axis3D AreaN = Frontier::Mathematics::EvaluateCross(Edge1, Edge2);
        if (Frontier::Mathematics::EvaluateLengthSquared(AreaN) <= 1.0e-14f)
        {
            return;
        }
        const Axis3D UnitFaceN = Frontier::Mathematics::NormalizeAxis(AreaN, { 0.0f, 0.0f, 1.0f });

        const Axis3D Wna = Frontier::Mathematics::NormalizeAxis(ConvertHtmlYUpNormalToWorldZUp(HtmlNa, BakeTransform), UnitFaceN);
        const Axis3D Wnb = Frontier::Mathematics::NormalizeAxis(ConvertHtmlYUpNormalToWorldZUp(HtmlNb, BakeTransform), UnitFaceN);
        const Axis3D Wnc = Frontier::Mathematics::NormalizeAxis(ConvertHtmlYUpNormalToWorldZUp(HtmlNc, BakeTransform), UnitFaceN);

        const uint32_t V0 = Built.Network.AppendVertex(Wa);
        const uint32_t V1 = Built.Network.AppendVertex(Wb);
        const uint32_t V2 = Built.Network.AppendVertex(Wc);
        Built.Network.Vertices[V0].Normal = Wna;
        Built.Network.Vertices[V1].Normal = Wnb;
        Built.Network.Vertices[V2].Normal = Wnc;

        const std::array<uint32_t, 3u> Loop { V0, V1, V2 };
        const std::array<UVAxes, 3u>   UvLoop { UvA, UvB, UvC };
        const uint16_t                 MatSlot = Palette.Resolve(MatCode);

        Frontier::GeometrySpace::FaceShadingAssignment Shading {};
        Shading.AlbedoLinearRGB = { 0.5f, 0.5f, 0.5f };

        const uint32_t FaceIdx = Built.Network.AppendFace(Loop, UvLoop, Shading, 0u, MatSlot);
        if (FaceIdx != Frontier::GeometrySpace::InvalidNetworkIndex)
        {
            Built.AuthoredCornerNormals.push_back(Wna.X);
            Built.AuthoredCornerNormals.push_back(Wna.Y);
            Built.AuthoredCornerNormals.push_back(Wna.Z);
            Built.AuthoredCornerNormals.push_back(Wnb.X);
            Built.AuthoredCornerNormals.push_back(Wnb.Y);
            Built.AuthoredCornerNormals.push_back(Wnb.Z);
            Built.AuthoredCornerNormals.push_back(Wnc.X);
            Built.AuthoredCornerNormals.push_back(Wnc.Y);
            Built.AuthoredCornerNormals.push_back(Wnc.Z);
        }
    }

    [[nodiscard]] static Axis3D ComputeFlatNormal(Axis3D A, Axis3D B, Axis3D C) noexcept
    {
        return Frontier::Mathematics::NormalizeAxis(
            Frontier::Mathematics::EvaluateCross(B - A, C - A),
            { 0.0f, 1.0f, 0.0f });
    }

    void AddQuadFlat(Axis3D A, Axis3D B, Axis3D C, Axis3D D, StudioMaterialCode MatCode)
    {
        const Axis3D N = ComputeFlatNormal(A, B, C);
        PushTri(A, B, C, N, N, N, MatCode, { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f });
        PushTri(A, C, D, N, N, N, MatCode, { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f });
    }

    void AddBox(
        Axis3D             Center,
        Axis3D             HalfExtents,
        StudioMaterialCode MatCode,
        float              Rx = 0.0f,
        float              Ry = 0.0f,
        float              Rz = 0.0f)
    {
        ResetScratchNetwork();
        Frontier::GeometrySpace::AppendBoxPrimitive(
            ScratchNet,
            { 0.0f, 0.0f, 0.0f },
            { HalfExtents.X, HalfExtents.Y, HalfExtents.Z },
            {},
            {},
            0u,
            Palette.Resolve(MatCode));

        const bool HasRotation = (std::fabs(Rx) > 1.0e-6f) || (std::fabs(Ry) > 1.0e-6f) || (std::fabs(Rz) > 1.0e-6f);
        std::array<Axis3D, 4u> Pts {};
        std::array<UVAxes, 4u> Uvs {};
        for (const auto& Face : ScratchNet.Faces)
        {
            if (Face.CornerCount < 3u)
            {
                continue;
            }
            const uint32_t Count = std::min<uint32_t>(4u, Face.CornerCount);
            uint32_t EdgeIdx = Face.BoundaryLoopEdgeIndex;
            for (uint32_t K = 0u; K < Count && EdgeIdx < ScratchNet.Edges.size(); ++K)
            {
                const auto& E = ScratchNet.Edges[EdgeIdx];
                const Axis3D Local = ScratchNet.Vertices[E.OriginVertexIndex].Axes;
                Pts[K] = (HasRotation ? RotateHtmlYUpVector(Local, Rx, Ry, Rz) : Local) + Center;
                Uvs[K] = E.ParameterUV;
                EdgeIdx = E.NextEdgeIndex;
            }
            const Axis3D N = ComputeFlatNormal(Pts[0], Pts[1], Pts[2]);
            PushTri(Pts[0], Pts[1], Pts[2], N, N, N, MatCode, Uvs[0], Uvs[1], Uvs[2]);
            if (Count == 4u)
            {
                PushTri(Pts[0], Pts[2], Pts[3], N, N, N, MatCode, Uvs[0], Uvs[2], Uvs[3]);
            }
        }
    }

    void AddTube(
        Axis3D             P0,
        Axis3D             P1,
        float              R0,
        float              R1,
        uint32_t           Segs,
        StudioMaterialCode MatCode,
        bool               CapEnds = true)
    {
        const Axis3D AxisVec = P1 - P0;
        const float  Len     = Frontier::Mathematics::EvaluateLength(AxisVec);
        if (Len <= 1.0e-5f)
        {
            return;
        }
        const Axis3D   AxisUnit   = AxisVec * (1.0f / Len);
        const uint32_t ActualSegs = std::max(6u, Segs);

        ResetScratchNetwork();
        Frontier::GeometrySpace::AppendCylinderFrustumPrimitive(
            ScratchNet,
            P0,
            P1,
            R0,
            R1,
            ActualSegs,
            {},
            {},
            0u,
            Palette.Resolve(MatCode));

        const std::size_t FaceLimit = CapEnds
            ? ScratchNet.Faces.size()
            : std::min<std::size_t>(ActualSegs, ScratchNet.Faces.size());

        std::array<Axis3D, 4u> Pts {};
        std::array<Axis3D, 4u> Norms {};
        std::array<UVAxes, 4u> Uvs {};
        for (std::size_t FaceIdx = 0u; FaceIdx < FaceLimit; ++FaceIdx)
        {
            const auto& Face = ScratchNet.Faces[FaceIdx];
            if (Face.CornerCount < 3u)
            {
                continue;
            }
            const uint32_t Count = std::min<uint32_t>(4u, Face.CornerCount);
            uint32_t EdgeIdx = Face.BoundaryLoopEdgeIndex;
            for (uint32_t K = 0u; K < Count && EdgeIdx < ScratchNet.Edges.size(); ++K)
            {
                const auto& E = ScratchNet.Edges[EdgeIdx];
                Pts[K] = ScratchNet.Vertices[E.OriginVertexIndex].Axes;
                Uvs[K] = E.ParameterUV;
                EdgeIdx = E.NextEdgeIndex;
            }
            const Axis3D FlatN = ComputeFlatNormal(Pts[0], Pts[1], Pts[2]);
            if (FaceIdx < ActualSegs)
            {
                for (uint32_t K = 0u; K < Count; ++K)
                {
                    const Axis3D Delta  = Pts[K] - P0;
                    const Axis3D Radial = Delta - AxisUnit * Frontier::Mathematics::EvaluateDot(Delta, AxisUnit);
                    Norms[K] = Frontier::Mathematics::NormalizeAxis(Radial, FlatN);
                }
                PushTri(Pts[0], Pts[1], Pts[2], Norms[0], Norms[1], Norms[2], MatCode, Uvs[0], Uvs[1], Uvs[2]);
                if (Count == 4u)
                {
                    PushTri(Pts[0], Pts[2], Pts[3], Norms[0], Norms[2], Norms[3], MatCode, Uvs[0], Uvs[2], Uvs[3]);
                }
            }
            else
            {
                PushTri(Pts[0], Pts[1], Pts[2], FlatN, FlatN, FlatN, MatCode, Uvs[0], Uvs[1], Uvs[2]);
            }
        }
    }

    void AddSphere(
        Axis3D             Center,
        float              Radius,
        uint32_t           Segs,
        uint32_t           Rings,
        StudioMaterialCode MatCode)
    {
        ResetScratchNetwork();
        Frontier::GeometrySpace::AppendSpherePrimitive(
            ScratchNet,
            { 0.0f, 0.0f, 0.0f },
            Radius,
            std::max(8u, Segs),
            std::max(4u, Rings),
            {},
            {},
            0u,
            Palette.Resolve(MatCode));

        std::array<Axis3D, 4u> Pts {};
        std::array<Axis3D, 4u> Norms {};
        std::array<UVAxes, 4u> Uvs {};
        for (const auto& Face : ScratchNet.Faces)
        {
            if (Face.CornerCount < 3u)
            {
                continue;
            }
            const uint32_t Count = std::min<uint32_t>(4u, Face.CornerCount);
            uint32_t EdgeIdx = Face.BoundaryLoopEdgeIndex;
            for (uint32_t K = 0u; K < Count && EdgeIdx < ScratchNet.Edges.size(); ++K)
            {
                const auto& E = ScratchNet.Edges[EdgeIdx];
                const Axis3D Local = ScratchNet.Vertices[E.OriginVertexIndex].Axes;
                Pts[K]   = Center + Local;
                Norms[K] = Frontier::Mathematics::NormalizeAxis(Local, { 0.0f, 1.0f, 0.0f });
                Uvs[K]   = E.ParameterUV;
                EdgeIdx  = E.NextEdgeIndex;
            }
            PushTri(Pts[0], Pts[1], Pts[2], Norms[0], Norms[1], Norms[2], MatCode, Uvs[0], Uvs[1], Uvs[2]);
            if (Count == 4u)
            {
                PushTri(Pts[0], Pts[2], Pts[3], Norms[0], Norms[2], Norms[3], MatCode, Uvs[0], Uvs[2], Uvs[3]);
            }
        }
    }

    [[nodiscard]] StudioBuiltMesh Finish()
    {
        Built.Network.LinkOppositeEdges(false);
        return std::move(Built);
    }

private:
    void ResetScratchNetwork() noexcept
    {
        ScratchNet.Vertices.clear();
        ScratchNet.Edges.clear();
        ScratchNet.Faces.clear();
    }

    const StudioMaterialPalette&            Palette;
    StudioMeshBakeTransform                 BakeTransform;
    StudioBuiltMesh                         Built {};
    Frontier::GeometrySpace::PolygonNetwork ScratchNet {};
};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           COORDINATE & MATERIAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

Axis3D ConvertHtmlPositionToWorldZUp(float Hx, float Hy, float Hz) noexcept
{
    return Frontier::Mathematics::ConvertVectorBetweenBases(
        { Hx, Hy, Hz },
        Frontier::Mathematics::WorldCoordinateBasis::RightHandedYUp,
        Frontier::Mathematics::WorldCoordinateBasis::RightHandedZUp);
}

Axis3D ConvertHtmlYUpToWorldZUp(
    Axis3D                         HtmlPt,
    const StudioMeshBakeTransform& Transform) noexcept
{
    const Axis3D Scaled {
        HtmlPt.X * Transform.UniformScale,
        HtmlPt.Y * Transform.UniformScale,
        HtmlPt.Z * Transform.UniformScale,
    };
    const Axis3D Rotated = RotateHtmlYUpVector(
        Scaled,
        Transform.PitchDegrees * Frontier::Constants::DegToRadFloat,
        Transform.YawDegrees * Frontier::Constants::DegToRadFloat,
        Transform.RollDegrees * Frontier::Constants::DegToRadFloat);
    return Frontier::Mathematics::ConvertVectorBetweenBases(
        Rotated,
        Frontier::Mathematics::WorldCoordinateBasis::RightHandedYUp,
        Frontier::Mathematics::WorldCoordinateBasis::RightHandedZUp);
}

Axis3D ConvertHtmlYUpNormalToWorldZUp(
    Axis3D                         HtmlNormal,
    const StudioMeshBakeTransform& Transform) noexcept
{
    const Axis3D Rotated = RotateHtmlYUpVector(
        HtmlNormal,
        Transform.PitchDegrees * Frontier::Constants::DegToRadFloat,
        Transform.YawDegrees * Frontier::Constants::DegToRadFloat,
        Transform.RollDegrees * Frontier::Constants::DegToRadFloat);
    return Frontier::Mathematics::NormalizeAxis(
        Frontier::Mathematics::ConvertVectorBetweenBases(
            Rotated,
            Frontier::Mathematics::WorldCoordinateBasis::RightHandedYUp,
            Frontier::Mathematics::WorldCoordinateBasis::RightHandedZUp),
        { 0.0f, 0.0f, 1.0f });
}

LinearRGB SrgbHexToLinearRgb(uint32_t HexRgb) noexcept
{
    const Frontier::ColourSpace::SrgbRgba8 Srgb {
        static_cast<uint8_t>((HexRgb >> 16u) & 0xFFu),
        static_cast<uint8_t>((HexRgb >> 8u) & 0xFFu),
        static_cast<uint8_t>(HexRgb & 0xFFu),
        255u,
    };
    const Frontier::ColourSpace::LinearRgba Lin = Frontier::ColourSpace::ConvertSrgb8ToLinear(
        Srgb,
        Frontier::ColourSpace::TransferCurveCategory::PiecewiseSrgb);
    return Lin.ToLinearRgb();
}

float ComputeTripodMountHeight(const TripodParams& Params) noexcept
{
    return Params.StandHeight + Params.ColumnLift + (Params.Casters ? 0.08f : 0.0f) + (Params.FloorMat ? 0.032f : 0.0f);
}

void RegisterStudioBaseMaterials(
    Frontier::MaterialSpace::CompactMaterialRegister& MaterialRegister,
    StudioMaterialPalette&                            OutPalette)
{
    using namespace Frontier::MaterialSpace;

    MaterialRegister.ClearRegister();
    OutPalette = {};

    const auto Reg = [&](StudioMaterialCode Code, std::string_view Name, SurfaceMaterial Mat)
    {
        Mat.MaterialName = std::string(Name);
        const uint16_t Slot = MaterialRegister.RegisterMaterial(Mat);
        OutPalette.Slots[static_cast<std::size_t>(Code)] = Slot;
    };

    // Slot 0 is pre-reserved as default studio clay inside CompactMaterialRegister::ClearRegister()
    OutPalette.Slots[static_cast<std::size_t>(StudioMaterialCode::DefaultClay)] = 0u;

    // Cyclorama cove: balanced neutral 40% photographic studio plaster cove
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor      = SrgbHexToLinearRgb(0xaab0ba);
        M.Specular.Roughness     = 0.48f;
        M.Specular.DielectricIOR = 1.44f;
        M.Specular.SpecularWeight = 0.36f;
        Reg(StudioMaterialCode::Cyclorama, "StudioCyclorama", M);
    }

    // Glossy studio slate tiles (alternating subtle charcoal slate pair)
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor            = SrgbHexToLinearRgb(0x1b1f26);
        M.Specular.Roughness           = 0.14f;
        M.Specular.DielectricIOR       = 1.54f;
        M.Specular.SpecularWeight      = 0.72f;
        M.Clearcoat.CoatWeight         = 0.45f;
        M.Clearcoat.CoatRoughness      = 0.08f;
        Reg(StudioMaterialCode::TiledFloor, "StudioTiledFloor", M);

        SurfaceMaterial Alt = M;
        Alt.Diffuse.BaseColor  = SrgbHexToLinearRgb(0x171b21);
        Alt.Specular.Roughness = 0.17f;
        Reg(StudioMaterialCode::TiledFloorAlt, "StudioTiledFloorAlt", Alt);
    }

    // Dark industrial ceiling ribs
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor      = SrgbHexToLinearRgb(0x181b20);
        M.Specular.Roughness     = 0.62f;
        M.Specular.Metalness     = 0.28f;
        M.Specular.DielectricIOR = 1.45f;
        Reg(StudioMaterialCode::CeilingRib, "StudioCeilingRib", M);
    }

    // Matte dark anodized aluminum / stand tubes
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor  = SrgbHexToLinearRgb(0x21252b);
        M.Specular.Roughness = 0.34f;
        M.Specular.Metalness = 0.78f;
        Reg(StudioMaterialCode::DarkMetal, "StudioDarkMetal", M);
    }

    // Brushed chrome steel (camera mounts, risers, hydraulic rods)
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor  = SrgbHexToLinearRgb(0xd8dde5);
        M.Specular.Roughness = 0.16f;
        M.Specular.Metalness = 0.96f;
        Reg(StudioMaterialCode::ChromeMetal, "StudioChromeMetal", M);
    }

    // Matte vulcanized rubber (floor mats, cables, grip rings)
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor      = SrgbHexToLinearRgb(0x121417);
        M.Specular.Roughness     = 0.82f;
        M.Specular.DielectricIOR = 1.42f;
        M.Specular.SpecularWeight = 0.25f;
        Reg(StudioMaterialCode::RubberMat, "StudioRubberMat", M);
    }

    // Luminous studio softbox silk diffuser
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor                        = { 0.95f, 0.96f, 0.99f };
        M.Specular.Roughness                       = 0.55f;
        M.PorosityLight.EmissionLuminanceNits      = 3.4f;
        M.PorosityLight.BlackbodyTemperatureKelvin = 5600.0f;
        Reg(StudioMaterialCode::EmissiveDiffuser, "StudioEmissiveDiffuser", M);
    }

    // Parabolic umbrella reflective silver fabric
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor  = SrgbHexToLinearRgb(0xcdd4de);
        M.Specular.Roughness = 0.18f;
        M.Specular.Metalness = 0.92f;
        Reg(StudioMaterialCode::UmbrellaSilver, "StudioUmbrellaSilver", M);
    }

    // Calibrated tether laptop & telemetry monitor LCD
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor                        = SrgbHexToLinearRgb(0x224c78);
        M.Specular.Roughness                       = 0.12f;
        M.PorosityLight.EmissionLuminanceNits      = 0.95f;
        M.PorosityLight.BlackbodyTemperatureKelvin = 6500.0f;
        Reg(StudioMaterialCode::TetherScreen, "StudioTetherScreen", M);
    }

    // Rugged Pelican flight case polymer
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor      = SrgbHexToLinearRgb(0x272b33);
        M.Specular.Roughness     = 0.56f;
        M.Specular.DielectricIOR = 1.48f;
        Reg(StudioMaterialCode::PelicanCase, "StudioPelicanCase", M);
    }

    // Lobby 2 Forest LED volume screen
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor                        = SrgbHexToLinearRgb(0x2d4c36);
        M.Specular.Roughness                       = 0.35f;
        M.PorosityLight.EmissionLuminanceNits      = 4.50f;
        M.PorosityLight.BlackbodyTemperatureKelvin = 5300.0f;
        Reg(StudioMaterialCode::ForestBackdrop, "StudioForestBackdrop", M);
    }

    // Duvetyne matte black studio V-flag & proscenium
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor       = SrgbHexToLinearRgb(0x0c0d10);
        M.Specular.Roughness      = 0.92f;
        M.Specular.SpecularWeight = 0.08f;
        Reg(StudioMaterialCode::BlackFlag, "StudioBlackFlag", M);
    }

    // Industrial steel fan housing & structural columns
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor  = SrgbHexToLinearRgb(0x3a404a);
        M.Specular.Roughness = 0.42f;
        M.Specular.Metalness = 0.72f;
        Reg(StudioMaterialCode::FanHousing, "StudioFanHousing", M);
    }

    // Spinning aluminum wind fan blade
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor  = SrgbHexToLinearRgb(0xaab3bf);
        M.Specular.Roughness = 0.24f;
        M.Specular.Metalness = 0.90f;
        Reg(StudioMaterialCode::FanBlade, "StudioFanBlade", M);
    }

    // Lobby 3 Cherry-Red mechanic's tool cabinets & kerbs
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor       = SrgbHexToLinearRgb(0xc81e24);
        M.Specular.Roughness      = 0.22f;
        M.Clearcoat.CoatWeight    = 0.68f;
        M.Clearcoat.CoatRoughness = 0.08f;
        Reg(StudioMaterialCode::CherryRedTool, "StudioCherryRedTool", M);
    }

    // Lobby 3 Safety Yellow lift posts, handrails & air hoses
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor    = SrgbHexToLinearRgb(0xe6a814);
        M.Specular.Roughness   = 0.32f;
        M.Clearcoat.CoatWeight = 0.45f;
        Reg(StudioMaterialCode::SafetyYellow, "StudioSafetyYellow", M);
    }

    // Lobby 3 Sealed concrete pit floor & grandstand masonry
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor       = SrgbHexToLinearRgb(0x5d626a);
        M.Specular.Roughness      = 0.48f;
        M.Specular.SpecularWeight = 0.46f;
        Reg(StudioMaterialCode::ConcreteGarage, "StudioConcreteGarage", M);
    }

    // Lobby 3 FIA track asphalt
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor       = SrgbHexToLinearRgb(0x23262b);
        M.Specular.Roughness      = 0.72f;
        M.Specular.SpecularWeight = 0.32f;
        Reg(StudioMaterialCode::TrackAsphalt, "StudioTrackAsphalt", M);
    }

    // Lobby 3 Crisp white FIA track markings & kerbs
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor  = SrgbHexToLinearRgb(0xe8ecf0);
        M.Specular.Roughness = 0.52f;
        Reg(StudioMaterialCode::TrackPaintWhite, "StudioTrackPaintWhite", M);
    }

    // Lobby 3 Mowed trackside grass runoff
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor       = SrgbHexToLinearRgb(0x3a642e);
        M.Specular.Roughness      = 0.84f;
        M.Specular.SpecularWeight = 0.18f;
        Reg(StudioMaterialCode::TrackGrass, "StudioTrackGrass", M);
    }

    // Lobby 3 Stadium canopy blue & gas cylinders
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor  = SrgbHexToLinearRgb(0x25578f);
        M.Specular.Roughness = 0.34f;
        M.Specular.Metalness = 0.42f;
        Reg(StudioMaterialCode::StadiumSkyBlue, "StudioStadiumSkyBlue", M);
    }

    // Lobby 3 Stadium afternoon sky dome
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor                        = SrgbHexToLinearRgb(0x88b8ea);
        M.Specular.Roughness                       = 0.90f;
        M.PorosityLight.EmissionLuminanceNits      = 0.90f;
        M.PorosityLight.BlackbodyTemperatureKelvin = 6200.0f;
        Reg(StudioMaterialCode::StadiumSkyDome, "StudioStadiumSkyDome", M);
    }

    // Lobby 3 Butcher-block oak workbench top
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor       = SrgbHexToLinearRgb(0x6e4a2b);
        M.Specular.Roughness      = 0.56f;
        M.Specular.SpecularWeight = 0.36f;
        Reg(StudioMaterialCode::TimberBench, "StudioTimberBench", M);
    }

    // Shared automotive component materials
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor  = SrgbHexToLinearRgb(0xbec6d1);
        M.Specular.Roughness = 0.18f;
        M.Specular.Metalness = 0.95f;
        Reg(StudioMaterialCode::VehicleRimAlloy, "VehicleRimAlloy", M);
    }
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor       = SrgbHexToLinearRgb(0x0b0d10);
        M.Specular.Roughness      = 0.08f;
        M.Clearcoat.CoatWeight    = 0.92f;
        M.Clearcoat.CoatRoughness = 0.04f;
        Reg(StudioMaterialCode::VehicleGlossCanopy, "VehicleGlossCanopy", M);
    }
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor               = SrgbHexToLinearRgb(0x0c141d);
        M.Specular.Roughness              = 0.04f;
        M.Specular.DielectricIOR          = 1.52f;
        M.Specular.SpecularWeight         = 0.95f;
        M.Clearcoat.CoatWeight            = 1.0f;
        M.Clearcoat.CoatRoughness         = 0.02f;
        M.Transmission.TransmissionWeight = 0.38f;
        Reg(StudioMaterialCode::VehicleGlass, "VehicleGlass", M);
    }
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor  = SrgbHexToLinearRgb(0x16181c);
        M.Specular.Roughness = 0.58f;
        Reg(StudioMaterialCode::VehicleIntakePolymer, "VehicleIntakePolymer", M);
    }
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor       = SrgbHexToLinearRgb(0x101114);
        M.Specular.Roughness      = 0.84f;
        M.Specular.SpecularWeight = 0.22f;
        Reg(StudioMaterialCode::VehicleTireRubber, "VehicleTireRubber", M);
    }
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor  = SrgbHexToLinearRgb(0x7c838d);
        M.Specular.Roughness = 0.22f;
        M.Specular.Metalness = 0.90f;
        Reg(StudioMaterialCode::VehicleTrimMetal, "VehicleTrimMetal", M);
    }
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor                        = SrgbHexToLinearRgb(0x1f2630);
        M.Specular.Roughness                       = 0.05f;
        M.Specular.DielectricIOR                   = 1.54f;
        M.Specular.SpecularWeight                  = 0.96f;
        M.Clearcoat.CoatWeight                     = 1.00f;
        M.Clearcoat.CoatRoughness                  = 0.02f;
        M.Transmission.TransmissionWeight          = 0.28f;
        M.PorosityLight.EmissionLuminanceNits      = 0.18f;
        M.PorosityLight.BlackbodyTemperatureKelvin = 5900.0f;
        Reg(StudioMaterialCode::VehicleHeadlightOptic, "VehicleHeadlightOptic", M);
    }
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor                        = SrgbHexToLinearRgb(0xa81012);
        M.Specular.Roughness                       = 0.08f;
        M.PorosityLight.EmissionLuminanceNits      = 0.42f;
        M.PorosityLight.BlackbodyTemperatureKelvin = 2200.0f;
        Reg(StudioMaterialCode::VehicleTaillightOptic, "VehicleTaillightOptic", M);
    }
    {
        SurfaceMaterial M {};
        M.Diffuse.BaseColor    = SrgbHexToLinearRgb(0xc42026);
        M.Specular.Roughness   = 0.20f;
        M.Specular.Metalness   = 0.35f;
        M.Clearcoat.CoatWeight = 0.85f;
        Reg(StudioMaterialCode::VehicleBrakeCaliper, "VehicleBrakeCaliper", M);
    }
}

uint16_t RegisterVehicleBodyPaintMaterial(
    Frontier::MaterialSpace::CompactMaterialRegister& MaterialRegister,
    std::string_view                                  MaterialTitle,
    LinearRGB                                         BodyColorLinear,
    float                                             Metallic,
    float                                             Roughness,
    float                                             ClearcoatWeight)
{
    Frontier::MaterialSpace::SurfaceMaterial Paint {};
    Paint.MaterialName            = std::string(MaterialTitle);
    Paint.Diffuse.BaseColor       = BodyColorLinear;
    Paint.Diffuse.BaseWeight      = 1.0f;
    Paint.Specular.Roughness      = Roughness;
    Paint.Specular.Metalness      = Metallic;
    Paint.Specular.DielectricIOR  = 1.50f;
    Paint.Specular.SpecularWeight = 0.65f;
    Paint.Clearcoat.CoatWeight    = ClearcoatWeight;
    Paint.Clearcoat.CoatRoughness = 0.045f;
    Paint.Clearcoat.CoatIOR       = 1.54f;
    return MaterialRegister.RegisterMaterial(Paint);
}

//------------------------------------------------------------------------------------------------------------------------
//                                        PROCEDURAL SURFACE CONSTRUCTORS (PORTED 1:1)
//------------------------------------------------------------------------------------------------------------------------

StudioBuiltMesh BuildCycloramaSurface(
    const CycloramaParams&         Params,
    const StudioMaterialPalette&   Palette,
    const StudioMeshBakeTransform& Transform)
{
    HtmlStudioMeshBuilder B("StudioCyclorama", Palette, Transform);
    const float W         = Params.CoveWidth;
    const float D         = Params.FloorDepth;
    const float H         = Params.WallHeight;
    const float R         = std::min(Params.CoveRadius, std::min(D * 0.48f, H * 0.48f));
    const float HalfW     = W * 0.5f;
    const float FrontZ    = D * 0.42f;
    const float BackFlatZ = -D * 0.58f + R;
    const float PlinthY   = 0.026f;

    struct ProfilePoint { float Z; float Y; float Nz; float Ny; };
    std::vector<ProfilePoint> Profile;
    Profile.reserve(40u);
    Profile.push_back({ FrontZ, PlinthY, 0.0f, 1.0f });

    constexpr int FloorSteps = 6;
    for (int I = 1; I <= FloorSteps; ++I)
    {
        const float T = static_cast<float>(I) / static_cast<float>(FloorSteps);
        Profile.push_back({ FrontZ + (BackFlatZ - FrontZ) * T, PlinthY, 0.0f, 1.0f });
    }
    constexpr int CoveSegs = 26;
    for (int I = 1; I <= CoveSegs; ++I)
    {
        const float Angle = (static_cast<float>(I) / static_cast<float>(CoveSegs)) * (Frontier::Constants::PiFloat * 0.5f);
        const float Z     = BackFlatZ - std::sin(Angle) * R;
        const float Y     = PlinthY + R - std::cos(Angle) * R;
        Profile.push_back({ Z, Y, std::sin(Angle), std::cos(Angle) });
    }
    const float BackZ = BackFlatZ - R;
    Profile.push_back({ BackZ, H, 1.0f, 0.0f });

    constexpr int XSegs = 12;
    for (int X = 0; X < XSegs; ++X)
    {
        const float X0 = -HalfW + (static_cast<float>(X) / static_cast<float>(XSegs)) * W;
        const float X1 = -HalfW + (static_cast<float>(X + 1) / static_cast<float>(XSegs)) * W;
        for (std::size_t P = 0u; P + 1u < Profile.size(); ++P)
        {
            const auto& P0 = Profile[P];
            const auto& P1 = Profile[P + 1u];
            const Axis3D A { X0, P0.Y, P0.Z };
            const Axis3D Bpt { X1, P0.Y, P0.Z };
            const Axis3D C { X1, P1.Y, P1.Z };
            const Axis3D Dpt { X0, P1.Y, P1.Z };
            const Axis3D Na { 0.0f, P0.Ny, P0.Nz };
            const Axis3D Nb { 0.0f, P1.Ny, P1.Nz };
            const float U0 = static_cast<float>(X) / static_cast<float>(XSegs);
            const float U1 = static_cast<float>(X + 1) / static_cast<float>(XSegs);
            const float V0 = static_cast<float>(P) / static_cast<float>(Profile.size());
            const float V1 = static_cast<float>(P + 1u) / static_cast<float>(Profile.size());
            B.PushTri(A, Bpt, C, Na, Na, Nb, StudioMaterialCode::Cyclorama, { U0, V0 }, { U1, V0 }, { U1, V1 });
            B.PushTri(A, C, Dpt, Na, Nb, Nb, StudioMaterialCode::Cyclorama, { U0, V0 }, { U1, V1 }, { U0, V1 });
        }
    }

    // Crisp dark metal plinth edge strips resting on outer floor
    B.AddQuadFlat({ -HalfW, 0.0f, FrontZ }, {  HalfW, 0.0f, FrontZ }, {  HalfW, PlinthY, FrontZ }, { -HalfW, PlinthY, FrontZ }, StudioMaterialCode::DarkMetal);
    B.AddQuadFlat({ -HalfW, 0.0f, BackZ  }, { -HalfW, 0.0f, FrontZ }, { -HalfW, PlinthY, FrontZ }, { -HalfW, PlinthY, BackZ  }, StudioMaterialCode::DarkMetal);
    B.AddQuadFlat({  HalfW, 0.0f, FrontZ }, {  HalfW, 0.0f, BackZ  }, {  HalfW, PlinthY, BackZ  }, {  HalfW, PlinthY, FrontZ }, StudioMaterialCode::DarkMetal);

    if (Params.SideWings)
    {
        const float WingDepth  = D * 0.72f;
        const float WingHeight = H * 0.94f;
        const float ZBack      = BackZ;
        const float ZFront     = BackZ + WingDepth;
        for (const float Side : { -1.0f, 1.0f })
        {
            const float  XWall = Side * HalfW;
            const float  Nx    = -Side;
            const Axis3D Wn { Nx, 0.0f, 0.0f };
            // Subdivide side walls into 4x4 quads to avoid giant near-plane triangles
            constexpr int WzSeg = 4;
            constexpr int WySeg = 4;
            for (int Iz = 0; Iz < WzSeg; ++Iz)
            {
                const float Za = ZBack + (static_cast<float>(Iz) / WzSeg) * (ZFront - ZBack);
                const float Zb = ZBack + (static_cast<float>(Iz + 1) / WzSeg) * (ZFront - ZBack);
                for (int Iy = 0; Iy < WySeg; ++Iy)
                {
                    const float Ya = PlinthY + (static_cast<float>(Iy) / WySeg) * (WingHeight - PlinthY);
                    const float Yb = PlinthY + (static_cast<float>(Iy + 1) / WySeg) * (WingHeight - PlinthY);
                    if (Side < 0.0f)
                    {
                        B.PushTri({ XWall, Ya, Za }, { XWall, Ya, Zb }, { XWall, Yb, Zb }, Wn, Wn, Wn, StudioMaterialCode::Cyclorama);
                        B.PushTri({ XWall, Ya, Za }, { XWall, Yb, Zb }, { XWall, Yb, Za }, Wn, Wn, Wn, StudioMaterialCode::Cyclorama);
                    }
                    else
                    {
                        B.PushTri({ XWall, Ya, Zb }, { XWall, Ya, Za }, { XWall, Yb, Za }, Wn, Wn, Wn, StudioMaterialCode::Cyclorama);
                        B.PushTri({ XWall, Ya, Zb }, { XWall, Yb, Za }, { XWall, Yb, Zb }, Wn, Wn, Wn, StudioMaterialCode::Cyclorama);
                    }
                }
            }

            constexpr float Fillet = 0.85f;
            constexpr int   Steps  = 10;
            for (int S = 0; S < Steps; ++S)
            {
                const float A0  = (static_cast<float>(S) / Steps) * (Frontier::Constants::PiFloat * 0.5f);
                const float A1  = (static_cast<float>(S + 1) / Steps) * (Frontier::Constants::PiFloat * 0.5f);
                const float Fx0 = XWall - Side * (Fillet - std::sin(A0) * Fillet);
                const float Fy0 = PlinthY + Fillet - std::cos(A0) * Fillet;
                const float Fx1 = XWall - Side * (Fillet - std::sin(A1) * Fillet);
                const float Fy1 = PlinthY + Fillet - std::cos(A1) * Fillet;
                const Axis3D N0 { -Side * std::sin(A0), std::cos(A0), 0.0f };
                const Axis3D N1 { -Side * std::sin(A1), std::cos(A1), 0.0f };
                const float FzBack = ZBack + R * 0.7f;
                if (Side < 0.0f)
                {
                    B.PushTri({ Fx0, Fy0, FzBack }, { Fx0, Fy0, ZFront }, { Fx1, Fy1, ZFront }, N0, N0, N1, StudioMaterialCode::Cyclorama);
                    B.PushTri({ Fx0, Fy0, FzBack }, { Fx1, Fy1, ZFront }, { Fx1, Fy1, FzBack }, N0, N1, N1, StudioMaterialCode::Cyclorama);
                }
                else
                {
                    B.PushTri({ Fx0, Fy0, ZFront }, { Fx0, Fy0, FzBack }, { Fx1, Fy1, FzBack }, N0, N0, N1, StudioMaterialCode::Cyclorama);
                    B.PushTri({ Fx0, Fy0, ZFront }, { Fx1, Fy1, FzBack }, { Fx1, Fy1, ZFront }, N0, N1, N1, StudioMaterialCode::Cyclorama);
                }
            }
        }
    }

    return B.Finish();
}

StudioBuiltMesh BuildTiledFloorSurface(
    const TiledFloorParams&        Params,
    const StudioMaterialPalette&   Palette,
    const StudioMeshBakeTransform& Transform)
{
    HtmlStudioMeshBuilder B("StudioTiledFloor", Palette, Transform);
    const float Pitch  = std::max(0.8f, Params.TilePitch);
    const int   CountX = std::max(4, static_cast<int>(std::round(Params.FloorWidth / Pitch)));
    const int   CountZ = std::max(4, static_cast<int>(std::round(Params.FloorDepth / Pitch)));
    const float Dx     = Params.FloorWidth / static_cast<float>(CountX);
    const float Dz     = Params.FloorDepth / static_cast<float>(CountZ);
    const float StartX = -Params.FloorWidth * 0.5f;
    const float StartZ = -Params.FloorDepth * 0.5f;
    const Axis3D UpN { 0.0f, 1.0f, 0.0f };

    // Subdivided studio tile deck at Y = 0 (Z_world = 0) to prevent large-triangle near-plane clipping and show architectural grid
    for (int Ix = 0; Ix < CountX; ++Ix)
    {
        const float X0 = StartX + static_cast<float>(Ix) * Dx;
        const float X1 = X0 + Dx;
        for (int Iz = 0; Iz < CountZ; ++Iz)
        {
            const float Z0 = StartZ + static_cast<float>(Iz) * Dz;
            const float Z1 = Z0 + Dz;
            const StudioMaterialCode TileMat =
                (Params.CheckerTiles && (((Ix + Iz) & 1) != 0))
                    ? StudioMaterialCode::TiledFloorAlt
                    : StudioMaterialCode::TiledFloor;
            B.PushTri({ X0, 0.0f, Z1 }, { X1, 0.0f, Z1 }, { X1, 0.0f, Z0 }, UpN, UpN, UpN, TileMat, { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f });
            B.PushTri({ X0, 0.0f, Z1 }, { X1, 0.0f, Z0 }, { X0, 0.0f, Z0 }, UpN, UpN, UpN, TileMat, { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f });
        }
    }
    return B.Finish();
}

StudioBuiltMesh BuildCeilingTrussSurface(
    const CeilingTrussParams&      Params,
    const StudioMaterialPalette&   Palette,
    const StudioMeshBakeTransform& Transform)
{
    HtmlStudioMeshBuilder B("StudioCeilingTruss", Palette, Transform);
    const float W    = Params.SpanWidth;
    const float D    = Params.SpanDepth;
    const float H    = Params.CeilingHeight;
    const int   Ribs = static_cast<int>(std::clamp(Params.RibCount, 4u, 24u));

    B.AddBox({ 0.0f, H + 0.14f, 0.0f }, { W * 0.5f, 0.08f, D * 0.5f }, StudioMaterialCode::CeilingRib);
    for (int I = 0; I < Ribs; ++I)
    {
        const float Z = -D * 0.45f + (static_cast<float>(I) / static_cast<float>(Ribs - 1)) * (D * 0.9f);
        B.AddBox({ 0.0f, H, Z }, { W * 0.48f, 0.09f, 0.07f }, StudioMaterialCode::CeilingRib);
    }

    if (Params.TrussGrid)
    {
        for (const float Tx : { -5.6f, -2.8f, 0.0f, 2.8f, 5.6f })
        {
            B.AddBox({ Tx, H - 0.18f, 0.0f }, { 0.06f, 0.06f, D * 0.45f }, StudioMaterialCode::DarkMetal);
            B.AddBox({ Tx, H - 0.44f, 0.0f }, { 0.05f, 0.05f, D * 0.45f }, StudioMaterialCode::DarkMetal);
            constexpr int Bays = 12;
            for (int J = 0; J < Bays; ++J)
            {
                const float Z0 = -D * 0.42f + (static_cast<float>(J) / Bays) * D * 0.84f;
                const float Z1 = -D * 0.42f + (static_cast<float>(J + 1) / Bays) * D * 0.84f;
                B.AddTube({ Tx, H - 0.18f, Z0 }, { Tx, H - 0.44f, Z1 }, 0.02f, 0.02f, 6u, StudioMaterialCode::DarkMetal, false);
                B.AddTube({ Tx, H - 0.44f, Z0 }, { Tx, H - 0.18f, Z1 }, 0.02f, 0.02f, 6u, StudioMaterialCode::DarkMetal, false);
            }
        }
        for (const float Tz : { -4.6f, -1.5f, 1.5f, 4.6f })
        {
            B.AddBox({ 0.0f, H - 0.30f, Tz }, { W * 0.44f, 0.055f, 0.055f }, StudioMaterialCode::DarkMetal);
        }
    }
    return B.Finish();
}

StudioBuiltMesh BuildTripodSurface(
    const TripodParams&            Params,
    const StudioMaterialPalette&   Palette,
    const StudioMeshBakeTransform& Transform)
{
    HtmlStudioMeshBuilder B("StudioTripod", Palette, Transform);
    const float StandH    = Params.StandHeight;
    const float Lift      = Params.ColumnLift;
    const float SpreadRad = Params.LegSpreadDeg * Frontier::Constants::DegToRadFloat;
    const float TubeR     = Params.TubeRadius;
    const int   LegSegs   = static_cast<int>(std::clamp(Params.LegSegments, 2u, 4u));
    const float HeadTilt  = Params.HeadTiltDeg * Frontier::Constants::DegToRadFloat;

    const float BaseY    = (Params.FloorMat ? 0.032f : 0.0f) + (Params.Casters ? 0.08f : 0.0f);
    const float ApexY    = BaseY + StandH * 0.72f;
    const float LegReach = std::tan(SpreadRad) * (ApexY - BaseY + 0.14f);

    if (Params.FloorMat)
    {
        B.AddBox({ 0.0f, 0.016f, 0.0f }, { 0.58f, 0.016f, 0.44f }, StudioMaterialCode::RubberMat);
    }

    for (int L = 0; L < 3; ++L)
    {
        const float Ang   = (static_cast<float>(L) / 3.0f) * Frontier::Constants::TauFloat + Frontier::Constants::PiFloat / 6.0f;
        const float FootX = std::cos(Ang) * LegReach;
        const float FootZ = std::sin(Ang) * LegReach;
        const float TopX  = std::cos(Ang) * 0.05f;
        const float TopZ  = std::sin(Ang) * 0.05f;

        for (int S = 0; S < LegSegs; ++S)
        {
            const float T0  = static_cast<float>(S) / static_cast<float>(LegSegs);
            const float T1  = static_cast<float>(S + 1) / static_cast<float>(LegSegs);
            const Axis3D P0 { TopX + (FootX - TopX) * T0, ApexY + (BaseY - ApexY) * T0, TopZ + (FootZ - TopZ) * T0 };
            const Axis3D P1 { TopX + (FootX - TopX) * T1, ApexY + (BaseY - ApexY) * T1, TopZ + (FootZ - TopZ) * T1 };
            const float Rad = TubeR * (1.0f - static_cast<float>(S) * 0.18f);
            const StudioMaterialCode LegMat = (S == LegSegs - 1 && !Params.FloorMat)
                ? StudioMaterialCode::ChromeMetal
                : StudioMaterialCode::DarkMetal;
            B.AddTube(P0, P1, Rad, Rad * 0.94f, 10u, LegMat);
            if (S > 0)
            {
                B.AddTube({ P0.X, P0.Y + 0.025f, P0.Z }, { P0.X, P0.Y - 0.025f, P0.Z }, Rad * 1.45f, Rad * 1.45f, 10u, StudioMaterialCode::RubberMat);
            }
        }

        if (Params.Casters)
        {
            B.AddTube({ FootX, BaseY, FootZ }, { FootX, BaseY - 0.04f, FootZ }, 0.02f, 0.02f, 8u, StudioMaterialCode::ChromeMetal);
            B.AddTube(
                { FootX - std::sin(Ang) * 0.022f, 0.036f, FootZ + std::cos(Ang) * 0.022f },
                { FootX + std::sin(Ang) * 0.022f, 0.036f, FootZ - std::cos(Ang) * 0.022f },
                0.036f, 0.036f, 12u, StudioMaterialCode::RubberMat);
        }
        else
        {
            B.AddTube({ FootX, BaseY + 0.03f, FootZ }, { FootX, BaseY, FootZ }, TubeR * 1.35f, TubeR * 1.65f, 10u, StudioMaterialCode::RubberMat);
        }

        if (Params.Spreader)
        {
            constexpr float MidT = 0.56f;
            const float Mx = TopX + (FootX - TopX) * MidT;
            const float My = ApexY + (BaseY - ApexY) * MidT;
            const float Mz = TopZ + (FootZ - TopZ) * MidT;
            B.AddTube({ 0.0f, My - 0.03f, 0.0f }, { Mx, My, Mz }, TubeR * 0.58f, TubeR * 0.58f, 8u, StudioMaterialCode::DarkMetal);
        }
    }

    const float ColTopY = BaseY + StandH + Lift;
    B.AddTube({ 0.0f, ApexY - 0.08f, 0.0f }, { 0.0f, ApexY + 0.07f, 0.0f }, 0.056f, 0.05f, 14u, StudioMaterialCode::DarkMetal);
    B.AddTube({ 0.0f, BaseY + StandH * 0.25f, 0.0f }, { 0.0f, BaseY + StandH, 0.0f }, TubeR * 1.4f, TubeR * 1.32f, 12u, StudioMaterialCode::DarkMetal);
    B.AddTube({ 0.0f, BaseY + StandH - 0.05f, 0.0f }, { 0.0f, ColTopY, 0.0f }, TubeR * 1.05f, TubeR * 1.02f, 12u, StudioMaterialCode::ChromeMetal);

    B.AddBox({ 0.0f, ColTopY + 0.035f, 0.0f }, { 0.068f, 0.038f, 0.068f }, StudioMaterialCode::DarkMetal, HeadTilt, 0.0f, 0.0f);
    B.AddTube({ 0.05f, ColTopY + 0.03f, -0.02f }, { 0.07f, ColTopY - 0.09f, -0.28f }, 0.008f, 0.011f, 8u, StudioMaterialCode::DarkMetal);

    if (Params.PowerCable)
    {
        const std::array<Axis3D, 7u> CablePts {{
            {  0.035f, ColTopY - 0.02f, -0.04f },
            {  0.080f, ColTopY * 0.72f, -0.14f },
            {  0.050f, ColTopY * 0.36f, -0.12f },
            {  0.090f, 0.060f,          -0.28f },
            {  0.240f, 0.014f,          -0.58f },
            {  0.120f, 0.014f,          -0.95f },
            { -0.220f, 0.014f,          -1.28f },
        }};
        for (std::size_t I = 0u; I + 1u < CablePts.size(); ++I)
        {
            B.AddTube(CablePts[I], CablePts[I + 1u], 0.01f, 0.01f, 6u, StudioMaterialCode::RubberMat, false);
        }
        if (Params.Casters)
        {
            B.AddBox({ 0.24f, 0.055f, -0.58f }, { 0.11f, 0.055f, 0.075f }, StudioMaterialCode::PelicanCase, 0.0f, 0.3f, 0.0f);
        }
    }

    return B.Finish();
}

StudioBuiltMesh BuildCinemaCameraSurface(
    const CinemaCameraParams&      Params,
    const StudioMaterialPalette&   Palette,
    const StudioMeshBakeTransform& Transform)
{
    HtmlStudioMeshBuilder B("StudioCinemaCamera", Palette, Transform);
    const float Tilt   = Params.PitchDeg * Frontier::Constants::DegToRadFloat;
    const float CosT   = std::cos(Tilt);
    const float SinT   = std::sin(Tilt);
    constexpr float PivotY = 0.12f;

    const auto AimPt = [&](float X, float Y, float Z) noexcept -> Axis3D
    {
        const float Dy = Y - PivotY;
        return { X, PivotY + Dy * CosT - Z * SinT, Dy * SinT + Z * CosT };
    };

    B.AddBox(AimPt(0.0f, 0.03f, 0.0f), { 0.08f, 0.024f, 0.12f }, StudioMaterialCode::DarkMetal, Tilt, 0.0f, 0.0f);

    if (Params.CameraStyle == "Cinema Matte Box")
    {
        B.AddBox(AimPt(0.0f, 0.17f, -0.02f), { 0.11f, 0.115f, 0.18f }, StudioMaterialCode::DarkMetal, Tilt, 0.0f, 0.0f);
        for (const auto& Reel : std::array<std::pair<float, float>, 2u>{{ { -0.12f, 0.36f }, { 0.10f, 0.35f } }})
        {
            B.AddTube(AimPt(-0.055f, Reel.second, Reel.first), AimPt(0.055f, Reel.second, Reel.first), Params.ReelRadius, Params.ReelRadius, 20u, StudioMaterialCode::DarkMetal);
            B.AddTube(AimPt(-0.062f, Reel.second, Reel.first), AimPt(0.062f, Reel.second, Reel.first), Params.ReelRadius * 0.34f, Params.ReelRadius * 0.34f, 14u, StudioMaterialCode::ChromeMetal);
        }
        B.AddTube(AimPt(-0.115f, 0.16f, 0.04f), AimPt(-0.155f, 0.16f, 0.04f), 0.045f, 0.045f, 14u, StudioMaterialCode::RubberMat);
        B.AddTube(AimPt(-0.07f, 0.21f, -0.18f), AimPt(-0.09f, 0.22f, -0.32f), 0.026f, 0.036f, 12u, StudioMaterialCode::RubberMat);
    }
    else if (Params.CameraStyle == "Medium Format Tether")
    {
        B.AddBox(AimPt(0.0f, 0.15f, -0.01f),  { 0.095f, 0.095f, 0.11f },  StudioMaterialCode::DarkMetal,    Tilt, 0.0f, 0.0f);
        B.AddBox(AimPt(0.0f, 0.26f, -0.04f),  { 0.065f, 0.035f, 0.075f }, StudioMaterialCode::DarkMetal,    Tilt, 0.0f, 0.0f);
        B.AddBox(AimPt(0.0f, 0.15f, -0.125f), { 0.065f, 0.045f, 0.008f }, StudioMaterialCode::TetherScreen, Tilt, 0.0f, 0.0f);
    }
    else
    {
        B.AddBox(AimPt(0.0f, 0.12f, 0.0f), { 0.075f, 0.065f, 0.055f }, StudioMaterialCode::DarkMetal, Tilt, 0.0f, 0.0f);
    }

    constexpr float LensStartZ = 0.14f;
    const float     LensEndZ   = LensStartZ + Params.LensLength;
    B.AddTube(AimPt(0.0f, 0.16f, LensStartZ), AimPt(0.0f, 0.16f, LensEndZ), 0.058f, 0.064f, 18u, StudioMaterialCode::DarkMetal);
    B.AddTube(AimPt(0.0f, 0.16f, LensStartZ + Params.LensLength * 0.35f), AimPt(0.0f, 0.16f, LensStartZ + Params.LensLength * 0.52f), 0.069f, 0.069f, 18u, StudioMaterialCode::RubberMat);
    B.AddTube(AimPt(0.0f, 0.16f, LensEndZ - 0.012f), AimPt(0.0f, 0.16f, LensEndZ + 0.008f), 0.052f, 0.052f, 16u, StudioMaterialCode::VehicleGlass);

    if (Params.MatteBox)
    {
        const float Mz = LensEndZ + 0.045f;
        B.AddBox(AimPt(0.0f, 0.16f, Mz), { 0.13f, 0.095f, 0.045f }, StudioMaterialCode::DarkMetal, Tilt, 0.0f, 0.0f);
        B.AddBox(AimPt(0.0f, 0.265f, Mz + 0.055f), { 0.135f, 0.006f, 0.065f }, StudioMaterialCode::DarkMetal, Tilt - 0.35f, 0.0f, 0.0f);
    }
    if (Params.TopHandle)
    {
        B.AddTube(AimPt(0.0f, 0.29f, -0.09f), AimPt(0.0f, 0.29f, 0.10f), 0.014f, 0.014f, 10u, StudioMaterialCode::DarkMetal);
    }

    return B.Finish();
}

StudioBuiltMesh BuildLightFixtureSurface(
    const LightFixtureParams&      Params,
    const StudioMaterialPalette&   Palette,
    const StudioMeshBakeTransform& Transform)
{
    HtmlStudioMeshBuilder B("StudioLightFixture", Palette, Transform);
    const float Aw    = Params.ApertureWidth;
    const float Ah    = Params.ApertureHeight;
    const float Depth = Params.HousingDepth;
    const float Tilt  = Params.YokeTiltDeg * Frontier::Constants::DegToRadFloat;

    if (Params.FixtureStyle == "Overhead Softbox")
    {
        const float Hw  = Aw * 0.5f;
        const float Hh  = Ah * 0.5f;
        const float Hd  = Depth * 0.5f;
        constexpr float Rim = 0.13f;

        B.AddBox({ 0.0f, Hd, 0.0f },                { Hw + Rim, 0.03f, Hh + Rim }, StudioMaterialCode::DarkMetal);
        B.AddBox({ 0.0f, 0.0f,  Hh + Rim * 0.5f },  { Hw + Rim, Hd, Rim * 0.5f },  StudioMaterialCode::DarkMetal);
        B.AddBox({ 0.0f, 0.0f, -(Hh + Rim * 0.5f) },{ Hw + Rim, Hd, Rim * 0.5f },  StudioMaterialCode::DarkMetal);
        B.AddBox({  Hw + Rim * 0.5f, 0.0f, 0.0f },  { Rim * 0.5f, Hd, Hh },        StudioMaterialCode::DarkMetal);
        B.AddBox({ -(Hw + Rim * 0.5f), 0.0f, 0.0f },{ Rim * 0.5f, Hd, Hh },        StudioMaterialCode::DarkMetal);

        B.AddBox({ 0.0f, Hd + 0.075f, 0.0f }, { Hw * 0.65f, 0.055f, Hh * 0.22f }, StudioMaterialCode::DarkMetal);
        B.AddBox({  Hw * 0.34f, Hd + 0.11f, -Hh * 0.12f }, { 0.22f, 0.08f, 0.16f }, StudioMaterialCode::PelicanCase);
        B.AddBox({ -Hw * 0.34f, Hd + 0.11f, -Hh * 0.12f }, { 0.18f, 0.07f, 0.14f }, StudioMaterialCode::PelicanCase);

        const float  DiffY = -Hd + 0.018f;
        const Axis3D Dn { 0.0f, -1.0f, 0.0f };
        // Subdivide softbox diffuser into 4x2 quads cleanly
        constexpr int SxSeg = 4;
        constexpr int SzSeg = 2;
        for (int Ix = 0; Ix < SxSeg; ++Ix)
        {
            const float X0 = -Hw + (static_cast<float>(Ix) / SxSeg) * (2.0f * Hw);
            const float X1 = -Hw + (static_cast<float>(Ix + 1) / SxSeg) * (2.0f * Hw);
            for (int Iz = 0; Iz < SzSeg; ++Iz)
            {
                const float Z0 = -Hh + (static_cast<float>(Iz) / SzSeg) * (2.0f * Hh);
                const float Z1 = -Hh + (static_cast<float>(Iz + 1) / SzSeg) * (2.0f * Hh);
                B.PushTri({ X0, DiffY, Z0 }, { X1, DiffY, Z0 }, { X1, DiffY, Z1 }, Dn, Dn, Dn, StudioMaterialCode::EmissiveDiffuser);
                B.PushTri({ X0, DiffY, Z0 }, { X1, DiffY, Z1 }, { X0, DiffY, Z1 }, Dn, Dn, Dn, StudioMaterialCode::EmissiveDiffuser);
            }
        }

        const float Drop = Params.SuspensionDrop;
        if (Drop > 0.1f)
        {
            for (const float Bx : { -Hw * 0.84f, 0.0f, Hw * 0.84f })
            {
                for (const float Bz : { -Hh * 0.78f, Hh * 0.78f })
                {
                    B.AddTube({ Bx, Hd, Bz }, { Bx, Hd + 0.09f, Bz }, 0.026f, 0.02f, 8u, StudioMaterialCode::ChromeMetal);
                    B.AddTube({ Bx, Hd + 0.08f, Bz }, { Bx, Hd + Drop, Bz }, 0.012f, 0.012f, 8u, StudioMaterialCode::DarkMetal);
                    B.AddBox({ Bx, Hd + Drop - 0.05f, Bz }, { 0.055f, 0.055f, 0.055f }, StudioMaterialCode::DarkMetal);
                }
            }

            const float UmbilicalX = Hw * 0.34f;
            const float UmbilicalZ = -Hh * 0.12f;
            constexpr int CoilSteps = 22;
            Axis3D PrevPt { UmbilicalX, Hd + Drop, UmbilicalZ };
            for (int S = 1; S <= CoilSteps; ++S)
            {
                const float T       = static_cast<float>(S) / CoilSteps;
                const float Y       = Hd + Drop * (1.0f - T) + 0.14f * T;
                const float SpiralR = 0.045f * std::sin(T * Frontier::Constants::PiFloat);
                const float Ang     = T * Frontier::Constants::PiFloat * 10.0f;
                const Axis3D NextPt {
                    UmbilicalX + std::cos(Ang) * SpiralR,
                    Y,
                    UmbilicalZ + std::sin(Ang) * SpiralR,
                };
                B.AddTube(PrevPt, NextPt, 0.013f, 0.013f, 6u, StudioMaterialCode::RubberMat, false);
                PrevPt = NextPt;
            }
            B.AddTube({ -Hw * 0.34f, Hd + 0.14f, -Hh * 0.12f }, { -Hw * 0.34f, Hd + Drop, -Hh * 0.12f }, 0.011f, 0.011f, 6u, StudioMaterialCode::RubberMat);
            for (const float Sx : { -1.0f, 1.0f })
            {
                B.AddTube({ Sx * Hw * 0.34f, Hd + 0.09f, -Hh * 0.12f }, { Sx * Hw * 0.78f, Hd + 0.05f, -Hh * 0.12f }, 0.012f, 0.012f, 6u, StudioMaterialCode::RubberMat);
                B.AddTube({ Sx * Hw * 0.34f, Hd + 0.09f, -Hh * 0.12f }, { Sx * Hw * 0.34f, Hd + 0.05f,  Hh * 0.65f }, 0.012f, 0.012f, 6u, StudioMaterialCode::RubberMat);
            }
        }
        return B.Finish();
    }

    if (Params.FixtureStyle == "Barn-Door Fresnel")
    {
        const float Radius = std::max(0.14f, Aw * 0.45f);
        const float Flare  = Params.BarnDoorFlare * Frontier::Constants::DegToRadFloat;
        B.AddTube({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.10f, 0.0f }, 0.018f, 0.018f, 8u, StudioMaterialCode::DarkMetal);
        B.AddBox({ 0.0f, 0.09f, 0.0f }, { Radius + 0.06f, 0.014f, 0.02f }, StudioMaterialCode::DarkMetal);
        B.AddBox({ -(Radius + 0.05f), 0.24f, 0.0f }, { 0.014f, 0.16f, 0.022f }, StudioMaterialCode::DarkMetal);
        B.AddBox({   Radius + 0.05f,  0.24f, 0.0f }, { 0.014f, 0.16f, 0.022f }, StudioMaterialCode::DarkMetal);

        constexpr float Cy = 0.28f;
        const float CosT   = std::cos(Tilt);
        const float SinT   = std::sin(Tilt);
        const Axis3D Fwd { 0.0f, -SinT, CosT };
        const Axis3D Up  { 0.0f,  CosT, SinT };
        const Axis3D BackPt  { 0.0f, Cy - Fwd.Y * Depth * 0.55f, -Fwd.Z * Depth * 0.55f };
        const Axis3D FrontPt { 0.0f, Cy + Fwd.Y * Depth * 0.45f,  Fwd.Z * Depth * 0.45f };
        B.AddTube(BackPt, FrontPt, Radius * 0.88f, Radius, 18u, StudioMaterialCode::DarkMetal);
        const Axis3D LensPt { 0.0f, FrontPt.Y + Fwd.Y * 0.018f, FrontPt.Z + Fwd.Z * 0.018f };
        B.AddTube(FrontPt, LensPt, Radius * 0.82f, Radius * 0.82f, 18u, StudioMaterialCode::EmissiveDiffuser);

        const float LeafLen = Radius * 1.15f;
        for (const float Sy : { -1.0f, 1.0f })
        {
            const Axis3D Center {
                0.0f,
                LensPt.Y + Up.Y * Sy * (Radius + LeafLen * 0.42f * std::sin(Flare)) + Fwd.Y * LeafLen * 0.48f * std::cos(Flare),
                LensPt.Z + Up.Z * Sy * (Radius + LeafLen * 0.42f * std::sin(Flare)) + Fwd.Z * LeafLen * 0.48f * std::cos(Flare),
            };
            B.AddBox(Center, { Radius * 0.96f, 0.008f, LeafLen * 0.5f }, StudioMaterialCode::DarkMetal, Tilt - Sy * Flare, 0.0f, 0.0f);
        }
        for (const float Sx : { -1.0f, 1.0f })
        {
            const Axis3D Center {
                Sx * (Radius + LeafLen * 0.42f * std::sin(Flare)),
                LensPt.Y + Fwd.Y * LeafLen * 0.48f * std::cos(Flare),
                LensPt.Z + Fwd.Z * LeafLen * 0.48f * std::cos(Flare),
            };
            B.AddBox(Center, { 0.008f, Radius * 0.96f, LeafLen * 0.5f }, StudioMaterialCode::DarkMetal, Tilt, Sx * Flare, 0.0f);
        }
        return B.Finish();
    }

    if (Params.FixtureStyle == "Parabolic Umbrella")
    {
        const uint32_t Ribs      = std::clamp(Params.UmbrellaRibs, 8u, 20u);
        const float    R         = std::max(0.45f, Aw * 0.5f);
        const float    BowlDepth = std::max(0.28f, Depth);
        constexpr float Cy       = 0.24f;
        const float CosT         = std::cos(Tilt);
        const float SinT         = std::sin(Tilt);
        const Axis3D Fwd { 0.0f, -SinT, CosT };
        const Axis3D Up  { 0.0f,  CosT, SinT };

        B.AddBox({ 0.0f, 0.09f, 0.0f }, { 0.055f, 0.09f, 0.08f }, StudioMaterialCode::DarkMetal, Tilt, 0.0f, 0.0f);
        const Axis3D Tip   { 0.0f, Cy - Fwd.Y * (BowlDepth + 0.12f), -Fwd.Z * (BowlDepth + 0.12f) };
        const Axis3D Mouth { 0.0f, Cy + Fwd.Y * 0.14f,                Fwd.Z * 0.14f };
        B.AddTube(Tip, Mouth, 0.009f, 0.009f, 8u, StudioMaterialCode::ChromeMetal);
        B.AddSphere({ 0.0f, Cy + Fwd.Y * 0.04f, Fwd.Z * 0.04f }, 0.065f, 10u, 12u, StudioMaterialCode::EmissiveDiffuser);

        constexpr uint32_t Rings = 5u;
        std::vector<std::vector<Axis3D>> InnerPts(Rings + 1u);
        std::vector<std::vector<Axis3D>> OuterPts(Rings + 1u);
        for (uint32_t Ri = 0u; Ri <= Rings; ++Ri)
        {
            const float T    = static_cast<float>(Ri) / Rings;
            const float Rad  = std::max(0.018f, R * std::sin(T * Frontier::Constants::PiFloat * 0.5f));
            const float ZOff = -(1.0f - T * T) * BowlDepth;
            InnerPts[Ri].resize(Ribs);
            OuterPts[Ri].resize(Ribs);
            for (uint32_t S = 0u; S < Ribs; ++S)
            {
                const float A       = (static_cast<float>(S) / Ribs) * Frontier::Constants::TauFloat;
                const float Cx      = std::cos(A) * Rad;
                const float CyLocal = std::sin(A) * Rad;
                InnerPts[Ri][S] = {
                    Cx,
                    Cy + Up.Y * CyLocal + Fwd.Y * ZOff,
                    Up.Z * CyLocal + Fwd.Z * ZOff,
                };
                // Offset outer matte black canopy 16mm behind silver inner skin to eliminate Z-fighting
                const float OutRad = Rad * 1.016f;
                const float OutZ   = ZOff - 0.016f;
                OuterPts[Ri][S] = {
                    std::cos(A) * OutRad,
                    Cy + Up.Y * std::sin(A) * OutRad + Fwd.Y * OutZ,
                    Up.Z * std::sin(A) * OutRad + Fwd.Z * OutZ,
                };
            }
        }
        for (uint32_t Ri = 0u; Ri < Rings; ++Ri)
        {
            for (uint32_t S = 0u; S < Ribs; ++S)
            {
                const uint32_t S1 = (S + 1u) % Ribs;
                B.AddQuadFlat(OuterPts[Ri][S], OuterPts[Ri][S1], OuterPts[Ri + 1u][S1], OuterPts[Ri + 1u][S], StudioMaterialCode::DarkMetal);
                B.AddQuadFlat(InnerPts[Ri + 1u][S], InnerPts[Ri + 1u][S1], InnerPts[Ri][S1], InnerPts[Ri][S], StudioMaterialCode::UmbrellaSilver);
            }
        }
        for (uint32_t S = 0u; S < Ribs; S += 2u)
        {
            B.AddTube(Tip, InnerPts[Rings][S], 0.005f, 0.004f, 6u, StudioMaterialCode::ChromeMetal, false);
        }
        return B.Finish();
    }

    if (Params.FixtureStyle == "Studio LED Panel")
    {
        const float Hw = std::max(0.25f, Aw * 0.5f);
        const float Hh = std::max(0.18f, Ah * 0.5f);
        const float Cy = Hh + 0.14f;
        B.AddTube({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.09f, 0.0f }, 0.018f, 0.018f, 8u, StudioMaterialCode::DarkMetal);
        B.AddBox({ 0.0f, 0.09f, 0.0f }, { Hw + 0.06f, 0.014f, 0.02f }, StudioMaterialCode::DarkMetal);
        B.AddBox({ -(Hw + 0.05f), Cy * 0.58f, 0.0f }, { 0.015f, Cy * 0.45f, 0.02f }, StudioMaterialCode::DarkMetal);
        B.AddBox({   Hw + 0.05f,  Cy * 0.58f, 0.0f }, { 0.015f, Cy * 0.45f, 0.02f }, StudioMaterialCode::DarkMetal);
        B.AddBox({ 0.0f, Cy, 0.0f }, { Hw, Hh, Depth * 0.45f }, StudioMaterialCode::DarkMetal, Tilt, 0.0f, 0.0f);
        const float CosT = std::cos(Tilt);
        const float SinT = std::sin(Tilt);
        const float FaceOff = Depth * 0.46f;
        B.AddBox({ 0.0f, Cy - SinT * FaceOff, CosT * FaceOff }, { Hw * 0.92f, Hh * 0.9f, 0.008f }, StudioMaterialCode::EmissiveDiffuser, Tilt, 0.0f, 0.0f);
        return B.Finish();
    }

    // Octa Softbox
    const float R     = std::max(0.32f, Aw * 0.5f);
    constexpr float Cy = 0.22f;
    const float CosT  = std::cos(Tilt);
    const float SinT  = std::sin(Tilt);
    const Axis3D Fwd { 0.0f, -SinT, CosT };
    const Axis3D Up  { 0.0f,  CosT, SinT };
    const Axis3D Back  { 0.0f, Cy - Fwd.Y * Depth * 0.65f, -Fwd.Z * Depth * 0.65f };
    const Axis3D Front { 0.0f, Cy + Fwd.Y * Depth * 0.35f,  Fwd.Z * Depth * 0.35f };
    B.AddBox({ 0.0f, 0.08f, 0.0f }, { 0.06f, 0.08f, 0.09f }, StudioMaterialCode::DarkMetal, Tilt, 0.0f, 0.0f);

    constexpr uint32_t Sides = 8u;
    std::array<Axis3D, Sides> BackRing {}, FrontRing {};
    for (uint32_t S = 0u; S < Sides; ++S)
    {
        const float A  = (static_cast<float>(S) / Sides) * Frontier::Constants::TauFloat + Frontier::Constants::PiFloat / 8.0f;
        const float Ca = std::cos(A);
        const float Sa = std::sin(A);
        BackRing[S]  = { Ca * 0.1f, Back.Y  + Up.Y * Sa * 0.1f, Back.Z  + Up.Z * Sa * 0.1f };
        FrontRing[S] = { Ca * R,    Front.Y + Up.Y * Sa * R,    Front.Z + Up.Z * Sa * R };
    }
    for (uint32_t S = 0u; S < Sides; ++S)
    {
        const uint32_t S1 = (S + 1u) % Sides;
        B.AddQuadFlat(BackRing[S], BackRing[S1], FrontRing[S1], FrontRing[S], StudioMaterialCode::DarkMetal);
        B.PushTri(Front, FrontRing[S], FrontRing[S1], Fwd, Fwd, Fwd, StudioMaterialCode::EmissiveDiffuser);
    }
    return B.Finish();
}

StudioBuiltMesh BuildWorkstationSurface(
    const WorkstationParams&       Params,
    const StudioMaterialPalette&   Palette,
    const StudioMeshBakeTransform& Transform)
{
    HtmlStudioMeshBuilder B("StudioWorkstation", Palette, Transform);
    const float Dw = Params.DeskWidth;
    const float Dd = Params.DeskDepth;
    const float Dh = Params.DeskHeight;

    if (Params.HasDesk)
    {
        B.AddBox({ 0.0f, Dh, 0.0f }, { Dw * 0.5f, 0.024f, Dd * 0.5f }, StudioMaterialCode::DarkMetal);
        for (const float Sx : { -1.0f, 1.0f })
        {
            for (const float Sz : { -1.0f, 1.0f })
            {
                const float Tx = Sx * (Dw * 0.44f);
                const float Tz = Sz * (Dd * 0.40f);
                const float Fx = Sx * (Dw * 0.49f);
                const float Fz = Sz * (Dd * 0.46f);
                B.AddTube({ Tx, Dh - 0.02f, Tz }, { Fx, 0.01f, Fz }, 0.0085f, 0.0085f, 8u, StudioMaterialCode::DarkMetal);
                B.AddTube({ Tx - Sx * 0.04f, Dh - 0.02f, Tz }, { Fx, 0.01f, Fz }, 0.008f, 0.008f, 8u, StudioMaterialCode::DarkMetal);
            }
        }
        B.AddBox({ -0.12f,  Dh + 0.032f,  0.02f },  { 0.22f, 0.009f, 0.15f },  StudioMaterialCode::DarkMetal,    0.0f,   -0.18f, 0.0f);
        B.AddBox({ -0.14f,  Dh + 0.150f, -0.13f },  { 0.22f, 0.135f, 0.008f }, StudioMaterialCode::DarkMetal,   -0.22f,  -0.18f, 0.0f);
        B.AddBox({ -0.135f, Dh + 0.150f, -0.12f },  { 0.20f, 0.118f, 0.004f }, StudioMaterialCode::TetherScreen,-0.22f,  -0.18f, 0.0f);
        B.AddBox({  0.36f,  Dh + 0.045f,  0.06f },  { 0.11f, 0.025f, 0.08f },  StudioMaterialCode::PelicanCase,  0.0f,    0.22f, 0.0f);
    }

    if (Params.FlightCases)
    {
        const float Cx = Params.HasDesk ? Dw * 0.52f + 0.28f : 0.0f;
        const float Cz = Params.HasDesk ? -0.12f : 0.0f;
        B.AddBox({ Cx, 0.18f, Cz }, { 0.34f, 0.18f, 0.24f }, StudioMaterialCode::PelicanCase, 0.0f, 0.15f, 0.0f);
        for (int R = -2; R <= 2; ++R)
        {
            B.AddBox({ Cx + static_cast<float>(R) * 0.11f, 0.368f, Cz }, { 0.018f, 0.01f, 0.22f }, StudioMaterialCode::DarkMetal, 0.0f, 0.15f, 0.0f);
        }
        B.AddBox({ Cx - 0.44f, 0.26f, Cz - 0.22f }, { 0.16f, 0.26f, 0.20f }, StudioMaterialCode::PelicanCase, 0.0f, -0.24f, 0.0f);
        B.AddTube({ Cx - 0.44f, 0.53f, Cz - 0.28f }, { Cx - 0.44f, 0.53f, Cz - 0.16f }, 0.012f, 0.012f, 8u, StudioMaterialCode::ChromeMetal);
    }

    if (Params.CoiledCables)
    {
        constexpr int Loops = 22;
        const float Cx = Params.HasDesk ? -Dw * 0.48f : 0.42f;
        const float Cz = Params.HasDesk ? 0.38f : 0.28f;
        for (int I = 0; I < Loops; ++I)
        {
            const float A0 = (static_cast<float>(I) / Loops) * Frontier::Constants::TauFloat;
            const float A1 = (static_cast<float>(I + 1) / Loops) * Frontier::Constants::TauFloat;
            const float R0 = 0.26f + 0.03f * std::sin(A0 * 2.0f);
            const float R1 = 0.26f + 0.03f * std::sin(A1 * 2.0f);
            B.AddTube(
                { Cx + std::cos(A0) * R0, 0.014f, Cz + std::sin(A0) * R0 },
                { Cx + std::cos(A1) * R1, 0.014f, Cz + std::sin(A1) * R1 },
                0.012f, 0.012f, 6u, StudioMaterialCode::RubberMat, false);
        }
        B.AddTube({ Cx - 0.26f, 0.014f, Cz }, { Cx - 1.15f, 0.014f, Cz - 0.65f }, 0.011f, 0.011f, 6u, StudioMaterialCode::RubberMat);
    }

    return B.Finish();
}

StudioBuiltMesh BuildForestBackdropSurface(
    const ForestBackdropParams&    Params,
    const StudioMaterialPalette&   Palette,
    const StudioMeshBakeTransform& Transform)
{
    HtmlStudioMeshBuilder B("StudioForestBackdrop", Palette, Transform);
    const float ScreenW = Params.ScreenWidth;
    const float ScreenH = Params.ScreenHeight;
    const float FrameW  = Params.ProsceniumWidth;
    const float FrameH  = Params.WallHeight;
    const float HalfSw  = ScreenW * 0.5f;
    const float HalfFw  = FrameW * 0.5f;
    constexpr float BottomY = 0.08f;
    const float TopScreenY  = BottomY + ScreenH;

    constexpr int SegX = 12;
    constexpr int SegY = 8;
    const Axis3D Fn { 0.0f, 0.0f, 1.0f };
    for (int Iy = 0; Iy < SegY; ++Iy)
    {
        const float Y0 = BottomY + (static_cast<float>(Iy) / SegY) * ScreenH;
        const float Y1 = BottomY + (static_cast<float>(Iy + 1) / SegY) * ScreenH;
        for (int Ix = 0; Ix < SegX; ++Ix)
        {
            const float X0 = -HalfSw + (static_cast<float>(Ix) / SegX) * ScreenW;
            const float X1 = -HalfSw + (static_cast<float>(Ix + 1) / SegX) * ScreenW;
            B.PushTri({ X0, Y0, 0.02f }, { X1, Y0, 0.02f }, { X1, Y1, 0.02f }, Fn, Fn, Fn, StudioMaterialCode::ForestBackdrop);
            B.PushTri({ X0, Y0, 0.02f }, { X1, Y1, 0.02f }, { X0, Y1, 0.02f }, Fn, Fn, Fn, StudioMaterialCode::ForestBackdrop);
        }
    }

    const float SideW    = std::max(1.2f, (FrameW - ScreenW) * 0.5f);
    const float TopBandH = std::max(0.4f, FrameH - TopScreenY);
    B.AddBox({ -(HalfSw + SideW * 0.5f), FrameH * 0.5f, -0.08f }, { SideW * 0.5f, FrameH * 0.5f, 0.16f }, StudioMaterialCode::BlackFlag);
    B.AddBox({   HalfSw + SideW * 0.5f,  FrameH * 0.5f, -0.08f }, { SideW * 0.5f, FrameH * 0.5f, 0.16f }, StudioMaterialCode::BlackFlag);
    B.AddBox({ 0.0f, TopScreenY + TopBandH * 0.5f, -0.08f }, { HalfSw, TopBandH * 0.5f, 0.16f }, StudioMaterialCode::BlackFlag);
    B.AddBox({ 0.0f, FrameH * 0.5f, -0.26f }, { HalfFw, FrameH * 0.5f, 0.06f }, StudioMaterialCode::DarkMetal);
    for (int Ix = -3; Ix <= 3; ++Ix)
    {
        B.AddTube({ static_cast<float>(Ix) * 2.6f, 0.0f, -0.34f }, { static_cast<float>(Ix) * 2.6f, FrameH, -0.34f }, 0.04f, 0.04f, 8u, StudioMaterialCode::DarkMetal);
    }

    constexpr float WingDepth = 7.4f;
    B.AddBox({ -HalfFw + 0.12f, FrameH * 0.5f, WingDepth * 0.5f }, { 0.14f, FrameH * 0.5f, WingDepth * 0.5f }, StudioMaterialCode::BlackFlag);
    B.AddBox({  HalfFw - 0.12f, FrameH * 0.5f, WingDepth * 0.5f }, { 0.14f, FrameH * 0.5f, WingDepth * 0.5f }, StudioMaterialCode::BlackFlag);

    if (Params.StudioFlags)
    {
        for (const float Side : { -1.0f, 1.0f })
        {
            const float Fx = Side * (HalfSw + 1.45f);
            constexpr float Fz = 5.4f;
            constexpr float Fy = 3.15f;
            const float FlagYaw = Side * -0.46f;
            B.AddBox({ Fx, Fy, Fz },          { 0.03f,  1.85f,  1.55f }, StudioMaterialCode::BlackFlag,   0.0f, FlagYaw, 0.0f);
            B.AddBox({ Fx, Fy + 1.86f, Fz },  { 0.035f, 0.028f, 1.58f }, StudioMaterialCode::ChromeMetal, 0.0f, FlagYaw, 0.0f);
            B.AddBox({ Fx, Fy - 1.86f, Fz },  { 0.035f, 0.028f, 1.58f }, StudioMaterialCode::ChromeMetal, 0.0f, FlagYaw, 0.0f);
            B.AddTube({ Fx, 0.04f, Fz }, { Fx, Fy - 1.82f, Fz }, 0.026f, 0.022f, 8u, StudioMaterialCode::ChromeMetal);
            for (int L = 0; L < 3; ++L)
            {
                const float La = (static_cast<float>(L) / 3.0f) * Frontier::Constants::TauFloat + 0.4f;
                B.AddTube({ Fx, 0.34f, Fz }, { Fx + std::cos(La) * 0.52f, 0.02f, Fz + std::sin(La) * 0.52f }, 0.015f, 0.013f, 6u, StudioMaterialCode::DarkMetal);
            }
        }
    }
    return B.Finish();
}

StudioBuiltMesh BuildStudioFanSurface(
    const StudioFanParams&         Params,
    const StudioMaterialPalette&   Palette,
    const StudioMeshBakeTransform& Transform)
{
    HtmlStudioMeshBuilder B("StudioWindFan", Palette, Transform);
    const float HubY  = Params.StandHeight;
    const float CageR = Params.FanRadius;
    const float Tilt  = Params.TiltDeg * Frontier::Constants::DegToRadFloat;
    const float CosT  = std::cos(Tilt);
    const float SinT  = std::sin(Tilt);

    B.AddBox({ 0.0f, 0.12f, 0.0f }, { 0.11f, 0.045f, 0.11f }, StudioMaterialCode::DarkMetal);
    for (int L = 0; L < 4; ++L)
    {
        const float A  = (static_cast<float>(L) / 4.0f) * Frontier::Constants::TauFloat + Frontier::Constants::PiFloat * 0.25f;
        const float Ex = std::cos(A) * 0.44f;
        const float Ez = std::sin(A) * 0.44f;
        B.AddTube({ 0.0f, 0.12f, 0.0f }, { Ex, 0.075f, Ez }, 0.022f, 0.018f, 8u, StudioMaterialCode::DarkMetal);
        B.AddTube({ Ex, 0.075f, Ez }, { Ex, 0.025f, Ez }, 0.024f, 0.024f, 8u, StudioMaterialCode::RubberMat);
    }
    B.AddTube({ 0.0f, 0.14f, 0.0f }, { 0.0f, HubY - CageR - 0.1f, 0.0f }, 0.028f, 0.025f, 10u, StudioMaterialCode::ChromeMetal);
    B.AddTube({ 0.0f, HubY - CageR - 0.14f, 0.0f }, { 0.0f, HubY - CageR - 0.05f, 0.0f }, 0.036f, 0.036f, 10u, StudioMaterialCode::DarkMetal);

    const float YokeW = CageR + 0.09f;
    B.AddBox({ 0.0f, HubY - CageR - 0.08f, 0.0f }, { YokeW, 0.02f, 0.028f }, StudioMaterialCode::FanHousing);
    B.AddBox({ -YokeW, HubY - (CageR + 0.08f) * 0.5f, 0.0f }, { 0.022f, (CageR + 0.08f) * 0.52f, 0.028f }, StudioMaterialCode::FanHousing);
    B.AddBox({  YokeW, HubY - (CageR + 0.08f) * 0.5f, 0.0f }, { 0.022f, (CageR + 0.08f) * 0.52f, 0.028f }, StudioMaterialCode::FanHousing);
    B.AddTube({ -YokeW - 0.05f, HubY, 0.0f }, { -YokeW + 0.02f, HubY, 0.0f }, 0.042f, 0.042f, 10u, StudioMaterialCode::DarkMetal);
    B.AddTube({  YokeW - 0.02f, HubY, 0.0f }, {  YokeW + 0.05f, HubY, 0.0f }, 0.042f, 0.042f, 10u, StudioMaterialCode::DarkMetal);

    const auto DrumPt = [&](float Lx, float Ly, float Lz) noexcept -> Axis3D
    {
        return { Lx, HubY + Ly * CosT - Lz * SinT, Ly * SinT + Lz * CosT };
    };

    constexpr int Segs        = 24;
    constexpr float HalfD     = 0.18f;
    for (int S = 0; S < Segs; ++S)
    {
        const float A0 = (static_cast<float>(S) / Segs) * Frontier::Constants::TauFloat;
        const float A1 = (static_cast<float>(S + 1) / Segs) * Frontier::Constants::TauFloat;
        const float C0 = std::cos(A0), S0 = std::sin(A0);
        const float C1 = std::cos(A1), S1 = std::sin(A1);
        B.AddQuadFlat(
            DrumPt(C0 * CageR, S0 * CageR, -HalfD),
            DrumPt(C1 * CageR, S1 * CageR, -HalfD),
            DrumPt(C1 * CageR, S1 * CageR,  HalfD),
            DrumPt(C0 * CageR, S0 * CageR,  HalfD),
            StudioMaterialCode::FanHousing);
        for (const float Zface : { -HalfD - 0.015f, HalfD + 0.025f })
        {
            for (const float Rs : { 0.38f, 0.68f, 0.96f })
            {
                B.AddTube(DrumPt(C0 * CageR * Rs, S0 * CageR * Rs, Zface), DrumPt(C1 * CageR * Rs, S1 * CageR * Rs, Zface), 0.005f, 0.005f, 5u, StudioMaterialCode::ChromeMetal, false);
            }
        }
    }
    for (int R = 0; R < 8; ++R)
    {
        const float A = (static_cast<float>(R) / 8.0f) * Frontier::Constants::TauFloat;
        B.AddTube(DrumPt(0.0f, 0.0f,  HalfD + 0.028f), DrumPt(std::cos(A) * CageR, std::sin(A) * CageR,  HalfD + 0.022f), 0.0055f, 0.0055f, 5u, StudioMaterialCode::ChromeMetal, false);
        B.AddTube(DrumPt(0.0f, 0.0f, -HalfD - 0.018f), DrumPt(std::cos(A) * CageR, std::sin(A) * CageR, -HalfD - 0.015f), 0.0055f, 0.0055f, 5u, StudioMaterialCode::ChromeMetal, false);
    }

    B.AddTube(DrumPt(0.0f, 0.0f, -HalfD - 0.24f), DrumPt(0.0f, 0.0f, 0.04f), 0.11f, 0.10f, 14u, StudioMaterialCode::DarkMetal);
    B.AddBox(DrumPt(0.0f, -0.15f, -HalfD - 0.14f), { 0.075f, 0.06f, 0.065f }, StudioMaterialCode::PelicanCase, Tilt, 0.0f, 0.0f);

    constexpr int BladeCount = 5;
    for (int K = 0; K < BladeCount; ++K)
    {
        const float BaseAng = (static_cast<float>(K) / BladeCount) * Frontier::Constants::TauFloat;
        constexpr float RInner = 0.07f;
        const float     ROuter = CageR * 0.89f;
        constexpr float Sweep  = 0.44f;
        constexpr float PitchZ = 0.055f;
        const float A0 = BaseAng - Sweep * 0.45f;
        const float A1 = BaseAng + Sweep * 0.55f;
        const Axis3D P00 = DrumPt(std::cos(A0) * RInner, std::sin(A0) * RInner, -PitchZ * 0.4f);
        const Axis3D P10 = DrumPt(std::cos(A1) * RInner, std::sin(A1) * RInner,  PitchZ * 0.4f);
        const Axis3D P11 = DrumPt(std::cos(A1 + 0.14f) * ROuter, std::sin(A1 + 0.14f) * ROuter,  PitchZ);
        const Axis3D P01 = DrumPt(std::cos(A0 + 0.08f) * ROuter, std::sin(A0 + 0.08f) * ROuter, -PitchZ);
        const Axis3D N   = HtmlStudioMeshBuilder::ComputeFlatNormal(P00, P10, P11);
        B.PushTri(P00, P10, P11, N, N, N, StudioMaterialCode::FanBlade);
        B.PushTri(P00, P11, P01, N, N, N, StudioMaterialCode::FanBlade);
    }
    B.AddTube(DrumPt(0.0f, 0.0f, -0.02f), DrumPt(0.0f, 0.0f, 0.09f), 0.068f, 0.02f, 12u, StudioMaterialCode::ChromeMetal);
    B.AddTube(DrumPt(0.0f, -0.18f, -HalfD - 0.14f), { 0.14f, 0.55f, -0.35f }, 0.011f, 0.011f, 6u, StudioMaterialCode::RubberMat);
    B.AddTube({ 0.14f, 0.55f, -0.35f }, { 0.22f, 0.014f, -0.68f }, 0.011f, 0.011f, 6u, StudioMaterialCode::RubberMat);
    B.AddTube({ 0.22f, 0.014f, -0.68f }, { -0.42f, 0.014f, -1.25f }, 0.011f, 0.011f, 6u, StudioMaterialCode::RubberMat);
    return B.Finish();
}

StudioBuiltMesh BuildGarageShellSurface(
    const GarageShellParams&       Params,
    const StudioMaterialPalette&   Palette,
    const StudioMeshBakeTransform& Transform)
{
    HtmlStudioMeshBuilder B("StudioGarageShell", Palette, Transform);
    const float W     = Params.GarageWidth;
    const float D     = Params.GarageDepth;
    const float H     = Params.GarageHeight;
    const float HalfW = W * 0.5f;
    constexpr float BackZ = -6.2f;

    B.AddBox({ -4.8f, 0.008f, BackZ }, { 6.1f, 0.008f, 0.32f }, StudioMaterialCode::FanHousing);
    B.AddTube({ -10.2f, 0.0f, BackZ + 0.55f }, { -10.2f, 0.85f, BackZ + 0.55f }, 0.075f, 0.075f, 10u, StudioMaterialCode::SafetyYellow);
    B.AddTube({  0.62f, 0.0f, BackZ + 0.55f }, {  0.62f, 0.85f, BackZ + 0.55f }, 0.075f, 0.075f, 10u, StudioMaterialCode::SafetyYellow);

    B.AddBox({ -HalfW + 0.55f, H * 0.5f, BackZ }, { 0.55f, H * 0.5f, 0.55f }, StudioMaterialCode::ConcreteGarage);
    B.AddBox({  1.15f,         H * 0.5f, BackZ }, { 0.46f, H * 0.5f, 0.55f }, StudioMaterialCode::ConcreteGarage);
    B.AddBox({  6.20f,         H * 0.5f, BackZ }, { 0.42f, H * 0.5f, 0.55f }, StudioMaterialCode::ConcreteGarage);
    B.AddBox({  HalfW - 0.55f, H * 0.5f, BackZ }, { 0.55f, H * 0.5f, 0.55f }, StudioMaterialCode::ConcreteGarage);

    constexpr float ArchTop = 4.95f;
    B.AddBox({ -4.9f, ArchTop + (H - ArchTop) * 0.5f, BackZ }, { 6.2f, (H - ArchTop) * 0.5f, 0.42f }, StudioMaterialCode::ConcreteGarage);
    B.AddTube({ -10.2f, ArchTop - 0.22f, BackZ + 0.28f }, { 0.7f, ArchTop - 0.22f, BackZ + 0.28f }, 0.22f, 0.22f, 12u, StudioMaterialCode::FanHousing);
    B.AddBox({ -4.8f, ArchTop + 0.36f, BackZ + 0.44f }, { 0.42f, 0.22f, 0.03f }, StudioMaterialCode::CherryRedTool);

    const auto AddSecurityCageBay = [&](float XMin, float XMax, float ZPos)
    {
        const float Cx   = (XMin + XMax) * 0.5f;
        const float Span = XMax - XMin;
        B.AddBox({ Cx, 3.35f,     ZPos }, { Span * 0.5f, 0.26f, 0.34f }, StudioMaterialCode::ConcreteGarage);
        B.AddBox({ Cx, H - 0.32f, ZPos }, { Span * 0.5f, 0.32f, 0.34f }, StudioMaterialCode::ConcreteGarage);
        B.AddBox({ Cx, H * 0.5f, ZPos - 1.35f }, { Span * 0.52f, H * 0.5f, 0.12f }, StudioMaterialCode::BlackFlag);
        const int BarCount = std::max(10, static_cast<int>(std::round(Span * 3.4f)));
        for (int I = 0; I <= BarCount; ++I)
        {
            const float Bx = XMin + 0.12f + (static_cast<float>(I) / BarCount) * (Span - 0.24f);
            B.AddTube({ Bx, 0.08f, ZPos }, { Bx, 3.08f,     ZPos }, 0.015f, 0.015f, 6u, StudioMaterialCode::ChromeMetal, false);
            B.AddTube({ Bx, 3.62f, ZPos }, { Bx, H - 0.65f, ZPos }, 0.015f, 0.015f, 6u, StudioMaterialCode::ChromeMetal, false);
        }
        for (const float Ry : { 1.05f, 2.08f, 4.60f, 5.65f })
        {
            B.AddBox({ Cx, Ry, ZPos }, { Span * 0.49f, 0.035f, 0.04f }, StudioMaterialCode::DarkMetal);
        }
    };
    AddSecurityCageBay(1.62f, 5.76f, BackZ);
    AddSecurityCageBay(6.62f, HalfW - 1.1f, BackZ);

    for (const float Side : { -1.0f, 1.0f })
    {
        const float Wx = Side * HalfW;
        B.AddBox({ Wx, H * 0.5f, 1.5f }, { 0.22f, H * 0.5f, D * 0.5f }, StudioMaterialCode::ConcreteGarage);
        for (float Z = BackZ + 2.6f; Z <= BackZ + D - 2.2f; Z += 4.2f)
        {
            B.AddBox({ Wx - Side * 0.36f, H * 0.5f, Z }, { 0.24f, H * 0.5f, 0.24f }, StudioMaterialCode::FanHousing);
        }
        B.AddBox({ Wx - Side * 0.24f, 1.12f, 1.5f }, { 0.03f, 0.08f, D * 0.48f }, StudioMaterialCode::SafetyYellow);
    }

    if (Params.ShowMezzanine)
    {
        for (const float Side : { -1.0f, 1.0f })
        {
            const float CatX        = Side * (HalfW - 1.45f);
            constexpr float DeckY   = 3.35f;
            constexpr float BackDz  = -3.8f;
            constexpr float FrontDz = 6.6f;
            const float DeckMidZ    = (BackDz + FrontDz) * 0.5f;
            const float DeckHalfLen = (FrontDz - BackDz) * 0.5f;
            const float RailX       = CatX - Side * 1.2f;

            B.AddBox({ CatX, DeckY, DeckMidZ }, { 1.25f, 0.1f, DeckHalfLen }, StudioMaterialCode::FanHousing);
            B.AddBox({ CatX - Side * 1.24f, DeckY + 0.06f, DeckMidZ }, { 0.05f, 0.14f, DeckHalfLen }, StudioMaterialCode::SafetyYellow);
            B.AddBox({ CatX, DeckY + 0.04f, FrontDz }, { 1.25f, 0.12f, 0.06f }, StudioMaterialCode::SafetyYellow);

            for (const float Zp : { -3.4f, 1.6f, 6.45f })
            {
                B.AddBox({ CatX - Side * 1.15f, DeckY * 0.5f, Zp }, { 0.10f, DeckY * 0.5f, 0.10f }, StudioMaterialCode::DarkMetal);
                B.AddBox({ CatX - Side * 1.15f, 0.03f,        Zp }, { 0.18f, 0.03f,        0.18f }, StudioMaterialCode::SafetyYellow);
            }

            B.AddTube({ RailX, DeckY + 1.02f, BackDz }, { RailX, DeckY + 1.02f, FrontDz }, 0.026f, 0.026f, 6u, StudioMaterialCode::SafetyYellow);
            B.AddTube({ RailX, DeckY + 0.54f, BackDz }, { RailX, DeckY + 0.54f, FrontDz }, 0.018f, 0.018f, 6u, StudioMaterialCode::SafetyYellow);
            for (float Zp = BackDz; Zp <= FrontDz + 0.01f; Zp += 1.73f)
            {
                B.AddTube({ RailX, DeckY + 0.05f, Zp }, { RailX, DeckY + 1.04f, Zp }, 0.02f, 0.02f, 6u, StudioMaterialCode::DarkMetal);
            }
            B.AddTube({ RailX, DeckY + 1.02f, BackDz }, { CatX + Side * 1.18f, DeckY + 1.02f, BackDz }, 0.024f, 0.024f, 6u, StudioMaterialCode::SafetyYellow);
            B.AddTube({ RailX, DeckY + 0.54f, BackDz }, { CatX + Side * 1.18f, DeckY + 0.54f, BackDz }, 0.018f, 0.018f, 6u, StudioMaterialCode::SafetyYellow);

            constexpr float StairFootZ  = 10.55f;
            constexpr float StairRun    = StairFootZ - FrontDz;
            const float     StairCx     = CatX - Side * 0.12f;
            constexpr float StairHalfW  = 0.64f;
            const float     SlopeAng    = std::atan2(DeckY, StairRun);
            const float     StringerLen = std::hypot(DeckY, StairRun);

            for (const float SSide : { -1.0f, 1.0f })
            {
                const float Sx = StairCx + SSide * (StairHalfW + 0.035f);
                B.AddBox({ Sx, DeckY * 0.5f, (FrontDz + StairFootZ) * 0.5f }, { 0.038f, 0.13f, StringerLen * 0.5f }, StudioMaterialCode::FanHousing, SlopeAng, 0.0f, 0.0f);
                B.AddBox({ Sx, 0.022f, StairFootZ }, { 0.10f, 0.022f, 0.14f }, StudioMaterialCode::SafetyYellow);
                B.AddTube({ Sx, 1.02f, StairFootZ }, { Sx, DeckY + 1.02f, FrontDz }, 0.024f, 0.024f, 6u, StudioMaterialCode::SafetyYellow);
                B.AddTube({ Sx, 0.54f, StairFootZ }, { Sx, DeckY + 0.54f, FrontDz }, 0.016f, 0.016f, 6u, StudioMaterialCode::SafetyYellow);
                for (int P = 0; P <= 4; ++P)
                {
                    const float U  = static_cast<float>(P) / 4.0f;
                    const float Py = U * DeckY;
                    const float Pz = StairFootZ - U * StairRun;
                    B.AddTube({ Sx, Py + 0.02f, Pz }, { Sx, Py + 1.04f, Pz }, 0.018f, 0.018f, 6u, StudioMaterialCode::DarkMetal);
                }
            }

            const float InnerStairX = StairCx - Side * (StairHalfW + 0.035f);
            B.AddTube({ RailX, DeckY + 1.02f, FrontDz }, { InnerStairX, DeckY + 1.02f, FrontDz }, 0.024f, 0.024f, 6u, StudioMaterialCode::SafetyYellow);
            B.AddTube({ RailX, DeckY + 0.54f, FrontDz }, { InnerStairX, DeckY + 0.54f, FrontDz }, 0.018f, 0.018f, 6u, StudioMaterialCode::SafetyYellow);
            const float OuterStairX = StairCx + Side * (StairHalfW + 0.035f);
            B.AddTube({ OuterStairX, DeckY + 1.02f, FrontDz }, { CatX + Side * 1.18f, DeckY + 1.02f, FrontDz }, 0.024f, 0.024f, 6u, StudioMaterialCode::SafetyYellow);

            constexpr int Steps = 12;
            for (int S = 1; S <= Steps; ++S)
            {
                const float T  = static_cast<float>(S) / (Steps + 1);
                const float Sy = T * DeckY;
                const float Sz = StairFootZ - T * StairRun;
                B.AddBox({ StairCx, Sy,          Sz },          { StairHalfW,         0.028f, 0.165f }, StudioMaterialCode::DarkMetal);
                B.AddBox({ StairCx, Sy + 0.024f, Sz + 0.152f }, { StairHalfW - 0.02f, 0.010f, 0.016f }, StudioMaterialCode::SafetyYellow);
            }
        }
    }

    B.AddBox({ 0.0f, H, 1.5f }, { HalfW + 0.3f, 0.12f, D * 0.5f + 0.4f }, StudioMaterialCode::CeilingRib);
    for (float Z = BackZ + 0.4f; Z <= BackZ + D - 0.5f; Z += 3.1f)
    {
        B.AddBox({ 0.0f, H - 0.34f, Z }, { HalfW - 0.1f, 0.09f, 0.12f }, StudioMaterialCode::DarkMetal);
        B.AddBox({ 0.0f, H - 0.78f, Z }, { HalfW - 0.1f, 0.07f, 0.10f }, StudioMaterialCode::DarkMetal);
        for (float X = -HalfW + 1.6f; X <= HalfW - 1.6f; X += 2.2f)
        {
            B.AddTube({ X, H - 0.34f, Z }, { X + 1.1f, H - 0.78f, Z }, 0.022f, 0.022f, 5u, StudioMaterialCode::FanHousing);
        }
    }

    B.AddBox({ -0.6f, H - 0.92f,  0.8f }, { 0.16f, 0.16f, 13.5f }, StudioMaterialCode::SafetyYellow);
    B.AddBox({ -0.6f, H - 1.24f, -0.8f }, { 0.34f, 0.24f,  0.48f }, StudioMaterialCode::FanHousing);
    B.AddTube({ -0.6f, H - 1.48f, -0.8f }, { -0.6f, H - 2.35f, -0.8f }, 0.018f, 0.018f, 6u, StudioMaterialCode::ChromeMetal);
    for (int Loop = 0; Loop < 4; ++Loop)
    {
        const float Z0 = -3.8f + static_cast<float>(Loop) * 1.45f;
        const float Z1 = Z0 + 1.45f;
        const float Zm = (Z0 + Z1) * 0.5f;
        B.AddTube({ -0.38f, H - 1.05f, Z0 }, { -0.38f, H - 1.85f, Zm }, 0.016f, 0.016f, 6u, StudioMaterialCode::RubberMat);
        B.AddTube({ -0.38f, H - 1.85f, Zm }, { -0.38f, H - 1.05f, Z1 }, 0.016f, 0.016f, 6u, StudioMaterialCode::RubberMat);
    }

    if (Params.ShowLinearLeds)
    {
        const float LedY = H - 1.02f;
        for (const float Lz : { -3.6f, 0.4f, 4.4f })
        {
            for (const float Lx : { -5.8f, 0.0f, 5.8f })
            {
                const float BarHalfLen = (Lx == 0.0f) ? 2.15f : 1.55f;
                B.AddTube({ Lx - BarHalfLen * 0.75f, LedY + 0.06f, Lz }, { Lx - BarHalfLen * 0.75f, H - 0.1f, Lz }, 0.008f, 0.008f, 5u, StudioMaterialCode::ChromeMetal);
                B.AddTube({ Lx + BarHalfLen * 0.75f, LedY + 0.06f, Lz }, { Lx + BarHalfLen * 0.75f, H - 0.1f, Lz }, 0.008f, 0.008f, 5u, StudioMaterialCode::ChromeMetal);
                B.AddBox({ Lx, LedY + 0.04f,  Lz }, { BarHalfLen,         0.04f,  0.11f  }, StudioMaterialCode::DarkMetal);
                B.AddBox({ Lx, LedY - 0.012f, Lz }, { BarHalfLen * 0.96f, 0.022f, 0.085f }, StudioMaterialCode::EmissiveDiffuser);
            }
        }
    }

    return B.Finish();
}

StudioBuiltMesh BuildCarLiftSurface(
    const CarLiftParams&           Params,
    const StudioMaterialPalette&   Palette,
    const StudioMeshBakeTransform& Transform)
{
    HtmlStudioMeshBuilder B("StudioCarLift", Palette, Transform);
    const float LiftH   = std::clamp(Params.LiftHeight, 0.15f, 2.6f);
    const float PostH   = Params.PostHeight;
    constexpr float DeckLen = 4.75f;
    constexpr float DeckW   = 2.48f;
    constexpr float PostX   = DeckLen * 0.44f;
    constexpr float PostZ   = DeckW * 0.54f;

    B.AddBox({ 0.0f, 0.012f, 0.0f },             { DeckLen * 0.56f, 0.012f, DeckW * 0.68f }, StudioMaterialCode::DarkMetal);
    B.AddBox({ 0.0f, 0.016f,  (PostZ + 0.18f) }, { DeckLen * 0.56f, 0.010f, 0.08f },         StudioMaterialCode::SafetyYellow);
    B.AddBox({ 0.0f, 0.016f, -(PostZ + 0.18f) }, { DeckLen * 0.56f, 0.010f, 0.08f },         StudioMaterialCode::SafetyYellow);

    for (const float Sx : { -1.0f, 1.0f })
    {
        for (const float Sz : { -1.0f, 1.0f })
        {
            const float Px = Sx * PostX;
            const float Pz = Sz * PostZ;
            B.AddBox({ Px, 0.05f,        Pz }, { 0.22f, 0.05f,        0.22f }, StudioMaterialCode::SafetyYellow);
            B.AddBox({ Px, PostH * 0.5f, Pz }, { 0.11f, PostH * 0.5f, 0.12f }, StudioMaterialCode::FanHousing);
            B.AddTube({ Px - Sx * 0.11f, 0.12f, Pz }, { Px - Sx * 0.11f, LiftH + 0.12f, Pz }, 0.036f, 0.036f, 10u, StudioMaterialCode::ChromeMetal);
            B.AddBox({ Px, LiftH - 0.06f, Pz }, { 0.16f, 0.22f, 0.17f }, StudioMaterialCode::SafetyYellow);
        }
        B.AddBox({ Sx * PostX, PostH - 0.10f, 0.0f }, { 0.10f, 0.12f, PostZ + 0.12f }, StudioMaterialCode::FanHousing);
        B.AddBox({ Sx * PostX, LiftH - 0.12f, 0.0f }, { 0.12f, 0.11f, PostZ + 0.08f }, StudioMaterialCode::SafetyYellow);
    }

    B.AddBox({ PostX + 0.22f, 1.35f, PostZ }, { 0.14f, 0.28f, 0.14f }, StudioMaterialCode::CherryRedTool);
    B.AddTube({ PostX + 0.22f, 1.65f, PostZ }, { PostX + 0.22f, 2.02f, PostZ }, 0.08f, 0.08f, 10u, StudioMaterialCode::DarkMetal);

    constexpr float TrackZ = 0.78f;
    for (const float Sz : { -1.0f, 1.0f })
    {
        B.AddBox({ 0.0f, LiftH - 0.05f, Sz * TrackZ },          { DeckLen * 0.50f, 0.050f, 0.32f }, StudioMaterialCode::FanHousing);
        B.AddBox({ 0.0f, LiftH + 0.025f, Sz * (TrackZ + 0.31f)},{ DeckLen * 0.49f, 0.032f, 0.025f }, StudioMaterialCode::SafetyYellow);
        B.AddBox({ -DeckLen * 0.5f - 0.34f, LiftH - 0.18f, Sz * TrackZ }, { 0.38f, 0.035f, 0.29f }, StudioMaterialCode::SafetyYellow, 0.0f, 0.0f, 0.38f);
        B.AddBox({  DeckLen * 0.47f,        LiftH + 0.06f, Sz * TrackZ }, { 0.06f, 0.060f, 0.26f }, StudioMaterialCode::CherryRedTool);
    }
    B.AddBox({ 0.35f, LiftH - 0.08f, 0.0f }, { 0.42f, 0.04f, TrackZ - 0.28f }, StudioMaterialCode::CherryRedTool);
    return B.Finish();
}

StudioBuiltMesh BuildGaragePropsSurface(
    const GaragePropParams&        Params,
    const StudioMaterialPalette&   Palette,
    const StudioMeshBakeTransform& Transform)
{
    HtmlStudioMeshBuilder B("StudioGarageProp_" + Params.PropVariant, Palette, Transform);
    const std::string& V = Params.PropVariant;

    if (V == "tool-cart")
    {
        constexpr float W = 1.55f, D = 0.72f, H = 0.94f;
        B.AddBox({ 0.0f, 0.20f, 0.0f }, { W * 0.5f, 0.025f, D * 0.5f }, StudioMaterialCode::ChromeMetal);
        B.AddBox({ 0.0f, H,     0.0f }, { W * 0.5f, 0.028f, D * 0.5f }, StudioMaterialCode::ChromeMetal);
        for (const float Sx : { -1.0f, 1.0f })
        {
            for (const float Sz : { -1.0f, 1.0f })
            {
                const float Cx = Sx * (W * 0.47f), Cz = Sz * (D * 0.45f);
                B.AddTube({ Cx, 0.12f, Cz }, { Cx, H,     Cz }, 0.022f, 0.022f,  8u, StudioMaterialCode::ChromeMetal);
                B.AddTube({ Cx, 0.12f, Cz }, { Cx, 0.03f, Cz }, 0.048f, 0.048f, 10u, StudioMaterialCode::RubberMat);
            }
        }
        B.AddTube({ W * 0.54f, H + 0.08f, -D * 0.38f }, { W * 0.54f, H + 0.08f, D * 0.38f }, 0.018f, 0.018f, 8u, StudioMaterialCode::ChromeMetal);

        B.AddBox({ 0.22f, H + 0.28f, 0.0f }, { 0.48f, 0.26f, 0.31f }, StudioMaterialCode::CherryRedTool);
        for (int Dr = 0; Dr < 4; ++Dr)
        {
            B.AddBox({ 0.22f, H + 0.10f + static_cast<float>(Dr) * 0.115f, 0.322f }, { 0.42f, 0.014f, 0.014f }, StudioMaterialCode::ChromeMetal);
        }
        B.AddBox({ -0.48f, H + 0.16f, 0.02f },  { 0.24f, 0.14f,  0.26f },  StudioMaterialCode::CherryRedTool);
        B.AddBox({ -0.48f, H + 0.16f, 0.288f }, { 0.19f, 0.014f, 0.014f }, StudioMaterialCode::ChromeMetal);
        for (int Tr = 0; Tr < 2; ++Tr)
        {
            const float TOff = static_cast<float>(Tr) * 0.22f;
            B.AddTube({ -0.24f, 0.23f + TOff, 0.0f }, { -0.24f, 0.43f + TOff, 0.0f }, 0.31f, 0.31f, 16u, StudioMaterialCode::VehicleTireRubber);
            B.AddTube({ -0.24f, 0.26f + TOff, 0.0f }, { -0.24f, 0.41f + TOff, 0.0f }, 0.17f, 0.17f, 14u, StudioMaterialCode::VehicleRimAlloy);
        }
        B.AddBox({ W * 0.5f + 0.28f, 0.22f, 0.08f }, { 0.13f, 0.22f, 0.18f }, StudioMaterialCode::UmbrellaSilver, 0.0f, -0.25f, 0.0f);
        B.AddBox({ W * 0.5f + 0.28f, 0.46f, 0.02f }, { 0.04f, 0.04f, 0.04f }, StudioMaterialCode::CherryRedTool);
        return B.Finish();
    }

    if (V == "tire-rack")
    {
        constexpr float Rw = 2.4f, Rh = 1.95f;
        for (const float Sx : { -1.0f, 1.0f })
        {
            for (const float Sz : { -1.0f, 1.0f })
            {
                B.AddTube({ Sx * Rw * 0.5f, 0.0f, Sz * 0.34f }, { Sx * Rw * 0.5f, Rh, Sz * 0.34f }, 0.026f, 0.026f, 8u, StudioMaterialCode::FanHousing);
            }
        }
        for (const float Ry : { 0.55f, 1.38f })
        {
            B.AddBox({ 0.0f, Ry, -0.28f }, { Rw * 0.5f, 0.035f, 0.035f }, StudioMaterialCode::SafetyYellow);
            B.AddBox({ 0.0f, Ry,  0.28f }, { Rw * 0.5f, 0.035f, 0.035f }, StudioMaterialCode::SafetyYellow);
            for (int T = -2; T <= 2; ++T)
            {
                const float Tx = static_cast<float>(T) * 0.44f;
                B.AddTube({ Tx - 0.16f, Ry + 0.34f, 0.0f }, { Tx + 0.16f, Ry + 0.34f, 0.0f }, 0.34f, 0.34f, 16u, StudioMaterialCode::VehicleTireRubber);
                B.AddTube({ Tx - 0.17f, Ry + 0.34f, 0.0f }, { Tx + 0.17f, Ry + 0.34f, 0.0f }, 0.18f, 0.18f, 12u, StudioMaterialCode::VehicleRimAlloy);
            }
        }
        B.AddBox({ -0.45f, 0.08f, 0.92f }, { 0.36f, 0.06f, 0.18f }, StudioMaterialCode::CherryRedTool, 0.0f, 0.32f, 0.0f);
        B.AddTube({ -0.75f, 0.12f, 1.02f }, { -1.35f, 0.95f, 1.25f }, 0.018f, 0.016f, 8u, StudioMaterialCode::ChromeMetal);
        for (const float Jx : { 0.42f, 0.86f })
        {
            B.AddTube({ Jx, 0.02f, 0.88f }, { Jx, 0.36f, 0.88f }, 0.14f, 0.045f, 4u, StudioMaterialCode::SafetyYellow);
            B.AddBox({ Jx, 0.38f, 0.88f }, { 0.06f, 0.025f, 0.04f }, StudioMaterialCode::DarkMetal);
        }
        return B.Finish();
    }

    if (V == "tool-chest")
    {
        constexpr float W = 1.86f, D = 0.68f, BaseH = 0.96f;
        B.AddBox({ 0.0f, 0.14f + (BaseH - 0.14f) * 0.5f, 0.0f }, { W * 0.5f, (BaseH - 0.14f) * 0.5f, D * 0.5f }, StudioMaterialCode::CherryRedTool);
        B.AddBox({ 0.0f, BaseH + 0.015f, 0.0f }, { W * 0.49f, 0.015f, D * 0.48f }, StudioMaterialCode::RubberMat);
        for (const float Sx : { -1.0f, 1.0f })
        {
            for (const float Sz : { -1.0f, 1.0f })
            {
                const float Cx = Sx * (W * 0.43f), Cz = Sz * (D * 0.38f);
                B.AddTube({ Cx, 0.14f, Cz }, { Cx, 0.025f, Cz }, 0.062f, 0.062f, 10u, StudioMaterialCode::RubberMat);
                B.AddBox({ Cx, 0.13f, Cz }, { 0.04f, 0.025f, 0.04f }, StudioMaterialCode::ChromeMetal);
            }
        }
        B.AddTube({ W * 0.55f, BaseH - 0.06f, -D * 0.36f }, { W * 0.55f, BaseH - 0.06f, D * 0.36f }, 0.018f, 0.018f, 8u, StudioMaterialCode::ChromeMetal);
        for (const float Sz : { -1.0f, 1.0f })
        {
            B.AddTube({ W * 0.5f, BaseH - 0.06f, Sz * D * 0.36f }, { W * 0.55f, BaseH - 0.06f, Sz * D * 0.36f }, 0.014f, 0.014f, 6u, StudioMaterialCode::ChromeMetal);
        }
        for (const float ColX : { -0.45f, 0.45f })
        {
            for (int Dr = 0; Dr < 5; ++Dr)
            {
                B.AddBox({ ColX, 0.24f + static_cast<float>(Dr) * 0.145f, D * 0.5f + 0.014f }, { 0.39f, 0.014f, 0.014f }, StudioMaterialCode::ChromeMetal);
            }
        }
        B.AddBox({ -0.42f, BaseH + 0.27f, -0.02f }, { 0.46f, 0.25f, 0.29f }, StudioMaterialCode::CherryRedTool);
        for (int Dr = 0; Dr < 3; ++Dr)
        {
            B.AddBox({ -0.42f, BaseH + 0.11f + static_cast<float>(Dr) * 0.11f, 0.278f }, { 0.39f, 0.012f, 0.012f }, StudioMaterialCode::ChromeMetal);
        }
        B.AddBox({ -0.42f, BaseH + 0.56f, -0.08f }, { 0.465f, 0.025f, 0.28f }, StudioMaterialCode::CherryRedTool, -0.35f, 0.0f, 0.0f);
        B.AddBox({  0.36f, BaseH + 0.055f, 0.04f }, { 0.24f, 0.025f, 0.14f }, StudioMaterialCode::DarkMetal);
        for (int Sk = 0; Sk < 7; ++Sk)
        {
            const float Sx = 0.18f + static_cast<float>(Sk) * 0.058f;
            B.AddTube({ Sx, BaseH + 0.08f, 0.04f }, { Sx, BaseH + 0.135f, 0.04f }, 0.014f, 0.014f, 8u, StudioMaterialCode::ChromeMetal);
        }
        B.AddBox({ 0.66f, BaseH + 0.08f, -0.08f }, { 0.11f,  0.042f, 0.042f }, StudioMaterialCode::CherryRedTool, 0.0f, 0.42f, 0.0f);
        B.AddBox({ 0.75f, BaseH + 0.08f, -0.11f }, { 0.055f, 0.028f, 0.028f }, StudioMaterialCode::ChromeMetal,   0.0f, 0.42f, 0.0f);
        return B.Finish();
    }

    if (V == "workbench")
    {
        constexpr float W = 2.25f, D = 0.82f, H = 0.94f;
        B.AddBox({ 0.0f, H,     0.0f }, { W * 0.50f, 0.036f, D * 0.50f }, StudioMaterialCode::TimberBench);
        B.AddBox({ 0.0f, 0.24f, 0.0f }, { W * 0.48f, 0.022f, D * 0.45f }, StudioMaterialCode::FanHousing);
        for (const float Sx : { -1.0f, 1.0f })
        {
            for (const float Sz : { -1.0f, 1.0f })
            {
                B.AddBox({ Sx * (W * 0.47f), H * 0.5f, Sz * (D * 0.44f) }, { 0.032f, H * 0.5f, 0.032f }, StudioMaterialCode::DarkMetal);
            }
            B.AddBox({ Sx * (W * 0.47f), H + 0.55f, -D * 0.44f }, { 0.026f, 0.55f, 0.026f }, StudioMaterialCode::DarkMetal);
        }
        B.AddBox({ 0.0f, H + 0.54f, -D * 0.43f }, { W * 0.47f, 0.46f,  0.018f }, StudioMaterialCode::FanHousing);
        B.AddBox({ 0.0f, H + 1.06f, -D * 0.22f }, { W * 0.46f, 0.035f, 0.14f  }, StudioMaterialCode::DarkMetal);
        B.AddBox({ 0.0f, H + 1.02f, -D * 0.22f }, { W * 0.42f, 0.012f, 0.07f  }, StudioMaterialCode::EmissiveDiffuser);
        for (int Wr = 0; Wr < 8; ++Wr)
        {
            const float Wx  = -0.78f + static_cast<float>(Wr) * 0.13f;
            const float Len =  0.22f + static_cast<float>(Wr % 4) * 0.06f;
            B.AddBox({ Wx, H + 0.62f - Len * 0.35f, -D * 0.40f }, { 0.016f, Len * 0.5f, 0.012f }, StudioMaterialCode::ChromeMetal);
        }
        for (int Sd = 0; Sd < 5; ++Sd)
        {
            const float Sx = 0.35f + static_cast<float>(Sd) * 0.11f;
            B.AddBox({ Sx, H + 0.72f, -D * 0.40f }, { 0.018f, 0.06f, 0.018f }, StudioMaterialCode::CherryRedTool);
            B.AddTube({ Sx, H + 0.66f, -D * 0.40f }, { Sx, H + 0.46f, -D * 0.40f }, 0.007f, 0.007f, 6u, StudioMaterialCode::ChromeMetal);
        }
        B.AddBox({ -W * 0.36f, H + 0.12f, D * 0.32f }, { 0.11f, 0.09f, 0.14f }, StudioMaterialCode::CherryRedTool);
        B.AddTube({ -W * 0.36f - 0.16f, H + 0.11f, D * 0.48f }, { -W * 0.36f + 0.16f, H + 0.11f, D * 0.48f }, 0.012f, 0.012f, 8u, StudioMaterialCode::ChromeMetal);
        B.AddBox({ 0.52f, H + 0.16f, 0.05f }, { 0.28f, 0.13f, 0.16f }, StudioMaterialCode::CherryRedTool, 0.0f, -0.18f, 0.0f);
        B.AddTube({ 0.42f, H + 0.32f, 0.05f }, { 0.62f, H + 0.32f, 0.05f }, 0.012f, 0.012f, 6u, StudioMaterialCode::ChromeMetal);
        B.AddTube({ -W * 0.5f - 0.42f, 0.0f,  -0.05f }, { -W * 0.5f - 0.42f, 0.88f, -0.05f }, 0.29f,  0.29f,  16u, StudioMaterialCode::CherryRedTool);
        B.AddTube({ -W * 0.5f - 0.42f, 0.28f, -0.05f }, { -W * 0.5f - 0.42f, 0.31f, -0.05f }, 0.305f, 0.305f, 16u, StudioMaterialCode::SafetyYellow);
        B.AddTube({ -W * 0.5f - 0.42f, 0.58f, -0.05f }, { -W * 0.5f - 0.42f, 0.61f, -0.05f }, 0.305f, 0.305f, 16u, StudioMaterialCode::SafetyYellow);
        return B.Finish();
    }

    if (V == "air-compressor")
    {
        B.AddTube({ -0.35f, 0.14f, 0.0f }, { -0.35f, 1.48f, 0.0f }, 0.28f, 0.28f, 18u, StudioMaterialCode::CherryRedTool);
        B.AddSphere({ -0.35f, 1.48f, 0.0f }, 0.278f, 14u, 7u, StudioMaterialCode::CherryRedTool);
        B.AddBox({ -0.35f, 0.07f, 0.0f }, { 0.34f, 0.07f, 0.34f }, StudioMaterialCode::DarkMetal);
        B.AddBox({ -0.35f, 1.76f, 0.0f }, { 0.32f, 0.025f, 0.22f }, StudioMaterialCode::DarkMetal);
        B.AddTube({ -0.52f, 1.88f, 0.0f }, { -0.26f, 1.88f, 0.0f }, 0.11f, 0.11f, 12u, StudioMaterialCode::FanHousing);
        B.AddBox({ -0.14f, 1.94f, 0.0f }, { 0.11f, 0.15f, 0.11f }, StudioMaterialCode::DarkMetal);
        B.AddTube({ -0.06f, 1.32f, 0.12f }, { 0.06f, 1.32f, 0.12f }, 0.042f, 0.042f, 10u, StudioMaterialCode::ChromeMetal);
        for (int C = 0; C < 5; ++C)
        {
            const float Cy = 0.72f + static_cast<float>(C) * 0.07f;
            B.AddTube({ -0.04f, Cy, 0.18f }, { -0.04f, Cy + 0.03f, 0.18f }, 0.11f, 0.11f, 12u, StudioMaterialCode::SafetyYellow);
        }
        B.AddBox({ 0.52f, 0.04f, 0.0f }, { 0.32f, 0.025f, 0.24f }, StudioMaterialCode::DarkMetal);
        B.AddTube({ 0.22f, 0.16f, -0.18f }, { 0.22f, 1.38f, -0.18f }, 0.018f, 0.018f, 8u, StudioMaterialCode::DarkMetal);
        B.AddTube({ 0.82f, 0.16f, -0.18f }, { 0.82f, 1.38f, -0.18f }, 0.018f, 0.018f, 8u, StudioMaterialCode::DarkMetal);
        B.AddTube({ 0.22f, 1.38f, -0.18f }, { 0.82f, 1.38f, -0.18f }, 0.018f, 0.018f, 8u, StudioMaterialCode::DarkMetal);
        B.AddTube({ 0.38f, 0.06f, 0.02f }, { 0.38f, 1.44f, 0.02f }, 0.115f, 0.115f, 14u, StudioMaterialCode::StadiumSkyBlue);
        B.AddSphere({ 0.38f, 1.44f, 0.02f }, 0.114f, 12u, 6u, StudioMaterialCode::StadiumSkyBlue);
        B.AddTube({ 0.38f, 1.54f, 0.02f }, { 0.38f, 1.66f, 0.02f }, 0.032f, 0.026f, 8u, StudioMaterialCode::ChromeMetal);
        B.AddTube({ 0.66f, 0.06f, 0.02f }, { 0.66f, 1.36f, 0.02f }, 0.110f, 0.110f, 14u, StudioMaterialCode::SafetyYellow);
        B.AddSphere({ 0.66f, 1.36f, 0.02f }, 0.109f, 12u, 6u, StudioMaterialCode::SafetyYellow);
        B.AddTube({ 0.66f, 1.46f, 0.02f }, { 0.66f, 1.58f, 0.02f }, 0.030f, 0.025f, 8u, StudioMaterialCode::ChromeMetal);
        return B.Finish();
    }

    // diagnostic-cabinet
    B.AddBox({ 0.0f, 0.88f, 0.0f   }, { 0.58f, 0.82f, 0.42f }, StudioMaterialCode::CherryRedTool);
    B.AddBox({ 0.0f, 1.32f, 0.425f }, { 0.48f, 0.28f, 0.02f }, StudioMaterialCode::TetherScreen);
    for (int Dr = 0; Dr < 4; ++Dr)
    {
        B.AddBox({ 0.0f, 0.24f + static_cast<float>(Dr) * 0.19f, 0.43f }, { 0.48f, 0.016f, 0.016f }, StudioMaterialCode::ChromeMetal);
    }
    for (const float Sx : { -0.46f, 0.46f })
    {
        for (const float Sz : { -0.32f, 0.32f })
        {
            B.AddTube({ Sx, 0.09f, Sz }, { Sx, 0.02f, Sz }, 0.045f, 0.045f, 8u, StudioMaterialCode::RubberMat);
        }
    }
    return B.Finish();
}

StudioBuiltMesh BuildGarageCablesSurface(
    const GarageCablesParams&      Params,
    const StudioMaterialPalette&   Palette,
    const StudioMeshBakeTransform& Transform)
{
    HtmlStudioMeshBuilder B("StudioGarageCables", Palette, Transform);
    const float Scale = Params.CableSpan / 11.5f;

    const auto AddSnake = [&](std::initializer_list<Axis3D> Pts, float Rad, StudioMaterialCode Mat)
    {
        std::vector<Axis3D> Vec(Pts);
        for (std::size_t I = 0u; I + 1u < Vec.size(); ++I)
        {
            B.AddTube(Vec[I], Vec[I + 1u], Rad, Rad, 6u, Mat, false);
        }
    };

    AddSnake({
        { -5.6f * Scale, 0.022f, -1.20f }, { -4.2f * Scale, 0.022f, 0.60f },
        { -2.8f * Scale, 0.022f,  1.85f }, { -1.1f * Scale, 0.022f, 2.55f },
        {  0.6f * Scale, 0.022f,  3.25f }, {  2.1f * Scale, 0.022f, 4.15f },
        {  3.6f * Scale, 0.022f,  4.95f }, {  5.1f * Scale, 0.022f, 5.45f },
    }, 0.024f, StudioMaterialCode::RubberMat);

    AddSnake({
        { -5.3f * Scale, 0.020f, -0.80f }, { -3.8f * Scale, 0.020f, 0.90f },
        { -2.3f * Scale, 0.020f,  2.10f }, { -0.7f * Scale, 0.020f, 2.75f },
        {  0.85f * Scale,0.020f,  3.40f }, {  2.5f * Scale, 0.020f, 4.05f },
        {  4.2f * Scale, 0.020f,  4.65f },
    }, 0.019f, StudioMaterialCode::RubberMat);

    AddSnake({
        { 1.2f * Scale, 0.018f, -3.40f }, { 2.4f * Scale, 0.018f, -1.60f },
        { 3.3f * Scale, 0.018f,  0.45f }, { 4.6f * Scale, 0.018f,  2.15f },
        { 5.8f * Scale, 0.018f,  3.45f },
    }, 0.018f, StudioMaterialCode::SafetyYellow);

    B.AddBox({ 2.35f * Scale, 0.032f, 4.25f }, { 0.95f, 0.032f, 0.26f }, StudioMaterialCode::RubberMat,    0.0f, -0.45f, 0.0f);
    B.AddBox({ 2.35f * Scale, 0.066f, 4.25f }, { 0.88f, 0.010f, 0.14f }, StudioMaterialCode::SafetyYellow, 0.0f, -0.45f, 0.0f);
    return B.Finish();
}

StudioBuiltMesh BuildRaceTrackSurface(
    const RaceTrackParams&         Params,
    const StudioMaterialPalette&   Palette,
    const StudioMeshBakeTransform& Transform)
{
    HtmlStudioMeshBuilder B("StudioRaceTrack", Palette, Transform);
    const float    TrackLen   = std::clamp(Params.TrackLength, 80.0f, 220.0f);
    const uint32_t LaneCount  = std::clamp(Params.TrackLanes, 2u, 4u);
    const float    HalfLen    = TrackLen * 0.5f;
    constexpr float PitFrontZ = -6.22f;
    constexpr float PitWallZ  = -13.65f;
    constexpr float TrackNearZ = -14.35f;
    const float     TrackWidth = 5.4f * static_cast<float>(LaneCount);
    const float     TrackFarZ  = TrackNearZ - TrackWidth;
    const float     TrackMidZ  = (TrackNearZ + TrackFarZ) * 0.5f;

    // Afternoon stadium sky dome (inward normals)
    constexpr float SkyRad   = 215.0f;
    constexpr int   SkyRings = 10;
    constexpr int   SkySegs  = 28;
    for (int R = 0; R < SkyRings; ++R)
    {
        const float Phi0 = -0.06f + (static_cast<float>(R) / SkyRings) * (Frontier::Constants::PiFloat * 0.56f);
        const float Phi1 = -0.06f + (static_cast<float>(R + 1) / SkyRings) * (Frontier::Constants::PiFloat * 0.56f);
        const float Y0   = std::sin(Phi0) * SkyRad;
        const float Y1   = std::sin(Phi1) * SkyRad;
        const float Rad0 = std::cos(Phi0) * SkyRad;
        const float Rad1 = std::cos(Phi1) * SkyRad;
        for (int S = 0; S < SkySegs; ++S)
        {
            const float Th0 = (static_cast<float>(S) / SkySegs) * Frontier::Constants::TauFloat;
            const float Th1 = (static_cast<float>(S + 1) / SkySegs) * Frontier::Constants::TauFloat;
            const Axis3D A  { std::cos(Th0) * Rad0, Y0, std::sin(Th0) * Rad0 - 15.0f };
            const Axis3D Bb { std::cos(Th1) * Rad0, Y0, std::sin(Th1) * Rad0 - 15.0f };
            const Axis3D C  { std::cos(Th1) * Rad1, Y1, std::sin(Th1) * Rad1 - 15.0f };
            const Axis3D D  { std::cos(Th0) * Rad1, Y1, std::sin(Th0) * Rad1 - 15.0f };
            const auto InN = [](Axis3D P) noexcept -> Axis3D
            {
                return Frontier::Mathematics::NormalizeAxis({ -P.X, -P.Y, -(P.Z + 15.0f) }, { 0.0f, -1.0f, 0.0f });
            };
            B.PushTri(A, C, Bb, InN(A), InN(C), InN(Bb), StudioMaterialCode::StadiumSkyDome);
            B.PushTri(A, D, C,  InN(A), InN(D), InN(C),  StudioMaterialCode::StadiumSkyDome);
        }
    }

    const float PitMidZ  = (PitFrontZ + PitWallZ) * 0.5f;
    const float PitDepth = std::fabs(PitWallZ - PitFrontZ);
    B.AddBox({ 0.0f, -0.03f, PitMidZ }, { HalfLen, 0.03f, PitDepth * 0.5f }, StudioMaterialCode::ConcreteGarage);
    B.AddBox({ 0.0f, 0.008f, -10.15f }, { HalfLen, 0.008f, 0.14f }, StudioMaterialCode::TrackPaintWhite);
    B.AddBox({ 0.0f, 0.008f, -12.95f }, { HalfLen, 0.008f, 0.12f }, StudioMaterialCode::TrackPaintWhite);
    B.AddBox({ 0.0f, 0.009f, -10.42f }, { HalfLen, 0.008f, 0.08f }, StudioMaterialCode::CherryRedTool);
    for (int Px = -44; Px <= 44; Px += 11)
    {
        B.AddBox({ static_cast<float>(Px), 0.009f, -8.35f },        { 0.11f, 0.008f, 1.75f }, StudioMaterialCode::TrackPaintWhite);
        B.AddBox({ static_cast<float>(Px) + 5.5f, 0.009f, -9.80f }, { 3.60f, 0.008f, 0.09f }, StudioMaterialCode::SafetyYellow);
    }

    B.AddBox({ 0.0f, 0.56f, PitWallZ }, { HalfLen, 0.56f, 0.28f }, StudioMaterialCode::ConcreteGarage);
    constexpr float SegW = 4.0f;
    for (float X = -HalfLen + SegW * 0.5f; X < HalfLen; X += SegW)
    {
        const bool IsRed = (static_cast<int>(std::floor((X + HalfLen) / SegW)) % 2) == 0;
        B.AddBox({ X, 1.15f, PitWallZ }, { SegW * 0.5f, 0.06f, 0.30f }, IsRed ? StudioMaterialCode::CherryRedTool : StudioMaterialCode::TrackPaintWhite);
    }
    for (float Fx = -64.0f; Fx <= 64.0f; Fx += 4.0f)
    {
        B.AddTube({ Fx, 1.18f, PitWallZ }, { Fx, 3.25f, PitWallZ },          0.032f, 0.028f, 6u, StudioMaterialCode::FanHousing);
        B.AddTube({ Fx, 3.25f, PitWallZ }, { Fx, 3.55f, PitWallZ - 0.32f }, 0.026f, 0.024f, 6u, StudioMaterialCode::FanHousing);
    }
    for (const float Cy : { 1.65f, 2.25f, 2.85f, 3.35f })
    {
        B.AddTube({ -64.0f, Cy, PitWallZ }, { 64.0f, Cy, PitWallZ }, 0.014f, 0.014f, 6u, StudioMaterialCode::ChromeMetal, false);
    }

    B.AddBox({ 0.0f, -0.025f, TrackMidZ }, { HalfLen, 0.025f, TrackWidth * 0.5f + 0.65f }, StudioMaterialCode::TrackAsphalt);
    B.AddBox({ 0.0f,  0.012f, TrackNearZ - 0.42f }, { HalfLen, 0.01f, 0.16f }, StudioMaterialCode::TrackPaintWhite);
    B.AddBox({ 0.0f,  0.012f, TrackFarZ  + 0.42f }, { HalfLen, 0.01f, 0.16f }, StudioMaterialCode::TrackPaintWhite);

    const float LaneW = (TrackWidth - 0.84f) / static_cast<float>(LaneCount);
    for (uint32_t L = 1u; L < LaneCount; ++L)
    {
        const float LaneZ = TrackNearZ - 0.42f - static_cast<float>(L) * LaneW;
        for (float Dx = -HalfLen + 4.0f; Dx < HalfLen - 4.0f; Dx += 7.5f)
        {
            B.AddBox({ Dx, 0.012f, LaneZ }, { 1.95f, 0.01f, 0.11f }, StudioMaterialCode::TrackPaintWhite);
        }
    }

    constexpr float SfX = -14.0f;
    B.AddBox({ SfX, 0.014f, TrackMidZ }, { 0.42f, 0.01f, TrackWidth * 0.48f }, StudioMaterialCode::TrackPaintWhite);
    for (int Ck = 0; Ck < 12; ++Ck)
    {
        const float Cz = TrackFarZ + 0.85f + static_cast<float>(Ck) * ((TrackWidth - 1.7f) / 12.0f);
        B.AddBox({ SfX + ((Ck % 2 == 0) ? 0.58f : -0.58f), 0.015f, Cz }, { 0.24f, 0.01f, 0.48f }, StudioMaterialCode::TrackPaintWhite);
    }
    for (int RowIdx = 0; RowIdx < 6; ++RowIdx)
    {
        const float BaseX = SfX + 6.5f + static_cast<float>(RowIdx) * 8.2f;
        for (int Col = 0; Col < 2; ++Col)
        {
            const float Gx = BaseX + static_cast<float>(Col) * 3.8f;
            const float Gz = (Col == 0) ? (TrackNearZ - 3.6f) : (TrackFarZ + 3.6f);
            B.AddBox({ Gx - 1.40f, 0.013f, Gz },         { 0.14f, 0.01f, 1.35f }, StudioMaterialCode::TrackPaintWhite);
            B.AddBox({ Gx - 0.25f, 0.013f, Gz - 1.25f }, { 1.25f, 0.01f, 0.12f }, StudioMaterialCode::TrackPaintWhite);
            B.AddBox({ Gx - 0.25f, 0.013f, Gz + 1.25f }, { 1.25f, 0.01f, 0.12f }, StudioMaterialCode::TrackPaintWhite);
        }
    }

    constexpr float KerbLen = 1.65f;
    for (float Kx = -HalfLen + KerbLen * 0.5f; Kx < HalfLen; Kx += KerbLen)
    {
        const bool IsRed = (static_cast<int>(std::floor((Kx + HalfLen) / KerbLen)) % 2) == 0;
        const auto KMat  = IsRed ? StudioMaterialCode::CherryRedTool : StudioMaterialCode::TrackPaintWhite;
        B.AddBox({ Kx, 0.045f, TrackNearZ + 0.18f }, { KerbLen * 0.495f, 0.055f, 0.42f }, KMat,  0.11f, 0.0f, 0.0f);
        B.AddBox({ Kx, 0.045f, TrackFarZ  - 0.18f }, { KerbLen * 0.495f, 0.055f, 0.42f }, KMat, -0.11f, 0.0f, 0.0f);
    }

    if (Params.ShowGantry)
    {
        constexpr float Gh = 7.4f;
        B.AddBox({ SfX, Gh * 0.5f, PitWallZ  - 0.35f }, { 0.55f, Gh * 0.5f, 0.55f }, StudioMaterialCode::FanHousing);
        B.AddBox({ SfX, Gh * 0.5f, TrackFarZ - 1.55f }, { 0.55f, Gh * 0.5f, 0.55f }, StudioMaterialCode::FanHousing);
        const float GSpan = std::fabs((TrackFarZ - 1.55f) - (PitWallZ - 0.35f));
        const float GMidZ = ((PitWallZ - 0.35f) + (TrackFarZ - 1.55f)) * 0.5f;
        B.AddBox({ SfX,         Gh - 0.55f, GMidZ }, { 0.52f, 0.85f, GSpan * 0.5f + 0.4f }, StudioMaterialCode::DarkMetal);
        B.AddBox({ SfX + 0.55f, Gh - 0.55f, GMidZ }, { 0.06f, 0.56f, GSpan * 0.46f },       StudioMaterialCode::CherryRedTool);
        B.AddBox({ SfX + 0.58f, Gh - 0.12f, GMidZ }, { 0.06f, 0.12f, GSpan * 0.46f },       StudioMaterialCode::TrackPaintWhite);
        for (int Pod = -2; Pod <= 2; ++Pod)
        {
            const float Pz = TrackMidZ + static_cast<float>(Pod) * 2.25f;
            B.AddBox({ SfX + 0.15f, Gh - 1.75f, Pz }, { 0.22f, 0.34f, 0.24f }, StudioMaterialCode::DarkMetal);
            B.AddSphere({ SfX + 0.38f, Gh - 1.75f, Pz }, 0.11f, 8u, 6u, StudioMaterialCode::CherryRedTool);
        }
    }

    const float GrassFarZ = TrackFarZ - 8.5f;
    B.AddBox({ 0.0f, -0.05f, (TrackFarZ + GrassFarZ) * 0.5f }, { HalfLen + 25.0f, 0.04f, 4.4f }, StudioMaterialCode::TrackGrass);
    B.AddBox({ 0.0f, -0.14f, -105.0f }, { 220.0f, 0.08f, 105.0f }, StudioMaterialCode::TrackGrass);
    const float BarrierZ = TrackFarZ - 5.6f;
    B.AddBox({ 0.0f, 0.72f, BarrierZ }, { HalfLen + 10.0f, 0.72f, 0.34f }, StudioMaterialCode::ConcreteGarage);
    for (float Bx = -HalfLen; Bx < HalfLen; Bx += 6.0f)
    {
        const bool RedBand = (static_cast<int>(std::floor((Bx + HalfLen) / 6.0f)) % 2) == 0;
        B.AddBox({ Bx + 3.0f, 1.32f, BarrierZ + 0.12f }, { 2.95f, 0.16f, 0.26f }, RedBand ? StudioMaterialCode::CherryRedTool : StudioMaterialCode::SafetyYellow);
    }
    for (float Fx = -68.0f; Fx <= 68.0f; Fx += 4.5f)
    {
        B.AddTube({ Fx, 1.4f, BarrierZ }, { Fx, 3.6f, BarrierZ }, 0.036f, 0.03f, 6u, StudioMaterialCode::FanHousing);
    }

    if (Params.ShowGrandstands)
    {
        constexpr float StandW    = 112.0f;
        const float     StandFrontZ = BarrierZ - 2.4f;
        constexpr int   TierCount = 14;
        constexpr float TierRise  = 0.76f;
        constexpr float TierRun   = 1.05f;
        constexpr float BaseLift  = 1.65f;

        B.AddBox({ 0.0f, BaseLift * 0.5f, StandFrontZ }, { StandW * 0.5f, BaseLift * 0.5f, 0.55f }, StudioMaterialCode::ConcreteGarage);
        for (int T = 0; T < TierCount; ++T)
        {
            const float Ty = BaseLift + static_cast<float>(T) * TierRise;
            const float Tz = StandFrontZ - static_cast<float>(T) * TierRun - 0.5f;
            B.AddBox({ 0.0f, Ty + TierRise * 0.3f, Tz }, { StandW * 0.5f, TierRise * 0.52f, TierRun * 0.54f }, StudioMaterialCode::ConcreteGarage);
            constexpr int   SeatBlocks = 7;
            constexpr float BlockW     = StandW / SeatBlocks;
            for (int Bl = 0; Bl < SeatBlocks; ++Bl)
            {
                const float Bcx       = -StandW * 0.5f + (static_cast<float>(Bl) + 0.5f) * BlockW;
                const bool  UseYellow = ((T >= 4 && T <= 8 && (Bl % 2) == 1) || (T >= 10 && (Bl % 2) == 0));
                B.AddBox({ Bcx, Ty + TierRise * 0.68f, Tz + 0.14f }, { BlockW * 0.44f, 0.24f, 0.22f }, UseYellow ? StudioMaterialCode::SafetyYellow : StudioMaterialCode::CherryRedTool);
            }
        }

        const float StandTopY  = BaseLift + static_cast<float>(TierCount) * TierRise;
        const float StandBackZ = StandFrontZ - static_cast<float>(TierCount) * TierRun;
        for (int Ax = -40; Ax <= 40; Ax += 16)
        {
            for (int St = 0; St < TierCount; ++St)
            {
                const float Sy = BaseLift + static_cast<float>(St) * TierRise + 0.26f;
                const float Sz = StandFrontZ - static_cast<float>(St) * TierRun - 0.15f;
                B.AddBox({ static_cast<float>(Ax), Sy, Sz }, { 0.85f, 0.28f, 0.58f }, StudioMaterialCode::ConcreteGarage);
            }
            B.AddTube({ static_cast<float>(Ax), BaseLift + 1.05f, StandFrontZ + 0.1f }, { static_cast<float>(Ax), StandTopY + 0.95f, StandBackZ }, 0.036f, 0.036f, 6u, StudioMaterialCode::SafetyYellow);
        }

        B.AddBox({ 0.0f, (StandTopY + 4.8f) * 0.5f, StandBackZ - 0.7f }, { StandW * 0.5f, (StandTopY + 4.8f) * 0.5f, 0.55f }, StudioMaterialCode::ConcreteGarage);
        const float RoofRearY  = StandTopY + 4.8f;
        const float RoofFrontY = StandTopY + 7.2f;
        const float RoofFrontZ = StandFrontZ - 2.2f;
        const float RoofMidZ   = (StandBackZ + RoofFrontZ) * 0.5f;
        const float RoofSpanZ  = std::fabs(StandBackZ - RoofFrontZ);
        B.AddBox({ 0.0f, (RoofRearY + RoofFrontY) * 0.5f, RoofMidZ }, { StandW * 0.51f, 0.34f, RoofSpanZ * 0.54f }, StudioMaterialCode::StadiumSkyBlue, -0.14f, 0.0f, 0.0f);
        B.AddBox({ 0.0f, RoofFrontY - 0.15f, RoofFrontZ }, { StandW * 0.51f, 0.48f, 0.22f }, StudioMaterialCode::TrackPaintWhite);
        for (float Rx = -50.0f; Rx <= 50.0f; Rx += 12.5f)
        {
            B.AddBox({ Rx, RoofRearY * 0.5f, StandBackZ - 0.3f }, { 0.34f, RoofRearY * 0.5f, 0.34f }, StudioMaterialCode::FanHousing);
            B.AddTube({ Rx, RoofRearY - 0.2f, StandBackZ }, { Rx, RoofFrontY - 0.25f, RoofFrontZ }, 0.18f, 0.14f, 6u, StudioMaterialCode::FanHousing);
        }
        for (const float Tx : { -42.0f, -14.0f, 14.0f, 42.0f })
        {
            const float TowerZ = StandBackZ - 2.2f;
            B.AddTube({ Tx, 0.0f, TowerZ }, { Tx, RoofFrontY + 8.5f, TowerZ + 1.8f }, 0.45f, 0.24f, 8u, StudioMaterialCode::FanHousing);
            B.AddBox({ Tx, RoofFrontY + 8.6f, TowerZ + 2.10f }, { 2.4f, 1.35f, 0.35f }, StudioMaterialCode::DarkMetal,        0.28f, 0.0f, 0.0f);
            B.AddBox({ Tx, RoofFrontY + 8.5f, TowerZ + 2.48f }, { 2.1f, 1.10f, 0.12f }, StudioMaterialCode::EmissiveDiffuser, 0.28f, 0.0f, 0.0f);
        }
    }

    return B.Finish();
}

StudioBuiltMesh BuildAssembledControlVehicleSurface(
    const Frontier::Codecs::AssembledControlVehicle& Vehicle,
    const StudioMaterialPalette&                     Palette,
    uint16_t                                         BodyPaintMaterialSlot,
    uint16_t                                         BrakeCaliperMaterialSlot,
    const StudioMeshBakeTransform&                   Transform,
    std::string_view                                 MeshTitle)
{
    using namespace Frontier::Codecs;

    StudioBuiltMesh Out {};
    Out.Network.NetworkTitle = std::string(MeshTitle);
    if (Vehicle.Triangles.empty())
    {
        return Out;
    }

    const uint32_t TriCount = static_cast<uint32_t>(Vehicle.Triangles.size());
    Out.Network.ReserveStorage(TriCount * 3u, TriCount * 3u, TriCount);
    Out.AuthoredCornerNormals.reserve(static_cast<std::size_t>(TriCount) * 9u);

    // Vehicle.Triangles are in FrontierWorldZUp (+X forward, +Y left, +Z up, resting at Z = 0).
    // Convert via Engine CoordinateSpace (ZUp <-> YUp) around the Y-up bake rotation!
    const float PitchRad = Transform.PitchDegrees * Frontier::Constants::DegToRadFloat;
    const float YawRad   = Transform.YawDegrees * Frontier::Constants::DegToRadFloat;
    const float RollRad  = Transform.RollDegrees * Frontier::Constants::DegToRadFloat;

    const auto XformPos = [&](Axis3D ZupPt) noexcept -> Axis3D
    {
        const Axis3D Html = Frontier::Mathematics::ConvertVectorBetweenBases(
            ZupPt * Transform.UniformScale,
            Frontier::Mathematics::WorldCoordinateBasis::RightHandedZUp,
            Frontier::Mathematics::WorldCoordinateBasis::RightHandedYUp);
        const Axis3D Rot = RotateHtmlYUpVector(Html, PitchRad, YawRad, RollRad);
        return Frontier::Mathematics::ConvertVectorBetweenBases(
            Rot,
            Frontier::Mathematics::WorldCoordinateBasis::RightHandedYUp,
            Frontier::Mathematics::WorldCoordinateBasis::RightHandedZUp);
    };
    const auto XformNorm = [&](Axis3D ZupN) noexcept -> Axis3D
    {
        const Axis3D Html = Frontier::Mathematics::ConvertVectorBetweenBases(
            ZupN,
            Frontier::Mathematics::WorldCoordinateBasis::RightHandedZUp,
            Frontier::Mathematics::WorldCoordinateBasis::RightHandedYUp);
        const Axis3D Rot = RotateHtmlYUpVector(Html, PitchRad, YawRad, RollRad);
        return Frontier::Mathematics::NormalizeAxis(
            Frontier::Mathematics::ConvertVectorBetweenBases(
                Rot,
                Frontier::Mathematics::WorldCoordinateBasis::RightHandedYUp,
                Frontier::Mathematics::WorldCoordinateBasis::RightHandedZUp),
            { 0.0f, 0.0f, 1.0f });
    };

    const auto ResolvePartSlot = [&](VehicleSurfaceSlot Slot) noexcept -> uint16_t
    {
        switch (Slot)
        {
            case VehicleSurfaceSlot::BodyCoat:
                return BodyPaintMaterialSlot;
            case VehicleSurfaceSlot::WheelCapAccent:
                return (BrakeCaliperMaterialSlot != 0u) ? BrakeCaliperMaterialSlot : Palette.Resolve(StudioMaterialCode::VehicleBrakeCaliper);
            case VehicleSurfaceSlot::RimAlloy:
                return Palette.Resolve(StudioMaterialCode::VehicleRimAlloy);
            case VehicleSurfaceSlot::CanopyGloss:
                return Palette.Resolve(StudioMaterialCode::VehicleGlossCanopy);
            case VehicleSurfaceSlot::VehicleGlass:
                return Palette.Resolve(StudioMaterialCode::VehicleGlass);
            case VehicleSurfaceSlot::VehiclePlastic:
                return Palette.Resolve(StudioMaterialCode::VehicleIntakePolymer);
            case VehicleSurfaceSlot::TyreRubber:
                return Palette.Resolve(StudioMaterialCode::VehicleTireRubber);
            case VehicleSurfaceSlot::BrakeDisc:
            case VehicleSurfaceSlot::VehicleTrim:
                return Palette.Resolve(StudioMaterialCode::VehicleTrimMetal);
            case VehicleSurfaceSlot::HeadlightLens:
                return Palette.Resolve(StudioMaterialCode::VehicleHeadlightOptic);
            case VehicleSurfaceSlot::TaillightLens:
                return Palette.Resolve(StudioMaterialCode::VehicleTaillightOptic);
            default:
                return BodyPaintMaterialSlot;
        }
    };

    float MinZ = 1.0e9f;
    for (const auto& Tri : Vehicle.Triangles)
    {
        const std::array<Axis3D, 3u> Corners {
            XformPos(Tri.CornerA),
            XformPos(Tri.CornerB),
            XformPos(Tri.CornerC),
        };
        const std::array<Axis3D, 3u> Normals {
            XformNorm(Tri.NormalA),
            XformNorm(Tri.NormalB),
            XformNorm(Tri.NormalC),
        };
        MinZ = std::min({ MinZ, Corners[0u].Z, Corners[1u].Z, Corners[2u].Z });

        std::array<uint32_t, 3u> TriV {};
        for (uint32_t C = 0u; C < 3u; ++C)
        {
            TriV[C] = Out.Network.AppendVertex(Corners[C]);
            Out.Network.Vertices[TriV[C]].Normal = Normals[C];
        }
        const std::array<UVAxes, 3u> TriUv {{
            { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f }
        }};
        Frontier::GeometrySpace::FaceShadingAssignment FaceShading {};
        FaceShading.AlbedoLinearRGB = { 0.5f, 0.5f, 0.5f };
        const uint16_t MatSlot = ResolvePartSlot(Tri.SurfaceSlot);
        const uint32_t FaceId = Out.Network.AppendFace(
            TriV,
            TriUv,
            FaceShading,
            0u,
            MatSlot);
        if (FaceId != Frontier::GeometrySpace::InvalidNetworkIndex)
        {
            for (uint32_t C = 0u; C < 3u; ++C)
            {
                Out.AuthoredCornerNormals.push_back(Normals[C].X);
                Out.AuthoredCornerNormals.push_back(Normals[C].Y);
                Out.AuthoredCornerNormals.push_back(Normals[C].Z);
            }
        }
    }

    if (std::isfinite(MinZ) && std::fabs(MinZ) > 1.0e-5f)
    {
        for (auto& V : Out.Network.Vertices)
        {
            V.Axes.Z -= MinZ;
        }
    }

    return Out;
}

} // namespace AutoSpace
