#pragma once

#include "StaticMeshComponent.h"

class UArrowComponent : public UStaticMeshComponent
{
	DECLARE_CLASS(UArrowComponent, UStaticMeshComponent)

	REFLECT_START(ClassName)
	REFLECT_END()

public:
	UArrowComponent();
	virtual ~UArrowComponent();

};
