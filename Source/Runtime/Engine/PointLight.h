#pragma once

#include "Engine/Light.h"

// 에디터에서 아이콘(빌보드)으로 보이고, 선택하면 스포트라이트 원뿔이 라인으로 그려진다.
class APointLight : public ALight
{
	DECLARE_CLASS(APointLight, ALight)

	REFLECT_START(ClassName)
	REFLECT_END()

public:
	APointLight();
	virtual ~APointLight() override = default;

	virtual void DuplicateSubObjects() override;
};
