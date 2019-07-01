// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CyLandEditorDetailCustomization_NewCyLand.h"
#include "Framework/Commands/UIAction.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "SlateOptMacros.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Notifications/SErrorText.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "CyLandEditorModule.h"
#include "CyLandEditorObject.h"
#include "CyLand.h"
#include "CyLandEditorUtils.h"
#include "NewCyLandUtils.h"

#include "DetailLayoutBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "DetailCategoryBuilder.h"
#include "PropertyCustomizationHelpers.h"

#include "SCyLandEditor.h"
#include "Dialogs/DlgPickAssetPath.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Input/SRotatorInputBox.h"
#include "ScopedTransaction.h"
#include "DesktopPlatformModule.h"
#include "AssetRegistryModule.h"

#include "TutorialMetaData.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "CyLandDataAccess.h"

#define LOCTEXT_NAMESPACE "CyLandEditor.NewCyLand"

TSharedRef<IDetailCustomization> FCyLandEditorDetailCustomization_NewCyLand::MakeInstance()
{
	return MakeShareable(new FCyLandEditorDetailCustomization_NewCyLand);
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void FCyLandEditorDetailCustomization_NewCyLand::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	if (!IsToolActive("NewCyLand"))
	{
		return;
	}

	IDetailCategoryBuilder& NewCyLandCategory = DetailBuilder.EditCategory("New CyLand");

	NewCyLandCategory.AddCustomRow(FText::GetEmpty())
	[
		SNew(SUniformGridPanel)
		.SlotPadding(FMargin(10, 2))
		+ SUniformGridPanel::Slot(0, 0)
		[
			SNew(SCheckBox)
			.Style(FEditorStyle::Get(), "RadioButton")
			.IsChecked(this, &FCyLandEditorDetailCustomization_NewCyLand::NewCyLandModeIsChecked, ENewCyLandPreviewMode::NewCyLand)
			.OnCheckStateChanged(this, &FCyLandEditorDetailCustomization_NewCyLand::OnNewCyLandModeChanged, ENewCyLandPreviewMode::NewCyLand)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NewCyLand", "Create New"))
			]
		]
		+ SUniformGridPanel::Slot(1, 0)
		[
			SNew(SCheckBox)
			.Style(FEditorStyle::Get(), "RadioButton")
			.IsChecked(this, &FCyLandEditorDetailCustomization_NewCyLand::NewCyLandModeIsChecked, ENewCyLandPreviewMode::ImportCyLand)
			.OnCheckStateChanged(this, &FCyLandEditorDetailCustomization_NewCyLand::OnNewCyLandModeChanged, ENewCyLandPreviewMode::ImportCyLand)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ImportCyLand", "Import from File"))
			]
		]
	];

	TSharedRef<IPropertyHandle> PropertyHandle_HeightmapFilename = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, ImportCyLand_HeightmapFilename));
	TSharedRef<IPropertyHandle> PropertyHandle_HeightmapImportResult = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, ImportCyLand_HeightmapImportResult));
	TSharedRef<IPropertyHandle> PropertyHandle_HeightmapErrorMessage = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, ImportCyLand_HeightmapErrorMessage));
	DetailBuilder.HideProperty(PropertyHandle_HeightmapImportResult);
	DetailBuilder.HideProperty(PropertyHandle_HeightmapErrorMessage);
	PropertyHandle_HeightmapFilename->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FCyLandEditorDetailCustomization_NewCyLand::OnImportHeightmapFilenameChanged));
	NewCyLandCategory.AddProperty(PropertyHandle_HeightmapFilename)
	.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateStatic(&GetVisibilityOnlyInNewCyLandMode, ENewCyLandPreviewMode::ImportCyLand)))
	.CustomWidget()
	.NameContent()
	[
		PropertyHandle_HeightmapFilename->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(250.0f)
	.MaxDesiredWidth(0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0,0,2,0)
		[
			SNew(SErrorText)
			.Visibility_Static(&GetHeightmapErrorVisibility, PropertyHandle_HeightmapImportResult)
			.BackgroundColor_Static(&GetHeightmapErrorColor, PropertyHandle_HeightmapImportResult)
			.ErrorText(NSLOCTEXT("UnrealEd", "Error", "!"))
			.ToolTip(
				SNew(SToolTip)
				.Text_Static(&GetPropertyValue<FText>, PropertyHandle_HeightmapErrorMessage)
			)
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1)
		[
			SNew(SEditableTextBox)
			.Font(DetailBuilder.GetDetailFont())
			.Text_Static(&GetPropertyValueText, PropertyHandle_HeightmapFilename)
			.OnTextCommitted_Static(&SetImportHeightmapFilenameString, PropertyHandle_HeightmapFilename)
			.HintText(LOCTEXT("Import_HeightmapNotSet", "(Please specify a heightmap)"))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(1,0,0,0)
		[
			SNew(SButton)
			//.Font(DetailBuilder.GetDetailFont())
			.ContentPadding(FMargin(4, 0))
			.Text(NSLOCTEXT("UnrealEd", "GenericOpenDialog", "..."))
			.OnClicked_Static(&OnImportHeightmapFilenameButtonClicked, PropertyHandle_HeightmapFilename)
		]
	];

	NewCyLandCategory.AddCustomRow(LOCTEXT("HeightmapResolution", "Heightmap Resolution"))
	.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateStatic(&GetVisibilityOnlyInNewCyLandMode, ENewCyLandPreviewMode::ImportCyLand)))
	.NameContent()
	[
		SNew(SBox)
		.VAlign(VAlign_Center)
		.Padding(FMargin(2))
		[
			SNew(STextBlock)
			.Font(DetailBuilder.GetDetailFont())
			.Text(LOCTEXT("HeightmapResolution", "Heightmap Resolution"))
		]
	]
	.ValueContent()
	[
		SNew(SBox)
		.Padding(FMargin(0,0,12,0)) // Line up with the other properties due to having no reset to default button
		[
			SNew(SComboButton)
			.OnGetMenuContent(this, &FCyLandEditorDetailCustomization_NewCyLand::GetImportCyLandResolutionMenu)
			.ContentPadding(2)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Font(DetailBuilder.GetDetailFont())
				.Text(this, &FCyLandEditorDetailCustomization_NewCyLand::GetImportCyLandResolution)
			]
		]
	];

	TSharedRef<IPropertyHandle> PropertyHandle_Material = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, NewCyLand_Material));
	NewCyLandCategory.AddProperty(PropertyHandle_Material);

	NewCyLandCategory.AddCustomRow(LOCTEXT("LayersLabel", "Layers"))
	.Visibility(TAttribute<EVisibility>(this, &FCyLandEditorDetailCustomization_NewCyLand::GetMaterialTipVisibility))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.Padding(15, 12, 0, 12)
		[
			SNew(STextBlock)
			.Font(DetailBuilder.GetDetailFont())
			.Text(LOCTEXT("Material_Tip","Hint: Assign a material to see CyLand layers"))
		]
	];

	TSharedRef<IPropertyHandle> PropertyHandle_AlphamapType = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, ImportCyLand_AlphamapType));
	NewCyLandCategory.AddProperty(PropertyHandle_AlphamapType)
	.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateStatic(&GetVisibilityOnlyInNewCyLandMode, ENewCyLandPreviewMode::ImportCyLand)));

	TSharedRef<IPropertyHandle> PropertyHandle_Layers = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, ImportCyLand_Layers));
	NewCyLandCategory.AddProperty(PropertyHandle_Layers);

	TSharedRef<IPropertyHandle> PropertyHandle_Location = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, NewCyLand_Location));
	TSharedRef<IPropertyHandle> PropertyHandle_Location_X = PropertyHandle_Location->GetChildHandle("X").ToSharedRef();
	TSharedRef<IPropertyHandle> PropertyHandle_Location_Y = PropertyHandle_Location->GetChildHandle("Y").ToSharedRef();
	TSharedRef<IPropertyHandle> PropertyHandle_Location_Z = PropertyHandle_Location->GetChildHandle("Z").ToSharedRef();
	NewCyLandCategory.AddProperty(PropertyHandle_Location)
	.CustomWidget()
	.NameContent()
	[
		PropertyHandle_Location->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(125.0f * 3.0f) // copied from FComponentTransformDetails
	.MaxDesiredWidth(125.0f * 3.0f)
	[
		SNew(SVectorInputBox)
		.bColorAxisLabels(true)
		.Font(DetailBuilder.GetDetailFont())
		.X_Static(&GetOptionalPropertyValue<float>, PropertyHandle_Location_X)
		.Y_Static(&GetOptionalPropertyValue<float>, PropertyHandle_Location_Y)
		.Z_Static(&GetOptionalPropertyValue<float>, PropertyHandle_Location_Z)
		.OnXCommitted_Static(&SetPropertyValue<float>, PropertyHandle_Location_X)
		.OnYCommitted_Static(&SetPropertyValue<float>, PropertyHandle_Location_Y)
		.OnZCommitted_Static(&SetPropertyValue<float>, PropertyHandle_Location_Z)
	];

	TSharedRef<IPropertyHandle> PropertyHandle_Rotation = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, NewCyLand_Rotation));
	TSharedRef<IPropertyHandle> PropertyHandle_Rotation_Roll  = PropertyHandle_Rotation->GetChildHandle("Roll").ToSharedRef();
	TSharedRef<IPropertyHandle> PropertyHandle_Rotation_Pitch = PropertyHandle_Rotation->GetChildHandle("Pitch").ToSharedRef();
	TSharedRef<IPropertyHandle> PropertyHandle_Rotation_Yaw   = PropertyHandle_Rotation->GetChildHandle("Yaw").ToSharedRef();
	NewCyLandCategory.AddProperty(PropertyHandle_Rotation)
	.CustomWidget()
	.NameContent()
	[
		PropertyHandle_Rotation->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(125.0f * 3.0f) // copied from FComponentTransformDetails
	.MaxDesiredWidth(125.0f * 3.0f)
	[
		SNew(SRotatorInputBox)
		.bColorAxisLabels(true)
		.AllowResponsiveLayout(true)
		.Font(DetailBuilder.GetDetailFont())
		.Roll_Static(&GetOptionalPropertyValue<float>, PropertyHandle_Rotation_Roll)
		.Pitch_Static(&GetOptionalPropertyValue<float>, PropertyHandle_Rotation_Pitch)
		.Yaw_Static(&GetOptionalPropertyValue<float>, PropertyHandle_Rotation_Yaw)
		.OnYawCommitted_Static(&SetPropertyValue<float>, PropertyHandle_Rotation_Yaw) // not allowed to roll or pitch CyLand
		.OnYawChanged_Lambda([=](float NewValue){ ensure(PropertyHandle_Rotation_Yaw->SetValue(NewValue, EPropertyValueSetFlags::InteractiveChange) == FPropertyAccess::Success); })
	];

	TSharedRef<IPropertyHandle> PropertyHandle_Scale = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, NewCyLand_Scale));
	TSharedRef<IPropertyHandle> PropertyHandle_Scale_X = PropertyHandle_Scale->GetChildHandle("X").ToSharedRef();
	TSharedRef<IPropertyHandle> PropertyHandle_Scale_Y = PropertyHandle_Scale->GetChildHandle("Y").ToSharedRef();
	TSharedRef<IPropertyHandle> PropertyHandle_Scale_Z = PropertyHandle_Scale->GetChildHandle("Z").ToSharedRef();
	NewCyLandCategory.AddProperty(PropertyHandle_Scale)
	.CustomWidget()
	.NameContent()
	[
		PropertyHandle_Scale->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(125.0f * 3.0f) // copied from FComponentTransformDetails
	.MaxDesiredWidth(125.0f * 3.0f)
	[
		SNew(SVectorInputBox)
		.bColorAxisLabels(true)
		.Font(DetailBuilder.GetDetailFont())
		.X_Static(&GetOptionalPropertyValue<float>, PropertyHandle_Scale_X)
		.Y_Static(&GetOptionalPropertyValue<float>, PropertyHandle_Scale_Y)
		.Z_Static(&GetOptionalPropertyValue<float>, PropertyHandle_Scale_Z)
		.OnXCommitted_Static(&SetScale, PropertyHandle_Scale_X)
		.OnYCommitted_Static(&SetScale, PropertyHandle_Scale_Y)
		.OnZCommitted_Static(&SetScale, PropertyHandle_Scale_Z)
	];

	TSharedRef<IPropertyHandle> PropertyHandle_QuadsPerSection = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, NewCyLand_QuadsPerSection));
	NewCyLandCategory.AddProperty(PropertyHandle_QuadsPerSection)
	.CustomWidget()
	.NameContent()
	[
		PropertyHandle_QuadsPerSection->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SComboButton)
		.OnGetMenuContent_Static(&GetSectionSizeMenu, PropertyHandle_QuadsPerSection)
		.ContentPadding(2)
		.ButtonContent()
		[
			SNew(STextBlock)
			.Font(DetailBuilder.GetDetailFont())
			.Text_Static(&GetSectionSize, PropertyHandle_QuadsPerSection)
		]
	];

	TSharedRef<IPropertyHandle> PropertyHandle_SectionsPerComponent = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, NewCyLand_SectionsPerComponent));
	NewCyLandCategory.AddProperty(PropertyHandle_SectionsPerComponent)
	.CustomWidget()
	.NameContent()
	[
		PropertyHandle_SectionsPerComponent->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SComboButton)
		.OnGetMenuContent_Static(&GetSectionsPerComponentMenu, PropertyHandle_SectionsPerComponent)
		.ContentPadding(2)
		.ButtonContent()
		[
			SNew(STextBlock)
			.Font(DetailBuilder.GetDetailFont())
			.Text_Static(&GetSectionsPerComponent, PropertyHandle_SectionsPerComponent)
		]
	];

	TSharedRef<IPropertyHandle> PropertyHandle_ComponentCount = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, NewCyLand_ComponentCount));
	TSharedRef<IPropertyHandle> PropertyHandle_ComponentCount_X = PropertyHandle_ComponentCount->GetChildHandle("X").ToSharedRef();
	TSharedRef<IPropertyHandle> PropertyHandle_ComponentCount_Y = PropertyHandle_ComponentCount->GetChildHandle("Y").ToSharedRef();
	NewCyLandCategory.AddProperty(PropertyHandle_ComponentCount)
	.CustomWidget()
	.NameContent()
	[
		PropertyHandle_ComponentCount->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1)
		[
			SNew(SNumericEntryBox<int32>)
			.LabelVAlign(VAlign_Center)
			.Font(DetailBuilder.GetDetailFont())
			.MinValue(1)
			.MaxValue(32)
			.MinSliderValue(1)
			.MaxSliderValue(32)
			.AllowSpin(true)
			.UndeterminedString(NSLOCTEXT("PropertyEditor", "MultipleValues", "Multiple Values"))
			.Value_Static(&FCyLandEditorDetailCustomization_Base::OnGetValue<int32>, PropertyHandle_ComponentCount_X)
			.OnValueChanged_Static(&FCyLandEditorDetailCustomization_Base::OnValueChanged<int32>, PropertyHandle_ComponentCount_X)
			.OnValueCommitted_Static(&FCyLandEditorDetailCustomization_Base::OnValueCommitted<int32>, PropertyHandle_ComponentCount_X)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(DetailBuilder.GetDetailFont())
			.Text(FText::FromString(FString().AppendChar(0xD7))) // Multiply sign
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1)
		[
			SNew(SNumericEntryBox<int32>)
			.LabelVAlign(VAlign_Center)
			.Font(DetailBuilder.GetDetailFont())
			.MinValue(1)
			.MaxValue(32)
			.MinSliderValue(1)
			.MaxSliderValue(32)
			.AllowSpin(true)
			.UndeterminedString(NSLOCTEXT("PropertyEditor", "MultipleValues", "Multiple Values"))
			.Value_Static(&FCyLandEditorDetailCustomization_Base::OnGetValue<int32>, PropertyHandle_ComponentCount_Y)
			.OnValueChanged_Static(&FCyLandEditorDetailCustomization_Base::OnValueChanged<int32>, PropertyHandle_ComponentCount_Y)
			.OnValueCommitted_Static(&FCyLandEditorDetailCustomization_Base::OnValueCommitted<int32>, PropertyHandle_ComponentCount_Y)
		]
	];

	NewCyLandCategory.AddCustomRow(LOCTEXT("Resolution", "Overall Resolution"))
	.RowTag("CyLandEditor.OverallResolution")
	.NameContent()
	[
		SNew(SBox)
		.VAlign(VAlign_Center)
		.Padding(FMargin(2))
		[
			SNew(STextBlock)
			.Font(DetailBuilder.GetDetailFont())
			.Text(LOCTEXT("Resolution", "Overall Resolution"))
			.ToolTipText(TAttribute<FText>(this, &FCyLandEditorDetailCustomization_NewCyLand::GetOverallResolutionTooltip))
		]
	]
	.ValueContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1)
		[
			SNew(SNumericEntryBox<int32>)
			.Font(DetailBuilder.GetDetailFont())
			.MinValue(1)
			.MaxValue(8192)
			.MinSliderValue(1)
			.MaxSliderValue(8192)
			.AllowSpin(true)
			//.MinSliderValue(TAttribute<TOptional<int32> >(this, &FCyLandEditorDetailCustomization_NewCyLand::GetMinCyLandResolution))
			//.MaxSliderValue(TAttribute<TOptional<int32> >(this, &FCyLandEditorDetailCustomization_NewCyLand::GetMaxCyLandResolution))
			.Value(this, &FCyLandEditorDetailCustomization_NewCyLand::GetCyLandResolutionX)
			.OnValueChanged(this, &FCyLandEditorDetailCustomization_NewCyLand::OnChangeCyLandResolutionX)
			.OnValueCommitted(this, &FCyLandEditorDetailCustomization_NewCyLand::OnCommitCyLandResolutionX)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(DetailBuilder.GetDetailFont())
			.Text(FText::FromString(FString().AppendChar(0xD7))) // Multiply sign
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1)
		.Padding(0,0,12,0) // Line up with the other properties due to having no reset to default button
		[
			SNew(SNumericEntryBox<int32>)
			.Font(DetailBuilder.GetDetailFont())
			.MinValue(1)
			.MaxValue(8192)
			.MinSliderValue(1)
			.MaxSliderValue(8192)
			.AllowSpin(true)
			//.MinSliderValue(TAttribute<TOptional<int32> >(this, &FCyLandEditorDetailCustomization_NewCyLand::GetMinCyLandResolution))
			//.MaxSliderValue(TAttribute<TOptional<int32> >(this, &FCyLandEditorDetailCustomization_NewCyLand::GetMaxCyLandResolution))
			.Value(this, &FCyLandEditorDetailCustomization_NewCyLand::GetCyLandResolutionY)
			.OnValueChanged(this, &FCyLandEditorDetailCustomization_NewCyLand::OnChangeCyLandResolutionY)
			.OnValueCommitted(this, &FCyLandEditorDetailCustomization_NewCyLand::OnCommitCyLandResolutionY)
		]
	];

	NewCyLandCategory.AddCustomRow(LOCTEXT("TotalComponents", "Total Components"))
	.RowTag("CyLandEditor.TotalComponents")
	.NameContent()
	[
		SNew(SBox)
		.VAlign(VAlign_Center)
		.Padding(FMargin(2))
		[
			SNew(STextBlock)
			.Font(DetailBuilder.GetDetailFont())
			.Text(LOCTEXT("TotalComponents", "Total Components"))
			.ToolTipText(LOCTEXT("NewCyLand_TotalComponents", "The total number of components that will be created for this CyLand."))
		]
	]
	.ValueContent()
	[
		SNew(SBox)
		.Padding(FMargin(0,0,12,0)) // Line up with the other properties due to having no reset to default button
		[
			SNew(SEditableTextBox)
			.IsReadOnly(true)
			.Font(DetailBuilder.GetDetailFont())
			.Text(this, &FCyLandEditorDetailCustomization_NewCyLand::GetTotalComponentCount)
		]
	];

	NewCyLandCategory.AddCustomRow(FText::GetEmpty())
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.Visibility_Static(&GetVisibilityOnlyInNewCyLandMode, ENewCyLandPreviewMode::NewCyLand)
			.Text(LOCTEXT("FillWorld", "Fill World"))
			.AddMetaData<FTutorialMetaData>(FTutorialMetaData(TEXT("FillWorldButton"), TEXT("LevelEditorToolBox")))
			.OnClicked(this, &FCyLandEditorDetailCustomization_NewCyLand::OnFillWorldButtonClicked)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.Visibility_Static(&GetVisibilityOnlyInNewCyLandMode, ENewCyLandPreviewMode::ImportCyLand)
			.Text(LOCTEXT("FitToData", "Fit To Data"))
			.AddMetaData<FTagMetaData>(TEXT("ImportButton"))
			.OnClicked(this, &FCyLandEditorDetailCustomization_NewCyLand::OnFitImportDataButtonClicked)
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1)
		//[
		//]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.Visibility_Static(&GetVisibilityOnlyInNewCyLandMode, ENewCyLandPreviewMode::NewCyLand)
			.Text(LOCTEXT("Create", "Create"))
			.AddMetaData<FTutorialMetaData>(FTutorialMetaData(TEXT("CreateButton"), TEXT("LevelEditorToolBox")))
			.OnClicked(this, &FCyLandEditorDetailCustomization_NewCyLand::OnCreateButtonClicked)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.Visibility_Static(&GetVisibilityOnlyInNewCyLandMode, ENewCyLandPreviewMode::ImportCyLand)
			.Text(LOCTEXT("Import", "Import"))
			.OnClicked(this, &FCyLandEditorDetailCustomization_NewCyLand::OnCreateButtonClicked)
			.IsEnabled(this, &FCyLandEditorDetailCustomization_NewCyLand::GetImportButtonIsEnabled)
		]
	];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

