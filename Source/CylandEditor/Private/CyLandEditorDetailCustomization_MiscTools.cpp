// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CyLandEditorDetailCustomization_MiscTools.h"
#include "Widgets/Text/STextBlock.h"
#include "SlateOptMacros.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "CyLandEditorObject.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DetailCategoryBuilder.h"

#include "ScopedTransaction.h"
#include "IDetailPropertyRow.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "SFlattenHeightEyeDropperButton.h"
#include "CyLandEdModeTools.h"

#define LOCTEXT_NAMESPACE "CyLandEditor.Tools"

TSharedRef<IDetailCustomization> FCyLandEditorDetailCustomization_MiscTools::MakeInstance()
{
	return MakeShareable(new FCyLandEditorDetailCustomization_MiscTools);
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void FCyLandEditorDetailCustomization_MiscTools::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& ToolsCategory = DetailBuilder.EditCategory("Tool Settings");

	if (IsBrushSetActive("BrushSet_Component"))
	{
		ToolsCategory.AddCustomRow(LOCTEXT("Component.ClearSelection", "Clear Component Selection"))
		.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateStatic(&FCyLandEditorDetailCustomization_MiscTools::GetClearComponentSelectionVisibility)))
		[
			SNew(SButton)
			.Text(LOCTEXT("Component.ClearSelection", "Clear Component Selection"))
			.HAlign(HAlign_Center)
			.OnClicked_Static(&FCyLandEditorDetailCustomization_MiscTools::OnClearComponentSelectionButtonClicked)
		];
	}

	//if (IsToolActive("Mask"))
	{
		ToolsCategory.AddCustomRow(LOCTEXT("Mask.ClearSelection", "Clear Region Selection"))
		.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateStatic(&FCyLandEditorDetailCustomization_MiscTools::GetClearRegionSelectionVisibility)))
		[
			SNew(SButton)
			.Text(LOCTEXT("Mask.ClearSelection", "Clear Region Selection"))
			.HAlign(HAlign_Center)
			.OnClicked_Static(&FCyLandEditorDetailCustomization_MiscTools::OnClearRegionSelectionButtonClicked)
		];
	}

	if (IsToolActive("Flatten"))
	{
		TSharedRef<IPropertyHandle> FlattenValueProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, FlattenTarget));
		IDetailPropertyRow& FlattenValueRow = ToolsCategory.AddProperty(FlattenValueProperty);
		FlattenValueRow.CustomWidget()
			.NameContent()
			[
				FlattenValueProperty->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.Padding(0.0f, 2.0f, 5.0f, 2.0f)
				.FillWidth(1.0f)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(SNumericEntryBox<float>)
					.AllowSpin(true)
					.Font(DetailBuilder.GetDetailFont())
					.Value(this, &FCyLandEditorDetailCustomization_MiscTools::GetFlattenValue)
					.OnValueChanged_Static(&FCyLandEditorDetailCustomization_Base::OnValueChanged<float>, FlattenValueProperty)
					.OnValueCommitted_Static(&FCyLandEditorDetailCustomization_Base::OnValueCommitted<float>, FlattenValueProperty)
					.MinValue(-32768.0f)
					.MaxValue(32768.0f)
					.SliderExponentNeutralValue(0.0f)
					.SliderExponent(5.0f)
					.ShiftMouseMovePixelPerDelta(20)
					.MinSliderValue(-32768.0f)
					.MaxSliderValue(32768.0f)
					.MinDesiredValueWidth(75.0f)
					.ToolTipText(LOCTEXT("FlattenToolTips", "Target height to flatten towards (in Unreal Units)"))
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 2.0f, 5.0f, 2.0f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SFlattenHeightEyeDropperButton)
					.OnBegin(this, &FCyLandEditorDetailCustomization_MiscTools::OnBeginFlattenToolEyeDrop)
					.OnComplete(this, &FCyLandEditorDetailCustomization_MiscTools::OnCompletedFlattenToolEyeDrop)
				]		
			];
	}

	if (IsToolActive("Splines"))
	{
		ToolsCategory.AddCustomRow(LOCTEXT("ApplySplinesLabel", "Apply Splines"))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(0, 6, 0, 0)
			[
				SNew(STextBlock)
				.Font(DetailBuilder.GetDetailFont())
				.ShadowOffset(FVector2D::UnitVector)
				.Text(LOCTEXT("Spline.ApplySplines", "Deform CyLand to Splines:"))
			]
		];
		ToolsCategory.AddCustomRow(LOCTEXT("ApplySplinesLabel", "Apply Splines"))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			[
				SNew(SButton)
				.ToolTipText(LOCTEXT("Spline.ApplySplines.All.Tooltip", "Deforms and paints the CyLand to fit all the CyLand spline segments and controlpoints."))
				.Text(LOCTEXT("Spline.ApplySplines.All", "All Splines"))
				.HAlign(HAlign_Center)
				.OnClicked_Static(&FCyLandEditorDetailCustomization_MiscTools::OnApplyAllSplinesButtonClicked)
			]
			+ SHorizontalBox::Slot()
			[
				SNew(SButton)
				.ToolTipText(LOCTEXT("Spline.ApplySplines.Tooltip", "Deforms and paints the CyLand to fit only the selected CyLand spline segments and controlpoints."))
				.Text(LOCTEXT("Spline.ApplySplines.Selected", "Only Selected"))
				.HAlign(HAlign_Center)
				.OnClicked_Static(&FCyLandEditorDetailCustomization_MiscTools::OnApplySelectedSplinesButtonClicked)
			]
		];
		ToolsCategory.AddCustomRow(LOCTEXT("Spline.bUseAutoRotateControlPoint.Selected", "Use Auto Rotate Control Point"))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(0, 6, 0, 0)
			[
				SNew(SCheckBox)
				.OnCheckStateChanged(this, &FCyLandEditorDetailCustomization_MiscTools::OnbUseAutoRotateControlPointChanged)
				.IsChecked(this, &FCyLandEditorDetailCustomization_MiscTools::GetbUseAutoRotateControlPoint)
				.Content()
				[
					SNew(STextBlock).Text(LOCTEXT("Spline.bUseAutoRotateControlPoint.Selected", "Use Auto Rotate Control Point"))
				]
			]
		];
	}


	if (IsToolActive("Ramp"))
	{
		ToolsCategory.AddCustomRow(LOCTEXT("RampLabel", "Ramp"))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(0, 6, 0, 0)
			[
				SNew(STextBlock)
				.Font(DetailBuilder.GetDetailFont())
				.ShadowOffset(FVector2D::UnitVector)
				.Text(LOCTEXT("Ramp.Hint", "Click to add ramp points, then press \"Add Ramp\"."))
			]
		];
		ToolsCategory.AddCustomRow(LOCTEXT("ApplyRampLabel", "Apply Ramp"))
		[
			SNew(SBox)
			.Padding(FMargin(0, 0, 12, 0)) // Line up with the other properties due to having no reset to default button
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(0, 0, 3, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("Ramp.Reset", "Reset"))
					.HAlign(HAlign_Center)
					.OnClicked_Static(&FCyLandEditorDetailCustomization_MiscTools::OnResetRampButtonClicked)
				]
				+ SHorizontalBox::Slot()
				.Padding(3, 0, 0, 0)
				[
					SNew(SButton)
					.IsEnabled_Static(&FCyLandEditorDetailCustomization_MiscTools::GetApplyRampButtonIsEnabled)
					.Text(LOCTEXT("Ramp.Apply", "Add Ramp"))
					.HAlign(HAlign_Center)
					.OnClicked_Static(&FCyLandEditorDetailCustomization_MiscTools::OnApplyRampButtonClicked)
				]
			]
		];
	}

	if (IsToolActive("Mirror"))
	{
		ToolsCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, MirrorPoint));
		ToolsCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, MirrorOp));
		ToolsCategory.AddCustomRow(LOCTEXT("ApplyMirrorLabel", "Apply Mirror"))
		[
			SNew(SBox)
			.Padding(FMargin(0, 0, 12, 0)) // Line up with the other properties due to having no reset to default button
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(0, 0, 3, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("Mirror.Reset", "Recenter"))
					.HAlign(HAlign_Center)
					.OnClicked_Lambda(&FCyLandEditorDetailCustomization_MiscTools::OnResetMirrorPointButtonClicked)
				]
				+ SHorizontalBox::Slot()
				.Padding(3, 0, 0, 0)
				[
					SNew(SButton)
					//.IsEnabled_Static(&FCyLandEditorDetailCustomization_MiscTools::GetApplyMirrorButtonIsEnabled)
					.Text(LOCTEXT("Mirror.Apply", "Apply"))
					.HAlign(HAlign_Center)
					.OnClicked_Static(&FCyLandEditorDetailCustomization_MiscTools::OnApplyMirrorButtonClicked)
				]
			]
		];
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

