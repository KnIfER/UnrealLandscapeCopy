// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CyLand/Classes/CyLand.h"
#include "CyLandProc.generated.h"


UCLASS()
class CYLAND_API UProceuduralGameLandUtils : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

	UFUNCTION(BlueprintCallable, Category = "asAS")
	static ACyLand* SpawnGameLand(AActor* context);

};