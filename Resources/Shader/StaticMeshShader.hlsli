#pragma once

// 거리 감쇠
float DistanceAttenuation(float dist, float Radius, float LightFalloffExponent)
{
    float glow = 1.0 - saturate(dist / Radius);
    return (pow(glow, LightFalloffExponent));
}

