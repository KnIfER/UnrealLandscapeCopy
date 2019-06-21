// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
CyLandEditProcedural.cpp: CyLand editing procedural mode
=============================================================================*/

#include "CyLandEdit.h"
#include "CyLand.h"
#include "CyLandProxy.h"
#include "CyLandStreamingProxy.h"
#include "CyLandInfo.h"
#include "CyLandComponent.h"
#include "CyLandLayerInfoObject.h"
#include "CyLandDataAccess.h"
#include "CyLandRender.h"
#include "CyLandRenderMobile.h"

#if WITH_EDITOR
#include "CyLandEditorModule.h"
#include "ComponentRecreateRenderStateContext.h"
#include "ITargetPlatform.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "Settings/EditorExperimentalSettings.h"
#endif

#include "Shader.h"
#include "GlobalShader.h"
#include "RendererInterface.h"
#include "PipelineStateCache.h"
#include "MaterialShaderType.h"
#include "EngineModule.h"
#include "ShaderParameterUtils.h"
#include "CyLandBPCustomBrush.h"

#define LOCTEXT_NAMESPACE "CyLand"

void ACyLandProxy::BeginDestroy()
{
	Super::BeginDestroy();

#if WITH_EDITORONLY_DATA
	if (GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
	{
		for (auto& ItPair : RenderDataPerHeightmap)
		{
			FCyRenderDataPerHeightmap& HeightmapRenderData = ItPair.Value;

			if (HeightmapRenderData.HeightmapsCPUReadBack != nullptr)
			{
				BeginReleaseResource(HeightmapRenderData.HeightmapsCPUReadBack);
			}
		}

		ReleaseResourceFence.BeginFence();
	}
#endif
}

bool ACyLandProxy::IsReadyForFinishDestroy()
{
	bool bReadyForFinishDestroy = Super::IsReadyForFinishDestroy();

#if WITH_EDITORONLY_DATA
	if (GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
	{
		if (bReadyForFinishDestroy)
		{
			bReadyForFinishDestroy = ReleaseResourceFence.IsFenceComplete();
		}
	}
#endif

	return bReadyForFinishDestroy;
}

void ACyLandProxy::FinishDestroy()
{
	Super::FinishDestroy();

#if WITH_EDITORONLY_DATA
	if (GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
	{
		check(ReleaseResourceFence.IsFenceComplete());

		for (auto& ItPair : RenderDataPerHeightmap)
		{
			FCyRenderDataPerHeightmap& HeightmapRenderData = ItPair.Value;

			delete HeightmapRenderData.HeightmapsCPUReadBack;
			HeightmapRenderData.HeightmapsCPUReadBack = nullptr;
		}
	}
#endif
}

#if WITH_EDITOR

static TAutoConsoleVariable<int32> CVarOutputProceduralDebugDrawCallName(
	TEXT("landscape.OutputProceduralDebugDrawCallName"),
	0,
	TEXT("This will output the name of each draw call for Scope Draw call event. This will allow readable draw call info through RenderDoc, for example."));

static TAutoConsoleVariable<int32> CVarOutputProceduralRTContent(
	TEXT("landscape.OutputProceduralRTContent"),
	0,
	TEXT("This will output the content of render target. This is used for debugging only."));

struct FCyLandProceduralVertex
{
	FVector2D Position;
	FVector2D UV;
};

struct FCyLandProceduralTriangle
{
	FCyLandProceduralVertex V0;
	FCyLandProceduralVertex V1;
	FCyLandProceduralVertex V2;
};

/** The filter vertex declaration resource type. */
class FCyLandProceduralVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;

	/** Destructor. */
	virtual ~FCyLandProceduralVertexDeclaration() {}

	virtual void InitRHI()
	{
		FVertexDeclarationElementList Elements;
		uint32 Stride = sizeof(FCyLandProceduralVertex);
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FCyLandProceduralVertex, Position), VET_Float2, 0, Stride));
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FCyLandProceduralVertex, UV), VET_Float2, 1, Stride));
		VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
	}

	virtual void ReleaseRHI()
	{
		VertexDeclarationRHI.SafeRelease();
	}
};


class FCyLandProceduralVertexBuffer : public FVertexBuffer
{
public:
	void Init(const TArray<FCyLandProceduralTriangle>& InTriangleList)
	{
		TriangleList = InTriangleList;
	}

private:

	/** Initialize the RHI for this rendering resource */
	void InitRHI() override
	{
		TResourceArray<FCyLandProceduralVertex, VERTEXBUFFER_ALIGNMENT> Vertices;
		Vertices.SetNumUninitialized(TriangleList.Num() * 3);

		for (int32 i = 0; i < TriangleList.Num(); ++i)
		{
			Vertices[i * 3 + 0] = TriangleList[i].V0;
			Vertices[i * 3 + 1] = TriangleList[i].V1;
			Vertices[i * 3 + 2] = TriangleList[i].V2;
		}
		
		// Create vertex buffer. Fill buffer with initial data upon creation
		FRHIResourceCreateInfo CreateInfo(&Vertices);
		VertexBufferRHI = RHICreateVertexBuffer(Vertices.GetResourceDataSize(), BUF_Static, CreateInfo);
	}

	TArray<FCyLandProceduralTriangle> TriangleList;
};



class FCyLandProceduralVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCyLandProceduralVS)

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4) && !IsConsolePlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
	}

	FCyLandProceduralVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		TransformParam.Bind(Initializer.ParameterMap, TEXT("Transform"), SPF_Mandatory);
	}

	FCyLandProceduralVS()
	{}

	void SetParameters(FRHICommandList& RHICmdList, const FMatrix& InProjectionMatrix)
	{
		SetShaderValue(RHICmdList, GetVertexShader(), TransformParam, InProjectionMatrix);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FShader::Serialize(Ar);
		Ar << TransformParam;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter TransformParam;
};

IMPLEMENT_GLOBAL_SHADER(FCyLandProceduralVS, "/Project/Private/LandscapeProceduralVS.usf", "VSMain", SF_Vertex);

struct FCyLandHeightmapProceduralShaderParameters
{
	FCyLandHeightmapProceduralShaderParameters()
		: ReadHeightmap1(nullptr)
		, ReadHeightmap2(nullptr)
		, HeightmapSize(0, 0)
		, ApplyLayerModifiers(false)
		, LayerWeight(1.0f)
		, LayerVisible(true)
		, OutputAsDelta(false)
		, GenerateNormals(false)
		, GridSize(0.0f, 0.0f, 0.0f)
		, CurrentMipHeightmapSize(0, 0)
		, ParentMipHeightmapSize(0, 0)
		, CurrentMipComponentVertexCount(0)
	{}

	UTexture* ReadHeightmap1;
	UTexture* ReadHeightmap2;
	FIntPoint HeightmapSize;
	bool ApplyLayerModifiers;
	float LayerWeight;
	bool LayerVisible;
	bool OutputAsDelta;
	bool GenerateNormals;
	FVector GridSize;
	FIntPoint CurrentMipHeightmapSize;
	FIntPoint ParentMipHeightmapSize;
	int32 CurrentMipComponentVertexCount;
};

class FCyLandHeightmapProceduralPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCyLandHeightmapProceduralPS);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4) && !IsConsolePlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
	}

	FCyLandHeightmapProceduralPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ReadHeightmapTexture1Param.Bind(Initializer.ParameterMap, TEXT("ReadHeightmapTexture1"));
		ReadHeightmapTexture2Param.Bind(Initializer.ParameterMap, TEXT("ReadHeightmapTexture2"));
		ReadHeightmapTexture1SamplerParam.Bind(Initializer.ParameterMap, TEXT("ReadHeightmapTexture1Sampler"));
		ReadHeightmapTexture2SamplerParam.Bind(Initializer.ParameterMap, TEXT("ReadHeightmapTexture2Sampler"));
		LayerInfoParam.Bind(Initializer.ParameterMap, TEXT("LayerInfo"));
		OutputConfigParam.Bind(Initializer.ParameterMap, TEXT("OutputConfig"));
		TextureSizeParam.Bind(Initializer.ParameterMap, TEXT("HeightmapTextureSize"));
		LandscapeGridScaleParam.Bind(Initializer.ParameterMap, TEXT("LandscapeGridScale"));
		ComponentVertexCountParam.Bind(Initializer.ParameterMap, TEXT("CurrentMipComponentVertexCount"));
	}

	FCyLandHeightmapProceduralPS()
	{}

	void SetParameters(FRHICommandList& RHICmdList, const FCyLandHeightmapProceduralShaderParameters& InParams)
	{
		SetTextureParameter(RHICmdList, GetPixelShader(), ReadHeightmapTexture1Param, ReadHeightmapTexture1SamplerParam, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InParams.ReadHeightmap1->Resource->TextureRHI);

		if (InParams.ReadHeightmap2 != nullptr)
		{
			SetTextureParameter(RHICmdList, GetPixelShader(), ReadHeightmapTexture2Param, ReadHeightmapTexture2SamplerParam, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InParams.ReadHeightmap2->Resource->TextureRHI);
		}

		FVector2D LayerInfo(InParams.LayerWeight, InParams.LayerVisible ? 1.0f : 0.0f);
		FVector4 OutputConfig(InParams.ApplyLayerModifiers ? 1.0f : 0.0f, InParams.OutputAsDelta ? 1.0f : 0.0f, InParams.ReadHeightmap2 ? 1.0f : 0.0f, InParams.GenerateNormals ? 1.0f : 0.0f);
		FVector2D TextureSize(InParams.HeightmapSize.X, InParams.HeightmapSize.Y);

		SetShaderValue(RHICmdList, GetPixelShader(), LayerInfoParam, LayerInfo);
		SetShaderValue(RHICmdList, GetPixelShader(), OutputConfigParam, OutputConfig);
		SetShaderValue(RHICmdList, GetPixelShader(), TextureSizeParam, TextureSize);
		SetShaderValue(RHICmdList, GetPixelShader(), LandscapeGridScaleParam, InParams.GridSize);
		SetShaderValue(RHICmdList, GetPixelShader(), ComponentVertexCountParam, (float)InParams.CurrentMipComponentVertexCount);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FShader::Serialize(Ar);
		Ar << ReadHeightmapTexture1Param;
		Ar << ReadHeightmapTexture2Param;
		Ar << ReadHeightmapTexture1SamplerParam;
		Ar << ReadHeightmapTexture2SamplerParam;
		Ar << LayerInfoParam;
		Ar << OutputConfigParam;
		Ar << TextureSizeParam;
		Ar << LandscapeGridScaleParam;
		Ar << ComponentVertexCountParam;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderResourceParameter ReadHeightmapTexture1Param;
	FShaderResourceParameter ReadHeightmapTexture2Param;
	FShaderResourceParameter ReadHeightmapTexture1SamplerParam;
	FShaderResourceParameter ReadHeightmapTexture2SamplerParam;
	FShaderParameter LayerInfoParam;
	FShaderParameter OutputConfigParam;
	FShaderParameter TextureSizeParam;
	FShaderParameter LandscapeGridScaleParam;
	FShaderParameter ComponentVertexCountParam;
};

IMPLEMENT_GLOBAL_SHADER(FCyLandHeightmapProceduralPS, "/Project/Private/LandscapeProceduralPS.usf", "PSMain", SF_Pixel);

class FCyLandHeightmapMipsProceduralPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCyLandHeightmapMipsProceduralPS);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4) && !IsConsolePlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
	}

	FCyLandHeightmapMipsProceduralPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ReadHeightmapTexture1Param.Bind(Initializer.ParameterMap, TEXT("ReadHeightmapTexture1"));
		ReadHeightmapTexture1SamplerParam.Bind(Initializer.ParameterMap, TEXT("ReadHeightmapTexture1Sampler"));
		CurrentMipHeightmapSizeParam.Bind(Initializer.ParameterMap, TEXT("CurrentMipTextureSize"));
		ParentMipHeightmapSizeParam.Bind(Initializer.ParameterMap, TEXT("ParentMipTextureSize"));
		CurrentMipComponentVertexCountParam.Bind(Initializer.ParameterMap, TEXT("CurrentMipComponentVertexCount"));
	}

	FCyLandHeightmapMipsProceduralPS()
	{}

	void SetParameters(FRHICommandList& RHICmdList, const FCyLandHeightmapProceduralShaderParameters& InParams)
	{
		SetTextureParameter(RHICmdList, GetPixelShader(), ReadHeightmapTexture1Param, ReadHeightmapTexture1SamplerParam, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InParams.ReadHeightmap1->Resource->TextureRHI);

		SetShaderValue(RHICmdList, GetPixelShader(), CurrentMipHeightmapSizeParam, FVector2D(InParams.CurrentMipHeightmapSize.X, InParams.CurrentMipHeightmapSize.Y));
		SetShaderValue(RHICmdList, GetPixelShader(), ParentMipHeightmapSizeParam, FVector2D(InParams.ParentMipHeightmapSize.X, InParams.ParentMipHeightmapSize.Y));
		SetShaderValue(RHICmdList, GetPixelShader(), CurrentMipComponentVertexCountParam, (float)InParams.CurrentMipComponentVertexCount);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FShader::Serialize(Ar);
		Ar << ReadHeightmapTexture1Param;
		Ar << ReadHeightmapTexture1SamplerParam;
		Ar << CurrentMipHeightmapSizeParam;
		Ar << ParentMipHeightmapSizeParam;
		Ar << CurrentMipComponentVertexCountParam;

		return bShaderHasOutdatedParameters;
	}

private:
	FShaderResourceParameter ReadHeightmapTexture1Param;
	FShaderResourceParameter ReadHeightmapTexture1SamplerParam;
	FShaderParameter CurrentMipHeightmapSizeParam;
	FShaderParameter ParentMipHeightmapSizeParam;
	FShaderParameter CurrentMipComponentVertexCountParam;
};

IMPLEMENT_GLOBAL_SHADER(FCyLandHeightmapMipsProceduralPS, "/Project/Private/LandscapeProceduralPS.usf", "PSMainMips", SF_Pixel);

/** The filter vertex declaration resource type. */


DECLARE_GPU_STAT_NAMED(CyLandProceduralRender, TEXT("CyLand Procedural Render"));

class FCyLandProceduralCopyResource_RenderThread
{
public: 
	FCyLandProceduralCopyResource_RenderThread(UTexture* InHeightmapRTRead, UTexture* InCopyResolveTarget, FTextureResource* InCopyResolveTargetCPUResource, FIntPoint InComponentSectionBase, int32 InSubSectionSizeQuad, int32 InNumSubSections, int32 InCurrentMip)
		: SourceResource(InHeightmapRTRead != nullptr ? InHeightmapRTRead->Resource : nullptr)
		, CopyResolveTargetResource(InCopyResolveTarget != nullptr ? InCopyResolveTarget->Resource : nullptr)
		, CopyResolveTargetCPUResource(InCopyResolveTargetCPUResource)
		, CurrentMip(InCurrentMip)
		, ComponentSectionBase(InComponentSectionBase)
		, SubSectionSizeQuad(InSubSectionSizeQuad)
		, NumSubSections(InNumSubSections)
		, SourceDebugName(SourceResource != nullptr ? InHeightmapRTRead->GetName() : TEXT(""))
		, CopyResolveDebugName(InCopyResolveTarget != nullptr ? InCopyResolveTarget->GetName() : TEXT(""))
	{}

	void CopyToResolveTarget(FRHICommandListImmediate& InRHICmdList)
	{
		if (SourceResource == nullptr || CopyResolveTargetResource == nullptr)
		{
			return;
		}

		SCOPE_CYCLE_COUNTER(STAT_CyLandRegenerateProceduralHeightmaps_RenderThread);
		SCOPED_DRAW_EVENTF(InRHICmdList, CyLandProceduralCopy, TEXT("LS Copy %s -> %s, Mip: %d"), *SourceDebugName, *CopyResolveDebugName, CurrentMip);
		SCOPED_GPU_STAT(InRHICmdList, CyLandProceduralRender);

		FIntPoint SourceReadTextureSize(SourceResource->GetSizeX(), SourceResource->GetSizeY());
		FIntPoint CopyResolveWriteTextureSize(CopyResolveTargetResource->GetSizeX() >> CurrentMip, CopyResolveTargetResource->GetSizeY() >> CurrentMip);

		int32 LocalComponentSizeQuad = SubSectionSizeQuad * NumSubSections;
		FVector2D HeightmapPositionOffset(FMath::RoundToInt(ComponentSectionBase.X / LocalComponentSizeQuad), FMath::RoundToInt(ComponentSectionBase.Y / LocalComponentSizeQuad));

		FResolveParams Params;
		Params.SourceArrayIndex = 0;
		Params.DestArrayIndex = CurrentMip;

		if (SourceReadTextureSize.X <= CopyResolveWriteTextureSize.X)
		{
			Params.Rect.X1 = 0;
			Params.Rect.X2 = SourceReadTextureSize.X;
			Params.DestRect.X1 = FMath::RoundToInt(HeightmapPositionOffset.X * (((SubSectionSizeQuad + 1) * NumSubSections) >> CurrentMip));
		}
		else
		{
			Params.Rect.X1 = FMath::RoundToInt(HeightmapPositionOffset.X * (((SubSectionSizeQuad + 1) * NumSubSections) >> CurrentMip));
			Params.Rect.X2 = Params.Rect.X1 + CopyResolveWriteTextureSize.X;
			Params.DestRect.X1 = 0;
		}

		if (SourceReadTextureSize.Y <= CopyResolveWriteTextureSize.Y)
		{
			Params.Rect.Y1 = 0;
			Params.Rect.Y2 = SourceReadTextureSize.Y;
			Params.DestRect.Y1 = FMath::RoundToInt(HeightmapPositionOffset.Y * (((SubSectionSizeQuad + 1) * NumSubSections) >> CurrentMip));
		}
		else
		{
			Params.Rect.Y1 = FMath::RoundToInt(HeightmapPositionOffset.Y * (((SubSectionSizeQuad + 1) * NumSubSections) >> CurrentMip));
			Params.Rect.Y2 = Params.Rect.Y1 + CopyResolveWriteTextureSize.Y;
			Params.DestRect.Y1 = 0;
		}

		InRHICmdList.CopyToResolveTarget(SourceResource->TextureRHI, CopyResolveTargetResource->TextureRHI, Params);

		if (CopyResolveTargetCPUResource != nullptr)
		{
			InRHICmdList.CopyToResolveTarget(SourceResource->TextureRHI, CopyResolveTargetCPUResource->TextureRHI, Params);
		}
	}

private:
	FTextureResource* SourceResource;
	FTextureResource* CopyResolveTargetResource;
	FTextureResource* CopyResolveTargetCPUResource;
	FIntPoint ReadRenderTargetSize;
	int32 CurrentMip;
	FIntPoint ComponentSectionBase;
	int32 SubSectionSizeQuad;
	int32 NumSubSections;
	FString SourceDebugName;
	FString CopyResolveDebugName;
};

class FCyLandHeightmapProceduralRender_RenderThread
{
public:

	FCyLandHeightmapProceduralRender_RenderThread(const FString& InDebugName, UTextureRenderTarget2D* InWriteRenderTarget, const FIntPoint& InWriteRenderTargetSize, const FIntPoint& InReadRenderTargetSize, const FMatrix& InProjectionMatrix, 
													 const FCyLandHeightmapProceduralShaderParameters& InShaderParams, int32 InCurrentMip, const TArray<FCyLandProceduralTriangle>& InTriangleList)
		: RenderTargetResource(InWriteRenderTarget->GameThread_GetRenderTargetResource())
		, WriteRenderTargetSize(InWriteRenderTargetSize)
		, ReadRenderTargetSize(InReadRenderTargetSize)
		, ProjectionMatrix(InProjectionMatrix)
		, ShaderParams(InShaderParams)
		, PrimitiveCount(InTriangleList.Num())
		, DebugName(InDebugName)
		, CurrentMip(InCurrentMip)
	{
		VertexBufferResource.Init(InTriangleList);
	}

	virtual ~FCyLandHeightmapProceduralRender_RenderThread()
	{}

	void Render(FRHICommandListImmediate& InRHICmdList, bool InClearRT)
	{
		SCOPE_CYCLE_COUNTER(STAT_CyLandRegenerateProceduralHeightmaps_RenderThread);
		SCOPED_DRAW_EVENTF(InRHICmdList, CyLandProceduralHeightmapRender, TEXT("%s"), DebugName.Len() > 0 ? *DebugName : TEXT("CyLandProceduralHeightmapRender"));
		SCOPED_GPU_STAT(InRHICmdList, CyLandProceduralRender);
		INC_DWORD_STAT(STAT_CyLandRegenerateProceduralHeightmapsDrawCalls);

		check(IsInRenderingThread());

		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(RenderTargetResource, NULL, FEngineShowFlags(ESFIM_Game)).SetWorldTimes(FApp::GetCurrentTime() - GStartTime, FApp::GetDeltaTime(), FApp::GetCurrentTime() - GStartTime));

		FSceneViewInitOptions ViewInitOptions;
		ViewInitOptions.SetViewRectangle(FIntRect(0, 0, WriteRenderTargetSize.X, WriteRenderTargetSize.Y));
		ViewInitOptions.ViewOrigin = FVector::ZeroVector;
		ViewInitOptions.ViewRotationMatrix = FMatrix::Identity;
		ViewInitOptions.ProjectionMatrix = ProjectionMatrix;
		ViewInitOptions.ViewFamily = &ViewFamily;
		ViewInitOptions.BackgroundColor = FLinearColor::Black;
		ViewInitOptions.OverlayColor = FLinearColor::White;

		// Create and add the new view
		FSceneView* View = new FSceneView(ViewInitOptions);
		ViewFamily.Views.Add(View);

		// Init VB/IB Resource
		VertexDeclaration.InitResource();
		VertexBufferResource.InitResource();

		// Setup Pipeline
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = VertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		//RT0ColorWriteMask, RT0ColorBlendOp, RT0ColorSrcBlend, RT0ColorDestBlend, RT0AlphaBlendOp, RT0AlphaSrcBlend, RT0AlphaDestBlend,
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		FRHIRenderPassInfo RenderPassInfo(ViewFamily.RenderTarget->GetRenderTargetTexture(), CurrentMip == 0 ? ERenderTargetActions::Clear_Store : ERenderTargetActions::Load_Store, nullptr, 0, 0);
		InRHICmdList.BeginRenderPass(RenderPassInfo, TEXT("DrawProceduralHeightmaps"));

		if (CurrentMip == 0)
		{
			// Setup Shaders
			TShaderMapRef<FCyLandProceduralVS> VertexShader(GetGlobalShaderMap(View->GetFeatureLevel()));
			TShaderMapRef<FCyLandHeightmapProceduralPS> PixelShader(GetGlobalShaderMap(View->GetFeatureLevel()));

			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);

			InRHICmdList.SetViewport(View->UnscaledViewRect.Min.X, View->UnscaledViewRect.Min.Y, 0.0f, View->UnscaledViewRect.Max.X, View->UnscaledViewRect.Max.Y, 1.0f);

			InRHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			SetGraphicsPipelineState(InRHICmdList, GraphicsPSOInit);

