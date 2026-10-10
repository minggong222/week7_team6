#pragma once
#include "LightComponentBase.h"
#include "Math/Sphere.h"

enum ELightComponentType
{
	LightType_Directional,
	LightType_Point,
	LightType_Spot,
	LightType_Rect,
	LightType_Ambient, // 언리얼엔 미존재
	LightType_MAX
};

class ULightComponent : public ULightComponentBase
{
	DECLARE_CLASS(ULightComponent, ULightComponentBase)
  public:
	virtual ELightComponentType GetLightType() const
	{
		//필수구현
		return LightType_MAX;
	};
	virtual FSphere GetBoundingSphere() const
	{
		// 빛이 커버하는 영역 전체를 덮게 정의해주세요. 단 디렉셔널/앰비언트는 구현 필요 X
		return FSphere{};
	};
};