// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories.h"

class FCyLandSplineTextObjectFactory : protected FCustomizableTextObjectFactory
{
public:
	FCyLandSplineTextObjectFactory(FFeedbackContext* InWarningContext = GWarn);

	TArray<UObject*> ImportSplines(UObject* InParent, const TCHAR* TextBuffer);

protected:
	TArray<UObject*> OutObjects;

	virtual void ProcessConstructedObject(UObject* CreatedObject) override;
	virtual bool CanCreateClass(UClass* ObjectClass, bool& bOmitSubObjs) const override;
};
