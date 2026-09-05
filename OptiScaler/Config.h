#pragma once

#include "SysUtils.h"
#include "State.h"

#include <optional>
#include <filesystem>

enum HasDefaultValue
{
    WithDefault,
    NoDefault,
    SoftDefault // Change always gets saved to the config
};

template <class T, HasDefaultValue defaultState = WithDefault> class CustomOptional : public std::optional<T>
{
  private:
    T _defaultValue;
    std::optional<T> _configIni;
    bool _volatile;

  public:
    CustomOptional(T defaultValue)
        requires(defaultState != NoDefault)
        : std::optional<T>(), _defaultValue(std::move(defaultValue)), _configIni(std::nullopt), _volatile(false)
    {
    }

    CustomOptional()
        requires(defaultState == NoDefault)
        : std::optional<T>(), _defaultValue(T {}), _configIni(std::nullopt), _volatile(false)
    {
    }

    // Prevents a change from being saved to ini
    constexpr void set_volatile_value(const T& value)
    {
        if (!_volatile)
        { // make sure the previously set value is saved
            if (this->has_value())
                _configIni = this->value();
            else
                _configIni = std::nullopt;
        }
        _volatile = true;
        std::optional<T>::operator=(value);
    }

    // Use this when first setting a CustomOptional
    constexpr void set_from_config(const std::optional<T>& opt)
    {
        if (!this->has_value())
        {
            _configIni = opt;
            std::optional<T>::operator=(opt);
        }
    }

    constexpr CustomOptional& operator=(const T& value)
    {
        _volatile = false;
        std::optional<T>::operator=(value);
        return *this;
    }

    constexpr CustomOptional& operator=(T&& value)
    {
        _volatile = false;
        std::optional<T>::operator=(std::move(value));
        return *this;
    }

    constexpr CustomOptional& operator=(const std::optional<T>& opt)
    {
        _volatile = false;
        std::optional<T>::operator=(opt);
        return *this;
    }

    constexpr CustomOptional& operator=(std::optional<T>&& opt)
    {
        _volatile = false;
        std::optional<T>::operator=(std::move(opt));
        return *this;
    }

    // Needed for string literals for some reason
    constexpr CustomOptional& operator=(const char* value)
        requires std::same_as<T, std::string>
    {
        _volatile = false;
        std::optional<T>::operator=(T(value));
        return *this;
    }

    constexpr T value_or_default() const&
        requires(defaultState != NoDefault)
    {
        return this->has_value() ? this->value() : _defaultValue;
    }

    constexpr T value_or_default() &&
        requires(defaultState != NoDefault) {
            return this->has_value() ? std::move(this->value()) : std::move(_defaultValue);
        }

        constexpr std::optional<T> value_for_config()
            requires(defaultState == WithDefault)
    {
        if (_volatile)
        {
            if (_configIni != _defaultValue)
                return _configIni;

            return std::nullopt;
        }

        if (!this->has_value() || *this == _defaultValue)
            return std::nullopt;

        return this->value();
    }

    constexpr std::optional<T> value_for_config()
        requires(defaultState != WithDefault)
    {
        if (_volatile)
            return _configIni;

        if (this->has_value())
            return this->value();

        return std::nullopt;
    }

    constexpr T value_for_config_or(T other)
    {
        auto option = value_for_config();

        if (option.has_value())
            return option.value();
        else
            return other;
    }
};

constexpr inline int UnboundKey = -1;
constexpr uint32_t NV_PRESET_LATEST = 0x00FFFFFF;

enum FpsOverlayPos : uint32_t
{
    FpsOverlayPos_TopLeft,
    FpsOverlayPos_TopRight,
    FpsOverlayPos_BottomLeft,
    FpsOverlayPos_BottomRight,
    FpsOverlayPos_COUNT,
};

enum FpsOverlay : uint32_t
{
    FpsOverlay_JustFPS,
    FpsOverlay_Simple,
    FpsOverlay_Detailed,
    FpsOverlay_DetailedGraph,
    FpsOverlay_Full,
    FpsOverlay_FullGraph,
    FpsOverlay_ReflexTimings,
    FpsOverlay_COUNT,
};

// Output scaling downscaler
enum class Scaler : uint32_t
{
    FSR1 = 0,
    Bicubic = 1,
    CatmullRom = 2,
    Lanczos2 = 3,
    Lanczos3 = 4,
    Kaiser2 = 5,
    Kaiser3 = 6,
    Magic = 7,
    Count
};

enum class ForceReflex : uint32_t
{
    InGame,
    ForceDisable,
    ForceEnable,
    Count
};

enum class LFXMode : uint32_t
{
    Conservative,
    Aggressive,
    ReflexIDs,
    Count
};

enum class LowLatencyInput : uint32_t
{
    None,
    Auto,
    AntiLag2,
    Reflex,
    XeLL,
    UeLowLatency,
    _
};

enum class LowLatencyMode : uint32_t
{
    None,
    Auto,
    LatencyFlex,
    AntiLag2,
    XeLL,
    AntiLagVk,
    Reflex
};

class Config
{
  public:
    Config();

    // Init flags
    CustomOptional<bool, NoDefault> DepthInverted;
    CustomOptional<bool, NoDefault> AutoExposure;
    CustomOptional<bool, NoDefault> HDR;
    CustomOptional<bool, NoDefault> JitterCancellation;
    CustomOptional<bool, NoDefault> DisplayResolution;
    CustomOptional<bool, NoDefault> DisableReactiveMask;
    CustomOptional<float> DlssReactiveMaskBias { 0.45f };

    // Logging
    CustomOptional<bool> LogToFile { false };
    CustomOptional<bool> LogToConsole { false };
    CustomOptional<bool> LogToDebug { false };
    CustomOptional<bool> LogToNGX { false };
    CustomOptional<bool> OpenConsole { false };
    CustomOptional<bool> DebugWait { false }; // not in ini
    CustomOptional<int> LogLevel { 0 };
    CustomOptional<std::wstring> LogFileName { L"OptiScaler.log" };
    CustomOptional<bool> LogSingleFile { true };
    CustomOptional<bool> LogAsync { false };
    CustomOptional<int> LogAsyncThreads { 4 };

    // XeSS
    CustomOptional<bool> BuildPipelines { true };
    CustomOptional<int32_t> NetworkModel { 0 };
    CustomOptional<bool> CreateHeaps { true };

