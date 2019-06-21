// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CyLandInfoMap.h"
#include "Engine/World.h"
#include "CyLandInfo.h"

UCyLandInfoMap::UCyLandInfoMap(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, World(nullptr)
{
}

void UCyLandInfoMap::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	check(Map.Num() == 0);
}

void UCyLandInfoMap::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsTransacting() || Ar.IsObjectReferenceCollector())
	{
		Ar << Map;
	}
}

void UCyLandInfoMap::BeginDestroy()
{
	if (World != nullptr)
	{
		World->PerModuleDataObjects.Remove(this);
	}

	Super::BeginDestroy();
}

void UCyLandInfoMap::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UCyLandInfoMap* This = CastChecked<UCyLandInfoMap>(InThis);
	Collector.AddReferencedObjects(This->Map, This);
}

#if WITH_EDITORONLY_DATA
UCyLandInfoMap& UCyLandInfoMap::GetCyLandInfoMap(UWorld* World)
{
	UCyLandInfoMap *FoundObject = nullptr;
	World->PerModuleDataObjects.FindItemByClass(&FoundObject);

	if (!FoundObject) {
		EObjectFlags NewLandscapeDataFlags = RF_NoFlags;
		if (World->HasAnyFlags(RF_Transactional))
		{
			NewLandscapeDataFlags = RF_Transactional;
		}
		FoundObject = NewObject<UCyLandInfoMap>(GetTransientPackage(), NAME_None, NewLandscapeDataFlags);
		FoundObject->World = World;
		World->PerModuleDataObjects.Add(FoundObject);
	}

	checkf(FoundObject, TEXT("ULandscapInfoMap object was not created for this UWorld."));

	return *FoundObject;
}
#endif // WITH_EDITORONLY_DATA
