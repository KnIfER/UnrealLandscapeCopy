// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformStackWalk.h"
#include "HAL/FileManager.h"
#include "Templates/ScopedPointer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Math/RandomStream.h"
#include "Stats/Stats.h"
#include "Async/AsyncWork.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectIterator.h"
#include "EngineDefines.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "ShowFlags.h"
#include "RHI.h"
#include "RenderingThread.h"
#include "ShaderParameters.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "Shader.h"
#include "CyLandProxy.h"
#include "LightMap.h"
#include "Engine/MapBuildDataRegistry.h"
#include "ShadowMap.h"
#include "CyLandComponent.h"
#include "CyLandVersion.h"
#include "MaterialShaderType.h"
#include "MeshMaterialShaderType.h"
#include "MeshMaterialShader.h"
#include "Materials/Material.h"
#include "Landscape/Classes/LandscapeGrassType.h"
#include "Landscape/Classes/Materials/MaterialExpressionLandscapeGrassOutput.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ContentStreaming.h"
#include "CyLandDataAccess.h"
#include "StaticMeshResources.h"
#include "CyLandLight.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceConstant.h"
#include "ShaderParameterUtils.h"
#include "EngineModule.h"
#include "CyLandRender.h"
#include "MaterialCompiler.h"
#include "Algo/Accumulate.h"
#include "UObject/Package.h"
#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "InstancedStaticMesh.h"
#include "MeshPassProcessor.h"
#include "MeshPassProcessor.inl"

#define LOCTEXT_NAMESPACE "CyLand"

DEFINE_LOG_CATEGORY_STATIC(LogGrass, Log, All);

static TAutoConsoleVariable<float> CVarGuardBandMultiplier(
	TEXT("grass.GuardBandMultiplier"),
	1.3f,
	TEXT("Used to control discarding in the grass system. Approximate range, 1-4. Multiplied by the cull distance to control when we add grass components."));

static TAutoConsoleVariable<float> CVarGuardBandDiscardMultiplier(
	TEXT("grass.GuardBandDiscardMultiplier"),
	1.4f,
	TEXT("Used to control discarding in the grass system. Approximate range, 1-4. Multiplied by the cull distance to control when we discard grass components."));

static TAutoConsoleVariable<int32> CVarMinFramesToKeepGrass(
	TEXT("grass.MinFramesToKeepGrass"),
	30,
	TEXT("Minimum number of frames before cached grass can be discarded; used to prevent thrashing."));

static TAutoConsoleVariable<int32> CVarGrassTickInterval(
	TEXT("grass.TickInterval"),
	1,
	TEXT("Number of frames between grass ticks."));

static TAutoConsoleVariable<float> CVarMinTimeToKeepGrass(
	TEXT("grass.MinTimeToKeepGrass"),
	5.0f,
	TEXT("Minimum number of seconds before cached grass can be discarded; used to prevent thrashing."));

static TAutoConsoleVariable<int32> CVarMaxInstancesPerComponent(
	TEXT("grass.MaxInstancesPerComponent"),
	65536,
	TEXT("Used to control the number of hierarchical components created. More can be more efficient, but can be hitchy as new components come into range"));

static TAutoConsoleVariable<int32> CVarMaxAsyncTasks(
	TEXT("grass.MaxAsyncTasks"),
	4,
	TEXT("Used to control the number of hierarchical components created at a time."));

static TAutoConsoleVariable<int32> CVarUseHaltonDistribution(
	TEXT("grass.UseHaltonDistribution"),
	0,
	TEXT("Used to control the distribution of grass instances. If non-zero, use a halton sequence."));

static TAutoConsoleVariable<float> CVarGrassDensityScale(
	TEXT("grass.densityScale"),
	1,
	TEXT("Multiplier on all grass densities."),
	ECVF_Scalability);

static TAutoConsoleVariable<float> CVarGrassCullDistanceScale(
	TEXT("grass.CullDistanceScale"),
	1,
	TEXT("Multiplier on all grass cull distances."),
	ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarGrassEnable(
	TEXT("grass.Enable"),
	1,
	TEXT("1: Enable Grass; 0: Disable Grass"));

static TAutoConsoleVariable<int32> CVarGrassDiscardDataOnLoad(
	TEXT("grass.DiscardDataOnLoad"),
	0,
	TEXT("1: Discard grass data on load (disables grass); 0: Keep grass data (requires reloading level)"),
	ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarUseStreamingManagerForCameras(
	TEXT("grass.UseStreamingManagerForCameras"),
	1,
	TEXT("1: Use Streaming Manager; 0: Use ViewLocationsRenderedLastFrame"));

static TAutoConsoleVariable<int32> CVarCullSubsections(
	TEXT("grass.CullSubsections"),
	1,
	TEXT("1: Cull each foliage component; 0: Cull only based on the landscape component."));

static TAutoConsoleVariable<int32> CVarDisableGPUCull(
	TEXT("grass.DisableGPUCull"),
	0,
	TEXT("For debugging. Set this to zero to see where the grass is generated. Useful for tweaking the guard bands."));

static TAutoConsoleVariable<int32> CVarPrerenderGrassmaps(
	TEXT("grass.PrerenderGrassmaps"),
	1,
	TEXT("1: Pre-render grass maps for all components in the editor; 0: Generate grass maps on demand while moving through the editor"));

static TAutoConsoleVariable<int32> CVarDisableDynamicShadows(
	TEXT("grass.DisableDynamicShadows"),
	0,
	TEXT("0: Dynamic shadows from grass follow the grass type bCastDynamicShadow flag; 1: Dynamic shadows are disabled for all grass"));

static TAutoConsoleVariable<int32> CVarIgnoreExcludeBoxes(
	TEXT("grass.IgnoreExcludeBoxes"),
	0,
	TEXT("For debugging. Ignores any exclusion boxes."));

DECLARE_CYCLE_STAT(TEXT("Grass Async Build Time"), STAT_FoliageGrassAsyncBuildTime, STATGROUP_Foliage);
DECLARE_CYCLE_STAT(TEXT("Grass Start Comp"), STAT_FoliageGrassStartComp, STATGROUP_Foliage);
DECLARE_CYCLE_STAT(TEXT("Grass End Comp"), STAT_FoliageGrassEndComp, STATGROUP_Foliage);
DECLARE_CYCLE_STAT(TEXT("Grass Destroy Comps"), STAT_FoliageGrassDestoryComp, STATGROUP_Foliage);
DECLARE_CYCLE_STAT(TEXT("Grass Update"), STAT_GrassUpdate, STATGROUP_Foliage);

static int32 GGrassUpdateInterval = 1;

static void GrassCVarSinkFunction()
{
	static float CachedGrassDensityScale = 1.0f;
	float GrassDensityScale = CVarGrassDensityScale.GetValueOnGameThread();

	if (FApp::IsGame())
	{
		GGrassUpdateInterval = FMath::Clamp<int32>(CVarGrassTickInterval.GetValueOnGameThread(), 1, 60);
	}

	static float CachedGrassCullDistanceScale = 1.0f;
	float GrassCullDistanceScale = CVarGrassCullDistanceScale.GetValueOnGameThread();

	static const IConsoleVariable* DetailModeCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DetailMode"));
	static int32 CachedDetailMode = DetailModeCVar ? DetailModeCVar->GetInt() : 0;
	int32 DetailMode = DetailModeCVar ? DetailModeCVar->GetInt() : 0;

	if (DetailMode != CachedDetailMode || 
		GrassDensityScale != CachedGrassDensityScale || 
		GrassCullDistanceScale != CachedGrassCullDistanceScale)
	{
		CachedGrassDensityScale = GrassDensityScale;
		CachedGrassCullDistanceScale = GrassCullDistanceScale;
		CachedDetailMode = DetailMode;

		for (auto* CyLand : TObjectRange<ACyLandProxy>(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::PendingKill))
		{
			CyLand->FlushGrassComponents(nullptr, false);
		}
	}
}

static FAutoConsoleVariableSink CVarGrassSink(FConsoleCommandDelegate::CreateStatic(&GrassCVarSinkFunction));

//
// Grass weightmap rendering
//

#if WITH_EDITOR
static bool ShouldCacheCyLandGrassShaders(EShaderPlatform Platform, const FMaterial* Material, const FVertexFactoryType* VertexFactoryType)
{
	// We only need grass weight shaders for CyLand vertex factories on desktop platforms
	return (Material->IsUsedWithLandscape() || Material->IsSpecialEngineMaterial()) &&
		IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM4) &&
		((VertexFactoryType == FindVertexFactoryType(FName(TEXT("FCyLandVertexFactory"), FNAME_Find))) || (VertexFactoryType == FindVertexFactoryType(FName(TEXT("FCyLandXYOffsetVertexFactory"), FNAME_Find))))
		&& !IsConsolePlatform(Platform);
}

class FCyLandGrassWeightShaderElementData : public FMeshMaterialShaderElementData
{
public:

	int32 OutputPass;
	FVector2D RenderOffset;
};

class FCyLandGrassWeightVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FCyLandGrassWeightVS, MeshMaterial);

	FShaderParameter RenderOffsetParameter;

protected:

	FCyLandGrassWeightVS()
	{}

	FCyLandGrassWeightVS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer)
	: FMeshMaterialShader(Initializer)
	{
		RenderOffsetParameter.Bind(Initializer.ParameterMap, TEXT("RenderOffset"));
		PassUniformBuffer.Bind(Initializer.ParameterMap, FSceneTexturesUniformParameters::StaticStructMetadata.GetShaderVariableName());
	}

public:

	static bool ShouldCompilePermutation(EShaderPlatform Platform, const FMaterial* Material, const FVertexFactoryType* VertexFactoryType)
	{
		return ShouldCacheCyLandGrassShaders(Platform, Material, VertexFactoryType);
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FCyLandGrassWeightShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);

		ShaderBindings.Add(RenderOffsetParameter, ShaderElementData.RenderOffset);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FMeshMaterialShader::Serialize(Ar);
		Ar << RenderOffsetParameter;
		return bShaderHasOutdatedParameters;
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FCyLandGrassWeightVS, TEXT("/Project/Private/LandscapeGrassWeight.usf"), TEXT("VSMain"), SF_Vertex);

class FCyLandGrassWeightPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FCyLandGrassWeightPS, MeshMaterial);
	FShaderParameter OutputPassParameter;
public:

	static bool ShouldCompilePermutation(EShaderPlatform Platform, const FMaterial* Material, const FVertexFactoryType* VertexFactoryType)
	{
		return ShouldCacheCyLandGrassShaders(Platform, Material, VertexFactoryType);
	}

	FCyLandGrassWeightPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	: FMeshMaterialShader(Initializer)
	{
		OutputPassParameter.Bind(Initializer.ParameterMap, TEXT("OutputPass"));
		PassUniformBuffer.Bind(Initializer.ParameterMap, FSceneTexturesUniformParameters::StaticStructMetadata.GetShaderVariableName());
	}

	FCyLandGrassWeightPS()
	{}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FCyLandGrassWeightShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);

		ShaderBindings.Add(OutputPassParameter, ShaderElementData.OutputPass);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FMeshMaterialShader::Serialize(Ar);
		Ar << OutputPassParameter;
		return bShaderHasOutdatedParameters;
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FCyLandGrassWeightPS, TEXT("/Project/Private/LandscapeGrassWeight.usf"), TEXT("PSMain"), SF_Pixel);

class FCyLandGrassWeightMeshProcessor : public FMeshPassProcessor
{
public:
	FCyLandGrassWeightMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext);

	void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch,
		uint64 BatchElementMask,
		int32 NumPasses,
		FVector2D ViewOffset,
		float PassOffsetX,
		int32 FirstHeightMipsPassIndex, 
		const TArray<int32>& HeightMips,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy);

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final
	{
		checkf(false, TEXT("Default AddMeshBatch can't be used as rendering requires extra parameters per pass."));
	}


private:
	void Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		int32 NumPasses,
		FVector2D ViewOffset,
		float PassOffsetX,
		int32 FirstHeightMipsPassIndex,
		const TArray<int32>& HeightMips);

	FMeshPassProcessorRenderState PassDrawRenderState;
};

FCyLandGrassWeightMeshProcessor::FCyLandGrassWeightMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, InViewIfDynamicMeshCommand->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
{
	PassDrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());

	PassDrawRenderState.SetViewUniformBuffer(InViewIfDynamicMeshCommand->ViewUniformBuffer);
	PassDrawRenderState.SetPassUniformBuffer(nullptr);
}

void FCyLandGrassWeightMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch,
	uint64 BatchElementMask,
	int32 NumPasses,
	FVector2D ViewOffset,
	float PassOffsetX,
	int32 FirstHeightMipsPassIndex, 
	const TArray<int32>& HeightMips, 
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy)
{
	// Determine the mesh's material and blend mode.
	const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
	const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);

	const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;

	Process(MeshBatch, BatchElementMask, PrimitiveSceneProxy, MaterialRenderProxy, Material, NumPasses, ViewOffset, PassOffsetX, FirstHeightMipsPassIndex, HeightMips);
}