			// Set shader params
			VertexShader->SetParameters(InRHICmdList, ProjectionMatrix);
			PixelShader->SetParameters(InRHICmdList, ShaderParams);
		}
		else
		{
			// Setup Shaders
			TShaderMapRef<FCyLandProceduralVS> VertexShader(GetGlobalShaderMap(View->GetFeatureLevel()));
			TShaderMapRef<FCyLandHeightmapMipsProceduralPS> PixelShader(GetGlobalShaderMap(View->GetFeatureLevel()));

			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);

			InRHICmdList.SetViewport(0.0f, 0.0f, 0.0f, WriteRenderTargetSize.X, WriteRenderTargetSize.Y, 1.0f);

			InRHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			SetGraphicsPipelineState(InRHICmdList, GraphicsPSOInit);

			// Set shader params
			VertexShader->SetParameters(InRHICmdList, ProjectionMatrix);
			PixelShader->SetParameters(InRHICmdList, ShaderParams);			
		}

		InRHICmdList.SetStencilRef(0);
		InRHICmdList.SetScissorRect(false, 0, 0, 0, 0);
		InRHICmdList.SetStreamSource(0, VertexBufferResource.VertexBufferRHI, 0);

		InRHICmdList.DrawPrimitive(0, PrimitiveCount, 1);

		InRHICmdList.EndRenderPass();

		VertexDeclaration.ReleaseResource();
		VertexBufferResource.ReleaseResource();
	}

private:
	FTextureRenderTargetResource* RenderTargetResource;
	FIntPoint WriteRenderTargetSize;
	FIntPoint ReadRenderTargetSize;
	FMatrix ProjectionMatrix;
	FCyLandHeightmapProceduralShaderParameters ShaderParams;
	FCyLandProceduralVertexBuffer VertexBufferResource;
	int32 PrimitiveCount;
	FCyLandProceduralVertexDeclaration VertexDeclaration;
	FString DebugName;
	int32 CurrentMip;
};

void ACyLandProxy::SetupProceduralLayers(int32 InNumComponentsX, int32 InNumComponentsY)
{
	ACyLand* CyLand = GetCyLandActor();
	check(CyLand);

	UCyLandInfo* Info = GetCyLandInfo();

	if (Info == nullptr)
	{
		return;
	}

	TArray<ACyLandProxy*> AllCyLands;
	AllCyLands.Add(CyLand);

	for (auto& It : Info->Proxies)
	{
		AllCyLands.Add(It);
	}

	// TEMP STUFF START
	bool Layer1Exist = false;
	FCyProceduralLayer Layer1;
	Layer1.Name = TEXT("Layer1");

	bool Layer2Exist = false;
	FCyProceduralLayer Layer2;
	Layer2.Name = TEXT("Layer2");	

	for (int32 i = 0; i < CyLand->ProceduralLayers.Num(); ++i)
	{
		if (CyLand->ProceduralLayers[i].Name == Layer1.Name)
		{
			Layer1Exist = true;				
		}

		if (CyLand->ProceduralLayers[i].Name == Layer2.Name)
		{
			Layer2Exist = true;
		}
	}

	if (!Layer1Exist)
	{
		CyLand->ProceduralLayers.Add(Layer1);

		for (ACyLandProxy* CyLandProxy : AllCyLands)
		{
			CyLandProxy->ProceduralLayersData.Add(Layer1.Name, FCyProceduralLayerData());
		}
	}

	if (!Layer2Exist)
	{
		CyLand->ProceduralLayers.Add(Layer2);

		for (ACyLandProxy* CyLandProxy : AllCyLands)
		{
			CyLandProxy->ProceduralLayersData.Add(Layer2.Name, FCyProceduralLayerData());
		}
	}	
	///// TEMP STUFF END

	int32 NumComponentsX = InNumComponentsX;
	int32 NumComponentsY = InNumComponentsY;
	bool GenerateComponentCounts = NumComponentsX == INDEX_NONE || NumComponentsY == INDEX_NONE;
	FIntPoint MaxSectionBase(0, 0);

	uint32 UpdateFlags = 0;

	// Setup all Heightmap data
	for (ACyLandProxy* CyLandProxy : AllCyLands)
	{
		for (UCyLandComponent* Component : CyLandProxy->CyLandComponents)
		{
			UTexture2D* ComponentHeightmapTexture = Component->GetHeightmap();

			FCyRenderDataPerHeightmap* Data = CyLandProxy->RenderDataPerHeightmap.Find(ComponentHeightmapTexture);

			if (Data == nullptr)
			{
				FCyRenderDataPerHeightmap NewData;
				NewData.Components.Add(Component);
				NewData.OriginalHeightmap = ComponentHeightmapTexture;
				NewData.HeightmapsCPUReadBack = new FCyLandProceduralTexture2DCPUReadBackResource(ComponentHeightmapTexture->Source.GetSizeX(), ComponentHeightmapTexture->Source.GetSizeY(), ComponentHeightmapTexture->GetPixelFormat(), ComponentHeightmapTexture->Source.GetNumMips());
				BeginInitResource(NewData.HeightmapsCPUReadBack);

				CyLandProxy->RenderDataPerHeightmap.Add(ComponentHeightmapTexture, NewData);
			}
			else
			{
				Data->Components.AddUnique(Component);
			}

			if (GenerateComponentCounts)
			{
				MaxSectionBase.X = FMath::Max(MaxSectionBase.X, Component->SectionBaseX);
				MaxSectionBase.Y = FMath::Max(MaxSectionBase.Y, Component->SectionBaseY);
			}
		}
	}

	if (GenerateComponentCounts)
	{
		NumComponentsX = (MaxSectionBase.X / ComponentSizeQuads) + 1;
		NumComponentsY = (MaxSectionBase.Y / ComponentSizeQuads) + 1;
	}

	const int32 TotalVertexCountX = (SubsectionSizeQuads * NumSubsections) * NumComponentsX + 1;
	const int32 TotalVertexCountY = (SubsectionSizeQuads * NumSubsections) * NumComponentsY + 1;

	if (CyLand->HeightmapRTList.Num() == 0)
	{
		CyLand->HeightmapRTList.Init(nullptr, EHeightmapRTType::Count);

		int32 CurrentMipSizeX = ((SubsectionSizeQuads + 1) * NumSubsections) * NumComponentsX;
		int32 CurrentMipSizeY = ((SubsectionSizeQuads + 1) * NumSubsections) * NumComponentsY;

		for (int32 i = 0; i < EHeightmapRTType::Count; ++i)
		{
			CyLand->HeightmapRTList[i] = NewObject<UTextureRenderTarget2D>(CyLand->GetOutermost());
			check(CyLand->HeightmapRTList[i]);
			CyLand->HeightmapRTList[i]->RenderTargetFormat = RTF_RGBA8;
			CyLand->HeightmapRTList[i]->AddressX = TextureAddress::TA_Clamp;
			CyLand->HeightmapRTList[i]->AddressY = TextureAddress::TA_Clamp;

			if (i < CyLandSizeMip1) // CyLand size RT
			{
				CyLand->HeightmapRTList[i]->InitAutoFormat(FMath::RoundUpToPowerOfTwo(TotalVertexCountX), FMath::RoundUpToPowerOfTwo(TotalVertexCountY));
			}
			else // Mips
			{
				CurrentMipSizeX >>= 1;
				CurrentMipSizeY >>= 1;
				CyLand->HeightmapRTList[i]->InitAutoFormat(FMath::RoundUpToPowerOfTwo(CurrentMipSizeX), FMath::RoundUpToPowerOfTwo(CurrentMipSizeY));
			}

			CyLand->HeightmapRTList[i]->UpdateResourceImmediate(true);

			// Only generate required mips RT
			if (CurrentMipSizeX == NumComponentsX && CurrentMipSizeY == NumComponentsY)
			{
				break;
			}
		}
	}

	TArray<FVector> VertexNormals;
	TArray<uint16> EmptyHeightmapData;

	UpdateFlags |= EProceduralContentUpdateFlag::Heightmap_Render;

	// Setup all Heightmap data
	for (ACyLandProxy* CyLandProxy : AllCyLands)
	{
		for (auto& ItPair : CyLandProxy->RenderDataPerHeightmap)
		{
			FCyRenderDataPerHeightmap& HeightmapRenderData = ItPair.Value;
			HeightmapRenderData.TopLeftSectionBase = FIntPoint(TotalVertexCountX, TotalVertexCountY);

			for (UCyLandComponent* Component : HeightmapRenderData.Components)
			{
				HeightmapRenderData.TopLeftSectionBase.X = FMath::Min(HeightmapRenderData.TopLeftSectionBase.X, Component->GetSectionBase().X);
				HeightmapRenderData.TopLeftSectionBase.Y = FMath::Min(HeightmapRenderData.TopLeftSectionBase.Y, Component->GetSectionBase().Y);
			}

			bool FirstLayer = true;

			for (auto& ItLayerDataPair : CyLandProxy->ProceduralLayersData)
			{
				FCyProceduralLayerData& LayerData = ItLayerDataPair.Value;

				if (LayerData.Heightmaps.Find(HeightmapRenderData.OriginalHeightmap) == nullptr)
				{
					UTexture2D* Heightmap = CyLandProxy->CreateCyLandTexture(HeightmapRenderData.OriginalHeightmap->Source.GetSizeX(), HeightmapRenderData.OriginalHeightmap->Source.GetSizeY(), TEXTUREGROUP_Terrain_Heightmap, HeightmapRenderData.OriginalHeightmap->Source.GetFormat());
					LayerData.Heightmaps.Add(HeightmapRenderData.OriginalHeightmap, Heightmap);

					int32 MipSubsectionSizeQuads = SubsectionSizeQuads;
					int32 MipSizeU = Heightmap->Source.GetSizeX();
					int32 MipSizeV = Heightmap->Source.GetSizeY();

					UpdateFlags |= EProceduralContentUpdateFlag::Heightmap_ResolveToTexture | EProceduralContentUpdateFlag::Heightmap_BoundsAndCollision;

					// Copy data from Heightmap to first layer, after that all other layer will get init to empty layer
					if (FirstLayer)
					{
						int32 MipIndex = 0;
						TArray<uint8> MipData;
						MipData.Reserve(MipSizeU*MipSizeV * sizeof(FColor));

						while (MipSizeU > 1 && MipSizeV > 1 && MipSubsectionSizeQuads >= 1)
						{
							MipData.Reset();
							HeightmapRenderData.OriginalHeightmap->Source.GetMipData(MipData, MipIndex);

							FColor* HeightmapTextureData = (FColor*)Heightmap->Source.LockMip(MipIndex);
							FMemory::Memcpy(HeightmapTextureData, MipData.GetData(), MipData.Num());
							Heightmap->Source.UnlockMip(MipIndex);

							MipSizeU >>= 1;
							MipSizeV >>= 1;

							MipSubsectionSizeQuads = ((MipSubsectionSizeQuads + 1) >> 1) - 1;
							++MipIndex;
						}
					}
					else
					{
						TArray<FColor*> HeightmapMipMapData;

						while (MipSizeU > 1 && MipSizeV > 1 && MipSubsectionSizeQuads >= 1)
						{
							int32 MipIndex = HeightmapMipMapData.Num();
							FColor* HeightmapTextureData = (FColor*)Heightmap->Source.LockMip(MipIndex);
							FMemory::Memzero(HeightmapTextureData, MipSizeU*MipSizeV * sizeof(FColor));
							HeightmapMipMapData.Add(HeightmapTextureData);

							MipSizeU >>= 1;
							MipSizeV >>= 1;

							MipSubsectionSizeQuads = ((MipSubsectionSizeQuads + 1) >> 1) - 1;
						}

						// Initialize blank heightmap data as if ALL components were in the same heightmap to prevent creating many allocations
						if (EmptyHeightmapData.Num() == 0)
						{
							EmptyHeightmapData.Init(32768, TotalVertexCountX * TotalVertexCountY);
						}

						const FVector DrawScale3D = GetRootComponent()->RelativeScale3D;

						// Init vertex normal data if required
						if (VertexNormals.Num() == 0)
						{
							VertexNormals.AddZeroed(TotalVertexCountX * TotalVertexCountY);
							for (int32 QuadY = 0; QuadY < TotalVertexCountY - 1; QuadY++)
							{
								for (int32 QuadX = 0; QuadX < TotalVertexCountX - 1; QuadX++)
								{
									const FVector Vert00 = FVector(0.0f, 0.0f, ((float)EmptyHeightmapData[FMath::Clamp<int32>(QuadY + 0, 0, TotalVertexCountY) * TotalVertexCountX + FMath::Clamp<int32>(QuadX + 0, 0, TotalVertexCountX)] - 32768.0f)*LANDSCAPE_ZSCALE) * DrawScale3D;
									const FVector Vert01 = FVector(0.0f, 1.0f, ((float)EmptyHeightmapData[FMath::Clamp<int32>(QuadY + 1, 0, TotalVertexCountY) * TotalVertexCountX + FMath::Clamp<int32>(QuadX + 0, 0, TotalVertexCountX)] - 32768.0f)*LANDSCAPE_ZSCALE) * DrawScale3D;
									const FVector Vert10 = FVector(1.0f, 0.0f, ((float)EmptyHeightmapData[FMath::Clamp<int32>(QuadY + 0, 0, TotalVertexCountY) * TotalVertexCountX + FMath::Clamp<int32>(QuadX + 1, 0, TotalVertexCountX)] - 32768.0f)*LANDSCAPE_ZSCALE) * DrawScale3D;
									const FVector Vert11 = FVector(1.0f, 1.0f, ((float)EmptyHeightmapData[FMath::Clamp<int32>(QuadY + 1, 0, TotalVertexCountY) * TotalVertexCountX + FMath::Clamp<int32>(QuadX + 1, 0, TotalVertexCountX)] - 32768.0f)*LANDSCAPE_ZSCALE) * DrawScale3D;

									const FVector FaceNormal1 = ((Vert00 - Vert10) ^ (Vert10 - Vert11)).GetSafeNormal();
									const FVector FaceNormal2 = ((Vert11 - Vert01) ^ (Vert01 - Vert00)).GetSafeNormal();

									// contribute to the vertex normals.
									VertexNormals[(QuadX + 1 + TotalVertexCountX * (QuadY + 0))] += FaceNormal1;
									VertexNormals[(QuadX + 0 + TotalVertexCountX * (QuadY + 1))] += FaceNormal2;
									VertexNormals[(QuadX + 0 + TotalVertexCountX * (QuadY + 0))] += FaceNormal1 + FaceNormal2;
									VertexNormals[(QuadX + 1 + TotalVertexCountX * (QuadY + 1))] += FaceNormal1 + FaceNormal2;
								}
							}
						}

						for (UCyLandComponent* Component : HeightmapRenderData.Components)
						{
							int32 HeightmapComponentOffsetX = FMath::RoundToInt((float)Heightmap->Source.GetSizeX() * Component->HeightmapScaleBias.Z);
							int32 HeightmapComponentOffsetY = FMath::RoundToInt((float)Heightmap->Source.GetSizeY() * Component->HeightmapScaleBias.W);

							for (int32 SubsectionY = 0; SubsectionY < NumSubsections; SubsectionY++)
							{
								for (int32 SubsectionX = 0; SubsectionX < NumSubsections; SubsectionX++)
								{
									for (int32 SubY = 0; SubY <= SubsectionSizeQuads; SubY++)
									{
										for (int32 SubX = 0; SubX <= SubsectionSizeQuads; SubX++)
										{
											// X/Y of the vertex we're looking at in component's coordinates.
											const int32 CompX = SubsectionSizeQuads * SubsectionX + SubX;
											const int32 CompY = SubsectionSizeQuads * SubsectionY + SubY;

											// X/Y of the vertex we're looking indexed into the texture data
											const int32 TexX = (SubsectionSizeQuads + 1) * SubsectionX + SubX;
											const int32 TexY = (SubsectionSizeQuads + 1) * SubsectionY + SubY;

											const int32 HeightTexDataIdx = (HeightmapComponentOffsetX + TexX) + (HeightmapComponentOffsetY + TexY) * Heightmap->Source.GetSizeX();

											// copy height and normal data
											int32 Value = FMath::Clamp<int32>(CompY + Component->GetSectionBase().Y, 0, TotalVertexCountY) * TotalVertexCountX + FMath::Clamp<int32>(CompX + Component->GetSectionBase().X, 0, TotalVertexCountX);
											const uint16 HeightValue = EmptyHeightmapData[Value];
											const FVector Normal = VertexNormals[CompX + Component->GetSectionBase().X + TotalVertexCountX * (CompY + Component->GetSectionBase().Y)].GetSafeNormal();

											HeightmapMipMapData[0][HeightTexDataIdx].R = HeightValue >> 8;
											HeightmapMipMapData[0][HeightTexDataIdx].G = HeightValue & 255;
											HeightmapMipMapData[0][HeightTexDataIdx].B = FMath::RoundToInt(127.5f * (Normal.X + 1.0f));
											HeightmapMipMapData[0][HeightTexDataIdx].A = FMath::RoundToInt(127.5f * (Normal.Y + 1.0f));
										}
									}
								}
							}

							bool IsBorderComponentX = (Component->GetSectionBase().X + 1 * NumSubsections) * InNumComponentsX == TotalVertexCountX;
							bool IsBorderComponentY = (Component->GetSectionBase().Y + 1 * NumSubsections) * InNumComponentsY == TotalVertexCountY;

							Component->GenerateHeightmapMips(HeightmapMipMapData, IsBorderComponentX ? MAX_int32 : 0, IsBorderComponentY ? MAX_int32 : 0);
						}

						// Add remaining mips down to 1x1 to heightmap texture.These do not represent quads and are just a simple averages of the previous mipmaps.
						// These mips are not used for sampling in the vertex shader but could be sampled in the pixel shader.
						int32 Mip = HeightmapMipMapData.Num();
						MipSizeU = (Heightmap->Source.GetSizeX()) >> Mip;
						MipSizeV = (Heightmap->Source.GetSizeY()) >> Mip;
						while (MipSizeU > 1 && MipSizeV > 1)
						{
							HeightmapMipMapData.Add((FColor*)Heightmap->Source.LockMip(Mip));
							const int32 PrevMipSizeU = (Heightmap->Source.GetSizeX()) >> (Mip - 1);
							const int32 PrevMipSizeV = (Heightmap->Source.GetSizeY()) >> (Mip - 1);

							for (int32 Y = 0; Y < MipSizeV; Y++)
							{
								for (int32 X = 0; X < MipSizeU; X++)
								{
									FColor* const TexData = &(HeightmapMipMapData[Mip])[X + Y * MipSizeU];

									const FColor* const PreMipTexData00 = &(HeightmapMipMapData[Mip - 1])[(X * 2 + 0) + (Y * 2 + 0) * PrevMipSizeU];
									const FColor* const PreMipTexData01 = &(HeightmapMipMapData[Mip - 1])[(X * 2 + 0) + (Y * 2 + 1) * PrevMipSizeU];
									const FColor* const PreMipTexData10 = &(HeightmapMipMapData[Mip - 1])[(X * 2 + 1) + (Y * 2 + 0) * PrevMipSizeU];
									const FColor* const PreMipTexData11 = &(HeightmapMipMapData[Mip - 1])[(X * 2 + 1) + (Y * 2 + 1) * PrevMipSizeU];

									TexData->R = (((int32)PreMipTexData00->R + (int32)PreMipTexData01->R + (int32)PreMipTexData10->R + (int32)PreMipTexData11->R) >> 2);
									TexData->G = (((int32)PreMipTexData00->G + (int32)PreMipTexData01->G + (int32)PreMipTexData10->G + (int32)PreMipTexData11->G) >> 2);
									TexData->B = (((int32)PreMipTexData00->B + (int32)PreMipTexData01->B + (int32)PreMipTexData10->B + (int32)PreMipTexData11->B) >> 2);
									TexData->A = (((int32)PreMipTexData00->A + (int32)PreMipTexData01->A + (int32)PreMipTexData10->A + (int32)PreMipTexData11->A) >> 2);
								}
							}
							Mip++;
							MipSizeU >>= 1;
							MipSizeV >>= 1;
						}

						for (int32 i = 0; i < HeightmapMipMapData.Num(); i++)
						{
							Heightmap->Source.UnlockMip(i);
						}
					}

					Heightmap->BeginCachePlatformData();
					Heightmap->ClearAllCachedCookedPlatformData();					
				}

				FirstLayer = false;
			}
		}
	}

	// Setup all Weightmap data
	// TODO

	// Fix Owning actor for Brushes. It can happen after save as operation, for example
	for (FCyProceduralLayer& Layer : CyLand->ProceduralLayers)
	{
		for (int32 i = 0; i < Layer.Brushes.Num(); ++i)
		{
			FCyLandProceduralLayerBrush& Brush = Layer.Brushes[i];

			if (Brush.BPCustomBrush->GetOwningCyLand() == nullptr)
			{
				Brush.BPCustomBrush->SetOwningCyLand(CyLand);
			}
		}

		// TEMP stuff
		if (Layer.HeightmapBrushOrderIndices.Num() == 0)
		{
			for (int32 i = 0; i < Layer.Brushes.Num(); ++i)
			{
				FCyLandProceduralLayerBrush& Brush = Layer.Brushes[i];

				if (Brush.BPCustomBrush->IsAffectingHeightmap())
				{
					Layer.HeightmapBrushOrderIndices.Add(i);
				}
			}
		}

		if (Layer.WeightmapBrushOrderIndices.Num() == 0)
		{
			for (int32 i = 0; i < Layer.Brushes.Num(); ++i)
			{
				FCyLandProceduralLayerBrush& Brush = Layer.Brushes[i];

				if (Brush.BPCustomBrush->IsAffectingWeightmap())
				{
					Layer.WeightmapBrushOrderIndices.Add(i);
				}
			}
		}		
		// TEMP stuff
	}

	CyLand->RequestProceduralContentUpdate(UpdateFlags);
}

