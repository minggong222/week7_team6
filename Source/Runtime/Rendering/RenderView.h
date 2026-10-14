#pragma once

#include "Math/Matrix.h"
#include "Math/Quat.h"
#include "Math/Vector.h"

enum class ECameraProjectionMode { Perspective, Orthographic };

struct FCameraTransform
{
    FVector Location;
    FQuat Rotation{0.0f, 0.0f, 0.0f, 1.0f};
};

struct FCameraProjection
{
    ECameraProjectionMode Mode;
    float FovDegrees;
    float OrthoWidth;
    float NearClip;
    float FarClip;
};

struct FViewCamera
{
    FCameraTransform Transform;
    FCameraProjection Projection;
};

struct FRenderViewData
{
    uint32 Width = 0;
    uint32 Height = 0;
    FViewCamera Camera{};
    FMatrix View;
    FMatrix Projection;
    FMatrix ViewProjection;
    FVector CameraForward;
};

FMatrix BuildProjectionMatrix(const FCameraProjection& Projection, float AspectRatio);
