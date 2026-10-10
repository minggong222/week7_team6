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
	float Intensity = 0;
	FLinearColor LightColor = {0.0f,0.0f,0.0f,1.0f};
	bool bVisible = true;
};