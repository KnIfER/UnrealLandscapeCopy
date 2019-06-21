// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CyLandEdMode.h"
#include "SceneView.h"
#include "Engine/Texture2D.h"
#include "EditorViewportClient.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "Engine/Light.h"
#include "Engine/Selection.h"
#include "EditorModeManager.h"
#include "CyLandFileFormatInterface.h"
#include "CyLandEditorModule.h"
#include "CyLandEditorObject.h"
#include "CyLand.h"
#include "CyLandStreamingProxy.h"

#include "EditorSupportDelegates.h"
#include "ScopedTransaction.h"
#include "CyLandEdit.h"
#include "CyLandEditorUtils.h"
#include "CyLandRender.h"
#include "CyLandDataAccess.h"
#include "Framework/Commands/UICommandList.h"
#include "LevelEditor.h"
#include "Toolkits/ToolkitManager.h"
#include "CyLandHeightfieldCollisionComponent.h"
#include "InstancedFoliageActor.h"
#include "EditorWorldExtension.h"
#include "ViewportWorldInteraction.h"
#include "VREditorInteractor.h"
#include "CyLandEdModeTools.h"
#include "CyLandInfoMap.h"

//Slate dependencies
#include "Misc/FeedbackContext.h"
#include "ILevelViewport.h"
#include "SCyLandEditor.h"
#include "Framework/Application/SlateApplication.h"

// VR Editor
#include "VREditorMode.h"

// Classes
#include "CyLandMaterialInstanceConstant.h"
#include "CyLandSplinesComponent.h"
#include "ComponentReregisterContext.h"
#include "EngineUtils.h"
#include "IVREditorModule.h"
#include "Misc/ScopedSlowTask.h"
#include "CyLandEditorCommands.h"
#include "Framework/Commands/InputBindingManager.h"
#include "MouseDeltaTracker.h"
#include "Interfaces/IMainFrameModule.h"
#include "CyLandBPCustomBrush.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Settings/EditorExperimentalSettings.h"

#define LOCTEXT_NAMESPACE "CyLand"

DEFINE_LOG_CATEGORY(LogCyLandEdMode);

struct HNewCyLandGrabHandleProxy : public HHitProxy
{
	DECLARE_HIT_PROXY();

	ECyLandEdge::Type Edge;

	HNewCyLandGrabHandleProxy(ECyLandEdge::Type InEdge) :
		HHitProxy(HPP_Wireframe),
		Edge(InEdge)
	{
	}

	virtual EMouseCursor::Type GetMouseCursor() override
	{
		switch (Edge)
		{
		case ECyLandEdge::X_Negative:
		case ECyLandEdge::X_Positive:
			return EMouseCursor::ResizeLeftRight;
		case ECyLandEdge::Y_Negative:
		case ECyLandEdge::Y_Positive:
			return EMouseCursor::ResizeUpDown;
		case ECyLandEdge::X_Negative_Y_Negative:
		case ECyLandEdge::X_Positive_Y_Positive:
			return EMouseCursor::ResizeSouthEast;
		case ECyLandEdge::X_Negative_Y_Positive:
		case ECyLandEdge::X_Positive_Y_Negative:
			return EMouseCursor::ResizeSouthWest;
		}

		return EMouseCursor::SlashedCircle;
	}
};

IMPLEMENT_HIT_PROXY(HNewCyLandGrabHandleProxy, HHitProxy)


void ACyLand::SplitHeightmap(UCyLandComponent* Comp, bool bMoveToCurrentLevel /*= false*/)
{
	UCyLandInfo* Info = Comp->GetCyLandInfo();
	ACyLand* CyLand = Info->CyLandActor.Get();
	int32 ComponentSizeVerts = Comp->NumSubsections * (Comp->SubsectionSizeQuads + 1);
	// make sure the heightmap UVs are powers of two.
	int32 HeightmapSizeU = (1 << FMath::CeilLogTwo(ComponentSizeVerts));
	int32 HeightmapSizeV = (1 << FMath::CeilLogTwo(ComponentSizeVerts));

	UTexture2D* HeightmapTexture = NULL;
	TArray<FColor*> HeightmapTextureMipData;
	// Scope for FCyLandEditDataInterface
	{
		// Read old data and split
		FCyLandEditDataInterface CyLandEdit(Info);
		TArray<uint8> HeightData;
		HeightData.AddZeroed((1 + Comp->ComponentSizeQuads)*(1 + Comp->ComponentSizeQuads)*sizeof(uint16));
		// Because of edge problem, normal would be just copy from old component data
		TArray<uint8> NormalData;
		NormalData.AddZeroed((1 + Comp->ComponentSizeQuads)*(1 + Comp->ComponentSizeQuads)*sizeof(uint16));
		CyLandEdit.GetHeightDataFast(Comp->GetSectionBase().X, Comp->GetSectionBase().Y, Comp->GetSectionBase().X + Comp->ComponentSizeQuads, Comp->GetSectionBase().Y + Comp->ComponentSizeQuads, (uint16*)HeightData.GetData(), 0, (uint16*)NormalData.GetData());

		// Construct the heightmap textures
		UObject* TextureOuter = bMoveToCurrentLevel ? Comp->GetWorld()->GetCurrentLevel()->GetOutermost() : nullptr;
		HeightmapTexture = Comp->GetCyLandProxy()->CreateCyLandTexture(HeightmapSizeU, HeightmapSizeV, TEXTUREGROUP_Terrain_Heightmap, TSF_BGRA8, TextureOuter);

		int32 MipSubsectionSizeQuads = Comp->SubsectionSizeQuads;
		int32 MipSizeU = HeightmapSizeU;
		int32 MipSizeV = HeightmapSizeV;
		while (MipSizeU > 1 && MipSizeV > 1 && MipSubsectionSizeQuads >= 1)
		{
			FColor* HeightmapTextureData = (FColor*)HeightmapTexture->Source.LockMip(HeightmapTextureMipData.Num());
			FMemory::Memzero(HeightmapTextureData, MipSizeU*MipSizeV*sizeof(FColor));
			HeightmapTextureMipData.Add(HeightmapTextureData);

			MipSizeU >>= 1;
			MipSizeV >>= 1;

			MipSubsectionSizeQuads = ((MipSubsectionSizeQuads + 1) >> 1) - 1;
		}

		Comp->HeightmapScaleBias = FVector4(1.0f / (float)HeightmapSizeU, 1.0f / (float)HeightmapSizeV, 0.0f, 0.0f);

#if WITH_EDITORONLY_DATA
		// TODO: There is a bug here with updating the mip regions
		/*if (GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
		{
			// Add component to new heightmap
			FRenderDataPerHeightmap* RenderData = CyLand->RenderDataPerHeightmap.Find(HeightmapTexture);

			if (RenderData == nullptr)
			{
				FRenderDataPerHeightmap NewData;
				NewData.Components.Add(Comp);
				NewData.TopLeftSectionBase = Comp->GetSectionBase();
				NewData.HeightmapKey = HeightmapTexture;
				NewData.HeightmapsCPUReadBack = new FCyLandProceduralTexture2DCPUReadBackResource(HeightmapTexture->Source.GetSizeX(), HeightmapTexture->Source.GetSizeY(), Comp->GetHeightmap(false)->GetPixelFormat(), HeightmapTexture->Source.GetNumMips());
				BeginInitResource(NewData.HeightmapsCPUReadBack);

				CyLand->RenderDataPerHeightmap.Add(HeightmapTexture, NewData);
			}
			else
			{
				for (UCyLandComponent* Component : RenderData->Components)
				{
					RenderData->TopLeftSectionBase.X = FMath::Min(RenderData->TopLeftSectionBase.X, Component->GetSectionBase().X);
					RenderData->TopLeftSectionBase.Y = FMath::Min(RenderData->TopLeftSectionBase.Y, Component->GetSectionBase().Y);
				}

				RenderData->Components.Add(Comp);
			}

			// Remove component from old heightmap
			FRenderDataPerHeightmap* OldRenderData = CyLand->RenderDataPerHeightmap.Find(Comp->GetHeightmap(false));
			OldRenderData->Components.Remove(Comp);

			bool RemoveLayerHeightmap = false;

			// No component use this heightmap anymore, so clean up
			if (OldRenderData->Components.Num() == 0)
			{
				ReleaseResourceAndFlush(OldRenderData->HeightmapsCPUReadBack);
				delete OldRenderData->HeightmapsCPUReadBack;
				OldRenderData->HeightmapsCPUReadBack = nullptr;
				CyLand->RenderDataPerHeightmap.Remove(Comp->GetHeightmap(false));
				RemoveLayerHeightmap = true;
			}

			// Move layer content to new layer heightmap
			for (FCyLandLayer& Layer : CyLand->CyLandLayers)
			{
				UTexture2D** OldLayerHeightmap = Layer.HeightmapTarget.Heightmaps.Find(Comp->GetHeightmap(false));

				if (OldLayerHeightmap != nullptr)
				{
					// If we remove the main heightmap, remove the layers one, as they exist on a 1 to 1
					if (RemoveLayerHeightmap)
					{
						Layer.HeightmapTarget.Heightmaps.Remove(*OldLayerHeightmap);
					}

					// Read old data and split
					TArray<uint8> LayerHeightData;
					LayerHeightData.AddZeroed((1 + Comp->ComponentSizeQuads)*(1 + Comp->ComponentSizeQuads) * sizeof(uint16));
					// Because of edge problem, normal would be just copy from old component data
					TArray<uint8> LayerNormalData;
					LayerNormalData.AddZeroed((1 + Comp->ComponentSizeQuads)*(1 + Comp->ComponentSizeQuads) * sizeof(uint16));
					CyLandEdit.GetHeightDataFast(Comp->GetSectionBase().X, Comp->GetSectionBase().Y, Comp->GetSectionBase().X + Comp->ComponentSizeQuads, Comp->GetSectionBase().Y + Comp->ComponentSizeQuads, (uint16*)LayerHeightData.GetData(), 0, (uint16*)LayerNormalData.GetData(), *OldLayerHeightmap);

					UTexture2D** NewLayerHeightmap = Layer.HeightmapTarget.Heightmaps.Find(HeightmapTexture);

					if (NewLayerHeightmap == nullptr)
					{
						UTexture2D* LayerHeightmapTexture = Comp->GetCyLandProxy()->CreateCyLandTexture(HeightmapSizeU, HeightmapSizeV, TEXTUREGROUP_Terrain_Heightmap, TSF_BGRA8, bMoveToCurrentLevel ? Comp->GetWorld()->GetCurrentLevel()->GetOutermost() : nullptr);

						MipSubsectionSizeQuads = Comp->SubsectionSizeQuads;
						MipSizeU = HeightmapSizeU;
						MipSizeV = HeightmapSizeV;
						int32 MipIndex = 0;

						while (MipSizeU > 1 && MipSizeV > 1 && MipSubsectionSizeQuads >= 1)
						{
							FColor* HeightmapTextureData = (FColor*)LayerHeightmapTexture->Source.LockMip(MipIndex);
							FMemory::Memzero(HeightmapTextureData, MipSizeU*MipSizeV * sizeof(FColor));
							LayerHeightmapTexture->Source.UnlockMip(MipIndex);

							MipSizeU >>= 1;
							MipSizeV >>= 1;

							MipSubsectionSizeQuads = ((MipSubsectionSizeQuads + 1) >> 1) - 1;
							++MipIndex;
						}

						Layer.HeightmapTarget.Heightmaps.Add(HeightmapTexture, LayerHeightmapTexture);

						CyLandEdit.SetHeightData(Comp->GetSectionBase().X, Comp->GetSectionBase().Y, Comp->GetSectionBase().X + Comp->ComponentSizeQuads, Comp->GetSectionBase().Y + Comp->ComponentSizeQuads, (uint16*)LayerHeightData.GetData(), 0, false, (uint16*)LayerNormalData.GetData(),
												    false, LayerHeightmapTexture, nullptr, true, true, true);
					}
					else
					{
						CyLandEdit.SetHeightData(Comp->GetSectionBase().X, Comp->GetSectionBase().Y, Comp->GetSectionBase().X + Comp->ComponentSizeQuads, Comp->GetSectionBase().Y + Comp->ComponentSizeQuads, (uint16*)LayerHeightData.GetData(), 0, false, (uint16*)LayerNormalData.GetData(),
												   false, *NewLayerHeightmap, nullptr, true, true, true);
					}
				}
			}
		}
		*/
#endif

		Comp->SetHeightmap(HeightmapTexture);
		Comp->UpdateMaterialInstances();

		for (int32 i = 0; i < HeightmapTextureMipData.Num(); i++)
		{
			HeightmapTexture->Source.UnlockMip(i);
		}
		CyLandEdit.SetHeightData(Comp->GetSectionBase().X, Comp->GetSectionBase().Y, Comp->GetSectionBase().X + Comp->ComponentSizeQuads, Comp->GetSectionBase().Y + Comp->ComponentSizeQuads, (uint16*)HeightData.GetData(), 0, false, (uint16*)NormalData.GetData());
	}

	// End of CyLandEdit interface
	HeightmapTexture->PostEditChange();
	// Reregister
	FComponentReregisterContext ReregisterContext(Comp);
}



void FCyLandTool::SetEditRenderType()
{
	GCyLandEditRenderMode = ECyLandEditRenderMode::SelectRegion | (GCyLandEditRenderMode & ECyLandEditRenderMode::BitMaskForMask);
}

namespace CyLandTool
{
	UMaterialInstance* CreateMaterialInstance(UMaterialInterface* BaseMaterial)
	{
		UCyLandMaterialInstanceConstant* MaterialInstance = NewObject<UCyLandMaterialInstanceConstant>(GetTransientPackage());
		MaterialInstance->bEditorToolUsage = true;
		MaterialInstance->SetParentEditorOnly(BaseMaterial);
		MaterialInstance->PostEditChange();
		return MaterialInstance;
	}
}

//
// FEdModeCyLand
//

/** Constructor */
FEdModeCyLand::FEdModeCyLand()
	: FEdMode()
	, NewCyLandPreviewMode(ENewCyLandPreviewMode::None)
	, DraggingEdge(ECyLandEdge::None)
	, DraggingEdge_Remainder(0)
	, CurrentGizmoActor(nullptr)
	, CopyPasteTool(nullptr)
	, SplinesTool(nullptr)
	, CyLandRenderAddCollision(nullptr)
	, CachedCyLandMaterial(nullptr)
	, ToolActiveViewport(nullptr)
	, bIsPaintingInVR(false)
	, InteractorPainting( nullptr )
{
	GLayerDebugColorMaterial = CyLandTool::CreateMaterialInstance(LoadObject<UMaterial>(nullptr, TEXT("/Engine/EditorLandscapeResources/LayerVisMaterial.LayerVisMaterial")));
	GSelectionColorMaterial  = CyLandTool::CreateMaterialInstance(LoadObject<UMaterialInstanceConstant>(nullptr, TEXT("/Engine/EditorLandscapeResources/SelectBrushMaterial_Selected.SelectBrushMaterial_Selected")));
	GSelectionRegionMaterial = CyLandTool::CreateMaterialInstance(LoadObject<UMaterialInstanceConstant>(nullptr, TEXT("/Engine/EditorLandscapeResources/SelectBrushMaterial_SelectedRegion.SelectBrushMaterial_SelectedRegion")));
	GMaskRegionMaterial      = CyLandTool::CreateMaterialInstance(LoadObject<UMaterialInstanceConstant>(nullptr, TEXT("/Engine/EditorLandscapeResources/MaskBrushMaterial_MaskedRegion.MaskBrushMaterial_MaskedRegion")));
	GCyLandBlackTexture   = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/Black.Black"));
	GCyLandLayerUsageMaterial = CyLandTool::CreateMaterialInstance(LoadObject<UMaterial>(nullptr, TEXT("/Engine/EditorLandscapeResources/LandscapeLayerUsageMaterial.LandscapeLayerUsageMaterial")));


	// Initialize modes
	InitializeToolModes();
	CurrentToolMode = nullptr;

	// Initialize tools.
	InitializeTool_Paint();
	InitializeTool_Smooth();
	InitializeTool_Flatten();
	InitializeTool_Erosion();
	InitializeTool_HydraErosion();
	InitializeTool_Noise();
	InitializeTool_Retopologize();
	InitializeTool_NewCyLand();
	InitializeTool_ResizeCyLand();
	InitializeTool_Select();
	InitializeTool_AddComponent();
	InitializeTool_DeleteComponent();
	InitializeTool_MoveToLevel();
	InitializeTool_Mask();
	InitializeTool_CopyPaste();
	InitializeTool_Visibility();
	InitializeTool_Splines();
	InitializeTool_Ramp();
	InitializeTool_Mirror();
	InitializeTool_BPCustom();

	CurrentTool = nullptr;
	CurrentToolIndex = INDEX_NONE;

	// Initialize brushes
	InitializeBrushes();

	CurrentBrush = CyLandBrushSets[0].Brushes[0];
	CurrentBrushSetIndex = 0;

	CurrentToolTarget.CyLandInfo = nullptr;
	CurrentToolTarget.TargetType = ECyLandToolTargetType::Heightmap;
	CurrentToolTarget.LayerInfo = nullptr;

	//otherwise UObjectGlobals.cpp] [Line: 2313] Objects have the same fully qualified name but different paths. New Object: LandscapeEditorObject /Engine/Transient.UISettings Existing Object: CyLandEditorObject /Engine/Transient.UISettings
	UISettings = NewObject<UCyLandEditorObject>(GetTransientPackage(), TEXT("UISettings_"), RF_Transactional);
	UISettings->SetParent(this);

	ICyLandEditorModule& CyLandEditorModule = FModuleManager::GetModuleChecked<ICyLandEditorModule>("CyLandEditor");
	TSharedPtr<FUICommandList> CommandList = CyLandEditorModule.GetCyLandLevelViewportCommandList();

	const FCyLandEditorCommands& CyLandActions = FCyLandEditorCommands::Get();
	CommandList->MapAction(CyLandActions.IncreaseBrushSize, FExecuteAction::CreateRaw(this, &FEdModeCyLand::ChangeBrushSize, true), FCanExecuteAction(), FIsActionChecked());
	CommandList->MapAction(CyLandActions.DecreaseBrushSize, FExecuteAction::CreateRaw(this, &FEdModeCyLand::ChangeBrushSize, false), FCanExecuteAction(), FIsActionChecked());
	CommandList->MapAction(CyLandActions.IncreaseBrushFalloff, FExecuteAction::CreateRaw(this, &FEdModeCyLand::ChangeBrushFalloff, true), FCanExecuteAction(), FIsActionChecked());
	CommandList->MapAction(CyLandActions.DecreaseBrushFalloff, FExecuteAction::CreateRaw(this, &FEdModeCyLand::ChangeBrushFalloff, false), FCanExecuteAction(), FIsActionChecked());
	CommandList->MapAction(CyLandActions.IncreaseBrushStrength, FExecuteAction::CreateRaw(this, &FEdModeCyLand::ChangeBrushStrength, true), FCanExecuteAction(), FIsActionChecked());
	CommandList->MapAction(CyLandActions.DecreaseBrushStrength, FExecuteAction::CreateRaw(this, &FEdModeCyLand::ChangeBrushStrength, false), FCanExecuteAction(), FIsActionChecked());
}

/** Destructor */
FEdModeCyLand::~FEdModeCyLand()
{
	// Destroy tools.
	CyLandTools.Empty();

	// Destroy brushes
	CyLandBrushSets.Empty();

	// Clean up Debug Materials
	FlushRenderingCommands();
	GLayerDebugColorMaterial = NULL;
	GSelectionColorMaterial = NULL;
	GSelectionRegionMaterial = NULL;
	GMaskRegionMaterial = NULL;
	GCyLandBlackTexture = NULL;
	GCyLandLayerUsageMaterial = NULL;

	InteractorPainting = nullptr;
}

/** FGCObject interface */
void FEdModeCyLand::AddReferencedObjects(FReferenceCollector& Collector)
{
	// Call parent implementation
	FEdMode::AddReferencedObjects(Collector);

	Collector.AddReferencedObject(UISettings);

	Collector.AddReferencedObject(GLayerDebugColorMaterial);
	Collector.AddReferencedObject(GSelectionColorMaterial);
	Collector.AddReferencedObject(GSelectionRegionMaterial);
	Collector.AddReferencedObject(GMaskRegionMaterial);
	Collector.AddReferencedObject(GCyLandBlackTexture);
	Collector.AddReferencedObject(GCyLandLayerUsageMaterial);
}