FText FCyLandEditorDetailCustomization_NewCyLand::GetOverallResolutionTooltip() const
{
	return (GetEditorMode() && GetEditorMode()->NewCyLandPreviewMode == ENewCyLandPreviewMode::ImportCyLand)
	? LOCTEXT("ImportCyLand_OverallResolution", "Overall final resolution of the imported CyLand in vertices")
	: LOCTEXT("NewCyLand_OverallResolution", "Overall final resolution of the new CyLand in vertices");
}

void FCyLandEditorDetailCustomization_NewCyLand::SetScale(float NewValue, ETextCommit::Type, TSharedRef<IPropertyHandle> PropertyHandle)
{
	float OldValue = 0;
	PropertyHandle->GetValue(OldValue);

	if (NewValue == 0)
	{
		if (OldValue < 0)
		{
			NewValue = -1;
		}
		else
		{
			NewValue = 1;
		}
	}

	ensure(PropertyHandle->SetValue(NewValue) == FPropertyAccess::Success);

	// Make X and Y scale match
	FName PropertyName = PropertyHandle->GetProperty()->GetFName();
	if (PropertyName == "X")
	{
		TSharedRef<IPropertyHandle> PropertyHandle_Y = PropertyHandle->GetParentHandle()->GetChildHandle("Y").ToSharedRef();
		ensure(PropertyHandle_Y->SetValue(NewValue) == FPropertyAccess::Success);
	}
	else if (PropertyName == "Y")
	{
		TSharedRef<IPropertyHandle> PropertyHandle_X = PropertyHandle->GetParentHandle()->GetChildHandle("X").ToSharedRef();
		ensure(PropertyHandle_X->SetValue(NewValue) == FPropertyAccess::Success);
	}
}

