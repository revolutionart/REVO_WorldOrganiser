// Copyright 2026, REVOLUTIONART All rights reserved

#include "REVOWorldOrganiser.h"

#include "Animation/SkeletalMeshActor.h"
#include "Camera/CameraActor.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/BrushComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/DecalComponent.h"
#include "Components/LightComponent.h"
#include "Editor.h"
#include "EditorActorFolders.h"
#include "EditorModeRegistry.h"
#include "Engine/Blueprint.h"
#include "Engine/Light.h"
#include "Engine/Selection.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameFramework/Volume.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "NiagaraActor.h"
#include "OrganisationEdMode.h"
#include "REVSelectionVolume.h"
#include "REVOWorldOrganiserCommands.h"
#include "REVOWorldOrganiserStyle.h"
#include "Sound/AmbientSound.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"

#define LOCTEXT_NAMESPACE "REVOWorldOrganiser"

void FREVOWorldOrganiserModule::StartupModule() {
  FREVOWorldOrganiserStyle::Initialize();
  FREVOWorldOrganiserCommands::Register();

  // Register the editor mode
  FEditorModeRegistry::Get().RegisterMode<FOrganisationEdMode>(
      FOrganisationEdMode::EM_OrganisationModeId,
      FText::FromString("REVO World Organiser"),
      FSlateIcon(FREVOWorldOrganiserStyle::GetStyleSetName(), "REVOWorldOrganiser.ToolbarIcon"),
      true);

  CommandList = MakeShared<FUICommandList>();
  CommandList->MapAction(
      FREVOWorldOrganiserCommands::Get().REVOWorldOrganiserAll,
      FExecuteAction::CreateRaw(
          this, &FREVOWorldOrganiserModule::REVOWorldOrganiserAll));
  CommandList->MapAction(
      FREVOWorldOrganiserCommands::Get().REVOWorldOrganiserBlueprints,
      FExecuteAction::CreateRaw(
          this, &FREVOWorldOrganiserModule::REVOWorldOrganiserBlueprints));
  CommandList->MapAction(
      FREVOWorldOrganiserCommands::Get().REVOWorldOrganiserLights,
      FExecuteAction::CreateRaw(
          this, &FREVOWorldOrganiserModule::REVOWorldOrganiserLights));

  UToolMenus::RegisterStartupCallback(
      FSimpleMulticastDelegate::FDelegate::CreateRaw(
          this, &FREVOWorldOrganiserModule::RegisterMenus));
}

void FREVOWorldOrganiserModule::ShutdownModule() {
  // Unregister the editor mode
  FEditorModeRegistry::Get().UnregisterMode(
      FOrganisationEdMode::EM_OrganisationModeId);

  if (FModuleManager::Get().IsModuleLoaded("ToolMenus")) {
    UToolMenus::UnregisterOwner(this);
  }

  FREVOWorldOrganiserCommands::Unregister();
  FREVOWorldOrganiserStyle::Shutdown();
}

namespace {
enum class EREVOWorldOrganiserFilter {
  All,
  StaticMeshes,
  Blueprints,
  Lights,
  SkeletalMeshes,
  Decals,
  Niagara,
  Volumes,
  Audio,
  Cameras,
  BlueprintsWithLights
};

bool PassesFilter(const AActor *Actor, EREVOWorldOrganiserFilter Filter) {
  if (!Actor) {
    return false;
  }

  switch (Filter) {
  case EREVOWorldOrganiserFilter::StaticMeshes:
    return Actor->IsA<AStaticMeshActor>();
  case EREVOWorldOrganiserFilter::Blueprints:
    return Actor->GetClass() && Actor->GetClass()->ClassGeneratedBy != nullptr;
  case EREVOWorldOrganiserFilter::Lights:
    return Actor->IsA<ALight>();
  case EREVOWorldOrganiserFilter::SkeletalMeshes:
    return Actor->IsA<ASkeletalMeshActor>();
  case EREVOWorldOrganiserFilter::Decals: {
    TInlineComponentArray<UDecalComponent *> DecalComponents;
    Actor->GetComponents(DecalComponents);
    return DecalComponents.Num() > 0;
  }
  case EREVOWorldOrganiserFilter::Niagara:
    return Actor->IsA<ANiagaraActor>();
  case EREVOWorldOrganiserFilter::Volumes:
    return Actor->IsA<AVolume>();
  case EREVOWorldOrganiserFilter::Audio: {
    if (Actor->IsA<AAmbientSound>()) {
      return true;
    }
    TInlineComponentArray<UAudioComponent *> AudioComponents;
    Actor->GetComponents(AudioComponents);
    return AudioComponents.Num() > 0;
  }
  case EREVOWorldOrganiserFilter::Cameras:
    return Actor->IsA<ACameraActor>();
  case EREVOWorldOrganiserFilter::BlueprintsWithLights: {
    if (Actor->GetClass() && Actor->GetClass()->ClassGeneratedBy != nullptr) {
      // Check for direct light components
      TInlineComponentArray<ULightComponent *> LightComponents;
      Actor->GetComponents(LightComponents);
      if (LightComponents.Num() > 0) {
        return true;
      }

      // Check for lights in nested ChildActorComponents
      TInlineComponentArray<UChildActorComponent *> ChildActorComponents;
      Actor->GetComponents(ChildActorComponents);
      for (UChildActorComponent *ChildActorComp : ChildActorComponents) {
        if (ChildActorComp && ChildActorComp->GetChildActor()) {
          AActor *ChildActor = ChildActorComp->GetChildActor();
          TInlineComponentArray<ULightComponent *> ChildLightComponents;
          ChildActor->GetComponents(ChildLightComponents);
          if (ChildLightComponents.Num() > 0) {
            return true;
          }
        }
      }
    }
    return false;
  }
  case EREVOWorldOrganiserFilter::All:
  default:
    return true;
  }
}
} // namespace

static void REVOWorldOrganiserInternal(EREVOWorldOrganiserFilter Filter) {
  if (!GEditor) {
    return;
  }

  UWorld *World = GEditor->GetEditorWorldContext().World();
  if (!World) {
    return;
  }

  AVolume *SelectedVolume = nullptr;
  for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It) {
    if (AVolume *Volume = Cast<AVolume>(*It)) {
      SelectedVolume = Volume;
      break;
    }
  }

  if (!SelectedVolume) {
    FNotificationInfo Info(LOCTEXT("REVOWorldOrganiser_NoVolume",
                                   "Select a volume actor first."));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
    return;
  }

  const FBox VolumeBounds = SelectedVolume->GetComponentsBoundingBox(true);

  GEditor->SelectNone(false, true, false);

  int32 DebugActorsChecked = 0;
  int32 DebugActorsPassedFilter = 0;
  int32 DebugActorsSelected = 0;

  for (TActorIterator<AActor> It(World); It; ++It) {
    AActor *Actor = *It;
    if (!Actor || Actor == SelectedVolume) {
      continue;
    }

    if (Actor->IsHiddenEd() || Actor->IsEditorOnly()) {
      continue;
    }

    DebugActorsChecked++;

    if (!PassesFilter(Actor, Filter)) {
      continue;
    }

    DebugActorsPassedFilter++;

    const FBox ActorBounds = Actor->GetComponentsBoundingBox(true);

    // For actors with invalid bounds (like lights, cameras), use pivot location
    // instead
    if (!ActorBounds.IsValid) {
      if (VolumeBounds.IsInsideOrOn(Actor->GetActorLocation())) {
        GEditor->SelectActor(Actor, true, false);
        DebugActorsSelected++;
      }
      continue;
    }

    // Use Intersects logic by default (matches most user expectations)
    if (VolumeBounds.Intersect(ActorBounds)) {
      GEditor->SelectActor(Actor, true, false);
      DebugActorsSelected++;
    }
  }

  GEditor->NoteSelectionChange();

  UE_LOG(LogTemp, Warning,
         TEXT("REVOWorldOrganiserInternal - Filter: %d, Checked: %d, Passed "
              "Filter: %d, Selected: %d"),
         (int32)Filter, DebugActorsChecked, DebugActorsPassedFilter,
         DebugActorsSelected);
}