    // --- DLSS 5 Neural Rendering (OptiScaler/dlssnr) --- removable as one block -----------------
    // DLSS Neural Rendering: a detail-synthesis pass over the upscaler's output. Off by default -- it is
    // an undocumented feature driven directly through its snippet, not something NVIDIA exposes.
    CustomOptional<bool> DlssNrEnabled { false };
    // Toggles the pass in game. Unbound by default -- a key that does something unexpected is worse
    // than one that does nothing.
    CustomOptional<int> DlssNrToggleKey { UnboundKey };

    // Which side of the upscaler the model runs on.
    //
    //   true   the upscaler's input, at render resolution -- 1707x960 for a 1440p monitor on Quality
    //   false  the upscaler's output, at display resolution, which is where NVIDIA runs it
    //
    // Before the upscale the model sees (render/display)^2 of the pixels, which is under half the
    // cost at Quality, and the upscaler then carries its detail through its own accumulation rather
    // than the model landing on an already resolved frame. What it gives up is the frame it is shown:
    // jittered and aliased rather than resolved, and the upscaler is free to reject some of what it
    // synthesised on it.
    //
    // Direct3D 12 only, which covers D3D12 games and both bridges. A game on the native Vulkan path
    // keeps the after-upscale placement whatever this says.
    CustomOptional<bool> DlssNrBeforeUpscale { true };

    // Takes the four matched screenshots. Unbound by default, like the toggle above.
    //
    // A key rather than only a menu button because the capture wants the scene held still and opening
    // the menu is the one action guaranteed to move it. A file named dlssnr-ab.trigger beside
    // OptiScaler does the same thing from outside the game.
    CustomOptional<int> DlssNrAbCaptureKey { UnboundKey };

    // How long the A/B capture holds each state before photographing it.
    //
    // A temporal upscaler blends a slice of the current frame into an accumulated history, so a shot
    // taken one frame after the edit is switched shows that slice and not the edit. Holding until the
    // history is entirely of one state is what makes the two shots comparable. Twenty-four frames is
    // comfortably past what DLSS and FSR accumulate over; raise it if the upscaler is slower to
    // converge, at the cost of standing still for longer.
    CustomOptional<uint32_t> DlssNrAbCaptureSettle { 24 };

    // Put the frame back on the pixel grid before the model sees it, and put its answer back where the
    // jitter is on the way out. Before the upscale only; after it the upscaler has already resolved the
    // jitter away and there is nothing to undo.
    //
    //   0  off
    //   1  subtract the camera's sub-pixel offset from what the model is shown
    //   2  add it
    //
    // Two directions because the sign of the jitter is the game's to choose, and the wrong one doubles
    // the misalignment instead of removing it. Off by default until a capture says which way round it
    // goes in a real title.
    CustomOptional<uint32_t> DlssNrDejitter { 0 };

    // What the upscaler is handed when the pass runs before it: the frame plus the model's edit, or
    // the model's picture.
    //
    // The composition does not add an edit onto the frame by default. It returns the model's answer as
    // a complete picture with its luminance re-anchored per pixel -- so at the shipped strengths the
    // output IS the model's picture, and the frame contributes its luminance and nothing else.
    //
    // After the upscale that is a defensible way to compose a finished frame. Before it, the game's
    // upscaler is then handed the model's reconstruction of a proxy -- tone-curved into [0,1], round
    // tripped through an sRGB surface, un-curved by a luminance rescale rather than by inverting the
    // curve -- and accumulates it temporally. That is the placement difference that was blamed on
    // resolution for a long time, and it is not the model's doing.
    //
    // On, the resolve rebuilds the frame's own proxy and carries only the model's difference onto it,
    // so the upscaler receives the game's own frame with the model's edit on it. Off is the old
    // behaviour, kept because it is the only way to see what this was.
    CustomOptional<bool> DlssNrCarryEdit { true };

    // Run a second DLSS, at a 1:1 ratio, over the model's input before the model sees it.
    //
    // Running the model before the upscale is the only affordable placement -- cost is the frame's
    // area -- and what it gives up is not resolution: the same model resolution AFTER the upscale
    // looks better. It is that the frame before the upscale is raw. Jittered, aliased, one sample a
    // pixel. Resolving that is what DLSS is for, and at 1:1 it does nothing else.
    //
    // Safe because the resolve is told the model's input was substituted, and on that flag carries the
    // model's DIFFERENCE onto the frame's own proxy instead of handing back the model's picture whole.
    // This pass is then present on both sides of that subtraction and cancels, and the upscaler
    // downstream receives the game's own jittered frame with the model's edit on it, never this one.
    // Without that flag -- which is how this shipped first -- the DLSS reconstruction went into the
    // player's frame, which is replacing the picture rather than cleaning the model's input.
    //
    // Costs one DLAA evaluate at render resolution, and a model instance's worth of history.
    CustomOptional<bool> DlssNrPrepass { false };

    // Which way to shift the model's edit back onto the still-jittered frame it is composed onto.
    //
    // 0 none, 1 subtract the jitter, 2 add it -- the same three choices, and the same reason for
    // there being three, as the de-jitter above: the sign of the offset is the game's convention.
    // Only meaningful while the prepass is running, since only then is the model's answer on a
    // different grid from the frame underneath it.
    CustomOptional<uint32_t> DlssNrPrepassRejitter { 1 };
    CustomOptional<uint32_t> DlssNrPreset { 0 };
    CustomOptional<float> DlssNrIntensity { 1.0f };
    // 0 default (standard), 1 natural, 2 cinematic -- the model's own processing profiles.
    CustomOptional<uint32_t> DlssNrStyle { 0 };
    CustomOptional<float> DlssNrLocalStructure { 1.0f };
    CustomOptional<float> DlssNrLocalTone { 1.0f };
    // -1 means follow local structure, which is the model's own default. It is not a strength of zero.
    CustomOptional<float> DlssNrSkinStructure { -1.0f };
    CustomOptional<bool> DlssNrAutoMask { true };

    // How much of the model's edit reaches the frame. Separated because detail synthesis is a luminance
    // edit and any colour shift is usually the part you do not want, and allowed past 1.0 because
    // exaggerating an edit is the only honest way to see whether there is one.
    CustomOptional<float> DlssNrTransferStrength { 1.0f };
    CustomOptional<float> DlssNrColourStrength { 1.0f };

    // The RenoDX reversible proxy mode. 0 = today's soft-knee encode + our composition (default,
    // byte-identical); 1 = unclipped Neutwo proxy + our composition; 2 = Neutwo proxy + pure-inverse
    // replace. An in-game A/B and a way back. Default 0 = byte-identical to before.
    CustomOptional<uint32_t> DlssNrReversibleMode { 0 };

