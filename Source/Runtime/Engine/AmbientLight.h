#pragma once

#include "Engine/Light.h"
#include "Components/ArrowComponent.h"

// 에디터에서 아이콘(빌보드) 및 방향이 보인다.
class AAmbientLight : public ALight
{
	DECLARE_CLASS(AAmbientLight, ALight)

public:
	AAmbientLight();
	virtual ~AAmbientLight() override = default;

private:
	UArrowComponent* ArrowComponent = nullptr;
};
