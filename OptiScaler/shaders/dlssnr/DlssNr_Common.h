#pragma once

// Everything about the Neural Rendering composition pass that is not Direct3D 12.
//
// Two kinds of thing live here. The constants the shader reads, so a Vulkan implementation can share
// the struct rather than redefine it and drift; and the parameter names the model is driven by, which
// are the model's own and identical whatever API is calling it.
//
// The Direct3D 12 side is DlssNr_Dx12, which implements Shader_Dx12 the way RCAS and Output Scaling
// do. The model itself is separate again: creating and evaluating an NGX feature is not a dispatch,
// so it does not belong in a shader class.

#include <cstdint>

// Which of the passes a dispatch is. One shader, because they read and write the same set of
// resources and differ only in what they compute.
enum DlssNrMode : uint32_t
{
    DlssNrMode_Encode = 0,     // the frame -> a tone-mapped proxy, plus an untouched copy
    DlssNrMode_Resolve = 1,    // proxy + the model's answer + the untouched copy -> the edited frame
    DlssNrMode_Downsample = 2, // the proxy -> a smaller proxy, when the model works below full size
    DlssNrMode_Meter = 3,      // the exposure texture -> tile (0,0), for the white point
    DlssNrMode_Calibrate = 4,  // the untouched frame -> a grid of tile peak luminances
    DlssNrMode_Accumulate = 5, // the de-jittered proxy + last frame's accumulation -> this frame's
    DlssNrMode_Probe = 6       // three mean luminances -- what the model was shown, what it returned,
                               // and the untouched frame -- into the first three texels of the meter
};

// The meter's grid. 64 x 64 tiles over the whole frame, whatever its size.
//
// Tiles rather than pixels because the number wanted is where white sits, not how bright the
// brightest pixel is: a single specular hit or a sky pixel is not the white point, and a frame's
// maximum is exactly the statistic that would be dominated by one. Averaging each tile first means
// anything smaller than a four-thousandth of the frame cannot decide the answer on its own.
//
// 4096 values is also small enough to read back and take a real percentile of on the CPU, rather
// than approximating one on the GPU.
constexpr uint32_t kDlssNrMeterGrid = 64;

// What the composition shader reads.
//
// The model does not replace the frame. It is shown a tone-mapped proxy of the picture, and its
// answer is transferred back onto the real frame -- so most of these describe how much of that answer
// to take, not what the model should do.
// Aligned to 256 because a constant buffer view's size must be a multiple of it. Without this the
// buffer is created at the struct's natural size, the view is invalid, and the device is removed a
// few milliseconds later -- with nothing in any log to say why. Every other shader here does the
// same thing; it is not optional.
// What the caller knows about the frame, and the pass cannot work out for itself.
//
// Everything here is a property of how the game encodes its buffers, not a setting: the user's
// choices -- preset, intensity, strengths, paper white -- stay in Config, so a caller placing this
// pass in a new pipeline does not have to plumb a dozen sliders through it.
//
// Sizes are deliberately absent. The output's dimensions come from its own descriptor and the guide
// sizes from theirs, so there is one less thing for a call site to get wrong.
struct DlssNrFrameInfo
{
    // Which way round depth runs. The game states this when it creates its own upscaler.
    bool DepthInverted = false;

    // How the game encodes its motion vectors, as the game itself reports it. Passed through: every
    // resource already carries a subrect saying how big it is, so scaling by the resolution ratio on
    // top of that counts it twice.
    float MvScaleX = 1.0f;
    float MvScaleY = 1.0f;

    // Throw away the model's history. Set it on a cut, a teleport, or the first frame of a feature.
    bool Reset = false;

    // Whether the colour buffer holds linear, open-ended light or a frame that has already been
    // through a tonemapper. Getting this wrong encodes an encoded frame a second time, which looks
    // washed out and banded.
    bool ColourIsLinearHdr = true;

    // The game's own exposure: a 1x1 texture holding, in the SDK's words, "the final exposure scale".
    //
    // This is the number that makes a cave and a field comparable, and it is the reason a fixed paper
    // white cannot serve both. It comes from the game, decided before anything here runs, so unlike a
    // statistic measured off the frame it cannot be pulled around by what this pass writes.
    //
    // May be null on any given frame -- GTA V supplied it, then did not, three times in one session --
    // so whoever consumes it holds the last good value rather than falling back to a default.
    void* ExposureTexture = nullptr;

    // The scale the game multiplied its buffer by for float precision, which DLSS is told so it can
    // undo it. Usually 1. Divided out before the exposure is applied, exactly as FSR's PrepareRgb does.
    float PreExposure = 1.0f;

    // How much of the depth and motion vector textures the game actually rendered into.
    //
    // Not the same thing as how big those textures are, and the difference is the whole point. A game
    // with dynamic resolution allocates its guides once at the largest size it will ever need and
    // then renders into the top-left corner of them, telling the upscaler how much is real through
    // DLSS.Render.Subrect.Dimensions. Sizing the guides from the resource instead means handing the
    // model whatever was left in the margin -- stale depth and stale vectors -- and calling it scene.
    //
    // Zero means the game did not say, in which case the resource's own size is the best answer
    // available and is what gets used.
    unsigned int RenderSubrectWidth = 0;
    unsigned int RenderSubrectHeight = 0;

