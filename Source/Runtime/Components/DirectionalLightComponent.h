#pragma once
#include "LightComponent.h"
#include "Math/Sphere.h"

class UDirectionalLightComponent : public ULightComponent
{
	DECLARE_CLASS(UDirectionalLightComponent, ULightComponent)
public:
	
	FVector LightDirection;

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
};