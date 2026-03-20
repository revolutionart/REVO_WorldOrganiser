# REVO World Organiser

**Version:** 1.0.0  
**Engine:** Unreal Engine 5.7  
**Author:** Revolutionart  
**Website:** [https://revolutionart.nl/](https://revolutionart.nl/)

An advanced Unreal Engine 5.7 editor plugin for selecting and organizing actors within volumes. This powerful tool streamlines level design workflows with intelligent filtering, automatic organization, and batch operations.

---

## 🌟 Features

### Core Selection Features
- **Volume-Based Selection** - Select actors inside custom REVSelectionVolume actors with visible wireframe bounds
- **Multiple Selection Modes**
  - **Contains Fully** - Actors must be ~75% inside the volume
  - **Intersects** - Any overlap with volume bounds
  - **Pivot Inside** - Actor pivot point must be inside volume
- **Component vs Actor Bounds** - Toggle between using individual component bounds or full actor bounds

### Advanced Filtering
- **11 Actor Type Filters**
  - Static Mesh Actors
  - Blueprint Actors
  - Light Actors
  - Skeletal Mesh Actors
  - Decal Actors
  - Level Instances
  - Niagara Actors (VFX)
  - Volumes
  - Audio Actors
  - Cameras
  - Blueprints with Lights (includes nested child actors)
  - Text3D Actors

- **Class-Based Filtering**
  - Filter by specific actor class
  - Filter by component class
  - Auto-scanned from current level

- **Tag Utilities**
  - Replace a tag across the entire level
  - Remove a tag across the entire level
  - Find actors with a selected tag

- **Filter Logic**: OR between type/class filters, AND between volume bounds and filters

### Organization Tools
- **Folder Management**
  - Move actors to custom folder paths
  - Browse existing folders with tree view
  - Auto-organize by actor type into subfolders
  - Create folders on-the-fly by typing path
  - Remove actors from folders (type "None" or clear path)
- **Data Layer Management**
  - Assign actors to data layers
  - Remove actors from all data layers
- **Utility Functions**
  - Remove empty folders from World Outliner (batch of 10)

### Mirror Actors
- **Transform Actors** - Mirror selected actors around world origin (0,0,0)
- **Multi-Axis Support** - Mirror on X, Y, and/or Z axes independently
- **Duplicate Mode** - Optionally duplicate before mirroring (non-destructive)
- **Auto-Organization** - Organize duplicates into target folder
- **Preserves Relative Positions** - Linear transformation maintains spatial relationships
- **Smart Rotation Handling**
  - Regular meshes: Scale-based mirroring with rotation transform
  - Lights: Vector-based rotation to preserve light direction
  - Blueprint lights: Automatic light component rotation adjustment

### Workflow Features
- **Preview Mode** - Count matching actors before selection
- **Preset System** - Save/load/remove filter configurations as JSON
- **Volume Management**
  - Spawn empty volumes with one click
  - Use selected volume from World Outliner
  - Dropdown picker with auto-refresh
- **Undo Support** - All organization operations support Ctrl+Z

---

## 📦 Installation

### Method 1: Marketplace Install (Recommended)
1. Install from Unreal Engine Marketplace
2. Enable plugin in Edit → Plugins → Editor category
3. Restart Unreal Editor

### Method 2: Manual Install
1. Download the plugin
2. Copy `SelectActorsInVolume` folder to:
   - **Project Plugin**: `YourProject/Plugins/`
   - **Engine Plugin**: `UE_5.7/Engine/Plugins/`
3. Enable plugin in Edit → Plugins → Editor category
4. Restart Unreal Editor

### Verification
After restart, you should see:
- **Toolbar Button**: "Select In Volume" dropdown button in Level Editor toolbar
- **Editor Mode**: "Organisation" mode available (opens toolkit panel)

---

## 🚀 Quick Start

### Basic Volume Selection
1. Place a **REVSelectionVolume** actor in your level
   - Click toolbar → "Spawn Empty Volume"
   - Or add manually: Place Actors panel → Volumes → REVSelectionVolume
2. Scale the volume to encompass desired actors
3. Open **Organisation** toolkit (Editor Modes panel)
4. Click "Use Selected" to set volume as target
5. Check desired actor type filters (e.g., "Static Mesh Actors")
6. Click **"Preview Selection"** to see count
7. Click **"Select Actors"** to select them

### Organizing Actors
1. After selecting actors (or using volume-based selection)
2. **For Folder Organization**:
   - Type folder path (e.g., `ENV/Buildings/Exterior`)
   - Or click "Browse" to select existing folder
   - Check "Move to Folder"
   - Optional: Check "Create sub-folders by Actor Type"
3. **For Data Layer Assignment**:
   - Choose data layer from "Move to Data Layer" dropdown
4. Click **"Move / Organize"**
   - Confirmation dialog will show actor count
   - Click "Yes" to apply changes

### Saving Presets
1. Configure your filters and settings
2. Type a name in "Preset Name" field
3. Click "Save Preset"
4. Presets saved to: `YourProject/Saved/SelectActorsInVolume/Presets/`

---

## 📖 Detailed Feature Guide

### Selection Modes Explained

**Contains Fully**
- Requires at least 75% of actor bounds inside volume
- Best for: Selecting only actors truly inside a region
- Example: Select props inside a room, excluding doorway items

**Intersects**
- Selects actors with any overlap with volume
- Best for: Catching all actors touching a region
- Example: Select all actors on or near a platform

**Pivot Inside**
- Only checks if actor pivot point (origin) is inside volume
- Best for: Lights, cameras, actors with invalid bounds
- Example: Select lights illuminating an area

### Component Bounds Mode
- **Unchecked (Default)**: Uses full actor bounding box
- **Checked**: Uses individual component bounds
- Useful for: Actors with complex hierarchies or multiple mesh components

### Actor Type Filters

| Filter | Description | Use Case |
|--------|-------------|----------|
| Static Mesh Actors | Non-movable geometry | Environment assets, architecture |
| Blueprint Actors | Custom gameplay actors | Interactive objects, logic actors |
| Light Actors | Standalone lights | Point lights, spot lights, directional lights |
| Skeletal Mesh Actors | Animated characters | Characters, creatures |
| Decal Actors | Projected textures | Stains, graffiti, surface details |
| Level Instances | Embedded level instances | Repeated level sections |
| Niagara Actors | Particle systems | VFX, particles, environmental effects |
| Volumes | Trigger/blocking volumes | Gameplay volumes, collision |
| Audio Actors | Sound emitters | Ambient sounds, audio components |
| Cameras | Camera actors | Cinematic cameras, viewpoints |
| Blueprints w/ Lights | Blueprints containing lights | Light fixtures, lamp BPs |
| Text3D Actors | 3D text rendering | Signs, labels |

### Class-Based Filtering

**Include Filters** (AND logic):
- Select specific actor class from dropdown
- Select specific component class from dropdown
- Actor must match BOTH if both are specified
- Auto-populated from classes in current level

**Examples**:
- Include: Actor Class = "BP_Furniture_Base" → Select only furniture blueprints
- Include Actor + Component: Select "BP_Lamp_Base" with "SpotLightComponent" → Only lamp blueprints with spot lights

### Tag Utilities

- Replace a tag across the entire level
- Remove a tag across the entire level
- Find and select actors with a tag

**Special Tag Filtering**: Tags starting with "GnIdentifier" are automatically excluded from dropdowns (internal tags)

### Legacy (Removed) Filters

The following filters were available in earlier builds and are now removed from the plugin UI and selection logic:

- **Exclude Actor Classes**: Excluded actors whose class matched any class in the list.
- **Exclude Component Classes**: Excluded actors that contained any component class in the list.
- **Include Tags**: Included actors that had at least one of the specified tags.
- **Exclude Tags**: Excluded actors that had any of the specified tags.

### Folder Organization

**Folder Path Format**:
- Use forward slashes: `ENV/Buildings/Exterior`
- Create nested folders: `Level_01/Area_A/Props`
- Case-sensitive

**Special Actions**:
- Type `None` or clear path to remove actors from folders
- "Browse" button shows tree view of existing folders (including empty folders)
- Folders created automatically if they don't exist

**Organize by Actor Type**:
- Creates subfolders under target path
- Subfolder names: `StaticMeshes`, `Blueprints`, `Lights`, `SkeletalMeshes`, `VFX`, `Volumes`, `Cameras`, `Audio`, `Decals`, `LevelInstances`, `Text3D`, `Other`
- Example: Target path `Props` with type organization:
  - Static meshes → `Props/StaticMeshes`
  - Blueprints → `Props/Blueprints`
  - etc.

**Remove Empty Folders**:
- Deletes folders with 0 actors
- Processes 10 folders per click (prevents crashes with large hierarchies)
- Click multiple times to remove all empty folders
- Deletes deepest folders first (prevents parent-child reference issues)
- Shows remaining count after each batch

### Mirror Actors Feature

**How It Works**:
- Mirrors actors around **world origin (0,0,0)**, NOT around selection center
- Each actor mirrored individually preserves relative positions (linear transformation)
- Only transforms position and rotation - preserves scale (except for mirror axis)

**Mirror Axis Selection**:
- **X-axis**: Mirror across YZ plane (X=0)
- **Y-axis**: Mirror across XZ plane (Y=0)
- **Z-axis**: Mirror across XY plane (Z=0) ⚠️ May place actors below ground
- Can select multiple axes for compound mirroring

**Duplicate Mode**:
- **Checked**: Creates copies before mirroring (originals unchanged)
- **Unchecked**: Mirrors actors in-place (destructive)
- "Organize duplicates" option: Places duplicates in "Move to Folder" path

**Smart Rotation Handling**:
- Regular meshes: Negates scale on perpendicular axis + rotation transform
- Standalone lights: Vector-based rotation to prevent direction flip
- Blueprint lights: Automatic component rotation adjustment

**Example**:
```
Original actor at (8120, 440, 1800)
After X-axis mirror → (-8120, 440, 1800)
Relative distance from origin preserved
```

### Preset System

**What's Saved**:
- Selection mode (Contains Fully/Intersects/Pivot Inside)
- Component bounds mode
- Actor type mask (all checkboxes)
- Class picker settings (actor/component classes)
- Folder settings (path, organize by type, etc.)
- Data layer reference

**What's NOT Saved**:
- Target volume actor (intentionally - volume selection is level-specific)

**Preset Management**:
- Presets stored as JSON in `ProjectSaved/SelectActorsInVolume/Presets/`
- "Default" preset cannot be removed
- Load preset from dropdown menu
- Remove button only enabled for non-Default presets

---

## 💡 Tips & Best Practices

### Performance Tips
1. **Use Preview** before large selections to verify filter setup
2. **Narrow filters** for better performance (combine type + class)
3. **Remove empty folders** in batches of 10 to prevent hangs
4. **Refresh class/tag lists** only when needed (scans entire level)

### Workflow Tips
1. **Create REVSelectionVolume templates** - Save volumes with common sizes as prefabs
2. **Use presets** for repeated tasks (e.g., "Exterior Lights Only", "All Props")
3. **Organize as you go** - Use folder organization during initial placement
4. **Tag strategically** - Add tags like "Exterior", "Interior", "LOD0" for easy tag utilities
5. **Duplicate before experimenting** - Use mirror duplicate mode to preserve originals

### Organization Best Practices
1. **Consistent naming** - Use standard folder structure across levels
2. **Shallow hierarchies** - Keep folder depth to 3-4 levels max for performance
3. **Data layers by function** - Separate by gameplay purpose (lighting, props, collision)
4. **Mirror workflow** - For symmetrical levels:
   - Build one half
   - Tag with "Side_A"
   - Duplicate and mirror for other half
   - Tag with "Side_B"

### Common Workflows

**Lighting Cleanup**:
1. Create volume around over-lit area
2. Filter: "Blueprints w/ Lights"
3. Use Tag Utilities → Find Actors with Tag "Temp" (optional)
4. Preview → Select → Delete or organize

**Asset Migration**:
1. Select area with volume
2. Filter by actor/component class
3. Organize into "Migration" folder
4. Move folder to new level

**Symmetrical Level Design**:
1. Build left side of level
2. Select all with volume
3. Mirror → X-axis + Duplicate + Organize duplicates
4. Two symmetrical halves auto-organized into folders

---

## 🔧 Troubleshooting

### Selection Issues

**"No actors selected" but actors are in volume**
- ✅ Check actor type filters are enabled
- ✅ Verify volume encompasses actor bounds (not just pivot)
- ✅ Try "Intersects" mode instead of "Contains Fully"
- ✅ Check for exclude filters blocking selection
- ✅ Verify actor isn't hidden or in locked layer

**"Preview shows 0 actors"**
- ✅ At least one actor type filter OR class picker filter must be enabled
- ✅ Volume must be selected ("Use Selected" button)
- ✅ Check include/exclude tag conflicts
- ✅ Click "Refresh Class Lists" if using class filters

**Lights/Cameras not selecting**
- ✅ These actors have invalid bounds - use "Pivot Inside" mode
- ✅ Ensure actor type checkbox is enabled
- ✅ Check if pivot point is actually inside volume

### Organization Issues

**"No organization actions enabled" error**
- ✅ Enable "Move to Folder" checkbox AND set folder path
- ✅ OR select a data layer from dropdown
- ✅ At least one organization action must be enabled

**Actors not moving to folder**
- ✅ "Move to Folder" checkbox must be checked
- ✅ Folder path cannot be empty
- ✅ Check folder path format (use `/` not `\`)

**Empty folders not deleting**
- ✅ Click button multiple times (deletes 10 per click)
- ✅ Folders must have 0 actors (100% empty)
- ✅ Wait for confirmation notification

### Mirror Issues

**Mirrored actors in wrong location**
- ✅ Mirror is around world origin (0,0,0), not selection center
- ✅ This is intentional - preserves relative positions
- ✅ Use duplicate mode if you need originals

**Lights pointing wrong direction after mirror**
- ✅ This should be fixed in v1.0.0
- ✅ Report bug if lights still incorrect

**Z-axis mirror places actors underground**
- ✅ This is expected behavior (mirroring across XY plane at Z=0)
- ✅ Use with caution or adjust Z position manually after

### UI Issues

**Dropdown menus empty**
- ✅ Click "Refresh Class Lists" to scan level
- ✅ Ensure level has actors with tags/classes
- ✅ For volume dropdown, spawn or select a REVSelectionVolume

**Class/tag lists outdated**
- ✅ Lists don't auto-update when level changes
- ✅ Click "Refresh Class Lists" button manually

---

## 🛠️ Technical Details

### System Requirements
- **Engine Version**: Unreal Engine 5.7.0 or higher
- **Module Type**: Editor-only (not loaded in runtime builds)
- **Dependencies**: Niagara plugin (auto-enabled)

### Module Architecture
- **Main Module**: `FSelectActorsInVolumeModule`
- **Editor Mode**: `FOrganisationEdMode` (custom toolkit)
- **Volume Actor**: `AREVSelectionVolume` (editor-only volume with visible bounds)
- **Settings**: `FVolumeSelectionSettings` (serializable configuration)

### Key Classes
- **SelectActorsInVolume.cpp**: Core selection/organization logic (~800 lines)
- **OrganisationEdModeToolkit.cpp**: Complete Slate UI (~3000 lines)
- **VolumeSelectionSettings**: Filter configuration with JSON serialization
- **REVSelectionVolume**: Simple AVolume subclass with UBoxComponent

### Filter Logic
- **Type/Class filters**: OR logic (pass if either matches)
- **Volume bounds**: AND logic (must pass spatial test)

### File Locations
- **Plugin**: `Engine/Plugins/SelectActorsInVolume/` or `Project/Plugins/SelectActorsInVolume/`
- **Presets**: `Project/Saved/SelectActorsInVolume/Presets/*.json`
- **Config**: Settings stored in memory, not config files (use presets)

---

## 📄 License

Copyright 2026, REVOLUTIONART. All rights reserved.

This plugin is proprietary software. Redistribution and modification are not permitted without explicit permission from REVOLUTIONART.

---

## 🤝 Support

- **Website**: [https://revolutionart.nl/](https://revolutionart.nl/)
- **Documentation**: See `.github/copilot-instructions.md` for developer guide
- **Bug Reports**: Contact via website

---

## 📝 Version History

See [CHANGELOG.md](CHANGELOG.md) for detailed version history.

### v1.0.0 (2026-01-31)
- Initial release
- Volume-based selection with 3 modes
- 11 actor type filters
- Class-based filtering (include)
- Folder organization with browse dialog
- Data layer management
- Mirror actors feature (X/Y/Z axes, duplicate mode)
- Preset system (save/load/remove)
- Remove empty folders utility

---

## 🎯 Roadmap

Future features under consideration:
- Undo/redo for selection operations
- Custom selection shapes (sphere, cylinder)
- Selection history
- Bulk rename actors
- Replace actor types
- Export/import selection sets
- World Partition integration
- Foliage tool integration

---

**Made with ❤️ by Revolutionart**