void FEdModeCyLand::InitializeToolModes()
{
	FCyLandToolMode* ToolMode_Manage = new(CyLandToolModes)FCyLandToolMode(TEXT("ToolMode_Manage"), ECyLandToolTargetTypeMask::NA);
	ToolMode_Manage->ValidTools.Add(TEXT("NewCyLand"));
	ToolMode_Manage->ValidTools.Add(TEXT("Select"));
	ToolMode_Manage->ValidTools.Add(TEXT("AddComponent"));
	ToolMode_Manage->ValidTools.Add(TEXT("DeleteComponent"));
	ToolMode_Manage->ValidTools.Add(TEXT("MoveToLevel"));
	ToolMode_Manage->ValidTools.Add(TEXT("ResizeCyLand"));
	ToolMode_Manage->ValidTools.Add(TEXT("Splines"));
	ToolMode_Manage->CurrentToolName = TEXT("Select");

	FCyLandToolMode* ToolMode_Sculpt = new(CyLandToolModes)FCyLandToolMode(TEXT("ToolMode_Sculpt"), ECyLandToolTargetTypeMask::Heightmap | ECyLandToolTargetTypeMask::Visibility);
	ToolMode_Sculpt->ValidTools.Add(TEXT("Sculpt"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("Smooth"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("Flatten"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("Ramp"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("Noise"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("Erosion"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("HydraErosion"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("Retopologize"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("Visibility"));

	if (GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
	{
		ToolMode_Sculpt->ValidTools.Add(TEXT("BPCustom"));
	}

	ToolMode_Sculpt->ValidTools.Add(TEXT("Mask"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("CopyPaste"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("Mirror"));

	FCyLandToolMode* ToolMode_Paint = new(CyLandToolModes)FCyLandToolMode(TEXT("ToolMode_Paint"), ECyLandToolTargetTypeMask::Weightmap);
	ToolMode_Paint->ValidTools.Add(TEXT("Paint"));
	ToolMode_Paint->ValidTools.Add(TEXT("Smooth"));
	ToolMode_Paint->ValidTools.Add(TEXT("Flatten"));
	ToolMode_Paint->ValidTools.Add(TEXT("Noise"));
	ToolMode_Paint->ValidTools.Add(TEXT("Visibility"));

	if (GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
	{
		ToolMode_Paint->ValidTools.Add(TEXT("BPCustom"));
	}
}

bool FEdModeCyLand::UsesToolkits() const
{
	return true;
}

TSharedRef<FUICommandList> FEdModeCyLand::GetUICommandList() const
{
	check(Toolkit.IsValid());
	return Toolkit->GetToolkitCommands();
}

/** FEdMode: Called when the mode is entered */
void FEdModeCyLand::Enter()
{
	// Call parent implementation
	FEdMode::Enter();

	OnLevelActorDeletedDelegateHandle = GEngine->OnLevelActorDeleted().AddSP(this, &FEdModeCyLand::OnLevelActorRemoved);
	OnLevelActorAddedDelegateHandle = GEngine->OnLevelActorAdded().AddSP(this, &FEdModeCyLand::OnLevelActorAdded);

	ACyLandProxy* SelectedCyLand = GEditor->GetSelectedActors()->GetTop<ACyLandProxy>();
	if (SelectedCyLand)
	{
		CurrentToolTarget.CyLandInfo = SelectedCyLand->GetCyLandInfo();
		GEditor->SelectNone(false, true);
		GEditor->SelectActor(SelectedCyLand, true, false);
	}
	else
	{
		GEditor->SelectNone(false, true);
	}

	for (TActorIterator<ACyLandGizmoActiveActor> It(GetWorld()); It; ++It)
	{
		CurrentGizmoActor = *It;
		break;
	}

	if (!CurrentGizmoActor.IsValid())
	{
		CurrentGizmoActor = GetWorld()->SpawnActor<ACyLandGizmoActiveActor>();
		CurrentGizmoActor->ImportFromClipboard();
	}

	// Update list of landscapes and layers
	// For now depends on the SpawnActor() above in order to get the current editor world as edmodes don't get told
	UpdateCyLandList();
	UpdateTargetList();

	OnWorldChangeDelegateHandle                 = FEditorSupportDelegates::WorldChange.AddRaw(this, &FEdModeCyLand::HandleLevelsChanged, true);
	OnLevelsChangedDelegateHandle				= GetWorld()->OnLevelsChanged().AddRaw(this, &FEdModeCyLand::HandleLevelsChanged, true);
	OnMaterialCompilationFinishedDelegateHandle = UMaterial::OnMaterialCompilationFinished().AddRaw(this, &FEdModeCyLand::OnMaterialCompilationFinished);

	if (CurrentToolTarget.CyLandInfo.IsValid())
	{
		ACyLandProxy* CyLandProxy = CurrentToolTarget.CyLandInfo->GetCyLandProxy();
		CyLandProxy->OnMaterialChangedDelegate().AddRaw(this, &FEdModeCyLand::OnCyLandMaterialChangedDelegate);

		if (GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
		{
			ACyLand* CyLand = CurrentToolTarget.CyLandInfo->CyLandActor.Get();

			if (CyLand != nullptr)
			{
				CyLand->RequestProceduralContentUpdate(EProceduralContentUpdateFlag::All_Render);
			}
		}
	}

	if (CurrentGizmoActor.IsValid())
	{
		CurrentGizmoActor->SetTargetCyLand(CurrentToolTarget.CyLandInfo.Get());

		CurrentGizmoActor.Get()->bSnapToCyLandGrid = UISettings->bSnapGizmo;
	}

	int32 SquaredDataTex = ACyLandGizmoActiveActor::DataTexSize * ACyLandGizmoActiveActor::DataTexSize;

	if (CurrentGizmoActor.IsValid() && !CurrentGizmoActor->GizmoTexture)
	{
		// Init Gizmo Texture...
		CurrentGizmoActor->GizmoTexture = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
		if (CurrentGizmoActor->GizmoTexture)
		{
			CurrentGizmoActor->GizmoTexture->Source.Init(
				ACyLandGizmoActiveActor::DataTexSize,
				ACyLandGizmoActiveActor::DataTexSize,
				1,
				1,
				TSF_G8
				);
			CurrentGizmoActor->GizmoTexture->SRGB = false;
			CurrentGizmoActor->GizmoTexture->CompressionNone = true;
			CurrentGizmoActor->GizmoTexture->MipGenSettings = TMGS_NoMipmaps;
			CurrentGizmoActor->GizmoTexture->AddressX = TA_Clamp;
			CurrentGizmoActor->GizmoTexture->AddressY = TA_Clamp;
			CurrentGizmoActor->GizmoTexture->LODGroup = TEXTUREGROUP_Terrain_Weightmap;
			uint8* TexData = CurrentGizmoActor->GizmoTexture->Source.LockMip(0);
			FMemory::Memset(TexData, 0, SquaredDataTex*sizeof(uint8));
			// Restore Sampled Data if exist...
			if (CurrentGizmoActor->CachedScaleXY > 0.0f)
			{
				int32 SizeX = FMath::CeilToInt(CurrentGizmoActor->CachedWidth / CurrentGizmoActor->CachedScaleXY);
				int32 SizeY = FMath::CeilToInt(CurrentGizmoActor->CachedHeight / CurrentGizmoActor->CachedScaleXY);
				for (int32 Y = 0; Y < CurrentGizmoActor->SampleSizeY; ++Y)
				{
					for (int32 X = 0; X < CurrentGizmoActor->SampleSizeX; ++X)
					{
						float TexX = X * SizeX / CurrentGizmoActor->SampleSizeX;
						float TexY = Y * SizeY / CurrentGizmoActor->SampleSizeY;
						int32 LX = FMath::FloorToInt(TexX);
						int32 LY = FMath::FloorToInt(TexY);

						float FracX = TexX - LX;
						float FracY = TexY - LY;

						FCyGizmoSelectData* Data00 = CurrentGizmoActor->SelectedData.Find(FIntPoint(LX, LY));
						FCyGizmoSelectData* Data10 = CurrentGizmoActor->SelectedData.Find(FIntPoint(LX + 1, LY));
						FCyGizmoSelectData* Data01 = CurrentGizmoActor->SelectedData.Find(FIntPoint(LX, LY + 1));
						FCyGizmoSelectData* Data11 = CurrentGizmoActor->SelectedData.Find(FIntPoint(LX + 1, LY + 1));

						TexData[X + Y*ACyLandGizmoActiveActor::DataTexSize] = FMath::Lerp(
							FMath::Lerp(Data00 ? Data00->Ratio : 0, Data10 ? Data10->Ratio : 0, FracX),
							FMath::Lerp(Data01 ? Data01->Ratio : 0, Data11 ? Data11->Ratio : 0, FracX),
							FracY
							) * 255;
					}
				}
			}
			CurrentGizmoActor->GizmoTexture->Source.UnlockMip(0);
			CurrentGizmoActor->GizmoTexture->PostEditChange();
			FlushRenderingCommands();
		}
	}

	if (CurrentGizmoActor.IsValid() && CurrentGizmoActor->SampledHeight.Num() != SquaredDataTex)
	{
		CurrentGizmoActor->SampledHeight.Empty(SquaredDataTex);
		CurrentGizmoActor->SampledHeight.AddZeroed(SquaredDataTex);
		CurrentGizmoActor->DataType = CyLGT_None;
	}

	if (CurrentGizmoActor.IsValid()) // Update Scene Proxy
	{
		CurrentGizmoActor->ReregisterAllComponents();
	}

	GCyLandEditRenderMode = ECyLandEditRenderMode::None;
	GCyLandEditModeActive = true;

	// Load UI settings from config file
	UISettings->Load();

	UpdateShownLayerList();

	// Initialize current tool prior to creating the CyLand toolkit in case it has a dependency on it
	if (CyLandList.Num() == 0)
	{
		SetCurrentToolMode("ToolMode_Manage", false);
		SetCurrentTool("NewCyLand");
	}
	else
	{
		if (CurrentToolMode == nullptr || (CurrentToolMode->CurrentToolName == FName("NewCyLand")))
		{
			SetCurrentToolMode("ToolMode_Sculpt", false);
			SetCurrentTool("Sculpt");
		}
		else
		{
			SetCurrentTool(CurrentToolMode->CurrentToolName);
		}
	}

	// Create the CyLand editor window
	if (!Toolkit.IsValid())
	{
		Toolkit = MakeShareable(new FCyLandToolKit);
		Toolkit->Init(Owner->GetToolkitHost());
	}

	// Force real-time viewports.  We'll back up the current viewport state so we can restore it when the
	// user exits this mode.
	const bool bWantRealTime = true;
	const bool bRememberCurrentState = true;
	ForceRealTimeViewports(bWantRealTime, bRememberCurrentState);

	CurrentBrush->EnterBrush();
	if (GizmoBrush)
	{
		GizmoBrush->EnterBrush();
	}

	// Register to find out about VR input events
	UViewportWorldInteraction* ViewportWorldInteraction = Cast<UViewportWorldInteraction>(GEditor->GetEditorWorldExtensionsManager()->GetEditorWorldExtensions(GetWorld())->FindExtension(UViewportWorldInteraction::StaticClass()));
	if (ViewportWorldInteraction != nullptr)
	{

			ViewportWorldInteraction->OnViewportInteractionInputAction().RemoveAll(this);
			ViewportWorldInteraction->OnViewportInteractionInputAction().AddRaw(this, &FEdModeCyLand::OnVRAction);

			ViewportWorldInteraction->OnViewportInteractionHoverUpdate().RemoveAll(this);
			ViewportWorldInteraction->OnViewportInteractionHoverUpdate().AddRaw(this, &FEdModeCyLand::OnVRHoverUpdate);

	}
}

/** FEdMode: Called when the mode is exited */
void FEdModeCyLand::Exit()
{
	// Unregister VR mode from event handlers
	UViewportWorldInteraction* ViewportWorldInteraction = Cast<UViewportWorldInteraction>(GEditor->GetEditorWorldExtensionsManager()->GetEditorWorldExtensions(GetWorld())->FindExtension(UViewportWorldInteraction::StaticClass()));
	if (ViewportWorldInteraction != nullptr)
	{
		ViewportWorldInteraction->OnViewportInteractionInputAction().RemoveAll(this);
		ViewportWorldInteraction->OnViewportInteractionHoverUpdate().RemoveAll(this);
	}

	GEngine->OnLevelActorDeleted().Remove(OnLevelActorDeletedDelegateHandle);
	GEngine->OnLevelActorAdded().Remove(OnLevelActorAddedDelegateHandle);
	
	FEditorSupportDelegates::WorldChange.Remove(OnWorldChangeDelegateHandle);
	GetWorld()->OnLevelsChanged().Remove(OnLevelsChangedDelegateHandle);
	UMaterial::OnMaterialCompilationFinished().Remove(OnMaterialCompilationFinishedDelegateHandle);

	if (CurrentToolTarget.CyLandInfo.IsValid())
	{
		ACyLandProxy* CyLandProxy = CurrentToolTarget.CyLandInfo->GetCyLandProxy();
		CyLandProxy->OnMaterialChangedDelegate().RemoveAll(this);
	}

	// Restore real-time viewport state if we changed it
	const bool bWantRealTime = false;
	const bool bRememberCurrentState = false;
	ForceRealTimeViewports(bWantRealTime, bRememberCurrentState);

	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}

	CurrentBrush->LeaveBrush();
	if (GizmoBrush)
	{
		GizmoBrush->LeaveBrush();
	}

	if (CurrentTool)
	{
		CurrentTool->PreviousBrushIndex = CurrentBrushSetIndex;
		CurrentTool->ExitTool();
	}
	CurrentTool = NULL;
	// Leave CurrentToolIndex set so we can restore the active tool on re-opening the CyLand editor

	CyLandList.Empty();
	CyLandTargetList.Empty();

	// Save UI settings to config file
	UISettings->Save();
	GCyLandViewMode = ECyLandViewMode::Normal;
	GCyLandEditRenderMode = ECyLandEditRenderMode::None;
	GCyLandEditModeActive = false;

	CurrentGizmoActor = NULL;

	GEditor->SelectNone(false, true);

	// Clear all GizmoActors if there is no CyLand in World
	bool bIsCyLandExist = false;
	for (TActorIterator<ACyLandProxy> It(GetWorld()); It; ++It)
	{
		bIsCyLandExist = true;
		break;
	}

	if (!bIsCyLandExist)
	{
		for (TActorIterator<ACyLandGizmoActor> It(GetWorld()); It; ++It)
		{
			GetWorld()->DestroyActor(*It, false, false);
		}
	}

	// Redraw one last time to remove any CyLand editor stuff from view
	GEditor->RedrawLevelEditingViewports();

	// Call parent implementation
	FEdMode::Exit();
}


void FEdModeCyLand::OnVRHoverUpdate(UViewportInteractor* Interactor, FVector& HoverImpactPoint, bool& bWasHandled)
{
	if (InteractorPainting != nullptr && InteractorPainting == Interactor && IVREditorModule::Get().IsVREditorModeActive())
	{
		UVREditorMode* VREditorMode = Cast<UVREditorMode>( GEditor->GetEditorWorldExtensionsManager()->GetEditorWorldExtensions( GetWorld() )->FindExtension( UVREditorMode::StaticClass() ) );
		if( VREditorMode != nullptr && VREditorMode->IsActive() && Interactor != nullptr && Interactor->GetDraggingMode() == EViewportInteractionDraggingMode::Nothing )
		{
			const UVREditorInteractor* VREditorInteractor = Cast<UVREditorInteractor>(Interactor);

			if (VREditorInteractor != nullptr && !VREditorInteractor->IsHoveringOverPriorityType() && CurrentTool && (CurrentTool->GetSupportedTargetTypes() == ECyLandToolTargetTypeMask::NA || CurrentToolTarget.TargetType != ECyLandToolTargetType::Invalid))
			{
				FVector HitLocation;
				FVector LaserPointerStart, LaserPointerEnd;
				if (Interactor->GetLaserPointer( /* Out */ LaserPointerStart, /* Out */ LaserPointerEnd))
				{
					if( CyLandTrace( LaserPointerStart, LaserPointerEnd, HitLocation ) )
					{
						if (CurrentTool && CurrentTool->IsToolActive())
						{
							CurrentTool->SetExternalModifierPressed(Interactor->IsModifierPressed());
							CurrentTool->MouseMove(nullptr, nullptr, HitLocation.X, HitLocation.Y);
						}

						if (CurrentBrush)
						{
							// Inform the brush of the current location, to update the cursor
							CurrentBrush->MouseMove(HitLocation.X, HitLocation.Y);
						}
					}
				}
			}
		}
	}
}

void FEdModeCyLand::OnVRAction(FEditorViewportClient& ViewportClient, UViewportInteractor* Interactor, const struct FViewportActionKeyInput& Action, bool& bOutIsInputCaptured, bool& bWasHandled)
{
	UVREditorMode* VREditorMode = Cast<UVREditorMode>( GEditor->GetEditorWorldExtensionsManager()->GetEditorWorldExtensions( GetWorld() )->FindExtension( UVREditorMode::StaticClass() ) );
	// Never show the traditional Unreal transform widget.  It doesn't work in VR because we don't have hit proxies.
	ViewportClient.EngineShowFlags.SetModeWidgets(false);

	if (VREditorMode != nullptr && VREditorMode->IsActive() && Interactor != nullptr && Interactor->GetDraggingMode() == EViewportInteractionDraggingMode::Nothing)
	{
		if (Action.ActionType == ViewportWorldActionTypes::SelectAndMove)
		{
			const UVREditorInteractor* VREditorInteractor = Cast<UVREditorInteractor>(Interactor);

			// Begin CyLand brush
			if (Action.Event == IE_Pressed && !VREditorInteractor->IsHoveringOverUI() && !VREditorInteractor->IsHoveringOverPriorityType() && CurrentTool)
			{
				if (ViewportClient.Viewport != nullptr && ViewportClient.Viewport == ToolActiveViewport)
				{
					CurrentTool->EndTool(&ViewportClient);
					ToolActiveViewport = nullptr;
				}

				if (CurrentTool->GetSupportedTargetTypes() == ECyLandToolTargetTypeMask::NA || CurrentToolTarget.TargetType != ECyLandToolTargetType::Invalid)
				{
					FVector HitLocation;
					FVector LaserPointerStart, LaserPointerEnd;
					if (Interactor->GetLaserPointer( /* Out */ LaserPointerStart, /* Out */ LaserPointerEnd))
					{
						if (CyLandTrace(LaserPointerStart, LaserPointerEnd, HitLocation))
						{
							if (!(CurrentToolTarget.TargetType == ECyLandToolTargetType::Weightmap && CurrentToolTarget.LayerInfo == NULL))
							{
								CurrentTool->SetExternalModifierPressed(Interactor->IsModifierPressed());
								if( CurrentTool->BeginTool(&ViewportClient, CurrentToolTarget, HitLocation))
								{
									ToolActiveViewport = ViewportClient.Viewport;
								}
							}

							bIsPaintingInVR = true;
							bWasHandled = true;
							bOutIsInputCaptured = false;

							InteractorPainting = Interactor;
						}
					}
				}
			}

			// End CyLand brush
			else if (Action.Event == IE_Released)
			{
				if (CurrentTool && ViewportClient.Viewport != nullptr && ViewportClient.Viewport == ToolActiveViewport)
				{
					CurrentTool->EndTool(&ViewportClient);
					ToolActiveViewport = nullptr;
				}

				bIsPaintingInVR = false;
			}
		}
	}

}

/** FEdMode: Called once per frame */
void FEdModeCyLand::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);

	if (!IsEditingEnabled())
	{
		return;
	}

	FViewport* const Viewport = ViewportClient->Viewport;

	if (ToolActiveViewport && ToolActiveViewport == Viewport && ensure(CurrentTool) && !bIsPaintingInVR)
	{
		// Require Ctrl or not as per user preference
		const ELandscapeFoliageEditorControlType LandscapeEditorControlType = GetDefault<ULevelEditorViewportSettings>()->LandscapeEditorControlType;

		if (!Viewport->KeyState(EKeys::LeftMouseButton) ||
			(LandscapeEditorControlType == ELandscapeFoliageEditorControlType::RequireCtrl && !IsCtrlDown(Viewport)))
		{
			// Don't end the current tool if we are just modifying it
			if (!IsAdjustingBrush(Viewport) && CurrentTool->IsToolActive())
			{
				CurrentTool->EndTool(ViewportClient);
				Viewport->CaptureMouse(false);
				ToolActiveViewport = nullptr;
			}
		}
	}

	if (NewCyLandPreviewMode == ENewCyLandPreviewMode::None)
	{
		const bool bStaleTargetCyLandInfo = CurrentToolTarget.CyLandInfo.IsStale();
		const bool bStaleTargetCyLand = CurrentToolTarget.CyLandInfo.IsValid() && (CurrentToolTarget.CyLandInfo->GetCyLandProxy() != nullptr);

		if (bStaleTargetCyLandInfo || bStaleTargetCyLand)
		{
			UpdateCyLandList();
		}

		if (CurrentToolTarget.CyLandInfo.IsValid())
		{
			ACyLandProxy* CyLandProxy = CurrentToolTarget.CyLandInfo->GetCyLandProxy();
			
			if (CyLandProxy == NULL ||
				CyLandProxy->GetCyLandMaterial() != CachedCyLandMaterial)
			{
				UpdateTargetList();
			}
			else
			{
				if (CurrentTool)
				{
					CurrentTool->Tick(ViewportClient, DeltaTime);
				}
			
				if (CurrentBrush)
				{
					CurrentBrush->Tick(ViewportClient, DeltaTime);
				}
			
				if (CurrentBrush != GizmoBrush && CurrentGizmoActor.IsValid() && GizmoBrush && (GCyLandEditRenderMode & ECyLandEditRenderMode::Gizmo))
				{
					GizmoBrush->Tick(ViewportClient, DeltaTime);
				}
			}
		}
	}
}


/** FEdMode: Called when the mouse is moved over the viewport */
bool FEdModeCyLand::MouseMove(FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 MouseX, int32 MouseY)
{
	// due to mouse capture this should only ever be called on the active viewport
	// if it ever gets called on another viewport the mouse has been released without us picking it up
	if (ToolActiveViewport && ensure(CurrentTool) && !bIsPaintingInVR)
	{
		int32 MouseXDelta = MouseX - InViewportClient->GetCachedMouseX();
		int32 MouseYDelta = MouseY - InViewportClient->GetCachedMouseY();

		if (FMath::Abs(MouseXDelta) > 0 || FMath::Abs(MouseYDelta) > 0)
		{
			const bool bSizeChange = FMath::Abs(MouseXDelta) > FMath::Abs(MouseYDelta) ?
				MouseXDelta > 0 : 
				MouseYDelta < 0; // The way y position is stored here is inverted relative to expected mouse movement to change brush size
			// Are we altering something about the brush?
			FInputChord CompareChord;
			FInputBindingManager::Get().GetUserDefinedChord(FCyLandEditorCommands::CyLandContext, TEXT("DragBrushSize"), EMultipleKeyBindingIndex::Primary, CompareChord);
			if (InViewport->KeyState(CompareChord.Key))
			{
				ChangeBrushSize(bSizeChange);
				return true;
			}

			FInputBindingManager::Get().GetUserDefinedChord(FCyLandEditorCommands::CyLandContext, TEXT("DragBrushStrength"), EMultipleKeyBindingIndex::Primary, CompareChord);
			if (InViewport->KeyState(CompareChord.Key))
			{
				ChangeBrushStrength(bSizeChange);
				return true;
			}

			FInputBindingManager::Get().GetUserDefinedChord(FCyLandEditorCommands::CyLandContext, TEXT("DragBrushFalloff"), EMultipleKeyBindingIndex::Primary, CompareChord);
			if (InViewport->KeyState(CompareChord.Key))
			{
				ChangeBrushFalloff(bSizeChange);
				return true;
			}
		}

		// Require Ctrl or not as per user preference
		const ELandscapeFoliageEditorControlType LandscapeEditorControlType = GetDefault<ULevelEditorViewportSettings>()->LandscapeEditorControlType;

		if (ToolActiveViewport != InViewport ||
			!InViewport->KeyState(EKeys::LeftMouseButton) ||
			(LandscapeEditorControlType == ELandscapeFoliageEditorControlType::RequireCtrl && !IsCtrlDown(InViewport)))
		{
			if (CurrentTool->IsToolActive())
			{
				CurrentTool->EndTool(InViewportClient);
			}
			InViewport->CaptureMouse(false);
			ToolActiveViewport = nullptr;
		}
	}

	if (!IsEditingEnabled())
	{
		return false;
	}

	bool Result = false;
	if (NewCyLandPreviewMode == ENewCyLandPreviewMode::None)
	{
		if (CurrentTool)
		{
			Result = CurrentTool->MouseMove(InViewportClient, InViewport, MouseX, MouseY);
			InViewportClient->Invalidate(false, false);
		}
	}
	return Result;
}

bool FEdModeCyLand::GetCursor(EMouseCursor::Type& OutCursor) const
{
	if (!IsEditingEnabled())
	{
		return false;
	}

	bool Result = false;
	if (NewCyLandPreviewMode == ENewCyLandPreviewMode::None)
	{
		if (CurrentTool)
		{
			Result = CurrentTool->GetCursor(OutCursor);
		}
	}

	return Result;
}

bool FEdModeCyLand::DisallowMouseDeltaTracking() const
{
	// We never want to use the mouse delta tracker while painting
	return (ToolActiveViewport != nullptr);
}

/**
 * Called when the mouse is moved while a window input capture is in effect
 *
 * @param	InViewportClient	Level editor viewport client that captured the mouse input
 * @param	InViewport			Viewport that captured the mouse input
 * @param	InMouseX			New mouse cursor X coordinate
 * @param	InMouseY			New mouse cursor Y coordinate
 *
 * @return	true if input was handled
 */
bool FEdModeCyLand::CapturedMouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 MouseX, int32 MouseY)
{
	return MouseMove(ViewportClient, Viewport, MouseX, MouseY);
}

namespace
{
	bool GIsGizmoDragging = false;
}

/** FEdMode: Called when a mouse button is pressed */
bool FEdModeCyLand::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	if (CurrentGizmoActor.IsValid() && CurrentGizmoActor->IsSelected() && GCyLandEditRenderMode & ECyLandEditRenderMode::Gizmo)
	{
		GIsGizmoDragging = true;
		return true;
	}
	return false;
}



/** FEdMode: Called when the a mouse button is released */
bool FEdModeCyLand::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	if (GIsGizmoDragging)
	{
		GIsGizmoDragging = false;
		return true;
	}
	return false;
}

namespace
{
	bool RayIntersectTriangle(const FVector& Start, const FVector& End, const FVector& A, const FVector& B, const FVector& C, FVector& IntersectPoint)
	{
		const FVector BA = A - B;
		const FVector CB = B - C;
		const FVector TriNormal = BA ^ CB;

		bool bCollide = FMath::SegmentPlaneIntersection(Start, End, FPlane(A, TriNormal), IntersectPoint);
		if (!bCollide)
		{
			return false;
		}

		FVector BaryCentric = FMath::ComputeBaryCentric2D(IntersectPoint, A, B, C);
		if (BaryCentric.X > 0.0f && BaryCentric.Y > 0.0f && BaryCentric.Z > 0.0f)
		{
			return true;
		}
		return false;
	}
};

/** Trace under the mouse cursor and return the CyLand hit and the hit location (in CyLand quad space) */
bool FEdModeCyLand::CyLandMouseTrace(FEditorViewportClient* ViewportClient, float& OutHitX, float& OutHitY)
{
	int32 MouseX = ViewportClient->Viewport->GetMouseX();
	int32 MouseY = ViewportClient->Viewport->GetMouseY();

	return CyLandMouseTrace(ViewportClient, MouseX, MouseY, OutHitX, OutHitY);
}

bool FEdModeCyLand::CyLandMouseTrace(FEditorViewportClient* ViewportClient, FVector& OutHitLocation)
{
	int32 MouseX = ViewportClient->Viewport->GetMouseX();
	int32 MouseY = ViewportClient->Viewport->GetMouseY();

	return CyLandMouseTrace(ViewportClient, MouseX, MouseY, OutHitLocation);
}

/** Trace under the specified coordinates and return the CyLand hit and the hit location (in CyLand quad space) */
bool FEdModeCyLand::CyLandMouseTrace(FEditorViewportClient* ViewportClient, int32 MouseX, int32 MouseY, float& OutHitX, float& OutHitY)
{
	FVector HitLocation;
	bool bResult = CyLandMouseTrace(ViewportClient, MouseX, MouseY, HitLocation);
	OutHitX = HitLocation.X;
	OutHitY = HitLocation.Y;
	return bResult;
}

bool FEdModeCyLand::CyLandMouseTrace(FEditorViewportClient* ViewportClient, int32 MouseX, int32 MouseY, FVector& OutHitLocation)
{
	// Cache a copy of the world pointer	
	UWorld* World = ViewportClient->GetWorld();

	// Compute a world space ray from the screen space mouse coordinates
	FSceneViewFamilyContext ViewFamily(FSceneViewFamilyContext::ConstructionValues(ViewportClient->Viewport, ViewportClient->GetScene(), ViewportClient->EngineShowFlags)
		.SetRealtimeUpdate(ViewportClient->IsRealtime()));

	FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily);
	FViewportCursorLocation MouseViewportRay(View, ViewportClient, MouseX, MouseY);
	FVector MouseViewportRayDirection = MouseViewportRay.GetDirection();

	FVector Start = MouseViewportRay.GetOrigin();
	FVector End = Start + WORLD_MAX * MouseViewportRayDirection;
	if (ViewportClient->IsOrtho())
	{
		Start -= WORLD_MAX * MouseViewportRayDirection;
	}

	return CyLandTrace(Start, End, OutHitLocation);
}

bool FEdModeCyLand::CyLandTrace(const FVector& InRayOrigin, const FVector& InRayEnd, FVector& OutHitLocation)
{
	FVector Start = InRayOrigin;
	FVector End = InRayEnd;

	// Cache a copy of the world pointer
	UWorld* World = GetWorld();

	TArray<FHitResult> Results;
	// Each CyLand component has 2 collision shapes, 1 of them is specific to CyLand editor
	// Trace only ECC_Visibility channel, so we do hit only Editor specific shape
	World->LineTraceMultiByObjectType(Results, Start, End, FCollisionObjectQueryParams(ECollisionChannel::ECC_Visibility), FCollisionQueryParams(SCENE_QUERY_STAT(CyLandTrace), true));

	for (int32 i = 0; i < Results.Num(); i++)
	{
		const FHitResult& Hit = Results[i];
		UCyLandHeightfieldCollisionComponent* CollisionComponent = Cast<UCyLandHeightfieldCollisionComponent>(Hit.Component.Get());
		if (CollisionComponent)
		{
			ACyLandProxy* HitCyLand = CollisionComponent->GetCyLandProxy();
			if (HitCyLand &&
				CurrentToolTarget.CyLandInfo.IsValid() &&
				CurrentToolTarget.CyLandInfo->CyLandGuid == HitCyLand->GetCyLandGuid())
			{
				OutHitLocation = HitCyLand->CyLandActorToWorld().InverseTransformPosition(Hit.Location);
				return true;
			}
		}
	}

	// For Add CyLand Component Mode
	if (CurrentTool->GetToolName() == FName("AddComponent") &&
		CurrentToolTarget.CyLandInfo.IsValid())
	{
		bool bCollided = false;
		FVector IntersectPoint;
		CyLandRenderAddCollision = NULL;
		// Need to optimize collision for AddCyLandComponent...?
		for (auto& XYToAddCollisionPair : CurrentToolTarget.CyLandInfo->XYtoAddCollisionMap)
		{
			FCyLandAddCollision& AddCollision = XYToAddCollisionPair.Value;
			// Triangle 1
			bCollided = RayIntersectTriangle(Start, End, AddCollision.Corners[0], AddCollision.Corners[3], AddCollision.Corners[1], IntersectPoint);
			if (bCollided)
			{
				CyLandRenderAddCollision = &AddCollision;
				break;
			}
			// Triangle 2
			bCollided = RayIntersectTriangle(Start, End, AddCollision.Corners[0], AddCollision.Corners[2], AddCollision.Corners[3], IntersectPoint);
			if (bCollided)
			{
				CyLandRenderAddCollision = &AddCollision;
				break;
			}
		}

		if (bCollided &&
			CurrentToolTarget.CyLandInfo.IsValid())
		{
			ACyLandProxy* Proxy = CurrentToolTarget.CyLandInfo.Get()->GetCurrentLevelCyLandProxy(true);
			if (Proxy)
			{
				OutHitLocation = Proxy->CyLandActorToWorld().InverseTransformPosition(IntersectPoint);
				return true;
			}
		}
	}

	return false;
}

bool FEdModeCyLand::CyLandPlaneTrace(FEditorViewportClient* ViewportClient, const FPlane& Plane, FVector& OutHitLocation)
{
	int32 MouseX = ViewportClient->Viewport->GetMouseX();
	int32 MouseY = ViewportClient->Viewport->GetMouseY();

	return CyLandPlaneTrace(ViewportClient, MouseX, MouseY, Plane, OutHitLocation);
}

bool FEdModeCyLand::CyLandPlaneTrace(FEditorViewportClient* ViewportClient, int32 MouseX, int32 MouseY, const FPlane& Plane, FVector& OutHitLocation)
{
	// Compute a world space ray from the screen space mouse coordinates
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		ViewportClient->Viewport,
		ViewportClient->GetScene(),
		ViewportClient->EngineShowFlags)
		.SetRealtimeUpdate(ViewportClient->IsRealtime()));
	FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily);
	FViewportCursorLocation MouseViewportRay(View, ViewportClient, MouseX, MouseY);

	FVector Start = MouseViewportRay.GetOrigin();
	FVector End = Start + WORLD_MAX * MouseViewportRay.GetDirection();

	OutHitLocation = FMath::LinePlaneIntersection(Start, End, Plane);

	return true;
}

namespace
{
	const int32 SelectionSizeThresh = 2 * 256 * 256;
	FORCEINLINE bool IsSlowSelect(UCyLandInfo* CyLandInfo)
	{
		if (CyLandInfo)
		{
			int32 MinX = MAX_int32, MinY = MAX_int32, MaxX = MIN_int32, MaxY = MIN_int32;
			CyLandInfo->GetSelectedExtent(MinX, MinY, MaxX, MaxY);
			return (MinX != MAX_int32 && ((MaxX - MinX) * (MaxY - MinY)));
		}
		return false;
	}
};

EEditAction::Type FEdModeCyLand::GetActionEditDuplicate()
{
	EEditAction::Type Result = EEditAction::Skip;

	if (NewCyLandPreviewMode == ENewCyLandPreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->GetActionEditDuplicate();
		}
	}

	return Result;
}

EEditAction::Type FEdModeCyLand::GetActionEditDelete()
{
	EEditAction::Type Result = EEditAction::Skip;

	if (NewCyLandPreviewMode == ENewCyLandPreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->GetActionEditDelete();
		}

		if (Result == EEditAction::Skip)
		{
			// Prevent deleting Gizmo during CyLandEdMode
			if (CurrentGizmoActor.IsValid())
			{
				if (CurrentGizmoActor->IsSelected())
				{
					if (GEditor->GetSelectedActors()->Num() > 1)
					{
						GEditor->GetSelectedActors()->Deselect(CurrentGizmoActor.Get());
						Result = EEditAction::Skip;
					}
					else
					{
						Result = EEditAction::Halt;
					}
				}
			}
		}
	}

	return Result;
}

EEditAction::Type FEdModeCyLand::GetActionEditCut()
{
	EEditAction::Type Result = EEditAction::Skip;

	if (NewCyLandPreviewMode == ENewCyLandPreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->GetActionEditCut();
		}
	}

	if (Result == EEditAction::Skip)
	{
		// Special case: we don't want the 'normal' cut operation to be possible at all while in this mode, 
		// so we need to stop evaluating the others in-case they come back as true.
		return EEditAction::Halt;
	}

	return Result;
}

