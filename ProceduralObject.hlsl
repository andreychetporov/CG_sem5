struct VertexInput
{
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float2 texCoord : TEXCOORD0;
    float3 tangent : TANGENT0;
    float tangentW : TANGENT1;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal : NORMAL;
    float4 color : COLOR;
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
    float4 color;
}

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;

    output.position = mul(float4(input.position, 1.0f), worldViewProj);
    output.worldPos = mul(float4(input.position, 1.0f), world).xyz;
    output.normal = normalize(mul(input.normal, (float3x3)world));
    output.color = color;

    return output;
}

PixelOutput PSMain(VertexOutput input)
{
    PixelOutput output;

    output.position = float4(input.worldPos, 1.0f);
    output.normal = float4(normalize(input.normal), 1.0f);

    // Простое освещение для отладки
    float3 lightDir = normalize(float3(0.3, -0.8, 0.5));
    float ndotl = max(0.2, dot(normalize(input.normal), -lightDir));
    output.albedo = float4(input.color.rgb * ndotl, 1.0f);

    return output;
}
