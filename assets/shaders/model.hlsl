struct VertexInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

cbuffer FrameData : register(b0) {
    float4x4 viewProjection;
};

VertexOutput mainVS(VertexInput input) {
    VertexOutput output;
    output.position = mul(viewProjection, float4(input.position, 1.0));
    output.uv = input.uv;
    return output;
}

float4 mainPS(VertexOutput input) : SV_Target0 {
    return float4(1.0, 1.0, 1.0, 1.0);
}

