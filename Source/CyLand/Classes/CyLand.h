// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "CyLandProxy.h"
#include "CyLandBPCustomBrush.h"

#include "CyLand.generated.h"

class UCyLandComponent;

UENUM()
enum ECyLandSetupErrors
{
	CyLSE_None,
	/** No CyLand Info available. */
	CyLSE_NoCyLandInfo,
	/** There was already component with same X,Y. */
	CyLSE_CollsionXY,
	/** No Layer Info, need to add proper layers. */
	CyNoLayerInfo,
	CyLSE_MAX,
};


enum class ERTDrawingType : uint8
{
	RTAtlas,
	RTAtlasToNonAtlas,
	RTNonAtlasToAtlas,
	RTNonAtlas,
	RTMips
};

enum EHeightmapRTType : uint8
{
	CyLandSizeCombinedAtlas,
	CyLandSizeCombinedNonAtlas,
	CyLandSizeScratch1,
	CyLandSizeScratch2,
	CyLandSizeScratch3,
	// Mips RT
	CyLandSizeMip1,
	CyLandSizeMip2,
	CyLandSizeMip3,
	CyLandSizeMip4,
	CyLandSizeMip5,
	CyLandSizeMip6,
	CyLandSizeMip7,
	Count
};

enum EProceduralContentUpdateFlag : uint32
{
	Heightmap_Setup					= 0x00000001u,
	Heightmap_Render				= 0x00000002u,
	Heightmap_BoundsAndCollision	= 0x00000004u,
	Heightmap_ResolveToTexture		= 0x00000008u,
	Heightmap_ResolveToTextureDDC	= 0x00000010u,

	// TODO: add weightmap update type
	Weightmap_Setup					= 0x00000100u,
	Weightmap_Render				= 0x00000200u,
	Weightmap_ResolveToTexture		= 0x00000400u,
	Weightmap_ResolveToTextureDDC	= 0x00000800u,

	// Combinations
	Heightmap_All = Heightmap_Render | Heightmap_BoundsAndCollision | Heightmap_ResolveToTexture,
	Heightmap_All_WithDDCUpdate = Heightmap_Render | Heightmap_BoundsAndCollision | Heightmap_ResolveToTextureDDC,
	Weightmap_All = Weightmap_Render | Weightmap_ResolveToTexture,
	Weightmap_All_WithDDCUpdate = Weightmap_Render | Weightmap_ResolveToTextureDDC,

	All_WithDDCUpdate = Heightmap_All_WithDDCUpdate | Weightmap_All_WithDDCUpdate,
	All = Heightmap_All | Weightmap_All,
	All_Setup = Heightmap_Setup | Weightmap_Setup,
	All_Render = Heightmap_Render | Weightmap_Render,
};

USTRUCT()
struct FCyLandProceduralLayerBrush
{
	GENERATED_USTRUCT_BODY()

	FCyLandProceduralLayerBrush()
		: BPCustomBrush(nullptr)
	{}

	FCyLandProceduralLayerBrush(ACyLandBlueprintCustomBrush* InBrush)
		: BPCustomBrush(InBrush)
	{}

#if WITH_EDITOR
	UTextureRenderTarget2D* Render(bool InIsHeightmap, UTextureRenderTarget2D* InCombinedResult)
	{
		TGuardValue<bool> AutoRestore(GAllowActorScriptExecutionInEditor, true);
		return BPCustomBrush->Render(InIsHeightmap, InCombinedResult);
	}

	bool IsInitialized() const 
	{
		return BPCustomBrush->IsInitialized();
	}

	void Initialize(const FIntRect& InBoundRect, const FIntPoint& InCyLandRenderTargetSize)
	{
		TGuardValue<bool> AutoRestore(GAllowActorScriptExecutionInEditor, true);
		FIntPoint CyLandSize = InBoundRect.Max - InBoundRect.Min;
		BPCustomBrush->Initialize(CyLandSize, InCyLandRenderTargetSize);
		BPCustomBrush->SetIsInitialized(true);
	}
#endif

	UPROPERTY()
	ACyLandBlueprintCustomBrush* BPCustomBrush;
};

USTRUCT()
struct FCyProceduralLayer
{
	GENERATED_USTRUCT_BODY()

	FCyProceduralLayer()
		: Name(NAME_None)
		, Visible(true)
		, Weight(1.0f)
	{}

	UPROPERTY()
	FName Name;

	UPROPERTY()
	bool Visible;

	UPROPERTY()
	float Weight;

	UPROPERTY()
	TArray<FCyLandProceduralLayerBrush> Brushes;

	UPROPERTY()
	TArray<int8> HeightmapBrushOrderIndices;

	UPROPERTY()
	TArray<int8> WeightmapBrushOrderIndices;
};

UCLASS(MinimalAPI, showcategories=(Display, Movement, Collision, Lighting, LOD, Input), hidecategories=(Mobility))
class ACyLand : public ACyLandProxy
{
	GENERATED_BODY()

public:
	ACyLand(const FObjectInitializer& ObjectInitializer);

