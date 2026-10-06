// SPDX-FileCopyrightText: 2026 The AriadShot Authors
// SPDX-License-Identifier: GPL-3.0-only

#version 440

layout(location = 0) in vec2 v_texCoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D u_workingTexture;

// IEC 61966-2-1 linear to sRGB conversion (docs/spec/03-rendering-contracts.md §3.3)
vec3 linearToSrgb(vec3 c)
{
    bvec3 cutoff = lessThanEqual(c, vec3(0.0031308));
    vec3 higher = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    vec3 lower = c * 12.92;
    return mix(higher, lower, cutoff);
}

void main()
{
    vec4 linearColor = texture(u_workingTexture, v_texCoord);
    vec3 clampedLinear = clamp(linearColor.rgb, 0.0, 1.0);
    vec3 srgbRgb = linearToSrgb(clampedLinear);
    fragColor = vec4(srgbRgb, linearColor.a);
}
