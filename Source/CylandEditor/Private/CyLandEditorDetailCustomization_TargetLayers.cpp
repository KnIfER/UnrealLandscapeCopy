// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CyLandEditorDetailCustomization_TargetLayers.h"
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
#include "Landscape/Classes/Materials/MaterialExpressionLandscapeVisibilityMask.h"
#include "CyLandEdit.h"
#include "IDetailGroup.h"
#include "Widgets/SBoxPanel.h"
#include "Editor/EditorStyle/Private/SlateEditorStyle.h"

#define LOCTEXT_NAMESPACE "CyLandEditor.TargetLayers"

TSharedRef<IDetailCustomization> FCyLandEditorDetailCustomization_TargetLayers::MakeInstance()
{
	return MakeShareable(new FCyLandEditorDetailCustomization_TargetLayers);
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void FCyLandEditorDetailCustomization_TargetLayers::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TSharedRef<IPropertyHandle> PropertyHandle_PaintingRestriction = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, PaintingRestriction));
	TSharedRef<IPropertyHandle> PropertyHandle_TargetDisplayOrder = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, TargetDisplayOrder));
	PropertyHandle_TargetDisplayOrder->MarkHiddenByCustomization();

	TSharedRef<IPropertyHandle> PropertyHandle_TargetShowUnusedLayers = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, ShowUnusedLayers));
	PropertyHandle_TargetShowUnusedLayers->MarkHiddenByCustomization();	

	if (!ShouldShowTargetLayers())
	{
		PropertyHandle_PaintingRestriction->MarkHiddenByCustomization();

		return;
	}

	IDetailCategoryBuilder& TargetsCategory = DetailBuilder.EditCategory("Target Layers");

	TargetsCategory.AddProperty(PropertyHandle_PaintingRestriction)
	.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateStatic(&FCyLandEditorDetailCustomization_TargetLayers::GetVisibility_PaintingRestriction)));

	TargetsCategory.AddCustomRow(FText())
	.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateStatic(&FCyLandEditorDetailCustomization_TargetLayers::GetVisibility_VisibilityTip)))
	[
		SNew(SErrorText)
		.Font(DetailBuilder.GetDetailFontBold())
		.AutoWrapText(true)
		.ErrorText(LOCTEXT("Visibility_Tip","Note: You must add a \"CyLand Visibility Mask\" node to your material before you can paint visibility."))
	];

	TargetsCategory.AddCustomBuilder(MakeShareable(new FCyLandEditorCustomNodeBuilder_TargetLayers(DetailBuilder.GetThumbnailPool().ToSharedRef(), PropertyHandle_TargetDisplayOrder, PropertyHandle_TargetShowUnusedLayers)));
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

bool FCyLandEditorDetailCustomization_TargetLayers::ShouldShowTargetLayers()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode && CyLandEdMode->CurrentToolMode)
	{
		const FName CurrentToolName = CyLandEdMode->CurrentTool->GetToolName();

		//bool bSupportsHeightmap = (CyLandEdMode->CurrentToolMode->SupportedTargetTypes & ECyLandToolTargetTypeMask::Heightmap) != 0;
		//bool bSupportsWeightmap = (CyLandEdMode->CurrentToolMode->SupportedTargetTypes & ECyLandToolTargetTypeMask::Weightmap) != 0;
		//bool bSupportsVisibility = (CyLandEdMode->CurrentToolMode->SupportedTargetTypes & ECyLandToolTargetTypeMask::Visibility) != 0;

		//// Visible if there are possible choices
		//if (bSupportsWeightmap || bSupportsHeightmap || bSupportsVisibility)
		if (CyLandEdMode->CurrentToolMode->SupportedTargetTypes != 0 && CurrentToolName != TEXT("BPCustom"))
		{
			return true;
		}
	}

	return false;
}

bool FCyLandEditorDetailCustomization_TargetLayers::ShouldShowPaintingRestriction()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();

	if (CyLandEdMode && CyLandEdMode->CurrentToolMode)
	{
		const FName CurrentToolName = CyLandEdMode->CurrentTool->GetToolName();

		if ((CyLandEdMode->CurrentToolTarget.TargetType == ECyLandToolTargetType::Weightmap && CurrentToolName != TEXT("BPCustom"))
			|| CyLandEdMode->CurrentToolTarget.TargetType == ECyLandToolTargetType::Visibility)
		{
			return true;
		}
	}

	return false;
}

EVisibility FCyLandEditorDetailCustomization_TargetLayers::GetVisibility_PaintingRestriction()
{
	return ShouldShowPaintingRestriction() ? EVisibility::Visible : EVisibility::Collapsed;
}

bool FCyLandEditorDetailCustomization_TargetLayers::ShouldShowVisibilityTip()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode && CyLandEdMode->CurrentToolTarget.CyLandInfo.IsValid())
	{
		if (CyLandEdMode->CurrentToolTarget.TargetType == ECyLandToolTargetType::Visibility)
		{
			ACyLandProxy* Proxy = CyLandEdMode->CurrentToolTarget.CyLandInfo->GetCyLandProxy();
			UMaterialInterface* HoleMaterial = Proxy->GetCyLandHoleMaterial();
			if (!HoleMaterial)
			{
				HoleMaterial = Proxy->GetCyLandMaterial();
			}
			if (!HoleMaterial->GetMaterial()->HasAnyExpressionsInMaterialAndFunctionsOfType<UMaterialExpressionLandscapeVisibilityMask>())
			{
				return true;
			}
		}
	}

	return false;
}

EVisibility FCyLandEditorDetailCustomization_TargetLayers::GetVisibility_VisibilityTip()
{
	return ShouldShowVisibilityTip() ? EVisibility::Visible : EVisibility::Collapsed;
}

//////////////////////////////////////////////////////////////////////////

FEdModeCyLand* FCyLandEditorCustomNodeBuilder_TargetLayers::GetEditorMode()
{
	return (FEdModeCyLand*)GLevelEditorModeTools().GetActiveMode(ICyLandEditorModule::EM_Landscape_Mimic);
}

FCyLandEditorCustomNodeBuilder_TargetLayers::FCyLandEditorCustomNodeBuilder_TargetLayers(TSharedRef<FAssetThumbnailPool> InThumbnailPool, TSharedRef<IPropertyHandle> InTargetDisplayOrderPropertyHandle, TSharedRef<IPropertyHandle> InTargetShowUnusedLayersPropertyHandle)
	: ThumbnailPool(InThumbnailPool)
	, TargetDisplayOrderPropertyHandle(InTargetDisplayOrderPropertyHandle)
	, TargetShowUnusedLayersPropertyHandle(InTargetShowUnusedLayersPropertyHandle)
{
}

FCyLandEditorCustomNodeBuilder_TargetLayers::~FCyLandEditorCustomNodeBuilder_TargetLayers()
{
	FEdModeCyLand::TargetsListUpdated.RemoveAll(this);
}

void FCyLandEditorCustomNodeBuilder_TargetLayers::SetOnRebuildChildren(FSimpleDelegate InOnRegenerateChildren)
{
	FEdModeCyLand::TargetsListUpdated.RemoveAll(this);
	if (InOnRegenerateChildren.IsBound())
	{
		FEdModeCyLand::TargetsListUpdated.Add(InOnRegenerateChildren);
	}
}

