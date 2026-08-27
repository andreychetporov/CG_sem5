struct Particle
{
    float3 position;
    float age;
    float3 velocity;
    float lifetime;
    float4 color;
    float size;
    float3 padding;
};

cbuffer ParticleRenderConstants : register(b0)
{
    float4x4 viewProjection;
    float3 cameraRight;
    float padding0;
    float3 cameraUp;
    float padding1;
}

StructuredBuffer<Particle> particles : register(t0);

struct VertexOutput
{
    float3 center : POSITION0;
    float4 color : COLOR0;
    float size : TEXCOORD0;
};

struct GeometryOutput
{
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float2 texCoord : TEXCOORD1;
    float4 color : COLOR0;
};

struct PixelOutput
{
    float4 position : SV_TARGET0;
    float4 normal : SV_TARGET1;
    float4 albedo : SV_TARGET2;
};

VertexOutput VSMain(uint vertexId : SV_VertexID)
{
    Particle particle = particles[vertexId];
    VertexOutput output;
    output.center = particle.position;
    output.color = particle.color;
    output.size = particle.size;
    return output;
}

[maxvertexcount(4)]
void GSMain(point VertexOutput input[1], inout TriangleStream<GeometryOutput> stream)
{
    const float2 corners[4] = {
        float2(-1.0f, -1.0f), float2(-1.0f, 1.0f),
        float2(1.0f, -1.0f), float2(1.0f, 1.0f)
    };
    const float2 uvs[4] = {
        float2(0.0f, 1.0f), float2(0.0f, 0.0f),
        float2(1.0f, 1.0f), float2(1.0f, 0.0f)
    };

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        GeometryOutput output;
        output.worldPos = input[0].center +
            cameraRight * corners[i].x * input[0].size +
            cameraUp * corners[i].y * input[0].size;
        output.position = mul(float4(output.worldPos, 1.0f), viewProjection);
        output.texCoord = uvs[i];
        output.color = input[0].color;
        stream.Append(output);
    }
}

PixelOutput PSMain(GeometryOutput input)
{
    float2 centered = input.texCoord * 2.0f - 1.0f;
    float radiusSquared = dot(centered, centered);
    if (radiusSquared > 1.0f)
        discard;

    PixelOutput output;
    output.position = float4(input.worldPos, 1.0f);
    float3 cameraForward = normalize(cross(cameraRight, cameraUp));
    float sphereDepth = sqrt(saturate(1.0f - radiusSquared));
    float3 sphereNormal = normalize(cameraRight * centered.x - cameraUp * centered.y -
        cameraForward * sphereDepth);
    // normal.w == 0 marks particles for their environment lighting in LightingPass.
    output.normal = float4(sphereNormal, 0.0f);
    output.albedo = float4(input.color.rgb, 1.0f);
    return output;
}
