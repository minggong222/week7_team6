#pragma once
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include <algorithm>
#include <immintrin.h>
#include <limits>

struct FBox
{
	FVector Min;
	FVector Max;
	FBox() : Min((std::numeric_limits<float>::max)()), Max(std::numeric_limits<float>::lowest()) {}
	FBox(const FVector& InMin, const FVector& InMax) : Min(InMin), Max(InMax) {}
	FBox(const FVector& Center, float Radius) : Min(Center - FVector(Radius)), Max(Center + FVector(Radius)) {}
	bool IsValid() const
	{
		return Min.X <= Max.X && Min.Y <= Max.Y && Min.Z <= Max.Z;
	}
	FBox& operator+=(const FBox& Other)
	{
		if (!Other.IsValid()) return *this;
		if (!IsValid())
		{
			*this = Other;
			return *this;
		}
		Min = FVector((std::min)(Min.X, Other.Min.X), (std::min)(Min.Y, Other.Min.Y), (std::min)(Min.Z, Other.Min.Z));
		Max = FVector((std::max)(Max.X, Other.Max.X), (std::max)(Max.Y, Other.Max.Y), (std::max)(Max.Z, Other.Max.Z));
		return *this;
	}
	FBox operator+(const FBox& Other) const
	{
		FBox Result = *this;
		Result += Other;
		return Result;
	}
	FBox& operator+=(const FVector& Point)
	{
		return *this += FBox(Point, Point);
	}
	FBox GetWorldAABB(const FMatrix& M) const
	{
		if (!IsValid()) return {};
		const __m128 vHalf = _mm_set1_ps(0.5f);
		const __m128 vMin = _mm_setr_ps(Min.X, Min.Y, Min.Z, 0.0f);
		const __m128 vMax = _mm_setr_ps(Max.X, Max.Y, Max.Z, 0.0f);
		const __m128 vCenter = _mm_mul_ps(_mm_add_ps(vMin, vMax), vHalf);
		const __m128 vExtent = _mm_mul_ps(_mm_sub_ps(vMax, vMin), vHalf);
		const __m128 Row0 = _mm_loadu_ps(M.M[0]);
		const __m128 Row1 = _mm_loadu_ps(M.M[1]);
		const __m128 Row2 = _mm_loadu_ps(M.M[2]);
		const __m128 Row3 = _mm_loadu_ps(M.M[3]);
		const __m128 CX = _mm_shuffle_ps(vCenter, vCenter, _MM_SHUFFLE(0, 0, 0, 0));
		const __m128 CY = _mm_shuffle_ps(vCenter, vCenter, _MM_SHUFFLE(1, 1, 1, 1));
		const __m128 CZ = _mm_shuffle_ps(vCenter, vCenter, _MM_SHUFFLE(2, 2, 2, 2));
		__m128 WorldCenter = _mm_add_ps(_mm_mul_ps(CX, Row0), _mm_mul_ps(CY, Row1));
		WorldCenter = _mm_add_ps(WorldCenter, _mm_add_ps(_mm_mul_ps(CZ, Row2), Row3));
		const __m128 AbsMask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
		const __m128 AbsRow0 = _mm_and_ps(Row0, AbsMask);
		const __m128 AbsRow1 = _mm_and_ps(Row1, AbsMask);
		const __m128 AbsRow2 = _mm_and_ps(Row2, AbsMask);
		const __m128 EX = _mm_shuffle_ps(vExtent, vExtent, _MM_SHUFFLE(0, 0, 0, 0));
		const __m128 EY = _mm_shuffle_ps(vExtent, vExtent, _MM_SHUFFLE(1, 1, 1, 1));
		const __m128 EZ = _mm_shuffle_ps(vExtent, vExtent, _MM_SHUFFLE(2, 2, 2, 2));
		__m128 WorldExtent = _mm_add_ps(_mm_mul_ps(EX, AbsRow0), _mm_mul_ps(EY, AbsRow1));
		WorldExtent = _mm_add_ps(WorldExtent, _mm_mul_ps(EZ, AbsRow2));
		const __m128 NewMin = _mm_sub_ps(WorldCenter, WorldExtent);
		const __m128 NewMax = _mm_add_ps(WorldCenter, WorldExtent);
		alignas(16) float OutMin[4];
		alignas(16) float OutMax[4];
		_mm_store_ps(OutMin, NewMin);
		_mm_store_ps(OutMax, NewMax);
		return FBox(FVector(OutMin[0], OutMin[1], OutMin[2]), FVector(OutMax[0], OutMax[1], OutMax[2]));
	}
};
