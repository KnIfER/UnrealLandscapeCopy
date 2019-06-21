// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "Engine/EngineTypes.h"
#include "CyLandToolInterface.h"
#include "CyLandProxy.h"
#include "CyLandGizmoActiveActor.h"
#include "CyLandEdMode.h"
#include "CyLandEditorObject.h"
#include "CyLand.h"
#include "CyLandStreamingProxy.h"
#include "ObjectTools.h"
#include "CyLandEdit.h"
#include "CyLandComponent.h"
#include "CyLandRender.h"
#include "PropertyEditorModule.h"
#include "InstancedFoliageActor.h"
#include "CyLandEdModeTools.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Materials/MaterialExpressionCyLandVisibilityMask.h"
#include "Algo/Copy.h"
#include "Settings/EditorExperimentalSettings.h"

#define LOCTEXT_NAMESPACE "CyLand"

//
// FCyLandToolSelect
//
class FCyLandToolStrokeSelect : public FCyLandToolStrokeBase
{
	bool bInitializedComponentInvert;
	bool bInvert;
	bool bNeedsSelectionUpdate;

public:
	FCyLandToolStrokeSelect(FEdModeCyLand* InEdMode, FEditorViewportClient* InViewportClient, const FCyLandToolTarget& InTarget)
		: FCyLandToolStrokeBase(InEdMode, InViewportClient, InTarget)
		, bInitializedComponentInvert(false)
		, bNeedsSelectionUpdate(false)
		, Cache(InTarget)
	{
	}

	~FCyLandToolStrokeSelect()
	{
		if (bNeedsSelectionUpdate)
		{
			TArray<UObject*> Objects;
			if (CyLandInfo)
			{
				TSet<UCyLandComponent*> SelectedComponents = CyLandInfo->GetSelectedComponents();
				Objects.Reset(SelectedComponents.Num());
				Algo::Copy(SelectedComponents, Objects);
			}
			FPropertyEditorModule& PropertyModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
			PropertyModule.UpdatePropertyViews(Objects);
		}
	}

	void Apply(FEditorViewportClient* ViewportClient, FCyLandBrush* Brush, const UCyLandEditorObject* UISettings, const TArray<FCyLandToolInteractorPosition>& InteractorPositions)
	{
		if (CyLandInfo)
		{
			CyLandInfo->Modify();

			// TODO - only retrieve bounds as we don't need the data
			const FCyLandBrushData BrushInfo = Brush->ApplyBrush(InteractorPositions);
			if (!BrushInfo)
			{
				return;
			}

			int32 X1, Y1, X2, Y2;
			BrushInfo.GetInclusiveBounds(X1, Y1, X2, Y2);

			// Shrink bounds by 1,1 to avoid GetComponentsInRegion picking up extra components on all sides due to the overlap between components
			TSet<UCyLandComponent*> NewComponents;
			CyLandInfo->GetComponentsInRegion(X1 + 1, Y1 + 1, X2 - 1, Y2 - 1, NewComponents);

			if (!bInitializedComponentInvert)
			{
				// Get the component under the mouse location. Copied from FCyLandBrushComponent::ApplyBrush()
				const float MouseX = InteractorPositions[0].Position.X;
				const float MouseY = InteractorPositions[0].Position.Y;
				const int32 MouseComponentIndexX = (MouseX >= 0.0f) ? FMath::FloorToInt(MouseX / CyLandInfo->ComponentSizeQuads) : FMath::CeilToInt(MouseX / CyLandInfo->ComponentSizeQuads);
				const int32 MouseComponentIndexY = (MouseY >= 0.0f) ? FMath::FloorToInt(MouseY / CyLandInfo->ComponentSizeQuads) : FMath::CeilToInt(MouseY / CyLandInfo->ComponentSizeQuads);
				UCyLandComponent* MouseComponent = CyLandInfo->XYtoComponentMap.FindRef(FIntPoint(MouseComponentIndexX, MouseComponentIndexY));

				if (MouseComponent != nullptr)
				{
					bInvert = CyLandInfo->GetSelectedComponents().Contains(MouseComponent);
				}
				else
				{
					bInvert = false;
				}

				bInitializedComponentInvert = true;
			}

			TSet<UCyLandComponent*> NewSelection;
			if (bInvert)
			{
				NewSelection = CyLandInfo->GetSelectedComponents().Difference(NewComponents);
			}
			else
			{
				NewSelection = CyLandInfo->GetSelectedComponents().Union(NewComponents);
			}

			CyLandInfo->Modify();
			CyLandInfo->UpdateSelectedComponents(NewSelection);

			// Update Details tab with selection
			bNeedsSelectionUpdate = true;
		}
	}

protected:
	FCyLandDataCache Cache;
};

class FCyLandToolSelect : public FCyLandToolBase<FCyLandToolStrokeSelect>
{
public:
	FCyLandToolSelect(FEdModeCyLand* InEdMode)
		: FCyLandToolBase<FCyLandToolStrokeSelect>(InEdMode)
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("Select"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_Selection", "Component Selection"); };
	virtual void SetEditRenderType() override { GCyLandEditRenderMode = ECyLandEditRenderMode::SelectComponent | (GCyLandEditRenderMode & ECyLandEditRenderMode::BitMaskForMask); }
	virtual bool SupportsMask() override { return false; }
};

//
// FCyLandToolMask
//
class FCyLandToolStrokeMask : public FCyLandToolStrokeBase
{
public:
	FCyLandToolStrokeMask(FEdModeCyLand* InEdMode, FEditorViewportClient* InViewportClient, const FCyLandToolTarget& InTarget)
		: FCyLandToolStrokeBase(InEdMode, InViewportClient, InTarget)
		, Cache(InTarget)
	{
	}

	void Apply(FEditorViewportClient* ViewportClient, FCyLandBrush* Brush, const UCyLandEditorObject* UISettings, const TArray<FCyLandToolInteractorPosition>& InteractorPositions)
	{
		if (CyLandInfo)
		{
			CyLandInfo->Modify();

			// Invert when holding Shift
			bool bInvert = InteractorPositions[ InteractorPositions.Num() - 1].bModifierPressed;

			const FCyLandBrushData BrushInfo = Brush->ApplyBrush(InteractorPositions);
			if (!BrushInfo)
			{
				return;
			}

			int32 X1, Y1, X2, Y2;
			BrushInfo.GetInclusiveBounds(X1, Y1, X2, Y2);

			// Tablet pressure
			float Pressure = ViewportClient->Viewport->IsPenActive() ? ViewportClient->Viewport->GetTabletPressure() : 1.0f;

			Cache.CacheData(X1, Y1, X2, Y2);
			TArray<uint8> Data;
			Cache.GetCachedData(X1, Y1, X2, Y2, Data);

			TSet<UCyLandComponent*> NewComponents;
			CyLandInfo->GetComponentsInRegion(X1, Y1, X2, Y2, NewComponents);
			CyLandInfo->UpdateSelectedComponents(NewComponents, false);

			for (int32 Y = BrushInfo.GetBounds().Min.Y; Y < BrushInfo.GetBounds().Max.Y; Y++)
			{
				const float* BrushScanline = BrushInfo.GetDataPtr(FIntPoint(0, Y));
				uint8* DataScanline = Data.GetData() + (Y - Y1) * (X2 - X1 + 1) + (0 - X1);

				for (int32 X = BrushInfo.GetBounds().Min.X; X < BrushInfo.GetBounds().Max.X; X++)
				{
					const FIntPoint Key = FIntPoint(X, Y);
					const float BrushValue = BrushScanline[X];

					if (BrushValue > 0.0f && CyLandInfo->IsValidPosition(X, Y))
					{
						float PaintValue = BrushValue * UISettings->ToolStrength * Pressure;
						float Value = DataScanline[X] / 255.0f;
						checkSlow(FMath::IsNearlyEqual(Value, CyLandInfo->SelectedRegion.FindRef(Key), 1 / 255.0f));
						if (bInvert)
						{
							Value = FMath::Max(Value - PaintValue, 0.0f);
						}
						else
						{
							Value = FMath::Min(Value + PaintValue, 1.0f);
						}
						if (Value > 0.0f)
						{
							CyLandInfo->SelectedRegion.Add(Key, Value);
						}
						else
						{
							CyLandInfo->SelectedRegion.Remove(Key);
						}

						DataScanline[X] = FMath::Clamp<int32>(FMath::RoundToInt(Value * 255), 0, 255);
					}
				}
			}

			ACyLand* CyLand = CyLandInfo->CyLandActor.Get();

			if (CyLand != nullptr && CyLand->HasProceduralContent && !GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
			{
				FMessageLog("MapCheck").Warning()->AddToken(FTextToken::Create(LOCTEXT("CyLandProcedural_ChangingDataWithoutSettings", "This map contains CyLand procedural content, modifying the CyLand data will result in data loss when the map is reopened with CyLand Procedural settings on. Please enable CyLand Procedural settings before modifying the data.")));
				FMessageLog("MapCheck").Open(EMessageSeverity::Warning);
			}

			Cache.SetCachedData(X1, Y1, X2, Y2, Data);
			Cache.Flush();
		}
	}

protected:
	FCyLandDataCache Cache;
};

class FCyLandToolMask : public FCyLandToolBase<FCyLandToolStrokeMask>
{
public:
	FCyLandToolMask(FEdModeCyLand* InEdMode)
		: FCyLandToolBase<FCyLandToolStrokeMask>(InEdMode)
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("Mask"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_Mask", "Region Selection"); };
	virtual void SetEditRenderType() override { GCyLandEditRenderMode = ECyLandEditRenderMode::SelectRegion | (GCyLandEditRenderMode & ECyLandEditRenderMode::BitMaskForMask); }
	virtual bool SupportsMask() override { return true; }

	virtual ECyLandToolType GetToolType() override { return ECyLandToolType::Mask; }
};

//
// FCyLandToolVisibility
//
class FCyLandToolStrokeVisibility : public FCyLandToolStrokeBase
{
public:
	FCyLandToolStrokeVisibility(FEdModeCyLand* InEdMode, FEditorViewportClient* InViewportClient, const FCyLandToolTarget& InTarget)
		: FCyLandToolStrokeBase(InEdMode, InViewportClient, InTarget)
		, Cache(InTarget)
	{
	}