void FCyLandGrassWeightMeshProcessor::Process(
	const FMeshBatch& MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	int32 NumPasses,
	FVector2D ViewOffset,
	float PassOffsetX,
	int32 FirstHeightMipsPassIndex,
	const TArray<int32>& HeightMips)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	TMeshProcessorShaders<
		FCyLandGrassWeightVS,
		FMeshMaterialShader,
		FMeshMaterialShader,
		FCyLandGrassWeightPS> PassShaders;

	PassShaders.PixelShader = MaterialResource.GetShader<FCyLandGrassWeightPS>(VertexFactory->GetType());
	PassShaders.VertexShader = MaterialResource.GetShader<FCyLandGrassWeightVS>(VertexFactory->GetType());

	const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, MaterialResource);
	const ERasterizerCullMode MeshCullMode = CM_None;

	FCyLandGrassWeightShaderElementData ShaderElementData;
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, -1, true);

	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(PassShaders.VertexShader, PassShaders.PixelShader);

	for (int32 PassIndex = 0; PassIndex < NumPasses; ++PassIndex)
	{
		ShaderElementData.OutputPass = (PassIndex >= FirstHeightMipsPassIndex) ? 0 : PassIndex;
		ShaderElementData.RenderOffset = ViewOffset + FVector2D(PassOffsetX * PassIndex, 0);

		uint64 Mask = (PassIndex >= FirstHeightMipsPassIndex) ? HeightMips[PassIndex - FirstHeightMipsPassIndex] : BatchElementMask;

		BuildMeshDrawCommands(
			MeshBatch,
			Mask,
			PrimitiveSceneProxy,
			MaterialRenderProxy,
			MaterialResource,
			PassDrawRenderState,
			PassShaders,
			MeshFillMode,
			MeshCullMode,
			SortKey,
			EMeshPassFeatures::Default,
			ShaderElementData);
	}
}

// data also accessible by render thread
class FCyLandGrassWeightExporter_RenderThread
{
	FCyLandGrassWeightExporter_RenderThread(int32 InNumGrassMaps, bool InbNeedsHeightmap, TArray<int32> InHeightMips)
		: RenderTargetResource(nullptr)
		, NumPasses(0)
		, HeightMips(MoveTemp(InHeightMips))
		, FirstHeightMipsPassIndex(MAX_int32)
	{
		if (InbNeedsHeightmap || InNumGrassMaps > 0)
		{
			NumPasses += FMath::DivideAndRoundUp(2 /* heightmap */ + InNumGrassMaps, 4);
		}
		if (HeightMips.Num() > 0)
		{
			FirstHeightMipsPassIndex = NumPasses;
			NumPasses += HeightMips.Num();
		}
	}

	friend class FCyLandGrassWeightExporter;

public:
	virtual ~FCyLandGrassWeightExporter_RenderThread()
	{}

	struct FComponentInfo
	{
		UCyLandComponent* Component;
		FVector2D ViewOffset;
		int32 PixelOffsetX;
		FCyLandComponentSceneProxy* SceneProxy;

		FComponentInfo(UCyLandComponent* InComponent, FVector2D& InViewOffset, int32 InPixelOffsetX)
			: Component(InComponent)
			, ViewOffset(InViewOffset)
			, PixelOffsetX(InPixelOffsetX)
			, SceneProxy((FCyLandComponentSceneProxy*)InComponent->SceneProxy)
		{}
	};

	FTextureRenderTarget2DResource* RenderTargetResource;
	TArray<FComponentInfo, TInlineAllocator<1>> ComponentInfos;
	FIntPoint TargetSize;
	int32 NumPasses;
	TArray<int32> HeightMips;
	int32 FirstHeightMipsPassIndex;
	float PassOffsetX;
	FVector ViewOrigin;
	FMatrix ViewRotationMatrix;
	FMatrix ProjectionMatrix;

	void RenderCyLandComponentToTexture_RenderThread(FRHICommandListImmediate& RHICmdList)
	{
		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(RenderTargetResource, NULL, FEngineShowFlags(ESFIM_Game)).SetWorldTimes(FApp::GetCurrentTime() - GStartTime, FApp::GetDeltaTime(), FApp::GetCurrentTime() - GStartTime));

		ViewFamily.LandscapeLODOverride = 0; // Force LOD render

		FSceneViewInitOptions ViewInitOptions;
		ViewInitOptions.SetViewRectangle(FIntRect(0, 0, TargetSize.X, TargetSize.Y));
		ViewInitOptions.ViewOrigin = ViewOrigin;
		ViewInitOptions.ViewRotationMatrix = ViewRotationMatrix;
		ViewInitOptions.ProjectionMatrix = ProjectionMatrix;
		ViewInitOptions.ViewFamily = &ViewFamily;

		GetRendererModule().CreateAndInitSingleView(RHICmdList, &ViewFamily, &ViewInitOptions);
		
		const FSceneView* View = ViewFamily.Views[0];
		RHICmdList.SetViewport(View->UnscaledViewRect.Min.X, View->UnscaledViewRect.Min.Y, 0.0f, View->UnscaledViewRect.Max.X, View->UnscaledViewRect.Max.Y, 1.0f);

		RHICmdList.SetScissorRect(false, 0, 0, 0, 0);

		FMemMark Mark(FMemStack::Get());

		DrawDynamicMeshPass(*View, RHICmdList,
			[View, PassOffsetX = PassOffsetX, &ComponentInfos = ComponentInfos, NumPasses = NumPasses, FirstHeightMipsPassIndex = FirstHeightMipsPassIndex, HeightMips = HeightMips](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
		{
			FCyLandGrassWeightMeshProcessor PassMeshProcessor(
				nullptr,
				View,
				DynamicMeshPassContext);

			const uint64 DefaultBatchElementMask = ~0ul;

			for (auto& ComponentInfo : ComponentInfos)
			{
				const FMeshBatch& Mesh = ComponentInfo.SceneProxy->GetGrassMeshBatch();
				Mesh.MaterialRenderProxy->UpdateUniformExpressionCacheIfNeeded(View->GetFeatureLevel());

				PassMeshProcessor.AddMeshBatch(Mesh, DefaultBatchElementMask, NumPasses, ComponentInfo.ViewOffset, PassOffsetX, FirstHeightMipsPassIndex, HeightMips, ComponentInfo.SceneProxy);
			}
		});
	}
};

class FCyLandGrassWeightExporter : public FCyLandGrassWeightExporter_RenderThread
{
	ACyLandProxy* CyLandProxy;
	int32 ComponentSizeVerts;
	int32 SubsectionSizeQuads;
	int32 NumSubsections;
	TArray<ULandscapeGrassType*> GrassTypes;
	UTextureRenderTarget2D* RenderTargetTexture;

public:
	FCyLandGrassWeightExporter(ACyLandProxy* InCyLandProxy, const TArray<UCyLandComponent*>& InCyLandComponents, TArray<ULandscapeGrassType*> InGrassTypes, bool InbNeedsHeightmap = true, TArray<int32> InHeightMips = {})
		: FCyLandGrassWeightExporter_RenderThread(
			InGrassTypes.Num(),
			InbNeedsHeightmap,
			MoveTemp(InHeightMips))
		, CyLandProxy(InCyLandProxy)
		, ComponentSizeVerts(InCyLandProxy->ComponentSizeQuads + 1)
		, SubsectionSizeQuads(InCyLandProxy->SubsectionSizeQuads)
		, NumSubsections(InCyLandProxy->NumSubsections)
		, GrassTypes(MoveTemp(InGrassTypes))
		, RenderTargetTexture(nullptr)
	{
		check(InCyLandComponents.Num() > 0);

		// todo: use a 2d target?
		TargetSize = FIntPoint(ComponentSizeVerts * NumPasses * InCyLandComponents.Num(), ComponentSizeVerts);
		FIntPoint TargetSizeMinusOne(TargetSize - FIntPoint(1, 1));
		PassOffsetX = 2.0f * (float)ComponentSizeVerts / (float)TargetSize.X;

		for (int32 Idx = 0; Idx < InCyLandComponents.Num(); Idx++)
		{
			UCyLandComponent* Component = InCyLandComponents[Idx];

			FIntPoint ComponentOffset = (Component->GetSectionBase() - CyLandProxy->CyLandSectionOffset);
			int32 PixelOffsetX = Idx * NumPasses * ComponentSizeVerts;

			FVector2D ViewOffset(-ComponentOffset.X, ComponentOffset.Y);
			ViewOffset.X += PixelOffsetX;
			ViewOffset /= (FVector2D(TargetSize) * 0.5f);

			ComponentInfos.Add(FComponentInfo(Component, ViewOffset, PixelOffsetX));
		}

		// center of target area in world
		FVector TargetCenter = CyLandProxy->GetTransform().TransformPosition(FVector(TargetSizeMinusOne, 0.f)*0.5f);

		// extent of target in world space
		FVector TargetExtent = FVector(TargetSize, 0.0f)*CyLandProxy->GetActorScale()*0.5f;

		ViewOrigin = TargetCenter;
		ViewRotationMatrix = FInverseRotationMatrix(CyLandProxy->GetActorRotation());
		ViewRotationMatrix *= FMatrix(FPlane(1, 0, 0, 0),
		                              FPlane(0,-1, 0, 0),
		                              FPlane(0, 0,-1, 0),
		                              FPlane(0, 0, 0, 1));

		const float ZOffset = WORLD_MAX;
		ProjectionMatrix = FReversedZOrthoMatrix(
			TargetExtent.X,
			TargetExtent.Y,
			0.5f / ZOffset,
			ZOffset);

		RenderTargetTexture = NewObject<UTextureRenderTarget2D>();
		check(RenderTargetTexture);
		RenderTargetTexture->ClearColor = FLinearColor::White;
		RenderTargetTexture->TargetGamma = 1.0f;
		RenderTargetTexture->InitCustomFormat(TargetSize.X, TargetSize.Y, PF_B8G8R8A8, false);
		RenderTargetResource = RenderTargetTexture->GameThread_GetRenderTargetResource()->GetTextureRenderTarget2DResource();

		// render
		FCyLandGrassWeightExporter_RenderThread* Exporter = this;
		ENQUEUE_RENDER_COMMAND(FDrawSceneCommand)(
			[Exporter](FRHICommandListImmediate& RHICmdList)
			{
				Exporter->RenderCyLandComponentToTexture_RenderThread(RHICmdList);
				FlushPendingDeleteRHIResources_RenderThread();
			});
	}

	TMap<UCyLandComponent*, TUniquePtr<FCyLandComponentGrassData>, TInlineSetAllocator<1>>
		FetchResults()
	{
		TArray<FColor> Samples;
		Samples.SetNumUninitialized(TargetSize.X*TargetSize.Y);

		// Copy the contents of the remote texture to system memory
		FReadSurfaceDataFlags ReadSurfaceDataFlags;
		ReadSurfaceDataFlags.SetLinearToGamma(false);
		RenderTargetResource->ReadPixels(Samples, ReadSurfaceDataFlags, FIntRect(0, 0, TargetSize.X, TargetSize.Y));

		TMap<UCyLandComponent*, TUniquePtr<FCyLandComponentGrassData>, TInlineSetAllocator<1>> Results;
		Results.Reserve(ComponentInfos.Num());
		for (auto& ComponentInfo : ComponentInfos)
		{
			UCyLandComponent* Component = ComponentInfo.Component;
			ACyLandProxy* Proxy = Component->GetCyLandProxy();

			TUniquePtr<FCyLandComponentGrassData> NewGrassData = MakeUnique<FCyLandComponentGrassData>(Component);

			if (FirstHeightMipsPassIndex > 0)
			{
				NewGrassData->HeightData.Empty(FMath::Square(ComponentSizeVerts));
			}
			else
			{
				NewGrassData->HeightData.Empty(0);
			}
			NewGrassData->HeightMipData.Empty(HeightMips.Num());

			TArray<TArray<uint8>*> GrassWeightArrays;
			GrassWeightArrays.Empty(GrassTypes.Num());
			for (auto GrassType : GrassTypes)
			{
				NewGrassData->WeightData.Add(GrassType);
			}

			// need a second loop because the WeightData map will reallocate its arrays as grass types are added
			for (auto GrassType : GrassTypes)
			{
				TArray<uint8>* DataArray = NewGrassData->WeightData.Find(GrassType);
				check(DataArray);
				DataArray->Empty(FMath::Square(ComponentSizeVerts));
				GrassWeightArrays.Add(DataArray);
			}

			// output debug bitmap
#if UE_BUILD_DEBUG
			static bool bOutputGrassBitmap = false;
			if (bOutputGrassBitmap)
			{
				FString TempPath = FPaths::ScreenShotDir();
				TempPath += TEXT("/GrassDebug");
				IFileManager::Get().MakeDirectory(*TempPath, true);
				FFileHelper::CreateBitmap(*(TempPath / "Grass"), TargetSize.X, TargetSize.Y, Samples.GetData(), nullptr, &IFileManager::Get(), nullptr, GrassTypes.Num() >= 2);
			}
#endif

			for (int32 PassIdx = 0; PassIdx < NumPasses; PassIdx++)
			{
				FColor* SampleData = &Samples[ComponentInfo.PixelOffsetX + PassIdx*ComponentSizeVerts];
				if (PassIdx < FirstHeightMipsPassIndex)
				{
					if (PassIdx == 0)
					{
						for (int32 y = 0; y < ComponentSizeVerts; y++)
						{
							for (int32 x = 0; x < ComponentSizeVerts; x++)
							{
								FColor& Sample = SampleData[x + y * TargetSize.X];
								uint16 Height = (((uint16)Sample.R) << 8) + (uint16)(Sample.G);
								NewGrassData->HeightData.Add(Height);
								if (GrassTypes.Num() > 0)
								{
									GrassWeightArrays[0]->Add(Sample.B);
									if (GrassTypes.Num() > 1)
									{
										GrassWeightArrays[1]->Add(Sample.A);
									}
								}
							}
						}
					}
					else
					{
						for (int32 y = 0; y < ComponentSizeVerts; y++)
						{
							for (int32 x = 0; x < ComponentSizeVerts; x++)
							{
								FColor& Sample = SampleData[x + y * TargetSize.X];

								int32 TypeIdx = PassIdx * 4 - 2;
								GrassWeightArrays[TypeIdx++]->Add(Sample.R);
								if (TypeIdx < GrassTypes.Num())
								{
									GrassWeightArrays[TypeIdx++]->Add(Sample.G);
									if (TypeIdx < GrassTypes.Num())
									{
										GrassWeightArrays[TypeIdx++]->Add(Sample.B);
										if (TypeIdx < GrassTypes.Num())
										{
											GrassWeightArrays[TypeIdx++]->Add(Sample.A);
										}
									}
								}
							}
						}
					}
				}
				else // PassIdx >= FirstHeightMipsPassIndex
				{
					const int32 Mip = HeightMips[PassIdx - FirstHeightMipsPassIndex];
					int32 MipSizeVerts = NumSubsections * (SubsectionSizeQuads >> Mip);
					TArray<uint16>& MipHeightData = NewGrassData->HeightMipData.Add(Mip);
					for (int32 y = 0; y < MipSizeVerts; y++)
					{
						for (int32 x = 0; x < MipSizeVerts; x++)
						{
							FColor& Sample = SampleData[x + y * TargetSize.X];
							uint16 Height = (((uint16)Sample.R) << 8) + (uint16)(Sample.G);
							MipHeightData.Add(Height);
						}
					}
				}
			}

			// remove null grass type if we had one (can occur if the node has null entries)
			NewGrassData->WeightData.Remove(nullptr);

			// Remove any grass data that is entirely weight 0
			for (auto Iter(NewGrassData->WeightData.CreateIterator()); Iter; ++Iter)
			{
				if (Iter->Value.IndexOfByPredicate([&](const int8& Weight) { return Weight != 0; }) == INDEX_NONE)
				{
					Iter.RemoveCurrent();
				}
			}

			Results.Add(Component, MoveTemp(NewGrassData));
		}

		return Results;
	}

