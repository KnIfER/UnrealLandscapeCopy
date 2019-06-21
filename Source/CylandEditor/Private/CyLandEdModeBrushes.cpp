// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInstanceConstant.h"
#include "EngineUtils.h"
#include "EditorViewportClient.h"
#include "CyLandToolInterface.h"
#include "CyLandProxy.h"
#include "CyLandGizmoActor.h"
#include "CyLandEdMode.h"
#include "CyLandEditorObject.h"

#include "CyLandRender.h"

#include "LevelUtils.h"

// 
// FCyLandBrush
//
bool GInCyLandBrushTransaction = false;

void FCyLandBrush::BeginStroke(float CyLandX, float CyLandY, FCyLandTool* CurrentTool)
{
	if (!GInCyLandBrushTransaction)
	{
		GEditor->BeginTransaction(FText::Format(NSLOCTEXT("UnrealEd", "CyLandMode_EditTransaction", "CyLand Editing: {0}"), CurrentTool->GetDisplayName()));
		GInCyLandBrushTransaction = true;
	}
}

void FCyLandBrush::EndStroke()
{
	if (ensure(GInCyLandBrushTransaction))
	{
		GEditor->EndTransaction();
		GInCyLandBrushTransaction = false;
	}
}

// 
// FCyLandBrushCircle
//

class FCyLandBrushCircle : public FCyLandBrush
{
	TSet<UCyLandComponent*> BrushMaterialComponents;
	TArray<UMaterialInstanceDynamic*> BrushMaterialFreeInstances;

protected:
	FVector2D LastMousePosition;
	UMaterialInterface* BrushMaterial;
	TMap<UCyLandComponent*, UMaterialInstanceDynamic*> BrushMaterialInstanceMap;

	virtual float CalculateFalloff(float Distance, float Radius, float Falloff) = 0;

	/** Protected so that only subclasses can create instances of this class. */
	FCyLandBrushCircle(FEdModeCyLand* InEdMode, UMaterialInterface* InBrushMaterial)
		: LastMousePosition(0, 0)
		, BrushMaterial(CyLandTool::CreateMaterialInstance(InBrushMaterial))
		, EdMode(InEdMode)
	{
	}

public:
	FEdModeCyLand* EdMode;

	// FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObject(BrushMaterial);

		// Allow any currently unused material instances to be GC'd
		BrushMaterialFreeInstances.Empty();

		Collector.AddReferencedObjects(BrushMaterialComponents);
		Collector.AddReferencedObjects(BrushMaterialInstanceMap);

