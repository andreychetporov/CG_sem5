struct BillboardVertex
{
    float3 center : POSITION;
    float2 size : TEXCOORD0;
};

struct BillboardOutput
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
};

struct PixelOutput
{
    float4 position : SV_TARGET0;
    float4 normal : SV_TARGET1;
    float4 albedo : SV_TARGET2;
};

cbuffer BillboardConstants : register(b0)
{
    float4x4 viewProj;
    float4x4 view;
    float3 cameraPos;
    float padding;
    float3 cameraRight;
    float padding2;
    float3 cameraUp;
    float padding3;
}

Texture2D billboardTexture : register(t0);
SamplerState billboardSampler : register(s0);

BillboardOutput VSMain(BillboardVertex input, uint vertexID : SV_VertexID)
{
    BillboardOutput output;

    float2 offsets[6] = {
        float2(-0.5f, -0.5f),
        float2(0.5f, -0.5f),
        float2(-0.5f, 0.5f),
        float2(-0.5f, 0.5f),
        float2(0.5f, -0.5f),
        float2(0.5f, 0.5f)
    };

    float2 offset = offsets[vertexID % 6];

    float3 worldPos = input.center
        + cameraRight * offset.x * input.size.x
        + cameraUp * offset.y * input.size.y;

    output.position = mul(float4(worldPos, 1.0f), viewProj);
    output.worldPos = worldPos;

    output.texCoord = offset + 0.5f;

    return output;
}

PixelOutput PSMain(BillboardOutput input)
{
    PixelOutput output;

    float4 texColor = billboardTexture.Sample(billboardSampler, input.texCoord);

    if (texColor.a < 0.1f)
        discard;

    output.position = float4(input.worldPos, 1.0f);

    float3 viewDir = normalize(cameraPos - input.worldPos);
    output.normal = float4(viewDir, 1.0f);

    output.albedo = texColor;

    return output;
}