EEditAction::Type FEdModeCyLand::GetActionEditCopy()
{
	EEditAction::Type Result = EEditAction::Skip;

	if (NewCyLandPreviewMode == ENewCyLandPreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->GetActionEditCopy();
		}

		if (Result == EEditAction::Skip)
		{
			if (GCyLandEditRenderMode & ECyLandEditRenderMode::Gizmo || GCyLandEditRenderMode & (ECyLandEditRenderMode::Select))
			{
				if (CurrentGizmoActor.IsValid() && GizmoBrush && CurrentGizmoActor->TargetCyLandInfo)
				{
					Result = EEditAction::Process;
				}
			}
		}
	}

	return Result;
}

EEditAction::Type FEdModeCyLand::GetActionEditPaste()
{
	EEditAction::Type Result = EEditAction::Skip;

	if (NewCyLandPreviewMode == ENewCyLandPreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->GetActionEditPaste();
		}

		if (Result == EEditAction::Skip)
		{
			if (GCyLandEditRenderMode & ECyLandEditRenderMode::Gizmo || GCyLandEditRenderMode & (ECyLandEditRenderMode::Select))
			{
				if (CurrentGizmoActor.IsValid() && GizmoBrush && CurrentGizmoActor->TargetCyLandInfo)
				{
					Result = EEditAction::Process;
				}
			}
		}
	}

	return Result;
}

bool FEdModeCyLand::ProcessEditDuplicate()
{
	if (!IsEditingEnabled())
	{
		return true;
	}

	bool Result = false;

	if (NewCyLandPreviewMode == ENewCyLandPreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->ProcessEditDuplicate();
		}
	}

	return Result;
}

bool FEdModeCyLand::ProcessEditDelete()
{
	if (!IsEditingEnabled())
	{
		return true;
	}

	bool Result = false;

	if (NewCyLandPreviewMode == ENewCyLandPreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->ProcessEditDelete();
		}
	}

	return Result;
}

bool FEdModeCyLand::ProcessEditCut()
{
	if (!IsEditingEnabled())
	{
		return true;
	}

	bool Result = false;

	if (NewCyLandPreviewMode == ENewCyLandPreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->ProcessEditCut();
		}
	}

	return Result;
}

bool FEdModeCyLand::ProcessEditCopy()
{
	if (!IsEditingEnabled())
	{
		return true;
	}

	bool Result = false;

	if (NewCyLandPreviewMode == ENewCyLandPreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->ProcessEditCopy();
		}

		if (!Result)
		{
			ACyLandBlueprintCustomBrush* CurrentlySelectedBPBrush = nullptr;
			for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
			{
				CurrentlySelectedBPBrush = Cast<ACyLandBlueprintCustomBrush>(*It);
				if (CurrentlySelectedBPBrush)
				{
					break;
				}
			}

			if (!CurrentlySelectedBPBrush)
			{
				bool IsSlowTask = IsSlowSelect(CurrentGizmoActor->TargetCyLandInfo);
				if (IsSlowTask)
				{
					GWarn->BeginSlowTask(LOCTEXT("BeginFitGizmoAndCopy", "Fit Gizmo to Selected Region and Copy Data..."), true);
				}

				FScopedTransaction Transaction(LOCTEXT("CyLandGizmo_Copy", "Copy CyLand data to Gizmo"));
				CurrentGizmoActor->Modify();
				CurrentGizmoActor->FitToSelection();
				CopyDataToGizmo();
				SetCurrentTool(FName("CopyPaste"));

				if (IsSlowTask)
				{
					GWarn->EndSlowTask();
				}

				Result = true;
			}			
		}
	}

	return Result;
}

bool FEdModeCyLand::ProcessEditPaste()
{
	if (!IsEditingEnabled())
	{
		return true;
	}

	bool Result = false;

	if (NewCyLandPreviewMode == ENewCyLandPreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->ProcessEditPaste();
		}

		if (!Result)
		{
			ACyLandBlueprintCustomBrush* CurrentlySelectedBPBrush = nullptr;
			for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
			{
				CurrentlySelectedBPBrush = Cast<ACyLandBlueprintCustomBrush>(*It);
				if (CurrentlySelectedBPBrush)
				{
					break;
				}
			}

			if (!CurrentlySelectedBPBrush)
			{
				bool IsSlowTask = IsSlowSelect(CurrentGizmoActor->TargetCyLandInfo);
				if (IsSlowTask)
				{
					GWarn->BeginSlowTask(LOCTEXT("BeginPasteGizmoDataTask", "Paste Gizmo Data..."), true);
				}
				PasteDataFromGizmo();
				SetCurrentTool(FName("CopyPaste"));
				if (IsSlowTask)
				{
					GWarn->EndSlowTask();
				}

				Result = true;
			}
		}
	}

	return Result;
}

bool FEdModeCyLand::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	if (!IsEditingEnabled())
	{
		return false;
	}

	if (NewCyLandPreviewMode != ENewCyLandPreviewMode::None)
	{
		return false;
	}

	// Override Click Input for Splines Tool
	if (CurrentTool && CurrentTool->HandleClick(HitProxy, Click))
	{
		return true;
	}

	return false;
}

bool FEdModeCyLand::IsAdjustingBrush(FViewport* InViewport) const
{
	FInputChord CompareChord;
	FInputBindingManager::Get().GetUserDefinedChord(FCyLandEditorCommands::CyLandContext, TEXT("DragBrushSize"), EMultipleKeyBindingIndex::Primary, CompareChord);
	if (InViewport->KeyState(CompareChord.Key))
	{
		return true;
	}
	FInputBindingManager::Get().GetUserDefinedChord(FCyLandEditorCommands::CyLandContext, TEXT("DragBrushFalloff"), EMultipleKeyBindingIndex::Primary, CompareChord);
	if (InViewport->KeyState(CompareChord.Key))
	{
		return true;
	}
	FInputBindingManager::Get().GetUserDefinedChord(FCyLandEditorCommands::CyLandContext, TEXT("DragBrushStrength"), EMultipleKeyBindingIndex::Primary, CompareChord);
	if (InViewport->KeyState(CompareChord.Key))
	{
		return true;
	}
	return false;
}

void FEdModeCyLand::ChangeBrushSize(bool bIncrease)
{
	UISettings->Modify();
	if (CurrentBrush->GetBrushType() == ECyLandBrushType::Component)
	{
		int32 Radius = UISettings->BrushComponentSize;
		if (bIncrease)
		{
			++Radius;
		}
		else
		{
			--Radius;
		}
		Radius = (int32)FMath::Clamp(Radius, 1, 64);
		UISettings->BrushComponentSize = Radius;
	}
	else
	{
		float Radius = UISettings->BrushRadius;
		const float SliderMin = 10.0f;
		const float SliderMax = 8192.0f;
		float Diff = 0.05f; //6.0f / SliderMax;
		if (!bIncrease)
		{
			Diff = -Diff;
		}

		float NewValue = Radius * (1.0f + Diff);

		if (bIncrease)
		{
			NewValue = FMath::Max(NewValue, Radius + 1.0f);
		}
		else
		{
			NewValue = FMath::Min(NewValue, Radius - 1.0f);
		}

		NewValue = (int32)FMath::Clamp(NewValue, SliderMin, SliderMax);
		UISettings->BrushRadius = NewValue;
	}
}


void FEdModeCyLand::ChangeBrushFalloff(bool bIncrease)
{
	UISettings->Modify();
	float Falloff = UISettings->BrushFalloff;
	const float SliderMin = 0.0f;
	const float SliderMax = 1.0f;
	float Diff = 0.05f; 
	if (!bIncrease)
	{
		Diff = -Diff;
	}

	float NewValue = Falloff * (1.0f + Diff);

	if (bIncrease)
	{
		NewValue = FMath::Max(NewValue, Falloff + 0.05f);
	}
	else
	{
		NewValue = FMath::Min(NewValue, Falloff - 0.05f);
	}

	NewValue = FMath::Clamp(NewValue, SliderMin, SliderMax);
	UISettings->BrushFalloff = NewValue;
}


void FEdModeCyLand::ChangeBrushStrength(bool bIncrease)
{
	UISettings->Modify();
	float Strength = UISettings->ToolStrength;
	const float SliderMin = 0.01f;
	const float SliderMax = 10.0f;
	float Diff = 0.05f; //6.0f / SliderMax;
	if (!bIncrease)
	{
		Diff = -Diff;
	}

	float NewValue = Strength * (1.0f + Diff);

	if (bIncrease)
	{
		NewValue = FMath::Max(NewValue, Strength + 0.05f);
	}
	else
	{
		NewValue = FMath::Min(NewValue, Strength - 0.05f);
	}

	NewValue = FMath::Clamp(NewValue, SliderMin, SliderMax);
	UISettings->ToolStrength = NewValue;
}