void FREVOWorldOrganiserModule::RegisterMenus() {
  UToolMenus *ToolMenus = UToolMenus::Get();
  if (!ToolMenus) {
    return;
  }

  // Extend the Level Editor toolbar
  {
    UToolMenu *ToolbarMenu =
        ToolMenus->ExtendMenu("LevelEditor.LevelEditorToolBar.User");
    if (ToolbarMenu) {
      FToolMenuSection &Section = ToolbarMenu->AddSection(
          "REVOWorldOrganiser",
          LOCTEXT("REVOWorldOrganiserSection", "Select In Volume"));

      FToolMenuEntry &Entry = Section.AddEntry(FToolMenuEntry::InitComboButton(
          "REVOWorldOrganiserCombo", FUIAction(),
          FOnGetContent::CreateLambda([this]() {
            FMenuBuilder MenuBuilder(true, CommandList);
            MenuBuilder.AddMenuEntry(
                LOCTEXT("SelectAll", "Select in volume: All Actor Types"),
                LOCTEXT("SelectAllTooltip",
                        "Select all actors inside the volume"),
                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]() {
                  REVOWorldOrganiserInternal(
                      EREVOWorldOrganiserFilter::All);
                })));
            MenuBuilder.AddSeparator();
            MenuBuilder.AddMenuEntry(
                LOCTEXT("SelectStaticMeshes",
                        "Select in volume: Static Mesh Actors"),
                LOCTEXT("SelectStaticMeshesTooltip",
                        "Select static mesh actors"),
                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]() {
                  REVOWorldOrganiserInternal(
                      EREVOWorldOrganiserFilter::StaticMeshes);
                })));
            MenuBuilder.AddMenuEntry(
                LOCTEXT("SelectBlueprints",
                        "Select in volume: Blueprint Actors"),
                LOCTEXT("SelectBlueprintsTooltip", "Select blueprint actors"),
                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]() {
                  REVOWorldOrganiserInternal(
                      EREVOWorldOrganiserFilter::Blueprints);
                })));
            MenuBuilder.AddMenuEntry(
                LOCTEXT("SelectLights", "Select in volume: Light Actors"),
                LOCTEXT("SelectLightsTooltip", "Select light actors"),
                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]() {
                  REVOWorldOrganiserInternal(
                      EREVOWorldOrganiserFilter::Lights);
                })));
            MenuBuilder.AddMenuEntry(
                LOCTEXT("SelectSkeletalMeshes",
                        "Select in volume: Skeletal Mesh Actors"),
                LOCTEXT("SelectSkeletalMeshesTooltip",
                        "Select skeletal mesh actors"),
                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]() {
                  REVOWorldOrganiserInternal(
                      EREVOWorldOrganiserFilter::SkeletalMeshes);
                })));
            MenuBuilder.AddMenuEntry(
                LOCTEXT("SelectDecals", "Select in volume: Decal Actors"),
                LOCTEXT("SelectDecalsTooltip", "Select decal actors"),
                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]() {
                  REVOWorldOrganiserInternal(
                      EREVOWorldOrganiserFilter::Decals);
                })));
            MenuBuilder.AddMenuEntry(
                LOCTEXT("SelectNiagara", "Select in volume: Niagara Actors"),
                LOCTEXT("SelectNiagaraTooltip",
                        "Select Niagara particle actors"),
                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]() {
                  REVOWorldOrganiserInternal(
                      EREVOWorldOrganiserFilter::Niagara);
                })));
            MenuBuilder.AddMenuEntry(
                LOCTEXT("SelectVolumes", "Select in volume: Volumes"),
                LOCTEXT("SelectVolumesTooltip", "Select volume actors"),
                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]() {
                  REVOWorldOrganiserInternal(
                      EREVOWorldOrganiserFilter::Volumes);
                })));
            MenuBuilder.AddMenuEntry(
                LOCTEXT("SelectAudio", "Select in volume: Audio Actors"),
                LOCTEXT("SelectAudioTooltip", "Select audio actors"),
                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]() {
                  REVOWorldOrganiserInternal(
                      EREVOWorldOrganiserFilter::Audio);
                })));
            MenuBuilder.AddMenuEntry(
                LOCTEXT("SelectCameras", "Select in volume: Cameras"),
                LOCTEXT("SelectCamerasTooltip", "Select camera actors"),
                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]() {
                  REVOWorldOrganiserInternal(
                      EREVOWorldOrganiserFilter::Cameras);
                })));
            MenuBuilder.AddMenuEntry(
                LOCTEXT("SelectBlueprintsWithLights",
                        "Select in volume: Blueprints w/ Lights"),
                LOCTEXT("SelectBlueprintsWithLightsTooltip",
                        "Select blueprints containing lights"),
                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]() {
                  REVOWorldOrganiserInternal(
                      EREVOWorldOrganiserFilter::BlueprintsWithLights);
                })));
            return MenuBuilder.MakeWidget();
          }),
          LOCTEXT("REVOWorldOrganiser_Label", "Select In Volume"),
          LOCTEXT("REVOWorldOrganiser_Tooltip",
                  "Select actors inside the selected volume."),
          FSlateIcon(FREVOWorldOrganiserStyle::GetStyleSetName(),
                     "REVOWorldOrganiser.ToolbarIcon")));
    }
  }
}

void FREVOWorldOrganiserModule::REVOWorldOrganiserAll() {
  REVOWorldOrganiserInternal(EREVOWorldOrganiserFilter::All);
}

void FREVOWorldOrganiserModule::REVOWorldOrganiserBlueprints() {
  REVOWorldOrganiserInternal(EREVOWorldOrganiserFilter::Blueprints);
}

void FREVOWorldOrganiserModule::REVOWorldOrganiserLights() {
  REVOWorldOrganiserInternal(EREVOWorldOrganiserFilter::Lights);
}

FVolumeSelectionSettings &FREVOWorldOrganiserModule::GetSettings() {
  return Settings;
}

const FVolumeSelectionSettings &
FREVOWorldOrganiserModule::GetSettings() const {
  return Settings;
}