void FCyLandEditorCustomNodeBuilder_TargetLayers::GenerateHeaderRowContent(FDetailWidgetRow& NodeRow)
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
			.Text(FText::FromString(TEXT("Layers")))
		];

	if (CyLandEdMode->CurrentToolMode->SupportedTargetTypes & ECyLandToolTargetTypeMask::Weightmap)
	{
		NodeRow.ValueWidget
		[
			SNew(SHorizontalBox)
			
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SComboButton)
				.ComboButtonStyle(FEditorStyle::Get(), "ToolbarComboButton")
				.ForegroundColor(FSlateColor::UseForeground())
				.HasDownArrow(true)
				.ContentPadding(FMargin(1, 0))
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				.ToolTipText(LOCTEXT("TargetLayerSortButtonTooltip", "Define how we want to sort the displayed layers"))
				.OnGetMenuContent(this, &FCyLandEditorCustomNodeBuilder_TargetLayers::GetTargetLayerDisplayOrderButtonMenuContent)
				.ButtonContent()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew( SOverlay )
						+SOverlay::Slot()
						[
							SNew(SImage)
							.Image(FEditorStyle::GetBrush("CyLandEditor.Target_DisplayOrder.Default"))
						]	
						+SOverlay::Slot()
						[
							SNew(SImage)
							.Image(this, &FCyLandEditorCustomNodeBuilder_TargetLayers::GetTargetLayerDisplayOrderBrush)
						]
					]
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(5.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SComboButton)
				.ComboButtonStyle(FEditorStyle::Get(), "ToolbarComboButton")
				.ForegroundColor(FSlateColor::UseForeground())
				.HasDownArrow(true)
				.ContentPadding(FMargin(1, 0))
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				.ToolTipText(LOCTEXT("TargetLayerUnusedLayerButtonTooltip", "Define if we want to display unused layers"))
				.OnGetMenuContent(this, &FCyLandEditorCustomNodeBuilder_TargetLayers::GetTargetLayerShowUnusedButtonMenuContent)
				.ButtonContent()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.WidthOverride(16.0f)
						.HeightOverride(16.0f)
						[
							SNew(SImage)
							.Image(FEditorStyle::GetBrush("GenericViewButton"))
						]
					]
				]
			]
		];
	}	
	else
	{
		NodeRow.IsEnabled(false);
	}
}

TSharedRef<SWidget> FCyLandEditorCustomNodeBuilder_TargetLayers::GetTargetLayerDisplayOrderButtonMenuContent()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr, nullptr, /*bCloseSelfOnly=*/ true);

	MenuBuilder.BeginSection("TargetLayerSortType", LOCTEXT("SortTypeHeading", "Sort Type"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("TargetLayerDisplayOrderDefault", "Default"),
			LOCTEXT("TargetLayerDisplayOrderDefaultToolTip", "Sort using order defined in the material."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FCyLandEditorCustomNodeBuilder_TargetLayers::SetSelectedDisplayOrder, ECyLandLayerDisplayMode::Default),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &FCyLandEditorCustomNodeBuilder_TargetLayers::IsSelectedDisplayOrder, ECyLandLayerDisplayMode::Default)
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("TargetLayerDisplayOrderAlphabetical", "Alphabetical"),
			LOCTEXT("TargetLayerDisplayOrderAlphabeticalToolTip", "Sort using alphabetical order."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FCyLandEditorCustomNodeBuilder_TargetLayers::SetSelectedDisplayOrder, ECyLandLayerDisplayMode::Alphabetical),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &FCyLandEditorCustomNodeBuilder_TargetLayers::IsSelectedDisplayOrder, ECyLandLayerDisplayMode::Alphabetical)
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("TargetLayerDisplayOrderCustom", "Custom"),
			LOCTEXT("TargetLayerDisplayOrderCustomToolTip", "This sort options will be set when changing manually display order by dragging layers"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FCyLandEditorCustomNodeBuilder_TargetLayers::SetSelectedDisplayOrder, ECyLandLayerDisplayMode::UserSpecific),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &FCyLandEditorCustomNodeBuilder_TargetLayers::IsSelectedDisplayOrder, ECyLandLayerDisplayMode::UserSpecific)
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> FCyLandEditorCustomNodeBuilder_TargetLayers::GetTargetLayerShowUnusedButtonMenuContent()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr, nullptr, /*bCloseSelfOnly=*/ true);

	MenuBuilder.BeginSection("TargetLayerUnusedType", LOCTEXT("UnusedTypeHeading", "Layer Visilibity"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("TargetLayerShowUnusedLayer", "Show all layers"),
			LOCTEXT("TargetLayerShowUnusedLayerToolTip", "Show all layers"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FCyLandEditorCustomNodeBuilder_TargetLayers::ShowUnusedLayers, true),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &FCyLandEditorCustomNodeBuilder_TargetLayers::ShouldShowUnusedLayers, true)
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("TargetLayerHideUnusedLayer", "Hide unused layers"),
			LOCTEXT("TargetLayerHideUnusedLayerToolTip", "Only show used layer"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FCyLandEditorCustomNodeBuilder_TargetLayers::ShowUnusedLayers, false),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &FCyLandEditorCustomNodeBuilder_TargetLayers::ShouldShowUnusedLayers, false)
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);
	}

	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void FCyLandEditorCustomNodeBuilder_TargetLayers::ShowUnusedLayers(bool Result)
{
	TargetShowUnusedLayersPropertyHandle->SetValue(Result);
}

bool FCyLandEditorCustomNodeBuilder_TargetLayers::ShouldShowUnusedLayers(bool Result) const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		return CyLandEdMode->UISettings->ShowUnusedLayers == Result;
	}

	return false;
}

void FCyLandEditorCustomNodeBuilder_TargetLayers::SetSelectedDisplayOrder(ECyLandLayerDisplayMode InDisplayOrder)
{
	TargetDisplayOrderPropertyHandle->SetValue((uint8)InDisplayOrder);	
}

bool FCyLandEditorCustomNodeBuilder_TargetLayers::IsSelectedDisplayOrder(ECyLandLayerDisplayMode InDisplayOrder) const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		return CyLandEdMode->UISettings->TargetDisplayOrder == InDisplayOrder;
	}

	return false;
}

const FSlateBrush* FCyLandEditorCustomNodeBuilder_TargetLayers::GetTargetLayerDisplayOrderBrush() const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		switch (CyLandEdMode->UISettings->TargetDisplayOrder)
		{
			case ECyLandLayerDisplayMode::Alphabetical: return FEditorStyle::Get().GetBrush("CyLandEditor.Target_DisplayOrder.Alphabetical");
			case ECyLandLayerDisplayMode::UserSpecific: return FEditorStyle::Get().GetBrush("CyLandEditor.Target_DisplayOrder.Custom");
		}
	}

	return nullptr;
}