	void ApplyResults()
	{
		TMap<UCyLandComponent*, TUniquePtr<FCyLandComponentGrassData>, TInlineSetAllocator<1>> NewGrassData = FetchResults();

		for (auto&& GrassDataPair : NewGrassData)
		{
			UCyLandComponent* Component = GrassDataPair.Key;
			FCyLandComponentGrassData* ComponentGrassData = GrassDataPair.Value.Release();
			ACyLandProxy* Proxy = Component->GetCyLandProxy();

			// Assign the new data (thread-safe)
			Component->GrassData = MakeShareable(ComponentGrassData);

			if (Proxy->bBakeMaterialPositionOffsetIntoCollision)
			{
				Component->UpdateCollisionData(true);
			}
		}
	}

	void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
	{
		if (RenderTargetTexture)
		{
			Collector.AddReferencedObject(RenderTargetTexture);
		}

		if (CyLandProxy)
		{
			Collector.AddReferencedObject(CyLandProxy);
		}

		for (auto& Info : ComponentInfos)
		{
			if (Info.Component)
			{
				Collector.AddReferencedObject(Info.Component);
			}
		}

		for (auto GrassType : GrassTypes)
		{
			if (GrassType)
			{
				Collector.AddReferencedObject(GrassType);
			}
		}
	}
};

FCyLandComponentGrassData::FCyLandComponentGrassData(UCyLandComponent* Component)
	: RotationForWPO(Component->GetCyLandMaterial()->GetMaterial()->WorldPositionOffset.IsConnected() ? Component->GetComponentTransform().GetRotation() : FQuat(0, 0, 0, 0))
{
	UMaterialInterface* Material = Component->GetCyLandMaterial();
	for (UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(Material); MIC; MIC = Cast<UMaterialInstanceConstant>(Material))
	{
		MaterialStateIds.Add(MIC->ParameterStateId);
		Material = MIC->Parent;
	}
	MaterialStateIds.Add(CastChecked<UMaterial>(Material)->StateId);
}

bool UCyLandComponent::MaterialHasGrass() const
{
	UMaterialInterface* Material = GetCyLandMaterial();
	TArray<const UMaterialExpressionLandscapeGrassOutput*> GrassExpressions;
	Material->GetMaterial()->GetAllExpressionsOfType<UMaterialExpressionLandscapeGrassOutput>(GrassExpressions);
	if (GrassExpressions.Num() > 0 &&
		GrassExpressions[0]->GrassTypes.Num() > 0)
	{
		return GrassExpressions[0]->GrassTypes.ContainsByPredicate([](FGrassInput& GrassInput) { return (GrassInput.Input.IsConnected() && GrassInput.GrassType); });
	}

	return false;
}

bool UCyLandComponent::IsGrassMapOutdated() const
{
	if (GrassData->HasData())
	{
		// check material / instances haven't changed
		const auto& MaterialStateIds = GrassData->MaterialStateIds;
		UMaterialInterface* Material = GetCyLandMaterial();
		int32 TestIndex = 0;
		for (UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(Material); MIC; MIC = Cast<UMaterialInstanceConstant>(Material))
		{
			if (!MaterialStateIds.IsValidIndex(TestIndex) || MaterialStateIds[TestIndex] != MIC->ParameterStateId)
			{
				return true;
			}
			Material = MIC->Parent;
			++TestIndex;
		}

		UMaterial* MaterialBase = Cast<UMaterial>(Material);

		// last one should be a UMaterial
		if (TestIndex != MaterialStateIds.Num() - 1 || (MaterialBase != nullptr && MaterialStateIds[TestIndex] != MaterialBase->StateId))
		{
			return true;
		}

		FQuat RotationForWPO = GetCyLandMaterial()->GetMaterial()->WorldPositionOffset.IsConnected() ? GetComponentTransform().GetRotation() : FQuat(0, 0, 0, 0);
		if (GrassData->RotationForWPO != RotationForWPO)
		{
			return true;
		}
	}
	return false;
}

bool UCyLandComponent::CanRenderGrassMap() const
{
	// Check we can render
	UWorld* ComponentWorld = GetWorld();//!GIsEditor ||   || ComponentWorld->IsGameWorld() 
	if (GUsingNullRHI || !ComponentWorld|| ComponentWorld->FeatureLevel < ERHIFeatureLevel::SM4 || !SceneProxy)
	{
		return false;
	}

	UMaterialInstance* MaterialInstance = GetMaterialInstanceCount(false) > 0 ? GetMaterialInstance(0) : nullptr;
	FMaterialResource* MaterialResource = MaterialInstance != nullptr ? MaterialInstance->GetMaterialResource(ComponentWorld->FeatureLevel) : nullptr;

	// Check we can render the material
	if (MaterialResource == nullptr || !MaterialResource->HasValidGameThreadShaderMap())
	{
		return false;
	}

	return true;
}

static bool IsTextureStreamedForGrassMapRender(UTexture2D* InTexture)
{
	if (!InTexture || InTexture->GetNumResidentMips() != InTexture->GetNumMips()
		|| !InTexture->Resource || ((FTexture2DResource*)InTexture->Resource)->GetCurrentFirstMip() > 0)
	{
		return false;
	}
	return true;
}

bool UCyLandComponent::AreTexturesStreamedForGrassMapRender() const
{
	// Check for valid heightmap that is fully streamed in
	if (!IsTextureStreamedForGrassMapRender(HeightmapTexture))
	{
		return false;
	}
	
	// Check for valid weightmaps that is fully streamed in
	for (auto WeightmapTexture : WeightmapTextures)
	{
		if (!IsTextureStreamedForGrassMapRender(WeightmapTexture))
		{
			return false;
		}
	}

	return true;
}

void UCyLandComponent::RenderGrassMap()
{
	UMaterialInterface* Material = GetCyLandMaterial();
	if (CanRenderGrassMap())
	{
		TArray<ULandscapeGrassType*> GrassTypes;

		TArray<const UMaterialExpressionLandscapeGrassOutput*> GrassExpressions;
		Material->GetMaterial()->GetAllExpressionsOfType<UMaterialExpressionLandscapeGrassOutput>(GrassExpressions);
		if (GrassExpressions.Num() > 0)
		{
			GrassTypes.Empty(GrassExpressions[0]->GrassTypes.Num());
			for (auto& GrassTypeInput : GrassExpressions[0]->GrassTypes)
			{
				GrassTypes.Add(GrassTypeInput.GrassType);
			}
		}

		const bool bBakeMaterialPositionOffsetIntoCollision = (GetCyLandProxy() && GetCyLandProxy()->bBakeMaterialPositionOffsetIntoCollision);

		TArray<int32> HeightMips;
		if (bBakeMaterialPositionOffsetIntoCollision)
		{
			if (CollisionMipLevel > 0)
			{
				HeightMips.Add(CollisionMipLevel);
			}
			if (SimpleCollisionMipLevel > CollisionMipLevel)
			{
				HeightMips.Add(SimpleCollisionMipLevel);
			}
		}

		if (GrassTypes.Num() > 0 || bBakeMaterialPositionOffsetIntoCollision)
		{
			TArray<UCyLandComponent*> CyLandComponents;
			CyLandComponents.Add(this);

			FCyLandGrassWeightExporter Exporter(GetCyLandProxy(), MoveTemp(CyLandComponents), MoveTemp(GrassTypes), true, MoveTemp(HeightMips));
			Exporter.ApplyResults();
		}
	}
}

TArray<uint16> UCyLandComponent::RenderWPOHeightmap(int32 LOD)
{
	TArray<uint16> Results;

	if (!CanRenderGrassMap())
	{
		GetMaterialInstance(0)->GetMaterialResource(GetWorld()->FeatureLevel)->FinishCompilation();
	}

	TArray<ULandscapeGrassType*> GrassTypes;
	TArray<UCyLandComponent*> CyLandComponents;
	CyLandComponents.Add(this);

	if (LOD == 0)
	{
		FCyLandGrassWeightExporter Exporter(GetCyLandProxy(), MoveTemp(CyLandComponents), MoveTemp(GrassTypes), true, {});
		TMap<UCyLandComponent*, TUniquePtr<FCyLandComponentGrassData>, TInlineSetAllocator<1>> TempGrassData;
		TempGrassData = Exporter.FetchResults();
		Results = MoveTemp(TempGrassData[this]->HeightData);
	}
	else
	{
		TArray<int32> HeightMips;
		HeightMips.Add(LOD);
		FCyLandGrassWeightExporter Exporter(GetCyLandProxy(), MoveTemp(CyLandComponents), MoveTemp(GrassTypes), false, MoveTemp(HeightMips));
		TMap<UCyLandComponent*, TUniquePtr<FCyLandComponentGrassData>, TInlineSetAllocator<1>> TempGrassData;
		TempGrassData = Exporter.FetchResults();
		Results = MoveTemp(TempGrassData[this]->HeightMipData[LOD]);
	}

	return Results;
}

void UCyLandComponent::RemoveGrassMap()
{
	GrassData = MakeShareable(new FCyLandComponentGrassData());
}

void ACyLandProxy::RenderGrassMaps(const TArray<UCyLandComponent*>& InCyLandComponents, const TArray<ULandscapeGrassType*>& GrassTypes)
{
	TArray<int32> HeightMips;
	if (CollisionMipLevel > 0)
	{
		HeightMips.Add(CollisionMipLevel);
	}
	if (SimpleCollisionMipLevel > CollisionMipLevel)
	{
		HeightMips.Add(SimpleCollisionMipLevel);
	}

	FCyLandGrassWeightExporter Exporter(this, InCyLandComponents, GrassTypes, true, MoveTemp(HeightMips));
	Exporter.ApplyResults();
}

#endif //WITH_EDITOR

// the purpose of this class is to copy the lightmap from the terrain, and set the CoordinateScale and CoordinateBias to zero.
// we re-use the same texture references, so the memory cost is relatively minimal.
class FCyLandGrassLightMap : public FLightMap2D
{
public:
	FCyLandGrassLightMap(const FLightMap2D& InLightMap)
		: FLightMap2D(InLightMap)
	{
		CoordinateScale = FVector2D::ZeroVector;
		CoordinateBias = FVector2D::ZeroVector;
	}
};

// the purpose of this class is to copy the shadowmap from the terrain, and set the CoordinateScale and CoordinateBias to zero.
// we re-use the same texture references, so the memory cost is relatively minimal.
class FCyLandGrassShadowMap : public FShadowMap2D
{
public:
	FCyLandGrassShadowMap(const FShadowMap2D& InShadowMap)
		: FShadowMap2D(InShadowMap)
	{
		CoordinateScale = FVector2D::ZeroVector;
		CoordinateBias = FVector2D::ZeroVector;
	}
};



//
// FCyLandComponentGrassData
//
SIZE_T FCyLandComponentGrassData::GetAllocatedSize() const
{
	SIZE_T WeightSize = 0; 
	for (auto It = WeightData.CreateConstIterator(); It; ++It)
	{
		WeightSize += It.Value().GetAllocatedSize();
	}
	return sizeof(*this)
		+ HeightData.GetAllocatedSize()
		+ WeightData.GetAllocatedSize() + WeightSize;
}