	void Apply(FEditorViewportClient* ViewportClient, FCyLandBrush* Brush, const UCyLandEditorObject* UISettings, const TArray<FCyLandToolInteractorPosition>& InteractorPositions)
	{
		if (CyLandInfo)
		{
			CyLandInfo->Modify();
			// Get list of verts to update
			FCyLandBrushData BrushInfo = Brush->ApplyBrush(InteractorPositions);
			if (!BrushInfo)
			{
				return;
			}

			int32 X1, Y1, X2, Y2;
			BrushInfo.GetInclusiveBounds(X1, Y1, X2, Y2);

			// Invert when holding Shift
			bool bInvert = InteractorPositions[InteractorPositions.Num() - 1].bModifierPressed;

			// Tablet pressure
			float Pressure = ViewportClient->Viewport->IsPenActive() ? ViewportClient->Viewport->GetTabletPressure() : 1.0f;

			Cache.CacheData(X1, Y1, X2, Y2);
			TArray<uint8> Data;
			Cache.GetCachedData(X1, Y1, X2, Y2, Data);

			for (int32 Y = BrushInfo.GetBounds().Min.Y; Y < BrushInfo.GetBounds().Max.Y; Y++)
			{
				const float* BrushScanline = BrushInfo.GetDataPtr(FIntPoint(0, Y));
				uint8* DataScanline = Data.GetData() + (Y - Y1) * (X2 - X1 + 1) + (0 - X1);

				for (int32 X = BrushInfo.GetBounds().Min.X; X < BrushInfo.GetBounds().Max.X; X++)
				{
					const float BrushValue = BrushScanline[X];

					if (BrushValue > 0.0f)
					{
						uint8 Value = bInvert ? 0 : 255; // Just on and off for visibility, for masking...
						DataScanline[X] = Value;
					}
				}
			}

			ACyLand* CyLand = CyLandInfo->CyLandActor.Get();

			if (CyLand != nullptr && CyLand->HasProceduralContent && !GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
			{
				FMessageLog("MapCheck").Warning()->AddToken(FTextToken::Create(LOCTEXT("CyLandProcedural_ChangingDataWithoutSettings", "This map contains CyLand procedural content, modifying the CyLand data will result in data loss when the map is reopened with CyLand Procedural settings on. Please enable CyLand Procedural settings before modifying the data.")));
				FMessageLog("MapCheck").Open(EMessageSeverity::Warning);
			}

			Cache.SetCachedData(X1, Y1, X2, Y2, Data);
			Cache.Flush();
		}
	}

protected:
	FCyLandVisCache Cache;
};

class FCyLandToolVisibility : public FCyLandToolBase<FCyLandToolStrokeVisibility>
{
public:
	FCyLandToolVisibility(FEdModeCyLand* InEdMode)
		: FCyLandToolBase<FCyLandToolStrokeVisibility>(InEdMode)
	{
	}

	virtual bool BeginTool(FEditorViewportClient* ViewportClient, const FCyLandToolTarget& InTarget, const FVector& InHitLocation) override
	{
		ACyLandProxy* Proxy = InTarget.CyLandInfo->GetCyLandProxy();
		UMaterialInterface* HoleMaterial = Proxy->GetCyLandHoleMaterial();
		if (!HoleMaterial)
		{
			HoleMaterial = Proxy->GetCyLandMaterial();
		}
		if (!HoleMaterial->GetMaterial()->HasAnyExpressionsInMaterialAndFunctionsOfType<UMaterialExpressionCyLandVisibilityMask>())
		{
			FMessageDialog::Open(EAppMsgType::Ok,
				LOCTEXT("CyLandVisibilityMaskMissing", "You must add a \"CyLand Visibility Mask\" node to your material before you can paint visibility."));
			return false;
		}

		return FCyLandToolBase<FCyLandToolStrokeVisibility>::BeginTool(ViewportClient, InTarget, InHitLocation);
	}

