#pragma once

#include "LightComponent.h"
#include "GameFramework/Actor.h"
#include "Math/Quat.h"
#include "Materials/Material.h"
#include "Components/BillboardComponent.h"
class FLineBatcher;

class UAmbientLightComponent : public ULightComponent
{
	DECLARE_CLASS(UAmbientLightComponent, ULightComponent)

public:
	UAmbientLightComponent();
	virtual ~UAmbientLightComponent() override = default;

	virtual ELightComponentType GetLightType() const override
	{
		return LightType_Ambient;
	};

	virtual FLightData CreateLightData() const override
	{
		FLightData LightData{};
		LightData.LightType = static_cast<uint32>(LightType_Ambient);

		FLinearColor Color = GetLightColor();
		LightData.Color = {Color.R, Color.G, Color.B};

		LightData.Intensity = Color.A;

		UBillboardComponent* BillboardComponent = GetOwner()->FindComponentByClass<UBillboardComponent>();

		if (BillboardComponent)
		{
			UMaterial* Material = BillboardComponent->GetMaterial(0);
			Material->BaseColor = {Color.R, Color.G, Color.B, Color.A};
		}

		return LightData;
	}
};