/** FEdMode: Called when a key is pressed */
bool FEdModeCyLand::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (!IsEditingEnabled())
	{
		return false;
	}

	if(IsAdjustingBrush(Viewport))
	{
		ToolActiveViewport = Viewport;
		return false; // false to let FEditorViewportClient.InputKey start mouse tracking and enable InputDelta() so we can use it
	}

	if (Event != IE_Released)
	{
		ICyLandEditorModule& CyLandEditorModule = FModuleManager::GetModuleChecked<ICyLandEditorModule>("CyLandEditor");

		if (CyLandEditorModule.GetCyLandLevelViewportCommandList()->ProcessCommandBindings(Key, FSlateApplication::Get().GetModifierKeys(), false/*Event == IE_Repeat*/))
		{
			return true;
		}
	}

	if (NewCyLandPreviewMode != ENewCyLandPreviewMode::None)
	{
		if (Key == EKeys::LeftMouseButton)
		{
			// Press mouse button
			if (Event == IE_Pressed && !IsAltDown(Viewport))
			{
				// See if we clicked on a new CyLand handle..
				int32 HitX = Viewport->GetMouseX();
				int32 HitY = Viewport->GetMouseY();
				HHitProxy*	HitProxy = Viewport->GetHitProxy(HitX, HitY);
				if (HitProxy)
				{
					if (HitProxy->IsA(HNewCyLandGrabHandleProxy::StaticGetType()))
					{
						HNewCyLandGrabHandleProxy* EdgeProxy = (HNewCyLandGrabHandleProxy*)HitProxy;
						DraggingEdge = EdgeProxy->Edge;
						DraggingEdge_Remainder = 0;

						return false; // false to let FEditorViewportClient.InputKey start mouse tracking and enable InputDelta() so we can use it
					}
				}
			}
			else if (Event == IE_Released)
			{
				if (DraggingEdge)
				{
					DraggingEdge = ECyLandEdge::None;
					DraggingEdge_Remainder = 0;

					return false; // false to let FEditorViewportClient.InputKey end mouse tracking
				}
			}
		}
	}
	else
	{
		// Override Key Input for Selection Brush
		if (CurrentBrush)
		{
			TOptional<bool> BrushKeyOverride = CurrentBrush->InputKey(ViewportClient, Viewport, Key, Event);
			if (BrushKeyOverride.IsSet())
			{
				return BrushKeyOverride.GetValue();
			}
		}

		if (CurrentTool && CurrentTool->InputKey(ViewportClient, Viewport, Key, Event) == true)
		{
			return true;
		}

		// Require Ctrl or not as per user preference
		ELandscapeFoliageEditorControlType LandscapeEditorControlType = GetDefault<ULevelEditorViewportSettings>()->LandscapeEditorControlType;

		// HACK - Splines tool has not yet been updated to support not using ctrl
		if (CurrentBrush->GetBrushType() == ECyLandBrushType::Splines)
		{
			LandscapeEditorControlType = ELandscapeFoliageEditorControlType::RequireCtrl;
		}

		// Special case to handle where user paint with Left Click then pressing a moving camera input, we do not want to process them so as long as the tool is active ignore other input
		if (CurrentTool != nullptr && CurrentTool->IsToolActive())
		{
			return true;
		}

		if (Key == EKeys::LeftMouseButton && Event == IE_Pressed)
		{
			// When debugging it's possible to miss the "mouse released" event, if we get a "mouse pressed" event when we think it's already pressed then treat it as release first
			if (ToolActiveViewport)
			{
				CurrentTool->EndTool(ViewportClient); //-V595
				Viewport->CaptureMouse(false);
				ToolActiveViewport = nullptr;
			}

			// Only activate tool if we're not already moving the camera and we're not trying to drag a transform widget
			// Not using "if (!ViewportClient->IsMovingCamera())" because it's wrong in ortho viewports :D
			bool bMovingCamera = Viewport->KeyState(EKeys::MiddleMouseButton) || Viewport->KeyState(EKeys::RightMouseButton) || IsAltDown(Viewport);

			if ((Viewport->IsPenActive() && Viewport->GetTabletPressure() > 0.0f) ||
				(!bMovingCamera && ViewportClient->GetCurrentWidgetAxis() == EAxisList::None &&
					((LandscapeEditorControlType == ELandscapeFoliageEditorControlType::IgnoreCtrl) ||
					 (LandscapeEditorControlType == ELandscapeFoliageEditorControlType::RequireCtrl   && IsCtrlDown(Viewport)) ||
					 (LandscapeEditorControlType == ELandscapeFoliageEditorControlType::RequireNoCtrl && !IsCtrlDown(Viewport)))))
			{
				if (CurrentTool && (CurrentTool->GetSupportedTargetTypes() == ECyLandToolTargetTypeMask::NA || CurrentToolTarget.TargetType != ECyLandToolTargetType::Invalid))
				{
					FVector HitLocation;
					if (CyLandMouseTrace(ViewportClient, HitLocation))
					{
						if (CurrentToolTarget.TargetType == ECyLandToolTargetType::Weightmap && CurrentToolTarget.LayerInfo == NULL)
						{
							FMessageDialog::Open(EAppMsgType::Ok,
								NSLOCTEXT("UnrealEd", "CyLandNeedToCreateLayerInfo", "This layer has no layer info assigned yet. You must create or assign a layer info before you can paint this layer."));
							// TODO: FName to LayerInfo: do we want to create the layer info here?
							//if (FMessageDialog::Open(EAppMsgType::YesNo, NSLOCTEXT("UnrealEd", "CyLandNeedToCreateLayerInfo", "This layer has no layer info assigned yet. You must create or assign a layer info before you can paint this layer.")) == EAppReturnType::Yes)
							//{
							//	CurrentToolTarget.CyLandInfo->CyLandProxy->CreateLayerInfo(*CurrentToolTarget.PlaceholderLayerName.ToString());
							//}
						}
						else
						{
							Viewport->CaptureMouse(true);

							if (CurrentTool->CanToolBeActivated())
							{
								bool bToolActive = CurrentTool->BeginTool(ViewportClient, CurrentToolTarget, HitLocation);
								if (bToolActive)
								{
									ToolActiveViewport = Viewport;
								}
								else
								{
									ToolActiveViewport = nullptr;
									Viewport->CaptureMouse(false);
								}
								ViewportClient->Invalidate(false, false);
								return bToolActive;
							}
						}
					}
				}
				return true;
			}
		}

		if (Key == EKeys::LeftMouseButton ||
			(LandscapeEditorControlType == ELandscapeFoliageEditorControlType::RequireCtrl && (Key == EKeys::LeftControl || Key == EKeys::RightControl)))
		{
			if (Event == IE_Released && CurrentTool && CurrentTool->IsToolActive() && ToolActiveViewport)
			{
				//Set the cursor position to that of the slate cursor so it wont snap back
				Viewport->SetPreCaptureMousePosFromSlateCursor();
				CurrentTool->EndTool(ViewportClient);
				Viewport->CaptureMouse(false);
				ToolActiveViewport = nullptr;
				return true;
			}
		}

		// Prev tool
		if (Event == IE_Pressed && Key == EKeys::Comma)
		{
			if (CurrentTool && CurrentTool->IsToolActive() && ToolActiveViewport)
			{
				CurrentTool->EndTool(ViewportClient);
				Viewport->CaptureMouse(false);
				ToolActiveViewport = nullptr;
			}

			int32 OldToolIndex = CurrentToolMode->ValidTools.Find(CurrentTool->GetToolName());
			int32 NewToolIndex = FMath::Max(OldToolIndex - 1, 0);
			SetCurrentTool(CurrentToolMode->ValidTools[NewToolIndex]);

			return true;
		}

		// Next tool
		if (Event == IE_Pressed && Key == EKeys::Period)
		{
			if (CurrentTool && ToolActiveViewport)
			{
				CurrentTool->EndTool(ViewportClient);
				Viewport->CaptureMouse(false);
				ToolActiveViewport = nullptr;
			}

			int32 OldToolIndex = CurrentToolMode->ValidTools.Find(CurrentTool->GetToolName());
			int32 NewToolIndex = FMath::Min(OldToolIndex + 1, CurrentToolMode->ValidTools.Num() - 1);
			SetCurrentTool(CurrentToolMode->ValidTools[NewToolIndex]);

			return true;
		}
	}

	return false;
}

/** FEdMode: Called when mouse drag input is applied */
bool FEdModeCyLand::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	if (!IsEditingEnabled())
	{
		return false;
	}

	if (NewCyLandPreviewMode != ENewCyLandPreviewMode::None)
	{
		if (InViewportClient->GetCurrentWidgetAxis() != EAxisList::None)
		{
			FVector DeltaScale = InScale;
			DeltaScale.X = DeltaScale.Y = (FMath::Abs(InScale.X) > FMath::Abs(InScale.Y)) ? InScale.X : InScale.Y;

			UISettings->Modify();
			UISettings->NewCyLand_Location += InDrag;
			UISettings->NewCyLand_Rotation += InRot;
			UISettings->NewCyLand_Scale += DeltaScale;

			return true;
		}
		else if (DraggingEdge != ECyLandEdge::None)
		{
			FVector HitLocation;
			CyLandPlaneTrace(InViewportClient, FPlane(UISettings->NewCyLand_Location, FVector(0, 0, 1)), HitLocation);

			FTransform Transform(UISettings->NewCyLand_Rotation, UISettings->NewCyLand_Location, UISettings->NewCyLand_Scale * UISettings->NewCyLand_QuadsPerSection * UISettings->NewCyLand_SectionsPerComponent);
			HitLocation = Transform.InverseTransformPosition(HitLocation);

			UISettings->Modify();
			switch (DraggingEdge)
			{
			case ECyLandEdge::X_Negative:
			case ECyLandEdge::X_Negative_Y_Negative:
			case ECyLandEdge::X_Negative_Y_Positive:
			{
				const int32 InitialComponentCountX = UISettings->NewCyLand_ComponentCount.X;
				const int32 Delta = FMath::RoundToInt(HitLocation.X + (float)InitialComponentCountX / 2);
				UISettings->NewCyLand_ComponentCount.X = InitialComponentCountX - Delta;
				UISettings->NewCyLand_ClampSize();
				const int32 ActualDelta = UISettings->NewCyLand_ComponentCount.X - InitialComponentCountX;
				UISettings->NewCyLand_Location -= Transform.TransformVector(FVector(((float)ActualDelta / 2), 0, 0));
			}
				break;
			case ECyLandEdge::X_Positive:
			case ECyLandEdge::X_Positive_Y_Negative:
			case ECyLandEdge::X_Positive_Y_Positive:
			{
				const int32 InitialComponentCountX = UISettings->NewCyLand_ComponentCount.X;
				int32 Delta = FMath::RoundToInt(HitLocation.X - (float)InitialComponentCountX / 2);
				UISettings->NewCyLand_ComponentCount.X = InitialComponentCountX + Delta;
				UISettings->NewCyLand_ClampSize();
				const int32 ActualDelta = UISettings->NewCyLand_ComponentCount.X - InitialComponentCountX;
				UISettings->NewCyLand_Location += Transform.TransformVector(FVector(((float)ActualDelta / 2), 0, 0));
			}
				break;
			case  ECyLandEdge::Y_Negative:
			case  ECyLandEdge::Y_Positive:
				break;
			}

			switch (DraggingEdge)
			{
			case ECyLandEdge::Y_Negative:
			case ECyLandEdge::X_Negative_Y_Negative:
			case ECyLandEdge::X_Positive_Y_Negative:
			{
				const int32 InitialComponentCountY = UISettings->NewCyLand_ComponentCount.Y;
				int32 Delta = FMath::RoundToInt(HitLocation.Y + (float)InitialComponentCountY / 2);
				UISettings->NewCyLand_ComponentCount.Y = InitialComponentCountY - Delta;
				UISettings->NewCyLand_ClampSize();
				const int32 ActualDelta = UISettings->NewCyLand_ComponentCount.Y - InitialComponentCountY;
				UISettings->NewCyLand_Location -= Transform.TransformVector(FVector(0, (float)ActualDelta / 2, 0));
			}
				break;
			case ECyLandEdge::Y_Positive:
			case ECyLandEdge::X_Negative_Y_Positive:
			case ECyLandEdge::X_Positive_Y_Positive:
			{
				const int32 InitialComponentCountY = UISettings->NewCyLand_ComponentCount.Y;
				int32 Delta = FMath::RoundToInt(HitLocation.Y - (float)InitialComponentCountY / 2);
				UISettings->NewCyLand_ComponentCount.Y = InitialComponentCountY + Delta;
				UISettings->NewCyLand_ClampSize();
				const int32 ActualDelta = UISettings->NewCyLand_ComponentCount.Y - InitialComponentCountY;
				UISettings->NewCyLand_Location += Transform.TransformVector(FVector(0, (float)ActualDelta / 2, 0));
			}
				break;
			case  ECyLandEdge::X_Negative:
			case  ECyLandEdge::X_Positive:
				break;
			}

			return true;
		}
	}

	if (CurrentTool && CurrentTool->InputDelta(InViewportClient, InViewport, InDrag, InRot, InScale))
	{
		return true;
	}

	return false;
}

void FEdModeCyLand::SetCurrentToolMode(FName ToolModeName, bool bRestoreCurrentTool /*= true*/)
{
	if (CurrentToolMode == NULL || ToolModeName != CurrentToolMode->ToolModeName)
	{
		for (int32 i = 0; i < CyLandToolModes.Num(); ++i)
		{
			if (CyLandToolModes[i].ToolModeName == ToolModeName)
			{
				CurrentToolMode = &CyLandToolModes[i];
				if (bRestoreCurrentTool)
				{
					if (CurrentToolMode->CurrentToolName == NAME_None)
					{
						CurrentToolMode->CurrentToolName = CurrentToolMode->ValidTools[0];
					}
					SetCurrentTool(CurrentToolMode->CurrentToolName);
				}
				break;
			}
		}
	}
}

void FEdModeCyLand::SetCurrentTool(FName ToolName)
{
	// Several tools have identically named versions for sculpting and painting
	// Prefer the one with the same target type as the current mode

	int32 BackupToolIndex = INDEX_NONE;
	int32 ToolIndex = INDEX_NONE;
	for (int32 i = 0; i < CyLandTools.Num(); ++i)
	{
		FCyLandTool* Tool = CyLandTools[i].Get();
		if (ToolName == Tool->GetToolName())
		{
			if ((Tool->GetSupportedTargetTypes() & CurrentToolMode->SupportedTargetTypes) != 0)
			{
				ToolIndex = i;
				break;
			}
			else if (BackupToolIndex == INDEX_NONE)
			{
				BackupToolIndex = i;
			}
		}
	}

	if (ToolIndex == INDEX_NONE)
	{
		checkf(BackupToolIndex != INDEX_NONE, TEXT("Tool '%s' not found, please check name is correct!"), *ToolName.ToString());
		ToolIndex = BackupToolIndex;
	}
	check(ToolIndex != INDEX_NONE);

	SetCurrentTool(ToolIndex);
}

void FEdModeCyLand::SetCurrentTool(int32 ToolIndex)
{
	if (CurrentTool)
	{
		CurrentTool->PreviousBrushIndex = CurrentBrushSetIndex;
		CurrentTool->ExitTool();
	}
	CurrentToolIndex = CyLandTools.IsValidIndex(ToolIndex) ? ToolIndex : 0;
	CurrentTool = CyLandTools[CurrentToolIndex].Get();
	if (!CurrentToolMode->ValidTools.Contains(CurrentTool->GetToolName()))
	{
		// if tool isn't valid for this mode then automatically switch modes
		// this mostly happens with shortcut keys
		bool bFoundValidMode = false;
		for (int32 i = 0; i < CyLandToolModes.Num(); ++i)
		{
			if (CyLandToolModes[i].ValidTools.Contains(CurrentTool->GetToolName()))
			{
				SetCurrentToolMode(CyLandToolModes[i].ToolModeName, false);
				bFoundValidMode = true;
				break;
			}
		}
		check(bFoundValidMode);
	}

	// Set target type appropriate for tool
	if (CurrentTool->GetSupportedTargetTypes() == ECyLandToolTargetTypeMask::NA)
	{
		CurrentToolTarget.TargetType = ECyLandToolTargetType::Invalid;
		CurrentToolTarget.LayerInfo = nullptr;
		CurrentToolTarget.LayerName = NAME_None;
	}
	else
	{
		const uint8 TargetTypeMask = CurrentToolMode->SupportedTargetTypes & CurrentTool->GetSupportedTargetTypes();
		checkSlow(TargetTypeMask != 0);

		if ((TargetTypeMask & ECyLandToolTargetTypeMask::FromType(CurrentToolTarget.TargetType)) == 0)
		{
			auto filter = [TargetTypeMask](const TSharedRef<FCyLandTargetListInfo>& Target){ return (TargetTypeMask & ECyLandToolTargetTypeMask::FromType(Target->TargetType)) != 0; };
			const TSharedRef<FCyLandTargetListInfo>* Target = CyLandTargetList.FindByPredicate(filter);
			if (Target != nullptr)
			{
				check(CurrentToolTarget.CyLandInfo == (*Target)->CyLandInfo);
				CurrentToolTarget.TargetType = (*Target)->TargetType;
				CurrentToolTarget.LayerInfo = (*Target)->LayerInfoObj;
				CurrentToolTarget.LayerName = (*Target)->LayerName;
			}
			else // can happen with for example paint tools if there are no paint layers defined
			{
				CurrentToolTarget.TargetType = ECyLandToolTargetType::Invalid;
				CurrentToolTarget.LayerInfo = nullptr;
				CurrentToolTarget.LayerName = NAME_None;
			}
		}
	}

	CurrentTool->EnterTool();

	CurrentTool->SetEditRenderType();
	//bool MaskEnabled = CurrentTool->SupportsMask() && CurrentToolTarget.CyLandInfo.IsValid() && CurrentToolTarget.CyLandInfo->SelectedRegion.Num();

	CurrentToolMode->CurrentToolName = CurrentTool->GetToolName();

	// Set Brush
	if (!CyLandBrushSets.IsValidIndex(CurrentTool->PreviousBrushIndex))
	{
		SetCurrentBrushSet(CurrentTool->ValidBrushes[0]);
	}
	else
	{
		SetCurrentBrushSet(CurrentTool->PreviousBrushIndex);
	}

	// Update GizmoActor CyLand Target (is this necessary?)
	if (CurrentGizmoActor.IsValid() && CurrentToolTarget.CyLandInfo.IsValid())
	{
		CurrentGizmoActor->SetTargetCyLand(CurrentToolTarget.CyLandInfo.Get());
	}

	if (Toolkit.IsValid())
	{
		StaticCastSharedPtr<FCyLandToolKit>(Toolkit)->NotifyToolChanged();
	}

	GEditor->RedrawLevelEditingViewports();
}

void FEdModeCyLand::RefreshDetailPanel()
{
	if (Toolkit.IsValid())
	{
		StaticCastSharedPtr<FCyLandToolKit>(Toolkit)->RefreshDetailPanel();
	}
}

void FEdModeCyLand::SetCurrentBrushSet(FName BrushSetName)
{
	for (int32 BrushIndex = 0; BrushIndex < CyLandBrushSets.Num(); BrushIndex++)
	{
		if (BrushSetName == CyLandBrushSets[BrushIndex].BrushSetName)
		{
			SetCurrentBrushSet(BrushIndex);
			return;
		}
	}
}

void FEdModeCyLand::SetCurrentBrushSet(int32 BrushSetIndex)
{
	if (CurrentBrushSetIndex != BrushSetIndex)
	{
		CyLandBrushSets[CurrentBrushSetIndex].PreviousBrushIndex = CyLandBrushSets[CurrentBrushSetIndex].Brushes.IndexOfByKey(CurrentBrush);

		CurrentBrushSetIndex = BrushSetIndex;
		if (CurrentTool)
		{
			CurrentTool->PreviousBrushIndex = BrushSetIndex;
		}

		SetCurrentBrush(CyLandBrushSets[CurrentBrushSetIndex].PreviousBrushIndex);
	}
}

void FEdModeCyLand::SetCurrentBrush(FName BrushName)
{
	for (int32 BrushIndex = 0; BrushIndex < CyLandBrushSets[CurrentBrushSetIndex].Brushes.Num(); BrushIndex++)
	{
		if (BrushName == CyLandBrushSets[CurrentBrushSetIndex].Brushes[BrushIndex]->GetBrushName())
		{
			SetCurrentBrush(BrushIndex);
			return;
		}
	}
}

void FEdModeCyLand::SetCurrentBrush(int32 BrushIndex)
{
	if (CurrentBrush != CyLandBrushSets[CurrentBrushSetIndex].Brushes[BrushIndex])
	{
		CurrentBrush->LeaveBrush();
		CurrentBrush = CyLandBrushSets[CurrentBrushSetIndex].Brushes[BrushIndex];
		CurrentBrush->EnterBrush();

		if (Toolkit.IsValid())
		{
			StaticCastSharedPtr<FCyLandToolKit>(Toolkit)->NotifyBrushChanged();
		}
	}
}

const TArray<TSharedRef<FCyLandTargetListInfo>>& FEdModeCyLand::GetTargetList() const
{
	return CyLandTargetList;
}

const TArray<FCyLandListInfo>& FEdModeCyLand::GetCyLandList()
{
	return CyLandList;
}

void FEdModeCyLand::AddLayerInfo(UCyLandLayerInfoObject* LayerInfo)
{
	if (CurrentToolTarget.CyLandInfo.IsValid() && CurrentToolTarget.CyLandInfo->GetLayerInfoIndex(LayerInfo) == INDEX_NONE)
	{
		ACyLandProxy* Proxy = CurrentToolTarget.CyLandInfo->GetCyLandProxy();
		CurrentToolTarget.CyLandInfo->Layers.Add(FCyLandInfoLayerSettings(LayerInfo, Proxy));
		UpdateTargetList();
	}
}

int32 FEdModeCyLand::UpdateCyLandList()
{
	CyLandList.Empty();

	if (!CurrentGizmoActor.IsValid())
	{
		ACyLandGizmoActiveActor* GizmoActor = NULL;
		for (TActorIterator<ACyLandGizmoActiveActor> It(GetWorld()); It; ++It)
		{
			GizmoActor = *It;
			break;
		}
	}

	int32 CurrentIndex = INDEX_NONE;
	UWorld* World = GetWorld();
	
	if (World)
	{
		int32 Index = 0;
		auto& CyLandInfoMap = UCyLandInfoMap::GetCyLandInfoMap(World);

		for (auto It = CyLandInfoMap.Map.CreateIterator(); It; ++It)
		{
			UCyLandInfo* CyLandInfo = It.Value();
			if (CyLandInfo && !CyLandInfo->IsPendingKill())
			{
				ACyLandProxy* CyLandProxy = CyLandInfo->GetCyLandProxy();
				if (CyLandProxy)
				{
					if (CurrentToolTarget.CyLandInfo == CyLandInfo)
					{
						CurrentIndex = Index;

						// Update GizmoActor CyLand Target (is this necessary?)
						if (CurrentGizmoActor.IsValid())
						{
							CurrentGizmoActor->SetTargetCyLand(CyLandInfo);
						}
					}

					int32 MinX, MinY, MaxX, MaxY;
					int32 Width = 0, Height = 0;
					if (CyLandInfo->GetCyLandExtent(MinX, MinY, MaxX, MaxY))
					{
						Width = MaxX - MinX + 1;
						Height = MaxY - MinY + 1;
					}

					CyLandList.Add(FCyLandListInfo(*CyLandProxy->GetName(), CyLandInfo,
						CyLandInfo->ComponentSizeQuads, CyLandInfo->ComponentNumSubsections, Width, Height));
					Index++;
				}
			}
		}
	}

	if (CurrentIndex == INDEX_NONE)
	{
		if (CyLandList.Num() > 0)
		{
			if (CurrentTool != nullptr)
			{
				CurrentBrush->LeaveBrush();
				CurrentTool->ExitTool();
			}
			CurrentToolTarget.CyLandInfo = CyLandList[0].Info;
			CurrentIndex = 0;

			SetCurrentProceduralLayer(0);

			// Init UI to saved value
			ACyLandProxy* CyLandProxy = CurrentToolTarget.CyLandInfo->GetCyLandProxy();

			if (CyLandProxy != nullptr)
			{
				UISettings->TargetDisplayOrder = CyLandProxy->TargetDisplayOrder;
			}

			UpdateTargetList();
			UpdateShownLayerList();

			if (CurrentTool != nullptr)
			{
				CurrentTool->EnterTool();
				CurrentBrush->EnterBrush();
			}
		}
		else
		{
			// no CyLand, switch to "new CyLand" tool
			CurrentToolTarget.CyLandInfo = nullptr;
			UpdateTargetList();
			SetCurrentToolMode("ToolMode_Manage", false);
			SetCurrentTool("NewCyLand");
		}
	}

	return CurrentIndex;
}

