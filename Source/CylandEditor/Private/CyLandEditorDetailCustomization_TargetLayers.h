// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "Layout/Visibility.h"
#include "Layout/Margin.h"
#include "Styling/SlateColor.h"
#include "Input/Reply.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Private/CyLandEdMode.h"
#include "IDetailCustomNodeBuilder.h"
#include "IDetailCustomization.h"
#include "AssetThumbnail.h"
#include "Framework/SlateDelegates.h"
#include "Private/CyLandEditorDetailCustomization_Base.h"

class FDetailWidgetRow;
class IDetailChildrenBuilder;
class IDetailLayoutBuilder;
class SDragAndDropVerticalBox;

/**
 * Slate widgets customizer for the target layers list in the CyLand Editor
 */

class FCyLandEditorDetailCustomization_TargetLayers : public FCyLandEditorDetailCustomization_Base
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

protected:
	static bool ShouldShowTargetLayers();
	static bool ShouldShowPaintingRestriction();
	static EVisibility GetVisibility_PaintingRestriction();
	static bool ShouldShowVisibilityTip();
	static EVisibility GetVisibility_VisibilityTip();
};

class FCyLandEditorCustomNodeBuilder_TargetLayers : public IDetailCustomNodeBuilder, public TSharedFromThis<FCyLandEditorCustomNodeBuilder_TargetLayers>
{
public:
	FCyLandEditorCustomNodeBuilder_TargetLayers(TSharedRef<FAssetThumbnailPool> ThumbnailPool, TSharedRef<IPropertyHandle> InTargetDisplayOrderPropertyHandle, TSharedRef<IPropertyHandle> InTargetShowUnusedLayersPropertyHandle);
	~FCyLandEditorCustomNodeBuilder_TargetLayers();

	virtual void SetOnRebuildChildren( FSimpleDelegate InOnRegenerateChildren ) override;
	virtual void GenerateHeaderRowContent( FDetailWidgetRow& NodeRow ) override;
	virtual void GenerateChildContent( IDetailChildrenBuilder& ChildrenBuilder ) override;
	virtual void Tick( float DeltaTime ) override {}
	virtual bool RequiresTick() const override { return false; }
	virtual bool InitiallyCollapsed() const override { return false; }
	virtual FName GetName() const override { return "TargetLayers"; }

protected:
	TSharedRef<FAssetThumbnailPool> ThumbnailPool;
	TSharedRef<IPropertyHandle> TargetDisplayOrderPropertyHandle;
	TSharedRef<IPropertyHandle> TargetShowUnusedLayersPropertyHandle;

	static class FEdModeCyLand* GetEditorMode();

	TSharedPtr<SWidget> GenerateRow(const TSharedRef<FCyLandTargetListInfo> Target);

