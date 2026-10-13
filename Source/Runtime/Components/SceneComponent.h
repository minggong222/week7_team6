#pragma once

#include "../Math/Transform.h"
#include "ActorComponent.h"
#include "Rendering/StaticMeshResources.h"
#include "Engine/EngineTypes.h"

struct FHitResult;

class USceneComponent : public UActorComponent
{
	DECLARE_CLASS(USceneComponent, UActorComponent)

	REFLECT_START(ClassName)
	PROPERTY(Transform)
	REFLECT_END()

  public:
	USceneComponent() = default;
	virtual ~USceneComponent() override;

	// Get & Set
	const FVector& GetRelativeLocation() const
	{
		return Transform.Location;
	}
	void SetRelativeLocation(const FVector& InLocation)
	{
		Transform.Location = InLocation;
		MarkTransformDirtyRecursive();
	}

	const FRotator& GetRelativeRotation() const
	{
		return Transform.Rotation;
	}
	void SetRelativeRotation(const FRotator& InRotation)
	{
		Transform.Rotation = InRotation;
		MarkTransformDirtyRecursive();
	}

	const FVector& GetRelativeScale3D() const
	{
		return Transform.Scale;
	}
	void SetRelativeScale3D(const FVector& InScale)
	{
		Transform.Scale = InScale;
		MarkTransformDirtyRecursive();
	}

	// 쿼터니언 적용된 회전행렬
	FQuat GetRelativeRotationQuat() const
	{
		return Transform.GetOrientation();
	}

	const FTransform& GetRelativeTransform() const
	{
		return Transform;
	}
	void SetRelativeTransform(const FTransform& InTransform)
	{
		Transform = InTransform;
		MarkTransformDirtyRecursive();
	}
	virtual void Serialize(json& Handle, bool bIsLoading) override;

	// Attatch-To
	USceneComponent* GetAttachParent() const
	{
		return AttachParent;
	}
	const TArray<USceneComponent*>& GetAttachChildren() const
	{
		return AttachChildren;
	}
	void SetupAttachment(USceneComponent* InParent);
	void DetachFromComponent();

	virtual FBox CalcLocalBounds() const
	{
		return FBox{FVector(), FVector()};
	}
	FBox CalcBounds() const
	{
		return CalcLocalBounds().GetWorldAABB(GetWorldMatrix());
	}

	FVector GetComponentLocation() const;
	FRotator GetComponentRotation() const;
	FVector GetComponentScale() const;

	FMatrix GetWorldMatrix() const;
	
	// 쉐이더에서 Normal Transformation에 쓰이는 행렬
	FMatrix GetNormalMatrix() const;

	void UpdateWorldMatrix() const;

	// Bounds 또는 Transform 변경을 현재 컴포넌트와 모든 자식 캐시에 전파한다.
	void MarkBoundsDirty()
	{
		MarkBoundsDirtyRecursive();
	}
	void MarkBoundsDirtyRecursive();
	void MarkTransformDirty()
	{
		MarkTransformDirtyRecursive();
	}
	void MarkTransformDirtyRecursive();
	uint64 GetBoundsRevision() const
	{
		return BoundsRevision;
	}

	//더티할 때만 새로 계산하고, 아니면 캐시된 박스 즉시 반환!
	const FBox GetWorldBounds()
	{
		if (bBoundsDirty)
		{
			CachedWorldBounds = CalcBounds();
			bBoundsDirty = false;
		}
		return CachedWorldBounds;
	}

	virtual void DuplicateSubObjects() override;

	// 월드 공간 Delta만큼 이동하고 월드 회전을 NewRotation으로 설정한다. 움직였으면 true.
	// SceneComponent는 충돌이 없으므로 bSweep, Teleport는 무시한다.
	virtual bool MoveComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* OutHit = nullptr, ETeleportType Teleport = ETeleportType::None);

	void SetIsEditorOnly(bool Value);
	bool GetIsEditorOnly() const { return bIsEditorOnly; }

	void SetIsVisualizationComponent(bool Value);
	bool GetIsVisualizationComponent() const { return bIsVisualizationComponent; }

protected:
	virtual void OnBoundsMarkedDirty() {}

	FTransform Transform;

	USceneComponent* AttachParent = nullptr; // Attach 부모 정보
	TArray<USceneComponent*> AttachChildren;

	// 에디터 전용 컴포넌트임을 나타냅니다. true로 지정되면 picking과 scale 및 rotation 조절이 비활성화됩니다.
	bool bIsEditorOnly = false;

	// 에디터 시각화 전용 컴포넌트임을 나타냅니다. true로 지정되면 Details에 표시되지 않습니다.
	bool bIsVisualizationComponent = false;

	bool bBoundsDirty = true; // 처음 생성 시에는 계산 필요
	mutable bool bWorldMatrixDirty = true;
	uint64 BoundsRevision = 1;
	FBox CachedWorldBounds;
	mutable FMatrix CachedWorldMatrix;
	mutable FMatrix CachedNormalMatrix;
};
