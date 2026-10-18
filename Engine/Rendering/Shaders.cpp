#include "Engine/Rendering/Shaders.hpp"

namespace ld::render {

const char* shaderSource() {
    return R"METAL(
#include <metal_stdlib>
using namespace metal;

struct FrameUniforms {
    float4x4 view;
    float4x4 projection;
    float4x4 viewInverse;
    float4x4 light;
    float4 sunDirection;
    float4 sunColor;
    float4 ambient;
    float4 fogColor;
    float4 shadowColor;
    float fogStart;
    float fogEnd;
    float fogExponent;
    float time;
    int shadowMode;
    float shadowTexel;
    float shadowRadius;
    float padding;
    float4 waterColor;
    float4 skyTop;
    float4 skyHorizon;
    float nearPlane;
    float farPlane;
    float waterFog;
    int underwater;
    int waterDepth;
    float pad2;
    float pad3;
    float pad4;
};

struct ObjectUniforms {
    float4x4 model;
    float4x4 normal;
    float4 diffuse;
    float4 multiply;
    float4 emission;
    int lighting;
    int terrainKind;
    int hasColors;
    int hasTexture;
    int doubleSided;
    int receivesShadow;
    int surface;
    int decal;
    int deep;
    float4 water;
};

struct Varyings {
    float4 position [[position]];
    float3 world;
    float3 viewPosition;
    float3 normal;
    float3 color;
    float2 uv;
    float4 lightPosition;
};

struct Background {
    float4 position [[position]];
    float t;
};

struct BackgroundUniforms {
    float4 top;
    float4 bottom;
    int gradient;
};

float3 decode(float3 c) {
    return select(pow((c + 0.055) / 1.055, float3(2.4)), c / 12.92, c <= 0.04045);
}

float3 encode(float3 c) {
    return select(1.055 * pow(max(c, float3(0.0031308)), float3(1.0 / 2.4)) - 0.055, c * 12.92, c <= 0.0031308);
}

float ld_hash(float2 p) { p = fract(p * float2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }

float ld_noise(texture2d<float> field, float2 p) {
    constexpr sampler tiled(filter::linear, mip_filter::linear, address::repeat);
    return field.sample(tiled, p * (1.0 / 32.0)).r;
}

float2 ld_slope(float2 p, float t, float footprint) {
    float2 d1 = float2(0.8, 0.6), d2 = float2(-0.6, 0.8), d3 = float2(0.31, -0.95);
    float a = cos(dot(p, d1) * 0.9 + t * 1.7) * 0.5 * 0.9 * saturate(1.0 - footprint * 0.9 * 0.6);
    float b = cos(dot(p, d2) * 1.7 + t * 2.3) * 0.3 * 1.7 * saturate(1.0 - footprint * 1.7 * 0.6);
    float c = cos(dot(p, d3) * 2.9 + t * 3.1) * 0.2 * 2.9 * saturate(1.0 - footprint * 2.9 * 0.6);
    return d1 * a + d2 * b + d3 * c;
}

float ld_linear(constant FrameUniforms& frame, float z) { return frame.nearPlane * frame.farPlane / (frame.farPlane - z * (frame.farPlane - frame.nearPlane)); }

vertex Varyings sceneVertex(uint vid [[vertex_id]],
                            const device packed_float3* positions [[buffer(0)]],
                            const device packed_float3* normals [[buffer(1)]],
                            const device packed_float3* colors [[buffer(2)]],
                            const device packed_float2* uvs [[buffer(3)]],
                            constant FrameUniforms& frame [[buffer(4)]],
                            constant ObjectUniforms& object [[buffer(5)]]) {
    float3 p = float3(positions[vid]);
    float3 n = float3(normals[vid]);
    float4 world = object.model * float4(p, 1.0);
    float4 view = frame.view * world;
    Varyings out;
    out.position = frame.projection * view;
    out.world = world.xyz;
    out.viewPosition = view.xyz;
    out.normal = normalize((object.normal * float4(n, 0.0)).xyz);
    out.color = object.hasColors ? float3(colors[vid]) : float3(1.0);
    out.uv = float2(uvs[vid]);
    out.lightPosition = frame.light * world;
    return out;
}

float shadowAmount(constant FrameUniforms& frame, depth2d<float> map, float4 lightPosition, float3 normal) {
    if (frame.shadowMode == 0) return 0.0;
    float3 p = lightPosition.xyz / lightPosition.w;
    float2 uv = float2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || p.z > 1.0) return 0.0;
    constexpr sampler compare(filter::linear, compare_func::less_equal, address::clamp_to_edge);
    float slope = 1.0 - abs(dot(normalize(normal), -frame.sunDirection.xyz));
    float bias = 0.0015 + 0.004 * slope;
    const float2 disk[16] = {float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725), float2(-0.094184101, -0.92938870), float2(0.34495938, 0.29387760),
                             float2(-0.91588581, 0.45771432), float2(-0.81544232, -0.87912464), float2(-0.38277543, 0.27676845), float2(0.97484398, 0.75648379),
                             float2(0.44323325, -0.97511554), float2(0.53742981, -0.47373420), float2(-0.26496911, -0.41893023), float2(0.79197514, 0.19090188),
                             float2(-0.24188840, 0.99706507), float2(-0.81409955, 0.91437590), float2(0.19984126, 0.78641367), float2(0.14383161, -0.14100790)};
    float lit = 0.0;
    for (int i = 0; i < 16; ++i) lit += map.sample_compare(compare, uv + disk[i] * frame.shadowTexel * frame.shadowRadius, p.z - bias);
    return 1.0 - lit / 16.0;
}

fragment float4 sceneFragment(Varyings in [[stage_in]], bool front [[front_facing]],
                              constant FrameUniforms& frame [[buffer(4)]],
                              constant ObjectUniforms& object [[buffer(5)]],
                              texture2d<float> noise [[texture(0)]],
                              depth2d<float> shadows [[texture(1)]],
                              texture2d<float> image [[texture(2)]],
                              depth2d<float> scene [[texture(3)]]) {
    float3 base = object.diffuse.rgb * in.color;
    float alpha = object.diffuse.a;
    if (object.hasTexture == 1 && object.decal == 1) {
        constexpr sampler edge(filter::linear, mip_filter::linear, address::clamp_to_zero);
        float4 texel = image.sample(edge, in.uv);
        base = mix(base, texel.rgb, texel.a);
    } else if (object.hasTexture == 1) {
        constexpr sampler smooth(filter::linear, mip_filter::linear, address::clamp_to_edge);
        float4 texel = image.sample(smooth, in.uv);
        base *= texel.rgb;
        alpha *= texel.a;
    }
    base *= object.multiply.rgb;
    float3 normal = normalize(in.normal);
    if (!front && object.doubleSided) normal = -normal;
    float3 viewNormal = normalize((frame.view * float4(normal, 0.0)).xyz);
    if (object.lighting == 2) {
        float3 world = in.world;
        float macro = ld_noise(noise, world.xz * 0.09) * 0.6 + ld_noise(noise, world.xz * 0.23) * 0.4;
        float distant = smoothstep(18.0, 70.0, length(in.viewPosition));
        float fine = mix(ld_noise(noise, world.xz * 2.7), 0.5, distant);
        float3 tone = float3((0.85 + 0.15 * macro) * (0.9 + 0.1 * fine));
        if (object.terrainKind == 1) {
            float tuft = ld_hash(floor(world.xz * 7.0));
            float blades = mix(0.9 + 0.1 * ld_noise(noise, float2(world.x * 14.0, world.z * 3.0)) + 0.08 * ld_noise(noise, float2(world.x * 3.0, world.z * 14.0)), 0.99, distant);
            float3 lush = mix(float3(0.78, 0.9, 0.66), float3(1.12, 1.08, 0.78), macro);
            tone = lush * blades * (tuft > 0.86 ? 1.1 : 1.0) * (0.9 + 0.1 * fine);
            float steep = smoothstep(0.78, 0.5, normal.y);
            tone = mix(tone, float3(1.05, 0.78, 0.55) * (0.85 + 0.15 * fine), steep);
        } else if (object.terrainKind == 2) {
            float strata = 0.82 + 0.18 * ld_noise(noise, float2(world.y * 1.6, (world.x + world.z) * 0.12));
            tone = float3(strata * (0.9 + 0.1 * fine) * (0.92 + 0.12 * macro));
        } else if (object.terrainKind == 3) {
            float ripple = 0.95 + 0.05 * sin(world.x * 2.2 + ld_noise(noise, world.xz * 0.6) * 5.0);
            tone = float3(ripple * (0.94 + 0.06 * fine) * (0.95 + 0.08 * macro));
        }
        base *= tone;
    } else if (object.lighting == 3) {
        float3 eye = frame.viewInverse[3].xyz;
        float3 toEye = normalize(eye - in.world);
        float3 tint = encode(object.diffuse.rgb);
        float distance = length(in.viewPosition);
        if (frame.underwater == 1) {
            float up = dot(-toEye, float3(0.0, 1.0, 0.0));
            float3 below = decode(mix(tint * 0.6, mix(tint, float3(1.0), 0.5), smoothstep(0.75, 1.0, up)));
            below = mix(below, frame.waterColor.rgb, 1.0 - exp(-distance / max(frame.waterFog, 0.01)));
            return float4(below, 0.85);
        }
        float3 surfaceNormal = normal;
        if (abs(normal.y) > 0.5) {
            float footprint = length(fwidth(in.world.xz));
            float2 slope = ld_slope(in.world.xz, frame.time * object.water.w / 10.0, footprint) * object.water.z;
            surfaceNormal = normalize(float3(-slope.x, 1.0, -slope.y));
        }
        float clarity = object.water.x;
        float reach = mix(2.0, 24.0, clarity);
        float thickness = 0.5;
        float shore = 1.0;
        float opacity = mix(0.85, 0.4, clarity);
        if (frame.waterDepth == 1 || object.deep == 1) {
            float d = object.deep == 1 ? 3.0 * reach : max(0.0, ld_linear(frame, scene.read(uint2(in.position.xy))) - ld_linear(frame, in.position.z));
            thickness = 1.0 - exp(-d / reach);
            shore = smoothstep(0.0, 0.35, d);
            opacity = mix(mix(0.7, 0.15, clarity), 1.0, thickness) * shore;
        }
        float3 water = decode(mix(mix(tint, float3(1.0), 0.12), tint * 0.5, thickness));
        float lit = frame.shadowMode == 1 ? 1.0 - frame.shadowColor.a * shadowAmount(frame, shadows, in.lightPosition, surfaceNormal) : 1.0;
        float3 color = water * (frame.ambient.rgb + frame.sunColor.rgb * max(0.0, dot(surfaceNormal, -frame.sunDirection.xyz)) * lit);
        float facing = clamp(dot(surfaceNormal, toEye), 0.0, 1.0);
        float reflection = object.water.y * (0.02 + 0.98 * pow(1.0 - facing, 5.0));
        float3 bounce = reflect(-toEye, surfaceNormal);
        float3 sky = mix(frame.skyHorizon.rgb, frame.skyTop.rgb, clamp(bounce.y, 0.0, 1.0));
        color = mix(color, sky, reflection);
        opacity = max(opacity, reflection * shore);
        float3 halfway = normalize(-frame.sunDirection.xyz + toEye);
        color += pow(max(0.0, dot(surfaceNormal, halfway)), 180.0) * frame.sunColor.rgb * lit;
        if (frame.fogEnd > frame.fogStart && frame.fogEnd > 0.0) {
            float f = pow(clamp((distance - frame.fogStart) / (frame.fogEnd - frame.fogStart), 0.0, 1.0), frame.fogExponent);
            color = mix(color, frame.fogColor.rgb, f);
        }
        return float4(color, opacity);
    }
    if (object.surface == 1) {
        float3 w = in.world;
        float grain = ld_noise(noise, float2(w.x * 0.7 + w.z * 0.45, w.y * 5.0));
        float rings = 0.5 + 0.5 * sin((w.x + w.z) * 3.4 + grain * 10.0);
        base *= mix(0.6, 1.08, rings) * (0.9 + 0.1 * ld_noise(noise, w.xz * 5.0 + w.y));
    } else if (object.surface == 2) {
        base *= 0.78;
    }
    float shadow = object.receivesShadow ? shadowAmount(frame, shadows, in.lightPosition, normal) : 0.0;
    float3 color = base;
    if (object.lighting != 1) {
        float ndl = max(0.0, dot(normal, -frame.sunDirection.xyz));
        float sun = frame.shadowMode == 1 ? 1.0 - frame.shadowColor.a * shadow : 1.0;
        color = base * (frame.ambient.rgb + frame.sunColor.rgb * ndl * sun);
    }
    if (object.surface >= 2 && object.lighting == 0) {
        float3 eye = normalize(-in.viewPosition);
        float3 sun = normalize((frame.view * float4(-frame.sunDirection.xyz, 0.0)).xyz);
        float3 halfway = normalize(sun + eye);
        float lit = frame.shadowMode == 1 ? 1.0 - frame.shadowColor.a * shadow : 1.0;
        float shine = pow(max(0.0, dot(viewNormal, halfway)), object.surface == 2 ? 40.0 : 90.0) * lit;
        float fresnel = pow(1.0 - clamp(dot(viewNormal, eye), 0.0, 1.0), 4.0);
        color += frame.sunColor.rgb * shine * (object.surface == 2 ? 0.7 : 0.9);
        color += (object.surface == 2 ? base * 0.35 : float3(0.18)) * fresnel;
        if (object.surface == 3) alpha = min(1.0, alpha + 0.5 * fresnel + shine);
    }
    color = min(color + object.emission.rgb, float3(1.0));
    if (frame.shadowMode == 2) color = mix(color, frame.shadowColor.rgb, frame.shadowColor.a * shadow);
    if (frame.fogEnd > frame.fogStart && frame.fogEnd > 0.0) {
        float f = pow(clamp((length(in.viewPosition) - frame.fogStart) / (frame.fogEnd - frame.fogStart), 0.0, 1.0), frame.fogExponent);
        color = mix(color, frame.fogColor.rgb, f);
    }
    if (frame.underwater == 1) color = mix(color, frame.waterColor.rgb, 1.0 - exp(-length(in.viewPosition) / max(frame.waterFog, 0.01)));
    return float4(color, alpha);
}

vertex float4 shadowVertex(uint vid [[vertex_id]],
                           const device packed_float3* positions [[buffer(0)]],
                           constant FrameUniforms& frame [[buffer(4)]],
                           constant ObjectUniforms& object [[buffer(5)]]) {
    return frame.light * (object.model * float4(float3(positions[vid]), 1.0));
}

vertex Background backgroundVertex(uint vid [[vertex_id]]) {
    float2 corners[3] = {float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0)};
    Background out;
    out.position = float4(corners[vid], 0.0, 1.0);
    out.t = corners[vid].y * 0.5 + 0.5;
    return out;
}

fragment float4 backgroundFragment(Background in [[stage_in]], constant BackgroundUniforms& uniforms [[buffer(0)]]) {
    float3 srgb = uniforms.gradient ? mix(uniforms.bottom.rgb, uniforms.top.rgb, clamp(in.t, 0.0, 1.0)) : uniforms.top.rgb;
    return float4(decode(srgb), 1.0);
}
)METAL";
}

}
