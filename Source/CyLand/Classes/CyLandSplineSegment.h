// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Misc/Guid.h"
#include "Components/SplineMeshComponent.h"
#include "CyLandSplinesComponent.h"
#include "CyLandSplineSegment.generated.h"

class UCyLandSplineControlPoint;
class UStaticMesh;

//Forward declarations
class UCyLandSplineControlPoint;

USTRUCT()
struct FCyLandSplineInterpPoint
{
	GENERATED_USTRUCT_BODY()

	/** Center Point */
	UPROPERTY()
	FVector Center;

	/** Left Point */
	UPROPERTY()
	FVector Left;

	/** Right Point */
	UPROPERTY()
	FVector Right;

	/** Left Falloff Point */
	UPROPERTY()
	FVector FalloffLeft;

	/** Right FalloffPoint */
	UPROPERTY()
	FVector FalloffRight;

	/** Start/End Falloff fraction */
	UPROPERTY()
	float StartEndFalloff;

	FCyLandSplineInterpPoint()
		: Center(ForceInitToZero)
		, Left(ForceInitToZero)
		, Right(ForceInitToZero)
		, FalloffLeft(ForceInitToZero)
		, FalloffRight(ForceInitToZero)
		, StartEndFalloff(0.0f)
	{
	}

	FCyLandSplineInterpPoint(FVector InCenter, FVector InLeft, FVector InRight, FVector InFalloffLeft, FVector InFalloffRight, float InStartEndFalloff) :
		Center(InCenter),
		Left(InLeft),
		Right(InRight),
		FalloffLeft(InFalloffLeft),
		FalloffRight(InFalloffRight),
		StartEndFalloff(InStartEndFalloff)
	{
	}
};

USTRUCT()
struct FCyLandSplineSegmentConnection
{
	GENERATED_USTRUCT_BODY()

	// Control point connected to this end of the segment
	UPROPERTY()
	UCyLandSplineControlPoint* ControlPoint;

	// Tangent length of the connection
	UPROPERTY(EditAnywhere, Category=CyLandSplineSegmentConnection)
	float TangentLen;

	// Socket on the control point that we are connected to
	UPROPERTY(EditAnywhere, Category=CyLandSplineSegmentConnection)
	FName SocketName;

	FCyLandSplineSegmentConnection()
		: ControlPoint(nullptr)
		, TangentLen(0.0f)
		, SocketName(NAME_None)
	{
	}
};

// Deprecated
UENUM()
enum CyLandSplineMeshOrientation
{
	CyLSMO_XUp,
	CyLSMO_YUp,
	CyLSMO_MAX,
};

USTRUCT()
struct FCyLandSplineMeshEntry
{
	GENERATED_USTRUCT_BODY()

	/** Mesh to use on the spline */
	UPROPERTY(EditAnywhere, Category=CyLandSplineMeshEntry)
	UStaticMesh* Mesh;

	/** Overrides mesh's materials */
	UPROPERTY(EditAnywhere, Category=CyLandSplineMeshEntry, AdvancedDisplay)
	TArray<UMaterialInterface*> MaterialOverrides;

	/** Whether to automatically center the mesh horizontally on the spline */
	UPROPERTY(EditAnywhere, Category=CyLandSplineMeshEntry, meta=(DisplayName="Center Horizontally"))
	uint32 bCenterH:1;

	/** Tweak to center the mesh correctly on the spline */
	UPROPERTY(EditAnywhere, Category=CyLandSplineMeshEntry, AdvancedDisplay, meta=(DisplayName="Center Adjust"))
	FVector2D CenterAdjust;

	/** Whether to scale the mesh to fit the width of the spline */
	UPROPERTY(EditAnywhere, Category=CyLandSplineMeshEntry)
	uint32 bScaleToWidth:1;

	/** Scale of the spline mesh, (Z=Forwards) */
	UPROPERTY(EditAnywhere, Category=CyLandSplineMeshEntry)
	FVector Scale;

	/** Orientation of the spline mesh, X=Up or Y=Up */
	UPROPERTY()
	TEnumAsByte<CyLandSplineMeshOrientation> Orientation_DEPRECATED;

	/** Chooses the forward axis for the spline mesh orientation */
	UPROPERTY(EditAnywhere, Category=CyLandSplineMeshEntry)
	TEnumAsByte<ESplineMeshAxis::Type> ForwardAxis;

	/** Chooses the up axis for the spline mesh orientation */
	UPROPERTY(EditAnywhere, Category=CyLandSplineMeshEntry)
	TEnumAsByte<ESplineMeshAxis::Type> UpAxis;

	FCyLandSplineMeshEntry() :
		Mesh(nullptr),
		MaterialOverrides(),
		bCenterH(true),
		CenterAdjust(0, 0),
		bScaleToWidth(true),
		Scale(1,1,1),
		Orientation_DEPRECATED(CyLSMO_YUp),
		ForwardAxis(ESplineMeshAxis::X),
		UpAxis(ESplineMeshAxis::Z)
	{
	}

	bool IsValid() const;
};


UCLASS(Within=CyLandSplinesComponent,autoExpandCategories=(CyLandSplineSegment,CyLandSplineMeshes),MinimalAPI)
class UCyLandSplineSegment : public UObject
{
	GENERATED_UCLASS_BODY()

// Directly editable data:
	UPROPERTY(EditAnywhere, EditFixedSize, Category=CyLandSplineSegment)
	FCyLandSplineSegmentConnection Connections[2];

#if WITH_EDITORONLY_DATA
	/**
	 * Name of blend layer to paint when applying spline to landscape
	 * If "none", no layer is painted
	 */
	UPROPERTY(EditAnywhere, Category=CyLandDeformation)
	FName LayerName;

