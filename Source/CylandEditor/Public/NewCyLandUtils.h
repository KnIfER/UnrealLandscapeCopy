// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ACyLand;
struct FCyLandFileResolution;
struct FCyLandImportLayerInfo;
class UCyLandEditorObject;
//CYLANDEDITOR_API  CYLAND_API
class  FNewCyLandUtils
{
public:
	static void ChooseBestComponentSizeForImport( UCyLandEditorObject* UISettings );
	static void ImportCyLandData( UCyLandEditorObject* UISettings, TArray< FCyLandFileResolution >& ImportResolutions );
	static TOptional< TArray< FCyLandImportLayerInfo > > CreateImportLayersInfo( UCyLandEditorObject* UISettings, int32 NewCyLandPreviewMode );
	static TArray< uint16 > ComputeHeightData( UCyLandEditorObject* UISettings, TArray< FCyLandImportLayerInfo >& ImportLayers, int32 NewCyLandPreviewMode );

	static const int32 SectionSizes[6];
	static const int32 NumSections[2];
};
