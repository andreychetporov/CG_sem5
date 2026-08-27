static const float PI = 3.14159265359f;

struct VertexInput { float3 position : POSITION; float2 texCoord : TEXCOORD; };
struct VertexOutput { float4 position : SV_POSITION; float2 texCoord : TEXCOORD; };
struct Light
{
    float3 position; float type;
    float3 direction; float range;
    float3 color; float intensity;
    float spotAngle; float3 padding;
};

cbuffer LightingConstants : register(b0)
{
    float3 cameraPosition; float debugMode;
    int numLights; int shadowsEnabled; int pcfEnabled; float shadowMapSize;
    float4 cascadeSplits;
    float3 cameraForward; float padding2;
    float4x4 cascadeViewProj[4];
    float3 testLightPosition; float testLightEnabled;
    float3 testLightColor; float testLightIntensity;
    float testLightRange; float iblEnabled; float2 testLightPadding;
}

StructuredBuffer<Light> lights : register(t0);
Texture2D positionTexture : register(t1);
Texture2D normalTexture : register(t2);
Texture2D albedoTexture : register(t3);
Texture2DArray shadowMap : register(t4);
TextureCube irradianceMap : register(t5);
TextureCube prefilteredEnvironmentMap : register(t6);
Texture2D brdfIntegrationMap : register(t7);
SamplerState gSampler : register(s0);
SamplerComparisonState shadowSampler : register(s1);
SamplerState iblSampler : register(s2);

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
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
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
    float normalFacing = saturate(dot(normal, directionToLight));
    float receiverBias = max(0.0018f, 0.0035f * (1.0f - normalFacing));
    return SampleCascadeShadow(worldPos, cascadeIndex, receiverBias);
}

float DistributionGGX(float3 normal, float3 halfVector, float roughness)
{
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float nDotH = saturate(dot(normal, halfVector));
    float denominator = nDotH * nDotH * (alphaSquared - 1.0f) + 1.0f;
    return alphaSquared / max(PI * denominator * denominator, 0.0001f);
}

float GeometrySchlickGGX(float nDotDirection, float roughness)
{
    float value = roughness + 1.0f;
    float k = (value * value) / 8.0f;
    return nDotDirection / max(nDotDirection * (1.0f - k) + k, 0.0001f);
}

float GeometrySmith(float3 normal, float3 viewDirection, float3 lightDirection, float roughness)
{
    return GeometrySchlickGGX(saturate(dot(normal, viewDirection)), roughness) *
        GeometrySchlickGGX(saturate(dot(normal, lightDirection)), roughness);
}

float3 FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0f - f0) * pow(1.0f - saturate(cosTheta), 5.0f);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 f0, float roughness)
{
    return f0 + (max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), f0) - f0) *
        pow(1.0f - saturate(cosTheta), 5.0f);
}

float3 EvaluateCookTorrance(float3 normal, float3 viewDirection, float3 lightDirection,
    float3 radiance, float3 albedo, float metallic, float roughness)
{
    float nDotL = saturate(dot(normal, lightDirection));
    float nDotV = saturate(dot(normal, viewDirection));
    if (nDotL <= 0.0f || nDotV <= 0.0f) return 0.0f;
    float3 halfVector = normalize(viewDirection + lightDirection);
    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 fresnel = FresnelSchlick(saturate(dot(halfVector, viewDirection)), f0);
    float distribution = DistributionGGX(normal, halfVector, roughness);
    float geometry = GeometrySmith(normal, viewDirection, lightDirection, roughness);
    float3 specular = distribution * geometry * fresnel / max(4.0f * nDotV * nDotL, 0.001f);
    float3 diffuseWeight = (1.0f - fresnel) * (1.0f - metallic);
    return (diffuseWeight * albedo / PI + specular) * radiance * nDotL;
}

float3 CalculateDirectionalLight(Light light, float3 normal, float3 viewDirection,
    float3 albedo, float metallic, float roughness, float shadow)
{
    return EvaluateCookTorrance(normal, viewDirection, normalize(-light.direction),
        light.color * light.intensity, albedo, metallic, roughness) * shadow;
}

float3 CalculatePointLight(Light light, float3 worldPos, float3 normal, float3 viewDirection,
    float3 albedo, float metallic, float roughness)
{
    float3 toLight = light.position - worldPos;
    float distance = length(toLight);
    if (distance > light.range || distance < 0.001f) return 0.0f;
    float rangeFade = saturate(1.0f - distance / light.range);
    rangeFade *= rangeFade;
    float attenuation = rangeFade / max(distance * distance, 0.01f);
    return EvaluateCookTorrance(normal, viewDirection, toLight / distance,
        light.color * light.intensity * attenuation, albedo, metallic, roughness);
}

