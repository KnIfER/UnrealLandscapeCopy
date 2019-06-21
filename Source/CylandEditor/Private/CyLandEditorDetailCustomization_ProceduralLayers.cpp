// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CyLandEditorDetailCustomization_ProceduralLayers.h"
#include "IDetailChildrenBuilder.h"
#include "Framework/Commands/UIAction.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "Brushes/SlateColorBrush.h"
#include "Layout/WidgetPath.h"
#include "SlateOptMacros.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SErrorText.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "EditorModeManager.h"
#include "EditorModes.h"
#include "CyLandEditorModule.h"
#include "CyLandEditorObject.h"

#include "DetailLayoutBuilder.h"
#include "IDetailPropertyRow.h"
#include "DetailCategoryBuilder.h"
#include "PropertyCustomizationHelpers.h"

#include "SCyLandEditor.h"
#include "Dialogs/DlgPickAssetPath.h"
#include "ObjectTools.h"
#include "ScopedTransaction.h"
#include "DesktopPlatformModule.h"
#include "AssetRegistryModule.h"

#include "CyLandRender.h"
#include "Materials/MaterialExpressionCyLandVisibilityMask.h"
#include "CyLandEdit.h"
#include "IDetailGroup.h"
#include "Widgets/SBoxPanel.h"
#include "Editor/EditorStyle/Private/SlateEditorStyle.h"
#include "CyLandEditorDetailCustomization_TargetLayers.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SNumericEntryBox.h"

#define LOCTEXT_NAMESPACE "CyLandEditor.Layers"

