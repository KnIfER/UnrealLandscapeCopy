/**
 * Material utilities
 */


#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"





class UCyLandComponent;












class CYLAND_API FMUtils
{
public:
	/**
* Creates bakes textures for a ULandscapeComponent
*
* @param LandscapeComponent		The component to bake textures for
* @return						Whether operation was successful
*/
	static bool ExportBaseColor(UCyLandComponent* LandscapeComponent, int32 TextureSize, TArray<FColor>& OutSamples);





	/**
	 * Generates a texture from an array of samples
	 *
	 * @param Outer					Outer for the material and texture objects, if NULL new packages will be created for each asset
	 * @param AssetLongName			Long asset path for the new texture
	 * @param Size					Resolution of the texture to generate (must match the number of samples)
	 * @param Samples				Color data for the texture
	 * @param CompressionSettings	Compression settings for the new texture
	 * @param LODGroup				LODGroup for the new texture
	 * @param Flags					ObjectFlags for the new texture
	 * @param bSRGB					Whether to set the bSRGB flag on the new texture
	 * @param SourceGuidHash		(optional) Hash (stored as Guid) to use part of the texture source's DDC key.
	 * @return						The new texture.
	 */
	static UTexture2D* CreateTexture(UPackage* Outer, const FString& AssetLongName, FIntPoint Size, const TArray<FColor>& Samples, TextureCompressionSettings CompressionSettings, TextureGroup LODGroup, EObjectFlags Flags, bool bSRGB, const FGuid& SourceGuidHash = FGuid());




};