	virtual const TCHAR* GetToolName() override { return TEXT("Visibility"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_Visibility", "Visibility"); };

	virtual void SetEditRenderType() override { GCyLandEditRenderMode = ECyLandEditRenderMode::None | (GCyLandEditRenderMode & ECyLandEditRenderMode::BitMaskForMask); }
	virtual bool SupportsMask() override { return false; }

	virtual ECyLandToolTargetTypeMask::Type GetSupportedTargetTypes() override
	{
		return ECyLandToolTargetTypeMask::Visibility;
	}
};

//
// FCyLandToolMoveToLevel
//
class FCyLandToolStrokeMoveToLevel : public FCyLandToolStrokeBase
{
public:
	FCyLandToolStrokeMoveToLevel(FEdModeCyLand* InEdMode, FEditorViewportClient* InViewportClient, const FCyLandToolTarget& InTarget)
		: FCyLandToolStrokeBase(InEdMode, InViewportClient, InTarget)
	{
	}

	void Apply(FEditorViewportClient* ViewportClient, FCyLandBrush* Brush, const UCyLandEditorObject* UISettings, const TArray<FCyLandToolInteractorPosition>& InteractorPositions)
	{
		ACyLand* CyLand = CyLandInfo ? CyLandInfo->CyLandActor.Get() : nullptr;

		if (CyLand)
		{
			CyLand->Modify();
			CyLandInfo->Modify();

			TArray<UObject*> RenameObjects;
			FString MsgBoxList;

			// Check the Physical Material is same package with CyLand
			if (CyLand->DefaultPhysMaterial && CyLand->DefaultPhysMaterial->GetOutermost() == CyLand->GetOutermost())
			{
				//FMessageDialog::Open( EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "CyLandPhyMaterial_Warning", "CyLand's DefaultPhysMaterial is in the same package as the CyLand Actor. To support streaming levels, you must move the PhysicalMaterial to another package.") );
				RenameObjects.AddUnique(CyLand->DefaultPhysMaterial);
				MsgBoxList += CyLand->DefaultPhysMaterial->GetPathName();
				MsgBoxList += FString::Printf(TEXT("\n"));
			}

			// Check the LayerInfoObjects are same package with CyLand
			for (int32 i = 0; i < CyLandInfo->Layers.Num(); ++i)
			{
				UCyLandLayerInfoObject* LayerInfo = CyLandInfo->Layers[i].LayerInfoObj;
				if (LayerInfo && LayerInfo->GetOutermost() == CyLand->GetOutermost())
				{
					RenameObjects.AddUnique(LayerInfo);
					MsgBoxList += LayerInfo->GetPathName();
					MsgBoxList += FString::Printf(TEXT("\n"));
				}
			}

			auto SelectedComponents = CyLandInfo->GetSelectedComponents();
			bool bBrush = false;
			if (!SelectedComponents.Num())
			{
				// Get list of verts to update
				// TODO - only retrieve bounds as we don't need the data
				FCyLandBrushData BrushInfo = Brush->ApplyBrush(InteractorPositions);
				if (!BrushInfo)
				{
					return;
				}

				int32 X1, Y1, X2, Y2;
				BrushInfo.GetInclusiveBounds(X1, Y1, X2, Y2);

				// Shrink bounds by 1,1 to avoid GetComponentsInRegion picking up extra components on all sides due to the overlap between components
				CyLandInfo->GetComponentsInRegion(X1 + 1, Y1 + 1, X2 - 1, Y2 - 1, SelectedComponents);
				bBrush = true;
			}

			check(ViewportClient->GetScene());
			UWorld* World = ViewportClient->GetScene()->GetWorld();
			check(World);
			if (SelectedComponents.Num())
			{
				bool bIsAllCurrentLevel = true;
				for (UCyLandComponent* Component : SelectedComponents)
				{
					if (Component->GetCyLandProxy()->GetLevel() != World->GetCurrentLevel())
					{
						bIsAllCurrentLevel = false;
					}
				}

				if (bIsAllCurrentLevel)
				{
					// Need to fix double WM
					if (!bBrush)
					{
						// Remove Selection
						CyLandInfo->ClearSelectedRegion(true);
					}
					return;
				}

				for (UCyLandComponent* Component : SelectedComponents)
				{
					UMaterialInterface* CyLandMaterial = Component->GetCyLandMaterial();
					if (CyLandMaterial && CyLandMaterial->GetOutermost() == Component->GetOutermost())
					{
						RenameObjects.AddUnique(CyLandMaterial);
						MsgBoxList += Component->GetName() + TEXT("'s ") + CyLandMaterial->GetPathName();
						MsgBoxList += FString::Printf(TEXT("\n"));
						//It.RemoveCurrent();
					}
				}

				if (RenameObjects.Num())
				{
					if (FMessageDialog::Open(EAppMsgType::OkCancel,
						FText::Format(
						NSLOCTEXT("UnrealEd", "CyLandMoveToStreamingLevel_SharedResources", "The following items must be moved out of the persistent level and into a package that can be shared between multiple levels:\n\n{0}"),
						FText::FromString(MsgBoxList))) == EAppReturnType::Type::Ok)
					{
						FString Path = CyLand->GetOutermost()->GetName() + TEXT("_sharedassets/");
						bool bSucceed = ObjectTools::RenameObjects(RenameObjects, false, TEXT(""), Path);
						if (!bSucceed)
						{
							FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "CyLandMoveToStreamingLevel_RenameFailed", "Move To Streaming Level did not succeed because shared resources could not be moved to a new package."));
							return;
						}
					}
					else
					{
						return;
					}
				}

				FScopedSlowTask SlowTask(0, LOCTEXT("BeginMovingCyLandComponentsToCurrentLevelTask", "Moving CyLand components to current level"));
				SlowTask.MakeDialogDelayed(10); // show slow task dialog after 10 seconds

				CyLandInfo->SortSelectedComponents();
				const int32 ComponentSizeVerts = CyLand->NumSubsections * (CyLand->SubsectionSizeQuads + 1);
				const int32 NeedHeightmapSize = 1 << FMath::CeilLogTwo(ComponentSizeVerts);

				TSet<ACyLandProxy*> SelectProxies;
				TSet<UCyLandComponent*> TargetSelectedComponents;
				TArray<UCyLandHeightfieldCollisionComponent*> TargetSelectedCollisionComponents;
				for (UCyLandComponent* Component : SelectedComponents)
				{
					SelectProxies.Add(Component->GetCyLandProxy());
					if (Component->GetCyLandProxy()->GetOuter() != World->GetCurrentLevel())
					{
						TargetSelectedComponents.Add(Component);
					}

					UCyLandHeightfieldCollisionComponent* CollisionComp = Component->CollisionComponent.Get();
					SelectProxies.Add(CollisionComp->GetCyLandProxy());
					if (CollisionComp->GetCyLandProxy()->GetOuter() != World->GetCurrentLevel())
					{
						TargetSelectedCollisionComponents.Add(CollisionComp);
					}
				}

				// Check which ones are need for height map change
				TSet<UTexture2D*> OldHeightmapTextures;
				for (UCyLandComponent* Component : TargetSelectedComponents)
				{
					Component->Modify();
					OldHeightmapTextures.Add(Component->GetHeightmap());
				}

				// Need to split all the component which share Heightmap with selected components
				TMap<UCyLandComponent*, bool> HeightmapUpdateComponents;
				HeightmapUpdateComponents.Reserve(TargetSelectedComponents.Num() * 4); // worst case
				for (UCyLandComponent* Component : TargetSelectedComponents)
				{
					// Search neighbor only
					const int32 SearchX = Component->GetHeightmap()->Source.GetSizeX() / NeedHeightmapSize - 1;
					const int32 SearchY = Component->GetHeightmap()->Source.GetSizeY() / NeedHeightmapSize - 1;
					const FIntPoint ComponentBase = Component->GetSectionBase() / Component->ComponentSizeQuads;

					for (int32 Y = -SearchY; Y <= SearchY; ++Y)
					{
						for (int32 X = -SearchX; X <= SearchX; ++X)
						{
							UCyLandComponent* const Neighbor = CyLandInfo->XYtoComponentMap.FindRef(ComponentBase + FIntPoint(X, Y));
							if (Neighbor && Neighbor->GetHeightmap() == Component->GetHeightmap() && !HeightmapUpdateComponents.Contains(Neighbor))
							{
								Neighbor->Modify();
								bool bNeedsMoveToCurrentLevel = TargetSelectedComponents.Contains(Neighbor);
								HeightmapUpdateComponents.Add(Neighbor, bNeedsMoveToCurrentLevel);
							}
						}
					}
				}

				// Changing Heightmap format for selected components
				for (const auto& HeightmapUpdateComponentPair : HeightmapUpdateComponents)
				{
					ACyLand::SplitHeightmap(HeightmapUpdateComponentPair.Key, HeightmapUpdateComponentPair.Value);
				}

				// Delete if it is no referenced textures...
				for (UTexture2D* Texture : OldHeightmapTextures)
				{
					Texture->SetFlags(RF_Transactional);
					Texture->Modify();
					Texture->MarkPackageDirty();
					Texture->ClearFlags(RF_Standalone);
				}

				ACyLandProxy* CyLandProxy = CyLandInfo->GetCurrentLevelCyLandProxy(false);
				if (!CyLandProxy)
				{
					CyLandProxy = World->SpawnActor<ACyLandStreamingProxy>();
					// copy shared properties to this new proxy
					CyLandProxy->GetSharedProperties(CyLand);

					// set proxy location
					// by default first component location
					UCyLandComponent* FirstComponent = *TargetSelectedComponents.CreateConstIterator();
					CyLandProxy->GetRootComponent()->SetWorldLocationAndRotation(FirstComponent->GetComponentLocation(), FirstComponent->GetComponentRotation());
					CyLandProxy->CyLandSectionOffset = FirstComponent->GetSectionBase();

					// Hide(unregister) the new CyLand if owning level currently in hidden state
					if (CyLandProxy->GetLevel()->bIsVisible == false)
					{
						CyLandProxy->UnregisterAllComponents();
					}
				}

				for (ACyLandProxy* Proxy : SelectProxies)
				{
					Proxy->Modify();
				}

				CyLandProxy->Modify();
				CyLandProxy->MarkPackageDirty();

				// Handle XY-offset textures (these don't need splitting, as they aren't currently shared between components like heightmaps/weightmaps can be)
				for (UCyLandComponent* Component : TargetSelectedComponents)
				{
					if (Component->XYOffsetmapTexture)
					{
						Component->XYOffsetmapTexture->Modify();
						Component->XYOffsetmapTexture->Rename(nullptr, CyLandProxy->GetOutermost());
					}
				}

				// Change Weight maps...
				{
					FCyLandEditDataInterface CyLandEdit(CyLandInfo);
					for (UCyLandComponent* Component : TargetSelectedComponents)
					{
						int32 TotalNeededChannels = Component->WeightmapLayerAllocations.Num();
						int32 CurrentLayer = 0;
						TArray<UTexture2D*> NewWeightmapTextures;

						// Code from UCyLandComponent::ReallocateWeightmaps
						// Move to other channels left
						while (TotalNeededChannels > 0)
						{
							// UE_LOG(LogCyLand, Log, TEXT("Still need %d channels"), TotalNeededChannels);

							UTexture2D* CurrentWeightmapTexture = nullptr;
							FCyLandWeightmapUsage* CurrentWeightmapUsage = nullptr;

							if (TotalNeededChannels < 4)
							{
								// UE_LOG(LogCyLand, Log, TEXT("Looking for nearest"));

								// see if we can find a suitable existing weightmap texture with sufficient channels
								int32 BestDistanceSquared = MAX_int32;
								for (auto& WeightmapUsagePair : CyLandProxy->WeightmapUsageMap)
								{
									FCyLandWeightmapUsage* TryWeightmapUsage = &WeightmapUsagePair.Value;
									if (TryWeightmapUsage->CyFreeChannelCount() >= TotalNeededChannels)
									{
										// See if this candidate is closer than any others we've found
										for (int32 ChanIdx = 0; ChanIdx < 4; ChanIdx++)
										{
											if (TryWeightmapUsage->ChannelUsage[ChanIdx] != nullptr)
											{
												int32 TryDistanceSquared = (TryWeightmapUsage->ChannelUsage[ChanIdx]->GetSectionBase() - Component->GetSectionBase()).SizeSquared();
												if (TryDistanceSquared < BestDistanceSquared)
												{
													CurrentWeightmapTexture = WeightmapUsagePair.Key;
													CurrentWeightmapUsage = TryWeightmapUsage;
													BestDistanceSquared = TryDistanceSquared;
												}
											}
										}
									}
								}
							}

							bool NeedsUpdateResource = false;
							// No suitable weightmap texture
							if (CurrentWeightmapTexture == nullptr)
							{
								Component->MarkPackageDirty();

								// Weightmap is sized the same as the component
								int32 WeightmapSize = (Component->SubsectionSizeQuads + 1) * Component->NumSubsections;

								// We need a new weightmap texture
								CurrentWeightmapTexture = CyLandProxy->CreateCyLandTexture(WeightmapSize, WeightmapSize, TEXTUREGROUP_Terrain_Weightmap, TSF_BGRA8);
								// Alloc dummy mips
								Component->CreateEmptyTextureMips(CurrentWeightmapTexture);
								CurrentWeightmapTexture->PostEditChange();

								// Store it in the usage map
								CurrentWeightmapUsage = &CyLandProxy->WeightmapUsageMap.Add(CurrentWeightmapTexture, FCyLandWeightmapUsage());

								// UE_LOG(LogCyLand, Log, TEXT("Making a new texture %s"), *CurrentWeightmapTexture->GetName());
							}

							NewWeightmapTextures.Add(CurrentWeightmapTexture);

							for (int32 ChanIdx = 0; ChanIdx < 4 && TotalNeededChannels > 0; ChanIdx++)
							{
								// UE_LOG(LogCyLand, Log, TEXT("Finding allocation for layer %d"), CurrentLayer);

								if (CurrentWeightmapUsage->ChannelUsage[ChanIdx] == nullptr)
								{
									// Use this allocation
									FCyWeightmapLayerAllocationInfo& AllocInfo = Component->WeightmapLayerAllocations[CurrentLayer];

									if (AllocInfo.WeightmapTextureIndex == 255)
									{
										// New layer - zero out the data for this texture channel
										CyLandEdit.ZeroTextureChannel(CurrentWeightmapTexture, ChanIdx);
									}
									else
									{
										UTexture2D* OldWeightmapTexture = Component->WeightmapTextures[AllocInfo.WeightmapTextureIndex];

										// Copy the data
										CyLandEdit.CopyTextureChannel(CurrentWeightmapTexture, ChanIdx, OldWeightmapTexture, AllocInfo.WeightmapTextureChannel);
										CyLandEdit.ZeroTextureChannel(OldWeightmapTexture, AllocInfo.WeightmapTextureChannel);

										// Remove the old allocation
										FCyLandWeightmapUsage* OldWeightmapUsage = Component->GetCyLandProxy()->WeightmapUsageMap.Find(OldWeightmapTexture);
										OldWeightmapUsage->ChannelUsage[AllocInfo.WeightmapTextureChannel] = nullptr;
									}

									// Assign the new allocation
									CurrentWeightmapUsage->ChannelUsage[ChanIdx] = Component;
									AllocInfo.WeightmapTextureIndex = NewWeightmapTextures.Num() - 1;
									AllocInfo.WeightmapTextureChannel = ChanIdx;
									CurrentLayer++;
									TotalNeededChannels--;
								}
							}
						}

						// Replace the weightmap textures
						Component->WeightmapTextures = NewWeightmapTextures;

						// Update the mipmaps for the textures we edited
						for (UTexture2D* WeightmapTexture : Component->WeightmapTextures)
						{
							FCyLandTextureDataInfo* WeightmapDataInfo = CyLandEdit.GetTextureDataInfo(WeightmapTexture);

							int32 NumMips = WeightmapTexture->Source.GetNumMips();
							TArray<FColor*> WeightmapTextureMipData;
							WeightmapTextureMipData.AddUninitialized(NumMips);
							for (int32 MipIdx = 0; MipIdx < NumMips; MipIdx++)
							{
								WeightmapTextureMipData[MipIdx] = (FColor*)WeightmapDataInfo->GetMipData(MipIdx);
							}

							UCyLandComponent::UpdateWeightmapMips(Component->NumSubsections, Component->SubsectionSizeQuads, WeightmapTexture, WeightmapTextureMipData, 0, 0, MAX_int32, MAX_int32, WeightmapDataInfo);
						}
					}
					// Need to Repacking all the Weight map (to make it packed well...)
					CyLand->RemoveInvalidWeightmaps();
				}

				// Move the components to the Proxy actor
				// This does not use the MoveSelectedActorsToCurrentLevel path as there is no support to only move certain components.
				for (UCyLandComponent* Component :TargetSelectedComponents)
				{
					// Need to move or recreate all related data (Height map, Weight map, maybe collision components, allocation info)
					Component->GetCyLandProxy()->CyLandComponents.Remove(Component);
					Component->UnregisterComponent();
					Component->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
					Component->InvalidateLightingCache();
					Component->Rename(nullptr, CyLandProxy);
					CyLandProxy->CyLandComponents.Add(Component);
					Component->AttachToComponent(CyLandProxy->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
					
					// clear transient mobile data
					Component->MobileDataSourceHash.Invalidate();
					Component->MobileMaterialInterfaces.Reset();
					Component->MobileWeightmapTextures.Reset();
					
					Component->UpdateMaterialInstances();

					FFormatNamedArguments Args;
					Args.Add(TEXT("ComponentName"), FText::FromString(Component->GetName()));
				}

				for (UCyLandHeightfieldCollisionComponent* Component : TargetSelectedCollisionComponents)
				{
					// Need to move or recreate all related data (Height map, Weight map, maybe collision components, allocation info)

					Component->GetCyLandProxy()->CollisionComponents.Remove(Component);
					Component->UnregisterComponent();
					Component->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
					Component->Rename(nullptr, CyLandProxy);
					CyLandProxy->CollisionComponents.Add(Component);
					Component->AttachToComponent(CyLandProxy->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);

					// Move any foliage associated
					AInstancedFoliageActor::MoveInstancesForComponentToCurrentLevel(Component);

					FFormatNamedArguments Args;
					Args.Add(TEXT("ComponentName"), FText::FromString(Component->GetName()));
				}

				GEditor->SelectNone(false, true);
				GEditor->SelectActor(CyLandProxy, true, false, true);

				GEditor->SelectNone(false, true);

				// Register our new components if destination CyLand is registered in scene 
				if (CyLandProxy->GetRootComponent()->IsRegistered())
				{
					CyLandProxy->RegisterAllComponents();
				}

				for (ACyLandProxy* Proxy : SelectProxies)
				{
					if (Proxy->GetRootComponent()->IsRegistered())
					{
						Proxy->RegisterAllComponents();
					}
				}

				//CyLand->bLockLocation = (CyLandInfo->XYtoComponentMap.Num() != CyLand->CyLandComponents.Num());

				// Remove Selection
				CyLandInfo->ClearSelectedRegion(true);

				//EdMode->SetMaskEnable(CyLand->SelectedRegion.Num());
			}
		}
	}
};

class FCyLandToolMoveToLevel : public FCyLandToolBase<FCyLandToolStrokeMoveToLevel>
{
public:
	FCyLandToolMoveToLevel(FEdModeCyLand* InEdMode)
		: FCyLandToolBase<FCyLandToolStrokeMoveToLevel>(InEdMode)
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("MoveToLevel"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_MoveToLevel", "Move to Streaming Level"); };

	virtual void SetEditRenderType() override { GCyLandEditRenderMode = ECyLandEditRenderMode::SelectComponent | (GCyLandEditRenderMode & ECyLandEditRenderMode::BitMaskForMask); }
	virtual bool SupportsMask() override { return false; }
};

//
// FCyLandToolAddComponent
//
class FCyLandToolStrokeAddComponent : public FCyLandToolStrokeBase
{
public:
	FCyLandToolStrokeAddComponent(FEdModeCyLand* InEdMode, FEditorViewportClient* InViewportClient, const FCyLandToolTarget& InTarget)
		: FCyLandToolStrokeBase(InEdMode, InViewportClient, InTarget)
		, HeightCache(InTarget)
		, XYOffsetCache(InTarget)
	{
	}

