#pragma once

#include "Engine/Light.h"
#include "Components/ArrowComponent.h"

// 에디터에서 아이콘(빌보드) 및 방향이 보인다.
class ADirectionalLight : public ALight
{
	DECLARE_CLASS(ADirectionalLight, ALight)

public:
	ADirectionalLight();
	virtual ~ADirectionalLight() override = default;

	virtual void DuplicateSubObjects() override;

private:
	UArrowComponent* ArrowComponent = nullptr;
};