		// If a user tool removes any components then we will have bad (null) entries in our TSet/TMap, remove them
		// We can't just call .Remove(nullptr) because the entries were hashed as non-null values so a hash lookup of nullptr won't find them
		for (auto It = BrushMaterialComponents.CreateIterator(); It; ++It)
		{
			if (*It == nullptr)
			{
				It.RemoveCurrent();
			}
		}
		for (auto It = BrushMaterialInstanceMap.CreateIterator(); It; ++It)
		{
			if (It->Key == nullptr || It->Value == nullptr)
			{
				It.RemoveCurrent();
			}
		}
	}

	virtual void LeaveBrush() override
	{
		for (UCyLandComponent* Component : BrushMaterialComponents)
		{
			if (Component)
			{
				Component->EditToolRenderData.ToolMaterial = nullptr;
				Component->UpdateEditToolRenderData();
			}
		}

		TArray<UMaterialInstanceDynamic*> BrushMaterialInstances;
		BrushMaterialInstanceMap.GenerateValueArray(BrushMaterialInstances);
		BrushMaterialFreeInstances += BrushMaterialInstances;
		BrushMaterialInstanceMap.Empty();
		BrushMaterialComponents.Empty();
	}

	virtual void BeginStroke(float CyLandX, float CyLandY, FCyLandTool* CurrentTool) override
	{
		FCyLandBrush::BeginStroke(CyLandX, CyLandY, CurrentTool);
		LastMousePosition = FVector2D(CyLandX, CyLandY);
	}

	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override
	{
		UCyLandInfo* CyLandInfo = EdMode->CurrentToolTarget.CyLandInfo.Get();
		ACyLandProxy* Proxy = CyLandInfo->GetCyLandProxy();

		const float ScaleXY = FMath::Abs(CyLandInfo->DrawScale.X);
		const float TotalRadius = EdMode->UISettings->BrushRadius / ScaleXY;
		const float Radius = (1.0f - EdMode->UISettings->BrushFalloff) * TotalRadius;
		const float Falloff = EdMode->UISettings->BrushFalloff * TotalRadius;

		FIntRect Bounds;
		Bounds.Min.X = FMath::FloorToInt(LastMousePosition.X - TotalRadius);
		Bounds.Min.Y = FMath::FloorToInt(LastMousePosition.Y - TotalRadius);
		Bounds.Max.X = FMath::CeilToInt( LastMousePosition.X + TotalRadius);
		Bounds.Max.Y = FMath::CeilToInt( LastMousePosition.Y + TotalRadius);

		TSet<UCyLandComponent*> NewComponents;

		// Adjusting the brush may use the same keybind as moving the camera as they can be user-set, so we need this second check.
		if (!ViewportClient->IsMovingCamera() || EdMode->IsAdjustingBrush(ViewportClient->Viewport))
		{
			// GetComponentsInRegion expects an inclusive max
			CyLandInfo->GetComponentsInRegion(Bounds.Min.X, Bounds.Min.Y, Bounds.Max.X - 1, Bounds.Max.Y - 1, NewComponents);
		}

		// Remove the material from any old components that are no longer in the region
		TSet<UCyLandComponent*> RemovedComponents = BrushMaterialComponents.Difference(NewComponents);
		for (UCyLandComponent* RemovedComponent : RemovedComponents)
		{
			BrushMaterialFreeInstances.Push(BrushMaterialInstanceMap.FindAndRemoveChecked(RemovedComponent));

			RemovedComponent->EditToolRenderData.ToolMaterial = nullptr;
			RemovedComponent->UpdateEditToolRenderData();
		}

		// Set brush material for components in new region
		TSet<UCyLandComponent*> AddedComponents = NewComponents.Difference(BrushMaterialComponents);
		for (UCyLandComponent* AddedComponent : AddedComponents)
		{
			UMaterialInstanceDynamic* BrushMaterialInstance = nullptr;
			if (BrushMaterialFreeInstances.Num() > 0)
			{
				BrushMaterialInstance = BrushMaterialFreeInstances.Pop();
			}
			else
			{
				BrushMaterialInstance = UMaterialInstanceDynamic::Create(BrushMaterial, nullptr);
			}
			BrushMaterialInstanceMap.Add(AddedComponent, BrushMaterialInstance);
			AddedComponent->EditToolRenderData.ToolMaterial = BrushMaterialInstance;
			AddedComponent->UpdateEditToolRenderData();
		}

		BrushMaterialComponents = MoveTemp(NewComponents);

		// Set params for brush material.
		FVector WorldLocation = Proxy->CyLandActorToWorld().TransformPosition(FVector(LastMousePosition.X, LastMousePosition.Y, 0));

		for (const auto& BrushMaterialInstancePair : BrushMaterialInstanceMap)
		{
			UCyLandComponent* const Component = BrushMaterialInstancePair.Key;
			UMaterialInstanceDynamic* const MaterialInstance = BrushMaterialInstancePair.Value;

				// Painting can cause the EditToolRenderData to be destructed, so update it if necessary
			if (!AddedComponents.Contains(Component))
			{
				if (Component->EditToolRenderData.ToolMaterial == nullptr)
				{
					Component->EditToolRenderData.ToolMaterial = MaterialInstance;
					Component->UpdateEditToolRenderData();
				}
			}

			MaterialInstance->SetScalarParameterValue(FName(TEXT("LocalRadius")), Radius);
			MaterialInstance->SetScalarParameterValue(FName(TEXT("LocalFalloff")), Falloff);
			MaterialInstance->SetVectorParameterValue(FName(TEXT("WorldPosition")), FLinearColor(WorldLocation.X, WorldLocation.Y, WorldLocation.Z, ScaleXY));

			bool bCanPaint = true;

			const ACyLandProxy* CyLandProxy = Component->GetCyLandProxy();
			const UCyLandLayerInfoObject* LayerInfo = EdMode->CurrentToolTarget.LayerInfo.Get();

			if (EdMode->CurrentToolTarget.TargetType == ECyLandToolTargetType::Weightmap &&
				EdMode->UISettings->PaintingRestriction != ECyLandLayerPaintingRestriction::None)
			{
				if (EdMode->UISettings->PaintingRestriction == ECyLandLayerPaintingRestriction::UseComponentWhitelist &&
					!Component->LayerWhitelist.Contains(LayerInfo))
				{
					bCanPaint = false;
				}
				else
				{
					bool bExisting = Component->WeightmapLayerAllocations.ContainsByPredicate([LayerInfo](const FCyWeightmapLayerAllocationInfo& Allocation) { return Allocation.LayerInfo == LayerInfo; });
					if (!bExisting)
					{
						if (EdMode->UISettings->PaintingRestriction == ECyLandLayerPaintingRestriction::ExistingOnly ||
							(EdMode->UISettings->PaintingRestriction == ECyLandLayerPaintingRestriction::UseMaxLayers &&
							 CyLandProxy->MaxPaintedLayersPerComponent > 0 && Component->WeightmapLayerAllocations.Num() >= CyLandProxy->MaxPaintedLayersPerComponent))
						{
							bCanPaint = false;
						}
					}
				}
			}

			MaterialInstance->SetScalarParameterValue("CanPaint", bCanPaint ? 1.0f : 0.0f);
		}
	}

	virtual void MouseMove(float CyLandX, float CyLandY) override
	{
		LastMousePosition = FVector2D(CyLandX, CyLandY);
	}

	virtual FCyLandBrushData ApplyBrush(const TArray<FCyLandToolInteractorPosition>& InInteractorPositions) override
	{
		UCyLandInfo* CyLandInfo = EdMode->CurrentToolTarget.CyLandInfo.Get();
		const float ScaleXY = FMath::Abs(CyLandInfo->DrawScale.X);
		const float TotalRadius = EdMode->UISettings->BrushRadius / ScaleXY;
		const float Radius = (1.0f - EdMode->UISettings->BrushFalloff) * TotalRadius;
		const float Falloff = EdMode->UISettings->BrushFalloff * TotalRadius;

		// Cap number of mouse positions to a sensible number
		TArray<FCyLandToolInteractorPosition> InteractorPositions;
		if (InInteractorPositions.Num() > 10)
		{
			for (int32 i = 0; i < 10; ++i)
			{
				// Scale so we include the first and last of the input positions
				InteractorPositions.Add(InInteractorPositions[(i * (InInteractorPositions.Num() - 1)) / 9]);
			}
		}
		else
		{
			InteractorPositions = InInteractorPositions;
		}

		FIntRect Bounds;
		for (const FCyLandToolInteractorPosition& InteractorPosition : InteractorPositions)
		{
			FIntRect SpotBounds;
			SpotBounds.Min.X = FMath::FloorToInt(InteractorPosition.Position.X - TotalRadius);
			SpotBounds.Min.Y = FMath::FloorToInt(InteractorPosition.Position.Y - TotalRadius);
			SpotBounds.Max.X = FMath::CeilToInt( InteractorPosition.Position.X + TotalRadius);
			SpotBounds.Max.Y = FMath::CeilToInt( InteractorPosition.Position.Y + TotalRadius);

			if (Bounds.IsEmpty())
			{
				Bounds = SpotBounds;
			}
			else
			{
				Bounds.Min = Bounds.Min.ComponentMin(SpotBounds.Min);
				Bounds.Max = Bounds.Max.ComponentMax(SpotBounds.Max);
			}
		}

		// Clamp to CyLand bounds
		int32 MinX, MaxX, MinY, MaxY;
		if (!ensure(CyLandInfo->GetCyLandExtent(MinX, MinY, MaxX, MaxY)))
		{
			// CyLand has no components somehow
			return FCyLandBrushData();
		}
		Bounds.Clip(FIntRect(MinX, MinY, MaxX + 1, MaxY + 1));

		FCyLandBrushData BrushData(Bounds);

		for (const FCyLandToolInteractorPosition& InteractorPosition : InteractorPositions)
		{
			FIntRect SpotBounds;
			SpotBounds.Min.X = FMath::Max(FMath::FloorToInt(InteractorPosition.Position.X - TotalRadius), Bounds.Min.X);
			SpotBounds.Min.Y = FMath::Max(FMath::FloorToInt(InteractorPosition.Position.Y - TotalRadius), Bounds.Min.Y);
			SpotBounds.Max.X = FMath::Min(FMath::CeilToInt( InteractorPosition.Position.X + TotalRadius), Bounds.Max.X);
			SpotBounds.Max.Y = FMath::Min(FMath::CeilToInt( InteractorPosition.Position.Y + TotalRadius), Bounds.Max.Y);

			for (int32 Y = SpotBounds.Min.Y; Y < SpotBounds.Max.Y; Y++)
			{
				float* Scanline = BrushData.GetDataPtr(FIntPoint(0, Y));

				for (int32 X = SpotBounds.Min.X; X < SpotBounds.Max.X; X++)
				{
					float PrevAmount = Scanline[X];
					if (PrevAmount < 1.0f)
					{
						// Distance from mouse
						float MouseDist = FMath::Sqrt(FMath::Square(InteractorPosition.Position.X - (float)X) + FMath::Square(InteractorPosition.Position.Y - (float)Y));

						float PaintAmount = CalculateFalloff(MouseDist, Radius, Falloff);

						if (PaintAmount > 0.0f)
						{
							if (EdMode->CurrentTool && EdMode->CurrentTool->GetToolType() != ECyLandToolType::Mask
								&& EdMode->UISettings->bUseSelectedRegion && CyLandInfo->SelectedRegion.Num() > 0)
							{
								float MaskValue = CyLandInfo->SelectedRegion.FindRef(FIntPoint(X, Y));
								if (EdMode->UISettings->bUseNegativeMask)
								{
									MaskValue = 1.0f - MaskValue;
								}
								PaintAmount *= MaskValue;
							}

							if (PaintAmount > PrevAmount)
							{
								// Set the brush value for this vertex
								Scanline[X] = PaintAmount;
							}
						}
					}
				}
			}
		}

		return BrushData;
	}
};

// 
// FCyLandBrushComponent
//

class FCyLandBrushComponent : public FCyLandBrush
{
	TSet<UCyLandComponent*> BrushMaterialComponents;