float3 CalculateSpotLight(Light light, float3 worldPos, float3 normal, float3 viewDirection,
    float3 albedo, float metallic, float roughness)
{
    float3 toLight = light.position - worldPos;
    float distance = length(toLight);
    if (distance > light.range || distance < 0.001f) return 0.0f;
    float3 lightDirection = toLight / distance;
    float theta = dot(lightDirection, -normalize(light.direction));
    float cutoff = cos(light.spotAngle);
    if (theta < cutoff) return 0.0f;
    float coneFade = pow(saturate((theta - cutoff) / max(1.0f - cutoff, 0.001f)), 2.0f);
    float rangeFade = saturate(1.0f - distance / light.range);
    float attenuation = coneFade * rangeFade * rangeFade / max(distance * distance, 0.01f);
    return EvaluateCookTorrance(normal, viewDirection, lightDirection,
        light.color * light.intensity * attenuation, albedo, metallic, roughness);
}

float3 CalculateIBL(float3 normal, float3 viewDirection, float3 albedo,
    float metallic, float roughness, float ao)
{
    float nDotV = saturate(dot(normal, viewDirection));
    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 fresnel = FresnelSchlickRoughness(nDotV, f0, roughness);
    float3 diffuseWeight = (1.0f - fresnel) * (1.0f - metallic);
    float3 diffuse = irradianceMap.Sample(iblSampler, normal).rgb * albedo;
    float3 reflection = reflect(-viewDirection, normal);
    float3 prefiltered = prefilteredEnvironmentMap.SampleLevel(iblSampler, reflection,
        roughness * 11.0f).rgb;
    float2 brdf = brdfIntegrationMap.Sample(iblSampler, float2(nDotV, roughness)).rg;
    float3 specular = prefiltered * (fresnel * brdf.x + brdf.y);
    return (diffuseWeight * diffuse + specular) * ao;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float4 positionData = positionTexture.Sample(gSampler, input.texCoord);
    float4 normalData = normalTexture.Sample(gSampler, input.texCoord);
    float4 albedoData = albedoTexture.Sample(gSampler, input.texCoord);
    if (albedoData.a < 0.01f) discard;
    float3 worldPos = positionData.xyz;
    float3 normal = normalize(normalData.xyz);
    float3 albedo = albedoData.rgb;
    bool pbrMaterial = abs(albedoData.a - 0.5f) < 0.2f;
    float metallic = pbrMaterial ? saturate(positionData.w) : 0.0f;
    float roughness = pbrMaterial ? clamp(normalData.w, 0.04f, 1.0f) : 0.55f;
    if (debugMode == 1.0f) return float4(worldPos * 0.1f, 1.0f);
    if (debugMode == 2.0f) return float4(normal * 0.5f + 0.5f, 1.0f);
    if (debugMode == 3.0f) return float4(albedo, 1.0f);

    float3 viewDirection = normalize(cameraPosition - worldPos);
    int cascadeIndex = 0;
    float shadow = CalculateShadow(worldPos, normal, cascadeIndex);
    if (debugMode == 4.0f)
    {
        const float3 cascadeColors[4] = {
            float3(1.0f, 0.25f, 0.25f), float3(0.25f, 1.0f, 0.25f),
            float3(0.25f, 0.5f, 1.0f), float3(1.0f, 0.85f, 0.25f) };
        return float4(albedo * cascadeColors[cascadeIndex], 1.0f);
    }

    float testSourceMask = (!pbrMaterial) ? step(1.5f, normalData.w) : 0.0f;
    if (testLightEnabled > 0.5f && testSourceMask > 0.5f)
        return float4(testLightColor * 40.0f, 1.0f);
    float particleMask = (!pbrMaterial) ? saturate(1.0f - normalData.w) : 0.0f;
    float3 lighting = iblEnabled > 0.5f
        ? CalculateIBL(normal, viewDirection, albedo, metallic, roughness, 1.0f)
        : float3(0.03f, 0.03f, 0.03f) * albedo;
    float hemisphere = normal.y * 0.5f + 0.5f;
    float3 particleAmbient = albedo * lerp(0.10f, 0.28f, hemisphere);
    lighting = lerp(lighting, max(lighting, particleAmbient), particleMask);

    for (int i = 0; i < numLights; ++i)
    {
        Light light = lights[i];
        if (light.type == 0.0f)
            lighting += CalculateDirectionalLight(light, normal, viewDirection, albedo,
                metallic, roughness, shadow);
        else if (light.type == 1.0f)
            lighting += CalculatePointLight(light, worldPos, normal, viewDirection, albedo,
                metallic, roughness);
        else if (light.type == 2.0f)
            lighting += CalculateSpotLight(light, worldPos, normal, viewDirection, albedo,
                metallic, roughness);
    }
    if (testLightEnabled > 0.5f)
    {
        Light testLight = (Light)0;
        testLight.position = testLightPosition;
        testLight.type = 1.0f;
        testLight.range = testLightRange;
        testLight.color = testLightColor;
        testLight.intensity = testLightIntensity;
        lighting += CalculatePointLight(testLight, worldPos, normal, viewDirection, albedo,
            metallic, roughness);
    }
    return float4(lighting, 1.0f);
}
