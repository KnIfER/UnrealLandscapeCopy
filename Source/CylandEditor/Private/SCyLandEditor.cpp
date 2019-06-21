// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SCyLandEditor.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Layout/SBorder.h"
#include "Modules/ModuleManager.h"
#include "SlateOptMacros.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SErrorText.h"
#include "EditorStyleSet.h"
#include "EditorModeManager.h"
#include "CyLandEditorModule.h"
#include "CyLandEditorCommands.h"
#include "CyLandEditorObject.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "IIntroTutorials.h"

#define LOCTEXT_NAMESPACE "CyLandEditor"

void SCyLandAssetThumbnail::Construct(const FArguments& InArgs, UObject* Asset, TSharedRef<FAssetThumbnailPool> ThumbnailPool)
{
	FIntPoint ThumbnailSize = InArgs._ThumbnailSize;

	AssetThumbnail = MakeShareable(new FAssetThumbnail(Asset, ThumbnailSize.X, ThumbnailSize.Y, ThumbnailPool));

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(ThumbnailSize.X)
		.HeightOverride(ThumbnailSize.Y)
		[
			AssetThumbnail->MakeThumbnailWidget()
		]
	];

	UMaterialInterface* MaterialInterface = Cast<UMaterialInterface>(Asset);
	if (MaterialInterface)
	{
		UMaterial::OnMaterialCompilationFinished().AddSP(this, &SCyLandAssetThumbnail::OnMaterialCompilationFinished);
	}
}

SCyLandAssetThumbnail::~SCyLandAssetThumbnail()
{
	UMaterial::OnMaterialCompilationFinished().RemoveAll(this);
}

void SCyLandAssetThumbnail::OnMaterialCompilationFinished(UMaterialInterface* MaterialInterface)
{
	UMaterialInterface* MaterialAsset = Cast<UMaterialInterface>(AssetThumbnail->GetAsset());
	if (MaterialAsset)
	{
		if (MaterialAsset->IsDependent(MaterialInterface))
		{
			// Refresh thumbnail
			AssetThumbnail->SetAsset(AssetThumbnail->GetAsset());
		}
	}
}

void SCyLandAssetThumbnail::SetAsset(UObject* Asset)
{
	AssetThumbnail->SetAsset(Asset);
}

//////////////////////////////////////////////////////////////////////////

void FCyLandToolKit::RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager)
{
}

void FCyLandToolKit::UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager)
{
}

void FCyLandToolKit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
	auto NameToCommandMap = FCyLandEditorCommands::Get().NameToCommandMap;

	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	TSharedRef<FUICommandList> CommandList = CyLandEdMode->GetUICommandList();

#define MAP_MODE(ModeName) CommandList->MapAction(NameToCommandMap.FindChecked(ModeName), FUIAction(FExecuteAction::CreateSP(this, &FCyLandToolKit::OnChangeMode, FName(ModeName)), FCanExecuteAction::CreateSP(this, &FCyLandToolKit::IsModeEnabled, FName(ModeName)), FIsActionChecked::CreateSP(this, &FCyLandToolKit::IsModeActive, FName(ModeName))));
	MAP_MODE("ToolMode_Manage");
	MAP_MODE("ToolMode_Sculpt");
	MAP_MODE("ToolMode_Paint");
#undef MAP_MODE

#define MAP_TOOL(ToolName) CommandList->MapAction(NameToCommandMap.FindChecked("Tool_" ToolName), FUIAction(FExecuteAction::CreateSP(this, &FCyLandToolKit::OnChangeTool, FName(ToolName)), FCanExecuteAction::CreateSP(this, &FCyLandToolKit::IsToolEnabled, FName(ToolName)), FIsActionChecked::CreateSP(this, &FCyLandToolKit::IsToolActive, FName(ToolName))));
	MAP_TOOL("NewCyLand");
	MAP_TOOL("ResizeCyLand");

	MAP_TOOL("Sculpt");
	MAP_TOOL("Paint");
	MAP_TOOL("Smooth");
	MAP_TOOL("Flatten");
	MAP_TOOL("Ramp");
	MAP_TOOL("Erosion");
	MAP_TOOL("HydraErosion");
	MAP_TOOL("Noise");
	MAP_TOOL("Retopologize");
	MAP_TOOL("Visibility");
	MAP_TOOL("BPCustom");

	MAP_TOOL("Select");
	MAP_TOOL("AddComponent");
	MAP_TOOL("DeleteComponent");
	MAP_TOOL("MoveToLevel");

	MAP_TOOL("Mask");
	MAP_TOOL("CopyPaste");
	MAP_TOOL("Mirror");

	MAP_TOOL("Splines");
