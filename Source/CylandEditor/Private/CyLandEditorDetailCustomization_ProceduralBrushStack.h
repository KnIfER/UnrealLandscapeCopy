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
 * Slate widgets customizer for the layers list in the CyLand Editor
 */

class FCyLandEditorDetailCustomization_ProceduralBrushStack : public FCyLandEditorDetailCustomization_Base
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};

class FCyLandEditorCustomNodeBuilder_ProceduralBrushStack : public IDetailCustomNodeBuilder, public TSharedFromThis<FCyLandEditorCustomNodeBuilder_ProceduralBrushStack>
{
public:
	FCyLandEditorCustomNodeBuilder_ProceduralBrushStack(TSharedRef<FAssetThumbnailPool> ThumbnailPool);
	~FCyLandEditorCustomNodeBuilder_ProceduralBrushStack();

	virtual void SetOnRebuildChildren( FSimpleDelegate InOnRegenerateChildren ) override;
	virtual void GenerateHeaderRowContent( FDetailWidgetRow& NodeRow ) override;
	virtual void GenerateChildContent( IDetailChildrenBuilder& ChildrenBuilder ) override;
	virtual void Tick( float DeltaTime ) override {}
	virtual bool RequiresTick() const override { return false; }
	virtual bool InitiallyCollapsed() const override { return false; }
	virtual FName GetName() const override { return "Brush Stack"; }

protected:
	TSharedRef<FAssetThumbnailPool> ThumbnailPool;

	static class FEdModeCyLand* GetEditorMode();

	TSharedPtr<SWidget> GenerateRow(int32 InLayerIndex);

	bool IsBrushSelected(int32 InBrushIndex) const;
	void OnBrushSelectionChanged(int32 InBrushIndex);
	FText GetBrushText(int32 InBrushIndex) const;
	FSlateColor GetBrushTextColor(int32 InBrushIndex) const;
	ACyLandBlueprintCustomBrush* GetBrush(int32 InBrushIndex) const;

	FReply ToggleCommitBrushes();
	bool IsCommitBrushesButtonEnabled() const;
	FText GetCommitBrushesButtonText() const;

	// Drag/Drop handling
	FReply HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, int32 SlotIndex, SVerticalBox::FSlot* Slot);
	TOptional<SDragAndDropVerticalBox::EItemDropZone> HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone DropZone, SVerticalBox::FSlot* Slot);
	FReply HandleAcceptDrop(FDragDropEvent const& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone DropZone, int32 SlotIndex, SVerticalBox::FSlot* Slot);
};

class FCyLandBrushDragDropOp : public FDragAndDropVerticalBoxOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FCyLandBrushDragDropOp, FDragAndDropVerticalBoxOp)

	TSharedPtr<SWidget> WidgetToShow;

	static TSharedRef<FCyLandBrushDragDropOp> New(int32 InSlotIndexBeingDragged, SVerticalBox::FSlot* InSlotBeingDragged, TSharedPtr<SWidget> InWidgetToShow);

public:
	virtual ~FCyLandBrushDragDropOp();

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;
};
