// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CyLandGizmoActor.h"
#include "Misc/MessageDialog.h"
#include "Misc/FileHelper.h"
#include "Misc/FeedbackContext.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineDefines.h"
#include "RHI.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "MaterialShared.h"
#include "CyLandInfo.h"
#include "Engine/Texture2D.h"
#include "CyLandLayerInfoObject.h"
#include "CyLandInfoMap.h"
#include "CyLandDataAccess.h"
#include "CyLandRender.h"
#include "CyLandGizmoActiveActor.h"
#include "CyLandGizmoRenderComponent.h"
#include "DynamicMeshBuilder.h"
#include "Engine/CollisionProfile.h"
#include "EngineUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Components/BillboardComponent.h"
#include "HAL/PlatformApplicationMisc.h"

namespace
{
	enum PreviewType
	{
		Invalid = -1,
		Both = 0,
		Add = 1,
		Sub = 2,
	};
}


class FCyLandGizmoMeshRenderProxy : public FMaterialRenderProxy
{
public:
	const FMaterialRenderProxy* const Parent;
	const float TopHeight;
	const float BottomHeight;
	const UTexture2D* AlphaTexture;
	const FLinearColor ScaleBias;
	const FMatrix WorldToCyLandMatrix;

	/** Initialization constructor. */
	FCyLandGizmoMeshRenderProxy(const FMaterialRenderProxy* InParent, const float InTop, const float InBottom, const UTexture2D* InAlphaTexture, const FLinearColor& InScaleBias, const FMatrix& InWorldToCyLandMatrix)
	:	Parent(InParent)
	,	TopHeight(InTop)
	,	BottomHeight(InBottom)
	,	AlphaTexture(InAlphaTexture)
	,	ScaleBias(InScaleBias)
	,	WorldToCyLandMatrix(InWorldToCyLandMatrix)
	{}

	// FMaterialRenderProxy interface.
	virtual const FMaterial& GetMaterialWithFallback(ERHIFeatureLevel::Type InFeatureLevel, const FMaterialRenderProxy*& OutFallbackMaterialRenderProxy) const override
	{
		return Parent->GetMaterialWithFallback(InFeatureLevel, OutFallbackMaterialRenderProxy);
	}

	virtual bool GetVectorValue(const FMaterialParameterInfo& ParameterInfo, FLinearColor* OutValue, const FMaterialRenderContext& Context) const
	{
		if (ParameterInfo.Name == FName(TEXT("AlphaScaleBias")))
		{
			*OutValue = ScaleBias;
			return true;
		}
		else
		if (ParameterInfo.Name == FName(TEXT("MatrixRow1")))
		{
			*OutValue = FLinearColor(WorldToCyLandMatrix.M[0][0], WorldToCyLandMatrix.M[0][1], WorldToCyLandMatrix.M[0][2],WorldToCyLandMatrix.M[0][3]);
			return true;
		}
		else
		if (ParameterInfo.Name == FName(TEXT("MatrixRow2")))
		{
			*OutValue = FLinearColor(WorldToCyLandMatrix.M[1][0], WorldToCyLandMatrix.M[1][1], WorldToCyLandMatrix.M[1][2],WorldToCyLandMatrix.M[1][3]);
			return true;
		}
		else
		if (ParameterInfo.Name == FName(TEXT("MatrixRow3")))
		{
			*OutValue = FLinearColor(WorldToCyLandMatrix.M[2][0], WorldToCyLandMatrix.M[2][1], WorldToCyLandMatrix.M[2][2],WorldToCyLandMatrix.M[2][3]);
			return true;
		}
		else
		if (ParameterInfo.Name == FName(TEXT("MatrixRow4")))
		{
			*OutValue = FLinearColor(WorldToCyLandMatrix.M[3][0], WorldToCyLandMatrix.M[3][1], WorldToCyLandMatrix.M[3][2],WorldToCyLandMatrix.M[3][3]);
			return true;
		}

		return Parent->GetVectorValue(ParameterInfo, OutValue, Context);
	}
	virtual bool GetScalarValue(const FMaterialParameterInfo& ParameterInfo, float* OutValue, const FMaterialRenderContext& Context) const
	{
		if (ParameterInfo.Name == FName(TEXT("Top")))
		{
			*OutValue = TopHeight;
			return true;
		}
		else if (ParameterInfo.Name == FName(TEXT("Bottom")))
		{
			*OutValue = BottomHeight;
			return true;
		}
		return Parent->GetScalarValue(ParameterInfo, OutValue, Context);
	}
	virtual bool GetTextureValue(const FMaterialParameterInfo& ParameterInfo,const UTexture** OutValue, const FMaterialRenderContext& Context) const
	{
		if (ParameterInfo.Name == FName(TEXT("AlphaTexture")))
		{
			// FIXME: This needs to return a black texture if AlphaTexture is NULL.
			// Returning NULL will cause the material to use GWhiteTexture.
			*OutValue = AlphaTexture;
			return true;
		}
		return Parent->GetTextureValue(ParameterInfo, OutValue, Context);
	}
};