int32 FREVOWorldOrganiserModule::GetPreviewCount() const {
  return PreviewCount;
}

namespace {
bool PassesActorTypeFilter(AActor *Actor, uint32 ActorTypeMask) {
  if (ActorTypeMask == 0) {
    return true; // No filter set, all actors pass
  }

  if ((ActorTypeMask &
       static_cast<uint32>(EVolumeActorType::StaticMeshActors)) &&
      Actor->IsA<AStaticMeshActor>()) {
    return true;
  }
  if ((ActorTypeMask &
       static_cast<uint32>(EVolumeActorType::BlueprintActors)) &&
      Actor->GetClass()->ClassGeneratedBy != nullptr) {
    return true;
  }
  if ((ActorTypeMask &
       static_cast<uint32>(EVolumeActorType::BlueprintsWithLights)) &&
      Actor->GetClass()->ClassGeneratedBy != nullptr) {
    // Check for direct light components
    TInlineComponentArray<ULightComponent *> LightComponents;
    Actor->GetComponents(LightComponents);
    if (LightComponents.Num() > 0) {
      return true;
    }

    // Check for lights in nested ChildActorComponents
    TInlineComponentArray<UChildActorComponent *> ChildActorComponents;
    Actor->GetComponents(ChildActorComponents);
    for (UChildActorComponent *ChildActorComp : ChildActorComponents) {
      if (ChildActorComp && ChildActorComp->GetChildActor()) {
        AActor *ChildActor = ChildActorComp->GetChildActor();
        TInlineComponentArray<ULightComponent *> ChildLightComponents;
        ChildActor->GetComponents(ChildLightComponents);
        if (ChildLightComponents.Num() > 0) {
          return true;
        }
      }
    }
  }
  if ((ActorTypeMask & static_cast<uint32>(EVolumeActorType::LightActors)) &&
      Actor->IsA<ALight>()) {
    return true;
  }
  if ((ActorTypeMask &
       static_cast<uint32>(EVolumeActorType::SkeletalMeshActors)) &&
      Actor->IsA<ASkeletalMeshActor>()) {
    return true;
  }
  if ((ActorTypeMask & static_cast<uint32>(EVolumeActorType::DecalActors))) {
    TInlineComponentArray<UDecalComponent *> DecalComponents;
    Actor->GetComponents(DecalComponents);
    if (DecalComponents.Num() > 0) {
      return true;
    }
  }
  if ((ActorTypeMask & static_cast<uint32>(EVolumeActorType::LevelInstances))) {
    // Check if actor is a Level Instance by class name
    const FString ClassName = Actor->GetClass()->GetName();
    if (ClassName.Contains(TEXT("LevelInstance"))) {
      return true;
    }
  }
  if ((ActorTypeMask & static_cast<uint32>(EVolumeActorType::NiagaraActors)) &&
      Actor->IsA<ANiagaraActor>()) {
    return true;
  }
  if ((ActorTypeMask & static_cast<uint32>(EVolumeActorType::Volumes)) &&
      Actor->IsA<AVolume>()) {
    return true;
  }
  if ((ActorTypeMask & static_cast<uint32>(EVolumeActorType::AudioActors))) {
    if (Actor->IsA<AAmbientSound>()) {
      return true;
    }
    TInlineComponentArray<UAudioComponent *> AudioComponents;
    Actor->GetComponents(AudioComponents);
    if (AudioComponents.Num() > 0) {
      return true;
    }
  }
  if ((ActorTypeMask & static_cast<uint32>(EVolumeActorType::Cameras)) &&
      Actor->IsA<ACameraActor>()) {
    return true;
  }
  if ((ActorTypeMask & static_cast<uint32>(EVolumeActorType::Text3DActors))) {
    // Check if actor is a Text3D actor by class name (to avoid hard dependency)
    const FString ClassName = Actor->GetClass()->GetName();
    if (ClassName.Contains(TEXT("Text3D"))) {
      return true;
    }
  }

  return false;
}

bool PassesClassPickerFilter(AActor *Actor,
                             const FVolumeSelectionSettings &Settings) {
  if (!Settings.bEnableClassPickerFilter) {
    return true; // Filter disabled, all actors pass
  }

  bool bHasActorFilter = !Settings.SelectedActorClassPath.IsEmpty();
  bool bHasComponentFilter = !Settings.SelectedComponentClassPath.IsEmpty();

  // If neither filter is set, no actors pass
  if (!bHasActorFilter && !bHasComponentFilter) {
    return false;
  }

  bool bPassesActorClass = true;
  bool bPassesComponentClass = true;

  // Check actor class if specified
  if (bHasActorFilter) {
    UClass *TargetActorClass =
        LoadObject<UClass>(nullptr, *Settings.SelectedActorClassPath);
    if (TargetActorClass) {
      bPassesActorClass = Actor->IsA(TargetActorClass);
    } else {
      bPassesActorClass = false; // Failed to load class
    }
  }

  // Check component class if specified
  if (bHasComponentFilter) {
    UClass *TargetComponentClass =
        LoadObject<UClass>(nullptr, *Settings.SelectedComponentClassPath);
    if (TargetComponentClass) {
      bPassesComponentClass = false; // Assume false until we find a match

      TInlineComponentArray<UActorComponent *> Components;
      Actor->GetComponents(Components);

      for (UActorComponent *Component : Components) {
        if (Component && Component->IsA(TargetComponentClass)) {
          bPassesComponentClass = true;
          break;
        }
      }
    } else {
      bPassesComponentClass = false; // Failed to load class
    }
  }

  bool bResult = bPassesActorClass && bPassesComponentClass;

  // If both filters specified, both must pass (AND logic)
  // If only one filter specified, only that one needs to pass
  return bResult;
}

bool IsExcludedByClassFilters(AActor *Actor,
                              const FVolumeSelectionSettings &Settings) {
  if (Settings.ExcludeActorClassList.Num() == 0 &&
      Settings.ExcludeComponentClassList.Num() == 0) {
    return false;
  }

  for (const FString &ClassPath : Settings.ExcludeActorClassList) {
    if (ClassPath.IsEmpty()) {
      continue;
    }
    UClass *ExcludedClass = LoadObject<UClass>(nullptr, *ClassPath);
    if (ExcludedClass && Actor->IsA(ExcludedClass)) {
      return true;
    }
  }

  for (const FString &ClassPath : Settings.ExcludeComponentClassList) {
    if (ClassPath.IsEmpty()) {
      continue;
    }
    UClass *ExcludedComponentClass = LoadObject<UClass>(nullptr, *ClassPath);
    if (!ExcludedComponentClass) {
      continue;
    }

    TInlineComponentArray<UActorComponent *> Components;
    Actor->GetComponents(Components);
    for (UActorComponent *Component : Components) {
      if (Component && Component->IsA(ExcludedComponentClass)) {
        return true;
      }
    }
  }

  return false;
}

bool PassesTagFilters(AActor *Actor, const FVolumeSelectionSettings &Settings) {
  if (Settings.ExcludeTagList.Num() > 0) {
    for (const FName &Tag : Settings.ExcludeTagList) {
      if (Actor->Tags.Contains(Tag)) {
        return false;
      }
    }
  }

  if (Settings.IncludeTagList.Num() == 0) {
    return true;
  }

  for (const FName &Tag : Settings.IncludeTagList) {
    if (Actor->Tags.Contains(Tag)) {
      return true;
    }
  }

  return false;
}

bool PassesVolumeFilter(AActor *Actor, AActor *VolumeActor,
                        const FVolumeSelectionSettings &Settings) {
  if (!VolumeActor) {
    return true; // No volume filter, all actors pass
  }

  // Get proper volume bounds - check for REVSelectionVolume with BoxComponent
  // first
  FBox VolumeBounds;
  if (AREVSelectionVolume *REVVolume = Cast<AREVSelectionVolume>(VolumeActor)) {
    // REVSelectionVolume uses BoxComponent for bounds
    if (UBoxComponent *BoxComp =
            REVVolume->FindComponentByClass<UBoxComponent>()) {
      // DEBUG: Log component scales and extents
      const FVector UnscaledExtent = BoxComp->GetUnscaledBoxExtent();
      // Use scaled box extent to build accurate bounds
      const FVector BoxExtent = BoxComp->GetScaledBoxExtent();
      const FVector BoxCenter = BoxComp->GetComponentLocation();
      VolumeBounds = FBox(BoxCenter - BoxExtent, BoxCenter + BoxExtent);
    } else {
      VolumeBounds = VolumeActor->GetComponentsBoundingBox(true);
    }
  } else if (AVolume *Volume = Cast<AVolume>(VolumeActor)) {
    // Standard volumes use BrushComponent
    if (UBrushComponent *BrushComp = Volume->GetBrushComponent()) {
      VolumeBounds = BrushComp->Bounds.GetBox();
    } else {
      VolumeBounds = VolumeActor->GetComponentsBoundingBox(true);
    }
  } else {
    VolumeBounds = VolumeActor->GetComponentsBoundingBox(true);
  }

  FBox ActorBounds;

  if (Settings.bUseComponentBounds) {
    ActorBounds = Actor->GetComponentsBoundingBox(true);
  } else {
    ActorBounds = Actor->GetComponentsBoundingBox(false);
  }

  if (!ActorBounds.IsValid) {
    // For actors with invalid bounds (like lights), use pivot location instead
    return VolumeBounds.IsInsideOrOn(Actor->GetActorLocation());
  }

  switch (Settings.SelectionMode) {
  case EVolumeSelectionMode::ContainsFully: {
    // 75% containment check based on bounds volume overlap
    if (!VolumeBounds.Intersect(ActorBounds)) {
      return false;
    }

    const FVector ActorExtent = ActorBounds.Max - ActorBounds.Min;
    const double ActorVolume =
        static_cast<double>(ActorExtent.X) * ActorExtent.Y * ActorExtent.Z;
    if (ActorVolume <= KINDA_SMALL_NUMBER) {
      return VolumeBounds.IsInsideOrOn(Actor->GetActorLocation());
    }

    const FVector IntersectMin =
        FVector::Max(VolumeBounds.Min, ActorBounds.Min);
    const FVector IntersectMax =
        FVector::Min(VolumeBounds.Max, ActorBounds.Max);
    const FVector IntersectExtent = IntersectMax - IntersectMin;
    const double IntersectVolume = static_cast<double>(IntersectExtent.X) *
                                   IntersectExtent.Y * IntersectExtent.Z;

    const double InsideRatio = IntersectVolume / ActorVolume;
    return InsideRatio >= Settings.ContainsFullyThreshold;
  }

  case EVolumeSelectionMode::Intersects:
    return VolumeBounds.Intersect(ActorBounds);

  case EVolumeSelectionMode::PivotInside:
    return VolumeBounds.IsInsideOrOn(Actor->GetActorLocation());

  default:
    return false;
  }
}
} // namespace

