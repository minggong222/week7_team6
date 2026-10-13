#pragma once

#include "Math/Vector.h"
#include "Math/Vector2D.h"
#include "Math/Vector4.h"

struct FVertex
{
	FVector Position = FVector();
	FVector2D UV = FVector2D();
	FVector4 Color = FVector4();
};

struct FTextVertex
{
	FVector Position;
	FVector2D UV;
};

// Todo: subuv
struct FParticleVertex
{
	FVector Position;
	FVector2D UV;
};

// StaticMesh용 PNCTT Vertex: Position / Normal / Color / Texcoord(UV) / Tangent
struct FVertexPNCTT
{
	FVector Position = FVector();
	FVector Normal = FVector();
	FVector4 Color = FVector4();
	FVector2D UV = FVector2D();
	FVector4 Tangent = FVector4();
};