/** Represents a CyLandGizmoRenderingComponent to the scene manager. */
class FCyLandGizmoRenderSceneProxy final : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	FVector XAxis, YAxis, Origin;
	float SampleSizeX, SampleSizeY;
	bool bHeightmapRendering;
	HHitProxy* HitProxy;
	FMatrix MeshRT;
	FVector FrustumVerts[8];
	TArray<FVector> SampledPositions;
	TArray<FVector> SampledNormals;
	FCyLandGizmoMeshRenderProxy* HeightmapRenderProxy;
	FMaterialRenderProxy* GizmoRenderProxy;

	FCyLandGizmoRenderSceneProxy(const UCyLandGizmoRenderComponent* InComponent):
		FPrimitiveSceneProxy(InComponent),
		bHeightmapRendering(false),
		HitProxy(nullptr),
		HeightmapRenderProxy(nullptr),
		GizmoRenderProxy(nullptr)
	{
#if WITH_EDITOR	
		ACyLandGizmoActiveActor* Gizmo = Cast<ACyLandGizmoActiveActor>(InComponent->GetOwner());
		if (Gizmo && Gizmo->GizmoMeshMaterial && Gizmo->GizmoDataMaterial && Gizmo->GetRootComponent())
		{
			UCyLandInfo* CyLandInfo = Gizmo->TargetCyLandInfo;
			if (CyLandInfo && CyLandInfo->GetCyLandProxy())
			{
				SampleSizeX = Gizmo->SampleSizeX;
				SampleSizeY = Gizmo->SampleSizeY;
				bHeightmapRendering = (Gizmo->DataType & CyLGT_Height);
				FTransform LToW = CyLandInfo->GetCyLandProxy()->CyLandActorToWorld();
				const float W = Gizmo->Width / 2;
				const float H = Gizmo->Height / 2;
				const float L = Gizmo->LengthZ;
				// The Gizmo's coordinate space is weird, it's partially relative to the landscape and partially relative to the world
				const FVector GizmoLocation = Gizmo->GetActorLocation();
				const FQuat   GizmoRotation = FRotator(0, Gizmo->GetActorRotation().Yaw, 0).Quaternion() * LToW.GetRotation();
				const FVector GizmoScale3D  = Gizmo->GetActorScale3D();
				const FTransform GizmoRT = FTransform(GizmoRotation, GizmoLocation, GizmoScale3D);

				FrustumVerts[0] = Gizmo->FrustumVerts[0] = GizmoRT.TransformPosition(FVector( - W, - H, + L ));
				FrustumVerts[1] = Gizmo->FrustumVerts[1] = GizmoRT.TransformPosition(FVector( + W, - H, + L ));
				FrustumVerts[2] = Gizmo->FrustumVerts[2] = GizmoRT.TransformPosition(FVector( + W, + H, + L ));
				FrustumVerts[3] = Gizmo->FrustumVerts[3] = GizmoRT.TransformPosition(FVector( - W, + H, + L ));

				FrustumVerts[4] = Gizmo->FrustumVerts[4] = GizmoRT.TransformPosition(FVector( - W, - H,   0 ));
				FrustumVerts[5] = Gizmo->FrustumVerts[5] = GizmoRT.TransformPosition(FVector( + W, - H,   0 ));
				FrustumVerts[6] = Gizmo->FrustumVerts[6] = GizmoRT.TransformPosition(FVector( + W, + H,   0 ));
				FrustumVerts[7] = Gizmo->FrustumVerts[7] = GizmoRT.TransformPosition(FVector( - W, + H,   0 ));

				XAxis  = GizmoRT.TransformPosition(FVector( + W,   0, + L ));
				YAxis  = GizmoRT.TransformPosition(FVector(   0, + H, + L ));
				Origin = GizmoRT.TransformPosition(FVector(   0,   0, + L ));

				const FMatrix WToL = LToW.ToMatrixWithScale().InverseFast();
				const FVector BaseLocation = WToL.TransformPosition(Gizmo->GetActorLocation());
				const float ScaleXY = CyLandInfo->DrawScale.X;

				MeshRT = FTranslationMatrix(FVector(-W / ScaleXY + 0.5, -H / ScaleXY + 0.5, 0) * GizmoScale3D) * FRotationTranslationMatrix(FRotator(0, Gizmo->GetActorRotation().Yaw, 0), FVector(BaseLocation.X, BaseLocation.Y, 0)) * LToW.ToMatrixWithScale();
				HeightmapRenderProxy = new FCyLandGizmoMeshRenderProxy( Gizmo->GizmoMeshMaterial->GetRenderProxy(), BaseLocation.Z + L, BaseLocation.Z, Gizmo->GizmoTexture, FLinearColor(Gizmo->TextureScale.X, Gizmo->TextureScale.Y, 0, 0), WToL );

				GizmoRenderProxy = (Gizmo->DataType != CyLGT_None) ? Gizmo->GizmoDataMaterial->GetRenderProxy() : Gizmo->GizmoMaterial->GetRenderProxy();

				// Cache sampled height
				float ScaleX = Gizmo->GetWidth() / Gizmo->CachedWidth / ScaleXY * Gizmo->CachedScaleXY;
				float ScaleY = Gizmo->GetHeight() / Gizmo->CachedHeight / ScaleXY * Gizmo->CachedScaleXY;
				FScaleMatrix Mat(FVector(ScaleX, ScaleY, L));
				FMatrix NormalM = Mat.InverseFast().GetTransposed();

				int32 SamplingSize = Gizmo->SampleSizeX * Gizmo->SampleSizeY;
				SampledPositions.Empty(SamplingSize);
				SampledNormals.Empty(SamplingSize);

				for (int32 Y = 0; Y < Gizmo->SampleSizeY; ++Y)
				{
					for (int32 X = 0; X < Gizmo->SampleSizeX; ++X)
					{
						FVector SampledPos = Gizmo->SampledHeight[X + Y * ACyLandGizmoActiveActor::DataTexSize];
						SampledPos.X *= ScaleX;
						SampledPos.Y *= ScaleY;
						SampledPos.Z = Gizmo->GetCyLandHeight(SampledPos.Z);

						FVector SampledNormal = NormalM.TransformVector(Gizmo->SampledNormal[X + Y * ACyLandGizmoActiveActor::DataTexSize]);
						SampledNormal = SampledNormal.GetSafeNormal();

						SampledPositions.Add(SampledPos);
						SampledNormals.Add(SampledNormal);
						//MeshBuilder.AddVertex(SampledPos, FVector2D((float)X / (Gizmo->SampleSizeX), (float)Y / (Gizmo->SampleSizeY)), TangentX, SampledNormal^TangentX, SampledNormal, FColor::White );
					}
				}
			}
		}
#endif
	}

	~FCyLandGizmoRenderSceneProxy()
	{
		delete HeightmapRenderProxy;
		HeightmapRenderProxy = NULL;
	}

#if WITH_EDITOR
	virtual HHitProxy* CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy> >& OutHitProxies) override
	{
		ACyLandGizmoActiveActor* Gizmo = CastChecked<ACyLandGizmoActiveActor>(Component->GetOwner());
		HitProxy = new HTranslucentActor(Gizmo, Component);
		OutHitProxies.Add(HitProxy);

		// by default we're not clickable, to allow the preview heightmap to be non-clickable (only the bounds frame)
		return nullptr;
	}
