// A PNG writer, in about a hundred lines and with no dependencies.
//
// The comparison this exists for is looked at, not measured, so the frames have to come out as
// something a person can open. Nothing in the tree writes an image: there is no stb, no DirectXTex,
// and windowscodecs is not linked -- so rather than add a library to the build for four files a
// session, PNG is written directly.
//
// It is lossless without compressing anything. Deflate defines a "stored" block -- a length, its
// complement, and the bytes -- so a valid zlib stream can be produced with no compressor at all. The
// files come out about the size of the raw pixels, which for a 1440p frame is around 10 MB, and every
// viewer reads them. A screenshot that is bit-exact matters more here than one that is small: the
// argument these are settling is about detail at the pixel level, and a codec in the middle of it is
// the confound the raw dumps beside this were written to avoid.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace dlssnr_png
{
inline uint32_t Crc32(const uint8_t* data, size_t length, uint32_t crc = 0xFFFFFFFFu)
{
    static uint32_t table[256];
    static bool built = false;

    if (!built)
    {
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t c = i;

            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);

            table[i] = c;
        }

        built = true;
    }

    for (size_t i = 0; i < length; ++i)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);

    return crc;
}

inline void PushBigEndian(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back((uint8_t) (v >> 24));
    out.push_back((uint8_t) (v >> 16));
    out.push_back((uint8_t) (v >> 8));
    out.push_back((uint8_t) v);
}

inline void PushChunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& body)
{
    PushBigEndian(out, (uint32_t) body.size());

    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), body.begin(), body.end());

    const uint32_t crc = Crc32(out.data() + start, out.size() - start) ^ 0xFFFFFFFFu;
    PushBigEndian(out, crc);
}

// rgb is tightly packed, three bytes per pixel, top row first.
inline bool Write(const std::filesystem::path& path, const uint8_t* rgb, unsigned int width,
                  unsigned int height)
{
    if (rgb == nullptr || width == 0 || height == 0)
        return false;

    std::vector<uint8_t> png;
    const uint8_t signature[8] = { 137, 'P', 'N', 'G', '\r', '\n', 26, '\n' };
    png.insert(png.end(), signature, signature + 8);

    std::vector<uint8_t> ihdr;
    PushBigEndian(ihdr, width);
    PushBigEndian(ihdr, height);
    ihdr.push_back(8); // bits per channel
    ihdr.push_back(2); // truecolour, no alpha
    ihdr.push_back(0); // deflate
    ihdr.push_back(0); // adaptive filtering
    ihdr.push_back(0); // no interlacing
    PushChunk(png, "IHDR", ihdr);

    // The filtered scanlines: a filter byte per row, and PNG's filter 0 is "none".
    const size_t rowBytes = (size_t) width * 3;
    std::vector<uint8_t> raw;
    raw.reserve((rowBytes + 1) * height);

    for (unsigned int y = 0; y < height; ++y)
    {
        raw.push_back(0);
        raw.insert(raw.end(), rgb + (size_t) y * rowBytes, rgb + (size_t) (y + 1) * rowBytes);
    }

    // Adler-32 over the unfiltered stream, which is what zlib checksums.
    uint32_t a = 1, b = 0;

    for (uint8_t v : raw)
    {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }

    std::vector<uint8_t> zlib;
    zlib.push_back(0x78); // deflate, 32K window
    zlib.push_back(0x01); // no preset dictionary, fastest

    // Stored blocks, each at most 65535 bytes.
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

    std::FILE* f = _wfopen(path.wstring().c_str(), L"wb");

    if (f == nullptr)
        return false;

    const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    std::fclose(f);
    return ok;
}
} // namespace dlssnr_png