EVisibility FCyLandEditorCustomNodeBuilder_TargetLayers::ShouldShowLayer(TSharedRef<FCyLandTargetListInfo> Target) const
{
	if (Target->TargetType == ECyLandToolTargetType::Weightmap)
	{
		FEdModeCyLand* CyLandEdMode = GetEditorMode();

		if (CyLandEdMode != nullptr)
		{
			return CyLandEdMode->ShouldShowLayer(Target) ? EVisibility::Visible : EVisibility::Collapsed;
		}
	}

	return EVisibility::Visible;
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void FCyLandEditorCustomNodeBuilder_TargetLayers::GenerateChildContent(IDetailChildrenBuilder& ChildrenBuilder)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != NULL)
	{
		const TArray<TSharedRef<FCyLandTargetListInfo>>& TargetList = CyLandEdMode->GetTargetList();
		const TArray<FName>* TargetDisplayOrderList = CyLandEdMode->GetTargetDisplayOrderList();
		const TArray<FName>& TargetShownLayerList = CyLandEdMode->GetTargetShownList();

		if (TargetDisplayOrderList == nullptr)
		{
			return;
		}

		TSharedPtr<SDragAndDropVerticalBox> TargetLayerList = SNew(SDragAndDropVerticalBox)
			.OnCanAcceptDrop(this, &FCyLandEditorCustomNodeBuilder_TargetLayers::HandleCanAcceptDrop)
			.OnAcceptDrop(this, &FCyLandEditorCustomNodeBuilder_TargetLayers::HandleAcceptDrop)
			.OnDragDetected(this, &FCyLandEditorCustomNodeBuilder_TargetLayers::HandleDragDetected);

		TargetLayerList->SetDropIndicator_Above(*FEditorStyle::GetBrush("CyLandEditor.TargetList.DropZone.Above"));
		TargetLayerList->SetDropIndicator_Below(*FEditorStyle::GetBrush("CyLandEditor.TargetList.DropZone.Below"));

		ChildrenBuilder.AddCustomRow(FText::FromString(FString(TEXT("Layers"))))
			.Visibility(EVisibility::Visible)
			[
				TargetLayerList.ToSharedRef()
			];

		for (int32 i = 0; i < TargetDisplayOrderList->Num(); ++i)
		{
			for (const TSharedRef<FCyLandTargetListInfo>& TargetInfo : TargetList)
			{
				if (TargetInfo->LayerName == (*TargetDisplayOrderList)[i] && (TargetInfo->TargetType != ECyLandToolTargetType::Weightmap || TargetShownLayerList.Find(TargetInfo->LayerName) != INDEX_NONE))
				{
					TSharedPtr<SWidget> GeneratedRowWidget = GenerateRow(TargetInfo);

					if (GeneratedRowWidget.IsValid())
					{
						TargetLayerList->AddSlot()
						.AutoHeight()						
						[
							GeneratedRowWidget.ToSharedRef()
						];
					}

					break;
				}
			}
		}
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedPtr<SWidget> FCyLandEditorCustomNodeBuilder_TargetLayers::GenerateRow(const TSharedRef<FCyLandTargetListInfo> Target)
{
	TSharedPtr<SWidget> RowWidget;

	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode)
	{
		if ((CyLandEdMode->CurrentTool->GetSupportedTargetTypes() & CyLandEdMode->CurrentToolMode->SupportedTargetTypes & ECyLandToolTargetTypeMask::FromType(Target->TargetType)) == 0)
		{
			return RowWidget;
		}
	}
	
	if (Target->TargetType != ECyLandToolTargetType::Weightmap)
	{
		RowWidget = SNew(SCyLandEditorSelectableBorder)
			.Padding(0)
			.VAlign(VAlign_Center)
			.OnContextMenuOpening_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::OnTargetLayerContextMenuOpening, Target)
			.OnSelected_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::OnTargetSelectionChanged, Target)
			.IsSelected_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::GetTargetLayerIsSelected, Target)			
			.Visibility(this, &FCyLandEditorCustomNodeBuilder_TargetLayers::ShouldShowLayer, Target)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(2))
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush(Target->TargetType == ECyLandToolTargetType::Heightmap ? TEXT("CyLandEditor.Target_Heightmap") : TEXT("CyLandEditor.Target_Visibility")))
				]
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
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.Text(Target->TargetName)
						.ShadowOffset(FVector2D::UnitVector)
					]
				]
			];
	}
	else
	{
		static const FSlateColorBrush SolidWhiteBrush = FSlateColorBrush(FColorList::White);

		RowWidget = SNew(SCyLandEditorSelectableBorder)
			.Padding(0)
			.VAlign(VAlign_Center)
			.OnContextMenuOpening_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::OnTargetLayerContextMenuOpening, Target)
			.OnSelected_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::OnTargetSelectionChanged, Target)
			.IsSelected_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::GetTargetLayerIsSelected, Target)
			.Visibility(this, &FCyLandEditorCustomNodeBuilder_TargetLayers::ShouldShowLayer, Target)
			[				
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.Padding(FMargin(2.0f, 0.0f, 2.0f, 0.0f))
					[
						SNew(SImage)
						.Image(FCoreStyle::Get().GetBrush("VerticalBoxDragIndicator"))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(2))
				[
					SNew(SBox)
					.Visibility_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::GetDebugModeLayerUsageVisibility, Target)
					.WidthOverride(48)
					.HeightOverride(48)
					[
						SNew(SImage)
						.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.ColorAndOpacity_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::GetLayerUsageDebugColor, Target)
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(2))
				[
					(Target->bValid)
					? (TSharedRef<SWidget>)(
					SNew(SCyLandAssetThumbnail, Target->ThumbnailMIC.Get(), ThumbnailPool)
					.Visibility_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::GetDebugModeLayerUsageVisibility_Invert, Target)
					.ThumbnailSize(FIntPoint(48, 48))
					)
					: (TSharedRef<SWidget>)(
					SNew(SImage)
					.Visibility_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::GetDebugModeLayerUsageVisibility_Invert, Target)
					.Image(FEditorStyle::GetBrush(TEXT("CyLandEditor.Target_Invalid")))
					)
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(4, 0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.VAlign(VAlign_Center)
					.Padding(0, 2, 0, 0)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						[
							SNew(STextBlock)
							.Font(IDetailLayoutBuilder::GetDetailFont())
							.Text(Target->TargetName)
							.ShadowOffset(FVector2D::UnitVector)
						]
						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Right)
						[
							SNew(STextBlock)
							.Visibility((Target->LayerInfoObj.IsValid() && Target->LayerInfoObj->bNoWeightBlend) ? EVisibility::Visible : EVisibility::Collapsed)
							.Font(IDetailLayoutBuilder::GetDetailFont())
							.Text(LOCTEXT("NoWeightBlend", "No Weight-Blend"))
							.ShadowOffset(FVector2D::UnitVector)
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.VAlign(VAlign_Center)
					[
						SNew(SHorizontalBox)
						.Visibility_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::GetTargetLayerInfoSelectorVisibility, Target)
						+ SHorizontalBox::Slot()
						.FillWidth(1)
						.VAlign(VAlign_Center)
						[
							SNew(SObjectPropertyEntryBox)
							.IsEnabled((bool)Target->bValid)
							.ObjectPath(Target->LayerInfoObj != NULL ? Target->LayerInfoObj->GetPathName() : FString())
							.AllowedClass(UCyLandLayerInfoObject::StaticClass())
							.OnObjectChanged_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::OnTargetLayerSetObject, Target)
							.OnShouldFilterAsset_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::ShouldFilterLayerInfo, Target->LayerName)
							.AllowClear(false)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SComboButton)
							.ButtonStyle( FEditorStyle::Get(), "HoverHintOnly" )
							.HasDownArrow(false)
							.ContentPadding(4.0f)
							.ForegroundColor(FSlateColor::UseForeground())
							.IsFocusable(false)
							.ToolTipText(LOCTEXT("Tooltip_Create", "Create Layer Info"))
							.IsEnabled_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::GetTargetLayerCreateEnabled, Target)
							.OnGetMenuContent_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::OnGetTargetLayerCreateMenu, Target)
							.ButtonContent()
							[
								SNew(SImage)
								.Image(FEditorStyle::GetBrush("CyLandEditor.Target_Create"))
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SButton)
							.ButtonStyle( FEditorStyle::Get(), "HoverHintOnly" )
							.ContentPadding(4.0f)
							.ForegroundColor(FSlateColor::UseForeground())
							.IsFocusable(false)
							.ToolTipText(LOCTEXT("Tooltip_MakePublic", "Make Layer Public (move layer info into asset file)"))
							.Visibility_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::GetTargetLayerMakePublicVisibility, Target)
							.OnClicked_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::OnTargetLayerMakePublicClicked, Target)
							[
								SNew(SImage)
								.Image(FEditorStyle::GetBrush("CyLandEditor.Target_MakePublic"))
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SButton)
							.ButtonStyle( FEditorStyle::Get(), "HoverHintOnly" )
							.ContentPadding(4.0f)
							.ForegroundColor(FSlateColor::UseForeground())
							.IsFocusable(false)
							.ToolTipText(LOCTEXT("Tooltip_Delete", "Delete Layer"))
							.Visibility_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::GetTargetLayerDeleteVisibility, Target)
							.OnClicked_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::OnTargetLayerDeleteClicked, Target)
							[
								SNew(SImage)
								.Image(FEditorStyle::GetBrush("CyLandEditor.Target_Delete"))
							]
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						.Visibility_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::GetDebugModeColorChannelVisibility, Target)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0, 2, 2, 2)
						[
							SNew(SCheckBox)
							.IsChecked_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::DebugModeColorChannelIsChecked, Target, 0)
							.OnCheckStateChanged_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::OnDebugModeColorChannelChanged, Target, 0)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("ViewMode.Debug_None", "None"))
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2)
						[
							SNew(SCheckBox)
							.IsChecked_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::DebugModeColorChannelIsChecked, Target, 1)
							.OnCheckStateChanged_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::OnDebugModeColorChannelChanged, Target, 1)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("ViewMode.Debug_R", "R"))
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2)
						[
							SNew(SCheckBox)
							.IsChecked_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::DebugModeColorChannelIsChecked, Target, 2)
							.OnCheckStateChanged_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::OnDebugModeColorChannelChanged, Target, 2)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("ViewMode.Debug_G", "G"))
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2)
						[
							SNew(SCheckBox)
							.IsChecked_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::DebugModeColorChannelIsChecked, Target, 4)
							.OnCheckStateChanged_Static(&FCyLandEditorCustomNodeBuilder_TargetLayers::OnDebugModeColorChannelChanged, Target, 4)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("ViewMode.Debug_B", "B"))
							]
						]
					]
				]
			];
	}

	return RowWidget;
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

