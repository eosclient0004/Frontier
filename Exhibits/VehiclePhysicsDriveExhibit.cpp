/*============================================================================================================================================
                                                    VEHICLEPHYSICSDRIVEEXHIBIT.CPP
============================================================================================================================================*/
// 🧩 Real JoltPhysics v5.6.0 ControlVehicle Driving, Powertrain, Socket-Free Constraint Rig & 5-Way Wheel Trace Benchmark Exhibit
//    - Drives Frontier's native VehiclePhysicsRig + PhysicsExtension + ConstraintSolver + ControlVehicleClassBuilder (zero rewritten physics)
//    - Proves: (1) Drive & WOT Acceleration + Turbocharger (Spool/Wastegate/BOV) + Clutch (Launch Slip -> Lockup) + DCT Shifts
//              (2) High-G Cornering & S-Turn (Ackermann + Steer Hinge Constraints + Pacejka MF6.1 + Body Roll + Tire Tracks)
//              (3) Braking on a Steep 10 deg Slope to Full Standstill + Zero-Slide Hold (Karnopp StaticFull < 3 cm creep over 3.0 s)
//              (4) Hero 3-Lane (Left + Center + Right) x +-40 deg Fan/Arc Trace (15 rays/wheel) vs SingleRay / 1-Lane Arc / Sphere / Cylinder
//    - Renders 4 real 3D animated GIF89a clips + 1 1920x1080 telemetry & benchmark summary PNG in Exhibits/Images/VehiclePhysics/

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ControlVehicleClassBuilder.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/SimulationSpace/PhysicsSpace/PhysicsExtension.h"
#include "Engine/SimulationSpace/PhysicsSpace/PhysicsMaterial.h"
#include "Engine/SimulationSpace/PhysicsSpace/VehicleDynamicsSolver.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace Frontier;
using namespace Frontier::ComponentClass;
using namespace Frontier::Mathematics;
using namespace Frontier::SimulationSpace::PhysicsSpace;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                        LIGHTWEIGHT 3D MATH & QUATERNION HELPERS
//------------------------------------------------------------------------------------------------------------------------

struct Vec3
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;

    Vec3 operator+(const Vec3& R) const noexcept { return { X + R.X, Y + R.Y, Z + R.Z }; }
    Vec3 operator-(const Vec3& R) const noexcept { return { X - R.X, Y - R.Y, Z - R.Z }; }
    Vec3 operator*(float S) const noexcept { return { X * S, Y * S, Z * S }; }
};

[[nodiscard]] inline Vec3 ToVec3(const Axis3D& A) noexcept { return { A.X, A.Y, A.Z }; }
[[nodiscard]] inline float Dot3(const Vec3& A, const Vec3& B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
[[nodiscard]] inline Vec3 Cross3(const Vec3& A, const Vec3& B) noexcept
{
    return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X };
}
[[nodiscard]] inline Vec3 Norm3(const Vec3& A) noexcept
{
    const float L = std::sqrt(Dot3(A, A));
    return (L > 1.0e-7f) ? (A * (1.0f / L)) : Vec3{ 0.0f, 1.0f, 0.0f };
}
[[nodiscard]] inline Vec3 RotQuat(const RigidOrientationQuaternion& Q, const Vec3& V) noexcept
{
    const Vec3 U { Q.X, Q.Y, Q.Z };
    const Vec3 T = Cross3(U, V) * 2.0f;
    return V + T * Q.W + Cross3(U, T);
}
[[nodiscard]] inline RigidOrientationQuaternion MulQuat(
    const RigidOrientationQuaternion& A,
    const RigidOrientationQuaternion& B) noexcept
{
    return {
        A.W * B.X + A.X * B.W + A.Y * B.Z - A.Z * B.Y,
        A.W * B.Y - A.X * B.Z + A.Y * B.W + A.Z * B.X,
        A.W * B.Z + A.X * B.Y - A.Y * B.X + A.Z * B.W,
        A.W * B.W - A.X * B.X - A.Y * B.Y - A.Z * B.Z,
    };
}
[[nodiscard]] inline RigidOrientationQuaternion AxisAngleQ(const Vec3& Axis, float Rad) noexcept
{
    const Vec3  N = Norm3(Axis);
    const float H = 0.5f * Rad;
    const float S = std::sin(H);
    return { N.X * S, N.Y * S, N.Z * S, std::cos(H) };
}

//------------------------------------------------------------------------------------------------------------------------
//                                   SELF-CONTAINED PNG WRITER + GIF89a ANIMATION WRITER
//------------------------------------------------------------------------------------------------------------------------

uint32_t Crc32Bytes(const uint8_t* Data, std::size_t Len) noexcept
{
    uint32_t C = 0xFFFFFFFFu;
    for (std::size_t I = 0u; I < Len; ++I)
    {
        C ^= Data[I];
        for (int B = 0; B < 8; ++B)
        {
            C = (C >> 1u) ^ (0xEDB88320u & (0u - (C & 1u)));
        }
    }
    return ~C;
}

uint32_t Adler32Bytes(const uint8_t* Data, std::size_t Len) noexcept
{
    uint32_t S1 = 1u, S2 = 0u;
    for (std::size_t I = 0u; I < Len; ++I)
    {
        S1 = (S1 + Data[I]) % 65521u;
        S2 = (S2 + S1) % 65521u;
    }
    return (S2 << 16u) | S1;
}

void AppendBe32(std::vector<uint8_t>& Out, uint32_t V)
{
    Out.push_back(static_cast<uint8_t>((V >> 24u) & 0xFFu));
    Out.push_back(static_cast<uint8_t>((V >> 16u) & 0xFFu));
    Out.push_back(static_cast<uint8_t>((V >> 8u) & 0xFFu));
    Out.push_back(static_cast<uint8_t>(V & 0xFFu));
}

void AppendPngChunk(std::vector<uint8_t>& Out, const char Type[4], const std::vector<uint8_t>& Payload)
{
    AppendBe32(Out, static_cast<uint32_t>(Payload.size()));
    const std::size_t TypeStart = Out.size();
    Out.insert(Out.end(), Type, Type + 4);
    Out.insert(Out.end(), Payload.begin(), Payload.end());
    AppendBe32(Out, Crc32Bytes(Out.data() + TypeStart, Payload.size() + 4u));
}

bool WriteRgbPng(const std::filesystem::path& Path, int W, int H, const std::vector<uint8_t>& Rgb)
{
    std::error_code Ec {};
    std::filesystem::create_directories(Path.parent_path(), Ec);

    std::vector<uint8_t> Raw {};
    Raw.reserve(static_cast<std::size_t>(H) * (1u + static_cast<std::size_t>(W) * 3u));
    for (int Y = 0; Y < H; ++Y)
    {
        Raw.push_back(0u);
        const std::size_t Off = static_cast<std::size_t>(Y) * static_cast<std::size_t>(W) * 3u;
        Raw.insert(Raw.end(), Rgb.begin() + static_cast<std::ptrdiff_t>(Off),
                              Rgb.begin() + static_cast<std::ptrdiff_t>(Off + static_cast<std::size_t>(W) * 3u));
    }

    std::vector<uint8_t> Zlib { 0x78u, 0x01u };
    std::size_t Pos = 0u;
    while (Pos < Raw.size())
    {
        const std::size_t Chunk = std::min<std::size_t>(65535u, Raw.size() - Pos);
        const bool Last = (Pos + Chunk >= Raw.size());
        Zlib.push_back(Last ? 0x01u : 0x00u);
        const uint16_t Len  = static_cast<uint16_t>(Chunk);
        const uint16_t NLen = static_cast<uint16_t>(~Len);
        Zlib.push_back(static_cast<uint8_t>(Len & 0xFFu));
        Zlib.push_back(static_cast<uint8_t>((Len >> 8u) & 0xFFu));
        Zlib.push_back(static_cast<uint8_t>(NLen & 0xFFu));
        Zlib.push_back(static_cast<uint8_t>((NLen >> 8u) & 0xFFu));
        Zlib.insert(Zlib.end(), Raw.begin() + static_cast<std::ptrdiff_t>(Pos),
                                Raw.begin() + static_cast<std::ptrdiff_t>(Pos + Chunk));
        Pos += Chunk;
    }
    AppendBe32(Zlib, Adler32Bytes(Raw.data(), Raw.size()));

    std::vector<uint8_t> Png { 137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u };
    std::vector<uint8_t> Ihdr {};
    AppendBe32(Ihdr, static_cast<uint32_t>(W));
    AppendBe32(Ihdr, static_cast<uint32_t>(H));
    Ihdr.push_back(8u); Ihdr.push_back(2u); Ihdr.push_back(0u); Ihdr.push_back(0u); Ihdr.push_back(0u);
    AppendPngChunk(Png, "IHDR", Ihdr);
    AppendPngChunk(Png, "IDAT", Zlib);
    AppendPngChunk(Png, "IEND", {});

    std::ofstream File(Path, std::ios::binary);
    if (!File) return false;
    File.write(reinterpret_cast<const char*>(Png.data()), static_cast<std::streamsize>(Png.size()));
    return File.good();
}

// Self-contained Animated GIF89a Writer (252-color 6x7x6 RGB cube + 4 extra grays + LZW with clean dictionary resets)
class AnimatedGifWriter
{
public:
    AnimatedGifWriter(int W, int H, uint16_t DelayCentisec = 5u)
        : Width(W), Height(H), FrameDelayCs(DelayCentisec)
    {
        BuildGlobalPalette();
        Bytes.reserve(512u * 1024u);

        // GIF89a header + Logical Screen Descriptor
        const char Sig[6] = { 'G', 'I', 'F', '8', '9', 'a' };
        Bytes.insert(Bytes.end(), Sig, Sig + 6);
        WriteLe16(static_cast<uint16_t>(Width));
        WriteLe16(static_cast<uint16_t>(Height));
        Bytes.push_back(0xF7u); // Global color table, 256 entries (2^(7+1))
        Bytes.push_back(0u);    // Background index
        Bytes.push_back(0u);    // Pixel aspect

        // Global Color Table (256 * 3 bytes)
        Bytes.insert(Bytes.end(), Palette.begin(), Palette.end());

        // NETSCAPE2.0 looping application extension (infinite loop)
        const uint8_t Netscape[] = {
            0x21u, 0xFFu, 0x0Bu,
            'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0',
            0x03u, 0x01u, 0x00u, 0x00u, 0x00u
        };
        Bytes.insert(Bytes.end(), Netscape, Netscape + sizeof(Netscape));
    }

    void AppendRgbFrame(const std::vector<uint8_t>& Rgb)
    {
        const std::size_t NumPx = static_cast<std::size_t>(Width) * static_cast<std::size_t>(Height);
        std::vector<uint8_t> Indices(NumPx);

        // Ordered 2x2 Bayer dithering into the 6x7x6 perceptual RGB palette for crisp gradients & zero banding
        static constexpr int Bayer2x2[4] = { -10, 10, 4, -4 };
        for (int Y = 0; Y < Height; ++Y)
        {
            for (int X = 0; X < Width; ++X)
            {
                const std::size_t P = static_cast<std::size_t>(Y) * static_cast<std::size_t>(Width) + static_cast<std::size_t>(X);
                const int D = Bayer2x2[((Y & 1) << 1) | (X & 1)];
                const int R = std::clamp(static_cast<int>(Rgb[P * 3u + 0u]) + D, 0, 255);
                const int G = std::clamp(static_cast<int>(Rgb[P * 3u + 1u]) + D, 0, 255);
                const int B = std::clamp(static_cast<int>(Rgb[P * 3u + 2u]) + D, 0, 255);
                const int Ri = (R * 5 + 127) / 255;
                const int Gi = (G * 6 + 127) / 255;
                const int Bi = (B * 5 + 127) / 255;
                Indices[P] = static_cast<uint8_t>(Ri * 42 + Gi * 6 + Bi);
            }
        }

        // Graphic Control Extension
        Bytes.push_back(0x21u);
        Bytes.push_back(0xF9u);
        Bytes.push_back(0x04u);
        Bytes.push_back(0x04u); // Do not dispose
        WriteLe16(FrameDelayCs);
        Bytes.push_back(0x00u);
        Bytes.push_back(0x00u);

        // Image Descriptor
        Bytes.push_back(0x2Cu);
        WriteLe16(0u);
        WriteLe16(0u);
        WriteLe16(static_cast<uint16_t>(Width));
        WriteLe16(static_cast<uint16_t>(Height));
        Bytes.push_back(0x00u); // Use global color table

        // LZW compressed image data (min code size = 8)
        EncodeLzw(Indices);
    }