	virtual ~FCyLandToolStrokeAddComponent()
	{
		// We flush here so here ~FXYOffsetmapAccessor can safely lock the heightmap data to update bounds
		HeightCache.Flush();
		XYOffsetCache.Flush();
	}

	virtual void Apply(FEditorViewportClient* ViewportClient, FCyLandBrush* Brush, const UCyLandEditorObject* UISettings, const TArray<FCyLandToolInteractorPosition>& InteractorPositions)
	{
		ACyLandProxy* CyLand = CyLandInfo ? CyLandInfo->GetCurrentLevelCyLandProxy(true) : nullptr;
		if (CyLand && EdMode->CyLandRenderAddCollision)
		{
			check(Brush->GetBrushType() == ECyLandBrushType::Component);

			// Get list of verts to update
			// TODO - only retrieve bounds as we don't need the data
			FCyLandBrushData BrushInfo = Brush->ApplyBrush(InteractorPositions);
			if (!BrushInfo)
			{
				return;
			}

			int32 X1, Y1, X2, Y2;
			BrushInfo.GetInclusiveBounds(X1, Y1, X2, Y2);

			// Find component range for this block of data, non shared vertices
			int32 ComponentIndexX1, ComponentIndexY1, ComponentIndexX2, ComponentIndexY2;
			ACyLand::CalcComponentIndicesNoOverlap(X1, Y1, X2, Y2, CyLand->ComponentSizeQuads, ComponentIndexX1, ComponentIndexY1, ComponentIndexX2, ComponentIndexY2);

			// expand the area by one vertex in each direction to ensure normals are calculated correctly
			X1 -= 1;
			Y1 -= 1;
			X2 += 1;
			Y2 += 1;

			TArray<uint16> Data;
			TArray<FVector> XYOffsetData;
			HeightCache.CacheData(X1, Y1, X2, Y2);
			XYOffsetCache.CacheData(X1, Y1, X2, Y2);
			HeightCache.GetCachedData(X1, Y1, X2, Y2, Data);
			bool bHasXYOffset = XYOffsetCache.GetCachedData(X1, Y1, X2, Y2, XYOffsetData);

			TArray<UCyLandComponent*> NewComponents;
			CyLand->Modify();
			CyLandInfo->Modify();
			for (int32 ComponentIndexY = ComponentIndexY1; ComponentIndexY <= ComponentIndexY2; ComponentIndexY++)
			{
				for (int32 ComponentIndexX = ComponentIndexX1; ComponentIndexX <= ComponentIndexX2; ComponentIndexX++)
				{
					UCyLandComponent* CyLandComponent = CyLandInfo->XYtoComponentMap.FindRef(FIntPoint(ComponentIndexX, ComponentIndexY));
					if (!CyLandComponent)
					{
						// Add New component...
						FIntPoint ComponentBase = FIntPoint(ComponentIndexX, ComponentIndexY)*CyLand->ComponentSizeQuads;
						CyLandComponent = NewObject<UCyLandComponent>(CyLand, NAME_None, RF_Transactional);
						CyLand->CyLandComponents.Add(CyLandComponent);
						NewComponents.Add(CyLandComponent);
						CyLandComponent->Init(
							ComponentBase.X, ComponentBase.Y,
							CyLand->ComponentSizeQuads,
							CyLand->NumSubsections,
							CyLand->SubsectionSizeQuads
							);
						CyLandComponent->AttachToComponent(CyLand->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);

						// Assign shared properties
						CyLandComponent->UpdatedSharedPropertiesFromActor();

						int32 ComponentVerts = (CyLand->SubsectionSizeQuads + 1) * CyLand->NumSubsections;
						// Update Weightmap Scale Bias
						CyLandComponent->WeightmapScaleBias = FVector4(1.0f / (float)ComponentVerts, 1.0f / (float)ComponentVerts, 0.5f / (float)ComponentVerts, 0.5f / (float)ComponentVerts);
						CyLandComponent->WeightmapSubsectionOffset = (float)(CyLandComponent->SubsectionSizeQuads + 1) / (float)ComponentVerts;

						TArray<FColor> HeightData;
						HeightData.Empty(FMath::Square(ComponentVerts));
						HeightData.AddZeroed(FMath::Square(ComponentVerts));
						CyLandComponent->InitHeightmapData(HeightData, true);
						CyLandComponent->UpdateMaterialInstances();

						CyLandInfo->XYtoComponentMap.Add(FIntPoint(ComponentIndexX, ComponentIndexY), CyLandComponent);
						CyLandInfo->XYtoAddCollisionMap.Remove(FIntPoint(ComponentIndexX, ComponentIndexY));
					}
				}
			}

			// Need to register to use general height/xyoffset data update
			for (int32 Idx = 0; Idx < NewComponents.Num(); Idx++)
			{
				NewComponents[Idx]->RegisterComponent();
			}

			if (CyLandInfo->CyLandActor.IsValid() && CyLandInfo->CyLandActor.Get()->HasProceduralContent && !GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
			{
				FMessageLog("MapCheck").Warning()->AddToken(FTextToken::Create(LOCTEXT("CyLandProcedural_ChangingDataWithoutSettings", "This map contains CyLand procedural content, modifying the CyLand data will result in data loss when the map is reopened with CyLand Procedural settings on. Please enable CyLand Procedural settings before modifying the data.")));
				FMessageLog("MapCheck").Open(EMessageSeverity::Warning);
			}

			if (bHasXYOffset)
			{
				XYOffsetCache.SetCachedData(X1, Y1, X2, Y2, XYOffsetData);
				XYOffsetCache.Flush();
			}

			HeightCache.SetCachedData(X1, Y1, X2, Y2, Data);
			HeightCache.Flush();

			for (UCyLandComponent* NewComponent : NewComponents)
			{
				// Update Collision
				NewComponent->UpdateCachedBounds();
				NewComponent->UpdateBounds();
				NewComponent->MarkRenderStateDirty();
				UCyLandHeightfieldCollisionComponent* CollisionComp = NewComponent->CollisionComponent.Get();
				if (CollisionComp && !bHasXYOffset)
				{
					CollisionComp->MarkRenderStateDirty();
					CollisionComp->RecreateCollision();
				}

				TMap<UCyLandLayerInfoObject*, int32> NeighbourLayerInfoObjectCount;

				// Cover 9 tiles around us to determine which object should we use by default
				for (int32 ComponentIndexX = ComponentIndexX1 - 1; ComponentIndexX <= ComponentIndexX2 + 1; ++ComponentIndexX)
				{
					for (int32 ComponentIndexY = ComponentIndexY1 - 1; ComponentIndexY <= ComponentIndexY2 + 1; ++ComponentIndexY)
					{
						UCyLandComponent* NeighbourComponent = CyLandInfo->XYtoComponentMap.FindRef(FIntPoint(ComponentIndexX, ComponentIndexY));

						if (NeighbourComponent != nullptr && NeighbourComponent != NewComponent)
						{
							UCyLandInfo* NeighbourCyLandInfo = NeighbourComponent->GetCyLandInfo();

							for (int32 i = 0; i < NeighbourCyLandInfo->Layers.Num(); ++i)
							{
								UCyLandLayerInfoObject* NeighbourLayerInfo = NeighbourCyLandInfo->Layers[i].LayerInfoObj;

								if (NeighbourLayerInfo != nullptr)
								{
									TArray<uint8> WeightmapTextureData;

									FCyLandComponentDataInterface DataInterface(NeighbourComponent);
									DataInterface.GetWeightmapTextureData(NeighbourLayerInfo, WeightmapTextureData);

									if (WeightmapTextureData.Num() > 0)
									{
										int32* Count = NeighbourLayerInfoObjectCount.Find(NeighbourLayerInfo);

										if (Count == nullptr)
										{
											Count = &NeighbourLayerInfoObjectCount.Add(NeighbourLayerInfo, 1);
										}

										for (uint8 Value : WeightmapTextureData)
										{
											(*Count) += Value;
										}
									}
								}
							}							
						}
					}					
				}

				int32 BestLayerInfoObjectCount = 0;
				UCyLandLayerInfoObject* BestLayerInfoObject = nullptr;

				for (auto& LayerInfoObjectCount : NeighbourLayerInfoObjectCount)
				{
					if (LayerInfoObjectCount.Value > BestLayerInfoObjectCount)
					{
						BestLayerInfoObjectCount = LayerInfoObjectCount.Value;
						BestLayerInfoObject = LayerInfoObjectCount.Key;
					}
				}
				
				if (BestLayerInfoObject != nullptr)
				{
					FCyLandEditDataInterface CyLandEdit(CyLandInfo);
					NewComponent->FillLayer(BestLayerInfoObject, CyLandEdit);
				}
			}

			EdMode->CyLandRenderAddCollision = nullptr;

			// Add/update "add collision" around the newly added components
			{
				// Top row
				int32 ComponentIndexY = ComponentIndexY1 - 1;
				for (int32 ComponentIndexX = ComponentIndexX1 - 1; ComponentIndexX <= ComponentIndexX2 + 1; ++ComponentIndexX)
				{
					if (!CyLandInfo->XYtoComponentMap.FindRef(FIntPoint(ComponentIndexX, ComponentIndexY)))
					{
						CyLandInfo->UpdateAddCollision(FIntPoint(ComponentIndexX, ComponentIndexY));
					}
				}

				// Sides
				for (ComponentIndexY = ComponentIndexY1; ComponentIndexY <= ComponentIndexY2; ++ComponentIndexY)
				{
					// Left
					int32 ComponentIndexX = ComponentIndexX1 - 1;
					if (!CyLandInfo->XYtoComponentMap.FindRef(FIntPoint(ComponentIndexX, ComponentIndexY)))
					{
						CyLandInfo->UpdateAddCollision(FIntPoint(ComponentIndexX, ComponentIndexY));
					}

					// Right
					ComponentIndexX = ComponentIndexX1 + 1;
					if (!CyLandInfo->XYtoComponentMap.FindRef(FIntPoint(ComponentIndexX, ComponentIndexY)))
					{
						CyLandInfo->UpdateAddCollision(FIntPoint(ComponentIndexX, ComponentIndexY));
					}
				}

				// Bottom row
				ComponentIndexY = ComponentIndexY2 + 1;
				for (int32 ComponentIndexX = ComponentIndexX1 - 1; ComponentIndexX <= ComponentIndexX2 + 1; ++ComponentIndexX)
				{
					if (!CyLandInfo->XYtoComponentMap.FindRef(FIntPoint(ComponentIndexX, ComponentIndexY)))
					{
						CyLandInfo->UpdateAddCollision(FIntPoint(ComponentIndexX, ComponentIndexY));
					}
				}
			}

			GEngine->BroadcastOnActorMoved(CyLand);
		}
	}

protected:
	FCyLandHeightCache HeightCache;
	FCyLandXYOffsetCache<true> XYOffsetCache;
};

class FCyLandToolAddComponent : public FCyLandToolBase<FCyLandToolStrokeAddComponent>
{
public:
	FCyLandToolAddComponent(FEdModeCyLand* InEdMode)
		: FCyLandToolBase<FCyLandToolStrokeAddComponent>(InEdMode)
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("AddComponent"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_AddComponent", "Add New CyLand Component"); };

	virtual void SetEditRenderType() override { GCyLandEditRenderMode = ECyLandEditRenderMode::None | (GCyLandEditRenderMode & ECyLandEditRenderMode::BitMaskForMask); }
	virtual bool SupportsMask() override { return false; }

	virtual void EnterTool() override
	{
		FCyLandToolBase<FCyLandToolStrokeAddComponent>::EnterTool();
		UCyLandInfo* CyLandInfo = EdMode->CurrentToolTarget.CyLandInfo.Get();
		CyLandInfo->UpdateAllAddCollisions(); // Todo - as this is only used by this tool, move it into this tool?
	}

	virtual void ExitTool() override
	{
		FCyLandToolBase<FCyLandToolStrokeAddComponent>::ExitTool();

		EdMode->CyLandRenderAddCollision = nullptr;
	}
};

//
// FCyLandToolDeleteComponent
//
class FCyLandToolStrokeDeleteComponent : public FCyLandToolStrokeBase
{
public:
	FCyLandToolStrokeDeleteComponent(FEdModeCyLand* InEdMode, FEditorViewportClient* InViewportClient, const FCyLandToolTarget& InTarget)
		: FCyLandToolStrokeBase(InEdMode, InViewportClient, InTarget)
	{
	}

