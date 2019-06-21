// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInterface.h"
#include "AI/NavigationSystemBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UnrealWidget.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "CyLandToolInterface.h"
#include "CyLandProxy.h"
#include "CyLandEdMode.h"
#include "Containers/ArrayView.h"
#include "CyLandEditorObject.h"
#include "ScopedTransaction.h"
#include "CyLandEdit.h"
#include "CyLandDataAccess.h"
#include "CyLandRender.h"
#include "CyLandHeightfieldCollisionComponent.h"
#include "CyLandEdModeTools.h"
#include "CyLandBPCustomBrush.h"
#include "CyLandInfo.h"
#include "CyLand.h"
//#include "CyLandDataAccess.h"

#define LOCTEXT_NAMESPACE "CyLand"

template<class ToolTarget>
class FCyLandToolBPCustom : public FCyLandTool
{
protected:
	FEdModeCyLand* EdMode;

public:
	FCyLandToolBPCustom(FEdModeCyLand* InEdMode)
		: EdMode(InEdMode)
	{
	}

	virtual bool UsesTransformWidget() const { return true; }
	virtual bool OverrideWidgetLocation() const { return false; }
	virtual bool OverrideWidgetRotation() const { return false; }

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("BPCustom"); }
	virtual FText GetDisplayName() override { return FText(); };

	virtual void SetEditRenderType() override { GCyLandEditRenderMode = ECyLandEditRenderMode::None | (GCyLandEditRenderMode & ECyLandEditRenderMode::BitMaskForMask); }
	virtual bool SupportsMask() override { return false; }

	virtual ECyLandToolTargetTypeMask::Type GetSupportedTargetTypes() override
	{
		return ECyLandToolTargetTypeMask::FromType(ToolTarget::TargetType);
	}

	virtual void EnterTool() override
	{
	}

	virtual void ExitTool() override
	{
	}

	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) 
	{
	}

	virtual bool BeginTool(FEditorViewportClient* ViewportClient, const FCyLandToolTarget& Target, const FVector& InHitLocation) override
	{
		if (EdMode->UISettings->BlueprintCustomBrush == nullptr)
		{
			return false;
		}

		ACyLandBlueprintCustomBrush* DefaultObject = Cast<ACyLandBlueprintCustomBrush>(EdMode->UISettings->BlueprintCustomBrush->GetDefaultObject(false));

		if (DefaultObject == nullptr)
		{
			return false;
		}

		// Only allow placing brushes that would affect our target type
		if ((DefaultObject->IsAffectingHeightmap() && Target.TargetType == ECyLandToolTargetType::Heightmap) || (DefaultObject->IsAffectingWeightmap() && Target.TargetType == ECyLandToolTargetType::Weightmap))
		{
			UCyLandInfo* Info = EdMode->CurrentToolTarget.CyLandInfo.Get();
			check(Info);

			FVector SpawnLocation = Info->GetCyLandProxy()->CyLandActorToWorld().TransformPosition(InHitLocation);

			FActorSpawnParameters SpawnInfo;
			SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SpawnInfo.bNoFail = true;
			SpawnInfo.OverrideLevel = Info->CyLandActor.Get()->GetTypedOuter<ULevel>(); // always spawn in the same level as the one containing the ACyLand

			ACyLandBlueprintCustomBrush* Brush = ViewportClient->GetWorld()->SpawnActor<ACyLandBlueprintCustomBrush>(EdMode->UISettings->BlueprintCustomBrush, SpawnLocation, FRotator(0.0f), SpawnInfo);
			EdMode->UISettings->BlueprintCustomBrush = nullptr;

			GEditor->SelectNone(true, true);
			GEditor->SelectActor(Brush, true, true);

			EdMode->RefreshDetailPanel();
		}		
		
		return true;
	}

	virtual void EndTool(FEditorViewportClient* ViewportClient) override
	{
	}

	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y) override
	{
		return false;
	}

	virtual bool InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey, EInputEvent InEvent) override
	{
		if (InKey == EKeys::Enter && InEvent == IE_Pressed)
		{
		}

		return false;
	}

	virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale) override
	{
		return false;
	}

	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override
	{
		// The editor can try to render the tool before the UpdateCyLandEditorData command runs and the CyLand editor realizes that the CyLand has been hidden/deleted
		const UCyLandInfo* const CyLandInfo = EdMode->CurrentToolTarget.CyLandInfo.Get();
		const ACyLandProxy* const CyLandProxy = CyLandInfo->GetCyLandProxy();
		if (CyLandProxy)
		{
			const FTransform CyLandToWorld = CyLandProxy->CyLandActorToWorld();

			int32 MinX, MinY, MaxX, MaxY;
			if (CyLandInfo->GetCyLandExtent(MinX, MinY, MaxX, MaxY))
			{
				// TODO if required
			}
		}
	}

	
protected:
/*	float GetLocalZAtPoint(const UCyLandInfo* CyLandInfo, int32 x, int32 y) const
	{
		// try to find Z location
		TSet<UCyLandComponent*> Components;
		CyLandInfo->GetComponentsInRegion(x, y, x, y, Components);
		for (UCyLandComponent* Component : Components)
		{
			FCyLandComponentDataInterface DataInterface(Component);
			return CyLandDataAccess::GetLocalHeight(DataInterface.GetHeight(x - Component->SectionBaseX, y - Component->SectionBaseY));
		}
		return 0.0f;
	}
*/

public:
};
/*
void FEdModeCyLand::ApplyMirrorTool()
{
	if (CurrentTool->GetToolName() == FName("Mirror"))
	{
		FCyLandToolMirror* MirrorTool = (FCyLandToolMirror*)CurrentTool;
		MirrorTool->ApplyMirror();
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEdModeCyLand::CenterMirrorTool()
{
	if (CurrentTool->GetToolName() == FName("Mirror"))
	{
		FCyLandToolMirror* MirrorTool = (FCyLandToolMirror*)CurrentTool;
		MirrorTool->CenterMirrorPoint();
		GEditor->RedrawLevelEditingViewports();
	}
}
*/



//
// Toolset initialization
//
void FEdModeCyLand::InitializeTool_BPCustom()
{
	auto Sculpt_Tool_BPCustom = MakeUnique<FCyLandToolBPCustom<FHeightmapToolTarget>>(this);
	Sculpt_Tool_BPCustom->ValidBrushes.Add("BrushSet_Dummy");
	CyLandTools.Add(MoveTemp(Sculpt_Tool_BPCustom));

	auto Paint_Tool_BPCustom = MakeUnique<FCyLandToolBPCustom<FWeightmapToolTarget>>(this);
	Paint_Tool_BPCustom->ValidBrushes.Add("BrushSet_Dummy");
	CyLandTools.Add(MoveTemp(Paint_Tool_BPCustom));
}

#undef LOCTEXT_NAMESPACE
