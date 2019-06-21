// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CyLandProxy.h"
#include "CyLandInfo.h"

#if WITH_EDITOR

CYLAND_API FCyLandImportLayerInfo::FCyLandImportLayerInfo(const FCyLandInfoLayerSettings& InLayerSettings)
	: LayerName(InLayerSettings.GetLayerName())
	, LayerInfo(InLayerSettings.LayerInfoObj)
	, SourceFilePath(InLayerSettings.GetEditorSettings().ReimportLayerFilePath)
{
}

#endif // WITH_EDITOR