void FEdModeCyLand::UpdateTargetList()
{
	CyLandTargetList.Empty();

	if (CurrentToolTarget.CyLandInfo.IsValid())
	{
		ACyLandProxy* CyLandProxy = CurrentToolTarget.CyLandInfo->GetCyLandProxy();

		if (CyLandProxy != nullptr)
		{
			CachedCyLandMaterial = CyLandProxy->GetCyLandMaterial();

			bool bFoundSelected = false;

			// Add heightmap
			CyLandTargetList.Add(MakeShareable(new FCyLandTargetListInfo(LOCTEXT("Heightmap", "Heightmap"), ECyLandToolTargetType::Heightmap, CurrentToolTarget.CyLandInfo.Get(), CurrentToolTarget.CurrentProceduralLayerIndex)));

			if (CurrentToolTarget.TargetType == ECyLandToolTargetType::Heightmap)
			{
				bFoundSelected = true;
			}

			// Add visibility
			FCyLandInfoLayerSettings VisibilitySettings(ACyLandProxy::VisibilityLayer, CyLandProxy);
			CyLandTargetList.Add(MakeShareable(new FCyLandTargetListInfo(LOCTEXT("Visibility", "Visibility"), ECyLandToolTargetType::Visibility, VisibilitySettings, CurrentToolTarget.CurrentProceduralLayerIndex)));

			if (CurrentToolTarget.TargetType == ECyLandToolTargetType::Visibility)
			{
				bFoundSelected = true;
			}

			// Add layers
			UTexture2D* ThumbnailWeightmap = NULL;
			UTexture2D* ThumbnailHeightmap = NULL;

			TargetLayerStartingIndex = CyLandTargetList.Num();

			for (auto It = CurrentToolTarget.CyLandInfo->Layers.CreateIterator(); It; It++)
			{
				FCyLandInfoLayerSettings& LayerSettings = *It;

				FName LayerName = LayerSettings.GetLayerName();

				if (LayerSettings.LayerInfoObj == ACyLandProxy::VisibilityLayer)
				{
					// Already handled above
					continue;
				}

				if (!bFoundSelected &&
					CurrentToolTarget.TargetType == ECyLandToolTargetType::Weightmap &&
					CurrentToolTarget.LayerInfo == LayerSettings.LayerInfoObj &&
					CurrentToolTarget.LayerName == LayerSettings.LayerName)
				{
					bFoundSelected = true;
				}

				// Ensure thumbnails are up valid
				if (LayerSettings.ThumbnailMIC == NULL)
				{
					if (ThumbnailWeightmap == NULL)
					{
						ThumbnailWeightmap = LoadObject<UTexture2D>(NULL, TEXT("/Engine/EditorLandscapeResources/LandscapeThumbnailWeightmap.LandscapeThumbnailWeightmap"), NULL, LOAD_None, NULL);
					}
					if (ThumbnailHeightmap == NULL)
					{
						ThumbnailHeightmap = LoadObject<UTexture2D>(NULL, TEXT("/Engine/EditorLandscapeResources/LandscapeThumbnailHeightmap.LandscapeThumbnailHeightmap"), NULL, LOAD_None, NULL);
					}

					// Construct Thumbnail MIC
					UMaterialInterface* CyLandMaterial = LayerSettings.Owner ? LayerSettings.Owner->GetCyLandMaterial() : UMaterial::GetDefaultMaterial(MD_Surface);
					LayerSettings.ThumbnailMIC = ACyLandProxy::GetLayerThumbnailMIC(CyLandMaterial, LayerName, ThumbnailWeightmap, ThumbnailHeightmap, LayerSettings.Owner);
				}

				// Add the layer
				CyLandTargetList.Add(MakeShareable(new FCyLandTargetListInfo(FText::FromName(LayerName), ECyLandToolTargetType::Weightmap, LayerSettings, CurrentToolTarget.CurrentProceduralLayerIndex)));
			}

			if (!bFoundSelected)
			{
				CurrentToolTarget.TargetType = ECyLandToolTargetType::Invalid;
				CurrentToolTarget.LayerInfo = nullptr;
				CurrentToolTarget.LayerName = NAME_None;
			}

			UpdateTargetLayerDisplayOrder(UISettings->TargetDisplayOrder);
		}
	}

	TargetsListUpdated.Broadcast();
}

void FEdModeCyLand::UpdateTargetLayerDisplayOrder(ECyLandLayerDisplayMode InTargetDisplayOrder)
{
	if (!CurrentToolTarget.CyLandInfo.IsValid())
	{
		return;
	}

	ACyLandProxy* CyLandProxy = CurrentToolTarget.CyLandInfo->GetCyLandProxy();

	if (CyLandProxy == nullptr)
	{
		return;
	}

	bool DetailPanelRefreshRequired = false;

	// Save value to CyLand
	CyLandProxy->TargetDisplayOrder = InTargetDisplayOrder;
	TArray<FName>& SavedTargetNameList = CyLandProxy->TargetDisplayOrderList;

	switch (InTargetDisplayOrder)
	{
		case ECyLandLayerDisplayMode::Default:
		{
			SavedTargetNameList.Empty();

			for (const TSharedRef<FCyLandTargetListInfo>& TargetInfo : CyLandTargetList)
			{
				SavedTargetNameList.Add(TargetInfo->LayerName);
			}

			DetailPanelRefreshRequired = true;
		}
		break;

		case ECyLandLayerDisplayMode::Alphabetical:
		{
			SavedTargetNameList.Empty();

			// Add only layers to be able to sort them by name
			for (int32 i = GetTargetLayerStartingIndex(); i < CyLandTargetList.Num(); ++i)
			{
				SavedTargetNameList.Add(CyLandTargetList[i]->LayerName);
			}

			SavedTargetNameList.Sort();

			// Then insert the non layer target that shouldn't be sorted
			for (int32 i = 0; i < GetTargetLayerStartingIndex(); ++i)
			{
				SavedTargetNameList.Insert(CyLandTargetList[i]->LayerName, i);
			}

			DetailPanelRefreshRequired = true;
		}
		break;

		case ECyLandLayerDisplayMode::UserSpecific:
		{
			for (const TSharedRef<FCyLandTargetListInfo>& TargetInfo : CyLandTargetList)
			{
				bool Found = false;

				for (const FName& LayerName : SavedTargetNameList)
				{
					if (TargetInfo->LayerName == LayerName)
					{
						Found = true;
						break;
					}
				}

				if (!Found)
				{
					DetailPanelRefreshRequired = true;
					SavedTargetNameList.Add(TargetInfo->LayerName);
				}
			}

			// Handle the removing of elements from material
			for (int32 i = SavedTargetNameList.Num() - 1; i >= 0; --i)
			{
				bool Found = false;

				for (const TSharedRef<FCyLandTargetListInfo>& TargetInfo : CyLandTargetList)
				{
					if (SavedTargetNameList[i] == TargetInfo->LayerName)
					{
						Found = true;
						break;
					}
				}

				if (!Found)
				{
					DetailPanelRefreshRequired = true;
					SavedTargetNameList.RemoveSingle(SavedTargetNameList[i]);
				}
			}
		}
		break;
	}	

	if (DetailPanelRefreshRequired)
	{
		if (Toolkit.IsValid())
		{
			StaticCastSharedPtr<FCyLandToolKit>(Toolkit)->RefreshDetailPanel();
		}
	}
}

void FEdModeCyLand::OnCyLandMaterialChangedDelegate()
{
	UpdateTargetList();
	UpdateShownLayerList();
}

void FEdModeCyLand::UpdateShownLayerList()
{
	if (!CurrentToolTarget.CyLandInfo.IsValid())
	{
		return;
	}

	// Make sure usage information is up to date
	UpdateLayerUsageInformation();

	bool DetailPanelRefreshRequired = false;

	ShownTargetLayerList.Empty();

	const TArray<FName>* DisplayOrderList = GetTargetDisplayOrderList();

	if (DisplayOrderList == nullptr)
	{
		return;
	}

	for (const FName& LayerName : *DisplayOrderList)
	{
		for (const TSharedRef<FCyLandTargetListInfo>& TargetInfo : GetTargetList())
		{
			if (TargetInfo->LayerName == LayerName)
			{
				// Keep a mapping of visible layer name to display order list so we can drag & drop proper items
				if (ShouldShowLayer(TargetInfo))
				{
					ShownTargetLayerList.Add(TargetInfo->LayerName);
					DetailPanelRefreshRequired = true;
				}

				break;
			}
		}
	}	

	if (DetailPanelRefreshRequired)
	{
		if (Toolkit.IsValid())
		{
			StaticCastSharedPtr<FCyLandToolKit>(Toolkit)->RefreshDetailPanel();
		}
	}
}

void FEdModeCyLand::UpdateLayerUsageInformation(TWeakObjectPtr<UCyLandLayerInfoObject>* LayerInfoObjectThatChanged)
{
	if (!CurrentToolTarget.CyLandInfo.IsValid())
	{
		return;
	}

	bool DetailPanelRefreshRequired = false;
	TArray<UCyLandComponent*> AllComponents;
	CurrentToolTarget.CyLandInfo->XYtoComponentMap.GenerateValueArray(AllComponents);

	TArray<TWeakObjectPtr<UCyLandLayerInfoObject>> LayerInfoObjectToProcess;
	const TArray<TSharedRef<FCyLandTargetListInfo>>& TargetList = GetTargetList();

	if (LayerInfoObjectThatChanged != nullptr)
	{
		if ((*LayerInfoObjectThatChanged).IsValid())
		{
			LayerInfoObjectToProcess.Add(*LayerInfoObjectThatChanged);
		}
	}
	else
	{
		LayerInfoObjectToProcess.Reserve(TargetList.Num());

		for (const TSharedRef<FCyLandTargetListInfo>& TargetInfo : TargetList)
		{
			if (!TargetInfo->LayerInfoObj.IsValid() || TargetInfo->TargetType != ECyLandToolTargetType::Weightmap)
			{
				continue;
			}

			LayerInfoObjectToProcess.Add(TargetInfo->LayerInfoObj);
		}
	}


	for (const TWeakObjectPtr<UCyLandLayerInfoObject>& LayerInfoObj : LayerInfoObjectToProcess)
	{		
		for (UCyLandComponent* Component : AllComponents)
		{
			TArray<uint8> WeightmapTextureData;
			FCyLandComponentDataInterface DataInterface(Component);
			DataInterface.GetWeightmapTextureData(LayerInfoObj.Get(), WeightmapTextureData);

			bool IsUsed = false;

			for (uint8 Value : WeightmapTextureData)
			{
				if (Value > 0)
				{
					IsUsed = true;
					break;
				}
			}

			bool PreviousValue = LayerInfoObj->IsReferencedFromLoadedData;
			LayerInfoObj->IsReferencedFromLoadedData = IsUsed;

			if (PreviousValue != LayerInfoObj->IsReferencedFromLoadedData)
			{
				DetailPanelRefreshRequired = true;
			}

			// Early exit as we already found a component using this layer
			if (LayerInfoObj->IsReferencedFromLoadedData)
			{
				break;
			}
		}
	}

	if (DetailPanelRefreshRequired)
	{
		if (Toolkit.IsValid())
		{
			StaticCastSharedPtr<FCyLandToolKit>(Toolkit)->RefreshDetailPanel();
		}
	}
}

bool FEdModeCyLand::ShouldShowLayer(TSharedRef<FCyLandTargetListInfo> Target) const
{
	if (!UISettings->ShowUnusedLayers)
	{
		return Target->LayerInfoObj.IsValid() && Target->LayerInfoObj.Get()->IsReferencedFromLoadedData;
	}

	return true;
}

const TArray<FName>& FEdModeCyLand::GetTargetShownList() const
{
	return ShownTargetLayerList;
}

int32 FEdModeCyLand::GetTargetLayerStartingIndex() const
{
	return TargetLayerStartingIndex;
}

const TArray<FName>* FEdModeCyLand::GetTargetDisplayOrderList() const
{
	if (!CurrentToolTarget.CyLandInfo.IsValid())
	{
		return nullptr;
	}

	ACyLandProxy* CyLandProxy = CurrentToolTarget.CyLandInfo->GetCyLandProxy();

	if (CyLandProxy == nullptr)
	{
		return nullptr;
	}

	return &CyLandProxy->TargetDisplayOrderList;
}

void FEdModeCyLand::MoveTargetLayerDisplayOrder(int32 IndexToMove, int32 IndexToDestination)
{
	if (!CurrentToolTarget.CyLandInfo.IsValid())
	{
		return;
	}

	ACyLandProxy* CyLandProxy = CurrentToolTarget.CyLandInfo->GetCyLandProxy();

	if (CyLandProxy == nullptr)
	{
		return;
	}
	
	FName Data = CyLandProxy->TargetDisplayOrderList[IndexToMove];
	CyLandProxy->TargetDisplayOrderList.RemoveAt(IndexToMove);
	CyLandProxy->TargetDisplayOrderList.Insert(Data, IndexToDestination);

	CyLandProxy->TargetDisplayOrder = ECyLandLayerDisplayMode::UserSpecific;
	UISettings->TargetDisplayOrder = ECyLandLayerDisplayMode::UserSpecific;

	// Everytime we move something from the display order we must rebuild the shown layer list
	UpdateShownLayerList();
}

FEdModeCyLand::FTargetsListUpdated FEdModeCyLand::TargetsListUpdated;

void FEdModeCyLand::HandleLevelsChanged(bool ShouldExitMode)
{
	bool bHadCyLand = (NewCyLandPreviewMode == ENewCyLandPreviewMode::None);

	UpdateCyLandList();
	UpdateTargetList();
	UpdateShownLayerList();

	// if the CyLand is deleted then close the CyLand editor
	if (ShouldExitMode && bHadCyLand && CurrentToolTarget.CyLandInfo == nullptr)
	{
		RequestDeletion();
	}

	// if a CyLand is added somehow then switch to sculpt
	if (!bHadCyLand && CurrentToolTarget.CyLandInfo != nullptr)
	{
		SetCurrentTool("Select");
		SetCurrentTool("Sculpt");
	}
}

void FEdModeCyLand::OnMaterialCompilationFinished(UMaterialInterface* MaterialInterface)
{
	if (CurrentToolTarget.CyLandInfo.IsValid() &&
		CurrentToolTarget.CyLandInfo->GetCyLandProxy() != NULL &&
		CurrentToolTarget.CyLandInfo->GetCyLandProxy()->GetCyLandMaterial() != NULL &&
		CurrentToolTarget.CyLandInfo->GetCyLandProxy()->GetCyLandMaterial()->IsDependent(MaterialInterface))
	{
		CurrentToolTarget.CyLandInfo->UpdateLayerInfoMap();
		UpdateTargetList();
		UpdateShownLayerList();
	}
}