TSharedRef<SWidget> FCyLandEditorDetailCustomization_NewCyLand::GetSectionSizeMenu(TSharedRef<IPropertyHandle> PropertyHandle)
{
	FMenuBuilder MenuBuilder(true, nullptr);

	for (int32 i = 0; i < ARRAY_COUNT(FNewCyLandUtils::SectionSizes); i++)
	{
		MenuBuilder.AddMenuEntry(FText::Format(LOCTEXT("NxNQuads", "{0}\u00D7{0} Quads"), FText::AsNumber(FNewCyLandUtils::SectionSizes[i])), FText::GetEmpty(),
			FSlateIcon(), FExecuteAction::CreateStatic(&OnChangeSectionSize, PropertyHandle, FNewCyLandUtils::SectionSizes[i]));
	}

	return MenuBuilder.MakeWidget();
}

void FCyLandEditorDetailCustomization_NewCyLand::OnChangeSectionSize(TSharedRef<IPropertyHandle> PropertyHandle, int32 NewSize)
{
	ensure(PropertyHandle->SetValue(NewSize) == FPropertyAccess::Success);
}

FText FCyLandEditorDetailCustomization_NewCyLand::GetSectionSize(TSharedRef<IPropertyHandle> PropertyHandle)
{
	int32 QuadsPerSection = 0;
	FPropertyAccess::Result Result = PropertyHandle->GetValue(QuadsPerSection);
	check(Result == FPropertyAccess::Success);

	if (Result == FPropertyAccess::MultipleValues)
	{
		return NSLOCTEXT("PropertyEditor", "MultipleValues", "Multiple Values");
	}

	return FText::Format(LOCTEXT("NxNQuads", "{0}\u00D7{0} Quads"), FText::AsNumber(QuadsPerSection));
}

