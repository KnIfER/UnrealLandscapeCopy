// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CyLandEditorDetailCustomization_ProceduralBrushStack.h"
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
#include "Widgets/Input/SButton.h"
#include "EditorModeManager.h"
#include "CyLandEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "IDetailPropertyRow.h"
#include "DetailCategoryBuilder.h"
#include "PropertyCustomizationHelpers.h"

#include "ScopedTransaction.h"

#include "CyLandEditorDetailCustomization_TargetLayers.h"
#include "Widgets/Input/SEditableText.h"
#include "CyLandBPCustomBrush.h"

#define LOCTEXT_NAMESPACE "CyLandEditor.Layers"

TSharedRef<IDetailCustomization> FCyLandEditorDetailCustomization_ProceduralBrushStack::MakeInstance()
{
	return MakeShareable(new FCyLandEditorDetailCustomization_ProceduralBrushStack);
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void FCyLandEditorDetailCustomization_ProceduralBrushStack::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& LayerCategory = DetailBuilder.EditCategory("Current Layer Brushes");

	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode && CyLandEdMode->CurrentToolMode != nullptr)
	{
		const FName CurrentToolName = CyLandEdMode->CurrentTool->GetToolName();

		if (CyLandEdMode->CurrentToolMode->SupportedTargetTypes != 0 && CurrentToolName == TEXT("BPCustom"))
		{
			LayerCategory.AddCustomBuilder(MakeShareable(new FCyLandEditorCustomNodeBuilder_ProceduralBrushStack(DetailBuilder.GetThumbnailPool().ToSharedRef())));
		}
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

//////////////////////////////////////////////////////////////////////////

FEdModeCyLand* FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::GetEditorMode()
{
	return (FEdModeCyLand*)GLevelEditorModeTools().GetActiveMode(ICyLandEditorModule::EM_Landscape_Mimic);
}

FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::FCyLandEditorCustomNodeBuilder_ProceduralBrushStack(TSharedRef<FAssetThumbnailPool> InThumbnailPool)
	: ThumbnailPool(InThumbnailPool)
{
}

FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::~FCyLandEditorCustomNodeBuilder_ProceduralBrushStack()
{
	
}

void FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::SetOnRebuildChildren(FSimpleDelegate InOnRegenerateChildren)
{
}

void FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::GenerateHeaderRowContent(FDetailWidgetRow& NodeRow)
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
			.Text(FText::FromString(TEXT("Stack")))
		];
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::GenerateChildContent(IDetailChildrenBuilder& ChildrenBuilder)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != NULL)
	{
		TSharedPtr<SDragAndDropVerticalBox> BrushesList = SNew(SDragAndDropVerticalBox)
			.OnCanAcceptDrop(this, &FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::HandleCanAcceptDrop)
			.OnAcceptDrop(this, &FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::HandleAcceptDrop)
			.OnDragDetected(this, &FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::HandleDragDetected);

		BrushesList->SetDropIndicator_Above(*FEditorStyle::GetBrush("CyLandEditor.TargetList.DropZone.Above"));
		BrushesList->SetDropIndicator_Below(*FEditorStyle::GetBrush("CyLandEditor.TargetList.DropZone.Below"));

		ChildrenBuilder.AddCustomRow(FText::FromString(FString(TEXT("Brush Stack"))))
			.Visibility(EVisibility::Visible)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Center)
				.Padding(0, 2)
				[
					BrushesList.ToSharedRef()
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Center)
				.Padding(0, 2)
				[
					SNew(SHorizontalBox)

					+SHorizontalBox::Slot()
					.HAlign(HAlign_Right)
					//.Padding(4, 0)
					[
						SNew(SButton)
						.Text(this, &FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::GetCommitBrushesButtonText)
						.OnClicked(this, &FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::ToggleCommitBrushes)
						.IsEnabled(this, &FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::IsCommitBrushesButtonEnabled)
					]
				]
			];

		if (CyLandEdMode->CurrentToolMode != nullptr)
		{
			const TArray<int8>& BrushOrderStack = CyLandEdMode->GetBrushesOrderForCurrentProceduralLayer(CyLandEdMode->CurrentToolTarget.TargetType);

			for (int32 i = 0; i < BrushOrderStack.Num(); ++i)
			{
				TSharedPtr<SWidget> GeneratedRowWidget = GenerateRow(i);

				if (GeneratedRowWidget.IsValid())
				{
					BrushesList->AddSlot()
						.AutoHeight()
						[
							GeneratedRowWidget.ToSharedRef()
						];
				}
			}
		}
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedPtr<SWidget> FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::GenerateRow(int32 InBrushIndex)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	TSharedPtr<SWidget> RowWidget = SNew(SCyLandEditorSelectableBorder)
		.Padding(0)
		.VAlign(VAlign_Center)
		.OnSelected(this, &FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::OnBrushSelectionChanged, InBrushIndex)
		.IsSelected(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::IsBrushSelected, InBrushIndex)))
		[	
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(4, 0)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Center)
				.Padding(0, 2)
				[
					SNew(STextBlock)
					.ColorAndOpacity(TAttribute<FSlateColor>::Create(TAttribute<FSlateColor>::FGetter::CreateSP(this, &FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::GetBrushTextColor, InBrushIndex)))
					.Text(this, &FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::GetBrushText, InBrushIndex)
				]
			]
		];
	
	return RowWidget;
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

