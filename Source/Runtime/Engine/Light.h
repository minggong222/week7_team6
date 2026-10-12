#pragma once

#include "GameFramework/Actor.h"
#include "Components/BillboardComponent.h"
#include "Components/SpotLightComponent.h"

// 에디터에서 아이콘(빌보드)으로 보이고, 선택하면 스포트라이트 원뿔이 라인으로 그려진다.
class ALight : public AActor
{
	DECLARE_CLASS(ALight, AActor)

	REFLECT_START(ClassName)
	REFLECT_END()

public:
	ALight();
	virtual ~ALight() override = default;

	UBillboardComponent* GetBillboardComponent() const { return BillboardComponent; }
	void SetBillboardComponent(UBillboardComponent* InComponent) { BillboardComponent = InComponent; }
	ULightComponent* GetLightComponent() const { return LightComponent; }
	void SetLightComponent(ULightComponent* InComponent) { LightComponent = InComponent; }
	virtual void DuplicateSubObjects() override;

  private:
	// 클릭해서 고를 수 있어야 하므로 프리미티브인 빌보드를 루트로 둔다
	UBillboardComponent* BillboardComponent = nullptr;
	ULightComponent* LightComponent = nullptr;
};