    // Whether the model's edit is applied. Off keeps the pass running (so Hold frame works) but shows
    // the clean upscaler frame -- for A/B'ing NR on/off on a frozen frame. Default true.
    CustomOptional<bool> DlssNrApplyModel { true };

    // Frame hold: freeze the NR pass's input so a live setting change re-renders the SAME frame -- the
    // only clean way to A/B our settings. A live testing toggle, not really a saved preference; off by
    // default. See dlssnr/design/frame-hold.md.
    CustomOptional<bool> DlssNrHoldFrame { false };


    // The most the pass may multiply or divide a pixel by. A detail pass has no business restyling a
    // light source, whatever the model returns.
    CustomOptional<float> DlssNrMaxRatio { 2.0f };

    // How a model that worked below the frame's size is brought back. 0 classic, 1 matched
    // residual. Only has an effect when Model resolution is under 100%.
    CustomOptional<uint32_t> DlssNrTransfer { 1 };

    // Measure the white point from the frame instead of taking it from the slider. On a frame the
    // game already tone mapped there is nothing to measure and this has no effect.
    //
    // Off by default, because it is not finished. The pass writes its result back into the same buffer
    // the meter reads, so with the pass running the meter is partly measuring its own output and the
    // two chase each other: Enshrouded, one session, 1545 samples spanning 0.01 to 97.9 with 57 jumps
    // beyond 1.5x in a single frame. Measured in the same spot seconds apart, 41.31 with the pass off
    // against 0.46 with it on. That is visible as the picture pumping and occasionally flickering.
    //
    // The slider is the supported control until the loop is broken. This stays as an opt-in so the
    // behaviour can still be looked at.


    // Take the white point from the game's own exposure texture instead of measuring or guessing.
    // Off by default until it has been seen to work in more than one game.
    CustomOptional<bool> DlssNrWhitePointFromExposure { true };

    // Ask the model, once, whether it will run on Direct3D 11 without the bridge.
    //
    // Off by default and deliberately so. Everything else this pass does reads memory it already owns;
    // this one initialises an NVIDIA subsystem on the game's live D3D11 device, in a process where the
    // D3D12 NGX instance is already running. It should return an error code and nothing more, but
    // "should" is doing work in that sentence and it ships into games nobody can test first.
    CustomOptional<bool> DlssNrProbeD3D11 { false };

    // 0 off, 1 the picture the model was shown, 2 its raw answer, 3 what it changed, amplified.
    CustomOptional<uint32_t> DlssNrDebugView { 0 };

    // Showing the pass against itself, without having to toggle it and remember what the last frame
    // looked like. 0 off, 1 side by side, 2 a wipe.
    //
    // Side by side squeezes the whole frame into each half, so it is a comparison rather than
    // something to play in. The wipe cuts one frame and resamples nothing, so it is; the split is a
    // stored setting and stays where it was put once the menu closes.
    CustomOptional<uint32_t> DlssNrCompare { 0 };
    CustomOptional<float> DlssNrCompareSplit { 0.5f };

    // Side by side only. 1 fits the whole frame at its right shape and accepts the bars; 2 fills
    // the half and crops the sides off instead.
    CustomOptional<float> DlssNrCompareZoom { 1.0f };

    // Which side the edited frame sits on, in both comparison modes.
    CustomOptional<bool> DlssNrCompareSwap { false };

    // Labels drawn onto the two sides of a comparison, so a screenshot still says which is which.
    // Drawn into the frame's own plane with a clip per side: in the wipe they are revealed and hidden
    // by the split exactly as the images are, and there is nothing to drag.
    CustomOptional<bool> DlssNrCompareTags { false };
    CustomOptional<float> DlssNrTagScale { 1.5f };

    // The fraction of the frame's resolution the model works at. The frame itself is never reduced --
    // only the model's contribution is computed small and enlarged, so the picture underneath is
    // untouched whatever this is set to. 1.0 is full resolution and behaves exactly as before.
    CustomOptional<float> DlssNrWorkingScale { 1.0f };

    // What the model's edit is scaled by before the upscaler downstream discards part of it. Applied
    // only when the pass runs before the upscale, where its output is an upscaler input rather than
    // the finished frame.
    //
    // Off by default. It was shipped on, at the measured inverses of what one A/B capture said
    // survives, and that was wrong twice over: the arithmetic clipped (green faces, shadows filling in
    // as black) and, once that was fixed, the idea underneath is still only a gain -- it can ask an
    // upscaler for more of an edit, not make it carry what it declined to. Left here because asking
    // is cheap and the answer is worth having per game, but the shipped picture is the composed one.
    // Which convention the model's motion-vector scale is handed over in when the model is NOT at
    // 100%: 0 the game's own value untouched, 1 that value times workWidth/width. At 100% the two are
    // identical and this setting does nothing at all.
    //
    // Defaults to 0 on the reference implementation's evidence. NVIDIA's own ReShade addon runs the
    // model on the UPSCALED colour with the game's live render-resolution depth and motion vectors and
    // does nothing to them -- so the model is routinely handed a colour raster twice the size of its
    // vectors and copes, which it can only do by rescaling from the MVec subrect itself. A factor
    // applied by the caller on top of that is counted twice, which is what the comment where this
    // value is stored has said all along.
    // Resolve the jitter out of the model's input by averaging frames, rather than only shifting the
    // sampling grid. 0 off, 1 reproject by subtracting the motion vector, 2 by adding it -- the sign is
    // the game's to choose. Only meaningful before the upscale, where the frame is still jittered.
    // Let the MODEL do the enlarging, instead of running it small and stretching its edit.
    //
    // Model resolution below 100% has never used the model's own upscaling: it runs the model at the
    // reduced size and enlarges the EDIT, leaving the frame underneath untouched. That is compositing,
    // which is why its help text talks about the model's contribution rather than about resolution.
    // Feature 18 takes a frame at one size and returns one at another -- NVIDIA's own addon carries
    // InputWidth, OutputWidth, Upscaling and ScalingRatio, and this project's forwarder already reads
    // the scaling ratio through the model's own callback. This switches that on.
    // Give the model half-size pixels for the picture it actually reads.
    //
    // Every scratch surface has always inherited the game's format -- four half floats for a linear HDR
    // game, eight bytes a pixel. But the encode ends in LinearToSrgb, which saturates, so the proxy the
    // model reads and the answer it writes are both inside [0,1] and fit in four. At 3440x1440 that is
    // forty megabytes each way per frame for the model alone.
    //
    // The frame the player sees is untouched: only the model's own surfaces change, never the kept
    // copy the edit is composed onto. Costs five to six mantissa bits a channel and the alpha, which is
    // why it is a setting rather than the default.
    CustomOptional<bool> DlssNrCompactProxy { false };

