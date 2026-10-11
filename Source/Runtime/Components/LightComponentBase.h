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

	const FLinearColor& GetLightColor() const { return LightColor; }
	void SetLightColor(const FLinearColor& InColor) { LightColor = InColor; }

	float Intensity = 1;
	FLinearColor LightColor = {1.0f,1.0f,1.0f,1.0f};
	bool bVisible = true;
};