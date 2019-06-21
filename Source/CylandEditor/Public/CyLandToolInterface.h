// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UObject/GCObject.h"
#include "UnrealWidget.h"
#include "EdMode.h"

class FEditorViewportClient;
class FPrimitiveDrawInterface;
class FSceneView;
class FViewport;
class UCyLandInfo;
class UCyLandLayerInfoObject;
class UMaterialInstance;
class UMaterialInterface;
class UViewportInteractor;
struct FViewportClick;

// FCyLandToolMousePosition - Struct to store mouse positions since the last time we applied the brush
struct FCyLandToolInteractorPosition
{
	// Stored in heightmap space.
	FVector2D Position;
	bool bModifierPressed;

	FCyLandToolInteractorPosition(FVector2D InPosition, const bool bInModifierPressed)
		: Position(InPosition)
		, bModifierPressed(bInModifierPressed)
	{
	}
};

enum class ECyLandBrushType
{
	Normal = 0,
	Alpha,
	Component,
	Gizmo,
	Splines
};

class FCyLandBrushData
{
protected:
	FIntRect Bounds;
	TArray<float> BrushAlpha;

public:
	FCyLandBrushData()
		: Bounds()
	{
	}

	FCyLandBrushData(FIntRect InBounds)
		: Bounds(InBounds)
	{
		BrushAlpha.SetNumZeroed(Bounds.Area());
	}

	FIntRect GetBounds() const
	{
		return Bounds;
	}

	// For compatibility with older CyLand code that uses inclusive bounds in 4 int32s
	void GetInclusiveBounds(int32& X1, int32& Y1, int32& X2, int32& Y2) const
	{
		X1 = Bounds.Min.X;
		Y1 = Bounds.Min.Y;
		X2 = Bounds.Max.X - 1;
		Y2 = Bounds.Max.Y - 1;
	}

	float* GetDataPtr(FIntPoint Position)
	{
		return BrushAlpha.GetData() + (Position.Y - Bounds.Min.Y) * Bounds.Width() + (Position.X - Bounds.Min.X);
	}
	const float* GetDataPtr(FIntPoint Position) const
	{
		return BrushAlpha.GetData() + (Position.Y - Bounds.Min.Y) * Bounds.Width() + (Position.X - Bounds.Min.X);
	}

	FORCEINLINE explicit operator bool() const
	{
		return BrushAlpha.Num() != 0;
	}

	FORCEINLINE bool operator!() const
	{
		return !(bool)*this;
	}
};

class FCyLandBrush : public FGCObject
{
public:
	virtual void MouseMove(float CyLandX, float CyLandY) = 0;
	virtual FCyLandBrushData ApplyBrush(const TArray<FCyLandToolInteractorPosition>& InteractorPositions) = 0;
	virtual TOptional<bool> InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey, EInputEvent InEvent) { return TOptional<bool>(); }
	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) {};
	virtual void BeginStroke(float CyLandX, float CyLandY, class FCyLandTool* CurrentTool);
	virtual void EndStroke();
	virtual void EnterBrush() {}
	virtual void LeaveBrush() {}
	virtual ~FCyLandBrush() {}
	virtual UMaterialInterface* GetBrushMaterial() { return NULL; }
	virtual const TCHAR* GetBrushName() = 0;
	virtual FText GetDisplayName() = 0;
	virtual ECyLandBrushType GetBrushType() { return ECyLandBrushType::Normal; }

	// FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override {}
};

struct FCyLandBrushSet
{
	FCyLandBrushSet(const TCHAR* InBrushSetName)
		: BrushSetName(InBrushSetName)
		, PreviousBrushIndex(0)
	{
	}

	const FName BrushSetName;
	TArray<FCyLandBrush*> Brushes;
	int32 PreviousBrushIndex;

	virtual ~FCyLandBrushSet()
	{
		for (int32 BrushIdx = 0; BrushIdx < Brushes.Num(); BrushIdx++)
		{
			delete Brushes[BrushIdx];
		}
	}
};

namespace ECyLandToolTargetType
{
	enum Type : int8
	{
		Heightmap  = 0,
		Weightmap  = 1,
		Visibility = 2,

		Invalid    = -1, // only valid for CyLandEdMode->CurrentToolTarget.TargetType
	};
}

namespace ECyLandToolTargetTypeMask
{
	enum Type : uint8
	{
		Heightmap  = 1 << ECyLandToolTargetType::Heightmap,
		Weightmap  = 1 << ECyLandToolTargetType::Weightmap,
		Visibility = 1 << ECyLandToolTargetType::Visibility,