#endif

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		//FMemMark Mark(FMemStack::Get());
#if WITH_EDITOR
		if( GizmoRenderProxy &&  HeightmapRenderProxy )
		{
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				if (VisibilityMap & (1 << ViewIndex))
				{
					const FSceneView* View = Views[ViewIndex];
					FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);

					// Axis
					PDI->DrawLine( Origin, XAxis, FLinearColor(1, 0, 0), SDPG_World );
					PDI->DrawLine( Origin, YAxis, FLinearColor(0, 1, 0), SDPG_World );

					{
						FDynamicMeshBuilder MeshBuilder(View->GetFeatureLevel());

						const FColor GizmoColor = FColor::White;
						MeshBuilder.AddVertex(FrustumVerts[0], FVector2D(0, 0), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);
						MeshBuilder.AddVertex(FrustumVerts[1], FVector2D(1, 0), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);
						MeshBuilder.AddVertex(FrustumVerts[2], FVector2D(1, 1), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);
						MeshBuilder.AddVertex(FrustumVerts[3], FVector2D(0, 1), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);

						MeshBuilder.AddVertex(FrustumVerts[4], FVector2D(0, 0), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);
						MeshBuilder.AddVertex(FrustumVerts[5], FVector2D(1, 0), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);
						MeshBuilder.AddVertex(FrustumVerts[6], FVector2D(1, 1), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);
						MeshBuilder.AddVertex(FrustumVerts[7], FVector2D(0, 1), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);

						MeshBuilder.AddVertex(FrustumVerts[1], FVector2D(0, 0), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);
						MeshBuilder.AddVertex(FrustumVerts[0], FVector2D(1, 0), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);
						MeshBuilder.AddVertex(FrustumVerts[4], FVector2D(1, 1), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);
						MeshBuilder.AddVertex(FrustumVerts[5], FVector2D(0, 1), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);

						MeshBuilder.AddVertex(FrustumVerts[3], FVector2D(0, 0), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);
						MeshBuilder.AddVertex(FrustumVerts[2], FVector2D(1, 0), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);
						MeshBuilder.AddVertex(FrustumVerts[6], FVector2D(1, 1), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);
						MeshBuilder.AddVertex(FrustumVerts[7], FVector2D(0, 1), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);

						MeshBuilder.AddVertex(FrustumVerts[2], FVector2D(0, 0), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);
						MeshBuilder.AddVertex(FrustumVerts[1], FVector2D(1, 0), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);
						MeshBuilder.AddVertex(FrustumVerts[5], FVector2D(1, 1), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);
						MeshBuilder.AddVertex(FrustumVerts[6], FVector2D(0, 1), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);

						MeshBuilder.AddVertex(FrustumVerts[0], FVector2D(0, 0), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);
						MeshBuilder.AddVertex(FrustumVerts[3], FVector2D(1, 0), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);
						MeshBuilder.AddVertex(FrustumVerts[7], FVector2D(1, 1), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);
						MeshBuilder.AddVertex(FrustumVerts[4], FVector2D(0, 1), FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), GizmoColor);

						for (int32 i = 0; i < 6; ++i)
						{
							int32 Idx = i*4;
							MeshBuilder.AddTriangle( Idx, Idx+2, Idx+1 );
							MeshBuilder.AddTriangle( Idx, Idx+3, Idx+2 );
						}

						MeshBuilder.GetMesh(FMatrix::Identity, GizmoRenderProxy, SDPG_World, true, false, false, ViewIndex, Collector, HitProxy);
					}

					if (bHeightmapRendering)
					{
						FDynamicMeshBuilder MeshBuilder(View->GetFeatureLevel());

						for (int32 Y = 0; Y < SampleSizeY; ++Y)
						{
							for (int32 X = 0; X < SampleSizeX; ++X)
							{
								FVector SampledNormal = SampledNormals[X + Y * SampleSizeX];
								FVector TangentX(SampledNormal.Z, 0, -SampledNormal.X);
								TangentX = TangentX.GetSafeNormal();

								MeshBuilder.AddVertex(SampledPositions[X + Y * SampleSizeX], FVector2D((float)X / (SampleSizeX), (float)Y / (SampleSizeY)), TangentX, SampledNormal^TangentX, SampledNormal, FColor::White);
							}
						}

						for (int32 Y = 0; Y < SampleSizeY; ++Y)
						{
							for (int32 X = 0; X < SampleSizeX; ++X)
							{
								if (X < SampleSizeX - 1 && Y < SampleSizeY - 1)
								{
									MeshBuilder.AddTriangle( (X+0) + (Y+0) * SampleSizeX, (X+1) + (Y+1) * SampleSizeX, (X+1) + (Y+0) * SampleSizeX );
									MeshBuilder.AddTriangle( (X+0) + (Y+0) * SampleSizeX, (X+0) + (Y+1) * SampleSizeX, (X+1) + (Y+1) * SampleSizeX );
								}
							}
						}

						MeshBuilder.GetMesh(MeshRT, HeightmapRenderProxy , SDPG_World, false, false, ViewIndex, Collector);
					}
				}
			}
		}
#endif
	};

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;
#if WITH_EDITOR
		const bool bVisible = View->Family->EngineShowFlags.Landscape;
		Result.bDrawRelevance = IsShown(View) && bVisible && !View->bIsGameView && GCyLandEditRenderMode & ECyLandEditRenderMode::Gizmo;
		Result.bDynamicRelevance = true;
		// ideally the TranslucencyRelevance should be filled out by the material, here we do it conservative
		Result.bSeparateTranslucencyRelevance = Result.bNormalTranslucencyRelevance = true;
#endif
		return Result;
	}

	virtual uint32 GetMemoryFootprint( void ) const override { return( sizeof( *this ) + GetAllocatedSize() ); }
	uint32 GetAllocatedSize( void ) const { return( FPrimitiveSceneProxy::GetAllocatedSize() ); }
};

UCyLandGizmoRenderComponent::UCyLandGizmoRenderComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bHiddenInGame = true;
	bIsEditorOnly = true;
	SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
}

FPrimitiveSceneProxy* UCyLandGizmoRenderComponent::CreateSceneProxy()
{
	return new FCyLandGizmoRenderSceneProxy(this);
}

void UCyLandGizmoRenderComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const 
{
#if WITH_EDITORONLY_DATA
	ACyLandGizmoActiveActor* Gizmo = Cast<ACyLandGizmoActiveActor>(GetOwner());
	if (Gizmo)
	{
		UMaterialInterface* GizmoMat = (Gizmo->DataType != CyLGT_None) ?
			(UMaterialInterface*)Gizmo->GizmoDataMaterial :
			(UMaterialInterface*)Gizmo->GizmoMaterial;

		if (GizmoMat)
		{
			OutMaterials.Add(GizmoMat);
		}
	}
#endif
}

FBoxSphereBounds UCyLandGizmoRenderComponent::CalcBounds(const FTransform& LocalToWorld) const
{
#if WITH_EDITOR
	ACyLandGizmoActiveActor* Gizmo = Cast<ACyLandGizmoActiveActor>(GetOwner());
	if (Gizmo)
	{
		UCyLandInfo* CyLandInfo = Gizmo->TargetCyLandInfo;
		if (CyLandInfo && CyLandInfo->GetCyLandProxy())
		{
			FTransform LToW = CyLandInfo->GetCyLandProxy()->CyLandActorToWorld();

			// We calculate this ourselves, not from Gizmo->FrustrumVerts, as those haven't been updated yet
			// The Gizmo's coordinate space is weird, it's partially relative to the landscape and partially relative to the world
			const FVector GizmoLocation = Gizmo->GetActorLocation();
			const FQuat   GizmoRotation = FRotator(0, Gizmo->GetActorRotation().Yaw, 0).Quaternion() * LToW.GetRotation();
			const FVector GizmoScale3D = Gizmo->GetActorScale3D();
			const FTransform GizmoRT = FTransform(GizmoRotation, GizmoLocation, GizmoScale3D);
			const float W = Gizmo->Width / 2;
			const float H = Gizmo->Height / 2;
			const float L = Gizmo->LengthZ;
			return FBoxSphereBounds(FBox(FVector(-W, -H, 0), FVector(+W, +H, +L))).TransformBy(GizmoRT);
		}
	}
#endif

	return Super::CalcBounds(LocalToWorld);
}

