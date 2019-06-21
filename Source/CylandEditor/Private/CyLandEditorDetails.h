// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Visibility.h"
#include "Textures/SlateIcon.h"
#include "Widgets/SWidget.h"
#include "UnrealClient.h"
#include "IDetailCustomization.h"
#include "Private/CyLandEditorDetailCustomization_Base.h"
#include "CyLandEditorDetailCustomization_ProceduralLayers.h"
#include "CyLandEditorDetailCustomization_ProceduralBrushStack.h"

class FCyLandEditorDetailCustomization_AlphaBrush;
class FCyLandEditorDetailCustomization_CopyPaste;
class FCyLandEditorDetailCustomization_MiscTools;
class FCyLandEditorDetailCustomization_NewCyLand;
class FCyLandEditorDetailCustomization_ResizeCyLand;
class FCyLandEditorDetailCustomization_TargetLayers;
class FUICommandList;
class IDetailLayoutBuilder;
class UCyLandInfo;

class FCyLandEditorDetails : public FCyLandEditorDetailCustomization_Base
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

protected:
	static FText GetLocalizedName(FString Name);

	static EVisibility GetTargetCyLandSelectorVisibility();
	static FText GetTargetCyLandName();
	static TSharedRef<SWidget> GetTargetCyLandMenu();
	static void OnChangeTargetCyLand(TWeakObjectPtr<UCyLandInfo> CyLandInfo);

	FText GetCurrentToolName() const;
	FSlateIcon GetCurrentToolIcon() const;
	TSharedRef<SWidget> GetToolSelector();
	bool GetToolSelectorIsVisible() const;
	EVisibility GetToolSelectorVisibility() const;

	FText GetCurrentBrushName() const;
	FSlateIcon GetCurrentBrushIcon() const;
	TSharedRef<SWidget> GetBrushSelector();
	bool GetBrushSelectorIsVisible() const;

	FText GetCurrentBrushFalloffName() const;
	FSlateIcon GetCurrentBrushFalloffIcon() const;
	TSharedRef<SWidget> GetBrushFalloffSelector();
	bool GetBrushFalloffSelectorIsVisible() const;

	TSharedPtr<FUICommandList> CommandList;

	TSharedPtr<FCyLandEditorDetailCustomization_NewCyLand> Customization_NewCyLand;
	TSharedPtr<FCyLandEditorDetailCustomization_ResizeCyLand> Customization_ResizeCyLand;
	TSharedPtr<FCyLandEditorDetailCustomization_CopyPaste> Customization_CopyPaste;
	TSharedPtr<FCyLandEditorDetailCustomization_MiscTools> Customization_MiscTools;
	TSharedPtr<FCyLandEditorDetailCustomization_AlphaBrush> Customization_AlphaBrush;
	TSharedPtr<FCyLandEditorDetailCustomization_TargetLayers> Customization_TargetLayers;
	TSharedPtr<FCyLandEditorDetailCustomization_ProceduralLayers> Customization_ProceduralLayers;
	TSharedPtr<FCyLandEditorDetailCustomization_ProceduralBrushStack> Customization_ProceduralBrushStack;
};
