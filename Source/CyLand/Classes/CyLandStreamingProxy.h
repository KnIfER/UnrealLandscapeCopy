// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "CyLandProxy.h"
#include "CyLandStreamingProxy.generated.h"

class ACyLand;
class UMaterialInterface;

UCLASS(MinimalAPI, notplaceable)
class ACyLandStreamingProxy : public ACyLandProxy
{
	GENERATED_BODY()

public:
	ACyLandStreamingProxy(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, Category=CyLandProxy)
	TLazyObjectPtr<ACyLand> CyLandActor;

	//~ Begin UObject Interface
#if WITH_EDITOR
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject Interface

	//~ Begin ACyLandBase Interface
	virtual ACyLand* GetCyLandActor() override;
#if WITH_EDITOR
	virtual UMaterialInterface* GetCyLandMaterial(int8 InLODIndex = INDEX_NONE) const override;
	virtual UMaterialInterface* GetCyLandHoleMaterial() const override;
#endif
	//~ End ACyLandBase Interface

	// Check input CyLand actor is match for this CyLandProxy (by GUID)
	bool IsValidCyLandActor(ACyLand* CyLand);
};
