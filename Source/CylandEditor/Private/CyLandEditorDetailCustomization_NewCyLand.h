// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Visibility.h"
#include "Styling/SlateColor.h"
#include "Input/Reply.h"
#include "Widgets/SWidget.h"
#include "Private/CyLandEdMode.h"
#include "CyLandFileFormatInterface.h"
#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "PropertyHandle.h"
#include "Private/CyLandEditorDetailCustomization_Base.h"

class IDetailLayoutBuilder;

/**
 * Slate widgets customizer for the "New CyLand" tool
 */

class FCyLandEditorDetailCustomization_NewCyLand : public FCyLandEditorDetailCustomization_Base
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

public:
	static void SetScale(float NewValue, ETextCommit::Type, TSharedRef<IPropertyHandle> PropertyHandle);

	static TSharedRef<SWidget> GetSectionSizeMenu(TSharedRef<IPropertyHandle> PropertyHandle);
	static void OnChangeSectionSize(TSharedRef<IPropertyHandle> PropertyHandle, int32 NewSize);
	static FText GetSectionSize(TSharedRef<IPropertyHandle> PropertyHandle);

	static TSharedRef<SWidget> GetSectionsPerComponentMenu(TSharedRef<IPropertyHandle> PropertyHandle);
	static void OnChangeSectionsPerComponent(TSharedRef<IPropertyHandle> PropertyHandle, int32 NewSize);
	static FText GetSectionsPerComponent(TSharedRef<IPropertyHandle> PropertyHandle);

	TOptional<int32> GetCyLandResolutionX() const;
	void OnChangeCyLandResolutionX(int32 NewValue);
	void OnCommitCyLandResolutionX(int32 NewValue, ETextCommit::Type CommitInfo);

	TOptional<int32> GetCyLandResolutionY() const;
	void OnChangeCyLandResolutionY(int32 NewValue);
	void OnCommitCyLandResolutionY(int32 NewValue, ETextCommit::Type CommitInfo);

	TOptional<int32> GetMinCyLandResolution() const;
	TOptional<int32> GetMaxCyLandResolution() const;

	FText GetTotalComponentCount() const;

	FReply OnCreateButtonClicked();
	FReply OnFillWorldButtonClicked();

	static EVisibility GetVisibilityOnlyInNewCyLandMode(ENewCyLandPreviewMode::Type value);
	ECheckBoxState NewCyLandModeIsChecked(ENewCyLandPreviewMode::Type value) const;
	void OnNewCyLandModeChanged(ECheckBoxState NewCheckedState, ENewCyLandPreviewMode::Type value);

	// Import
	static EVisibility GetHeightmapErrorVisibility(TSharedRef<IPropertyHandle> PropertyHandle_HeightmapImportResult);
	static FSlateColor GetHeightmapErrorColor(TSharedRef<IPropertyHandle> PropertyHandle_HeightmapImportResult);
	static void SetImportHeightmapFilenameString(const FText& NewValue, ETextCommit::Type CommitInfo, TSharedRef<IPropertyHandle> PropertyHandle_HeightmapFilename);
	void OnImportHeightmapFilenameChanged();
	static FReply OnImportHeightmapFilenameButtonClicked(TSharedRef<IPropertyHandle> PropertyHandle_HeightmapFilename);

	TSharedRef<SWidget> GetImportCyLandResolutionMenu();
	void OnChangeImportCyLandResolution(int32 NewConfigIndex);
	FText GetImportCyLandResolution() const;

	bool GetImportButtonIsEnabled() const;
	FReply OnFitImportDataButtonClicked();
	void ChooseBestComponentSizeForImport(FEdModeCyLand* CyLandEdMode);

	FText GetOverallResolutionTooltip() const;

	// Import layers
	EVisibility GetMaterialTipVisibility() const;

protected:
	TArray<FCyLandFileResolution> ImportResolutions;
};

class FCyLandEditorStructCustomization_FCyLandImportLayer : public FCyLandEditorStructCustomization_Base
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

public:
	static FReply OnLayerFilenameButtonClicked(TSharedRef<IPropertyHandle> PropertyHandle_LayerFilename);
	static bool ShouldFilterLayerInfo(const struct FAssetData& AssetData, FName LayerName);

	static EVisibility GetImportLayerCreateVisibility(TSharedRef<IPropertyHandle> PropertyHandle_LayerInfo);
	static TSharedRef<SWidget> OnGetImportLayerCreateMenu(TSharedRef<IPropertyHandle> PropertyHandle_LayerInfo, FName LayerName);
	static void OnImportLayerCreateClicked(TSharedRef<IPropertyHandle> PropertyHandle_LayerInfo, FName LayerName, bool bNoWeightBlend);

	static EVisibility GetErrorVisibility(TSharedRef<IPropertyHandle> PropertyHandle_ImportResult);
	static FSlateColor GetErrorColor(TSharedRef<IPropertyHandle> PropertyHandle_ImportResult);
	static FText GetErrorText(TSharedRef<IPropertyHandle> PropertyHandle_ErrorMessage);
};
