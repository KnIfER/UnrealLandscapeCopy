// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CyLandSplineSegment.h"

class UCyLandInfo;
class UCyLandLayerInfoObject;

namespace CyLandSplineRaster
{
#if WITH_EDITOR
	bool FixSelfIntersection(TArray<FCyLandSplineInterpPoint>& Points, FVector FCyLandSplineInterpPoint::* Side);

	void Pointify(const FInterpCurveVector& SplineInfo, TArray<FCyLandSplineInterpPoint>& OutPoints, int32 NumSubdivisions,
		float StartFalloffFraction, float EndFalloffFraction,
		const float StartWidth, const float EndWidth,
		const float StartSideFalloff, const float EndSideFalloff,
		const float StartRollDegrees, const float EndRollDegrees);

	void RasterizeSegmentPoints(UCyLandInfo* CyLandInfo, TArray<FCyLandSplineInterpPoint> Points, const FTransform& SplineToWorld, bool bRaiseTerrain, bool bLowerTerrain, UCyLandLayerInfoObject* LayerInfo);
#endif
}