EVisibility FCyLandEditorDetailCustomization_MiscTools::GetClearComponentSelectionVisibility()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode && CyLandEdMode->CurrentTool)
	{
		const FName CurrentToolName = CyLandEdMode->CurrentTool->GetToolName();
		if (CurrentToolName == FName("Select"))
		{
			return EVisibility::Visible;
		}
		else if (CyLandEdMode->CurrentToolTarget.CyLandInfo.IsValid() && CyLandEdMode->CurrentToolTarget.CyLandInfo->GetSelectedComponents().Num() > 0)
		{
			return EVisibility::Visible;
		}
	}

	return EVisibility::Collapsed;
}

DEFINE_LOG_CATEGORY_STATIC(LogCyLandEditor, Warning, All);

FReply FCyLandEditorDetailCustomization_MiscTools::OnClearComponentSelectionButtonClicked()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode)
	{
		UCyLandInfo* CyLandInfo = CyLandEdMode->CurrentToolTarget.CyLandInfo.Get();
		if (CyLandInfo)
		{
			FScopedTransaction Transaction(LOCTEXT("Component.Undo_ClearSelected", "Clearing Selection"));
			CyLandInfo->Modify();
			CyLandInfo->ClearSelectedRegion(true);
			auto saoruande = CyLandInfo->GetCyLandProxy();
			if (saoruande) {
				UE_LOG(LogCyLandEditor, Warning, TEXT("ACyLand ahha %d"), saoruande->CollisionComponents.Num());
				//saoruande->CollisionComponents[0]->AddRelativeRotation(FRotator(10, 10, 45));
				FVector CompXY(0, 0, 0);
				int step = saoruande->ComponentSizeQuads;
				for (int j = 0; j < 2; j++)
				for (int i = 0; i < saoruande->CyLandComponents.Num()/2; i++)
				{
					//UE_LOG(LogCyLandEditor, Warning, TEXT("ACyLand ahha %s"), *saoruande->CyLandComponents[i]->GetComponentLocation().ToString());
			
					saoruande->CyLandComponents[j * saoruande->CyLandComponents.Num() / 2 +i]->SetRelativeLocation((CompXY + FVector(1, 0, 0) * i + FVector(0, 1, 0) * j) * step);
					//saoruande->CyLandComponents[i]->SetRelativeLocation(FVector(1,0,0));
				}
			}
		}
	}

	return FReply::Handled();
}