TSharedRef<IDetailCustomization> FCyLandEditorDetailCustomization_ProceduralLayers::MakeInstance()
{
	return MakeShareable(new FCyLandEditorDetailCustomization_ProceduralLayers);
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void FCyLandEditorDetailCustomization_ProceduralLayers::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& LayerCategory = DetailBuilder.EditCategory("Procedural Layers");

	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode && CyLandEdMode->CurrentToolMode != nullptr)
	{
		const FName CurrentToolName = CyLandEdMode->CurrentTool->GetToolName();

		if (CyLandEdMode->CurrentToolMode->SupportedTargetTypes != 0)
		{
			LayerCategory.AddCustomBuilder(MakeShareable(new FCyLandEditorCustomNodeBuilder_ProceduralLayers(DetailBuilder.GetThumbnailPool().ToSharedRef())));
		}
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

//////////////////////////////////////////////////////////////////////////

FEdModeCyLand* FCyLandEditorCustomNodeBuilder_ProceduralLayers::GetEditorMode()
{
	return (FEdModeCyLand*)GLevelEditorModeTools().GetActiveMode(ICyLandEditorModule::EM_Landscape_Mimic);
}

FCyLandEditorCustomNodeBuilder_ProceduralLayers::FCyLandEditorCustomNodeBuilder_ProceduralLayers(TSharedRef<FAssetThumbnailPool> InThumbnailPool)
	: ThumbnailPool(InThumbnailPool)
{
}

FCyLandEditorCustomNodeBuilder_ProceduralLayers::~FCyLandEditorCustomNodeBuilder_ProceduralLayers()
{
}

void FCyLandEditorCustomNodeBuilder_ProceduralLayers::SetOnRebuildChildren(FSimpleDelegate InOnRegenerateChildren)
{
}

void FCyLandEditorCustomNodeBuilder_ProceduralLayers::GenerateHeaderRowContent(FDetailWidgetRow& NodeRow)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	
	if (CyLandEdMode == NULL)
	{
		return;	
	}

	NodeRow.NameWidget
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(FText::FromString(TEXT("")))
		];
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void FCyLandEditorCustomNodeBuilder_ProceduralLayers::GenerateChildContent(IDetailChildrenBuilder& ChildrenBuilder)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != NULL)
	{
		TSharedPtr<SDragAndDropVerticalBox> LayerList = SNew(SDragAndDropVerticalBox)
			.OnCanAcceptDrop(this, &FCyLandEditorCustomNodeBuilder_ProceduralLayers::HandleCanAcceptDrop)
			.OnAcceptDrop(this, &FCyLandEditorCustomNodeBuilder_ProceduralLayers::HandleAcceptDrop)
			.OnDragDetected(this, &FCyLandEditorCustomNodeBuilder_ProceduralLayers::HandleDragDetected);

		LayerList->SetDropIndicator_Above(*FEditorStyle::GetBrush("CyLandEditor.TargetList.DropZone.Above"));
		LayerList->SetDropIndicator_Below(*FEditorStyle::GetBrush("CyLandEditor.TargetList.DropZone.Below"));

		ChildrenBuilder.AddCustomRow(FText::FromString(FString(TEXT("Procedural Layers"))))
			.Visibility(EVisibility::Visible)
			[
				LayerList.ToSharedRef()
			];

		for (int32 i = 0; i < CyLandEdMode->GetProceduralLayerCount(); ++i)
		{
			TSharedPtr<SWidget> GeneratedRowWidget = GenerateRow(i);

			if (GeneratedRowWidget.IsValid())
			{
				LayerList->AddSlot()
					.AutoHeight()
					[
						GeneratedRowWidget.ToSharedRef()
					];
			}
		}
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedPtr<SWidget> FCyLandEditorCustomNodeBuilder_ProceduralLayers::GenerateRow(int32 InLayerIndex)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	TSharedPtr<SWidget> RowWidget = SNew(SCyLandEditorSelectableBorder)
		.Padding(0)
		.VAlign(VAlign_Center)
		//.OnContextMenuOpening_Static(&FCyLandEditorCustomNodeBuilder_Layers::OnTargetLayerContextMenuOpening, Target)
		.OnSelected(this, &FCyLandEditorCustomNodeBuilder_ProceduralLayers::OnLayerSelectionChanged, InLayerIndex)
		.IsSelected(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &FCyLandEditorCustomNodeBuilder_ProceduralLayers::IsLayerSelected, InLayerIndex)))
		.Visibility(EVisibility::Visible)
		[
			SNew(SHorizontalBox)
			
			/*+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(2))
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush(TEXT("CyLandEditor.Target_Heightmap")))
			]
			*/

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.FillWidth(1.0f)
			.Padding(4, 0)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Center)
				.Padding(0, 2)
				.HAlign(HAlign_Left)				
				[
					SNew(SEditableText)
					.SelectAllTextWhenFocused(true)
					.IsReadOnly(true)
					.Text(this, &FCyLandEditorCustomNodeBuilder_ProceduralLayers::GetLayerText, InLayerIndex)
					.ToolTipText(LOCTEXT("FCyLandEditorCustomNodeBuilder_ProceduralLayers_tooltip", "Name of the Layer"))
					.OnTextCommitted(FOnTextCommitted::CreateSP(this, &FCyLandEditorCustomNodeBuilder_ProceduralLayers::OnLayerTextCommitted, InLayerIndex))
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(0, 2)
			.HAlign(HAlign_Center)				
			[
				SNew(SCheckBox)
				.OnCheckStateChanged(this, &FCyLandEditorCustomNodeBuilder_ProceduralLayers::OnLayerVisibilityChanged, InLayerIndex)
				.IsChecked(TAttribute<ECheckBoxState>::Create(TAttribute<ECheckBoxState>::FGetter::CreateSP(this, &FCyLandEditorCustomNodeBuilder_ProceduralLayers::IsLayerVisible, InLayerIndex)))
				.ToolTipText(LOCTEXT("FCyLandEditorCustomNodeBuilder_ProceduralLayerVisibility_Tooltips", "Is layer visible"))
				.Content()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("FCyLandEditorCustomNodeBuilder_ProceduralLayerVisibility", "Visibility"))
				]
			]
			+ SHorizontalBox::Slot()
			.Padding(0)
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FCyLandEditorCustomNodeBuilder_ProceduralLayerWeight", "Weight"))
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(0, 2)
			.HAlign(HAlign_Left)
			.FillWidth(1.0f)
			[
				SNew(SNumericEntryBox<float>)
				.AllowSpin(true)
				.MinValue(0.0f)
				.MaxValue(65536.0f)
				.MaxSliderValue(65536.0f)
				.MinDesiredValueWidth(25.0f)
				.Value(this, &FCyLandEditorCustomNodeBuilder_ProceduralLayers::GetLayerWeight, InLayerIndex)
				.OnValueChanged(this, &FCyLandEditorCustomNodeBuilder_ProceduralLayers::SetLayerWeight, InLayerIndex)
				.IsEnabled(true)
			]			
		];	

	return RowWidget;
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void FCyLandEditorCustomNodeBuilder_ProceduralLayers::OnLayerTextCommitted(const FText& InText, ETextCommit::Type InCommitType, int32 InLayerIndex)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();

	if (CyLandEdMode != nullptr)
	{
		CyLandEdMode->SetProceduralLayerName(InLayerIndex, *InText.ToString());
	}
}

