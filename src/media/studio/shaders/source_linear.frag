// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#version 440

layout(location = 0) in vec2 v_texCoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D u_sourceTexture;

// IEC 61966-2-1 sRGB to linear conversion (macshot/Capture/EffectsVideoCompositor.swift:160@b4d4f3a)
vec3 srgbToLinear(vec3 c)
{
    bvec3 cutoff = lessThanEqual(c, vec3(0.04045));
    vec3 higher = pow((c + vec3(0.055)) / 1.055, vec3(2.4));
    vec3 lower = c / 12.92;
    return mix(higher, lower, cutoff);
}

void main()
{
    vec4 srgb = texture(u_sourceTexture, v_texCoord);
    vec3 linearRgb = srgbToLinear(clamp(srgb.rgb, 0.0, 1.0));
    fragColor = vec4(linearRgb, srgb.a);
}