    // The game's own DLSS_Feature_Create_Flags, kept whole rather than only decoded into the two
    // fields above. A second DLSS run over this frame has to state the same things about the game's
    // data that the game stated -- which way depth runs, whether the vectors carry the jitter -- and
    // those bits live nowhere else once the parameter block has moved on.
    unsigned int CreateFlags = 0;

    // The sub-pixel offset the upscaler jittered the camera by, in render-resolution pixels, as the
    // game reports it. Every upscaler in the tree reads this; the pass did not, which is what left the
    // model unable to trust its own history on a pre-upscale frame.
    float JitterX = 0.0f;
    float JitterY = 0.0f;
};

struct alignas(256) DlssNrConstants
{
    uint32_t Mode;
    float WhitePoint;

    uint32_t Width;
    uint32_t Height;

    // How much of the model's edit lands, and how much of it is allowed to be colour rather than
    // luminance. Separating the two is what keeps saturated highlights from shifting hue.
    float TransferStrength;
    float ColourStrength;

    uint32_t DebugView;

    // A ceiling on how far a pixel may be brightened. The transfer is a ratio, and a ratio against a
    // near-black proxy pixel is unbounded without one.
    float MaxRatio;

    // Set when the game's buffer is already tone-mapped, in which case there is nothing to convert
    // and the transfer is the identity.
    uint32_t Passthrough;

    float MvScaleX;
    float MvScaleY;

    // Depth and motion vectors come from the upscaler's inputs and so may be at render resolution
    // while colour and output are at display resolution.
    uint32_t GuideWidth;
    uint32_t GuideHeight;

    // Showing the pass against itself. 0 off, 1 side by side, 2 a wipe.
    //
    // Both are drawn by the resolve rather than by a pass of their own, because the resolve is the
    // one place that already holds the frame as the upscaler produced it and the frame the model
    // edited. Comparing them anywhere else would mean keeping a second copy of one of them.
    uint32_t CompareMode;
    float CompareSplit;

    // How much of the frame side by side shows. 1 fits the whole thing at its right shape and
    // letterboxes what is left over; 2 fills the half and crops to the middle instead.
    float CompareZoom;

    // Which side the edited frame is on. Swapping matters because the eye is not even-handed about
    // left and right, so a difference can look like an improvement purely from where it sits.
    uint32_t CompareSwap;

    // How a model that worked below the frame's size is brought back. 0 classic, 1 matched residual.
    //
    // Classic composes the model's own low-resolution picture against the full-resolution frame, so
    // the two disagree by the blur the downsample introduced as well as by the edit -- and the
    // composition reads that disagreement as headroom the frame has and the model never saw. Matched
    // residual takes only the model's *difference* from low resolution and lays it on the frame's own
    // full-resolution proxy, so the two pictures being compared are at the same scale and the only
    // thing carried up from small is the edit itself.
    //
    // The idea and the cube-scaled residual are hhkbble's, from the multi-pass PR against this fork.
    uint32_t Transfer;

    // What the debug views are multiplied by on their way out.
    //
    // They have to be scaled into the frame's units or the game's tonemapper shows them wrong, but
    // scaling them by the live white point makes the instrument move with the thing being measured:
    // two captures at different exposures then differ by the exposure, whatever the edit did. This
    // is the user's own multiplier, which holds still while the meter works.
    float DebugScale;

    // The reversible-proxy mode. 0 soft knee + our composition (default), 1 unclipped Neutwo proxy +
    // our composition, 2 Neutwo proxy + pure-inverse replace (model's answer straight back, no
    // composition). Trailing field, mirroring the shader's cbuffer, so the layout stays a flat run of
    // 4-byte scalars that C++ and HLSL agree on.
    uint32_t ReversibleMode;

    // 0 = output the clean upscaler frame (the pass still runs, so Hold frame keeps a frozen frame
    // to A/B against), 1 = apply the model's edit. Trailing scalar, mirrored in the shader cbuffer.
    uint32_t ApplyModel;

    // D3D12 source-1 zero-latency exposure. UseGameExposure = 1 makes the shader read the game's live
    // exposure texture (bound at t4) instead of the CPU-resolved white point; ExposurePreMul is
    // preExposure * trim, so the live white point is ExposurePreMul / exposure. Mirrored in the cbuffer.
    uint32_t UseGameExposure;
    float ExposurePreMul;

    // The sub-pixel offset the upscaler jittered the camera by this frame, in pixels of this dispatch.
    //
    // Only meaningful before the upscale. The model carries temporal state and reprojects it with the
    // motion vectors it is handed -- and those describe scene motion, not the jitter, which the
    // upscaler adds itself. So a frame taken before the upscale moves under the model every frame in a
    // pattern its vectors do not explain, its carried state never lines up, and it stops committing to
    // fine detail. Showing it a de-jittered picture and putting its answer back where the jitter
    // actually is costs two sub-pixel taps and no extra pass.
    float JitterX;
    float JitterY;

