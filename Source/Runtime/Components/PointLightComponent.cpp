#include "EnginePCH.h"
#include "PointLightComponent.h"

ELightComponentType UPointLightComponent::GetLightType() const
{
	//필수구현
	return ELightComponentType::LightType_Point;
};

FSphere UPointLightComponent::GetBoundingSphere() const
{
	// 빛이 커버하는 영역 전체를 덮게 정의해주세요. 단 디렉셔널/앰비언트는 구현 필요 X
	return FSphere{};
};