TSharedRef<SWidget> FCyLandEditorDetailCustomization_NewCyLand::GetSectionsPerComponentMenu(TSharedRef<IPropertyHandle> PropertyHandle)
{
	FMenuBuilder MenuBuilder(true, nullptr);

	for (int32 i = 0; i < ARRAY_COUNT(FNewCyLandUtils::NumSections); i++)
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("Width"), FNewCyLandUtils::NumSections[i]);
		Args.Add(TEXT("Height"), FNewCyLandUtils::NumSections[i]);
		MenuBuilder.AddMenuEntry(FText::Format(FNewCyLandUtils::NumSections[i] == 1 ? LOCTEXT("1x1Section", "{Width}\u00D7{Height} Section") : LOCTEXT("NxNSections", "{Width}\u00D7{Height} Sections"), Args),
			FText::GetEmpty(), FSlateIcon(), FExecuteAction::CreateStatic(&OnChangeSectionsPerComponent, PropertyHandle, FNewCyLandUtils::NumSections[i]));
	}

	return MenuBuilder.MakeWidget();
}

void FCyLandEditorDetailCustomization_NewCyLand::OnChangeSectionsPerComponent(TSharedRef<IPropertyHandle> PropertyHandle, int32 NewSize)
{
	ensure(PropertyHandle->SetValue(NewSize) == FPropertyAccess::Success);
}

FText FCyLandEditorDetailCustomization_NewCyLand::GetSectionsPerComponent(TSharedRef<IPropertyHandle> PropertyHandle)
{
	int32 SectionsPerComponent = 0;
	FPropertyAccess::Result Result = PropertyHandle->GetValue(SectionsPerComponent);
	check(Result == FPropertyAccess::Success);

	if (Result == FPropertyAccess::MultipleValues)
	{
		return NSLOCTEXT("PropertyEditor", "MultipleValues", "Multiple Values");
	}

	FFormatNamedArguments Args;
	Args.Add(TEXT("Width"), SectionsPerComponent);
	Args.Add(TEXT("Height"), SectionsPerComponent);
	return FText::Format(SectionsPerComponent == 1 ? LOCTEXT("1x1Section", "{Width}\u00D7{Height} Section") : LOCTEXT("NxNSections", "{Width}\u00D7{Height} Sections"), Args);
}

TOptional<int32> FCyLandEditorDetailCustomization_NewCyLand::GetCyLandResolutionX() const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		return (CyLandEdMode->UISettings->NewCyLand_ComponentCount.X * CyLandEdMode->UISettings->NewCyLand_SectionsPerComponent * CyLandEdMode->UISettings->NewCyLand_QuadsPerSection + 1);
	}

	return 0;
}

void FCyLandEditorDetailCustomization_NewCyLand::OnChangeCyLandResolutionX(int32 NewValue)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		int32 NewComponentCountX = CyLandEdMode->UISettings->CalcComponentsCount(NewValue);
		if (NewComponentCountX != CyLandEdMode->UISettings->NewCyLand_ComponentCount.X)
		{
			if (!GEditor->IsTransactionActive())
			{
				GEditor->BeginTransaction(LOCTEXT("ChangeResolutionX_Transaction", "Change CyLand Resolution X"));
			}

			CyLandEdMode->UISettings->Modify();
			CyLandEdMode->UISettings->NewCyLand_ComponentCount.X = NewComponentCountX;
		}
	}
}

void FCyLandEditorDetailCustomization_NewCyLand::OnCommitCyLandResolutionX(int32 NewValue, ETextCommit::Type CommitInfo)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		if (!GEditor->IsTransactionActive())
		{
			GEditor->BeginTransaction(LOCTEXT("ChangeResolutionX_Transaction", "Change CyLand Resolution X"));
		}
		CyLandEdMode->UISettings->Modify();
		CyLandEdMode->UISettings->NewCyLand_ComponentCount.X = CyLandEdMode->UISettings->CalcComponentsCount(NewValue);
		GEditor->EndTransaction();
	}
}

TOptional<int32> FCyLandEditorDetailCustomization_NewCyLand::GetCyLandResolutionY() const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		return (CyLandEdMode->UISettings->NewCyLand_ComponentCount.Y * CyLandEdMode->UISettings->NewCyLand_SectionsPerComponent * CyLandEdMode->UISettings->NewCyLand_QuadsPerSection + 1);
	}

	return 0;
}

