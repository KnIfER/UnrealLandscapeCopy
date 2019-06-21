// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CyLandEditorDetailCustomization_ResizeCyLand.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SlateOptMacros.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "CyLandEditorObject.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "DetailCategoryBuilder.h"

//#include "ObjectTools.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "CyLandEditor.ResizeCyLand"

const int32 FCyLandEditorDetailCustomization_ResizeCyLand::SectionSizes[] = {7, 15, 31, 63, 127, 255};
const int32 FCyLandEditorDetailCustomization_ResizeCyLand::NumSections[] = {1, 2};

TSharedRef<IDetailCustomization> FCyLandEditorDetailCustomization_ResizeCyLand::MakeInstance()
{
	return MakeShareable(new FCyLandEditorDetailCustomization_ResizeCyLand);
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void FCyLandEditorDetailCustomization_ResizeCyLand::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	if (!IsToolActive("ResizeCyLand"))
	{
		return;
	}

	IDetailCategoryBuilder& ResizeCyLandCategory = DetailBuilder.EditCategory("Change Component Size");

	ResizeCyLandCategory.AddCustomRow(LOCTEXT("OriginalNewLabel", "Original New"))
	//.NameContent()
	//[
	//]
	.ValueContent()
	.MinDesiredWidth(180)
	.MaxDesiredWidth(180)
	[
		SNew(SBox)
		.VAlign(VAlign_Center)
		.Padding(FMargin(0,8,12,2)) // Line up with the other properties due to having no reset to default button
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(DetailBuilder.GetDetailFont())
				.Text(LOCTEXT("Original", "Original"))
				.ToolTipText(LOCTEXT("Original_Tip", "The properties of the CyLand as it currently exists"))
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.FillWidth(1.1f)
			[
				SNew(STextBlock)
				.Font(DetailBuilder.GetDetailFont())
				.Text(LOCTEXT("New", "New"))
				.ToolTipText(LOCTEXT("New_Tip", "The properties the CyLand will have after the resize operation is completed"))
			]
		]
	];

	TSharedRef<IPropertyHandle> PropertyHandle_QuadsPerSection = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, ResizeCyLand_QuadsPerSection));
	ResizeCyLandCategory.AddProperty(PropertyHandle_QuadsPerSection)
	.OverrideResetToDefault(FResetToDefaultOverride::Create(
		FIsResetToDefaultVisible::CreateStatic(&FCyLandEditorDetailCustomization_ResizeCyLand::IsSectionSizeResetToDefaultVisible),
		FResetToDefaultHandler::CreateStatic(&FCyLandEditorDetailCustomization_ResizeCyLand::OnSectionSizeResetToDefault)))
	.CustomWidget()
	.NameContent()
	[
		PropertyHandle_QuadsPerSection->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(180)
	.MaxDesiredWidth(180)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(DetailBuilder.GetDetailFont())
			.Text_Static(&GetOriginalSectionSize)
		]
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.FillWidth(1.1f)
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
		]
	];

	TSharedRef<IPropertyHandle> PropertyHandle_SectionsPerComponent = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, ResizeCyLand_SectionsPerComponent));
	ResizeCyLandCategory.AddProperty(PropertyHandle_SectionsPerComponent)
	.OverrideResetToDefault(FResetToDefaultOverride::Create(
		FIsResetToDefaultVisible::CreateStatic(&FCyLandEditorDetailCustomization_ResizeCyLand::IsSectionsPerComponentResetToDefaultVisible),
		FResetToDefaultHandler::CreateStatic(&FCyLandEditorDetailCustomization_ResizeCyLand::OnSectionsPerComponentResetToDefault)))
	.CustomWidget()
	.NameContent()
	[
		PropertyHandle_SectionsPerComponent->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(180)
	.MaxDesiredWidth(180)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(DetailBuilder.GetDetailFont())
			.Text_Static(&GetOriginalSectionsPerComponent)
		]
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.FillWidth(1.1f)
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
		]
	];
	
	TSharedRef<IPropertyHandle> PropertyHandle_ConvertMode = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, ResizeCyLand_ConvertMode));
	ResizeCyLandCategory.AddProperty(PropertyHandle_ConvertMode)
	.CustomWidget()
	.NameContent()
	[
		PropertyHandle_ConvertMode->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(180)
	.MaxDesiredWidth(180)
	[
		PropertyHandle_ConvertMode->CreatePropertyValueWidget()
	];

	TSharedRef<IPropertyHandle> PropertyHandle_ComponentCount = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, ResizeCyLand_ComponentCount));
	TSharedRef<IPropertyHandle> PropertyHandle_ComponentCount_X = PropertyHandle_ComponentCount->GetChildHandle("X").ToSharedRef();
	TSharedRef<IPropertyHandle> PropertyHandle_ComponentCount_Y = PropertyHandle_ComponentCount->GetChildHandle("Y").ToSharedRef();
	ResizeCyLandCategory.AddProperty(PropertyHandle_ComponentCount)
	.OverrideResetToDefault(FResetToDefaultOverride::Hide())
	.CustomWidget()
	.NameContent()
	[
		PropertyHandle_ComponentCount->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(180)
	.MaxDesiredWidth(180)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(DetailBuilder.GetDetailFont())
			.Text_Static(&GetOriginalComponentCount)
		]
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.FillWidth(1.1f)
		[
			SNew(STextBlock)
			.Font(DetailBuilder.GetDetailFont())
			.Text_Static(&GetComponentCount, PropertyHandle_ComponentCount_X, PropertyHandle_ComponentCount_Y)
		]
	];

	ResizeCyLandCategory.AddCustomRow(LOCTEXT("Resolution", "Overall Resolution"))
	.NameContent()
	[
		SNew(SBox)
		.VAlign(VAlign_Center)
		.Padding(FMargin(2))
		[
			SNew(STextBlock)
			.Font(DetailBuilder.GetDetailFont())
			.Text(LOCTEXT("Resolution", "Overall Resolution"))
			.ToolTipText(LOCTEXT("Resolution_Tip", "Overall resolution of the entire CyLand in vertices"))
		]
	]
	.ValueContent()
	.MinDesiredWidth(180)
	.MaxDesiredWidth(180)
	[
		SNew(SBox)
		.VAlign(VAlign_Center)
		.Padding(FMargin(0,0,12,0)) // Line up with the other properties due to having no reset to default button
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(DetailBuilder.GetDetailFont())
				.Text_Static(&GetOriginalCyLandResolution)
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.FillWidth(1.1f)
			[
				SNew(STextBlock)
				.Font(DetailBuilder.GetDetailFont())
				.Text_Static(&GetCyLandResolution)
			]
		]
	];

	ResizeCyLandCategory.AddCustomRow(LOCTEXT("TotalComponents", "Total Components"))
	.NameContent()
	[
		SNew(SBox)
		.VAlign(VAlign_Center)
		.Padding(FMargin(2))
		[
			SNew(STextBlock)
			.Font(DetailBuilder.GetDetailFont())
			.Text(LOCTEXT("TotalComponents", "Total Components"))
			.ToolTipText(LOCTEXT("TotalComponents_Tip", "The total number of components in the CyLand"))
		]
	]
	.ValueContent()
	.MinDesiredWidth(180)
	.MaxDesiredWidth(180)
	[
		SNew(SBox)
		.VAlign(VAlign_Center)
		.Padding(FMargin(0,0,12,0)) // Line up with the other properties due to having no reset to default button
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(DetailBuilder.GetDetailFont())
				.Text_Static(&GetOriginalTotalComponentCount)
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.FillWidth(1.1f)
			[
				SNew(STextBlock)
				.Font(DetailBuilder.GetDetailFont())
				.Text_Static(&GetTotalComponentCount)
			]
		]
	];

	ResizeCyLandCategory.AddCustomRow(FText::GetEmpty())
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1)
		//[
		//]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("Apply", "Apply"))
			.OnClicked(this, &FCyLandEditorDetailCustomization_ResizeCyLand::OnApplyButtonClicked)
		]
	];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

