#pragma once
#include "LocalLightComponent.h"

class UPointLightComponent : public ULocalLightComponent
{
	DECLARE_CLASS(UPointLightComponent, ULocalLightComponent)
	REFLECT_START(ClassName)
	PROPERTY(LightFalloffExponent)
	REFLECT_END()
  public:
	virtual ELightComponentType GetLightType() const override;
	virtual FSphere GetBoundingSphere() const override;
	virtual FLightData CreateLightData() const override;

	float LightFalloffExponent = 5.0f; // 빛 감쇠 지수(빛이 닿는 범위 내 분포)

	void SetLightFalloffExponent(float InLightFalloffExponent) { LightFalloffExponent = InLightFalloffExponent; }
	
};