FReply FCyLandEditorCustomNodeBuilder_TargetLayers::HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, int32 SlotIndex, SVerticalBox::FSlot* Slot)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();

	if (CyLandEdMode != nullptr)
	{
		const TArray<FName>& TargetShownList = CyLandEdMode->GetTargetShownList();

		if (TargetShownList.IsValidIndex(SlotIndex))
		{
			const TArray<FName>* TargetDisplayOrderList = CyLandEdMode->GetTargetDisplayOrderList();

			if (TargetDisplayOrderList != nullptr)
			{
				FName ShownTargetName = CyLandEdMode->UISettings->ShowUnusedLayers && TargetShownList.IsValidIndex(SlotIndex + CyLandEdMode->GetTargetLayerStartingIndex()) ? TargetShownList[SlotIndex + CyLandEdMode->GetTargetLayerStartingIndex()] : TargetShownList[SlotIndex];
				int32 DisplayOrderLayerIndex = TargetDisplayOrderList->Find(ShownTargetName);

				if (TargetDisplayOrderList->IsValidIndex(DisplayOrderLayerIndex))
				{
					const TArray<TSharedRef<FCyLandTargetListInfo>>& TargetList = CyLandEdMode->GetTargetList();

					for (const TSharedRef<FCyLandTargetListInfo>& TargetInfo : TargetList)
					{
						if (TargetInfo->LayerName == (*TargetDisplayOrderList)[DisplayOrderLayerIndex])
						{
							TSharedPtr<SWidget> Row = GenerateRow(TargetInfo);

							if (Row.IsValid())
							{
								return FReply::Handled().BeginDragDrop(FTargetLayerDragDropOp::New(SlotIndex, Slot, Row));
							}
						}
					}
				}
			}
		}
	}

	return FReply::Unhandled();
}

TOptional<SDragAndDropVerticalBox::EItemDropZone> FCyLandEditorCustomNodeBuilder_TargetLayers::HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone DropZone, SVerticalBox::FSlot* Slot)
{
	TSharedPtr<FTargetLayerDragDropOp> DragDropOperation = DragDropEvent.GetOperationAs<FTargetLayerDragDropOp>();

	if (DragDropOperation.IsValid())
	{
		return DropZone;
	}

	return TOptional<SDragAndDropVerticalBox::EItemDropZone>();
}

FReply FCyLandEditorCustomNodeBuilder_TargetLayers::HandleAcceptDrop(FDragDropEvent const& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone DropZone, int32 SlotIndex, SVerticalBox::FSlot* Slot)
{
	TSharedPtr<FTargetLayerDragDropOp> DragDropOperation = DragDropEvent.GetOperationAs<FTargetLayerDragDropOp>();

	if (DragDropOperation.IsValid())
	{
		FEdModeCyLand* CyLandEdMode = GetEditorMode();

		if (CyLandEdMode != nullptr)
		{
			const TArray<FName>& TargetShownList = CyLandEdMode->GetTargetShownList();

			if (TargetShownList.IsValidIndex(DragDropOperation->SlotIndexBeingDragged) && TargetShownList.IsValidIndex(SlotIndex))
			{
				const TArray<FName>* TargetDisplayOrderList = CyLandEdMode->GetTargetDisplayOrderList();

				if (TargetDisplayOrderList != nullptr && TargetShownList.IsValidIndex(DragDropOperation->SlotIndexBeingDragged + CyLandEdMode->GetTargetLayerStartingIndex()) && TargetShownList.IsValidIndex(SlotIndex + CyLandEdMode->GetTargetLayerStartingIndex()))
				{
					int32 StartingLayerIndex = TargetDisplayOrderList->Find(CyLandEdMode->UISettings->ShowUnusedLayers ? TargetShownList[DragDropOperation->SlotIndexBeingDragged + CyLandEdMode->GetTargetLayerStartingIndex()] : TargetShownList[DragDropOperation->SlotIndexBeingDragged]);
					int32 DestinationLayerIndex = TargetDisplayOrderList->Find(CyLandEdMode->UISettings->ShowUnusedLayers ? TargetShownList[SlotIndex + CyLandEdMode->GetTargetLayerStartingIndex()] : TargetShownList[SlotIndex]);

					if (StartingLayerIndex != INDEX_NONE && DestinationLayerIndex != INDEX_NONE)
					{
						CyLandEdMode->MoveTargetLayerDisplayOrder(StartingLayerIndex, DestinationLayerIndex);

						return FReply::Handled();
					}
				}
			}
		}
	}

	return FReply::Unhandled();
}

