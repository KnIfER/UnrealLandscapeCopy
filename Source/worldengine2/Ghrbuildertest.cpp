// Fill out your copyright notice in the Description page of Project Settings.

#include "Ghrbuildertest.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/Private/InstancedStaticMesh.h"
#include "Landscape/Classes/LandscapeGrassType.h"

DEFINE_LOG_CATEGORY_STATIC(LogGhrMimic, Warning, All);

struct FGhrBuilderBase
{
	bool bHaveValidData;
	float GhrDensity;
	FVector DrawScale;
	FVector DrawLoc;
	FMatrix MyToWorld;

	FIntPoint SectionBase;
	FIntPoint MySectionOffset;
	int32 ComponentSizeQuads;
	FVector Origin;
	FVector Extent;
	FVector ComponentOrigin;

	int32 SqrtMaxInstances;

	FGhrBuilderBase(AActor* My, USceneComponent* Component, const FGrassVariety& GhrVariety, ERHIFeatureLevel::Type FeatureLevel, int32 SqrtSubsections = 1, int32 SubX = 0, int32 SubY = 0, bool bEnableDensityScaling = true)
	{
		bHaveValidData = true;

		const float DensityScale = 1.0f;
		GhrDensity = GhrVariety.GrassDensity.GetValueForFeatureLevel(FeatureLevel) * DensityScale;

		DrawScale = My->GetRootComponent()->RelativeScale3D;
		DrawLoc = My->GetActorLocation();
		MySectionOffset = FIntPoint(0,0);// My->MySectionOffset;

		SectionBase = FIntPoint(0, 0);// Component->GetSectionBase();
		ComponentSizeQuads = 7; //Component->ComponentSizeQuads;

		Origin = FVector(DrawScale.X * float(SectionBase.X), DrawScale.Y * float(SectionBase.Y), 0.0f);
		Extent = FVector(DrawScale.X * float(SectionBase.X + ComponentSizeQuads), DrawScale.Y * float(SectionBase.Y + ComponentSizeQuads), 0.0f) - Origin;

		ComponentOrigin = Origin - FVector(DrawScale.X * MySectionOffset.X, DrawScale.Y * MySectionOffset.Y, 0.0f);

		SqrtMaxInstances = FMath::CeilToInt(FMath::Sqrt(FMath::Abs(Extent.X * Extent.Y * GhrDensity / 1000.0f / 1000.0f)));

		if (SqrtMaxInstances == 0)
		{
			bHaveValidData = false;
		}
		const FRotator DrawRot = My->GetActorRotation();
		MyToWorld = My->GetRootComponent()->GetComponentTransform().ToMatrixNoScale();
		UE_LOG(LogGhrMimic, Warning, TEXT("pre check(SqrtMaxInstances > 2 * SqrtSubsections) %d %d , %f %f %f     %f  "), SqrtMaxInstances, SqrtSubsections, Extent.X , Extent.Y , GhrDensity, DrawScale.X);

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

struct FMyComponentGhrAccess
{
	FMyComponentGhrAccess()
	{}

	bool IsValid()
	{
		//return WeightData && WeightData->Num() == FMath::Square(Stride) && HeightData.Num() == FMath::Square(Stride);
		return true;
	}

	FORCEINLINE float GetHeight(int32 IdxX, int32 IdxY)
	{
		return 1;
	}
	FORCEINLINE float GetWeight(int32 IdxX, int32 IdxY)
	{
		return 1000.;
	}

	FORCEINLINE int32 GetStride()
	{
		return 2;
	}

private:
};



FMyAsyncGhrTask::FMyAsyncGhrTask(FAsyncGhrBuilder* InBuilder, const FCachedMyFoliage::FGhrCompKey& InKey, UHierarchicalInstancedStaticMeshComponent* InFoliage)
	: Builder(InBuilder)
	, Key(InKey)
	, Foliage(InFoliage)
{
}

template<uint32 Base>
static FORCEINLINE float Halton(uint32 Index)
{
	float Result = 0.0f;
	float InvBase = 1.0f / Base;
	float Fraction = InvBase;
	while (Index > 0)
	{
		Result += (Index % Base) * Fraction;
		Index /= Base;
		Fraction *= InvBase;
	}
	return Result;
}

struct FAsyncGhrBuilder : public FGhrBuilderBase
{
	FMyComponentGhrAccess GhrData;
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

	bool UseMyLightmap;
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

	FAsyncGhrBuilder(AActor* My, USceneComponent* Component, const ULandscapeGrassType* GhrType, const FGrassVariety& GhrVariety, ERHIFeatureLevel::Type FeatureLevel, UHierarchicalInstancedStaticMeshComponent* HierarchicalInstancedStaticMeshComponent, int32 SqrtSubsections, int32 SubX, int32 SubY, uint32 InHaltonBaseIndex, TArray<FBox>& InExcludedBoxes)
		: FGhrBuilderBase(My, Component, GhrVariety, FeatureLevel, SqrtSubsections, SubX, SubY, GhrType->bEnableDensityScaling)
		, GhrData()
		, Scaling(GhrVariety.Scaling)
		, ScaleX(GhrVariety.ScaleX)
		, ScaleY(GhrVariety.ScaleY)
		, ScaleZ(GhrVariety.ScaleZ)
		, RandomRotation(GhrVariety.RandomRotation)
		, RandomScale(GhrVariety.ScaleX.Size() > 0 || GhrVariety.ScaleY.Size() > 0 || GhrVariety.ScaleZ.Size() > 0)
		, AlignToSurface(GhrVariety.AlignToSurface)
		, PlacementJitter(GhrVariety.PlacementJitter)
		, RandomStream(HierarchicalInstancedStaticMeshComponent->InstancingRandomSeed)
		, XForm(MyToWorld* HierarchicalInstancedStaticMeshComponent->GetComponentTransform().ToMatrixWithScale().Inverse())
		, MeshBox(GhrVariety.GrassMesh->GetBounds().GetBox())
		, DesiredInstancesPerLeaf(HierarchicalInstancedStaticMeshComponent->DesiredInstancesPerLeaf())

		, BuildTime(0)
		, TotalInstances(0)
		, HaltonBaseIndex(InHaltonBaseIndex)

		, UseMyLightmap(GhrVariety.bUseLandscapeLightmap)
		, LightmapBaseBias(FVector2D::ZeroVector)
		, LightmapBaseScale(FVector2D::UnitVector)
		, ShadowmapBaseBias(FVector2D::ZeroVector)
		, ShadowmapBaseScale(FVector2D::UnitVector)
		, LightMapComponentBias(FVector2D::ZeroVector)
		, LightMapComponentScale(FVector2D::UnitVector)
		, RequireCPUAccess(GhrVariety.bKeepInstanceBufferCPUCopy)

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

		bHaveValidData = bHaveValidData && GhrData.IsValid();

		InstanceBuffer.SetAllowCPUAccess(RequireCPUAccess);

		check(DesiredInstancesPerLeaf > 0);

		if (UseMyLightmap)
		{
			InitMyLightmap(Component);
		}
	}

	void InitMyLightmap(USceneComponent* Component)
	{
	}

	void SetInstance(int32 InstanceIndex, const FMatrix& InXForm, float RandomFraction)
	{
		//if (UseMyLightmap)
		//{
		//	float InstanceX = InXForm.M[3][0];
		//	float InstanceY = InXForm.M[3][1];
		//
		//	FVector2D NormalizedGhrCoordinate;
		//	NormalizedGhrCoordinate.X = (InstanceX - ComponentOrigin.X) * LightMapComponentScale.X + LightMapComponentBias.X;
		//	NormalizedGhrCoordinate.Y = (InstanceY - ComponentOrigin.Y) * LightMapComponentScale.Y + LightMapComponentBias.Y;
		//
		//	FVector2D LightMapCoordinate = NormalizedGhrCoordinate * LightmapBaseScale + LightmapBaseBias;
		//	FVector2D ShadowMapCoordinate = NormalizedGhrCoordinate * ShadowmapBaseScale + ShadowmapBaseBias;
		//
		//	InstanceBuffer.SetInstance(InstanceIndex, InXForm, RandomStream.GetFraction(), LightMapCoordinate, ShadowMapCoordinate);
		//}
		//else
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
		//SCOPE_CYCLE_COUNTER(STAT_FoliageGhrAsyncBuildTime);
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
			UE_LOG(LogGhrMimic, Warning, TEXT(" NumKept %d"), NumKept);

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
		int32 IdxX1 = FMath::Clamp<int32>(X1, 0, GhrData.GetStride() - 1);
		int32 IdxY1 = FMath::Clamp<int32>(Y1, 0, GhrData.GetStride() - 1);
		int32 IdxX2 = FMath::Clamp<int32>(X2, 0, GhrData.GetStride() - 1);
		int32 IdxY2 = FMath::Clamp<int32>(Y2, 0, GhrData.GetStride() - 1);

		float LerpX = FMath::Fractional(TestX);
		float LerpY = FMath::Fractional(TestY);

		float Result = 0.0f;
		if (bWeight)
		{
			// sample
			float Sample11 = GhrData.GetWeight(IdxX1, IdxY1);
			float Sample21 = GhrData.GetWeight(IdxX2, IdxY1);
			float Sample12 = GhrData.GetWeight(IdxX1, IdxY2);
			float Sample22 = GhrData.GetWeight(IdxX2, IdxY2);

			// Bilinear interpolate
			Result = FMath::Lerp(
				FMath::Lerp(Sample11, Sample21, LerpX),
				FMath::Lerp(Sample12, Sample22, LerpX),
				LerpY);
		}

		{
			// sample
			float Sample11 = GhrData.GetHeight(IdxX1, IdxY1);
			float Sample21 = GhrData.GetHeight(IdxX2, IdxY1);
			float Sample12 = GhrData.GetHeight(IdxX1, IdxY2);
			float Sample22 = GhrData.GetHeight(IdxX2, IdxY2);

			OutLocation->X = InLocation.X - DrawScale.X * float(MySectionOffset.X);
			OutLocation->Y = InLocation.Y - DrawScale.Y * float(MySectionOffset.Y);
			// Bilinear interpolate
			OutLocation->Z = DrawScale.Z * FMath::Lerp(
				FMath::Lerp(Sample11, Sample21, LerpX),
				FMath::Lerp(Sample12, Sample22, LerpX),
				LerpY);
		}
		return Result;
	}
};


void FMyAsyncGhrTask::DoWork()
{
	Builder->Build();
}

FMyAsyncGhrTask::~FMyAsyncGhrTask()
{
	delete Builder;
}



void AGhrbuildertest::Tick(float DeltaTime)
{
	//SCOPE_CYCLE_COUNTER(STAT_VoxelWorld_Tick);

	Super::Tick(DeltaTime);


}

void AGhrbuildertest::dotest()
{
	//SCOPE_CYCLE_COUNTER(STAT_VoxelWorld_Tick);
	ERHIFeatureLevel::Type FeatureLevel = GetWorld()->Scene->GetFeatureLevel();
	auto& mGrassVariety = mGrassType->GrassVarieties[0];
	auto Component = GetRootComponent();
	FCachedMyFoliage::FGhrComp NewComp;
	NewComp.Key.BasedOn = Component;
	NewComp.Key.GhrType = mGrassType;
	NewComp.Key.SqrtSubsections = 7;
	NewComp.Key.CachedMaxInstancesPerComponent = 1000;
	NewComp.Key.SubsectionX = 0;
	NewComp.Key.SubsectionY = 0;
	NewComp.Key.NumVarieties = mGrassType->GrassVarieties.Num();
	NewComp.Key.VarietyIndex = 0;

	UHierarchicalInstancedStaticMeshComponent* HierarchicalInstancedStaticMeshComponent;
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_GrassCreateComp);
		HierarchicalInstancedStaticMeshComponent = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, FName("asd"), RF_Transient);
	}
	NewComp.Foliage = HierarchicalInstancedStaticMeshComponent;

	FoliageCache.CachedGhrComps.Add(NewComp);

	// To guarantee consistency across platforms, we force the string to be lowercase and always treat it as an ANSI string.
	int32 FolSeed = FCrc::StrCrc32(StringCast<ANSICHAR>(*FString::Printf(TEXT("%s%s%d %d %d"), *mGrassType->GetName().ToLower(), *Component->GetName().ToLower(), 0, 0, 0)).Get());
	if (FolSeed == 0)
	{
		FolSeed++;
	}
	bool bDisableDynamicShadows = false;
	HierarchicalInstancedStaticMeshComponent->SetStaticMesh(mGrassVariety.GrassMesh);
	HierarchicalInstancedStaticMeshComponent->Mobility = EComponentMobility::Static;
	HierarchicalInstancedStaticMeshComponent->MinLOD = mGrassVariety.MinLOD;
	HierarchicalInstancedStaticMeshComponent->bSelectable = false;
	HierarchicalInstancedStaticMeshComponent->bHasPerInstanceHitProxies = false;
	HierarchicalInstancedStaticMeshComponent->bReceivesDecals = mGrassVariety.bReceivesDecals;
	static FName NoCollision(TEXT("NoCollision"));
	HierarchicalInstancedStaticMeshComponent->SetCollisionProfileName(NoCollision);
	HierarchicalInstancedStaticMeshComponent->bDisableCollision = true;
	HierarchicalInstancedStaticMeshComponent->SetCanEverAffectNavigation(false);
	HierarchicalInstancedStaticMeshComponent->InstancingRandomSeed = FolSeed;
	HierarchicalInstancedStaticMeshComponent->LightingChannels = mGrassVariety.LightingChannels;
	HierarchicalInstancedStaticMeshComponent->bCastStaticShadow = false;
	HierarchicalInstancedStaticMeshComponent->CastShadow = mGrassVariety.bCastDynamicShadow && !bDisableDynamicShadows;
	HierarchicalInstancedStaticMeshComponent->bCastDynamicShadow = mGrassVariety.bCastDynamicShadow && !bDisableDynamicShadows;

	{
		//QUICK_SCOPE_CYCLE_COUNTER(STAT_GrassAttachComp);

		HierarchicalInstancedStaticMeshComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		FTransform DesiredTransform = GetRootComponent()->GetComponentTransform();
		//DesiredTransform.RemoveScaling();
		//HierarchicalInstancedStaticMeshComponent->SetWorldTransform(DesiredTransform);
		//
		FoliageComponents.Add(HierarchicalInstancedStaticMeshComponent);
	}
	//auto HierarchicalInstancedStaticMeshComponent2 = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, FName("asd"), RF_Transient);
	//HierarchicalInstancedStaticMeshComponent2->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	//HierarchicalInstancedStaticMeshComponent2->SetStaticMesh(mGrassVariety.GrassMesh);
	//HierarchicalInstancedStaticMeshComponent2->RegisterComponent();
	//HierarchicalInstancedStaticMeshComponent2->AddInstance(FTransform(FVector(100, 100, 100)));
	
	FAsyncGhrBuilder* Builder = new FAsyncGhrBuilder(this, GetRootComponent(), mGrassType, mGrassVariety, FeatureLevel, HierarchicalInstancedStaticMeshComponent, 7, 0, 0, 0, NewComp.ExcludedBoxes);
	HierarchicalInstancedStaticMeshComponent->RegisterComponent();
	//HierarchicalInstancedStaticMeshComponent->AddInstance(FTransform(FVector(100, 100, 100)));
	Builder->Build();
	{
		//SCOPE_CYCLE_COUNTER(STAT_FoliageGrassEndComp);
		//FCyAsyncGrassTask& Inner = Task->GetTask();
		//AsyncFoliageTasks.RemoveAtSwap(Index--);
		//UHierarchicalInstancedStaticMeshComponent* HierarchicalInstancedStaticMeshComponent = Inner.Foliage.Get();
		//int32 NumBuiltRenderInstances = Inner.Builder->InstanceBuffer.GetNumInstances();
		int32 NumBuiltRenderInstances = Builder->InstanceBuffer.GetNumInstances();
		//UE_LOG(LogCore, Display, TEXT("%d instances in %4.0fms     %6.0f instances / sec"), NumBuiltRenderInstances, 1000.0f * float(Inner.Builder->BuildTime), float(NumBuiltRenderInstances) / float(Inner.Builder->BuildTime));

		if (HierarchicalInstancedStaticMeshComponent)
		{
			if (NumBuiltRenderInstances > 0)
			{
				QUICK_SCOPE_CYCLE_COUNTER(STAT_FoliageGrassEndComp_AcceptPrebuiltTree);

				if (!HierarchicalInstancedStaticMeshComponent->PerInstanceRenderData.IsValid())
				{
					HierarchicalInstancedStaticMeshComponent->InitPerInstanceRenderData(true, &Builder->InstanceBuffer, Builder->RequireCPUAccess);
				}
				else
				{
					HierarchicalInstancedStaticMeshComponent->PerInstanceRenderData->UpdateFromPreallocatedData(Builder->InstanceBuffer);
				}

				HierarchicalInstancedStaticMeshComponent->AcceptPrebuiltTree(Builder->ClusterTree, Builder->OutOcclusionLayerNum, NumBuiltRenderInstances);
				if (GetWorld())//bForceSync && 
				{
					QUICK_SCOPE_CYCLE_COUNTER(STAT_FoliageGrassEndComp_SyncUpdate);
					HierarchicalInstancedStaticMeshComponent->RecreateRenderState_Concurrent();
				}
			}
		}
		FCachedMyFoliage::FGhrComp* Existing = FoliageCache.CachedGhrComps.Find(NewComp.Key);
		if (Existing)
		{
			Existing->Pending = false;
			if (Existing->PreviousFoliage.IsValid())
			{
				//SCOPE_CYCLE_COUNTER(STAT_FoliageGrassDestoryComp);
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
		//delete Task;
		//if (!bForceSync)
		//{
		//	break; // one per frame is fine
		//}
	}
}