void ACyLand::CopyProceduralTargetToResolveTarget(UTexture* InHeightmapRTRead, UTexture* InCopyResolveTarget, FTextureResource* InCopyResolveTargetCPUResource, const FIntPoint& InFirstComponentSectionBase, int32 InCurrentMip) const
{
	FCyLandProceduralCopyResource_RenderThread CopyResource(InHeightmapRTRead, InCopyResolveTarget, InCopyResolveTargetCPUResource, InFirstComponentSectionBase, SubsectionSizeQuads, NumSubsections, InCurrentMip);

	ENQUEUE_RENDER_COMMAND(FCyLandProceduralCopyResultCommand)(
		[CopyResource](FRHICommandListImmediate& RHICmdList) mutable
		{
			CopyResource.CopyToResolveTarget(RHICmdList);
		});
}

void ACyLand::DrawHeightmapComponentsToRenderTargetMips(TArray<UCyLandComponent*>& InComponentsToDraw, UTexture* InReadHeightmap, bool InClearRTWrite, struct FCyLandHeightmapProceduralShaderParameters& InShaderParams) const
{
	bool OutputDebugName = CVarOutputProceduralDebugDrawCallName.GetValueOnAnyThread() == 1 || CVarOutputProceduralRTContent.GetValueOnAnyThread() == 1 ? true : false;
	int32 CurrentMip = 1;
	UTexture* ReadMipRT = InReadHeightmap;

	for (int32 MipRTIndex = EHeightmapRTType::CyLandSizeMip1; MipRTIndex < EHeightmapRTType::Count; ++MipRTIndex)
	{
		UTextureRenderTarget2D* WriteMipRT = HeightmapRTList[MipRTIndex];

		if (WriteMipRT != nullptr)
		{
			DrawHeightmapComponentsToRenderTarget(OutputDebugName ? FString::Printf(TEXT("LS Height: %s = -> %s CombinedAtlasWithMips %d"), *ReadMipRT->GetName(), *WriteMipRT->GetName(), CurrentMip) : TEXT(""),
												  InComponentsToDraw, ReadMipRT, nullptr, WriteMipRT, ERTDrawingType::RTMips, InClearRTWrite, InShaderParams, CurrentMip++);
		}

		ReadMipRT = HeightmapRTList[MipRTIndex];
	}
}

