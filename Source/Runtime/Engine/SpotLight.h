#pragma once

#include "Engine/Light.h"
#include "Components/ArrowComponent.h"

// 에디터에서 아이콘(빌보드)으로 보이고, 선택하면 스포트라이트 원뿔이 라인으로 그려진다.
class ASpotLight : public ALight
{
	DECLARE_CLASS(ASpotLight, ALight)

public:
	ASpotLight();
	virtual ~ASpotLight() override = default;

	virtual void DuplicateSubObjects() override;

  private:
	UArrowComponent* ArrowComponent = nullptr;
};
