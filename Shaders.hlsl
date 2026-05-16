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
    float4 color : COLOR;
    float3 normal : NORMAL;
    float3 worldPos : TEXCOORD0;
    float2 texCoord : TEXCOORD1;
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
    output.normal = mul(input.normal, (float3x3)world);
    output.color = input.color;
    output.texCoord = input.texCoord * uvScale + uvOffset;

    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float3 normal = normalize(input.normal);
    float3 lightDirection = normalize(-lightDir.xyz);

    float diffuse = max(dot(normal, lightDirection), 0.0f);

    float3 ambient = ambientColor.rgb * ambientColor.a;
    float3 diffuseLight = lightColor.rgb * lightColor.a * diffuse;

    float4 texColor = diffuseTexture.Sample(diffuseSampler, input.texCoord);
    float3 finalColor = texColor.rgb * (ambient + diffuseLight);

    return float4(finalColor, texColor.a);
}