	void Apply(FEditorViewportClient* ViewportClient, FCyLandBrush* Brush, const UCyLandEditorObject* UISettings, const TArray<FCyLandToolInteractorPosition>& InteractorPositions)
	{
		if (CyLandInfo)
		{
			auto SelectedComponents = CyLandInfo->GetSelectedComponents();
			if (SelectedComponents.Num() == 0)
			{
				// Get list of components to delete from brush
				// TODO - only retrieve bounds as we don't need the vert data
				FCyLandBrushData BrushInfo = Brush->ApplyBrush(InteractorPositions);
				if (!BrushInfo)
				{
					return;
				}

				int32 X1, Y1, X2, Y2;
				BrushInfo.GetInclusiveBounds(X1, Y1, X2, Y2);

				// Shrink bounds by 1,1 to avoid GetComponentsInRegion picking up extra components on all sides due to the overlap between components
				CyLandInfo->GetComponentsInRegion(X1 + 1, Y1 + 1, X2 - 1, Y2 - 1, SelectedComponents);
			}

			// Delete the components
			EdMode->DeleteCyLandComponents(CyLandInfo, SelectedComponents);
		}
	}
};

class FCyLandToolDeleteComponent : public FCyLandToolBase<FCyLandToolStrokeDeleteComponent>
{
public:
	FCyLandToolDeleteComponent(FEdModeCyLand* InEdMode)
		: FCyLandToolBase<FCyLandToolStrokeDeleteComponent>(InEdMode)
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("DeleteComponent"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_DeleteComponent", "Delete CyLand Components"); };

	virtual void SetEditRenderType() override { GCyLandEditRenderMode = ECyLandEditRenderMode::SelectComponent | (GCyLandEditRenderMode & ECyLandEditRenderMode::BitMaskForMask); }
	virtual bool SupportsMask() override { return false; }
};

//
// FCyLandToolCopy
//
template<class ToolTarget>
class FCyLandToolStrokeCopy : public FCyLandToolStrokeBase
{
public:
	FCyLandToolStrokeCopy(FEdModeCyLand* InEdMode, FEditorViewportClient* InViewportClient, const FCyLandToolTarget& InTarget)
		: FCyLandToolStrokeBase(InEdMode, InViewportClient, InTarget)
		, Cache(InTarget)
		, HeightCache(InTarget)
		, WeightCache(InTarget)
	{
	}

	struct FGizmoPreData
	{
		float Ratio;
		float Data;
	};

