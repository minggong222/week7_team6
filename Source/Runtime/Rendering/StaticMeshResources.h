#pragma once

#include "Vertex.h"
#include "Math/Box.h"

// Cooked obj Data의 Section 구조체
struct FStaticMeshSection
{
	uint32 StartIndex = 0;
	uint32 IndexCount = 0;
	uint32 MaterialSlotIndex = 0;
};

struct FStaticMaterial
{
	FString Name;
	FVector4 BaseColor = FVector4(1, 1, 1, 1);
	FVector SpecularColor = FVector(1.0f, 1.0f, 1.0f);
	FVector EmissiveColor = FVector(0.0f, 0.0f, 0.0f);
	float Shininess = 16.0f;
	float NormalScale = 1.0f;
	FString DiffuseTexturePath;
	FString NormalTexturePath;
	FString SpecularTexturePath;
	FString EmissiveTexturePath;
	FString OpacityTexturePath; // map_d. 비어 있지 않으면 텍스처 알파로 투명도를 정한다
};

// 메시 로컬 공간에서 피킹 Ray가 통과할 삼각형만 찾기 위한 가속 노드다.
struct FMeshPickingBVHNode
{
	FBox Bounds{};
	uint32 Left = 0;
	uint32 Right = 0;
	uint32 First = 0;
	uint32 Count = 0;
	bool bLeaf = false;
};

// 피킹 BVH의 4갈래 노드 (World·Mesh 공용). 이진 트리의 두 층을 한 층으로 합쳐 내려가는 단계를 절반으로 줄인다.
// 자식 박스 4개를 축별 배열로 모아 SIMD로 한 번에 검사한다. 128B = 캐시 라인 2개.
struct alignas(64) FPickingBVHNode4
{
	static constexpr uint32 EmptySlot = static_cast<uint32>(-1);

	float MinX[4];
	float MinY[4];
	float MinZ[4];
	float MaxX[4];
	float MaxY[4];
	float MaxZ[4];
	uint32 Child[4]; // Count > 0: 리프의 첫 원소 인덱스, Count == 0: 자식 4갈래 노드 인덱스
	uint32 Count[4]; // 리프 원소 수. 0은 안쪽 노드, EmptySlot은 빈 칸

	void SetBounds(const uint32 Slot, const FBox& Bounds)
	{
		MinX[Slot] = Bounds.Min.X;
		MinY[Slot] = Bounds.Min.Y;
		MinZ[Slot] = Bounds.Min.Z;
		MaxX[Slot] = Bounds.Max.X;
		MaxY[Slot] = Bounds.Max.Y;
		MaxZ[Slot] = Bounds.Max.Z;
	}
};

// 피킹 전용 삼각형. 세 꼭짓점 위치만 BVH 리프 순서로 연속 저장해 정밀 검사 때 인덱스·정점 간접 참조를 없앤다.
struct FPickingTriangle
{
	FVector V0;
	FVector V1;
	FVector V2;
};

struct FStaticMeshRenderData
{
	TArray<FVertexPNCTT> Vertices;
	TArray<uint32> Indices;
	TArray<FStaticMeshSection> Sections;
	TArray<FStaticMaterial> MaterialSlots;
	FBox AABB;

	// 동일 Mesh를 사용하는 모든 Component가 공유한다. 첫 정밀 피킹 때 한 번만 구축한다.
	mutable TArray<uint32> PickingTriangleIndices;
	mutable TArray<FMeshPickingBVHNode> PickingBVHNodes;
	// 피킹 순회용 4갈래 노드. 이진 노드(PickingBVHNodes)를 구축한 뒤 두 층씩 합쳐 만든다.
	mutable TArray<FPickingBVHNode4> PickingBVHNodes4;
	// PickingTriangleIndices와 같은 순서(BVH 리프 순서)의 삼각형 위치. BVH 리프 검사는 이 배열만 읽는다.
	mutable TArray<FPickingTriangle> PickingTriangles;
	mutable bool bPickingBVHBuilt = false;

	void InvalidatePickingBVH()
	{
		PickingTriangleIndices.Reset();
		PickingBVHNodes.Reset();
		PickingBVHNodes4.Reset();
		PickingTriangles.Reset();
		bPickingBVHBuilt = false;
	}

	// TODO: 나중에 Sections, MaterialSlots 도 Append 해줘야 함.
	void Append(const FStaticMeshRenderData& Other)
	{
		InvalidatePickingBVH();
		uint32 Base = (uint32)Vertices.Num();

		Vertices.Append(Other.Vertices);

		for (uint32 i : Other.Indices)
			Indices.Add(Base + i);
	}

	void Translate(const FVector& Offset)
	{
		InvalidatePickingBVH();
		for (auto& V : Vertices)
			V.Position += Offset;
	}

	// Position, UV, Normal로 Tangent(xyz)와 handedness(w)를 채운다. UV가 퇴화한 정점은 xyz가 0으로 남는다.
	void CalculateTangents();

	// Vertices, Indices, Sections, MaterialSlots가 서로 맞는지 검사한다.
	// 실패하면 이유를 OutError에 담는다. 로그는 호출한 쪽이 경로와 함께 남긴다.
	bool Validate(FString& OutError) const;
};
