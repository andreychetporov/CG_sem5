struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

cbuffer SkyboxConstants : register(b0)
{
    float4x4 inverseViewProjection;
    float3 cameraPosition;
    float padding;
};

TextureCube<float4> environmentMap : register(t0);
SamplerState environmentSampler : register(s0);

VertexOutput VSMain(uint vertexId : SV_VertexID)
{
    static const float2 positions[4] = {
        float2(-1.0f, -1.0f), float2(-1.0f, 1.0f),
        float2(1.0f, -1.0f), float2(1.0f, 1.0f)
    };
    VertexOutput output;
    output.position = float4(positions[vertexId], 0.0f, 1.0f);
    output.uv = positions[vertexId] * float2(0.5f, -0.5f) + 0.5f;
    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float2 ndc = input.uv * 2.0f - 1.0f;
    float4 farPoint = mul(float4(ndc, 1.0f, 1.0f), inverseViewProjection);
    float3 viewDirection = normalize(farPoint.xyz / farPoint.w - cameraPosition);
    // DDS cubemap uses the opposite vertical convention to the screen-space ray.
    viewDirection.y = -viewDirection.y;
    return float4(environmentMap.SampleLevel(environmentSampler, viewDirection, 0).rgb, 1.0f);
}
