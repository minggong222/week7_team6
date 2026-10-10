#pragma once
#include "Math/Color.h"
#include "SceneComponent.h"

class ULightComponentBase : public USceneComponent
{
	DECLARE_CLASS(ULightComponentBase, USceneComponent)
	REFLECT_START(ClassName)
	PROPERTY(Intensity)
	PROPERTY_TYPE(LightColor, Color)
	PROPERTY(bVisible)
	REFLECT_END()
  public:
	float Intensity;
	FLinearColor LightColor;
	bool bVisible;
};