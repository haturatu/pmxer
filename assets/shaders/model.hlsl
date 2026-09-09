struct VertexInput {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float2 additionalUv1 : TEXCOORD3;
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 additionalUv1 : TEXCOORD2;
    float3 worldPosition : TEXCOORD3;
};

cbuffer FrameData : register(b0, space1) {
    row_major float4x4 viewProjection;
    float4 edgeParameters;
};

cbuffer ViewportData : register(b0, space3) {
    float4 cameraPosition;
    float4 lightDirection;
    float4 viewportLighting;
    float4 previewStrength;
};

cbuffer MaterialData : register(b1, space3) {
    float4 diffuse;
    float4 ambientShininess;
    float4 specular;
    float4 textureMultiply;
    float4 textureAdd;
    float4 sphereMultiply;
    float4 sphereAdd;
    float4 toonMultiply;
    float4 toonAdd;
    float4 edgeColor;
    float4 materialModes;
    float4 textureFlags;
};

Texture2D baseTexture : register(t0, space2);
Texture2D sphereTexture : register(t1, space2);
Texture2D toonTexture : register(t2, space2);
SamplerState baseSampler : register(s0, space2);
SamplerState sphereSampler : register(s1, space2);
SamplerState toonSampler : register(s2, space2);

VertexOutput mainVS(VertexInput input) {
    VertexOutput output;
    const float3 position = input.position + normalize(input.normal) * edgeParameters.x * 0.01;
    output.position = mul(viewProjection, float4(position, 1.0));
    output.uv = input.uv;
    output.normal = input.normal;
    output.additionalUv1 = input.additionalUv1;
    output.worldPosition = input.position;
    return output;
}

float4 mainPS(VertexOutput input) : SV_Target0 {
    if (materialModes.z > 0.5)
        return edgeColor;
    const float3 normal = normalize(input.normal);
    const float lightIntensity = max(viewportLighting.x, 0.0);
    const float ambientIntensity = max(viewportLighting.y, 0.0);
    const float shadingMode = viewportLighting.w;
    const float rawNdotL = clamp(dot(normal, normalize(lightDirection.xyz)), -1.0, 1.0);
    const float directLight = saturate(rawNdotL) * lightIntensity;
    const float4 textureColor = baseTexture.Sample(baseSampler, input.uv);
    float3 baseDiffuse = diffuse.rgb;
    const bool baseMissing = textureFlags.x > 0.5;
    if (baseMissing)
        baseDiffuse = lerp(baseDiffuse, float3(0.72, 0.74, 0.78), 0.70);
    const float3 baseSample = textureColor.rgb * textureMultiply.rgb + textureAdd.rgb;

    const float2 toonUv = float2(0.5, 0.5 - rawNdotL * 0.5);
    const float4 toonColor = toonTexture.Sample(toonSampler, toonUv);
    float3 toonFactor = toonColor.rgb * toonMultiply.rgb + toonAdd.rgb;
    if (baseMissing)
        toonFactor = lerp(float3(1.0, 1.0, 1.0), toonFactor, 0.40);

    float3 color;
    if (shadingMode > 1.5) {
        color = baseDiffuse * baseSample;
    } else if (shadingMode > 0.5) {
        const float diffuseLight = 0.25 + 0.75 * directLight;
        color = baseSample * saturate(baseDiffuse * diffuseLight +
                                      ambientShininess.rgb * ambientIntensity);
    } else {
        const float diffuseLight = 0.25 + 0.75 * directLight;
        color = baseSample * saturate(baseDiffuse * diffuseLight +
                                      ambientShininess.rgb * ambientIntensity);
    }

    if (shadingMode <= 1.5) {
        const float toonStrength = shadingMode < 0.5 ? 1.0 : saturate(previewStrength.x);
        color *= lerp(float3(1.0, 1.0, 1.0), toonFactor, toonStrength);
    }

    if (shadingMode <= 1.5 && materialModes.x > 0.5) {
        const float2 sphereUv = materialModes.x < 2.5
                                    ? normal.xy * 0.5 + 0.5
                                    : input.additionalUv1;
        const float4 sphereColor = sphereTexture.Sample(sphereSampler, sphereUv);
        const float3 sphere = sphereColor.rgb * sphereMultiply.rgb + sphereAdd.rgb;
        const float sphereStrength = shadingMode < 0.5 ? 1.0 : saturate(previewStrength.y);
        if (materialModes.x < 1.5)
            color *= lerp(float3(1.0, 1.0, 1.0), sphere, sphereStrength);
        else if (materialModes.x < 2.5)
            color += sphere * sphereStrength;
        else
            color *= lerp(float3(1.0, 1.0, 1.0), sphere, sphereStrength);
    }

    if (shadingMode <= 1.5) {
        const float3 viewDirection = normalize(cameraPosition.xyz - input.worldPosition);
        const float3 halfVector = normalize(normalize(lightDirection.xyz) + viewDirection);
        const float specularLight = rawNdotL > 0.0
                                        ? pow(saturate(dot(normal, halfVector)),
                                              max(ambientShininess.w, 1.0))
                                        : 0.0;
        const float specularStrength = shadingMode < 0.5 ? 1.0 : saturate(previewStrength.z);
        color += specular.rgb * specularLight * specularStrength;
    }

    color *= exp2(viewportLighting.z);

    color = lerp(color, float3(1.0, 0.62, 0.08),
                 saturate(materialModes.w));
    return float4(color,
                  saturate(diffuse.a * (textureColor.a * textureMultiply.a + textureAdd.a)));
}