EVisibility FCyLandEditorDetailCustomization_MiscTools::GetClearRegionSelectionVisibility()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode && CyLandEdMode->CurrentTool)
	{
		const FName CurrentToolName = CyLandEdMode->CurrentTool->GetToolName();
		if (CurrentToolName == FName("Mask"))
		{
			return EVisibility::Visible;
		}
		else if (CyLandEdMode->CurrentTool && CyLandEdMode->CurrentTool->SupportsMask() &&
			CyLandEdMode->CurrentToolTarget.CyLandInfo.IsValid() && CyLandEdMode->CurrentToolTarget.CyLandInfo->SelectedRegion.Num() > 0)
		{
			return EVisibility::Visible;
		}
	}

	return EVisibility::Collapsed;
}

FReply FCyLandEditorDetailCustomization_MiscTools::OnClearRegionSelectionButtonClicked()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode)
	{
		UCyLandInfo* CyLandInfo = CyLandEdMode->CurrentToolTarget.CyLandInfo.Get();
		if (CyLandInfo)
		{
			FScopedTransaction Transaction(LOCTEXT("Region.Undo_ClearSelected", "Clearing Region Selection"));
			CyLandInfo->Modify();
			CyLandInfo->ClearSelectedRegion(false);
		}
	}

	return FReply::Handled();
}