	virtual const TCHAR* GetBrushName() override { return TEXT("Component"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_Brush_Component", "Component"); };

protected:
	FVector2D LastMousePosition;
	UMaterialInterface* BrushMaterial;
public:
	FEdModeCyLand* EdMode;

	FCyLandBrushComponent(FEdModeCyLand* InEdMode)
		: BrushMaterial(nullptr)
		, EdMode(InEdMode)
	{
		UMaterial* BaseBrushMaterial = LoadObject<UMaterial>(nullptr, TEXT("/Engine/EditorLandscapeResources/SelectBrushMaterial.SelectBrushMaterial"));
		BrushMaterial = CyLandTool::CreateMaterialInstance(BaseBrushMaterial);
	}

	// FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObjects(BrushMaterialComponents);
		Collector.AddReferencedObject(BrushMaterial);
	}

	virtual ECyLandBrushType GetBrushType() override { return ECyLandBrushType::Component; }

	virtual void LeaveBrush() override
	{
		for (TSet<UCyLandComponent*>::TIterator It(BrushMaterialComponents); It; ++It)
		{
			if ((*It) != nullptr)
			{
				(*It)->EditToolRenderData.ToolMaterial = nullptr;
				(*It)->UpdateEditToolRenderData();
			}
		}
		BrushMaterialComponents.Empty();
	}

	virtual void BeginStroke(float CyLandX, float CyLandY, FCyLandTool* CurrentTool) override
	{
		FCyLandBrush::BeginStroke(CyLandX, CyLandY, CurrentTool);
		LastMousePosition = FVector2D(CyLandX, CyLandY);
	}

	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override
	{
		TSet<UCyLandComponent*> NewComponents;

		// Adjusting the brush may use the same keybind as moving the camera as they can be user-set, so we need this second check.
		if (!ViewportClient->IsMovingCamera() || EdMode->IsAdjustingBrush(ViewportClient->Viewport))
		{
			UCyLandInfo* CyLandInfo = EdMode->CurrentToolTarget.CyLandInfo.Get();
			if (CyLandInfo && CyLandInfo->ComponentSizeQuads > 0)
			{
				const int32 BrushSize = FMath::Max(EdMode->UISettings->BrushComponentSize, 0);

				const float BrushOriginX = LastMousePosition.X / CyLandInfo->ComponentSizeQuads - (BrushSize - 1) / 2.0f;
				const float BrushOriginY = LastMousePosition.Y / CyLandInfo->ComponentSizeQuads - (BrushSize - 1) / 2.0f;
				const int32 ComponentIndexX = FMath::FloorToInt(BrushOriginX);
				const int32 ComponentIndexY = FMath::FloorToInt(BrushOriginY);

				for (int32 YIndex = 0; YIndex < BrushSize; ++YIndex)
				{
					for (int32 XIndex = 0; XIndex < BrushSize; ++XIndex)
					{
						UCyLandComponent* Component = CyLandInfo->XYtoComponentMap.FindRef(FIntPoint((ComponentIndexX + XIndex), (ComponentIndexY + YIndex)));
						if (Component && FLevelUtils::IsLevelVisible(Component->GetCyLandProxy()->GetLevel()))
						{
							// For MoveToLevel
							if (EdMode->CurrentTool->GetToolName() == FName("MoveToLevel"))
							{
								if (Component->GetCyLandProxy() && !Component->GetCyLandProxy()->GetLevel()->IsCurrentLevel())
								{
									NewComponents.Add(Component);
								}
							}
							else
							{
								NewComponents.Add(Component);
							}
						}
					}
				}

				// Set brush material for components in new region
				for (UCyLandComponent* NewComponent : NewComponents)
				{
					NewComponent->EditToolRenderData.ToolMaterial = BrushMaterial;
					NewComponent->UpdateEditToolRenderData();
				}
			}
		}

		// Remove the material from any old components that are no longer in the region
		TSet<UCyLandComponent*> RemovedComponents = BrushMaterialComponents.Difference(NewComponents);
		for (UCyLandComponent* RemovedComponent : RemovedComponents)
		{
			if (RemovedComponent != nullptr)
			{
				RemovedComponent->EditToolRenderData.ToolMaterial = nullptr;
				RemovedComponent->UpdateEditToolRenderData();
			}
		}

		BrushMaterialComponents = MoveTemp(NewComponents);
	}

	virtual void MouseMove(float CyLandX, float CyLandY) override
	{
		LastMousePosition = FVector2D(CyLandX, CyLandY);
	}

	virtual FCyLandBrushData ApplyBrush(const TArray<FCyLandToolInteractorPosition>& InteractorPositions) override
	{
		// Selection Brush only works for 
		UCyLandInfo* CyLandInfo = EdMode->CurrentToolTarget.CyLandInfo.Get();

		FIntRect Bounds;

		// The add component tool needs the raw bounds of the brush rather than the bounds of the actually existing components under the brush
		if (EdMode->CurrentTool->GetToolName() == FName("AddComponent"))
		{
			const int32 BrushSize = FMath::Max(EdMode->UISettings->BrushComponentSize, 0);

			const float BrushOriginX = LastMousePosition.X / CyLandInfo->ComponentSizeQuads - (BrushSize - 1) / 2.0f;
			const float BrushOriginY = LastMousePosition.Y / CyLandInfo->ComponentSizeQuads - (BrushSize - 1) / 2.0f;
			const int32 ComponentIndexX = FMath::FloorToInt(BrushOriginX);
			const int32 ComponentIndexY = FMath::FloorToInt(BrushOriginY);

			Bounds.Min.X = (ComponentIndexX) * CyLandInfo->ComponentSizeQuads;
			Bounds.Min.Y = (ComponentIndexY) * CyLandInfo->ComponentSizeQuads;
			Bounds.Max.X = (ComponentIndexX + BrushSize) * CyLandInfo->ComponentSizeQuads + 1;
			Bounds.Max.Y = (ComponentIndexY + BrushSize) * CyLandInfo->ComponentSizeQuads + 1;
		}
		else
		{
			if (BrushMaterialComponents.Num() == 0)
			{
				return FCyLandBrushData();
			}

			// Get extent for all components
			Bounds.Min.X = INT_MAX;
			Bounds.Min.Y = INT_MAX;
			Bounds.Max.X = INT_MIN;
			Bounds.Max.Y = INT_MIN;

			for (UCyLandComponent* Component : BrushMaterialComponents)
			{
				if (ensure(Component))
				{
					Component->GetComponentExtent(Bounds.Min.X, Bounds.Min.Y, Bounds.Max.X, Bounds.Max.Y);
				}
			}

			// GetComponentExtent returns an inclusive max bound
			Bounds.Max += FIntPoint(1, 1);
		}

		FCyLandBrushData BrushData(Bounds);

		for (int32 Y = Bounds.Min.Y; Y < Bounds.Max.Y; Y++)
		{
			float* Scanline = BrushData.GetDataPtr(FIntPoint(0, Y));

			for (int32 X = Bounds.Min.X; X < Bounds.Max.X; X++)
			{
				float PaintAmount = 1.0f;
				if (EdMode->CurrentTool && EdMode->CurrentTool->GetToolType() != ECyLandToolType::Mask
					&& EdMode->UISettings->bUseSelectedRegion && CyLandInfo->SelectedRegion.Num() > 0)
				{
					float MaskValue = CyLandInfo->SelectedRegion.FindRef(FIntPoint(X, Y));
					if (EdMode->UISettings->bUseNegativeMask)
					{
						MaskValue = 1.0f - MaskValue;
					}
					PaintAmount *= MaskValue;
				}

				// Set the brush value for this vertex
				Scanline[X] = PaintAmount;
			}
		}

		return BrushData;
	}
};

// 
// FCyLandBrushGizmo
//

class FCyLandBrushGizmo : public FCyLandBrush
{
	TSet<UCyLandComponent*> BrushMaterialComponents;