ACyLandGizmoActor::ACyLandGizmoActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	SpriteComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (!IsRunningCommandlet() && (SpriteComponent != nullptr))
	{
		// Structure to hold one-time initialization
		struct FConstructorStatics
		{
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> DecalActorIconTexture;
			FName ID_Misc;
			FText NAME_Misc;
			FConstructorStatics()
				: DecalActorIconTexture(TEXT("Texture2D'/Engine/EditorResources/S_DecalActorIcon.S_DecalActorIcon'"))
				, ID_Misc(TEXT("Misc"))
				, NAME_Misc(NSLOCTEXT("SpriteCategory", "Misc", "Misc"))
			{
			}
		};
		static FConstructorStatics ConstructorStatics;

		SpriteComponent->Sprite = ConstructorStatics.DecalActorIconTexture.Get();
		SpriteComponent->RelativeScale3D = FVector(0.5f, 0.5f, 0.5f);
		SpriteComponent->bHiddenInGame = true;
		SpriteComponent->SpriteInfo.Category = ConstructorStatics.ID_Misc;
		SpriteComponent->SpriteInfo.DisplayName = ConstructorStatics.NAME_Misc;
		SpriteComponent->bIsScreenSizeScaled = true;
	}
#endif

	USceneComponent* SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent0"));
	RootComponent = SceneComponent;

#if WITH_EDITORONLY_DATA
	bEditable = false;
	Width = 1280.0f;
	Height = 1280.0f;
	LengthZ = 1280.0f;
	MarginZ = 512.0f;
	MinRelativeZ = 0.0f;
	RelativeScaleZ = 1.0f;
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR

void ACyLandGizmoActor::Duplicate(ACyLandGizmoActor* Gizmo)
{
	Gizmo->Width = Width;
	Gizmo->Height = Height;
	Gizmo->LengthZ = LengthZ;
	Gizmo->MarginZ = MarginZ;
	//Gizmo->TargetCyLandInfo = TargetCyLandInfo;

	Gizmo->SetActorLocation( GetActorLocation(), false );
	Gizmo->SetActorRotation( GetActorRotation() );

	if( Gizmo->GetRootComponent() != NULL && GetRootComponent() != NULL )
	{
		Gizmo->GetRootComponent()->SetRelativeScale3D( GetRootComponent()->RelativeScale3D );
	}

	Gizmo->MinRelativeZ = MinRelativeZ;
	Gizmo->RelativeScaleZ = RelativeScaleZ;

	Gizmo->ReregisterAllComponents();
}
#endif	//WITH_EDITOR

ACyLandGizmoActiveActor::ACyLandGizmoActiveActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer
		.DoNotCreateDefaultSubobject(TEXT("Sprite"))
	)
{
#if WITH_EDITORONLY_DATA
	if (!IsRunningCommandlet())
	{
		// Structure to hold one-time initialization
		struct FConstructorStatics
		{
			ConstructorHelpers::FObjectFinder<UMaterial> CyLandGizmo_Mat;
			ConstructorHelpers::FObjectFinder<UMaterialInstanceConstant> CyLandGizmo_Mat_Copied;
			ConstructorHelpers::FObjectFinder<UMaterial> CyLandGizmoHeight_Mat;
			FConstructorStatics()
				: CyLandGizmo_Mat(TEXT("/Engine/EditorLandscapeResources/LandscapeGizmo_Mat"))
				, CyLandGizmo_Mat_Copied(TEXT("/Engine/EditorLandscapeResources/LandscapeGizmo_Mat_Copied"))
				, CyLandGizmoHeight_Mat(TEXT("/Engine/EditorLandscapeResources/LandscapeGizmoHeight_Mat"))
			{
			}
		};
		static FConstructorStatics ConstructorStatics;

		GizmoMaterial = ConstructorStatics.CyLandGizmo_Mat.Object;
		GizmoDataMaterial = ConstructorStatics.CyLandGizmo_Mat_Copied.Object;
		GizmoMeshMaterial = ConstructorStatics.CyLandGizmoHeight_Mat.Object;
	}
#endif // WITH_EDITORONLY_DATA

	UCyLandGizmoRenderComponent* CyLandGizmoRenderComponent = CreateDefaultSubobject<UCyLandGizmoRenderComponent>(TEXT("GizmoRendererComponent0"));
	CyLandGizmoRenderComponent->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);

	RootComponent = CyLandGizmoRenderComponent;
#if WITH_EDITORONLY_DATA
	bEditable = true;
	Width = 1280.0f;
	Height = 1280.0f;
	LengthZ = 1280.0f;
	MarginZ = 512.0f;
	DataType = CyLGT_None;
	SampleSizeX = 0;
	SampleSizeY = 0;
	CachedWidth = 1.0f;
	CachedHeight = 1.0f;
	CachedScaleXY = 1.0f;
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR
void ACyLandGizmoActiveActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	if( PropertyName == FName(TEXT("LengthZ")) )
	{
		if (LengthZ < 0)
		{
			LengthZ = MarginZ;
		}
	}
	else if ( PropertyName == FName(TEXT("TargetCyLandInfo")) )
	{
		SetTargetCyLand(TargetCyLandInfo);
	}
	// AActor::PostEditChange will ForceUpdateComponents()
	Super::PostEditChangeProperty(PropertyChangedEvent);
}


void ACyLandGizmoActiveActor::PostEditMove(bool bFinished)
{
	Super::PostEditMove( bFinished );

	if (bFinished)
	{
		UnsnappedRotation = FRotator::ZeroRotator;
	}
}

FVector ACyLandGizmoActiveActor::SnapToCyLandGrid(const FVector& GizmoLocation) const
{
	check(TargetCyLandInfo);
	const FTransform LToW = TargetCyLandInfo->GetCyLandProxy()->CyLandActorToWorld();
	const FVector CyLandSpaceLocation = LToW.InverseTransformPosition(GizmoLocation);
	const FVector SnappedCyLandSpaceLocation = CyLandSpaceLocation.GridSnap(1);
	const FVector ResultLocation = LToW.TransformPosition(SnappedCyLandSpaceLocation);
	return ResultLocation;
}

void ACyLandGizmoActiveActor::EditorApplyTranslation(const FVector& DeltaTranslation, bool bAltDown, bool bShiftDown, bool bCtrlDown)
{
	if (bSnapToCyLandGrid)
	{
		const FVector GizmoLocation = GetActorLocation() + DeltaTranslation;
		const FVector ResultLocation = SnapToCyLandGrid(GizmoLocation);

		SetActorLocation(ResultLocation, false);
	}
	else
	{
		Super::EditorApplyTranslation(DeltaTranslation, bAltDown, bShiftDown, bCtrlDown);
	}

	ReregisterAllComponents();
}

FRotator ACyLandGizmoActiveActor::SnapToCyLandGrid(const FRotator& GizmoRotation) const
{
	// Snap to multiples of 90 Yaw in landscape coordinate system
	//check(TargetCyLandInfo && TargetCyLandInfo->CyLandProxy);
	//const FTransform LToW = TargetCyLandInfo->CyLandProxy->ActorToWorld();
	//const FRotator CyLandSpaceRotation = (LToW.GetRotation().InverseFast() * GizmoRotation.Quaternion()).Rotator().GetNormalized();
	//const FRotator SnappedCyLandSpaceRotation = FRotator(0, FMath::GridSnap(CyLandSpaceRotation.Yaw, 90), 0);
	//const FRotator ResultRotation = (SnappedCyLandSpaceRotation.Quaternion() * LToW.GetRotation()).Rotator().GetNormalized();

	// Gizmo rotation is used as if it was relative to the landscape even though it isn't, so snap in world space
	const FRotator ResultRotation = FRotator(0, FMath::GridSnap(GizmoRotation.Yaw, 90), 0);
	return ResultRotation;
}