bool FCyLandEditorCustomNodeBuilder_TargetLayers::GetTargetLayerIsSelected(const TSharedRef<FCyLandTargetListInfo> Target)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode)
	{
		return
			CyLandEdMode->CurrentToolTarget.TargetType == Target->TargetType &&
			CyLandEdMode->CurrentToolTarget.LayerName == Target->LayerName &&
			CyLandEdMode->CurrentToolTarget.LayerInfo == Target->LayerInfoObj; // may be null
	}

	return false;
}

void FCyLandEditorCustomNodeBuilder_TargetLayers::OnTargetSelectionChanged(const TSharedRef<FCyLandTargetListInfo> Target)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode)
	{
		CyLandEdMode->CurrentToolTarget.TargetType = Target->TargetType;
		if (Target->TargetType == ECyLandToolTargetType::Heightmap)
		{
			checkSlow(Target->LayerInfoObj == NULL);
			CyLandEdMode->CurrentToolTarget.LayerInfo = NULL;
			CyLandEdMode->CurrentToolTarget.LayerName = NAME_None;
		}
		else
		{
			CyLandEdMode->CurrentToolTarget.LayerInfo = Target->LayerInfoObj;
			CyLandEdMode->CurrentToolTarget.LayerName = Target->LayerName;
		}
	}
}

TSharedPtr<SWidget> FCyLandEditorCustomNodeBuilder_TargetLayers::OnTargetLayerContextMenuOpening(const TSharedRef<FCyLandTargetListInfo> Target)
{
	if (Target->TargetType == ECyLandToolTargetType::Heightmap || Target->LayerInfoObj != NULL)
	{
		FMenuBuilder MenuBuilder(true, NULL);

		MenuBuilder.BeginSection("CyLandEditorLayerActions", LOCTEXT("LayerContextMenu.Heading", "Layer Actions"));
		{
			// Export
			FUIAction ExportAction = FUIAction(FExecuteAction::CreateStatic(&FCyLandEditorCustomNodeBuilder_TargetLayers::OnExportLayer, Target));
			MenuBuilder.AddMenuEntry(LOCTEXT("LayerContextMenu.Export", "Export to file"), FText(), FSlateIcon(), ExportAction);

			// Import
			FUIAction ImportAction = FUIAction(FExecuteAction::CreateStatic(&FCyLandEditorCustomNodeBuilder_TargetLayers::OnImportLayer, Target));
			MenuBuilder.AddMenuEntry(LOCTEXT("LayerContextMenu.Import", "Import from file"), FText(), FSlateIcon(), ImportAction);

			// Reimport
			const FString& ReimportPath = Target->ReimportFilePath();

			if (!ReimportPath.IsEmpty())
			{
				FUIAction ReImportAction = FUIAction(FExecuteAction::CreateStatic(&FCyLandEditorCustomNodeBuilder_TargetLayers::OnReimportLayer, Target));
				MenuBuilder.AddMenuEntry(FText::Format(LOCTEXT("LayerContextMenu.ReImport", "Reimport from {0}"), FText::FromString(ReimportPath)), FText(), FSlateIcon(), ReImportAction);
			}

			if (Target->TargetType == ECyLandToolTargetType::Weightmap)
			{
				MenuBuilder.AddMenuSeparator();

				// Fill
				FUIAction FillAction = FUIAction(FExecuteAction::CreateStatic(&FCyLandEditorCustomNodeBuilder_TargetLayers::OnFillLayer, Target));
				MenuBuilder.AddMenuEntry(LOCTEXT("LayerContextMenu.Fill", "Fill Layer"), LOCTEXT("LayerContextMenu.Fill_Tooltip", "Fills this layer to 100% across the entire CyLand. If this is a weight-blended (normal) layer, all other weight-blended layers will be cleared."), FSlateIcon(), FillAction);

				// Clear
				FUIAction ClearAction = FUIAction(FExecuteAction::CreateStatic(&FCyLandEditorCustomNodeBuilder_TargetLayers::OnClearLayer, Target));
				MenuBuilder.AddMenuEntry(LOCTEXT("LayerContextMenu.Clear", "Clear Layer"), LOCTEXT("LayerContextMenu.Clear_Tooltip", "Clears this layer to 0% across the entire CyLand. If this is a weight-blended (normal) layer, other weight-blended layers will be adjusted to compensate."), FSlateIcon(), ClearAction);

				// Rebuild material instances
				FUIAction RebuildAction = FUIAction(FExecuteAction::CreateStatic(&FCyLandEditorCustomNodeBuilder_TargetLayers::OnRebuildMICs, Target));
				MenuBuilder.AddMenuEntry(LOCTEXT("LayerContextMenu.Rebuild", "Rebuild Materials"), LOCTEXT("LayerContextMenu.Rebuild_Tooltip", "Rebuild material instances used for this CyLand."), FSlateIcon(), ClearAction);
			}
			else if (Target->TargetType == ECyLandToolTargetType::Visibility)
			{
				MenuBuilder.AddMenuSeparator();

				// Clear
				FUIAction ClearAction = FUIAction(FExecuteAction::CreateStatic(&FCyLandEditorCustomNodeBuilder_TargetLayers::OnClearLayer, Target));
				MenuBuilder.AddMenuEntry(LOCTEXT("LayerContextMenu.ClearHoles", "Remove all Holes"), FText(), FSlateIcon(), ClearAction);
			}
		}
		MenuBuilder.EndSection();

		return MenuBuilder.MakeWidget();
	}

	return NULL;
}

void FCyLandEditorCustomNodeBuilder_TargetLayers::OnExportLayer(const TSharedRef<FCyLandTargetListInfo> Target)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode)
	{
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();

		UCyLandInfo* CyLandInfo = Target->CyLandInfo.Get();
		UCyLandLayerInfoObject* LayerInfoObj = Target->LayerInfoObj.Get(); // NULL for heightmaps

		// Prompt for filename
		FString SaveDialogTitle;
		FString DefaultFileName;
		const TCHAR* FileTypes = nullptr;

		ICyLandEditorModule& CyLandEditorModule = FModuleManager::GetModuleChecked<ICyLandEditorModule>("CyLandEditor");

		if (Target->TargetType == ECyLandToolTargetType::Heightmap)
		{
			SaveDialogTitle = LOCTEXT("ExportHeightmap", "Export CyLand Heightmap").ToString();
			DefaultFileName = TEXT("Heightmap");
			FileTypes = CyLandEditorModule.GetHeightmapExportDialogTypeString();
		}
		else //if (Target->TargetType == ECyLandToolTargetType::Weightmap)
		{
			SaveDialogTitle = FText::Format(LOCTEXT("ExportLayer", "Export CyLand Layer: {0}"), FText::FromName(LayerInfoObj->LayerName)).ToString();
			DefaultFileName = LayerInfoObj->LayerName.ToString();
			FileTypes = CyLandEditorModule.GetWeightmapExportDialogTypeString();
		}

		// Prompt the user for the filenames
		TArray<FString> SaveFilenames;
		bool bOpened = DesktopPlatform->SaveFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			*SaveDialogTitle,
			*CyLandEdMode->UISettings->LastImportPath,
			*DefaultFileName,
			FileTypes,
			EFileDialogFlags::None,
			SaveFilenames
			);

		if (bOpened)
		{
			const FString& SaveFilename(SaveFilenames[0]);
			CyLandEdMode->UISettings->LastImportPath = FPaths::GetPath(SaveFilename);

			// Actually do the export
			if (Target->TargetType == ECyLandToolTargetType::Heightmap)
			{
				CyLandInfo->ExportHeightmap(SaveFilename);
			}
			else //if (Target->TargetType == ECyLandToolTargetType::Weightmap)
			{
				CyLandInfo->ExportLayer(LayerInfoObj, SaveFilename);
			}

			Target->ReimportFilePath() = SaveFilename;
		}
	}
}

