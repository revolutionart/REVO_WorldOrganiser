# Select Actors In Volume - AI Coding Instructions
# Select Actors In Volume - AI Coding Instructions

## Project Overview
Unreal Engine 5.7 editor-only plugin for selecting/organizing actors inside volumes with a Slate toolkit UI.

## Architecture (big picture)
- Module logic: [Source/SelectActorsInVolume/Private/SelectActorsInVolume.cpp](../Source/SelectActorsInVolume/Private/SelectActorsInVolume.cpp)
  - Module lifecycle, toolbar button, selection/preview/organize entry points.
  - Presets load/save via JSON in [Source/SelectActorsInVolume/Private/VolumeSelectionSettings.cpp](../Source/SelectActorsInVolume/Private/VolumeSelectionSettings.cpp).
- Editor mode + toolkit UI: [Source/SelectActorsInVolume/Private/OrganisationEdMode.cpp](../Source/SelectActorsInVolume/Private/OrganisationEdMode.cpp) and [Source/SelectActorsInVolume/Private/OrganisationEdModeToolkit.cpp](../Source/SelectActorsInVolume/Private/OrganisationEdModeToolkit.cpp).
- Selection volume actor: [Source/SelectActorsInVolume/Public/REVSelectionVolume.h](../Source/SelectActorsInVolume/Public/REVSelectionVolume.h).
- Settings struct: [Source/SelectActorsInVolume/Public/VolumeSelectionSettings.h](../Source/SelectActorsInVolume/Public/VolumeSelectionSettings.h).

## Key patterns to follow
- Selection logic: type/class filters use OR, then volume filter AND. Entry points are `RunPreview()` and `RunSelection()` in [Source/SelectActorsInVolume/Private/SelectActorsInVolume.cpp](../Source/SelectActorsInVolume/Private/SelectActorsInVolume.cpp).
- Class picker options are populated by scanning all actors/components in the current level (see `RefreshClassPickerOptions()` in [Source/SelectActorsInVolume/Private/OrganisationEdModeToolkit.cpp](../Source/SelectActorsInVolume/Private/OrganisationEdModeToolkit.cpp)).
- Folder operations use `FScopedTransaction` and `Actor->Modify()` for undo, then `Actor->SetFolderPath()`.
- Tag utilities (replace/remove/find) are driven by `ReplaceTagInLevel()`, `RemoveTagFromLevel()`, and `FindAndNavigateToActorsWithTag()` in [Source/SelectActorsInVolume/Private/SelectActorsInVolume.cpp](../Source/SelectActorsInVolume/Private/SelectActorsInVolume.cpp) and [Source/SelectActorsInVolume/Private/OrganisationEdModeToolkit.cpp](../Source/SelectActorsInVolume/Private/OrganisationEdModeToolkit.cpp).

## Build & workflow
- Build via UBT (Editor target):
  - F:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat UE57Plugin_DevEditor Win64 Development -Project="E:/UnrealEngine Projects/UE57Plugin_Dev/UE57Plugin_Dev.uproject" -WaitMutex -FromMsBuild -architecture=x64
- C++ changes require editor restart (UE5.7 no hot reload for this plugin).

## Conventions & dependencies
- Module type is Editor-only; toolbar registered via `UToolMenus::ExtendMenu("LevelEditor.LevelEditorToolBar.User")`.
- Slate UI uses nested SVerticalBox/SHorizontalBox; keep consistent styling with `FAppStyle` brushes and `PrimaryButton` style.
- Dependencies in [Source/SelectActorsInVolume/SelectActorsInVolume.Build.cs](../Source/SelectActorsInVolume/SelectActorsInVolume.Build.cs) include UnrealEd, LevelEditor, ToolMenus, DataLayerEditor, Niagara, CinematicCamera.

## Common edits
- Add actor type filter: update `EVolumeActorType`, `PassesActorTypeFilter()`, and the checkbox UI.
- Add preset field: update `FVolumeSelectionSettings` plus JSON serialize/deserialize.