	void Apply(FEditorViewportClient* ViewportClient, FCyLandBrush* Brush, const UCyLandEditorObject* UISettings, const TArray<FCyLandToolInteractorPosition>& InteractorPositions)
	{
		//UCyLandInfo* CyLandInfo = EdMode->CurrentToolTarget.CyLandInfo;
		ACyLandGizmoActiveActor* Gizmo = EdMode->CurrentGizmoActor.Get();
		if (CyLandInfo && Gizmo && Gizmo->GizmoTexture && Gizmo->GetRootComponent())
		{
			Gizmo->TargetCyLandInfo = CyLandInfo;

			// Get list of verts to update
			// TODO - only retrieve bounds as we don't need the data
			FCyLandBrushData BrushInfo = Brush->ApplyBrush(InteractorPositions);
			if (!BrushInfo)
			{
				return;
			}

			int32 X1, Y1, X2, Y2;
			BrushInfo.GetInclusiveBounds(X1, Y1, X2, Y2);

			//Gizmo->Modify(); // No transaction for Copied data as other tools...
			//Gizmo->SelectedData.Empty();
			Gizmo->ClearGizmoData();

			// Tablet pressure
			//float Pressure = ViewportClient->Viewport->IsPenActive() ? ViewportClient->Viewport->GetTabletPressure() : 1.0f;

			bool bApplyToAll = EdMode->UISettings->bApplyToAllTargets;
			const int32 LayerNum = CyLandInfo->Layers.Num();

			TArray<uint16> HeightData;
			TArray<uint8> WeightDatas; // Weight*Layers...
			TArray<typename ToolTarget::CacheClass::DataType> Data;

			TSet<UCyLandLayerInfoObject*> LayerInfoSet;

			if (bApplyToAll)
			{
				HeightCache.CacheData(X1, Y1, X2, Y2);
				HeightCache.GetCachedData(X1, Y1, X2, Y2, HeightData);

				WeightCache.CacheData(X1, Y1, X2, Y2);
				WeightCache.GetCachedData(X1, Y1, X2, Y2, WeightDatas, LayerNum);
			}
			else
			{
				Cache.CacheData(X1, Y1, X2, Y2);
				Cache.GetCachedData(X1, Y1, X2, Y2, Data);
			}

			const float ScaleXY = CyLandInfo->DrawScale.X;
			float Width = Gizmo->GetWidth();
			float Height = Gizmo->GetHeight();

			Gizmo->CachedWidth = Width;
			Gizmo->CachedHeight = Height;
			Gizmo->CachedScaleXY = ScaleXY;

			// Rasterize Gizmo regions
			int32 SizeX = FMath::CeilToInt(Width / ScaleXY);
			int32 SizeY = FMath::CeilToInt(Height / ScaleXY);

			const float W = (Width - ScaleXY) / (2 * ScaleXY);
			const float H = (Height - ScaleXY) / (2 * ScaleXY);

			FMatrix WToL = CyLandInfo->GetCyLandProxy()->CyLandActorToWorld().ToMatrixWithScale().InverseFast();
			//FMatrix LToW = CyLand->LocalToWorld();

			FVector BaseLocation = WToL.TransformPosition(Gizmo->GetActorLocation());
			FMatrix GizmoLocalToCyLand = FRotationTranslationMatrix(FRotator(0, Gizmo->GetActorRotation().Yaw, 0), FVector(BaseLocation.X, BaseLocation.Y, 0));

			const int32 NeighborNum = 4;
			bool bDidCopy = false;
			bool bFullCopy = !EdMode->UISettings->bUseSelectedRegion || !CyLandInfo->SelectedRegion.Num();
			//bool bInverted = EdMode->UISettings->bUseSelectedRegion && EdMode->UISettings->bUseNegativeMask;

			// TODO: This is a mess and badly needs refactoring
			for (int32 Y = 0; Y < SizeY; ++Y)
			{
				for (int32 X = 0; X < SizeX; ++X)
				{
					FVector CyLandLocal = GizmoLocalToCyLand.TransformPosition(FVector(-W + X, -H + Y, 0));
					int32 LX = FMath::FloorToInt(CyLandLocal.X);
					int32 LY = FMath::FloorToInt(CyLandLocal.Y);

					{
						for (int32 i = -1; (!bApplyToAll && i < 0) || i < LayerNum; ++i)
						{
							// Don't try to copy data for null layers
							if ((bApplyToAll && i >= 0 && !CyLandInfo->Layers[i].LayerInfoObj) ||
								(!bApplyToAll && !EdMode->CurrentToolTarget.LayerInfo.Get()))
							{
								continue;
							}

							FGizmoPreData GizmoPreData[NeighborNum];

							for (int32 LocalY = 0; LocalY < 2; ++LocalY)
							{
								for (int32 LocalX = 0; LocalX < 2; ++LocalX)
								{
									int32 x = FMath::Clamp(LX + LocalX, X1, X2);
									int32 y = FMath::Clamp(LY + LocalY, Y1, Y2);
									GizmoPreData[LocalX + LocalY * 2].Ratio = CyLandInfo->SelectedRegion.FindRef(FIntPoint(x, y));
									int32 index = (x - X1) + (y - Y1)*(1 + X2 - X1);

									if (bApplyToAll)
									{
										if (i < 0)
										{
											GizmoPreData[LocalX + LocalY * 2].Data = Gizmo->GetNormalizedHeight(HeightData[index]);
										}
										else
										{
											GizmoPreData[LocalX + LocalY * 2].Data = WeightDatas[index*LayerNum + i];
										}
									}
									else
									{
										typename ToolTarget::CacheClass::DataType OriginalValue = Data[index];
										if (EdMode->CurrentToolTarget.TargetType == ECyLandToolTargetType::Heightmap)
										{
											GizmoPreData[LocalX + LocalY * 2].Data = Gizmo->GetNormalizedHeight(OriginalValue);
										}
										else
										{
											GizmoPreData[LocalX + LocalY * 2].Data = OriginalValue;
										}
									}
								}
							}

							FGizmoPreData LerpedData;
							float FracX = CyLandLocal.X - LX;
							float FracY = CyLandLocal.Y - LY;
							LerpedData.Ratio = bFullCopy ? 1.0f :
								FMath::Lerp(
								FMath::Lerp(GizmoPreData[0].Ratio, GizmoPreData[1].Ratio, FracX),
								FMath::Lerp(GizmoPreData[2].Ratio, GizmoPreData[3].Ratio, FracX),
								FracY
								);

							LerpedData.Data = FMath::Lerp(
								FMath::Lerp(GizmoPreData[0].Data, GizmoPreData[1].Data, FracX),
								FMath::Lerp(GizmoPreData[2].Data, GizmoPreData[3].Data, FracX),
								FracY
								);

							if (!bDidCopy && LerpedData.Ratio > 0.0f)
							{
								bDidCopy = true;
							}

							if (LerpedData.Ratio > 0.0f)
							{
								// Added for LayerNames
								if (bApplyToAll)
								{
									if (i >= 0)
									{
										LayerInfoSet.Add(CyLandInfo->Layers[i].LayerInfoObj);
									}
								}
								else
								{
									if (EdMode->CurrentToolTarget.TargetType == ECyLandToolTargetType::Weightmap)
									{
										LayerInfoSet.Add(EdMode->CurrentToolTarget.LayerInfo.Get());
									}
								}

								FCyGizmoSelectData* GizmoSelectData = Gizmo->SelectedData.Find(FIntPoint(X, Y));
								if (GizmoSelectData)
								{
									if (bApplyToAll)
									{
										if (i < 0)
										{
											GizmoSelectData->HeightData = LerpedData.Data;
										}
										else
										{
											GizmoSelectData->WeightDataMap.Add(CyLandInfo->Layers[i].LayerInfoObj, LerpedData.Data);
										}
									}
									else
									{
										if (EdMode->CurrentToolTarget.TargetType == ECyLandToolTargetType::Heightmap)
										{
											GizmoSelectData->HeightData = LerpedData.Data;
										}
										else
										{
											GizmoSelectData->WeightDataMap.Add(EdMode->CurrentToolTarget.LayerInfo.Get(), LerpedData.Data);
										}
									}
								}
								else
								{
									FCyGizmoSelectData NewData;
									NewData.Ratio = LerpedData.Ratio;
									if (bApplyToAll)
									{
										if (i < 0)
										{
											NewData.HeightData = LerpedData.Data;
										}
										else
										{
											NewData.WeightDataMap.Add(CyLandInfo->Layers[i].LayerInfoObj, LerpedData.Data);
										}
									}
									else
									{
										if (EdMode->CurrentToolTarget.TargetType == ECyLandToolTargetType::Heightmap)
										{
											NewData.HeightData = LerpedData.Data;
										}
										else
										{
											NewData.WeightDataMap.Add(EdMode->CurrentToolTarget.LayerInfo.Get(), LerpedData.Data);
										}
									}
									Gizmo->SelectedData.Add(FIntPoint(X, Y), NewData);
								}
							}
						}
					}
				}
			}

			if (bDidCopy)
			{
				if (!bApplyToAll)
				{
					if (EdMode->CurrentToolTarget.TargetType == ECyLandToolTargetType::Heightmap)
					{
						Gizmo->DataType = ECyLandGizmoType(Gizmo->DataType | CyLGT_Height);
					}
					else
					{
						Gizmo->DataType = ECyLandGizmoType(Gizmo->DataType | CyLGT_Weight);
					}
				}
				else
				{
					if (LayerNum > 0)
					{
						Gizmo->DataType = ECyLandGizmoType(Gizmo->DataType | CyLGT_Height);
						Gizmo->DataType = ECyLandGizmoType(Gizmo->DataType | CyLGT_Weight);
					}
					else
					{
						Gizmo->DataType = ECyLandGizmoType(Gizmo->DataType | CyLGT_Height);
					}
				}

				Gizmo->SampleData(SizeX, SizeY);

				// Update LayerInfos
				for (UCyLandLayerInfoObject* LayerInfo : LayerInfoSet)
				{
					Gizmo->LayerInfos.Add(LayerInfo);
				}
			}

			//// Clean up Ratio 0 regions... (That was for sampling...)
			//for ( TMap<uint64, FCyGizmoSelectData>::TIterator It(Gizmo->SelectedData); It; ++It )
			//{
			//	FCyGizmoSelectData& Data = It.Value();
			//	if (Data.Ratio <= 0.0f)
			//	{
			//		Gizmo->SelectedData.Remove(It.Key());
			//	}
			//}

			Gizmo->ExportToClipboard();

			GEngine->BroadcastLevelActorListChanged();
		}
	}

protected:
	typename ToolTarget::CacheClass Cache;
	FCyLandHeightCache HeightCache;
	FCyLandFullWeightCache WeightCache;
};

template<class ToolTarget>
class FCyLandToolCopy : public FCyLandToolBase<FCyLandToolStrokeCopy<ToolTarget>>
{
public:
	FCyLandToolCopy(FEdModeCyLand* InEdMode)
		: FCyLandToolBase<FCyLandToolStrokeCopy<ToolTarget> >(InEdMode)
		, BackupCurrentBrush(nullptr)
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("Copy"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_Copy", "Copy"); };

	virtual void SetEditRenderType() override
	{
		GCyLandEditRenderMode = ECyLandEditRenderMode::Gizmo | (GCyLandEditRenderMode & ECyLandEditRenderMode::BitMaskForMask);
		GCyLandEditRenderMode |= (this->EdMode && this->EdMode->CurrentToolTarget.CyLandInfo.IsValid() && this->EdMode->CurrentToolTarget.CyLandInfo->SelectedRegion.Num()) ? ECyLandEditRenderMode::SelectRegion : ECyLandEditRenderMode::SelectComponent;
	}

	virtual ECyLandToolTargetTypeMask::Type GetSupportedTargetTypes() override
	{
		return ECyLandToolTargetTypeMask::FromType(ToolTarget::TargetType);
	}

	virtual bool BeginTool(FEditorViewportClient* ViewportClient, const FCyLandToolTarget& InTarget, const FVector& InHitLocation) override
	{
		this->EdMode->GizmoBrush->Tick(ViewportClient, 0.1f);

		// horrible hack
		// (but avoids duplicating the code from FCyLandToolBase)
		BackupCurrentBrush = this->EdMode->CurrentBrush;
		this->EdMode->CurrentBrush = this->EdMode->GizmoBrush;

		return FCyLandToolBase<FCyLandToolStrokeCopy<ToolTarget>>::BeginTool(ViewportClient, InTarget, InHitLocation);
	}

	virtual void EndTool(FEditorViewportClient* ViewportClient) override
	{
		FCyLandToolBase<FCyLandToolStrokeCopy<ToolTarget>>::EndTool(ViewportClient);

		this->EdMode->CurrentBrush = BackupCurrentBrush;
	}

protected:
	FCyLandBrush* BackupCurrentBrush;
};

//
// FCyLandToolPaste
//
template<class ToolTarget>
class FCyLandToolStrokePaste : public FCyLandToolStrokeBase
{
public:
	FCyLandToolStrokePaste(FEdModeCyLand* InEdMode, FEditorViewportClient* InViewportClient, const FCyLandToolTarget& InTarget)
		: FCyLandToolStrokeBase(InEdMode, InViewportClient, InTarget)
		, Cache(InTarget)
		, HeightCache(InTarget)
		, WeightCache(InTarget)
	{
	}

