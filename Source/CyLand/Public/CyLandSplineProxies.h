// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/UObjectGlobals.h"
#include "HitProxies.h"

class UCyLandSplineSegment;

//////////////////////////////////////////////////////////////////////////
// LANDSCAPE SPLINES HIT PROXY

struct HCyLandSplineProxy : public HHitProxy
{
	DECLARE_HIT_PROXY( CYLAND_API );

	HCyLandSplineProxy(EHitProxyPriority InPriority = HPP_Wireframe) :
		HHitProxy(InPriority)
	{
	}
	virtual EMouseCursor::Type GetMouseCursor() override
	{
		return EMouseCursor::Crosshairs;
	}
};

struct HCyLandSplineProxy_Segment : public HCyLandSplineProxy
{
	DECLARE_HIT_PROXY( CYLAND_API );

	class UCyLandSplineSegment* SplineSegment;

	HCyLandSplineProxy_Segment(class UCyLandSplineSegment* InSplineSegment) :
		HCyLandSplineProxy(),
		SplineSegment(InSplineSegment)
	{
	}
	virtual void AddReferencedObjects( FReferenceCollector& Collector ) override
	{
		Collector.AddReferencedObject( SplineSegment );
	}
};

struct HCyLandSplineProxy_ControlPoint : public HCyLandSplineProxy
{
	DECLARE_HIT_PROXY( CYLAND_API );

	class UCyLandSplineControlPoint* ControlPoint;

	HCyLandSplineProxy_ControlPoint(class UCyLandSplineControlPoint* InControlPoint) :
		HCyLandSplineProxy(HPP_Foreground),
		ControlPoint(InControlPoint)
	{
	}
	virtual void AddReferencedObjects( FReferenceCollector& Collector ) override
	{
		Collector.AddReferencedObject( ControlPoint );
	}
};

struct HCyLandSplineProxy_Tangent : public HCyLandSplineProxy
{
	DECLARE_HIT_PROXY( CYLAND_API );

	UCyLandSplineSegment* SplineSegment;
	uint32 End:1;

	HCyLandSplineProxy_Tangent(class UCyLandSplineSegment* InSplineSegment, bool InEnd) :
		HCyLandSplineProxy(HPP_UI),
		SplineSegment(InSplineSegment),
		End(InEnd)
	{
	}
	CYLAND_API virtual void Serialize(FArchive& Ar);

	virtual EMouseCursor::Type GetMouseCursor() override
	{
		return EMouseCursor::CardinalCross;
	}

	virtual void AddReferencedObjects( FReferenceCollector& Collector ) override
	{
		Collector.AddReferencedObject( SplineSegment );
	}
};
