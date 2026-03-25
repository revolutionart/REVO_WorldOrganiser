// Copyright 2026, REVOLUTIONART All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class AActor;
class UDataLayerAsset;

UENUM()
enum class EVolumeSelectionMode : uint8 {
  ContainsFully,
  Intersects,
  PivotInside
};

enum class EVolumeActorType : uint32 {
  None = 0,
  StaticMeshActors = 1 << 0,
  BlueprintActors = 1 << 1,
  LightActors = 1 << 2,
  SkeletalMeshActors = 1 << 3,
  DecalActors = 1 << 4,
  LevelInstances = 1 << 5,
  NiagaraActors = 1 << 6,
  Volumes = 1 << 7,
  AudioActors = 1 << 8,
  Cameras = 1 << 9,
  BlueprintsWithLights = 1 << 10,
  Text3DActors = 1 << 11
};

struct FVolumeSelectionSettings {
  TWeakObjectPtr<AActor> TargetVolumeActor;

  EVolumeSelectionMode SelectionMode = EVolumeSelectionMode::ContainsFully;

  float ContainsFullyThreshold = 0.75f;

  bool bUseComponentBounds = false;

  uint32 ActorTypeMask = 0;

  // Exclude filters (rebuilt)
  TArray<FString> ExcludeActorClassList;
  TArray<FString> ExcludeComponentClassList;

  // Tag filters (rebuilt)
  TArray<FName> IncludeTagList;
  TArray<FName> ExcludeTagList;

  // Class Picker Filter
  bool bEnableClassPickerFilter = false;
  TArray<FString> SelectedActorClassPaths;
  TArray<FString> SelectedComponentClassPaths;

  bool bMoveToFolder = false;
  FName TargetFolderPath;
  bool bAutoNameFolderFromVolume = true;
  bool bPreserveFolderStructure = true;
  bool bOrganizeByActorType = false;

  TObjectPtr<UDataLayerAsset> TargetDataLayer = nullptr;
};

namespace VolumeSelectionSettings {
FString SerializeToJson(const FVolumeSelectionSettings &Settings);
bool DeserializeFromJson(const FString &JsonString,
                         FVolumeSelectionSettings &OutSettings);
} // namespace VolumeSelectionSettings