	/** If the spline is above the terrain, whether to raise the terrain up to the level of the spline when applying it to the landscape. */
	UPROPERTY(EditAnywhere, Category=CyLandDeformation)
	uint32 bRaiseTerrain:1;

	/** If the spline is below the terrain, whether to lower the terrain down to the level of the spline when applying it to the landscape. */
	UPROPERTY(EditAnywhere, Category=CyLandDeformation)
	uint32 bLowerTerrain:1;

	/** Spline meshes from this list are used in random order along the spline. */
	UPROPERTY(EditAnywhere, Category=CyLandSplineMeshes)
	TArray<FCyLandSplineMeshEntry> SplineMeshes;

	UPROPERTY()
	uint32 bEnableCollision_DEPRECATED:1;

	/** Name of the collision profile to use for this spline */
	//
	// TODO: This field does not have proper Slate customization.
	// Instead of a text field, this should be a dropdown with the
	// default option.
	//
	UPROPERTY(EditAnywhere, Category=CyLandSplineMeshes)
	FName CollisionProfileName;

	/** Whether the Spline Meshes should cast a shadow. */
	UPROPERTY(EditAnywhere, Category=CyLandSplineMeshes)
	uint32 bCastShadow:1;

	/** Random seed used for choosing which order to use spline meshes. Ignored if only one mesh is set. */
	UPROPERTY(EditAnywhere, Category=CyLandSplineMeshes, AdvancedDisplay)
	int32 RandomSeed;

	/**  Max draw distance for all the mesh pieces used in this spline */
	UPROPERTY(EditAnywhere, Category=CyLandSplineMeshes, AdvancedDisplay, meta=(DisplayName="Max Draw Distance"))
	float LDMaxDrawDistance;

	/**
	 * Translucent objects with a lower sort priority draw behind objects with a higher priority.
	 * Translucent objects with the same priority are rendered from back-to-front based on their bounds origin.
	 *
	 * Ignored if the object is not translucent.  The default priority is zero.
	 * Warning: This should never be set to a non-default value unless you know what you are doing, as it will prevent the renderer from sorting correctly.
	 */
	UPROPERTY(EditAnywhere, Category=CyLandSplineMeshes, AdvancedDisplay)
	int32 TranslucencySortPriority;

	/** Whether to hide the mesh in game */
	UPROPERTY(EditAnywhere, Category=CyLandSplineMeshes, AdvancedDisplay)
	uint32 bHiddenInGame:1;

	/** Whether spline meshes should be placed in landscape proxy streaming levels (true) or the spline's level (false) */
	UPROPERTY(EditAnywhere, Category=CyLandSplineMeshes, AdvancedDisplay)
	uint32 bPlaceSplineMeshesInStreamingLevels : 1;
	
	/** Mesh Collision Settings */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Collision, meta = (ShowOnlyInnerProperties))
	FBodyInstance BodyInstance;

protected:
	UPROPERTY(Transient)
	uint32 bSelected : 1;

	UPROPERTY(Transient)
	uint32 bNavDirty : 1;
#endif

// Procedural data:
protected:
	/** Actual data for spline. */
	UPROPERTY()
	FInterpCurveVector SplineInfo;

	/** Spline points */
	UPROPERTY()
	TArray<FCyLandSplineInterpPoint> Points;

	/** Bounds of points */
	UPROPERTY()
	FBox Bounds;

	/** Spline meshes */
	UPROPERTY(TextExportTransient)
	TArray<USplineMeshComponent*> LocalMeshComponents;

#if WITH_EDITORONLY_DATA
	/** World references for mesh components stored in other streaming levels */
	UPROPERTY(TextExportTransient, NonPIEDuplicateTransient)
	TArray<TSoftObjectPtr<UWorld>> ForeignWorlds;

	/** Key for tracking whether this segment has been modified relative to the mesh components stored in other streaming levels */
	UPROPERTY(TextExportTransient, NonPIEDuplicateTransient)
	FGuid ModificationKey;
#endif

public:
	const FBox& GetBounds() const { return Bounds; }
	const TArray<FCyLandSplineInterpPoint>& GetPoints() const { return Points; }

#if WITH_EDITOR
	bool IsSplineSelected() const { return bSelected; }
	virtual void SetSplineSelected(bool bInSelected);

	virtual void AutoFlipTangents();

	TMap<UCyLandSplinesComponent*, TArray<USplineMeshComponent*>> GetForeignMeshComponents();
	TArray<USplineMeshComponent*> GetLocalMeshComponents() const;
	
	virtual void UpdateSplinePoints(bool bUpdateCollision = true);

	void UpdateSplineEditorMesh();
	virtual void DeleteSplinePoints();

	const TArray<TSoftObjectPtr<UWorld>>& GetForeignWorlds() const { return ForeignWorlds; }
	FGuid GetModificationKey() const { return ModificationKey; }
#endif

	virtual void FindNearest(const FVector& InLocation, float& t, FVector& OutLocation, FVector& OutTangent);

	//~ Begin UObject Interface
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
	virtual bool Modify(bool bAlwaysMarkDirty = true) override;
#if WITH_EDITOR
	virtual void PostEditUndo() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual void PostEditImport() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
protected:
	virtual void PostInitProperties() override;
private:
	void UpdateMeshCollisionProfile(USplineMeshComponent* MeshComponent);
public:
	//~ End UObject Interface

	friend class FCyLandToolSplines;
};
