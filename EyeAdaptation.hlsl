Texture2D<float4> hdrTexture : register(t0);
RWStructuredBuffer<float> tileLuminance : register(u0);
RWStructuredBuffer<float> adaptedLuminance : register(u1);

cbuffer EyeAdaptationConstants : register(b0)
{
    uint imageWidth;
    uint imageHeight;
    uint tileCountX;
    uint tileCount;
    float deltaTime;
    float brightenSpeed;
    float darkenSpeed;
    uint resetAdaptation;
}

groupshared float sharedLuminance[256];

[numthreads(16, 16, 1)]
void CSReduceTiles(uint3 dispatchThreadId : SV_DispatchThreadID,
    uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    float luminance = 0.0f;
    if (dispatchThreadId.x < imageWidth && dispatchThreadId.y < imageHeight)
    {
        float3 color = max(hdrTexture.Load(int3(dispatchThreadId.xy, 0)).rgb, 0.0f);
        luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    }

    sharedLuminance[groupIndex] = luminance;
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint stride = 128; stride > 0; stride >>= 1)
    {
        if (groupIndex < stride)
            sharedLuminance[groupIndex] += sharedLuminance[groupIndex + stride];
        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex == 0)
        tileLuminance[groupId.y * tileCountX + groupId.x] = sharedLuminance[0];
}

[numthreads(256, 1, 1)]
void CSAdapt(uint groupIndex : SV_GroupIndex)
{
    float sum = 0.0f;
    for (uint tile = groupIndex; tile < tileCount; tile += 256)
        sum += tileLuminance[tile];

    sharedLuminance[groupIndex] = sum;
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint stride = 128; stride > 0; stride >>= 1)
    {
        if (groupIndex < stride)
            sharedLuminance[groupIndex] += sharedLuminance[groupIndex + stride];
        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex == 0)
    {
        float currentLuminance = max(sharedLuminance[0] /
            max((float)(imageWidth * imageHeight), 1.0f), 0.001f);
        float previousLuminance = adaptedLuminance[0];
        float speed = currentLuminance > previousLuminance ? brightenSpeed : darkenSpeed;
        float blendFactor = 1.0f - exp(-max(deltaTime, 0.0f) * speed);
        adaptedLuminance[0] = resetAdaptation != 0 ? currentLuminance :
            lerp(previousLuminance, currentLuminance, saturate(blendFactor));
    }
}