#undef MAP_TOOL

#define MAP_BRUSH_SET(BrushSetName) CommandList->MapAction(NameToCommandMap.FindChecked(BrushSetName), FUIAction(FExecuteAction::CreateSP(this, &FCyLandToolKit::OnChangeBrushSet, FName(BrushSetName)), FCanExecuteAction::CreateSP(this, &FCyLandToolKit::IsBrushSetEnabled, FName(BrushSetName)), FIsActionChecked::CreateSP(this, &FCyLandToolKit::IsBrushSetActive, FName(BrushSetName))));
	MAP_BRUSH_SET("BrushSet_Circle");
	MAP_BRUSH_SET("BrushSet_Alpha");
	MAP_BRUSH_SET("BrushSet_Pattern");
	MAP_BRUSH_SET("BrushSet_Component");
	MAP_BRUSH_SET("BrushSet_Gizmo");
#undef MAP_BRUSH_SET

#define MAP_BRUSH(BrushName) CommandList->MapAction(NameToCommandMap.FindChecked(BrushName), FUIAction(FExecuteAction::CreateSP(this, &FCyLandToolKit::OnChangeBrush, FName(BrushName)), FCanExecuteAction(), FIsActionChecked::CreateSP(this, &FCyLandToolKit::IsBrushActive, FName(BrushName))));
	MAP_BRUSH("Circle_Smooth");
	MAP_BRUSH("Circle_Linear");
	MAP_BRUSH("Circle_Spherical");
	MAP_BRUSH("Circle_Tip");
#undef MAP_BRUSH

	CyLandEditorWidgets = SNew(SCyLandEditor, SharedThis(this));

	FModeToolkit::Init(InitToolkitHost);
}

FName FCyLandToolKit::GetToolkitFName() const
{
	return FName("CyLandEditor");
}

FText FCyLandToolKit::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "CyLand Editor");
}

FEdModeCyLand* FCyLandToolKit::GetEditorMode() const
{
	return (FEdModeCyLand*)GLevelEditorModeTools().GetActiveMode(ICyLandEditorModule::EM_Landscape_Mimic);
}

TSharedPtr<SWidget> FCyLandToolKit::GetInlineContent() const
{
	return CyLandEditorWidgets;
}

void FCyLandToolKit::NotifyToolChanged()
{
	CyLandEditorWidgets->NotifyToolChanged();
}

void FCyLandToolKit::NotifyBrushChanged()
{
	CyLandEditorWidgets->NotifyBrushChanged();
}

void FCyLandToolKit::RefreshDetailPanel()
{
	CyLandEditorWidgets->RefreshDetailPanel();
}

void FCyLandToolKit::OnChangeMode(FName ModeName)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode)
	{
		CyLandEdMode->SetCurrentToolMode(ModeName);
	}
}

bool FCyLandToolKit::IsModeEnabled(FName ModeName) const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode)
	{
		// Manage is the only mode enabled if we have no landscape
		if (ModeName == "ToolMode_Manage" || CyLandEdMode->GetCyLandList().Num() > 0)
		{
			return true;
		}
	}

	return false;
}

bool FCyLandToolKit::IsModeActive(FName ModeName) const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode && CyLandEdMode->CurrentTool)
	{
		return CyLandEdMode->CurrentToolMode->ToolModeName == ModeName;
	}

	return false;
}

void FCyLandToolKit::OnChangeTool(FName ToolName)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		CyLandEdMode->SetCurrentTool(ToolName);
	}
}

bool FCyLandToolKit::IsToolEnabled(FName ToolName) const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		if (ToolName == "NewCyLand" || CyLandEdMode->GetCyLandList().Num() > 0)
		{
			return true;
		}
	}

	return false;
}

bool FCyLandToolKit::IsToolActive(FName ToolName) const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr && CyLandEdMode->CurrentTool != nullptr)
	{
		const FName CurrentToolName = CyLandEdMode->CurrentTool->GetToolName();
		return CurrentToolName == ToolName;
	}

	return false;
}

void FCyLandToolKit::OnChangeBrushSet(FName BrushSetName)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		CyLandEdMode->SetCurrentBrushSet(BrushSetName);
	}
}

bool FCyLandToolKit::IsBrushSetEnabled(FName BrushSetName) const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode && CyLandEdMode->CurrentTool)
	{
		return CyLandEdMode->CurrentTool->ValidBrushes.Contains(BrushSetName);
	}

	return false;
}