FArchive& operator<<(FArchive& Ar, FCyLandComponentGrassData& Data)
{
	Ar.UsingCustomVersion(FCyLandCustomVersion::GUID);

#if WITH_EDITORONLY_DATA
	if (!Ar.IsFilterEditorOnly())
	{
		if (Ar.CustomVer(FCyLandCustomVersion::GUID) >= FCyLandCustomVersion::GrassMaterialInstanceFix)
		{
			Ar << Data.MaterialStateIds;
		}
		else
		{
			Data.MaterialStateIds.Empty(1);
			if (Ar.UE4Ver() >= VER_UE4_SERIALIZE_LANDSCAPE_GRASS_DATA_MATERIAL_GUID)
			{
				FGuid MaterialStateId;
				Ar << MaterialStateId;
				Data.MaterialStateIds.Add(MaterialStateId);
			}
		}

		if (Ar.CustomVer(FCyLandCustomVersion::GUID) >= FCyLandCustomVersion::GrassMaterialWPO)
		{
			Ar << Data.RotationForWPO;
		}
	}
#endif

	Data.HeightData.BulkSerialize(Ar);

#if WITH_EDITORONLY_DATA
	if (!Ar.IsFilterEditorOnly())
	{
		if (Ar.CustomVer(FCyLandCustomVersion::GUID) >= FCyLandCustomVersion::CollisionMaterialWPO)
		{
			if (Ar.CustomVer(FCyLandCustomVersion::GUID) >= FCyLandCustomVersion::LightmassMaterialWPO)
			{
				// todo - BulkSerialize each mip?
				Ar << Data.HeightMipData;
			}
			else
			{
				checkSlow(Ar.IsLoading());

				TArray<uint16> CollisionHeightData;
				CollisionHeightData.BulkSerialize(Ar);
				if (CollisionHeightData.Num())
				{
					const int32 ComponentSizeQuads = FMath::Sqrt(Data.HeightData.Num()) - 1;
					const int32 CollisionSizeQuads = FMath::Sqrt(CollisionHeightData.Num()) - 1;
					const int32 CollisionMip = FMath::FloorLog2(ComponentSizeQuads / CollisionSizeQuads);
					Data.HeightMipData.Add(CollisionMip, MoveTemp(CollisionHeightData));
				}

				TArray<uint16> SimpleCollisionHeightData;
				SimpleCollisionHeightData.BulkSerialize(Ar);
				if (SimpleCollisionHeightData.Num())
				{
					const int32 ComponentSizeQuads = FMath::Sqrt(Data.HeightData.Num()) - 1;
					const int32 SimpleCollisionSizeQuads = FMath::Sqrt(SimpleCollisionHeightData.Num()) - 1;
					const int32 SimpleCollisionMip = FMath::FloorLog2(ComponentSizeQuads / SimpleCollisionSizeQuads);
					Data.HeightMipData.Add(SimpleCollisionMip, MoveTemp(SimpleCollisionHeightData));
				}
			}
		}
	}
#endif

	// Each weight data array, being 1 byte will be serialized in bulk.
	Ar << Data.WeightData;

	return Ar;
}

void FCyLandComponentGrassData::ConditionalDiscardDataOnLoad()
{
	if (!GIsEditor && CVarGrassDiscardDataOnLoad.GetValueOnAnyThread())
	{
		// Remove data for grass types which have scalability enabled
		for (auto GrassTypeIt = WeightData.CreateIterator(); GrassTypeIt; ++GrassTypeIt)
		{
			if (!GrassTypeIt.Key() || GrassTypeIt.Key()->bEnableDensityScaling)
			{
				GrassTypeIt.RemoveCurrent();
			}
		}

		// If all grass types have been removed, discard the height data too.
		if (WeightData.Num() == 0)
		{
			HeightData.Empty();
			*this = FCyLandComponentGrassData();
		}
	}
}

//
// ACyLandProxy grass-related functions
//

void ACyLandProxy::TickGrass()
{
	if (GGrassUpdateInterval > 1)
	{
		if ((GFrameNumber + FrameOffsetForTickInterval) % uint32(GGrassUpdateInterval))
		{
			return;
		}
	}
	// Update foliage
	static TArray<FVector> OldCameras;
	if (CVarUseStreamingManagerForCameras.GetValueOnGameThread() == 0)
	{
		UWorld* World = GetWorld();
		if (!World)
		{
			return;
		}

		if (!OldCameras.Num() && !World->ViewLocationsRenderedLastFrame.Num())
		{
			// no cameras, no grass update
			return;
		}

		// there is a bug here, which often leaves us with no cameras in the editor
		const TArray<FVector>& Cameras = World->ViewLocationsRenderedLastFrame.Num() ? World->ViewLocationsRenderedLastFrame : OldCameras;

		if (&Cameras != &OldCameras)
		{
			check(IsInGameThread());
			OldCameras = Cameras;
		}
		UpdateGrass(Cameras);
	}
	else
	{
		int32 Num = IStreamingManager::Get().GetNumViews();
		if (!Num)
		{
			// no cameras, no grass update
			return;
		}
		OldCameras.Reset(Num);
		for (int32 Index = 0; Index < Num; Index++)
		{
			auto& ViewInfo = IStreamingManager::Get().GetViewInformation(Index);
			OldCameras.Add(ViewInfo.ViewOrigin);
		}
		UpdateGrass(OldCameras);
	}
}

struct FGrassBuilderBase
{
	bool bHaveValidData;
	float GrassDensity;
	FVector DrawScale;
	FVector DrawLoc;
	FMatrix CyLandToWorld;

	FIntPoint SectionBase;
	FIntPoint CyLandSectionOffset;
	int32 ComponentSizeQuads;
	FVector Origin;
	FVector Extent;
	FVector ComponentOrigin;

	int32 SqrtMaxInstances;

	FGrassBuilderBase(ACyLandProxy* CyLand, UCyLandComponent* Component, const FGrassVariety& GrassVariety, ERHIFeatureLevel::Type FeatureLevel, int32 SqrtSubsections = 1, int32 SubX = 0, int32 SubY = 0, bool bEnableDensityScaling = true)
	{
		bHaveValidData = true;

		const float DensityScale = bEnableDensityScaling ? CVarGrassDensityScale.GetValueOnAnyThread() : 1.0f;
		GrassDensity = GrassVariety.GrassDensity.GetValueForFeatureLevel(FeatureLevel) * DensityScale;

		DrawScale = CyLand->GetRootComponent()->RelativeScale3D;
		DrawLoc = CyLand->GetActorLocation();
		CyLandSectionOffset = CyLand->CyLandSectionOffset;

		SectionBase = Component->GetSectionBase();
		ComponentSizeQuads = Component->ComponentSizeQuads;

		Origin = FVector(DrawScale.X * float(SectionBase.X), DrawScale.Y * float(SectionBase.Y), 0.0f);
		Extent = FVector(DrawScale.X * float(SectionBase.X + ComponentSizeQuads), DrawScale.Y * float(SectionBase.Y + ComponentSizeQuads), 0.0f) - Origin;

		ComponentOrigin = Origin - FVector(DrawScale.X * CyLandSectionOffset.X, DrawScale.Y * CyLandSectionOffset.Y, 0.0f);

		SqrtMaxInstances = FMath::CeilToInt(FMath::Sqrt(FMath::Abs(Extent.X * Extent.Y * GrassDensity / 1000.0f / 1000.0f)));

		if (SqrtMaxInstances == 0)
		{
			bHaveValidData = false;
		}
		const FRotator DrawRot = CyLand->GetActorRotation();
		CyLandToWorld = CyLand->GetRootComponent()->GetComponentTransform().ToMatrixNoScale();

		if (bHaveValidData && SqrtSubsections != 1)
		{
			check(SqrtMaxInstances > 2 * SqrtSubsections);
			SqrtMaxInstances /= SqrtSubsections;
			check(SqrtMaxInstances > 0);

			Extent /= float(SqrtSubsections);
			Origin += Extent * FVector(float(SubX), float(SubY), 0.0f);
		}
	}
};

// FCyLandComponentGrassAccess - accessor wrapper for data for one GrassType from one Component
struct FCyLandComponentGrassAccess
{
	FCyLandComponentGrassAccess(const UCyLandComponent* InComponent, const ULandscapeGrassType* GrassType)
	: GrassData(InComponent->GrassData)
	, HeightData(InComponent->GrassData->HeightData)
	, WeightData(InComponent->GrassData->WeightData.Find(GrassType))
	, Stride(InComponent->ComponentSizeQuads + 1)
	{}

	bool IsValid()
	{
		return WeightData && WeightData->Num() == FMath::Square(Stride) && HeightData.Num() == FMath::Square(Stride);
	}

	FORCEINLINE float GetHeight(int32 IdxX, int32 IdxY)
	{
		return CyLandDataAccess::GetLocalHeight(HeightData[IdxX + Stride*IdxY]);
	}
	FORCEINLINE float GetWeight(int32 IdxX, int32 IdxY)
	{
		return ((float)(*WeightData)[IdxX + Stride*IdxY]) / 255.f;
	}

	FORCEINLINE int32 GetStride()
	{
		return Stride;
	}

private:
	TSharedRef<FCyLandComponentGrassData, ESPMode::ThreadSafe> GrassData;
	TArray<uint16>& HeightData;
	TArray<uint8>* WeightData;
	int32 Stride;
};

template<uint32 Base>
static FORCEINLINE float Halton(uint32 Index)
{
	float Result = 0.0f;
	float InvBase = 1.0f / Base;
	float Fraction = InvBase;
	while( Index > 0 )
	{
		Result += ( Index % Base ) * Fraction;
		Index /= Base;
		Fraction *= InvBase;
	}
	return Result;
}

struct FAsyncGrassBuilder : public FGrassBuilderBase
{
	FCyLandComponentGrassAccess GrassData;
	EGrassScaling Scaling;
	FFloatInterval ScaleX;
	FFloatInterval ScaleY;
	FFloatInterval ScaleZ;
	bool RandomRotation;
	bool RandomScale;
	bool AlignToSurface;
	float PlacementJitter;
	FRandomStream RandomStream;
	FMatrix XForm;
	FBox MeshBox;
	int32 DesiredInstancesPerLeaf;

	double BuildTime;
	int32 TotalInstances;
	uint32 HaltonBaseIndex;

	bool UseCyLandLightmap;
	FVector2D LightmapBaseBias;
	FVector2D LightmapBaseScale;
	FVector2D ShadowmapBaseBias;
	FVector2D ShadowmapBaseScale;
	FVector2D LightMapComponentBias;
	FVector2D LightMapComponentScale;
	bool RequireCPUAccess;

	TArray<FBox> ExcludedBoxes;

	// output
	FStaticMeshInstanceData InstanceBuffer;
	TArray<FClusterNode> ClusterTree;
	int32 OutOcclusionLayerNum;

	FAsyncGrassBuilder(ACyLandProxy* CyLand, UCyLandComponent* Component, const ULandscapeGrassType* GrassType, const FGrassVariety& GrassVariety, ERHIFeatureLevel::Type FeatureLevel, UHierarchicalInstancedStaticMeshComponent* HierarchicalInstancedStaticMeshComponent, int32 SqrtSubsections, int32 SubX, int32 SubY, uint32 InHaltonBaseIndex, TArray<FBox>& InExcludedBoxes)
		: FGrassBuilderBase(CyLand, Component, GrassVariety, FeatureLevel, SqrtSubsections, SubX, SubY, GrassType->bEnableDensityScaling)
		, GrassData(Component, GrassType)
		, Scaling(GrassVariety.Scaling)
		, ScaleX(GrassVariety.ScaleX)
		, ScaleY(GrassVariety.ScaleY)
		, ScaleZ(GrassVariety.ScaleZ)
		, RandomRotation(GrassVariety.RandomRotation)
		, RandomScale(GrassVariety.ScaleX.Size() > 0 || GrassVariety.ScaleY.Size() > 0 || GrassVariety.ScaleZ.Size() > 0)
		, AlignToSurface(GrassVariety.AlignToSurface)
		, PlacementJitter(GrassVariety.PlacementJitter)
		, RandomStream(HierarchicalInstancedStaticMeshComponent->InstancingRandomSeed)
		, XForm(CyLandToWorld * HierarchicalInstancedStaticMeshComponent->GetComponentTransform().ToMatrixWithScale().Inverse())
		, MeshBox(GrassVariety.GrassMesh->GetBounds().GetBox())
		, DesiredInstancesPerLeaf(HierarchicalInstancedStaticMeshComponent->DesiredInstancesPerLeaf())

		, BuildTime(0)
		, TotalInstances(0)
		, HaltonBaseIndex(InHaltonBaseIndex)

		, UseCyLandLightmap(GrassVariety.bUseLandscapeLightmap)
		, LightmapBaseBias(FVector2D::ZeroVector)
		, LightmapBaseScale(FVector2D::UnitVector)
		, ShadowmapBaseBias(FVector2D::ZeroVector)
		, ShadowmapBaseScale(FVector2D::UnitVector)
		, LightMapComponentBias(FVector2D::ZeroVector)
		, LightMapComponentScale(FVector2D::UnitVector)
		, RequireCPUAccess(GrassVariety.bKeepInstanceBufferCPUCopy)