    // 0 off, 1 subtract the offset from what the model is shown, 2 add it. Two conventions because the
    // sign of the jitter is the game's to choose and getting it wrong doubles the error rather than
    // removing it -- so it is settled by measurement rather than by assumption.
    uint32_t DejitterMode;

    // What the edit is multiplied by before an upscaler downstream gets to discard some of it, split
    // because luminance and colour are not discarded at the same rate. Only meaningful before the
    // upscale; 1 (and 0, which is what an unfilled dispatch leaves here) means no compensation.
    //
    // Measured, not guessed. On a matched A/B capture with the after-upscale placement as the control,
    // the pass's edit reaches the screen at 83% of its luminance and 48% of its colour, and the loss
    // does not vary with spatial scale -- so it is a gain, and this is its inverse.
    float CompLuma;
    float CompChroma;

    // How much of the current frame enters the accumulated input the model is shown, and which way the
    // game's motion vectors point. Zero alpha and zero mode mean the accumulation is not running.
    //
    // This is what actually resolves the jitter. De-jitter alone moves the sampling grid onto the pixel
    // centres and adds nothing; averaging successive frames, each jittered differently, recovers detail
    // no single frame holds. The frame handed to the upscaler is untouched either way.
    float AccumAlpha;
    uint32_t AccumMv;

    // How the model's answer becomes the frame this pass writes. Before the upscale only; the
    // after-upscale placement always uses 0 and is byte-identical to what it always was.
    //
    //   0  the model's picture, luminance-anchored to the frame. What this always did.
    //   1  rebuild the frame's own proxy first and carry the model's difference onto it. Needed when
    //      the model's input was reduced or substituted; the IDENTITY when it was neither, because
    //      fullProxy + (model - proxy) is model when proxy is already the frame's own proxy.
    //   2  the frame times what the model changed: original * (model / proxy). Both sides of that
    //      division are in proxy space so the space cancels, leaving a dimensionless "what the model
    //      did" applied to the frame the player actually has -- at the frame's own precision, with
    //      its highlights intact, and bit-exact where the model changed nothing.
    //
    // Modes 0 and 1 reconstruct the output from proxy space, so the frame's detail and precision are
    // replaced by that reconstruction and then accumulated by the upscaler downstream. That is the
    // placement difference that was blamed on the model's resolution.
    uint32_t ComposeMode;
};

class DlssNr_Common
{
  protected:
    // The model's own parameter names, spelled once.
    //
    // These are not ours to choose and they do not vary by API, which is the whole reason they are
    // here rather than in the Direct3D 12 file. Getting one wrong is silent: the model keeps its
    // previous value and the control simply appears to do nothing.
    static constexpr const char* kEnabled = "DLSSNR.Enabled";
    static constexpr const char* kWidth = "DLSSNR.Width";
    static constexpr const char* kHeight = "DLSSNR.Height";

    static constexpr const char* kColor = "DLSSNR.Color";
    static constexpr const char* kDepth = "DLSSNR.Depth";
    static constexpr const char* kMotion = "DLSSNR.MVec";
    static constexpr const char* kOutput = "DLSSNR.Output";

    static constexpr const char* kDepthInverted = "DLSSNR.DepthInverted";
    static constexpr const char* kReset = "DLSSNR.Reset";
    static constexpr const char* kMvScaleX = "DLSSNR.MVecScaleX";
    static constexpr const char* kMvScaleY = "DLSSNR.MVecScaleY";

    // Read once, while the feature is built. Writing these only at evaluate does nothing at all,
    // which is why several of them appeared to be dead controls for a long time.
    static constexpr const char* kPreset = "DLSSNR.Hint.Render.Preset";
    static constexpr const char* kIntensity = "DLSSNR.Intensity";
    static constexpr const char* kStyle = "DLSSNR.Style";
    static constexpr const char* kLocalStructure = "DLSSNR.LocalStructureStrength";
    static constexpr const char* kLocalTone = "DLSSNR.LocalToneStrength";
    // Not a parameter of this model. Kept named so nobody re-adds it: a scan of nvngx_dlssnr.dll for
    // DLSSNR.* yields 61 names and this is absent from them, while every other name here is present.
    // Writing it was harmless -- the block is string-keyed -- but it made a control look real when
    // nothing was listening, which is worse than not having one.
    // static constexpr const char* kGlobalTone = "DLSSNR.GlobalToneStrength";

    // Despite the name, this is the automatic *skin* mask, not an interface mask.
    static constexpr const char* kAutoMask = "DLSSNR.UseAutoMask";

    // Defaults to -1, meaning follow local structure. It is not a 0..1 strength and -1 is not "off".
    static constexpr const char* kSkinStructure = "DLSSNR.SkinStructureStrength";

    // The interface layer, its alpha, and the composited frame. The model accepts all three and is
    // currently given none of them: with no interface supplied there is nothing to correct.
    static constexpr const char* kUi = "DLSSNR.UI";
    static constexpr const char* kUiAlpha = "DLSSNR.UIAlpha";
    static constexpr const char* kBackbuffer = "DLSSNR.Backbuffer";
    static constexpr const char* kUiCorrection = "DLSSNR.UICorrection";
};