void ACyLandGizmoActiveActor::EditorApplyRotation(const FRotator& DeltaRotation, bool bAltDown, bool bShiftDown, bool bCtrlDown)
{
	if (bSnapToCyLandGrid)
	{
		// Based on AActor::EditorApplyRotation
		FRotator GizmoRotation = GetActorRotation() + UnsnappedRotation;
		FRotator Winding, Remainder;
		GizmoRotation.GetWindingAndRemainder(Winding, Remainder);
		const FQuat ActorQ = Remainder.Quaternion();
		const FQuat DeltaQ = DeltaRotation.Quaternion();
		const FQuat ResultQ = DeltaQ * ActorQ;
		const FRotator NewActorRotRem = FRotator( ResultQ );
		FRotator DeltaRot = NewActorRotRem - Remainder;
		DeltaRot.Normalize();

		GizmoRotation += DeltaRot;

		const FRotator ResultRotation = SnapToCyLandGrid(GizmoRotation);

		UnsnappedRotation = GizmoRotation - ResultRotation;
		UnsnappedRotation.Pitch = 0;
		UnsnappedRotation.Roll = 0;
		UnsnappedRotation.Normalize();

		SetActorRotation(ResultRotation);
	}
	else
	{
		Super::EditorApplyRotation(DeltaRotation, bAltDown, bShiftDown, bCtrlDown);
	}

	ReregisterAllComponents();
}

ACyLandGizmoActor* ACyLandGizmoActiveActor::SpawnGizmoActor()
{
	// ACyLandGizmoActor is history for ACyLandGizmoActiveActor
	ACyLandGizmoActor* NewActor = GetWorld()->SpawnActor<ACyLandGizmoActor>();
	Duplicate(NewActor);
	return NewActor;
}

void ACyLandGizmoActiveActor::SetTargetCyLand(UCyLandInfo* CyLandInfo)
{
	UCyLandInfo* PrevInfo = TargetCyLandInfo;
	if (!CyLandInfo || CyLandInfo->HasAnyFlags(RF_BeginDestroyed))
	{
		TargetCyLandInfo = nullptr;
		if (GetWorld())
		{
			for (const TPair<FGuid, UCyLandInfo*>& InfoMapPair : UCyLandInfoMap::GetCyLandInfoMap(GetWorld()).Map)
			{
				UCyLandInfo* CandidateInfo = InfoMapPair.Value;
				if (CandidateInfo && !CandidateInfo->HasAnyFlags(RF_BeginDestroyed) && CandidateInfo->GetCyLandProxy() != nullptr)
				{
					TargetCyLandInfo = CandidateInfo;
					break;
				}
			}
		}
	}
	else
	{
		TargetCyLandInfo = CyLandInfo;
	}

	// if there's no copied data, try to move somewhere useful
	if (TargetCyLandInfo && TargetCyLandInfo != PrevInfo && DataType == CyLGT_None)
	{
		MarginZ = TargetCyLandInfo->DrawScale.Z * 3;
		Width = Height = TargetCyLandInfo->DrawScale.X * (TargetCyLandInfo->ComponentSizeQuads+1);

		float NewLengthZ;
		FVector NewLocation = TargetCyLandInfo->GetCyLandCenterPos(NewLengthZ);
		SetLength(NewLengthZ);
		SetActorLocation( NewLocation, false );
		SetActorRotation(FRotator::ZeroRotator);
	}

	ReregisterAllComponents();
}

void ACyLandGizmoActiveActor::ClearGizmoData()
{
	DataType = CyLGT_None;
	SelectedData.Empty();
	LayerInfos.Empty();

	// If the clipboard contains copied gizmo data, clear it also
	FString ClipboardString;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardString);
	const TCHAR* Str = *ClipboardString;
	if (FParse::Command(&Str, TEXT("GizmoData=")))
	{
		FPlatformApplicationMisc::ClipboardCopy(TEXT(""));
	}

	ReregisterAllComponents();
}

void ACyLandGizmoActiveActor::FitToSelection()
{
	if (TargetCyLandInfo)
	{
		// Find fit size
		int32 MinX = MAX_int32, MinY = MAX_int32;
		int32 MaxX = MIN_int32, MaxY = MIN_int32;
		TargetCyLandInfo->GetSelectedExtent(MinX, MinY, MaxX, MaxY);
		if (MinX != MAX_int32)
		{
			float ScaleXY = TargetCyLandInfo->DrawScale.X;
			Width = ScaleXY * (MaxX - MinX + 1) / (GetRootComponent()->RelativeScale3D.X);
			Height = ScaleXY * (MaxY - MinY + 1) / (GetRootComponent()->RelativeScale3D.Y);
			float NewLengthZ;
			FVector NewLocation = TargetCyLandInfo->GetCyLandCenterPos(NewLengthZ, MinX, MinY, MaxX, MaxY);
			SetLength(NewLengthZ);
			SetActorLocation(NewLocation, false);
			SetActorRotation(FRotator::ZeroRotator);
			// Reset Z render scale values...
			MinRelativeZ = 0.f;
			RelativeScaleZ = 1.f;
			ReregisterAllComponents();
		}
	}
}

void ACyLandGizmoActiveActor::FitMinMaxHeight()
{
	if (TargetCyLandInfo)
	{
		float MinZ = HALF_WORLD_MAX, MaxZ = -HALF_WORLD_MAX;
		// Change MinRelativeZ and RelativeZScale to fit Gizmo Box
		for (auto It = SelectedData.CreateConstIterator(); It; ++It )
		{
			const FCyGizmoSelectData& Data = It.Value();
			MinZ = FMath::Min(MinZ, Data.HeightData);
			MaxZ = FMath::Max(MaxZ, Data.HeightData);
		}

		if (MinZ != HALF_WORLD_MAX && MaxZ > MinZ + KINDA_SMALL_NUMBER)
		{
			MinRelativeZ = MinZ;
			RelativeScaleZ = 1.f / (MaxZ - MinZ);
			ReregisterAllComponents();
		}
	}
}

float ACyLandGizmoActiveActor::GetNormalizedHeight(uint16 CyLandHeight) const
{
	if (TargetCyLandInfo)
	{
		ACyLandProxy* Proxy = TargetCyLandInfo->GetCyLandProxy();
		if (Proxy)
		{
			// Need to make it scale...?
			float ZScale = GetLength();
			if (ZScale > KINDA_SMALL_NUMBER)
			{
				FVector LocalGizmoPos = Proxy->CyLandActorToWorld().InverseTransformPosition(GetActorLocation());
				return FMath::Clamp<float>( (( CyLandDataAccess::GetLocalHeight(CyLandHeight) - LocalGizmoPos.Z) * TargetCyLandInfo->DrawScale.Z) / ZScale, 0.f, 1.f );
			}
		}
	}
	return 0.f;
}