		// output
		, InstanceBuffer(/*bSupportsVertexHalfFloat*/ GVertexElementTypeSupport.IsSupported(VET_Half2))
		, ClusterTree()
		, OutOcclusionLayerNum(0)
	{
		if (InExcludedBoxes.Num())
		{
			FMatrix BoxXForm = HierarchicalInstancedStaticMeshComponent->GetComponentToWorld().ToMatrixWithScale().Inverse() * XForm.Inverse();
			for (const FBox& Box : InExcludedBoxes)
			{
				ExcludedBoxes.Add(Box.TransformBy(BoxXForm));
			}
		}

		bHaveValidData = bHaveValidData && GrassData.IsValid();

		InstanceBuffer.SetAllowCPUAccess(RequireCPUAccess);

		check(DesiredInstancesPerLeaf > 0);

		if (UseCyLandLightmap)
		{
			InitCyLandLightmap(Component);
		}
	}

	void InitCyLandLightmap(UCyLandComponent* Component)
	{
		const int32 SubsectionSizeQuads = Component->SubsectionSizeQuads;
		const int32 NumSubsections = Component->NumSubsections;
		const int32 CyLandComponentSizeQuads = Component->ComponentSizeQuads;
	
		const int32 StaticLightingLOD = Component->GetCyLandProxy()->StaticLightingLOD;
		const int32 ComponentSizeVerts = CyLandComponentSizeQuads + 1;
		const float LightMapRes = Component->StaticLightingResolution > 0.0f ? Component->StaticLightingResolution : Component->GetCyLandProxy()->StaticLightingResolution;
		const int32 LightingLOD = Component->GetCyLandProxy()->StaticLightingLOD;

		// Calculate mapping from landscape to lightmap space for mapping landscape grass to the landscape lightmap
		// Copied from the calculation of FCyLandCyUniformShaderParameters::LandscapeLightmapScaleBias in FCyLandComponentSceneProxy::OnTransformChanged()
		int32 PatchExpandCountX = 0;
		int32 PatchExpandCountY = 0;
		int32 DesiredSize = 1;
		const float LightMapRatio = ::GetTerrainExpandPatchCount(LightMapRes, PatchExpandCountX, PatchExpandCountY, CyLandComponentSizeQuads, (NumSubsections * (SubsectionSizeQuads + 1)), DesiredSize, LightingLOD);
		const float LightmapLODScaleX = LightMapRatio / ((ComponentSizeVerts >> StaticLightingLOD) + 2 * PatchExpandCountX);
		const float LightmapLODScaleY = LightMapRatio / ((ComponentSizeVerts >> StaticLightingLOD) + 2 * PatchExpandCountY);
		const float LightmapBiasX = PatchExpandCountX * LightmapLODScaleX;
		const float LightmapBiasY = PatchExpandCountY * LightmapLODScaleY;
		const float LightmapScaleX = LightmapLODScaleX * (float)((ComponentSizeVerts >> StaticLightingLOD) - 1) / CyLandComponentSizeQuads;
		const float LightmapScaleY = LightmapLODScaleY * (float)((ComponentSizeVerts >> StaticLightingLOD) - 1) / CyLandComponentSizeQuads;

		LightMapComponentScale = FVector2D(LightmapScaleX, LightmapScaleY) / FVector2D(DrawScale);
		LightMapComponentBias = FVector2D(LightmapBiasX, LightmapBiasY);

		const FMeshMapBuildData* MeshMapBuildData = Component->GetMeshMapBuildData();

		if (MeshMapBuildData != nullptr)
		{
			if (MeshMapBuildData->LightMap.IsValid())
			{
				LightmapBaseBias = MeshMapBuildData->LightMap->GetLightMap2D()->GetCoordinateBias();
				LightmapBaseScale = MeshMapBuildData->LightMap->GetLightMap2D()->GetCoordinateScale();
			}

			if (MeshMapBuildData->ShadowMap.IsValid())
			{
				ShadowmapBaseBias = MeshMapBuildData->ShadowMap->GetShadowMap2D()->GetCoordinateBias();
				ShadowmapBaseScale = MeshMapBuildData->ShadowMap->GetShadowMap2D()->GetCoordinateScale();
			}
		}
	}

	void SetInstance(int32 InstanceIndex, const FMatrix& InXForm, float RandomFraction)
	{
		if (UseCyLandLightmap)
		{
			float InstanceX = InXForm.M[3][0];
			float InstanceY = InXForm.M[3][1];

			FVector2D NormalizedGrassCoordinate;
			NormalizedGrassCoordinate.X = (InstanceX - ComponentOrigin.X) * LightMapComponentScale.X + LightMapComponentBias.X;
			NormalizedGrassCoordinate.Y = (InstanceY - ComponentOrigin.Y) * LightMapComponentScale.Y + LightMapComponentBias.Y;

			FVector2D LightMapCoordinate = NormalizedGrassCoordinate * LightmapBaseScale + LightmapBaseBias;
			FVector2D ShadowMapCoordinate = NormalizedGrassCoordinate * ShadowmapBaseScale + ShadowmapBaseBias;

			InstanceBuffer.SetInstance(InstanceIndex, InXForm, RandomStream.GetFraction(), LightMapCoordinate, ShadowMapCoordinate);
		}
		else
		{
			InstanceBuffer.SetInstance(InstanceIndex, InXForm, RandomStream.GetFraction());
		}
	}

	FVector GetRandomScale() const
	{
		FVector Result(1.0f);

		switch (Scaling)
		{
		case EGrassScaling::Uniform:
			Result.X = ScaleX.Interpolate(RandomStream.GetFraction());
			Result.Y = Result.X;
			Result.Z = Result.X;
			break;
		case EGrassScaling::Free:
			Result.X = ScaleX.Interpolate(RandomStream.GetFraction());
			Result.Y = ScaleY.Interpolate(RandomStream.GetFraction());
			Result.Z = ScaleZ.Interpolate(RandomStream.GetFraction());
			break;
		case EGrassScaling::LockXY:
			Result.X = ScaleX.Interpolate(RandomStream.GetFraction());
			Result.Y = Result.X;
			Result.Z = ScaleZ.Interpolate(RandomStream.GetFraction());
			break;
		default:
			check(0);
		}

		return Result;
	}

	bool IsExcluded(const FVector& LocationWithHeight)
	{
		for (const FBox& Box : ExcludedBoxes)
		{
			if (Box.IsInside(LocationWithHeight))
			{
				return true;
			}
		}
		return false;
	}

	void Build()
	{
		SCOPE_CYCLE_COUNTER(STAT_FoliageGrassAsyncBuildTime);
		check(bHaveValidData);
		double StartTime = FPlatformTime::Seconds();

		float Div = 1.0f / float(SqrtMaxInstances);
		TArray<FMatrix> InstanceTransforms;
		if (HaltonBaseIndex)
		{
			if (Extent.X < 0)
			{
				Origin.X += Extent.X;
				Extent.X *= -1.0f;
			}
			if (Extent.Y < 0)
			{
				Origin.Y += Extent.Y;
				Extent.Y *= -1.0f;
			}
			int32 MaxNum = SqrtMaxInstances * SqrtMaxInstances;
			InstanceTransforms.Reserve(MaxNum);
			FVector DivExtent(Extent * Div);
			for (int32 InstanceIndex = 0; InstanceIndex < MaxNum; InstanceIndex++)
			{
				float HaltonX = Halton<2>(InstanceIndex + HaltonBaseIndex);
				float HaltonY = Halton<3>(InstanceIndex + HaltonBaseIndex);
				FVector Location(Origin.X + HaltonX * Extent.X, Origin.Y + HaltonY * Extent.Y, 0.0f);
				FVector LocationWithHeight;
				float Weight = GetLayerWeightAtLocationLocal(Location, &LocationWithHeight);
				bool bKeep = Weight > 0.0f && Weight >= RandomStream.GetFraction() && !IsExcluded(LocationWithHeight);
				if (bKeep)
				{
					const FVector Scale = RandomScale ? GetRandomScale() : FVector(1);
					const float Rot = RandomRotation ? RandomStream.GetFraction() * 360.0f : 0.0f;
					const FMatrix BaseXForm = FScaleRotationTranslationMatrix(Scale, FRotator(0.0f, Rot, 0.0f), FVector::ZeroVector);
					FMatrix OutXForm;
					if (AlignToSurface)
					{
						FVector LocationWithHeightDX;
						FVector LocationDX(Location);
						LocationDX.X = FMath::Clamp<float>(LocationDX.X + (HaltonX < 0.5f ? DivExtent.X : -DivExtent.X), Origin.X, Origin.X + Extent.X);
						GetLayerWeightAtLocationLocal(LocationDX, &LocationWithHeightDX, false);

						FVector LocationWithHeightDY;
						FVector LocationDY(Location);
						LocationDY.Y = FMath::Clamp<float>(LocationDX.Y + (HaltonY < 0.5f ? DivExtent.Y : -DivExtent.Y), Origin.Y, Origin.Y + Extent.Y);
						GetLayerWeightAtLocationLocal(LocationDY, &LocationWithHeightDY, false);

						if (LocationWithHeight != LocationWithHeightDX && LocationWithHeight != LocationWithHeightDY)
						{
							FVector NewZ = ((LocationWithHeight - LocationWithHeightDX) ^ (LocationWithHeight - LocationWithHeightDY)).GetSafeNormal();
							NewZ *= FMath::Sign(NewZ.Z);

							const FVector NewX = (FVector(0, -1, 0) ^ NewZ).GetSafeNormal();
							const FVector NewY = NewZ ^ NewX;

							FMatrix Align = FMatrix(NewX, NewY, NewZ, FVector::ZeroVector);
							OutXForm = (BaseXForm * Align).ConcatTranslation(LocationWithHeight) * XForm;
						}
						else
						{
							OutXForm = BaseXForm.ConcatTranslation(LocationWithHeight) * XForm;
						}
					}
					else
					{
						OutXForm = BaseXForm.ConcatTranslation(LocationWithHeight) * XForm;
					}
					InstanceTransforms.Add(OutXForm);
				}
			}
			if (InstanceTransforms.Num())
			{
				TotalInstances += InstanceTransforms.Num();
				InstanceBuffer.AllocateInstances(InstanceTransforms.Num(), EResizeBufferFlags::AllowSlackOnGrow | EResizeBufferFlags::AllowSlackOnReduce, true);
				for (int32 InstanceIndex = 0; InstanceIndex < InstanceTransforms.Num(); InstanceIndex++)
				{
					const FMatrix& OutXForm = InstanceTransforms[InstanceIndex];
					SetInstance(InstanceIndex, OutXForm, RandomStream.GetFraction());
				}
			}
		}
		else
		{
			int32 NumKept = 0;
			float MaxJitter1D = FMath::Clamp<float>(PlacementJitter, 0.0f, .99f) * Div * .5f;
			FVector MaxJitter(MaxJitter1D, MaxJitter1D, 0.0f);
			MaxJitter *= Extent;
			Origin += Extent * (Div * 0.5f);
			struct FInstanceLocal
			{
				FVector Pos;
				bool bKeep;
			};
			TArray<FInstanceLocal> Instances;
			Instances.AddUninitialized(SqrtMaxInstances * SqrtMaxInstances);
			{
				int32 InstanceIndex = 0;
				for (int32 xStart = 0; xStart < SqrtMaxInstances; xStart++)
				{
					for (int32 yStart = 0; yStart < SqrtMaxInstances; yStart++)
					{
						FVector Location(Origin.X + float(xStart) * Div * Extent.X, Origin.Y + float(yStart) * Div * Extent.Y, 0.0f);

						// NOTE: We evaluate the random numbers on the stack and store them in locals rather than inline within the FVector() constructor below, because 
						// the order of evaluation of function arguments in C++ is unspecified.  We really want this to behave consistently on all sorts of
						// different platforms!
						const float FirstRandom = RandomStream.GetFraction();
						const float SecondRandom = RandomStream.GetFraction();
						Location += FVector(FirstRandom * 2.0f - 1.0f, SecondRandom * 2.0f - 1.0f, 0.0f) * MaxJitter;

						FInstanceLocal& Instance = Instances[InstanceIndex];
						float Weight = GetLayerWeightAtLocationLocal(Location, &Instance.Pos);
						Instance.bKeep = Weight > 0.0f && Weight >= RandomStream.GetFraction() && !IsExcluded(Instance.Pos);
						if (Instance.bKeep)
						{
							NumKept++;
						}
						InstanceIndex++;
					}
				}
			}
			if (NumKept)
			{
				InstanceTransforms.AddUninitialized(NumKept);
				TotalInstances += NumKept;
				{
					InstanceBuffer.AllocateInstances(NumKept, EResizeBufferFlags::AllowSlackOnGrow | EResizeBufferFlags::AllowSlackOnReduce, true);
					int32 InstanceIndex = 0;
					int32 OutInstanceIndex = 0;
					for (int32 xStart = 0; xStart < SqrtMaxInstances; xStart++)
					{
						for (int32 yStart = 0; yStart < SqrtMaxInstances; yStart++)
						{
							const FInstanceLocal& Instance = Instances[InstanceIndex];
							if (Instance.bKeep)
							{
								const FVector Scale = RandomScale ? GetRandomScale() : FVector(1);
								const float Rot = RandomRotation ? RandomStream.GetFraction() * 360.0f : 0.0f;
								const FMatrix BaseXForm = FScaleRotationTranslationMatrix(Scale, FRotator(0.0f, Rot, 0.0f), FVector::ZeroVector);
								FMatrix OutXForm;
								if (AlignToSurface)
								{
									FVector PosX1 = xStart ? Instances[InstanceIndex - SqrtMaxInstances].Pos : Instance.Pos;
									FVector PosX2 = (xStart + 1 < SqrtMaxInstances) ? Instances[InstanceIndex + SqrtMaxInstances].Pos : Instance.Pos;
									FVector PosY1 = yStart ? Instances[InstanceIndex - 1].Pos : Instance.Pos;
									FVector PosY2 = (yStart + 1 < SqrtMaxInstances) ? Instances[InstanceIndex + 1].Pos : Instance.Pos;

									if (PosX1 != PosX2 && PosY1 != PosY2)
									{
										FVector NewZ = ((PosX1 - PosX2) ^ (PosY1 - PosY2)).GetSafeNormal();
										NewZ *= FMath::Sign(NewZ.Z);

										const FVector NewX = (FVector(0, -1, 0) ^ NewZ).GetSafeNormal();
										const FVector NewY = NewZ ^ NewX;

										FMatrix Align = FMatrix(NewX, NewY, NewZ, FVector::ZeroVector);
										OutXForm = (BaseXForm * Align).ConcatTranslation(Instance.Pos) * XForm;
									}
									else
									{
										OutXForm = BaseXForm.ConcatTranslation(Instance.Pos) * XForm;
									}
								}
								else
								{
									OutXForm = BaseXForm.ConcatTranslation(Instance.Pos) * XForm;
								}
								InstanceTransforms[OutInstanceIndex] = OutXForm;
								SetInstance(OutInstanceIndex++, OutXForm, RandomStream.GetFraction());
							}
							InstanceIndex++;
						}
					}
				}
			}
		}

		int32 NumInstances = InstanceTransforms.Num();
		if (NumInstances)
		{
			TArray<int32> SortedInstances;
			TArray<int32> InstanceReorderTable;
			UHierarchicalInstancedStaticMeshComponent::BuildTreeAnyThread(InstanceTransforms, MeshBox, ClusterTree, SortedInstances, InstanceReorderTable, OutOcclusionLayerNum, DesiredInstancesPerLeaf);

			// in-place sort the instances
			
			for (int32 FirstUnfixedIndex = 0; FirstUnfixedIndex < NumInstances; FirstUnfixedIndex++)
			{
				int32 LoadFrom = SortedInstances[FirstUnfixedIndex];
				if (LoadFrom != FirstUnfixedIndex)
				{
					check(LoadFrom > FirstUnfixedIndex);
					InstanceBuffer.SwapInstance(FirstUnfixedIndex, LoadFrom);

					int32 SwapGoesTo = InstanceReorderTable[FirstUnfixedIndex];
					check(SwapGoesTo > FirstUnfixedIndex);
					check(SortedInstances[SwapGoesTo] == FirstUnfixedIndex);
					SortedInstances[SwapGoesTo] = LoadFrom;
					InstanceReorderTable[LoadFrom] = SwapGoesTo;

					InstanceReorderTable[FirstUnfixedIndex] = FirstUnfixedIndex;
					SortedInstances[FirstUnfixedIndex] = FirstUnfixedIndex;
				}
			}
		}
		BuildTime = FPlatformTime::Seconds() - StartTime;
	}
	FORCEINLINE_DEBUGGABLE float GetLayerWeightAtLocationLocal(const FVector& InLocation, FVector* OutLocation, bool bWeight = true)
	{
		// Find location
		float TestX = InLocation.X / DrawScale.X - (float)SectionBase.X;
		float TestY = InLocation.Y / DrawScale.Y - (float)SectionBase.Y;

		// Find data
		int32 X1 = FMath::FloorToInt(TestX);
		int32 Y1 = FMath::FloorToInt(TestY);
		int32 X2 = FMath::CeilToInt(TestX);
		int32 Y2 = FMath::CeilToInt(TestY);

		// Clamp to prevent the sampling of the final columns from overflowing
		int32 IdxX1 = FMath::Clamp<int32>(X1, 0, GrassData.GetStride() - 1);
		int32 IdxY1 = FMath::Clamp<int32>(Y1, 0, GrassData.GetStride() - 1);
		int32 IdxX2 = FMath::Clamp<int32>(X2, 0, GrassData.GetStride() - 1);
		int32 IdxY2 = FMath::Clamp<int32>(Y2, 0, GrassData.GetStride() - 1);

		float LerpX = FMath::Fractional(TestX);
		float LerpY = FMath::Fractional(TestY);

		float Result = 0.0f;
		if (bWeight)
		{
			// sample
			float Sample11 = GrassData.GetWeight(IdxX1, IdxY1);
			float Sample21 = GrassData.GetWeight(IdxX2, IdxY1);
			float Sample12 = GrassData.GetWeight(IdxX1, IdxY2);
			float Sample22 = GrassData.GetWeight(IdxX2, IdxY2);

			// Bilinear interpolate
			Result = FMath::Lerp(
				FMath::Lerp(Sample11, Sample21, LerpX),
				FMath::Lerp(Sample12, Sample22, LerpX),
				LerpY);
		}

		{
			// sample
			float Sample11 = GrassData.GetHeight(IdxX1, IdxY1);
			float Sample21 = GrassData.GetHeight(IdxX2, IdxY1);
			float Sample12 = GrassData.GetHeight(IdxX1, IdxY2);
			float Sample22 = GrassData.GetHeight(IdxX2, IdxY2);

			OutLocation->X = InLocation.X - DrawScale.X * float(CyLandSectionOffset.X);
			OutLocation->Y = InLocation.Y - DrawScale.Y * float(CyLandSectionOffset.Y);
			// Bilinear interpolate
			OutLocation->Z = DrawScale.Z * FMath::Lerp(
				FMath::Lerp(Sample11, Sample21, LerpX),
				FMath::Lerp(Sample12, Sample22, LerpX),
				LerpY);
		}
		return Result;
	}
};

