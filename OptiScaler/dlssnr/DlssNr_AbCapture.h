// Four screenshots that settle what the pass is doing, taken by the pass itself.
//
// The question this exists for is "is that difference the model, or is it the implementation?", and
// looking at two separate play sessions cannot answer it: different frames, different exposure, and
// the eye is not a reliable instrument across a gap of seconds. What answers it is the same picture
// with one variable moved.
//
// Two pairs come out, because there are two honest ways to hold the frame still and they measure
// different things:
//
//   seam_before / seam_after   THE SAME FRAME, exactly. What the pass was shown and what it produced,
//                              copied either side of the resolve within one evaluate. Nothing else can
//                              differ -- same jitter, same history, same everything -- so any
//                              difference here is the model's edit and nothing else. This is the pair
//                              that catches a broken implementation: wrong subrect, wrong colour
//                              transform, a frame the barriers left half-written.
//
//   no_nr / with_nr            The finished, upscaled frame, on two CONSECUTIVE frames. The model's
//                              edit is suppressed for one and applied for the next, so what changes
//                              is the edit and one frame of the game. This is the pair that answers
//                              what it actually looks like -- the seam pair cannot, because running
//                              before the upscale means the finished frame does not exist yet when
//                              the pass runs, and no upscaler can be asked for two of them.
//
// Suppression is exact rather than approximate: the resolve's ApplyModel already writes back the
// untouched frame it kept, so the suppressed frame is bit-identical to what the upscaler would have
// been handed with Neural Rendering switched off. It is a real control, not a near-miss.
//
// Stand still while it runs. Two frames is 30 ms; a camera moving through them shows up as motion in
// the second pair, and the first pair is unaffected either way.

#pragma once

#include <windows.h>
#include <d3d12.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "DlssNr_Png.h"

namespace abcapture
{
// One image on its way back from the GPU.
struct Shot
{
    ID3D12Resource* readback = nullptr;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
    unsigned long long bytes = 0;
    D3D12_RESOURCE_DESC desc = {};
    bool filled = false;
};

// A copy needs a fully typed format: a placed footprint carrying a typeless one makes
// CopyTextureRegion invalid, and an invalid copy removes the device.
inline DXGI_FORMAT TypedForCopy(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    default:
        return f;
    }
}

inline float HalfToFloat(uint16_t h)
{
    const uint32_t sign = (uint32_t) (h >> 15) << 31;
    uint32_t exponent = (h >> 10) & 0x1F;
    uint32_t mantissa = h & 0x3FF;

    if (exponent == 0)
    {
        if (mantissa == 0)
        {
            const uint32_t bits = sign;
            float out;
            std::memcpy(&out, &bits, 4);
            return out;
        }

        // Subnormal: normalise it by hand.
        exponent = 1;

        while ((mantissa & 0x400) == 0)
        {
            mantissa <<= 1;
            --exponent;
        }

        mantissa &= 0x3FF;
    }
    else if (exponent == 31)
    {
        const uint32_t bits = sign | 0x7F800000u | (mantissa << 13);
        float out;
        std::memcpy(&out, &bits, 4);
        return out;
    }

    const uint32_t bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
    float out;
    std::memcpy(&out, &bits, 4);
    return out;
}

// The 11 and 10 bit floats of R11G11B10: five exponent bits each, no sign.
inline float SmallFloat(uint32_t bits, int mantissaBits)
{
    const uint32_t mantissaMask = (1u << mantissaBits) - 1u;
    const uint32_t exponent = bits >> mantissaBits;
    const uint32_t mantissa = bits & mantissaMask;

    if (exponent == 0)
        return mantissa == 0 ? 0.0f : (float) mantissa / (float) (1u << mantissaBits) * 6.103515625e-5f;

    // The reserved exponent, which in a screenshot has nowhere to go. Handing back a real infinity
    // costs a C4756 for the constant alone, and a NaN reaching the cast to a byte at the end of this
    // file is undefined rather than merely wrong -- so both come back as something the clamp can eat.
    if (exponent == 31)
        return mantissa == 0 ? 3.4e38f : 0.0f;

    return std::ldexp(1.0f + (float) mantissa / (float) (1u << mantissaBits), (int) exponent - 15);
}

inline bool IsFloatFormat(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
        return true;
    default:
        return false;
    }
}

