// The captures, written with their range intact.
//
// The sRGB PNGs beside this are for looking at quickly, and they throw away most of what a capture
// from an HDR game contains: the frame is divided by paper white, saturated at 1, and squashed into
// eight bits. Everything above paper white -- a sky, a specular hit, a light source, which is exactly
// where a colour argument tends to live -- comes out as flat white in all four images at once, so the
// comparison the capture exists for cannot be made there at all.
//
// Two formats, because they are for different things and neither replaces the other:
//
//   OpenEXR, half float, uncompressed. The frame's own scene-linear values, unmodified: not divided
//   by paper white, not curved, not clipped. This is the file to measure from, and the only one that
//   is exactly what the GPU held. Read by Photoshop, Affinity, GIMP, Krita, DaVinci, ImageMagick and
//   every VFX tool; NOT by Windows Photos.
//
//   PNG, sixteen bits a channel, PQ encoded and marked as HDR10 with a cICP chunk. This is the file
//   to LOOK at: Windows Photos, Chrome and Edge read cICP and put it on the screen as real HDR on an
//   HDR display. It is a display-referred picture rather than the frame's own numbers -- absolute,
//   with paper white placed at the 203 nits BT.2408 specifies for it -- so measure from the EXR and
//   look at this. PQ stops at 10000 nits, which is 49x paper white, so this clips where a real frame
//   goes past it (the sun, a muzzle flash) and the EXR does not.
//
// JPEG XL is not here. A conformant encoder needs entropy coding, Brotli-compressed metadata and a
// modular-mode bitstream -- it is libjxl, plus brotli and highway, and this project builds with
// MSBuild and vendors nothing of that shape. The two formats above carry the same range losslessly,
// so nothing is given up but the container.
//
// Primaries are taken as BT.709 throughout, which is the assumption the composition shader already
// makes -- its luminance weights are BT.709's -- so this is consistent with the pass rather than a
// second guess about the game.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#include "DlssNr_Png.h"

namespace dlssnr_hdr
{
// The inverse of the reader beside this. Round to nearest even, and the whole finite float range maps:
// anything past half's maximum saturates rather than becoming an infinity a viewer would render as a
// hole in the picture.
inline uint16_t FloatToHalf(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);

    const uint16_t sign = (uint16_t) ((bits >> 16) & 0x8000u);
    const int32_t exponent = (int32_t) ((bits >> 23) & 0xFFu) - 127;
    uint32_t mantissa = bits & 0x7FFFFFu;

    if (exponent == 128) // infinity or NaN
        return (uint16_t) (sign | 0x7C00u | (mantissa != 0 ? 0x200u : 0u));

    if (exponent > 15) // past half's range
        return (uint16_t) (sign | 0x7BFFu);

    if (exponent < -24) // rounds to zero
        return sign;

    if (exponent < -14)
    {
        // Subnormal: shift the implicit one back in and let the rounding below carry into it.
        mantissa |= 0x800000u;
        const int32_t shift = -14 - exponent;
        const uint32_t half = mantissa >> (13 + shift);
        const uint32_t rest = mantissa & ((1u << (13 + shift)) - 1u);
        const uint32_t tie = 1u << (12 + shift);
        return (uint16_t) (sign | (half + ((rest > tie || (rest == tie && (half & 1))) ? 1u : 0u)));
    }

    const uint32_t half = ((uint32_t) (exponent + 15) << 10) | (mantissa >> 13);
    const uint32_t rest = mantissa & 0x1FFFu;

    return (uint16_t) (sign + half + ((rest > 0x1000u || (rest == 0x1000u && (half & 1))) ? 1u : 0u));
}

// ---------------------------------------------------------------------------------------------
// OpenEXR, scanline, NO_COMPRESSION, half. The frame's own values.
// ---------------------------------------------------------------------------------------------

inline void PushLE32(std::vector<uint8_t>& out, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        out.push_back((uint8_t) (v >> (8 * i)));
}

inline void PushLE64(std::vector<uint8_t>& out, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        out.push_back((uint8_t) (v >> (8 * i)));
}

inline void PushString(std::vector<uint8_t>& out, const char* s)
{
    while (*s != '\0')
        out.push_back((uint8_t) *s++);

    out.push_back(0);
}

inline void PushAttr(std::vector<uint8_t>& out, const char* name, const char* type,
                     const std::vector<uint8_t>& body)
{
    PushString(out, name);
    PushString(out, type);
    PushLE32(out, (uint32_t) body.size());
    out.insert(out.end(), body.begin(), body.end());
}