void FCyLandEditorDetailCustomization_NewCyLand::OnChangeCyLandResolutionY(int32 NewValue)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		int32 NewComponentCountY = CyLandEdMode->UISettings->CalcComponentsCount(NewValue);
		if (NewComponentCountY != CyLandEdMode->UISettings->NewCyLand_ComponentCount.Y)
		{
			if (!GEditor->IsTransactionActive())
			{
				GEditor->BeginTransaction(LOCTEXT("ChangeResolutionY_Transaction", "Change CyLand Resolution Y"));
			}
			
			CyLandEdMode->UISettings->Modify();
			CyLandEdMode->UISettings->NewCyLand_ComponentCount.Y = NewComponentCountY;
		}
	}
}

void FCyLandEditorDetailCustomization_NewCyLand::OnCommitCyLandResolutionY(int32 NewValue, ETextCommit::Type CommitInfo)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		if (!GEditor->IsTransactionActive())
		{
			GEditor->BeginTransaction(LOCTEXT("ChangeResolutionY_Transaction", "Change CyLand Resolution Y"));
		}
		CyLandEdMode->UISettings->Modify();
		CyLandEdMode->UISettings->NewCyLand_ComponentCount.Y = CyLandEdMode->UISettings->CalcComponentsCount(NewValue);
		GEditor->EndTransaction();
	}
}

TOptional<int32> FCyLandEditorDetailCustomization_NewCyLand::GetMinCyLandResolution() const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		// Min size is one component
		return (CyLandEdMode->UISettings->NewCyLand_SectionsPerComponent * CyLandEdMode->UISettings->NewCyLand_QuadsPerSection + 1);
	}

	return 0;
}

TOptional<int32> FCyLandEditorDetailCustomization_NewCyLand::GetMaxCyLandResolution() const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		// Max size is either whole components below 8192 verts, or 32 components
		const int32 QuadsPerComponent = (CyLandEdMode->UISettings->NewCyLand_SectionsPerComponent * CyLandEdMode->UISettings->NewCyLand_QuadsPerSection);
		//return (float)(FMath::Min(32, FMath::FloorToInt(8191 / QuadsPerComponent)) * QuadsPerComponent);
		return (8191 / QuadsPerComponent) * QuadsPerComponent + 1;
	}

	return 0;
}

FText FCyLandEditorDetailCustomization_NewCyLand::GetTotalComponentCount() const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		return FText::AsNumber(CyLandEdMode->UISettings->NewCyLand_ComponentCount.X * CyLandEdMode->UISettings->NewCyLand_ComponentCount.Y);
	}

	return FText::FromString(TEXT("---"));
}


EVisibility FCyLandEditorDetailCustomization_NewCyLand::GetVisibilityOnlyInNewCyLandMode(ENewCyLandPreviewMode::Type value)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		if (CyLandEdMode->NewCyLandPreviewMode == value)
		{
			return EVisibility::Visible;
		}
	}
	return EVisibility::Collapsed;
}

ECheckBoxState FCyLandEditorDetailCustomization_NewCyLand::NewCyLandModeIsChecked(ENewCyLandPreviewMode::Type value) const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		if (CyLandEdMode->NewCyLandPreviewMode == value)
		{
			return ECheckBoxState::Checked;
		}
	}
	return ECheckBoxState::Unchecked;
}

void FCyLandEditorDetailCustomization_NewCyLand::OnNewCyLandModeChanged(ECheckBoxState NewCheckedState, ENewCyLandPreviewMode::Type value)
{
	if (NewCheckedState == ECheckBoxState::Checked)
	{
		FEdModeCyLand* CyLandEdMode = GetEditorMode();
		if (CyLandEdMode != nullptr)
		{
			CyLandEdMode->NewCyLandPreviewMode = value;

			if (value == ENewCyLandPreviewMode::ImportCyLand)
			{
				CyLandEdMode->NewCyLandPreviewMode = ENewCyLandPreviewMode::ImportCyLand;
			}
		}
	}
}

DEFINE_LOG_CATEGORY_STATIC(NewCyLand, Log, All);
//create
FReply FCyLandEditorDetailCustomization_NewCyLand::OnCreateButtonClicked()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr && 
		CyLandEdMode->GetWorld() != nullptr && 
		CyLandEdMode->GetWorld()->GetCurrentLevel()->bIsVisible)
	{
		const int32 ComponentCountX = CyLandEdMode->UISettings->NewCyLand_ComponentCount.X;
		const int32 ComponentCountY = CyLandEdMode->UISettings->NewCyLand_ComponentCount.Y;
		const int32 QuadsPerComponent = CyLandEdMode->UISettings->NewCyLand_SectionsPerComponent * CyLandEdMode->UISettings->NewCyLand_QuadsPerSection;
		const int32 SizeX = ComponentCountX * QuadsPerComponent + 1;
		const int32 SizeY = ComponentCountY * QuadsPerComponent + 1;

		TOptional< TArray< FCyLandImportLayerInfo > > ImportLayers = FNewCyLandUtils::CreateImportLayersInfo( CyLandEdMode->UISettings, CyLandEdMode->NewCyLandPreviewMode );

		if ( !ImportLayers )
		{
			return FReply::Handled();
		}
		UE_LOG(NewCyLand, Warning, TEXT("create NewCyLand offset ?? %d"), ImportLayers.GetValue().Num());
		TArray<uint16> Data = FNewCyLandUtils::ComputeHeightData( CyLandEdMode->UISettings, ImportLayers.GetValue(), CyLandEdMode->NewCyLandPreviewMode );

		FScopedTransaction Transaction(LOCTEXT("Undo", "Creating New CyLand"));

		FVector Offset = FTransform(CyLandEdMode->UISettings->NewCyLand_Rotation, FVector::ZeroVector,
			CyLandEdMode->UISettings->NewCyLand_Scale).TransformVector(FVector(-ComponentCountX * QuadsPerComponent / 2, -ComponentCountY * QuadsPerComponent / 2, 0));
		
		UE_LOG(NewCyLand, Warning, TEXT("create NewCyLand offset %f %f %f"), Offset.X , Offset.Y, Offset.Z);
		Offset += CyLandEdMode->UISettings->NewCyLand_Location;
		UE_LOG(NewCyLand, Warning, TEXT("create NewCyLand at %f %f %f"), Offset.X, Offset.Y, Offset.Z);
		ACyLand* CyLand = CyLandEdMode->GetWorld()->SpawnActor<ACyLand>(Offset, CyLandEdMode->UISettings->NewCyLand_Rotation);
		CyLand->CyLandMaterial = CyLandEdMode->UISettings->NewCyLand_Material.Get();
		CyLand->SetActorRelativeScale3D(CyLandEdMode->UISettings->NewCyLand_Scale);

		CyLand->Imports(FGuid::NewGuid(), 0, 0, SizeX-1, SizeY-1, CyLandEdMode->UISettings->NewCyLand_SectionsPerComponent, CyLandEdMode->UISettings->NewCyLand_QuadsPerSection, Data.GetData(),
			nullptr, ImportLayers.GetValue(), CyLandEdMode->UISettings->ImportCyLand_AlphamapType);

		// automatically calculate a lighting LOD that won't crash lightmass (hopefully)
		// < 2048x2048 -> LOD0
		// >=2048x2048 -> LOD1
		// >= 4096x4096 -> LOD2
		// >= 8192x8192 -> LOD3
		CyLand->StaticLightingLOD = FMath::DivideAndRoundUp(FMath::CeilLogTwo((SizeX * SizeY) / (2048 * 2048) + 1), (uint32)2);

		if (CyLandEdMode->NewCyLandPreviewMode == ENewCyLandPreviewMode::ImportCyLand)
		{
			CyLand->ReimportHeightmapFilePath = CyLandEdMode->UISettings->ImportCyLand_HeightmapFilename;
		}

		UCyLandInfo* CyLandInfo = CyLand->CreateCyLandInfo();
		CyLandInfo->UpdateLayerInfoMap(CyLand);

		// Import doesn't fill in the LayerInfo for layers with no data, do that now
		const TArray<FCyLandImportLayer>& ImportCyLandLayersList = CyLandEdMode->UISettings->ImportCyLand_Layers;
		for (int32 i = 0; i < ImportCyLandLayersList.Num(); i++)
		{
			if (ImportCyLandLayersList[i].LayerInfo != nullptr)
			{
				if (CyLandEdMode->NewCyLandPreviewMode == ENewCyLandPreviewMode::ImportCyLand)
				{
					CyLand->EditorLayerSettings.Add(FCyLandEditorLayerSettings(ImportCyLandLayersList[i].LayerInfo, ImportCyLandLayersList[i].SourceFilePath));
				}
				else
				{
					CyLand->EditorLayerSettings.Add(FCyLandEditorLayerSettings(ImportCyLandLayersList[i].LayerInfo));
				}

				int32 LayerInfoIndex = CyLandInfo->GetLayerInfoIndex(ImportCyLandLayersList[i].LayerName);
				if (ensure(LayerInfoIndex != INDEX_NONE))
				{
					FCyLandInfoLayerSettings& LayerSettings = CyLandInfo->Layers[LayerInfoIndex];
					LayerSettings.LayerInfoObj = ImportCyLandLayersList[i].LayerInfo;
				}
			}
		}

		CyLandEdMode->UpdateCyLandList();
		CyLandEdMode->CurrentToolTarget.CyLandInfo = CyLandInfo;
		CyLandEdMode->CurrentToolTarget.TargetType = ECyLandToolTargetType::Heightmap;
		CyLandEdMode->CurrentToolTarget.LayerInfo = nullptr;
		CyLandEdMode->CurrentToolTarget.LayerName = NAME_None;
		CyLandEdMode->UpdateTargetList();

		CyLandEdMode->SetCurrentTool("Select"); // change tool so switching back to the manage mode doesn't give "New CyLand" again
		CyLandEdMode->SetCurrentTool("Sculpt"); // change to sculpting mode and tool
		CyLandEdMode->SetCurrentProceduralLayer(0);

		if (CyLandEdMode->CurrentToolTarget.CyLandInfo.IsValid())
		{
			ACyLandProxy* CyLandProxy = CyLandEdMode->CurrentToolTarget.CyLandInfo->GetCyLandProxy();
			CyLandProxy->OnMaterialChangedDelegate().AddRaw(CyLandEdMode, &FEdModeCyLand::OnCyLandMaterialChangedDelegate);
		}
	}

	return FReply::Handled();
}