bool FCyLandToolKit::IsBrushSetActive(FName BrushSetName) const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode && CyLandEdMode->CurrentBrushSetIndex >= 0)
	{
		const FName CurrentBrushSetName = CyLandEdMode->CyLandBrushSets[CyLandEdMode->CurrentBrushSetIndex].BrushSetName;
		return CurrentBrushSetName == BrushSetName;
	}

	return false;
}

void FCyLandToolKit::OnChangeBrush(FName BrushName)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		CyLandEdMode->SetCurrentBrush(BrushName);
	}
}

bool FCyLandToolKit::IsBrushActive(FName BrushName) const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode && CyLandEdMode->CurrentBrush)
	{
		const FName CurrentBrushName = CyLandEdMode->CurrentBrush->GetBrushName();
		return CurrentBrushName == BrushName;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SCyLandEditor::Construct(const FArguments& InArgs, TSharedRef<FCyLandToolKit> InParentToolkit)
{
	TSharedRef<FUICommandList> CommandList = InParentToolkit->GetToolkitCommands();

	// Modes:
	FToolBarBuilder ModeSwitchButtons(CommandList, FMultiBoxCustomization::None);
	{
		ModeSwitchButtons.AddToolBarButton(FCyLandEditorCommands::Get().ManageMode, NAME_None, LOCTEXT("Mode.Manage", "Manage"), LOCTEXT("Mode.Manage.Tooltip", "Contains tools to add a new landscape, import/export landscape, add/remove components and manage streaming"));
		ModeSwitchButtons.AddToolBarButton(FCyLandEditorCommands::Get().SculptMode, NAME_None, LOCTEXT("Mode.Sculpt", "Sculpt"), LOCTEXT("Mode.Sculpt.Tooltip", "Contains tools that modify the shape of a landscape"));
		ModeSwitchButtons.AddToolBarButton(FCyLandEditorCommands::Get().PaintMode,  NAME_None, LOCTEXT("Mode.Paint",  "Paint"),  LOCTEXT("Mode.Paint.Tooltip",  "Contains tools that paint materials on to a landscape"));
	}

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs(false, false, false,FDetailsViewArgs::HideNameArea);

	DetailsPanel = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsPanel->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateSP(this, &SCyLandEditor::GetIsPropertyVisible));

	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode)
	{
		DetailsPanel->SetObject(CyLandEdMode->UISettings);
	}

	IIntroTutorials& IntroTutorials = FModuleManager::LoadModuleChecked<IIntroTutorials>(TEXT("IntroTutorials"));

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 5)
		[
			SAssignNew(Error, SErrorText)
		]
		+ SVerticalBox::Slot()
		.Padding(0)
		[
			SNew(SVerticalBox)
			.IsEnabled(this, &SCyLandEditor::GetCyLandEditorIsEnabled)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 0, 4, 5)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SBorder)
					.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
					.HAlign(HAlign_Center)
					[

						ModeSwitchButtons.MakeWidget()
					]
				]

				// Tutorial link
				+ SOverlay::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Bottom)
				.Padding(4)
				[
					IntroTutorials.CreateTutorialsWidget(TEXT("CyLandMode"))
				]
			]
			+ SVerticalBox::Slot()
			.Padding(0)
			[
				DetailsPanel.ToSharedRef()
			]
		]
	];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

FEdModeCyLand* SCyLandEditor::GetEditorMode() const
{
	return (FEdModeCyLand*)GLevelEditorModeTools().GetActiveMode(ICyLandEditorModule::EM_Landscape_Mimic);
}