	void Apply(FEditorViewportClient* ViewportClient, FCyLandBrush* Brush, const UCyLandEditorObject* UISettings, const TArray<FCyLandToolInteractorPosition>& InteractorPositions)
	{
		//UCyLandInfo* CyLandInfo = EdMode->CurrentToolTarget.CyLandInfo;
		ACyLandGizmoActiveActor* Gizmo = EdMode->CurrentGizmoActor.Get();
		// Cache and copy in Gizmo's region...
		if (CyLandInfo && Gizmo && Gizmo->GetRootComponent())
		{
			if (Gizmo->SelectedData.Num() == 0)
			{
				return;
			}

			// Automatically fill in any placeholder layers
			// This gives a much better user experience when copying data to a newly created CyLand
			for (UCyLandLayerInfoObject* LayerInfo : Gizmo->LayerInfos)
			{
				int32 LayerInfoIndex = CyLandInfo->GetLayerInfoIndex(LayerInfo);
				if (LayerInfoIndex == INDEX_NONE)
				{
					LayerInfoIndex = CyLandInfo->GetLayerInfoIndex(LayerInfo->LayerName);
					if (LayerInfoIndex != INDEX_NONE)
					{
						FCyLandInfoLayerSettings& LayerSettings = CyLandInfo->Layers[LayerInfoIndex];

						if (LayerSettings.LayerInfoObj == nullptr)
						{
							LayerSettings.Owner = CyLandInfo->GetCyLandProxy(); // this isn't strictly accurate, but close enough
							LayerSettings.LayerInfoObj = LayerInfo;
							LayerSettings.bValid = true;
						}
					}
				}
			}

			Gizmo->TargetCyLandInfo = CyLandInfo;
			float ScaleXY = CyLandInfo->DrawScale.X;

			//CyLandInfo->Modify();

			// Get list of verts to update
			FCyLandBrushData BrushInfo = Brush->ApplyBrush(InteractorPositions);
			if (!BrushInfo)
			{
				return;
			}

			int32 X1, Y1, X2, Y2;
			BrushInfo.GetInclusiveBounds(X1, Y1, X2, Y2);

			// Tablet pressure
			float Pressure = (ViewportClient && ViewportClient->Viewport->IsPenActive()) ? ViewportClient->Viewport->GetTabletPressure() : 1.0f;

			// expand the area by one vertex in each direction to ensure normals are calculated correctly
			X1 -= 1;
			Y1 -= 1;
			X2 += 1;
			Y2 += 1;

			const bool bApplyToAll = EdMode->UISettings->bApplyToAllTargets;
			const int32 LayerNum = Gizmo->LayerInfos.Num() > 0 ? CyLandInfo->Layers.Num() : 0;

			TArray<uint16> HeightData;
			TArray<uint8> WeightDatas; // Weight*Layers...
			TArray<typename ToolTarget::CacheClass::DataType> Data;

			if (bApplyToAll)
			{
				HeightCache.CacheData(X1, Y1, X2, Y2);
				HeightCache.GetCachedData(X1, Y1, X2, Y2, HeightData);

				if (LayerNum > 0)
				{
					WeightCache.CacheData(X1, Y1, X2, Y2);
					WeightCache.GetCachedData(X1, Y1, X2, Y2, WeightDatas, LayerNum);
				}
			}
			else
			{
				Cache.CacheData(X1, Y1, X2, Y2);
				Cache.GetCachedData(X1, Y1, X2, Y2, Data);
			}

			const float Width = Gizmo->GetWidth();
			const float Height = Gizmo->GetHeight();

			const float W = Gizmo->GetWidth() / (2 * ScaleXY);
			const float H = Gizmo->GetHeight() / (2 * ScaleXY);

			const float SignX = Gizmo->GetRootComponent()->RelativeScale3D.X > 0.0f ? 1.0f : -1.0f;
			const float SignY = Gizmo->GetRootComponent()->RelativeScale3D.Y > 0.0f ? 1.0f : -1.0f;

			const float ScaleX = Gizmo->CachedWidth / Width * ScaleXY / Gizmo->CachedScaleXY;
			const float ScaleY = Gizmo->CachedHeight / Height * ScaleXY / Gizmo->CachedScaleXY;

			FMatrix WToL = CyLandInfo->GetCyLandProxy()->CyLandActorToWorld().ToMatrixWithScale().InverseFast();
			//FMatrix LToW = CyLand->LocalToWorld();
			FVector BaseLocation = WToL.TransformPosition(Gizmo->GetActorLocation());
			//FMatrix CyLandLocalToGizmo = FRotationTranslationMatrix(FRotator(0, Gizmo->Rotation.Yaw, 0), FVector(BaseLocation.X - W + 0.5, BaseLocation.Y - H + 0.5, 0));
			FMatrix CyLandToGizmoLocal =
				(FTranslationMatrix(FVector((-W + 0.5)*SignX, (-H + 0.5)*SignY, 0)) * FScaleRotationTranslationMatrix(FVector(SignX, SignY, 1.0f), FRotator(0, Gizmo->GetActorRotation().Yaw, 0), FVector(BaseLocation.X, BaseLocation.Y, 0))).InverseFast();

			for (int32 Y = BrushInfo.GetBounds().Min.Y; Y < BrushInfo.GetBounds().Max.Y; Y++)
			{
				const float* BrushScanline = BrushInfo.GetDataPtr(FIntPoint(0, Y));

				for (int32 X = BrushInfo.GetBounds().Min.X; X < BrushInfo.GetBounds().Max.X; X++)
				{
					const float BrushValue = BrushScanline[X];

					if (BrushValue > 0.0f)
					{
						// TODO: This is a mess and badly needs refactoring

						// Value before we apply our painting
						int32 index = (X - X1) + (Y - Y1)*(1 + X2 - X1);
						float PaintAmount = (Brush->GetBrushType() == ECyLandBrushType::Gizmo) ? BrushValue : BrushValue * EdMode->UISettings->ToolStrength * Pressure;

						FVector GizmoLocal = CyLandToGizmoLocal.TransformPosition(FVector(X, Y, 0));
						GizmoLocal.X *= ScaleX * SignX;
						GizmoLocal.Y *= ScaleY * SignY;

						int32 LX = FMath::FloorToInt(GizmoLocal.X);
						int32 LY = FMath::FloorToInt(GizmoLocal.Y);

						float FracX = GizmoLocal.X - LX;
						float FracY = GizmoLocal.Y - LY;

						FCyGizmoSelectData* Data00 = Gizmo->SelectedData.Find(FIntPoint(LX, LY));
						FCyGizmoSelectData* Data10 = Gizmo->SelectedData.Find(FIntPoint(LX + 1, LY));
						FCyGizmoSelectData* Data01 = Gizmo->SelectedData.Find(FIntPoint(LX, LY + 1));
						FCyGizmoSelectData* Data11 = Gizmo->SelectedData.Find(FIntPoint(LX + 1, LY + 1));

						for (int32 i = -1; (!bApplyToAll && i < 0) || i < LayerNum; ++i)
						{
							if ((bApplyToAll && (i < 0)) || (!bApplyToAll && EdMode->CurrentToolTarget.TargetType == ECyLandToolTargetType::Heightmap))
							{
								float OriginalValue;
								if (bApplyToAll)
								{
									OriginalValue = HeightData[index];
								}
								else
								{
									OriginalValue = Data[index];
								}

								float Value = CyLandDataAccess::GetLocalHeight(OriginalValue);

								float DestValue = FCyLandHeightCache::ClampValue(
									CyLandDataAccess::GetTexHeight(
									FMath::Lerp(
									FMath::Lerp(Data00 ? FMath::Lerp(Value, Gizmo->GetCyLandHeight(Data00->HeightData), Data00->Ratio) : Value,
									Data10 ? FMath::Lerp(Value, Gizmo->GetCyLandHeight(Data10->HeightData), Data10->Ratio) : Value, FracX),
									FMath::Lerp(Data01 ? FMath::Lerp(Value, Gizmo->GetCyLandHeight(Data01->HeightData), Data01->Ratio) : Value,
									Data11 ? FMath::Lerp(Value, Gizmo->GetCyLandHeight(Data11->HeightData), Data11->Ratio) : Value, FracX),
									FracY
									))
									);

								switch (EdMode->UISettings->PasteMode)
								{
								case ECyLandToolPasteMode::Raise:
									PaintAmount = OriginalValue < DestValue ? PaintAmount : 0.0f;
									break;
								case ECyLandToolPasteMode::Lower:
									PaintAmount = OriginalValue > DestValue ? PaintAmount : 0.0f;
									break;
								default:
									break;
								}

								if (bApplyToAll)
								{
									HeightData[index] = FMath::Lerp(OriginalValue, DestValue, PaintAmount);
								}
								else
								{
									Data[index] = FMath::Lerp(OriginalValue, DestValue, PaintAmount);
								}
							}
							else
							{
								UCyLandLayerInfoObject* LayerInfo;
								float OriginalValue;
								if (bApplyToAll)
								{
									LayerInfo = CyLandInfo->Layers[i].LayerInfoObj;
									OriginalValue = WeightDatas[index*LayerNum + i];
								}
								else
								{
									LayerInfo = EdMode->CurrentToolTarget.LayerInfo.Get();
									OriginalValue = Data[index];
								}

								float DestValue = FCyLandAlphaCache::ClampValue(
									FMath::Lerp(
									FMath::Lerp(Data00 ? FMath::Lerp(OriginalValue, Data00->WeightDataMap.FindRef(LayerInfo), Data00->Ratio) : OriginalValue,
									Data10 ? FMath::Lerp(OriginalValue, Data10->WeightDataMap.FindRef(LayerInfo), Data10->Ratio) : OriginalValue, FracX),
									FMath::Lerp(Data01 ? FMath::Lerp(OriginalValue, Data01->WeightDataMap.FindRef(LayerInfo), Data01->Ratio) : OriginalValue,
									Data11 ? FMath::Lerp(OriginalValue, Data11->WeightDataMap.FindRef(LayerInfo), Data11->Ratio) : OriginalValue, FracX),
									FracY
									));

								if (bApplyToAll)
								{
									WeightDatas[index*LayerNum + i] = FMath::Lerp(OriginalValue, DestValue, PaintAmount);
								}
								else
								{
									Data[index] = FMath::Lerp(OriginalValue, DestValue, PaintAmount);
								}
							}
						}
					}
				}
			}

			for (UCyLandLayerInfoObject* LayerInfo : Gizmo->LayerInfos)
			{
				if (CyLandInfo->GetLayerInfoIndex(LayerInfo) != INDEX_NONE)
				{
					WeightCache.AddDirtyLayer(LayerInfo);
				}
			}

			ACyLand* CyLand = CyLandInfo->CyLandActor.Get();

			if (CyLand != nullptr && CyLand->HasProceduralContent && !GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
			{
				FMessageLog("MapCheck").Warning()->AddToken(FTextToken::Create(LOCTEXT("CyLandProcedural_ChangingDataWithoutSettings", "This map contains CyLand procedural content, modifying the CyLand data will result in data loss when the map is reopened with CyLand Procedural settings on. Please enable CyLand Procedural settings before modifying the data.")));
				FMessageLog("MapCheck").Open(EMessageSeverity::Warning);
			}

			if (bApplyToAll)
			{
				HeightCache.SetCachedData(X1, Y1, X2, Y2, HeightData);
				HeightCache.Flush();
				if (WeightDatas.Num())
				{
					// Set the layer data, bypassing painting restrictions because it doesn't work well when altering multiple layers
					WeightCache.SetCachedData(X1, Y1, X2, Y2, WeightDatas, LayerNum, ECyLandLayerPaintingRestriction::None);
				}
				WeightCache.Flush();
			}
			else
			{
				Cache.SetCachedData(X1, Y1, X2, Y2, Data);
				Cache.Flush();
			}

			GEngine->BroadcastLevelActorListChanged();
		}
	}

protected:
	typename ToolTarget::CacheClass Cache;
	FCyLandHeightCache HeightCache;
	FCyLandFullWeightCache WeightCache;
};

template<class ToolTarget>
class FCyLandToolPaste : public FCyLandToolBase<FCyLandToolStrokePaste<ToolTarget>>
{
public:
	FCyLandToolPaste(FEdModeCyLand* InEdMode)
		: FCyLandToolBase<FCyLandToolStrokePaste<ToolTarget>>(InEdMode)
		, bUseGizmoRegion(false)
		, BackupCurrentBrush(nullptr)
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("Paste"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_Region", "Region Copy/Paste"); };

	virtual void SetEditRenderType() override
	{
		GCyLandEditRenderMode = ECyLandEditRenderMode::Gizmo | (GCyLandEditRenderMode & ECyLandEditRenderMode::BitMaskForMask);
		GCyLandEditRenderMode |= (this->EdMode && this->EdMode->CurrentToolTarget.CyLandInfo.IsValid() && this->EdMode->CurrentToolTarget.CyLandInfo->SelectedRegion.Num()) ? ECyLandEditRenderMode::SelectRegion : ECyLandEditRenderMode::SelectComponent;
	}

	void SetGizmoMode(bool InbUseGizmoRegion)
	{
		bUseGizmoRegion = InbUseGizmoRegion;
	}

	virtual ECyLandToolTargetTypeMask::Type GetSupportedTargetTypes() override
	{
		return ECyLandToolTargetTypeMask::FromType(ToolTarget::TargetType);
	}

	virtual bool BeginTool(FEditorViewportClient* ViewportClient, const FCyLandToolTarget& InTarget, const FVector& InHitLocation) override
	{
		this->EdMode->GizmoBrush->Tick(ViewportClient, 0.1f);

		// horrible hack
		// (but avoids duplicating the code from FCyLandToolBase)
		BackupCurrentBrush = this->EdMode->CurrentBrush;
		if (bUseGizmoRegion)
		{
			this->EdMode->CurrentBrush = this->EdMode->GizmoBrush;
		}

		return FCyLandToolBase<FCyLandToolStrokePaste<ToolTarget>>::BeginTool(ViewportClient, InTarget, InHitLocation);
	}

	virtual void EndTool(FEditorViewportClient* ViewportClient) override
	{
		FCyLandToolBase<FCyLandToolStrokePaste<ToolTarget>>::EndTool(ViewportClient);

		if (bUseGizmoRegion)
		{
			this->EdMode->CurrentBrush = BackupCurrentBrush;
		}
		check(this->EdMode->CurrentBrush == BackupCurrentBrush);
	}

	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y) override
	{
		if (bUseGizmoRegion)
		{
			return true;
		}

		return FCyLandToolBase<FCyLandToolStrokePaste<ToolTarget>>::MouseMove(ViewportClient, Viewport, x, y);
	}

protected:
	bool bUseGizmoRegion;
	FCyLandBrush* BackupCurrentBrush;
};

//
// FCyLandToolCopyPaste
//
template<class ToolTarget>
class FCyLandToolCopyPaste : public FCyLandToolPaste<ToolTarget>
{
public:
	FCyLandToolCopyPaste(FEdModeCyLand* InEdMode)
		: FCyLandToolPaste<ToolTarget>(InEdMode)
		, CopyTool(InEdMode)
	{
	}

