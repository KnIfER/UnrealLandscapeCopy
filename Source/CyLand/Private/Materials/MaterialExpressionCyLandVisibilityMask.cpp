// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Materials/MaterialExpressionCyLandVisibilityMask.h"
#include "Engine/Engine.h"
#include "EngineGlobals.h"
#include "MaterialCompiler.h"
#include "Materials/Material.h"

#define LOCTEXT_NAMESPACE "CyLand"


///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionCyLandVisibilityMask
///////////////////////////////////////////////////////////////////////////////

FName UMaterialExpressionCyLandVisibilityMask::ParameterName = FName("__LANDSCAPE_VISIBILITY__");

UMaterialExpressionCyLandVisibilityMask::UMaterialExpressionCyLandVisibilityMask(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_CyLand;
		FConstructorStatics()
			: NAME_CyLand(LOCTEXT("CyLand", "CyLand"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	bIsParameterExpression = true;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_CyLand);
#endif
}

FGuid& UMaterialExpressionCyLandVisibilityMask::GetParameterExpressionId()
{
	return ExpressionGUID;
}

#if WITH_EDITOR
int32 UMaterialExpressionCyLandVisibilityMask::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 MaskLayerCode = Compiler->StaticTerrainLayerWeight(ParameterName, Compiler->Constant(0.f));
	return MaskLayerCode == INDEX_NONE ? Compiler->Constant(1.f) : Compiler->Sub(Compiler->Constant(1.f), MaskLayerCode);
}
#endif // WITH_EDITOR

UTexture* UMaterialExpressionCyLandVisibilityMask::GetReferencedTexture()
{
	return GEngine->WeightMapPlaceholderTexture;
}

void UMaterialExpressionCyLandVisibilityMask::GetAllParameterInfo(TArray<FMaterialParameterInfo> &OutParameterInfo, TArray<FGuid> &OutParameterIds, const FMaterialParameterInfo& InBaseParameterInfo) const
{
	int32 CurrentSize = OutParameterInfo.Num();
	FMaterialParameterInfo NewParameter(ParameterName, InBaseParameterInfo.Association, InBaseParameterInfo.Index);
	OutParameterInfo.AddUnique(NewParameter);

	if (CurrentSize != OutParameterInfo.Num())
	{
		OutParameterIds.Add(ExpressionGUID);
	}
}

#if WITH_EDITOR
void UMaterialExpressionCyLandVisibilityMask::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString(TEXT("CyLand Visibility Mask")));
}
#endif // WITH_EDITOR

bool UMaterialExpressionCyLandVisibilityMask::NeedsLoadForClient() const
{
	return ParameterName != NAME_None;
}

#undef LOCTEXT_NAMESPACE