void ACyLandProxy::FlushGrassComponents(const TSet<UCyLandComponent*>* OnlyForComponents, bool bFlushGrassMaps)
{
	if (OnlyForComponents)
	{
		for (FCachedCyLandFoliage::TGrassSet::TIterator Iter(FoliageCache.CachedGrassComps); Iter; ++Iter)
		{
			UCyLandComponent* Component = (*Iter).Key.BasedOn.Get();
			// if the weak pointer in the cache is invalid, we should kill them anyway
			if (Component == nullptr || OnlyForComponents->Contains(Component))
			{
				UHierarchicalInstancedStaticMeshComponent *Used = (*Iter).Foliage.Get();
				if (Used)
				{
					SCOPE_CYCLE_COUNTER(STAT_FoliageGrassDestoryComp);
					Used->ClearInstances();
					Used->DetachFromComponent(FDetachmentTransformRules(EDetachmentRule::KeepRelative, false));
					Used->DestroyComponent();
				}
				Iter.RemoveCurrent();
			}
		}
#if WITH_EDITOR
		if (GIsEditor && bFlushGrassMaps && GetWorld() && GetWorld()->FeatureLevel >= ERHIFeatureLevel::SM4)
		{
			for (UCyLandComponent* Component : *OnlyForComponents)
			{
				Component->RemoveGrassMap();
			}
		}
#endif
	}
	else
	{
		// Clear old foliage component containers
		FoliageComponents.Empty();

		// Might as well clear the cache...
		FoliageCache.ClearCache();
		// Destroy any owned foliage components
		TInlineComponentArray<UHierarchicalInstancedStaticMeshComponent*> FoliageComps;
		GetComponents(FoliageComps);
		for (UHierarchicalInstancedStaticMeshComponent* Component : FoliageComps)
		{
			SCOPE_CYCLE_COUNTER(STAT_FoliageGrassDestoryComp);
			Component->ClearInstances();
			Component->DetachFromComponent(FDetachmentTransformRules(EDetachmentRule::KeepRelative, false));
			Component->DestroyComponent();
		}

		TArray<USceneComponent*> AttachedFoliageComponents = RootComponent->GetAttachChildren().FilterByPredicate(
			[](USceneComponent* Component)
		{
			return Cast<UHierarchicalInstancedStaticMeshComponent>(Component);
		});

		// Destroy any attached but un-owned foliage components
		for (USceneComponent* Component : AttachedFoliageComponents)
		{
			SCOPE_CYCLE_COUNTER(STAT_FoliageGrassDestoryComp);
			CastChecked<UHierarchicalInstancedStaticMeshComponent>(Component)->ClearInstances();
			Component->DetachFromComponent(FDetachmentTransformRules(EDetachmentRule::KeepRelative, false));
			Component->DestroyComponent();
		}

#if WITH_EDITOR
		UWorld* World = GetWorld();

		if (GIsEditor && bFlushGrassMaps && World != nullptr && World->Scene != nullptr && World->Scene->GetFeatureLevel() >= ERHIFeatureLevel::SM4)
		{
			// Clear GrassMaps
			for (UActorComponent* Component : GetComponents())
			{
				if (UCyLandComponent* CyLandComp = Cast<UCyLandComponent>(Component))
				{
					CyLandComp->RemoveGrassMap();
				}
			}
		}
#endif
	}
}

TArray<ULandscapeGrassType*> ACyLandProxy::GetGrassTypes() const
{
	TArray<ULandscapeGrassType*> GrassTypes;
	if (CyLandMaterial)
	{
		TArray<const UMaterialExpressionLandscapeGrassOutput*> GrassExpressions;
		CyLandMaterial->GetMaterial()->GetAllExpressionsOfType<UMaterialExpressionLandscapeGrassOutput>(GrassExpressions);
		if (GrassExpressions.Num() > 0)
		{
			for (auto& Type : GrassExpressions[0]->GrassTypes)
			{
				GrassTypes.Add(Type.GrassType);
			}
		}
	}
	return GrassTypes;
}

static uint32 GGrassExclusionChangeTag = 1;
static uint32 GFrameNumberLastStaleCheck = 0;
static TMap<FWeakObjectPtr, FBox> GGrassExclusionBoxes;

void ACyLandProxy::AddExclusionBox(FWeakObjectPtr Owner, const FBox& BoxToRemove)
{
	GGrassExclusionBoxes.Add(Owner, BoxToRemove);
	GGrassExclusionChangeTag++;
}
void ACyLandProxy::RemoveExclusionBox(FWeakObjectPtr Owner)
{
	GGrassExclusionBoxes.Remove(Owner);
	GGrassExclusionChangeTag++;
}
void ACyLandProxy::RemoveAllExclusionBoxes()
{
	if (GGrassExclusionBoxes.Num())
	{
		GGrassExclusionBoxes.Empty();
		GGrassExclusionChangeTag++;
	}
}


#if WITH_EDITOR
int32 ACyLandProxy::TotalComponentsNeedingGrassMapRender = 0;
int32 ACyLandProxy::TotalTexturesToStreamForVisibleGrassMapRender = 0;
int32 ACyLandProxy::TotalComponentsNeedingTextureBaking = 0;
#endif