FReply FCyLandEditorDetailCustomization_NewCyLand::OnFillWorldButtonClicked()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		FVector& NewCyLandLocation = CyLandEdMode->UISettings->NewCyLand_Location;
		NewCyLandLocation.X = 0;
		NewCyLandLocation.Y = 0;

		const int32 QuadsPerComponent = CyLandEdMode->UISettings->NewCyLand_SectionsPerComponent * CyLandEdMode->UISettings->NewCyLand_QuadsPerSection;
		CyLandEdMode->UISettings->NewCyLand_ComponentCount.X = FMath::CeilToInt(WORLD_MAX / QuadsPerComponent / CyLandEdMode->UISettings->NewCyLand_Scale.X);
		CyLandEdMode->UISettings->NewCyLand_ComponentCount.Y = FMath::CeilToInt(WORLD_MAX / QuadsPerComponent / CyLandEdMode->UISettings->NewCyLand_Scale.Y);
		CyLandEdMode->UISettings->NewCyLand_ClampSize();
	}

	return FReply::Handled();
}

FReply FCyLandEditorDetailCustomization_NewCyLand::OnFitImportDataButtonClicked()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		ChooseBestComponentSizeForImport(CyLandEdMode);
	}

	return FReply::Handled();
}

bool FCyLandEditorDetailCustomization_NewCyLand::GetImportButtonIsEnabled() const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		if (CyLandEdMode->UISettings->ImportCyLand_HeightmapImportResult == ECyLandImportResult::Error)
		{
			return false;
		}

		for (int32 i = 0; i < CyLandEdMode->UISettings->ImportCyLand_Layers.Num(); ++i)
		{
			if (CyLandEdMode->UISettings->ImportCyLand_Layers[i].ImportResult == ECyLandImportResult::Error)
			{
				return false;
			}
		}

		return true;
	}
	return false;
}

EVisibility FCyLandEditorDetailCustomization_NewCyLand::GetHeightmapErrorVisibility(TSharedRef<IPropertyHandle> PropertyHandle_HeightmapImportResult)
{
	ECyLandImportResult HeightmapImportResult;
	FPropertyAccess::Result Result = PropertyHandle_HeightmapImportResult->GetValue((uint8&)HeightmapImportResult);

	if (Result == FPropertyAccess::Fail)
	{
		return EVisibility::Collapsed;
	}

	if (Result == FPropertyAccess::MultipleValues)
	{
		return EVisibility::Visible;
	}

	if (HeightmapImportResult != ECyLandImportResult::Success)
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

FSlateColor FCyLandEditorDetailCustomization_NewCyLand::GetHeightmapErrorColor(TSharedRef<IPropertyHandle> PropertyHandle_HeightmapImportResult)
{
	ECyLandImportResult HeightmapImportResult;
	FPropertyAccess::Result Result = PropertyHandle_HeightmapImportResult->GetValue((uint8&)HeightmapImportResult);

	if (Result == FPropertyAccess::Fail ||
		Result == FPropertyAccess::MultipleValues)
	{
		return FCoreStyle::Get().GetColor("ErrorReporting.BackgroundColor");
	}

	switch (HeightmapImportResult)
	{
	case ECyLandImportResult::Success:
		return FCoreStyle::Get().GetColor("InfoReporting.BackgroundColor");
	case ECyLandImportResult::Warning:
		return FCoreStyle::Get().GetColor("ErrorReporting.WarningBackgroundColor");
	case ECyLandImportResult::Error:
		return FCoreStyle::Get().GetColor("ErrorReporting.BackgroundColor");
	default:
		check(0);
		return FSlateColor();
	}
}

void FCyLandEditorDetailCustomization_NewCyLand::SetImportHeightmapFilenameString(const FText& NewValue, ETextCommit::Type CommitInfo, TSharedRef<IPropertyHandle> PropertyHandle_HeightmapFilename)
{
	FString HeightmapFilename = NewValue.ToString();
	ensure(PropertyHandle_HeightmapFilename->SetValue(HeightmapFilename) == FPropertyAccess::Success);
}

void FCyLandEditorDetailCustomization_NewCyLand::OnImportHeightmapFilenameChanged()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		FNewCyLandUtils::ImportCyLandData(CyLandEdMode->UISettings, ImportResolutions);
	}
}