/** FEdMode: Render the mesh paint tool */
void FEdModeCyLand::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	/** Call parent implementation */
	FEdMode::Render(View, Viewport, PDI);

	if (!IsEditingEnabled())
	{
		return;
	}

	if (NewCyLandPreviewMode != ENewCyLandPreviewMode::None)
	{
		static const float        CornerSize = 0.33f;
		static const FLinearColor CornerColour(1.0f, 1.0f, 0.5f);
		static const FLinearColor EdgeColour(1.0f, 1.0f, 0.0f);
		static const FLinearColor ComponentBorderColour(0.0f, 0.85f, 0.0f);
		static const FLinearColor SectionBorderColour(0.0f, 0.4f, 0.0f);
		static const FLinearColor InnerColour(0.0f, 0.25f, 0.0f);

		const ELevelViewportType ViewportType = ((FEditorViewportClient*)Viewport->GetClient())->ViewportType;

		const int32 ComponentCountX = UISettings->NewCyLand_ComponentCount.X;
		const int32 ComponentCountY = UISettings->NewCyLand_ComponentCount.Y;
		const int32 QuadsPerComponent = UISettings->NewCyLand_SectionsPerComponent * UISettings->NewCyLand_QuadsPerSection;
		const float ComponentSize = QuadsPerComponent;
		const FVector Offset = UISettings->NewCyLand_Location + FTransform(UISettings->NewCyLand_Rotation, FVector::ZeroVector, UISettings->NewCyLand_Scale).TransformVector(FVector(-ComponentCountX * ComponentSize / 2, -ComponentCountY * ComponentSize / 2, 0));
		const FTransform Transform = FTransform(UISettings->NewCyLand_Rotation, Offset, UISettings->NewCyLand_Scale);

		if (NewCyLandPreviewMode == ENewCyLandPreviewMode::ImportCyLand)
		{
			const TArray<uint16>& ImportHeights = UISettings->GetImportCyLandData();
			if (ImportHeights.Num() != 0)
			{
				const float InvQuadsPerComponent = 1.0f / (float)QuadsPerComponent;
				const int32 SizeX = ComponentCountX * QuadsPerComponent + 1;
				const int32 SizeY = ComponentCountY * QuadsPerComponent + 1;
				const int32 ImportSizeX = UISettings->ImportCyLand_Width;
				const int32 ImportSizeY = UISettings->ImportCyLand_Height;
				const int32 OffsetX = (SizeX - ImportSizeX) / 2;
				const int32 OffsetY = (SizeY - ImportSizeY) / 2;

				for (int32 ComponentY = 0; ComponentY < ComponentCountY; ComponentY++)
				{
					const int32 Y0 = ComponentY * QuadsPerComponent;
					const int32 Y1 = (ComponentY + 1) * QuadsPerComponent;

					const int32 ImportY0 = FMath::Clamp<int32>(Y0 - OffsetY, 0, ImportSizeY - 1);
					const int32 ImportY1 = FMath::Clamp<int32>(Y1 - OffsetY, 0, ImportSizeY - 1);

					for (int32 ComponentX = 0; ComponentX < ComponentCountX; ComponentX++)
					{
						const int32 X0 = ComponentX * QuadsPerComponent;
						const int32 X1 = (ComponentX + 1) * QuadsPerComponent;
						const int32 ImportX0 = FMath::Clamp<int32>(X0 - OffsetX, 0, ImportSizeX - 1);
						const int32 ImportX1 = FMath::Clamp<int32>(X1 - OffsetX, 0, ImportSizeX - 1);
						const float Z00 = ((float)ImportHeights[ImportX0 + ImportY0 * ImportSizeX] - 32768.0f) * LANDSCAPE_ZSCALE;
						const float Z01 = ((float)ImportHeights[ImportX0 + ImportY1 * ImportSizeX] - 32768.0f) * LANDSCAPE_ZSCALE;
						const float Z10 = ((float)ImportHeights[ImportX1 + ImportY0 * ImportSizeX] - 32768.0f) * LANDSCAPE_ZSCALE;
						const float Z11 = ((float)ImportHeights[ImportX1 + ImportY1 * ImportSizeX] - 32768.0f) * LANDSCAPE_ZSCALE;

						if (ComponentX == 0)
						{
							PDI->SetHitProxy(new HNewCyLandGrabHandleProxy(ECyLandEdge::X_Negative));
							PDI->DrawLine(Transform.TransformPosition(FVector(X0, Y0, Z00)), Transform.TransformPosition(FVector(X0, Y1, Z01)), ComponentBorderColour, SDPG_Foreground);
							PDI->SetHitProxy(NULL);
						}

						if (ComponentX == ComponentCountX - 1)
						{
							PDI->SetHitProxy(new HNewCyLandGrabHandleProxy(ECyLandEdge::X_Positive));
							PDI->DrawLine(Transform.TransformPosition(FVector(X1, Y0, Z10)), Transform.TransformPosition(FVector(X1, Y1, Z11)), ComponentBorderColour, SDPG_Foreground);
							PDI->SetHitProxy(NULL);
						}
						else
						{
							PDI->DrawLine(Transform.TransformPosition(FVector(X1, Y0, Z10)), Transform.TransformPosition(FVector(X1, Y1, Z11)), ComponentBorderColour, SDPG_Foreground);
						}

						if (ComponentY == 0)
						{
							PDI->SetHitProxy(new HNewCyLandGrabHandleProxy(ECyLandEdge::Y_Negative));
							PDI->DrawLine(Transform.TransformPosition(FVector(X0, Y0, Z00)), Transform.TransformPosition(FVector(X1, Y0, Z10)), ComponentBorderColour, SDPG_Foreground);
							PDI->SetHitProxy(NULL);
						}

						if (ComponentY == ComponentCountY - 1)
						{
							PDI->SetHitProxy(new HNewCyLandGrabHandleProxy(ECyLandEdge::Y_Positive));
							PDI->DrawLine(Transform.TransformPosition(FVector(X0, Y1, Z01)), Transform.TransformPosition(FVector(X1, Y1, Z11)), ComponentBorderColour, SDPG_Foreground);
							PDI->SetHitProxy(NULL);
						}
						else
						{
							PDI->DrawLine(Transform.TransformPosition(FVector(X0, Y1, Z01)), Transform.TransformPosition(FVector(X1, Y1, Z11)), ComponentBorderColour, SDPG_Foreground);
						}

						// intra-component lines - too slow for big landscapes
						/*
						for (int32 x=1;x<QuadsPerComponent;x++)
						{
						PDI->DrawLine(Transform.TransformPosition(FVector(X0+x, Y0, FMath::Lerp(Z00,Z10,(float)x*InvQuadsPerComponent))), Transform.TransformPosition(FVector(X0+x, Y1, FMath::Lerp(Z01,Z11,(float)x*InvQuadsPerComponent))), ComponentBorderColour, SDPG_World);
						}
						for (int32 y=1;y<QuadsPerComponent;y++)
						{
						PDI->DrawLine(Transform.TransformPosition(FVector(X0, Y0+y, FMath::Lerp(Z00,Z01,(float)y*InvQuadsPerComponent))), Transform.TransformPosition(FVector(X1, Y0+y, FMath::Lerp(Z10,Z11,(float)y*InvQuadsPerComponent))), ComponentBorderColour, SDPG_World);
						}
						*/
					}
				}
			}
		}
		else //if (NewCyLandPreviewMode == ENewCyLandPreviewMode::NewCyLand)
		{
			if (ViewportType == LVT_Perspective || ViewportType == LVT_OrthoXY || ViewportType == LVT_OrthoNegativeXY)
			{
				for (int32 x = 0; x <= ComponentCountX * QuadsPerComponent; x++)
				{
					if (x == 0)
					{
						PDI->SetHitProxy(new HNewCyLandGrabHandleProxy(ECyLandEdge::X_Negative_Y_Negative));
						PDI->DrawLine(Transform.TransformPosition(FVector(x, 0, 0)), Transform.TransformPosition(FVector(x, CornerSize * ComponentSize, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewCyLandGrabHandleProxy(ECyLandEdge::X_Negative));
						PDI->DrawLine(Transform.TransformPosition(FVector(x, CornerSize * ComponentSize, 0)), Transform.TransformPosition(FVector(x, (ComponentCountY - CornerSize) * ComponentSize, 0)), EdgeColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewCyLandGrabHandleProxy(ECyLandEdge::X_Negative_Y_Positive));
						PDI->DrawLine(Transform.TransformPosition(FVector(x, (ComponentCountY - CornerSize) * ComponentSize, 0)), Transform.TransformPosition(FVector(x, ComponentCountY * ComponentSize, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(NULL);
					}
					else if (x == ComponentCountX * QuadsPerComponent)
					{
						PDI->SetHitProxy(new HNewCyLandGrabHandleProxy(ECyLandEdge::X_Positive_Y_Negative));
						PDI->DrawLine(Transform.TransformPosition(FVector(x, 0, 0)), Transform.TransformPosition(FVector(x, CornerSize * ComponentSize, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewCyLandGrabHandleProxy(ECyLandEdge::X_Positive));
						PDI->DrawLine(Transform.TransformPosition(FVector(x, CornerSize * ComponentSize, 0)), Transform.TransformPosition(FVector(x, (ComponentCountY - CornerSize) * ComponentSize, 0)), EdgeColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewCyLandGrabHandleProxy(ECyLandEdge::X_Positive_Y_Positive));
						PDI->DrawLine(Transform.TransformPosition(FVector(x, (ComponentCountY - CornerSize) * ComponentSize, 0)), Transform.TransformPosition(FVector(x, ComponentCountY * ComponentSize, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(NULL);
					}
					else if (x % QuadsPerComponent == 0)
					{
						PDI->DrawLine(Transform.TransformPosition(FVector(x, 0, 0)), Transform.TransformPosition(FVector(x, ComponentCountY * ComponentSize, 0)), ComponentBorderColour, SDPG_Foreground);
					}
					else if (x % UISettings->NewCyLand_QuadsPerSection == 0)
					{
						PDI->DrawLine(Transform.TransformPosition(FVector(x, 0, 0)), Transform.TransformPosition(FVector(x, ComponentCountY * ComponentSize, 0)), SectionBorderColour, SDPG_Foreground);
					}
					else
					{
						PDI->DrawLine(Transform.TransformPosition(FVector(x, 0, 0)), Transform.TransformPosition(FVector(x, ComponentCountY * ComponentSize, 0)), InnerColour, SDPG_World);
					}
				}
			}
			else
			{
				// Don't allow dragging to resize in side-view
				// and there's no point drawing the inner lines as only the outer is visible
				PDI->DrawLine(Transform.TransformPosition(FVector(0, 0, 0)), Transform.TransformPosition(FVector(0, ComponentCountY * ComponentSize, 0)), EdgeColour, SDPG_World);
				PDI->DrawLine(Transform.TransformPosition(FVector(ComponentCountX * QuadsPerComponent, 0, 0)), Transform.TransformPosition(FVector(ComponentCountX * QuadsPerComponent, ComponentCountY * ComponentSize, 0)), EdgeColour, SDPG_World);
			}

			if (ViewportType == LVT_Perspective || ViewportType == LVT_OrthoXY || ViewportType == LVT_OrthoNegativeXY)
			{
				for (int32 y = 0; y <= ComponentCountY * QuadsPerComponent; y++)
				{
					if (y == 0)
					{
						PDI->SetHitProxy(new HNewCyLandGrabHandleProxy(ECyLandEdge::X_Negative_Y_Negative));
						PDI->DrawLine(Transform.TransformPosition(FVector(0, y, 0)), Transform.TransformPosition(FVector(CornerSize * ComponentSize, y, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewCyLandGrabHandleProxy(ECyLandEdge::Y_Negative));
						PDI->DrawLine(Transform.TransformPosition(FVector(CornerSize * ComponentSize, y, 0)), Transform.TransformPosition(FVector((ComponentCountX - CornerSize) * ComponentSize, y, 0)), EdgeColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewCyLandGrabHandleProxy(ECyLandEdge::X_Positive_Y_Negative));
						PDI->DrawLine(Transform.TransformPosition(FVector((ComponentCountX - CornerSize) * ComponentSize, y, 0)), Transform.TransformPosition(FVector(ComponentCountX * ComponentSize, y, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(NULL);
					}
					else if (y == ComponentCountY * QuadsPerComponent)
					{
						PDI->SetHitProxy(new HNewCyLandGrabHandleProxy(ECyLandEdge::X_Negative_Y_Positive));
						PDI->DrawLine(Transform.TransformPosition(FVector(0, y, 0)), Transform.TransformPosition(FVector(CornerSize * ComponentSize, y, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewCyLandGrabHandleProxy(ECyLandEdge::Y_Positive));
						PDI->DrawLine(Transform.TransformPosition(FVector(CornerSize * ComponentSize, y, 0)), Transform.TransformPosition(FVector((ComponentCountX - CornerSize) * ComponentSize, y, 0)), EdgeColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewCyLandGrabHandleProxy(ECyLandEdge::X_Positive_Y_Positive));
						PDI->DrawLine(Transform.TransformPosition(FVector((ComponentCountX - CornerSize) * ComponentSize, y, 0)), Transform.TransformPosition(FVector(ComponentCountX * ComponentSize, y, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(NULL);
					}
					else if (y % QuadsPerComponent == 0)
					{
						PDI->DrawLine(Transform.TransformPosition(FVector(0, y, 0)), Transform.TransformPosition(FVector(ComponentCountX * ComponentSize, y, 0)), ComponentBorderColour, SDPG_Foreground);
					}
					else if (y % UISettings->NewCyLand_QuadsPerSection == 0)
					{
						PDI->DrawLine(Transform.TransformPosition(FVector(0, y, 0)), Transform.TransformPosition(FVector(ComponentCountX * ComponentSize, y, 0)), SectionBorderColour, SDPG_Foreground);
					}
					else
					{
						PDI->DrawLine(Transform.TransformPosition(FVector(0, y, 0)), Transform.TransformPosition(FVector(ComponentCountX * ComponentSize, y, 0)), InnerColour, SDPG_World);
					}
				}
			}
			else
			{
				// Don't allow dragging to resize in side-view
				// and there's no point drawing the inner lines as only the outer is visible
				PDI->DrawLine(Transform.TransformPosition(FVector(0, 0, 0)), Transform.TransformPosition(FVector(ComponentCountX * ComponentSize, 0, 0)), EdgeColour, SDPG_World);
				PDI->DrawLine(Transform.TransformPosition(FVector(0, ComponentCountY * QuadsPerComponent, 0)), Transform.TransformPosition(FVector(ComponentCountX * ComponentSize, ComponentCountY * QuadsPerComponent, 0)), EdgeColour, SDPG_World);
			}
		}

		return;
	}

	if (CyLandRenderAddCollision)
	{
		PDI->DrawLine(CyLandRenderAddCollision->Corners[0], CyLandRenderAddCollision->Corners[3], FColor(0, 255, 128), SDPG_Foreground);
		PDI->DrawLine(CyLandRenderAddCollision->Corners[3], CyLandRenderAddCollision->Corners[1], FColor(0, 255, 128), SDPG_Foreground);
		PDI->DrawLine(CyLandRenderAddCollision->Corners[1], CyLandRenderAddCollision->Corners[0], FColor(0, 255, 128), SDPG_Foreground);

		PDI->DrawLine(CyLandRenderAddCollision->Corners[0], CyLandRenderAddCollision->Corners[2], FColor(0, 255, 128), SDPG_Foreground);
		PDI->DrawLine(CyLandRenderAddCollision->Corners[2], CyLandRenderAddCollision->Corners[3], FColor(0, 255, 128), SDPG_Foreground);
		PDI->DrawLine(CyLandRenderAddCollision->Corners[3], CyLandRenderAddCollision->Corners[0], FColor(0, 255, 128), SDPG_Foreground);
	}

	// Override Rendering for Splines Tool
	if (CurrentTool)
	{
		CurrentTool->Render(View, Viewport, PDI);
	}
}

/** FEdMode: Render HUD elements for this tool */
void FEdModeCyLand::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
}

bool FEdModeCyLand::UsesTransformWidget() const
{
	if (NewCyLandPreviewMode != ENewCyLandPreviewMode::None)
	{
		return true;
	}

	// Override Widget for Splines Tool
	if (CurrentTool && CurrentTool->UsesTransformWidget())
	{
		return true;
	}

	return (CurrentGizmoActor.IsValid() && CurrentGizmoActor->IsSelected() && (GCyLandEditRenderMode & ECyLandEditRenderMode::Gizmo));
}

bool FEdModeCyLand::ShouldDrawWidget() const
{
	return UsesTransformWidget();
}

EAxisList::Type FEdModeCyLand::GetWidgetAxisToDraw(FWidget::EWidgetMode InWidgetMode) const
{
	if (NewCyLandPreviewMode == ENewCyLandPreviewMode::None)
	{
		// Override Widget for Splines Tool
		if (CurrentTool)
		{
			return CurrentTool->GetWidgetAxisToDraw(InWidgetMode);
		}
	}

	switch (InWidgetMode)
	{
	case FWidget::WM_Translate:
		return EAxisList::XYZ;
	case FWidget::WM_Rotate:
		return EAxisList::Z;
	case FWidget::WM_Scale:
		return EAxisList::XYZ;
	default:
		return EAxisList::None;
	}
}

FVector FEdModeCyLand::GetWidgetLocation() const
{
	if (NewCyLandPreviewMode != ENewCyLandPreviewMode::None)
	{
		return UISettings->NewCyLand_Location;
	}

	if (CurrentGizmoActor.IsValid() && (GCyLandEditRenderMode & ECyLandEditRenderMode::Gizmo) && CurrentGizmoActor->IsSelected())
	{
		if (CurrentGizmoActor->TargetCyLandInfo && CurrentGizmoActor->TargetCyLandInfo->GetCyLandProxy())
		{
			// Apply CyLand transformation when it is available
			UCyLandInfo* CyLandInfo = CurrentGizmoActor->TargetCyLandInfo;
			return CurrentGizmoActor->GetActorLocation()
				+ FQuatRotationMatrix(CyLandInfo->GetCyLandProxy()->GetActorQuat()).TransformPosition(FVector(0, 0, CurrentGizmoActor->GetLength()));
		}
		return CurrentGizmoActor->GetActorLocation();
	}

	// Override Widget for Splines Tool
	if (CurrentTool && CurrentTool->OverrideWidgetLocation())
	{
		return CurrentTool->GetWidgetLocation();
	}

	return FEdMode::GetWidgetLocation();
}

bool FEdModeCyLand::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	if (NewCyLandPreviewMode != ENewCyLandPreviewMode::None)
	{
		InMatrix = FRotationMatrix(UISettings->NewCyLand_Rotation);
		return true;
	}

	// Override Widget for Splines Tool
	if (CurrentTool && CurrentTool->OverrideWidgetRotation())
	{
		InMatrix = CurrentTool->GetWidgetRotation();
		return true;
	}

	return false;
}

bool FEdModeCyLand::GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	return GetCustomDrawingCoordinateSystem(InMatrix, InData);
}

/** FEdMode: Handling SelectActor */
bool FEdModeCyLand::Select(AActor* InActor, bool bInSelected)
{
	if (!IsEditingEnabled())
	{
		return false;
	}

	if (InActor->IsA<ACyLandProxy>() && bInSelected)
	{
		ACyLandProxy* CyLand = CastChecked<ACyLandProxy>(InActor);

		if (CurrentToolTarget.CyLandInfo != CyLand->GetCyLandInfo())
		{
			CurrentToolTarget.CyLandInfo = CyLand->GetCyLandInfo();
			UpdateTargetList();

			// If we were in "New CyLand" mode and we select a CyLand then switch to editing mode
			if (NewCyLandPreviewMode != ENewCyLandPreviewMode::None)
			{
				SetCurrentTool("Sculpt");
			}
		}
	}

	if (IsSelectionAllowed(InActor, bInSelected))
	{
		// false means "we haven't handled the selection", which allows the editor to perform the selection
		// so false means "allow"
		return false;
	}

	// true means "we have handled the selection", which effectively blocks the selection from happening
	// so true means "block"
	return true;
}

/** FEdMode: Check to see if an actor can be selected in this mode - no side effects */
bool FEdModeCyLand::IsSelectionAllowed(AActor* InActor, bool bInSelection) const
{
	if (!IsEditingEnabled())
	{
		return false;
	}

	// Override Selection for Splines Tool
	if (CurrentTool && CurrentTool->OverrideSelection())
	{
		return CurrentTool->IsSelectionAllowed(InActor, bInSelection);
	}

	if (!bInSelection)
	{
		// always allow de-selection
		return true;
	}

	if (InActor->IsA(ACyLandProxy::StaticClass()))
	{
		return true;
	}
	else if (InActor->IsA(ACyLandGizmoActor::StaticClass()))
	{
		return true;
	}
	else if (InActor->IsA(ALight::StaticClass()))
	{
		return true;
	}
	else if (InActor->IsA(ACyLandBlueprintCustomBrush::StaticClass()))
	{
		return true;
	}

	return true;
}

/** FEdMode: Called when the currently selected actor has changed */
void FEdModeCyLand::ActorSelectionChangeNotify()
{
	if (CurrentGizmoActor.IsValid() && CurrentGizmoActor->IsSelected())
	{
		GEditor->SelectNone(false, true);
		GEditor->SelectActor(CurrentGizmoActor.Get(), true, false, true);
	}
	/*
		USelection* EditorSelection = GEditor->GetSelectedActors();
		for ( FSelectionIterator Itor(EditorSelection) ; Itor ; ++Itor )
		{
		if (((*Itor)->IsA(ACyLandGizmoActor::StaticClass())) )
		{
		bIsGizmoSelected = true;
		break;
		}
		}
	*/
}

void FEdModeCyLand::ActorMoveNotify()
{
	//GUnrealEd->UpdateFloatingPropertyWindows();
}

void FEdModeCyLand::PostUndo()
{
	HandleLevelsChanged(false);
}

/** Forces all level editor viewports to realtime mode */
void FEdModeCyLand::ForceRealTimeViewports(const bool bEnable, const bool bStoreCurrentState)

{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<ILevelEditor> LevelEditor = LevelEditorModule.GetFirstLevelEditor();
	if (LevelEditor.IsValid())
	{
		TArray<TSharedPtr<ILevelViewport>> Viewports = LevelEditor->GetViewports();
		for (const TSharedPtr<ILevelViewport>& ViewportWindow : Viewports)
		{
			if (ViewportWindow.IsValid())
			{
				FEditorViewportClient& Viewport = ViewportWindow->GetLevelViewportClient();
				if (bEnable)
				{
					Viewport.SetRealtime(bEnable, bStoreCurrentState);

					// @todo vreditor: Force game view to true in VREditor since we can't use hitproxies and debug objects yet
					UVREditorMode* VREditorMode = Cast<UVREditorMode>( GEditor->GetEditorWorldExtensionsManager()->GetEditorWorldExtensions( GetWorld() )->FindExtension( UVREditorMode::StaticClass() ) );
					if( VREditorMode != nullptr && VREditorMode->IsActive())
					{
						Viewport.SetVREditView(true);
					} 
					else
					{
						Viewport.SetVREditView(false);
					}
				}
				else
				{
					const bool bAllowDisable = true;
					Viewport.RestoreRealtime(bAllowDisable);
				}
			}
		}
	}
}

void FEdModeCyLand::ReimportData(const FCyLandTargetListInfo& TargetInfo)
{
	const FString& SourceFilePath = TargetInfo.ReimportFilePath();
	if (SourceFilePath.Len())
	{
		ImportData(TargetInfo, SourceFilePath);
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "CyLandReImport_BadFileName", "Reimport Source Filename is invalid"));
	}
}

void FEdModeCyLand::ImportData(const FCyLandTargetListInfo& TargetInfo, const FString& Filename)
{
	UCyLandInfo* CyLandInfo = TargetInfo.CyLandInfo.Get();
	int32 MinX, MinY, MaxX, MaxY;
	if (CyLandInfo && CyLandInfo->GetCyLandExtent(MinX, MinY, MaxX, MaxY))
	{
		const FCyLandFileResolution CyLandResolution = {(uint32)(1 + MaxX - MinX), (uint32)(1 + MaxY - MinY)};

		ICyLandEditorModule& CyLandEditorModule = FModuleManager::GetModuleChecked<ICyLandEditorModule>("CyLandEditor");

		if (TargetInfo.TargetType == ECyLandToolTargetType::Heightmap)
		{
			const ICyLandHeightmapFileFormat* HeightmapFormat = CyLandEditorModule.GetHeightmapFormatByExtension(*FPaths::GetExtension(Filename, true));

			if (!HeightmapFormat)
			{
				FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("CyLandEditor.NewCyLand", "Import_UnknownFileType", "File type not recognised"));

				return;
			}

			FCyLandFileResolution ImportResolution = {0, 0};

			const FCyLandHeightmapInfo HeightmapInfo = HeightmapFormat->Validate(*Filename);

			// display error message if there is one, and abort the import
			if (HeightmapInfo.ResultCode == ECyLandImportResult::Error)
			{
				FMessageDialog::Open(EAppMsgType::Ok, HeightmapInfo.ErrorMessage);

				return;
			}

			// if the file is a raw format with multiple possibly resolutions, only attempt import if one matches the current CyLand
			if (HeightmapInfo.PossibleResolutions.Num() > 1)
			{
				if (!HeightmapInfo.PossibleResolutions.Contains(CyLandResolution))
				{
					FFormatNamedArguments Args;
					Args.Add(TEXT("CyLandSizeX"), CyLandResolution.Width);
					Args.Add(TEXT("CyLandSizeY"), CyLandResolution.Height);

					FMessageDialog::Open(EAppMsgType::Ok,
						FText::Format(NSLOCTEXT("CyLandEditor.NewCyLand", "Import_HeightmapSizeMismatchRaw", "The heightmap file does not match the current CyLand extent ({CyLandSizeX}\u00D7{CyLandSizeY}), and its exact resolution could not be determined"), Args));

					return;
				}
				else
				{
					ImportResolution = CyLandResolution;
				}
			}

			// display warning message if there is one and allow user to cancel
			if (HeightmapInfo.ResultCode == ECyLandImportResult::Warning)
			{
				auto Result = FMessageDialog::Open(EAppMsgType::OkCancel, HeightmapInfo.ErrorMessage);

				if (Result != EAppReturnType::Ok)
				{
					return;
				}
			}

			// if the file is a format with resolution information, warn the user if the resolution doesn't match the current CyLand
			// unlike for raw this is only a warning as we can pad/clip the data if we know what resolution it is
			if (HeightmapInfo.PossibleResolutions.Num() == 1)
			{
				ImportResolution = HeightmapInfo.PossibleResolutions[0];
				if (ImportResolution != CyLandResolution)
				{
					FFormatNamedArguments Args;
					Args.Add(TEXT("FileSizeX"), ImportResolution.Width);
					Args.Add(TEXT("FileSizeY"), ImportResolution.Height);
					Args.Add(TEXT("CyLandSizeX"), CyLandResolution.Width);
					Args.Add(TEXT("CyLandSizeY"), CyLandResolution.Height);

					auto Result = FMessageDialog::Open(EAppMsgType::OkCancel,
						FText::Format(NSLOCTEXT("CyLandEditor.NewCyLand", "Import_HeightmapSizeMismatch", "The heightmap file's size ({FileSizeX}\u00D7{FileSizeY}) does not match the current CyLand extent ({CyLandSizeX}\u00D7{CyLandSizeY}), if you continue it will be padded/clipped to fit"), Args));

					if (Result != EAppReturnType::Ok)
					{
						return;
					}
				}
			}

			FCyLandHeightmapImportData ImportData = HeightmapFormat->Import(*Filename, ImportResolution);

			if (ImportData.ResultCode == ECyLandImportResult::Error)
			{
				FMessageDialog::Open(EAppMsgType::Ok, ImportData.ErrorMessage);

				return;
			}

			if (GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
			{
				ChangeHeightmapsToCurrentProceduralLayerHeightmaps();
			}

			TArray<uint16> Data;
			if (ImportResolution != CyLandResolution)
			{
				// Cloned from FCyLandEditorDetailCustomization_NewCyLand.OnCreateButtonClicked
				// so that reimports behave the same as the initial import :)

				const int32 OffsetX = (int32)(CyLandResolution.Width - ImportResolution.Width) / 2;
				const int32 OffsetY = (int32)(CyLandResolution.Height - ImportResolution.Height) / 2;

				Data.SetNumUninitialized(CyLandResolution.Width * CyLandResolution.Height * sizeof(uint16));

				CyLandEditorUtils::ExpandData<uint16>(Data.GetData(), ImportData.Data.GetData(),
					0, 0, ImportResolution.Width - 1, ImportResolution.Height - 1,
					-OffsetX, -OffsetY, CyLandResolution.Width - OffsetX - 1, CyLandResolution.Height - OffsetY - 1);
			}
			else
			{
				Data = MoveTemp(ImportData.Data);
			}

			FScopedTransaction Transaction(LOCTEXT("Undo_ImportHeightmap", "Importing CyLand Heightmap"));

			FHeightmapAccessor<false> HeightmapAccessor(CyLandInfo);
			HeightmapAccessor.SetData(MinX, MinY, MaxX, MaxY, Data.GetData());

			if (GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
			{
				ChangeHeightmapsToCurrentProceduralLayerHeightmaps(true);

				check(CurrentToolTarget.CyLandInfo->CyLandActor.IsValid());
				CurrentToolTarget.CyLandInfo->CyLandActor.Get()->RequestProceduralContentUpdate(EProceduralContentUpdateFlag::Heightmap_All);
			}
		}
		else
		{
			const ICyLandWeightmapFileFormat* WeightmapFormat = CyLandEditorModule.GetWeightmapFormatByExtension(*FPaths::GetExtension(Filename, true));

			if (!WeightmapFormat)
			{
				FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("CyLandEditor.NewCyLand", "Import_UnknownFileType", "File type not recognised"));

				return;
			}

			FCyLandFileResolution ImportResolution = {0, 0};

			const FCyLandWeightmapInfo WeightmapInfo = WeightmapFormat->Validate(*Filename, TargetInfo.LayerName);

			// display error message if there is one, and abort the import
			if (WeightmapInfo.ResultCode == ECyLandImportResult::Error)
			{
				FMessageDialog::Open(EAppMsgType::Ok, WeightmapInfo.ErrorMessage);

				return;
			}

			// if the file is a raw format with multiple possibly resolutions, only attempt import if one matches the current CyLand
			if (WeightmapInfo.PossibleResolutions.Num() > 1)
			{
				if (!WeightmapInfo.PossibleResolutions.Contains(CyLandResolution))
				{
					FFormatNamedArguments Args;
					Args.Add(TEXT("CyLandSizeX"), CyLandResolution.Width);
					Args.Add(TEXT("CyLandSizeY"), CyLandResolution.Height);

					FMessageDialog::Open(EAppMsgType::Ok,
						FText::Format(NSLOCTEXT("CyLandEditor.NewCyLand", "Import_LayerSizeMismatch_ResNotDetermined", "The layer file does not match the current CyLand extent ({CyLandSizeX}\u00D7{CyLandSizeY}), and its exact resolution could not be determined"), Args));

					return;
				}
				else
				{
					ImportResolution = CyLandResolution;
				}
			}

			// display warning message if there is one and allow user to cancel
			if (WeightmapInfo.ResultCode == ECyLandImportResult::Warning)
			{
				auto Result = FMessageDialog::Open(EAppMsgType::OkCancel, WeightmapInfo.ErrorMessage);

				if (Result != EAppReturnType::Ok)
				{
					return;
				}
			}

			// if the file is a format with resolution information, warn the user if the resolution doesn't match the current CyLand
			// unlike for raw this is only a warning as we can pad/clip the data if we know what resolution it is
			if (WeightmapInfo.PossibleResolutions.Num() == 1)
			{
				ImportResolution = WeightmapInfo.PossibleResolutions[0];
				if (ImportResolution != CyLandResolution)
				{
					FFormatNamedArguments Args;
					Args.Add(TEXT("FileSizeX"), ImportResolution.Width);
					Args.Add(TEXT("FileSizeY"), ImportResolution.Height);
					Args.Add(TEXT("CyLandSizeX"), CyLandResolution.Width);
					Args.Add(TEXT("CyLandSizeY"), CyLandResolution.Height);

					auto Result = FMessageDialog::Open(EAppMsgType::OkCancel,
						FText::Format(NSLOCTEXT("CyLandEditor.NewCyLand", "Import_LayerSizeMismatch_WillClamp", "The layer file's size ({FileSizeX}\u00D7{FileSizeY}) does not match the current CyLand extent ({CyLandSizeX}\u00D7{CyLandSizeY}), if you continue it will be padded/clipped to fit"), Args));

					if (Result != EAppReturnType::Ok)
					{
						return;
					}
				}
			}

			FCyLandWeightmapImportData ImportData = WeightmapFormat->Import(*Filename, TargetInfo.LayerName, ImportResolution);

			if (ImportData.ResultCode == ECyLandImportResult::Error)
			{
				FMessageDialog::Open(EAppMsgType::Ok, ImportData.ErrorMessage);

				return;
			}

			TArray<uint8> Data;
			if (ImportResolution != CyLandResolution)
			{
				// Cloned from FCyLandEditorDetailCustomization_NewCyLand.OnCreateButtonClicked
				// so that reimports behave the same as the initial import :)

				const int32 OffsetX = (int32)(CyLandResolution.Width - ImportResolution.Width) / 2;
				const int32 OffsetY = (int32)(CyLandResolution.Height - ImportResolution.Height) / 2;

				Data.SetNumUninitialized(CyLandResolution.Width * CyLandResolution.Height * sizeof(uint8));

				CyLandEditorUtils::ExpandData<uint8>(Data.GetData(), ImportData.Data.GetData(),
					0, 0, ImportResolution.Width - 1, ImportResolution.Height - 1,
					-OffsetX, -OffsetY, CyLandResolution.Width - OffsetX - 1, CyLandResolution.Height - OffsetY - 1);
			}
			else
			{
				Data = MoveTemp(ImportData.Data);
			}

			FScopedTransaction Transaction(LOCTEXT("Undo_ImportWeightmap", "Importing CyLand Layer"));

			FAlphamapAccessor<false, false> AlphamapAccessor(CyLandInfo, TargetInfo.LayerInfoObj.Get());
			AlphamapAccessor.SetData(MinX, MinY, MaxX, MaxY, Data.GetData(), ECyLandLayerPaintingRestriction::None);
		}
	}
}

void FEdModeCyLand::DeleteCyLandComponents(UCyLandInfo* CyLandInfo, TSet<UCyLandComponent*> ComponentsToDelete)
{
	CyLandInfo->Modify();
	ACyLandProxy* Proxy = CyLandInfo->GetCyLandProxy();
	Proxy->Modify();

	for (UCyLandComponent* Component : ComponentsToDelete)
	{
		Component->Modify();
		UCyLandHeightfieldCollisionComponent* CollisionComp = Component->CollisionComponent.Get();
		if (CollisionComp)
		{
			CollisionComp->Modify();
		}
	}

	int32 ComponentSizeVerts = CyLandInfo->ComponentNumSubsections * (CyLandInfo->SubsectionSizeQuads + 1);
	int32 NeedHeightmapSize = 1 << FMath::CeilLogTwo(ComponentSizeVerts);

	TSet<UCyLandComponent*> HeightmapUpdateComponents;
	// Need to split all the component which share Heightmap with selected components
	// Search neighbor only
	for (UCyLandComponent* Component : ComponentsToDelete)
	{
		int32 SearchX = Component->GetHeightmap()->Source.GetSizeX() / NeedHeightmapSize;
		int32 SearchY = Component->GetHeightmap()->Source.GetSizeY() / NeedHeightmapSize;
		FIntPoint ComponentBase = Component->GetSectionBase() / Component->ComponentSizeQuads;

		for (int32 Y = 0; Y < SearchY; ++Y)
		{
			for (int32 X = 0; X < SearchX; ++X)
			{
				// Search for four directions...
				for (int32 Dir = 0; Dir < 4; ++Dir)
				{
					int32 XDir = (Dir >> 1) ? 1 : -1;
					int32 YDir = (Dir % 2) ? 1 : -1;
					UCyLandComponent* Neighbor = CyLandInfo->XYtoComponentMap.FindRef(ComponentBase + FIntPoint(XDir*X, YDir*Y));
					if (Neighbor && Neighbor->GetHeightmap() == Component->GetHeightmap() && !HeightmapUpdateComponents.Contains(Neighbor))
					{
						Neighbor->Modify();
						HeightmapUpdateComponents.Add(Neighbor);
					}
				}
			}
		}
	}

	// Changing Heightmap format for selected components
	for (UCyLandComponent* Component : HeightmapUpdateComponents)
	{
		ACyLand::SplitHeightmap(Component, false);
	}

	// Remove attached foliage
	for (UCyLandComponent* Component : ComponentsToDelete)
	{
		UCyLandHeightfieldCollisionComponent* CollisionComp = Component->CollisionComponent.Get();
		if (CollisionComp)
		{
			AInstancedFoliageActor::DeleteInstancesForComponent(Proxy->GetWorld(), CollisionComp);
		}
	}

	// Check which ones are need for height map change
	for (UCyLandComponent* Component : ComponentsToDelete)
	{
		// Reset neighbors LOD information
		FIntPoint ComponentBase = Component->GetSectionBase() / Component->ComponentSizeQuads;
		FIntPoint NeighborKeys[8] =
		{
			ComponentBase + FIntPoint(-1, -1),
			ComponentBase + FIntPoint(+0, -1),
			ComponentBase + FIntPoint(+1, -1),
			ComponentBase + FIntPoint(-1, +0),
			ComponentBase + FIntPoint(+1, +0),
			ComponentBase + FIntPoint(-1, +1),
			ComponentBase + FIntPoint(+0, +1),
			ComponentBase + FIntPoint(+1, +1)
		};

		for (const FIntPoint& NeighborKey : NeighborKeys)
		{
			UCyLandComponent* NeighborComp = CyLandInfo->XYtoComponentMap.FindRef(NeighborKey);
			if (NeighborComp && !ComponentsToDelete.Contains(NeighborComp))
			{
				NeighborComp->Modify();
				NeighborComp->InvalidateLightingCache();

				// is this really needed? It can happen multiple times per component!
				FComponentReregisterContext ReregisterContext(NeighborComp);
			}
		}

		// Remove Selected Region in deleted Component
		for (int32 Y = 0; Y < Component->ComponentSizeQuads; ++Y)
		{
			for (int32 X = 0; X < Component->ComponentSizeQuads; ++X)
			{
				CyLandInfo->SelectedRegion.Remove(FIntPoint(X, Y) + Component->GetSectionBase());
			}
		}

		UTexture2D* HeightmapTexture = Component->GetHeightmap();

		if (HeightmapTexture)
		{
			HeightmapTexture->SetFlags(RF_Transactional);
			HeightmapTexture->Modify();
			HeightmapTexture->MarkPackageDirty();
			HeightmapTexture->ClearFlags(RF_Standalone); // Remove when there is no reference for this Heightmap...
		}

		for (int32 i = 0; i < Component->WeightmapTextures.Num(); ++i)
		{
			Component->WeightmapTextures[i]->SetFlags(RF_Transactional);
			Component->WeightmapTextures[i]->Modify();
			Component->WeightmapTextures[i]->MarkPackageDirty();
			Component->WeightmapTextures[i]->ClearFlags(RF_Standalone);
		}

		if (Component->XYOffsetmapTexture)
		{
			Component->XYOffsetmapTexture->SetFlags(RF_Transactional);
			Component->XYOffsetmapTexture->Modify();
			Component->XYOffsetmapTexture->MarkPackageDirty();
			Component->XYOffsetmapTexture->ClearFlags(RF_Standalone);
		}

		UCyLandHeightfieldCollisionComponent* CollisionComp = Component->CollisionComponent.Get();
		if (CollisionComp)
		{
			CollisionComp->DestroyComponent();
		}
		Component->DestroyComponent();
	}

	// Remove Selection
	CyLandInfo->ClearSelectedRegion(true);
	//EdMode->SetMaskEnable(CyLand->SelectedRegion.Num());
	GEngine->BroadcastLevelActorListChanged();
}

ACyLand* FEdModeCyLand::ChangeComponentSetting(int32 NumComponentsX, int32 NumComponentsY, int32 NumSubsections, int32 SubsectionSizeQuads, bool bResample)
{
	FScopedSlowTask Progress(3, LOCTEXT("CyLandChangeComponentSetting", "Changing CyLand Component Settings..."));
	Progress.MakeDialog();
	int32 CurrentTaskProgress = 0;

	check(NumComponentsX > 0);
	check(NumComponentsY > 0);
	check(NumSubsections > 0);
	check(SubsectionSizeQuads > 0);

	const int32 NewComponentSizeQuads = NumSubsections * SubsectionSizeQuads;

	ACyLand* CyLand = NULL;

	UCyLandInfo* CyLandInfo = CurrentToolTarget.CyLandInfo.Get();
	if (ensure(CyLandInfo != NULL))
	{
		int32 OldMinX, OldMinY, OldMaxX, OldMaxY;
		if (CyLandInfo->GetCyLandExtent(OldMinX, OldMinY, OldMaxX, OldMaxY))
		{
			ACyLandProxy* OldCyLandProxy = CyLandInfo->GetCyLandProxy();

			const int32 OldVertsX = OldMaxX - OldMinX + 1;
			const int32 OldVertsY = OldMaxY - OldMinY + 1;
			const int32 NewVertsX = NumComponentsX * NewComponentSizeQuads + 1;
			const int32 NewVertsY = NumComponentsY * NewComponentSizeQuads + 1;

			TArray<uint16> HeightData;
			TArray<FCyLandImportLayerInfo> ImportLayerInfos;
			FVector CyLandOffset = FVector::ZeroVector;
			FIntPoint CyLandOffsetQuads = FIntPoint::ZeroValue;
			float CyLandScaleFactor = 1.0f;

			int32 NewMinX, NewMinY, NewMaxX, NewMaxY;

			{ // Scope to flush the texture update before doing the import
				FCyLandEditDataInterface CyLandEdit(CyLandInfo);

				if (bResample)
				{
					NewMinX = OldMinX / CyLandInfo->ComponentSizeQuads * NewComponentSizeQuads;
					NewMinY = OldMinY / CyLandInfo->ComponentSizeQuads * NewComponentSizeQuads;
					NewMaxX = NewMinX + NewVertsX - 1;
					NewMaxY = NewMinY + NewVertsY - 1;

					HeightData.AddZeroed(OldVertsX * OldVertsY * sizeof(uint16));

					// GetHeightData alters its args, so make temp copies to avoid screwing things up
					int32 TMinX = OldMinX, TMinY = OldMinY, TMaxX = OldMaxX, TMaxY = OldMaxY;
					CyLandEdit.GetHeightData(TMinX, TMinY, TMaxX, TMaxY, HeightData.GetData(), 0);

					HeightData = CyLandEditorUtils::ResampleData(HeightData,
						OldVertsX, OldVertsY, NewVertsX, NewVertsY);

					for (const FCyLandInfoLayerSettings& LayerSettings : CyLandInfo->Layers)
					{
						if (LayerSettings.LayerInfoObj != NULL)
						{
							auto ImportLayerInfo = new(ImportLayerInfos)FCyLandImportLayerInfo(LayerSettings);
							ImportLayerInfo->LayerData.AddZeroed(OldVertsX * OldVertsY * sizeof(uint8));

							TMinX = OldMinX; TMinY = OldMinY; TMaxX = OldMaxX; TMaxY = OldMaxY;
							CyLandEdit.GetWeightData(LayerSettings.LayerInfoObj, TMinX, TMinY, TMaxX, TMaxY, ImportLayerInfo->LayerData.GetData(), 0);

							ImportLayerInfo->LayerData = CyLandEditorUtils::ResampleData(ImportLayerInfo->LayerData,
								OldVertsX, OldVertsY,
								NewVertsX, NewVertsY);
						}
					}

					CyLandScaleFactor = (float)OldCyLandProxy->ComponentSizeQuads / NewComponentSizeQuads;
				}
				else
				{
					NewMinX = OldMinX + (OldVertsX - NewVertsX) / 2;
					NewMinY = OldMinY + (OldVertsY - NewVertsY) / 2;
					NewMaxX = NewMinX + NewVertsX - 1;
					NewMaxY = NewMinY + NewVertsY - 1;
					const int32 RequestedMinX = FMath::Max(OldMinX, NewMinX);
					const int32 RequestedMinY = FMath::Max(OldMinY, NewMinY);
					const int32 RequestedMaxX = FMath::Min(OldMaxX, NewMaxX);
					const int32 RequestedMaxY = FMath::Min(OldMaxY, NewMaxY);

					const int32 RequestedVertsX = RequestedMaxX - RequestedMinX + 1;
					const int32 RequestedVertsY = RequestedMaxY - RequestedMinY + 1;

					HeightData.AddZeroed(RequestedVertsX * RequestedVertsY * sizeof(uint16));

					// GetHeightData alters its args, so make temp copies to avoid screwing things up
					int32 TMinX = RequestedMinX, TMinY = RequestedMinY, TMaxX = RequestedMaxX, TMaxY = RequestedMaxY;
					CyLandEdit.GetHeightData(TMinX, TMinY, TMaxX, OldMaxY, HeightData.GetData(), 0);

					HeightData = CyLandEditorUtils::ExpandData(HeightData,
						RequestedMinX, RequestedMinY, RequestedMaxX, RequestedMaxY,
						NewMinX, NewMinY, NewMaxX, NewMaxY);

					for (const FCyLandInfoLayerSettings& LayerSettings : CyLandInfo->Layers)
					{
						if (LayerSettings.LayerInfoObj != NULL)
						{
							auto ImportLayerInfo = new(ImportLayerInfos)FCyLandImportLayerInfo(LayerSettings);
							ImportLayerInfo->LayerData.AddZeroed(NewVertsX * NewVertsY * sizeof(uint8));

							TMinX = RequestedMinX; TMinY = RequestedMinY; TMaxX = RequestedMaxX; TMaxY = RequestedMaxY;
							CyLandEdit.GetWeightData(LayerSettings.LayerInfoObj, TMinX, TMinY, TMaxX, TMaxY, ImportLayerInfo->LayerData.GetData(), 0);

							ImportLayerInfo->LayerData = CyLandEditorUtils::ExpandData(ImportLayerInfo->LayerData,
								RequestedMinX, RequestedMinY, RequestedMaxX, RequestedMaxY,
								NewMinX, NewMinY, NewMaxX, NewMaxY);
						}
					}

					// offset CyLand to component boundary
					CyLandOffset = FVector(NewMinX, NewMinY, 0) * OldCyLandProxy->GetActorScale();
					CyLandOffsetQuads = FIntPoint(NewMinX, NewMinY);
					NewMinX = 0;
					NewMinY = 0;
					NewMaxX = NewVertsX - 1;
					NewMaxY = NewVertsY - 1;
				}
			}

			Progress.EnterProgressFrame(CurrentTaskProgress++);

			const FVector Location = OldCyLandProxy->GetActorLocation() + CyLandOffset;
			FActorSpawnParameters SpawnParams;
			SpawnParams.OverrideLevel = OldCyLandProxy->GetLevel();
			CyLand = OldCyLandProxy->GetWorld()->SpawnActor<ACyLand>(Location, OldCyLandProxy->GetActorRotation(), SpawnParams);

			const FVector OldScale = OldCyLandProxy->GetActorScale();
			CyLand->SetActorRelativeScale3D(FVector(OldScale.X * CyLandScaleFactor, OldScale.Y * CyLandScaleFactor, OldScale.Z));

			CyLand->CyLandMaterial = OldCyLandProxy->CyLandMaterial;
			CyLand->CyLandMaterialsOverride = OldCyLandProxy->CyLandMaterialsOverride;
			CyLand->CollisionMipLevel = OldCyLandProxy->CollisionMipLevel;
			CyLand->Imports(FGuid::NewGuid(), NewMinX, NewMinY, NewMaxX, NewMaxY, NumSubsections, SubsectionSizeQuads, HeightData.GetData(), *OldCyLandProxy->ReimportHeightmapFilePath, ImportLayerInfos, ECyLandImportAlphamapType::Additive);

			CyLand->MaxLODLevel = OldCyLandProxy->MaxLODLevel;
			CyLand->LODDistanceFactor_DEPRECATED = OldCyLandProxy->LODDistanceFactor_DEPRECATED;
			CyLand->LODFalloff_DEPRECATED = OldCyLandProxy->LODFalloff_DEPRECATED;
			CyLand->TessellationComponentScreenSize = OldCyLandProxy->TessellationComponentScreenSize;
			CyLand->ComponentScreenSizeToUseSubSections = OldCyLandProxy->ComponentScreenSizeToUseSubSections;
			CyLand->UseTessellationComponentScreenSizeFalloff = OldCyLandProxy->UseTessellationComponentScreenSizeFalloff;
			CyLand->TessellationComponentScreenSizeFalloff = OldCyLandProxy->TessellationComponentScreenSizeFalloff;
			CyLand->LODDistributionSetting = OldCyLandProxy->LODDistributionSetting;
			CyLand->LOD0DistributionSetting = OldCyLandProxy->LOD0DistributionSetting;
			CyLand->OccluderGeometryLOD = OldCyLandProxy->OccluderGeometryLOD;
			CyLand->ExportLOD = OldCyLandProxy->ExportLOD;
			CyLand->StaticLightingLOD = OldCyLandProxy->StaticLightingLOD;
			CyLand->NegativeZBoundsExtension = OldCyLandProxy->NegativeZBoundsExtension;
			CyLand->PositiveZBoundsExtension = OldCyLandProxy->PositiveZBoundsExtension;
			CyLand->DefaultPhysMaterial = OldCyLandProxy->DefaultPhysMaterial;
			CyLand->StreamingDistanceMultiplier = OldCyLandProxy->StreamingDistanceMultiplier;
			CyLand->CyLandHoleMaterial = OldCyLandProxy->CyLandHoleMaterial;
			CyLand->StaticLightingResolution = OldCyLandProxy->StaticLightingResolution;
			CyLand->bCastStaticShadow = OldCyLandProxy->bCastStaticShadow;
			CyLand->bCastShadowAsTwoSided = OldCyLandProxy->bCastShadowAsTwoSided;
			CyLand->LightingChannels = OldCyLandProxy->LightingChannels;
			CyLand->bRenderCustomDepth = OldCyLandProxy->bRenderCustomDepth;
			CyLand->CustomDepthStencilValue = OldCyLandProxy->CustomDepthStencilValue;
			CyLand->LightmassSettings = OldCyLandProxy->LightmassSettings;
			CyLand->CollisionThickness = OldCyLandProxy->CollisionThickness;
			CyLand->BodyInstance.SetCollisionProfileName(OldCyLandProxy->BodyInstance.GetCollisionProfileName());
			if (CyLand->BodyInstance.DoesUseCollisionProfile() == false)
			{
				CyLand->BodyInstance.SetCollisionEnabled(OldCyLandProxy->BodyInstance.GetCollisionEnabled());
				CyLand->BodyInstance.SetObjectType(OldCyLandProxy->BodyInstance.GetObjectType());
				CyLand->BodyInstance.SetResponseToChannels(OldCyLandProxy->BodyInstance.GetResponseToChannels());
			}
			CyLand->EditorLayerSettings = OldCyLandProxy->EditorLayerSettings;
			CyLand->bUsedForNavigation = OldCyLandProxy->bUsedForNavigation;
			CyLand->MaxPaintedLayersPerComponent = OldCyLandProxy->MaxPaintedLayersPerComponent;

			CyLand->CreateCyLandInfo();

			// Clone CyLand splines
			TLazyObjectPtr<ACyLand> OldCyLandActor = CyLandInfo->CyLandActor;
			if (OldCyLandActor.IsValid() && OldCyLandActor->SplineComponent != NULL)
			{
				UCyLandSplinesComponent* OldSplines = OldCyLandActor->SplineComponent;
				UCyLandSplinesComponent* NewSplines = DuplicateObject<UCyLandSplinesComponent>(OldSplines, CyLand, OldSplines->GetFName());
				NewSplines->AttachToComponent(CyLand->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);

				const FVector OldSplineScale = OldSplines->GetRelativeTransform().GetScale3D();
				NewSplines->SetRelativeScale3D(FVector(OldSplineScale.X / CyLandScaleFactor, OldSplineScale.Y / CyLandScaleFactor, OldSplineScale.Z));
				CyLand->SplineComponent = NewSplines;
				NewSplines->RegisterComponent();

				// TODO: Foliage on spline meshes
			}

			Progress.EnterProgressFrame(CurrentTaskProgress++);

			if (bResample)
			{
				// Remap foliage to the resampled components
				UCyLandInfo* NewCyLandInfo = CyLand->GetCyLandInfo();
				for (const TPair<FIntPoint, UCyLandComponent*>& Entry : CyLandInfo->XYtoComponentMap)
				{
					UCyLandComponent* NewComponent = NewCyLandInfo->XYtoComponentMap.FindRef(Entry.Key);
					if (NewComponent)
					{
						UCyLandHeightfieldCollisionComponent* OldCollisionComponent = Entry.Value->CollisionComponent.Get();
						UCyLandHeightfieldCollisionComponent* NewCollisionComponent = NewComponent->CollisionComponent.Get();

						if (OldCollisionComponent && NewCollisionComponent)
						{
							AInstancedFoliageActor::MoveInstancesToNewComponent(OldCollisionComponent->GetWorld(), OldCollisionComponent, NewCollisionComponent);
							NewCollisionComponent->SnapFoliageInstances(FBox(FVector(-WORLD_MAX), FVector(WORLD_MAX)));
						}
					}
				}

				Progress.EnterProgressFrame(CurrentTaskProgress++);

				// delete any components that were deleted in the original
				TSet<UCyLandComponent*> ComponentsToDelete;
				for (const TPair<FIntPoint, UCyLandComponent*>& Entry : NewCyLandInfo->XYtoComponentMap)
				{
					if (!CyLandInfo->XYtoComponentMap.Contains(Entry.Key))
					{
						ComponentsToDelete.Add(Entry.Value);
					}
				}
				if (ComponentsToDelete.Num() > 0)
				{
					DeleteCyLandComponents(NewCyLandInfo, ComponentsToDelete);
				}
			}
			else
			{
				UCyLandInfo* NewCyLandInfo = CyLand->GetCyLandInfo();

				// Move instances
				for (const TPair<FIntPoint, UCyLandComponent*>& OldEntry : CyLandInfo->XYtoComponentMap)
				{
					UCyLandHeightfieldCollisionComponent* OldCollisionComponent = OldEntry.Value->CollisionComponent.Get();

					if (OldCollisionComponent)
					{
						UWorld* World = OldCollisionComponent->GetWorld();

						for (const TPair<FIntPoint, UCyLandComponent*>& NewEntry : NewCyLandInfo->XYtoComponentMap)
						{
							UCyLandHeightfieldCollisionComponent* NewCollisionComponent = NewEntry.Value->CollisionComponent.Get();

							if (NewCollisionComponent && FBoxSphereBounds::BoxesIntersect(NewCollisionComponent->Bounds, OldCollisionComponent->Bounds))
							{
								FBox Box = NewCollisionComponent->Bounds.GetBox();
								Box.Min.Z = -WORLD_MAX;
								Box.Max.Z = WORLD_MAX;

								AInstancedFoliageActor::MoveInstancesToNewComponent(World, OldCollisionComponent, Box, NewCollisionComponent);
							}
						}
					}
				}

				// Snap them to the bounds
				for (const TPair<FIntPoint, UCyLandComponent*>& NewEntry : NewCyLandInfo->XYtoComponentMap)
				{
					UCyLandHeightfieldCollisionComponent* NewCollisionComponent = NewEntry.Value->CollisionComponent.Get();

					if (NewCollisionComponent)
					{
						FBox Box = NewCollisionComponent->Bounds.GetBox();
						Box.Min.Z = -WORLD_MAX;
						Box.Max.Z = WORLD_MAX;

						NewCollisionComponent->SnapFoliageInstances(Box);
					}
				}

				Progress.EnterProgressFrame(CurrentTaskProgress++);

				// delete any components that are in areas that were entirely deleted in the original
				TSet<UCyLandComponent*> ComponentsToDelete;
				for (const TPair<FIntPoint, UCyLandComponent*>& Entry : NewCyLandInfo->XYtoComponentMap)
				{
					float OldX = Entry.Key.X * NewComponentSizeQuads + CyLandOffsetQuads.X;
					float OldY = Entry.Key.Y * NewComponentSizeQuads + CyLandOffsetQuads.Y;
					TSet<UCyLandComponent*> OverlapComponents;
					CyLandInfo->GetComponentsInRegion(OldX, OldY, OldX + NewComponentSizeQuads, OldY + NewComponentSizeQuads, OverlapComponents, false);
					if (OverlapComponents.Num() == 0)
					{
						ComponentsToDelete.Add(Entry.Value);
					}
				}
				if (ComponentsToDelete.Num() > 0)
				{
					DeleteCyLandComponents(NewCyLandInfo, ComponentsToDelete);
				}
			}

			// Delete the old CyLand and all its proxies
			for (ACyLandStreamingProxy* Proxy : TActorRange<ACyLandStreamingProxy>(OldCyLandProxy->GetWorld()))
			{
				if (Proxy->CyLandActor == OldCyLandActor)
				{
					Proxy->Destroy();
				}
			}
			OldCyLandProxy->Destroy();
		}
	}

	GEditor->RedrawLevelEditingViewports();

	return CyLand;
}

ECyLandEditingState FEdModeCyLand::GetEditingState() const
{
	UWorld* World = GetWorld();

	if (GEditor->bIsSimulatingInEditor)
	{
		return ECyLandEditingState::SIEWorld;
	}
	else if (GEditor->PlayWorld != NULL)
	{
		return ECyLandEditingState::PIEWorld;
	}
	else if (World == nullptr)
	{
		return ECyLandEditingState::Unknown;
	}
	else if (World->FeatureLevel < ERHIFeatureLevel::SM4)
	{
		return ECyLandEditingState::BadFeatureLevel;
	}
	else if (NewCyLandPreviewMode == ENewCyLandPreviewMode::None &&
		!CurrentToolTarget.CyLandInfo.IsValid())
	{
		return ECyLandEditingState::NoCyLand;
	}

	return ECyLandEditingState::Enabled;
}

int32 FEdModeCyLand::GetProceduralLayerCount() const
{
	if (CurrentToolTarget.CyLandInfo == nullptr)
	{
		return 0;
	}

	ACyLand* CyLand = CurrentToolTarget.CyLandInfo->CyLandActor.Get();

	if (CyLand == nullptr)
	{
		return 0;
	}

	return CyLand->ProceduralLayers.Num();
}

void FEdModeCyLand::SetCurrentProceduralLayer(int32 InLayerIndex)
{
	CurrentToolTarget.CurrentProceduralLayerIndex = InLayerIndex;

	RefreshDetailPanel();
}

int32 FEdModeCyLand::GetCurrentProceduralLayerIndex() const
{
	return CurrentToolTarget.CurrentProceduralLayerIndex;
}

FName FEdModeCyLand::GetProceduralLayerName(int32 InLayerIndex) const
{
	if (CurrentToolTarget.CyLandInfo == nullptr)
	{
		return NAME_None;
	}

	ACyLand* CyLand = CurrentToolTarget.CyLandInfo->CyLandActor.Get();

	if (CyLand == nullptr || !CyLand->ProceduralLayers.IsValidIndex(InLayerIndex))
	{
		return NAME_None;
	}

	return CyLand->ProceduralLayers[InLayerIndex].Name;
}

FName FEdModeCyLand::GetCurrentProceduralLayerName() const
{
	return GetProceduralLayerName(CurrentToolTarget.CurrentProceduralLayerIndex);
}

void FEdModeCyLand::SetProceduralLayerName(int32 InLayerIndex, const FName& InName)
{
	if (CurrentToolTarget.CyLandInfo == nullptr)
	{
		return;
	}

	ACyLand* CyLand = CurrentToolTarget.CyLandInfo->CyLandActor.Get();

	if (CyLand == nullptr || !CyLand->ProceduralLayers.IsValidIndex(InLayerIndex))
	{
		return;
	}

	CyLand->ProceduralLayers[InLayerIndex].Name = InName;
}

float FEdModeCyLand::GetProceduralLayerWeight(int32 InLayerIndex) const
{
	if (CurrentToolTarget.CyLandInfo == nullptr)
	{
		return 1.0f;
	}

	ACyLand* CyLand = CurrentToolTarget.CyLandInfo->CyLandActor.Get();

	if (CyLand == nullptr || !CyLand->ProceduralLayers.IsValidIndex(InLayerIndex))
	{
		return 1.0f;
	}

	return CyLand->ProceduralLayers[InLayerIndex].Weight;
}

void FEdModeCyLand::SetProceduralLayerWeight(float InWeight, int32 InLayerIndex)
{
	if (CurrentToolTarget.CyLandInfo == nullptr)
	{
		return;
	}

	ACyLand* CyLand = CurrentToolTarget.CyLandInfo->CyLandActor.Get();

	if (CyLand == nullptr || !CyLand->ProceduralLayers.IsValidIndex(InLayerIndex))
	{
		return;
	}

	CyLand->ProceduralLayers[InLayerIndex].Weight = InWeight;

	CyLand->RequestProceduralContentUpdate(EProceduralContentUpdateFlag::Heightmap_All);
}

void FEdModeCyLand::SetProceduralLayerVisibility(bool InVisible, int32 InLayerIndex)
{
	if (CurrentToolTarget.CyLandInfo == nullptr)
	{
		return;
	}

	ACyLand* CyLand = CurrentToolTarget.CyLandInfo->CyLandActor.Get();

	if (CyLand == nullptr || !CyLand->ProceduralLayers.IsValidIndex(InLayerIndex))
	{
		return;
	}

	CyLand->ProceduralLayers[InLayerIndex].Visible = InVisible;

	CyLand->RequestProceduralContentUpdate(EProceduralContentUpdateFlag::Heightmap_All);
}

bool FEdModeCyLand::IsProceduralLayerVisible(int32 InLayerIndex) const
{
	if (CurrentToolTarget.CyLandInfo == nullptr)
	{
		return true;
	}

	ACyLand* CyLand = CurrentToolTarget.CyLandInfo->CyLandActor.Get();

	if (CyLand == nullptr || !CyLand->ProceduralLayers.IsValidIndex(InLayerIndex))
	{
		return true;
	}

	return CyLand->ProceduralLayers[InLayerIndex].Visible;
}

void FEdModeCyLand::AddBrushToCurrentProceduralLayer(int32 InTargetType, ACyLandBlueprintCustomBrush* InBrush)
{
	if (CurrentToolTarget.CyLandInfo == nullptr || !CurrentToolTarget.CyLandInfo->CyLandActor.IsValid())
	{
		return;
	}

	ACyLand* CyLand = CurrentToolTarget.CyLandInfo->CyLandActor.Get();

	FCyProceduralLayer* Layer = GetCurrentProceduralLayer();

	if (Layer == nullptr)
	{
		return;
	}

	int32 AddedIndex = Layer->Brushes.Add(FCyLandProceduralLayerBrush(InBrush));

	if (InTargetType == ECyLandToolTargetType::Type::Heightmap)
	{
		Layer->HeightmapBrushOrderIndices.Add(AddedIndex);
	}
	else
	{
		Layer->WeightmapBrushOrderIndices.Add(AddedIndex);
	}

	InBrush->SetOwningCyLand(CyLand);

	CyLand->RequestProceduralContentUpdate(InTargetType == ECyLandToolTargetType::Type::Heightmap ? EProceduralContentUpdateFlag::Heightmap_All : EProceduralContentUpdateFlag::Weightmap_All);
}

void FEdModeCyLand::RequestProceduralContentUpdate()
{
	if (CurrentToolTarget.CyLandInfo == nullptr || !CurrentToolTarget.CyLandInfo->CyLandActor.IsValid())
	{
		return;
	}

	ACyLand* CyLand = CurrentToolTarget.CyLandInfo->CyLandActor.Get();

	CyLand->RequestProceduralContentUpdate(CurrentToolTarget.TargetType == ECyLandToolTargetType::Type::Heightmap ? EProceduralContentUpdateFlag::Heightmap_All : EProceduralContentUpdateFlag::Weightmap_All);	
}

void FEdModeCyLand::RemoveBrushFromCurrentProceduralLayer(int32 InTargetType, ACyLandBlueprintCustomBrush* InBrush)
{
	if (CurrentToolTarget.CyLandInfo == nullptr || !CurrentToolTarget.CyLandInfo->CyLandActor.IsValid())
	{
		return;
	}

	ACyLand* CyLand = CurrentToolTarget.CyLandInfo->CyLandActor.Get();

	FCyProceduralLayer* Layer = GetCurrentProceduralLayer();

	if (Layer == nullptr)
	{
		return;
	}

	int32 IndexToRemove = INDEX_NONE;
	for (int32 i = 0; i < Layer->Brushes.Num(); ++i)
	{
		if (Layer->Brushes[i].BPCustomBrush == InBrush)
		{
			IndexToRemove = i;
			break;
		}
	}

	if (IndexToRemove != INDEX_NONE)
	{
		Layer->Brushes.RemoveAt(IndexToRemove);

		if (InTargetType == ECyLandToolTargetType::Type::Heightmap)
		{
			for (int32 i = 0; i < Layer->HeightmapBrushOrderIndices.Num(); ++i)
			{
				if (Layer->HeightmapBrushOrderIndices[i] == IndexToRemove)
				{
					// Update the value of the index of all the one after the one we removed, so index still correctly match actual brushes list
					for (int32 j = 0; j < Layer->HeightmapBrushOrderIndices.Num(); ++j)
					{
						if (Layer->HeightmapBrushOrderIndices[j] > IndexToRemove)
						{
							--Layer->HeightmapBrushOrderIndices[j];
						}
					}

					Layer->HeightmapBrushOrderIndices.RemoveAt(i);
					break;
				}
			}
		}
		else
		{
			for (int32 i = 0; i < Layer->WeightmapBrushOrderIndices.Num(); ++i)
			{
				if (Layer->WeightmapBrushOrderIndices[i] == IndexToRemove)
				{
					// Update the value of the index of all the one after the one we removed, so index still correctly match actual brushes list
					for (int32 j = 0; j < Layer->WeightmapBrushOrderIndices.Num(); ++j)
					{
						if (Layer->WeightmapBrushOrderIndices[j] > IndexToRemove)
						{
							--Layer->HeightmapBrushOrderIndices[j];
						}
					}

					Layer->WeightmapBrushOrderIndices.RemoveAt(i);
					break;
				}
			}
		}

		InBrush->SetOwningCyLand(nullptr);
	}

	CyLand->RequestProceduralContentUpdate(InTargetType == ECyLandToolTargetType::Type::Heightmap ? EProceduralContentUpdateFlag::Heightmap_All : EProceduralContentUpdateFlag::Weightmap_All);
}

bool FEdModeCyLand::AreAllBrushesCommitedToCurrentProceduralLayer(int32 InTargetType)
{
	FCyProceduralLayer* Layer = GetCurrentProceduralLayer();

	if (Layer == nullptr)
	{
		return false;
	}

	for (FCyLandProceduralLayerBrush& Brush : Layer->Brushes)
	{
		if (!Brush.BPCustomBrush->IsCommited() 
			&& ((InTargetType == ECyLandToolTargetType::Type::Heightmap && Brush.BPCustomBrush->IsAffectingHeightmap()) || (InTargetType == ECyLandToolTargetType::Type::Weightmap && Brush.BPCustomBrush->IsAffectingWeightmap())))
		{
			return false;
		}
	}

	return true;	
}

void FEdModeCyLand::SetCurrentProceduralLayerBrushesCommitState(int32 InTargetType, bool InCommited)
{
	FCyProceduralLayer* Layer = GetCurrentProceduralLayer();

	if (Layer == nullptr)
	{
		return;
	}

	for (FCyLandProceduralLayerBrush& Brush : Layer->Brushes)
	{
		Brush.BPCustomBrush->SetCommitState(InCommited);
	}

	GEngine->BroadcastLevelActorListChanged();
}

TArray<int8>& FEdModeCyLand::GetBrushesOrderForCurrentProceduralLayer(int32 InTargetType) const
{
	FCyProceduralLayer* Layer = GetCurrentProceduralLayer();
	check(Layer);

	if (InTargetType == ECyLandToolTargetType::Type::Heightmap)
	{
		return Layer->HeightmapBrushOrderIndices;
	}
	else
	{
		return Layer->WeightmapBrushOrderIndices;
	}
}

ACyLandBlueprintCustomBrush* FEdModeCyLand::GetBrushForCurrentProceduralLayer(int32 InTargetType, int8 InBrushIndex) const
{
	FCyProceduralLayer* Layer = GetCurrentProceduralLayer();

	if (Layer == nullptr)
	{
		return nullptr;
	}

	if (InTargetType == ECyLandToolTargetType::Type::Heightmap)
	{
		if (Layer->HeightmapBrushOrderIndices.IsValidIndex(InBrushIndex))
		{
			int8 ActualBrushIndex = Layer->HeightmapBrushOrderIndices[InBrushIndex];
			if (Layer->Brushes.IsValidIndex(ActualBrushIndex))
			{
				return Layer->Brushes[ActualBrushIndex].BPCustomBrush;
			}
		}
	}
	else
	{
		if (Layer->WeightmapBrushOrderIndices.IsValidIndex(InBrushIndex))
		{
			int8 ActualBrushIndex = Layer->WeightmapBrushOrderIndices[InBrushIndex];
			if (Layer->Brushes.IsValidIndex(ActualBrushIndex))
			{
				return Layer->Brushes[ActualBrushIndex].BPCustomBrush;
			}
		}
	}

	return nullptr;
}

TArray<ACyLandBlueprintCustomBrush*> FEdModeCyLand::GetBrushesForCurrentProceduralLayer(int32 InTargetType)
{
	TArray<ACyLandBlueprintCustomBrush*> Brushes;

	FCyProceduralLayer* Layer = GetCurrentProceduralLayer();

	if (Layer == nullptr)
	{
		return Brushes;
	}

	Brushes.Reserve(Layer->Brushes.Num());

	for (const FCyLandProceduralLayerBrush& Brush : Layer->Brushes)
	{
		if ((Brush.BPCustomBrush->IsAffectingHeightmap() && InTargetType == ECyLandToolTargetType::Type::Heightmap) 
			|| (Brush.BPCustomBrush->IsAffectingWeightmap() && InTargetType == ECyLandToolTargetType::Type::Weightmap))
		{
			Brushes.Add(Brush.BPCustomBrush);
		}
	}

	return Brushes;
}

FCyProceduralLayer* FEdModeCyLand::GetCurrentProceduralLayer() const
{
	if (!CurrentToolTarget.CyLandInfo.IsValid())
	{
		return nullptr;
	}

	ACyLand* CyLand = CurrentToolTarget.CyLandInfo->CyLandActor.Get();

	if (CyLand == nullptr)
	{
		return nullptr;
	}

	FName CurrentLayerName = GetCurrentProceduralLayerName();

	if (CurrentLayerName == NAME_None)
	{
		return nullptr;
	}

	for (FCyProceduralLayer& Layer : CyLand->ProceduralLayers)
	{
		if (Layer.Name == CurrentLayerName)
		{
			return &Layer;
		}
	}

	return nullptr;
}

void FEdModeCyLand::ChangeHeightmapsToCurrentProceduralLayerHeightmaps(bool InResetCurrentEditingHeightmap)
{
	if (!CurrentToolTarget.CyLandInfo.IsValid() || !CurrentToolTarget.CyLandInfo->CyLandActor.IsValid())
	{
		return;
	}

	TArray<ACyLandProxy*> AllCyLands;
	AllCyLands.Add(CurrentToolTarget.CyLandInfo->CyLandActor.Get());

	for (const auto& It : CurrentToolTarget.CyLandInfo->Proxies)
	{
		AllCyLands.Add(It);
	}

	FName CurrentLayerName = GetCurrentProceduralLayerName();

	if (CurrentLayerName == NAME_None)
	{
		return;
	}

	for (ACyLandProxy* CyLandProxy : AllCyLands)
	{
		FCyProceduralLayerData* CurrentLayerData = CyLandProxy->ProceduralLayersData.Find(CurrentLayerName);

		if (CurrentLayerData == nullptr)
		{
			continue;
		}

		for (UCyLandComponent* Component : CyLandProxy->CyLandComponents)
		{
			if (InResetCurrentEditingHeightmap)
			{
				Component->SetCurrentEditingHeightmap(nullptr);
			}
			else
			{
				UTexture2D** LayerHeightmap = CurrentLayerData->Heightmaps.Find(Component->GetHeightmap());

				if (LayerHeightmap != nullptr)
				{
					Component->SetCurrentEditingHeightmap(*LayerHeightmap);
				}
			}

			Component->MarkRenderStateDirty();
		}
	}
}

void FEdModeCyLand::OnLevelActorAdded(AActor* InActor)
{
	ACyLandBlueprintCustomBrush* Brush = Cast<ACyLandBlueprintCustomBrush>(InActor);

	if (Brush != nullptr && Brush->GetTypedOuter<UPackage>() != GetTransientPackage())
	{
		AddBrushToCurrentProceduralLayer(CurrentToolTarget.TargetType, Brush);
		RefreshDetailPanel();
	}
}

void FEdModeCyLand::OnLevelActorRemoved(AActor* InActor)
{
	ACyLandBlueprintCustomBrush* Brush = Cast<ACyLandBlueprintCustomBrush>(InActor);

	if (Brush != nullptr && Brush->GetTypedOuter<UPackage>() != GetTransientPackage())
	{
		RemoveBrushFromCurrentProceduralLayer(CurrentToolTarget.TargetType, Brush);
		RefreshDetailPanel();
	}
}

bool CyLandEditorUtils::SetHeightmapData(ACyLandProxy* CyLand, const TArray<uint16>& Data)
{
	FIntRect ComponentsRect = CyLand->GetBoundingRect() + CyLand->CyLandSectionOffset;

	if (Data.Num() == (1 + ComponentsRect.Width())*(1 + ComponentsRect.Height()))
	{
		FHeightmapAccessor<false> HeightmapAccessor(CyLand->GetCyLandInfo());
		HeightmapAccessor.SetData(ComponentsRect.Min.X, ComponentsRect.Min.Y, ComponentsRect.Max.X, ComponentsRect.Max.Y, Data.GetData());
		return true;
	}

	return false;
}

bool CyLandEditorUtils::SetWeightmapData(ACyLandProxy* CyLand, UCyLandLayerInfoObject* LayerObject, const TArray<uint8>& Data)
{
	FIntRect ComponentsRect = CyLand->GetBoundingRect() + CyLand->CyLandSectionOffset;

	if (Data.Num() == (1 + ComponentsRect.Width())*(1 + ComponentsRect.Height()))
	{
		FAlphamapAccessor<false, true> AlphamapAccessor(CyLand->GetCyLandInfo(), LayerObject);
		AlphamapAccessor.SetData(ComponentsRect.Min.X, ComponentsRect.Min.Y, ComponentsRect.Max.X, ComponentsRect.Max.Y, Data.GetData(), ECyLandLayerPaintingRestriction::None);
		return true;
	}

	return false;
}

FName FCyLandTargetListInfo::GetLayerName() const
{
	return LayerInfoObj.IsValid() ? LayerInfoObj->LayerName : LayerName;
}

#undef LOCTEXT_NAMESPACE