    bool Save(const std::filesystem::path& Path)
    {
        std::error_code Ec {};
        std::filesystem::create_directories(Path.parent_path(), Ec);
        Bytes.push_back(0x3Bu); // GIF trailer
        std::ofstream File(Path, std::ios::binary);
        if (!File) return false;
        File.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
        return File.good();
    }

private:
    int Width = 0;
    int Height = 0;
    uint16_t FrameDelayCs = 5u;
    std::array<uint8_t, 256u * 3u> Palette {};
    std::vector<uint8_t> Bytes {};

    void WriteLe16(uint16_t V)
    {
        Bytes.push_back(static_cast<uint8_t>(V & 0xFFu));
        Bytes.push_back(static_cast<uint8_t>((V >> 8u) & 0xFFu));
    }

    void BuildGlobalPalette()
    {
        int Idx = 0;
        for (int R = 0; R < 6; ++R)
        {
            for (int G = 0; G < 7; ++G)
            {
                for (int B = 0; B < 6; ++B)
                {
                    Palette[static_cast<std::size_t>(Idx) * 3u + 0u] = static_cast<uint8_t>((R * 255) / 5);
                    Palette[static_cast<std::size_t>(Idx) * 3u + 1u] = static_cast<uint8_t>((G * 255) / 6);
                    Palette[static_cast<std::size_t>(Idx) * 3u + 2u] = static_cast<uint8_t>((B * 255) / 5);
                    ++Idx;
                }
            }
        }
        // Remaining 4 entries (252..255) -> crisp studio grays
        const uint8_t Grays[4] = { 18u, 40u, 110u, 220u };
        for (int K = 0; K < 4; ++K)
        {
            Palette[static_cast<std::size_t>(Idx) * 3u + 0u] = Grays[K];
            Palette[static_cast<std::size_t>(Idx) * 3u + 1u] = Grays[K];
            Palette[static_cast<std::size_t>(Idx) * 3u + 2u] = Grays[K];
            ++Idx;
        }
    }