void FCyLandEditorCustomNodeBuilder_TargetLayers::OnImportLayer(const TSharedRef<FCyLandTargetListInfo> Target)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode)
	{
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();

		UCyLandInfo* CyLandInfo = Target->CyLandInfo.Get();
		UCyLandLayerInfoObject* LayerInfoObj = Target->LayerInfoObj.Get(); // NULL for heightmaps

		// Prompt for filename
		FString OpenDialogTitle;
		FString DefaultFileName;
		const TCHAR* FileTypes = nullptr;

		ICyLandEditorModule& CyLandEditorModule = FModuleManager::GetModuleChecked<ICyLandEditorModule>("CyLandEditor");

		if (Target->TargetType == ECyLandToolTargetType::Heightmap)
		{
			OpenDialogTitle = *LOCTEXT("ImportHeightmap", "Import CyLand Heightmap").ToString();
			DefaultFileName = TEXT("Heightmap.png");
			FileTypes = CyLandEditorModule.GetHeightmapImportDialogTypeString();
		}
		else //if (Target->TargetType == ECyLandToolTargetType::Weightmap)
		{
			OpenDialogTitle = *FText::Format(LOCTEXT("ImportLayer", "Import CyLand Layer: {0}"), FText::FromName(LayerInfoObj->LayerName)).ToString();
			DefaultFileName = *FString::Printf(TEXT("%s.png"), *(LayerInfoObj->LayerName.ToString()));
			FileTypes = CyLandEditorModule.GetWeightmapImportDialogTypeString();
		}

		// Prompt the user for the filenames
		TArray<FString> OpenFilenames;
		bool bOpened = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			*OpenDialogTitle,
			*CyLandEdMode->UISettings->LastImportPath,
			*DefaultFileName,
			FileTypes,
			EFileDialogFlags::None,
			OpenFilenames
			);

		if (bOpened)
		{
			const FString& OpenFilename(OpenFilenames[0]);
			CyLandEdMode->UISettings->LastImportPath = FPaths::GetPath(OpenFilename);

			// Actually do the Import
			CyLandEdMode->ImportData(*Target, OpenFilename);

			Target->ReimportFilePath() = OpenFilename;
		}
	}
}

void FCyLandEditorCustomNodeBuilder_TargetLayers::OnReimportLayer(const TSharedRef<FCyLandTargetListInfo> Target)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode)
	{
		CyLandEdMode->ReimportData(*Target);
	}
}

void FCyLandEditorCustomNodeBuilder_TargetLayers::OnFillLayer(const TSharedRef<FCyLandTargetListInfo> Target)
{
	FScopedTransaction Transaction(LOCTEXT("Undo_FillLayer", "Filling CyLand Layer"));
	if (Target->CyLandInfo.IsValid() && Target->LayerInfoObj.IsValid())
	{
		FCyLandEditDataInterface CyLandEdit(Target->CyLandInfo.Get());
		CyLandEdit.FillLayer(Target->LayerInfoObj.Get());
	}
}

void FCyLandEditorCustomNodeBuilder_TargetLayers::FillEmptyLayers(UCyLandInfo* CyLandInfo, UCyLandLayerInfoObject* CyLandInfoObject)
{
	FCyLandEditDataInterface CyLandEdit(CyLandInfo);
	CyLandEdit.FillEmptyLayers(CyLandInfoObject);
}


void FCyLandEditorCustomNodeBuilder_TargetLayers::OnClearLayer(const TSharedRef<FCyLandTargetListInfo> Target)
{
	FScopedTransaction Transaction(LOCTEXT("Undo_ClearLayer", "Clearing CyLand Layer"));
	if (Target->CyLandInfo.IsValid() && Target->LayerInfoObj.IsValid())
	{
		FCyLandEditDataInterface CyLandEdit(Target->CyLandInfo.Get());
		CyLandEdit.DeleteLayer(Target->LayerInfoObj.Get());
	}
}

void FCyLandEditorCustomNodeBuilder_TargetLayers::OnRebuildMICs(const TSharedRef<FCyLandTargetListInfo> Target)
{
	if (Target->CyLandInfo.IsValid())
	{
		Target->CyLandInfo.Get()->GetCyLandProxy()->UpdateAllComponentMaterialInstances();
	}
}


bool FCyLandEditorCustomNodeBuilder_TargetLayers::ShouldFilterLayerInfo(const FAssetData& AssetData, FName LayerName)
{
	const FName LayerNameMetaData = AssetData.GetTagValueRef<FName>("LayerName");
	if (!LayerNameMetaData.IsNone())
	{
		return LayerNameMetaData != LayerName;
	}

	UCyLandLayerInfoObject* LayerInfo = CastChecked<UCyLandLayerInfoObject>(AssetData.GetAsset());
	return LayerInfo->LayerName != LayerName;
}

void FCyLandEditorCustomNodeBuilder_TargetLayers::OnTargetLayerSetObject(const FAssetData& AssetData, const TSharedRef<FCyLandTargetListInfo> Target)
{
	// Can't assign null to a layer
	UObject* Object = AssetData.GetAsset();
	if (Object == NULL)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("Undo_UseExisting", "Assigning Layer to CyLand"));

	UCyLandLayerInfoObject* SelectedLayerInfo = const_cast<UCyLandLayerInfoObject*>(CastChecked<UCyLandLayerInfoObject>(Object));

	if (SelectedLayerInfo != Target->LayerInfoObj.Get())
	{
		if (ensure(SelectedLayerInfo->LayerName == Target->GetLayerName()))
		{
			UCyLandInfo* CyLandInfo = Target->CyLandInfo.Get();
			CyLandInfo->Modify();
			if (Target->LayerInfoObj.IsValid())
			{
				int32 Index = CyLandInfo->GetLayerInfoIndex(Target->LayerInfoObj.Get(), Target->Owner.Get());
				if (ensure(Index != INDEX_NONE))
				{
					FCyLandInfoLayerSettings& LayerSettings = CyLandInfo->Layers[Index];

					CyLandInfo->ReplaceLayer(LayerSettings.LayerInfoObj, SelectedLayerInfo);

					LayerSettings.LayerInfoObj = SelectedLayerInfo;
				}
			}
			else
			{
				int32 Index = CyLandInfo->GetLayerInfoIndex(Target->LayerName, Target->Owner.Get());
				if (ensure(Index != INDEX_NONE))
				{
					FCyLandInfoLayerSettings& LayerSettings = CyLandInfo->Layers[Index];
					LayerSettings.LayerInfoObj = SelectedLayerInfo;

					Target->CyLandInfo->CreateLayerEditorSettingsFor(SelectedLayerInfo);
				}
			}

			FEdModeCyLand* CyLandEdMode = GetEditorMode();
			if (CyLandEdMode)
			{
				if (CyLandEdMode->CurrentToolTarget.LayerName == Target->LayerName
					&& CyLandEdMode->CurrentToolTarget.LayerInfo == Target->LayerInfoObj)
				{
					CyLandEdMode->CurrentToolTarget.LayerInfo = SelectedLayerInfo;
				}
				CyLandEdMode->UpdateTargetList();
			}

			FillEmptyLayers(CyLandInfo, SelectedLayerInfo);
		}
		else
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("Error_LayerNameMismatch", "Can't use this layer info because the layer name does not match"));
		}
	}
}