	const TCHAR* GetBrushName() override { return TEXT("Gizmo"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_Brush_Gizmo", "Gizmo"); };

protected:
	UMaterialInstanceDynamic* BrushMaterial;
public:
	FEdModeCyLand* EdMode;

	FCyLandBrushGizmo(FEdModeCyLand* InEdMode)
		: BrushMaterial(nullptr)
		, EdMode(InEdMode)
	{
		UMaterialInterface* GizmoMaterial = LoadObject<UMaterialInstanceConstant>(nullptr, TEXT("/Engine/EditorLandscapeResources/MaskBrushMaterial_Gizmo.MaskBrushMaterial_Gizmo"));
		BrushMaterial = UMaterialInstanceDynamic::Create(CyLandTool::CreateMaterialInstance(GizmoMaterial), nullptr);
	}

	// FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObjects(BrushMaterialComponents);
		Collector.AddReferencedObject(BrushMaterial);
	}

	virtual ECyLandBrushType GetBrushType() override { return ECyLandBrushType::Gizmo; }

	virtual void EnterBrush() override
	{
		// Make sure gizmo actor is selected
		ACyLandGizmoActiveActor* Gizmo = EdMode->CurrentGizmoActor.Get();
		if (Gizmo)
		{
			GEditor->SelectNone(false, true);
			GEditor->SelectActor(Gizmo, true, false, true);
		}
	}

	virtual void LeaveBrush() override
	{
		for (TSet<UCyLandComponent*>::TIterator It(BrushMaterialComponents); It; ++It)
		{
			if ((*It) != nullptr)
			{
				(*It)->EditToolRenderData.ToolMaterial = nullptr;
				(*It)->UpdateEditToolRenderData();
			}
		}
		BrushMaterialComponents.Empty();
	}

	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override
	{
		if (GCyLandEditRenderMode & ECyLandEditRenderMode::Gizmo || GCyLandEditRenderMode & ECyLandEditRenderMode::Select)
		{
			ACyLandGizmoActiveActor* Gizmo = EdMode->CurrentGizmoActor.Get();

			if (Gizmo && Gizmo->TargetCyLandInfo && (Gizmo->TargetCyLandInfo == EdMode->CurrentToolTarget.CyLandInfo.Get()) && Gizmo->GizmoTexture && Gizmo->GetRootComponent())
			{
				UCyLandInfo* CyLandInfo = Gizmo->TargetCyLandInfo;
				if (CyLandInfo && CyLandInfo->GetCyLandProxy())
				{
					float ScaleXY = FMath::Abs(CyLandInfo->DrawScale.X);
					FMatrix LToW = CyLandInfo->GetCyLandProxy()->CyLandActorToWorld().ToMatrixWithScale();
					FMatrix WToL = LToW.InverseFast();

					UTexture2D* DataTexture = Gizmo->GizmoTexture;
					FIntRect Bounds(MAX_int32, MAX_int32, MIN_int32, MIN_int32);
					FVector LocalPos[4];
					//FMatrix WorldToLocal = Proxy->LocalToWorld().Inverse();
					for (int32 i = 0; i < 4; ++i)
					{
						//LocalPos[i] = WorldToLocal.TransformPosition(Gizmo->FrustumVerts[i]);
						LocalPos[i] = WToL.TransformPosition(Gizmo->FrustumVerts[i]);
						Bounds.Min.X = FMath::Min(Bounds.Min.X, (int32)LocalPos[i].X);
						Bounds.Min.Y = FMath::Min(Bounds.Min.Y, (int32)LocalPos[i].Y);
						Bounds.Max.X = FMath::Max(Bounds.Max.X, (int32)LocalPos[i].X);
						Bounds.Max.Y = FMath::Max(Bounds.Max.Y, (int32)LocalPos[i].Y);
					}

					// GetComponentsInRegion expects an inclusive max
					TSet<UCyLandComponent*> NewComponents;
					CyLandInfo->GetComponentsInRegion(Bounds.Min.X, Bounds.Min.Y, Bounds.Max.X - 1, Bounds.Max.Y - 1, NewComponents);

					float SquaredScaleXY = FMath::Square(ScaleXY);
					FLinearColor AlphaScaleBias(
						SquaredScaleXY / (Gizmo->GetWidth() * DataTexture->GetSizeX()),
						SquaredScaleXY / (Gizmo->GetHeight() * DataTexture->GetSizeY()),
						Gizmo->TextureScale.X,
						Gizmo->TextureScale.Y
						);
					BrushMaterial->SetVectorParameterValue(FName(TEXT("AlphaScaleBias")), AlphaScaleBias);

					float Angle = (-EdMode->CurrentGizmoActor->GetActorRotation().Euler().Z) * PI / 180.0f;
					FLinearColor CyLandLocation(EdMode->CurrentGizmoActor->GetActorLocation().X, EdMode->CurrentGizmoActor->GetActorLocation().Y, EdMode->CurrentGizmoActor->GetActorLocation().Z, Angle);
					BrushMaterial->SetVectorParameterValue(FName(TEXT("CyLandLocation")), CyLandLocation);
					BrushMaterial->SetTextureParameterValue(FName(TEXT("AlphaTexture")), DataTexture);

					// Set brush material for components in new region
					for (UCyLandComponent* NewComponent : NewComponents)
					{
						NewComponent->EditToolRenderData.GizmoMaterial = ((Gizmo->DataType != CyLGT_None) && (GCyLandEditRenderMode & ECyLandEditRenderMode::Gizmo) ? BrushMaterial : nullptr);
						NewComponent->UpdateEditToolRenderData();
					}

					// Remove the material from any old components that are no longer in the region
					TSet<UCyLandComponent*> RemovedComponents = BrushMaterialComponents.Difference(NewComponents);
					for (UCyLandComponent* RemovedComponent : RemovedComponents)
					{
						if (RemovedComponent != nullptr)
						{
							RemovedComponent->EditToolRenderData.GizmoMaterial = nullptr;
							RemovedComponent->UpdateEditToolRenderData();
						}
					}

					BrushMaterialComponents = MoveTemp(NewComponents);
				}
			}
		}
	}

	virtual void MouseMove(float CyLandX, float CyLandY) override
	{
	}

	virtual TOptional<bool> InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey, EInputEvent InEvent) override
	{

		if (InKey == EKeys::LeftMouseButton && InEvent == IE_Pressed)
		{
			int32 HitX = InViewport->GetMouseX();
			int32 HitY = InViewport->GetMouseY();
			HHitProxy* HitProxy = InViewport->GetHitProxy(HitX, HitY);

			HActor* ActorHitProxy = HitProxyCast<HActor>(HitProxy);
			if (ActorHitProxy && ActorHitProxy->Actor->IsA<ACyLandGizmoActor>())
			{
				// don't treat clicks on a CyLand gizmo as a tool invocation
				return TOptional<bool>(false);
			}
		}

		// default behaviour
		return TOptional<bool>();
	}