float ACyLandGizmoActiveActor::GetWorldHeight(float NormalizedHeight) const
{
	if (TargetCyLandInfo)
	{
		ACyLandProxy* Proxy = TargetCyLandInfo->GetCyLandProxy();
		if (Proxy)
		{
			float ZScale = GetLength();
			if (ZScale > KINDA_SMALL_NUMBER)
			{
				FVector LocalGizmoPos = Proxy->CyLandActorToWorld().InverseTransformPosition(GetActorLocation());
				return NormalizedHeight * ZScale + LocalGizmoPos.Z * TargetCyLandInfo->DrawScale.Z;
			}
		}
	}
	return 0.f;
}

float ACyLandGizmoActiveActor::GetCyLandHeight(float NormalizedHeight) const
{
	if (TargetCyLandInfo)
	{
		NormalizedHeight = (NormalizedHeight - MinRelativeZ) * RelativeScaleZ;
		float ScaleZ = TargetCyLandInfo->DrawScale.Z;
		return (GetWorldHeight(NormalizedHeight) / ScaleZ);
	}
	return 0.f;
}

void ACyLandGizmoActiveActor::CalcNormal()
{
	int32 SquaredDataTex = DataTexSize * DataTexSize;
	if (SampledHeight.Num() == SquaredDataTex && SampleSizeX > 0 && SampleSizeY > 0 )
	{
		if (SampledNormal.Num() != SquaredDataTex)
		{
			SampledNormal.Empty(SquaredDataTex);
			SampledNormal.AddZeroed(SquaredDataTex);
		}
		for (int32 Y = 0; Y < SampleSizeY-1; ++Y)
		{
			for (int32 X = 0; X < SampleSizeX-1; ++X)
			{
				FVector Vert00 = SampledHeight[X + Y*DataTexSize];
				FVector Vert01 = SampledHeight[X + (Y+1)*DataTexSize];
				FVector Vert10 = SampledHeight[X+1 + Y*DataTexSize];
				FVector Vert11 = SampledHeight[X+1 + (Y+1)*DataTexSize];

				FVector FaceNormal1 = ((Vert00-Vert10) ^ (Vert10-Vert11)).GetSafeNormal();
				FVector FaceNormal2 = ((Vert11-Vert01) ^ (Vert01-Vert00)).GetSafeNormal(); 

				// contribute to the vertex normals.
				SampledNormal[X + Y*DataTexSize] += FaceNormal1;
				SampledNormal[X + (Y+1)*DataTexSize] += FaceNormal2;
				SampledNormal[X+1 + Y*DataTexSize] += FaceNormal1 + FaceNormal2;
				SampledNormal[X+1 + (Y+1)*DataTexSize] += FaceNormal1 + FaceNormal2;
			}
		}
		for (int32 Y = 0; Y < SampleSizeY; ++Y)
		{
			for (int32 X = 0; X < SampleSizeX; ++X)
			{
				SampledNormal[X + Y*DataTexSize] = SampledNormal[X + Y*DataTexSize].GetSafeNormal();
			}
		}
	}
}

void ACyLandGizmoActiveActor::SampleData(int32 SizeX, int32 SizeY)
{
	if (TargetCyLandInfo && GizmoTexture)
	{
		// Rasterize rendering Texture...
		int32 TexSizeX = FMath::Min(ACyLandGizmoActiveActor::DataTexSize, SizeX);
		int32 TexSizeY = FMath::Min(ACyLandGizmoActiveActor::DataTexSize, SizeY);
		SampleSizeX = TexSizeX;
		SampleSizeY = TexSizeY;

		// Update Data Texture...
		//DataTexture->SetFlags(RF_Transactional);
		//DataTexture->Modify();

		TextureScale = FVector2D( (float)SizeX / FMath::Max(ACyLandGizmoActiveActor::DataTexSize, SizeX), (float)SizeY / FMath::Max(ACyLandGizmoActiveActor::DataTexSize, SizeY));
		uint8* TexData = GizmoTexture->Source.LockMip(0);
		int32 GizmoTexSizeX = GizmoTexture->Source.GetSizeX();
		for (int32 Y = 0; Y < TexSizeY; ++Y)
		{
			for (int32 X = 0; X < TexSizeX; ++X)
			{
				float TexX = static_cast<float>(X) * SizeX / TexSizeX;
				float TexY = static_cast<float>(Y) * SizeY / TexSizeY;
				int32 LX = FMath::FloorToInt(TexX);
				int32 LY = FMath::FloorToInt(TexY);

				float FracX = TexX - LX;
				float FracY = TexY - LY;

				FCyGizmoSelectData* Data00 = SelectedData.Find(FIntPoint(LX, LY));
				FCyGizmoSelectData* Data10 = SelectedData.Find(FIntPoint(LX+1, LY));
				FCyGizmoSelectData* Data01 = SelectedData.Find(FIntPoint(LX, LY+1));
				FCyGizmoSelectData* Data11 = SelectedData.Find(FIntPoint(LX+1, LY+1));

				// Invert Tex Data to show selected region more visible
				TexData[X + Y*GizmoTexSizeX] = 255 - FMath::Lerp(
					FMath::Lerp(Data00 ? Data00->Ratio : 0, Data10 ? Data10->Ratio : 0, FracX),
					FMath::Lerp(Data01 ? Data01->Ratio : 0, Data11 ? Data11->Ratio : 0, FracX),
					FracY
					) * 255;

				if (DataType & CyLGT_Height)
				{
					float NormalizedHeight = FMath::Lerp(
						FMath::Lerp(Data00 ? Data00->HeightData : 0, Data10 ? Data10->HeightData : 0, FracX),
						FMath::Lerp(Data01 ? Data01->HeightData : 0, Data11 ? Data11->HeightData : 0, FracX),
						FracY
						);

					SampledHeight[X + Y*GizmoTexSizeX] = FVector(LX, LY, NormalizedHeight);
				}
			}
		}

		if (DataType & CyLGT_Height)
		{
			CalcNormal();
		}

		GizmoTexture->TemporarilyDisableStreaming();
		FUpdateTextureRegion2D Region(0, 0, 0, 0, TexSizeX, TexSizeY);
		GizmoTexture->UpdateTextureRegions(0, 1, &Region, GizmoTexSizeX, sizeof(uint8), TexData);
		FlushRenderingCommands();
		GizmoTexture->Source.UnlockMip(0);

		ReregisterAllComponents();
	}
}