bool FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::IsBrushSelected(int32 InBrushIndex) const
{
	ACyLandBlueprintCustomBrush* Brush = GetBrush(InBrushIndex);

	return Brush != nullptr ? Brush->IsSelected() : false;
}

void FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::OnBrushSelectionChanged(int32 InBrushIndex)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();

	if (CyLandEdMode != nullptr && CyLandEdMode->AreAllBrushesCommitedToCurrentProceduralLayer(CyLandEdMode->CurrentToolTarget.TargetType))
	{
		return;
	}

	ACyLandBlueprintCustomBrush* Brush = GetBrush(InBrushIndex);

	if (Brush != nullptr && !Brush->IsCommited())
	{
		GEditor->SelectNone(true, true);
		GEditor->SelectActor(Brush, true, true);
	}
}

FText FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::GetBrushText(int32 InBrushIndex) const
{
	ACyLandBlueprintCustomBrush* Brush = GetBrush(InBrushIndex);

	if (Brush != nullptr)
	{
		return FText::FromString(Brush->GetActorLabel());
	}

	return FText::FromName(NAME_None);
}

FSlateColor FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::GetBrushTextColor(int32 InBrushIndex) const
{
	ACyLandBlueprintCustomBrush* Brush = GetBrush(InBrushIndex);

	if (Brush != nullptr)
	{
		return Brush->IsCommited() ? FSlateColor::UseSubduedForeground() : FSlateColor::UseForeground();
	}

	return FSlateColor::UseSubduedForeground();
}

ACyLandBlueprintCustomBrush* FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::GetBrush(int32 InBrushIndex) const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();

	if (CyLandEdMode != nullptr)
	{
		return CyLandEdMode->GetBrushForCurrentProceduralLayer(CyLandEdMode->CurrentToolTarget.TargetType, InBrushIndex);
	}

	return nullptr;
}

FReply FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::ToggleCommitBrushes()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();

	if (CyLandEdMode != nullptr)
	{
		bool CommitBrushes = !CyLandEdMode->AreAllBrushesCommitedToCurrentProceduralLayer(CyLandEdMode->CurrentToolTarget.TargetType);

		if (CommitBrushes)
		{
			TArray<ACyLandBlueprintCustomBrush*> BrushStack = CyLandEdMode->GetBrushesForCurrentProceduralLayer(CyLandEdMode->CurrentToolTarget.TargetType);

			for (ACyLandBlueprintCustomBrush* Brush : BrushStack)
			{
				GEditor->SelectActor(Brush, false, true);
			}
		}

		CyLandEdMode->SetCurrentProceduralLayerBrushesCommitState(CyLandEdMode->CurrentToolTarget.TargetType, CommitBrushes);
	}

	return FReply::Handled();
}

bool FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::IsCommitBrushesButtonEnabled() const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();

	if (CyLandEdMode != nullptr)
	{
		TArray<ACyLandBlueprintCustomBrush*> BrushStack = CyLandEdMode->GetBrushesForCurrentProceduralLayer(CyLandEdMode->CurrentToolTarget.TargetType);

		return BrushStack.Num() > 0;
	}

	return false;
}

