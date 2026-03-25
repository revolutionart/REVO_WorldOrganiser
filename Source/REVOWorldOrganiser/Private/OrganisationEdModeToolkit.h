// Copyright 2026, REVOLUTIONART All rights reserved

#pragma once

#include "Toolkits/BaseToolkit.h"

class FOrganisationEdModeToolkit : public FModeToolkit {
public:
  virtual void Init(const TSharedPtr<IToolkitHost> &InitToolkitHost) override;
  virtual FName GetToolkitFName() const override;
  virtual FText GetBaseToolkitName() const override;
  virtual class FEdMode *GetEditorMode() const override;
  virtual TSharedPtr<SWidget> GetInlineContent() const override;

private:
  /** Executes the mirror actors operation on selected actors */
  void ExecuteMirrorActors();

  /** Refreshes the class picker dropdown options by scanning the current level
   */
  void RefreshClassPickerOptions();

  /** Refreshes the tag picker dropdown options by scanning the current level */
  void RefreshTagOptions();

  /** Refreshes the volume picker dropdown options by scanning the current level
   */
  void RefreshVolumeOptions();

  /** Updates the folder path using the selected volume number */
  void UpdateFolderPathFromSelectedVolume();

  /** Finds and navigates through actors with the selected tag */
  void FindAndNavigateToActorsWithTag();

  /** Main toolkit widget */
  TSharedPtr<SWidget> ToolkitWidget;

  /** Mirror axis flags */
  bool bMirrorX = false;
  bool bMirrorY = false;
  bool bMirrorZ = false;

  /** Duplicate actors before mirroring */
  bool bDuplicateBeforeMirror = false;

  /** Organize duplicates into 'Move to Folder' path */
  bool bOrganizeDuplicates = false;

  /** Class picker filter dropdown options */
  TArray<TSharedPtr<UClass *>> ActorClassOptions;
  TArray<TSharedPtr<UClass *>> ComponentClassOptions;

  /** Selected classes from dropdowns (stored as pointers during runtime) */
  TArray<UClass *> SelectedActorClasses;
  TArray<UClass *> SelectedComponentClasses;

  /** Combo boxes for class selection */
  TSharedPtr<class SComboBox<TSharedPtr<UClass *>>> ActorClassComboBox;
  TSharedPtr<class SComboBox<TSharedPtr<UClass *>>> ComponentClassComboBox;

  /** Combo boxes for exclude class selection (rebuilt) */
  TSharedPtr<class SComboBox<TSharedPtr<UClass *>>> ExcludeActorClassComboBox;
  TSharedPtr<class SComboBox<TSharedPtr<UClass *>>>
      ExcludeComponentClassComboBox;

  /** Tag picker dropdown options */
  TArray<TSharedPtr<FName>> TagOptions;

  /** Combo boxes for tag filters (rebuilt) */
  TSharedPtr<class SComboBox<TSharedPtr<FName>>> IncludeTagComboBox;
  TSharedPtr<class SComboBox<TSharedPtr<FName>>> ExcludeTagComboBox;

  /** Tag utilities selection */
  TSharedPtr<class SComboBox<TSharedPtr<FName>>> ReplaceTagComboBox;
  FName SelectedReplaceTag = NAME_None;

  /** Volume picker dropdown options */
  TArray<TSharedPtr<AActor *>> VolumeOptions;

  /** Selected volume from dropdown (stored as pointer during runtime) */
  AActor *SelectedVolume = nullptr;

  /** Combo box for volume selection */
  TSharedPtr<class SComboBox<TSharedPtr<AActor *>>> VolumeComboBox;
};