CYLAND_API void ACyLandGizmoActiveActor::Import( int32 VertsX, int32 VertsY, uint16* HeightData, TArray<UCyLandLayerInfoObject*> ImportLayerInfos, uint8* LayerDataPointers[] )
{
	if (VertsX <= 0 || VertsY <= 0 || HeightData == NULL || TargetCyLandInfo == NULL || GizmoTexture == NULL || (ImportLayerInfos.Num() && !LayerDataPointers) )
	{
		return;
	}

	GWarn->BeginSlowTask( NSLOCTEXT("CyLand", "BeginImportingGizmoDataTask", "Importing Gizmo Data"), true);

	ClearGizmoData();

	CachedScaleXY = TargetCyLandInfo->DrawScale.X;
	CachedWidth = CachedScaleXY * VertsX; // (DrawScale * DrawScale3D.X);
	CachedHeight = CachedScaleXY * VertsY; // (DrawScale * DrawScale3D.Y);
	
	float CurrentWidth = GetWidth();
	float CurrentHeight = GetHeight();
	LengthZ = GetLength();

	FVector Scale3D = FVector(CurrentWidth / CachedWidth, CurrentHeight / CachedHeight, 1.f);
	GetRootComponent()->SetRelativeScale3D(Scale3D);

	Width = CachedWidth;
	Height = CachedHeight;

	DataType = ECyLandGizmoType(DataType | CyLGT_Height);
	if (ImportLayerInfos.Num())
	{
		DataType = ECyLandGizmoType(DataType | CyLGT_Weight);
	}

	for (int32 Y = 0; Y < VertsY; ++Y)
	{
		for (int32 X = 0; X < VertsX; ++X)
		{
			FCyGizmoSelectData Data;
			Data.Ratio = 1.f;
			Data.HeightData = (float)HeightData[X + Y*VertsX] / 65535.f; //GetNormalizedHeight(HeightData[X + Y*VertsX]);
			for (int32 i = 0; i < ImportLayerInfos.Num(); ++i)
			{
				Data.WeightDataMap.Add( ImportLayerInfos[i], LayerDataPointers[i][X + Y*VertsX] );
			}
			SelectedData.Add(FIntPoint(X, Y), Data);
		}
	}

	SampleData(VertsX, VertsY);

	for (auto It = ImportLayerInfos.CreateConstIterator(); It; ++It)
	{
		LayerInfos.Add(*It);
	}

	GWarn->EndSlowTask();

	ReregisterAllComponents();
}

void ACyLandGizmoActiveActor::Export(int32 Index, TArray<FString>& Filenames)
{
	//guard around case where landscape has no layer structs
	if (Filenames.Num() == 0)
	{
		return;
	}

	bool bExportOneTarget = (Filenames.Num() == 1);

	if (TargetCyLandInfo)
	{
		int32 MinX = MAX_int32, MinY = MAX_int32;
		int32 MaxX = MIN_int32, MaxY = MIN_int32;
		for (const TPair<FIntPoint, FCyGizmoSelectData>& SelectedDataPair : SelectedData)
		{
			const FIntPoint Key = SelectedDataPair.Key;
			if (MinX > Key.X) MinX = Key.X;
			if (MaxX < Key.X) MaxX = Key.X;
			if (MinY > Key.Y) MinY = Key.Y;
			if (MaxY < Key.Y) MaxY = Key.Y;
		}

		if (MinX != MAX_int32)
		{
			GWarn->BeginSlowTask( NSLOCTEXT("CyLand", "BeginExportingGizmoDataTask", "Exporting Gizmo Data"), true);

			TArray<uint8> HeightData;
			if (!bExportOneTarget || Index == -1)
			{
				HeightData.AddZeroed((1+MaxX-MinX)*(1+MaxY-MinY)*sizeof(uint16));
			}
			uint16* pHeightData = (uint16*)HeightData.GetData();

			TArray<TArray<uint8> > WeightDatas;
			for( int32 i=1;i<Filenames.Num();i++ )
			{
				TArray<uint8> WeightData;
				if (!bExportOneTarget || Index == i-1)
				{
					WeightData.AddZeroed((1+MaxX-MinX)*(1+MaxY-MinY));
				}
				WeightDatas.Add(WeightData);
			}

			for (int32 Y = MinY; Y <= MaxY; ++Y)
			{
				for (int32 X = MinX; X <= MaxX; ++X)
				{
					const FCyGizmoSelectData* Data = SelectedData.Find(FIntPoint(X, Y));
					if (Data)
					{
						int32 Idx = (X-MinX) + Y *(1+MaxX-MinX);
						if (!bExportOneTarget || Index == -1)
						{
							pHeightData[Idx] = FMath::Clamp<uint16>(Data->HeightData * 65535.f, 0, 65535);
						}

						for( int32 i=1;i<Filenames.Num();i++ )
						{
							if (!bExportOneTarget || Index == i-1)
							{
								TArray<uint8>& WeightData = WeightDatas[i-1];
								WeightData[Idx] = FMath::Clamp<uint8>(Data->WeightDataMap.FindRef(LayerInfos[i-1]), 0, 255);
							}
						}
					}
				}
			}

			if (!bExportOneTarget || Index == -1)
			{
				FFileHelper::SaveArrayToFile(HeightData,*Filenames[0]);
			}

			for( int32 i=1;i<Filenames.Num();i++ )
			{
				if (!bExportOneTarget || Index == i-1)
				{
					FFileHelper::SaveArrayToFile(WeightDatas[i-1],*Filenames[bExportOneTarget ? 0 : i]);
				}
			}

			GWarn->EndSlowTask();
		}
		else
		{
			FMessageDialog::Open( EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "CyLandGizmoExport_Warning", "CyLand Gizmo has no copyed data. You need to choose proper targets and copy it to Gizmo."));
		}
	}
}

void ACyLandGizmoActiveActor::ExportToClipboard()
{
	if (TargetCyLandInfo && DataType != CyLGT_None)
	{
		//GWarn->BeginSlowTask( TEXT("Exporting Gizmo Data From Clipboard"), true);

		FString ClipboardString(TEXT("GizmoData="));

		ClipboardString += FString::Printf(TEXT(" Type=%d,TextureScaleX=%g,TextureScaleY=%g,SampleSizeX=%d,SampleSizeY=%d,CachedWidth=%g,CachedHeight=%g,CachedScaleXY=%g "), 
			(int32)DataType, TextureScale.X, TextureScale.Y, SampleSizeX, SampleSizeY, CachedWidth, CachedHeight, CachedScaleXY);

		for (int32 Y = 0; Y < SampleSizeY; ++Y )
		{
			for (int32 X = 0; X < SampleSizeX; ++X)
			{
				FVector& V = SampledHeight[X + Y * DataTexSize];
				ClipboardString += FString::Printf(TEXT("%d %d %d "), (int32)V.X, (int32)V.Y, *(int32*)(&V.Z) );
			}
		}

		ClipboardString += FString::Printf(TEXT("LayerInfos= "));

		for (UCyLandLayerInfoObject* LayerInfo : LayerInfos)
		{
			ClipboardString += FString::Printf(TEXT("%s "), *LayerInfo->GetPathName() );
		}

		ClipboardString += FString::Printf(TEXT("Region= "));

		for (const TPair<FIntPoint, FCyGizmoSelectData>& SelectedDataPair : SelectedData)
		{
			const FIntPoint Key = SelectedDataPair.Key;
			const FCyGizmoSelectData& Data = SelectedDataPair.Value;
			ClipboardString += FString::Printf(TEXT("%d %d %d %d %d "), Key.X, Key.Y, *(int32*)(&Data.Ratio), *(int32*)(&Data.HeightData), Data.WeightDataMap.Num());

			for (const TPair<UCyLandLayerInfoObject*, float>& WeightDataPair : Data.WeightDataMap)
			{
				ClipboardString += FString::Printf(TEXT("%d %d "), LayerInfos.Find(WeightDataPair.Key), *(int32*)(&WeightDataPair.Value));
			}
		}

		FPlatformApplicationMisc::ClipboardCopy(*ClipboardString);

		//GWarn->EndSlowTask();
	}
}