FText FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::GetCommitBrushesButtonText() const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();

	if (CyLandEdMode != nullptr)
	{
		return CyLandEdMode->AreAllBrushesCommitedToCurrentProceduralLayer(CyLandEdMode->CurrentToolTarget.TargetType) ? LOCTEXT("UnCommitBrushesText", "Uncommit") : LOCTEXT("CommitBrushesText", "Commit");
	}

	return FText::FromName(NAME_None);
}

FReply FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, int32 SlotIndex, SVerticalBox::FSlot* Slot)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();

	if (CyLandEdMode != nullptr)
	{
		const TArray<int8>& BrushOrderStack = CyLandEdMode->GetBrushesOrderForCurrentProceduralLayer(CyLandEdMode->CurrentToolTarget.TargetType);

		if (BrushOrderStack.IsValidIndex(SlotIndex))
		{
			TSharedPtr<SWidget> Row = GenerateRow(SlotIndex);

			if (Row.IsValid())
			{
				return FReply::Handled().BeginDragDrop(FCyLandBrushDragDropOp::New(SlotIndex, Slot, Row));
			}
		}
	}

	return FReply::Unhandled();
}

TOptional<SDragAndDropVerticalBox::EItemDropZone> FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone DropZone, SVerticalBox::FSlot* Slot)
{
	TSharedPtr<FCyLandBrushDragDropOp> DragDropOperation = DragDropEvent.GetOperationAs<FCyLandBrushDragDropOp>();

	if (DragDropOperation.IsValid())
	{
		return DropZone;
	}

	return TOptional<SDragAndDropVerticalBox::EItemDropZone>();
}

FReply FCyLandEditorCustomNodeBuilder_ProceduralBrushStack::HandleAcceptDrop(FDragDropEvent const& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone DropZone, int32 SlotIndex, SVerticalBox::FSlot* Slot)
{
	TSharedPtr<FCyLandBrushDragDropOp> DragDropOperation = DragDropEvent.GetOperationAs<FCyLandBrushDragDropOp>();

	if (DragDropOperation.IsValid())
	{
		FEdModeCyLand* CyLandEdMode = GetEditorMode();

		if (CyLandEdMode != nullptr)
		{
			TArray<int8>& BrushOrderStack = CyLandEdMode->GetBrushesOrderForCurrentProceduralLayer(CyLandEdMode->CurrentToolTarget.TargetType);

			if (BrushOrderStack.IsValidIndex(DragDropOperation->SlotIndexBeingDragged) && BrushOrderStack.IsValidIndex(SlotIndex))
			{
				int32 StartingLayerIndex = DragDropOperation->SlotIndexBeingDragged;
				int32 DestinationLayerIndex = SlotIndex;

				if (StartingLayerIndex != INDEX_NONE && DestinationLayerIndex != INDEX_NONE)
				{
					int8 MovingBrushIndex = BrushOrderStack[StartingLayerIndex];
					 
					BrushOrderStack.RemoveAt(StartingLayerIndex);
					BrushOrderStack.Insert(MovingBrushIndex, DestinationLayerIndex);

					CyLandEdMode->RefreshDetailPanel();
					CyLandEdMode->RequestProceduralContentUpdate();

					return FReply::Handled();
				}
			}
		}
	}

	return FReply::Unhandled();
}

TSharedRef<FCyLandBrushDragDropOp> FCyLandBrushDragDropOp::New(int32 InSlotIndexBeingDragged, SVerticalBox::FSlot* InSlotBeingDragged, TSharedPtr<SWidget> WidgetToShow)
{
	TSharedRef<FCyLandBrushDragDropOp> Operation = MakeShareable(new FCyLandBrushDragDropOp);

	Operation->MouseCursor = EMouseCursor::GrabHandClosed;
	Operation->SlotIndexBeingDragged = InSlotIndexBeingDragged;
	Operation->SlotBeingDragged = InSlotBeingDragged;
	Operation->WidgetToShow = WidgetToShow;

	Operation->Construct();

	return Operation;
}

FCyLandBrushDragDropOp::~FCyLandBrushDragDropOp()
{
}

TSharedPtr<SWidget> FCyLandBrushDragDropOp::GetDefaultDecorator() const
{
	return SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("ContentBrowser.AssetDragDropTooltipBackground"))
		.Content()
		[
			WidgetToShow.ToSharedRef()
		];

}

#undef LOCTEXT_NAMESPACE