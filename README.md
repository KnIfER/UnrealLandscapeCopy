# Unreal Landscape Module Copy
Landscape -> CyLand  
LandscapeEditor -> CyLandEditor
### Utimate goal:
- FOR PLANET
### Main drawbacks: 
1. Some editor icons are lost . 
2. Relationships to other engine modules are broken, so in the end I think you still need to modify and compile the entire engine    
3. ExportMapping() and another method decleared in the LandscapeLight.h are defined in the StaticLightingExport.cpp in the UnrealEd module, so static lighting is not supported.
4. Does **not** support hotload, it will crash the engine.

### Procedural Landscape at play mode runtime:
- [Forum blueprint procedural landscape](https://forums.unrealengine.com/community/community-content-tools-and-tutorials/1557162-blueprint-powered-procedural-terrain-generation-is-now-possible-in-ue4-4-20) and original [implementation](https://github.com/hippowombat/BPTerrainGen)
- Fixed hierachical grass. The landscape material and grasses used are from the KiteDemo, and they are too big to be uploaded.
- Generating one Landscape of 1km(1017components) at runtime will take about 0.2 seconds. Then generating a area of 100km*100km **at once** will take at least 30 minutes(acctually an out-of-memory error will occur)!
- Not tested in packaged game.