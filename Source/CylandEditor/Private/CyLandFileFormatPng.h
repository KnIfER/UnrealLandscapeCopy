// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "CyLandFileFormatInterface.h"

// Implement .png file format
class FCyLandHeightmapFileFormat_Png : public ICyLandHeightmapFileFormat
{
private:
	FCyLandFileTypeInfo FileTypeInfo;

public:
	FCyLandHeightmapFileFormat_Png();

	virtual const FCyLandFileTypeInfo& GetInfo() const override
	{
		return FileTypeInfo;
	}

	virtual FCyLandHeightmapInfo Validate(const TCHAR* HeightmapFilename) const override;
	virtual FCyLandHeightmapImportData Import(const TCHAR* HeightmapFilename, FCyLandFileResolution ExpectedResolution) const override;
	virtual void Export(const TCHAR* HeightmapFilename, TArrayView<const uint16> Data, FCyLandFileResolution DataResolution, FVector Scale) const override;
};

//////////////////////////////////////////////////////////////////////////

class FCyLandWeightmapFileFormat_Png : public ICyLandWeightmapFileFormat
{
private:
	FCyLandFileTypeInfo FileTypeInfo;

public:
	FCyLandWeightmapFileFormat_Png();

	virtual const FCyLandFileTypeInfo& GetInfo() const override
	{
		return FileTypeInfo;
	}

	virtual FCyLandWeightmapInfo Validate(const TCHAR* WeightmapFilename, FName LayerName) const override;
	virtual FCyLandWeightmapImportData Import(const TCHAR* WeightmapFilename, FName LayerName, FCyLandFileResolution ExpectedResolution) const override;
	virtual void Export(const TCHAR* WeightmapFilename, FName LayerName, TArrayView<const uint8> Data, FCyLandFileResolution DataResolution) const override;
};
