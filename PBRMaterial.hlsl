cbuffer ObjectConstants : register(b0)
{
    float4x4 worldViewProj;
    float4x4 world;
    float4 color;
}

Texture2D albedoMap : register(t0);
Texture2D metallicMap : register(t1);
Texture2D normalMap : register(t2);
Texture2D roughnessMap : register(t3);
SamplerState materialSampler : register(s0);

struct VertexInput
{
    float3 position : POSITION;
    float4 color : COLOR;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : POSITION;
    float3 worldNormal : NORMAL;
    float2 texCoord : TEXCOORD;
};

struct GBufferOutput
{
    float4 position : SV_TARGET0;
    float4 normal : SV_TARGET1;
    float4 albedo : SV_TARGET2;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(float4(input.position, 1.0f), worldViewProj);
    output.worldPosition = mul(float4(input.position, 1.0f), world).xyz;
    output.worldNormal = normalize(mul(input.normal, (float3x3)world));
    output.texCoord = input.texCoord;
    return output;
}

GBufferOutput PSMain(VertexOutput input)
{
    GBufferOutput output;
    float3 dpdx = ddx(input.worldPosition);
    float3 dpdy = ddy(input.worldPosition);
    float2 duvdx = ddx(input.texCoord);
    float2 duvdy = ddy(input.texCoord);
    float3 tangent = normalize(dpdx * duvdy.y - dpdy * duvdx.y);
    float3 bitangent = normalize(-dpdx * duvdy.x + dpdy * duvdx.x);
    float3 tangentNormal = normalMap.Sample(materialSampler, input.texCoord).xyz * 2.0f - 1.0f;
    float3 normal = normalize(tangentNormal.x * tangent + tangentNormal.y * bitangent +
        tangentNormal.z * normalize(input.worldNormal));

    float3 albedo = pow(max(albedoMap.Sample(materialSampler, input.texCoord).rgb, 0.0f), 2.2f);
    float metallic = saturate(metallicMap.Sample(materialSampler, input.texCoord).r);
    float roughness = clamp(roughnessMap.Sample(materialSampler, input.texCoord).r, 0.04f, 1.0f);

    // The unused G-buffer components carry the PBR material parameters.
    // Albedo alpha 0.5 distinguishes this material from scene geometry and particles.
    output.position = float4(input.worldPosition, metallic);
    output.normal = float4(normal, roughness);
    output.albedo = float4(albedo, 0.5f);
    return output;
}
