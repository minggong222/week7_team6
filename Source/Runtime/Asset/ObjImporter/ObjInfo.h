// .obj/.mtl 파일의 정보를 담는 구조체
// 추후 관련된 경로 기준을 '엔진 프로젝트 루트' -> '게임 프로젝트 루트'로 변경해야 함
#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "HAL/Platform.h"
#include "Math/Vector.h"
#include "Math/Vector2D.h"

#include <functional>

struct FObjIndex
{
	int32 PositionIndex = -1;
	int32 UVIndex = -1;
	int32 NormalIndex = -1;
	int32 FaceIndex = -1;

	bool operator==(const FObjIndex&) const = default;
};

// Cook에서 (P, T, N) 조합으로 정점 중복을 제거할 때 쓰는 해시
struct FObjIndexHash
{
	size_t operator()(const FObjIndex& Index) const
	{
		size_t Hash = std::hash<int32>{}(Index.PositionIndex);
		Hash = Hash * 31 + std::hash<int32>{}(Index.UVIndex);
		Hash = Hash * 31 + std::hash<int32>{}(Index.NormalIndex);
		Hash = Hash * 31 + std::hash<int32>{}(Index.FaceIndex);
		return Hash;
	}
};

struct FObjFace
{
	TArray<FObjIndex> Indexes;
};

struct FObjSection
{
	FString Material;
	int32 MaterialIndex = 0;
	int32 Start = 0; // Faces Index
	int32 Count = 0;
};

struct FObjMaterialInfo
{
	FString Name;
	FVector Ka = FVector(1.0f, 1.0f, 1.0f);
	FVector Kd = FVector(1.0f, 1.0f, 1.0f);
	FVector Ks = FVector(0.0f, 0.0f, 0.0f);
	FVector Ke = FVector(0.0f, 0.0f, 0.0f);
	float Ns = 32.0f;
	float D = 1.0f;
	float BumpScale = 1.0f; // map_Bump의 -bm 옵션
	int32 Illum = 2;		// 0, 1: 스페큘러 없음 / 2 이상: 스페큘러 있음

	// 텍스처 경로는 모두 저장소 루트 기준
	FString MapKd;
	FString MapKs;
	FString MapKe;
	FString MapBump;
	FString MapD;
};

struct FObjInfo
{
	FString Path; // 저장소 루트 기준
	TArray<FVector> Positions;
	TArray<FVector2D> UVs;
	TArray<FVector> Normals;
	TArray<FVector4> Tangents;
	TArray<FVector4> Bitangents;
	TArray<FObjFace> Faces;
	TArray<FObjSection> Sections;
	TArray<FString> Mtllibs; // .obj 폴더 기준
	TArray<FObjMaterialInfo> MaterialInfos;
};