void ACyLandProxy::UpdateGrass(const TArray<FVector>& Cameras, bool bForceSync)
{
	SCOPE_CYCLE_COUNTER(STAT_GrassUpdate);
	if (GFrameNumberLastStaleCheck != GFrameNumber && CVarIgnoreExcludeBoxes.GetValueOnAnyThread() == 0)
	{
		GFrameNumberLastStaleCheck = GFrameNumber;
		for (auto Iter = GGrassExclusionBoxes.CreateIterator(); Iter; ++Iter)
		{
			if (!Iter->Key.IsValid())
			{
				Iter.RemoveCurrent();
				GGrassExclusionChangeTag++;
			}
		}
	}

	if (CVarGrassEnable.GetValueOnAnyThread() > 0)
	{
		TArray<ULandscapeGrassType*> GrassTypes = GetGrassTypes();

		float GuardBand = CVarGuardBandMultiplier.GetValueOnAnyThread();
		float DiscardGuardBand = CVarGuardBandDiscardMultiplier.GetValueOnAnyThread();
		bool bCullSubsections = CVarCullSubsections.GetValueOnAnyThread() > 0;
		bool bDisableGPUCull = CVarDisableGPUCull.GetValueOnAnyThread() > 0;
		bool bDisableDynamicShadows = CVarDisableDynamicShadows.GetValueOnAnyThread() > 0;
		int32 MaxInstancesPerComponent = FMath::Max<int32>(1024, CVarMaxInstancesPerComponent.GetValueOnAnyThread());
		int32 MaxTasks = CVarMaxAsyncTasks.GetValueOnAnyThread();
		const float CullDistanceScale = CVarGrassCullDistanceScale.GetValueOnAnyThread();

		UWorld* World = GetWorld();
		if (World)
		{
#if WITH_EDITOR
			int32 RequiredTexturesNotStreamedIn = 0;
			TSet<UCyLandComponent*> ComponentsNeedingGrassMapRender;
			TSet<UTexture2D*> CurrentForcedStreamedTextures;
			TSet<UTexture2D*> DesiredForceStreamedTextures;

			if (true)//!World->IsGameWorld()
			{
				// see if we need to flush grass for any components
				TSet<UCyLandComponent*> FlushComponents;
				for (auto Component : CyLandComponents)
				{
					if (Component != nullptr)
					{
						UTexture2D* Heightmap = Component->GetHeightmap();
						// check textures currently needing force streaming
						if (Heightmap->bForceMiplevelsToBeResident)
						{
							CurrentForcedStreamedTextures.Add(Heightmap);
						}
						for (auto WeightmapTexture : Component->WeightmapTextures)
						{
							if (WeightmapTexture->bForceMiplevelsToBeResident)
							{
								CurrentForcedStreamedTextures.Add(WeightmapTexture);
							}
						}

						if (Component->IsGrassMapOutdated())
						{
							FlushComponents.Add(Component);
						}

						if (GrassTypes.Num() > 0 || bBakeMaterialPositionOffsetIntoCollision)
						{
							if (Component->IsGrassMapOutdated() ||
								!Component->GrassData->HasData())
							{
								ComponentsNeedingGrassMapRender.Add(Component);
							}
						}
					}
				}
				if (FlushComponents.Num())
				{
					FlushGrassComponents(&FlushComponents);
				}
			}
#endif
			ERHIFeatureLevel::Type FeatureLevel = World->Scene->GetFeatureLevel();

			int32 NumCompsCreated = 0;
			for (int32 ComponentIndex = 0; ComponentIndex < CyLandComponents.Num(); ComponentIndex++)
			{

				UCyLandComponent* Component = CyLandComponents[ComponentIndex];

				// skip if we have no data and no way to generate it
				if (Component==nullptr || (World->IsGameWorld() && !Component->GrassData->HasData()))
				{
					continue;
				}

				FScopeCycleCounter Context(Component->GetStatID());

				FBoxSphereBounds WorldBounds = Component->CalcBounds(Component->GetComponentTransform());
				float MinDistanceToComp = Cameras.Num() ? MAX_flt : 0.0f;

				for (auto& Pos : Cameras)
				{
					MinDistanceToComp = FMath::Min<float>(MinDistanceToComp, WorldBounds.ComputeSquaredDistanceFromBoxToPoint(Pos));
				}
				if (Component->ChangeTag != GGrassExclusionChangeTag)
				{
					Component->ActiveExcludedBoxes.Empty();
					if (GGrassExclusionBoxes.Num() && CVarIgnoreExcludeBoxes.GetValueOnAnyThread() == 0)
					{
						FBox WorldBox = WorldBounds.GetBox();

						for (const TPair<FWeakObjectPtr, FBox>& Pair : GGrassExclusionBoxes)
						{
							if (Pair.Value.Intersect(WorldBox))
							{
								Component->ActiveExcludedBoxes.AddUnique(Pair.Value);
							}
						}
					}
					Component->ChangeTag = GGrassExclusionChangeTag;
				}

				MinDistanceToComp = FMath::Sqrt(MinDistanceToComp);

				for (auto GrassType : GrassTypes)
				{
					if (GrassType)
					{
						int32 GrassVarietyIndex = -1;
						uint32 HaltonBaseIndex = 1;
						for (auto& GrassVariety : GrassType->GrassVarieties)
						{
							GrassVarietyIndex++;
							int32 EndCullDistance = GrassVariety.EndCullDistance.GetValueForFeatureLevel(FeatureLevel);
							if (GrassVariety.GrassMesh && GrassVariety.GrassDensity.GetValueForFeatureLevel(FeatureLevel) > 0.0f && EndCullDistance > 0)
							{
								float MustHaveDistance = GuardBand * (float)EndCullDistance * CullDistanceScale;
								float DiscardDistance = DiscardGuardBand * (float)EndCullDistance * CullDistanceScale;

								bool bUseHalton = !GrassVariety.bUseGrid;

								if (!bUseHalton && MinDistanceToComp > DiscardDistance)
								{
									continue;
								}

								FGrassBuilderBase ForSubsectionMath(this, Component, GrassVariety, FeatureLevel);

								int32 SqrtSubsections = 1;

								if (ForSubsectionMath.bHaveValidData && ForSubsectionMath.SqrtMaxInstances > 0)
								{
									SqrtSubsections = FMath::Clamp<int32>(FMath::CeilToInt(float(ForSubsectionMath.SqrtMaxInstances) / FMath::Sqrt((float)MaxInstancesPerComponent)), 1, 16);
								}
								int32 MaxInstancesSub = FMath::Square(ForSubsectionMath.SqrtMaxInstances / SqrtSubsections);

								if (bUseHalton && MinDistanceToComp > DiscardDistance)
								{
									HaltonBaseIndex += MaxInstancesSub * SqrtSubsections * SqrtSubsections;
									continue;
								}

								FBox LocalBox = Component->CachedLocalBox;
								FVector LocalExtentDiv = (LocalBox.Max - LocalBox.Min) * FVector(1.0f / float(SqrtSubsections), 1.0f / float(SqrtSubsections), 1.0f);
								for (int32 SubX = 0; SubX < SqrtSubsections; SubX++)
								{
									for (int32 SubY = 0; SubY < SqrtSubsections; SubY++)
									{
										float MinDistanceToSubComp = MinDistanceToComp;

										FBox WorldSubBox;

										if ((bCullSubsections && SqrtSubsections > 1) || Component->ActiveExcludedBoxes.Num())
										{
											FVector BoxMin;
											BoxMin.X = LocalBox.Min.X + LocalExtentDiv.X * float(SubX);
											BoxMin.Y = LocalBox.Min.Y + LocalExtentDiv.Y * float(SubY);
											BoxMin.Z = LocalBox.Min.Z;

											FVector BoxMax;
											BoxMax.X = LocalBox.Min.X + LocalExtentDiv.X * float(SubX + 1);
											BoxMax.Y = LocalBox.Min.Y + LocalExtentDiv.Y * float(SubY + 1);
											BoxMax.Z = LocalBox.Max.Z;

											FBox LocalSubBox(BoxMin, BoxMax);
											WorldSubBox = LocalSubBox.TransformBy(Component->GetComponentTransform());

											if (bCullSubsections && SqrtSubsections > 1)
											{
												MinDistanceToSubComp = Cameras.Num() ? MAX_flt : 0.0f;
												for (auto& Pos : Cameras)
												{
													MinDistanceToSubComp = FMath::Min<float>(MinDistanceToSubComp, ComputeSquaredDistanceFromBoxToPoint(WorldSubBox.Min, WorldSubBox.Max, Pos));
												}
												MinDistanceToSubComp = FMath::Sqrt(MinDistanceToSubComp);
											}
										}

										if (bUseHalton)
										{
											HaltonBaseIndex += MaxInstancesSub;  // we are going to pre-increment this for all of the continues...however we need to subtract later if we actually do this sub
										}

										if (MinDistanceToSubComp > DiscardDistance)
										{
											continue;
										}

										FCachedCyLandFoliage::FGrassComp NewComp;
										NewComp.Key.BasedOn = Component;
										NewComp.Key.GrassType = GrassType;
										NewComp.Key.SqrtSubsections = SqrtSubsections;
										NewComp.Key.CachedMaxInstancesPerComponent = MaxInstancesPerComponent;
										NewComp.Key.SubsectionX = SubX;
										NewComp.Key.SubsectionY = SubY;
										NewComp.Key.NumVarieties = GrassType->GrassVarieties.Num();
										NewComp.Key.VarietyIndex = GrassVarietyIndex;

										bool bRebuildForBoxes = false;

										{
											FCachedCyLandFoliage::FGrassComp* Existing = FoliageCache.CachedGrassComps.Find(NewComp.Key);
											if (Existing && !Existing->PreviousFoliage.IsValid() && Existing->ExclusionChangeTag != GGrassExclusionChangeTag && !Existing->PendingRemovalRebuild && !Existing->Pending)
											{
												for (const FBox& Box : Component->ActiveExcludedBoxes)
												{
													if (Box.Intersect(WorldSubBox))
													{
														NewComp.ExcludedBoxes.Add(Box);
													}
												}
												if (NewComp.ExcludedBoxes != Existing->ExcludedBoxes)
												{
													bRebuildForBoxes = true;
													NewComp.PreviousFoliage = Existing->Foliage;
													Existing->PendingRemovalRebuild = true;
												}
												else
												{
													Existing->ExclusionChangeTag = GGrassExclusionChangeTag;
												}
											}

											if (Existing || MinDistanceToSubComp > MustHaveDistance)
											{
												if (Existing)
												{
													Existing->Touch();
												}
												if (!bRebuildForBoxes)
												{
													continue;
												}
											}
										}

										if (!bRebuildForBoxes && !bForceSync && (NumCompsCreated || AsyncFoliageTasks.Num() >= MaxTasks))
										{
											continue; // one per frame, but we still want to touch the existing ones and we must do the rebuilds because we changed the tag
										}
										if (!bRebuildForBoxes)
										{
											for (const FBox& Box : Component->ActiveExcludedBoxes)
											{
												if (Box.Intersect(WorldSubBox))
												{
													NewComp.ExcludedBoxes.Add(Box);
												}
											}
										}
										NewComp.ExclusionChangeTag = GGrassExclusionChangeTag;

#if WITH_EDITOR
										// render grass data if we don't have any
										if (!Component->GrassData->HasData())
										{
											if (!Component->CanRenderGrassMap())
											{
												// we can't currently render grassmaps (eg shaders not compiled)
												continue;
											}
											else if (!Component->AreTexturesStreamedForGrassMapRender())
											{
												// we're ready to generate but our textures need streaming in
												DesiredForceStreamedTextures.Add(Component->GetHeightmap());
												for (auto WeightmapTexture : Component->WeightmapTextures)
												{
													DesiredForceStreamedTextures.Add(WeightmapTexture);
												}
												RequiredTexturesNotStreamedIn++;
												continue;
											}

											QUICK_SCOPE_CYCLE_COUNTER(STAT_GrassRenderToTexture);
											Component->RenderGrassMap();
											ComponentsNeedingGrassMapRender.Remove(Component);
										}
#endif

										NumCompsCreated++;

										SCOPE_CYCLE_COUNTER(STAT_FoliageGrassStartComp);

										// To guarantee consistency across platforms, we force the string to be lowercase and always treat it as an ANSI string.
										int32 FolSeed = FCrc::StrCrc32( StringCast<ANSICHAR>( *FString::Printf( TEXT("%s%s%d %d %d"), *GrassType->GetName().ToLower(), *Component->GetName().ToLower(), SubX, SubY, GrassVarietyIndex)).Get() );
										if (FolSeed == 0)
										{
											FolSeed++;
										}

										// Do not record the transaction of creating temp component for visualizations
										ClearFlags(RF_Transactional);
										bool PreviousPackageDirtyFlag = GetOutermost()->IsDirty();

										UHierarchicalInstancedStaticMeshComponent* HierarchicalInstancedStaticMeshComponent;
										{
											QUICK_SCOPE_CYCLE_COUNTER(STAT_GrassCreateComp);
											HierarchicalInstancedStaticMeshComponent = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
										}
										NewComp.Foliage = HierarchicalInstancedStaticMeshComponent;
										FoliageCache.CachedGrassComps.Add(NewComp);

										HierarchicalInstancedStaticMeshComponent->Mobility = EComponentMobility::Static;
										HierarchicalInstancedStaticMeshComponent->SetStaticMesh(GrassVariety.GrassMesh);
										HierarchicalInstancedStaticMeshComponent->MinLOD = GrassVariety.MinLOD;
										HierarchicalInstancedStaticMeshComponent->bSelectable = false;
										HierarchicalInstancedStaticMeshComponent->bHasPerInstanceHitProxies = false;
										HierarchicalInstancedStaticMeshComponent->bReceivesDecals = GrassVariety.bReceivesDecals;
										static FName NoCollision(TEXT("NoCollision"));
										HierarchicalInstancedStaticMeshComponent->SetCollisionProfileName(NoCollision);
										HierarchicalInstancedStaticMeshComponent->bDisableCollision = true;
										HierarchicalInstancedStaticMeshComponent->SetCanEverAffectNavigation(false);
										HierarchicalInstancedStaticMeshComponent->InstancingRandomSeed = FolSeed;
										HierarchicalInstancedStaticMeshComponent->LightingChannels = GrassVariety.LightingChannels;
										HierarchicalInstancedStaticMeshComponent->bCastStaticShadow = false;
										HierarchicalInstancedStaticMeshComponent->CastShadow = GrassVariety.bCastDynamicShadow && !bDisableDynamicShadows;
										HierarchicalInstancedStaticMeshComponent->bCastDynamicShadow = GrassVariety.bCastDynamicShadow && !bDisableDynamicShadows;
										
										const FMeshMapBuildData* MeshMapBuildData = Component->GetMeshMapBuildData();

										if (GrassVariety.bUseLandscapeLightmap
											&& GrassVariety.GrassMesh->GetNumLODs() > 0
											&& MeshMapBuildData
											&& MeshMapBuildData->LightMap)
										{
											HierarchicalInstancedStaticMeshComponent->SetLODDataCount(GrassVariety.GrassMesh->GetNumLODs(), GrassVariety.GrassMesh->GetNumLODs());

											FLightMapRef GrassLightMap = new FCyLandGrassLightMap(*MeshMapBuildData->LightMap->GetLightMap2D());
											FShadowMapRef GrassShadowMap = MeshMapBuildData->ShadowMap ? new FCyLandGrassShadowMap(*MeshMapBuildData->ShadowMap->GetShadowMap2D()) : nullptr;

											for (auto& LOD : HierarchicalInstancedStaticMeshComponent->LODData)
											{
												LOD.OverrideMapBuildData = MakeUnique<FMeshMapBuildData>();
												LOD.OverrideMapBuildData->LightMap = GrassLightMap;
												LOD.OverrideMapBuildData->ShadowMap = GrassShadowMap;
												LOD.OverrideMapBuildData->ResourceCluster = MeshMapBuildData->ResourceCluster;
											}
										}

										if (!Cameras.Num() || bDisableGPUCull)
										{
											// if we don't have any cameras, then we are rendering landscape LOD materials or somesuch and we want to disable culling
											HierarchicalInstancedStaticMeshComponent->InstanceStartCullDistance = 0;
											HierarchicalInstancedStaticMeshComponent->InstanceEndCullDistance = 0;
										}
										else
										{
											HierarchicalInstancedStaticMeshComponent->InstanceStartCullDistance = GrassVariety.StartCullDistance.GetValueForFeatureLevel(FeatureLevel) * CullDistanceScale;
											HierarchicalInstancedStaticMeshComponent->InstanceEndCullDistance = GrassVariety.EndCullDistance.GetValueForFeatureLevel(FeatureLevel) * CullDistanceScale;
										}

										//@todo - take the settings from a UFoliageType object.  For now, disable distance field lighting on grass so we don't hitch.
										HierarchicalInstancedStaticMeshComponent->bAffectDistanceFieldLighting = false;

										{
											QUICK_SCOPE_CYCLE_COUNTER(STAT_GrassAttachComp);

											HierarchicalInstancedStaticMeshComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
											FTransform DesiredTransform = GetRootComponent()->GetComponentTransform();
											DesiredTransform.RemoveScaling();
											HierarchicalInstancedStaticMeshComponent->SetWorldTransform(DesiredTransform);

											FoliageComponents.Add(HierarchicalInstancedStaticMeshComponent);
										}

										FAsyncGrassBuilder* Builder;

										{
											QUICK_SCOPE_CYCLE_COUNTER(STAT_GrassCreateBuilder);

											uint32 HaltonIndexForSub = 0;
											if (bUseHalton)
											{
												check(HaltonBaseIndex > (uint32)MaxInstancesSub);
												HaltonIndexForSub = HaltonBaseIndex - (uint32)MaxInstancesSub;
											}
											Builder = new FAsyncGrassBuilder(this, Component, GrassType, GrassVariety, FeatureLevel, HierarchicalInstancedStaticMeshComponent, SqrtSubsections, SubX, SubY, HaltonIndexForSub, NewComp.ExcludedBoxes);
										}

										if (Builder->bHaveValidData)
										{
											FAsyncTask<FCyAsyncGrassTask>* Task = new FAsyncTask<FCyAsyncGrassTask>(Builder, NewComp.Key, HierarchicalInstancedStaticMeshComponent);

											Task->StartBackgroundTask();

											AsyncFoliageTasks.Add(Task);
										}
										else
										{
											delete Builder;
										}
										{
											QUICK_SCOPE_CYCLE_COUNTER(STAT_GrassRegisterComp);

											HierarchicalInstancedStaticMeshComponent->RegisterComponent();
										}

										SetFlags(RF_Transactional);
										GetOutermost()->SetDirtyFlag(PreviousPackageDirtyFlag);
									}
								}
							}
						}
					}
				}
			}

#if WITH_EDITOR

			TotalTexturesToStreamForVisibleGrassMapRender -= NumTexturesToStreamForVisibleGrassMapRender;
			NumTexturesToStreamForVisibleGrassMapRender = RequiredTexturesNotStreamedIn;
			TotalTexturesToStreamForVisibleGrassMapRender += NumTexturesToStreamForVisibleGrassMapRender;

			{
				int32 NumComponentsRendered = 0;
				int32 NumComponentsUnableToRender = 0;
				if ((GrassTypes.Num() > 0 && CVarPrerenderGrassmaps.GetValueOnAnyThread() > 0) || bBakeMaterialPositionOffsetIntoCollision)
				{
					// try to render some grassmaps
					TArray<UCyLandComponent*> ComponentsToRender;
					for (auto Component : ComponentsNeedingGrassMapRender)
					{
						if (Component->CanRenderGrassMap())
						{
							if (Component->AreTexturesStreamedForGrassMapRender())
							{
								// We really want to throttle the number based on component size.
								if (NumComponentsRendered <= 4)
								{
									ComponentsToRender.Add(Component);
									NumComponentsRendered++;
								}
							}
							else
							if (TotalTexturesToStreamForVisibleGrassMapRender == 0)
							{
								// Force stream in other heightmaps but only if we're not waiting for the textures 
								// near the camera to stream in
								DesiredForceStreamedTextures.Add(Component->GetHeightmap());
								for (auto WeightmapTexture : Component->WeightmapTextures)
								{
									DesiredForceStreamedTextures.Add(WeightmapTexture);
								}
							}
						}
						else
						{
							NumComponentsUnableToRender++;
						}
					}
					if (ComponentsToRender.Num())
					{
						RenderGrassMaps(ComponentsToRender, GrassTypes);
						MarkPackageDirty();
					}
				}

				TotalComponentsNeedingGrassMapRender -= NumComponentsNeedingGrassMapRender;
				NumComponentsNeedingGrassMapRender = ComponentsNeedingGrassMapRender.Num() - NumComponentsRendered - NumComponentsUnableToRender;
				TotalComponentsNeedingGrassMapRender += NumComponentsNeedingGrassMapRender;

				// Update resident flags
				for (auto Texture : DesiredForceStreamedTextures.Difference(CurrentForcedStreamedTextures))
				{
					Texture->bForceMiplevelsToBeResident = true;
				}
				for (auto Texture : CurrentForcedStreamedTextures.Difference(DesiredForceStreamedTextures))
				{
					Texture->bForceMiplevelsToBeResident = false;
				}

			}
#endif
		}
	}

	TSet<UHierarchicalInstancedStaticMeshComponent *> StillUsed;
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_Grass_StillUsed);

		// trim cached items based on time, pending and emptiness
		double OldestToKeepTime = FPlatformTime::Seconds() - CVarMinTimeToKeepGrass.GetValueOnGameThread();
		uint32 OldestToKeepFrame = GFrameNumber - CVarMinFramesToKeepGrass.GetValueOnGameThread() * GGrassUpdateInterval;
		for (FCachedCyLandFoliage::TGrassSet::TIterator Iter(FoliageCache.CachedGrassComps); Iter; ++Iter)
		{
			const FCachedCyLandFoliage::FGrassComp& GrassItem = *Iter;
			UHierarchicalInstancedStaticMeshComponent *Used = GrassItem.Foliage.Get();
			UHierarchicalInstancedStaticMeshComponent *UsedPrev = GrassItem.PreviousFoliage.Get();
			bool bOld =
				!GrassItem.Pending &&
				(
					!GrassItem.Key.BasedOn.Get() ||
					!GrassItem.Key.GrassType.Get() ||
					!Used ||
				(GrassItem.LastUsedFrameNumber < OldestToKeepFrame && GrassItem.LastUsedTime < OldestToKeepTime)
				);
			if (bOld)
			{
				Iter.RemoveCurrent();
			}
			else if (Used || UsedPrev)
			{
				if (!StillUsed.Num())
				{
					StillUsed.Reserve(FoliageCache.CachedGrassComps.Num());
				}
				if (Used)
				{
					StillUsed.Add(Used);
				}
				if (UsedPrev)
				{
					StillUsed.Add(UsedPrev);
				}
			}
		}
	}
	if (StillUsed.Num() < FoliageComponents.Num())
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_Grass_DelComps);

		// delete components that are no longer used
		for (int32 Index = 0; Index < FoliageComponents.Num(); Index++)
		{
			UHierarchicalInstancedStaticMeshComponent* HComponent = FoliageComponents[Index];
			if (!StillUsed.Contains(HComponent))
			{
				{
					SCOPE_CYCLE_COUNTER(STAT_FoliageGrassDestoryComp);
					if (HComponent)
					{
						HComponent->ClearInstances();
						HComponent->DetachFromComponent(FDetachmentTransformRules(EDetachmentRule::KeepRelative, false));
						HComponent->DestroyComponent();
					}
					FoliageComponents.RemoveAtSwap(Index--);
				}
				if (!bForceSync)
				{
					break; // one per frame is fine
				}
			}
		}
	}
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_Grass_FinishAsync);
		// finish async tasks
		for (int32 Index = 0; Index < AsyncFoliageTasks.Num(); Index++)
		{
			FAsyncTask<FCyAsyncGrassTask>* Task = AsyncFoliageTasks[Index];
			if (bForceSync)
			{
				Task->EnsureCompletion();
			}
			if (Task->IsDone())
			{
				SCOPE_CYCLE_COUNTER(STAT_FoliageGrassEndComp);
				FCyAsyncGrassTask& Inner = Task->GetTask();
				AsyncFoliageTasks.RemoveAtSwap(Index--);
				UHierarchicalInstancedStaticMeshComponent* HierarchicalInstancedStaticMeshComponent = Inner.Foliage.Get();
				int32 NumBuiltRenderInstances = Inner.Builder->InstanceBuffer.GetNumInstances();
				//UE_LOG(LogCore, Display, TEXT("%d instances in %4.0fms     %6.0f instances / sec"), NumBuiltRenderInstances, 1000.0f * float(Inner.Builder->BuildTime), float(NumBuiltRenderInstances) / float(Inner.Builder->BuildTime));

				if (HierarchicalInstancedStaticMeshComponent && StillUsed.Contains(HierarchicalInstancedStaticMeshComponent))
				{
					if (NumBuiltRenderInstances > 0)
					{
						QUICK_SCOPE_CYCLE_COUNTER(STAT_FoliageGrassEndComp_AcceptPrebuiltTree);

						if (!HierarchicalInstancedStaticMeshComponent->PerInstanceRenderData.IsValid())
						{
							HierarchicalInstancedStaticMeshComponent->InitPerInstanceRenderData(true, &Inner.Builder->InstanceBuffer, Inner.Builder->RequireCPUAccess);
						}
						else
						{
							HierarchicalInstancedStaticMeshComponent->PerInstanceRenderData->UpdateFromPreallocatedData(Inner.Builder->InstanceBuffer);
						}

						HierarchicalInstancedStaticMeshComponent->AcceptPrebuiltTree(Inner.Builder->ClusterTree, Inner.Builder->OutOcclusionLayerNum, NumBuiltRenderInstances);
						if (bForceSync && GetWorld())
						{
							QUICK_SCOPE_CYCLE_COUNTER(STAT_FoliageGrassEndComp_SyncUpdate);
							HierarchicalInstancedStaticMeshComponent->RecreateRenderState_Concurrent();
						}
					}
				}
				FCachedCyLandFoliage::FGrassComp* Existing = FoliageCache.CachedGrassComps.Find(Inner.Key);
				if (Existing)
				{
					Existing->Pending = false;
					if (Existing->PreviousFoliage.IsValid())
					{
						SCOPE_CYCLE_COUNTER(STAT_FoliageGrassDestoryComp);
						UHierarchicalInstancedStaticMeshComponent* HComponent = Existing->PreviousFoliage.Get();
						if (HComponent)
						{
							HComponent->ClearInstances();
							HComponent->DetachFromComponent(FDetachmentTransformRules(EDetachmentRule::KeepRelative, false));
							HComponent->DestroyComponent();
						}
						FoliageComponents.RemoveSwap(HComponent);
						Existing->PreviousFoliage = nullptr;
					}

					Existing->Touch();
				}
				delete Task;
				if (!bForceSync)
				{
					break; // one per frame is fine
				}
			}
		}
	}
}

