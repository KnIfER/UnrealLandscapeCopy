# Unreal Landscape Module Copy
Landscape -> CyLand  
LandscapeEditor -> CyLandEditor
### Utimate goal:
- FOR PLANET
### Main drawbacks: 
1. Editor tool icons are lost .
2. ExportMapping() and another method decleared in the LandscapeLight.h are defined in the StaticLightingExport.cpp in the UnrealEd module, so in the end I think you still need to compile the entire engine 

### Procedural Landscape at play mode runtime:
- [Forum blueprint procedural landscape](https://forums.unrealengine.com/community/community-content-tools-and-tutorials/1557162-blueprint-powered-procedural-terrain-generation-is-now-possible-in-ue4-4-20) and original [implementation](https://github.com/hippowombat/BPTerrainGen)