// rgb is tightly packed, three floats per pixel, top row first.
inline bool WriteExr(const std::filesystem::path& path, const float* rgb, unsigned int width,
                     unsigned int height)
{
    if (rgb == nullptr || width == 0 || height == 0)
        return false;

    std::vector<uint8_t> f;
    PushLE32(f, 0x01312F76); // magic
    PushLE32(f, 2);          // version 2, no flags: scanline, single part

    // Channels, which the format requires in alphabetical order -- so B, G, R, and the pixel data
    // below has to be written in that order too rather than in the order it is held.
    {
        std::vector<uint8_t> ch;

        for (const char* name : { "B", "G", "R" })
        {
            PushString(ch, name);
            PushLE32(ch, 1); // HALF
            ch.push_back(0); // pLinear
            ch.push_back(0);
            ch.push_back(0);
            ch.push_back(0);
            PushLE32(ch, 1); // x sampling
            PushLE32(ch, 1); // y sampling
        }

        ch.push_back(0); // the list terminator
        PushAttr(f, "channels", "chlist", ch);
    }

    {
        std::vector<uint8_t> c;
        c.push_back(0); // NO_COMPRESSION
        PushAttr(f, "compression", "compression", c);
    }

    {
        std::vector<uint8_t> box;
        PushLE32(box, 0);
        PushLE32(box, 0);
        PushLE32(box, width - 1);
        PushLE32(box, height - 1);
        PushAttr(f, "dataWindow", "box2i", box);
        PushAttr(f, "displayWindow", "box2i", box);
    }

    {
        std::vector<uint8_t> l;
        l.push_back(0); // INCREASING_Y
        PushAttr(f, "lineOrder", "lineOrder", l);
    }

    {
        std::vector<uint8_t> a;
        const float one = 1.0f;
        uint32_t bits;
        std::memcpy(&bits, &one, 4);
        PushLE32(a, bits);
        PushAttr(f, "pixelAspectRatio", "float", a);
        PushAttr(f, "screenWindowWidth", "float", a);
    }

    {
        std::vector<uint8_t> c;
        PushLE32(c, 0);
        PushLE32(c, 0);
        PushAttr(f, "screenWindowCenter", "v2f", c);
    }

    f.push_back(0); // end of the header

    // One scanline per block, so the offset table has an entry each. Every block is the same size,
    // which is what makes the table computable before the data is built.
    const uint64_t tableAt = f.size();
    const uint64_t blockBytes = 8 + (uint64_t) width * 3 * 2; // y, size, then B G R as halves
    const uint64_t dataAt = tableAt + (uint64_t) height * 8;

    for (unsigned int y = 0; y < height; ++y)
        PushLE64(f, dataAt + (uint64_t) y * blockBytes);

    for (unsigned int y = 0; y < height; ++y)
    {
        PushLE32(f, y);
        PushLE32(f, (uint32_t) (width * 3 * 2));

        for (int c = 2; c >= 0; --c) // B, G, R
        {
            for (unsigned int x = 0; x < width; ++x)
            {
                const uint16_t h = FloatToHalf(rgb[((size_t) y * width + x) * 3 + c]);
                f.push_back((uint8_t) (h & 0xFF));
                f.push_back((uint8_t) (h >> 8));
            }
        }
    }

    std::FILE* out = _wfopen(path.wstring().c_str(), L"wb");

    if (out == nullptr)
        return false;

    const bool ok = std::fwrite(f.data(), 1, f.size(), out) == f.size();
    std::fclose(out);
    return ok;
}

// ---------------------------------------------------------------------------------------------
// PNG, sixteen bits a channel, PQ, marked HDR10.
// ---------------------------------------------------------------------------------------------

// BT.2408's reference: diffuse white in an HDR picture sits at 203 nits. The captures arrive scaled so
// that 1.0 is the paper white the pass itself used, which is the same idea in relative units, so this
// is what turns one into the other.
constexpr float kPaperWhiteNits = 203.0f;

// SMPTE ST 2084, taking absolute luminance in nits and returning the code value.
inline float PqFromNits(float nits)
{
    const float y = std::min(std::max(nits, 0.0f), 10000.0f) / 10000.0f;
    const float m1 = 0.1593017578125f;
    const float m2 = 78.84375f;
    const float c1 = 0.8359375f;
    const float c2 = 18.8515625f;
    const float c3 = 18.6875f;
    const float p = std::pow(y, m1);

    return std::pow((c1 + c2 * p) / (1.0f + c3 * p), m2);
}

