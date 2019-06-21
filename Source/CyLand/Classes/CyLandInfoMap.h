// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Misc/Guid.h"

#include "CyLandInfoMap.generated.h"

class UCyLandInfo;

UCLASS()
class UCyLandInfoMap : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	void BeginDestroy() override;
	void PostDuplicate(bool bDuplicateForPIE) override;
	void Serialize(FArchive& Ar) override;

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	TMap<FGuid, UCyLandInfo*> Map;
	UWorld* World;


#if WITH_EDITORONLY_DATA
	/**
	* Gets landscape-specific data for given world.
	*
	* @param World A pointer to world that return data should be associated with.
	*
	* @returns CyLand-specific data associated with given world.
	*/
	CYLAND_API static UCyLandInfoMap& GetCyLandInfoMap(UWorld* World);
#endif // WITH_EDITORONLY_DATA
};
