struct VertexInput
{
    float3 position : POSITION;
    float2 texCoord : TEXCOORD;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD;
};

cbuffer PostProcessConstants : register(b0)
{
    float3 cameraPosition;
    float focusDistance;
    float3 cameraForward;
    float focusRange;
    float2 inverseResolution;
    float maxBlurRadius;
    float manualExposure;
    int depthOfFieldEnabled;
    int eyeAdaptationEnabled;
    float exposureKey;
    float gamma;
}

Texture2D<float4> hdrTexture : register(t0);
Texture2D<float4> positionTexture : register(t1);
StructuredBuffer<float> adaptedLuminance : register(t2);
SamplerState linearSampler : register(s0);

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position, 1.0f);
    output.texCoord = input.texCoord;
    return output;
}

float ViewDepth(float2 uv)
{
    float3 worldPosition = positionTexture.SampleLevel(linearSampler, uv, 0).xyz;
    return dot(worldPosition - cameraPosition, cameraForward);
}

float CircleOfConfusion(float depth)
{
    float focusCore = max(focusRange * 0.2f, 0.1f);
    return saturate((abs(depth - focusDistance) - focusCore) /
        max(focusRange, 0.1f));
}

float3 ApplyDepthOfField(float2 uv, float3 centerColor)
{
    float centerDepth = ViewDepth(uv);
    if (centerDepth <= 0.0f)
        return centerColor;

    float coc = CircleOfConfusion(centerDepth);
    if (coc < 0.01f)
        return centerColor;

    static const float2 poisson[16] = {
        float2(-0.94201624f, -0.39906216f), float2(0.94558609f, -0.76890725f),
        float2(-0.09418410f, -0.92938870f), float2(0.34495938f, 0.29387760f),
        float2(-0.91588581f, 0.45771432f), float2(-0.81544232f, -0.87912464f),
        float2(-0.38277543f, 0.27676845f), float2(0.97484398f, 0.75648379f),
        float2(0.44323325f, -0.97511554f), float2(0.53742981f, -0.47373420f),
        float2(-0.26496911f, -0.41893023f), float2(0.79197514f, 0.19090188f),
        float2(-0.24188840f, 0.99706507f), float2(-0.81409955f, 0.91437590f),
        float2(0.19984126f, 0.78641367f), float2(0.14383161f, -0.14100790f)
    };

    float2 blurScale = inverseResolution * maxBlurRadius * coc;
    float3 accumulated = centerColor;
    float totalWeight = 1.0f;

    [unroll]
    for (int i = 0; i < 16; ++i)
    {
        float2 sampleUv = saturate(uv + poisson[i] * blurScale);
        float sampleDepth = ViewDepth(sampleUv);
        float depthWeight = sampleDepth > 0.0f ?
            saturate(1.25f - abs(sampleDepth - centerDepth) /
                max(focusRange * 2.0f, 1.0f)) : 0.15f;
        accumulated += hdrTexture.SampleLevel(linearSampler, sampleUv, 0).rgb * depthWeight;
        totalWeight += depthWeight;
    }

    return accumulated / max(totalWeight, 0.001f);
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float3 hdrColor = hdrTexture.SampleLevel(linearSampler, input.texCoord, 0).rgb;
    if (depthOfFieldEnabled != 0)
        hdrColor = ApplyDepthOfField(input.texCoord, hdrColor);

    float averageLuminance = max(adaptedLuminance[0], 0.001f);
    // The exponent deliberately exaggerates the response for a clear laboratory
    // demonstration: bright frames strongly close the virtual aperture, while
    // dark frames receive a much larger exposure boost.
    float exposureRatio = max(exposureKey / averageLuminance, 0.001f);
    float automaticExposure = clamp(pow(exposureRatio, 1.8f), 0.03f, 8.0f);
    float exposure = eyeAdaptationEnabled != 0 ? automaticExposure : manualExposure;

    float3 mappedColor = 1.0f - exp(-max(hdrColor, 0.0f) * exposure);
    mappedColor = pow(saturate(mappedColor), 1.0f / max(gamma, 0.001f));
    return float4(mappedColor, 1.0f);
}
