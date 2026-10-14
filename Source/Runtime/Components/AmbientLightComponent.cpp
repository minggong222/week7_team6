#include "EnginePCH.h"
#include "PointLightComponent.h"
#include "Rendering/LineBatcher.h"
#include "AmbientLightComponent.h"

UAmbientLightComponent::UAmbientLightComponent()
{
	LightColor = FLinearColor{
		0.1f,
		0.1f,
		0.1f,
		1.0f,
	};
}