FText FCyLandEditorDetailCustomization_ResizeCyLand::GetOriginalSectionSize()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != NULL)
	{
		return FText::Format(LOCTEXT("NxNQuads", "{0}x{0} Quads"), FText::AsNumber(CyLandEdMode->UISettings->ResizeCyLand_Original_QuadsPerSection));
	}

	return FText::FromString("---");
}

TSharedRef<SWidget> FCyLandEditorDetailCustomization_ResizeCyLand::GetSectionSizeMenu(TSharedRef<IPropertyHandle> PropertyHandle)
{
	FMenuBuilder MenuBuilder(true, NULL);

	for (int32 i = 0; i < ARRAY_COUNT(SectionSizes); i++)
	{
		MenuBuilder.AddMenuEntry(FText::Format(LOCTEXT("NxNQuads", "{0}x{0} Quads"), FText::AsNumber(SectionSizes[i])), FText::GetEmpty(), FSlateIcon(), FExecuteAction::CreateStatic(&OnChangeSectionSize, PropertyHandle, SectionSizes[i]));
	}

	return MenuBuilder.MakeWidget();
}

void FCyLandEditorDetailCustomization_ResizeCyLand::OnChangeSectionSize(TSharedRef<IPropertyHandle> PropertyHandle, int32 NewSize)
{
	ensure(PropertyHandle->SetValue(NewSize) == FPropertyAccess::Success);
}

