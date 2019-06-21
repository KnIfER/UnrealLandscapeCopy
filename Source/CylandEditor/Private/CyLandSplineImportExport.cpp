// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CyLandSplineImportExport.h"
#include "CyLandSplineSegment.h"
#include "CyLandSplineControlPoint.h"

#define LOCTEXT_NAMESPACE "CyLand"

FCyLandSplineTextObjectFactory::FCyLandSplineTextObjectFactory(FFeedbackContext* InWarningContext /*= GWarn*/)
	: FCustomizableTextObjectFactory(InWarningContext)
{
}

TArray<UObject*> FCyLandSplineTextObjectFactory::ImportSplines(UObject* InParent, const TCHAR* TextBuffer)
{
	if (FParse::Command(&TextBuffer, TEXT("BEGIN SPLINES")))
	{
		ProcessBuffer(InParent, RF_Transactional, TextBuffer);

		//FParse::Command(&TextBuffer, TEXT("END SPLINES"));
	}

	return OutObjects;
}

void FCyLandSplineTextObjectFactory::ProcessConstructedObject(UObject* CreatedObject)
{
	OutObjects.Add(CreatedObject);

	CreatedObject->PostEditImport();
}

bool FCyLandSplineTextObjectFactory::CanCreateClass(UClass* ObjectClass, bool& bOmitSubObjs) const
{
	if (ObjectClass == UCyLandSplineControlPoint::StaticClass() ||
		ObjectClass == UCyLandSplineSegment::StaticClass())
	{
		return true;
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
