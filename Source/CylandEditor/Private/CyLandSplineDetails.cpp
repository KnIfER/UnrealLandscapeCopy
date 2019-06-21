// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CyLandSplineDetails.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "CyLandEdMode.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "EditorModeManager.h"
#include "CyLandEditorModule.h"
#include "DetailWidgetRow.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"

#define LOCTEXT_NAMESPACE "CyLandSplineDetails"


TSharedRef<IDetailCustomization> FCyLandSplineDetails::MakeInstance()
{
	return MakeShareable(new FCyLandSplineDetails);
}

void FCyLandSplineDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& CyLandSplineCategory = DetailBuilder.EditCategory("CyLandSpline", FText::GetEmpty(), ECategoryPriority::Transform);

	CyLandSplineCategory.AddCustomRow(FText::GetEmpty())
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.Padding(0, 0, 2, 0)
		.VAlign(VAlign_Center)
		.FillWidth(1)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SelectAll", "Select all connected:"))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1)
		[
			SNew(SButton)
			.Text(LOCTEXT("ControlPoints", "Control Points"))
			.HAlign(HAlign_Center)
			.OnClicked(this, &FCyLandSplineDetails::OnSelectConnectedControlPointsButtonClicked)
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1)
		[
			SNew(SButton)
			.Text(LOCTEXT("Segments", "Segments"))
			.HAlign(HAlign_Center)
			.OnClicked(this, &FCyLandSplineDetails::OnSelectConnectedSegmentsButtonClicked)
		]
	];

	CyLandSplineCategory.AddCustomRow(FText::GetEmpty())
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.Padding(0, 0, 2, 0)
		.VAlign(VAlign_Center)
		.FillWidth(1)
		[
			SNew(SButton)
			.Text(LOCTEXT("Move Selected ControlPnts+Segs to Current level", "Move to current level"))
			.HAlign(HAlign_Center)
			.OnClicked(this, &FCyLandSplineDetails::OnMoveToCurrentLevelButtonClicked)
			.IsEnabled(this, &FCyLandSplineDetails::IsMoveToCurrentLevelButtonEnabled)
		]
	];
}

class FEdModeCyLand* FCyLandSplineDetails::GetEditorMode() const
{
	return (FEdModeCyLand*)GLevelEditorModeTools().GetActiveMode(ICyLandEditorModule::EM_Landscape_Mimic);
}

FReply FCyLandSplineDetails::OnSelectConnectedControlPointsButtonClicked()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode && CyLandEdMode->CurrentToolTarget.CyLandInfo.IsValid())
	{
		CyLandEdMode->SelectAllConnectedSplineControlPoints();
	}

	return FReply::Handled();
}

FReply FCyLandSplineDetails::OnSelectConnectedSegmentsButtonClicked()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode && CyLandEdMode->CurrentToolTarget.CyLandInfo.IsValid())
	{
		CyLandEdMode->SelectAllConnectedSplineSegments();
	}

	return FReply::Handled();
}

FReply FCyLandSplineDetails::OnMoveToCurrentLevelButtonClicked()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode && CyLandEdMode->CurrentToolTarget.CyLandInfo.IsValid() && CyLandEdMode->CurrentToolTarget.CyLandInfo->GetCurrentLevelCyLandProxy(true))
	{
		CyLandEdMode->SplineMoveToCurrentLevel();
	}

	return FReply::Handled();
}

bool FCyLandSplineDetails::IsMoveToCurrentLevelButtonEnabled() const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	return (CyLandEdMode && CyLandEdMode->CurrentToolTarget.CyLandInfo.IsValid() && CyLandEdMode->CurrentToolTarget.CyLandInfo->GetCurrentLevelCyLandProxy(true));
}

#undef LOCTEXT_NAMESPACE