	static bool GetTargetLayerIsSelected(const TSharedRef<FCyLandTargetListInfo> Target);
	static void OnTargetSelectionChanged(const TSharedRef<FCyLandTargetListInfo> Target);
	static TSharedPtr<SWidget> OnTargetLayerContextMenuOpening(const TSharedRef<FCyLandTargetListInfo> Target);
	static void OnExportLayer(const TSharedRef<FCyLandTargetListInfo> Target);
	static void OnImportLayer(const TSharedRef<FCyLandTargetListInfo> Target);
	static void OnReimportLayer(const TSharedRef<FCyLandTargetListInfo> Target);
	static void OnFillLayer(const TSharedRef<FCyLandTargetListInfo> Target);
	static void FillEmptyLayers(UCyLandInfo* CyLandInfo, UCyLandLayerInfoObject* CyLandInfoObject);
	static void OnClearLayer(const TSharedRef<FCyLandTargetListInfo> Target);
	static void OnRebuildMICs(const TSharedRef<FCyLandTargetListInfo> Target);
	static bool ShouldFilterLayerInfo(const struct FAssetData& AssetData, FName LayerName);
	static void OnTargetLayerSetObject(const FAssetData& AssetData, const TSharedRef<FCyLandTargetListInfo> Target);
	static EVisibility GetTargetLayerInfoSelectorVisibility(const TSharedRef<FCyLandTargetListInfo> Target);
	static bool GetTargetLayerCreateEnabled(const TSharedRef<FCyLandTargetListInfo> Target);
	static EVisibility GetTargetLayerMakePublicVisibility(const TSharedRef<FCyLandTargetListInfo> Target);
	static EVisibility GetTargetLayerDeleteVisibility(const TSharedRef<FCyLandTargetListInfo> Target);
	static TSharedRef<SWidget> OnGetTargetLayerCreateMenu(const TSharedRef<FCyLandTargetListInfo> Target);
	static void OnTargetLayerCreateClicked(const TSharedRef<FCyLandTargetListInfo> Target, bool bNoWeightBlend);
	static FReply OnTargetLayerMakePublicClicked(const TSharedRef<FCyLandTargetListInfo> Target);
	static FReply OnTargetLayerDeleteClicked(const TSharedRef<FCyLandTargetListInfo> Target);
	static FSlateColor GetLayerUsageDebugColor(const TSharedRef<FCyLandTargetListInfo> Target);
	static EVisibility GetDebugModeLayerUsageVisibility(const TSharedRef<FCyLandTargetListInfo> Target);
	static EVisibility GetDebugModeLayerUsageVisibility_Invert(const TSharedRef<FCyLandTargetListInfo> Target);
	static EVisibility GetDebugModeColorChannelVisibility(const TSharedRef<FCyLandTargetListInfo> Target);
	static ECheckBoxState DebugModeColorChannelIsChecked(const TSharedRef<FCyLandTargetListInfo> Target, int32 Channel);
	static void OnDebugModeColorChannelChanged(ECheckBoxState NewCheckedState, const TSharedRef<FCyLandTargetListInfo> Target, int32 Channel);

	// Drag/Drop handling
	FReply HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, int32 SlotIndex, SVerticalBox::FSlot* Slot);
	TOptional<SDragAndDropVerticalBox::EItemDropZone> HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone DropZone, SVerticalBox::FSlot* Slot);
	FReply HandleAcceptDrop(FDragDropEvent const& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone DropZone, int32 SlotIndex, SVerticalBox::FSlot* Slot);

	const FSlateBrush* GetTargetLayerDisplayOrderBrush() const;
	TSharedRef<SWidget> GetTargetLayerDisplayOrderButtonMenuContent();
	void SetSelectedDisplayOrder(ECyLandLayerDisplayMode InDisplayOrder);
	bool IsSelectedDisplayOrder(ECyLandLayerDisplayMode InDisplayOrder) const;

	TSharedRef<SWidget> GetTargetLayerShowUnusedButtonMenuContent();
	void ShowUnusedLayers(bool Result);
	bool ShouldShowUnusedLayers(bool Result) const;
	EVisibility ShouldShowLayer(TSharedRef<FCyLandTargetListInfo> Target) const;
};

class SCyLandEditorSelectableBorder : public SBorder
{
public:
	SLATE_BEGIN_ARGS(SCyLandEditorSelectableBorder)
		: _Content()
		, _HAlign(HAlign_Fill)
		, _VAlign(VAlign_Fill)
		, _Padding(FMargin(2.0f))
	{ }
		SLATE_DEFAULT_SLOT(FArguments, Content)

		SLATE_ARGUMENT(EHorizontalAlignment, HAlign)
		SLATE_ARGUMENT(EVerticalAlignment, VAlign)
		SLATE_ATTRIBUTE(FMargin, Padding)

		SLATE_EVENT(FOnContextMenuOpening, OnContextMenuOpening)
		SLATE_EVENT(FSimpleDelegate, OnSelected)
		SLATE_ATTRIBUTE(bool, IsSelected)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);
	const FSlateBrush* GetBorder() const;

protected:
	FOnContextMenuOpening OnContextMenuOpening;
	FSimpleDelegate OnSelected;
	TAttribute<bool> IsSelected;
};

class FTargetLayerDragDropOp : public FDragAndDropVerticalBoxOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FTargetLayerDragDropOp, FDragAndDropVerticalBoxOp)

	TSharedPtr<SWidget> WidgetToShow;

	static TSharedRef<FTargetLayerDragDropOp> New(int32 InSlotIndexBeingDragged, SVerticalBox::FSlot* InSlotBeingDragged, TSharedPtr<SWidget> InWidgetToShow);

public:
	virtual ~FTargetLayerDragDropOp();

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;
};