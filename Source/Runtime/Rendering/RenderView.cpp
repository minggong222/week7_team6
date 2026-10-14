#include "EnginePCH.h"
#include "RenderView.h"
#include <cassert>
#include <cmath>

FMatrix BuildProjectionMatrix(const FCameraProjection& Projection, const float AspectRatio)
{
    assert(AspectRatio > 0.0f);
    assert(Projection.NearClip > 0.0f && Projection.FarClip > Projection.NearClip);
    FMatrix Result;
    if (Projection.Mode == ECameraProjectionMode::Perspective)
    {
        assert(Projection.FovDegrees > 0.0f && Projection.FovDegrees < 180.0f);
        const float ScaleY = 1.0f / tanf(Projection.FovDegrees * 3.14159265358979323846f / 360.0f);
        Result.M[1][0] = ScaleY / AspectRatio;
        Result.M[2][1] = ScaleY;
        Result.M[0][2] = Projection.FarClip / (Projection.FarClip - Projection.NearClip);
        Result.M[3][2] = -Projection.NearClip * Projection.FarClip / (Projection.FarClip - Projection.NearClip);
        Result.M[0][3] = 1.0f;
    }
    else
    {
        assert(Projection.OrthoWidth > 0.0f);
        const float Height = Projection.OrthoWidth / AspectRatio;
        Result.M[1][0] = 2.0f / Projection.OrthoWidth;
        Result.M[2][1] = 2.0f / Height;
        Result.M[0][2] = 1.0f / (Projection.FarClip - Projection.NearClip);
        Result.M[3][2] = -Projection.NearClip / (Projection.FarClip - Projection.NearClip);
        Result.M[3][3] = 1.0f;
    }
    return Result;
}
