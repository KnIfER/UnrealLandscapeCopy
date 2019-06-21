// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "CyLandInfo.h"
#include "Components/PrimitiveComponent.h"
#include "CyLandSplinesComponent.generated.h"

class ACyLandProxy;
class FPrimitiveSceneProxy;
class UCyControlPointMeshComponent;
class UCyLandSplineControlPoint;
class UCyLandSplineSegment;
class UMeshComponent;
class USplineMeshComponent;
class UStaticMesh;
class UTexture2D;

// structs for ForeignWorldSplineDataMap
// these are editor-only, but we don't have the concept of an editor-only USTRUCT
USTRUCT()
struct FCyForeignControlPointData
{
	GENERATED_USTRUCT_BODY()

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FGuid ModificationKey;

	UPROPERTY()
	UCyControlPointMeshComponent* MeshComponent;

	UPROPERTY()
	TLazyObjectPtr<UCyLandSplineControlPoint> Identifier;

	friend bool operator==(const FCyForeignControlPointData& LHS, const FCyForeignControlPointData& RHS)
	{
		return LHS.Identifier == RHS.Identifier;
	}
#endif
};

USTRUCT()
struct FCyForeignSplineSegmentData
{
	GENERATED_USTRUCT_BODY()

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FGuid ModificationKey;

	UPROPERTY()
	TArray<USplineMeshComponent*> MeshComponents;

	UPROPERTY()
	TLazyObjectPtr<UCyLandSplineSegment> Identifier;

	friend bool operator==(const FCyForeignSplineSegmentData& LHS, const FCyForeignSplineSegmentData& RHS)
	{
		return LHS.Identifier == RHS.Identifier;
	}
#endif
};

USTRUCT()
struct FCyForeignWorldSplineData
{
	GENERATED_USTRUCT_BODY()

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TMap<TLazyObjectPtr<UCyLandSplineControlPoint>, FCyForeignControlPointData> ForeignControlPointDataMap_DEPRECATED;

	UPROPERTY()
	TArray<FCyForeignControlPointData> ForeignControlPointData;

	UPROPERTY()
	TMap<TLazyObjectPtr<UCyLandSplineSegment>, FCyForeignSplineSegmentData> ForeignSplineSegmentDataMap_DEPRECATED;

	UPROPERTY()
	TArray<FCyForeignSplineSegmentData> ForeignSplineSegmentData;
#endif

#if WITH_EDITOR
	bool IsEmpty();

	FCyForeignControlPointData* FindControlPoint(UCyLandSplineControlPoint* InIdentifer);
	FCyForeignSplineSegmentData* FindSegmentData(UCyLandSplineSegment* InIdentifer);
#endif
};

//////////////////////////////////////////////////////////////////////////
// UCyLandSplinesComponent
//////////////////////////////////////////////////////////////////////////

UCLASS(MinimalAPI)
class UCyLandSplinesComponent : public UPrimitiveComponent
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITORONLY_DATA
	/** Resolution of the spline, in distance per point */
	UPROPERTY()
	float SplineResolution;

	/** Color to use to draw the splines */
	UPROPERTY()
	FColor SplineColor;

	/** Sprite used to draw control points */
	UPROPERTY()
	UTexture2D* ControlPointSprite;

	/** Mesh used to draw splines that have no mesh */
	UPROPERTY()
	UStaticMesh* SplineEditorMesh;

	/** Whether we are in-editor and showing spline editor meshes */
	UPROPERTY(NonTransactional, Transient)
	uint32 bShowSplineEditorMesh:1;
#endif

protected:
	UPROPERTY(TextExportTransient)
	TArray<UCyLandSplineControlPoint*> ControlPoints;

	UPROPERTY(TextExportTransient)
	TArray<UCyLandSplineSegment*> Segments;

#if WITH_EDITORONLY_DATA
	// Serialized
	UPROPERTY(TextExportTransient)
	TMap<TSoftObjectPtr<UWorld>, FCyForeignWorldSplineData> ForeignWorldSplineDataMap;

	// Transient - rebuilt on load
	TMap<UMeshComponent*, UObject*> MeshComponentLocalOwnersMap;
	TMap<UMeshComponent*, TLazyObjectPtr<UObject>> MeshComponentForeignOwnersMap;