    CustomOptional<bool> DlssNrModelUpscale { false };

    CustomOptional<uint32_t> DlssNrInputAccum { 0 };

    // How much of the current frame enters that average. Lower resolves more and lags more.
    CustomOptional<float> DlssNrInputAccumAlpha { 0.15f };

    CustomOptional<uint32_t> DlssNrMvScaleMode { 0 };

    CustomOptional<float> DlssNrCompLuma { 1.0f };
    CustomOptional<float> DlssNrCompChroma { 1.0f };

    // Filter used for NR supersampling (working scale > 1): the model runs above native, and this is
    // the downscaler that averages its answer back to native. Independent of OutputScalingDownscaler
    // so NR and Output Scaling can run different filters at once. Lanczos3 is the sharp default.
    CustomOptional<Scaler> DlssNrScalingDownscaler { Scaler::Lanczos3 };

    // Ask the driver's own nvngx.dll whether it will dispatch Neural Rendering, once per session.
    //
    // Everything here drives the model's DLL directly through a forwarder, because the model refuses
    // callers whose module path does not contain "nvngx.dll". But the model ships inside the driver
    // store, and NVIDIA does not ship a feature DLL that no dispatcher can reach -- so the driver's
    // nvngx.dll may well know feature 18 already. If it does, the forwarder is unnecessary, the
    // signature question disappears, and users stop needing a 165 MB copy in every game folder.
    //
    // Off by default: it is a diagnostic, not a feature.
    CustomOptional<bool> DlssNrProxyProbe { false };

    // Run Neural Rendering through the driver's own nvngx.dll rather than through the forwarder.
    //
    // This is how DLSS itself is called. The forwarder exists only because driving the model
    // directly trips its caller check, and a probe showed the driver dispatches feature 18 already:
    // asking for 18 answers differently from asking for a feature that does not exist. OptiScaler
    // also already tells the driver where to look, since NVNGX_FeatureInfo_Paths carries the game
    // and OptiScaler folders into Init_Ext.
    //
    // Off until it is shown to produce the same picture. If it does, the forwarder can go.
    CustomOptional<bool> DlssNrUseProxy { false };

    // Look for the exposure the game computed but never handed to the upscaler.
    //
    // Off by default, and it has to be. Reading a resource the game owns means assuming what state
    // it is in, and unlike depth and motion vectors -- where NGX documents the contract -- a buffer
    // found by its shape comes with no promise at all. UNORDERED_ACCESS is the reasonable
    // assumption, since every candidate got here by having a UAV made on it, but it is an
    // assumption, and nobody who has not asked for the scan should be carrying that risk.
    //
    // It decides nothing either way. It watches and it reports, because the last two times a number
    // was inferred here it went straight into the interface and was wrong.
    CustomOptional<bool> DlssNrScanExposure { false };

    // Anchoring the scan: the white point that looked right, and the scan's value at that moment.
    //
    // The absolute white point cannot be derived from a buffer whose units are unknown. What CAN be
    // derived is every value after the first: if the scan's number halves, the scene got twice as
    // bright, and the white point follows -- whatever the number actually means, because only the
    // ratio is used and the units cancel.
    //
    // So the user sets it once, in one lighting condition, and presses a button. After that it stays
    // correct through every cave and every noon without being touched again. Which is also the shape
    // that makes per-game profiles work: one person anchors a game, everybody else gets the number.
    //
    // Zero means not anchored, and then nothing happens at all.
    // A lamp in the corner showing what the scan currently thinks the light is doing: red for dark,
    // green for full light, and the shades between. Off by default; it is for watching the thing
    // work, not for playing with.
    // Where the white point comes from. One control, because there is one answer.
    //
    //   0  the paper white slider, and nothing else
    //   1  the exposure the game hands the upscaler
    //   2  a buffer the scan found, anchored to a white point the user chose once
    //
    // This replaces two independent checkboxes that could both be on. They were made exclusive by
    // greying, which deadlocked -- each disabled the other, so once both were set the only way out
    // was a button the notice never mentioned -- and then by clearing, which silently undid a
    // setting the user had made. Both were attempts to stop an illegal state being REACHED. A single
    // choice cannot reach it: there is nothing to keep consistent, because there is only one value.
    CustomOptional<uint32_t> DlssNrWhitePointSource { 1 };

    CustomOptional<bool> DlssNrScanMeter { false };

    CustomOptional<float> DlssNrScanAnchorValue { 0.0f };       // legacy single anchor, migrated then unused
    CustomOptional<float> DlssNrScanAnchorWhitePoint { 0.0f };  // legacy single anchor, migrated then unused

    // The multi-point anchor table, serialised as "scan:white;scan:white;..." ascending. See
    // dlssnr/design/multi-point-anchoring.md. Replaces the single pair above; a pre-existing single
    // anchor is migrated into a one-row table on first load.
    CustomOptional<std::string> DlssNrScanAnchors { std::string() };

    // Whether the scan's number rises or falls with the light.
    //
    // A found buffer carries no contract. Most engines store an exposure -- a multiplier that goes
    // DOWN as the scene gets brighter -- but some store its reciprocal, and nothing in the buffer
    // says which. Rather than guess and be silently wrong in half the games, this is one click: if
    // the picture moves the wrong way, flip it.
    CustomOptional<bool> DlssNrScanInverted { false };







    // The trim on an exposure-derived white point, kept apart from the manual divisor on purpose.
    //
    // These are two different quantities that happened to share one slider: the manual path wants an
    // absolute divisor on an open-ended linear buffer, which in Nioh 3 is about 240, and the exposure
    // path wants a multiplier on a number the game already supplied, where anything far from 1 is
    // a sign the read is wrong rather than a preference. Sharing one stored value meant touching the
    // slider in one mode silently destroyed the number found in the other.
    //
    // 1.0 is the identity: take the game's exposure exactly as given. That is the "safe value", and
    // it is safe by construction rather than by being written down somewhere.
    CustomOptional<float> DlssNrWhitePointTrim { 1.0f };

    // The scan's trim, kept apart from the exposure texture's.
    //
    // They are trims on different things and a value found against one is meaningless against the
    // other. Sharing one slider meant switching source silently carried a number across, so a
    // picture that had been tuned came back wrong for a reason nothing on screen explained.
    CustomOptional<float> DlssNrScanTrim { 1.0f };