FReply FCyLandEditorDetailCustomization_NewCyLand::OnImportHeightmapFilenameButtonClicked(TSharedRef<IPropertyHandle> PropertyHandle_HeightmapFilename)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	check(CyLandEdMode != nullptr);

	// Prompt the user for the Filenames
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform != nullptr)
	{
		ICyLandEditorModule& CyLandEditorModule = FModuleManager::GetModuleChecked<ICyLandEditorModule>("CyLandEditor");
		const TCHAR* FileTypes = CyLandEditorModule.GetHeightmapImportDialogTypeString();

		TArray<FString> OpenFilenames;
		bool bOpened = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			NSLOCTEXT("UnrealEd", "Import", "Import").ToString(),
			CyLandEdMode->UISettings->LastImportPath,
			TEXT(""),
			FileTypes,
			EFileDialogFlags::None,
			OpenFilenames);

		if (bOpened)
		{
			ensure(PropertyHandle_HeightmapFilename->SetValue(OpenFilenames[0]) == FPropertyAccess::Success);
			CyLandEdMode->UISettings->LastImportPath = FPaths::GetPath(OpenFilenames[0]);
		}
	}

	return FReply::Handled();

}

TSharedRef<SWidget> FCyLandEditorDetailCustomization_NewCyLand::GetImportCyLandResolutionMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	for (int32 i = 0; i < ImportResolutions.Num(); i++)
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("Width"), ImportResolutions[i].Width);
		Args.Add(TEXT("Height"), ImportResolutions[i].Height);
		MenuBuilder.AddMenuEntry(FText::Format(LOCTEXT("ImportResolution_Format", "{Width}\u00D7{Height}"), Args), FText(), FSlateIcon(), FExecuteAction::CreateSP(this, &FCyLandEditorDetailCustomization_NewCyLand::OnChangeImportCyLandResolution, i));
	}

	return MenuBuilder.MakeWidget();
}

void FCyLandEditorDetailCustomization_NewCyLand::OnChangeImportCyLandResolution(int32 Index)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		CyLandEdMode->UISettings->ImportCyLand_Width = ImportResolutions[Index].Width;
		CyLandEdMode->UISettings->ImportCyLand_Height = ImportResolutions[Index].Height;
		CyLandEdMode->UISettings->ClearImportCyLandData();
		ChooseBestComponentSizeForImport(CyLandEdMode);
	}
}

FText FCyLandEditorDetailCustomization_NewCyLand::GetImportCyLandResolution() const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		const int32 Width = CyLandEdMode->UISettings->ImportCyLand_Width;
		const int32 Height = CyLandEdMode->UISettings->ImportCyLand_Height;
		if (Width != 0 && Height != 0)
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("Width"), Width);
			Args.Add(TEXT("Height"), Height);
			return FText::Format(LOCTEXT("ImportResolution_Format", "{Width}\u00D7{Height}"), Args);
		}
		else
		{
			return LOCTEXT("ImportResolution_Invalid", "(invalid)");
		}
	}

	return FText::GetEmpty();
}

void FCyLandEditorDetailCustomization_NewCyLand::ChooseBestComponentSizeForImport(FEdModeCyLand* CyLandEdMode)
{
	FNewCyLandUtils::ChooseBestComponentSizeForImport(CyLandEdMode->UISettings);
}

EVisibility FCyLandEditorDetailCustomization_NewCyLand::GetMaterialTipVisibility() const
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		if (CyLandEdMode->UISettings->ImportCyLand_Layers.Num() == 0)
		{
			return EVisibility::Visible;
		}
	}
	return EVisibility::Collapsed;
}

//////////////////////////////////////////////////////////////////////////

TSharedRef<IPropertyTypeCustomization> FCyLandEditorStructCustomization_FCyLandImportLayer::MakeInstance()
{
	return MakeShareable(new FCyLandEditorStructCustomization_FCyLandImportLayer);
}

void FCyLandEditorStructCustomization_FCyLandImportLayer::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
}

void FCyLandEditorStructCustomization_FCyLandImportLayer::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	TSharedRef<IPropertyHandle> PropertyHandle_LayerName = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FCyLandImportLayer, LayerName)).ToSharedRef();
	TSharedRef<IPropertyHandle> PropertyHandle_LayerInfo = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FCyLandImportLayer, LayerInfo)).ToSharedRef();
	TSharedRef<IPropertyHandle> PropertyHandle_SourceFilePath = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FCyLandImportLayer, SourceFilePath)).ToSharedRef();
	TSharedRef<IPropertyHandle> PropertyHandle_ThumbnailMIC = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FCyLandImportLayer, ThumbnailMIC)).ToSharedRef();
	TSharedRef<IPropertyHandle> PropertyHandle_ImportResult = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FCyLandImportLayer, ImportResult)).ToSharedRef();
	TSharedRef<IPropertyHandle> PropertyHandle_ErrorMessage = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FCyLandImportLayer, ErrorMessage)).ToSharedRef();

	FName LayerName;
	FText LayerNameText;
	FPropertyAccess::Result Result = PropertyHandle_LayerName->GetValue(LayerName);
	checkSlow(Result == FPropertyAccess::Success);
	LayerNameText = FText::FromName(LayerName);
	if (Result == FPropertyAccess::MultipleValues)
	{
		LayerName = NAME_None;
		LayerNameText = NSLOCTEXT("PropertyEditor", "MultipleValues", "Multiple Values");
	}

	UObject* ThumbnailMIC = nullptr;
	Result = PropertyHandle_ThumbnailMIC->GetValue(ThumbnailMIC);
	checkSlow(Result == FPropertyAccess::Success);

	ChildBuilder.AddCustomRow(LayerNameText)
	.NameContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1)
		.VAlign(VAlign_Center)
		.Padding(FMargin(2))
		[
			SNew(STextBlock)
			.Font(StructCustomizationUtils.GetRegularFont())
			.Text(LayerNameText)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(2))
		[
			SNew(SCyLandAssetThumbnail, ThumbnailMIC, StructCustomizationUtils.GetThumbnailPool().ToSharedRef())
			.ThumbnailSize(FIntPoint(48, 48))
		]
	]
	.ValueContent()
	.MinDesiredWidth(250.0f) // copied from SPropertyEditorAsset::GetDesiredWidth
	.MaxDesiredWidth(0)
	[
		SNew(SBox)
		.VAlign(VAlign_Center)
		.Padding(FMargin(0,0,12,0)) // Line up with the other properties due to having no reset to default button
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UCyLandLayerInfoObject::StaticClass())
					.PropertyHandle(PropertyHandle_LayerInfo)
					.OnShouldFilterAsset_Static(&ShouldFilterLayerInfo, LayerName)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SComboButton)
					//.ButtonStyle( FEditorStyle::Get(), "NoBorder" )
					.ButtonStyle( FEditorStyle::Get(), "HoverHintOnly" )
					.HasDownArrow(false)
					//.ContentPadding(0)
					.ContentPadding(4.0f)
					.ForegroundColor(FSlateColor::UseForeground())
					.IsFocusable(false)
					.ToolTipText(LOCTEXT("Target_Create", "Create Layer Info"))
					.Visibility_Static(&GetImportLayerCreateVisibility, PropertyHandle_LayerInfo)
					.OnGetMenuContent_Static(&OnGetImportLayerCreateMenu, PropertyHandle_LayerInfo, LayerName)
					.ButtonContent()
					[
						SNew(SImage)
						.Image(FEditorStyle::GetBrush("CyLandEditor.Target_Create"))
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				.Visibility_Static(&FCyLandEditorDetailCustomization_NewCyLand::GetVisibilityOnlyInNewCyLandMode, ENewCyLandPreviewMode::ImportCyLand)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 2, 0)
				[
					SNew(SErrorText)
					.Visibility_Static(&GetErrorVisibility, PropertyHandle_ImportResult)
					.BackgroundColor_Static(&GetErrorColor, PropertyHandle_ImportResult)
					.ErrorText(NSLOCTEXT("UnrealEd", "Error", "!"))
					.ToolTip(
						SNew(SToolTip)
						.Text_Static(&GetErrorText, PropertyHandle_ErrorMessage)
					)
				]
				+ SHorizontalBox::Slot()
				[
					PropertyHandle_SourceFilePath->CreatePropertyValueWidget()
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(1, 0, 0, 0)
				[
					SNew(SButton)
					.ContentPadding(FMargin(4, 0))
					.Text(NSLOCTEXT("UnrealEd", "GenericOpenDialog", "..."))
					.OnClicked_Static(&FCyLandEditorStructCustomization_FCyLandImportLayer::OnLayerFilenameButtonClicked, PropertyHandle_SourceFilePath)
				]
			]
		]
	];
}