void ACyLand::DrawHeightmapComponentsToRenderTarget(const FString& InDebugName, TArray<UCyLandComponent*>& InComponentsToDraw, UTexture* InHeightmapRTRead, UTextureRenderTarget2D* InOptionalHeightmapRTRead2, UTextureRenderTarget2D* InHeightmapRTWrite,
													  ERTDrawingType InDrawType, bool InClearRTWrite, FCyLandHeightmapProceduralShaderParameters& InShaderParams, int32 InMipRender) const
{
	check(InHeightmapRTRead != nullptr);
	check(InHeightmapRTWrite != nullptr);

	FIntPoint HeightmapWriteTextureSize(InHeightmapRTWrite->SizeX, InHeightmapRTWrite->SizeY);
	FIntPoint HeightmapReadTextureSize(InHeightmapRTRead->Source.GetSizeX(), InHeightmapRTRead->Source.GetSizeY());
	UTextureRenderTarget2D* HeightmapRTRead = Cast<UTextureRenderTarget2D>(InHeightmapRTRead);

	if (HeightmapRTRead != nullptr)
	{
		HeightmapReadTextureSize.X = HeightmapRTRead->SizeX;
		HeightmapReadTextureSize.Y = HeightmapRTRead->SizeY;
	}

	// Quad Setup
	TArray<FCyLandProceduralTriangle> TriangleList;
	TriangleList.Reserve(InComponentsToDraw.Num() * 2 * NumSubsections);

	switch (InDrawType)
	{
		case ERTDrawingType::RTAtlas:
		{
			for (UCyLandComponent* Component : InComponentsToDraw)
			{
				FVector2D HeightmapScaleBias(Component->HeightmapScaleBias.Z, Component->HeightmapScaleBias.W);
				GenerateHeightmapQuadsAtlas(Component->GetSectionBase(), HeightmapScaleBias, SubsectionSizeQuads, HeightmapReadTextureSize, HeightmapWriteTextureSize, TriangleList);
			}
		} break;

		case ERTDrawingType::RTAtlasToNonAtlas:
		{
			for (UCyLandComponent* Component : InComponentsToDraw)
			{
				FVector2D HeightmapScaleBias(Component->HeightmapScaleBias.Z, Component->HeightmapScaleBias.W);
				GenerateHeightmapQuadsAtlasToNonAtlas(Component->GetSectionBase(), HeightmapScaleBias, SubsectionSizeQuads, HeightmapReadTextureSize, HeightmapWriteTextureSize, TriangleList);
			}
		} break;

		case ERTDrawingType::RTNonAtlas:
		{
			for (UCyLandComponent* Component : InComponentsToDraw)
			{
				FVector2D HeightmapScaleBias(Component->HeightmapScaleBias.Z, Component->HeightmapScaleBias.W);
				GenerateHeightmapQuadsNonAtlas(Component->GetSectionBase(), HeightmapScaleBias, SubsectionSizeQuads, HeightmapReadTextureSize, HeightmapWriteTextureSize, TriangleList);
			}
		} break;

		case ERTDrawingType::RTNonAtlasToAtlas:
		{
			for (UCyLandComponent* Component : InComponentsToDraw)
			{
				FVector2D HeightmapScaleBias(Component->HeightmapScaleBias.Z, Component->HeightmapScaleBias.W);
				GenerateHeightmapQuadsNonAtlasToAtlas(Component->GetSectionBase(), HeightmapScaleBias, SubsectionSizeQuads, HeightmapReadTextureSize, HeightmapWriteTextureSize, TriangleList);
			}
		} break;

		case ERTDrawingType::RTMips:
		{
			for (UCyLandComponent* Component : InComponentsToDraw)
			{
				FVector2D HeightmapScaleBias(Component->HeightmapScaleBias.Z, Component->HeightmapScaleBias.W);
				GenerateHeightmapQuadsMip(Component->GetSectionBase(), HeightmapScaleBias, SubsectionSizeQuads, HeightmapReadTextureSize, HeightmapWriteTextureSize, InMipRender, TriangleList);
			}			
		} break;

		default:
		{
			check(false);
			return;
		}
	}

	InShaderParams.ReadHeightmap1 = InHeightmapRTRead;
	InShaderParams.ReadHeightmap2 = InOptionalHeightmapRTRead2;
	InShaderParams.HeightmapSize = HeightmapReadTextureSize;
	InShaderParams.CurrentMipComponentVertexCount = (((SubsectionSizeQuads + 1) * NumSubsections) >> InMipRender);

	if (InMipRender > 0)
	{
		InShaderParams.CurrentMipHeightmapSize = HeightmapWriteTextureSize;
		InShaderParams.ParentMipHeightmapSize = HeightmapReadTextureSize;
	}

	FMatrix ProjectionMatrix = AdjustProjectionMatrixForRHI(FTranslationMatrix(FVector(0, 0, 0)) *
															FMatrix(FPlane(1.0f / (FMath::Max<uint32>(HeightmapWriteTextureSize.X, 1.f) / 2.0f), 0.0, 0.0f, 0.0f), FPlane(0.0f, -1.0f / (FMath::Max<uint32>(HeightmapWriteTextureSize.Y, 1.f) / 2.0f), 0.0f, 0.0f), FPlane(0.0f, 0.0f, 1.0f, 0.0f), FPlane(-1.0f, 1.0f, 0.0f, 1.0f)));

	FCyLandHeightmapProceduralRender_RenderThread ProceduralRender(InDebugName, InHeightmapRTWrite, HeightmapWriteTextureSize, HeightmapReadTextureSize, ProjectionMatrix, InShaderParams, InMipRender, TriangleList);

	ENQUEUE_RENDER_COMMAND(FDrawSceneCommand)(
		[ProceduralRender, ClearRT = InClearRTWrite](FRHICommandListImmediate& RHICmdList) mutable
		{
			ProceduralRender.Render(RHICmdList, ClearRT);
		});
	
	PrintDebugRTHeightmap(InDebugName, InHeightmapRTWrite, InMipRender, InShaderParams.GenerateNormals);
}

void ACyLand::GenerateHeightmapQuad(const FIntPoint& InVertexPosition, const float InVertexSize, const FVector2D& InUVStart, const FVector2D& InUVSize, TArray<FCyLandProceduralTriangle>& OutTriangles) const
{
	FCyLandProceduralTriangle Tri1;
	
	Tri1.V0.Position = FVector2D(InVertexPosition.X, InVertexPosition.Y);
	Tri1.V1.Position = FVector2D(InVertexPosition.X + InVertexSize, InVertexPosition.Y);
	Tri1.V2.Position = FVector2D(InVertexPosition.X + InVertexSize, InVertexPosition.Y + InVertexSize);

	Tri1.V0.UV = FVector2D(InUVStart.X, InUVStart.Y);
	Tri1.V1.UV = FVector2D(InUVStart.X + InUVSize.X, InUVStart.Y);
	Tri1.V2.UV = FVector2D(InUVStart.X + InUVSize.X, InUVStart.Y + InUVSize.Y);
	OutTriangles.Add(Tri1);

	FCyLandProceduralTriangle Tri2;
	Tri2.V0.Position = FVector2D(InVertexPosition.X + InVertexSize, InVertexPosition.Y + InVertexSize);
	Tri2.V1.Position = FVector2D(InVertexPosition.X, InVertexPosition.Y + InVertexSize);
	Tri2.V2.Position = FVector2D(InVertexPosition.X, InVertexPosition.Y);

	Tri2.V0.UV = FVector2D(InUVStart.X + InUVSize.X, InUVStart.Y + InUVSize.Y);
	Tri2.V1.UV = FVector2D(InUVStart.X, InUVStart.Y + InUVSize.Y);
	Tri2.V2.UV = FVector2D(InUVStart.X, InUVStart.Y);

	OutTriangles.Add(Tri2);
}

void ACyLand::GenerateHeightmapQuadsAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, TArray<FCyLandProceduralTriangle>& OutTriangles) const
{
	FIntPoint ComponentSectionBase = InSectionBase;
	FIntPoint UVComponentSectionBase = InSectionBase;

	int32 LocalComponentSizeQuad = InSubSectionSizeQuad * NumSubsections;
	int32 SubsectionSizeVerts = InSubSectionSizeQuad + 1;

	FVector2D HeightmapPositionOffset(FMath::RoundToInt(ComponentSectionBase.X / LocalComponentSizeQuad), FMath::RoundToInt(ComponentSectionBase.Y / LocalComponentSizeQuad));
	FVector2D ComponentsPerTexture(FMath::RoundToInt(InWriteSize.X / LocalComponentSizeQuad), FMath::RoundToInt(InWriteSize.Y / LocalComponentSizeQuad));

	if (InReadSize.X >= InWriteSize.X)
	{
		if (InReadSize.X == InWriteSize.X)
		{
			if (ComponentsPerTexture.X > 1.0f)
			{
				UVComponentSectionBase.X = HeightmapPositionOffset.X * (SubsectionSizeVerts * NumSubsections);
			}
			else
			{
				UVComponentSectionBase.X -= (UVComponentSectionBase.X + LocalComponentSizeQuad > InWriteSize.X) ? FMath::FloorToInt((HeightmapPositionOffset.X / ComponentsPerTexture.X)) * ComponentsPerTexture.X * LocalComponentSizeQuad : 0;
			}
		}

		ComponentSectionBase.X -= (ComponentSectionBase.X + LocalComponentSizeQuad > InWriteSize.X) ? FMath::FloorToInt((HeightmapPositionOffset.X / ComponentsPerTexture.X)) * ComponentsPerTexture.X * LocalComponentSizeQuad : 0;
		HeightmapPositionOffset.X = ComponentSectionBase.X / LocalComponentSizeQuad;
	}

	if (InReadSize.Y >= InWriteSize.Y)
	{
		if (InReadSize.Y == InWriteSize.Y)
		{
			if (ComponentsPerTexture.Y > 1.0f)
			{
				UVComponentSectionBase.Y = HeightmapPositionOffset.Y * (SubsectionSizeVerts * NumSubsections);
			}
			else
			{
				UVComponentSectionBase.Y -= (UVComponentSectionBase.Y + LocalComponentSizeQuad > InWriteSize.Y) ? FMath::FloorToInt((HeightmapPositionOffset.Y / ComponentsPerTexture.Y)) * ComponentsPerTexture.Y * LocalComponentSizeQuad : 0;
			}
		}

		ComponentSectionBase.Y -= (ComponentSectionBase.Y + LocalComponentSizeQuad > InWriteSize.Y) ? FMath::FloorToInt((HeightmapPositionOffset.Y / ComponentsPerTexture.Y)) * ComponentsPerTexture.Y * LocalComponentSizeQuad : 0;
		HeightmapPositionOffset.Y = ComponentSectionBase.Y / LocalComponentSizeQuad;
	}

	ComponentSectionBase.X = HeightmapPositionOffset.X * (SubsectionSizeVerts * NumSubsections);
	ComponentSectionBase.Y = HeightmapPositionOffset.Y * (SubsectionSizeVerts * NumSubsections);

	FVector2D HeightmapUVSize((float)SubsectionSizeVerts / (float)InReadSize.X, (float)SubsectionSizeVerts / (float)InReadSize.Y);
	FIntPoint SubSectionSectionBase;

	for (int8 SubY = 0; SubY < NumSubsections; ++SubY)
	{
		for (int8 SubX = 0; SubX < NumSubsections; ++SubX)
		{
			SubSectionSectionBase.X = ComponentSectionBase.X + SubsectionSizeVerts * SubX;
			SubSectionSectionBase.Y = ComponentSectionBase.Y + SubsectionSizeVerts * SubY;

			// Offset for this component's data in heightmap texture
			FVector2D HeightmapUVStart;

			if (InReadSize.X >= InWriteSize.X)
			{
				HeightmapUVStart.X = ((float)UVComponentSectionBase.X / (float)InReadSize.X) + HeightmapUVSize.X * (float)SubX;
			}
			else
			{
				HeightmapUVStart.X = InScaleBias.X + HeightmapUVSize.X * (float)SubX;
			}

			if (InReadSize.Y >= InWriteSize.Y)
			{
				HeightmapUVStart.Y = ((float)UVComponentSectionBase.Y / (float)InReadSize.Y) + HeightmapUVSize.Y * (float)SubY;
			}
			else
			{
				HeightmapUVStart.Y = InScaleBias.Y + HeightmapUVSize.Y * (float)SubY;
			}

			GenerateHeightmapQuad(SubSectionSectionBase, SubsectionSizeVerts, HeightmapUVStart, HeightmapUVSize, OutTriangles);
		}
	}
}