		NA = 0,
		All = 0xFF,
	};

	inline ECyLandToolTargetTypeMask::Type FromType(ECyLandToolTargetType::Type TargetType)
	{
		if (TargetType == ECyLandToolTargetType::Invalid)
		{
			return ECyLandToolTargetTypeMask::NA;
		}
		return (ECyLandToolTargetTypeMask::Type)(1 << TargetType);
	}
}

struct FCyLandToolTarget
{
	TWeakObjectPtr<UCyLandInfo> CyLandInfo;
	ECyLandToolTargetType::Type TargetType;
	TWeakObjectPtr<UCyLandLayerInfoObject> LayerInfo;
	FName LayerName;
	int32 CurrentProceduralLayerIndex;

	FCyLandToolTarget()
		: CyLandInfo()
		, TargetType(ECyLandToolTargetType::Heightmap)
		, LayerInfo()
		, LayerName(NAME_None)
		, CurrentProceduralLayerIndex(INDEX_NONE)
	{
	}
};

enum class ECyLandToolType
{
	Normal = 0,
	Mask,
};

/**
 * FCyLandTool
 */
class FCyLandTool : public FGCObject
{
public:
	virtual void EnterTool() {}
	virtual bool IsToolActive() const { return false;  }
	virtual void ExitTool() {}
	virtual bool BeginTool(FEditorViewportClient* ViewportClient, const FCyLandToolTarget& Target, const FVector& InHitLocation) = 0;
	virtual void EndTool(FEditorViewportClient* ViewportClient) = 0;
	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) {};
	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y) = 0;
	virtual bool HandleClick(HHitProxy* HitProxy, const FViewportClick& Click) { return false; }
	virtual bool InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey, EInputEvent InEvent) { return false; }
	virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale) { return false; }
	virtual bool GetCursor(EMouseCursor::Type& OutCursor) const { return false;  }

	FCyLandTool() : PreviousBrushIndex(-1) {}
	virtual ~FCyLandTool() {}
	virtual const TCHAR* GetToolName() = 0;
	virtual FText GetDisplayName() = 0;
	virtual void SetEditRenderType();
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) {}
	virtual bool SupportsMask() { return true; }
	virtual bool SupportsComponentSelection() { return false; }
	virtual bool OverrideSelection() const { return false; }
	virtual bool IsSelectionAllowed(AActor* InActor, bool bInSelection) const { return false; }
	virtual bool UsesTransformWidget() const { return false; }
	virtual EAxisList::Type GetWidgetAxisToDraw(FWidget::EWidgetMode InWidgetMode) const { return EAxisList::All; }

	virtual bool OverrideWidgetLocation() const { return true; }
	virtual bool OverrideWidgetRotation() const { return true; }
	virtual FVector GetWidgetLocation() const { return FVector::ZeroVector; }
	virtual FMatrix GetWidgetRotation() const { return FMatrix::Identity; }
	virtual bool DisallowMouseDeltaTracking() const { return false; }

	virtual void SetCanToolBeActivated(bool Value) { }
	virtual bool CanToolBeActivated() const { return true;  }
	virtual void SetExternalModifierPressed(const bool bPressed) {};

	virtual EEditAction::Type GetActionEditDuplicate() { return EEditAction::Skip; }
	virtual EEditAction::Type GetActionEditDelete() { return EEditAction::Skip; }
	virtual EEditAction::Type GetActionEditCut() { return EEditAction::Skip; }
	virtual EEditAction::Type GetActionEditCopy() { return EEditAction::Skip; }
	virtual EEditAction::Type GetActionEditPaste() { return EEditAction::Skip; }
	virtual bool ProcessEditDuplicate() { return false; }
	virtual bool ProcessEditDelete() { return false; }
	virtual bool ProcessEditCut() { return false; }
	virtual bool ProcessEditCopy() { return false; }
	virtual bool ProcessEditPaste() { return false; }

	// Functions which doesn't need Viewport data...
	virtual void Process(int32 Index, int32 Arg) {}
	virtual ECyLandToolType GetToolType() { return ECyLandToolType::Normal; }
	virtual ECyLandToolTargetTypeMask::Type GetSupportedTargetTypes() { return ECyLandToolTargetTypeMask::NA; };

	// FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override {}

public:
	int32					PreviousBrushIndex;
	TArray<FName>			ValidBrushes;
};

namespace CyLandTool
{
	UMaterialInstance* CreateMaterialInstance(UMaterialInterface* BaseMaterial);
}
