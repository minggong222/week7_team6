#pragma once
#include "LightComponent.h"

class ULocalLightComponent : public ULightComponent
{
	DECLARE_CLASS(ULocalLightComponent, ULightComponent)
	REFLECT_START(ClassName)
	PROPERTY(AttenuationRadius)
	REFLECT_END()
  public:
	float AttenuationRadius; // 감쇠반경 (빛이 가시적으로 미치는 영향의 범위)

	void SetAttenuationRadius(float InRadius) { AttenuationRadius = InRadius; }
};