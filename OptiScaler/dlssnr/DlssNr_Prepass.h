#pragma once

// A second DLSS, run for no reason but to hand the model a clean picture.
//
// Running Neural Rendering before the upscale is the only placement that is affordable: the model's
// cost is the frame's area, so at render resolution it is a quarter of what it is at the display's.
// What it costs in return is quality, and the reason is not resolution -- the same model resolution
// after the upscale looks better. It is that the frame before the upscale is RAW: jittered, aliased,
// one sample per pixel. NVIDIA's own addon author named it exactly when asked -- "jitter would mess
// it up. need a way to resolve jitter" -- and the frame-averaging accumulation built for that only
// half works, because averaging frames is a poor imitation of what a temporal upscaler does.
//
// So use the real thing. DLSS at a 1:1 ratio -- DLAA -- takes a jittered, aliased render-resolution
// frame and returns a resolved one the same size: the jitter accumulated out, the aliasing gone, the
// history rectified against motion. That is precisely the picture the model is missing, produced by
// the one piece of software in the process that is actually good at producing it.
//
// The output of this is NOT what the player sees -- but that took a change downstream to be true, and
// it is worth writing down why.
//
// The composition is NOT a transfer by default. It hands back the model's PICTURE with its luminance
// re-anchored to the frame, not an edit added onto the frame. Under that arithmetic everything this
// pass does to the model's input arrives in the player's frame and goes on to the game's upscaler --
// a pass meant to clean the model's input would instead be replacing the frame with its own
// reconstruction. That is exactly what the first version of this did.
//
// The resolve is now told when the model's input was substituted (DlssNrConstants::InputSubstituted),
// and on that flag it rebuilds the frame's own proxy and carries only the model's DIFFERENCE onto it.
// THEN this cancels: it is present in the model's answer and in the picture that answer is measured
// against, so it subtracts out. With that in place the pass has one job, to give the model something
// clean to reason about, and it cannot damage the frame even if it does that job badly.
//
// The one path that is deliberately not covered is reversible "replace" (modes 2 and 4), which exists
// to show the model's raw answer with no composition at all. With this running, that view shows the
// model's answer on a DLSS-resolved frame -- still a true picture of what the model returned, but no
// longer a picture of what the model did to the GAME'S frame.
//
// What it costs is one DLAA evaluate at render resolution, which is a fraction of what the model
// itself costs at the same size.
//
// This drives the driver's own nvngx directly, the way DlssNr_Proxy does for the model, rather than
// going through OptiScaler's upscaler classes -- those are built around the game's single upscaler
// instance and carry output scaling, sharpening and hudfix with them. This wants none of that.

#include <d3d12.h>

#include <shaders/dlssnr/DlssNr_Common.h>

namespace DlssNr
{
namespace Prepass
{
// Whether the driver's nvngx is up and exports what this needs. False on a machine with no DLSS.
bool Available();

// Clean `in` into `out`, both width x height, using the game's own guides.
//
// Returns true only when `out` holds a resolved frame. False means nothing was written and the
// caller should carry on with the picture it already had -- a build frame, a refusal, a machine
// without DLSS. There is no half state: the pass either produces a frame or it does not.
//
// `createFlags` is the game's own DLSS_Feature_Create_Flags, read from the parameter block this
// frame arrived on; only the bits that describe the GAME'S data are taken from it.
//
// `in` and `out` must be in NON_PIXEL_SHADER_RESOURCE and UNORDERED_ACCESS respectively, which is
// where the surrounding pass already keeps them.
bool Run(ID3D12GraphicsCommandList* cmdList, ID3D12Device* device, ID3D12Resource* in,
         ID3D12Resource* depth, ID3D12Resource* motion, ID3D12Resource* out, unsigned int width,
         unsigned int height, unsigned int guideWidth, unsigned int guideHeight,
         const DlssNrFrameInfo& frame, unsigned int createFlags, bool reset);

// Hand the feature back. Safe to call when nothing was built.
void Release();

// What the last create answered, for the menu to show without asking anyone to read a log.
struct Status
{
    bool built = false;
    bool refused = false;
    unsigned int lastResult = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    const char* why = "";
};

Status State();
} // namespace Prepass
} // namespace DlssNr