// BT.709 primaries to BT.2020, which is what the cICP below says the file is in. The pass works in
// BT.709 -- its luminance weights are BT.709's -- so this is the conversion that makes the marking
// true rather than a label on unconverted numbers.
inline void Bt709ToBt2020(const float in[3], float out[3])
{
    out[0] = 0.6274039f * in[0] + 0.3292830f * in[1] + 0.0433131f * in[2];
    out[1] = 0.0690973f * in[0] + 0.9195404f * in[1] + 0.0113623f * in[2];
    out[2] = 0.0163914f * in[0] + 0.0880132f * in[1] + 0.8955953f * in[2];
}

// rgb is tightly packed, three floats per pixel, top row first, scaled so 1.0 is paper white.
inline bool WriteHdrPng(const std::filesystem::path& path, const float* rgb, unsigned int width,
                        unsigned int height)
{
    if (rgb == nullptr || width == 0 || height == 0)
        return false;

    using namespace dlssnr_png;

    std::vector<uint8_t> png;
    const uint8_t signature[8] = { 137, 'P', 'N', 'G', '\r', '\n', 26, '\n' };
    png.insert(png.end(), signature, signature + 8);

    std::vector<uint8_t> ihdr;
    PushBigEndian(ihdr, width);
    PushBigEndian(ihdr, height);
    ihdr.push_back(16); // bits per channel
    ihdr.push_back(2);  // truecolour, no alpha
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    PushChunk(png, "IHDR", ihdr);

    // What makes this an HDR file rather than a very dark SDR one. Coding-independent code points:
    // BT.2020 primaries, the PQ transfer function, identity matrix coefficients, full range -- which
    // is HDR10, and is what Windows Photos, Chrome and Edge look for. Without it a viewer reads PQ
    // code values as sRGB and shows a crushed, dim picture.
    {
        std::vector<uint8_t> cicp;
        cicp.push_back(9);  // colour primaries: BT.2020
        cicp.push_back(16); // transfer: SMPTE ST 2084 (PQ)
        cicp.push_back(0);  // matrix: identity (RGB)
        cicp.push_back(1);  // full range
        PushChunk(png, "cICP", cicp);
    }

    const size_t rowBytes = (size_t) width * 6;
    std::vector<uint8_t> raw;
    raw.reserve((rowBytes + 1) * height);

    for (unsigned int y = 0; y < height; ++y)
    {
        raw.push_back(0); // filter 0, none

        for (unsigned int x = 0; x < width; ++x)
        {
            float wide[3];
            Bt709ToBt2020(rgb + ((size_t) y * width + x) * 3, wide);

            for (int c = 0; c < 3; ++c)
            {
                // Negatives are real here: BT.709 primaries reach outside BT.2020's in places, and the
                // conversion above hands back what that costs. PQ has no answer for them, so they
                // clamp -- to black rather than to a wrapped code value.
                const float v = PqFromNits(wide[c] * kPaperWhiteNits);
                const auto q = (uint16_t) (std::min(std::max(v, 0.0f), 1.0f) * 65535.0f + 0.5f);
                raw.push_back((uint8_t) (q >> 8)); // PNG is big endian
                raw.push_back((uint8_t) (q & 0xFF));
            }
        }
    }

    uint32_t a = 1, b = 0;

    for (uint8_t v : raw)
    {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }

    std::vector<uint8_t> zlib;
    zlib.push_back(0x78);
    zlib.push_back(0x01);

    size_t offset = 0;

    while (offset < raw.size())
    {
        const size_t chunk = std::min<size_t>(65535, raw.size() - offset);
        const bool last = offset + chunk >= raw.size();

        zlib.push_back(last ? 1 : 0);
        zlib.push_back((uint8_t) (chunk & 0xFF));
        zlib.push_back((uint8_t) (chunk >> 8));
        zlib.push_back((uint8_t) (~chunk & 0xFF));
        zlib.push_back((uint8_t) ((~chunk >> 8) & 0xFF));
        zlib.insert(zlib.end(), raw.begin() + offset, raw.begin() + offset + chunk);

        offset += chunk;
    }

    PushBigEndian(zlib, (b << 16) | a);
    PushChunk(png, "IDAT", zlib);
    PushChunk(png, "IEND", {});

    std::FILE* out = _wfopen(path.wstring().c_str(), L"wb");

    if (out == nullptr)
        return false;

    const bool ok = std::fwrite(png.data(), 1, png.size(), out) == png.size();
    std::fclose(out);
    return ok;
}
} // namespace dlssnr_hdr