#define MAX_GIZMO_PROP_TEXT_LENGTH			1024*1024*8

void ACyLandGizmoActiveActor::ImportFromClipboard()
{
	FString ClipboardString;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardString);
	const TCHAR* Str = *ClipboardString;
	
	if(FParse::Command(&Str,TEXT("GizmoData=")))
	{
		int32 ClipBoardSize = ClipboardString.Len();
		if (ClipBoardSize > MAX_GIZMO_PROP_TEXT_LENGTH)
		{
			if( EAppReturnType::Yes != FMessageDialog::Open( EAppMsgType::YesNo,
				FText::Format(NSLOCTEXT("UnrealEd", "CyLandGizmoImport_Warning", "CyLand Gizmo is about to import large amount data ({0}MB) from the clipboard, which will take some time. Do you want to proceed?"),
				FText::AsNumber(ClipBoardSize >> 20) ) ) )
			{
				return;
			}
		}

		GWarn->BeginSlowTask( NSLOCTEXT("CyLand", "BeginImportingGizmoDataFromClipboardTask", "Importing Gizmo Data From Clipboard"), true);

		FParse::Next(&Str);


		int32 ReadNum = 0;

		uint8 Type = 0;
		ReadNum += FParse::Value(Str, TEXT("Type="), Type) ? 1 : 0;
		DataType = (ECyLandGizmoType)Type;

		ReadNum += FParse::Value(Str, TEXT("TextureScaleX="), TextureScale.X) ? 1 : 0;
		ReadNum += FParse::Value(Str, TEXT("TextureScaleY="), TextureScale.Y) ? 1 : 0;
		ReadNum += FParse::Value(Str, TEXT("SampleSizeX="), SampleSizeX) ? 1 : 0;
		ReadNum += FParse::Value(Str, TEXT("SampleSizeY="), SampleSizeY) ? 1 : 0;
		ReadNum += FParse::Value(Str, TEXT("CachedWidth="), CachedWidth) ? 1 : 0;
		ReadNum += FParse::Value(Str, TEXT("CachedHeight="), CachedHeight) ? 1 : 0;
		ReadNum += FParse::Value(Str, TEXT("CachedScaleXY="), CachedScaleXY) ? 1 : 0;

		if (ReadNum > 0)
		{
			while (!FChar::IsWhitespace(*Str))
			{
				Str++;
			}
			FParse::Next(&Str);

			int32 SquaredDataTex = DataTexSize * DataTexSize;
			if (SampledHeight.Num() != SquaredDataTex)
			{
				SampledHeight.Empty(SquaredDataTex);
				SampledHeight.AddZeroed(SquaredDataTex);
			}

			// For Sample Height...
			TCHAR* StopStr;
			for (int32 Y = 0; Y < SampleSizeY; ++Y )
			{
				for (int32 X = 0; X < SampleSizeX; ++X)
				{
					FVector& V = SampledHeight[X + Y * DataTexSize];
					V.X = FCString::Strtoi(Str, &StopStr, 10);
					while (!FChar::IsWhitespace(*Str))
					{
						Str++;
					}
					FParse::Next(&Str);
					V.Y = FCString::Strtoi(Str, &StopStr, 10);
					while (!FChar::IsWhitespace(*Str))
					{
						Str++;
					}
					FParse::Next(&Str);
					//V.Z = FCString::Atof(Str);
					*((int32*)(&V.Z)) = FCString::Strtoi(Str, &StopStr, 10);
					while (!FChar::IsWhitespace(*Str))
					{
						Str++;
					}
					FParse::Next(&Str);
				}
			}

			CalcNormal();

			TCHAR StrBuf[1024];
			if(FParse::Command(&Str,TEXT("LayerInfos=")))
			{
				while( !FParse::Command(&Str,TEXT("Region=")) )
				{
					FParse::Next(&Str);
					int32 i = 0;
					while (!FChar::IsWhitespace(*Str))
					{
						StrBuf[i++] = *Str;
						Str++;
					}
					StrBuf[i] = 0;
					LayerInfos.Add( LoadObject<UCyLandLayerInfoObject>(NULL, StrBuf) );
				}
			}

			//if(FParse::Command(&Str,TEXT("Region=")))
			{
				while (*Str)
				{
					FParse::Next(&Str);
					int32 X, Y, LayerNum;
					FCyGizmoSelectData Data;
					X = FCString::Strtoi(Str, &StopStr, 10);
					while (!FChar::IsWhitespace(*Str))
					{
						Str++;
					}
					FParse::Next(&Str);
					Y = FCString::Strtoi(Str, &StopStr, 10);
					while (!FChar::IsWhitespace(*Str))
					{
						Str++;
					}
					FParse::Next(&Str);
					*((int32*)(&Data.Ratio)) = FCString::Strtoi(Str, &StopStr, 10);
					while (!FChar::IsWhitespace(*Str))
					{
						Str++;
					}
					FParse::Next(&Str);
					*((int32*)(&Data.HeightData)) = FCString::Strtoi(Str, &StopStr, 10);
					while (!FChar::IsWhitespace(*Str))
					{
						Str++;
					}
					FParse::Next(&Str);
					LayerNum = FCString::Strtoi(Str, &StopStr, 10);
					while (!FChar::IsWhitespace(*Str))
					{
						Str++;
					}
					FParse::Next(&Str);
					for (int32 i = 0; i < LayerNum; ++i)
					{
						int32 LayerIndex = FCString::Strtoi(Str, &StopStr, 10);
						while (!FChar::IsWhitespace(*Str))
						{
							Str++;
						}
						FParse::Next(&Str);
						float Weight;
						*((int32*)(&Weight)) = FCString::Strtoi(Str, &StopStr, 10);
						while (!FChar::IsWhitespace(*Str))
						{
							Str++;
						}
						FParse::Next(&Str);
						Data.WeightDataMap.Add(LayerInfos[LayerIndex], Weight);
					}
					SelectedData.Add(FIntPoint(X, Y), Data);
				}
			}
		}

		GWarn->EndSlowTask();

		ReregisterAllComponents();
	}
}
#endif	//WITH_EDITOR

#if WITH_EDITORONLY_DATA
/** Returns SpriteComponent subobject **/
UBillboardComponent* ACyLandGizmoActor::GetSpriteComponent() const { return SpriteComponent; }
#endif
