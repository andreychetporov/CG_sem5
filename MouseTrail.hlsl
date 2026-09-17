Texture2D<float4> previousTrail : register(t0);
RWTexture2D<float4> nextTrail : register(u0);

cbuffer MouseTrailConstants : register(b0)
{
    uint imageWidth;
    uint imageHeight;
    float2 currentMouseUV;
    float2 previousMouseUV;
    float deltaTime;
    float decaySpeed;
    float brushRadius;
    uint resetTrail;
    uint drawTrail;
};

float DistanceToSegment(float2 point_, float2 segmentStart, float2 segmentEnd)
{
    float2 segment = segmentEnd - segmentStart;
    float segmentLengthSquared = dot(segment, segment);

    float t = segmentLengthSquared > 0.000001f
        ? saturate(dot(point_ - segmentStart, segment) / segmentLengthSquared)
        :
    0.0f;

    float2 closestPoint = segmentStart + segment * t;
    return length(point_ - closestPoint);
}

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= imageWidth ||
        dispatchThreadId.y >= imageHeight)
    {
        return;
    }

    float2 uv =
        (float2(dispatchThreadId.xy) + 0.5f) /
        float2(imageWidth, imageHeight);

    float4 oldTrail = resetTrail != 0
        ? float4(0.0f, 0.0f, 0.0f, 0.0f)
        : previousTrail.Load(int3(dispatchThreadId.xy, 0));

    float decay = exp(-decaySpeed * max(deltaTime, 0.0f));
    float4 trail = oldTrail * decay;

    float distanceToMousePath =
        DistanceToSegment(uv, previousMouseUV, currentMouseUV);

    float stamp = drawTrail != 0
        ? 1.0f - smoothstep(
            brushRadius * 0.45f,
            brushRadius,
            distanceToMousePath
        )
        : 0.0f;

    float3 blueColor = float3(0.03f, 0.25f, 1.0f);

    trail.rgb = min(trail.rgb + blueColor * stamp, 1.5f);
    trail.a = saturate(max(trail.a, stamp));

    nextTrail[dispatchThreadId.xy] = trail;
}