FReply FCyLandEditorStructCustomization_FCyLandImportLayer::OnLayerFilenameButtonClicked(TSharedRef<IPropertyHandle> PropertyHandle_LayerFilename)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	check(CyLandEdMode != nullptr);

	// Prompt the user for the Filenames
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform != nullptr)
	{
		ICyLandEditorModule& CyLandEditorModule = FModuleManager::GetModuleChecked<ICyLandEditorModule>("CyLandEditor");
		const TCHAR* FileTypes = CyLandEditorModule.GetWeightmapImportDialogTypeString();

		TArray<FString> OpenFilenames;
		bool bOpened = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			NSLOCTEXT("UnrealEd", "Import", "Import").ToString(),
			CyLandEdMode->UISettings->LastImportPath,
			TEXT(""),
			FileTypes,
			EFileDialogFlags::None,
			OpenFilenames);

		if (bOpened)
		{
			ensure(PropertyHandle_LayerFilename->SetValue(OpenFilenames[0]) == FPropertyAccess::Success);
			CyLandEdMode->UISettings->LastImportPath = FPaths::GetPath(OpenFilenames[0]);
		}
	}

	return FReply::Handled();
}

bool FCyLandEditorStructCustomization_FCyLandImportLayer::ShouldFilterLayerInfo(const FAssetData& AssetData, FName LayerName)
{
	const FName LayerNameMetaData = AssetData.GetTagValueRef<FName>("LayerName");
	if (!LayerNameMetaData.IsNone())
	{
		return LayerNameMetaData != LayerName;
	}

	UCyLandLayerInfoObject* LayerInfo = CastChecked<UCyLandLayerInfoObject>(AssetData.GetAsset());
	return LayerInfo->LayerName != LayerName;
}

EVisibility FCyLandEditorStructCustomization_FCyLandImportLayer::GetImportLayerCreateVisibility(TSharedRef<IPropertyHandle> PropertyHandle_LayerInfo)
{
	UObject* LayerInfoAsUObject = nullptr;
	if (PropertyHandle_LayerInfo->GetValue(LayerInfoAsUObject) != FPropertyAccess::Fail &&
		LayerInfoAsUObject == nullptr)
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

TSharedRef<SWidget> FCyLandEditorStructCustomization_FCyLandImportLayer::OnGetImportLayerCreateMenu(TSharedRef<IPropertyHandle> PropertyHandle_LayerInfo, FName LayerName)
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(LOCTEXT("Target_Create_Blended", "Weight-Blended Layer (normal)"), FText(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&OnImportLayerCreateClicked, PropertyHandle_LayerInfo, LayerName, false)));

	MenuBuilder.AddMenuEntry(LOCTEXT("Target_Create_NoWeightBlend", "Non Weight-Blended Layer"), FText(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&OnImportLayerCreateClicked, PropertyHandle_LayerInfo, LayerName, true)));

	return MenuBuilder.MakeWidget();
}

void FCyLandEditorStructCustomization_FCyLandImportLayer::OnImportLayerCreateClicked(TSharedRef<IPropertyHandle> PropertyHandle_LayerInfo, FName LayerName, bool bNoWeightBlend)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != nullptr)
	{
		// Hack as we don't have a direct world pointer in the EdMode...
		ULevel* Level = CyLandEdMode->CurrentGizmoActor->GetWorld()->GetCurrentLevel();

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

			UPackage* Package = CreatePackage(nullptr, *PackageName);
			UCyLandLayerInfoObject* LayerInfo = NewObject<UCyLandLayerInfoObject>(Package, LayerObjectName, RF_Public | RF_Standalone | RF_Transactional);
			LayerInfo->LayerName = LayerName;
			LayerInfo->bNoWeightBlend = bNoWeightBlend;

			const UObject* LayerInfoAsUObject = LayerInfo; // HACK: If SetValue took a reference to a const ptr (T* const &) or a non-reference (T*) then this cast wouldn't be necessary
			ensure(PropertyHandle_LayerInfo->SetValue(LayerInfoAsUObject) == FPropertyAccess::Success);

			// Notify the asset registry
			FAssetRegistryModule::AssetCreated(LayerInfo);

			// Mark the package dirty...
			Package->MarkPackageDirty();

			// Show in the content browser
			TArray<UObject*> Objects;
			Objects.Add(LayerInfo);
			GEditor->SyncBrowserToObjects(Objects);
		}
	}
}

EVisibility FCyLandEditorStructCustomization_FCyLandImportLayer::GetErrorVisibility(TSharedRef<IPropertyHandle> PropertyHandle_ImportResult)
{
	ECyLandImportResult WeightmapImportResult;
	FPropertyAccess::Result Result = PropertyHandle_ImportResult->GetValue((uint8&)WeightmapImportResult);

	if (Result == FPropertyAccess::Fail ||
		Result == FPropertyAccess::MultipleValues)
	{
		return EVisibility::Visible;
	}

	if (WeightmapImportResult != ECyLandImportResult::Success)
	{
		return EVisibility::Visible;
	}
	return EVisibility::Collapsed;
}

FSlateColor FCyLandEditorStructCustomization_FCyLandImportLayer::GetErrorColor(TSharedRef<IPropertyHandle> PropertyHandle_ImportResult)
{
	ECyLandImportResult WeightmapImportResult;
	FPropertyAccess::Result Result = PropertyHandle_ImportResult->GetValue((uint8&)WeightmapImportResult);
	check(Result == FPropertyAccess::Success);

	if (Result == FPropertyAccess::MultipleValues)
	{
		return FCoreStyle::Get().GetColor("ErrorReporting.BackgroundColor");
	}

	switch (WeightmapImportResult)
	{
	case ECyLandImportResult::Success:
		return FCoreStyle::Get().GetColor("InfoReporting.BackgroundColor");
	case ECyLandImportResult::Warning:
		return FCoreStyle::Get().GetColor("ErrorReporting.WarningBackgroundColor");
	case ECyLandImportResult::Error:
		return FCoreStyle::Get().GetColor("ErrorReporting.BackgroundColor");
	default:
		check(0);
		return FSlateColor();
	}
}

FText FCyLandEditorStructCustomization_FCyLandImportLayer::GetErrorText(TSharedRef<IPropertyHandle> PropertyHandle_ErrorMessage)
{
	FText ErrorMessage;
	FPropertyAccess::Result Result = PropertyHandle_ErrorMessage->GetValue(ErrorMessage);
	if (Result == FPropertyAccess::Fail)
	{
		return LOCTEXT("Import_LayerUnknownError", "Unknown Error");
	}
	else if (Result == FPropertyAccess::MultipleValues)
	{
		return NSLOCTEXT("PropertyEditor", "MultipleValues", "Multiple Values");
	}

	return ErrorMessage;
}

#undef LOCTEXT_NAMESPACE
