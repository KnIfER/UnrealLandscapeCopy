// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"

#include "CyLandMeshProxyActor.generated.h"

UCLASS(MinimalAPI)
class ACyLandMeshProxyActor : public AActor
{
	GENERATED_UCLASS_BODY()

private:
	UPROPERTY(Category = CyLandMeshProxyActor, VisibleAnywhere, BlueprintReadOnly, meta = (ExposeFunctionCategories = "Mesh,Rendering,Physics,Components|StaticMesh", AllowPrivateAccess = "true"))
	class UCyLandMeshProxyComponent* CyLandMeshProxyComponent;

public:
	/** Returns StaticMeshComponent subobject **/
	class UCyLandMeshProxyComponent* GetCyLandMeshProxyComponent() const { return CyLandMeshProxyComponent; }
};

