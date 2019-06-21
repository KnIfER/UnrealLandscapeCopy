// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "HitProxies.h"
#include "AI/NavigationSystemBase.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/Texture2D.h"
#include "UnrealWidget.h"
#include "EditorModeManager.h"
#include "UnrealEdGlobals.h"
#include "EditorViewportClient.h"
#include "CyLandToolInterface.h"
#include "CyLandEdMode.h"
#include "CyLandEditorObject.h"
#include "ScopedTransaction.h"
#include "CyLandEdit.h"
#include "CyLandRender.h"
#include "CyLandDataAccess.h"
#include "CyLandHeightfieldCollisionComponent.h"
#include "Raster.h"

#define LOCTEXT_NAMESPACE "CyLand"

class FCyLandRampToolHeightRasterPolicy
{
public:
	// X = Side Falloff Alpha, Y = Height
	typedef FVector2D InterpolantType;

	/** Initialization constructor. */
	FCyLandRampToolHeightRasterPolicy(TArray<uint16>& InData, int32 InMinX, int32 InMinY, int32 InMaxX, int32 InMaxY, bool InbRaiseTerrain, bool InbLowerTerrain) :
		Data(InData),
		MinX(InMinX),
		MinY(InMinY),
		MaxX(InMaxX),
		MaxY(InMaxY),
		bRaiseTerrain(InbRaiseTerrain),
		bLowerTerrain(InbLowerTerrain)
	{
	}

protected:

	// FTriangleRasterizer policy interface.

	int32 GetMinX() const { return MinX; }
	int32 GetMaxX() const { return MaxX; }
	int32 GetMinY() const { return MinY; }
	int32 GetMaxY() const { return MaxY; }

	void ProcessPixel(int32 X, int32 Y, const InterpolantType& Interpolant, bool BackFacing)
	{
		const float CosInterpX = (Interpolant.X >= 1 ? 1 : 0.5f - 0.5f * FMath::Cos(Interpolant.X * PI));
		const float Alpha = CosInterpX;
		uint16& Dest = Data[(Y - MinY)*(1 + MaxX - MinX) + X - MinX];
		float Value = FMath::Lerp((float)Dest, Interpolant.Y, Alpha);
		uint16 DValue = (uint32)FMath::Clamp<float>(Value, 0, CyLandDataAccess::MaxValue);
		if ((bRaiseTerrain && DValue > Dest) ||
			(bLowerTerrain && DValue < Dest))
		{
			Dest = DValue;
		}
	}

private:
	TArray<uint16>& Data;
	int32 MinX, MinY, MaxX, MaxY;
	uint32 bRaiseTerrain : 1, bLowerTerrain : 1;
};

class HCyLandRampToolPointHitProxy : public HHitProxy
{
	DECLARE_HIT_PROXY();

	int8 Point;

	HCyLandRampToolPointHitProxy(int8 InPoint) :
		HHitProxy(HPP_Foreground),
		Point(InPoint)
	{
	}

	virtual EMouseCursor::Type GetMouseCursor() override
	{
		return EMouseCursor::Crosshairs;
	}
};

IMPLEMENT_HIT_PROXY(HCyLandRampToolPointHitProxy, HHitProxy)

