// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CyLandComponent.h"
#include "CyLandLayerInfoObject.h"
#include "Materials/MaterialInstance.h"
#include "CyLandEdit.h"
#include "CyLandRender.h"

FName FCyWeightmapLayerAllocationInfo::GetLayerName() const
{
	if (LayerInfo)
	{
		return LayerInfo->LayerName;
	}
	return NAME_None;
}

#if WITH_EDITOR

void FCyLandEditToolRenderData::UpdateDebugColorMaterial(const UCyLandComponent* const Component)
{
	Component->GetLayerDebugColorKey(DebugChannelR, DebugChannelG, DebugChannelB);
}

void FCyLandEditToolRenderData::UpdateSelectionMaterial(int32 InSelectedType, const UCyLandComponent* const Component)
{
	// Check selection
	if (SelectedType != InSelectedType && (SelectedType & ST_REGION) && !(InSelectedType & ST_REGION))
	{
		// Clear Select textures...
		if (DataTexture)
		{
			FCyLandEditDataInterface CyLandEdit(Component->GetCyLandInfo());
			CyLandEdit.ZeroTexture(DataTexture);
		}
	}

	SelectedType = InSelectedType;
}

void UCyLandComponent::UpdateEditToolRenderData()
{
	FCyLandComponentSceneProxy* CyLandSceneProxy = (FCyLandComponentSceneProxy*)SceneProxy;

	if (CyLandSceneProxy != nullptr)
	{
		TArray<UMaterialInterface*> UsedMaterialsForVerification;
		const bool bGetDebugMaterials = true;
		GetUsedMaterials(UsedMaterialsForVerification, bGetDebugMaterials);

		FCyLandEditToolRenderData CyLandEditToolRenderData = EditToolRenderData;
		ENQUEUE_RENDER_COMMAND(UpdateEditToolRenderData)(
			[CyLandEditToolRenderData, CyLandSceneProxy, UsedMaterialsForVerification](FRHICommandListImmediate& RHICmdList)
			{
				CyLandSceneProxy->EditToolRenderData = CyLandEditToolRenderData;				
				CyLandSceneProxy->SetUsedMaterialForVerification(UsedMaterialsForVerification);
			});
	}
}

#endif
