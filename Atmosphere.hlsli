// Distances in kilometres, scattering and extinction coefficients in km^-1.
static const float PlanetRadius = 6360.0;
static const float AtmosphereRadius = 6420.0;
static const float PiAtm = 3.14159265;

float2 AtmosphereDensity(float3 samplePosition)
{
    float altitude = max(length(samplePosition) - PlanetRadius, 0.0);
    return exp(-altitude / float2(8.0, 1.2));
}

float AtmosphereExit(float3 origin, float3 direction)
{
    float b = dot(origin, direction);
    float c = dot(origin, origin) - AtmosphereRadius * AtmosphereRadius;
    return max(sqrt(max(b * b - c, 0.0)) - b, 0.0);
}

float GroundIntersection(float3 origin, float3 direction)
{
    float b = dot(origin, direction);
    float c = dot(origin, origin) - PlanetRadius * PlanetRadius;
    float discriminant = b * b - c;
    // Near root, expressed without subtracting nearly equal values.
    return b < 0 && discriminant >= 0 ? max(c, 0.0) / max(-b + sqrt(max(discriminant, 0.0)), 1e-5) : 1e9;
}

float3 OpticalExtinction(float2 column, float3 betaR, float betaM)
{
    // Aerosols have a single-scattering albedo of 0.9.
    return betaR * column.x + (betaM / 0.9) * column.y;
}

float3 SunTransmittance(float3 samplePosition, float3 sun, float3 betaR, float betaM)
{
    float distanceToTop = AtmosphereExit(samplePosition, sun);
    if (GroundIntersection(samplePosition, sun) < distanceToTop)
        return 0;
    float2 column = 0;
    float start = 0;
    [loop] for (int i = 1; i <= 8; ++i)
    {
        float u = i / 8.0;
        float end = distanceToTop * u * u;
        column += AtmosphereDensity(samplePosition + sun * ((start + end) * 0.5)) * (end - start);
        start = end;
    }
    return exp(-OpticalExtinction(column, betaR, betaM));
}

float3 AtmosphereOrigin(float3 cameraMetres)
{
    return float3(cameraMetres.x * 0.001, PlanetRadius + max(cameraMetres.y * 0.001, 0.002), cameraMetres.z * 0.001);
}

float3 IntegrateAtmosphere(float3 origin, float3 viewRay, float distanceLimit, bool sky,
    float3 sun, float3 betaR, float betaM, out float3 transmission)
{
    float endOfAir = AtmosphereExit(origin, viewRay);
    float groundDistance = GroundIntersection(origin, viewRay);
    float distance = min(endOfAir, distanceLimit);
    if (sky) distance = min(distance, groundDistance);

    // Both vectors point away from the sample (toward sky/sun). The dot
    // product peaks when looking at the sun: forward Mie scattering.
    float mu = clamp(dot(viewRay, sun), -1.0, 1.0);
    float rayleighPhase = 3.0 * (1.0 + mu * mu) / (16.0 * PiAtm);
    const float g = 0.8;
    float miePhase = (1.0 - g * g) / (4.0 * PiAtm * pow(1.0 + g * g - 2.0 * g * mu, 1.5));
    const float3 sunRadiance = float3(15.0, 14.5, 13.7);

    float2 viewColumn = 0;
    float3 scattered = 0;
    float start = 0;
    [loop] for (int i = 1; i <= 16; ++i)
    {
        float u = i / 16.0;
        float end = distance * u * u;
        float stepLength = end - start;
        float3 samplePosition = origin + viewRay * ((start + end) * 0.5);
        float2 density = AtmosphereDensity(samplePosition);
        float2 stepColumn = density * stepLength;
        float3 toEye = exp(-OpticalExtinction(viewColumn + stepColumn * 0.5, betaR, betaM));
        float3 toSun = SunTransmittance(samplePosition, sun, betaR, betaM);
        scattered += toEye * toSun * (betaR * rayleighPhase * stepColumn.x + betaM * miePhase * stepColumn.y);
        viewColumn += stepColumn;
        start = end;
    }
    transmission = exp(-OpticalExtinction(viewColumn, betaR, betaM));
    if (sky && groundDistance >= endOfAir)
    {
        // Finite angular radius, with a narrow smooth edge.
        float disc = smoothstep(0.999984, 0.999991, mu);
        scattered += transmission * disc * 6.0;
    }
    return scattered * sunRadiance;
}