FText FCyLandEditorCustomNodeBuilder_ProceduralLayers::GetLayerText(int32 InLayerIndex) const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();

	if (CyLandEdMode != nullptr)
	{
		return FText::FromName(CyLandEdMode->GetProceduralLayerName(InLayerIndex));
	}

	return FText::FromString(TEXT("None"));
}

bool FCyLandEditorCustomNodeBuilder_ProceduralLayers::IsLayerSelected(int32 InLayerIndex)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode)
	{
		return CyLandEdMode->GetCurrentProceduralLayerIndex() == InLayerIndex;
	}

	return false;
}

void FCyLandEditorCustomNodeBuilder_ProceduralLayers::OnLayerSelectionChanged(int32 InLayerIndex)
{	
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode)
	{
		CyLandEdMode->SetCurrentProceduralLayer(InLayerIndex);
		CyLandEdMode->UpdateTargetList();
	}
}

TOptional<float> FCyLandEditorCustomNodeBuilder_ProceduralLayers::GetLayerWeight(int32 InLayerIndex) const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();

	if (CyLandEdMode)
	{
		return CyLandEdMode->GetProceduralLayerWeight(InLayerIndex);
	}

	return 1.0f;
}

void FCyLandEditorCustomNodeBuilder_ProceduralLayers::SetLayerWeight(float InWeight, int32 InLayerIndex)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();

	if (CyLandEdMode)
	{
		CyLandEdMode->SetProceduralLayerWeight(InWeight, InLayerIndex);
	}
}

void FCyLandEditorCustomNodeBuilder_ProceduralLayers::OnLayerVisibilityChanged(ECheckBoxState NewState, int32 InLayerIndex)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();

	if (CyLandEdMode)
	{
		CyLandEdMode->SetProceduralLayerVisibility(NewState == ECheckBoxState::Checked, InLayerIndex);
	}
}

ECheckBoxState FCyLandEditorCustomNodeBuilder_ProceduralLayers::IsLayerVisible(int32 InLayerIndex) const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();

	if (CyLandEdMode)
	{
		return CyLandEdMode->IsProceduralLayerVisible(InLayerIndex) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	return ECheckBoxState::Unchecked;
}

FReply FCyLandEditorCustomNodeBuilder_ProceduralLayers::HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, int32 SlotIndex, SVerticalBox::FSlot* Slot)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();

	if (CyLandEdMode != nullptr)
	{
		// TODO: handle drag & drop
	}

	return FReply::Unhandled();
}

TOptional<SDragAndDropVerticalBox::EItemDropZone> FCyLandEditorCustomNodeBuilder_ProceduralLayers::HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone DropZone, SVerticalBox::FSlot* Slot)
{
	// TODO: handle drag & drop
	return TOptional<SDragAndDropVerticalBox::EItemDropZone>();
}

FReply FCyLandEditorCustomNodeBuilder_ProceduralLayers::HandleAcceptDrop(FDragDropEvent const& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone DropZone, int32 SlotIndex, SVerticalBox::FSlot* Slot)
{
	// TODO: handle drag & drop
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