void ACyLand::GenerateHeightmapQuadsMip(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, int32 CurrentMip, TArray<FCyLandProceduralTriangle>& OutTriangles) const
{
	int32 LocalComponentSizeQuad = InSubSectionSizeQuad * NumSubsections;
	int32 SubsectionSizeVerts = InSubSectionSizeQuad + 1;
	int32 MipSubsectionSizeVerts = SubsectionSizeVerts >> CurrentMip;

	FVector2D HeightmapPositionOffset(FMath::RoundToInt(InSectionBase.X / LocalComponentSizeQuad), FMath::RoundToInt(InSectionBase.Y / LocalComponentSizeQuad));
	FVector2D ComponentsPerTexture(FMath::RoundToInt(InWriteSize.X / LocalComponentSizeQuad), FMath::RoundToInt(InWriteSize.Y / LocalComponentSizeQuad));

	FIntPoint ComponentSectionBase(HeightmapPositionOffset.X * (MipSubsectionSizeVerts * NumSubsections), HeightmapPositionOffset.Y * (MipSubsectionSizeVerts * NumSubsections));
	FIntPoint UVComponentSectionBase(HeightmapPositionOffset.X * (SubsectionSizeVerts * NumSubsections), HeightmapPositionOffset.Y * (SubsectionSizeVerts * NumSubsections));
	FVector2D HeightmapUVSize((float)(SubsectionSizeVerts >> (CurrentMip - 1)) / (float)InReadSize.X, (float)(SubsectionSizeVerts >> (CurrentMip - 1)) / (float)InReadSize.Y);
	FIntPoint SubSectionSectionBase;

	for (int8 SubY = 0; SubY < NumSubsections; ++SubY)
	{
		for (int8 SubX = 0; SubX < NumSubsections; ++SubX)
		{
			SubSectionSectionBase.X = ComponentSectionBase.X + MipSubsectionSizeVerts * SubX;
			SubSectionSectionBase.Y = ComponentSectionBase.Y + MipSubsectionSizeVerts * SubY;

			// Offset for this component's data in heightmap texture
			FVector2D HeightmapUVStart;
			HeightmapUVStart.X = ((float)(UVComponentSectionBase.X >> (CurrentMip - 1)) / (float)InReadSize.X) + HeightmapUVSize.X * (float)SubX;
			HeightmapUVStart.Y = ((float)(UVComponentSectionBase.Y >> (CurrentMip - 1)) / (float)InReadSize.Y) + HeightmapUVSize.Y * (float)SubY;

			GenerateHeightmapQuad(SubSectionSectionBase, MipSubsectionSizeVerts, HeightmapUVStart, HeightmapUVSize, OutTriangles);
		}
	}
}

void ACyLand::GenerateHeightmapQuadsAtlasToNonAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InHeightmapReadTextureSize, const FIntPoint& InHeightmapWriteTextureSize, TArray<FCyLandProceduralTriangle>& OutTriangles) const
{
	FIntPoint ComponentSectionBase = InSectionBase;
	int32 LocalComponentSizeQuad = InSubSectionSizeQuad * NumSubsections;
	int32 HeightmapPositionOffsetX = ComponentSectionBase.X / LocalComponentSizeQuad;
	int32 HeightmapPositionOffsetY = ComponentSectionBase.Y / LocalComponentSizeQuad;
	int32 SubsectionSizeVerts = InSubSectionSizeQuad + 1;

	FIntPoint UVComponentSectionBase = InSectionBase;
	UVComponentSectionBase.X = HeightmapPositionOffsetX * (SubsectionSizeVerts * NumSubsections);
	UVComponentSectionBase.Y = HeightmapPositionOffsetY * (SubsectionSizeVerts * NumSubsections);

	ComponentSectionBase.X = HeightmapPositionOffsetX * (InSubSectionSizeQuad * NumSubsections);
	ComponentSectionBase.Y = HeightmapPositionOffsetY * (InSubSectionSizeQuad * NumSubsections);

	FVector2D HeightmapUVSize((float)SubsectionSizeVerts / (float)InHeightmapReadTextureSize.X, (float)SubsectionSizeVerts / (float)InHeightmapReadTextureSize.Y);
	FIntPoint SubSectionSectionBase;

	for (int8 SubY = 0; SubY < NumSubsections; ++SubY)
	{
		for (int8 SubX = 0; SubX < NumSubsections; ++SubX)
		{
			SubSectionSectionBase.X = ComponentSectionBase.X + InSubSectionSizeQuad * SubX;
			SubSectionSectionBase.Y = ComponentSectionBase.Y + InSubSectionSizeQuad * SubY;

			// Offset for this component's data in heightmap texture
			FVector2D HeightmapUVStart;

			if (InHeightmapReadTextureSize.X >= InHeightmapWriteTextureSize.X)
			{
				HeightmapUVStart.X = ((float)UVComponentSectionBase.X / (float)InHeightmapReadTextureSize.X) + HeightmapUVSize.X * (float)SubX;
			}
			else
			{
				HeightmapUVStart.X = InScaleBias.X + HeightmapUVSize.X * (float)SubX;
			}

			if (InHeightmapReadTextureSize.Y >= InHeightmapWriteTextureSize.Y)
			{
				HeightmapUVStart.Y = ((float)UVComponentSectionBase.Y / (float)InHeightmapReadTextureSize.Y) + HeightmapUVSize.Y * (float)SubY;
			}
			else
			{
				HeightmapUVStart.Y = InScaleBias.Y + HeightmapUVSize.Y * (float)SubY;
			}

			GenerateHeightmapQuad(SubSectionSectionBase, SubsectionSizeVerts, HeightmapUVStart, HeightmapUVSize, OutTriangles);
		}
	}
}

void ACyLand::GenerateHeightmapQuadsNonAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InHeightmapReadTextureSize, const FIntPoint& InHeightmapWriteTextureSize, TArray<FCyLandProceduralTriangle>& OutTriangles) const
{
	// We currently only support drawing in non atlas mode with the same texture size
	check(InHeightmapReadTextureSize.X == InHeightmapWriteTextureSize.X && InHeightmapReadTextureSize.Y == InHeightmapWriteTextureSize.Y);

	FIntPoint ComponentSectionBase = InSectionBase;
	int32 LocalComponentSizeQuad = InSubSectionSizeQuad * NumSubsections;
	int32 HeightmapPositionOffsetX = ComponentSectionBase.X / LocalComponentSizeQuad;
	int32 HeightmapPositionOffsetY = ComponentSectionBase.Y / LocalComponentSizeQuad;
	int32 SubsectionSizeVerts = InSubSectionSizeQuad + 1;

	FIntPoint UVComponentSectionBase = InSectionBase;
	UVComponentSectionBase.X = HeightmapPositionOffsetX * (InSubSectionSizeQuad * NumSubsections);
	UVComponentSectionBase.Y = HeightmapPositionOffsetY * (InSubSectionSizeQuad * NumSubsections);

	FVector2D HeightmapUVSize((float)SubsectionSizeVerts / (float)InHeightmapReadTextureSize.X, (float)SubsectionSizeVerts / (float)InHeightmapReadTextureSize.Y);
	FIntPoint SubSectionSectionBase;

	for (int8 SubY = 0; SubY < NumSubsections; ++SubY)
	{
		for (int8 SubX = 0; SubX < NumSubsections; ++SubX)
		{
			SubSectionSectionBase.X = ComponentSectionBase.X + InSubSectionSizeQuad * SubX;
			SubSectionSectionBase.Y = ComponentSectionBase.Y + InSubSectionSizeQuad * SubY;

			// Offset for this component's data in heightmap texture
			FVector2D HeightmapUVStart(((float)UVComponentSectionBase.X / (float)InHeightmapReadTextureSize.X) + HeightmapUVSize.X * (float)SubX, ((float)UVComponentSectionBase.Y / (float)InHeightmapReadTextureSize.Y) + HeightmapUVSize.Y * (float)SubY);
			GenerateHeightmapQuad(SubSectionSectionBase, SubsectionSizeVerts, HeightmapUVStart, HeightmapUVSize, OutTriangles);
		}
	}
}

void ACyLand::GenerateHeightmapQuadsNonAtlasToAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InHeightmapReadTextureSize, const FIntPoint& InHeightmapWriteTextureSize, TArray<FCyLandProceduralTriangle>& OutTriangles) const
{
	FIntPoint ComponentSectionBase = InSectionBase;
	int32 LocalComponentSizeQuad = InSubSectionSizeQuad * NumSubsections;
	int32 HeightmapPositionOffsetX = ComponentSectionBase.X / LocalComponentSizeQuad;
	int32 HeightmapPositionOffsetY = ComponentSectionBase.Y / LocalComponentSizeQuad;
	int32 SubsectionSizeVerts = InSubSectionSizeQuad + 1;

	ComponentSectionBase.X = HeightmapPositionOffsetX * (SubsectionSizeVerts * NumSubsections);
	ComponentSectionBase.Y = HeightmapPositionOffsetY * (SubsectionSizeVerts * NumSubsections);

	FVector2D HeightmapUVSize((float)SubsectionSizeVerts / (float)InHeightmapReadTextureSize.X, (float)SubsectionSizeVerts / (float)InHeightmapReadTextureSize.Y);
	FIntPoint SubSectionSectionBase;

	for (int8 SubY = 0; SubY < NumSubsections; ++SubY)
	{
		for (int8 SubX = 0; SubX < NumSubsections; ++SubX)
		{
			SubSectionSectionBase.X = ComponentSectionBase.X + SubsectionSizeVerts * SubX;
			SubSectionSectionBase.Y = ComponentSectionBase.Y + SubsectionSizeVerts * SubY;

			// Offset for this component's data in heightmap texture
			float HeightmapScaleBiasZ = (float)InSectionBase.X / (float)InHeightmapReadTextureSize.X;
			float HeightmapScaleBiasW = (float)InSectionBase.Y / (float)InHeightmapReadTextureSize.Y;
			FVector2D HeightmapUVStart(HeightmapScaleBiasZ + ((float)InSubSectionSizeQuad / (float)InHeightmapReadTextureSize.X) * (float)SubX, HeightmapScaleBiasW + ((float)InSubSectionSizeQuad / (float)InHeightmapReadTextureSize.Y) * (float)SubY);

			GenerateHeightmapQuad(SubSectionSectionBase, SubsectionSizeVerts, HeightmapUVStart, HeightmapUVSize, OutTriangles);
		}
	}
}

