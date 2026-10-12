#include "EnginePCH.h"
#include "PointLightComponent.h"
#include "Rendering/LineBatcher.h"

namespace
{
void AddWireSphere(FLineBatcher* LineBatcher, const FVector& Center, float Radius, const FVector4& Color)
	{
		// 원 하나의 분할 수
		constexpr int32 SPHERE_SEGMENT_COUNT = 32;

		const FVector AxisX(1.0f, 0.0f, 0.0f);
		const FVector AxisY(0.0f, 1.0f, 0.0f);
		const FVector AxisZ(0.0f, 0.0f, 1.0f);
		const FVector PlaneAxes[3][2] = {{AxisX, AxisY}, {AxisY, AxisZ}, {AxisX, AxisZ}};

		for (const auto& Axes : PlaneAxes)
		{
			FVector PreviousPoint = Center + Axes[0] * Radius;
			for (int32 i = 1; i <= SPHERE_SEGMENT_COUNT; ++i)
			{
				const float Angle = (2.0f * PI * i) / SPHERE_SEGMENT_COUNT;
				const FVector Point = Center + Axes[0] * (std::cosf(Angle) * Radius) + Axes[1] * (std::sinf(Angle) * Radius);

				LineBatcher->AddLine(PreviousPoint, Point, Color);
				PreviousPoint = Point;
			}
		}
	}
}


void UPointLightComponent::DrawDebug(FLineBatcher* LineBatcher) const
{
	if (LineBatcher == nullptr)
	{
		return;
	}

	const float SafeRadius = (AttenuationRadius > 0.0f) ? AttenuationRadius : 0.0f;

	AddWireSphere(LineBatcher, GetComponentLocation(), SafeRadius, FVector4(LightColor.R, LightColor.G, LightColor.B, 1.0f));
}