#endif

	// References to components owned by landscape splines in other levels
	// for cooked build (uncooked keeps references via ForeignWorldSplineDataMap)
	UPROPERTY(TextExportTransient)
	TArray<UMeshComponent*> CookedForeignMeshComponents;

public:
	/** Get a list of spline mesh components representing this landscape spline (Editor only) */
	UFUNCTION(BlueprintCallable, Category = CyLandSplines)
	TArray<USplineMeshComponent*> GetSplineMeshComponents();

	void CheckSplinesValid();
	bool ModifySplines(bool bAlwaysMarkDirty = true);

#if WITH_EDITOR
	virtual void ShowSplineEditorMesh(bool bShow);

	// Rebuilds all spline points and meshes for all spline control points and segments in this splines component
	// @param bBuildCollision Building collision data is very slow, so during interactive changes pass false to improve user experience (make sure to call with true when done)
	virtual void RebuildAllSplines(bool bBuildCollision = true);

	// returns a suitable UCyLandSplinesComponent to place streaming meshes into, given a location
	// falls back to "this" if it can't find another suitable, so never returns nullptr
	// @param bCreate whether to create a component if a suitable actor is found but it has no splines component yet
	UCyLandSplinesComponent* GetStreamingSplinesComponentByLocation(const FVector& LocalLocation, bool bCreate = true);

	// returns the matching UCyLandSplinesComponent for a given level, *can return null*
	// @param bCreate whether to create a component if a suitable actor is found but it has no splines component yet
	UCyLandSplinesComponent* GetStreamingSplinesComponentForLevel(ULevel* Level, bool bCreate = true);

	// gathers and returns all currently existing 
	TArray<UCyLandSplinesComponent*> GetAllStreamingSplinesComponents();

	virtual void UpdateModificationKey(UCyLandSplineSegment* Owner);
	virtual void UpdateModificationKey(UCyLandSplineControlPoint* Owner);
	virtual void AddForeignMeshComponent(UCyLandSplineSegment* Owner, USplineMeshComponent* Component);
	virtual void RemoveForeignMeshComponent(UCyLandSplineSegment* Owner, USplineMeshComponent* Component);
	virtual void RemoveAllForeignMeshComponents(UCyLandSplineSegment* Owner);
	virtual void AddForeignMeshComponent(UCyLandSplineControlPoint* Owner, UCyControlPointMeshComponent* Component);
	virtual void RemoveForeignMeshComponent(UCyLandSplineControlPoint* Owner, UCyControlPointMeshComponent* Component);
	virtual void DestroyOrphanedForeignMeshComponents(UWorld* OwnerWorld);
	virtual UCyControlPointMeshComponent*   GetForeignMeshComponent(UCyLandSplineControlPoint* Owner);
	virtual TArray<USplineMeshComponent*> GetForeignMeshComponents(UCyLandSplineSegment* Owner);

	virtual UObject* GetOwnerForMeshComponent(const UMeshComponent* SplineMeshComponent);

	void AutoFixMeshComponentErrors(UWorld* OtherWorld);

	bool IsUsingEditorMesh(const USplineMeshComponent* SplineMeshComponent) const;
#endif

	//~ Begin UObject Interface

	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif // WITH_EDITOR
	//~ End UObject Interface

	//~ Begin UActorComponent Interface
#if WITH_EDITOR
	virtual void CheckForErrors() override;
#endif
	virtual void OnRegister() override;
	//~ End UActorComponent Interface
	
	//~ Begin UPrimitiveComponent Interface.
#if WITH_EDITOR
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
#endif
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End UPrimitiveComponent Interface.

	// many friends
	friend class FCyLandToolSplines;
	friend class FCyLandSplinesSceneProxy;
	friend class UCyLandSplineControlPoint;
	friend class UCyLandSplineSegment;
#if WITH_EDITOR
	// TODO - move this out of UCyLandInfo
	friend bool UCyLandInfo::ApplySplinesInternal(bool bOnlySelected, ACyLandProxy* CyLand);
#endif
};
