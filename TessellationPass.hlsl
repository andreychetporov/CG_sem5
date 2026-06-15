struct VertexInput
{
    float3 position : POSITION;
    float4 color : COLOR;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
};

struct HullInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
};

struct HullConstantOutput
{
    float edges[3] : SV_TessFactor;
    float inside : SV_InsideTessFactor;
};

struct DomainOutput
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
    float3 cameraPosition;
    float tessellationFactor;
    float minTessDistance;
    float maxTessDistance;
    float minTessFactor;
    float maxTessFactor;
    float time;
    float waveAmplitude;
    float waveFrequency;
    float waveSpeed;
}

Texture2D diffuseTexture : register(t0);
Texture2D displacementTexture : register(t1);
Texture2D normalTexture : register(t2);
SamplerState textureSampler : register(s0);

HullInput VSMain(VertexInput input)
{
    HullInput output;
    output.position = input.position;
    output.normal = input.normal;
    output.texCoord = input.texCoord * uvScale + uvOffset;
    return output;
}

float CalculateTessellationFactor(float3 worldPos)
{
    float distance = length(cameraPosition - worldPos);
    float t = saturate((distance - minTessDistance) / (maxTessDistance - minTessDistance));
    return lerp(maxTessFactor, minTessFactor, t);
}

HullConstantOutput HSConstant(InputPatch<HullInput, 3> patch, uint patchID : SV_PrimitiveID)
{
    HullConstantOutput output;

    float3 worldPos0 = mul(float4(patch[0].position, 1.0f), world).xyz;
    float3 worldPos1 = mul(float4(patch[1].position, 1.0f), world).xyz;
    float3 worldPos2 = mul(float4(patch[2].position, 1.0f), world).xyz;

    float3 edgeCenter0 = (worldPos1 + worldPos2) * 0.5f;
    float3 edgeCenter1 = (worldPos2 + worldPos0) * 0.5f;
    float3 edgeCenter2 = (worldPos0 + worldPos1) * 0.5f;

    output.edges[0] = CalculateTessellationFactor(edgeCenter0);
    output.edges[1] = CalculateTessellationFactor(edgeCenter1);
    output.edges[2] = CalculateTessellationFactor(edgeCenter2);

    output.inside = (output.edges[0] + output.edges[1] + output.edges[2]) / 3.0f;

    return output;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("HSConstant")]
HullInput HSMain(InputPatch<HullInput, 3> patch, uint i : SV_OutputControlPointID, uint patchID : SV_PrimitiveID)
{
    return patch[i];
}

[domain("tri")]
DomainOutput DSMain(HullConstantOutput input, float3 barycentricCoords : SV_DomainLocation, const OutputPatch<HullInput, 3> patch)
{
    DomainOutput output;

    float3 position = patch[0].position * barycentricCoords.x +
                      patch[1].position * barycentricCoords.y +
                      patch[2].position * barycentricCoords.z;

    float3 normal = patch[0].normal * barycentricCoords.x +
                    patch[1].normal * barycentricCoords.y +
                    patch[2].normal * barycentricCoords.z;
    normal = normalize(normal);

    float2 texCoord = patch[0].texCoord * barycentricCoords.x +
                      patch[1].texCoord * barycentricCoords.y +
                      patch[2].texCoord * barycentricCoords.z;

    float displacement = displacementTexture.SampleLevel(textureSampler, texCoord, 0).r;

    displacement = (displacement - 0.5f) * 2.0f;

    float displacementScale = 0.1f;
    position += normal * displacement * displacementScale;

    // Wave animation
    float wave = sin(position.x * waveFrequency + position.z * waveFrequency + time * waveSpeed) * waveAmplitude;
    position += normal * wave;

    output.worldPos = mul(float4(position, 1.0f), world).xyz;

    output.normal = normalize(mul(normal, (float3x3)world));
    output.texCoord = texCoord;

    output.position = mul(float4(position, 1.0f), worldViewProj);

    return output;
}

PixelOutput PSMain(DomainOutput input)
{
    PixelOutput output;

    float3 normalMap = normalTexture.Sample(textureSampler, input.texCoord).rgb;

    bool isFallbackNormal = (abs(normalMap.r - 0.5f) < 0.01f &&
                             abs(normalMap.g - 0.5f) < 0.01f &&
                             abs(normalMap.b - 1.0f) < 0.01f);

    float3 finalNormal;

    if (isFallbackNormal)
    {
        finalNormal = normalize(input.normal);
    }
    else
    {
        normalMap = normalMap * 2.0f - 1.0f;

        float3 N = normalize(input.normal);
        float3 T = normalize(cross(N, float3(0, 1, 0)));
        if (length(T) < 0.01f)
            T = normalize(cross(N, float3(1, 0, 0)));
        float3 B = cross(N, T);

        float3x3 TBN = float3x3(T, B, N);
        float3 mappedNormal = normalize(mul(normalMap, TBN));

        float normalMapStrength = 0.5f;
        finalNormal = normalize(lerp(N, mappedNormal, normalMapStrength));
    }

    output.position = float4(input.worldPos, 1.0f);
    output.normal = float4(finalNormal, 1.0f);
    output.albedo = diffuseTexture.Sample(textureSampler, input.texCoord);

    return output;
}