EVisibility FCyLandEditorCustomNodeBuilder_TargetLayers::GetTargetLayerInfoSelectorVisibility(const TSharedRef<FCyLandTargetListInfo> Target)
{
	if (Target->TargetType == ECyLandToolTargetType::Weightmap)
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

bool FCyLandEditorCustomNodeBuilder_TargetLayers::GetTargetLayerCreateEnabled(const TSharedRef<FCyLandTargetListInfo> Target)
{
	if (!Target->LayerInfoObj.IsValid())
	{
		return true;
	}

	return false;
}

EVisibility FCyLandEditorCustomNodeBuilder_TargetLayers::GetTargetLayerMakePublicVisibility(const TSharedRef<FCyLandTargetListInfo> Target)
{
	if (Target->bValid && Target->LayerInfoObj.IsValid() && Target->LayerInfoObj->GetOutermost()->ContainsMap())
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

EVisibility FCyLandEditorCustomNodeBuilder_TargetLayers::GetTargetLayerDeleteVisibility(const TSharedRef<FCyLandTargetListInfo> Target)
{
	if (!Target->bValid)
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

TSharedRef<SWidget> FCyLandEditorCustomNodeBuilder_TargetLayers::OnGetTargetLayerCreateMenu(const TSharedRef<FCyLandTargetListInfo> Target)
{
	FMenuBuilder MenuBuilder(true, NULL);

	MenuBuilder.AddMenuEntry(LOCTEXT("Menu_Create_Blended", "Weight-Blended Layer (normal)"), FText(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&FCyLandEditorCustomNodeBuilder_TargetLayers::OnTargetLayerCreateClicked, Target, false)));

	MenuBuilder.AddMenuEntry(LOCTEXT("Menu_Create_NoWeightBlend", "Non Weight-Blended Layer"), FText(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&FCyLandEditorCustomNodeBuilder_TargetLayers::OnTargetLayerCreateClicked, Target, true)));

	return MenuBuilder.MakeWidget();
}

void FCyLandEditorCustomNodeBuilder_TargetLayers::OnTargetLayerCreateClicked(const TSharedRef<FCyLandTargetListInfo> Target, bool bNoWeightBlend)
{
	check(!Target->LayerInfoObj.IsValid());

	FScopedTransaction Transaction(LOCTEXT("Undo_Create", "Creating New CyLand Layer"));

	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode)
	{
		FName LayerName = Target->GetLayerName();
		ULevel* Level = Target->Owner->GetLevel();

		// Build default layer object name and package name
		FName LayerObjectName = FName(*FString::Printf(TEXT("%s_LayerInfo"), *LayerName.ToString()));
		FString Path = Level->GetOutermost()->GetName() + TEXT("_sharedassets/");
		if (Path.StartsWith("/Temp/"))
		{
			Path = FString("/Game/") + Path.RightChop(FString("/Temp/").Len());
		}
		FString PackageName = Path + LayerObjectName.ToString();

		TSharedRef<SDlgPickAssetPath> NewLayerDlg =
			SNew(SDlgPickAssetPath)
			.Title(LOCTEXT("CreateNewLayerInfo", "Create New CyLand Layer Info Object"))
			.DefaultAssetPath(FText::FromString(PackageName));

		if (NewLayerDlg->ShowModal() != EAppReturnType::Cancel)
		{
			PackageName = NewLayerDlg->GetFullAssetPath().ToString();
			LayerObjectName = FName(*NewLayerDlg->GetAssetName().ToString());

			UPackage* Package = CreatePackage(NULL, *PackageName);
			UCyLandLayerInfoObject* LayerInfo = NewObject<UCyLandLayerInfoObject>(Package, LayerObjectName, RF_Public | RF_Standalone | RF_Transactional);
			LayerInfo->LayerName = LayerName;
			LayerInfo->bNoWeightBlend = bNoWeightBlend;

			UCyLandInfo* CyLandInfo = Target->CyLandInfo.Get();
			CyLandInfo->Modify();
			int32 Index = CyLandInfo->GetLayerInfoIndex(LayerName, Target->Owner.Get());
			if (Index == INDEX_NONE)
			{
				CyLandInfo->Layers.Add(FCyLandInfoLayerSettings(LayerInfo, Target->Owner.Get()));
			}
			else
			{
				CyLandInfo->Layers[Index].LayerInfoObj = LayerInfo;
			}

			if (CyLandEdMode->CurrentToolTarget.LayerName == Target->LayerName
				&& CyLandEdMode->CurrentToolTarget.LayerInfo == Target->LayerInfoObj)
			{
				CyLandEdMode->CurrentToolTarget.LayerInfo = LayerInfo;
			}

			Target->LayerInfoObj = LayerInfo;
			Target->CyLandInfo->CreateLayerEditorSettingsFor(LayerInfo);

			// Notify the asset registry
			FAssetRegistryModule::AssetCreated(LayerInfo);

			// Mark the package dirty...
			Package->MarkPackageDirty();

			// Show in the content browser
			TArray<UObject*> Objects;
			Objects.Add(LayerInfo);
			GEditor->SyncBrowserToObjects(Objects);
			
			CyLandEdMode->TargetsListUpdated.Broadcast();

			FillEmptyLayers(CyLandInfo, LayerInfo);
		}
	}
}

FReply FCyLandEditorCustomNodeBuilder_TargetLayers::OnTargetLayerMakePublicClicked(const TSharedRef<FCyLandTargetListInfo> Target)
{
	FScopedTransaction Transaction(LOCTEXT("Undo_MakePublic", "Make Layer Public"));
	TArray<UObject*> Objects;
	Objects.Add(Target->LayerInfoObj.Get());

	FString Path = Target->Owner->GetOutermost()->GetName() + TEXT("_sharedassets");
	bool bSucceed = ObjectTools::RenameObjects(Objects, false, TEXT(""), Path);
	if (bSucceed)
	{
		FEdModeCyLand* CyLandEdMode = GetEditorMode();
		if (CyLandEdMode)
		{
			CyLandEdMode->UpdateTargetList();
		}
	}
	else
	{
		Transaction.Cancel();
	}

	return FReply::Handled();
}