FCyAsyncGrassTask::FCyAsyncGrassTask(FAsyncGrassBuilder* InBuilder, const FCachedCyLandFoliage::FGrassCompKey& InKey, UHierarchicalInstancedStaticMeshComponent* InFoliage)
	: Builder(InBuilder)
	, Key(InKey)
	, Foliage(InFoliage)
{
}

void FCyAsyncGrassTask::DoWork()
{
	Builder->Build();
}

FCyAsyncGrassTask::~FCyAsyncGrassTask()
{
	delete Builder;
}

static void FlushGrass(const TArray<FString>& Args)
{
	for (ACyLandProxy* CyLand : TObjectRange<ACyLandProxy>(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::PendingKill))
	{
		CyLand->FlushGrassComponents();
	}
}

static void FlushGrassPIE(const TArray<FString>& Args)
{
	for (ACyLandProxy* CyLand : TObjectRange<ACyLandProxy>(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::PendingKill))
	{
		CyLand->FlushGrassComponents(nullptr, false);
	}
}

static void DumpExclusionBoxes(const TArray<FString>& Args)
{
	for (const TPair<FWeakObjectPtr, FBox>& Pair : GGrassExclusionBoxes)
	{
		UObject* Owner = Pair.Key.Get();
		UE_LOG(LogCore, Warning, TEXT("%f %f %f   %f %f %f   %s"),
			Pair.Value.Min.X,
			Pair.Value.Min.Y,
			Pair.Value.Min.Z,
			Pair.Value.Max.X,
			Pair.Value.Max.Y,
			Pair.Value.Max.Z,
			Owner ? *Owner->GetFullName() : TEXT("[stale]")
		);
	}
}

static FAutoConsoleCommand FlushGrassCmd(
	TEXT("grass.FlushCache"),
	TEXT("Flush the grass cache, debugging."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&FlushGrass)
	);

static FAutoConsoleCommand FlushGrassCmdPIE(
	TEXT("grass.FlushCachePIE"),
	TEXT("Flush the grass cache, debugging."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&FlushGrassPIE)
	);

static FAutoConsoleCommand DumpExclusionBoxesCmd(
	TEXT("grass.DumpExclusionBoxes"),
	TEXT("Print the exclusion boxes, debugging."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&DumpExclusionBoxes)
);


#undef LOCTEXT_NAMESPACE