void ACyLand::PrintDebugHeightData(const FString& InContext, const TArray<FColor>& InHeightmapData, const FIntPoint& InDataSize, int32 InMipRender, bool InOutputNormals) const
{
	bool DisplayDebugPrint = CVarOutputProceduralRTContent.GetValueOnAnyThread() == 1 ? true : false;
	bool DisplayHeightAsDelta = false;

	if (!DisplayDebugPrint)
	{
		return;
	}

	TArray<uint16> HeightData;
	TArray<FVector> NormalData;
	HeightData.Reserve(InHeightmapData.Num());
	NormalData.Reserve(InHeightmapData.Num());

	for (FColor Color : InHeightmapData)
	{
		uint16 Height = ((Color.R << 8) | Color.G);
		HeightData.Add(Height);

		if (InOutputNormals)
		{
			FVector Normal;
			Normal.X = Color.B > 0.0f ? ((float)Color.B / 127.5f - 1.0f) : 0.0f;
			Normal.Y = Color.A > 0.0f ? ((float)Color.A / 127.5f - 1.0f) : 0.0f;
			Normal.Z = 0.0f;

			NormalData.Add(Normal);
		}
	}

	UE_LOG(LogCyLandBP, Display, TEXT("Context: %s"), *InContext);

	int32 MipSize = ((SubsectionSizeQuads + 1) >> InMipRender);

	for (int32 Y = 0; Y < InDataSize.Y; ++Y)
	{
		FString HeightmapHeightOutput;

		for (int32 X = 0; X < InDataSize.X; ++X)
		{
			int32 HeightDelta = HeightData[X + Y * InDataSize.X];

			if (DisplayHeightAsDelta)
			{
				HeightDelta = HeightDelta >= 32768 ? HeightDelta - 32768 : HeightDelta;
			}

			if (X > 0 && MipSize > 0 && X % MipSize == 0)
			{
				HeightmapHeightOutput += FString::Printf(TEXT("  "));
			}

			FString HeightStr = FString::Printf(TEXT("%d"), HeightDelta);

			int32 PadCount = 5 - HeightStr.Len();
			if (PadCount > 0)
			{
				HeightStr = FString::ChrN(PadCount, '0') + HeightStr;
			}

			HeightmapHeightOutput += HeightStr + TEXT(" ");
		}

		if (Y > 0 && MipSize > 0 && Y % MipSize == 0)
		{
			UE_LOG(LogCyLandBP, Display, TEXT(""));
		}

		UE_LOG(LogCyLandBP, Display, TEXT("%s"), *HeightmapHeightOutput);
	}

	if (InOutputNormals)
	{
		UE_LOG(LogCyLandBP, Display, TEXT(""));

		for (int32 Y = 0; Y < InDataSize.Y; ++Y)
		{
			FString HeightmapNormaltOutput;

			for (int32 X = 0; X < InDataSize.X; ++X)
			{
				FVector Normal = NormalData[X + Y * InDataSize.X];

				if (X > 0 && MipSize > 0 && X % MipSize == 0)
				{
					HeightmapNormaltOutput += FString::Printf(TEXT("  "));
				}

				HeightmapNormaltOutput += FString::Printf(TEXT(" %s"), *Normal.ToString());
			}

			if (Y > 0 && MipSize > 0 && Y % MipSize == 0)
			{
				UE_LOG(LogCyLandBP, Display, TEXT(""));
			}

			UE_LOG(LogCyLandBP, Display, TEXT("%s"), *HeightmapNormaltOutput);
		}
	}
}

void ACyLand::PrintDebugRTHeightmap(FString Context, UTextureRenderTarget2D* InDebugRT, int32 InMipRender, bool InOutputNormals) const
{
	bool DisplayDebugPrint = CVarOutputProceduralRTContent.GetValueOnAnyThread() == 1 ? true : false;

	if (!DisplayDebugPrint)
	{
		return;
	}

	FTextureRenderTargetResource* RenderTargetResource = InDebugRT->GameThread_GetRenderTargetResource();
	ENQUEUE_RENDER_COMMAND(HeightmapRTCanvasRenderTargetResolveCommand)(
		[RenderTargetResource](FRHICommandListImmediate& RHICmdList)
		{
			// Copy (resolve) the rendered image from the frame buffer to its render target texture
			RHICmdList.CopyToResolveTarget(RenderTargetResource->GetRenderTargetTexture(), RenderTargetResource->TextureRHI, FResolveParams());
		});

	FlushRenderingCommands();
	int32 MinX, MinY, MaxX, MaxY;
	const UCyLandInfo* CyLandInfo = GetCyLandInfo();
	CyLandInfo->GetCyLandExtent(MinX, MinY, MaxX, MaxY);
	FIntRect SampleRect = FIntRect(0, 0, InDebugRT->SizeX, InDebugRT->SizeY);

	FReadSurfaceDataFlags Flags(RCM_UNorm, CubeFace_MAX);

	TArray<FColor> OutputRTHeightmap;
	OutputRTHeightmap.Reserve(SampleRect.Width() * SampleRect.Height());

	InDebugRT->GameThread_GetRenderTargetResource()->ReadPixels(OutputRTHeightmap, Flags, SampleRect);

	PrintDebugHeightData(Context, OutputRTHeightmap, FIntPoint(SampleRect.Width(), SampleRect.Height()), InMipRender, InOutputNormals);
}

void ACyLand::RegenerateProceduralHeightmaps()
{
	SCOPE_CYCLE_COUNTER(STAT_CyLandRegenerateProceduralHeightmaps);

	UCyLandInfo* Info = GetCyLandInfo();

	if (ProceduralContentUpdateFlags == 0 || Info == nullptr)
	{
		return;
	}

	TArray<ACyLandProxy*> AllCyLands;
	AllCyLands.Add(this);

	for (auto& It : Info->Proxies)
	{
		AllCyLands.Add(It);
	}

	for (ACyLandProxy* CyLand : AllCyLands)
	{
		for (auto& ItLayerDataPair : CyLand->ProceduralLayersData)
		{
			for (auto& ItHeightmapPair : ItLayerDataPair.Value.Heightmaps)
			{
				UTexture2D* OriginalHeightmap = ItHeightmapPair.Key;
				UTexture2D* LayerHeightmap = ItHeightmapPair.Value;

				if (!LayerHeightmap->IsAsyncCacheComplete() || !OriginalHeightmap->IsFullyStreamedIn())
				{
					return;
				}

				if (LayerHeightmap->Resource == nullptr)
				{
					LayerHeightmap->FinishCachePlatformData();

					LayerHeightmap->Resource = LayerHeightmap->CreateResource();
					if (LayerHeightmap->Resource)
					{
						BeginInitResource(LayerHeightmap->Resource);
					}
				}

				if (!LayerHeightmap->Resource->IsInitialized() || !LayerHeightmap->IsFullyStreamedIn())
				{
					return;
				}
			}
		}
	}

	TArray<UCyLandComponent*> AllCyLandComponents;

	for (ACyLandProxy* CyLand : AllCyLands)
	{
		AllCyLandComponents.Append(CyLand->CyLandComponents);
	}

	if ((ProceduralContentUpdateFlags & EProceduralContentUpdateFlag::Heightmap_Render) != 0 && HeightmapRTList.Num() > 0)
	{
		FCyLandHeightmapProceduralShaderParameters ShaderParams;

		bool FirstLayer = true;
		UTextureRenderTarget2D* CombinedHeightmapAtlasRT = HeightmapRTList[EHeightmapRTType::CyLandSizeCombinedAtlas];
		UTextureRenderTarget2D* CombinedHeightmapNonAtlasRT = HeightmapRTList[EHeightmapRTType::CyLandSizeCombinedNonAtlas];
		UTextureRenderTarget2D* CyLandScratchRT1 = HeightmapRTList[EHeightmapRTType::CyLandSizeScratch1];
		UTextureRenderTarget2D* CyLandScratchRT2 = HeightmapRTList[EHeightmapRTType::CyLandSizeScratch2];
		UTextureRenderTarget2D* CyLandScratchRT3 = HeightmapRTList[EHeightmapRTType::CyLandSizeScratch3];

		bool OutputDebugName = CVarOutputProceduralDebugDrawCallName.GetValueOnAnyThread() == 1 || CVarOutputProceduralRTContent.GetValueOnAnyThread() == 1 ? true : false;

		for (FCyProceduralLayer& Layer : ProceduralLayers)
		{
			//Draw Layer heightmap to Combined RT Atlas
			ShaderParams.ApplyLayerModifiers = true;
			ShaderParams.LayerVisible = Layer.Visible;
			ShaderParams.LayerWeight = Layer.Weight;

			for (ACyLandProxy* CyLand : AllCyLands)
			{
				FCyProceduralLayerData* LayerData = CyLand->ProceduralLayersData.Find(Layer.Name);

				if (LayerData != nullptr)
				{
					for (auto& ItPair : LayerData->Heightmaps)
					{
						FCyRenderDataPerHeightmap& HeightmapRenderData = *CyLand->RenderDataPerHeightmap.Find(ItPair.Key);
						UTexture2D* Heightmap = ItPair.Value;

						CopyProceduralTargetToResolveTarget(Heightmap, CyLandScratchRT1, nullptr, HeightmapRenderData.TopLeftSectionBase, 0);

						PrintDebugRTHeightmap(OutputDebugName ? FString::Printf(TEXT("LS Height: %s Component %s += -> CombinedAtlas %s"), *Layer.Name.ToString(), *Heightmap->GetName(), *CyLandScratchRT1->GetName()) : TEXT(""), CyLandScratchRT1);
					}
				}
			}

			// NOTE: From this point on, we always work in non atlas, we'll convert back at the end to atlas only
			DrawHeightmapComponentsToRenderTarget(OutputDebugName ? FString::Printf(TEXT("LS Height: %s += -> NonAtlas %s"), *Layer.Name.ToString(), *CyLandScratchRT1->GetName(), *CyLandScratchRT2->GetName()) : TEXT(""),
												  AllCyLandComponents, CyLandScratchRT1, nullptr, CyLandScratchRT2, ERTDrawingType::RTAtlasToNonAtlas, true, ShaderParams);

			// Combine Current layer with current result
			DrawHeightmapComponentsToRenderTarget(OutputDebugName ? FString::Printf(TEXT("LS Height: %s += -> CombinedNonAtlas %s"), *Layer.Name.ToString(), *CyLandScratchRT2->GetName(), *CombinedHeightmapNonAtlasRT->GetName()) : TEXT(""),
												AllCyLandComponents, CyLandScratchRT2, FirstLayer ? nullptr : CyLandScratchRT3, CombinedHeightmapNonAtlasRT, ERTDrawingType::RTNonAtlas, FirstLayer, ShaderParams);

			ShaderParams.ApplyLayerModifiers = false;

			if (Layer.Visible)
			{
				// Draw each Combined RT into a Non Atlas RT format to be use as base for all brush rendering
				if (Layer.Brushes.Num() > 0)
				{
					CopyProceduralTargetToResolveTarget(CombinedHeightmapNonAtlasRT, CyLandScratchRT1, nullptr, FIntPoint(0,0), 0);
					PrintDebugRTHeightmap(OutputDebugName ? FString::Printf(TEXT("LS Height: %s Component %s += -> CombinedNonAtlas %s"), *Layer.Name.ToString(), *CombinedHeightmapNonAtlasRT->GetName(), *CyLandScratchRT1->GetName()) : TEXT(""), CyLandScratchRT1);
				}

				// Draw each brushes				
				for (int32 i = 0; i < Layer.HeightmapBrushOrderIndices.Num(); ++i)
				{
					// TODO: handle conversion from float to RG8 by using material params to write correct values
					// TODO: handle conversion/handling of RT not same size as internal size

					FCyLandProceduralLayerBrush& Brush = Layer.Brushes[Layer.HeightmapBrushOrderIndices[i]];

					if (Brush.BPCustomBrush == nullptr)
					{
						continue;
					}

					check(Brush.BPCustomBrush->IsAffectingHeightmap());

					if (!Brush.IsInitialized())
					{
						Brush.Initialize(GetBoundingRect(), FIntPoint(CombinedHeightmapNonAtlasRT->SizeX, CombinedHeightmapNonAtlasRT->SizeY));
					}

					UTextureRenderTarget2D* BrushOutputNonAtlasRT = Brush.Render(true, CombinedHeightmapNonAtlasRT);

					if (BrushOutputNonAtlasRT == nullptr || BrushOutputNonAtlasRT->SizeX != CombinedHeightmapNonAtlasRT->SizeX || BrushOutputNonAtlasRT->SizeY != CombinedHeightmapNonAtlasRT->SizeY)
					{
						continue;
					}

					INC_DWORD_STAT(STAT_CyLandRegenerateProceduralHeightmapsDrawCalls); // Brush Render

					PrintDebugRTHeightmap(OutputDebugName ? FString::Printf(TEXT("LS Height: %s %s -> BrushNonAtlas %s"), *Layer.Name.ToString(), *Brush.BPCustomBrush->GetName(), *BrushOutputNonAtlasRT->GetName()) : TEXT(""), BrushOutputNonAtlasRT);

					// Resolve back to Combined heightmap
					CopyProceduralTargetToResolveTarget(BrushOutputNonAtlasRT, CombinedHeightmapNonAtlasRT, nullptr, FIntPoint(0, 0), 0);
					PrintDebugRTHeightmap(OutputDebugName ? FString::Printf(TEXT("LS Height: %s Component %s += -> CombinedNonAtlas %s"), *Layer.Name.ToString(), *BrushOutputNonAtlasRT->GetName(), *CombinedHeightmapNonAtlasRT->GetName()) : TEXT(""), CombinedHeightmapNonAtlasRT);
				}
			}

			CopyProceduralTargetToResolveTarget(CombinedHeightmapNonAtlasRT, CyLandScratchRT3, nullptr, FIntPoint(0, 0), 0);
			PrintDebugRTHeightmap(OutputDebugName ? FString::Printf(TEXT("LS Height: %s Component %s += -> CombinedNonAtlas %s"), *Layer.Name.ToString(), *CombinedHeightmapNonAtlasRT->GetName(), *CyLandScratchRT3->GetName()) : TEXT(""), CyLandScratchRT3);

			FirstLayer = false;
		}

		ShaderParams.GenerateNormals = true;
		ShaderParams.GridSize = GetRootComponent()->RelativeScale3D;

		DrawHeightmapComponentsToRenderTarget(OutputDebugName ? FString::Printf(TEXT("LS Height: %s = -> CombinedNonAtlasNormals : %s"), *CombinedHeightmapNonAtlasRT->GetName(), *CyLandScratchRT1->GetName()) : TEXT(""),
											  AllCyLandComponents, CombinedHeightmapNonAtlasRT, nullptr, CyLandScratchRT1, ERTDrawingType::RTNonAtlas, true, ShaderParams);

		ShaderParams.GenerateNormals = false;

		DrawHeightmapComponentsToRenderTarget(OutputDebugName ? FString::Printf(TEXT("LS Height: %s = -> CombinedAtlasFinal : %s"), *CyLandScratchRT1->GetName(), *CombinedHeightmapAtlasRT->GetName()) : TEXT(""),
											  AllCyLandComponents, CyLandScratchRT1, nullptr, CombinedHeightmapAtlasRT, ERTDrawingType::RTNonAtlasToAtlas, true, ShaderParams);

		DrawHeightmapComponentsToRenderTargetMips(AllCyLandComponents, CombinedHeightmapAtlasRT, true, ShaderParams);

		// Copy back all Mips to original heightmap data
		for (ACyLandProxy* CyLand : AllCyLands)
		{
			for (auto& ItPair : CyLand->RenderDataPerHeightmap)
			{
				int32 CurrentMip = 0;
				FCyRenderDataPerHeightmap& HeightmapRenderData = ItPair.Value;

				CopyProceduralTargetToResolveTarget(CombinedHeightmapAtlasRT, HeightmapRenderData.OriginalHeightmap, HeightmapRenderData.HeightmapsCPUReadBack, HeightmapRenderData.TopLeftSectionBase, CurrentMip++);

				for (int32 MipRTIndex = EHeightmapRTType::CyLandSizeMip1; MipRTIndex < EHeightmapRTType::Count; ++MipRTIndex)
				{
					if (HeightmapRTList[MipRTIndex] != nullptr)
					{
						CopyProceduralTargetToResolveTarget(HeightmapRTList[MipRTIndex], HeightmapRenderData.OriginalHeightmap, HeightmapRenderData.HeightmapsCPUReadBack, HeightmapRenderData.TopLeftSectionBase, CurrentMip++);
					}
				}
			}
		}
	}

	if ((ProceduralContentUpdateFlags & EProceduralContentUpdateFlag::Heightmap_ResolveToTexture) != 0 || (ProceduralContentUpdateFlags & EProceduralContentUpdateFlag::Heightmap_ResolveToTextureDDC) != 0)
	{
		ResolveProceduralHeightmapTexture((ProceduralContentUpdateFlags & EProceduralContentUpdateFlag::Heightmap_ResolveToTextureDDC) != 0);
	}

	if ((ProceduralContentUpdateFlags & EProceduralContentUpdateFlag::Heightmap_BoundsAndCollision) != 0)
	{
		for (UCyLandComponent* Component : AllCyLandComponents)
		{
			Component->UpdateCachedBounds();
			Component->UpdateComponentToWorld();

			Component->UpdateCollisionData(false);
		}
	}

	ProceduralContentUpdateFlags = 0;

	// If doing rendering debug, keep doing the render only
	if (CVarOutputProceduralDebugDrawCallName.GetValueOnAnyThread() == 1)
	{
		ProceduralContentUpdateFlags = EProceduralContentUpdateFlag::Heightmap_Render;
	}
}

