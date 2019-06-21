// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CyLandEditorDetailCustomization_Base.h"
#include "EditorModeManager.h"
#include "CyLandEditorModule.h"
#include "CyLandEdMode.h"



FEdModeCyLand* FCyLandEditorDetailCustomization_Base::GetEditorMode()
{
	return (FEdModeCyLand*)GLevelEditorModeTools().GetActiveMode(ICyLandEditorModule::EM_Landscape_Mimic);
}

bool FCyLandEditorDetailCustomization_Base::IsToolActive(FName ToolName)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode != NULL && CyLandEdMode->CurrentTool != NULL)
	{
		const FName CurrentToolName = CyLandEdMode->CurrentTool->GetToolName();
		return CurrentToolName == ToolName;
	}

	return false;
}

bool FCyLandEditorDetailCustomization_Base::IsBrushSetActive(FName BrushSetName)
{
	FEdModeCyLand* CyLandEdMode = GetEditorMode();
	if (CyLandEdMode && CyLandEdMode->CurrentBrushSetIndex >= 0)
	{
		const FName CurrentBrushSetName = CyLandEdMode->CyLandBrushSets[CyLandEdMode->CurrentBrushSetIndex].BrushSetName;
		return CurrentBrushSetName == BrushSetName;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////

FEdModeCyLand* FCyLandEditorStructCustomization_Base::GetEditorMode()
{
	return (FEdModeCyLand*)GLevelEditorModeTools().GetActiveMode(ICyLandEditorModule::EM_Landscape_Mimic);
}