    // How many times to run the model over the same frame, each pass fed the previous one's answer.
    //
    // 1 is what the model was trained for and what every published number describes. Above that it
    // is being asked to enhance its own output, which is outside its training distribution: detail
    // compounds, and so does anything it got wrong. Two often looks richer. Four usually looks
    // synthetic. Ten is there because somebody will want to see it.
    //
    // The cost is exactly linear -- the model is 99% of the frame's expense and every pass pays it
    // again -- so 10 costs ten times, near enough. There is no shortcut and no amortisation: the
    // passes are sequential and each one needs the last one's output. Which is why this belongs with
    // the reduced placements rather than against them: three passes at half resolution is about
    // three quarters of one pass at full, and that is the trade it exists to let you make.
    //
    // Every extra pass gets its OWN model instance, not a second call into the first. One instance
    // told three times that a frame passed, with the same motion vectors each time, fights its own
    // history from the second run on -- which is what "loses detail on later passes" was. Separate
    // instances each see one frame per frame, which is the contract they were built for, and the
    // cost of that is an instance's worth of video memory per pass.
    CustomOptional<uint32_t> DlssNrPasses { 1 };

    // Which depth convention the model is told the guide uses.
    //
    //   0  what the game's own DLSS feature was created with, which is what it means for the upscaler
    //   1  force normal
    //   2  force inverted
    //
    // Writes one set of matched before/after frames per session, without anyone having to ask. The
    // folder is cleared at the start of each run, so it holds one session's worth and never grows.
    CustomOptional<bool> DlssNrAutoCapture { true };





    // Multiplies the (auto or manual) white point before the encode: what the model considers "white".
    // Higher means highlights sit lower on the curve and the model treats them as less extreme.
    CustomOptional<float> DlssNrWhitePointScale { 1.0f };





    // --- end DLSS 5 Neural Rendering -------------------------------------------------------------

    // DLSS
    CustomOptional<bool> DLSSEnabled { true };
    CustomOptional<bool> RenderPresetOverride { false };
    CustomOptional<uint32_t> RenderPresetForAll { 0 };
    CustomOptional<uint32_t> RenderPresetDLAA { 0 };
    CustomOptional<uint32_t> RenderPresetUltraQuality { 0 };
    CustomOptional<uint32_t> RenderPresetQuality { 0 };
    CustomOptional<uint32_t> RenderPresetBalanced { 0 };
    CustomOptional<uint32_t> RenderPresetPerformance { 0 };
    CustomOptional<uint32_t> RenderPresetUltraPerformance { 0 };

    // DLSSD
    CustomOptional<bool> DLSSDRenderPresetOverride { false };
    CustomOptional<uint32_t> DLSSDRenderPresetForAll { 0 };
    CustomOptional<uint32_t> DLSSDRenderPresetDLAA { 0 };
    CustomOptional<uint32_t> DLSSDRenderPresetUltraQuality { 0 };
    CustomOptional<uint32_t> DLSSDRenderPresetQuality { 0 };
    CustomOptional<uint32_t> DLSSDRenderPresetBalanced { 0 };
    CustomOptional<uint32_t> DLSSDRenderPresetPerformance { 0 };
    CustomOptional<uint32_t> DLSSDRenderPresetUltraPerformance { 0 };

    // Nukems
    CustomOptional<bool> NvngxFGMakeDepthCopy { false };

    // Libraries
    CustomOptional<std::wstring, NoDefault> MainDllPath;
    CustomOptional<std::wstring, NoDefault> FfxDx12Path;
    CustomOptional<std::wstring, NoDefault> FfxDx12SRPath;
    CustomOptional<std::wstring, NoDefault> FfxDx12FGPath;
    CustomOptional<std::wstring, NoDefault> FfxDx12RRPath;
    CustomOptional<std::wstring, NoDefault> FfxDx12RCPath;
    CustomOptional<std::wstring, NoDefault> FfxVkPath;
    CustomOptional<std::wstring, NoDefault> XeSSLibrary;
    CustomOptional<std::wstring, NoDefault> XeFGLibrary;
    CustomOptional<std::wstring, NoDefault> XeLLLibrary;
    CustomOptional<std::wstring, NoDefault> XeSSDx11Library;
    CustomOptional<std::wstring, NoDefault> NvngxPath;
    CustomOptional<std::wstring, NoDefault> NVNGX_DLSS_Library;
    CustomOptional<std::wstring, NoDefault> DLSSFeaturePath;
    CustomOptional<std::wstring, NoDefault> NvapiDllPath;

    // Sharpness
    CustomOptional<SharpenShader> SharpnessShader { SharpenShader::RCAS };
    CustomOptional<bool> OverrideSharpness { false };
    CustomOptional<float> Sharpness { 0.4f };

    // RCAS
    CustomOptional<bool> RcasEnabled { false };
    CustomOptional<bool> ContrastEnabled { false };
    CustomOptional<float> Contrast { -0.3f };

    // DA Sharpening
    CustomOptional<float, NoDefault> DADepthScale;
    CustomOptional<float, NoDefault> DADepthBias;
    CustomOptional<bool, NoDefault> DAClampOutput;

    // MAS
    CustomOptional<bool> MotionSharpnessEnabled { false };
    CustomOptional<bool> MotionSharpnessDebug { false };
    CustomOptional<float> MotionSharpness { 0.2f };
    CustomOptional<float> MotionThreshold { 0.0f };
    CustomOptional<float> MotionScaleLimit { 10.0f };

    // Magnifier
    CustomOptional<bool> MagnifierEnabled { false };
    CustomOptional<float> MagnifierSize { 15.f }; // % of screen Height
    CustomOptional<int> MagnifierZoomFactor { 4 };
    CustomOptional<float> MagnifierBorderSize { 0.3f };   // % of screen Height
    CustomOptional<float> MagnifierCursorOffsetX { 0.f }; // Pixels
    CustomOptional<float> MagnifierCursorOffsetY { 0.f }; // Pixels
    CustomOptional<float, NoDefault> MagnifierStaticPosX; // % of screen Width, static pos enabled if both are defined
    CustomOptional<float, NoDefault> MagnifierStaticPosY; // % of screen Height