	virtual void TickActor(float DeltaTime, ELevelTick TickType, FActorTickFunction& ThisTickFunction) override;

	//~ Begin ACyLandProxy Interface
	CYLAND_API virtual ACyLand* GetCyLandActor() override;
#if WITH_EDITOR
	//~ End ACyLandProxy Interface

	CYLAND_API bool HasAllComponent(); // determine all component is in this actor
	
	// Include Components with overlapped vertices
	// X2/Y2 Coordinates are "inclusive" max values
	CYLAND_API static void CalcComponentIndicesOverlap(const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, const int32 ComponentSizeQuads, 
		int32& ComponentIndexX1, int32& ComponentIndexY1, int32& ComponentIndexX2, int32& ComponentIndexY2);

	// Exclude Components with overlapped vertices
	// X2/Y2 Coordinates are "inclusive" max values
	CYLAND_API static void CalcComponentIndicesNoOverlap(const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, const int32 ComponentSizeQuads,
		int32& ComponentIndexX1, int32& ComponentIndexY1, int32& ComponentIndexX2, int32& ComponentIndexY2);

	static void SplitHeightmap(UCyLandComponent* Comp, bool bMoveToCurrentLevel = false);
	
	//~ Begin UObject Interface.
	virtual void PreSave(const class ITargetPlatform* TargetPlatform) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditMove(bool bFinished) override;
	virtual bool ShouldImport(FString* ActorPropString, bool IsMovingLevel) override;
	virtual void PostEditImport() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
#endif
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	//~ End UObject Interface


#if WITH_EDITOR
	// Procedural stuff
	CYLAND_API void RegenerateProceduralContent();
	CYLAND_API void RegenerateProceduralHeightmaps();
	CYLAND_API void ResolveProceduralHeightmapTexture(bool InUpdateDDC);
	CYLAND_API void RegenerateProceduralWeightmaps();

	CYLAND_API void RequestProceduralContentUpdate(uint32 InDataFlags);

	void GenerateHeightmapQuad(const FIntPoint& InVertexPosition, const float InVertexSize, const FVector2D& InUVStart, const FVector2D& InUVSize, TArray<struct FCyLandProceduralTriangle>& OutTriangles) const;
	void GenerateHeightmapQuadsAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, TArray<struct FCyLandProceduralTriangle>& OutTriangles) const;
	void GenerateHeightmapQuadsAtlasToNonAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InHeightmapReadTextureSize, const FIntPoint& InHeightmapWriteTextureSize, TArray<struct FCyLandProceduralTriangle>& OutTriangles) const;
	void GenerateHeightmapQuadsNonAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InHeightmapReadTextureSize, const FIntPoint& InHeightmapWriteTextureSize, TArray<struct FCyLandProceduralTriangle>& OutTriangles) const;
	void GenerateHeightmapQuadsNonAtlasToAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InHeightmapReadTextureSize, const FIntPoint& InHeightmapWriteTextureSize, TArray<struct FCyLandProceduralTriangle>& OutTriangles) const;
	void GenerateHeightmapQuadsMip(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, int32 CurrentMip, TArray<FCyLandProceduralTriangle>& OutTriangles) const;

	void DrawHeightmapComponentsToRenderTarget(const FString& InDebugName, TArray<UCyLandComponent*>& InComponentsToDraw, UTexture* InHeightmapRTRead, UTextureRenderTarget2D* InOptionalHeightmapRTRead2, UTextureRenderTarget2D* InHeightmapRTWrite, ERTDrawingType InDrawType,
											   bool InClearRTWrite, struct FCyLandHeightmapProceduralShaderParameters& InShaderParams, int32 InMipRender = 0) const;

	void DrawHeightmapComponentsToRenderTargetMips(TArray<UCyLandComponent*>& InComponentsToDraw, UTexture* InReadHeightmap, bool InClearRTWrite, struct FCyLandHeightmapProceduralShaderParameters& InShaderParams) const;

	void CopyProceduralTargetToResolveTarget(UTexture* InHeightmapRTRead, UTexture* InCopyResolveTarget, FTextureResource* InCopyResolveTargetCPUResource, const FIntPoint& InFirstComponentSectionBase, int32 InCurrentMip) const;

	void PrintDebugRTHeightmap(FString Context, UTextureRenderTarget2D* InDebugRT, int32 InMipRender = 0, bool InOutputNormals = false) const;
	void PrintDebugHeightData(const FString& InContext, const TArray<FColor>& InHeightmapData, const FIntPoint& InDataSize, int32 InMipRender, bool InOutputNormals = false) const;

	void OnPreSaveWorld(uint32 SaveFlags, UWorld* World);
	void OnPostSaveWorld(uint32 SaveFlags, UWorld* World, bool bSuccess);
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(TextExportTransient)
	TArray<FCyProceduralLayer> ProceduralLayers;

	UPROPERTY(Transient)
	bool PreviousExperimentalCyLandProcedural;

	UPROPERTY(Transient)
	uint32 ProceduralContentUpdateFlags;

	UPROPERTY(Transient)
	TArray<UTextureRenderTarget2D*> HeightmapRTList;
#endif
};