	// Just hybrid of Copy and Paste tool
	virtual const TCHAR* GetToolName() override { return TEXT("CopyPaste"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_Region", "Region Copy/Paste"); };

	virtual void EnterTool() override
	{
		// Make sure gizmo actor is selected
		ACyLandGizmoActiveActor* Gizmo = this->EdMode->CurrentGizmoActor.Get();
		if (Gizmo)
		{
			GEditor->SelectNone(false, true);
			GEditor->SelectActor(Gizmo, true, false, true);
		}
	}

	// Copy tool doesn't use any view information, so just do it as one function
	void Copy()
	{
		CopyTool.BeginTool(nullptr, this->EdMode->CurrentToolTarget, FVector::ZeroVector);
		CopyTool.EndTool(nullptr);
	}

	void Paste()
	{
		this->SetGizmoMode(true);
		this->BeginTool(nullptr, this->EdMode->CurrentToolTarget, FVector::ZeroVector);
		this->EndTool(nullptr);
		this->SetGizmoMode(false);
	}

protected:
	FCyLandToolCopy<ToolTarget> CopyTool;
};

void FEdModeCyLand::CopyDataToGizmo()
{
	// For Copy operation...
	if (CopyPasteTool /*&& CopyPasteTool == CurrentTool*/)
	{
		CopyPasteTool->Copy();
	}
	if (CurrentGizmoActor.IsValid())
	{
		GEditor->SelectNone(false, true);
		GEditor->SelectActor(CurrentGizmoActor.Get(), true, true, true);
	}
}

void FEdModeCyLand::PasteDataFromGizmo()
{
	// For Paste for Gizmo Region operation...
	if (CopyPasteTool /*&& CopyPasteTool == CurrentTool*/)
	{
		CopyPasteTool->Paste();
	}
	if (CurrentGizmoActor.IsValid())
	{
		GEditor->SelectNone(false, true);
		GEditor->SelectActor(CurrentGizmoActor.Get(), true, true, true);
	}
}

//
// FCyLandToolNewCyLand
//
class FCyLandToolNewCyLand : public FCyLandTool
{
public:
	FEdModeCyLand* EdMode;
	ENewCyLandPreviewMode::Type NewCyLandPreviewMode;

	FCyLandToolNewCyLand(FEdModeCyLand* InEdMode)
		: FCyLandTool()
		, EdMode(InEdMode)
		, NewCyLandPreviewMode(ENewCyLandPreviewMode::NewCyLand)
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("NewCyLand"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_NewCyLand", "New CyLand"); };

	virtual void SetEditRenderType() override { GCyLandEditRenderMode = ECyLandEditRenderMode::None | (GCyLandEditRenderMode & ECyLandEditRenderMode::BitMaskForMask); }
	virtual bool SupportsMask() override { return false; }

	virtual void EnterTool() override
	{
		EdMode->NewCyLandPreviewMode = NewCyLandPreviewMode;
		EdMode->UISettings->ImportCyLandData();
	}

	virtual void ExitTool() override
	{
		NewCyLandPreviewMode = EdMode->NewCyLandPreviewMode;
		EdMode->NewCyLandPreviewMode = ENewCyLandPreviewMode::None;
		EdMode->UISettings->ClearImportCyLandData();
	}

	virtual bool BeginTool(FEditorViewportClient* ViewportClient, const FCyLandToolTarget& Target, const FVector& InHitLocation) override
	{
		// does nothing
		return false;
	}

	virtual void EndTool(FEditorViewportClient* ViewportClient) override
	{
		// does nothing
	}

	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y) override
	{
		// does nothing
		return false;
	}
};


//
// FCyLandToolResizeCyLand
//
class FCyLandToolResizeCyLand : public FCyLandTool
{
public:
	FEdModeCyLand* EdMode;

	FCyLandToolResizeCyLand(FEdModeCyLand* InEdMode)
		: FCyLandTool()
		, EdMode(InEdMode)
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("ResizeCyLand"); }
	virtual FText GetDisplayName() override { return LOCTEXT("CyLandMode_ResizeCyLand", "Change CyLand Component Size"); };

	virtual void SetEditRenderType() override { GCyLandEditRenderMode = ECyLandEditRenderMode::None | (GCyLandEditRenderMode & ECyLandEditRenderMode::BitMaskForMask); }
	virtual bool SupportsMask() override { return false; }

	virtual void EnterTool() override
	{
		const int32 ComponentSizeQuads = EdMode->CurrentToolTarget.CyLandInfo->ComponentSizeQuads;
		int32 MinX, MinY, MaxX, MaxY;
		if (EdMode->CurrentToolTarget.CyLandInfo->GetCyLandExtent(MinX, MinY, MaxX, MaxY))
		{
			EdMode->UISettings->ResizeCyLand_Original_ComponentCount.X = (MaxX - MinX) / ComponentSizeQuads;
			EdMode->UISettings->ResizeCyLand_Original_ComponentCount.Y = (MaxY - MinY) / ComponentSizeQuads;
			EdMode->UISettings->ResizeCyLand_ComponentCount = EdMode->UISettings->ResizeCyLand_Original_ComponentCount;
		}
		else
		{
			EdMode->UISettings->ResizeCyLand_Original_ComponentCount = FIntPoint::ZeroValue;
			EdMode->UISettings->ResizeCyLand_ComponentCount = FIntPoint::ZeroValue;
		}
		EdMode->UISettings->ResizeCyLand_Original_QuadsPerSection = EdMode->CurrentToolTarget.CyLandInfo->SubsectionSizeQuads;
		EdMode->UISettings->ResizeCyLand_Original_SectionsPerComponent = EdMode->CurrentToolTarget.CyLandInfo->ComponentNumSubsections;
		EdMode->UISettings->ResizeCyLand_QuadsPerSection = EdMode->UISettings->ResizeCyLand_Original_QuadsPerSection;
		EdMode->UISettings->ResizeCyLand_SectionsPerComponent = EdMode->UISettings->ResizeCyLand_Original_SectionsPerComponent;
	}

	virtual void ExitTool() override
	{
	}

	virtual bool BeginTool(FEditorViewportClient* ViewportClient, const FCyLandToolTarget& Target, const FVector& InHitLocation) override
	{
		// does nothing
		return false;
	}

	virtual void EndTool(FEditorViewportClient* ViewportClient) override
	{
		// does nothing
	}

	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y) override
	{
		// does nothing
		return false;
	}
};

//////////////////////////////////////////////////////////////////////////

void FEdModeCyLand::InitializeTool_NewCyLand()
{
	auto Tool_NewCyLand = MakeUnique<FCyLandToolNewCyLand>(this);
	Tool_NewCyLand->ValidBrushes.Add("BrushSet_Dummy");
	CyLandTools.Add(MoveTemp(Tool_NewCyLand));
}

void FEdModeCyLand::InitializeTool_ResizeCyLand()
{
	auto Tool_ResizeCyLand = MakeUnique<FCyLandToolResizeCyLand>(this);
	Tool_ResizeCyLand->ValidBrushes.Add("BrushSet_Dummy");
	CyLandTools.Add(MoveTemp(Tool_ResizeCyLand));
}

void FEdModeCyLand::InitializeTool_Select()
{
	auto Tool_Select = MakeUnique<FCyLandToolSelect>(this);
	Tool_Select->ValidBrushes.Add("BrushSet_Component");
	CyLandTools.Add(MoveTemp(Tool_Select));
}

void FEdModeCyLand::InitializeTool_AddComponent()
{
	auto Tool_AddComponent = MakeUnique<FCyLandToolAddComponent>(this);
	Tool_AddComponent->ValidBrushes.Add("BrushSet_Component");
	CyLandTools.Add(MoveTemp(Tool_AddComponent));
}

void FEdModeCyLand::InitializeTool_DeleteComponent()
{
	auto Tool_DeleteComponent = MakeUnique<FCyLandToolDeleteComponent>(this);
	Tool_DeleteComponent->ValidBrushes.Add("BrushSet_Component");
	CyLandTools.Add(MoveTemp(Tool_DeleteComponent));
}

void FEdModeCyLand::InitializeTool_MoveToLevel()
{
	auto Tool_MoveToLevel = MakeUnique<FCyLandToolMoveToLevel>(this);
	Tool_MoveToLevel->ValidBrushes.Add("BrushSet_Component");
	CyLandTools.Add(MoveTemp(Tool_MoveToLevel));
}

void FEdModeCyLand::InitializeTool_Mask()
{
	auto Tool_Mask = MakeUnique<FCyLandToolMask>(this);
	Tool_Mask->ValidBrushes.Add("BrushSet_Circle");
	Tool_Mask->ValidBrushes.Add("BrushSet_Alpha");
	Tool_Mask->ValidBrushes.Add("BrushSet_Pattern");
	CyLandTools.Add(MoveTemp(Tool_Mask));
}

void FEdModeCyLand::InitializeTool_CopyPaste()
{
	auto Tool_CopyPaste_Heightmap = MakeUnique<FCyLandToolCopyPaste<FHeightmapToolTarget>>(this);
	Tool_CopyPaste_Heightmap->ValidBrushes.Add("BrushSet_Circle");
	Tool_CopyPaste_Heightmap->ValidBrushes.Add("BrushSet_Alpha");
	Tool_CopyPaste_Heightmap->ValidBrushes.Add("BrushSet_Pattern");
	Tool_CopyPaste_Heightmap->ValidBrushes.Add("BrushSet_Gizmo");
	CopyPasteTool = Tool_CopyPaste_Heightmap.Get();
	CyLandTools.Add(MoveTemp(Tool_CopyPaste_Heightmap));

	//auto Tool_CopyPaste_Weightmap = MakeUnique<FCyLandToolCopyPaste<FWeightmapToolTarget>>(this);
	//Tool_CopyPaste_Weightmap->ValidBrushes.Add("BrushSet_Circle");
	//Tool_CopyPaste_Weightmap->ValidBrushes.Add("BrushSet_Alpha");
	//Tool_CopyPaste_Weightmap->ValidBrushes.Add("BrushSet_Pattern");
	//Tool_CopyPaste_Weightmap->ValidBrushes.Add("BrushSet_Gizmo");
	//CyLandTools.Add(MoveTemp(Tool_CopyPaste_Weightmap));
}

void FEdModeCyLand::InitializeTool_Visibility()
{
	auto Tool_Visibility = MakeUnique<FCyLandToolVisibility>(this);
	Tool_Visibility->ValidBrushes.Add("BrushSet_Circle");
	Tool_Visibility->ValidBrushes.Add("BrushSet_Alpha");
	Tool_Visibility->ValidBrushes.Add("BrushSet_Pattern");
	CyLandTools.Add(MoveTemp(Tool_Visibility));
}

#undef LOCTEXT_NAMESPACE