    // Menu
    CustomOptional<float, NoDefault> MenuScale;
    CustomOptional<bool> OverlayMenu { true };
    CustomOptional<int> ShortcutKey { VK_INSERT };
    CustomOptional<bool> ExtendedLimits { false };
    CustomOptional<bool> ShowFps { false };
    /// 0 Top Left, 1 Top Right, 2 Bottom Left, 3 Bottom Right
    CustomOptional<FpsOverlayPos> FpsOverlayPosition { FpsOverlayPos_TopLeft };
    /// 0 Only FPS, 1 +Avg FPS & Upscaler info 2 +Frame Time,
    /// 3 +Upscaler Time, 4 +Frame Time Graph, 5 +Upscaler Time Graph
    /// 6 +Reflex timings
    CustomOptional<FpsOverlay> FpsOverlayType { FpsOverlay_JustFPS };
    CustomOptional<int> FpsShortcutKey { VK_PRIOR };
    CustomOptional<int> FpsCycleShortcutKey { VK_NEXT };
    CustomOptional<bool> FpsOverlayHorizontal { false };
    CustomOptional<float> FpsOverlayAlpha { 0.4f };
    CustomOptional<float, NoDefault> FpsScale; // No value means same as MenuScale
    CustomOptional<bool> UseHQFont { true };
    CustomOptional<bool> DisableSplash { false };
    CustomOptional<float> FontSize { 14.0f };
    CustomOptional<std::wstring, NoDefault> TTFFontPath;
    CustomOptional<int> FGShortcutKey { VK_END };
    CustomOptional<bool> LightTheme { false };
    CustomOptional<bool> OverlaysUseTheme { false };
    CustomOptional<float> MenuAccentColorR { 0.00f };
    CustomOptional<float> MenuAccentColorG { 0.40f };
    CustomOptional<float> MenuAccentColorB { 0.77f };
    CustomOptional<float> MenuBGColorR { 0.0f };
    CustomOptional<float> MenuBGColorG { 0.0f };
    CustomOptional<float> MenuBGColorB { 0.0f };
    CustomOptional<float> MenuBGColorA { 0.99f };

    // Hooks
    CustomOptional<bool> HookOriginalNvngxOnly { false };
    CustomOptional<bool> EarlyHooking { false };
    CustomOptional<bool> UseNtdllHooks { true };

    // Upscale Ratio Override
    CustomOptional<bool> UpscaleRatioOverrideEnabled { false };
    CustomOptional<float> UpscaleRatioOverrideValue { 1.3f };

    // DRS
    CustomOptional<bool> DrsMinOverrideEnabled { false };
    CustomOptional<bool> DrsMaxOverrideEnabled { false };

    // Quality Overrides
    CustomOptional<bool> QualityRatioOverrideEnabled { false };
    CustomOptional<float> QualityRatio_DLAA { 1.0f };
    CustomOptional<float> QualityRatio_UltraQuality { 1.3f };
    CustomOptional<float> QualityRatio_Quality { 1.5f };
    CustomOptional<float> QualityRatio_Balanced { 1.7f };
    CustomOptional<float> QualityRatio_Performance { 2.0f };
    CustomOptional<float> QualityRatio_UltraPerformance { 3.0f };

    // ProcessFilter
    CustomOptional<std::wstring, NoDefault> TargetProcess;
    CustomOptional<std::wstring> ProcessExclusionList = {
        L"crashpad_handler.exe|crashreport.exe|crashreporter.exe|crs-handler.exe|crs-uploader.exe|crs-video.exe|"
        L"unitycrashhandler64.exe|idtechlauncher.exe|cefviewwing.exe|ace-setup64.exe|ace-service64.exe|"
        L"qtwebengineprocess.exe|platformprocess.exe|bugsplathd64.exe|bssndrpt64.exe|pspcsdkappmgr.exe|pspcsdkcore.exe|"
        L"pspcsdkstttts.exe|pspcsdktelemetry.exe|pspcsdkui.exe|pspcsdkupdatechecker.exe|pspcsdkvoicechat.exe|"
        L"pspcsdkwebview.exe|windhawk.exe|vscodium.exe|crash_reporter.exe|steamerrorreporter64.exe|crashreportclient."
        L"exe|edcefcrashpadprocess.exe|edcefrenderprocess.exe"
    };

    // Hotfixes
    CustomOptional<bool> CheckForUpdate { true };
    CustomOptional<bool, SoftDefault> DisableOverlays { false };

    CustomOptional<bool> SimulateWaitableObject { false };

    CustomOptional<float, NoDefault> MipmapBiasOverride; // disabled by default
    CustomOptional<bool> MipmapBiasFixedOverride { false };
    CustomOptional<bool> MipmapBiasScaleOverride { false };
    CustomOptional<bool> MipmapBiasOverrideAll { false };

    CustomOptional<int, NoDefault> AnisotropyOverride; // disabled by default
    CustomOptional<bool> OverrideShaderSampler { true };
    CustomOptional<bool> AnisotropyModifyComp { true };
    CustomOptional<bool> AnisotropyModifyMinMax { true };
    CustomOptional<bool> AnisotropySkipPointFilter { true };

    CustomOptional<int, NoDefault> RoundInternalResolution; // disabled by default

    CustomOptional<int, NoDefault> SkipFirstFrames; // disabled by default
    CustomOptional<bool> RestoreComputeSignature { false };
    CustomOptional<bool> RestoreGraphicSignature { false };
    CustomOptional<bool> ExtendedStateRestore { false };

    CustomOptional<bool> UsePrecompiledShaders { true };

    CustomOptional<bool> UseGenericAppIdWithDlss { false };
    CustomOptional<bool> PreferDedicatedGpu { true };
    CustomOptional<bool> PreferFirstDedicatedGpu { false };

    CustomOptional<int32_t, NoDefault> ColorResourceBarrier;    // disabled by default
    CustomOptional<int32_t, NoDefault> MVResourceBarrier;       // disabled by default
    CustomOptional<int32_t, NoDefault> DepthResourceBarrier;    // disabled by default
    CustomOptional<int32_t, NoDefault> ExposureResourceBarrier; // disabled by default
    CustomOptional<int32_t, NoDefault> MaskResourceBarrier;     // disabled by default
    CustomOptional<int32_t, NoDefault> OutputResourceBarrier;   // disabled by default

    CustomOptional<bool> CreateD3D12DeviceForLuma { false };

    // Upscalers
    CustomOptional<Upscaler, SoftDefault> Dx11Upscaler { Upscaler::FSR22 };
    CustomOptional<Upscaler, SoftDefault> Dx12Upscaler { Upscaler::XeSS };
    CustomOptional<Upscaler, SoftDefault> VulkanUpscaler { Upscaler::FSR22 };

    // Output Scaling
    CustomOptional<bool> OutputScalingEnabled { false };
    CustomOptional<float> OutputScalingMultiplier { 1.5f };
    CustomOptional<Scaler> OutputScalingDownscaler { Scaler::FSR1 };

