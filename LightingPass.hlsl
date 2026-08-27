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

struct Light
{
    float3 position;
    float type;
    float3 direction;
    float range;
    float3 color;
    float intensity;
    float spotAngle;
    float3 padding;
};

cbuffer LightingConstants : register(b0)
{
    float3 cameraPosition;
    float debugMode;
    int numLights;
    int shadowsEnabled;
    int pcfEnabled;
    float shadowMapSize;
    float4 cascadeSplits;
    float3 cameraForward;
    float padding2;
    float4x4 cascadeViewProj[4];
}

StructuredBuffer<Light> lights : register(t0);
Texture2D positionTexture : register(t1);
Texture2D normalTexture : register(t2);
Texture2D albedoTexture : register(t3);
Texture2DArray shadowMap : register(t4);

SamplerState gSampler : register(s0);
SamplerComparisonState shadowSampler : register(s1);

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position, 1.0f);
    output.texCoord = input.texCoord;
    return output;
}

float SampleCascadeShadow(float3 worldPos, int cascadeIndex, float receiverBias)
{
    float4 lightPosition = mul(float4(worldPos, 1.0f), cascadeViewProj[cascadeIndex]);
    float3 projected = lightPosition.xyz / lightPosition.w;
    float2 uv = float2(projected.x * 0.5f + 0.5f, -projected.y * 0.5f + 0.5f);
    if (any(uv < 0.0f) || any(uv > 1.0f) || projected.z <= 0.0f || projected.z >= 1.0f)
        return 1.0f;

    float receiverDepth = projected.z - receiverBias;
    if (pcfEnabled == 0)
        return shadowMap.SampleCmpLevelZero(shadowSampler, float3(uv, cascadeIndex), receiverDepth);

    float visibility = 0.0f;
    float2 texelSize = 1.0f / shadowMapSize;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
            visibility += shadowMap.SampleCmpLevelZero(shadowSampler,
                float3(uv + float2(x, y) * texelSize, cascadeIndex), receiverDepth);
    }
    return visibility / 9.0f;
}

float CalculateShadow(float3 worldPos, float3 normal, out int cascadeIndex)
{
    float viewDepth = dot(worldPos - cameraPosition, cameraForward);
    cascadeIndex = 0;
    if (viewDepth > cascadeSplits.x) cascadeIndex = 1;
    if (viewDepth > cascadeSplits.y) cascadeIndex = 2;
    if (viewDepth > cascadeSplits.z) cascadeIndex = 3;
    if (viewDepth > cascadeSplits.w || shadowsEnabled == 0)
        return 1.0f;

    float3 directionToLight = normalize(-lights[0].direction);
    float normalFacing = saturate(dot(normalize(normal), directionToLight));
    float receiverBias = max(0.0018f, 0.0035f * (1.0f - normalFacing));
    return SampleCascadeShadow(worldPos, cascadeIndex, receiverBias);
}

float3 CalculateDirectionalLight(Light light, float3 normal, float3 viewDir, float3 albedo, float shadow)
{
    float3 lightDir = normalize(-light.direction);
    float diff = max(dot(normal, lightDir), 0.0f);

    float3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0f), 32.0f);

    float3 diffuse = light.color * diff * light.intensity;
    float3 specular = light.color * spec * light.intensity * 0.3f;

    return (diffuse + specular) * albedo * shadow;
}

float3 CalculatePointLight(Light light, float3 worldPos, float3 normal, float3 viewDir, float3 albedo)
{
    float3 lightDir = light.position - worldPos;
    float distance = length(lightDir);

    if (distance > light.range)
        return float3(0.0f, 0.0f, 0.0f);

    lightDir = normalize(lightDir);

    float diff = max(dot(normal, lightDir), 0.0f);

    float3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0f), 32.0f);

    float attenuation = 1.0f - (distance / light.range);
    attenuation = attenuation * attenuation;

    float3 diffuse = light.color * diff * light.intensity * attenuation;
    float3 specular = light.color * spec * light.intensity * 0.5f * attenuation;

    return (diffuse + specular) * albedo;
}

float3 CalculateSpotLight(Light light, float3 worldPos, float3 normal, float3 viewDir, float3 albedo)
{
    float3 lightDir = light.position - worldPos;
    float distance = length(lightDir);

    if (distance > light.range)
        return float3(0.0f, 0.0f, 0.0f);

    lightDir = normalize(lightDir);

    float3 spotDir = normalize(light.direction);
    float theta = dot(lightDir, -spotDir);
    float cutoff = cos(light.spotAngle);

    if (theta < cutoff)
        return float3(0.0f, 0.0f, 0.0f);

    float diff = max(dot(normal, lightDir), 0.0f);

    float3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0f), 32.0f);

    float attenuation = 1.0f - (distance / light.range);
    attenuation = attenuation * attenuation;

    float spotIntensity = (theta - cutoff) / (1.0f - cutoff);
    spotIntensity = pow(spotIntensity, 2.0f);

    float3 diffuse = light.color * diff * light.intensity * attenuation * spotIntensity;
    float3 specular = light.color * spec * light.intensity * 0.5f * attenuation * spotIntensity;

    return (diffuse + specular) * albedo;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float3 worldPos = positionTexture.Sample(gSampler, input.texCoord).xyz;
    float3 normal = normalize(normalTexture.Sample(gSampler, input.texCoord).xyz);
    float4 albedo = albedoTexture.Sample(gSampler, input.texCoord);

    if (albedo.a < 0.01f)
        discard;

    if (debugMode == 1.0f)
        return float4(worldPos * 0.1f, 1.0f);
    if (debugMode == 2.0f)
        return float4(normal * 0.5f + 0.5f, 1.0f);
    if (debugMode == 3.0f)
        return albedo;

    float3 viewDir = normalize(cameraPosition - worldPos);

    int cascadeIndex = 0;
    float shadow = CalculateShadow(worldPos, normal, cascadeIndex);
    if (debugMode == 4.0f)
    {
        const float3 cascadeColors[4] = {
            float3(1.0f, 0.25f, 0.25f),
            float3(0.25f, 1.0f, 0.25f),
            float3(0.25f, 0.5f, 1.0f),
            float3(1.0f, 0.85f, 0.25f)
        };
        return float4(albedo.rgb * cascadeColors[cascadeIndex], 1.0f);
    }

    
    float3 ambient = float3(0.28f, 0.30f, 0.34f) * albedo.rgb;
    float3 lighting = ambient;

    for (int i = 0; i < numLights; i++)
    {
        Light light = lights[i];

        if (light.type == 0.0f)
        {
            lighting += CalculateDirectionalLight(light, normal, viewDir, albedo.rgb, shadow);
        }
        else if (light.type == 1.0f)
        {
            lighting += CalculatePointLight(light, worldPos, normal, viewDir, albedo.rgb);
        }
        else if (light.type == 2.0f)
        {
            lighting += CalculateSpotLight(light, worldPos, normal, viewDir, albedo.rgb);
        }
    }

    return float4(lighting, 1.0f);
}