FText FCyLandEditorDetailCustomization_ResizeCyLand::GetSectionSize(TSharedRef<IPropertyHandle> PropertyHandle)
{
	int32 QuadsPerSection = 0;
	FPropertyAccess::Result Result = PropertyHandle->GetValue(QuadsPerSection);
	check(Result == FPropertyAccess::Success);

	if (Result == FPropertyAccess::MultipleValues)
	{
		return NSLOCTEXT("PropertyEditor", "MultipleValues", "Multiple Values");
	}

	return FText::Format(LOCTEXT("NxNQuads", "{0}x{0} Quads"), FText::AsNumber(QuadsPerSection));
}

bool FCyLandEditorDetailCustomization_ResizeCyLand::IsSectionSizeResetToDefaultVisible(TSharedPtr<IPropertyHandle> PropertyHandle)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != NULL)
	{
		return CyLandEdMode->UISettings->ResizeCyLand_QuadsPerSection != CyLandEdMode->UISettings->ResizeCyLand_Original_QuadsPerSection;
	}

	return false;
}

void FCyLandEditorDetailCustomization_ResizeCyLand::OnSectionSizeResetToDefault(TSharedPtr<IPropertyHandle> PropertyHandle)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != NULL)
	{
		CyLandEdMode->UISettings->ResizeCyLand_QuadsPerSection = CyLandEdMode->UISettings->ResizeCyLand_Original_QuadsPerSection;
	}
}

FText FCyLandEditorDetailCustomization_ResizeCyLand::GetOriginalSectionsPerComponent()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != NULL)
	{
		int32 SectionsPerComponent = CyLandEdMode->UISettings->ResizeCyLand_Original_SectionsPerComponent;

		FFormatNamedArguments Args;
		Args.Add(TEXT("Width"), SectionsPerComponent);
		Args.Add(TEXT("Height"), SectionsPerComponent);
		return FText::Format(SectionsPerComponent == 1 ? LOCTEXT("1x1Section", "{Width}\u00D7{Height} Section") : LOCTEXT("NxNSections", "{Width}\u00D7{Height} Sections"), Args);
	}

	return FText::FromString("---");
}

TSharedRef<SWidget> FCyLandEditorDetailCustomization_ResizeCyLand::GetSectionsPerComponentMenu(TSharedRef<IPropertyHandle> PropertyHandle)
{
	FMenuBuilder MenuBuilder(true, NULL);

	for (int32 i = 0; i < ARRAY_COUNT(NumSections); i++)
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("Width"), NumSections[i]);
		Args.Add(TEXT("Height"), NumSections[i]);
		MenuBuilder.AddMenuEntry(FText::Format(NumSections[i] == 1 ? LOCTEXT("1x1Section", "{Width}\u00D7{Height} Section") : LOCTEXT("NxNSections", "{Width}\u00D7{Height} Sections"), Args), FText::GetEmpty(), FSlateIcon(), FExecuteAction::CreateStatic(&OnChangeSectionsPerComponent, PropertyHandle, NumSections[i]));
	}

	return MenuBuilder.MakeWidget();
}

void FCyLandEditorDetailCustomization_ResizeCyLand::OnChangeSectionsPerComponent(TSharedRef<IPropertyHandle> PropertyHandle, int32 NewSize)
{
	ensure(PropertyHandle->SetValue(NewSize) == FPropertyAccess::Success);
}

FText FCyLandEditorDetailCustomization_ResizeCyLand::GetSectionsPerComponent(TSharedRef<IPropertyHandle> PropertyHandle)
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

bool FCyLandEditorDetailCustomization_ResizeCyLand::IsSectionsPerComponentResetToDefaultVisible(TSharedPtr<IPropertyHandle> PropertyHandle)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != NULL)
	{
		return CyLandEdMode->UISettings->ResizeCyLand_SectionsPerComponent != CyLandEdMode->UISettings->ResizeCyLand_Original_SectionsPerComponent;
	}

	return false;
}

void FCyLandEditorDetailCustomization_ResizeCyLand::OnSectionsPerComponentResetToDefault(TSharedPtr<IPropertyHandle> PropertyHandle)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != NULL)
	{
		CyLandEdMode->UISettings->ResizeCyLand_SectionsPerComponent = CyLandEdMode->UISettings->ResizeCyLand_Original_SectionsPerComponent;
	}
}

