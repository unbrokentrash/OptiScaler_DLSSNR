#include "pch.h"
#include "DlssNr_Prepass.h"

#include <Config.h>
#include <Logger.h>
#include <NVNGX_Parameter.h>
#include <State.h>
#include <proxies/NVNGX_Proxy.h>

namespace
{
struct PrepassState
{
    // Our own parameter block, not the game's and not the driver's capability block.
    //
    // The game's is being written by the game every frame and read by its own upscaler; the driver's
    // capability block is shared with whatever else is running and lays its setters out in an order
    // the SDK header does not describe (which is the whole story of DlssNr_Proxy's vtable hunting).
    // OptiScaler's own implementation is an ordinary typed block, and the driver accepts one -- that
    // is exactly what it is handed when OptiScaler drives DLSS for the game.
    NVNGX_Parameters* params = nullptr;

    NVSDK_NGX_Handle handle {};
    NVSDK_NGX_Handle* feature = nullptr;

    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int builtFlags = 0;

    // A refusal is remembered rather than retried. Asking again every frame is how the driver's
    // latches get exhausted, and the answer will not change between two frames.
    bool refused = false;
    unsigned int lastResult = 0;
    const char* why = "";

    // Set on the frame the feature is created, so that frame does not also evaluate it. Creating and
    // evaluating a feature on one command list is the dice roll that hangs the GPU, and this pass
    // learned that the expensive way on the model's own feature.
    bool justBuilt = false;
};

PrepassState g_pre;

// The bits we send, and where each one comes from.
//
// Not simply the game's flags forwarded. Half of them describe the GAME'S data -- which way depth
// runs, whether its vectors carry the jitter -- and those are the game's to state. The other half
// describe what the picture IS, and this picture is not the game's frame: it is the display-referred
// proxy the encode built, bounded in [0,1] and already tone mapped. Telling DLSS that is HDR would
// have it undo a curve nobody applied.
unsigned int FlagsFor(const DlssNrFrameInfo& frame, unsigned int gameFlags)
{
    unsigned int flags = 0;

    // The game's, about the game's guides.
    if (frame.DepthInverted)
        flags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;

    if ((gameFlags & NVSDK_NGX_DLSS_Feature_Flags_MVJittered) != 0)
        flags |= NVSDK_NGX_DLSS_Feature_Flags_MVJittered;

    // Ours, about the picture.
    //
    // MVLowRes says the vectors are at the input raster rather than the output's. Here those are the
    // same size -- a 1:1 pass -- so the claim is true either way round, and this is the convention
    // every low-res-MV integration uses.
    flags |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;

    // Auto exposure, because the picture is bounded and there is no exposure to state. The game's
    // exposure describes the game's linear buffer, and this is not that buffer.
    flags |= NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;

    // No sharpening. DLSS's own sharpener is deprecated, and anything it added here would be read by
    // the composition as detail the model invented.
    return flags;
}

void ReleaseFeature()
{
    if (g_pre.feature != nullptr && NVNGXProxy::D3D12_ReleaseFeature() != nullptr)
        NVNGXProxy::D3D12_ReleaseFeature()(g_pre.feature);

    g_pre.feature = nullptr;
    g_pre.width = 0;
    g_pre.height = 0;
    g_pre.builtFlags = 0;
}
} // namespace

namespace DlssNr
{
namespace Prepass
{
bool Available()
{
    return NVNGXProxy::NVNGXModule() != nullptr && NVNGXProxy::D3D12_CreateFeature() != nullptr &&
           NVNGXProxy::D3D12_EvaluateFeature() != nullptr && NVNGXProxy::D3D12_ReleaseFeature() != nullptr;
}

void Release()
{
    ReleaseFeature();

    if (g_pre.params != nullptr)
    {
        delete g_pre.params;
        g_pre.params = nullptr;
    }

    g_pre.refused = false;
    g_pre.why = "";
}

Status State()
{
    Status s {};
    s.built = g_pre.feature != nullptr;
    s.refused = g_pre.refused;
    s.lastResult = g_pre.lastResult;
    s.width = g_pre.width;
    s.height = g_pre.height;
    s.why = g_pre.why;
    return s;
}

bool Run(ID3D12GraphicsCommandList* cmdList, ID3D12Device* device, ID3D12Resource* in,
         ID3D12Resource* depth, ID3D12Resource* motion, ID3D12Resource* out, unsigned int width,
         unsigned int height, unsigned int guideWidth, unsigned int guideHeight,
         const DlssNrFrameInfo& frame, unsigned int createFlags, bool reset)
{
    if (g_pre.refused || cmdList == nullptr || device == nullptr || in == nullptr || out == nullptr ||
        depth == nullptr || motion == nullptr || width == 0 || height == 0)
        return false;

    if (!Available())
    {
        // Not a refusal to remember: a game that has not touched DLSS yet may init it later, and this
        // costs three pointer comparisons a frame.
        g_pre.why = "the driver's nvngx is not loaded";
        return false;
    }

    const unsigned int wantFlags = FlagsFor(frame, createFlags);

    // A changed size or a changed claim about the data invalidates what was built for the old one.
    if (g_pre.feature != nullptr &&
        (g_pre.width != width || g_pre.height != height || g_pre.builtFlags != wantFlags))
    {
        LOG_INFO("DLSS-NR prepass: rebuilding, {}x{} flags {} -> {}x{} flags {}", g_pre.width,
                 g_pre.height, g_pre.builtFlags, width, height, wantFlags);
        ReleaseFeature();
    }

    if (g_pre.params == nullptr)
        g_pre.params = new NVNGX_Parameters(API::DX12, true);

    if (g_pre.params == nullptr)
    {
        g_pre.refused = true;
        g_pre.why = "no parameter block";
        return false;
    }

    NVNGX_Parameters& params = *g_pre.params;

    if (g_pre.feature == nullptr)
    {
        // The core has to be up before a feature can be asked for. Idempotent, and already done by
        // the game's own DLSS in most sessions -- but not in a game running FSR or XeSS through
        // OptiScaler, which is exactly a session where this is worth having.
        if (!NVNGXProxy::IsDx12Inited() && !NVNGXProxy::InitDx12(device))
        {
            g_pre.refused = true;
            g_pre.why = "the NGX core would not initialise";
            LOG_ERROR("DLSS-NR prepass: NGX would not initialise on this device");
            return false;
        }

        params.Set(NVSDK_NGX_Parameter_CreationNodeMask, 1u);
        params.Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1u);
        params.Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, wantFlags);