int32 FREVOWorldOrganiserModule::RunPreview() {
  // Validate filter configuration
  const bool bHasActorTypeFilter = Settings.ActorTypeMask != 0;
  const bool bHasClassPickerFilter =
      Settings.bEnableClassPickerFilter &&
      (!Settings.SelectedActorClassPath.IsEmpty() ||
       !Settings.SelectedComponentClassPath.IsEmpty());
  const bool bHasExcludeClassFilter =
      Settings.ExcludeActorClassList.Num() > 0 ||
      Settings.ExcludeComponentClassList.Num() > 0;
  const bool bHasTagFilter =
      Settings.IncludeTagList.Num() > 0 || Settings.ExcludeTagList.Num() > 0;
  if (!bHasActorTypeFilter && !bHasClassPickerFilter &&
      !bHasExcludeClassFilter && !bHasTagFilter) {
    FNotificationInfo Info(
        LOCTEXT("NoFilters", "No Actor Types selected. Please select at least "
                             "one Actor Type, enable Class Picker Filter, or "
                             "use Exclude/Tag filters."));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
    PreviewCount = 0;
    return 0;
  }

  if (!GEditor) {
    PreviewCount = 0;
    return 0;
  }

  UWorld *World = GEditor->GetEditorWorldContext().World();
  if (!World) {
    PreviewCount = 0;
    return 0;
  }

  AActor *VolumeActor = Settings.TargetVolumeActor.Get();
  if (!VolumeActor) {
    FNotificationInfo Info(
        LOCTEXT("NoVolume", "No target volume selected. Use 'Set Volume from "
                            "Selection' to set a volume."));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
    PreviewCount = 0;
    return 0;
  }

  PreviewCount = 0;

  for (TActorIterator<AActor> It(World); It; ++It) {
    AActor *Actor = *It;
    if (!Actor || Actor == VolumeActor) {
      continue;
    }

    if (Actor->IsHiddenEd() || Actor->IsEditorOnly()) {
      continue;
    }

    const bool bHasTypeOrClassFilter =
        bHasActorTypeFilter || bHasClassPickerFilter;
    bool bPassesTypeOrClassFilter = !bHasTypeOrClassFilter;
    if (bHasActorTypeFilter &&
        PassesActorTypeFilter(Actor, Settings.ActorTypeMask)) {
      bPassesTypeOrClassFilter = true;
    }
    if (bHasClassPickerFilter && PassesClassPickerFilter(Actor, Settings)) {
      bPassesTypeOrClassFilter = true;
    }

    if (!bPassesTypeOrClassFilter) {
      continue;
    }

    if (IsExcludedByClassFilters(Actor, Settings)) {
      continue;
    }

    if (!PassesTagFilters(Actor, Settings)) {
      continue;
    }

    if (!PassesVolumeFilter(Actor, VolumeActor, Settings)) {
      continue;
    }

    // Only count actors that can actually be selected (not locked, not in
    // locked layers, etc.)
    if (!GEditor->CanSelectActor(Actor, true, false)) {
      continue;
    }

    ++PreviewCount;
  }

  FNotificationInfo Info(
      FText::Format(LOCTEXT("PreviewComplete",
                            "Preview: {0} actor(s) match the current filters"),
                    FText::AsNumber(PreviewCount)));
  Info.ExpireDuration = 3.0f;
  FSlateNotificationManager::Get().AddNotification(Info);

  return PreviewCount;
}

