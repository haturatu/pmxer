struct VertexInput {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float3 normal : TEXCOORD1;
};

cbuffer FrameData : register(b0, space1) {
    row_major float4x4 viewProjection;
};

cbuffer MaterialData : register(b0, space3) {
    float4 diffuse;
};

Texture2D baseTexture : register(t0, space3);
SamplerState baseSampler : register(s0, space3);

VertexOutput mainVS(VertexInput input) {
    VertexOutput output;
    output.position = mul(viewProjection, float4(input.position, 1.0));
    output.uv = input.uv;
    output.normal = input.normal;
    return output;
}

float4 mainPS(VertexOutput input) : SV_Target0 {
    const float3 lightDirection = normalize(float3(-0.35, 0.75, 0.55));
    const float light = 0.25 + 0.75 * saturate(dot(normalize(input.normal), lightDirection));
    const float4 textureColor = baseTexture.Sample(baseSampler, input.uv);
    return float4(diffuse.rgb * textureColor.rgb * light, diffuse.a * textureColor.a);
}
