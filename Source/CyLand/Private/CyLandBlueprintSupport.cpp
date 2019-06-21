// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	CyLandBlueprintSupport.cpp: CyLand blueprint functions
  =============================================================================*/

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "CyLandProxy.h"
#include "CyLandSplineSegment.h"
#include "CyLandSplineRaster.h"
#include "Components/SplineComponent.h"
#include "CyLandComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "CyLandProxy.h"

void ACyLandProxy::EditorApplySpline(USplineComponent* InSplineComponent, float StartWidth, float EndWidth, float StartSideFalloff, float EndSideFalloff, float StartRoll, float EndRoll, int32 NumSubdivisions, bool bRaiseHeights, bool bLowerHeights, UCyLandLayerInfoObject* PaintLayer)
{
#if WITH_EDITOR
	if (InSplineComponent && !GetWorld()->IsGameWorld())
	{
		TArray<FCyLandSplineInterpPoint> Points;
		CyLandSplineRaster::Pointify(InSplineComponent->SplineCurves.Position, Points, NumSubdivisions, 0.0f, 0.0f, StartWidth, EndWidth, StartSideFalloff, EndSideFalloff, StartRoll, EndRoll);

		FTransform SplineToWorld = InSplineComponent->GetComponentTransform();
		CyLandSplineRaster::RasterizeSegmentPoints(GetCyLandInfo(), MoveTemp(Points), SplineToWorld, bRaiseHeights, bLowerHeights, PaintLayer);
	}
#endif
}

void ACyLandProxy::SetCyLandMaterialTextureParameterValue(FName ParameterName, class UTexture* Value)
{	
	if (bUseDynamicMaterialInstance)
	{
		for (UCyLandComponent* Component : CyLandComponents)
		{
			for (UMaterialInstanceDynamic* MaterialInstance : Component->MaterialInstancesDynamic)
			{
				if (MaterialInstance != nullptr)
				{
					MaterialInstance->SetTextureParameterValue(ParameterName, Value);
				}
			}
		}
	}
}

void ACyLandProxy::SetCyLandMaterialVectorParameterValue(FName ParameterName, FLinearColor Value)
{
	if (bUseDynamicMaterialInstance)
	{
		for (UCyLandComponent* Component : CyLandComponents)
		{
			for (UMaterialInstanceDynamic* MaterialInstance : Component->MaterialInstancesDynamic)
			{
				if (MaterialInstance != nullptr)
				{
					MaterialInstance->SetVectorParameterValue(ParameterName, Value);
				}
			}
		}		
	}
}

void ACyLandProxy::SetCyLandMaterialScalarParameterValue(FName ParameterName, float Value)
{
	if (bUseDynamicMaterialInstance)
	{
		for (UCyLandComponent* Component : CyLandComponents)
		{
			for (UMaterialInstanceDynamic* MaterialInstance : Component->MaterialInstancesDynamic)
			{
				if (MaterialInstance != nullptr)
				{
					MaterialInstance->SetScalarParameterValue(ParameterName, Value);
				}
			}
		}			
	}
}

void ACyLandProxy::EditorSetCyLandMaterial(UMaterialInterface* NewCyLandMaterial)
{
#if WITH_EDITOR
	if (!GetWorld()->IsGameWorld())
	{
		CyLandMaterial = NewCyLandMaterial;
		FPropertyChangedEvent PropertyChangedEvent(FindFieldChecked<UProperty>(GetClass(), FName("CyLandMaterial")));
		PostEditChangeProperty(PropertyChangedEvent);
	}
#endif
}