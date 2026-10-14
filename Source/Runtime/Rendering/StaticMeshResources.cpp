#include "EnginePCH.h"
#include "Rendering/StaticMeshResources.h"

#include <format>

void FStaticMeshRenderData::CalculateTangents()
{
	TArray<FVector> Tangents;
	TArray<FVector> Bitangents;
	Tangents.SetNum(Vertices.Num());
	Bitangents.SetNum(Vertices.Num());

	for (int32 i = 0; i + 2 < static_cast<int32>(Indices.Num()); i += 3)
	{
		const uint32 I0 = Indices[i];
		const uint32 I1 = Indices[i + 1];
		const uint32 I2 = Indices[i + 2];

		const FVector Edge1 = Vertices[I1].Position - Vertices[I0].Position;
		const FVector Edge2 = Vertices[I2].Position - Vertices[I0].Position;
		const FVector2D DeltaUV1 = Vertices[I1].UV - Vertices[I0].UV;
		const FVector2D DeltaUV2 = Vertices[I2].UV - Vertices[I0].UV;

		const float determinantTerm0 = DeltaUV1.X * DeltaUV2.Y;
		const float determinantTerm1 = DeltaUV1.Y * DeltaUV2.X;
		const float determinant = determinantTerm0 - determinantTerm1;
		const float determinantScale = std::abs(determinantTerm0) + std::abs(determinantTerm1);

		if (determinantScale < 1e-20f || std::abs(determinant) < determinantScale * 1e-6f)
			continue;

		const float inverseDeterminant = 1.0f / determinant;
		const FVector T = (Edge1 * DeltaUV2.Y - Edge2 * DeltaUV1.Y) * inverseDeterminant;
		const FVector B = (Edge2 * DeltaUV1.X - Edge1 * DeltaUV2.X) * inverseDeterminant;

		Tangents[I0] += T;
		Tangents[I1] += T;
		Tangents[I2] += T;
		Bitangents[I0] += B;
		Bitangents[I1] += B;
		Bitangents[I2] += B;
	}

	for (int32 v = 0; v < Vertices.Num(); ++v)
	{
		const FVector N = Vertices[v].Normal.Normalized();
		FVector T = Tangents[v] - N * N.Dot(Tangents[v]);

		if (T.Length() < 1e-6f)
		{
			Vertices[v].Tangent = FVector4(0.0f, 0.0f, 0.0f, 1.0f);
			continue;
		}
		T = T.Normalized();

		const float handedness = N.Cross(T).Dot(Bitangents[v]) < 0.0f ? -1.0f : 1.0f;
		Vertices[v].Tangent = FVector4(T.X, T.Y, T.Z, handedness);
	}
}

bool FStaticMeshRenderData::Validate(FString& OutError) const
{
	const uint32 VertexCount = static_cast<uint32>(Vertices.Num());
	const uint32 IndexCount = static_cast<uint32>(Indices.Num());

	if (VertexCount == 0 || IndexCount == 0 || IndexCount % 3 != 0)
	{
		OutError = std::format("invalid mesh (vertices {}, indices {})", VertexCount, IndexCount);
		return false;
	}

	for (uint32 Index : Indices)
	{
		if (Index >= VertexCount)
		{
			OutError = std::format("index {} out of range (vertices {})", Index, VertexCount);
			return false;
		}
	}

	uint32 SectionIndexSum = 0;
	for (const FStaticMeshSection& Section : Sections)
	{
		if (Section.StartIndex + Section.IndexCount > IndexCount)
		{
			OutError = std::format("section range {}+{} exceeds indices {}", Section.StartIndex, Section.IndexCount, IndexCount);
			return false;
		}
		if (Section.MaterialSlotIndex >= static_cast<uint32>(MaterialSlots.Num()))
		{
			OutError = std::format("material slot {} out of range (slots {})", Section.MaterialSlotIndex, MaterialSlots.Num());
			return false;
		}
		SectionIndexSum += Section.IndexCount;
	}
	if (SectionIndexSum != IndexCount)
	{
		OutError = std::format("section index sum {} != indices {}", SectionIndexSum, IndexCount);
		return false;
	}

	return true;
}
