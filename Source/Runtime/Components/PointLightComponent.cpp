#include "EnginePCH.h"
#include "PointLightComponent.h"

#include "Rendering/LineBatcher.h"

ELightComponentType UPointLightComponent::GetLightType() const
{
	//필수구현
	return ELightComponentType::LightType_Point;
};

FSphere UPointLightComponent::GetBoundingSphere() const
{
	// 빛이 커버하는 영역 전체를 덮게 정의해주세요. 단 디렉셔널/앰비언트는 구현 필요 X
	FSphere Sphere = FSphere();
	Sphere.Center = GetComponentLocation();
	Sphere.Radius = AttenuationRadius;
	return Sphere;
};

FLightData UPointLightComponent::CreateLightData() const
{
	FLightData LightData = FLightData();
	LightData.LightType = GetLightType();
	LightData.Position = GetComponentLocation();
	FVector LightRGB = FVector(LightColor.R, LightColor.G, LightColor.B) * Intensity;
	LightData.LightColor = FVector4(LightRGB, LightColor.A);
	LightData.AttenuationRadius = AttenuationRadius;
	LightData.LightFalloffExponent = LightFalloffExponent;

	return LightData;
}

void UPointLightComponent::DrawDebug(FLineBatcher* LineBatcher) const
{
	if (LineBatcher == nullptr)
	{
		return;
	}

	const float SafeRadius = (AttenuationRadius > 0.0f) ? AttenuationRadius : 0.0f;

	LineBatcher->AddWireSphere(GetComponentLocation(), SafeRadius, FVector4(LightColor.R, LightColor.G, LightColor.B, 1.0f));
}