    // FSR
    CustomOptional<bool> FsrDebugView { false };
    CustomOptional<int> FfxUpscalerIndex { 0 };
    CustomOptional<int> FfxFGIndex { 0 };
    CustomOptional<bool> FsrUseMaskForTransparency { true };
    CustomOptional<bool> FsrNonLinearColorSpace { false };
    CustomOptional<bool> FsrNonLinearSRGB { false };
    CustomOptional<bool> FsrNonLinearPQ { false };
    CustomOptional<bool> FsrAgilitySDKUpgrade { false };

    // These default values will be overwritten at upscaler init time with optimized values
    CustomOptional<float> FsrVelocity { 1.0f };
    CustomOptional<float> FsrReactiveScale { 1.0f };
    CustomOptional<float> FsrShadingScale { 1.0f };
    CustomOptional<float> FsrAccAddPerFrame { 0.333f };
    CustomOptional<float> FsrMinDisOccAcc { -0.333f };

    // FSR4
    CustomOptional<FSR4Support> Fsr4ForceModel { FSR4Support::None };
    CustomOptional<uint32_t, NoDefault> Fsr4Preset;
    CustomOptional<bool> Fsr4EnableWatermark { false };
    CustomOptional<bool> Fsr4DoNotLoadAmdxc64 { false };

    // FSR Common
    CustomOptional<float> FsrVerticalFov { 60.0f };
    CustomOptional<float> FsrHorizontalFov { 0.0f }; // off by default
    CustomOptional<float> FsrCameraNear { 0.1f };
    CustomOptional<float> FsrCameraFar { 100000.0f };
    CustomOptional<bool> FsrUseFsrInputValues { true };

    // dx11wdx12
    CustomOptional<bool> Dx11DelayedInit { false };
    CustomOptional<bool> DontUseNTShared { true };

    // vulkanwdx12
    CustomOptional<bool> VulkanUseCopyForInputs { false };
    CustomOptional<bool> VulkanUseCopyForOutput { false };

    // NVAPI Override
    CustomOptional<bool> DisableFlipMetering { false };

    // Spoofing
    CustomOptional<bool, SoftDefault> DxgiSpoofing { true };
    CustomOptional<bool> DxgiFactoryWrapping { false };
    CustomOptional<bool> StreamlineSpoofing { true };
    CustomOptional<std::string, NoDefault> DxgiBlacklist; // disabled by default
    CustomOptional<int, NoDefault> DxgiVRAM;              // disabled by default
    CustomOptional<bool> VulkanSpoofing { false };
    CustomOptional<bool> VulkanExtensionSpoofing { false };
    CustomOptional<int, NoDefault> VulkanVRAM; // disabled by default
    CustomOptional<bool> SpoofHAGS { false };
    CustomOptional<bool> SpoofFeatureLevel { false };
    CustomOptional<uint32_t> SpoofedVendorId { VendorId::Nvidia };
    CustomOptional<uint32_t> SpoofedDeviceId { 0x2684 };
    CustomOptional<uint32_t, NoDefault> TargetVendorId;
    CustomOptional<uint32_t, NoDefault> TargetDeviceId;
    CustomOptional<std::wstring> SpoofedGPUName { L"NVIDIA GeForce RTX 4090" };
    CustomOptional<bool> UESpoofIntelAtomics64 { false };
    CustomOptional<bool> SpoofRegistry { false };
    CustomOptional<bool> SpoofUser32 { false };
    CustomOptional<std::wstring> SpoofedDriver { L"32.0.15.9155" };

    // Plugins
    CustomOptional<std::wstring, NoDefault> PluginPath;
    CustomOptional<bool> LoadSpecialK { false };
    CustomOptional<bool> LoadReShade { false };
    CustomOptional<bool> LoadCustomAmdxc64OnRdna2 { false };
    CustomOptional<bool> LoadAsiPlugins { false };
    CustomOptional<int> LateAsiPluginsDelay { 30 };

    // Frame Generation
    CustomOptional<FGInput> FGInput { FGInput::NoFG };
    CustomOptional<FGOutput> FGOutput { FGOutput::NoFG };
    CustomOptional<FGNvngxReplacement> FGNvngxReplacement { FGNvngxReplacement::None };
    CustomOptional<bool> FGDrawUIOverFG { false };
    CustomOptional<bool> FGUIPremultipliedAlpha { true };
    CustomOptional<bool> FGDisableHudless { false };
    CustomOptional<bool> FGDisableUI { false };
    CustomOptional<bool> FGSkipReset { false };
    CustomOptional<int> FGAllowedFrameAhead { 1 };
    CustomOptional<bool> FGDepthValidNow { false };
    CustomOptional<bool> FGVelocityValidNow { false };
    CustomOptional<bool> FGHudlessValidNow { false };
    CustomOptional<bool> FGOnlyAcceptFirstHudless { false };
    CustomOptional<bool> FGPreserveSwapChain { true };
    CustomOptional<bool> FGSkipResizeBuffers { false };
    CustomOptional<bool> FGModifyBufferState { false };
    CustomOptional<bool> FGModifySCIndex { false };
    CustomOptional<float> FGHudCutoff { 0.0f };
    CustomOptional<FrameTimeSource> FTInput { FrameTimeSource::Input };

    // OptiFG
    CustomOptional<bool> FGEnabled { false };
    CustomOptional<bool> FGUseMutexForSwapchain { true };
    CustomOptional<bool> FGMakeMVCopy { true };
    CustomOptional<bool> FGMakeDepthCopy { true };
    CustomOptional<bool> FGResourceFlip { false };
    CustomOptional<bool> FGResourceFlipOffset { false };
    CustomOptional<bool> FGAlwaysCaptureFSRFGSwapchain { false };

    CustomOptional<int, NoDefault> FGRectLeft;
    CustomOptional<int, NoDefault> FGRectTop;
    CustomOptional<int, NoDefault> FGRectWidth;
    CustomOptional<int, NoDefault> FGRectHeight;

    // OptiFG - Hudfix
    CustomOptional<bool> FGDisableHUDFix { false };
    CustomOptional<bool> FGHUDFix { false };
    CustomOptional<int> FGHUDLimit { 1 };
    CustomOptional<bool> FGHUDFixExtended { false };
    CustomOptional<bool> FGImmediateCapture { false };
    CustomOptional<bool> FGDontUseSwapchainBuffers { false };
    CustomOptional<bool> FGRelaxedResolutionCheck { false };
    CustomOptional<bool> FGHudfixDisableRTV { false };
    CustomOptional<bool> FGHudfixDisableSRV { false };
    CustomOptional<bool> FGHudfixDisableUAV { false };
    CustomOptional<bool> FGHudfixDisableOM { false };
    CustomOptional<bool> FGHudfixDisableDispatch { false };
    CustomOptional<bool> FGHudfixDisableDI { false };
    CustomOptional<bool> FGHudfixDisableDII { false };
    CustomOptional<bool> FGHudfixDisableSCR { true };
    CustomOptional<bool> FGHudfixDisableSGR { true };

