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
    float3 testLightPosition;
    float testLightEnabled;
    float3 testLightColor;
    float testLightIntensity;
    float testLightRange;
    float3 testLightPadding;
}

StructuredBuffer<Light> lights : register(t0);
Texture2D positionTexture : register(t1);
Texture2D normalTexture : register(t2);
Texture2D albedoTexture : register(t3);
Texture2DArray shadowMap : register(t4);
TextureCube<float4> irradianceMap : register(t5);
TextureCube<float4> prefilteredEnvMap : register(t6);
Texture2D<float2> brdfIntegrationMap : register(t7);

SamplerState gSampler : register(s0);
SamplerComparisonState shadowSampler : register(s1);

VertexOutput VSMain(uint vertexId : SV_VertexID)
{
    static const float2 positions[4] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  1.0f),
        float2( 1.0f, -1.0f),
        float2( 1.0f,  1.0f)
    };
    static const float2 texCoords[4] = {
        float2(0.0f, 1.0f),
        float2(0.0f, 0.0f),
        float2(1.0f, 1.0f),
        float2(1.0f, 0.0f)
    };
    VertexOutput output;
    output.position = float4(positions[vertexId], 0.0f, 1.0f);
    output.texCoord = texCoords[vertexId];
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

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = saturate(dot(N, H));
    float denominator = nDotH * nDotH * (a2 - 1.0f) + 1.0f;
    return a2 / max(3.14159265f * denominator * denominator, 0.0001f);
}

float GeometrySchlickGGX(float nDotV, float roughness)
{
    float r = roughness + 1.0f;
    float k = r * r / 8.0f;
    return nDotV / max(nDotV * (1.0f - k) + k, 0.0001f);
}

float3 FresnelSchlick(float cosine, float3 F0)
{
    return F0 + (1.0f - F0) * pow(1.0f - cosine, 5.0f);
}

float3 EvaluatePBR(float3 N, float3 V, float3 L, float3 radiance, float3 albedo, float roughness)
{
    float3 H = normalize(V + L);
    float3 F0 = float3(0.04f, 0.04f, 0.04f);
    float3 F = FresnelSchlick(saturate(dot(H, V)), F0);
    float G = GeometrySchlickGGX(saturate(dot(N, V)), roughness) *
        GeometrySchlickGGX(saturate(dot(N, L)), roughness);
    float specular = DistributionGGX(N, H, roughness) * G /
        max(4.0f * saturate(dot(N, V)) * saturate(dot(N, L)), 0.0001f);
    float3 diffuse = (1.0f - F) * albedo / 3.14159265f;
    return (diffuse + F * specular) * radiance * saturate(dot(N, L));
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float3 worldPos = positionTexture.Sample(gSampler, input.texCoord).xyz;
    float4 normalData = normalTexture.Sample(gSampler, input.texCoord);
    float3 normal = normalize(normalData.xyz);
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
    float roughness = lerp(0.62f, 0.22f, saturate(1.0f - normalData.w));
    float metallic = 0.0f;
    float3 lighting = 0.0f;
    int cascadeIndex = 0;
    float shadow = CalculateShadow(worldPos, normal, cascadeIndex);
    if (debugMode == 4.0f)
    {
        const float3 cascadeColors[4] = {
            float3(1.0f, 0.25f, 0.25f), float3(0.25f, 1.0f, 0.25f),
            float3(0.25f, 0.5f, 1.0f), float3(1.0f, 0.85f, 0.25f)
        };
        return float4(albedo.rgb * cascadeColors[cascadeIndex], 1.0f);
    }

    float testSourceMask = step(1.5f, normalData.w);
    if (testLightEnabled > 0.5f && testSourceMask > 0.5f)
        return float4(testLightColor * testLightIntensity, 1.0f);

    for (int i = 0; i < numLights; i++)
    {
        Light light = lights[i];

        float3 lightDir;
        float attenuation = 1.0f;
        if (light.type == 0.0f)
        {
            lightDir = normalize(-light.direction);
            attenuation = shadow;
        }
        else
        {
            float3 toLight = light.position - worldPos;
            float distanceToLight = length(toLight);
            if (distanceToLight > light.range) continue;
            lightDir = toLight / max(distanceToLight, 0.0001f);
            attenuation = pow(saturate(1.0f - distanceToLight / light.range), 2.0f);
        }
        lighting += EvaluatePBR(normal, viewDir, lightDir, light.color * light.intensity * attenuation,
            albedo.rgb, roughness);
    }

    if (testLightEnabled > 0.5f)
    {
        float3 toTestLight = testLightPosition - worldPos;
        float distanceToTestLight = length(toTestLight);
        float3 testLightDir = toTestLight / max(distanceToTestLight, 0.0001f);
        float attenuation = pow(saturate(1.0f - distanceToTestLight / testLightRange), 2.0f);
        lighting += EvaluatePBR(normal, viewDir, testLightDir,
            testLightColor * testLightIntensity * attenuation, albedo.rgb, roughness);
    }

    float nDotV = saturate(dot(normal, viewDir));
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo.rgb, metallic);
    float3 F = FresnelSchlick(nDotV, F0);
    float3 diffuseIBL = irradianceMap.Sample(gSampler, normal).rgb * albedo.rgb;
    float3 reflection = reflect(-viewDir, normal);
    float3 prefiltered = prefilteredEnvMap.SampleLevel(gSampler, reflection, roughness * 11.0f).rgb;
    float2 brdf = brdfIntegrationMap.Sample(gSampler, float2(nDotV, roughness));
    float3 specularIBL = prefiltered * (F * brdf.x + brdf.y);
    lighting += (1.0f - F) * (1.0f - metallic) * diffuseIBL + specularIBL;
    return float4(lighting, 1.0f);
}
