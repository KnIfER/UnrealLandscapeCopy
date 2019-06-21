// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CyLandInfo.h"
#include "CyLandLayerInfoObject.h"

CYLAND_API FCyLandInfoLayerSettings::FCyLandInfoLayerSettings(UCyLandLayerInfoObject* InLayerInfo, class ACyLandProxy* InProxy)
	: LayerInfoObj(InLayerInfo)
	, LayerName((InLayerInfo != NULL) ? InLayerInfo->LayerName : NAME_None)
#if WITH_EDITORONLY_DATA
	, ThumbnailMIC(NULL)
	, Owner(InProxy)
	, DebugColorChannel(0)
	, bValid(false)
#endif
{
}