    // OptiFG - Resource Tracking
    CustomOptional<bool> FGAlwaysTrackHeaps { false };
    CustomOptional<bool> FGResourceBlocking { false };
    CustomOptional<bool> FGUseShards { false };

    // OptiFG - DLSS-D Depth scale
    CustomOptional<bool> FGEnableDepthScale { false };
    CustomOptional<float> FGDepthScaleMax { 10000.0f };

    // FSR-FG
    CustomOptional<bool> FGDebugView { false };
    CustomOptional<bool> FGDebugResetLines { false };
    CustomOptional<bool> FGDebugTearLines { false };
    CustomOptional<bool> FGDebugPacingLines { false };
    CustomOptional<bool> FGAsync { false };
    CustomOptional<bool> FGFramePacingTuning { true };
    CustomOptional<float> FGFPTSafetyMarginInMs { 0.01f };
    CustomOptional<float> FGFPTVarianceFactor { 0.3f };
    CustomOptional<bool> FGFPTAllowHybridSpin { false };
    CustomOptional<int> FGFPTHybridSpinTime { 2 };
    CustomOptional<bool> FGFPTAllowWaitForSingleObjectOnFence { false };

    CustomOptional<bool> FSRFGSkipConfigForHudless { false };
    CustomOptional<bool> FSRFGSkipDispatchForHudless { false };
    CustomOptional<bool> FSRFGEnableWatermark { false };

    // XeFG
    CustomOptional<bool> FGXeFGIgnoreInitChecks { false };
    CustomOptional<int> FGXeFGInterpolationCount { 1 };
    CustomOptional<bool> FGXeFGUIComposition { false };
    CustomOptional<bool> FGXeFGDepthInverted { true };
    CustomOptional<bool> FGXeFGJitteredMV { false };
    CustomOptional<bool> FGXeFGHighResMV { false };
    CustomOptional<bool> FGXeFGDebugView { false };
    CustomOptional<bool> FGXeFGForceBorderless { false };

    // DLSSG
    CustomOptional<int> FGDLSSGInterpolationCount { 1 }; // For Opti's own SL instance
    CustomOptional<bool> FGDLSSGUseGamesReflexMarkers { true };
    CustomOptional<int, NoDefault>
        FGDLSSGOverrideInterpolationCount; // For overriding game's value sent to SL, could be Nvngx FG, could be noFG
                                           // but someone just uses real DLSSG
    CustomOptional<bool> FGDLSSGOverrideForceDMFG { false };   // Overrides game's DLSSG mode to Dynamic
    CustomOptional<bool> FGDLSSGForceDMFG { false };           // Overrides Opti's DLSSG mode to Dynamic
    CustomOptional<float> FGDLSSGFramerateTargetDMFG { 0.0f }; // 0.0 means auto-detects the display refresh rate

    // As per
    // https://github.com/artur-graniszewski/dlss-enabler-main/blob/a92464d468eb0d91ae17befa66c6bf6229f20b9f/Utils/DlssgProxy.cpp#L1033
    CustomOptional<uint32_t> NvngxFGDispatchFlags { 0x10000000 }; // IGNORE_UI_TEXTURE
    CustomOptional<bool> NvngxFGShowDebug { false };
    CustomOptional<bool> NvngxFGDisableHudless { false };

    // fakenvapi
    CustomOptional<bool> UseFakenvapi { true };
    CustomOptional<bool> ForceXeLL { false };
    CustomOptional<bool> FN_ForceLatencyFlex { false };
    CustomOptional<LFXMode> FN_LatencyFlexMode { LFXMode::Conservative };
    CustomOptional<ForceReflex> FN_ForceReflex { ForceReflex::InGame };
    CustomOptional<LowLatencyInput> LowLatencyInput { LowLatencyInput::Auto }; // TODO: no reading/saving to config
    CustomOptional<LowLatencyMode> LowLatencyOutput { LowLatencyMode::Auto };

    // Inputs
    CustomOptional<bool> EnableDlssInputs { true };
    CustomOptional<bool> EnableXeSSInputs { true };
    CustomOptional<bool> UseFsr2Inputs { true };
    CustomOptional<bool> UseFsr2Dx11Inputs { false };
    CustomOptional<bool> UseFsr2VulkanInputs { false };
    CustomOptional<bool> Fsr2Pattern { false };
    CustomOptional<bool> UseFsr3Inputs { true };
    CustomOptional<bool> Fsr3Pattern { false };
    CustomOptional<bool> UseFfxInputs { true };
    CustomOptional<bool> EnableHotSwapping { false };
    CustomOptional<bool> EnableFsr2Inputs { true };
    CustomOptional<bool> EnableFsr3Inputs { true };
    CustomOptional<bool> EnableFfxInputs { true };

    // Framerate
    CustomOptional<float> FramerateLimit { 0.0f };

    // HDR
    CustomOptional<bool> ForceHDR { false };
    CustomOptional<bool> UseHDR10 { false };
    CustomOptional<bool> SkipColorSpace { false };

    // V-Sync
    CustomOptional<bool> OverrideVsync { false };
    CustomOptional<bool, NoDefault> ForceVsync;
    CustomOptional<UINT> VsyncInterval { 0 };

    // Old configs for compat reasons
    CustomOptional<bool, NoDefault> _DONTUSE_Fsr4ForceEnableInt8;

    bool LoadFromPath(const wchar_t* InPath);
    bool SaveIni();
    bool SaveXeFG();

    void CheckUpscalerFiles();

    std::vector<std::string> GetConfigLog();

    static Config* Instance();

  private:
    inline static Config* _config;
    inline static std::vector<std::string> _log;

    std::filesystem::path absoluteFileName;
    std::wstring fileName = L"OptiScaler.ini";

    bool Reload(std::filesystem::path iniPath);

    std::optional<std::string> readString(std::string section, std::string key, bool lowercase = false);
    std::optional<std::wstring> readWString(std::string section, std::string key, bool lowercase = false);
    std::optional<float> readFloat(std::string section, std::string key);
    std::optional<int> readInt(std::string section, std::string key);
    std::optional<uint32_t> readUInt(std::string section, std::string key);
    std::optional<bool> readBool(std::string section, std::string key);

    template <typename Enum> std::optional<Enum> readEnum(std::string section, std::string key);
};