        // 1:1 in and out. This is DLAA -- DLSS with nothing to upscale -- which is the only ratio
        // that makes sense here: the model is meant to be shown the same picture it would have been
        // shown anyway, with the jitter and the aliasing taken out of it, not a smaller one.
        params.Set(NVSDK_NGX_Parameter_Width, width);
        params.Set(NVSDK_NGX_Parameter_Height, height);
        params.Set(NVSDK_NGX_Parameter_OutWidth, width);
        params.Set(NVSDK_NGX_Parameter_OutHeight, height);
        params.Set(NVSDK_NGX_Parameter_PerfQualityValue, (unsigned int) NVSDK_NGX_PerfQuality_Value_DLAA);

        g_pre.feature = &g_pre.handle;

        const auto created = NVNGXProxy::D3D12_CreateFeature()(cmdList, NVSDK_NGX_Feature_SuperSampling,
                                                              &params, &g_pre.feature);

        g_pre.lastResult = (unsigned int) created;

        if (created != NVSDK_NGX_Result_Success || g_pre.feature == nullptr)
        {
            g_pre.refused = true;
            g_pre.feature = nullptr;
            g_pre.why = "the driver refused a 1:1 DLSS feature";
            LOG_ERROR("DLSS-NR prepass: CreateFeature(SuperSampling) at {}x{} failed 0x{:X}", width,
                      height, (unsigned int) created);
            return false;
        }

        g_pre.width = width;
        g_pre.height = height;
        g_pre.builtFlags = wantFlags;
        g_pre.why = "";
        g_pre.justBuilt = true;

        LOG_INFO("DLSS-NR prepass: DLAA up at {}x{} (flags {}), cleaning the model's input", width,
                 height, wantFlags);

        // Nothing evaluates on a creation frame. One frame on the picture the pass would have used
        // anyway is invisible; a hung GPU is not.
        return false;
    }

    if (g_pre.justBuilt)
    {
        // The create went out on the previous command list, which the game has since submitted.
        g_pre.justBuilt = false;
    }

    params.Set(NVSDK_NGX_Parameter_Color, in);
    params.Set(NVSDK_NGX_Parameter_Output, out);
    params.Set(NVSDK_NGX_Parameter_Depth, depth);
    params.Set(NVSDK_NGX_Parameter_MotionVectors, motion);

    // No sharpening, ever. See FlagsFor.
    params.Set(NVSDK_NGX_Parameter_Sharpness, 0.0f);

    // The jitter, which is the entire point of this pass existing.
    params.Set(NVSDK_NGX_Parameter_Jitter_Offset_X, frame.JitterX);
    params.Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, frame.JitterY);

    // The game's own vector encoding, passed through rather than derived. Deriving it is the bug this
    // project has already made once, on the model's own vectors.
    params.Set(NVSDK_NGX_Parameter_MV_Scale_X, frame.MvScaleX);
    params.Set(NVSDK_NGX_Parameter_MV_Scale_Y, frame.MvScaleY);

    params.Set(NVSDK_NGX_Parameter_Reset, reset ? 1u : 0u);

    // The colour is ours and wholly valid; the guides are the game's and may be a corner of a larger
    // texture, which is what the subrect dimensions are for.
    params.Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, 0u);
    params.Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y, 0u);
    params.Set(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X, 0u);
    params.Set(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y, 0u);
    params.Set(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, 0u);
    params.Set(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y, 0u);
    params.Set(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X, 0u);
    params.Set(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y, 0u);

    // How much of the input raster is real. Ours is all of it; the guides carry their own size, and
    // when the game supplies guides at a different resolution DLSS scales from this.
    params.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, width);
    params.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, height);

    // Unused, but read: guideWidth and guideHeight describe the game's own rendered area, and are
    // logged once against the colour size so a mismatch is visible rather than inferred.
    {
        static bool saidGuides = false;

        if (!saidGuides)
        {
            saidGuides = true;
            LOG_INFO("DLSS-NR prepass: colour {}x{}, guides {}x{}, mv scale {:.3f}/{:.3f}", width,
                     height, guideWidth, guideHeight, frame.MvScaleX, frame.MvScaleY);
        }
    }

    const auto result = NVNGXProxy::D3D12_EvaluateFeature()(cmdList, g_pre.feature, &params, nullptr);

    g_pre.lastResult = (unsigned int) result;

    if (result != NVSDK_NGX_Result_Success)
    {
        // One failed evaluate is not a reason to lose the pass -- the caller simply keeps the picture
        // it had -- but a feature that has started refusing will keep refusing, so stop asking.
        g_pre.refused = true;
        g_pre.why = "the 1:1 DLSS evaluate was rejected";
        LOG_ERROR("DLSS-NR prepass: EvaluateFeature failed 0x{:X}, standing down for this session",
                  (unsigned int) result);
        return false;
    }

    return true;
}
} // namespace Prepass
} // namespace DlssNr