int32 FREVOWorldOrganiserModule::RunSelection() {
  // Validate filter configuration
  const bool bHasActorTypeFilter = Settings.ActorTypeMask != 0;
  const bool bHasClassPickerFilter =
      Settings.bEnableClassPickerFilter &&
      (!Settings.SelectedActorClassPath.IsEmpty() ||
       !Settings.SelectedComponentClassPath.IsEmpty());
  const bool bHasExcludeClassFilter =
      Settings.ExcludeActorClassList.Num() > 0 ||
      Settings.ExcludeComponentClassList.Num() > 0;
  const bool bHasTagFilter =
      Settings.IncludeTagList.Num() > 0 || Settings.ExcludeTagList.Num() > 0;
  if (!bHasActorTypeFilter && !bHasClassPickerFilter &&
      !bHasExcludeClassFilter && !bHasTagFilter) {
    FNotificationInfo Info(
        LOCTEXT("NoFilters", "No Actor Types selected. Please select at least "
                             "one Actor Type, enable Class Picker Filter, or "
                             "use Exclude/Tag filters."));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
    return 0;
  }

  if (!GEditor) {
    return 0;
  }

  UWorld *World = GEditor->GetEditorWorldContext().World();
  if (!World) {
    return 0;
  }

  AActor *VolumeActor = Settings.TargetVolumeActor.Get();
  if (!VolumeActor) {
    FNotificationInfo Info(
        LOCTEXT("NoVolume", "No target volume selected. Use 'Set Volume from "
                            "Selection' to set a volume."));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
    return 0;
  }

  // Clear current selection
  GEditor->SelectNone(false, false);

  int32 SelectedCount = 0;

  for (TActorIterator<AActor> It(World); It; ++It) {
    AActor *Actor = *It;
    if (!Actor || Actor == VolumeActor) {
      continue;
    }

    if (Actor->IsHiddenEd() || Actor->IsEditorOnly()) {
      continue;
    }

    const bool bHasTypeOrClassFilter =
        bHasActorTypeFilter || bHasClassPickerFilter;
    bool bPassesTypeOrClassFilter = !bHasTypeOrClassFilter;
    if (bHasActorTypeFilter &&
        PassesActorTypeFilter(Actor, Settings.ActorTypeMask)) {
      bPassesTypeOrClassFilter = true;
    }
    if (bHasClassPickerFilter && PassesClassPickerFilter(Actor, Settings)) {
      bPassesTypeOrClassFilter = true;
    }

    if (!bPassesTypeOrClassFilter) {
      continue;
    }

    if (IsExcludedByClassFilters(Actor, Settings)) {
      continue;
    }

    if (!PassesTagFilters(Actor, Settings)) {
      continue;
    }

    if (!PassesVolumeFilter(Actor, VolumeActor, Settings)) {
      continue;
    }

    // Select the actor
    GEditor->SelectActor(Actor, true, false);
    ++SelectedCount;
  }

  GEditor->NoteSelectionChange();

  FNotificationInfo Info(
      FText::Format(LOCTEXT("SelectionComplete", "Selected {0} actor(s)"),
                    FText::AsNumber(SelectedCount)));
  Info.ExpireDuration = 3.0f;
  FSlateNotificationManager::Get().AddNotification(Info);

  return SelectedCount;
}

int32 FREVOWorldOrganiserModule::RunMoveAndOrganize() {
  // Validate that at least one organization action is enabled
  const bool bHasFolderAction = Settings.bMoveToFolder;
  const bool bHasDataLayerAction = Settings.TargetDataLayer != nullptr;

  if (!bHasFolderAction && !bHasDataLayerAction) {
    FNotificationInfo Info(
        LOCTEXT("OrganizeNoAction",
                "No organization actions enabled. Please check 'Move to "
                "Folder' or set a 'Move to Data Layer' to organize actors."));
    Info.ExpireDuration = 4.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
    return 0;
  }

  // Check if we have INCLUDE filters configured (exclude filters don't require volume selection)
  const bool bHasActorTypeFilter = Settings.ActorTypeMask != 0;
  const bool bHasClassPickerFilter =
      Settings.bEnableClassPickerFilter &&
      (!Settings.SelectedActorClassPath.IsEmpty() ||
       !Settings.SelectedComponentClassPath.IsEmpty());
  const bool bHasIncludeTagFilter = Settings.IncludeTagList.Num() > 0;
  
  // Only INCLUDE filters trigger volume-based selection.
  // Exclude filters are applied to any selection (manual or filtered) but don't force volume mode.
  const bool bHasAnyIncludeFilter = bHasActorTypeFilter || bHasClassPickerFilter || bHasIncludeTagFilter;

  // If INCLUDE filters are configured, run volume-based selection first
  if (bHasAnyIncludeFilter) {
    int32 SelectedCount = RunSelection();
    if (SelectedCount == 0) {
      FNotificationInfo Info(
          LOCTEXT("OrganizeNoActors",
                  "No actors found to organize with current filters."));
      Info.ExpireDuration = 3.0f;
      FSlateNotificationManager::Get().AddNotification(Info);
      return 0;
    }
  } else {
    // No filters configured - use manually selected actors from Outliner
    if (!GEditor) {
      return 0;
    }
    
    int32 ManualSelectionCount = 0;
    for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It) {
      if (Cast<AActor>(*It)) {
        ++ManualSelectionCount;
      }
    }
    
    if (ManualSelectionCount == 0) {
      FNotificationInfo Info(
          LOCTEXT("OrganizeNoSelection",
                  "No actors selected. Either select actors manually in the "
                  "Outliner, or configure filters and use a volume."));
      Info.ExpireDuration = 4.0f;
      FSlateNotificationManager::Get().AddNotification(Info);
      return 0;
    }
  }

  if (!GEditor) {
    return 0;
  }

  // Create undo transaction
  FScopedTransaction Transaction(
      LOCTEXT("MoveAndOrganize", "Move and Organize Actors"));

  // Collect all actors to organize first
  TArray<AActor*> ActorsToOrganize;
  for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It) {
    AActor *Actor = Cast<AActor>(*It);
    if (Actor) {
      ActorsToOrganize.Add(Actor);
    }
  }

  // Collect unique packages that need modification
  TSet<UPackage*> PackagesToModify;
  for (AActor* Actor : ActorsToOrganize) {
    if (UPackage* Package = Actor->GetOutermost()) {
      PackagesToModify.Add(Package);
    }
  }

  // Batch mark packages for modification (avoids spamming source control checkout prompts)
  for (UPackage* Package : PackagesToModify) {
    Package->MarkPackageDirty();
  }

  // Now modify actors within the transaction
  for (AActor* Actor : ActorsToOrganize) {
    Actor->Modify();
  }

  int32 OrganizedCount = 0;

  // Now organize all actors
  for (AActor* Actor : ActorsToOrganize) {
    // Move to folder if enabled
    if (Settings.bMoveToFolder && !Settings.TargetFolderPath.IsNone()) {
      if (Settings.bOrganizeByActorType) {
        // Organize into subfolders by actor type
        FName SubFolder = Settings.TargetFolderPath;
        FString SubFolderStr = Settings.TargetFolderPath.ToString();

        if (Actor->IsA<AStaticMeshActor>()) {
          SubFolderStr += TEXT("/StaticMeshes");
        } else if (Actor->GetClass()->ClassGeneratedBy != nullptr) {
          SubFolderStr += TEXT("/Blueprints");
        } else if (Actor->IsA<ALight>()) {
          SubFolderStr += TEXT("/Lights");
        } else if (Actor->IsA<ASkeletalMeshActor>()) {
          SubFolderStr += TEXT("/SkeletalMeshes");
        } else if (Actor->IsA<ANiagaraActor>()) {
          SubFolderStr += TEXT("/VFX");
        } else if (Actor->IsA<AVolume>()) {
          SubFolderStr += TEXT("/Volumes");
        } else if (Actor->IsA<ACameraActor>()) {
          SubFolderStr += TEXT("/Cameras");
        } else if (Actor->IsA<AAmbientSound>()) {
          SubFolderStr += TEXT("/Audio");
        } else {
          // Check for Decal actors
          TInlineComponentArray<UDecalComponent *> DecalComponents;
          Actor->GetComponents(DecalComponents);
          if (DecalComponents.Num() > 0) {
            SubFolderStr += TEXT("/Decals");
          }
          // Check for Level Instances
          else if (Actor->GetClass()->GetName().Contains(
                       TEXT("LevelInstance"))) {
            SubFolderStr += TEXT("/LevelInstances");
          }
          // Check for Text3D actors
          else if (Actor->GetClass()->GetName().Contains(TEXT("Text3D"))) {
            SubFolderStr += TEXT("/Text3D");
          } else {
            SubFolderStr += TEXT("/Other");
          }
        }

        SubFolder = FName(*SubFolderStr);
        Actor->SetFolderPath(SubFolder);
      } else {
        Actor->SetFolderPath(Settings.TargetFolderPath);
      }
    }

    // Move to data layer if enabled
    if (Settings.TargetDataLayer) {
      // Get the data layer manager and add actor to data layer instance
      if (UWorld *ActorWorld = Actor->GetWorld()) {
        if (UDataLayerManager *DataLayerManager =
                UDataLayerManager::GetDataLayerManager(ActorWorld)) {
          if (const UDataLayerInstance *DataLayerInstance =
                  DataLayerManager->GetDataLayerInstance(
                      Settings.TargetDataLayer)) {
            const_cast<UDataLayerInstance *>(DataLayerInstance)
                ->AddActor(Actor);
          }
        }
      }
    }

    ++OrganizedCount;
  }

  FNotificationInfo Info(
      FText::Format(LOCTEXT("OrganizeComplete",
                            "Organized {0} actor(s) into folders/data layers"),
                    FText::AsNumber(OrganizedCount)));
  Info.ExpireDuration = 3.0f;
  FSlateNotificationManager::Get().AddNotification(Info);

  return OrganizedCount;
}

