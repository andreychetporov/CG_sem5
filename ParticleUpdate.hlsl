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

cbuffer ParticleUpdateConstants : register(b0)
{
    float3 emitterPosition;
    float deltaTime;
    float3 gravity;
    float totalTime;
}

ConsumeStructuredBuffer<Particle> inputParticles : register(u0);
AppendStructuredBuffer<Particle> outputParticles : register(u1);

float Hash(uint value)
{
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return frac(value * 0.0000001192092896f);
}

[numthreads(256, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    Particle particle = inputParticles.Consume();
    particle.age += deltaTime;

    if (particle.age >= particle.lifetime)
    {
        uint seed = dispatchThreadId.x + (uint)(totalTime * 97.0f) * 1664525u;
        float angle = Hash(seed + 1u) * 6.2831853f;
        float radius = sqrt(Hash(seed + 2u)) * 0.35f;
        particle.position = emitterPosition + float3(cos(angle) * radius, 0.0f, sin(angle) * radius);
        particle.velocity = float3(cos(angle) * 0.8f, 3.0f + Hash(seed + 3u) * 2.5f, sin(angle) * 0.8f);
        particle.age = 0.0f;
        particle.lifetime = 1.8f + Hash(seed + 4u) * 1.8f;
        particle.size = 0.055f + Hash(seed + 5u) * 0.065f;
        particle.color = lerp(float4(1.0f, 0.25f, 0.03f, 1.0f),
            float4(1.0f, 0.9f, 0.2f, 1.0f), Hash(seed + 6u));
    }
    else
    {
        particle.velocity += gravity * deltaTime;
        particle.position += particle.velocity * deltaTime;
    }

    outputParticles.Append(particle);
}
