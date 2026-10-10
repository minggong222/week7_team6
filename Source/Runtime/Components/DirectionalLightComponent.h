#pragma once
#include "LightComponent.h"
#include "Math/Sphere.h"

class UDirectionalLightComponent : public ULightComponent
{
	DECLARE_CLASS(UDirectionalLightComponent, ULightComponent)

	REFLECT_START(UDirectionalLightComponent)
	PROPERTY(AmbientIntensity)
	PROPERTY(Intensity)
	PROPERTY(LightDirection)
	PROPERTY(DirectionalLightColor)
	PROPERTY(AmbientLightColor)
	REFLECT_END()

public:
	
	float AmbientIntensity = 0.5f;
	float Intensity = 0.6f;
	FVector LightDirection = {-1.0f, 0.0f, 0.0f};
	FVector4 DirectionalLightColor = {1.0f, 1.0f, 1.0f, 1.0f};
	FVector4 AmbientLightColor = {1.0f, 1.0f, 1.0f, 1.0f};

	virtual ELightComponentType GetLightType() const
	{
		//필수구현
		return LightType_Directional;
	};

	virtual FSphere GetBoundingSphere() const
	{
		// 빛이 커버하는 영역 전체를 덮게 정의해주세요. 단 디렉셔널/앰비언트는 구현 필요 X
		return FSphere{};
	};

	virtual FLightData CreateLightData() const
	{
		// 렌더러가 호출합니다. 정의부탁드립니다
		return FLightData{
			.LightType = LightType_Directional,

			.AmbientColor = AmbientLightColor,
			.AmbientIntensity = AmbientIntensity,

			.DirectionalDirection = LightDirection,
			.DirectionalColor = DirectionalLightColor,
			.DirectionalIntensity = Intensity,
		};
	}

};