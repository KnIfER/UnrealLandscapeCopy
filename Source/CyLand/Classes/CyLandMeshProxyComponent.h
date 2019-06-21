// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "Components/StaticMeshComponent.h"

#include "CyLandMeshProxyComponent.generated.h"

class ACyLandProxy;
class FPrimitiveSceneProxy;

UCLASS(MinimalAPI)
class UCyLandMeshProxyComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UCyLandMeshProxyComponent(const FObjectInitializer& ObjectInitializer);

private:
	/* The landscape this proxy was generated for */
	UPROPERTY()
	FGuid CyLandGuid;

	/* The components this proxy was generated for */
	UPROPERTY()
	TArray<FIntPoint> ProxyComponentBases;

	/* LOD level proxy was generated for */
	UPROPERTY()
	int8 ProxyLOD;

public:
	CYLAND_API void InitializeForCyLand(ACyLandProxy* CyLand, int8 InProxyLOD);
	
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
};