class FCyLandToolRamp : public FCyLandTool
{
protected:
	FEdModeCyLand* EdMode;
	UTexture2D* SpriteTexture;
	FVector Points[2];
	int8 NumPoints;
	int8 SelectedPoint;
	bool bMovingPoint;

public:
	FCyLandToolRamp(FEdModeCyLand* InEdMode)
		: EdMode(InEdMode)
		, SpriteTexture(LoadObject<UTexture2D>(NULL, TEXT("/Engine/EditorResources/S_Terrain.S_Terrain")))
		, NumPoints(0)
		, SelectedPoint(INDEX_NONE)
		, bMovingPoint(false)
	{
		check(SpriteTexture);
	}

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObject(SpriteTexture);
	}

	virtual const TCHAR* GetToolName() override { return TEXT("Ramp"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_Ramp", "Ramp"); };

	virtual void SetEditRenderType() override { GCyLandEditRenderMode = ECyLandEditRenderMode::None | (GCyLandEditRenderMode & ECyLandEditRenderMode::BitMaskForMask); }
	virtual bool SupportsMask() override { return false; }

	virtual ECyLandToolTargetTypeMask::Type GetSupportedTargetTypes() override
	{
		return ECyLandToolTargetTypeMask::Heightmap;
	}

	virtual void EnterTool() override
	{
		NumPoints = 0;
		SelectedPoint = INDEX_NONE;
		GLevelEditorModeTools().SetWidgetMode(FWidget::WM_Translate);
	}

	virtual bool BeginTool(FEditorViewportClient* ViewportClient, const FCyLandToolTarget& Target, const FVector& InHitLocation) override
	{
		if (NumPoints < 2)
		{
			Points[NumPoints] = InHitLocation;
			SelectedPoint = NumPoints;
			NumPoints++;
			bMovingPoint = true;
			GLevelEditorModeTools().SetWidgetMode(FWidget::WM_Translate);
		}
		else
		{
			if (SelectedPoint != INDEX_NONE)
			{
				Points[SelectedPoint] = InHitLocation;
				bMovingPoint = true;
				GLevelEditorModeTools().SetWidgetMode(FWidget::WM_Translate);
			}
		}

		GUnrealEd->RedrawLevelEditingViewports();

		return true;
	}

	virtual void EndTool(FEditorViewportClient* ViewportClient) override
	{
		bMovingPoint = false;
	}

	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y) override
	{
		if (bMovingPoint)
		{
			if (!Viewport->KeyState(EKeys::LeftMouseButton))
			{
				bMovingPoint = false;
				return false;
			}

			FVector HitLocation;
			if (EdMode->CyLandMouseTrace(ViewportClient, x, y, HitLocation))
			{
				if (NumPoints == 1)
				{
					SelectedPoint = NumPoints;
					NumPoints++;
				}

				Points[SelectedPoint] = HitLocation;

				GUnrealEd->RedrawLevelEditingViewports();
			}

			return true;
		}

		return false;
	}

	virtual bool InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey, EInputEvent InEvent) override
	{
		if (InKey == EKeys::Enter && InEvent == IE_Pressed)
		{
			if (CanApplyRamp())
			{
				ApplyRamp();
			}
		}

		if (InKey == EKeys::Escape && InEvent == IE_Pressed)
		{
			ResetRamp();
		}

		// Handle clicking on points to select them and drag them around
		if (InKey == EKeys::LeftMouseButton)
		{
			if (InEvent == IE_Pressed)
			{
				if (!InViewport->KeyState(EKeys::MiddleMouseButton) && !InViewport->KeyState(EKeys::RightMouseButton) && !IsAltDown(InViewport) && InViewportClient->GetCurrentWidgetAxis() == EAxisList::None)
				{
					HHitProxy* HitProxy = InViewport->GetHitProxy(InViewport->GetMouseX(), InViewport->GetMouseY());
					if (HitProxy && HitProxy->IsA(HCyLandRampToolPointHitProxy::StaticGetType()))
					{
						HCyLandRampToolPointHitProxy* PointHitProxy = (HCyLandRampToolPointHitProxy*)HitProxy;
						SelectedPoint = PointHitProxy->Point;
						GLevelEditorModeTools().SetWidgetMode(FWidget::WM_Translate);
						GUnrealEd->RedrawLevelEditingViewports();

						bMovingPoint = true;
						return true;
					}
				}
				return false;
			}
			else if (InEvent == IE_Released)
			{
				bMovingPoint = false;
				return false;
			}
		}

		if (InKey == EKeys::End && InEvent == IE_Pressed)
		{
			if (SelectedPoint != INDEX_NONE)
			{
				const int32 MinX = FMath::FloorToInt(Points[SelectedPoint].X);
				const int32 MinY = FMath::FloorToInt(Points[SelectedPoint].Y);
				const int32 MaxX = MinX + 1;
				const int32 MaxY = MinY + 1;

				FCyLandEditDataInterface CyLandEdit(EdMode->CurrentToolTarget.CyLandInfo.Get());

				TArray<uint16> Data;
				Data.AddZeroed(4);

				int32 ValidMinX = MinX;
				int32 ValidMinY = MinY;
				int32 ValidMaxX = MaxX;
				int32 ValidMaxY = MaxY;
				CyLandEdit.GetHeightData(ValidMinX, ValidMinY, ValidMaxX, ValidMaxY, Data.GetData(), 0);

				if (ValidMaxX - ValidMinX != 1 && ValidMaxY - ValidMinY != 1)
				{
					// If we didn't read 4 values then we're partly off the edge of the CyLand
					return true;
				}

				checkSlow(ValidMinX == MinX);
				checkSlow(ValidMinY == MinY);
				checkSlow(ValidMaxX == MaxX);
				checkSlow(ValidMaxY == MaxY);

				Points[SelectedPoint].Z = (FMath::BiLerp<float>(Data[0], Data[1], Data[2], Data[3], FMath::Frac(Points[SelectedPoint].X), FMath::Frac(Points[SelectedPoint].Y)) - CyLandDataAccess::MidValue) * LANDSCAPE_ZSCALE;

				return true;
			}
		}

		// Change Ramp Width
		if ((InEvent == IE_Pressed || InEvent == IE_Repeat) && (InKey == EKeys::LeftBracket || InKey == EKeys::RightBracket))
		{
			const float OldValue = EdMode->UISettings->RampWidth;
			const float SliderMin = 0.0f;
			const float SliderMax = 8192.0f;
			const float Diff = 0.05f;

			float NewValue;
			if (InKey == EKeys::LeftBracket)
			{
				NewValue = OldValue - OldValue * Diff;
				NewValue = FMath::Min(NewValue, OldValue - 1.0f);
			}
			else
			{
				NewValue = OldValue + OldValue * Diff;
				NewValue = FMath::Max(NewValue, OldValue + 1.0f);
			}

			NewValue = FMath::RoundToFloat(FMath::Clamp(NewValue, SliderMin, SliderMax));

			EdMode->UISettings->RampWidth = NewValue;

			return true;
		}

		return false;
	}

	virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale) override
	{
		if (SelectedPoint != INDEX_NONE && InViewportClient->GetCurrentWidgetAxis() != EAxisList::None)
		{
			const ACyLandProxy* CyLandProxy = EdMode->CurrentToolTarget.CyLandInfo->GetCyLandProxy();
			const FTransform CyLandToWorld = CyLandProxy->CyLandActorToWorld();

			Points[SelectedPoint] += CyLandToWorld.InverseTransformVector(InDrag);

			return true;
		}

		return false;
	}

	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override
	{
		// The editor can try to render the tool before the UpdateCyLandEditorData command runs and the CyLand editor realizes that the CyLand has been hidden/deleted
		const ACyLandProxy* CyLandProxy = EdMode->CurrentToolTarget.CyLandInfo->GetCyLandProxy();
		if (CyLandProxy && NumPoints > 0)
		{
			const FTransform CyLandToWorld = CyLandProxy->CyLandActorToWorld();

			const FLinearColor SelectedSpriteColor = FLinearColor::White + (GEngine->GetSelectedMaterialColor() * GEngine->SelectionHighlightIntensity * 10);

			FVector WorldPoints[2];
			for (int32 i = 0; i < NumPoints; i++)
			{
				WorldPoints[i] = CyLandToWorld.TransformPosition(Points[i]);
			}

			float SpriteScale = EdMode->UISettings->RampWidth / 4;
			if (NumPoints > 1)
			{
				SpriteScale = FMath::Min(SpriteScale, (WorldPoints[1] - WorldPoints[0]).Size() / 2);
			}
			SpriteScale = FMath::Clamp<float>(SpriteScale, 10, 500);

			for (int32 i = 0; i < NumPoints; i++)
			{
				const FLinearColor SpriteColor = (i == SelectedPoint) ? SelectedSpriteColor : FLinearColor::White;

				PDI->SetHitProxy(new HCyLandRampToolPointHitProxy(i));
				PDI->DrawSprite(WorldPoints[i],
					SpriteScale,
					SpriteScale,
					SpriteTexture->Resource,
					SpriteColor,
					SDPG_Foreground,
					0, SpriteTexture->Resource->GetSizeX(),
					0, SpriteTexture->Resource->GetSizeY(),
					SE_BLEND_Masked);
			}
			PDI->SetHitProxy(NULL);

			if (NumPoints == 2)
			{
				const FVector Side = FVector::CrossProduct(Points[1] - Points[0], FVector(0, 0, 1)).GetSafeNormal2D();
				const FVector InnerSide = Side * (EdMode->UISettings->RampWidth * 0.5f * (1 - EdMode->UISettings->RampSideFalloff));
				const FVector OuterSide = Side * (EdMode->UISettings->RampWidth * 0.5f);
				FVector InnerVerts[2][2];
				InnerVerts[0][0] = WorldPoints[0] - InnerSide;
				InnerVerts[0][1] = WorldPoints[0] + InnerSide;
				InnerVerts[1][0] = WorldPoints[1] - InnerSide;
				InnerVerts[1][1] = WorldPoints[1] + InnerSide;

				FVector OuterVerts[2][2];
				OuterVerts[0][0] = WorldPoints[0] - OuterSide;
				OuterVerts[0][1] = WorldPoints[0] + OuterSide;
				OuterVerts[1][0] = WorldPoints[1] - OuterSide;
				OuterVerts[1][1] = WorldPoints[1] + OuterSide;

				// Left
				DrawDashedLine(PDI, OuterVerts[0][0], OuterVerts[1][0], FColor::White, 50, SDPG_Foreground);

				// Center
				DrawDashedLine(PDI, InnerVerts[0][0], InnerVerts[0][1], FColor::White, 50, SDPG_Foreground);
				PDI->DrawLine(InnerVerts[0][0], InnerVerts[0][1], FLinearColor::White, SDPG_World);
				DrawDashedLine(PDI, InnerVerts[0][0], InnerVerts[1][0], FColor::White, 50, SDPG_Foreground);
				PDI->DrawLine(InnerVerts[0][0], InnerVerts[1][0], FLinearColor::White, SDPG_World);
				DrawDashedLine(PDI, InnerVerts[0][1], InnerVerts[1][1], FColor::White, 50, SDPG_Foreground);
				PDI->DrawLine(InnerVerts[0][1], InnerVerts[1][1], FLinearColor::White, SDPG_World);
				DrawDashedLine(PDI, InnerVerts[1][0], InnerVerts[1][1], FColor::White, 50, SDPG_Foreground);
				PDI->DrawLine(InnerVerts[1][0], InnerVerts[1][1], FLinearColor::White, SDPG_World);

				// Right
				DrawDashedLine(PDI, OuterVerts[0][1], OuterVerts[1][1], FColor::White, 50, SDPG_Foreground);
			}
		}
	}

	virtual bool OverrideSelection() const override
	{
		return true;
	}

	virtual bool IsSelectionAllowed(AActor* InActor, bool bInSelection) const override
	{
		// Only filter selection not deselection
		if (bInSelection)
		{
			return false;
		}

		return true;
	}

	virtual bool UsesTransformWidget() const override
	{
		if (SelectedPoint != INDEX_NONE)
		{
			// The editor can try to render the transform widget before the CyLand editor ticks and realizes that the CyLand has been hidden/deleted
			const ACyLandProxy* CyLandProxy = EdMode->CurrentToolTarget.CyLandInfo->GetCyLandProxy();
			if (CyLandProxy)
			{
				return true;
			}
		}

		return false;
	}

	virtual EAxisList::Type GetWidgetAxisToDraw(FWidget::EWidgetMode CheckMode) const override
	{
		if (SelectedPoint != INDEX_NONE)
		{
			if (CheckMode == FWidget::WM_Translate)
			{
				return EAxisList::XYZ;
			}
			else
			{
				return EAxisList::None;
			}
		}

		return EAxisList::None;
	}

	virtual FVector GetWidgetLocation() const override
	{
		if (SelectedPoint != INDEX_NONE)
		{
			const ACyLandProxy* CyLandProxy = EdMode->CurrentToolTarget.CyLandInfo->GetCyLandProxy();
			if (CyLandProxy)
			{
				const FTransform CyLandToWorld = CyLandProxy->CyLandActorToWorld();
				return CyLandToWorld.TransformPosition(Points[SelectedPoint]);
			}
		}

		return FVector::ZeroVector;
	}

	virtual FMatrix GetWidgetRotation() const override
	{
		if (SelectedPoint != INDEX_NONE)
		{
			const ACyLandProxy* CyLandProxy = EdMode->CurrentToolTarget.CyLandInfo->GetCyLandProxy();
			if (CyLandProxy)
			{
				const FTransform CyLandToWorld = CyLandProxy->CyLandActorToWorld();
				return FQuatRotationTranslationMatrix(CyLandToWorld.GetRotation(), FVector::ZeroVector);
			}
		}

		return FMatrix::Identity;
	}

	virtual void ApplyRamp()
	{
		FScopedTransaction Transaction(LOCTEXT("Ramp_Apply", "CyLand Editing: Add ramp"));

		const UCyLandInfo* CyLandInfo = EdMode->CurrentToolTarget.CyLandInfo.Get();
		const ACyLandProxy* CyLandProxy = CyLandInfo->GetCyLandProxy();
		const FTransform CyLandToWorld = CyLandProxy->CyLandActorToWorld();

		const FVector2D Side = FVector2D(FVector::CrossProduct(Points[1] - Points[0], FVector(0,0,1))).GetSafeNormal();
		const FVector2D InnerSide = Side * (EdMode->UISettings->RampWidth * 0.5f * (1 - EdMode->UISettings->RampSideFalloff)) / CyLandToWorld.GetScale3D().X;
		const FVector2D OuterSide = Side * (EdMode->UISettings->RampWidth * 0.5f) / CyLandToWorld.GetScale3D().X;

		FVector2D InnerVerts[2][2];
		InnerVerts[0][0] = FVector2D(Points[0]) - InnerSide;
		InnerVerts[0][1] = FVector2D(Points[0]) + InnerSide;
		InnerVerts[1][0] = FVector2D(Points[1]) - InnerSide;
		InnerVerts[1][1] = FVector2D(Points[1]) + InnerSide;

		FVector2D OuterVerts[2][2];
		OuterVerts[0][0] = FVector2D(Points[0]) - OuterSide;
		OuterVerts[0][1] = FVector2D(Points[0]) + OuterSide;
		OuterVerts[1][0] = FVector2D(Points[1]) - OuterSide;
		OuterVerts[1][1] = FVector2D(Points[1]) + OuterSide;

		float Heights[2];
		Heights[0] = Points[0].Z * LANDSCAPE_INV_ZSCALE + CyLandDataAccess::MidValue;
		Heights[1] = Points[1].Z * LANDSCAPE_INV_ZSCALE + CyLandDataAccess::MidValue;

		int32 MinX = FMath::CeilToInt(FMath::Min(FMath::Min(OuterVerts[0][0].X, OuterVerts[0][1].X), FMath::Min(OuterVerts[1][0].X, OuterVerts[1][1].X))) - 1; // +/- 1 to make sure we have enough data for calculating correct normals
		int32 MinY = FMath::CeilToInt(FMath::Min(FMath::Min(OuterVerts[0][0].Y, OuterVerts[0][1].Y), FMath::Min(OuterVerts[1][0].Y, OuterVerts[1][1].Y))) - 1;
		int32 MaxX = FMath::FloorToInt(FMath::Max(FMath::Max(OuterVerts[0][0].X, OuterVerts[0][1].X), FMath::Max(OuterVerts[1][0].X, OuterVerts[1][1].X))) + 1;
		int32 MaxY = FMath::FloorToInt(FMath::Max(FMath::Max(OuterVerts[0][0].Y, OuterVerts[0][1].Y), FMath::Max(OuterVerts[1][0].Y, OuterVerts[1][1].Y))) + 1;

		// I'd dearly love to use FIntRect in this code, but CyLand works with "Inclusive Max" and FIntRect is "Exclusive Max"
		int32 CyLandMinX, CyLandMinY, CyLandMaxX, CyLandMaxY;
		if (!CyLandInfo->GetCyLandExtent(CyLandMinX, CyLandMinY, CyLandMaxX, CyLandMaxY))
		{
			return;
		}

		MinX = FMath::Max(MinX, CyLandMinX);
		MinY = FMath::Max(MinY, CyLandMinY);
		MaxX = FMath::Min(MaxX, CyLandMaxX);
		MaxY = FMath::Min(MaxY, CyLandMaxY);

		if (MinX > MaxX || MinY > MaxY)
		{
			// The bounds don't intersect any data, so we skip applying the ramp entirely
			return;
		}

		FCyLandEditDataInterface CyLandEdit(EdMode->CurrentToolTarget.CyLandInfo.Get());

		// Heights raster
		bool bRaiseTerrain = true; //EdMode->UISettings->Ramp_bRaiseTerrain;
		bool bLowerTerrain = true; //EdMode->UISettings->Ramp_bLowerTerrain;
		if (bRaiseTerrain || bLowerTerrain)
		{
			TArray<uint16> Data;
			Data.AddZeroed((1 + MaxY - MinY) * (1 + MaxX - MinX));

			int32 ValidMinX = MinX;
			int32 ValidMinY = MinY;
			int32 ValidMaxX = MaxX;
			int32 ValidMaxY = MaxY;
			CyLandEdit.GetHeightData(ValidMinX, ValidMinY, ValidMaxX, ValidMaxY, Data.GetData(), 0);

			if (ValidMinX > ValidMaxX || ValidMinY > ValidMaxY)
			{
				// The bounds don't intersect any data, so we skip applying the ramp entirely
				return;
			}

			FCyLandEditDataInterface::ShrinkData(Data, MinX, MinY, MaxX, MaxY, ValidMinX, ValidMinY, ValidMaxX, ValidMaxY);

			MinX = ValidMinX;
			MinY = ValidMinY;
			MaxX = ValidMaxX;
			MaxY = ValidMaxY;

			FTriangleRasterizer<FCyLandRampToolHeightRasterPolicy> Rasterizer(
				FCyLandRampToolHeightRasterPolicy(Data, MinX, MinY, MaxX, MaxY, bRaiseTerrain, bLowerTerrain));

			// Left
			Rasterizer.DrawTriangle(FVector2D(0, Heights[0]), FVector2D(1, Heights[0]), FVector2D(0, Heights[1]), OuterVerts[0][0], InnerVerts[0][0], OuterVerts[1][0], false);
			Rasterizer.DrawTriangle(FVector2D(1, Heights[0]), FVector2D(0, Heights[1]), FVector2D(1, Heights[1]), InnerVerts[0][0], OuterVerts[1][0], InnerVerts[1][0], false);

			// Center
			Rasterizer.DrawTriangle(FVector2D(1, Heights[0]), FVector2D(1, Heights[0]), FVector2D(1, Heights[1]), InnerVerts[0][0], InnerVerts[0][1], InnerVerts[1][0], false);
			Rasterizer.DrawTriangle(FVector2D(1, Heights[0]), FVector2D(1, Heights[1]), FVector2D(1, Heights[1]), InnerVerts[0][1], InnerVerts[1][0], InnerVerts[1][1], false);

			// Right
			Rasterizer.DrawTriangle(FVector2D(1, Heights[0]), FVector2D(0, Heights[0]), FVector2D(1, Heights[1]), InnerVerts[0][1], OuterVerts[0][1], InnerVerts[1][1], false);
			Rasterizer.DrawTriangle(FVector2D(0, Heights[0]), FVector2D(1, Heights[1]), FVector2D(0, Heights[1]), OuterVerts[0][1], InnerVerts[1][1], OuterVerts[1][1], false);

			CyLandEdit.SetHeightData(MinX, MinY, MaxX, MaxY, Data.GetData(), 0, true);
			CyLandEdit.Flush();

			TSet<UCyLandComponent*> Components;
			if (CyLandEdit.GetComponentsInRegion(MinX, MinY, MaxX, MaxY, &Components))
			{
				for (UCyLandComponent* Component : Components)
				{
					// Recreate collision for modified components and update the navmesh
					UCyLandHeightfieldCollisionComponent* CollisionComponent = Component->CollisionComponent.Get();
					if (CollisionComponent)
					{
						CollisionComponent->RecreateCollision();
						FNavigationSystem::UpdateComponentData(*CollisionComponent);
					}
				}
			}
		}
	}

	bool CanApplyRamp()
	{
		return NumPoints == 2;
	}

	void ResetRamp()
	{
		NumPoints = 0;
		SelectedPoint = INDEX_NONE;
	}
};