void FREVOWorldOrganiserModule::SetTargetVolumeFromSelection() {
  if (!GEditor) {
    return;
  }

  // Find first selected volume
  AActor *SelectedVolume = nullptr;
  for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It) {
    if (AActor *Actor = Cast<AActor>(*It)) {
      if (Actor->IsA<AVolume>()) {
        SelectedVolume = Actor;
        break;
      }
    }
  }

  if (SelectedVolume) {
    Settings.TargetVolumeActor = SelectedVolume;

    FNotificationInfo Info(
        FText::Format(LOCTEXT("VolumeSet", "Target volume set to: {0}"),
                      FText::FromString(SelectedVolume->GetActorLabel())));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
  } else {
    FNotificationInfo Info(
        LOCTEXT("NoVolumeSelected",
                "No volume selected. Please select a volume actor first."));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
  }
}

void FREVOWorldOrganiserModule::SpawnEmptyVolume() {
  if (!GEditor) {
    return;
  }

  UWorld *World = GEditor->GetEditorWorldContext().World();
  if (!World) {
    return;
  }

  // Get spawn location from camera or world origin
  FVector SpawnLocation = FVector::ZeroVector;
  FRotator SpawnRotation = FRotator::ZeroRotator;

  if (GEditor->GetActiveViewport()) {
    FViewport *Viewport = GEditor->GetActiveViewport();
    FViewportClient *Client = Viewport->GetClient();
    if (FEditorViewportClient *EditorClient =
            static_cast<FEditorViewportClient *>(Client)) {
      SpawnLocation = EditorClient->GetViewLocation() +
                      (EditorClient->GetViewRotation().Vector() * 500.0f);
    }
  }

  // Spawn REVSelectionVolume
  FActorSpawnParameters SpawnParams;
  SpawnParams.Name = NAME_None;
  SpawnParams.SpawnCollisionHandlingOverride =
      ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

  AREVSelectionVolume *NewVolume = World->SpawnActor<AREVSelectionVolume>(
      SpawnLocation, SpawnRotation, SpawnParams);

  if (NewVolume) {
    // Select the newly spawned volume
    GEditor->SelectNone(false, true);
    GEditor->SelectActor(NewVolume, true, true);

    // Set as target volume
    Settings.TargetVolumeActor = NewVolume;

    FNotificationInfo Info(FText::Format(
        LOCTEXT("VolumeSpawned", "Created new selection volume: {0}"),
        FText::FromString(NewVolume->GetActorLabel())));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
  }
}

FText FREVOWorldOrganiserModule::GetTargetVolumeLabel() const {
  if (AActor *VolumeActor = Settings.TargetVolumeActor.Get()) {
    return FText::FromString(VolumeActor->GetActorLabel());
  }
  return LOCTEXT("NoVolume", "No Volume Selected");
}

FString GetPresetDirectory() {
  return FPaths::ProjectSavedDir() / TEXT("REVOWorldOrganiser") /
         TEXT("Presets");
}

TArray<FString> FREVOWorldOrganiserModule::GetPresetNames() const {
  TArray<FString> PresetNames;

  const FString PresetDir = GetPresetDirectory();
  IPlatformFile &PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

  if (PlatformFile.DirectoryExists(*PresetDir)) {
    TArray<FString> FoundFiles;
    PlatformFile.FindFiles(FoundFiles, *PresetDir, TEXT(".json"));

    for (const FString &FilePath : FoundFiles) {
      FString FileName = FPaths::GetBaseFilename(FilePath);
      PresetNames.Add(FileName);
    }
  }

  return PresetNames;
}

