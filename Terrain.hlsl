cbuffer TerrainConstants : register(b0)
{
    float4x4 viewProjection;
    float lod;
    float debugLOD;
    float2 padding;
};

struct VSOut
{
    float4 position : SV_POSITION;
    float3 world : TEXCOORD0;
    float3 normal : NORMAL;
};

VSOut VSMain(float3 position : POSITION, float3 normal : NORMAL)
{
    VSOut output;
    output.position = mul(float4(position, 1.0f), viewProjection);
    output.world = position;
    output.normal = normal;
    return output;
}

struct Targets
{
    float4 position : SV_TARGET0;
    float4 normal : SV_TARGET1;
    float4 albedo : SV_TARGET2;
};

Targets PSMain(VSOut input)
{
    Targets output;
    float3 normalizedNormal = normalize(input.normal);

    const float3 grass = float3(0.12f, 0.27f, 0.055f);
    const float3 rock = float3(0.31f, 0.27f, 0.22f);
    const float3 snow = float3(0.8f, 0.83f, 0.86f);

    float slope = saturate((1.0f - normalizedNormal.y) * 4.0f);
    float3 color = lerp(grass, rock, slope);

    float snowAmount = smoothstep(175.0f, 210.0f, input.world.y);
    color = lerp(color, snow, snowAmount);

    color *= 0.94f + 0.06f * sin(input.world.x * 0.7f) * cos(input.world.z * 0.5f);

    if (debugLOD > 0.5f)
    {
        const float3 colors[5] =
        {
            float3(1.0f, 0.0f, 0.0f),
            float3(1.0f, 0.6f, 0.0f),
            float3(0.0f, 0.7f, 0.0f),
            float3(0.0f, 0.4f, 1.0f),
            float3(0.7f, 0.0f, 1.0f)
        };
        color = colors[(int)lod];
    }

    output.position = float4(input.world, 1.0f);
    output.normal = float4(normalizedNormal, 1.0f);
    output.albedo = float4(color, 1.0f);
    return output;
}

Texture2D<float> heightMap : register(t0);
StructuredBuffer<float4> terrainNodes : register(t1);

struct GeneratedVertex
{
    float3 position;
    float3 normal;
};

RWStructuredBuffer<GeneratedVertex> generatedVertices : register(u0);

cbuffer TerrainGenerationConstants : register(b0)
{
    uint sourceWidth;
    uint sourceHeight;
    uint nodeCount;
    uint grid;
};

float SampleTerrainHeight(float2 worldPosition)
{
    float2 uv = saturate((worldPosition + 2048.0f) / 4096.0f);
    uv *= float2(sourceWidth - 1, sourceHeight - 1);

    uint2 low = (uint2)uv;
    uint2 high = min(low + 1, uint2(sourceWidth - 1, sourceHeight - 1));
    float2 fraction = frac(uv);

    float top = lerp(
        heightMap.Load(int3(low, 0)),
        heightMap.Load(int3(uint2(high.x, low.y), 0)),
        fraction.x);
    float bottom = lerp(
        heightMap.Load(int3(uint2(low.x, high.y), 0)),
        heightMap.Load(int3(high, 0)),
        fraction.x);

    return lerp(top, bottom, fraction.y) * 400.0f;
}

[numthreads(64, 1, 1)]
void CSGenerate(uint3 id : SV_DispatchThreadID)
{
    const uint mainVertices = (grid + 1) * (grid + 1);
    const uint verticesPerNode = mainVertices + 4 * (grid + 1);
    const uint localIndex = id.x;
    const uint nodeIndex = id.y;

    if (localIndex >= verticesPerNode || nodeIndex >= nodeCount)
        return;

    uint2 gridPosition;
    bool skirt = localIndex >= mainVertices;

    if (!skirt)
    {
        gridPosition = uint2(
            localIndex % (grid + 1),
            localIndex / (grid + 1));
    }
    else
    {
        uint edgeVertex = localIndex - mainVertices;
        uint edge = edgeVertex / (grid + 1);
        uint coordinate = edgeVertex % (grid + 1);

        if (edge == 0)
            gridPosition = uint2(coordinate, 0);
        else if (edge == 1)
            gridPosition = uint2(coordinate, grid);
        else if (edge == 2)
            gridPosition = uint2(0, coordinate);
        else
            gridPosition = uint2(grid, coordinate);
    }

    float4 node = terrainNodes[nodeIndex];
    float2 positionXZ = node.xy + (float2(gridPosition) / grid) * node.z;
    float height = SampleTerrainHeight(positionXZ);

    float nx = SampleTerrainHeight(positionXZ + float2(-1.0f, 0.0f))
             - SampleTerrainHeight(positionXZ + float2(1.0f, 0.0f));
    float nz = SampleTerrainHeight(positionXZ + float2(0.0f, -1.0f))
             - SampleTerrainHeight(positionXZ + float2(0.0f, 1.0f));

    GeneratedVertex vertex;
    vertex.position = float3(
        positionXZ.x,
        height - (skirt ? 65.0f : 0.0f),
        positionXZ.y);
    vertex.normal = normalize(float3(nx, 2.0f, nz));

    generatedVertices[nodeIndex * verticesPerNode + localIndex] = vertex;
}