void FEdModeCyLand::ApplyRampTool()
{
	if (CurrentTool->GetToolName() == FName("Ramp"))
	{
		FCyLandToolRamp* RampTool = (FCyLandToolRamp*)CurrentTool;
		RampTool->ApplyRamp();
		GEditor->RedrawLevelEditingViewports();
	}
}

bool FEdModeCyLand::CanApplyRampTool()
{
	if (CurrentTool->GetToolName() == FName("Ramp"))
	{
		FCyLandToolRamp* RampTool = (FCyLandToolRamp*)CurrentTool;

		return RampTool->CanApplyRamp();
	}
	return false;
}

void FEdModeCyLand::ResetRampTool()
{
	if (CurrentTool->GetToolName() == FName("Ramp"))
	{
		FCyLandToolRamp* RampTool = (FCyLandToolRamp*)CurrentTool;
		RampTool->ResetRamp();
		GEditor->RedrawLevelEditingViewports();
	}
}

//
// Toolset initialization
//
void FEdModeCyLand::InitializeTool_Ramp()
{
	auto Tool_Ramp = MakeUnique<FCyLandToolRamp>(this);
	Tool_Ramp->ValidBrushes.Add("BrushSet_Dummy");
	CyLandTools.Add(MoveTemp(Tool_Ramp));
}

#undef LOCTEXT_NAMESPACE