void FREVOWorldOrganiserModule::SavePreset(const FString &PresetName) {
  if (PresetName.IsEmpty()) {
    FNotificationInfo Info(
        LOCTEXT("EmptyPresetName", "Preset name cannot be empty."));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
    return;
  }

  const FString PresetDir = GetPresetDirectory();
  IPlatformFile &PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

  // Create directory if it doesn't exist
  if (!PlatformFile.DirectoryExists(*PresetDir)) {
    PlatformFile.CreateDirectoryTree(*PresetDir);
  }

  const FString FilePath = PresetDir / (PresetName + TEXT(".json"));
  const FString JsonString = VolumeSelectionSettings::SerializeToJson(Settings);

  if (FFileHelper::SaveStringToFile(JsonString, *FilePath)) {
    FNotificationInfo Info(FText::Format(
        LOCTEXT("PresetSaved", "Preset '{0}' saved successfully."),
        FText::FromString(PresetName)));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
  } else {
    FNotificationInfo Info(FText::Format(
        LOCTEXT("PresetSaveFailed", "Failed to save preset '{0}'."),
        FText::FromString(PresetName)));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
  }
}

bool FREVOWorldOrganiserModule::LoadPreset(const FString &PresetName) {
  if (PresetName.IsEmpty()) {
    return false;
  }

  const FString FilePath = GetPresetDirectory() / (PresetName + TEXT(".json"));

  FString JsonString;
  if (FFileHelper::LoadFileToString(JsonString, *FilePath)) {
    FVolumeSelectionSettings LoadedSettings;
    if (VolumeSelectionSettings::DeserializeFromJson(JsonString,
                                                     LoadedSettings)) {
      // Preserve the current target volume actor (don't load from preset)
      LoadedSettings.TargetVolumeActor = Settings.TargetVolumeActor;

      Settings = LoadedSettings;

      FNotificationInfo Info(FText::Format(
          LOCTEXT("PresetLoaded", "Preset '{0}' loaded successfully."),
          FText::FromString(PresetName)));
      Info.ExpireDuration = 3.0f;
      FSlateNotificationManager::Get().AddNotification(Info);

      return true;
    }
  }

  FNotificationInfo Info(
      FText::Format(LOCTEXT("PresetLoadFailed", "Failed to load preset '{0}'."),
                    FText::FromString(PresetName)));
  Info.ExpireDuration = 3.0f;
  FSlateNotificationManager::Get().AddNotification(Info);

  return false;
}

int32 FREVOWorldOrganiserModule::RemoveEmptyFolders() {
  if (!GEditor) {
    return 0;
  }

  UWorld *World = GEditor->GetEditorWorldContext().World();
  if (!World) {
    return 0;
  }

  // Collect all folders and check which are empty
  TArray<FFolder> AllFolders;
  TMap<FName, int32> FolderActorCounts;

  // Initialize all folders with 0 actors
  FActorFolders::Get().ForEachFolder(
      *World, [&AllFolders, &FolderActorCounts](const FFolder &Folder) {
        if (!Folder.GetPath().IsNone()) {
          AllFolders.Add(Folder);
          FolderActorCounts.Add(Folder.GetPath(), 0);
        }
        return true; // Continue iteration
      });

  // Count actors in each folder - must be 100% sure they are empty
  for (TActorIterator<AActor> It(World); It; ++It) {
    AActor *Actor = *It;
    if (Actor && !Actor->GetFolderPath().IsNone()) {
      FName FolderPath = Actor->GetFolderPath();
      if (FolderActorCounts.Contains(FolderPath)) {
        FolderActorCounts[FolderPath]++;
      }
    }
  }

  // Find empty folders (confirmed 0 actors)
  TArray<FFolder> EmptyFolders;
  for (const FFolder &Folder : AllFolders) {
    if (FolderActorCounts[Folder.GetPath()] == 0) {
      EmptyFolders.Add(Folder);

      // Debug: Log empty folder detection
      UE_LOG(LogTemp, Warning, TEXT("Empty folder detected: %s"),
             *Folder.GetPath().ToString());
    }
  }

  // Sort by depth (deepest first) to delete children before parents
  // This prevents Scene Outliner tree corruption from parent-child references
  EmptyFolders.Sort([](const FFolder &A, const FFolder &B) {
    const FString PathA = A.GetPath().ToString();
    const FString PathB = B.GetPath().ToString();

    // Count depth by counting slashes
    int32 DepthA = 0;
    int32 DepthB = 0;
    for (const TCHAR Char : PathA) {
      if (Char == '/')
        DepthA++;
    }
    for (const TCHAR Char : PathB) {
      if (Char == '/')
        DepthB++;
    }

    // Sort by depth descending (deepest first)
    if (DepthA != DepthB) {
      return DepthA > DepthB;
    }

    // If same depth, sort alphabetically for consistency
    return PathA > PathB;
  });

  if (EmptyFolders.Num() == 0) {
    FNotificationInfo Info(
        LOCTEXT("RemoveEmptyFolders_None",
                "No empty folders found in the World Outliner."));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
    return 0;
  }

  // Delete only first 10 empty folders per button click
  const int32 FoldersToDelete = FMath::Min(10, EmptyFolders.Num());
  const int32 RemainingAfterDeletion = EmptyFolders.Num() - FoldersToDelete;

  // Confirmation dialog
  const FText ConfirmMessage =
      RemainingAfterDeletion > 0
          ? FText::Format(
                LOCTEXT("RemoveEmptyFolders_Confirm_Partial",
                        "Delete {0} empty folder(s) from the World "
                        "Outliner?\n\n({1} empty folder(s) will remain - click "
                        "button again to continue)\n\nThis operation can be "
                        "undone with Ctrl+Z."),
                FText::AsNumber(FoldersToDelete),
                FText::AsNumber(RemainingAfterDeletion))
          : FText::Format(
                LOCTEXT(
                    "RemoveEmptyFolders_Confirm_All",
                    "Delete {0} empty folder(s) from the World "
                    "Outliner?\n\nThis operation can be undone with Ctrl+Z."),
                FText::AsNumber(FoldersToDelete));

  const EAppReturnType::Type Result =
      FMessageDialog::Open(EAppMsgType::YesNo, ConfirmMessage);

  if (Result != EAppReturnType::Yes) {
    return 0;
  }

  // Create undo transaction
  FScopedTransaction Transaction(
      LOCTEXT("RemoveEmptyFolders", "Remove Empty Folders"));

  int32 DeletedCount = 0;
  FActorFolders &ActorFolders = FActorFolders::Get();

  // Delete only first 10 folders
  for (int32 i = 0; i < FoldersToDelete; ++i) {
    ActorFolders.DeleteFolder(*World, EmptyFolders[i]);
    ++DeletedCount;
  }

  // Show result notification
  const FText ResultMessage =
      RemainingAfterDeletion > 0
          ? FText::Format(
                LOCTEXT("RemoveEmptyFolders_Success_Partial",
                        "Deleted {0} empty folder(s). {1} empty folder(s) "
                        "remaining - click button again to continue."),
                FText::AsNumber(DeletedCount),
                FText::AsNumber(RemainingAfterDeletion))
          : FText::Format(
                LOCTEXT(
                    "RemoveEmptyFolders_Success_Complete",
                    "Deleted {0} empty folder(s). All empty folders removed!"),
                FText::AsNumber(DeletedCount));

  FNotificationInfo Info(ResultMessage);
  Info.ExpireDuration = 5.0f;
  FSlateNotificationManager::Get().AddNotification(Info);

  return DeletedCount;
}

