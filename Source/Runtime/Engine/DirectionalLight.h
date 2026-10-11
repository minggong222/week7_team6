#pragma once

#include "Engine/Light.h"

// 에디터에서 아이콘(빌보드)으로 보이고, 선택하면 스포트라이트 원뿔이 라인으로 그려진다.
class ADirectionalLight : public ALight
{
	DECLARE_CLASS(ADirectionalLight, ALight)

	REFLECT_START(ClassName)
	REFLECT_END()

public:
	ADirectionalLight();
	virtual ~ADirectionalLight() override = default;

	virtual void DuplicateSubObjects() override;
};
