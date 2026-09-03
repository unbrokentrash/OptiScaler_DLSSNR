#pragma once

// DLSS 5 Neural Rendering for OptiScaler.
//
// Two halves, and only one of them is a shader.
//
// The model is an NGX feature -- created and evaluated rather than dispatched -- and it lives here,
// under OptiScaler/dlssnr/. The composition pass, which builds the proxy the model is shown and
// transfers its answer back onto the frame, is an ordinary compute shader and lives with the others
// under OptiScaler/shaders/dlssnr/.
//
// The two are named a letter apart, so which is which:
//   dlssnr/DlssNrFeature_Dx12    the model, as an NGX feature
//   shaders/dlssnr/DlssNr_Dx12   the composition pass, as a Shader_Dx12
//
// On Direct3D 12 the pass runs on one side of the upscaler or the other -- DlssNrBeforeUpscale
// decides -- and both sides are the same pass over a different frame. Before, the model is shown the
// upscaler's input at render resolution and the parameter block is pointed at the edited copy for the
// evaluate; after, it is shown the finished frame at display resolution and edits it in place.
//
// Call sites, for the record:
//   inputs/NVNGX_DLSS_Dx12.cpp        both sides, for native DLSS and for OptiScaler's own upscalers
//   upscalers/IFeature_Dx11wDx12.cpp  both sides, inside the D3D11-on-D3D12 bridge
//   upscalers/IFeature_VkwDx12.cpp    both sides, inside the Vulkan-on-D3D12 bridge
//   inputs/NVNGX_DLSS_Vk.cpp          the native Vulkan path, which runs after the upscale only
//   menu/menu_common.cpp              the settings panel

#include "DlssNrFeature_Dx12.h"