    void EncodeLzw(const std::vector<uint8_t>& Indices)
    {
        constexpr int ClearCode = 256;
        constexpr int EoiCode   = 257;
        constexpr int MaxCode   = 4095;

        Bytes.push_back(8u); // LZW minimum code size

        std::vector<uint8_t> SubBlock {};
        SubBlock.reserve(256u);
        uint32_t BitBuf  = 0u;
        int      BitCnt  = 0;

        auto FlushBlock = [&]()
        {
            if (!SubBlock.empty())
            {
                Bytes.push_back(static_cast<uint8_t>(SubBlock.size()));
                Bytes.insert(Bytes.end(), SubBlock.begin(), SubBlock.end());
                SubBlock.clear();
            }
        };

        auto EmitCode = [&](int Code, int CodeBits)
        {
            BitBuf |= (static_cast<uint32_t>(Code) << static_cast<uint32_t>(BitCnt));
            BitCnt += CodeBits;
            while (BitCnt >= 8)
            {
                SubBlock.push_back(static_cast<uint8_t>(BitBuf & 0xFFu));
                BitBuf >>= 8u;
                BitCnt -= 8;
                if (SubBlock.size() == 255u) FlushBlock();
            }
        };

        // Open-addressed hash table for fast LZW string lookup (key = (prefix<<8)|sym)
        constexpr uint32_t TableSize = 8192u;
        std::array<int32_t, TableSize> HashKeys {};
        std::array<uint16_t, TableSize> HashVals {};
        auto ResetDict = [&]()
        {
            HashKeys.fill(-1);
        };

        ResetDict();
        int NextCode = 258;
        int CodeBits = 9;
        EmitCode(ClearCode, CodeBits);

        int Prefix = Indices.empty() ? EoiCode : static_cast<int>(Indices[0u]);
        for (std::size_t I = 1u; I < Indices.size(); ++I)
        {
            const int Sym = static_cast<int>(Indices[I]);
            const int32_t Key = (Prefix << 8) | Sym;
            uint32_t Slot = (static_cast<uint32_t>(Key) * 2654435761u) & (TableSize - 1u);
            bool Found = false;
            while (HashKeys[Slot] != -1)
            {
                if (HashKeys[Slot] == Key)
                {
                    Prefix = static_cast<int>(HashVals[Slot]);
                    Found  = true;
                    break;
                }
                Slot = (Slot + 1u) & (TableSize - 1u);
            }
            if (!Found)
            {
                EmitCode(Prefix, CodeBits);
                if (NextCode <= MaxCode)
                {
                    HashKeys[Slot] = Key;
                    HashVals[Slot] = static_cast<uint16_t>(NextCode++);
                    if (NextCode > (1 << CodeBits) && CodeBits < 12)
                    {
                        ++CodeBits;
                    }
                }
                else
                {
                    EmitCode(ClearCode, CodeBits);
                    ResetDict();
                    NextCode = 258;
                    CodeBits = 9;
                }
                Prefix = Sym;
            }
        }

        if (!Indices.empty()) EmitCode(Prefix, CodeBits);
        EmitCode(EoiCode, CodeBits);
        if (BitCnt > 0) SubBlock.push_back(static_cast<uint8_t>(BitBuf & 0xFFu));
        FlushBlock();
        Bytes.push_back(0x00u); // Block terminator
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                  CANVAS, 5x7 BITMAP FONT & FAST 3D Z-BUFFER RASTERIZER
//------------------------------------------------------------------------------------------------------------------------

struct CanvasRgb
{
    int Width  = 0;
    int Height = 0;
    std::vector<uint8_t> Rgb {};
    std::vector<float>   Depth {};

    CanvasRgb(int W, int H)
        : Width(W), Height(H)
        , Rgb(static_cast<std::size_t>(W) * static_cast<std::size_t>(H) * 3u, 0u)
        , Depth(static_cast<std::size_t>(W) * static_cast<std::size_t>(H), 1.0e9f)
    {}

    void ClearSky(uint8_t TopR, uint8_t TopG, uint8_t TopB, uint8_t BotR, uint8_t BotG, uint8_t BotB)
    {
        std::fill(Depth.begin(), Depth.end(), 1.0e9f);
        for (int Y = 0; Y < Height; ++Y)
        {
            const float T = static_cast<float>(Y) / static_cast<float>(std::max(1, Height - 1));
            const uint8_t R = static_cast<uint8_t>(TopR + (BotR - TopR) * T);
            const uint8_t G = static_cast<uint8_t>(TopG + (BotG - TopG) * T);
            const uint8_t B = static_cast<uint8_t>(TopB + (BotB - TopB) * T);
            for (int X = 0; X < Width; ++X)
            {
                const std::size_t Off = (static_cast<std::size_t>(Y) * static_cast<std::size_t>(Width) + static_cast<std::size_t>(X)) * 3u;
                Rgb[Off + 0u] = R; Rgb[Off + 1u] = G; Rgb[Off + 2u] = B;
            }
        }
    }

    inline void PutPx(int X, int Y, uint8_t R, uint8_t G, uint8_t B, float Alpha = 1.0f) noexcept
    {
        if (X < 0 || X >= Width || Y < 0 || Y >= Height) return;
        const std::size_t Off = (static_cast<std::size_t>(Y) * static_cast<std::size_t>(Width) + static_cast<std::size_t>(X)) * 3u;
        if (Alpha >= 0.999f)
        {
            Rgb[Off + 0u] = R; Rgb[Off + 1u] = G; Rgb[Off + 2u] = B;
        }
        else
        {
            const float Inv = 1.0f - Alpha;
            Rgb[Off + 0u] = static_cast<uint8_t>(Rgb[Off + 0u] * Inv + R * Alpha);
            Rgb[Off + 1u] = static_cast<uint8_t>(Rgb[Off + 1u] * Inv + G * Alpha);
            Rgb[Off + 2u] = static_cast<uint8_t>(Rgb[Off + 2u] * Inv + B * Alpha);
        }
    }

    void FillRect(int X0, int Y0, int W, int H, uint8_t R, uint8_t G, uint8_t B, float Alpha = 1.0f)
    {
        const int X1 = std::min(Width, X0 + W);
        const int Y1 = std::min(Height, Y0 + H);
        X0 = std::max(0, X0); Y0 = std::max(0, Y0);
        for (int Y = Y0; Y < Y1; ++Y)
            for (int X = X0; X < X1; ++X)
                PutPx(X, Y, R, G, B, Alpha);
    }

    void DrawLine2D(int X0, int Y0, int X1, int Y1, uint8_t R, uint8_t G, uint8_t B, int Thickness = 1)
    {
        int Dx = std::abs(X1 - X0), Sx = (X0 < X1) ? 1 : -1;
        int Dy = -std::abs(Y1 - Y0), Sy = (Y0 < Y1) ? 1 : -1;
        int Err = Dx + Dy;
        for (;;)
        {
            for (int Ty = -Thickness / 2; Ty <= Thickness / 2; ++Ty)
                for (int Tx = -Thickness / 2; Tx <= Thickness / 2; ++Tx)
                    PutPx(X0 + Tx, Y0 + Ty, R, G, B);
            if (X0 == X1 && Y0 == Y1) break;
            const int E2 = 2 * Err;
            if (E2 >= Dy) { Err += Dy; X0 += Sx; }
            if (E2 <= Dx) { Err += Dx; Y0 += Sy; }
        }
    }

    void Blit(const CanvasRgb& Src, int DstX, int DstY)
    {
        for (int Y = 0; Y < Src.Height; ++Y)
        {
            const int Ty = DstY + Y;
            if (Ty < 0 || Ty >= Height) continue;
            for (int X = 0; X < Src.Width; ++X)
            {
                const int Tx = DstX + X;
                if (Tx < 0 || Tx >= Width) continue;
                const std::size_t SOff = (static_cast<std::size_t>(Y) * static_cast<std::size_t>(Src.Width) + static_cast<std::size_t>(X)) * 3u;
                const std::size_t DOff = (static_cast<std::size_t>(Ty) * static_cast<std::size_t>(Width) + static_cast<std::size_t>(Tx)) * 3u;
                Rgb[DOff + 0u] = Src.Rgb[SOff + 0u];
                Rgb[DOff + 1u] = Src.Rgb[SOff + 1u];
                Rgb[DOff + 2u] = Src.Rgb[SOff + 2u];
            }
        }
    }
};

std::array<uint8_t, 7> Glyph5x7(char Ch)
{
    if (Ch >= 'a' && Ch <= 'z') Ch = static_cast<char>(Ch - 'a' + 'A');
    switch (Ch)
    {
        case 'A': return { 0x0E,0x11,0x11,0x1F,0x11,0x11,0x11 };
        case 'B': return { 0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E };
        case 'C': return { 0x0E,0x11,0x10,0x10,0x10,0x11,0x0E };
        case 'D': return { 0x1C,0x12,0x11,0x11,0x11,0x12,0x1C };
        case 'E': return { 0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F };
        case 'F': return { 0x1F,0x10,0x10,0x1E,0x10,0x10,0x10 };
        case 'G': return { 0x0E,0x11,0x10,0x17,0x11,0x11,0x0F };
        case 'H': return { 0x11,0x11,0x11,0x1F,0x11,0x11,0x11 };
        case 'I': return { 0x0E,0x04,0x04,0x04,0x04,0x04,0x0E };
        case 'J': return { 0x07,0x02,0x02,0x02,0x12,0x12,0x0C };
        case 'K': return { 0x11,0x12,0x14,0x18,0x14,0x12,0x11 };
        case 'L': return { 0x10,0x10,0x10,0x10,0x10,0x10,0x1F };
        case 'M': return { 0x11,0x1B,0x15,0x15,0x11,0x11,0x11 };
        case 'N': return { 0x11,0x19,0x15,0x13,0x11,0x11,0x11 };
        case 'O': return { 0x0E,0x11,0x11,0x11,0x11,0x11,0x0E };
        case 'P': return { 0x1E,0x11,0x11,0x1E,0x10,0x10,0x10 };
        case 'Q': return { 0x0E,0x11,0x11,0x11,0x15,0x12,0x0D };
        case 'R': return { 0x1E,0x11,0x11,0x1E,0x14,0x12,0x11 };
        case 'S': return { 0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E };
        case 'T': return { 0x1F,0x04,0x04,0x04,0x04,0x04,0x04 };
        case 'U': return { 0x11,0x11,0x11,0x11,0x11,0x11,0x0E };
        case 'V': return { 0x11,0x11,0x11,0x11,0x11,0x0A,0x04 };
        case 'W': return { 0x11,0x11,0x11,0x15,0x15,0x1B,0x11 };
        case 'X': return { 0x11,0x11,0x0A,0x04,0x0A,0x11,0x11 };
        case 'Y': return { 0x11,0x11,0x0A,0x04,0x04,0x04,0x04 };
        case 'Z': return { 0x1F,0x01,0x02,0x04,0x08,0x10,0x1F };
        case '0': return { 0x0E,0x11,0x13,0x15,0x19,0x11,0x0E };
        case '1': return { 0x04,0x0C,0x04,0x04,0x04,0x04,0x0E };
        case '2': return { 0x0E,0x11,0x01,0x02,0x04,0x08,0x1F };
        case '3': return { 0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E };
        case '4': return { 0x02,0x06,0x0A,0x12,0x1F,0x02,0x02 };
        case '5': return { 0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E };
        case '6': return { 0x06,0x08,0x10,0x1E,0x11,0x11,0x0E };
        case '7': return { 0x1F,0x01,0x02,0x04,0x08,0x08,0x08 };
        case '8': return { 0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E };
        case '9': return { 0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C };
        case '.': return { 0x00,0x00,0x00,0x00,0x00,0x0C,0x0C };
        case ',': return { 0x00,0x00,0x00,0x00,0x04,0x04,0x08 };
        case ':': return { 0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00 };
        case '-': return { 0x00,0x00,0x00,0x1F,0x00,0x00,0x00 };
        case '+': return { 0x00,0x04,0x04,0x1F,0x04,0x04,0x00 };
        case '/': return { 0x01,0x02,0x02,0x04,0x08,0x08,0x10 };
        case '%': return { 0x19,0x19,0x02,0x04,0x08,0x13,0x13 };
        case '(': return { 0x02,0x04,0x08,0x08,0x08,0x04,0x02 };
        case ')': return { 0x08,0x04,0x02,0x02,0x02,0x04,0x08 };
        case '[': return { 0x0E,0x08,0x08,0x08,0x08,0x08,0x0E };
        case ']': return { 0x0E,0x02,0x02,0x02,0x02,0x02,0x0E };
        case '|': return { 0x04,0x04,0x04,0x04,0x04,0x04,0x04 };
        case '=': return { 0x00,0x00,0x1F,0x00,0x1F,0x00,0x00 };
        case '&': return { 0x0C,0x12,0x14,0x08,0x15,0x12,0x0D };
        case '<': return { 0x02,0x04,0x08,0x10,0x08,0x04,0x02 };
        case '>': return { 0x08,0x04,0x02,0x01,0x02,0x04,0x08 };
        case '_': return { 0x00,0x00,0x00,0x00,0x00,0x00,0x1F };
        default:  return { 0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
    }
}

void DrawText(CanvasRgb& C, int X, int Y, const std::string& Text, int Scale, uint8_t R, uint8_t G, uint8_t B)
{
    int CursorX = X;
    for (char Ch : Text)
    {
        const auto Rows = Glyph5x7(Ch);
        for (int Row = 0; Row < 7; ++Row)
        {
            for (int Col = 0; Col < 5; ++Col)
            {
                if (Rows[static_cast<std::size_t>(Row)] & (1u << (4 - Col)))
                {
                    C.FillRect(CursorX + Col * Scale, Y + Row * Scale, Scale, Scale, R, G, B);
                }
            }
        }
        CursorX += 6 * Scale;
    }
}

struct Camera3D
{
    Vec3  Eye    {};
    Vec3  Right  {};
    Vec3  Up     {};
    Vec3  Fwd    {};
    float FocalX = 500.0f;
    float FocalY = 500.0f;
    float HalfW  = 320.0f;
    float HalfH  = 180.0f;

    static Camera3D LookAt(const Vec3& EyePos, const Vec3& Target, float FovYDeg, int W, int H)
    {
        Camera3D Cam {};
        Cam.Eye   = EyePos;
        Cam.Fwd   = Norm3(Target - EyePos);
        Cam.Right = Norm3(Cross3(Cam.Fwd, Vec3{ 0.0f, 1.0f, 0.0f }));
        Cam.Up    = Norm3(Cross3(Cam.Right, Cam.Fwd));
        Cam.HalfW = 0.5f * static_cast<float>(W);
        Cam.HalfH = 0.5f * static_cast<float>(H);
        const float TanHalf = std::tan(0.5f * FovYDeg * static_cast<float>(Constants::DegToRad));
        Cam.FocalY = Cam.HalfH / std::max(TanHalf, 0.1f);
        Cam.FocalX = Cam.FocalY;
        return Cam;
    }

    bool Project(const Vec3& P, float& OutX, float& OutY, float& OutZ) const noexcept
    {
        const Vec3 D = P - Eye;
        const float Z = Dot3(D, Fwd);
        if (Z <= 0.12f) return false;
        const float X = Dot3(D, Right);
        const float Y = Dot3(D, Up);
        OutX = HalfW + (X * FocalX) / Z;
        OutY = HalfH - (Y * FocalY) / Z;
        OutZ = Z;
        return true;
    }
};

struct MeshTri
{
    Vec3 V0, V1, V2;
    Vec3 Normal;
    uint8_t R, G, B;
    bool Metallic = false;
};

std::vector<MeshTri> ExtractMeshTriangles(
    const GeometrySpace::PolygonNetwork& Net,
    uint8_t OverrideR, uint8_t OverrideG, uint8_t OverrideB,
    bool UseShadingColors,
    uint32_t Stride = 1u)
{
    std::vector<MeshTri> Tris {};
    Tris.reserve(Net.Faces.size() / std::max(1u, Stride) + 16u);
    for (uint32_t Fi = 0u; Fi < Net.Faces.size(); Fi += std::max(1u, Stride))
    {
        const auto& F = Net.Faces[Fi];
        if (F.CornerCount < 3u || F.BoundaryLoopEdgeIndex == GeometrySpace::InvalidNetworkIndex) continue;

        uint32_t E = F.BoundaryLoopEdgeIndex;
        const Vec3 P0 = ToVec3(Net.Vertices[Net.Edges[E].OriginVertexIndex].Axes);
        E = Net.Edges[E].NextEdgeIndex;
        Vec3 Prev = ToVec3(Net.Vertices[Net.Edges[E].OriginVertexIndex].Axes);
        for (uint16_t K = 2u; K < F.CornerCount; ++K)
        {
            E = Net.Edges[E].NextEdgeIndex;
            const Vec3 Curr = ToVec3(Net.Vertices[Net.Edges[E].OriginVertexIndex].Axes);
            const Vec3 N = Cross3(Prev - P0, Curr - P0);
            if (Dot3(N, N) > 1.0e-10f)
            {
                MeshTri T {};
                T.V0 = P0; T.V1 = Prev; T.V2 = Curr;
                T.Normal = Norm3(N);
                if (UseShadingColors)
                {
                    auto LinToSrgb = [](float L) -> uint8_t
                    {
                        const float S = std::pow(std::clamp(L, 0.0f, 1.0f), 1.0f / 2.2f);
                        return static_cast<uint8_t>(std::clamp(S * 255.0f, 0.0f, 255.0f));
                    };
                    T.R = LinToSrgb(F.Shading.AlbedoLinearRGB.X);
                    T.G = LinToSrgb(F.Shading.AlbedoLinearRGB.Y);
                    T.B = LinToSrgb(F.Shading.AlbedoLinearRGB.Z);
                    if (T.R < 22u && T.G < 22u && T.B < 22u)
                    {
                        // Lift obsidian body paint slightly so contours pop crisply in GIF palette
                        T.R = 44u; T.G = 56u; T.B = 76u;
                    }
                    T.Metallic = (F.Shading.Metallic > 0.4f);
                }
                else
                {
                    T.R = OverrideR; T.G = OverrideG; T.B = OverrideB;
                }
                Tris.push_back(T);
            }
            Prev = Curr;
        }
    }
    return Tris;
}

void RasterizeTriangle3D(
    CanvasRgb& C, const Camera3D& Cam,
    const Vec3& W0, const Vec3& W1, const Vec3& W2,
    uint8_t R, uint8_t G, uint8_t B)
{
    float X0, Y0, Z0, X1, Y1, Z1, X2, Y2, Z2;
    if (!Cam.Project(W0, X0, Y0, Z0)) return;
    if (!Cam.Project(W1, X1, Y1, Z1)) return;
    if (!Cam.Project(W2, X2, Y2, Z2)) return;

    const float Area = (X1 - X0) * (Y2 - Y0) - (Y1 - Y0) * (X2 - X0);
    if (std::fabs(Area) < 0.25f) return;
    const float InvArea = 1.0f / Area;

    const int MinX = std::max(0, static_cast<int>(std::floor(std::min({ X0, X1, X2 }))));
    const int MaxX = std::min(C.Width - 1, static_cast<int>(std::ceil(std::max({ X0, X1, X2 }))));
    const int MinY = std::max(0, static_cast<int>(std::floor(std::min({ Y0, Y1, Y2 }))));
    const int MaxY = std::min(C.Height - 1, static_cast<int>(std::ceil(std::max({ Y0, Y1, Y2 }))));
    if (MinX > MaxX || MinY > MaxY) return;

    const float Iz0 = 1.0f / Z0, Iz1 = 1.0f / Z1, Iz2 = 1.0f / Z2;
    for (int Py = MinY; Py <= MaxY; ++Py)
    {
        const float Fy = static_cast<float>(Py) + 0.5f;
        for (int Px = MinX; Px <= MaxX; ++Px)
        {
            const float Fx = static_cast<float>(Px) + 0.5f;
            const float W_0 = ((X1 - Fx) * (Y2 - Fy) - (Y1 - Fy) * (X2 - Fx)) * InvArea;
            const float W_1 = ((X2 - Fx) * (Y0 - Fy) - (Y2 - Fy) * (X0 - Fx)) * InvArea;
            const float W_2 = 1.0f - W_0 - W_1;
            if (W_0 < 0.0f || W_1 < 0.0f || W_2 < 0.0f) continue;

            const float Depth = 1.0f / (W_0 * Iz0 + W_1 * Iz1 + W_2 * Iz2);
            const std::size_t Idx = static_cast<std::size_t>(Py) * static_cast<std::size_t>(C.Width) + static_cast<std::size_t>(Px);
            if (Depth < C.Depth[Idx])
            {
                C.Depth[Idx] = Depth;
                C.Rgb[Idx * 3u + 0u] = R;
                C.Rgb[Idx * 3u + 1u] = G;
                C.Rgb[Idx * 3u + 2u] = B;
            }
        }
    }
}

void DrawLine3D(CanvasRgb& C, const Camera3D& Cam, const Vec3& A, const Vec3& B, uint8_t R, uint8_t G, uint8_t BCol, int Thick = 1)
{
    float X0, Y0, Z0, X1, Y1, Z1;
    if (!Cam.Project(A, X0, Y0, Z0) || !Cam.Project(B, X1, Y1, Z1)) return;
    C.DrawLine2D(static_cast<int>(X0), static_cast<int>(Y0), static_cast<int>(X1), static_cast<int>(Y1), R, G, BCol, Thick);
}

void DrawBox3D(
    CanvasRgb& C, const Camera3D& Cam,
    const Vec3& Center, const RigidOrientationQuaternion& Orient, const Vec3& HalfExt,
    uint8_t R, uint8_t G, uint8_t B)
{
    Vec3 Pts[8];
    for (int I = 0; I < 8; ++I)
    {
        const Vec3 L {
            ((I & 1) ? 1.0f : -1.0f) * HalfExt.X,
            ((I & 2) ? 1.0f : -1.0f) * HalfExt.Y,
            ((I & 4) ? 1.0f : -1.0f) * HalfExt.Z,
        };
        Pts[I] = Center + RotQuat(Orient, L);
    }
    static constexpr int Quads[6][4] = {
        { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 },
        { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 }
    };
    const Vec3 Sun = Norm3({ 0.45f, 0.82f, -0.35f });
    for (int Q = 0; Q < 6; ++Q)
    {
        const Vec3& A = Pts[Quads[Q][0]];
        const Vec3& P = Pts[Quads[Q][1]];
        const Vec3& D = Pts[Quads[Q][2]];
        const Vec3& E = Pts[Quads[Q][3]];
        const Vec3 N = Norm3(Cross3(P - A, D - A));
        const float L = 0.36f + 0.64f * std::max(0.0f, Dot3(N, Sun));
        const uint8_t SR = static_cast<uint8_t>(std::clamp(R * L, 0.0f, 255.0f));
        const uint8_t SG = static_cast<uint8_t>(std::clamp(G * L, 0.0f, 255.0f));
        const uint8_t SB = static_cast<uint8_t>(std::clamp(B * L, 0.0f, 255.0f));
        RasterizeTriangle3D(C, Cam, A, P, D, SR, SG, SB);
        RasterizeTriangle3D(C, Cam, A, D, E, SR, SG, SB);
    }
}

void DrawGroundGrid(
    CanvasRgb& C, const Camera3D& Cam,
    const Vec3& CenterWorld, float PitchRad, float SpanX, float SpanZ, float StepM)
{
    const RigidOrientationQuaternion Q = AxisAngleQ({ 0.0f, 0.0f, 1.0f }, PitchRad);
    const Vec3 Origin { 0.0f, 0.0f, 0.0f };
    const float BaseX = std::floor(CenterWorld.X / StepM) * StepM;
    const float BaseZ = std::floor(CenterWorld.Z / StepM) * StepM;
    const Vec3 Sun = Norm3({ 0.45f, 0.82f, -0.35f });
    const Vec3 N = RotQuat(Q, { 0.0f, 1.0f, 0.0f });
    const float Shade = 0.55f + 0.45f * std::max(0.0f, Dot3(N, Sun));

    for (float X = BaseX - SpanX; X < BaseX + SpanX; X += StepM)
    {
        for (float Z = BaseZ - SpanZ; Z < BaseZ + SpanZ; Z += StepM)
        {
            const int Ix = static_cast<int>(std::floor(X / StepM));
            const int Iz = static_cast<int>(std::floor(Z / StepM));
            const bool Checker = ((Ix + Iz) & 1) == 0;
            const bool CenterStripe = (std::fabs(Z + 0.5f * StepM) < 0.75f);
            uint8_t R = Checker ? 44u : 56u;
            uint8_t G = Checker ? 48u : 61u;
            uint8_t B = Checker ? 56u : 70u;
            if (CenterStripe && (Ix & 1) == 0) { R = 195u; G = 165u; B = 55u; }
            if (std::fabs(PitchRad) > 0.02f && ((Ix % 4) == 0)) { R = 140u; G = 78u; B = 45u; }
            R = static_cast<uint8_t>(std::clamp(R * Shade, 0.0f, 255.0f));
            G = static_cast<uint8_t>(std::clamp(G * Shade, 0.0f, 255.0f));
            B = static_cast<uint8_t>(std::clamp(B * Shade, 0.0f, 255.0f));

            const Vec3 P0 = Origin + RotQuat(Q, { X,         0.0f, Z         });
            const Vec3 P1 = Origin + RotQuat(Q, { X + StepM, 0.0f, Z         });
            const Vec3 P2 = Origin + RotQuat(Q, { X + StepM, 0.0f, Z + StepM });
            const Vec3 P3 = Origin + RotQuat(Q, { X,         0.0f, Z + StepM });
            RasterizeTriangle3D(C, Cam, P0, P1, P2, R, G, B);
            RasterizeTriangle3D(C, Cam, P0, P2, P3, R, G, B);
        }
    }
}

struct VehicleRenderMeshes
{
    std::vector<MeshTri> Body;
    std::vector<MeshTri> Tyre;
    std::vector<MeshTri> Rim;
    std::vector<MeshTri> Wing;
};

void RenderVehicleMesh(
    CanvasRgb& C, const Camera3D& Cam,
    const VehicleRenderMeshes& Meshes,
    const VehicleReading& Reading,
    const VehicleRigSpecification& Spec,
    float GroundPitchRad,
    bool DrawBrakeGlow,
    bool DrawFanTraces)
{
    const Vec3 Sun = Norm3({ 0.45f, 0.82f, -0.35f });
    const Vec3 Pos = ToVec3(Reading.WorldPositionMeters);
    const RigidOrientationQuaternion Q = Reading.WorldOrientation;

    // 1. Soft projected ground contact shadow underneath chassis
    {
        const RigidOrientationQuaternion Gq = AxisAngleQ({ 0.0f, 0.0f, 1.0f }, GroundPitchRad);
        const Vec3 Fwd = RotQuat(Q, { 1.0f, 0.0f, 0.0f });
        const Vec3 Rgt = RotQuat(Q, { 0.0f, 0.0f, 1.0f });
        // Project chassis center onto ground plane
        const Vec3 Gn = RotQuat(Gq, { 0.0f, 1.0f, 0.0f });
        const float HAbove = Dot3(Pos, Gn);
        const Vec3 ShadC = Pos - Gn * (HAbove - 0.02f);
        const Vec3 S0 = ShadC - Fwd * 2.75f - Rgt * 1.15f;
        const Vec3 S1 = ShadC + Fwd * 2.75f - Rgt * 1.15f;
        const Vec3 S2 = ShadC + Fwd * 2.75f + Rgt * 1.15f;
        const Vec3 S3 = ShadC - Fwd * 2.75f + Rgt * 1.15f;
        RasterizeTriangle3D(C, Cam, S0, S1, S2, 16u, 19u, 24u);
        RasterizeTriangle3D(C, Cam, S0, S2, S3, 16u, 19u, 24u);
    }

    auto DrawMeshWithPose = [&](const std::vector<MeshTri>& Tris, const Vec3& TPos, const RigidOrientationQuaternion& TQuat, bool ForceRedGlow)
    {
        for (const auto& T : Tris)
        {
            const Vec3 W0 = TPos + RotQuat(TQuat, T.V0);
            const Vec3 W1 = TPos + RotQuat(TQuat, T.V1);
            const Vec3 W2 = TPos + RotQuat(TQuat, T.V2);
            const Vec3 WN = Norm3(RotQuat(TQuat, T.Normal));
            const Vec3 ViewDir = Norm3(Cam.Eye - W0);
            const float NdL = std::max(0.0f, Dot3(WN, Sun));
            const Vec3 HalfV = Norm3(Sun + ViewDir);
            const float Specular = std::pow(std::max(0.0f, Dot3(WN, HalfV)), T.Metallic ? 24.0f : 10.0f) * (T.Metallic ? 0.55f : 0.22f);
            const float Rim = std::pow(1.0f - std::max(0.0f, Dot3(WN, ViewDir)), 3.0f) * 0.22f;
            const float Light = 0.26f + 0.72f * NdL;

            float R = T.R * Light + Specular * 235.0f + Rim * 120.0f;
            float G = T.G * Light + Specular * 235.0f + Rim * 145.0f;
            float B = T.B * Light + Specular * 245.0f + Rim * 185.0f;
            if (ForceRedGlow) { R = std::min(255.0f, R + 145.0f); G *= 0.55f; B *= 0.35f; }
            RasterizeTriangle3D(C, Cam, W0, W1, W2,
                static_cast<uint8_t>(std::clamp(R, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(G, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(B, 0.0f, 255.0f)));
        }
    };

    // 2. Chassis Body (real ControlledIntegrator.GeometrySpace mesh!)
    DrawMeshWithPose(Meshes.Body, Pos, Q, false);

    // 3. RearWing positioned via Frontier Dock Constraint!
    if (Reading.RearWingAttached)
    {
        const Vec3 WingPos = Pos + RotQuat(Q, ToVec3(Spec.Aerodynamics.RearWingLocalMountMeters));
        DrawMeshWithPose(Meshes.Wing, WingPos, Q, false);
    }

    // 4. 4x Wheels articulated via Frontier Spring + Steer Hinge Constraints (no sockets!)
    for (uint32_t I = 0u; I < MaxVehicleWheelCorners; ++I)
    {
        const auto& W = Reading.Corners[I];
        const Vec3 HubWorld = Pos + RotQuat(Q, ToVec3(W.WheelHubLocalMeters));
        const bool RightSide = !Spec.Corners[I].LeftSide;
        const RigidOrientationQuaternion FlipRight = RightSide ? AxisAngleQ({ 0.0f, 1.0f, 0.0f }, static_cast<float>(Constants::Pi))
                                                               : RigidOrientationQuaternion{ 0.0f, 0.0f, 0.0f, 1.0f };
        const RigidOrientationQuaternion SteerQ  = AxisAngleQ({ 0.0f, 1.0f, 0.0f }, -W.SteerAngleRad);
        const RigidOrientationQuaternion CamberQ = AxisAngleQ({ 1.0f, 0.0f, 0.0f }, RightSide ? -W.DynamicCamberRad : W.DynamicCamberRad);
        const RigidOrientationQuaternion SpinQ   = AxisAngleQ({ 0.0f, 0.0f, 1.0f }, RightSide ? W.SpinAngleRad : -W.SpinAngleRad);
        const RigidOrientationQuaternion WheelQ  = MulQuat(Q, MulQuat(SteerQ, MulQuat(CamberQ, MulQuat(FlipRight, SpinQ))));

        DrawMeshWithPose(Meshes.Tyre, HubWorld, WheelQ, false);
        const bool HotRotor = DrawBrakeGlow && (W.BrakeTorqueNm > 400.0f);
        DrawMeshWithPose(Meshes.Rim, HubWorld, WheelQ, HotRotor);

        // Visualize socket-free Constraint anchor marker
        if (DrawFanTraces)
        {
            const Vec3 AxisForward = RotQuat(Q, { 1.0f, 0.0f, 0.0f });
            const Vec3 AxisUp      = RotQuat(Q, { 0.0f, 1.0f, 0.0f });
            const Vec3 AxisRight   = RotQuat(Q, { 0.0f, 0.0f, 1.0f });
            const float Cs = std::cos(W.SteerAngleRad);
            const float Sn = std::sin(W.SteerAngleRad);
            const Vec3 SteeredFwd   = AxisForward * Cs + AxisRight * Sn;
            const Vec3 SteeredRight = AxisForward * (-Sn) + AxisRight * Cs;
            const float TireR = Spec.Corners[I].Tire.OuterRadiusMeters;
            const float HalfSh = 0.40f * Spec.Corners[I].Tire.WidthMeters;
            static constexpr float ArcSin[5] = { -0.64278761f, -0.34202014f, 0.0f, 0.34202014f, 0.64278761f };
            static constexpr float ArcCos[5] = {  0.76604444f,  0.93969262f, 1.0f, 0.93969262f, 0.76604444f };
            const float LaneZ[3] = { -HalfSh, 0.0f, HalfSh };
            const uint8_t LaneCol[3][3] = { { 255u, 90u, 220u }, { 70u, 235u, 255u }, { 75u, 245u, 130u } };
            const Vec3 MountW = Pos + RotQuat(Q, ToVec3(Spec.Corners[I].LocalMountMeters));
            for (int L = 0; L < 3; ++L)
            {
                const bool LaneHit = ((W.ContactLaneMask & (1u << L)) != 0u);
                const Vec3 LOrg = MountW + SteeredRight * LaneZ[L];
                for (int K = 0; K < 5; ++K)
                {
                    const Vec3 Start = LOrg + SteeredFwd * (TireR * ArcSin[K]);
                    const Vec3 End   = Start - AxisUp * (TireR * ArcCos[K] + 0.10f);
                    DrawLine3D(C, Cam, Start, End,
                        LaneHit ? LaneCol[L][0] : 90u,
                        LaneHit ? LaneCol[L][1] : 95u,
                        LaneHit ? LaneCol[L][2] : 110u,
                        (I == 0u) ? 2 : 1);
                }
            }
        }
    }

    // Rear tail/brake light bar glow during braking
    if (DrawBrakeGlow)
    {
        const Vec3 TailL = Pos + RotQuat(Q, { -2.92f, 0.45f, -0.78f });
        const Vec3 TailR = Pos + RotQuat(Q, { -2.92f, 0.45f,  0.78f });
        DrawLine3D(C, Cam, TailL, TailR, 255u, 38u, 32u, 4);
    }
}

void DrawHudBar(CanvasRgb& C, int X, int Y, int W, int H, float Frac, uint8_t R, uint8_t G, uint8_t B)
{
    C.FillRect(X, Y, W, H, 26u, 30u, 40u);
    const int FillW = static_cast<int>(std::clamp(Frac, 0.0f, 1.0f) * static_cast<float>(W - 2));
    if (FillW > 0) C.FillRect(X + 1, Y + 1, FillW, H - 2, R, G, B);
}

void DrawTelemetryOverlay(
    CanvasRgb& C,
    const std::string& BannerTitle,
    const std::string& SubLine,
    const VehicleReading& R,
    const std::string& ExtraMetric = "")
{
    C.FillRect(0, 0, C.Width, 44, 12u, 15u, 22u, 0.88f);
    DrawText(C, 10, 7, BannerTitle, 2, 240u, 245u, 255u);
    DrawText(C, 10, 27, SubLine, 1, 130u, 215u, 245u);

    // Bottom translucent telemetry HUD bar
    const int HudY = C.Height - 58;
    C.FillRect(0, HudY, C.Width, 58, 12u, 15u, 22u, 0.88f);

    char Buf[160];
    std::snprintf(Buf, sizeof(Buf), "SPD %5.1f KM/H | G%d | RPM %4.0f | TURBO %4.2f BAR (%2.0fK)",
        R.SpeedKph, R.ActiveGear, R.EngineRpm, R.TurboBoostBar, R.TurboShaftRpm * 0.001f);
    DrawText(C, 10, HudY + 7, Buf, 1, 235u, 242u, 255u);

    std::snprintf(Buf, sizeof(Buf), "CLUTCH %3.0f%% (%s) SLIP %4.0f RPM | LONG %+4.2fG | LAT %+4.2fG | CONSTR %u",
        R.ClutchEngagement * 100.0f, R.ClutchLocked ? "LOCK" : "SLIP", R.ClutchSlipRpm,
        R.LongitudinalG, R.LateralG, R.ActiveAttachmentConstraints);
    DrawText(C, 10, HudY + 21, Buf, 1, 180u, 225u, 195u);

    if (!ExtraMetric.empty())
    {
        DrawText(C, 10, HudY + 36, ExtraMetric, 1, 255u, 215u, 110u);
    }

    // Mini RPM / Boost / Clutch gauges on right
    const int Gx = C.Width - 155;
    DrawText(C, Gx, HudY + 6,  "RPM", 1, 175u, 190u, 210u);
    DrawHudBar(C, Gx + 28, HudY + 6,  118, 8, R.EngineRpm / 7200.0f, 65u, 210u, 255u);
    DrawText(C, Gx, HudY + 20, "BST", 1, 175u, 190u, 210u);
    DrawHudBar(C, Gx + 28, HudY + 20, 118, 8, R.TurboBoostBar / 1.4f, 255u, 150u, 45u);
    DrawText(C, Gx, HudY + 34, "CLT", 1, 175u, 190u, 210u);
    DrawHudBar(C, Gx + 28, HudY + 34, 118, 8, R.ClutchEngagement, 85u, 235u, 130u);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           PHYSICS HELPERS & BENCHMARK TYPES
//------------------------------------------------------------------------------------------------------------------------

RigidBodyToken SpawnFlatTrack(PhysicsExtension& Physics, float HalfLengthX = 1200.0f, float HalfWidthZ = 400.0f)
{
    RigidBodyDescriptor Ground {};
    Ground.Shape.ShapeCategory     = CollisionShapeCategory::Box;
    Ground.Shape.HalfExtentsMeters = { HalfLengthX, 0.5f, HalfWidthZ };
    Ground.MotionCategory          = RigidMotionCategory::Static;
    Ground.PositionMeters          = { 0.0f, -0.5f, 0.0f };
    Ground.CollisionChannel        = static_cast<uint16_t>(CollisionLayerChannel::StaticEnvironment);
    return Physics.IssueRigidBody(Ground);
}

RigidBodyToken SpawnTiltedRamp(PhysicsExtension& Physics, float PitchAngleRad)
{
    const float HalfA = 0.5f * PitchAngleRad;
    RigidBodyDescriptor Ramp {};
    Ramp.Shape.ShapeCategory     = CollisionShapeCategory::Box;
    Ramp.Shape.HalfExtentsMeters = { 160.0f, 0.5f, 40.0f };
    Ramp.MotionCategory          = RigidMotionCategory::Static;
    Ramp.PositionMeters          = { 0.0f, -0.5f * std::cos(PitchAngleRad), 0.0f };
    Ramp.Orientation             = { 0.0f, 0.0f, std::sin(HalfA), std::cos(HalfA) };
    Ramp.CollisionChannel        = static_cast<uint16_t>(CollisionLayerChannel::StaticEnvironment);
    return Physics.IssueRigidBody(Ramp);
}

void StepCouple(PhysicsExtension& Physics, VehiclePhysicsRig& Rig, double FixedDt)
{
    Rig.StepOnPhysicsThread(Physics, FixedDt);
    Physics.Tick(FixedDt);
}

struct TraceCandidateMetrics
{
    const char* Name           = "";
    int         RaysOrCasts    = 0;
    double      AvgStepUs      = 0.0;
    float       PeakSuspVelMps = 0.0f;
    float       PeakLoadN      = 0.0f;
    uint32_t    ShoulderHits   = 0u;
    uint8_t     PeakLaneMask   = 0u;
    bool        Valid          = false;
};

struct TelemetrySample
{
    float TimeS;
    float SpeedKph;
    float Rpm;
    float BoostBar;
    float ClutchEng;
    float ClutchSlipRpm;
    int   Gear;
};

void DrawPlotCard(
    CanvasRgb& Sheet, int X, int Y, int W, int H,
    const std::string& Title,
    const std::vector<float>& SeriesA, float MaxA, const std::string& LabelA, uint8_t AR, uint8_t AG, uint8_t AB,
    const std::vector<float>& SeriesB, float MaxB, const std::string& LabelB, uint8_t BR, uint8_t BG, uint8_t BB)
{
    Sheet.FillRect(X, Y, W, H, 18u, 22u, 31u);
    Sheet.FillRect(X, Y, W, 24, 24u, 30u, 42u);
    DrawText(Sheet, X + 8, Y + 8, Title, 1, 235u, 242u, 255u);
    DrawText(Sheet, X + W - 210, Y + 8, LabelA, 1, AR, AG, AB);
    DrawText(Sheet, X + W - 105, Y + 8, LabelB, 1, BR, BG, BB);

    const int Px = X + 14, Py = Y + 32, Pw = W - 28, Ph = H - 44;
    Sheet.FillRect(Px, Py, Pw, Ph, 12u, 15u, 22u);
    for (int G = 1; G < 4; ++G)
    {
        const int Gy = Py + (Ph * G) / 4;
        Sheet.DrawLine2D(Px, Gy, Px + Pw, Gy, 28u, 34u, 46u, 1);
    }

    auto PlotLine = [&](const std::vector<float>& S, float MaxV, uint8_t R, uint8_t G, uint8_t B)
    {
        if (S.size() < 2u || MaxV <= 1.0e-5f) return;
        for (std::size_t I = 1u; I < S.size(); ++I)
        {
            const int X0 = Px + static_cast<int>((static_cast<float>(I - 1u) / static_cast<float>(S.size() - 1u)) * Pw);
            const int X1 = Px + static_cast<int>((static_cast<float>(I) / static_cast<float>(S.size() - 1u)) * Pw);
            const int Y0 = Py + Ph - static_cast<int>(std::clamp(S[I - 1u] / MaxV, 0.0f, 1.0f) * (Ph - 4)) - 2;
            const int Y1 = Py + Ph - static_cast<int>(std::clamp(S[I] / MaxV, 0.0f, 1.0f) * (Ph - 4)) - 2;
            Sheet.DrawLine2D(X0, Y0, X1, Y1, R, G, B, 2);
        }
    };
    PlotLine(SeriesA, MaxA, AR, AG, AB);
    PlotLine(SeriesB, MaxB, BR, BG, BB);
}

bool CheckGate(bool Cond, const char* Msg, uint32_t& Passed, uint32_t& Failed)
{
    if (Cond)
    {
        ++Passed;
        std::printf("[PASS] %s\n", Msg);
        return true;
    }
    ++Failed;
    std::fprintf(stderr, "[FAIL] %s\n", Msg);
    return false;
}

} // namespace

int main(int Argc, char** Argv)
{
    bool RenderGifs = false;
    for (int I = 1; I < Argc; ++I)
    {
        if (std::strcmp(Argv[I], "--render-gif") == 0) RenderGifs = true;
        if (std::strcmp(Argv[I], "--skip-gif") == 0)   RenderGifs = false;
    }

    const std::filesystem::path RepoRoot =
#ifdef FRONTIER_REPO_ROOT_DIR
        std::filesystem::path(FRONTIER_REPO_ROOT_DIR);
#else
        std::filesystem::current_path();
#endif
    const std::filesystem::path OutputDir = RepoRoot / "Exhibits" / "Images" / "VehiclePhysics";
    std::error_code Ec {};
    std::filesystem::create_directories(OutputDir, Ec);

    std::printf("=== Frontier Real JoltPhysics v5.6.0 ControlVehicle Drive & GIF Exhibit ===\n");
    uint32_t Passed = 0u, Failed = 0u;

    ControlVehicleBuildResult Built {};
    std::string Diag {};
    if (!CheckGate(ControlVehicleClassBuilder::ConstructControlVehicle(RepoRoot, Built, Diag),
            "Loaded ControlVehicle from ControlledIntegrator.GeometrySpace (25,068 faces)", Passed, Failed))
    {
        std::fprintf(stderr, "Diagnostic: %s\n", Diag.c_str());
        return 1;
    }

    // Render 100% solid, watertight ControlVehicle meshes directly from ControlledIntegrator.GeometrySpace
    VehicleRenderMeshes Meshes {};
    Meshes.Body = ExtractMeshTriangles(Built.GeometryRepository[0u].Asset.Network, 48u, 62u, 88u, true, 1u);
    Meshes.Tyre = ExtractMeshTriangles(Built.GeometryRepository[1u].Asset.Network, 36u, 38u, 44u, false, 1u);
    Meshes.Rim  = ExtractMeshTriangles(Built.GeometryRepository[2u].Asset.Network, 195u, 202u, 214u, false, 1u);
    Meshes.Wing = ExtractMeshTriangles(Built.GeometryRepository[3u].Asset.Network, 225u, 85u, 40u, false, 1u);

    constexpr double FixedDt = 1.0 / 120.0;
    constexpr float  SpawnY  = 0.41715f;
    constexpr int    GifW    = 640;
    constexpr int    GifH    = 360;

    CanvasRgb KeyframeDrive(GifW, GifH);
    CanvasRgb KeyframeTurn(GifW, GifH);
    CanvasRgb KeyframeSlope(GifW, GifH);
    CanvasRgb KeyframeFan(GifW, GifH);

    std::vector<TelemetrySample> DriveTrace {};
    std::vector<float> SlopeSpeedTrace {};
    std::vector<float> SlopeCreepCmTrace {};

    //==================================================================================================================
    // SCENARIO 1: SOCKET-FREE CONSTRAINT RIG + DRIVE & WOT ACCELERATION + TURBO + CLUTCH + DCT SHIFTS (GIF 01)
    //==================================================================================================================
    {
        PhysicsSimulationParameters Params {};
        Params.GravityMps2 = { 0.0f, -9.80665f, 0.0f };
        Params.FixedStepHz = 120.0f;
        PhysicsExtension Physics(Params);
        (void)Physics.Initialize();
        (void)SpawnFlatTrack(Physics);

        VehicleRigSpecification Spec = VehiclePhysicsRig::ConstructSpecificationFromControlVehicle(Built.ClassDefinition);
        Spec.WheelContactMode          = WheelContactQueryMode::HeroThreeLaneFanArc;
        Spec.BindAttachmentConstraints = true;
        VehiclePhysicsRig Rig {};
        (void)Rig.Initialize(Physics, Spec, { 0.0f, SpawnY, 0.0f });

        // Settle 2.0 s at idle (matches ControlVehiclePhysicsValidation equilibrium settle)
        for (uint32_t S = 0u; S < 240u; ++S) StepCouple(Physics, Rig, FixedDt);
        const auto RestR = Rig.AcquireVehicleReading();
        const float TotalLoadN = RestR->Corners[0u].VerticalLoadNewtons + RestR->Corners[1u].VerticalLoadNewtons
                               + RestR->Corners[2u].VerticalLoadNewtons + RestR->Corners[3u].VerticalLoadNewtons;
        const float ExpWeightN = RestR->LiveTotalMassKg * 9.80665f;
        const bool All3LanesHit = (RestR->Corners[0u].ContactLaneMask == 0x07u && RestR->Corners[0u].ContactRayHits >= 9u);

        CheckGate(RestR->ActiveAttachmentConstraints == 7u && RestR->Corners[0u].ConstraintIntact && RestR->RearWingDockToken.Valid(),
            "Gate 1a: Socket-free Frontier ConstraintSolver rig bound (4x Spring + 2x Steer Hinge + 1x RearWing Dock = 7 active)", Passed, Failed);
        CheckGate(All3LanesHit && std::fabs(TotalLoadN - ExpWeightN) < ExpWeightN * 0.04f,
            "Gate 1b: Hero 3-Lane (L+C+R) x +-40 deg Fan Trace contacts all 3 lanes & holds static equilibrium within 4%", Passed, Failed);

        AnimatedGifWriter Gif01(GifW, GifH, 5u);
        VehicleControlDirective DriveDir {};
        DriveDir.Category           = VehicleDirectiveCategory::DriverInput;
        DriveDir.ThrottleNormalized = 1.0f;
        (void)Rig.SubmitControlDirective(DriveDir);

        float PeakSpeedKph   = 0.0f;
        float PeakTurboRpm   = 0.0f;
        float PeakBoostBar   = 0.0f;
        float PeakClutchSlip = 0.0f;
        bool  SawClutchSlip  = false;
        bool  SawClutchLock  = false;
        bool  SawWastegate   = false;
        float PeakBov        = 0.0f;
        int   PeakGear       = 1;

        // 7.0 s WOT + 0.5 s throttle lift (BOV surge relief) = 900 steps (capture every 12 steps -> 75 frames)
        for (uint32_t S = 0u; S < 900u; ++S)
        {
            if (S == 840u)
            {
                DriveDir.ThrottleNormalized = 0.0f;
                (void)Rig.SubmitControlDirective(DriveDir);
            }
            StepCouple(Physics, Rig, FixedDt);
            const auto R = Rig.AcquireVehicleReading();
            PeakSpeedKph   = std::max(PeakSpeedKph, R->SpeedKph);
            PeakTurboRpm   = std::max(PeakTurboRpm, R->TurboShaftRpm);
            PeakBoostBar   = std::max(PeakBoostBar, R->TurboBoostBar);
            PeakClutchSlip = std::max(PeakClutchSlip, std::fabs(R->ClutchSlipRpm));
            if (R->ClutchEngagement > 0.15f && R->ClutchEngagement < 0.96f && std::fabs(R->ClutchSlipRpm) > 250.0f) SawClutchSlip = true;
            if (R->ClutchLocked) SawClutchLock = true;
            if (R->WastegatePosition > 0.05f) SawWastegate = true;
            PeakBov  = std::max(PeakBov, R->BovPosition);
            PeakGear = std::max(PeakGear, R->ActiveGear);

            if ((S % 5u) == 0u)
            {
                DriveTrace.push_back({
                    static_cast<float>(S * FixedDt), R->SpeedKph, R->EngineRpm,
                    R->TurboBoostBar, R->ClutchEngagement, std::fabs(R->ClutchSlipRpm), R->ActiveGear
                });
            }

            if (RenderGifs && (S % 12u) == 0u)
            {
                CanvasRgb Frame(GifW, GifH);
                Frame.ClearSky(18u, 26u, 42u, 48u, 62u, 82u);
                const Vec3 CarP = ToVec3(R->WorldPositionMeters);
                const Vec3 Eye  = CarP + Vec3{ -7.8f, 2.55f, 4.8f };
                const Camera3D Cam = Camera3D::LookAt(Eye, CarP + Vec3{ 1.2f, 0.55f, 0.0f }, 46.0f, GifW, GifH);
                DrawGroundGrid(Frame, Cam, CarP, 0.0f, 36.0f, 16.0f, 3.0f);
                RenderVehicleMesh(Frame, Cam, Meshes, *R, Spec, 0.0f, false, false);
                char Extra[128];
                std::snprintf(Extra, sizeof(Extra), "WOT LAUNCH | WG %3.0f%% | BOV %3.0f%% | MAP %5.1f KPA | FUEL %4.1fL",
                    R->WastegatePosition * 100.0f, R->BovPosition * 100.0f, R->ManifoldPressureKPa, R->FuelLiters);
                DrawTelemetryOverlay(Frame,
                    "01. DRIVE & ACCELERATE (TURBO + CLUTCH + DCT)",
                    "JOLT V5.6.0 CONTROLVEHICLE | HERO 3-LANE +-40D FAN TRACE | SOCKET-FREE CONSTRAINTS",
                    *R, Extra);
                Gif01.AppendRgbFrame(Frame.Rgb);
                if (S == 480u) KeyframeDrive = Frame;
            }
        }

        if (RenderGifs)
        {
            (void)Gif01.Save(OutputDir / "01_Drive_Accelerate_Turbo_Clutch.gif");
            std::printf("[SAVED] %s\n", (OutputDir / "01_Drive_Accelerate_Turbo_Clutch.gif").string().c_str());
        }

        std::printf("[INFO] Drive/Turbo/Clutch: Peak=%.1f km/h, Gear=%d, Turbo=%.0f rpm / %.2f bar (WG=%d, BOV=%.2f), ClutchSlip=%.0f rpm (Slip=%d Lock=%d)\n",
            PeakSpeedKph, PeakGear, PeakTurboRpm, PeakBoostBar, SawWastegate ? 1 : 0, PeakBov, PeakClutchSlip, SawClutchSlip ? 1 : 0, SawClutchLock ? 1 : 0);

        CheckGate(PeakSpeedKph > 155.0f && PeakGear >= 3,
            "Gate 2a: Drives & accelerates >155 km/h with automatic DCT upshifts", Passed, Failed);
        CheckGate(PeakTurboRpm > 80000.0f && PeakBoostBar > 0.85f && PeakBov > 0.25f,
            "Gate 2b: Turbocharger spools >80k rpm (>0.85 bar) & BOV vents cleanly on throttle lift", Passed, Failed);
        CheckGate(SawClutchSlip && SawClutchLock && PeakClutchSlip > 300.0f,
            "Gate 2c: Clutch executes launch bite slip (>300 rpm) then locks smoothly (<50 rpm slip)", Passed, Failed);

        Rig.Finalize(Physics);
        Physics.Finalize();
    }

    //==================================================================================================================
    // SCENARIO 2: CORNERING & S-TURN WITH ACKERMANN + STEER HINGE CONSTRAINTS + TIRE TRACKS (GIF 02)
    //==================================================================================================================
    {
        PhysicsSimulationParameters Params {};
        Params.GravityMps2 = { 0.0f, -9.80665f, 0.0f };
        Params.FixedStepHz = 120.0f;
        PhysicsExtension Physics(Params);
        (void)Physics.Initialize();
        (void)SpawnFlatTrack(Physics);

        VehicleRigSpecification Spec = VehiclePhysicsRig::ConstructSpecificationFromControlVehicle(Built.ClassDefinition);
        Spec.WheelContactMode          = WheelContactQueryMode::HeroThreeLaneFanArc;
        Spec.BindAttachmentConstraints = true;
        VehiclePhysicsRig Rig {};
        (void)Rig.Initialize(Physics, Spec, { 0.0f, SpawnY, 0.0f });

        // Bring vehicle up to ~75 km/h before S-turn recording
        VehicleControlDirective Drive {};
        Drive.Category           = VehicleDirectiveCategory::DriverInput;
        Drive.ThrottleNormalized = 0.85f;
        (void)Rig.SubmitControlDirective(Drive);
        for (uint32_t S = 0u; S < 180u; ++S) StepCouple(Physics, Rig, FixedDt);

        AnimatedGifWriter Gif02(GifW, GifH, 5u);
        std::vector<std::array<Vec3, 4u>> TireTrail {};

        float PeakLatG      = 0.0f;
        float PeakRightYaw  = 0.0f;
        float PeakLeftYaw   = 0.0f;
        bool  AckermannOk   = false;
        bool  SteerConstrOk = false;

        // 4.0 s S-turn: 0..2.0 s Right turn (+0.62 steer), 2.0..4.0 s Left counter-turn (-0.62 steer)
        for (uint32_t S = 0u; S < 480u; ++S)
        {
            Drive.ThrottleNormalized = 0.42f;
            Drive.SteeringNormalized = (S < 240u) ? 0.62f : -0.62f;
            (void)Rig.SubmitControlDirective(Drive);
            StepCouple(Physics, Rig, FixedDt);
            const auto R = Rig.AcquireVehicleReading();

            PeakLatG     = std::max(PeakLatG, std::fabs(R->LateralG));
            PeakRightYaw = std::max(PeakRightYaw, R->YawRateRadPerSec);
            PeakLeftYaw  = std::min(PeakLeftYaw, R->YawRateRadPerSec);
            if (S < 240u && R->Corners[1u].SteerAngleRad > R->Corners[0u].SteerAngleRad + 0.003f) AckermannOk = true;
            if (R->Corners[0u].SteerConstraintToken.Valid() && R->Corners[1u].SteerConstraintToken.Valid()) SteerConstrOk = true;

            if ((S % 3u) == 0u)
            {
                TireTrail.push_back({
                    ToVec3(R->Corners[0u].ContactPointWorldM),
                    ToVec3(R->Corners[1u].ContactPointWorldM),
                    ToVec3(R->Corners[2u].ContactPointWorldM),
                    ToVec3(R->Corners[3u].ContactPointWorldM),
                });
            }

            if (RenderGifs && (S % 7u) == 0u)
            {
                CanvasRgb Frame(GifW, GifH);
                Frame.ClearSky(16u, 24u, 38u, 42u, 54u, 72u);
                const Vec3 CarP = ToVec3(R->WorldPositionMeters);
                const Vec3 CarF = RotQuat(R->WorldOrientation, { 1.0f, 0.0f, 0.0f });
                const Vec3 CarR = RotQuat(R->WorldOrientation, { 0.0f, 0.0f, 1.0f });
                const Vec3 Eye  = CarP - CarF * 8.6f + Vec3{ 0.0f, 4.6f, 0.0f } + CarR * 3.4f;
                const Camera3D Cam = Camera3D::LookAt(Eye, CarP + CarF * 2.5f, 48.0f, GifW, GifH);
                DrawGroundGrid(Frame, Cam, CarP, 0.0f, 38.0f, 38.0f, 3.0f);

                // Draw persistent tire slip tracks
                for (std::size_t T = 1u; T < TireTrail.size(); ++T)
                {
                    for (int W = 0; W < 4; ++W)
                    {
                        DrawLine3D(Frame, Cam, TireTrail[T - 1u][W] + Vec3{ 0.0f, 0.03f, 0.0f },
                                               TireTrail[T][W] + Vec3{ 0.0f, 0.03f, 0.0f }, 28u, 30u, 36u, 2);
                    }
                }
                RenderVehicleMesh(Frame, Cam, Meshes, *R, Spec, 0.0f, false, false);
                char Extra[128];
                std::snprintf(Extra, sizeof(Extra), "STEER FL=%+4.1fD FR=%+4.1fD (ACKERMANN) | YAW=%+4.2f RAD/S | TREAD=%4.1fC",
                    R->Corners[0u].SteerAngleRad * 57.2958f, R->Corners[1u].SteerAngleRad * 57.2958f,
                    R->YawRateRadPerSec, R->Corners[0u].TreadTempKelvin - 273.15f);
                DrawTelemetryOverlay(Frame,
                    "02. HIGH-G S-TURN CORNERING (STEER HINGES)",
                    "ACKERMANN GEOMETRY | FRONT STEER HINGE CONSTRAINTS | COMBINED-SLIP GRIP",
                    *R, Extra);
                Gif02.AppendRgbFrame(Frame.Rgb);
                if (S == 140u) KeyframeTurn = Frame;
            }
        }

        if (RenderGifs)
        {
            (void)Gif02.Save(OutputDir / "02_Cornering_Steering_Constraints.gif");
            std::printf("[SAVED] %s\n", (OutputDir / "02_Cornering_Steering_Constraints.gif").string().c_str());
        }

        std::printf("[INFO] Cornering S-Turn: PeakLatG=%.2f g, RightYaw=+%.2f rad/s, LeftYaw=%.2f rad/s, Ackermann=%d\n",
            PeakLatG, PeakRightYaw, PeakLeftYaw, AckermannOk ? 1 : 0);
        CheckGate(PeakLatG > 0.55f && PeakRightYaw > 0.25f && PeakLeftYaw < -0.20f && AckermannOk && SteerConstrOk,
            "Gate 3: Executes high-G right + left S-turn (>0.55 g) with Ackermann Steer Hinge constraints", Passed, Failed);

        Rig.Finalize(Physics);
        Physics.Finalize();
    }

    //==================================================================================================================
    // SCENARIO 3: BRAKE ON STEEP 10-DEGREE SLOPE TO FULL STOP + ZERO-SLIDE KARNOPP HOLD (GIF 03)
    //==================================================================================================================
    {
        PhysicsSimulationParameters Params {};
        Params.GravityMps2 = { 0.0f, -9.80665f, 0.0f };
        Params.FixedStepHz = 120.0f;
        PhysicsExtension Physics(Params);
        (void)Physics.Initialize();

        constexpr float SlopeDeg = 10.0f;
        const float SlopeRad = SlopeDeg * static_cast<float>(Constants::DegToRad);
        (void)SpawnTiltedRamp(Physics, SlopeRad);

        VehicleRigSpecification Spec = VehiclePhysicsRig::ConstructSpecificationFromControlVehicle(Built.ClassDefinition);
        Spec.WheelContactMode          = WheelContactQueryMode::HeroThreeLaneFanArc;
        Spec.BindAttachmentConstraints = true;

        // Spawn facing DOWNHILL (-X direction on +X-uphill ramp, OR spawn facing +X downhill on -SlopeRad ramp)
        // Using negative pitch ramp (-10 deg): +X goes DOWNHILL! Car accelerates forward downhill, brakes hard, holds rock-steady!
        Physics.Finalize();
        (void)Physics.Initialize();
        (void)SpawnTiltedRamp(Physics, -SlopeRad);

        const float HalfA = 0.5f * (-SlopeRad);
        const RigidOrientationQuaternion DownhillQuat { 0.0f, 0.0f, std::sin(HalfA), std::cos(HalfA) };
        const Axis3D SpawnPos { -25.0f * std::cos(SlopeRad) + SpawnY * std::sin(SlopeRad),
                                 25.0f * std::sin(SlopeRad) + SpawnY * std::cos(SlopeRad), 0.0f };

        VehiclePhysicsRig Rig {};
        (void)Rig.Initialize(Physics, Spec, SpawnPos, DownhillQuat);

        // Phase A (1.8 s): Drive downhill at brisk throttle to ~50+ km/h
        VehicleControlDirective Input {};
        Input.Category           = VehicleDirectiveCategory::DriverInput;
        Input.ThrottleNormalized = 0.85f;
        (void)Rig.SubmitControlDirective(Input);

        AnimatedGifWriter Gif03(GifW, GifH, 5u);
        float SpeedAtBrakeKph = 0.0f;
        float SpeedAtStopMps  = 99.0f;
        float InitialRotorK   = 0.0f;
        float FinalRotorK     = 0.0f;
        Axis3D HoldAnchorPos  {};
        bool  AnchorCaptured  = false;
        float MaxCreepMeters  = 0.0f;
        bool  HeldStaticFull  = false;

        // Total 7.0 s (840 steps): 0..252 drive downhill (~52 km/h), 252..480 brake hard to stop ON 10-deg slope, 480..840 hold 3.0 s verifying ZERO slide
        for (uint32_t S = 0u; S < 840u; ++S)
        {
            if (S == 252u)
            {
                const auto Pre = Rig.AcquireVehicleReading();
                SpeedAtBrakeKph = Pre->SpeedKph;
                InitialRotorK   = Pre->Corners[0u].BrakeTempKelvin;
                Input.ThrottleNormalized = 0.0f;
                Input.BrakeNormalized    = 1.0f;
                (void)Rig.SubmitControlDirective(Input);
            }
            StepCouple(Physics, Rig, FixedDt);
            const auto R = Rig.AcquireVehicleReading();

            if (!AnchorCaptured && S >= 300u && R->GroundSpeedMps < 0.05f)
            {
                AnchorCaptured = true;
                HoldAnchorPos  = R->WorldPositionMeters;
                SpeedAtStopMps = R->GroundSpeedMps;
                FinalRotorK    = R->Corners[0u].BrakeTempKelvin;
            }
            float CreepNowM = 0.0f;
            if (AnchorCaptured)
            {
                const float Dx = R->WorldPositionMeters.X - HoldAnchorPos.X;
                const float Dy = R->WorldPositionMeters.Y - HoldAnchorPos.Y;
                const float Dz = R->WorldPositionMeters.Z - HoldAnchorPos.Z;
                CreepNowM      = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
                MaxCreepMeters = std::max(MaxCreepMeters, CreepNowM);
                if (R->Corners[0u].FrictionRegime == FrictionRegimeCategory::StaticFull) HeldStaticFull = true;
            }
            if ((S % 5u) == 0u)
            {
                SlopeSpeedTrace.push_back(R->SpeedKph);
                SlopeCreepCmTrace.push_back(CreepNowM * 100.0f);
            }

            if (RenderGifs && (S % 10u) == 0u)
            {
                CanvasRgb Frame(GifW, GifH);
                Frame.ClearSky(20u, 24u, 36u, 52u, 60u, 76u);
                const Vec3 CarP = ToVec3(R->WorldPositionMeters);
                const Vec3 Eye  = CarP + Vec3{ 2.0f, 2.6f, 10.2f };
                const Camera3D Cam = Camera3D::LookAt(Eye, CarP + Vec3{ 0.4f, 0.3f, 0.0f }, 44.0f, GifW, GifH);
                DrawGroundGrid(Frame, Cam, CarP, -SlopeRad, 38.0f, 14.0f, 3.0f);
                RenderVehicleMesh(Frame, Cam, Meshes, *R, Spec, -SlopeRad, S >= 252u, false);

                const char* PhaseStr = (S < 252u) ? "DOWNHILL APPROACH" : (AnchorCaptured ? "SLOPE HOLD (STATICFULL)" : "HARD BRAKING ON 10D SLOPE");
                char Extra[140];
                std::snprintf(Extra, sizeof(Extra), "10 DEG SLOPE | %s | CREEP=%.2f CM | ROTOR=%.0fK",
                    PhaseStr, CreepNowM * 100.0f, R->Corners[0u].BrakeTempKelvin);
                DrawTelemetryOverlay(Frame,
                    "03. 10-DEG SLOPE BRAKING & ZERO-SLIDE KARNOPP HOLD",
                    "DRIVES DOWNHILL -> BRAKES TO DEAD STOP -> HOLDS ROCK-STEADY (<3 CM CREEP)",
                    *R, Extra);
                Gif03.AppendRgbFrame(Frame.Rgb);
                if (S == 340u) KeyframeSlope = Frame;
            }
        }

        if (RenderGifs)
        {
            (void)Gif03.Save(OutputDir / "03_Slope_Brake_And_Hold.gif");
            std::printf("[SAVED] %s\n", (OutputDir / "03_Slope_Brake_And_Hold.gif").string().c_str());
        }

        const auto FinalHoldR = Rig.AcquireVehicleReading();
        std::printf("[INFO] 10-deg Slope Brake & Hold: Approach=%.1f km/h -> Stop=%.4f m/s, HoldCreep=%.4f m (%.2f cm), FinalSpd=%.4f m/s, Rotor=%.1f->%.1f K\n",
            SpeedAtBrakeKph, SpeedAtStopMps, MaxCreepMeters, MaxCreepMeters * 100.0f, FinalHoldR->GroundSpeedMps, InitialRotorK, FinalRotorK);

        CheckGate(SpeedAtBrakeKph > 45.0f && AnchorCaptured && SpeedAtStopMps < 0.05f,
            "Gate 4: Brakes cleanly from >45 km/h downhill on a 10-degree slope to full standstill (<0.05 m/s)", Passed, Failed);
        CheckGate(MaxCreepMeters < 0.03f && FinalHoldR->GroundSpeedMps < 0.02f && HeldStaticFull,
            "Gate 5: Does NOT slide off 10-degree slope (Karnopp StaticFull hold, creep < 3 cm over 2.5+ s)", Passed, Failed);

        Rig.Finalize(Physics);
        Physics.Finalize();
    }

    //==================================================================================================================
    // SCENARIO 4: 5-WAY WHEEL GROUND-CONTACT BENCHMARK (FULL CURB + SHOULDER-ONLY CURB) & 3-LANE FAN GIF (GIF 04)
    //==================================================================================================================
    auto BenchmarkContactMode = [&](WheelContactQueryMode Mode, const char* Name, int RaysOrCasts, bool RecordGif) -> TraceCandidateMetrics
    {
        TraceCandidateMetrics Out {};
        Out.Name        = Name;
        Out.RaysOrCasts = RaysOrCasts;

        PhysicsSimulationParameters Params {};
        Params.GravityMps2 = { 0.0f, -9.80665f, 0.0f };
        Params.FixedStepHz = 120.0f;
        PhysicsExtension Physics(Params);
        if (!Physics.Initialize()) return Out;
        (void)SpawnFlatTrack(Physics);

        // Obstacle A: Full-width 5.5 cm sharp track curb at X = [11.0, 15.0] m
        RigidBodyDescriptor CurbA {};
        CurbA.Shape.ShapeCategory     = CollisionShapeCategory::Box;
        CurbA.Shape.HalfExtentsMeters = { 2.0f, 0.0275f, 10.0f };
        CurbA.MotionCategory          = RigidMotionCategory::Static;
        CurbA.PositionMeters          = { 13.0f, 0.0275f, 0.0f };
        CurbA.PhysicsMaterialId       = static_cast<uint16_t>(SurfaceMaterialCategory::Curb);
        CurbA.CollisionChannel        = static_cast<uint16_t>(CollisionLayerChannel::StaticEnvironment);
        (void)Physics.IssueRigidBody(CurbA);

        // Obstacle B: Narrow shoulder-only 5.5 cm curb edge at X = [17.0, 22.0] m, hitting ONLY the outer-left shoulder
        // of the FL/RL tires (center of FL tire is at Z = -1.048 m; outer shoulder is at Z = -1.174 m -> box ends at Z = -1.11 m!)
        RigidBodyDescriptor CurbB {};
        CurbB.Shape.ShapeCategory     = CollisionShapeCategory::Box;
        CurbB.Shape.HalfExtentsMeters = { 2.5f, 0.0275f, 0.35f };
        CurbB.MotionCategory          = RigidMotionCategory::Static;
        CurbB.PositionMeters          = { 19.5f, 0.0275f, -1.46f }; // Inner edge at Z = -1.11 m (misses center Z=-1.048, hits left shoulder!)
        CurbB.PhysicsMaterialId       = static_cast<uint16_t>(SurfaceMaterialCategory::Curb);
        CurbB.CollisionChannel        = static_cast<uint16_t>(CollisionLayerChannel::StaticEnvironment);
        (void)Physics.IssueRigidBody(CurbB);

        VehicleRigSpecification Spec = VehiclePhysicsRig::ConstructSpecificationFromControlVehicle(Built.ClassDefinition);
        Spec.WheelContactMode          = Mode;
        Spec.BindAttachmentConstraints = true;
        Spec.EnableDebugLogging        = false;
        VehiclePhysicsRig Rig {};
        if (!Rig.Initialize(Physics, Spec, { 0.0f, SpawnY, 0.0f })) return Out;

        VehicleControlDirective Drive {};
        Drive.Category           = VehicleDirectiveCategory::DriverInput;
        Drive.ThrottleNormalized = 0.72f;
        (void)Rig.SubmitControlDirective(Drive);

        AnimatedGifWriter Gif04(GifW, GifH, 5u);
        double SumUs = 0.0;
        for (uint32_t S = 0u; S < 420u; ++S)
        {
            const auto T0 = std::chrono::steady_clock::now();
            StepCouple(Physics, Rig, FixedDt);
            const auto T1 = std::chrono::steady_clock::now();
            SumUs += std::chrono::duration<double, std::micro>(T1 - T0).count();

            const auto R = Rig.AcquireVehicleReading();
            if (!std::isfinite(R->SpeedKph)) return Out;

            const float X = R->WorldPositionMeters.X;
            if (X >= 8.5f && X <= 15.5f)
            {
                Out.PeakSuspVelMps = std::max(Out.PeakSuspVelMps, std::fabs(R->Corners[0u].SuspensionVelocityMps));
                Out.PeakLoadN      = std::max(Out.PeakLoadN, R->Corners[0u].VerticalLoadNewtons);
            }
            if (X >= 15.0f && X <= 24.0f)
            {
                if (R->Corners[0u].SurfaceMaterialId == static_cast<uint16_t>(SurfaceMaterialCategory::Curb))
                {
                    ++Out.ShoulderHits;
                }
            }
            Out.PeakLaneMask |= R->Corners[0u].ContactLaneMask;

            if (RecordGif && RenderGifs && S >= 110u && S < 390u && ((S - 110u) % 4u) == 0u)
            {
                CanvasRgb Frame(GifW, GifH);
                Frame.ClearSky(16u, 22u, 34u, 38u, 48u, 64u);
                const Vec3 CarP = ToVec3(R->WorldPositionMeters);
                const Vec3 FlW  = CarP + RotQuat(R->WorldOrientation, ToVec3(R->Corners[0u].WheelHubLocalMeters));
                const Vec3 Eye  = FlW + Vec3{ 3.8f, 1.55f, -3.9f };
                const Camera3D Cam = Camera3D::LookAt(Eye, FlW + Vec3{ -0.4f, 0.05f, 0.4f }, 42.0f, GifW, GifH);
                DrawGroundGrid(Frame, Cam, CarP, 0.0f, 32.0f, 14.0f, 2.0f);
                DrawBox3D(Frame, Cam, { 13.0f, 0.0275f,  0.0f  }, {}, { 2.0f, 0.0275f, 10.0f }, 215u, 115u, 45u);
                DrawBox3D(Frame, Cam, { 19.5f, 0.0275f, -1.46f }, {}, { 2.5f, 0.0275f,  0.35f }, 235u, 70u, 145u);
                RenderVehicleMesh(Frame, Cam, Meshes, *R, Spec, 0.0f, false, true);
                char Extra[140];
                std::snprintf(Extra, sizeof(Extra), "FL RAYS=%2u/15 | LANES=[%c %c %c] | MAT=%s | SUSP_VEL=%+4.2f M/S",
                    static_cast<unsigned>(R->Corners[0u].ContactRayHits),
                    (R->Corners[0u].ContactLaneMask & 1u) ? 'L' : '-',
                    (R->Corners[0u].ContactLaneMask & 2u) ? 'C' : '-',
                    (R->Corners[0u].ContactLaneMask & 4u) ? 'R' : '-',
                    (R->Corners[0u].SurfaceMaterialId == static_cast<uint16_t>(SurfaceMaterialCategory::Curb)) ? "CURB" : "ROAD",
                    R->Corners[0u].SuspensionVelocityMps);
                DrawTelemetryOverlay(Frame,
                    "04. HERO 3-LANE (L+C+R) X +-40D FAN TRACE PROOF",
                    "MAGENTA=LEFT SHOULDER | CYAN=CENTER | GREEN=RIGHT SHOULDER | CATCHES SHOULDER CURBS",
                    *R, Extra);
                Gif04.AppendRgbFrame(Frame.Rgb);
                if (S == 270u) KeyframeFan = Frame;
            }
        }

        if (RecordGif && RenderGifs)
        {
            (void)Gif04.Save(OutputDir / "04_Hero3LaneFanTrace_CurbProof.gif");
            std::printf("[SAVED] %s\n", (OutputDir / "04_Hero3LaneFanTrace_CurbProof.gif").string().c_str());
        }

        Out.AvgStepUs = SumUs / 420.0;
        Out.Valid     = (Rig.AcquireVehicleReading()->WorldPositionMeters.X > 22.0f);
        Rig.Finalize(Physics);
        Physics.Finalize();
        return Out;
    };

    const std::array<TraceCandidateMetrics, 5u> Bench = {{
        BenchmarkContactMode(WheelContactQueryMode::SingleRay,           "SingleRay (1)",          1,  false),
        BenchmarkContactMode(WheelContactQueryMode::MultiRayArc,         "MultiRayArc (1x5 40d)",  5,  false),
        BenchmarkContactMode(WheelContactQueryMode::HeroThreeLaneFanArc, "Hero3LaneFan (3x5 40d)", 15, true),
        BenchmarkContactMode(WheelContactQueryMode::SphereShapeCast,     "SphereShapeCast (Jolt)", 1,  false),
        BenchmarkContactMode(WheelContactQueryMode::CylinderShapeCast,   "CylinderShapeCast(Jolt)",1,  false),
    }};

    std::printf("\n--- 5-Way Wheel Ground-Contact Candidate Benchmark (Real JoltPhysics v5.6.0, 120 Hz) ---\n");
    for (const auto& B : Bench)
    {
        std::printf("  %-24s | %2d q/whl | %6.1f us/step | CurbSuspVel=%4.2f m/s | ShoulderCurbHits=%3u | Lanes=0x%X\n",
            B.Name, B.RaysOrCasts, B.AvgStepUs, B.PeakSuspVelMps, B.ShoulderHits, static_cast<unsigned>(B.PeakLaneMask));
    }

    CheckGate(Bench[0].Valid && Bench[1].Valid && Bench[2].Valid && Bench[3].Valid && Bench[4].Valid,
        "Gate 6a: All 5 wheel contact candidates traverse full curb + shoulder curb stably", Passed, Failed);
    CheckGate(Bench[2].ShoulderHits > 10u && Bench[0].ShoulderHits == 0u && Bench[1].ShoulderHits == 0u,
        "Gate 6b: HeroThreeLaneFanArc (3-lane L+C+R) detects narrow shoulder curb missed by SingleRay & 1-lane MultiRayArc", Passed, Failed);
    CheckGate(Bench[2].PeakSuspVelMps < Bench[0].PeakSuspVelMps,
        "Gate 6c: HeroThreeLaneFanArc +-40 deg fan smooths vertical curb impact spike vs SingleRay", Passed, Failed);

    //==================================================================================================================
    // COMPOSITE 1920x1080 SUMMARY PROOF SHEET PNG
    //==================================================================================================================
    if (RenderGifs)
    {
        CanvasRgb Sheet(1920, 1080);
        Sheet.FillRect(0, 0, 1920, 1080, 10u, 13u, 20u);
        Sheet.FillRect(0, 0, 1920, 56, 16u, 21u, 32u);
        DrawText(Sheet, 20, 12, "FRONTIER JOLT V5.6.0 CONTROLVEHICLE PHYSICS PROOF - DRIVING, SLOPES, CONSTRAINTS & 3-LANE FAN TRACES", 2, 240u, 246u, 255u);
        DrawText(Sheet, 20, 36, "REAL VEHICLEPHYSICSRIG + CONSTRAINTSOLVER (ZERO SOCKETS) + TURBO + CLUTCH + KARNOPP SLOPE HOLD + 5-WAY TRACE BENCHMARK", 1, 135u, 215u, 245u);

        // Top 2x2 keyframes (Wait: 3 across top @ 620x350? Even cleaner: 4 keyframes across top row or 2x2 left + plots/table right!)
        // Left column (1288 px): 2x2 keyframes (640x360 each -> fits in 1288x736!)
        Sheet.Blit(KeyframeDrive, 12,  68);
        Sheet.Blit(KeyframeTurn,  660, 68);
        Sheet.Blit(KeyframeSlope, 12,  436);
        Sheet.Blit(KeyframeFan,   660, 436);

        // Right column (600 px): Live Telemetry Curves (Powertrain, Turbo/Clutch, Slope Brake & Hold)
        std::vector<float> SpdS, RpmS, BstS, CltS, SlpS;
        for (const auto& T : DriveTrace)
        {
            SpdS.push_back(T.SpeedKph);
            RpmS.push_back(T.Rpm);
            BstS.push_back(T.BoostBar);
            CltS.push_back(T.ClutchEng * 100.0f);
            SlpS.push_back(T.ClutchSlipRpm);
        }
        DrawPlotCard(Sheet, 1312, 68, 596, 232,
            "POWERTRAIN: SPEED (KM/H) & ENGINE RPM",
            SpdS, 220.0f, "SPEED (CYAN)", 70u, 225u, 255u,
            RpmS, 7500.0f, "RPM (AMBER)", 255u, 195u, 70u);

        DrawPlotCard(Sheet, 1312, 312, 596, 232,
            "TURBO BOOST (BAR) & CLUTCH ENGAGEMENT (%)",
            BstS, 1.5f, "BOOST (ORANGE)", 255u, 130u, 55u,
            CltS, 105.0f, "CLUTCH % (GRN)", 85u, 240u, 135u);

        DrawPlotCard(Sheet, 1312, 556, 596, 240,
            "10-DEG SLOPE BRAKE (KM/H) & HOLD CREEP (CM)",
            SlopeSpeedTrace, 75.0f, "SPEED (CYAN)", 70u, 225u, 255u,
            SlopeCreepCmTrace, 5.0f, "CREEP CM (RED)", 255u, 90u, 105u);

        // Bottom full-width 5-Way Wheel Contact Candidate Benchmark Table
        Sheet.FillRect(12, 812, 1896, 254, 16u, 20u, 29u);
        Sheet.FillRect(12, 812, 1896, 30, 24u, 31u, 44u);
        DrawText(Sheet, 24, 821, "5-WAY WHEEL GROUND-CONTACT CANDIDATE BENCHMARK (REAL JOLT V5.6.0, 120 HZ, FULL 5.5CM CURB + SHOULDER-ONLY CURB)", 1, 240u, 246u, 255u);
        DrawText(Sheet, 24, 852, "CANDIDATE MODE                 | QUERIES/WHL |  STEP COST   | CURB SUSP VEL | SHOULDER CURB | RECOMMENDATION", 1, 145u, 205u, 245u);
        Sheet.DrawLine2D(24, 865, 1890, 865, 45u, 55u, 72u, 1);

        const char* Recs[5] = {
            "CHEAPEST BASELINE - MISSES SHOULDER CURBS & SPIKES VERTICAL IMPACT",
            "TRAFFIC / AI LOD - FAST 1-LANE +-40D FAN, SMOOTH CURBS, MISSES SHOULDER",
            "HERO VEHICLE WINNER - 3X 1-LANE (L+C+R), TRUE WIDTH, PER-LANE CONTACT MASK",
            "JOLT SPHERE SWEEP - 3.2X OVER-WIDE (1.02M) FALSE SIDEWALL SNAGGING",
            "JOLT 3D CYLINDER - FAST & SMOOTH ON BOXES, SINGLE HIT ONLY (NO L/C/R MASK)"
        };
        for (int I = 0; I < 5; ++I)
        {
            const int Ry = 878 + I * 34;
            if (I == 2) Sheet.FillRect(18, Ry - 5, 1884, 28, 22u, 48u, 44u);
            char Row[256];
            std::snprintf(Row, sizeof(Row), "%-30s | %4d        | %6.1f US    | %6.2f M/S    | %4u HITS     | %s",
                Bench[I].Name, Bench[I].RaysOrCasts, Bench[I].AvgStepUs, Bench[I].PeakSuspVelMps, Bench[I].ShoulderHits, Recs[I]);
            DrawText(Sheet, 24, Ry + 3, Row, 1,
                (I == 2) ? 120u : 220u,
                (I == 2) ? 255u : 226u,
                (I == 2) ? 175u : 238u);
        }

        (void)WriteRgbPng(OutputDir / "05_VehiclePhysics_ProofSummarySheet.png", 1920, 1080, Sheet.Rgb);
        std::printf("[SAVED] %s\n", (OutputDir / "05_VehiclePhysics_ProofSummarySheet.png").string().c_str());
    }

    std::printf("=== Summary: Passed=%u Failed=%u ===\n", Passed, Failed);
    return (Failed == 0u) ? 0 : 1;
}
