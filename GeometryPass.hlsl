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
    float3 worldPos : TEXCOORD0;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD1;
};

struct PixelOutput
{
    float4 position : SV_TARGET0;
    float4 normal : SV_TARGET1;
    float4 albedo : SV_TARGET2;
};

cbuffer PerObjectCB : register(b0)
{
    float4x4 worldViewProj;
    float4x4 world;
    float4 lightDir;
    float4 lightColor;
    float4 ambientColor;
    float2 uvScale;
    float2 uvOffset;
}

Texture2D diffuseTexture : register(t0);
SamplerState diffuseSampler : register(s0);

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;

    output.position = mul(float4(input.position, 1.0f), worldViewProj);
    output.worldPos = mul(float4(input.position, 1.0f), world).xyz;
    output.normal = normalize(mul(input.normal, (float3x3)world));
    output.texCoord = input.texCoord * uvScale + uvOffset;

    return output;
}

PixelOutput PSMain(VertexOutput input)
{
    PixelOutput output;

    output.position = float4(input.worldPos, 1.0f);
    output.normal = float4(normalize(input.normal), 1.0f);
    output.albedo = diffuseTexture.Sample(diffuseSampler, input.texCoord);

    return output;
}
