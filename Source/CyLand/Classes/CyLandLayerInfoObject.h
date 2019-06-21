// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "CyLandLayerInfoObject.generated.h"

class UPhysicalMaterial;
struct FPropertyChangedEvent;

UCLASS(MinimalAPI)
class UCyLandLayerInfoObject : public UObject
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(VisibleAnywhere, Category=CyLandLayerInfoObject, AssetRegistrySearchable)
	FName LayerName;

	UPROPERTY(EditAnywhere, Category=CyLandLayerInfoObject)
	UPhysicalMaterial* PhysMaterial;

	UPROPERTY(EditAnywhere, Category=CyLandLayerInfoObject)
	float Hardness;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category=CyLandLayerInfoObject)
	uint32 bNoWeightBlend:1;

	UPROPERTY(NonTransactional, Transient)
	bool IsReferencedFromLoadedData;
#endif // WITH_EDITORONLY_DATA

	/* The color to use for layer usage debug */
	UPROPERTY(EditAnywhere, Category=CyLandLayerInfoObject)
	FLinearColor LayerUsageDebugColor;

#if WITH_EDITOR
	//~ Begin UObject Interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostLoad() override;
	//~ End UObject Interface
#endif
};
