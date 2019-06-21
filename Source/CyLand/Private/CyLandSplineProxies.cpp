// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CyLandSplineProxies.h"
#include "CyLandSplineSegment.h"

CYLAND_API void HCyLandSplineProxy_Tangent::Serialize(FArchive& Ar)
{
	Ar << SplineSegment;
}