void FREVOWorldOrganiserModule::ReplaceTagInLevel(FName OldTag,
                                                    FName NewTag) {
  if (!GEditor) {
    return;
  }

  UWorld *World = GEditor->GetEditorWorldContext().World();
  if (!World) {
    return;
  }

  if (OldTag.IsNone()) {
    FNotificationInfo Info(
        LOCTEXT("ReplaceTag_NoOldTag", "No tag selected to replace."));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
    return;
  }

  if (NewTag.IsNone()) {
    FNotificationInfo Info(
        LOCTEXT("ReplaceTag_NoNewTag", "No new tag specified."));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
    return;
  }

  // Confirmation dialog
  const FText ConfirmMessage = FText::Format(
      LOCTEXT("ReplaceTag_Confirm",
              "Replace tag '{0}' with '{1}' on all actors in the "
              "level?\n\nThis operation can be undone with Ctrl+Z."),
      FText::FromName(OldTag), FText::FromName(NewTag));

  const EAppReturnType::Type Result =
      FMessageDialog::Open(EAppMsgType::YesNo, ConfirmMessage);

  if (Result != EAppReturnType::Yes) {
    return;
  }

  // Create undo transaction
  FScopedTransaction Transaction(LOCTEXT("ReplaceTag", "Replace Tag in Level"));

  int32 ModifiedActorCount = 0;

  // Iterate through all actors in the level
  for (TActorIterator<AActor> It(World); It; ++It) {
    AActor *Actor = *It;
    if (!Actor) {
      continue;
    }

    // Check if actor has the old tag
    if (Actor->Tags.Contains(OldTag)) {
      Actor->Modify();
      Actor->Tags.Remove(OldTag);
      Actor->Tags.AddUnique(NewTag);
      ++ModifiedActorCount;
    }
  }

  FNotificationInfo Info(
      FText::Format(LOCTEXT("ReplaceTag_Success",
                            "Replaced tag '{0}' with '{1}' on {2} actor(s)"),
                    FText::FromName(OldTag), FText::FromName(NewTag),
                    FText::AsNumber(ModifiedActorCount)));
  Info.ExpireDuration = 3.0f;
  FSlateNotificationManager::Get().AddNotification(Info);
}

void FREVOWorldOrganiserModule::RemoveTagFromLevel(FName TagToRemove) {
  if (!GEditor) {
    return;
  }

  UWorld *World = GEditor->GetEditorWorldContext().World();
  if (!World) {
    return;
  }

  if (TagToRemove.IsNone()) {
    FNotificationInfo Info(
        LOCTEXT("RemoveTag_NoTag", "No tag selected to remove."));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
    return;
  }

  // Confirmation dialog
  const FText ConfirmMessage = FText::Format(
      LOCTEXT("RemoveTag_Confirm",
              "Remove tag '{0}' from all actors in the level?\n\nThis "
              "operation can be undone with Ctrl+Z."),
      FText::FromName(TagToRemove));

  const EAppReturnType::Type Result =
      FMessageDialog::Open(EAppMsgType::YesNo, ConfirmMessage);

  if (Result != EAppReturnType::Yes) {
    return;
  }

  // Create undo transaction
  FScopedTransaction Transaction(LOCTEXT("RemoveTag", "Remove Tag from Level"));

  int32 ModifiedActorCount = 0;

  // Iterate through all actors in the level
  for (TActorIterator<AActor> It(World); It; ++It) {
    AActor *Actor = *It;
    if (!Actor) {
      continue;
    }

    // Check if actor has the tag
    if (Actor->Tags.Contains(TagToRemove)) {
      Actor->Modify();
      Actor->Tags.Remove(TagToRemove);
      ++ModifiedActorCount;
    }
  }

  FNotificationInfo Info(FText::Format(
      LOCTEXT("RemoveTag_Success", "Removed tag '{0}' from {1} actor(s)"),
      FText::FromName(TagToRemove), FText::AsNumber(ModifiedActorCount)));
  Info.ExpireDuration = 3.0f;
  FSlateNotificationManager::Get().AddNotification(Info);
}

int32 FREVOWorldOrganiserModule::RemoveExcludeTagsFromLevel() {
  if (!GEditor) {
    return 0;
  }

  UWorld *World = GEditor->GetEditorWorldContext().World();
  if (!World) {
    return 0;
  }

  if (Settings.ExcludeTagList.Num() == 0) {
    FNotificationInfo Info(
        LOCTEXT("RemoveExcludeTags_None", "No exclude tags to remove."));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
    return 0;
  }

  FString TagListString;
  for (int32 i = 0; i < Settings.ExcludeTagList.Num(); ++i) {
    if (i > 0) {
      TagListString += TEXT(", ");
    }
    TagListString += Settings.ExcludeTagList[i].ToString();
  }

  const FText ConfirmMessage = FText::Format(
      LOCTEXT("RemoveExcludeTags_Confirm",
              "Remove exclude tags ({0}) from all actors in the level?\n\n"
              "This operation can be undone with Ctrl+Z."),
      FText::FromString(TagListString));

  const EAppReturnType::Type Result =
      FMessageDialog::Open(EAppMsgType::YesNo, ConfirmMessage);

  if (Result != EAppReturnType::Yes) {
    return 0;
  }

  FScopedTransaction Transaction(
      LOCTEXT("RemoveExcludeTags", "Remove Exclude Tags from Level"));

  int32 ModifiedActorCount = 0;

  for (TActorIterator<AActor> It(World); It; ++It) {
    AActor *Actor = *It;
    if (!Actor) {
      continue;
    }

    bool bModified = false;
    for (const FName &Tag : Settings.ExcludeTagList) {
      if (Actor->Tags.Contains(Tag)) {
        if (!bModified) {
          Actor->Modify();
          bModified = true;
        }
        Actor->Tags.Remove(Tag);
      }
    }

    if (bModified) {
      ++ModifiedActorCount;
    }
  }

  FNotificationInfo Info(
      FText::Format(LOCTEXT("RemoveExcludeTags_Success",
                            "Removed exclude tags from {0} actor(s)"),
                    FText::AsNumber(ModifiedActorCount)));
  Info.ExpireDuration = 3.0f;
  FSlateNotificationManager::Get().AddNotification(Info);

  return ModifiedActorCount;
}

void FREVOWorldOrganiserModule::RemovePreset(const FString &PresetName) {
  if (PresetName.IsEmpty()) {
    return;
  }

  const FString FilePath = GetPresetDirectory() / (PresetName + TEXT(".json"));
  IPlatformFile &PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

  if (PlatformFile.DeleteFile(*FilePath)) {
    FNotificationInfo Info(FText::Format(
        LOCTEXT("PresetRemoved", "Preset '{0}' removed successfully."),
        FText::FromString(PresetName)));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
  } else {
    FNotificationInfo Info(FText::Format(
        LOCTEXT("PresetRemoveFailed", "Failed to remove preset '{0}'."),
        FText::FromString(PresetName)));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
  }
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FREVOWorldOrganiserModule, REVOWorldOrganiser)