	virtual FCyLandBrushData ApplyBrush(const TArray<FCyLandToolInteractorPosition>& InteractorPositions) override
	{
		// Selection Brush only works for 
		ACyLandGizmoActiveActor* Gizmo = EdMode->CurrentGizmoActor.Get();
		UCyLandInfo* CyLandInfo = EdMode->CurrentToolTarget.CyLandInfo.Get();

		if (!Gizmo || !Gizmo->GetRootComponent())
		{
			return FCyLandBrushData();
		}

		if (BrushMaterialComponents.Num() == 0)
		{
			return FCyLandBrushData();
		}

		Gizmo->TargetCyLandInfo = CyLandInfo;
		float ScaleXY = FMath::Abs(CyLandInfo->DrawScale.X);

		// Get extent for all components
		FIntRect Bounds;
		Bounds.Min.X = INT_MAX;
		Bounds.Min.Y = INT_MAX;
		Bounds.Max.X = INT_MIN;
		Bounds.Max.Y = INT_MIN;

		for (UCyLandComponent* Component : BrushMaterialComponents)
		{
			if (ensure(Component))
			{
				Component->GetComponentExtent(Bounds.Min.X, Bounds.Min.Y, Bounds.Max.X, Bounds.Max.Y);
			}
		}

		FCyLandBrushData BrushData(Bounds);

		//FMatrix CyLandToGizmoLocal = CyLand->LocalToWorld() * Gizmo->WorldToLocal();
		const float LW = Gizmo->GetWidth() / (2 * ScaleXY);
		const float LH = Gizmo->GetHeight() / (2 * ScaleXY);

		FMatrix WToL = CyLandInfo->GetCyLandProxy()->CyLandActorToWorld().ToMatrixWithScale().InverseFast();
		FVector BaseLocation = WToL.TransformPosition(Gizmo->GetActorLocation());
		FMatrix CyLandToGizmoLocal =
			(FTranslationMatrix(FVector(-LW + 0.5, -LH + 0.5, 0)) * FRotationTranslationMatrix(FRotator(0, Gizmo->GetActorRotation().Yaw, 0), FVector(BaseLocation.X, BaseLocation.Y, 0))).InverseFast();

		float W = Gizmo->GetWidth() / ScaleXY; //Gizmo->GetWidth() / (Gizmo->DrawScale * Gizmo->DrawScale3D.X);
		float H = Gizmo->GetHeight() / ScaleXY; //Gizmo->GetHeight() / (Gizmo->DrawScale * Gizmo->DrawScale3D.Y);

		for (int32 Y = Bounds.Min.Y; Y < Bounds.Max.Y; Y++)
		{
			float* Scanline = BrushData.GetDataPtr(FIntPoint(0, Y));

			for (int32 X = Bounds.Min.X; X < Bounds.Max.X; X++)
			{
				FVector GizmoLocal = CyLandToGizmoLocal.TransformPosition(FVector(X, Y, 0));
				if (GizmoLocal.X < W && GizmoLocal.X > 0 && GizmoLocal.Y < H && GizmoLocal.Y > 0)
				{
					float PaintAmount = 1.0f;
					// Transform in 0,0 origin LW radius
					if (EdMode->UISettings->bSmoothGizmoBrush)
					{
						FVector TransformedLocal(FMath::Abs(GizmoLocal.X - LW), FMath::Abs(GizmoLocal.Y - LH) * (W / H), 0);
						float FalloffRadius = LW * EdMode->UISettings->BrushFalloff;
						float SquareRadius = LW - FalloffRadius;
						float Cos = FMath::Abs(TransformedLocal.X) / TransformedLocal.Size2D();
						float Sin = FMath::Abs(TransformedLocal.Y) / TransformedLocal.Size2D();
						float RatioX = FalloffRadius > 0.0f ? 1.0f - FMath::Clamp((FMath::Abs(TransformedLocal.X) - Cos*SquareRadius) / FalloffRadius, 0.0f, 1.0f) : 1.0f;
						float RatioY = FalloffRadius > 0.0f ? 1.0f - FMath::Clamp((FMath::Abs(TransformedLocal.Y) - Sin*SquareRadius) / FalloffRadius, 0.0f, 1.0f) : 1.0f;
						float Ratio = TransformedLocal.Size2D() > SquareRadius ? RatioX * RatioY : 1.0f; //TransformedLocal.X / LW * TransformedLocal.Y / LW;
						PaintAmount = Ratio*Ratio*(3 - 2 * Ratio); //FMath::Lerp(SquareFalloff, RectFalloff*RectFalloff, Ratio);
					}

					if (PaintAmount)
					{
						if (EdMode->CurrentTool && EdMode->CurrentTool->GetToolType() != ECyLandToolType::Mask
							&& EdMode->UISettings->bUseSelectedRegion && CyLandInfo->SelectedRegion.Num() > 0)
						{
							float MaskValue = CyLandInfo->SelectedRegion.FindRef(FIntPoint(X, Y));
							if (EdMode->UISettings->bUseNegativeMask)
							{
								MaskValue = 1.0f - MaskValue;
							}
							PaintAmount *= MaskValue;
						}

						// Set the brush value for this vertex
						Scanline[X] = PaintAmount;
					}
				}
			}
		}

		return BrushData;
	}
};

// 
// FCyLandBrushSplines
//
class FCyLandBrushSplines : public FCyLandBrush
{
public:
	const TCHAR* GetBrushName() override { return TEXT("Splines"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_Brush_Splines", "Splines"); };

	FEdModeCyLand* EdMode;

	FCyLandBrushSplines(FEdModeCyLand* InEdMode)
		: EdMode(InEdMode)
	{
	}

	virtual ~FCyLandBrushSplines()
	{
	}

	virtual ECyLandBrushType GetBrushType() override { return ECyLandBrushType::Splines; }

	virtual void MouseMove(float CyLandX, float CyLandY) override
	{
	}

	virtual FCyLandBrushData ApplyBrush(const TArray<FCyLandToolInteractorPosition>& InteractorPositions) override
	{
		return FCyLandBrushData();
	}
};

// 
// FCyLandBrushDummy
//
class FCyLandBrushDummy : public FCyLandBrush
{
public:
	const TCHAR* GetBrushName() override { return TEXT("None"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_Brush_None", "None"); };

	FEdModeCyLand* EdMode;

	FCyLandBrushDummy(FEdModeCyLand* InEdMode)
		: EdMode(InEdMode)
	{
	}

	virtual ~FCyLandBrushDummy()
	{
	}

	virtual ECyLandBrushType GetBrushType() override { return ECyLandBrushType::Normal; }

	virtual void MouseMove(float CyLandX, float CyLandY) override
	{
	}

	virtual FCyLandBrushData ApplyBrush(const TArray<FCyLandToolInteractorPosition>& InteractorPositions) override
	{
		return FCyLandBrushData();
	}
};

class FCyLandBrushCircle_Linear : public FCyLandBrushCircle
{
protected:
	/** Protected so only subclasses can create instances. */
	FCyLandBrushCircle_Linear(FEdModeCyLand* InEdMode, UMaterialInterface* InBrushMaterial)
		: FCyLandBrushCircle(InEdMode, InBrushMaterial)
	{
	}

	virtual float CalculateFalloff(float Distance, float Radius, float Falloff) override
	{
		return Distance < Radius ? 1.0f :
			Falloff > 0.0f ? FMath::Max<float>(0.0f, 1.0f - (Distance - Radius) / Falloff) :
			0.0f;
	}

public:
	static FCyLandBrushCircle_Linear* Create(FEdModeCyLand* InEdMode)
	{
		UMaterialInstanceConstant* CircleBrushMaterial_Linear = LoadObject<UMaterialInstanceConstant>(nullptr, TEXT("/Engine/EditorLandscapeResources/CircleBrushMaterial_Linear.CircleBrushMaterial_Linear"), nullptr, LOAD_None, nullptr);
		return new FCyLandBrushCircle_Linear(InEdMode, CircleBrushMaterial_Linear);
	}
	virtual const TCHAR* GetBrushName() override { return TEXT("Circle_Linear"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_Brush_Falloff_Linear", "Linear falloff"); };

};

class FCyLandBrushCircle_Smooth : public FCyLandBrushCircle_Linear
{
protected:
	FCyLandBrushCircle_Smooth(FEdModeCyLand* InEdMode, UMaterialInterface* InBrushMaterial)
		: FCyLandBrushCircle_Linear(InEdMode, InBrushMaterial)
	{
	}

	virtual float CalculateFalloff(float Distance, float Radius, float Falloff) override
	{
		float y = FCyLandBrushCircle_Linear::CalculateFalloff(Distance, Radius, Falloff);
		// Smooth-step it
		return y*y*(3 - 2 * y);
	}

public:
	static FCyLandBrushCircle_Smooth* Create(FEdModeCyLand* InEdMode)
	{
		UMaterialInstanceConstant* CircleBrushMaterial_Smooth = LoadObject<UMaterialInstanceConstant>(nullptr, TEXT("/Engine/EditorLandscapeResources/CircleBrushMaterial_Smooth.CircleBrushMaterial_Smooth"), nullptr, LOAD_None, nullptr);
		return new FCyLandBrushCircle_Smooth(InEdMode, CircleBrushMaterial_Smooth);
	}
	virtual const TCHAR* GetBrushName() override { return TEXT("Circle_Smooth"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_Brush_Falloff_Smooth", "Smooth falloff"); };

};

class FCyLandBrushCircle_Spherical : public FCyLandBrushCircle
{
protected:
	FCyLandBrushCircle_Spherical(FEdModeCyLand* InEdMode, UMaterialInterface* InBrushMaterial)
		: FCyLandBrushCircle(InEdMode, InBrushMaterial)
	{
	}

	virtual float CalculateFalloff(float Distance, float Radius, float Falloff) override
	{
		if (Distance <= Radius)
		{
			return 1.0f;
		}

		if (Distance > Radius + Falloff)
		{
			return 0.0f;
		}

		// Elliptical falloff
		return FMath::Sqrt(1.0f - FMath::Square((Distance - Radius) / Falloff));
	}

public:
	static FCyLandBrushCircle_Spherical* Create(FEdModeCyLand* InEdMode)
	{
		UMaterialInstanceConstant* CircleBrushMaterial_Spherical = LoadObject<UMaterialInstanceConstant>(nullptr, TEXT("/Engine/EditorLandscapeResources/CircleBrushMaterial_Spherical.CircleBrushMaterial_Spherical"), nullptr, LOAD_None, nullptr);
		return new FCyLandBrushCircle_Spherical(InEdMode, CircleBrushMaterial_Spherical);
	}
	virtual const TCHAR* GetBrushName() override { return TEXT("Circle_Spherical"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_Brush_Falloff_Spherical", "Spherical falloff"); };
};

class FCyLandBrushCircle_Tip : public FCyLandBrushCircle
{
protected:
	FCyLandBrushCircle_Tip(FEdModeCyLand* InEdMode, UMaterialInterface* InBrushMaterial)
		: FCyLandBrushCircle(InEdMode, InBrushMaterial)
	{
	}

	virtual float CalculateFalloff(float Distance, float Radius, float Falloff) override
	{
		if (Distance <= Radius)
		{
			return 1.0f;
		}

		if (Distance > Radius + Falloff)
		{
			return 0.0f;
		}

		// inverse elliptical falloff
		return 1.0f - FMath::Sqrt(1.0f - FMath::Square((Falloff + Radius - Distance) / Falloff));
	}

public:
	static FCyLandBrushCircle_Tip* Create(FEdModeCyLand* InEdMode)
	{
		UMaterialInstanceConstant* CircleBrushMaterial_Tip = LoadObject<UMaterialInstanceConstant>(nullptr, TEXT("/Engine/EditorLandscapeResources/CircleBrushMaterial_Tip.CircleBrushMaterial_Tip"), nullptr, LOAD_None, nullptr);
		return new FCyLandBrushCircle_Tip(InEdMode, CircleBrushMaterial_Tip);
	}
	virtual const TCHAR* GetBrushName() override { return TEXT("Circle_Tip"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_Brush_Falloff_Tip", "Tip falloff"); };
};


// FCyLandBrushAlphaBase
class FCyLandBrushAlphaBase : public FCyLandBrushCircle_Smooth
{
public:
	FCyLandBrushAlphaBase(FEdModeCyLand* InEdMode, UMaterialInterface* InBrushMaterial)
		: FCyLandBrushCircle_Smooth(InEdMode, InBrushMaterial)
	{
	}

	float GetAlphaSample(float SampleX, float SampleY)
	{
		int32 SizeX = EdMode->UISettings->AlphaTextureSizeX;
		int32 SizeY = EdMode->UISettings->AlphaTextureSizeY;

		// Bilinear interpolate the values from the alpha texture
		int32 SampleX0 = FMath::FloorToInt(SampleX);
		int32 SampleX1 = (SampleX0 + 1) % SizeX;
		int32 SampleY0 = FMath::FloorToInt(SampleY);
		int32 SampleY1 = (SampleY0 + 1) % SizeY;

		const uint8* AlphaData = EdMode->UISettings->AlphaTextureData.GetData();

		float Alpha00 = (float)AlphaData[SampleX0 + SampleY0 * SizeX] / 255.0f;
		float Alpha01 = (float)AlphaData[SampleX0 + SampleY1 * SizeX] / 255.0f;
		float Alpha10 = (float)AlphaData[SampleX1 + SampleY0 * SizeX] / 255.0f;
		float Alpha11 = (float)AlphaData[SampleX1 + SampleY1 * SizeX] / 255.0f;

		return FMath::Lerp(
			FMath::Lerp(Alpha00, Alpha01, FMath::Fractional(SampleX)),
			FMath::Lerp(Alpha10, Alpha11, FMath::Fractional(SampleX)),
			FMath::Fractional(SampleY)
			);
	}

};

//
// FCyLandBrushAlphaPattern
//
class FCyLandBrushAlphaPattern : public FCyLandBrushAlphaBase
{
protected:
	FCyLandBrushAlphaPattern(FEdModeCyLand* InEdMode, UMaterialInterface* InBrushMaterial)
		: FCyLandBrushAlphaBase(InEdMode, InBrushMaterial)
	{
	}

public:
	static FCyLandBrushAlphaPattern* Create(FEdModeCyLand* InEdMode)
	{
		UMaterialInstanceConstant* PatternBrushMaterial = LoadObject<UMaterialInstanceConstant>(nullptr, TEXT("/Engine/EditorLandscapeResources/PatternBrushMaterial_Smooth.PatternBrushMaterial_Smooth"), nullptr, LOAD_None, nullptr);
		return new FCyLandBrushAlphaPattern(InEdMode, PatternBrushMaterial);
	}

	virtual ECyLandBrushType GetBrushType() override { return ECyLandBrushType::Alpha; }

	virtual FCyLandBrushData ApplyBrush(const TArray<FCyLandToolInteractorPosition>& InteractorPositions) override
	{
		UCyLandInfo* CyLandInfo = EdMode->CurrentToolTarget.CyLandInfo.Get();
		const float ScaleXY = FMath::Abs(CyLandInfo->DrawScale.X);
		const float TotalRadius = EdMode->UISettings->BrushRadius / ScaleXY;
		const float Radius = (1.0f - EdMode->UISettings->BrushFalloff) * TotalRadius;
		const float Falloff = EdMode->UISettings->BrushFalloff * TotalRadius;

		int32 SizeX = EdMode->UISettings->AlphaTextureSizeX;
		int32 SizeY = EdMode->UISettings->AlphaTextureSizeY;

		FIntRect Bounds;
		Bounds.Min.X = FMath::FloorToInt(LastMousePosition.X - TotalRadius);
		Bounds.Min.Y = FMath::FloorToInt(LastMousePosition.Y - TotalRadius);
		Bounds.Max.X = FMath::CeilToInt( LastMousePosition.X + TotalRadius);
		Bounds.Max.Y = FMath::CeilToInt( LastMousePosition.Y + TotalRadius);


		// Clamp to CyLand bounds
		int32 MinX, MaxX, MinY, MaxY;
		if (!ensure(CyLandInfo->GetCyLandExtent(MinX, MinY, MaxX, MaxY)))
		{
			// CyLand has no components somehow
			return FCyLandBrushData();
		}
		Bounds.Clip(FIntRect(MinX, MinY, MaxX + 1, MaxY + 1));

		FCyLandBrushData BrushData(Bounds);

		for (int32 Y = Bounds.Min.Y; Y < Bounds.Max.Y; Y++)
		{
			float* Scanline = BrushData.GetDataPtr(FIntPoint(0, Y));

			for (int32 X = Bounds.Min.X; X < Bounds.Max.X; X++)
			{
				float Angle;
				FVector2D Scale;
				FVector2D Bias;
				if (EdMode->UISettings->bUseWorldSpacePatternBrush)
				{
					FVector2D LocalOrigin = -FVector2D(CyLandInfo->GetCyLandProxy()->CyLandActorToWorld().InverseTransformPosition(FVector(EdMode->UISettings->WorldSpacePatternBrushSettings.Origin, 0.0f)));
					const FVector2D LocalScale = FVector2D(
						ScaleXY / (EdMode->UISettings->WorldSpacePatternBrushSettings.RepeatSize * ((float)SizeX / SizeY)),
						ScaleXY / (EdMode->UISettings->WorldSpacePatternBrushSettings.RepeatSize));
					LocalOrigin *= LocalScale;
					Angle = -EdMode->UISettings->WorldSpacePatternBrushSettings.Rotation;
					if (EdMode->UISettings->WorldSpacePatternBrushSettings.bCenterTextureOnOrigin)
					{
						LocalOrigin += FVector2D(0.5f, 0.5f).GetRotated(-Angle);
					}
					Scale = FVector2D(SizeX, SizeY) * LocalScale;
					Bias = FVector2D(SizeX, SizeY) * LocalOrigin;
				}
				else
				{
					Scale.X = 1.0f / EdMode->UISettings->AlphaBrushScale;
					Scale.Y = 1.0f / EdMode->UISettings->AlphaBrushScale;
					Bias.X = SizeX * EdMode->UISettings->AlphaBrushPanU;
					Bias.Y = SizeY * EdMode->UISettings->AlphaBrushPanV;
					Angle = EdMode->UISettings->AlphaBrushRotation;
				}

				// Find alphamap sample location
				FVector2D SamplePos = FVector2D(X, Y) * Scale + Bias;
				SamplePos = SamplePos.GetRotated(Angle);

				float ModSampleX = FMath::Fmod(SamplePos.X, (float)SizeX);
				float ModSampleY = FMath::Fmod(SamplePos.Y, (float)SizeY);

				if (ModSampleX < 0.0f)
				{
					ModSampleX += (float)SizeX;
				}
				if (ModSampleY < 0.0f)
				{
					ModSampleY += (float)SizeY;
				}

				// Sample the alpha texture
				float Alpha = GetAlphaSample(ModSampleX, ModSampleY);

				// Distance from mouse
				float MouseDist = FMath::Sqrt(FMath::Square(LastMousePosition.X - (float)X) + FMath::Square(LastMousePosition.Y - (float)Y));

				float PaintAmount = CalculateFalloff(MouseDist, Radius, Falloff) * Alpha;

				if (PaintAmount > 0.0f)
				{
					if (EdMode->CurrentTool && EdMode->CurrentTool->GetToolType() != ECyLandToolType::Mask
						&& EdMode->UISettings->bUseSelectedRegion && CyLandInfo->SelectedRegion.Num() > 0)
					{
						float MaskValue = CyLandInfo->SelectedRegion.FindRef(FIntPoint(X, Y));
						if (EdMode->UISettings->bUseNegativeMask)
						{
							MaskValue = 1.0f - MaskValue;
						}
						PaintAmount *= MaskValue;
					}
					// Set the brush value for this vertex
					Scanline[X] = PaintAmount;
				}
			}
		}
		return BrushData;
	}

	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override
	{
		FCyLandBrushCircle::Tick(ViewportClient, DeltaTime);

		ACyLandProxy* Proxy = EdMode->CurrentToolTarget.CyLandInfo.IsValid() ? EdMode->CurrentToolTarget.CyLandInfo->GetCyLandProxy() : nullptr;
		if (Proxy)
		{
			const float ScaleXY = FMath::Abs(EdMode->CurrentToolTarget.CyLandInfo->DrawScale.X);
			int32 SizeX = EdMode->UISettings->AlphaTextureSizeX;
			int32 SizeY = EdMode->UISettings->AlphaTextureSizeY;

			FLinearColor AlphaScaleBias;
			float Angle;
			if (EdMode->UISettings->bUseWorldSpacePatternBrush)
			{
				FVector2D LocalOrigin = -FVector2D(Proxy->CyLandActorToWorld().InverseTransformPosition(FVector(EdMode->UISettings->WorldSpacePatternBrushSettings.Origin, 0.0f)));
				const FVector2D Scale = FVector2D(
					ScaleXY / (EdMode->UISettings->WorldSpacePatternBrushSettings.RepeatSize * ((float)SizeX / SizeY)),
					ScaleXY / (EdMode->UISettings->WorldSpacePatternBrushSettings.RepeatSize));
				LocalOrigin *= Scale;
				Angle = -EdMode->UISettings->WorldSpacePatternBrushSettings.Rotation;
				if (EdMode->UISettings->WorldSpacePatternBrushSettings.bCenterTextureOnOrigin)
				{
					LocalOrigin += FVector2D(0.5f, 0.5f).GetRotated(-Angle);
				}
				AlphaScaleBias = FLinearColor(
					Scale.X,
					Scale.Y,
					LocalOrigin.X,
					LocalOrigin.Y);
			}
			else
			{
				AlphaScaleBias = FLinearColor(
					1.0f / (EdMode->UISettings->AlphaBrushScale * SizeX),
					1.0f / (EdMode->UISettings->AlphaBrushScale * SizeY),
					EdMode->UISettings->AlphaBrushPanU,
					EdMode->UISettings->AlphaBrushPanV);
				Angle = EdMode->UISettings->AlphaBrushRotation;
			}
			Angle = FMath::DegreesToRadians(Angle);

			FVector CyLandLocation = Proxy->CyLandActorToWorld().GetTranslation();
			FLinearColor CyLandLocationParam(CyLandLocation.X, CyLandLocation.Y, CyLandLocation.Z, Angle);

			int32 Channel = EdMode->UISettings->AlphaTextureChannel;
			FLinearColor AlphaTextureMask(Channel == 0 ? 1 : 0, Channel == 1 ? 1 : 0, Channel == 2 ? 1 : 0, Channel == 3 ? 1 : 0);

			for (const auto& BrushMaterialInstancePair : BrushMaterialInstanceMap)
			{
				UMaterialInstanceDynamic* const MaterialInstance = BrushMaterialInstancePair.Value;
				MaterialInstance->SetVectorParameterValue(FName(TEXT("AlphaScaleBias")),    AlphaScaleBias);
				MaterialInstance->SetVectorParameterValue(FName(TEXT("CyLandLocation")), CyLandLocationParam);
				MaterialInstance->SetVectorParameterValue(FName(TEXT("AlphaTextureMask")),  AlphaTextureMask);
				MaterialInstance->SetTextureParameterValue(FName(TEXT("AlphaTexture")),     EdMode->UISettings->AlphaTexture);
			}
		}
	}

	virtual const TCHAR* GetBrushName() override { return TEXT("Pattern"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_Brush_PatternAlpha", "Pattern Alpha"); };
};


//
// FCyLandBrushAlpha
//
class FCyLandBrushAlpha : public FCyLandBrushAlphaBase
{
	float LastMouseAngle;
	FVector2D OldMousePosition;	// a previous mouse position, kept until we move a certain distance away, for smoothing deltas
	double LastMouseSampleTime;

protected:
	FCyLandBrushAlpha(FEdModeCyLand* InEdMode, UMaterialInterface* InBrushMaterial)
		: FCyLandBrushAlphaBase(InEdMode, InBrushMaterial)
		, LastMouseAngle(0.0f)
		, OldMousePosition(0.0f, 0.0f)
		, LastMouseSampleTime(FPlatformTime::Seconds())
	{
	}

public:
	static FCyLandBrushAlpha* Create(FEdModeCyLand* InEdMode)
	{
		UMaterialInstanceConstant* AlphaBrushMaterial = LoadObject<UMaterialInstanceConstant>(nullptr, TEXT("/Engine/EditorLandscapeResources/AlphaBrushMaterial_Smooth.AlphaBrushMaterial_Smooth"), nullptr, LOAD_None, nullptr);
		return new FCyLandBrushAlpha(InEdMode, AlphaBrushMaterial);
	}

	virtual FCyLandBrushData ApplyBrush(const TArray<FCyLandToolInteractorPosition>& InteractorPositions) override
	{
		UCyLandInfo* CyLandInfo = EdMode->CurrentToolTarget.CyLandInfo.Get();
		if (EdMode->UISettings->bAlphaBrushAutoRotate && OldMousePosition.IsZero())
		{
			OldMousePosition = LastMousePosition;
			LastMouseAngle = 0.0f;
			LastMouseSampleTime = FPlatformTime::Seconds();
			return FCyLandBrushData();
		}
		else
		{
			float ScaleXY = FMath::Abs(CyLandInfo->DrawScale.X);
			float Radius = EdMode->UISettings->BrushRadius / ScaleXY;
			int32 SizeX = EdMode->UISettings->AlphaTextureSizeX;
			int32 SizeY = EdMode->UISettings->AlphaTextureSizeY;
			float MaxSize = 2.0f * FMath::Sqrt(FMath::Square(Radius) / 2.0f);
			float AlphaBrushScale = MaxSize / (float)FMath::Max<int32>(SizeX, SizeY);
			const float BrushAngle = EdMode->UISettings->bAlphaBrushAutoRotate ? LastMouseAngle : FMath::DegreesToRadians(EdMode->UISettings->AlphaBrushRotation);

			FIntRect Bounds;
			Bounds.Min.X = FMath::FloorToInt(LastMousePosition.X - Radius);
			Bounds.Min.Y = FMath::FloorToInt(LastMousePosition.Y - Radius);
			Bounds.Max.X = FMath::CeilToInt( LastMousePosition.X + Radius);
			Bounds.Max.Y = FMath::CeilToInt( LastMousePosition.Y + Radius);


			// Clamp to CyLand bounds
			int32 MinX, MaxX, MinY, MaxY;
			if (!ensure(CyLandInfo->GetCyLandExtent(MinX, MinY, MaxX, MaxY)))
			{
				// CyLand has no components somehow
				return FCyLandBrushData();
			}
			Bounds.Clip(FIntRect(MinX, MinY, MaxX + 1, MaxY + 1));

			FCyLandBrushData BrushData(Bounds);

			for (int32 Y = Bounds.Min.Y; Y < Bounds.Max.Y; Y++)
			{
				float* Scanline = BrushData.GetDataPtr(FIntPoint(0, Y));

				for (int32 X = Bounds.Min.X; X < Bounds.Max.X; X++)
				{
					// Find alphamap sample location
					float ScaleSampleX = ((float)X - LastMousePosition.X) / AlphaBrushScale;
					float ScaleSampleY = ((float)Y - LastMousePosition.Y) / AlphaBrushScale;

					// Rotate around center to match angle
					float SampleX = ScaleSampleX * FMath::Cos(BrushAngle) - ScaleSampleY * FMath::Sin(BrushAngle);
					float SampleY = ScaleSampleY * FMath::Cos(BrushAngle) + ScaleSampleX * FMath::Sin(BrushAngle);

					SampleX += (float)SizeX * 0.5f;
					SampleY += (float)SizeY * 0.5f;

					if (SampleX >= 0 && SampleX <= (SizeX - 1) &&
						SampleY >= 0 && SampleY <= (SizeY - 1))
					{
						// Sample the alpha texture
						float Alpha = GetAlphaSample(SampleX, SampleY);

						if (Alpha > 0.0f)
						{
							// Set the brush value for this vertex
							FIntPoint VertexKey = FIntPoint(X, Y);

							if (EdMode->CurrentTool && EdMode->CurrentTool->GetToolType() != ECyLandToolType::Mask
								&& EdMode->UISettings->bUseSelectedRegion && CyLandInfo->SelectedRegion.Num() > 0)
							{
								float MaskValue = CyLandInfo->SelectedRegion.FindRef(FIntPoint(X, Y));
								if (EdMode->UISettings->bUseNegativeMask)
								{
									MaskValue = 1.0f - MaskValue;
								}
								Alpha *= MaskValue;
							}

							Scanline[X] = Alpha;
						}
					}
				}
			}

			return BrushData;
		}
	}

	virtual void MouseMove(float CyLandX, float CyLandY) override
	{
		FCyLandBrushAlphaBase::MouseMove(CyLandX, CyLandY);

		if (EdMode->UISettings->bAlphaBrushAutoRotate)
		{
			// don't do anything with the angle unless we move at least 0.1 units.
			FVector2D MouseDelta = LastMousePosition - OldMousePosition;
			if (MouseDelta.SizeSquared() >= FMath::Square(0.5f))
			{
				double SampleTime = FPlatformTime::Seconds();
				float DeltaTime = (float)(SampleTime - LastMouseSampleTime);
				FVector2D MouseDirection = MouseDelta.GetSafeNormal();
				float MouseAngle = FMath::Lerp(LastMouseAngle, FMath::Atan2(-MouseDirection.Y, MouseDirection.X), FMath::Min<float>(10.0f * DeltaTime, 1.0f));		// lerp over 100ms
				LastMouseAngle = MouseAngle;
				LastMouseSampleTime = SampleTime;
				OldMousePosition = LastMousePosition;
				// UE_LOG(LogCyLand, Log, TEXT("(%f,%f) delta (%f,%f) angle %f"), CyLandX, CyLandY, MouseDirection.X, MouseDirection.Y, MouseAngle);
			}
		}
	}

	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override
	{
		FCyLandBrushCircle::Tick(ViewportClient, DeltaTime);

		UCyLandInfo* CyLandInfo = EdMode->CurrentToolTarget.CyLandInfo.Get();
		if (CyLandInfo)
		{
			float ScaleXY = FMath::Abs(CyLandInfo->DrawScale.X);
			int32 SizeX = EdMode->UISettings->AlphaTextureSizeX;
			int32 SizeY = EdMode->UISettings->AlphaTextureSizeY;
			float Radius = EdMode->UISettings->BrushRadius / ScaleXY;
			float MaxSize = 2.0f * FMath::Sqrt(FMath::Square(Radius) / 2.0f);
			float AlphaBrushScale = MaxSize / (float)FMath::Max<int32>(SizeX, SizeY);

			FLinearColor BrushScaleRot(
				1.0f / (AlphaBrushScale * SizeX),
				1.0f / (AlphaBrushScale * SizeY),
				0.0f,
				EdMode->UISettings->bAlphaBrushAutoRotate ? LastMouseAngle : FMath::DegreesToRadians(EdMode->UISettings->AlphaBrushRotation)
				);

			int32 Channel = EdMode->UISettings->AlphaTextureChannel;
			FLinearColor AlphaTextureMask(Channel == 0 ? 1 : 0, Channel == 1 ? 1 : 0, Channel == 2 ? 1 : 0, Channel == 3 ? 1 : 0);

			for (const auto& BrushMaterialInstancePair : BrushMaterialInstanceMap)
			{
				UMaterialInstanceDynamic* const MaterialInstance = BrushMaterialInstancePair.Value;
				MaterialInstance->SetVectorParameterValue(FName(TEXT("BrushScaleRot")), BrushScaleRot);
				MaterialInstance->SetVectorParameterValue(FName(TEXT("AlphaTextureMask")), AlphaTextureMask);
				MaterialInstance->SetTextureParameterValue(FName(TEXT("AlphaTexture")), EdMode->UISettings->AlphaTexture);
			}
		}
	}

	virtual const TCHAR* GetBrushName() override { return TEXT("Alpha"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_Brush_Alpha", "Alpha"); };

};


void FEdModeCyLand::InitializeBrushes()
{
	FCyLandBrushSet* BrushSet;
	BrushSet = new(CyLandBrushSets)FCyLandBrushSet(TEXT("BrushSet_Circle"));
	BrushSet->Brushes.Add(FCyLandBrushCircle_Smooth::Create(this));
	BrushSet->Brushes.Add(FCyLandBrushCircle_Linear::Create(this));
	BrushSet->Brushes.Add(FCyLandBrushCircle_Spherical::Create(this));
	BrushSet->Brushes.Add(FCyLandBrushCircle_Tip::Create(this));

	BrushSet = new(CyLandBrushSets)FCyLandBrushSet(TEXT("BrushSet_Alpha"));
	BrushSet->Brushes.Add(FCyLandBrushAlpha::Create(this));

	BrushSet = new(CyLandBrushSets)FCyLandBrushSet(TEXT("BrushSet_Pattern"));
	BrushSet->Brushes.Add(FCyLandBrushAlphaPattern::Create(this));

	BrushSet = new(CyLandBrushSets)FCyLandBrushSet(TEXT("BrushSet_Component"));
	BrushSet->Brushes.Add(new FCyLandBrushComponent(this));

	BrushSet = new(CyLandBrushSets)FCyLandBrushSet(TEXT("BrushSet_Gizmo"));
	GizmoBrush = new FCyLandBrushGizmo(this);
	BrushSet->Brushes.Add(GizmoBrush);

	BrushSet = new(CyLandBrushSets)FCyLandBrushSet(TEXT("BrushSet_Splines"));
	BrushSet->Brushes.Add(new FCyLandBrushSplines(this));

	BrushSet = new(CyLandBrushSets)FCyLandBrushSet(TEXT("BrushSet_Dummy"));
	BrushSet->Brushes.Add(new FCyLandBrushDummy(this));
}