FReply FCyLandEditorCustomNodeBuilder_TargetLayers::OnTargetLayerDeleteClicked(const TSharedRef<FCyLandTargetListInfo> Target)
{
	check(Target->CyLandInfo.IsValid());

	if (FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("Prompt_DeleteLayer", "Are you sure you want to delete this layer?")) == EAppReturnType::Yes)
	{
		FScopedTransaction Transaction(LOCTEXT("Undo_Delete", "Delete Layer"));

		Target->CyLandInfo->DeleteLayer(Target->LayerInfoObj.Get(), Target->LayerName);

		FEdModeCyLand* CyLandEdMode = GetEditorMode();
		if (CyLandEdMode)
		{
			CyLandEdMode->UpdateTargetList();
			CyLandEdMode->UpdateShownLayerList();
		}
	}

	return FReply::Handled();
}

FSlateColor FCyLandEditorCustomNodeBuilder_TargetLayers::GetLayerUsageDebugColor(const TSharedRef<FCyLandTargetListInfo> Target)
{
	if (GCyLandViewMode == ECyLandViewMode::LayerUsage && Target->TargetType != ECyLandToolTargetType::Heightmap && ensure(Target->LayerInfoObj.IsValid()))
	{
		return FSlateColor(Target->LayerInfoObj->LayerUsageDebugColor);
	}
	return FSlateColor(FLinearColor(0, 0, 0, 0));
}

EVisibility FCyLandEditorCustomNodeBuilder_TargetLayers::GetDebugModeLayerUsageVisibility(const TSharedRef<FCyLandTargetListInfo> Target)
{
	if (GCyLandViewMode == ECyLandViewMode::LayerUsage && Target->TargetType != ECyLandToolTargetType::Heightmap && Target->LayerInfoObj.IsValid())
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

EVisibility FCyLandEditorCustomNodeBuilder_TargetLayers::GetDebugModeLayerUsageVisibility_Invert(const TSharedRef<FCyLandTargetListInfo> Target)
{
	if (GCyLandViewMode == ECyLandViewMode::LayerUsage && Target->TargetType != ECyLandToolTargetType::Heightmap && Target->LayerInfoObj.IsValid())
	{
		return EVisibility::Collapsed;
	}

	return EVisibility::Visible;
}

EVisibility FCyLandEditorCustomNodeBuilder_TargetLayers::GetDebugModeColorChannelVisibility(const TSharedRef<FCyLandTargetListInfo> Target)
{
	if (GCyLandViewMode == ECyLandViewMode::DebugLayer && Target->TargetType != ECyLandToolTargetType::Heightmap && Target->LayerInfoObj.IsValid())
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

ECheckBoxState FCyLandEditorCustomNodeBuilder_TargetLayers::DebugModeColorChannelIsChecked(const TSharedRef<FCyLandTargetListInfo> Target, int32 Channel)
{
	if (Target->DebugColorChannel == Channel)
	{
		return ECheckBoxState::Checked;
	}

	return ECheckBoxState::Unchecked;
}

void FCyLandEditorCustomNodeBuilder_TargetLayers::OnDebugModeColorChannelChanged(ECheckBoxState NewCheckedState, const TSharedRef<FCyLandTargetListInfo> Target, int32 Channel)
{
	if (NewCheckedState == ECheckBoxState::Checked)
	{
		// Enable on us and disable colour channel on other targets
		if (ensure(Target->LayerInfoObj != NULL))
		{
			UCyLandInfo* CyLandInfo = Target->CyLandInfo.Get();
			int32 Index = CyLandInfo->GetLayerInfoIndex(Target->LayerInfoObj.Get(), Target->Owner.Get());
			if (ensure(Index != INDEX_NONE))
			{
				for (auto It = CyLandInfo->Layers.CreateIterator(); It; It++)
				{
					FCyLandInfoLayerSettings& LayerSettings = *It;
					if (It.GetIndex() == Index)
					{
						LayerSettings.DebugColorChannel = Channel;
					}
					else
					{
						LayerSettings.DebugColorChannel &= ~Channel;
					}
				}
				CyLandInfo->UpdateDebugColorMaterial();

				FEdModeCyLand* CyLandEdMode = GetEditorMode();
				if (CyLandEdMode)
				{
					CyLandEdMode->UpdateTargetList();
				}
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////

void SCyLandEditorSelectableBorder::Construct(const FArguments& InArgs)
{
	SBorder::Construct(
		SBorder::FArguments()
		.HAlign(InArgs._HAlign)
		.VAlign(InArgs._VAlign)
		.Padding(InArgs._Padding)
		.BorderImage(this, &SCyLandEditorSelectableBorder::GetBorder)
		.Content()
		[
			InArgs._Content.Widget
		]
	);

	OnContextMenuOpening = InArgs._OnContextMenuOpening;
	OnSelected = InArgs._OnSelected;
	IsSelected = InArgs._IsSelected;
}

FReply SCyLandEditorSelectableBorder::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MyGeometry.IsUnderLocation(MouseEvent.GetScreenSpacePosition()))
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton &&
			OnSelected.IsBound())
		{
			OnSelected.Execute();

			return FReply::Handled().ReleaseMouseCapture();
		}
		else if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton &&
			OnContextMenuOpening.IsBound())
		{
			TSharedPtr<SWidget> Content = OnContextMenuOpening.Execute();
			if (Content.IsValid())
			{
				FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();

				FSlateApplication::Get().PushMenu(SharedThis(this), WidgetPath, Content.ToSharedRef(), MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
			}

			return FReply::Handled().ReleaseMouseCapture();
		}
	}

	return FReply::Unhandled();
}

const FSlateBrush* SCyLandEditorSelectableBorder::GetBorder() const
{
	const bool bIsSelected = IsSelected.Get();
	const bool bHovered = IsHovered() && OnSelected.IsBound();

	if (bIsSelected)
	{
		return bHovered
			? FEditorStyle::GetBrush("CyLandEditor.TargetList", ".RowSelectedHovered")
			: FEditorStyle::GetBrush("CyLandEditor.TargetList", ".RowSelected");
	}
	else
	{
		return bHovered
			? FEditorStyle::GetBrush("CyLandEditor.TargetList", ".RowBackgroundHovered")
			: FEditorStyle::GetBrush("CyLandEditor.TargetList", ".RowBackground");
	}
}

TSharedRef<FTargetLayerDragDropOp> FTargetLayerDragDropOp::New(int32 InSlotIndexBeingDragged, SVerticalBox::FSlot* InSlotBeingDragged, TSharedPtr<SWidget> WidgetToShow)
{
	TSharedRef<FTargetLayerDragDropOp> Operation = MakeShareable(new FTargetLayerDragDropOp);

	Operation->MouseCursor = EMouseCursor::GrabHandClosed;
	Operation->SlotIndexBeingDragged = InSlotIndexBeingDragged;
	Operation->SlotBeingDragged = InSlotBeingDragged;
	Operation->WidgetToShow = WidgetToShow;

	Operation->Construct();

	return Operation;
}

FTargetLayerDragDropOp::~FTargetLayerDragDropOp()
{
}

TSharedPtr<SWidget> FTargetLayerDragDropOp::GetDefaultDecorator() const
{
	return SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("ContentBrowser.AssetDragDropTooltipBackground"))
			.Content()
			[
				WidgetToShow.ToSharedRef()
			];
		
}

#undef LOCTEXT_NAMESPACE
