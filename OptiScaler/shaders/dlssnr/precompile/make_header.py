# Turns a compiled shader into the byte-array header the pass includes.
#
# The composition shader ships precompiled, with no source fallback: the D3D12 path includes
# DlssNr_Shader.h and the Vulkan path DlssNr_Shader_Vk.h, and nothing in the build compiles the HLSL.
# That is deliberate -- it means a user who has never installed a shader compiler still gets the pass --
# but it left the repository with no way to regenerate either blob, so editing dlssnr.hlsl did nothing
# until someone reproduced the original compile by hand.
#
# This is that compile, written down. See dlssnr_shader.yml, which runs it on a Windows runner because
# DXIL has to be signed by dxil.dll and that only exists on Windows.
#
#   python make_header.py DlssNr_Shader.cso    DlssNr_cso   DlssNr_Shader.h
#   python make_header.py DlssNr_Shader_Vk.spv dlssnr_spv   DlssNr_Shader_Vk.h

import sys


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__ or "usage: make_header.py <binary> <symbol> <header>")
        return 2

    binary, symbol, header = sys.argv[1], sys.argv[2], sys.argv[3]

    with open(binary, "rb") as f:
        data = f.read()

    if not data:
        print(f"{binary} is empty")
        return 1

    lines = ["#pragma once", "", f"inline static const unsigned char {symbol}[] = {{"]

    # Twelve to a line with a trailing space, and none on the last, which is the shape the checked-in
    # headers already have. Matching it keeps the diff to the bytes that actually changed.
    for start in range(0, len(data), 12):
        chunk = data[start : start + 12]
        text = "    " + ", ".join(f"0x{b:02x}" for b in chunk)

        if start + 12 < len(data):
            text += ", "

        lines.append(text)

    lines.append("};")

    with open(header, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")

    print(f"{binary}: {len(data)} bytes -> {header} as {symbol}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
