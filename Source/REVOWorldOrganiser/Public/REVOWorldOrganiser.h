// Copyright 2026, REVOLUTIONART All rights reserved

#pragma once

#include "Modules/ModuleManager.h"
#include "VolumeSelectionSettings.h"

class FUICommandList;

class FREVOWorldOrganiserModule : public IModuleInterface {
public:
  virtual void StartupModule() override;
  virtual void ShutdownModule() override;

  void REVOWorldOrganiserAll();
  void REVOWorldOrganiserBlueprints();
  void REVOWorldOrganiserLights();
  void REVOWorldOrganiserStaticMeshes();
  void REVOWorldOrganiserSkeletalMeshes();
  void REVOWorldOrganiserLevelInstances();
  void REVOWorldOrganiserPackedLevelActors();
  void REVOWorldOrganiserDecals();
  void REVOWorldOrganiserNiagaraActors();

  FVolumeSelectionSettings &GetSettings();
  const FVolumeSelectionSettings &GetSettings() const;

  void SetTargetVolumeFromSelection();
  void SpawnEmptyVolume();
  FText GetTargetVolumeLabel() const;

  int32 RunPreview();
  int32 RunSelection();
  int32 RunMoveAndOrganize();

  int32 GetPreviewCount() const;

  TArray<FString> GetPresetNames() const;
  void SavePreset(const FString &PresetName);
  bool LoadPreset(const FString &PresetName);
  void RemovePreset(const FString &PresetName);

  int32 RemoveExcludeTagsFromLevel();
  int32 RemoveEmptyFolders();
  void ReplaceTagInLevel(FName OldTag, FName NewTag);
  void RemoveTagFromLevel(FName TagToRemove);

private:
  void RegisterMenus();

  TSharedPtr<FUICommandList> CommandList;

  FVolumeSelectionSettings Settings;
  int32 PreviewCount = 0;
};