void ACyLand::ResolveProceduralHeightmapTexture(bool InUpdateDDC)
{
	SCOPE_CYCLE_COUNTER(STAT_CyLandResolveProceduralHeightmap);

	UCyLandInfo* Info = GetCyLandInfo();

	TArray<ACyLandProxy*> AllCyLands;
	AllCyLands.Add(this);

	for (auto& It : Info->Proxies)
	{
		AllCyLands.Add(It);
	}

	TArray<UTexture2D*> PendingDDCUpdateTextureList;

	for (ACyLandProxy* CyLand : AllCyLands)
	{
		TArray<TArray<FColor>> MipData;

		for (auto& ItPair : CyLand->RenderDataPerHeightmap)
		{
			FCyRenderDataPerHeightmap& HeightmapRenderData = ItPair.Value;

			if (HeightmapRenderData.HeightmapsCPUReadBack == nullptr)
			{
				continue;
			}

			if (MipData.Num() == 0)
			{
				MipData.AddDefaulted(HeightmapRenderData.HeightmapsCPUReadBack->TextureRHI->GetNumMips());
			}

			int32 MipSizeU = HeightmapRenderData.HeightmapsCPUReadBack->GetSizeX();
			int32 MipSizeV = HeightmapRenderData.HeightmapsCPUReadBack->GetSizeY();
			int32 MipIndex = 0;

			while (MipSizeU >= 1 && MipSizeV >= 1)
			{
				MipData[MipIndex].Reset();

				FReadSurfaceDataFlags Flags(RCM_UNorm, CubeFace_MAX);
				Flags.SetMip(MipIndex);
				FIntRect Rect(0, 0, MipSizeU, MipSizeV);

				{
					TArray<FColor>* OutData = &MipData[MipIndex];
					FTextureRHIRef SourceTextureRHI = HeightmapRenderData.HeightmapsCPUReadBack->TextureRHI;
					ENQUEUE_RENDER_COMMAND(ReadSurfaceCommand)(
						[SourceTextureRHI, Rect, OutData, Flags](FRHICommandListImmediate& RHICmdList)
						{
							RHICmdList.ReadSurfaceData(SourceTextureRHI, Rect, *OutData, Flags);
						});
				}

				MipSizeU >>= 1;
				MipSizeV >>= 1;
				++MipIndex;
			}

			FlushRenderingCommands();

			for (MipIndex = 0; MipIndex < MipData.Num(); ++MipIndex)
			{
				if (MipData[MipIndex].Num() > 0)
				{
					PrintDebugHeightData(CVarOutputProceduralRTContent.GetValueOnAnyThread() == 1 ? FString::Printf(TEXT("CPUReadBack -> Source Heightmap %s, Mip: %d"), *HeightmapRenderData.OriginalHeightmap->GetName(), MipIndex) : TEXT(""),
										 MipData[MipIndex], FIntPoint(HeightmapRenderData.HeightmapsCPUReadBack->GetSizeX() >> MipIndex, HeightmapRenderData.HeightmapsCPUReadBack->GetSizeY() >> MipIndex), MipIndex, true);

					FColor* HeightmapTextureData = (FColor*)HeightmapRenderData.OriginalHeightmap->Source.LockMip(MipIndex);
					FMemory::Memzero(HeightmapTextureData, MipData[MipIndex].Num() * sizeof(FColor));
					FMemory::Memcpy(HeightmapTextureData, MipData[MipIndex].GetData(), MipData[MipIndex].Num() * sizeof(FColor));
					HeightmapRenderData.OriginalHeightmap->Source.UnlockMip(MipIndex);
				}
			}

			if (InUpdateDDC)
			{
				HeightmapRenderData.OriginalHeightmap->BeginCachePlatformData();
				HeightmapRenderData.OriginalHeightmap->ClearAllCachedCookedPlatformData();
				PendingDDCUpdateTextureList.Add(HeightmapRenderData.OriginalHeightmap);
				HeightmapRenderData.OriginalHeightmap->MarkPackageDirty();
			}
		}
	}	

	if (InUpdateDDC)
	{
		// Wait for all texture to be finished, do them async, since we can have many to update but we still need to wait for all of them to be finish before continuing
		for (UTexture* PendingDDCUpdateTexture : PendingDDCUpdateTextureList)
		{
			PendingDDCUpdateTexture->FinishCachePlatformData();

			PendingDDCUpdateTexture->Resource = PendingDDCUpdateTexture->CreateResource();
			if (PendingDDCUpdateTexture->Resource)
			{
				BeginInitResource(PendingDDCUpdateTexture->Resource);
			}
		}
	}
}

void ACyLand::RegenerateProceduralWeightmaps()
{

}

void ACyLand::RequestProceduralContentUpdate(uint32 InDataFlags)
{
	ProceduralContentUpdateFlags = InDataFlags;
}

void ACyLand::RegenerateProceduralContent()
{
	if ((ProceduralContentUpdateFlags & Heightmap_Setup) != 0 || (ProceduralContentUpdateFlags & Weightmap_Setup) != 0)
	{
		SetupProceduralLayers();
	}

	RegenerateProceduralHeightmaps();
	RegenerateProceduralWeightmaps();
}

void ACyLand::OnPreSaveWorld(uint32 SaveFlags, UWorld* World)
{
	if (GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
	{
		// Need to perform setup here, as it's possible to get here with the data not setup, when doing a Save As on a level
		if (PreviousExperimentalCyLandProcedural != GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
		{
			PreviousExperimentalCyLandProcedural = GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape;
			RequestProceduralContentUpdate(EProceduralContentUpdateFlag::All_Setup | EProceduralContentUpdateFlag::All_WithDDCUpdate);
		}
		else
		{
			RequestProceduralContentUpdate(EProceduralContentUpdateFlag::Heightmap_ResolveToTextureDDC | EProceduralContentUpdateFlag::Weightmap_ResolveToTextureDDC);
		}

		RegenerateProceduralContent();
		ProceduralContentUpdateFlags = 0; // Force reset so we don't end up performing save info at the next Tick
	}
}

void ACyLand::OnPostSaveWorld(uint32 SaveFlags, UWorld* World, bool bSuccess)
{	
}
#endif //WITH_EDITOR

#undef LOCTEXT_NAMESPACE