inline float LinearToSrgb(float v)
{
    v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

// Reads one pixel as three floats. Returns false for a format this does not know, which is reported
// rather than guessed at -- a wrong guess would come out as a plausible-looking wrong picture.
inline bool ReadPixel(const uint8_t* row, unsigned int x, DXGI_FORMAT format, float rgb[3])
{
    switch (format)
    {
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    {
        const uint16_t* p = (const uint16_t*) row + (size_t) x * 4;
        rgb[0] = HalfToFloat(p[0]);
        rgb[1] = HalfToFloat(p[1]);
        rgb[2] = HalfToFloat(p[2]);
        return true;
    }
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    {
        const float* p = (const float*) row + (size_t) x * 4;
        rgb[0] = p[0];
        rgb[1] = p[1];
        rgb[2] = p[2];
        return true;
    }
    case DXGI_FORMAT_R11G11B10_FLOAT:
    {
        const uint32_t v = ((const uint32_t*) row)[x];
        rgb[0] = SmallFloat(v & 0x7FF, 6);
        rgb[1] = SmallFloat((v >> 11) & 0x7FF, 6);
        rgb[2] = SmallFloat((v >> 22) & 0x3FF, 5);
        return true;
    }
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    {
        const uint8_t* p = row + (size_t) x * 4;
        rgb[0] = p[0] / 255.0f;
        rgb[1] = p[1] / 255.0f;
        rgb[2] = p[2] / 255.0f;
        return true;
    }
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    {
        const uint8_t* p = row + (size_t) x * 4;
        rgb[0] = p[2] / 255.0f;
        rgb[1] = p[1] / 255.0f;
        rgb[2] = p[0] / 255.0f;
        return true;
    }
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    {
        const uint32_t v = ((const uint32_t*) row)[x];
        rgb[0] = (v & 0x3FF) / 1023.0f;
        rgb[1] = ((v >> 10) & 0x3FF) / 1023.0f;
        rgb[2] = ((v >> 20) & 0x3FF) / 1023.0f;
        return true;
    }
    default:
        return false;
    }
}

// Takes the four shots in order and writes them out.
class AbCapture
{
  public:
    // Arms a capture. Ignored while one is already running, so a held key cannot queue a hundred.
    //
    // settleFrames is how long each state is held before its shot is taken, and it is the whole
    // difference between a measurement and a misleading number. A temporal upscaler resolves each
    // frame by blending a small slice of the current sample -- ten to twenty-five per cent, typically
    // -- into an accumulated history. Photographing a single edited frame therefore photographs that
    // slice: the answer comes out at roughly a fifth of the edit and says far more about the blend
    // weight than about the pass. Holding each state until the history is entirely of that state
    // measures what a player actually sees.
    void request(unsigned int settleFrames)
    {
        if (stage_ != Stage::Idle)
            return;

        settle_ = settleFrames;
        countdown_ = settleFrames;
        stage_ = Stage::SettlingOff;
    }

    bool isActive() const { return stage_ != Stage::Idle; }

    // Whether the model's edit must be suppressed for this frame. The resolve writes back the frame it
    // kept, so the result is bit-identical to the pass being switched off -- and it stays that way for
    // the whole settling run, so the upscaler's history is built entirely from unedited frames.
    bool suppressModel() const
    {
        return stage_ == Stage::SettlingOff || stage_ == Stage::SuppressedFrame;
    }

    // Whether the seam pair should be taken this frame. Only on the frame the model actually ran.
    bool wantSeam() const { return stage_ == Stage::AppliedFrame; }

    // The frame either side of the resolve, within one evaluate. Exactly the same frame.
    void recordSeam(ID3D12GraphicsCommandList* cmd, ID3D12Device* device, ID3D12Resource* clean,
                    D3D12_RESOURCE_STATES cleanState, ID3D12Resource* edited,
                    D3D12_RESOURCE_STATES editedState)
    {
        if (stage_ != Stage::AppliedFrame || clean == nullptr || edited == nullptr)
            return;

        takeShot(cmd, device, clean, cleanState, seamBefore_);
        takeShot(cmd, device, edited, editedState, seamAfter_);
    }

    // The finished, upscaled frame. Called once the upscaler has written it, whichever side of the
    // upscaler the pass itself ran on. This is what advances the sequence.
    void recordOutput(ID3D12GraphicsCommandList* cmd, ID3D12Device* device, ID3D12Resource* output,
                      D3D12_RESOURCE_STATES outputState)
    {
        if (output == nullptr)
            return;

        if (stage_ == Stage::SuppressedFrame)
        {
            takeShot(cmd, device, output, outputState, noNr_);

            // Let the edit back in, and let the upscaler's history fill with edited frames before the
            // second shot, so the pair compares two settled pictures rather than one settled picture
            // against a single frame of transition.
            stage_ = Stage::SettlingOn;
            countdown_ = settle_;
            return;
        }

        if (stage_ == Stage::AppliedFrame)
        {
            takeShot(cmd, device, output, outputState, withNr_);

            // The copies are recorded into the game's own list and there is no fence here to wait on,
            // so the map waits for the frame they came from to be comfortably retired. The same rule
            // the raw capture beside this obeys, and for the same reason.
            stage_ = Stage::Draining;
            drain_ = 8;
        }
    }

    // Call once per frame. Returns true when write() should be called.
    bool tick()
    {
        if (stage_ == Stage::SettlingOff || stage_ == Stage::SettlingOn)
        {
            if (countdown_ > 0 && --countdown_ > 0)
                return false;

            // The next frame is the one photographed, and the stage has to say so before it starts,
            // because the pass reads this at the top of the frame and the shot is taken at the end.
            stage_ = stage_ == Stage::SettlingOff ? Stage::SuppressedFrame : Stage::AppliedFrame;
            return false;
        }

        if (stage_ != Stage::Draining)
            return false;

        if (--drain_ > 0)
            return false;

        stage_ = Stage::Ready;
        return true;
    }

    // Writes the four images and a note saying how to read them. Returns the folder, or empty.
    std::string write(const std::filesystem::path& root, float whitePoint, bool passthrough,
                      bool beforeUpscale)
    {
        if (stage_ != Stage::Ready)
            return {};

        char stamp[32] = {};
        SYSTEMTIME t {};
        GetLocalTime(&t);
        std::snprintf(stamp, sizeof(stamp), "ab_%04u%02u%02u_%02u%02u%02u", t.wYear, t.wMonth, t.wDay,
                      t.wHour, t.wMinute, t.wSecond);

        const auto dir = root / stamp;
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);

        const float divisor = passthrough ? 1.0f : (whitePoint > 1e-4f ? whitePoint : 1.0f);

        bool any = false;
        any |= dump(dir / "1_no_nr.png", noNr_, divisor);
        any |= dump(dir / "2_with_nr.png", withNr_, divisor);
        any |= dump(dir / "3_seam_before.png", seamBefore_, divisor);
        any |= dump(dir / "4_seam_after.png", seamAfter_, divisor);

        writeNote(dir, whitePoint, passthrough, beforeUpscale, settle_);
        release();

        return any ? dir.string() : std::string();
    }

    void release()
    {
        for (Shot* s : { &noNr_, &withNr_, &seamBefore_, &seamAfter_ })
        {
            if (s->readback != nullptr)
                s->readback->Release();

            *s = Shot {};
        }

        stage_ = Stage::Idle;
        drain_ = 0;
        countdown_ = 0;
    }

  private:
    enum class Stage
    {
        Idle,
        SettlingOff,     // the edit is held back, and the upscaler's history is emptying of it
        SuppressedFrame, // still held back, and this is the frame photographed
        SettlingOn,      // the edit is back, and the history is filling with it
        AppliedFrame,    // the model runs; the seam pair and the finished frame are both taken
        Draining,        // waiting for the GPU to be past the copies
        Ready
    };

    static bool alloc(ID3D12Device* device, ID3D12Resource* source, Shot& shot)
    {
        shot.desc = source->GetDesc();
        shot.desc.Format = TypedForCopy(shot.desc.Format);

        unsigned long long total = 0;
        device->GetCopyableFootprints(&shot.desc, 0, 1, 0, &shot.layout, nullptr, nullptr, &total);
        shot.bytes = total;

        if (total == 0)
            return false;

        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC buf = {};
        buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buf.Width = total;
        buf.Height = 1;
        buf.DepthOrArraySize = 1;
        buf.MipLevels = 1;
        buf.Format = DXGI_FORMAT_UNKNOWN;
        buf.SampleDesc.Count = 1;
        buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        return SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buf,
                                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                         IID_PPV_ARGS(&shot.readback)));
    }

    static void takeShot(ID3D12GraphicsCommandList* cmd, ID3D12Device* device, ID3D12Resource* src,
                         D3D12_RESOURCE_STATES state, Shot& shot)
    {
        if (shot.filled || src == nullptr || device == nullptr)
            return;

        if (shot.readback == nullptr && !alloc(device, src, shot))
            return;

        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = src;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = state;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;

        const bool needsTransition = state != D3D12_RESOURCE_STATE_COPY_SOURCE;

        if (needsTransition)
            cmd->ResourceBarrier(1, &b);

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = shot.readback;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = shot.layout;

        D3D12_TEXTURE_COPY_LOCATION source = {};
        source.pResource = src;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        source.SubresourceIndex = 0;

        cmd->CopyTextureRegion(&dst, 0, 0, 0, &source, nullptr);

        if (needsTransition)
        {
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            b.Transition.StateAfter = state;
            cmd->ResourceBarrier(1, &b);
        }

        shot.filled = true;
    }

    // The same transform for every image in the set, so the four are comparable to each other. A
    // linear frame is divided by the paper white the pass itself used and sRGB-encoded; a frame that
    // was already display-referred is written as it stands.
    static bool dump(const std::filesystem::path& path, Shot& shot, float divisor)
    {
        if (!shot.filled || shot.readback == nullptr)
            return false;

        void* mapped = nullptr;
        D3D12_RANGE range = { 0, (SIZE_T) shot.bytes };

        if (FAILED(shot.readback->Map(0, &range, &mapped)) || mapped == nullptr)
            return false;

        const auto width = (unsigned int) shot.desc.Width;
        const auto height = shot.desc.Height;
        const bool isFloat = IsFloatFormat(shot.desc.Format);

        std::vector<uint8_t> rgb((size_t) width * height * 3);
        bool understood = true;

        for (unsigned int y = 0; y < height && understood; ++y)
        {
            const uint8_t* row = (const uint8_t*) mapped + (size_t) y * shot.layout.Footprint.RowPitch;

            for (unsigned int x = 0; x < width; ++x)
            {
                float pixel[3] = { 0.0f, 0.0f, 0.0f };

                if (!ReadPixel(row, x, shot.desc.Format, pixel))
                {
                    understood = false;
                    break;
                }

                for (int c = 0; c < 3; ++c)
                {
                    float channel = pixel[c];

                    // Games do write infinities into HDR buffers, and NaN survives every comparison
                    // below to reach a cast that has no defined answer for it. Saturate one, blacken
                    // the other, and nothing downstream has to think about either.
                    if (!std::isfinite(channel))
                        channel = channel > 0.0f ? 3.4e38f : 0.0f;

                    // A linear frame is open-ended and has to be brought into the range a screen
                    // shows; one that is already display-referred is there already.
                    const float v = isFloat ? LinearToSrgb(channel / divisor)
                                            : std::min(std::max(channel, 0.0f), 1.0f);
                    rgb[((size_t) y * width + x) * 3 + c] = (uint8_t) (v * 255.0f + 0.5f);
                }
            }
        }

        D3D12_RANGE written = { 0, 0 };
        shot.readback->Unmap(0, &written);

        if (!understood)
            return false;

        return dlssnr_png::Write(path, rgb.data(), width, height);
    }

    void writeNote(const std::filesystem::path& dir, float whitePoint, bool passthrough,
                   bool beforeUpscale, unsigned int settle) const
    {
        const auto path = dir / "info.txt";
        std::FILE* f = _wfopen(path.wstring().c_str(), L"wt");

        if (f == nullptr)
            return;

        std::fprintf(f, "DLSS Neural Rendering -- A/B capture\n\n");
        std::fprintf(f, "The pass ran %s the upscaler.\n\n",
                     beforeUpscale ? "BEFORE (render resolution)" : "AFTER (display resolution)");

        std::fprintf(f, "1_no_nr.png      the finished frame with the model's edit held back\n");
        std::fprintf(f, "2_with_nr.png    the finished frame with it applied, the very next frame\n");
        std::fprintf(f, "3_seam_before.png  what the pass was shown\n");
        std::fprintf(f, "4_seam_after.png   what it produced -- the SAME frame as 3, exactly\n\n");

        std::fprintf(f, "3 and 4 are one frame, copied either side of the resolve, so nothing at all\n"
                        "differs between them but the edit. That is the pair to read when asking\n"
                        "whether the pass is implemented right.\n\n");

        std::fprintf(f, "1 and 2 each had their state held for %u frames before being photographed, so\n"
                        "the upscaler's history was entirely of that state when the shot was taken.\n"
                        "Without that hold the second shot would catch a single edited frame blended\n"
                        "into a history of unedited ones, and a temporal upscaler keeps only a small\n"
                        "slice of the current frame -- the answer would come out at about a fifth of\n"
                        "the edit and would be measuring the blend weight, not the pass.\n\n"
                        "The run takes roughly %u frames end to end. Hold still for all of it.\n\n",
                     settle, settle * 2 + 12);

        if (beforeUpscale)
            std::fprintf(f, "Running before the upscale, 3 and 4 are at render resolution and 1 and 2\n"
                            "are the upscaled result, so 3/4 are not a crop of 1/2.\n\n");

        std::fprintf(f, "Paper white %.4f, colour transform %s. Every image had the same transform\n"
                        "applied, so they are comparable to each other but not to a screenshot taken\n"
                        "any other way.\n",
                     whitePoint, passthrough ? "off (the frame was already tone mapped)" : "on");

        std::fclose(f);
    }

    Shot noNr_ {};
    Shot withNr_ {};
    Shot seamBefore_ {};
    Shot seamAfter_ {};

    Stage stage_ = Stage::Idle;
    int drain_ = 0;
    unsigned int settle_ = 0;
    unsigned int countdown_ = 0;
};
} // namespace abcapture