FReply FCyLandEditorDetailCustomization_MiscTools::OnApplyAllSplinesButtonClicked()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode && CyLandEdMode->CurrentToolTarget.CyLandInfo.IsValid())
	{
		CyLandEdMode->CurrentToolTarget.CyLandInfo->ApplySplines(false);
	}

	return FReply::Handled();
}

FReply FCyLandEditorDetailCustomization_MiscTools::OnApplySelectedSplinesButtonClicked()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode && CyLandEdMode->CurrentToolTarget.CyLandInfo.IsValid())
	{
		CyLandEdMode->CurrentToolTarget.CyLandInfo->ApplySplines(true);
	}

	return FReply::Handled();
}

void FCyLandEditorDetailCustomization_MiscTools::OnbUseAutoRotateControlPointChanged(ECheckBoxState NewState)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode)
	{
		CyLandEdMode->SetbUseAutoRotateOnJoin(NewState == ECheckBoxState::Checked);
	}
}

ECheckBoxState FCyLandEditorDetailCustomization_MiscTools::GetbUseAutoRotateControlPoint() const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode)
	{
		return CyLandEdMode->GetbUseAutoRotateOnJoin() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Unchecked;
}

FReply FCyLandEditorDetailCustomization_MiscTools::OnApplyRampButtonClicked()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != NULL && IsToolActive(FName("Ramp")))
	{
		CyLandEdMode->ApplyRampTool();
	}

	return FReply::Handled();
}

bool FCyLandEditorDetailCustomization_MiscTools::GetApplyRampButtonIsEnabled()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != NULL && IsToolActive(FName("Ramp")))
	{
		return CyLandEdMode->CanApplyRampTool();
	}

	return false;
}

FReply FCyLandEditorDetailCustomization_MiscTools::OnResetRampButtonClicked()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != NULL && IsToolActive(FName("Ramp")))
	{
		CyLandEdMode->ResetRampTool();
	}

	return FReply::Handled();
}

FReply FCyLandEditorDetailCustomization_MiscTools::OnApplyMirrorButtonClicked()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != NULL && IsToolActive(FName("Mirror")))
	{
		CyLandEdMode->ApplyMirrorTool();
	}

	return FReply::Handled();
}

FReply FCyLandEditorDetailCustomization_MiscTools::OnResetMirrorPointButtonClicked()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != NULL && IsToolActive(FName("Mirror")))
	{
		CyLandEdMode->CenterMirrorTool();
	}

	return FReply::Handled();
}

TOptional<float> FCyLandEditorDetailCustomization_MiscTools::GetFlattenValue() const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr && IsToolActive(FName("Flatten")))
	{
		if (CyLandEdMode->UISettings->bFlattenEyeDropperModeActivated)
		{
			return CyLandEdMode->UISettings->FlattenEyeDropperModeDesiredTarget;
		}

		return CyLandEdMode->UISettings->FlattenTarget;
	}

	return 0.0f;
}

void FCyLandEditorDetailCustomization_MiscTools::OnBeginFlattenToolEyeDrop()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr && IsToolActive(FName("Flatten")))
	{
		CyLandEdMode->UISettings->bFlattenEyeDropperModeActivated = true;
		CyLandEdMode->CurrentTool->SetCanToolBeActivated(false);
	}
}

void FCyLandEditorDetailCustomization_MiscTools::OnCompletedFlattenToolEyeDrop(bool Canceled)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr && IsToolActive(FName("Flatten")))
	{
		CyLandEdMode->UISettings->bFlattenEyeDropperModeActivated = false;
		CyLandEdMode->CurrentTool->SetCanToolBeActivated(true);

		if (!Canceled)
		{
			CyLandEdMode->UISettings->FlattenTarget = CyLandEdMode->UISettings->FlattenEyeDropperModeDesiredTarget;
		}
	}
}

#undef LOCTEXT_NAMESPACE
