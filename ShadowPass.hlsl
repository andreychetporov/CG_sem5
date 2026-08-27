cbuffer ShadowConstants : register(b0)
{
    float4x4 worldLightViewProj;
}

struct VertexInput
{
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float2 texCoord : TEXCOORD0;
    float3 tangent : TANGENT0;
    float tangentW : TANGENT1;
};

float4 VSMain(VertexInput input) : SV_POSITION
{
    return mul(float4(input.position, 1.0f), worldLightViewProj);
}
