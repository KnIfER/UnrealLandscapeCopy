// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Materials/MaterialExpressionCyLandLayerWeight.h"
#include "Engine/Engine.h"
#include "EngineGlobals.h"
#include "MaterialCompiler.h"
#include "Materials/Material.h"

#define LOCTEXT_NAMESPACE "CyLand"


///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionCyLandLayerWeight
///////////////////////////////////////////////////////////////////////////////

UMaterialExpressionCyLandLayerWeight::UMaterialExpressionCyLandLayerWeight(const FObjectInitializer& ObjectInitializer)
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

	PreviewWeight = 0.0f;
	ConstBase = FVector(0.f, 0.f, 0.f);
}


FGuid& UMaterialExpressionCyLandLayerWeight::GetParameterExpressionId()
{
	return ExpressionGUID;
}


void UMaterialExpressionCyLandLayerWeight::PostLoad()
{
	Super::PostLoad();

	if (GetLinkerUE4Version() < VER_UE4_FIXUP_TERRAIN_LAYER_NODES)
	{
		UpdateParameterGuid(true, true);
	}
}

#if WITH_EDITOR
bool UMaterialExpressionCyLandLayerWeight::IsResultMaterialAttributes(int32 OutputIndex)
{
	if (ContainsInputLoop())
	{
		// If there is a loop anywhere in this expression's inputs then we can't risk checking them
		return false;
	}
	bool bLayerIsMaterialAttributes = Layer.Expression != nullptr && Layer.Expression->IsResultMaterialAttributes(Layer.OutputIndex);
	bool bBaseIsMaterialAttributes = Base.Expression != nullptr && Base.Expression->IsResultMaterialAttributes(Base.OutputIndex);
	return bLayerIsMaterialAttributes || bBaseIsMaterialAttributes;
}

int32 UMaterialExpressionCyLandLayerWeight::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	const int32 BaseCode = Base.Expression ? Base.Compile(Compiler) : Compiler->Constant3(ConstBase.X, ConstBase.Y, ConstBase.Z);
	const int32 WeightCode = Compiler->StaticTerrainLayerWeight(ParameterName, Compiler->Constant(PreviewWeight));

	int32 ReturnCode = INDEX_NONE;
	if (WeightCode == INDEX_NONE)
	{
		ReturnCode = BaseCode;
	}
	else
	{
		const int32 LayerCode = Layer.Compile(Compiler);
		ReturnCode = Compiler->Add(BaseCode, Compiler->Mul(LayerCode, WeightCode));
	}

	if (ReturnCode != INDEX_NONE && //If we've already failed for some other reason don't bother with this check. It could have been the reentrant check causing this to loop infinitely!
		Layer.Expression != nullptr && Base.Expression != nullptr &&
		Layer.Expression->IsResultMaterialAttributes(Layer.OutputIndex) != Base.Expression->IsResultMaterialAttributes(Base.OutputIndex))
	{
		Compiler->Error(TEXT("Cannot mix MaterialAttributes and non MaterialAttributes nodes"));
	}

	return ReturnCode;
}
#endif // WITH_EDITOR

UTexture* UMaterialExpressionCyLandLayerWeight::GetReferencedTexture()
{
	return GEngine->WeightMapPlaceholderTexture;
}

#if WITH_EDITOR
void UMaterialExpressionCyLandLayerWeight::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString::Printf(TEXT("Layer '%s'"), *ParameterName.ToString()));
}

bool UMaterialExpressionCyLandLayerWeight::MatchesSearchQuery(const TCHAR* SearchQuery)
{
	TArray<FString> Captions;
	GetCaption(Captions);
	for (const FString Caption : Captions)
	{
		if (Caption.Contains(SearchQuery))
		{
			return true;
		}
	}

	return Super::MatchesSearchQuery(SearchQuery);
}

#endif // WITH_EDITOR

void UMaterialExpressionCyLandLayerWeight::GetAllParameterInfo(TArray<FMaterialParameterInfo> &OutParameterInfo, TArray<FGuid> &OutParameterIds, const FMaterialParameterInfo& InBaseParameterInfo) const
{
	int32 CurrentSize = OutParameterInfo.Num();
	FMaterialParameterInfo NewParameter(ParameterName, InBaseParameterInfo.Association, InBaseParameterInfo.Index);
	OutParameterInfo.AddUnique(NewParameter);

	if (CurrentSize != OutParameterInfo.Num())
	{
		OutParameterIds.Add(ExpressionGUID);
	}
}

bool UMaterialExpressionCyLandLayerWeight::NeedsLoadForClient() const
{
	return ParameterName != NAME_None;
}

#undef LOCTEXT_NAMESPACE