FText SCyLandEditor::GetErrorText() const
{
	const FEdModeCyLand* CyLandEdMode = GetEditorMode();
	ECyLandEditingState EditState = CyLandEdMode->GetEditingState();
	switch (EditState)
	{
		case ECyLandEditingState::SIEWorld:
		{

			if (CyLandEdMode->NewCyLandPreviewMode != ENewCyLandPreviewMode::None)
			{
				return LOCTEXT("IsSimulatingError_create", "Can't create landscape while simulating!");
			}
			else
			{
				return LOCTEXT("IsSimulatingError_edit", "Can't edit landscape while simulating!");
			}
			break;
		}
		case ECyLandEditingState::PIEWorld:
		{
			if (CyLandEdMode->NewCyLandPreviewMode != ENewCyLandPreviewMode::None)
			{
				return LOCTEXT("IsPIEError_create", "Can't create landscape in PIE!");
			}
			else
			{
				return LOCTEXT("IsPIEError_edit", "Can't edit landscape in PIE!");
			}
			break;
		}
		case ECyLandEditingState::BadFeatureLevel:
		{
			if (CyLandEdMode->NewCyLandPreviewMode != ENewCyLandPreviewMode::None)
			{
				return LOCTEXT("IsFLError_create", "Can't create landscape with a feature level less than SM4!");
			}
			else
			{
				return LOCTEXT("IsFLError_edit", "Can't edit landscape with a feature level less than SM4!");
			}
			break;
		}
		case ECyLandEditingState::NoCyLand:
		{
			return LOCTEXT("NoCyLandError", "No CyLand!");
		}
		case ECyLandEditingState::Enabled:
		{
			return FText::GetEmpty();
		}
		default:
			checkNoEntry();
	}

	return FText::GetEmpty();
}

bool SCyLandEditor::GetCyLandEditorIsEnabled() const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode)
	{
		Error->SetError(GetErrorText());
		return CyLandEdMode->GetEditingState() == ECyLandEditingState::Enabled;
	}
	return false;
}

bool SCyLandEditor::GetIsPropertyVisible(const FPropertyAndParent& PropertyAndParent) const
{
	const UProperty& Property = PropertyAndParent.Property;

	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr && CyLandEdMode->CurrentTool != nullptr)
	{
		if (Property.HasMetaData("ShowForMask"))
		{
			const bool bMaskEnabled = CyLandEdMode->CurrentTool &&
				CyLandEdMode->CurrentTool->SupportsMask() &&
				CyLandEdMode->CurrentToolTarget.CyLandInfo.IsValid() &&
				CyLandEdMode->CurrentToolTarget.CyLandInfo->SelectedRegion.Num() > 0;

			if (bMaskEnabled)
			{
				return true;
			}
		}
		if (Property.HasMetaData("ShowForTools"))
		{
			const FName CurrentToolName = CyLandEdMode->CurrentTool->GetToolName();

			TArray<FString> ShowForTools;
			Property.GetMetaData("ShowForTools").ParseIntoArray(ShowForTools, TEXT(","), true);
			if (!ShowForTools.Contains(CurrentToolName.ToString()))
			{
				return false;
			}
		}
		if (Property.HasMetaData("ShowForBrushes"))
		{
			const FName CurrentBrushSetName = CyLandEdMode->CyLandBrushSets[CyLandEdMode->CurrentBrushSetIndex].BrushSetName;
			// const FName CurrentBrushName = CyLandEdMode->CurrentBrush->GetBrushName();

			TArray<FString> ShowForBrushes;
			Property.GetMetaData("ShowForBrushes").ParseIntoArray(ShowForBrushes, TEXT(","), true);
			if (!ShowForBrushes.Contains(CurrentBrushSetName.ToString()))
				//&& !ShowForBrushes.Contains(CurrentBrushName.ToString())
			{
				return false;
			}
		}
		if (Property.HasMetaData("ShowForTargetTypes"))
		{
			static const TCHAR* TargetTypeNames[] = { TEXT("Heightmap"), TEXT("Weightmap"), TEXT("Visibility") };

			TArray<FString> ShowForTargetTypes;
			Property.GetMetaData("ShowForTargetTypes").ParseIntoArray(ShowForTargetTypes, TEXT(","), true);

			const ECyLandToolTargetType::Type CurrentTargetType = CyLandEdMode->CurrentToolTarget.TargetType;
			if (CurrentTargetType == ECyLandToolTargetType::Invalid ||
				ShowForTargetTypes.FindByKey(TargetTypeNames[CurrentTargetType]) == nullptr)
			{
				return false;
			}
		}
		if (Property.HasMetaData("ShowForBPCustomTool"))
		{
			const FName CurrentToolName = CyLandEdMode->CurrentTool->GetToolName();

			if (CurrentToolName != TEXT("BPCustom"))
			{
				return false;
			}
		}

		return true;
	}

	return false;
}

void SCyLandEditor::NotifyToolChanged()
{
	RefreshDetailPanel();
}

void SCyLandEditor::NotifyBrushChanged()
{
	RefreshDetailPanel();
}

void SCyLandEditor::RefreshDetailPanel()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode)
	{
		// Refresh details panel
		DetailsPanel->SetObject(CyLandEdMode->UISettings, true);
	}
}

#undef LOCTEXT_NAMESPACE