FText FCyLandEditorDetailCustomization_ResizeCyLand::GetOriginalComponentCount()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != NULL)
	{
		return FText::Format(LOCTEXT("NxN", "{0}\u00D7{1}"),
			FText::AsNumber(CyLandEdMode->UISettings->ResizeCyLand_Original_ComponentCount.X),
			FText::AsNumber(CyLandEdMode->UISettings->ResizeCyLand_Original_ComponentCount.Y));
	}
	return FText::FromString(TEXT("---"));
}

FText FCyLandEditorDetailCustomization_ResizeCyLand::GetComponentCount(TSharedRef<IPropertyHandle> PropertyHandle_ComponentCount_X, TSharedRef<IPropertyHandle> PropertyHandle_ComponentCount_Y)
{
	return FText::Format(LOCTEXT("NxN", "{0}\u00D7{1}"),
		FCyLandEditorDetailCustomization_Base::GetPropertyValueText(PropertyHandle_ComponentCount_X),
		FCyLandEditorDetailCustomization_Base::GetPropertyValueText(PropertyHandle_ComponentCount_Y));
}

FText FCyLandEditorDetailCustomization_ResizeCyLand::GetOriginalCyLandResolution()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != NULL)
	{
		const int32 Original_ComponentSizeQuads = CyLandEdMode->UISettings->ResizeCyLand_Original_SectionsPerComponent * CyLandEdMode->UISettings->ResizeCyLand_Original_QuadsPerSection;
		return FText::Format(LOCTEXT("NxN", "{0}\u00D7{1}"),
			FText::AsNumber(CyLandEdMode->UISettings->ResizeCyLand_Original_ComponentCount.X * Original_ComponentSizeQuads + 1),
			FText::AsNumber(CyLandEdMode->UISettings->ResizeCyLand_Original_ComponentCount.Y * Original_ComponentSizeQuads + 1));
	}

	return FText::FromString(TEXT("---"));
}

FText FCyLandEditorDetailCustomization_ResizeCyLand::GetCyLandResolution()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != NULL)
	{
		const int32 ComponentSizeQuads = CyLandEdMode->UISettings->ResizeCyLand_SectionsPerComponent * CyLandEdMode->UISettings->ResizeCyLand_QuadsPerSection;
		return FText::Format(LOCTEXT("NxN", "{0}\u00D7{1}"),
			FText::AsNumber(CyLandEdMode->UISettings->ResizeCyLand_ComponentCount.X * ComponentSizeQuads + 1),
			FText::AsNumber(CyLandEdMode->UISettings->ResizeCyLand_ComponentCount.Y * ComponentSizeQuads + 1));
	}

	return FText::FromString(TEXT("---"));
}

FText FCyLandEditorDetailCustomization_ResizeCyLand::GetOriginalTotalComponentCount()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != NULL)
	{
		return FText::AsNumber(CyLandEdMode->UISettings->ResizeCyLand_Original_ComponentCount.X * CyLandEdMode->UISettings->ResizeCyLand_Original_ComponentCount.Y);
	}

	return FText::FromString(TEXT("---"));
}

FText FCyLandEditorDetailCustomization_ResizeCyLand::GetTotalComponentCount()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != NULL)
	{
		return FText::AsNumber(CyLandEdMode->UISettings->ResizeCyLand_ComponentCount.X * CyLandEdMode->UISettings->ResizeCyLand_ComponentCount.Y);
	}

	return FText::FromString(TEXT("---"));
}

FReply FCyLandEditorDetailCustomization_ResizeCyLand::OnApplyButtonClicked()
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != NULL)
	{
		FScopedTransaction Transaction(LOCTEXT("Undo", "Changing CyLand Component Size"));

		const FIntPoint ComponentCount = CyLandEdMode->UISettings->ResizeCyLand_ComponentCount;
		const int32 SectionsPerComponent = CyLandEdMode->UISettings->ResizeCyLand_SectionsPerComponent;
		const int32 QuadsPerSection = CyLandEdMode->UISettings->ResizeCyLand_QuadsPerSection;
		const bool bResample = (CyLandEdMode->UISettings->ResizeCyLand_ConvertMode == ECyLandConvertMode::Resample);
		CyLandEdMode->ChangeComponentSetting(ComponentCount.X, ComponentCount.Y, SectionsPerComponent, QuadsPerSection, bResample);

		CyLandEdMode->UpdateCyLandList();
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
