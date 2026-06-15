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
    float3 padding2;
}

StructuredBuffer<Light> lights : register(t0);
Texture2D positionTexture : register(t1);
Texture2D normalTexture : register(t2);
Texture2D albedoTexture : register(t3);

SamplerState gSampler : register(s0);

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position, 1.0f);
    output.texCoord = input.texCoord;
    return output;
}

float3 CalculateDirectionalLight(Light light, float3 normal, float3 viewDir, float3 albedo)
{
    float3 lightDir = normalize(-light.direction);
    float diff = max(dot(normal, lightDir), 0.0f);

    float3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0f), 32.0f);

    float3 diffuse = light.color * diff * light.intensity;
    float3 specular = light.color * spec * light.intensity * 0.3f;

    return (diffuse + specular) * albedo;
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

    
    float3 ambient = float3(0.15f, 0.18f, 0.22f) * albedo.rgb;
    float3 lighting = ambient;

    for (int i = 0; i < numLights; i++)
    {
        Light light = lights[i];

        if (light.type == 0.0f)
        {
            lighting += CalculateDirectionalLight(light, normal, viewDir, albedo.rgb);
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
