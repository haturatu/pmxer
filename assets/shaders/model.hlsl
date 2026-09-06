struct VertexInput {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

cbuffer FrameData : register(b0, space1) {
    row_major float4x4 viewProjection;
};

cbuffer MaterialData : register(b0, space3) {
    float4 diffuse;
};

VertexOutput mainVS(VertexInput input) {
    VertexOutput output;
    output.position = mul(viewProjection, float4(input.position, 1.0));
    output.uv = input.uv;
    return output;
}

float4 mainPS(VertexOutput input) : SV_Target0 {
    return diffuse;
}
