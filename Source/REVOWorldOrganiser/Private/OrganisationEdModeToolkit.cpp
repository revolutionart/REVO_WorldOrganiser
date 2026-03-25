// Copyright 2026, REVOLUTIONART All rights reserved

/**
 * OrganisationEdModeToolkit.cpp
 *
 * Implementation of the editor toolkit UI for the Volume Selection Tool.
 * This file creates the complete Slate UI for selecting and organizing actors
 * within volumes.
 *
 * Key Features:
 * - Volume selection with multiple modes (Contains Fully, Intersects, Pivot
 * Inside)
 * - Actor type filtering (Static Mesh, Blueprint, Lights, etc.)
 * - Tag-based inclusion/exclusion filtering
 * - Data Layer filtering
 * - Folder organization with browse dialog
 * - Preset save/load system
 * - Preview functionality
 */

#include "OrganisationEdModeToolkit.h"

#include "OrganisationEdMode.h"
#include "REVSelectionVolume.h"
#include "REVOWorldOrganiser.h"
#include "VolumeSelectionSettings.h"

#include "Components/BrushComponent.h"
#include "Components/LightComponent.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "Editor.h"
#include "EditorActorFolders.h"
#include "EditorModeManager.h"
#include "Engine/Level.h"
#include "Engine/Light.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "Folder.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Volume.h"
#include "Internationalization/Regex.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STreeView.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"

#define LOCTEXT_NAMESPACE "OrganisationEdModeToolkit"

namespace {
/**
 * Helper function to safely retrieve the REVOWorldOrganiser module.
 *
 * @return Pointer to the module if loaded, nullptr otherwise
 */
FREVOWorldOrganiserModule *GetSelectActorsModule() {
  if (FModuleManager::Get().IsModuleLoaded("REVOWorldOrganiser")) {
    return FModuleManager::GetModulePtr<FREVOWorldOrganiserModule>(
        "REVOWorldOrganiser");
  }

  return nullptr;
}

/**
 * Data structure representing a folder node in the folder hierarchy tree.
 * Used by the folder picker dialog to display World Outliner folders in a tree
 * view.
 */
struct FFolderTreeItem {
  FName FolderPath; // Full path of the folder (e.g., "ENV/Buildings/Exterior")
  FString FolderName; // Display name of just this folder (e.g., "Exterior")
  TArray<TSharedPtr<FFolderTreeItem>> Children; // Child folders

  FFolderTreeItem(const FName &InPath, const FString &InName)
      : FolderPath(InPath), FolderName(InName) {}
};

/**
 * Builds a hierarchical tree structure of all folders in the World Outliner.
 * This function collects ALL folders including empty ones using the
 * FActorFolders API.
 *
 * @param World The editor world to scan for folders
 * @return Root folder tree item containing the full hierarchy, or nullptr if
 * world is invalid
 */
TSharedPtr<FFolderTreeItem> BuildFolderHierarchy(UWorld *World) {
  if (!World) {
    return nullptr;
  }

  // Create root folder item to hold all top-level folders
  TSharedPtr<FFolderTreeItem> Root =
      MakeShared<FFolderTreeItem>(FName(), TEXT("/"));

  // Map for quick lookup of folder tree items by their full path
  TMap<FName, TSharedPtr<FFolderTreeItem>> FolderMap;
  FolderMap.Add(FName(), Root);

  // Collect all folders including empty ones using FActorFolders singleton
  // This is the key API that gives us empty folders that TActorIterator
  // couldn't find
  TSet<FName> AllFolderPaths;
  FActorFolders::Get().ForEachFolder(*World,
                                     [&AllFolderPaths](const FFolder &Folder) {
                                       if (!Folder.GetPath().IsNone()) {
                                         AllFolderPaths.Add(Folder.GetPath());
                                       }
                                       return true; // Continue iteration
                                     });

  // Sort folders alphabetically to ensure parent folders are processed before
  // their children (e.g., "ENV" before "ENV/Buildings" before
  // "ENV/Buildings/Exterior")
  TArray<FName> SortedFolders = AllFolderPaths.Array();
  SortedFolders.Sort([](const FName &A, const FName &B) {
    return A.ToString() < B.ToString();
  });

  // Build the parent-child hierarchy by processing each folder path
  for (const FName &FolderPath : SortedFolders) {
    FString PathString = FolderPath.ToString();
    TArray<FString> PathParts;
    PathString.ParseIntoArray(PathParts, TEXT("/"));

    if (PathParts.Num() == 0) {
      continue;
    }

    // Determine the parent folder path for this folder
    // For "ENV/Buildings/Exterior", parent is "ENV/Buildings"
    FName ParentPath; // Empty for top-level folders
    if (PathParts.Num() > 1) {
      FString ParentPathString;
      for (int32 i = 0; i < PathParts.Num() - 1; ++i) {
        if (i > 0) {
          ParentPathString += TEXT("/");
        }
        ParentPathString += PathParts[i];
      }
      ParentPath = FName(*ParentPathString);
    }

    // Create a new tree item for this folder if it doesn't already exist
    if (!FolderMap.Contains(FolderPath)) {
      // PathParts.Last() gives us just the leaf name (e.g., "Exterior" from
      // "ENV/Buildings/Exterior")
      TSharedPtr<FFolderTreeItem> NewFolder =
          MakeShared<FFolderTreeItem>(FolderPath, PathParts.Last());

      // Add this folder as a child of its parent in the hierarchy
      if (TSharedPtr<FFolderTreeItem> *ParentItem =
              FolderMap.Find(ParentPath)) {
        (*ParentItem)->Children.Add(NewFolder);
      }

      FolderMap.Add(FolderPath, NewFolder);
    }
  }

  return Root;
}

/**
 * Custom Slate widget that displays a folder picker dialog.
 * Shows a tree view of all World Outliner folders and allows the user to select
 * one. When a folder is selected, it updates the target text box in the main
 * UI.
 */
class SFolderPickerDialog : public SCompoundWidget {
public:
  SLATE_BEGIN_ARGS(SFolderPickerDialog) {}
  SLATE_END_ARGS()

  /**
   * Constructs the folder picker dialog widget.
   *
   * @param InArgs Slate arguments (unused)
   * @param InWorld World to scan for folders
   * @param InTargetTextBox Text box in main UI that will receive the selected
   * folder path
   */
  void Construct(const FArguments &InArgs, UWorld *InWorld,
                 TSharedPtr<SEditableTextBox> InTargetTextBox) {
    TargetTextBox = InTargetTextBox;
    RootItem = BuildFolderHierarchy(InWorld);

    // Sort child folders alphabetically for better UX
    if (RootItem.IsValid() && RootItem->Children.Num() > 0) {
      RootItem->Children.StableSort([](const TSharedPtr<FFolderTreeItem> &A,
                                       const TSharedPtr<FFolderTreeItem> &B) {
        return A->FolderName < B->FolderName;
      });
    }

    // Conditionally create either a tree view (if folders exist) or a message
    // (if no folders)
    TSharedRef<SWidget> ContentWidget = SNullWidget::NullWidget;

    if (RootItem.IsValid() && RootItem->Children.Num() > 0) {
      // Create tree view showing the folder hierarchy
      ContentWidget =
          SAssignNew(FolderTreeView, STreeView<TSharedPtr<FFolderTreeItem>>)
              .TreeItemsSource(&RootItem->Children)
              .OnGenerateRow(this, &SFolderPickerDialog::OnGenerateRow)
              .OnGetChildren(this, &SFolderPickerDialog::OnGetChildren)
              .OnSelectionChanged(this,
                                  &SFolderPickerDialog::OnSelectionChanged)
              .SelectionMode(ESelectionMode::Single);
    } else {
      // Show message when no folders exist in the World Outliner
      ContentWidget =
          SNew(STextBlock)
              .Text(LOCTEXT("NoFoldersFound",
                            "No folders found in the current level.\nCreate "
                            "folders in the World Outliner first."))
              .AutoWrapText(true)
              .Justification(ETextJustify::Center)
              .ColorAndOpacity(FSlateColor::UseSubduedForeground());
    }

    ChildSlot
        [SNew(SVerticalBox) +
         SVerticalBox::Slot().FillHeight(1.0f).Padding(4.0f)[ContentWidget] +
         SVerticalBox::Slot().AutoHeight().Padding(4.0f)
             [SNew(SHorizontalBox) +
              SHorizontalBox::Slot().FillWidth(1.0f)[SNullWidget::NullWidget] +
              SHorizontalBox::Slot().AutoWidth().Padding(
                  2.0f)[SNew(SButton)
                            .Text(LOCTEXT("SelectFolder", "Select"))
                            .OnClicked(this,
                                       &SFolderPickerDialog::OnSelectClicked)] +
              SHorizontalBox::Slot().AutoWidth().Padding(
                  2.0f)[SNew(SButton)
                            .Text(LOCTEXT("Cancel", "Cancel"))
                            .OnClicked(
                                this, &SFolderPickerDialog::OnCancelClicked)]]];
  }

private:
  /**
   * Callback to generate a tree row widget for a folder item.
   * Creates a row with a folder icon and the folder name.
   *
   * @param Item The folder tree item to create a row for
   * @param OwnerTable The table view that owns this row
   * @return A table row widget
   */
  TSharedRef<ITableRow>
  OnGenerateRow(TSharedPtr<FFolderTreeItem> Item,
                const TSharedRef<STableViewBase> &OwnerTable) {
    return SNew(STableRow<TSharedPtr<FFolderTreeItem>>, OwnerTable)
        [SNew(SHorizontalBox) +
         SHorizontalBox::Slot()
             .AutoWidth()
             .VAlign(VAlign_Center)
             .Padding(2.0f, 0.0f, 4.0f, 0.0f)
                 [SNew(SImage)
                      .Image(FAppStyle::GetBrush("SceneOutliner.FolderClosed"))
                      .ColorAndOpacity(FLinearColor(1.0f, 0.85f, 0.0f))] +
         SHorizontalBox::Slot()
             .AutoWidth()
             .VAlign(VAlign_Center)
             .Padding(2.0f)[SNew(STextBlock)
                                .Text(FText::FromString(Item->FolderName))]];
  }

  /**
   * Callback to get child items for tree view expansion.
   *
   * @param Item The parent folder item
   * @param OutChildren Array to populate with child folder items
   */
  void OnGetChildren(TSharedPtr<FFolderTreeItem> Item,
                     TArray<TSharedPtr<FFolderTreeItem>> &OutChildren) {
    if (Item.IsValid()) {
      OutChildren = Item->Children;
    }
  }

  /**
   * Callback when a folder is selected in the tree view.
   * Stores the selected folder for later use when "Select" button is clicked.
   *
   * @param Item The selected folder item
   * @param SelectInfo How the selection occurred (mouse click, keyboard, etc.)
   */
  void OnSelectionChanged(TSharedPtr<FFolderTreeItem> Item,
                          ESelectInfo::Type SelectInfo) {
    SelectedFolder = Item;
  }

  /**
   * Handler for the Select button click.
   * Updates the target text box with the selected folder path and closes the
   * dialog.
   *
   * @return FReply::Handled() to indicate the click was handled
   */
  FReply OnSelectClicked() {
    if (SelectedFolder.IsValid() && TargetTextBox.IsValid()) {
      // Update the text box in the main UI with the selected folder path
      TargetTextBox->SetText(FText::FromName(SelectedFolder->FolderPath));

      // Update the module settings to persist the selection
      FREVOWorldOrganiserModule *Module = GetSelectActorsModule();
      if (Module) {
        Module->GetSettings().TargetFolderPath = SelectedFolder->FolderPath;
      }
    }

    CloseDialog();
    return FReply::Handled();
  }

  /**
   * Handler for the Cancel button click.
   * Closes the dialog without making changes.
   *
   * @return FReply::Handled() to indicate the click was handled
   */
  FReply OnCancelClicked() {
    CloseDialog();
    return FReply::Handled();
  }

  /**
   * Helper method to close the dialog window.
   * Finds the parent window and requests its destruction.
   */
  void CloseDialog() {
    TSharedPtr<SWindow> ParentWindow =
        FSlateApplication::Get().FindWidgetWindow(AsShared());
    if (ParentWindow.IsValid()) {
      ParentWindow->RequestDestroyWindow();
    }
  }

  // Member variables
  TSharedPtr<FFolderTreeItem> RootItem; // Root of the folder hierarchy tree
  TSharedPtr<FFolderTreeItem>
      SelectedFolder; // Currently selected folder in the tree
  TSharedPtr<STreeView<TSharedPtr<FFolderTreeItem>>>
      FolderTreeView; // Tree view widget
  TSharedPtr<SEditableTextBox>
      TargetTextBox; // Reference to main UI text box to update
};
} // namespace

/**
 * Initializes the editor mode toolkit and constructs the complete Slate UI.
 * This is the main entry point that builds all the widgets, settings panels,
 * and action buttons.
 *
 * @param InitToolkitHost The toolkit host that will contain this toolkit
 */
void FOrganisationEdModeToolkit::Init(
    const TSharedPtr<IToolkitHost> &InitToolkitHost) {
  FREVOWorldOrganiserModule *Module = GetSelectActorsModule();

  // Text box references for capturing in lambdas
  TSharedPtr<SEditableTextBox> ReplaceTagTextBox;
  TSharedPtr<SEditableTextBox> FolderPathTextBox;

  // PresetNameTextBox needs special handling for capture by reference in
  // lambdas Created on heap so it can be safely captured and used across
  // multiple lambda callbacks
  TSharedPtr<SEditableTextBox> *PresetNameTextBoxPtr =
      new TSharedPtr<SEditableTextBox>();

  ToolkitWidget =
      SNew(SScrollBox) +
      SScrollBox::Slot()
          [SNew(SVerticalBox)
           // ===== HEADER =====
           + SVerticalBox::Slot().AutoHeight().Padding(
                 6.0f)[SNew(STextBlock)
                           .Text(LOCTEXT("OrganisationModeTitle",
                                         "Volume Selection Tool"))
                           .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))] +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f)[SNew(SSeparator)]
           // ===== PRESET MANAGEMENT SECTION =====
           // Allows saving/loading/removing configuration presets
           +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f)
               [SNew(SHorizontalBox) +
                SHorizontalBox::Slot().AutoWidth().Padding(2.0f)[
                    // Dropdown menu button to load existing presets
                    SNew(SComboButton)
                        .ToolTipText(LOCTEXT(
                            "LoadPresetTooltip",
                            "Load a previously saved preset configuration"))
                        .ButtonContent()[SNew(STextBlock)
                                             .Text(LOCTEXT("LoadPreset",
                                                           "Load Preset"))]
                        .OnGetMenuContent_Lambda([Module,
                                                  PresetNameTextBoxPtr]() {
                          FMenuBuilder MenuBuilder(true, nullptr);
                          if (Module) {
                            const TArray<FString> PresetNames =
                                Module->GetPresetNames();
                            for (const FString &Name : PresetNames) {
                              MenuBuilder.AddMenuEntry(
                                  FText::FromString(Name), FText::GetEmpty(),
                                  FSlateIcon(),
                                  FUIAction(FExecuteAction::CreateLambda(
                                      [Module, Name, PresetNameTextBoxPtr]() {
                                        Module->LoadPreset(Name);
                                        if (PresetNameTextBoxPtr &&
                                            PresetNameTextBoxPtr->IsValid()) {
                                          (*PresetNameTextBoxPtr)
                                              ->SetText(
                                                  FText::FromString(Name));
                                        }
                                      })));
                            }
                          }
                          return MenuBuilder.MakeWidget();
                        })] +
                SHorizontalBox::Slot().FillWidth(1.0f).Padding(
                    2.0f)[SAssignNew(*PresetNameTextBoxPtr, SEditableTextBox)
                              .HintText(LOCTEXT("PresetName", "Preset Name"))] +
                SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
                    [SNew(SButton)
                         .ToolTipText(LOCTEXT("SavePresetTooltip",
                                              "Save current settings as a "
                                              "preset with the specified name"))
                         .Text(LOCTEXT("SavePreset", "Save Preset"))
                         .OnClicked_Lambda([Module, PresetNameTextBoxPtr]() {
                           if (Module) {
                             const FString Name =
                                 (PresetNameTextBoxPtr &&
                                  PresetNameTextBoxPtr->IsValid())
                                     ? (*PresetNameTextBoxPtr)
                                           ->GetText()
                                           .ToString()
                                     : TEXT("Default");
                             Module->SavePreset(Name.IsEmpty() ? TEXT("Default")
                                                               : Name);
                           }
                           return FReply::Handled();
                         })] +
                SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
                    [SNew(SButton)
                         .ToolTipText(LOCTEXT(
                             "RemovePresetTooltip",
                             "Remove the preset with the specified name "
                             "(Default preset cannot be removed)"))
                         .Text(LOCTEXT("RemovePreset", "Remove Preset"))
                         .IsEnabled_Lambda([PresetNameTextBoxPtr]() {
                           if (PresetNameTextBoxPtr &&
                               PresetNameTextBoxPtr->IsValid()) {
                             const FString Name =
                                 (*PresetNameTextBoxPtr)->GetText().ToString();
                             return !Name.IsEmpty() &&
                                    !Name.Equals(TEXT("Default"),
                                                 ESearchCase::IgnoreCase);
                           }
                           return false;
                         })
                         .OnClicked_Lambda([Module, PresetNameTextBoxPtr]() {
                           if (Module && PresetNameTextBoxPtr &&
                               PresetNameTextBoxPtr->IsValid()) {
                             const FString Name =
                                 (*PresetNameTextBoxPtr)->GetText().ToString();
                             if (!Name.IsEmpty() &&
                                 !Name.Equals(TEXT("Default"),
                                              ESearchCase::IgnoreCase)) {
                               Module->RemovePreset(Name);
                               (*PresetNameTextBoxPtr)
                                   ->SetText(FText::GetEmpty());
                             }
                           }
                           return FReply::Handled();
                         })]]

           // ===== VOLUME SELECTION SECTION =====
           // Dedicated section for selecting the target volume actor
           +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f, 6.0f, 6.0f, 2.0f)
               [SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("DetailsView.CategoryTop"))
                    .BorderBackgroundColor(
                        FLinearColor(0.02f, 0.02f, 0.02f, 1.0f))
                    .Padding(FMargin(6.0f, 8.0f))
                        [SNew(SHorizontalBox) +
                         SHorizontalBox::Slot()
                             .AutoWidth()
                             .VAlign(VAlign_Center)
                             .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                 [SNew(SImage)
                                      .Image(FAppStyle::GetBrush(
                                          "Icons.ChevronDown"))
                                      .ColorAndOpacity(
                                          FSlateColor::UseForeground())] +
                         SHorizontalBox::Slot().VAlign(VAlign_Center)
                             [SNew(STextBlock)
                                  .Text(LOCTEXT("VolumeSelectionLabel",
                                                "Volume Selection"))
                                  .Font(FCoreStyle::GetDefaultFontStyle(
                                      "Bold", 10))]]] +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f, 6.0f, 6.0f, 2.0f)
               [SNew(SHorizontalBox) +
                SHorizontalBox::Slot().AutoWidth().Padding(2.0f).VAlign(
                    VAlign_Center)
                    [SNew(SButton)
                         .ToolTipText(
                             LOCTEXT("UseSelectedVolumeTooltip",
                                     "Use the currently selected volume actor "
                                     "in the level as the target volume"))
                         .Text(LOCTEXT("UseSelected", "Use Selected"))
                         .OnClicked_Lambda([Module, this]() {
                           if (Module) {
                             Module->SetTargetVolumeFromSelection();
                             RefreshVolumeOptions();
                             if (Module->GetSettings()
                                     .bAutoNameFolderFromVolume) {
                               UpdateFolderPathFromSelectedVolume();
                             }
                           }
                           return FReply::Handled();
                         })] +
                SHorizontalBox::Slot().AutoWidth().Padding(2.0f).VAlign(
                    VAlign_Center)
                    [SNew(SButton)
                         .ToolTipText(
                             LOCTEXT("ClearVolumeTooltip",
                                     "Clear the selected volume to work with "
                                     "manually selected actors"))
                         .Text(LOCTEXT("ClearVolume", "Clear Volume Selection"))
                         .OnClicked_Lambda([Module, this]() {
                           if (Module) {
                             Module->GetSettings().TargetVolumeActor = nullptr;
                           }
                           RefreshVolumeOptions();
                           return FReply::Handled();
                         })
                         .IsEnabled_Lambda([Module]() {
                           return Module &&
                                  Module->GetSettings()
                                          .TargetVolumeActor.Get() != nullptr;
                         })] +
                SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(8.0f, 2.0f)
                    .VAlign(VAlign_Center)
                        [SNew(STextBlock)
                             .Text_Lambda([Module]() {
                               if (!Module) {
                                 return LOCTEXT("VolumeNone",
                                                "No Volume Selected");
                               }

                               const AActor *Target =
                                   Module->GetSettings()
                                       .TargetVolumeActor.Get();
                               if (!Target) {
                                 return LOCTEXT("VolumeNotSelected",
                                                "No Volume Selected");
                               }

                               // Validate that the selected actor is actually a
                               // usable volume
                               bool bIsValid = Target->IsA<AVolume>();
                               if (!bIsValid) {
                                 const UBrushComponent *Brush =
                                     Target->FindComponentByClass<
                                         UBrushComponent>();
                                 bIsValid = Brush != nullptr &&
                                            Brush->Bounds.GetBox().IsValid;
                               }

                               if (!bIsValid) {
                                 return FText::Format(
                                     LOCTEXT("VolumeInvalid",
                                             "Invalid Volume: {0}"),
                                     FText::FromString(
                                         Target->GetActorLabel()));
                               }

                               // Display only the volume name (without folder
                               // path)
                               return FText::FromString(
                                   Target->GetActorLabel());
                             })
                             .ColorAndOpacity_Lambda([Module]() {
                               if (!Module) {
                                 return FSlateColor(FLinearColor::Red);
                               }

                               const AActor *Target =
                                   Module->GetSettings()
                                       .TargetVolumeActor.Get();
                               if (!Target) {
                                 return FSlateColor(FLinearColor::Red);
                               }

                               // Check if it's a valid volume
                               bool bIsValid = Target->IsA<AVolume>();
                               if (!bIsValid) {
                                 const UBrushComponent *Brush =
                                     Target->FindComponentByClass<
                                         UBrushComponent>();
                                 bIsValid = Brush != nullptr &&
                                            Brush->Bounds.GetBox().IsValid;
                               }

                               return bIsValid ? FSlateColor::UseForeground()
                                               : FSlateColor(FLinearColor::Red);
                             })]] +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f, 2.0f, 6.0f, 2.0f)
               [SNew(SHorizontalBox) +
                SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f).VAlign(
                    VAlign_Center)
                    [SAssignNew(VolumeComboBox, SComboBox<TSharedPtr<AActor *>>)
                         .ToolTipText(LOCTEXT(
                             "VolumePickerTooltip",
                             "Select a REVSelectionVolume from the level"))
                         .OptionsSource(&VolumeOptions)
                         .IsEnabled_Lambda(
                             [this]() { return VolumeOptions.Num() > 0; })
                         .OnGenerateWidget_Lambda(
                             [](TSharedPtr<AActor *> Item) {
                               if (Item.IsValid() && *Item && IsValid(*Item)) {
                                 return SNew(STextBlock)
                                     .Text(FText::FromString(
                                         (*Item)->GetActorLabel()));
                               }
                               return SNew(STextBlock)
                                   .Text(LOCTEXT("NoVolumes", "No Volumes"));
                             })
                         .OnSelectionChanged_Lambda(
                             [this, Module](TSharedPtr<AActor *> NewSelection,
                                            ESelectInfo::Type SelectInfo) {
                               if (NewSelection.IsValid() && *NewSelection &&
                                   IsValid(*NewSelection) && Module) {
                                 SelectedVolume = *NewSelection;
                                 Module->GetSettings().TargetVolumeActor =
                                     SelectedVolume;
                                 if (Module->GetSettings()
                                         .bAutoNameFolderFromVolume) {
                                   UpdateFolderPathFromSelectedVolume();
                                 }
                               }
                             })[SNew(STextBlock)
                                    .Text_Lambda([this, Module]() {
                                      if (VolumeOptions.Num() == 0) {
                                        return LOCTEXT(
                                            "NoVolumesInLevel",
                                            "No REVSelectionVolume in level");
                                      }
                                      if (!Module) {
                                        return LOCTEXT("NoVolume",
                                                       "Select a volume...");
                                      }
                                      const AActor *Target =
                                          Module->GetSettings()
                                              .TargetVolumeActor.Get();
                                      if (Target) {
                                        return FText::FromString(
                                            Target->GetActorLabel());
                                      }
                                      return LOCTEXT("SelectVolume",
                                                     "Select a volume...");
                                    })
                                    .ColorAndOpacity_Lambda([this]() {
                                      if (VolumeOptions.Num() == 0) {
                                        return FSlateColor(
                                            FLinearColor(0.5f, 0.5f, 0.5f));
                                      }
                                      return FSlateColor::UseForeground();
                                    })]] +
                SHorizontalBox::Slot().AutoWidth().Padding(2.0f).VAlign(
                    VAlign_Center)
                    [SNew(SButton)
                         .ToolTipText(
                             LOCTEXT("RefreshVolumesTooltip",
                                     "Refresh the list of REVSelectionVolume "
                                     "actors in the level"))
                         .Text(LOCTEXT("Refresh", "Refresh"))
                         .OnClicked_Lambda([this]() {
                           RefreshVolumeOptions();
                           return FReply::Handled();
                         })]] +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f, 2.0f, 6.0f, 6.0f)
               [SNew(SButton)
                    .ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>(
                        "PrimaryButton"))
                    .ToolTipText(LOCTEXT("SpawnEmptyVolumeTooltip",
                                         "Spawn a new empty volume at world "
                                         "origin (0,0,0) with no collision"))
                    .Text(LOCTEXT("SpawnEmptyVolume", "Spawn Empty Volume"))
                    .HAlign(HAlign_Center)
                    .ContentPadding(FMargin(16.0f, 6.0f))
                    .OnClicked_Lambda([Module]() {
                      if (Module) {
                        Module->SpawnEmptyVolume();
                      }
                      return FReply::Handled();
                    })]

           // ===== SELECTION OPTIONS SECTION =====
           +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f, 6.0f, 6.0f, 2.0f)
               [SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("DetailsView.CategoryTop"))
                    .BorderBackgroundColor(
                        FLinearColor(0.02f, 0.02f, 0.02f, 1.0f))
                    .Padding(FMargin(6.0f, 8.0f))
                        [SNew(SHorizontalBox) +
                         SHorizontalBox::Slot()
                             .AutoWidth()
                             .VAlign(VAlign_Center)
                             .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                 [SNew(SImage)
                                      .Image(FAppStyle::GetBrush(
                                          "Icons.ChevronDown"))
                                      .ColorAndOpacity(
                                          FSlateColor::UseForeground())] +
                         SHorizontalBox::Slot().VAlign(VAlign_Center)
                             [SNew(STextBlock)
                                  .Text(LOCTEXT("SelectionOptionsLabel",
                                                "Selection Options"))
                                  .Font(FCoreStyle::GetDefaultFontStyle("Bold",
                                                                        10))]]]
           // ===== SELECTION MODE =====
           +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f)
               [SNew(SHorizontalBox) +
                SHorizontalBox::Slot().AutoWidth().Padding(2.0f).VAlign(
                    VAlign_Center)[SNew(STextBlock)
                                       .Text(LOCTEXT("SelectionModeLabel",
                                                     "Selection Mode:"))] +
                SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
                    [SNew(SCheckBox)
                         .Style(FAppStyle::Get(), "RadioButton")
                         .ToolTipText(
                             LOCTEXT("ContainsFullyTooltip",
                                     "Select actors based on how much of their "
                                     "volume is inside the selection volume.\n"
                                     "Use the slider to set minimum "
                                     "containment threshold (100% = fully "
                                     "inside, 0% = any overlap)"))
                         .IsChecked_Lambda([Module]() {
                           return Module &&
                                          Module->GetSettings().SelectionMode ==
                                              EVolumeSelectionMode::
                                                  ContainsFully
                                      ? ECheckBoxState::Checked
                                      : ECheckBoxState::Unchecked;
                         })
                         .OnCheckStateChanged_Lambda(
                             [Module](ECheckBoxState State) {
                               if (Module && State == ECheckBoxState::Checked) {
                                 Module->GetSettings().SelectionMode =
                                     EVolumeSelectionMode::ContainsFully;
                               }
                             })[SNew(STextBlock)
                                    .Text(LOCTEXT("ContainsFully",
                                                  "Contains Fully"))]] +
                SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(2.0f, 2.0f, 8.0f, 2.0f)
                    .VAlign(VAlign_Center)
                        [SNew(STextBlock)
                             .Text(LOCTEXT("ThresholdLabel", "Min %"))
                             .ToolTipText(LOCTEXT(
                                 "ThresholdTooltip",
                                 "Minimum percentage of actor volume that must "
                                 "be inside the selection volume.\n"
                                 "100% = Only fully contained actors\n"
                                 "0% = Any actor with any overlap"))] +
                SHorizontalBox::Slot().FillWidth(0.3f).Padding(2.0f).VAlign(
                    VAlign_Center)
                    [SNew(SSlider)
                         .Value_Lambda([Module]() {
                           return Module ? Module->GetSettings()
                                               .ContainsFullyThreshold
                                         : 0.75f;
                         })
                         .OnValueChanged_Lambda([Module](float NewValue) {
                           if (Module) {
                             // Round to nearest 25% increment
                             float RoundedValue =
                                 FMath::RoundToFloat(NewValue * 4.0f) / 4.0f;
                             Module->GetSettings().ContainsFullyThreshold =
                                 RoundedValue;
                           }
                         })
                         .StepSize(0.25f)
                         .MinValue(0.0f)
                         .MaxValue(1.0f)] +
                SHorizontalBox::Slot().AutoWidth().Padding(2.0f).VAlign(
                    VAlign_Center)[SNew(STextBlock).Text_Lambda([Module]() {
                  if (Module) {
                    const float Threshold =
                        Module->GetSettings().ContainsFullyThreshold;
                    return FText::AsPercent(
                        Threshold,
                        &FNumberFormattingOptions::DefaultNoGrouping());
                  }
                  return FText::FromString(TEXT("75%"));
                })] +
                SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
                    [SNew(SCheckBox)
                         .Style(FAppStyle::Get(), "RadioButton")
                         .ToolTipText(LOCTEXT(
                             "IntersectsTooltip",
                             "Select actors that overlap with the volume (any "
                             "part of actor bounds inside)"))
                         .IsChecked_Lambda([Module]() {
                           return Module &&
                                          Module->GetSettings().SelectionMode ==
                                              EVolumeSelectionMode::Intersects
                                      ? ECheckBoxState::Checked
                                      : ECheckBoxState::Unchecked;
                         })
                         .OnCheckStateChanged_Lambda([Module](
                                                         ECheckBoxState State) {
                           if (Module && State == ECheckBoxState::Checked) {
                             Module->GetSettings().SelectionMode =
                                 EVolumeSelectionMode::Intersects;
                           }
                         })[SNew(STextBlock)
                                .Text(LOCTEXT("Intersects", "Intersects"))]] +
                SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
                    [SNew(SCheckBox)
                         .Style(FAppStyle::Get(), "RadioButton")
                         .ToolTipText(LOCTEXT("PivotInsideTooltip",
                                              "Select actors whose pivot point "
                                              "(origin) is inside the volume"))
                         .IsChecked_Lambda([Module]() {
                           return Module &&
                                          Module->GetSettings().SelectionMode ==
                                              EVolumeSelectionMode::PivotInside
                                      ? ECheckBoxState::Checked
                                      : ECheckBoxState::Unchecked;
                         })
                         .OnCheckStateChanged_Lambda(
                             [Module](ECheckBoxState State) {
                               if (Module && State == ECheckBoxState::Checked) {
                                 Module->GetSettings().SelectionMode =
                                     EVolumeSelectionMode::PivotInside;
                               }
                             })[SNew(STextBlock)
                                    .Text(LOCTEXT("PivotInside",
                                                  "Pivot Inside"))]]] +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f)
               [SNew(SHorizontalBox) +
                SHorizontalBox::Slot().AutoWidth().Padding(2.0f).VAlign(
                    VAlign_Center)[SNew(STextBlock)
                                       .Text(
                                           LOCTEXT("ComponentBoundsModeLabel",
                                                   "Component Bounds Mode:"))] +
                SHorizontalBox::Slot().AutoWidth().Padding(2.0f).VAlign(
                    VAlign_Center)
                    [SNew(SCheckBox)
                         .ToolTipText(LOCTEXT(
                             "UseComponentBoundsTooltip",
                             "Use individual component bounds instead of actor "
                             "bounds. Useful for actors with complex "
                             "hierarchies or multiple mesh components."))
                         .IsChecked_Lambda([Module]() {
                           return Module && Module->GetSettings()
                                                .bUseComponentBounds
                                      ? ECheckBoxState::Checked
                                      : ECheckBoxState::Unchecked;
                         })
                         .OnCheckStateChanged_Lambda(
                             [Module](ECheckBoxState State) {
                               if (Module) {
                                 Module->GetSettings().bUseComponentBounds =
                                     (State == ECheckBoxState::Checked);
                               }
                             })[SNew(STextBlock)
                                    .Text(LOCTEXT("UseComponentBounds",
                                                  "Use Component Bounds"))
                                    .Margin(FMargin(6.0f, 0.0f, 8.0f, 0.0f))]]]
           // ===== FILTERS CATEGORY HEADER =====
           // All filtering options for narrowing down actor selection
           +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f, 6.0f, 6.0f, 2.0f)
               [SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("DetailsView.CategoryTop"))
                    .BorderBackgroundColor(
                        FLinearColor(0.02f, 0.02f, 0.02f, 1.0f))
                    .Padding(FMargin(6.0f, 8.0f))
                        [SNew(SHorizontalBox) +
                         SHorizontalBox::Slot()
                             .AutoWidth()
                             .VAlign(VAlign_Center)
                             .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                 [SNew(SImage)
                                      .Image(
                                          FAppStyle::GetBrush("Icons.Filter"))
                                      .ColorAndOpacity(
                                          FSlateColor::UseForeground())] +
                         SHorizontalBox::Slot().VAlign(VAlign_Center)
                             [SNew(STextBlock)
                                  .Text(LOCTEXT("FiltersLabel", "Filters"))
                                  .Font(FCoreStyle::GetDefaultFontStyle("Bold",
                                                                        10))]]]
           // ===== ACTOR TYPE FILTERS =====
           // Checkboxes for including specific actor types
           +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f)
               [SNew(SVerticalBox) +
                SVerticalBox::Slot().AutoHeight().Padding(2.0f)
                    [SNew(STextBlock)
                         .Text(
                             LOCTEXT("ActorTypeFilters", "Actor Type Filters"))
                         .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))] +
                SVerticalBox::Slot().AutoHeight().Padding(2.0f, 4.0f)
                    [SNew(SCheckBox)
                         .ToolTipText(
                             LOCTEXT("AllActorTypesTooltip",
                                     "Toggle all actor type filters at once"))
                         .OnCheckStateChanged_Lambda([Module](ECheckBoxState
                                                                  NewState) {
                           if (Module) {
                             if (NewState == ECheckBoxState::Checked) {
                               Module->GetSettings().ActorTypeMask =
                                   static_cast<uint32>(
                                       EVolumeActorType::StaticMeshActors) |
                                   static_cast<uint32>(
                                       EVolumeActorType::BlueprintActors) |
                                   static_cast<uint32>(
                                       EVolumeActorType::LightActors) |
                                   static_cast<uint32>(
                                       EVolumeActorType::SkeletalMeshActors) |
                                   static_cast<uint32>(
                                       EVolumeActorType::DecalActors) |
                                   static_cast<uint32>(
                                       EVolumeActorType::LevelInstances) |
                                   static_cast<uint32>(
                                       EVolumeActorType::NiagaraActors) |
                                   static_cast<uint32>(
                                       EVolumeActorType::Volumes) |
                                   static_cast<uint32>(
                                       EVolumeActorType::AudioActors) |
                                   static_cast<uint32>(
                                       EVolumeActorType::Cameras) |
                                   static_cast<uint32>(
                                       EVolumeActorType::Text3DActors);
                             } else {
                               Module->GetSettings().ActorTypeMask = 0;
                             }
                           }
                         })
                         .IsChecked_Lambda([Module]() {
                           if (Module) {
                             const uint32 AllTypes =
                                 static_cast<uint32>(
                                     EVolumeActorType::StaticMeshActors) |
                                 static_cast<uint32>(
                                     EVolumeActorType::BlueprintActors) |
                                 static_cast<uint32>(
                                     EVolumeActorType::LightActors) |
                                 static_cast<uint32>(
                                     EVolumeActorType::SkeletalMeshActors) |
                                 static_cast<uint32>(
                                     EVolumeActorType::DecalActors) |
                                 static_cast<uint32>(
                                     EVolumeActorType::LevelInstances) |
                                 static_cast<uint32>(
                                     EVolumeActorType::NiagaraActors) |
                                 static_cast<uint32>(
                                     EVolumeActorType::Volumes) |
                                 static_cast<uint32>(
                                     EVolumeActorType::AudioActors) |
                                 static_cast<uint32>(
                                     EVolumeActorType::Cameras) |
                                 static_cast<uint32>(
                                     EVolumeActorType::Text3DActors);
                             return (Module->GetSettings().ActorTypeMask ==
                                     AllTypes)
                                        ? ECheckBoxState::Checked
                                        : ECheckBoxState::Unchecked;
                           }
                           return ECheckBoxState::Unchecked;
                         })[SNew(STextBlock)
                                .Text(
                                    LOCTEXT("AllActorTypes", "All Actor Types"))
                                .Margin(FMargin(6.0f, 0.0f, 8.0f, 0.0f))]] +
                SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(8.0f, 4.0f, 2.0f, 2.0f)
                        [SNew(SHorizontalBox) +
                         SHorizontalBox::Slot().FillWidth(1.0f)
                             [SNew(SVerticalBox) +
                              SVerticalBox::Slot().AutoHeight().Padding(0.0f,
                                                                        2.0f)
                                  [SNew(SCheckBox)
                                       .ToolTipText(LOCTEXT(
                                           "StaticMeshActorsTooltip",
                                           "Include actors with "
                                           "static mesh components "
                                           "(non-movable geometry)"))
                                       .OnCheckStateChanged_Lambda(
                                           [Module](ECheckBoxState NewState) {
                                             if (Module) {
                                               if (NewState ==
                                                   ECheckBoxState::Checked) {
                                                 Module->GetSettings()
                                                     .ActorTypeMask |=
                                                     static_cast<uint32>(
                                                         EVolumeActorType::
                                                             StaticMeshActors);
                                               } else {
                                                 Module->GetSettings()
                                                     .ActorTypeMask &=
                                                     ~static_cast<uint32>(
                                                         EVolumeActorType::
                                                             StaticMeshActors);
                                               }
                                             }
                                           })
                                       .IsChecked_Lambda([Module]() {
                                         if (Module) {
                                           return (Module->GetSettings()
                                                       .ActorTypeMask &
                                                   static_cast<uint32>(
                                                       EVolumeActorType::
                                                           StaticMeshActors)) !=
                                                          0
                                                      ? ECheckBoxState::Checked
                                                      : ECheckBoxState::
                                                            Unchecked;
                                         }
                                         return ECheckBoxState::Unchecked;
                                       })[SNew(STextBlock)
                                              .Text(LOCTEXT("StaticMeshActors",
                                                            "Static Mesh "
                                                            "Actors"))
                                              .Margin(FMargin(6.0f, 0.0f, 8.0f,
                                                              0.0f))]] +
                              SVerticalBox::Slot().AutoHeight().Padding(0.0f,
                                                                        2.0f)
                                  [SNew(SCheckBox)
                                       .ToolTipText(LOCTEXT(
                                           "BlueprintActorsTooltip",
                                           "Include Blueprint-based actors "
                                           "(custom gameplay actors created "
                                           "from Blueprints)"))
                                       .OnCheckStateChanged_Lambda(
                                           [Module](ECheckBoxState NewState) {
                                             if (Module) {
                                               if (NewState ==
                                                   ECheckBoxState::Checked) {
                                                 Module->GetSettings()
                                                     .ActorTypeMask |=
                                                     static_cast<uint32>(
                                                         EVolumeActorType::
                                                             BlueprintActors);
                                               } else {
                                                 Module->GetSettings()
                                                     .ActorTypeMask &=
                                                     ~static_cast<uint32>(
                                                         EVolumeActorType::
                                                             BlueprintActors);
                                               }
                                             }
                                           })
                                       .IsChecked_Lambda([Module]() {
                                         if (Module) {
                                           return (Module->GetSettings()
                                                       .ActorTypeMask &
                                                   static_cast<uint32>(
                                                       EVolumeActorType::
                                                           BlueprintActors)) !=
                                                          0
                                                      ? ECheckBoxState::Checked
                                                      : ECheckBoxState::
                                                            Unchecked;
                                         }
                                         return ECheckBoxState::Unchecked;
                                       })[SNew(STextBlock)
                                              .Text(LOCTEXT("BlueprintActors",
                                                            "Blueprint "
                                                            "Actors"))
                                              .Margin(FMargin(6.0f, 0.0f, 8.0f,
                                                              0.0f))]] +
                              SVerticalBox::Slot().AutoHeight().Padding(0.0f,
                                                                        2.0f)
                                  [SNew(SCheckBox)
                                       .ToolTipText(LOCTEXT(
                                           "LightActorsTooltip",
                                           "Include lighting actors (point "
                                           "lights, spot lights, directional "
                                           "lights, etc.)"))
                                       .OnCheckStateChanged_Lambda(
                                           [Module](ECheckBoxState NewState) {
                                             if (Module) {
                                               if (NewState ==
                                                   ECheckBoxState::Checked) {
                                                 Module->GetSettings()
                                                     .ActorTypeMask |=
                                                     static_cast<uint32>(
                                                         EVolumeActorType::
                                                             LightActors);
                                               } else {
                                                 Module->GetSettings()
                                                     .ActorTypeMask &=
                                                     ~static_cast<uint32>(
                                                         EVolumeActorType::
                                                             LightActors);
                                               }
                                             }
                                           })
                                       .IsChecked_Lambda([Module]() {
                                         if (Module) {
                                           return (Module->GetSettings()
                                                       .ActorTypeMask &
                                                   static_cast<uint32>(
                                                       EVolumeActorType::
                                                           LightActors)) != 0
                                                      ? ECheckBoxState::Checked
                                                      : ECheckBoxState::
                                                            Unchecked;
                                         }
                                         return ECheckBoxState::Unchecked;
                                       })[SNew(STextBlock)
                                              .Text(LOCTEXT("LightActors",
                                                            "Light Actors"))
                                              .Margin(FMargin(6.0f, 0.0f, 8.0f,
                                                              0.0f))]] +
                              SVerticalBox::Slot().AutoHeight().Padding(0.0f,
                                                                        2.0f)
                                  [SNew(SCheckBox)
                                       .ToolTipText(LOCTEXT(
                                           "SkeletalMeshActorsTooltip",
                                           "Include actors with skeletal mesh "
                                           "components (animated characters, "
                                           "creatures)"))
                                       .OnCheckStateChanged_Lambda(
                                           [Module](ECheckBoxState NewState) {
                                             if (Module) {
                                               if (NewState ==
                                                   ECheckBoxState::Checked) {
                                                 Module->GetSettings()
                                                     .ActorTypeMask |=
                                                     static_cast<uint32>(
                                                         EVolumeActorType::
                                                             SkeletalMeshActors);
                                               } else {
                                                 Module->GetSettings()
                                                     .ActorTypeMask &=
                                                     ~static_cast<uint32>(
                                                         EVolumeActorType::
                                                             SkeletalMeshActors);
                                               }
                                             }
                                           })
                                       .IsChecked_Lambda([Module]() {
                                         if (Module) {
                                           return (Module->GetSettings()
                                                       .ActorTypeMask &
                                                   static_cast<uint32>(
                                                       EVolumeActorType::
                                                           SkeletalMeshActors)) !=
                                                          0
                                                      ? ECheckBoxState::Checked
                                                      : ECheckBoxState::
                                                            Unchecked;
                                         }
                                         return ECheckBoxState::Unchecked;
                                       })[SNew(STextBlock)
                                              .Text(LOCTEXT(
                                                  "SkeletalMeshActors",
                                                  "Skeletal Mesh Actors"))
                                              .Margin(FMargin(6.0f, 0.0f, 8.0f,
                                                              0.0f))]] +
                              SVerticalBox::Slot().AutoHeight().Padding(0.0f,
                                                                        2.0f)
                                  [SNew(SCheckBox)
                                       .ToolTipText(LOCTEXT(
                                           "DecalActorsTooltip",
                                           "Include decal actors (projected "
                                           "textures on surfaces like stains, "
                                           "graffiti)"))
                                       .OnCheckStateChanged_Lambda(
                                           [Module](ECheckBoxState NewState) {
                                             if (Module) {
                                               if (NewState ==
                                                   ECheckBoxState::Checked) {
                                                 Module->GetSettings()
                                                     .ActorTypeMask |=
                                                     static_cast<uint32>(
                                                         EVolumeActorType::
                                                             DecalActors);
                                               } else {
                                                 Module->GetSettings()
                                                     .ActorTypeMask &=
                                                     ~static_cast<uint32>(
                                                         EVolumeActorType::
                                                             DecalActors);
                                               }
                                             }
                                           })
                                       .IsChecked_Lambda([Module]() {
                                         if (Module) {
                                           return (Module->GetSettings()
                                                       .ActorTypeMask &
                                                   static_cast<uint32>(
                                                       EVolumeActorType::
                                                           DecalActors)) != 0
                                                      ? ECheckBoxState::Checked
                                                      : ECheckBoxState::
                                                            Unchecked;
                                         }
                                         return ECheckBoxState::Unchecked;
                                       })[SNew(STextBlock)
                                              .Text(LOCTEXT("DecalActors",
                                                            "Decal Actors"))
                                              .Margin(FMargin(6.0f, 0.0f, 8.0f,
                                                              0.0f))]] +
                              SVerticalBox::Slot().AutoHeight().Padding(0.0f,
                                                                        2.0f)
                                  [SNew(SCheckBox)
                                       .ToolTipText(LOCTEXT(
                                           "LevelInstancesTooltip",
                                           "Include Level Instance actors "
                                           "(embedded level instances)"))
                                       .OnCheckStateChanged_Lambda(
                                           [Module](ECheckBoxState NewState) {
                                             if (Module) {
                                               if (NewState ==
                                                   ECheckBoxState::Checked) {
                                                 Module->GetSettings()
                                                     .ActorTypeMask |=
                                                     static_cast<uint32>(
                                                         EVolumeActorType::
                                                             LevelInstances);
                                               } else {
                                                 Module->GetSettings()
                                                     .ActorTypeMask &=
                                                     ~static_cast<uint32>(
                                                         EVolumeActorType::
                                                             LevelInstances);
                                               }
                                             }
                                           })
                                       .IsChecked_Lambda([Module]() {
                                         if (Module) {
                                           return (Module->GetSettings()
                                                       .ActorTypeMask &
                                                   static_cast<uint32>(
                                                       EVolumeActorType::
                                                           LevelInstances)) != 0
                                                      ? ECheckBoxState::Checked
                                                      : ECheckBoxState::
                                                            Unchecked;
                                         }
                                         return ECheckBoxState::Unchecked;
                                       })[SNew(STextBlock)
                                              .Text(LOCTEXT("LevelInstances",
                                                            "Level Instances"))
                                              .Margin(FMargin(6.0f, 0.0f, 8.0f,
                                                              0.0f))]]] +
                         SHorizontalBox::Slot().FillWidth(1.0f)
                             [SNew(SVerticalBox) +
                              SVerticalBox::Slot().AutoHeight().Padding(0.0f,
                                                                        2.0f)
                                  [SNew(SCheckBox)
                                       .ToolTipText(
                                           LOCTEXT("NiagaraActorsTooltip",
                                                   "Include Niagara particle "
                                                   "system actors (visual "
                                                   "effects, particles)"))
                                       .OnCheckStateChanged_Lambda(
                                           [Module](ECheckBoxState NewState) {
                                             if (Module) {
                                               if (NewState ==
                                                   ECheckBoxState::Checked) {
                                                 Module->GetSettings()
                                                     .ActorTypeMask |=
                                                     static_cast<uint32>(
                                                         EVolumeActorType::
                                                             NiagaraActors);
                                               } else {
                                                 Module->GetSettings()
                                                     .ActorTypeMask &=
                                                     ~static_cast<uint32>(
                                                         EVolumeActorType::
                                                             NiagaraActors);
                                               }
                                             }
                                           })
                                       .IsChecked_Lambda([Module]() {
                                         if (Module) {
                                           return (Module->GetSettings()
                                                       .ActorTypeMask &
                                                   static_cast<uint32>(
                                                       EVolumeActorType::
                                                           NiagaraActors)) != 0
                                                      ? ECheckBoxState::Checked
                                                      : ECheckBoxState::
                                                            Unchecked;
                                         }
                                         return ECheckBoxState::Unchecked;
                                       })[SNew(STextBlock)
                                              .Text(LOCTEXT("NiagaraActors",
                                                            "Niagara Actors"))
                                              .Margin(FMargin(6.0f, 0.0f, 8.0f,
                                                              0.0f))]] +
                              SVerticalBox::Slot().AutoHeight().Padding(0.0f,
                                                                        2.0f)
                                  [SNew(SCheckBox)
                                       .ToolTipText(LOCTEXT(
                                           "VolumesTooltip",
                                           "Include volume actors (trigger "
                                           "volumes, blocking volumes, etc.)"))
                                       .OnCheckStateChanged_Lambda(
                                           [Module](ECheckBoxState NewState) {
                                             if (Module) {
                                               if (NewState ==
                                                   ECheckBoxState::Checked) {
                                                 Module->GetSettings()
                                                     .ActorTypeMask |=
                                                     static_cast<uint32>(
                                                         EVolumeActorType::
                                                             Volumes);
                                               } else {
                                                 Module->GetSettings()
                                                     .ActorTypeMask &=
                                                     ~static_cast<uint32>(
                                                         EVolumeActorType::
                                                             Volumes);
                                               }
                                             }
                                           })
                                       .IsChecked_Lambda([Module]() {
                                         if (Module) {
                                           return (Module->GetSettings()
                                                       .ActorTypeMask &
                                                   static_cast<uint32>(
                                                       EVolumeActorType::
                                                           Volumes)) != 0
                                                      ? ECheckBoxState::Checked
                                                      : ECheckBoxState::
                                                            Unchecked;
                                         }
                                         return ECheckBoxState::Unchecked;
                                       })[SNew(STextBlock)
                                              .Text(LOCTEXT("Volumes", "Volume"
                                                                       "s"))
                                              .Margin(FMargin(6.0f, 0.0f, 8.0f,
                                                              0.0f))]] +
                              SVerticalBox::Slot().AutoHeight().Padding(0.0f,
                                                                        2.0f)
                                  [SNew(SCheckBox)
                                       .ToolTipText(LOCTEXT(
                                           "AudioActorsTooltip",
                                           "Include audio actors (ambient "
                                           "sounds, sound emitters)"))
                                       .OnCheckStateChanged_Lambda(
                                           [Module](ECheckBoxState NewState) {
                                             if (Module) {
                                               if (NewState ==
                                                   ECheckBoxState::Checked) {
                                                 Module->GetSettings()
                                                     .ActorTypeMask |=
                                                     static_cast<uint32>(
                                                         EVolumeActorType::
                                                             AudioActors);
                                               } else {
                                                 Module->GetSettings()
                                                     .ActorTypeMask &=
                                                     ~static_cast<uint32>(
                                                         EVolumeActorType::
                                                             AudioActors);
                                               }
                                             }
                                           })
                                       .IsChecked_Lambda([Module]() {
                                         if (Module) {
                                           return (Module->GetSettings()
                                                       .ActorTypeMask &
                                                   static_cast<uint32>(
                                                       EVolumeActorType::
                                                           AudioActors)) != 0
                                                      ? ECheckBoxState::Checked
                                                      : ECheckBoxState::
                                                            Unchecked;
                                         }
                                         return ECheckBoxState::Unchecked;
                                       })[SNew(STextBlock)
                                              .Text(LOCTEXT("AudioActors",
                                                            "Audio Actors"))
                                              .Margin(FMargin(6.0f, 0.0f, 8.0f,
                                                              0.0f))]] +
                              SVerticalBox::Slot().AutoHeight().Padding(0.0f,
                                                                        2.0f)
                                  [SNew(SCheckBox)
                                       .ToolTipText(LOCTEXT(
                                           "CamerasTooltip",
                                           "Include camera actors (cinematic "
                                           "cameras, player cameras)"))
                                       .OnCheckStateChanged_Lambda(
                                           [Module](ECheckBoxState NewState) {
                                             if (Module) {
                                               if (NewState ==
                                                   ECheckBoxState::Checked) {
                                                 Module->GetSettings()
                                                     .ActorTypeMask |=
                                                     static_cast<uint32>(
                                                         EVolumeActorType::
                                                             Cameras);
                                               } else {
                                                 Module->GetSettings()
                                                     .ActorTypeMask &=
                                                     ~static_cast<uint32>(
                                                         EVolumeActorType::
                                                             Cameras);
                                               }
                                             }
                                           })
                                       .IsChecked_Lambda([Module]() {
                                         if (Module) {
                                           return (Module->GetSettings()
                                                       .ActorTypeMask &
                                                   static_cast<uint32>(
                                                       EVolumeActorType::
                                                           Cameras)) != 0
                                                      ? ECheckBoxState::Checked
                                                      : ECheckBoxState::
                                                            Unchecked;
                                         }
                                         return ECheckBoxState::Unchecked;
                                       })[SNew(STextBlock)
                                              .Text(LOCTEXT("Cameras", "Camera"
                                                                       "s"))
                                              .Margin(FMargin(6.0f, 0.0f, 8.0f,
                                                              0.0f))]] +
                              SVerticalBox::Slot().AutoHeight().Padding(0.0f,
                                                                        2.0f)
                                  [SNew(SCheckBox)
                                       .ToolTipText(LOCTEXT(
                                           "BlueprintsWithLightsTooltip",
                                           "Include only Blueprint actors "
                                           "that contain lights (checks "
                                           "nested ChildActorComponents)"))
                                       .OnCheckStateChanged_Lambda(
                                           [Module](ECheckBoxState NewState) {
                                             if (Module) {
                                               if (NewState ==
                                                   ECheckBoxState::Checked) {
                                                 Module->GetSettings()
                                                     .ActorTypeMask |= static_cast<
                                                     uint32>(
                                                     EVolumeActorType::
                                                         BlueprintsWithLights);
                                               } else {
                                                 Module->GetSettings()
                                                     .ActorTypeMask &=
                                                     ~static_cast<uint32>(
                                                         EVolumeActorType::
                                                             BlueprintsWithLights);
                                               }
                                             }
                                           })
                                       .IsChecked_Lambda([Module]() {
                                         if (Module) {
                                           return (Module->GetSettings()
                                                       .ActorTypeMask &
                                                   static_cast<uint32>(
                                                       EVolumeActorType::
                                                           BlueprintsWithLights)) !=
                                                          0
                                                      ? ECheckBoxState::Checked
                                                      : ECheckBoxState::
                                                            Unchecked;
                                         }
                                         return ECheckBoxState::Unchecked;
                                       })[SNew(STextBlock)
                                              .Text(LOCTEXT(
                                                  "BlueprintsWithLights",
                                                  "Blueprints w/ Lights"))
                                              .Margin(FMargin(6.0f, 0.0f, 8.0f,
                                                              0.0f))]] +
                              SVerticalBox::Slot().AutoHeight().Padding(0.0f,
                                                                        2.0f)
                                  [SNew(SCheckBox)
                                       .ToolTipText(
                                           LOCTEXT("Text3DActorsTooltip",
                                                   "Include Text3D actors (3D "
                                                   "text rendering)"))
                                       .OnCheckStateChanged_Lambda(
                                           [Module](ECheckBoxState NewState) {
                                             if (Module) {
                                               if (NewState ==
                                                   ECheckBoxState::Checked) {
                                                 Module->GetSettings()
                                                     .ActorTypeMask |=
                                                     static_cast<uint32>(
                                                         EVolumeActorType::
                                                             Text3DActors);
                                               } else {
                                                 Module->GetSettings()
                                                     .ActorTypeMask &=
                                                     ~static_cast<uint32>(
                                                         EVolumeActorType::
                                                             Text3DActors);
                                               }
                                             }
                                           })
                                       .IsChecked_Lambda([Module]() {
                                         if (Module) {
                                           return (Module->GetSettings()
                                                       .ActorTypeMask &
                                                   static_cast<uint32>(
                                                       EVolumeActorType::
                                                           Text3DActors)) != 0
                                                      ? ECheckBoxState::Checked
                                                      : ECheckBoxState::
                                                            Unchecked;
                                         }
                                         return ECheckBoxState::Unchecked;
                                       })[SNew(STextBlock)
                                              .Text(LOCTEXT("Text3DActors",
                                                            "Text3D Actors"))
                                              .Margin(FMargin(6.0f, 0.0f, 8.0f,
                                                              0.0f))]]]]]
           // ===== EXCLUDE CLASSES + TAG FILTERS (REBUILT) =====
           + SVerticalBox::Slot().AutoHeight().Padding(6.0f, 12.0f, 6.0f, 6.0f)
                 [SNew(SVerticalBox) +
                  SVerticalBox::Slot().AutoHeight().Padding(2.0f)
                      [SNew(SHorizontalBox) +
                       SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
                           [SNew(SVerticalBox) +
                            SVerticalBox::Slot().AutoHeight().Padding(0.0f,
                                                                      2.0f)
                                [SNew(STextBlock)
                                     .Text(LOCTEXT("ExcludeActorClassesTitle",
                                                   "Exclude Actor Classes"))
                                     .Font(FCoreStyle::GetDefaultFontStyle(
                                         "Italic", 8))] +
                            SVerticalBox::Slot().AutoHeight()
                                [SNew(SHorizontalBox) +
                                 SHorizontalBox::Slot().FillWidth(1.0f).Padding(
                                     2.0f)
                                     [SAssignNew(
                                          ExcludeActorClassComboBox,
                                          SComboBox<TSharedPtr<UClass *>>)
                                          .ToolTipText(LOCTEXT(
                                              "ExcludeActorClassTooltip",
                                              "Select an actor class to "
                                              "exclude"))
                                          .OptionsSource(&ActorClassOptions)
                                          .OnSelectionChanged_Lambda(
                                              [Module,
                                               this](TSharedPtr<UClass *>
                                                         SelectedClass,
                                                     ESelectInfo::Type) {
                                                if (Module &&
                                                    SelectedClass.IsValid() &&
                                                    *SelectedClass) {
                                                  Module->GetSettings()
                                                      .ExcludeActorClassList
                                                      .AddUnique(
                                                          (*SelectedClass)
                                                              ->GetPathName());
                                                  if (ExcludeActorClassComboBox
                                                          .IsValid()) {
                                                    ExcludeActorClassComboBox
                                                        ->SetSelectedItem(
                                                            nullptr);
                                                  }
                                                }
                                              })
                                          .OnGenerateWidget_Lambda(
                                              [](TSharedPtr<UClass *> Item) {
                                                FString DisplayName = TEXT(
                                                    "Select actor class...");
                                                if (Item.IsValid() && *Item) {
                                                  DisplayName =
                                                      (*Item)->GetName();
                                                  if (DisplayName.EndsWith(
                                                          TEXT("_C"))) {
                                                    DisplayName.LeftChopInline(
                                                        2);
                                                  }
                                                }
                                                return SNew(STextBlock)
                                                    .Text(FText::FromString(
                                                        DisplayName));
                                              })[SNew(STextBlock)
                                                     .Text(LOCTEXT(
                                                         "ExcludeActorClassSele"
                                                         "ct",
                                                         "Select actor "
                                                         "class..."))]] +
                                 SHorizontalBox::Slot().AutoWidth().Padding(
                                     2.0f)
                                     [SNew(SButton)
                                          .Text(LOCTEXT("ExcludeActorClassClea"
                                                        "r",
                                                        "Clear"))
                                          .OnClicked_Lambda([Module]() {
                                            if (Module) {
                                              Module->GetSettings()
                                                  .ExcludeActorClassList
                                                  .Empty();
                                            }
                                            return FReply::Handled();
                                          })
                                          .IsEnabled_Lambda([Module]() {
                                            return Module &&
                                                   Module->GetSettings()
                                                           .ExcludeActorClassList
                                                           .Num() > 0;
                                          })]] +
                            SVerticalBox::Slot().AutoHeight().Padding(
                                2.0f)[SNew(STextBlock)
                                          .Text_Lambda([Module]() {
                                            if (!Module ||
                                                Module->GetSettings()
                                                        .ExcludeActorClassList
                                                        .Num() == 0) {
                                              return LOCTEXT(
                                                  "NoExcludedActors",
                                                  "No exclude actor classes");
                                            }

                                            FString ClassList;
                                            for (int32 i = 0;
                                                 i < Module->GetSettings()
                                                         .ExcludeActorClassList
                                                         .Num();
                                                 ++i) {
                                              if (i > 0) {
                                                ClassList += TEXT(", ");
                                              }
                                              FString ClassPath =
                                                  Module->GetSettings()
                                                      .ExcludeActorClassList[i];
                                              int32 DotIndex = INDEX_NONE;
                                              if (ClassPath.FindLastChar(
                                                      '.', DotIndex)) {
                                                ClassPath = ClassPath.RightChop(
                                                    DotIndex + 1);
                                              }
                                              if (ClassPath.EndsWith(
                                                      TEXT("_C"))) {
                                                ClassPath.LeftChopInline(2);
                                              }
                                              ClassList += ClassPath;
                                            }
                                            return FText::FromString(ClassList);
                                          })
                                          .AutoWrapText(true)
                                          .ColorAndOpacity(FLinearColor(
                                              0.7f, 0.7f, 0.7f))]] +
                       SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
                           [SNew(SVerticalBox) +
                            SVerticalBox::Slot().AutoHeight().Padding(0.0f,
                                                                      2.0f)
                                [SNew(STextBlock)
                                     .Text(LOCTEXT("ExcludeComponentClassesTitl"
                                                   "e",
                                                   "Exclude Component Classes"))
                                     .Font(FCoreStyle::GetDefaultFontStyle(
                                         "Italic", 8))] +
                            SVerticalBox::Slot().AutoHeight()
                                [SNew(SHorizontalBox) +
                                 SHorizontalBox::Slot().FillWidth(1.0f).Padding(
                                     2.0f)
                                     [SAssignNew(
                                          ExcludeComponentClassComboBox,
                                          SComboBox<TSharedPtr<UClass *>>)
                                          .ToolTipText(LOCTEXT(
                                              "ExcludeComponentClassTooltip",
                                              "Select a component class to "
                                              "exclude"))
                                          .OptionsSource(&ComponentClassOptions)
                                          .OnSelectionChanged_Lambda(
                                              [Module,
                                               this](TSharedPtr<UClass *>
                                                         SelectedClass,
                                                     ESelectInfo::Type) {
                                                if (Module &&
                                                    SelectedClass.IsValid() &&
                                                    *SelectedClass) {
                                                  Module->GetSettings()
                                                      .ExcludeComponentClassList
                                                      .AddUnique(
                                                          (*SelectedClass)
                                                              ->GetPathName());
                                                  if (ExcludeComponentClassComboBox
                                                          .IsValid()) {
                                                    ExcludeComponentClassComboBox
                                                        ->SetSelectedItem(
                                                            nullptr);
                                                  }
                                                }
                                              })
                                          .OnGenerateWidget_Lambda(
                                              [](TSharedPtr<UClass *> Item) {
                                                FString DisplayName =
                                                    TEXT("Select component "
                                                         "class...");
                                                if (Item.IsValid() && *Item) {
                                                  DisplayName =
                                                      (*Item)->GetName();
                                                  if (DisplayName.EndsWith(
                                                          TEXT("_C"))) {
                                                    DisplayName.LeftChopInline(
                                                        2);
                                                  }
                                                }
                                                return SNew(STextBlock)
                                                    .Text(FText::FromString(
                                                        DisplayName));
                                              })[SNew(STextBlock)
                                                     .Text(LOCTEXT(
                                                         "ExcludeComponentClass"
                                                         "Select",
                                                         "Select component "
                                                         "class..."))]] +
                                 SHorizontalBox::Slot().AutoWidth().Padding(
                                     2.0f)
                                     [SNew(SButton)
                                          .Text(LOCTEXT("ExcludeComponentClassC"
                                                        "lear",
                                                        "Clear"))
                                          .OnClicked_Lambda([Module]() {
                                            if (Module) {
                                              Module->GetSettings()
                                                  .ExcludeComponentClassList
                                                  .Empty();
                                            }
                                            return FReply::Handled();
                                          })
                                          .IsEnabled_Lambda([Module]() {
                                            return Module &&
                                                   Module->GetSettings()
                                                           .ExcludeComponentClassList
                                                           .Num() > 0;
                                          })]] +
                            SVerticalBox::Slot().AutoHeight().Padding(2.0f)
                                [SNew(STextBlock)
                                     .Text_Lambda([Module]() {
                                       if (!Module ||
                                           Module->GetSettings()
                                                   .ExcludeComponentClassList
                                                   .Num() == 0) {
                                         return LOCTEXT(
                                             "NoExcludedComponents",
                                             "No exclude component classes");
                                       }

                                       FString ClassList;
                                       for (int32 i = 0;
                                            i < Module->GetSettings()
                                                    .ExcludeComponentClassList
                                                    .Num();
                                            ++i) {
                                         if (i > 0) {
                                           ClassList += TEXT(", ");
                                         }
                                         FString ClassPath =
                                             Module->GetSettings()
                                                 .ExcludeComponentClassList[i];
                                         int32 DotIndex = INDEX_NONE;
                                         if (ClassPath.FindLastChar('.',
                                                                    DotIndex)) {
                                           ClassPath = ClassPath.RightChop(
                                               DotIndex + 1);
                                         }
                                         if (ClassPath.EndsWith(TEXT("_C"))) {
                                           ClassPath.LeftChopInline(2);
                                         }
                                         ClassList += ClassPath;
                                       }
                                       return FText::FromString(ClassList);
                                     })
                                     .AutoWrapText(true)
                                     .ColorAndOpacity(
                                         FLinearColor(0.7f, 0.7f, 0.7f))]]] +
                  SVerticalBox::Slot().AutoHeight().Padding(6.0f, 6.0f, 6.0f,
                                                            2.0f)
                      [SNew(SHorizontalBox) +
                       SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
                           [SNew(SVerticalBox) +
                            SVerticalBox::Slot().AutoHeight().Padding(0.0f,
                                                                      2.0f)
                                [SNew(STextBlock)
                                     .Text(LOCTEXT("IncludeTagsTitle", "Include"
                                                                       " Tags"))
                                     .Font(FCoreStyle::GetDefaultFontStyle(
                                         "Italic", 8))] +
                            SVerticalBox::Slot().AutoHeight()
                                [SNew(SHorizontalBox) +
                                 SHorizontalBox::Slot().FillWidth(1.0f).Padding(
                                     2.0f)
                                     [SAssignNew(IncludeTagComboBox,
                                                 SComboBox<TSharedPtr<FName>>)
                                          .ToolTipText(LOCTEXT(
                                              "IncludeTagsTooltip",
                                              "Select a tag to include"))
                                          .OptionsSource(&TagOptions)
                                          .OnSelectionChanged_Lambda(
                                              [Module, this](
                                                  TSharedPtr<FName> SelectedTag,
                                                  ESelectInfo::Type) {
                                                if (Module &&
                                                    SelectedTag.IsValid()) {
                                                  Module->GetSettings()
                                                      .IncludeTagList.AddUnique(
                                                          *SelectedTag);
                                                  if (IncludeTagComboBox
                                                          .IsValid()) {
                                                    IncludeTagComboBox
                                                        ->SetSelectedItem(
                                                            nullptr);
                                                  }
                                                }
                                              })
                                          .OnGenerateWidget_Lambda(
                                              [](TSharedPtr<FName> Item) {
                                                FString DisplayName =
                                                    TEXT("Select tag...");
                                                if (Item.IsValid()) {
                                                  DisplayName =
                                                      Item->ToString();
                                                }
                                                return SNew(STextBlock)
                                                    .Text(FText::FromString(
                                                        DisplayName));
                                              })[SNew(STextBlock)
                                                     .Text(LOCTEXT(
                                                         "IncludeTagsSelect",
                                                         "Select tag to "
                                                         "include..."))]] +
                                 SHorizontalBox::Slot().AutoWidth().Padding(
                                     2.0f)[SNew(SButton)
                                               .Text(LOCTEXT("IncludeTagsClear",
                                                             "Clear"))
                                               .OnClicked_Lambda([Module]() {
                                                 if (Module) {
                                                   Module->GetSettings()
                                                       .IncludeTagList.Empty();
                                                 }
                                                 return FReply::Handled();
                                               })
                                               .IsEnabled_Lambda([Module]() {
                                                 return Module &&
                                                        Module->GetSettings()
                                                                .IncludeTagList
                                                                .Num() > 0;
                                               })]] +
                            SVerticalBox::Slot().AutoHeight().Padding(
                                2.0f)[SNew(STextBlock)
                                          .Text_Lambda([Module]() {
                                            if (!Module ||
                                                Module->GetSettings()
                                                        .IncludeTagList.Num() ==
                                                    0) {
                                              return LOCTEXT("NoIncludeTags",
                                                             "No include tags");
                                            }
                                            FString TagList;
                                            for (int32 i = 0;
                                                 i < Module->GetSettings()
                                                         .IncludeTagList.Num();
                                                 ++i) {
                                              if (i > 0) {
                                                TagList += TEXT(", ");
                                              }
                                              TagList += Module->GetSettings()
                                                             .IncludeTagList[i]
                                                             .ToString();
                                            }
                                            return FText::FromString(TagList);
                                          })
                                          .AutoWrapText(true)
                                          .ColorAndOpacity(FLinearColor(
                                              0.7f, 0.7f, 0.7f))]] +
                       SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
                           [SNew(SVerticalBox) +
                            SVerticalBox::Slot().AutoHeight().Padding(0.0f,
                                                                      2.0f)
                                [SNew(STextBlock)
                                     .Text(LOCTEXT("ExcludeTagsTitle", "Exclude"
                                                                       " Tags"))
                                     .Font(FCoreStyle::GetDefaultFontStyle(
                                         "Italic", 8))] +
                            SVerticalBox::Slot().AutoHeight()
                                [SNew(SHorizontalBox) +
                                 SHorizontalBox::Slot().FillWidth(1.0f).Padding(
                                     2.0f)
                                     [SAssignNew(ExcludeTagComboBox,
                                                 SComboBox<TSharedPtr<FName>>)
                                          .ToolTipText(LOCTEXT(
                                              "ExcludeTagsTooltip",
                                              "Select a tag to exclude"))
                                          .OptionsSource(&TagOptions)
                                          .OnSelectionChanged_Lambda(
                                              [Module, this](
                                                  TSharedPtr<FName> SelectedTag,
                                                  ESelectInfo::Type) {
                                                if (Module &&
                                                    SelectedTag.IsValid()) {
                                                  Module->GetSettings()
                                                      .ExcludeTagList.AddUnique(
                                                          *SelectedTag);
                                                  if (ExcludeTagComboBox
                                                          .IsValid()) {
                                                    ExcludeTagComboBox
                                                        ->SetSelectedItem(
                                                            nullptr);
                                                  }
                                                }
                                              })
                                          .OnGenerateWidget_Lambda(
                                              [](TSharedPtr<FName> Item) {
                                                FString DisplayName =
                                                    TEXT("Select tag...");
                                                if (Item.IsValid()) {
                                                  DisplayName =
                                                      Item->ToString();
                                                }
                                                return SNew(STextBlock)
                                                    .Text(FText::FromString(
                                                        DisplayName));
                                              })[SNew(STextBlock)
                                                     .Text(LOCTEXT(
                                                         "ExcludeTagsSelect",
                                                         "Select tag to "
                                                         "exclude..."))]] +
                                 SHorizontalBox::Slot().AutoWidth().Padding(
                                     2.0f)[SNew(SButton)
                                               .Text(LOCTEXT("ExcludeTagsClear",
                                                             "Clear"))
                                               .OnClicked_Lambda([Module]() {
                                                 if (Module) {
                                                   Module->GetSettings()
                                                       .ExcludeTagList.Empty();
                                                 }
                                                 return FReply::Handled();
                                               })
                                               .IsEnabled_Lambda([Module]() {
                                                 return Module &&
                                                        Module->GetSettings()
                                                                .ExcludeTagList
                                                                .Num() > 0;
                                               })]] +
                            SVerticalBox::Slot().AutoHeight().Padding(
                                2.0f)[SNew(STextBlock)
                                          .Text_Lambda([Module]() {
                                            if (!Module ||
                                                Module->GetSettings()
                                                        .ExcludeTagList.Num() ==
                                                    0) {
                                              return LOCTEXT("NoExcludeTags",
                                                             "No exclude tags");
                                            }
                                            FString TagList;
                                            for (int32 i = 0;
                                                 i < Module->GetSettings()
                                                         .ExcludeTagList.Num();
                                                 ++i) {
                                              if (i > 0) {
                                                TagList += TEXT(", ");
                                              }
                                              TagList += Module->GetSettings()
                                                             .ExcludeTagList[i]
                                                             .ToString();
                                            }
                                            return FText::FromString(TagList);
                                          })
                                          .AutoWrapText(true)
                                          .ColorAndOpacity(FLinearColor(
                                              0.7f, 0.7f, 0.7f))]]] +
                  SVerticalBox::Slot().AutoHeight().Padding(
                      6.0f, 6.0f, 6.0f,
                      2.0f)[SNew(SButton)
                                .ButtonStyle(&FAppStyle::Get()
                                                  .GetWidgetStyle<FButtonStyle>(
                                                      "PrimaryButton"))
                                .HAlign(HAlign_Center)
                                .ContentPadding(FMargin(16.0f, 6.0f))
                                .Text(LOCTEXT("RemoveExcludeTagsButton",
                                              "Remove Exclude Tags from Level"))
                                .IsEnabled_Lambda([Module]() {
                                  return Module &&
                                         Module->GetSettings()
                                                 .ExcludeTagList.Num() > 0;
                                })
                                .OnClicked_Lambda([Module]() {
                                  if (Module) {
                                    Module->RemoveExcludeTagsFromLevel();
                                  }
                                  return FReply::Handled();
                                })]]
           // ===== TAG UTILITIES SECTION =====
           +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f, 6.0f, 6.0f, 2.0f)
               [SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("DetailsView.CategoryTop"))
                    .BorderBackgroundColor(
                        FLinearColor(0.02f, 0.02f, 0.02f, 1.0f))
                    .Padding(FMargin(6.0f, 8.0f))
                        [SNew(SHorizontalBox) +
                         SHorizontalBox::Slot()
                             .AutoWidth()
                             .VAlign(VAlign_Center)
                             .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                 [SNew(SImage)
                                      .Image(FAppStyle::GetBrush("Icons.Tag"))
                                      .ColorAndOpacity(
                                          FSlateColor::UseForeground())] +
                         SHorizontalBox::Slot().VAlign(VAlign_Center)
                             [SNew(STextBlock)
                                  .Text(LOCTEXT("TagUtilitiesLabel",
                                                "Tag Utilities"))
                                  .Font(FCoreStyle::GetDefaultFontStyle(
                                      "Bold", 10))]]] +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f, 0.0f, 6.0f, 4.0f)
               [SNew(STextBlock)
                    .Text(LOCTEXT(
                        "TagUtilitiesDescription",
                        "Replace or remove a specific tag across the entire "
                        "level."))
                    .AutoWrapText(true)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground())] +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f)
               [SNew(SHorizontalBox) +
                SHorizontalBox::Slot().FillWidth(0.55f).Padding(2.0f)
                    [SAssignNew(ReplaceTagComboBox,
                                SComboBox<TSharedPtr<FName>>)
                         .ToolTipText(LOCTEXT("SelectReplaceTagTooltip",
                                              "Select a tag to replace"))
                         .OptionsSource(&TagOptions)
                         .OnSelectionChanged_Lambda(
                             [this](TSharedPtr<FName> SelectedTag,
                                    ESelectInfo::Type SelectInfo) {
                               if (SelectedTag.IsValid()) {
                                 SelectedReplaceTag = *SelectedTag;
                               }
                             })
                         .OnGenerateWidget_Lambda([](TSharedPtr<FName> Item) {
                           FString DisplayName = TEXT("Select tag...");
                           if (Item.IsValid()) {
                             DisplayName = Item->ToString();
                           }
                           return SNew(STextBlock)
                               .Text(FText::FromString(DisplayName));
                         })[SNew(STextBlock).Text_Lambda([this]() {
                           return SelectedReplaceTag.IsNone()
                                      ? LOCTEXT("SelectTagToReplace",
                                                "Select tag to replace...")
                                      : FText::FromName(SelectedReplaceTag);
                         })]] +
                SHorizontalBox::Slot().FillWidth(1.0f).Padding(
                    2.0f)[SAssignNew(ReplaceTagTextBox, SEditableTextBox)
                              .HintText(LOCTEXT("ReplaceWithTagHint",
                                                "Replace with tag..."))]] +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f, 2.0f, 6.0f, 2.0f)
               [SNew(SHorizontalBox) +
                SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
                    [SNew(SButton)
                         .HAlign(HAlign_Center)
                         .Text(LOCTEXT("ReplaceTagInLevel",
                                       "Replace Tag in Level"))
                         .OnClicked_Lambda([Module, this, ReplaceTagTextBox]() {
                           if (!Module) {
                             return FReply::Handled();
                           }

                           if (SelectedReplaceTag.IsNone()) {
                             FNotificationInfo Info(
                                 LOCTEXT("ReplaceTag_NoOldTag",
                                         "No tag selected to replace."));
                             Info.ExpireDuration = 3.0f;
                             FSlateNotificationManager::Get().AddNotification(
                                 Info);
                             return FReply::Handled();
                           }

                           const FString NewTagString =
                               ReplaceTagTextBox.IsValid()
                                   ? ReplaceTagTextBox->GetText().ToString()
                                   : FString();
                           if (NewTagString.IsEmpty()) {
                             FNotificationInfo Info(
                                 LOCTEXT("ReplaceTag_NoNewTag",
                                         "No new tag specified."));
                             Info.ExpireDuration = 3.0f;
                             FSlateNotificationManager::Get().AddNotification(
                                 Info);
                             return FReply::Handled();
                           }

                           Module->ReplaceTagInLevel(SelectedReplaceTag,
                                                     FName(*NewTagString));
                           return FReply::Handled();
                         })] +
                SHorizontalBox::Slot().AutoWidth().Padding(
                    2.0f)[SNew(SButton)
                              .HAlign(HAlign_Center)
                              .Text(LOCTEXT("RemoveTagFromLevel",
                                            "Remove Tag from Level"))
                              .OnClicked_Lambda([Module, this]() {
                                if (!Module) {
                                  return FReply::Handled();
                                }

                                if (SelectedReplaceTag.IsNone()) {
                                  FNotificationInfo Info(
                                      LOCTEXT("RemoveTag_NoTag",
                                              "No tag selected to remove."));
                                  Info.ExpireDuration = 3.0f;
                                  FSlateNotificationManager::Get()
                                      .AddNotification(Info);
                                  return FReply::Handled();
                                }

                                Module->RemoveTagFromLevel(SelectedReplaceTag);
                                return FReply::Handled();
                              })]] +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f, 2.0f, 6.0f, 2.0f)
               [SNew(SButton)
                    .ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>(
                        "PrimaryButton"))
                    .HAlign(HAlign_Center)
                    .ContentPadding(FMargin(16.0f, 6.0f))
                    .Text(LOCTEXT("RefreshTagsList", "Refresh Tags List"))
                    .OnClicked_Lambda([this]() {
                      RefreshTagOptions();
                      return FReply::Handled();
                    })] +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f, 2.0f, 6.0f, 6.0f)
               [SNew(SButton)
                    .HAlign(HAlign_Center)
                    .ContentPadding(FMargin(16.0f, 6.0f))
                    .Text(LOCTEXT("FindActorsWithTag", "Find Actors with Tag"))
                    .OnClicked_Lambda([this]() {
                      FindAndNavigateToActorsWithTag();
                      return FReply::Handled();
                    })]
           // ===== CLASS PICKER FILTER =====
           +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f, 6.0f, 6.0f, 2.0f)
               [SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("DetailsView.CategoryTop"))
                    .BorderBackgroundColor(
                        FLinearColor(0.02f, 0.02f, 0.02f, 1.0f))
                    .Padding(FMargin(6.0f, 8.0f))
                        [SNew(SHorizontalBox) +
                         SHorizontalBox::Slot()
                             .AutoWidth()
                             .VAlign(VAlign_Center)
                             .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                 [SNew(SImage)
                                      .Image(
                                          FAppStyle::GetBrush("Icons.Filter"))
                                      .ColorAndOpacity(
                                          FSlateColor::UseForeground())] +
                         SHorizontalBox::Slot().VAlign(VAlign_Center)
                             [SNew(STextBlock)
                                  .Text(LOCTEXT("ClassFilterLabel",
                                                "Class Filter"))
                                  .Font(FCoreStyle::GetDefaultFontStyle(
                                      "Bold", 10))]]] +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f)
               [SNew(SVerticalBox) +
                SVerticalBox::Slot().AutoHeight().Padding(2.0f, 4.0f)
                    [SNew(SCheckBox)
                         .ToolTipText(LOCTEXT("EnableClassPickerTooltip",
                                              "Enable filtering by specific "
                                              "actor or component classes"))
                         .OnCheckStateChanged_Lambda(
                             [Module](ECheckBoxState NewState) {
                               if (Module) {
                                 Module->GetSettings()
                                     .bEnableClassPickerFilter =
                                     (NewState == ECheckBoxState::Checked);
                               }
                             })
                         .IsChecked_Lambda([Module]() {
                           if (Module) {
                             return Module->GetSettings()
                                            .bEnableClassPickerFilter
                                        ? ECheckBoxState::Checked
                                        : ECheckBoxState::Unchecked;
                           }
                           return ECheckBoxState::Unchecked;
                         })[SNew(STextBlock)
                                .Text(LOCTEXT("EnableClassPicker",
                                              "Enable Class Filter"))
                                .Margin(FMargin(6.0f, 0.0f, 8.0f, 0.0f))]] +
                SVerticalBox::Slot().AutoHeight().Padding(2.0f, 4.0f)
                    [SNew(SHorizontalBox) +
                     SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
                         [SNew(SVerticalBox) +
                          SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
                              [SNew(STextBlock)
                                   .Text(LOCTEXT("ActorClassLabel",
                                                 "Actor Class"))
                                   .Font(FCoreStyle::GetDefaultFontStyle(
                                       "Italic", 8))] +
                          SVerticalBox::Slot().AutoHeight()
                              [SNew(SHorizontalBox) +
                               SHorizontalBox::Slot().FillWidth(1.0f).Padding(
                                   2.0f)
                                   [SAssignNew(ActorClassComboBox,
                                               SComboBox<TSharedPtr<UClass *>>)
                                        .ToolTipText(LOCTEXT(
                                            "ActorClassTooltip",
                                            "Select an actor class to add"))
                                        .OptionsSource(&ActorClassOptions)
                                        .OnSelectionChanged_Lambda(
                                            [Module,
                                             this](TSharedPtr<UClass *>
                                                       SelectedClass,
                                                   ESelectInfo::Type) {
                                              if (Module &&
                                                  SelectedClass.IsValid() &&
                                                  *SelectedClass) {
                                                UClass *NewClass =
                                                    *SelectedClass;
                                                SelectedActorClasses.AddUnique(
                                                    NewClass);
                                                Module->GetSettings()
                                                    .SelectedActorClassPaths
                                                    .AddUnique(
                                                        NewClass
                                                            ->GetPathName());
                                                if (ActorClassComboBox
                                                        .IsValid()) {
                                                  ActorClassComboBox
                                                      ->SetSelectedItem(
                                                          nullptr);
                                                }
                                              }
                                            })
                                        .OnGenerateWidget_Lambda(
                                            [](TSharedPtr<UClass *> Item) {
                                              FString DisplayName =
                                                  TEXT("Select actor "
                                                       "class...");
                                              if (Item.IsValid() && *Item) {
                                                DisplayName =
                                                    (*Item)->GetName();
                                                if (DisplayName.EndsWith(
                                                        TEXT("_C"))) {
                                                  DisplayName.LeftChopInline(2);
                                                }
                                              }
                                              return SNew(STextBlock)
                                                  .Text(FText::FromString(
                                                      DisplayName));
                                            })[SNew(STextBlock)
                                                   .Text(LOCTEXT(
                                                       "ActorClassSelect",
                                                       "Select actor "
                                                       "class..."))]] +
                               SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
                                   [SNew(SButton)
                                        .Text(LOCTEXT("ActorClassClear",
                                                      "Clear"))
                                        .OnClicked_Lambda([Module, this]() {
                                          if (Module) {
                                            Module->GetSettings()
                                                .SelectedActorClassPaths
                                                .Empty();
                                          }
                                          SelectedActorClasses.Empty();
                                          return FReply::Handled();
                                        })
                                        .IsEnabled_Lambda([this]() {
                                          return SelectedActorClasses.Num() > 0;
                                        })]] +
                          SVerticalBox::Slot().AutoHeight().Padding(2.0f)
                              [SNew(STextBlock)
                                   .Text_Lambda([this]() {
                                     if (SelectedActorClasses.Num() == 0) {
                                       return LOCTEXT("NoActorClasses",
                                                      "No actor classes");
                                     }
                                     FString ClassList;
                                     for (int32 i = 0;
                                          i < SelectedActorClasses.Num();
                                          ++i) {
                                       if (i > 0) {
                                         ClassList += TEXT(", ");
                                       }
                                       FString DisplayName =
                                           SelectedActorClasses[i]->GetName();
                                       if (DisplayName.EndsWith(TEXT("_C"))) {
                                         DisplayName.LeftChopInline(2);
                                       }
                                       ClassList += DisplayName;
                                     }
                                     return FText::FromString(ClassList);
                                   })
                                   .AutoWrapText(true)
                                   .ColorAndOpacity(
                                       FLinearColor(0.7f, 0.7f, 0.7f))]] +
                     SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
                         [SNew(SVerticalBox) +
                          SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
                              [SNew(STextBlock)
                                   .Text(LOCTEXT("ComponentClassLabel",
                                                 "Component Class"))
                                   .Font(FCoreStyle::GetDefaultFontStyle(
                                       "Italic", 8))] +
                          SVerticalBox::Slot().AutoHeight()
                              [SNew(SHorizontalBox) +
                               SHorizontalBox::Slot().FillWidth(1.0f).Padding(
                                   2.0f)
                                   [SAssignNew(ComponentClassComboBox,
                                               SComboBox<TSharedPtr<UClass *>>)
                                        .ToolTipText(LOCTEXT(
                                            "ComponentClassTooltip",
                                            "Select a component class to add"))
                                        .OptionsSource(&ComponentClassOptions)
                                        .OnSelectionChanged_Lambda(
                                            [Module,
                                             this](TSharedPtr<UClass *>
                                                       SelectedClass,
                                                   ESelectInfo::Type) {
                                              if (Module &&
                                                  SelectedClass.IsValid() &&
                                                  *SelectedClass) {
                                                UClass *NewClass =
                                                    *SelectedClass;
                                                SelectedComponentClasses
                                                    .AddUnique(NewClass);
                                                Module->GetSettings()
                                                    .SelectedComponentClassPaths
                                                    .AddUnique(
                                                        NewClass
                                                            ->GetPathName());
                                                if (ComponentClassComboBox
                                                        .IsValid()) {
                                                  ComponentClassComboBox
                                                      ->SetSelectedItem(
                                                          nullptr);
                                                }
                                              }
                                            })
                                        .OnGenerateWidget_Lambda(
                                            [](TSharedPtr<UClass *> Item) {
                                              FString DisplayName =
                                                  TEXT("Select component "
                                                       "class...");
                                              if (Item.IsValid() && *Item) {
                                                DisplayName =
                                                    (*Item)->GetName();
                                                if (DisplayName.EndsWith(
                                                        TEXT("_C"))) {
                                                  DisplayName.LeftChopInline(2);
                                                }
                                              }
                                              return SNew(STextBlock)
                                                  .Text(FText::FromString(
                                                      DisplayName));
                                            })[SNew(STextBlock)
                                                   .Text(LOCTEXT(
                                                       "ComponentClassSelect",
                                                       "Select component "
                                                       "class..."))]] +
                               SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
                                   [SNew(SButton)
                                        .Text(LOCTEXT("ComponentClassClear",
                                                      "Clear"))
                                        .OnClicked_Lambda([Module, this]() {
                                          if (Module) {
                                            Module->GetSettings()
                                                .SelectedComponentClassPaths
                                                .Empty();
                                          }
                                          SelectedComponentClasses.Empty();
                                          return FReply::Handled();
                                        })
                                        .IsEnabled_Lambda([this]() {
                                          return SelectedComponentClasses.Num() >
                                                 0;
                                        })]] +
                          SVerticalBox::Slot().AutoHeight().Padding(2.0f)
                              [SNew(STextBlock)
                                   .Text_Lambda([this]() {
                                     if (SelectedComponentClasses.Num() == 0) {
                                       return LOCTEXT(
                                           "NoComponentClasses",
                                           "No component classes");
                                     }
                                     FString ClassList;
                                     for (int32 i = 0;
                                          i < SelectedComponentClasses.Num();
                                          ++i) {
                                       if (i > 0) {
                                         ClassList += TEXT(", ");
                                       }
                                       FString DisplayName =
                                           SelectedComponentClasses[i]
                                               ->GetName();
                                       if (DisplayName.EndsWith(TEXT("_C"))) {
                                         DisplayName.LeftChopInline(2);
                                       }
                                       ClassList += DisplayName;
                                     }
                                     return FText::FromString(ClassList);
                                   })
                                   .AutoWrapText(true)
                                   .ColorAndOpacity(
                                       FLinearColor(0.7f, 0.7f, 0.7f))]]] +
                SVerticalBox::Slot().AutoHeight().Padding(
                    2.0f, 8.0f, 2.0f,
                    2.0f)[SNew(SButton)
                              .ButtonStyle(&FAppStyle::Get()
                                                .GetWidgetStyle<FButtonStyle>(
                                                    "PrimaryButton"))
                              .HAlign(HAlign_Center)
                              .Text(LOCTEXT("RefreshClassLists",
                                            "Refresh Class Lists"))
                              .ToolTipText(LOCTEXT(
                                  "RefreshClassListsTooltip",
                                  "Rescan the current level and update "
                                  "actor/component class dropdown options"))
                              .OnClicked_Lambda([this]() {
                                RefreshClassPickerOptions();
                                RefreshTagOptions();
                                return FReply::Handled();
                              })]] +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f, 6.0f, 6.0f, 2.0f)
               [SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("DetailsView.CategoryTop"))
                    .BorderBackgroundColor(
                        FLinearColor(0.02f, 0.02f, 0.02f, 1.0f))
                    .Padding(FMargin(6.0f, 8.0f))
                        [SNew(SHorizontalBox) +
                         SHorizontalBox::Slot()
                             .AutoWidth()
                             .VAlign(VAlign_Center)
                             .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                 [SNew(SImage)
                                      .Image(FAppStyle::GetBrush(
                                          "Icons.FolderClosed"))
                                      .ColorAndOpacity(
                                          FSlateColor::UseForeground())] +
                         SHorizontalBox::Slot().VAlign(VAlign_Center)
                             [SNew(STextBlock)
                                  .Text(LOCTEXT("OrganisationLabel",
                                                "Organization"))
                                  .Font(FCoreStyle::GetDefaultFontStyle(
                                      "Bold", 10))]]] +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f, 0.0f, 6.0f, 4.0f)
               [SNew(STextBlock)
                    .Text(LOCTEXT("FolderExplanation",
                                  "Specify the target folder path (e.g., "
                                  "ENV/Building or ENV/Building/MyFolder). Use "
                                  "Browse to select from existing folders or "
                                  "type a new folder name to create one."))
                    .AutoWrapText(true)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground())] +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f)
               [SNew(SVerticalBox) +
                SVerticalBox::Slot().AutoHeight().Padding(2.0f, 0.0f, 2.0f,
                                                          4.0f)
                    [SNew(STextBlock)
                         .Text(LOCTEXT(
                             "FolderHint", "Tip: Clear path or type 'None' to "
                                           "remove actors from folders"))
                         .Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
                         .ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))] +
                SVerticalBox::Slot().AutoHeight()
                    [SNew(SHorizontalBox) +
                     SHorizontalBox::Slot()
                         .FillWidth(0.4f)
                         .Padding(2.0f)
                         .VAlign(VAlign_Center)
                             [SNew(STextBlock)
                                  .Text(LOCTEXT("MoveToFolderLabel",
                                                "Move to Folder Path:"))
                                  .Font(FCoreStyle::GetDefaultFontStyle("Bold",
                                                                        10))] +
                     SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
                         [SAssignNew(FolderPathTextBox, SEditableTextBox)
                              .HintText(LOCTEXT("FolderPath", "ENV/Folder"))
                              .Text_Lambda([Module]() {
                                if (Module) {
                                  return FText::FromName(
                                      Module->GetSettings().TargetFolderPath);
                                }
                                return FText::GetEmpty();
                              })
                              .OnTextCommitted_Lambda(
                                  [Module](const FText &Text,
                                           ETextCommit::Type) {
                                    if (Module) {
                                      Module->GetSettings().TargetFolderPath =
                                          FName(*Text.ToString());
                                    }
                                  })] +
                     SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
                         [SNew(SButton)
                              .ButtonStyle(&FAppStyle::Get()
                                                .GetWidgetStyle<FButtonStyle>(
                                                    "PrimaryButton"))
                              .ToolTipText(
                                  LOCTEXT("BrowseFolderTooltip",
                                          "Browse and select a folder from the "
                                          "World Outliner hierarchy"))
                              .Text(LOCTEXT("BrowseFolder", "Browse"))
                              .OnClicked_Lambda([FolderPathTextBox]() {
                                if (GEditor &&
                                    GEditor->GetEditorWorldContext().World()) {
                                  TSharedRef<SWindow> PickerWindow =
                                      SNew(SWindow)
                                          .Title(LOCTEXT("FolderPickerTitle",
                                                         "Select Folder"))
                                          .ClientSize(FVector2D(400, 500))
                                          .SupportsMaximize(false)
                                          .SupportsMinimize(false)[SNew(
                                              SFolderPickerDialog,
                                              GEditor->GetEditorWorldContext()
                                                  .World(),
                                              FolderPathTextBox)];

                                  FSlateApplication::Get().AddWindow(
                                      PickerWindow);
                                }
                                return FReply::Handled();
                              })]]] +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f, 4.0f, 6.0f, 6.0f)
               [SNew(SHorizontalBox) +
                SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
                    [SNew(SCheckBox)
                         .IsChecked_Lambda([Module]() {
                           return Module && Module->GetSettings().bMoveToFolder
                                      ? ECheckBoxState::Checked
                                      : ECheckBoxState::Unchecked;
                         })
                         .OnCheckStateChanged_Lambda([Module](
                                                         ECheckBoxState State) {
                           if (Module) {
                             Module->GetSettings().bMoveToFolder =
                                 (State == ECheckBoxState::Checked);
                           }
                         })[SNew(STextBlock)
                                .Text(LOCTEXT("MoveToFolder", "Move to Folder"))
                                .Margin(FMargin(6.0f, 0.0f, 8.0f, 0.0f))]] +
                SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
                    [SNew(SCheckBox)
                         .IsChecked_Lambda([Module]() {
                           return Module && Module->GetSettings()
                                                .bOrganizeByActorType
                                      ? ECheckBoxState::Checked
                                      : ECheckBoxState::Unchecked;
                         })
                         .OnCheckStateChanged_Lambda(
                             [Module](ECheckBoxState State) {
                               if (Module) {
                                 Module->GetSettings().bOrganizeByActorType =
                                     (State == ECheckBoxState::Checked);
                               }
                             })[SNew(STextBlock)
                                    .Text(LOCTEXT(
                                        "OrganizeByActorType",
                                        "Create sub-folders by Actor Type"))
                                    .Margin(FMargin(6.0f, 0.0f, 8.0f, 0.0f))]] +
                SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
                    [SNew(SCheckBox)
                         .ToolTipText(LOCTEXT(
                             "AutoNameFolderFromVolumeTooltip",
                             "Use the selected volume number to update any "
                             "Floor_XX or FloorXX segment in the folder path"))
                         .IsChecked_Lambda([Module]() {
                           return Module && Module->GetSettings()
                                                .bAutoNameFolderFromVolume
                                      ? ECheckBoxState::Checked
                                      : ECheckBoxState::Unchecked;
                         })
                         .OnCheckStateChanged_Lambda([this, Module](
                                                         ECheckBoxState State) {
                           if (Module) {
                             Module->GetSettings().bAutoNameFolderFromVolume =
                                 (State == ECheckBoxState::Checked);
                             if (State == ECheckBoxState::Checked) {
                               UpdateFolderPathFromSelectedVolume();
                             }
                           }
                         })[SNew(STextBlock)
                                .Text(LOCTEXT("AutoNameFolderFromVolume",
                                              "Use volume # as floor"))
                                .Margin(FMargin(6.0f, 0.0f, 8.0f, 0.0f))]]] +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f, 12.0f, 6.0f, 6.0f)
               [SNew(SButton)
                    .HAlign(HAlign_Center)
                    .ToolTipText(
                        LOCTEXT("RemoveEmptyFoldersTooltip",
                                "Remove all empty folders from the World "
                                "Outliner (folders with no actors)"))
                    .Text(LOCTEXT("RemoveEmptyFolders", "Remove Empty Folders"))
                    .ContentPadding(FMargin(16.0f, 6.0f))
                    .OnClicked_Lambda([Module]() {
                      if (Module) {
                        Module->RemoveEmptyFolders();
                      }
                      return FReply::Handled();
                    })] +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f)
               [SNew(SHorizontalBox) +
                SHorizontalBox::Slot().FillWidth(0.6f).Padding(2.0f).VAlign(
                    VAlign_Center)
                    [SNew(STextBlock)
                         .Text(LOCTEXT("DataLayerLabel", "Move to Data Layer:"))
                         .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))] +
                SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
                    [SNew(SComboButton)
                         .ButtonContent()
                             [SNew(STextBlock).Text_Lambda([Module]() {
                               if (Module &&
                                   Module->GetSettings().TargetDataLayer) {
                                 return FText::FromString(
                                     Module->GetSettings()
                                         .TargetDataLayer->GetName());
                               }
                               return LOCTEXT("DataLayerNone", "None");
                             })]
                         .OnGetMenuContent_Lambda([Module]() {
                           FMenuBuilder MenuBuilder(true, nullptr);

                           MenuBuilder.AddMenuEntry(
                               LOCTEXT("ClearDataLayer", "None"),
                               FText::GetEmpty(), FSlateIcon(),
                               FUIAction(
                                   FExecuteAction::CreateLambda([Module]() {
                                     if (Module) {
                                       Module->GetSettings().TargetDataLayer =
                                           nullptr;
                                     }
                                   })));

                           MenuBuilder.AddSeparator();

                           if (GEditor) {
                             if (UDataLayerEditorSubsystem
                                     *DataLayerEditorSubsystem =
                                         GEditor->GetEditorSubsystem<
                                             UDataLayerEditorSubsystem>()) {
                               TArray<UDataLayerInstance *> DataLayers =
                                   DataLayerEditorSubsystem->GetAllDataLayers();
                               for (UDataLayerInstance *DataLayerInstance :
                                    DataLayers) {
                                 if (DataLayerInstance &&
                                     DataLayerInstance->GetAsset()) {
                                   const UDataLayerAsset *Asset =
                                       DataLayerInstance->GetAsset();
                                   MenuBuilder.AddMenuEntry(
                                       FText::FromString(Asset->GetName()),
                                       FText::GetEmpty(), FSlateIcon(),
                                       FUIAction(FExecuteAction::CreateLambda(
                                           [Module, Asset]() {
                                             if (Module) {
                                               Module->GetSettings()
                                                   .TargetDataLayer =
                                                   const_cast<
                                                       UDataLayerAsset *>(
                                                       Asset);
                                             }
                                           })));
                                 }
                               }
                             }
                           }

                           return MenuBuilder.MakeWidget();
                         })] +
                SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
                    [SNew(SButton)
                         .Text(LOCTEXT("RemoveFromDataLayers",
                                       "Remove from Data Layers"))
                         .ToolTipText(LOCTEXT(
                             "RemoveFromDataLayersTooltip",
                             "Remove selected actors from all data layers"))
                         .OnClicked_Lambda([]() {
                           if (GEditor) {
                             if (UDataLayerEditorSubsystem
                                     *DataLayerEditorSubsystem =
                                         GEditor->GetEditorSubsystem<
                                             UDataLayerEditorSubsystem>()) {
                               TArray<AActor *> SelectedActors;
                               for (FSelectionIterator It(
                                        *GEditor->GetSelectedActors());
                                    It; ++It) {
                                 if (AActor *Actor = Cast<AActor>(*It)) {
                                   SelectedActors.Add(Actor);
                                 }
                               }

                               if (SelectedActors.Num() > 0) {
                                 FScopedTransaction Transaction(
                                     LOCTEXT("RemoveFromDataLayers_Transaction",
                                             "Remove Actors from Data Layers"));

                                 if (DataLayerEditorSubsystem
                                         ->RemoveActorsFromAllDataLayers(
                                             SelectedActors)) {
                                   FNotificationInfo Info(FText::Format(
                                       LOCTEXT("RemovedFromDataLayers",
                                               "Removed {0} actor(s) from all "
                                               "data layers"),
                                       FText::AsNumber(SelectedActors.Num())));
                                   Info.ExpireDuration = 3.0f;
                                   FSlateNotificationManager::Get()
                                       .AddNotification(Info);
                                 }
                               } else {
                                 FNotificationInfo Info(LOCTEXT(
                                     "NoActorsSelected", "No actors selected"));
                                 Info.ExpireDuration = 3.0f;
                                 FSlateNotificationManager::Get()
                                     .AddNotification(Info);
                               }
                             }
                           }
                           return FReply::Handled();
                         })]]
           // ===== ACTION BUTTONS =====
           // Three main action buttons: Preview Selection (left), Select Actors
           // and Move/Organize (right)
           +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f)
               [SNew(SHorizontalBox)
                // Preview button on far left
                + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
                      [SNew(SButton)
                           .ToolTipText(LOCTEXT(
                               "PreviewTooltip",
                               "Preview which actors will be selected based on "
                               "current filters (highlights in viewport)"))
                           .Text(LOCTEXT("Preview", "Preview Selection"))
                           .ContentPadding(FMargin(16.0f, 6.0f))
                           .OnClicked_Lambda([Module]() {
                             if (Module) {
                               Module->RunPreview();
                             }
                             return FReply::Handled();
                           })]
                // Spacer to push remaining buttons to the right
                +
                SHorizontalBox::Slot().FillWidth(1.0f)[SNullWidget::NullWidget]
                // Select Actors button on right
                + SHorizontalBox::Slot().AutoWidth().Padding(
                      2.0f)[SNew(SButton)
                                .ToolTipText(
                                    LOCTEXT("SelectActorsTooltip",
                                            "Select all actors in the volume "
                                            "that match the current filters"))
                                .Text(LOCTEXT("SelectActors", "Select Actors"))
                                .ContentPadding(FMargin(16.0f, 6.0f))
                                .OnClicked_Lambda([Module]() {
                                  if (Module) {
                                    const int32 SelectedCount =
                                        Module->RunSelection();
                                    // Update preview count to match what was
                                    // actually selected
                                    Module->RunPreview();
                                  }
                                  return FReply::Handled();
                                })]
                // Move/Organize button (primary action button) on far right
                +
                SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
                    [SNew(SButton)
                         .ButtonStyle(
                             &FAppStyle::Get().GetWidgetStyle<FButtonStyle>(
                                 "PrimaryButton"))
                         .ToolTipText(LOCTEXT("OrganizeTooltip",
                                              "Select actors and organize them "
                                              "into folders/data layers/levels "
                                              "based on current settings"))
                         .Text(LOCTEXT("OrganizeActors", "Move / Organize"))
                         .ContentPadding(FMargin(16.0f, 6.0f))
                         .OnClicked_Lambda([Module]() {
                           if (Module) {
                             // Validate that at least one organization action
                             // is enabled
                             const bool bHasFolderAction =
                                 Module->GetSettings().bMoveToFolder;
                             const bool bHasDataLayerAction =
                                 Module->GetSettings().TargetDataLayer !=
                                 nullptr;

                             if (!bHasFolderAction && !bHasDataLayerAction) {
                               FNotificationInfo Info(LOCTEXT(
                                   "OrganizeNoAction",
                                   "No organization actions enabled. Please "
                                   "check 'Move to Folder' or set a 'Move to "
                                   "Data Layer' to organize actors."));
                               Info.ExpireDuration = 4.0f;
                               FSlateNotificationManager::Get().AddNotification(
                                   Info);
                               return FReply::Handled();
                             }

                             // Check if using volume-based selection or current
                             // selection. Volume mode requires at least one
                             // include filter — without filters, fall back to
                             // the current outliner selection regardless of
                             // whether a volume target is set.
                             AActor *VolumeActor =
                                 Module->GetSettings().TargetVolumeActor.Get();
                             const bool bHasIncludeFilters =
                                 (Module->GetSettings().ActorTypeMask != 0) ||
                                 (Module->GetSettings().bEnableClassPickerFilter &&
                                  (Module->GetSettings().SelectedActorClassPaths.Num() > 0 ||
                                   Module->GetSettings().SelectedComponentClassPaths.Num() > 0)) ||
                                 (Module->GetSettings().IncludeTagList.Num() > 0);
                             const bool bUsingVolume = (VolumeActor != nullptr) && bHasIncludeFilters;
                             int32 ActorCount = 0;

                             if (bUsingVolume) {
                               // Volume mode: run preview to count actors
                               ActorCount = Module->RunPreview();

                               if (ActorCount == 0) {
                                 FNotificationInfo Info(
                                     LOCTEXT("OrganizeNoActors",
                                             "No actors found to organize with "
                                             "current filters."));
                                 Info.ExpireDuration = 3.0f;
                                 FSlateNotificationManager::Get()
                                     .AddNotification(Info);
                                 return FReply::Handled();
                               }
                             } else {
                               // Current selection mode: count selected actors
                               for (FSelectionIterator It(
                                        *GEditor->GetSelectedActors());
                                    It; ++It) {
                                 if (Cast<AActor>(*It)) {
                                   ActorCount++;
                                 }
                               }

                               if (ActorCount == 0) {
                                 FNotificationInfo Info(LOCTEXT(
                                     "OrganizeNoSelection",
                                     "No actors selected. Either select actors "
                                     "manually or set a volume target."));
                                 Info.ExpireDuration = 3.0f;
                                 FSlateNotificationManager::Get()
                                     .AddNotification(Info);
                                 return FReply::Handled();
                               }
                             }

                             // ALWAYS show confirmation dialog for safety
                             // before organizing actors
                             const FText MessageText = FText::Format(
                                 LOCTEXT("OrganizeConfirmation",
                                         "This will organize {0} actor(s). Do "
                                         "you want to continue?"),
                                 FText::AsNumber(ActorCount));

                             const EAppReturnType::Type Result =
                                 FMessageDialog::Open(EAppMsgType::YesNo,
                                                      MessageText);

                             // Only proceed if user explicitly clicks "Yes"
                             if (Result == EAppReturnType::Yes) {
                               Module->RunMoveAndOrganize();
                             }
                           }
                           return FReply::Handled();
                         })]]
           // ===== PREVIEW COUNTER LABEL =====
           // Displays how many actors will be selected with current filters
           // Updates dynamically and changes color to highlight issues
           + SVerticalBox::Slot().AutoHeight().Padding(6.0f, 2.0f, 6.0f, 6.0f)
                 [SNew(STextBlock)
                      .Text_Lambda([Module]() {
                        if (!Module) {
                          return LOCTEXT("PreviewLabel_NoModule",
                                         "Preview: N/A");
                        }

                        // Check if any filter is active
                        const bool bHasActorTypeFilter =
                            Module->GetSettings().ActorTypeMask != 0;
                        const bool bHasClassPickerFilter =
                            Module->GetSettings().bEnableClassPickerFilter &&
                            (Module->GetSettings()
                                  .SelectedActorClassPaths.Num() > 0 ||
                             Module->GetSettings()
                                  .SelectedComponentClassPaths.Num() > 0);
                        const bool bHasExcludeClassFilter =
                            Module->GetSettings().ExcludeActorClassList.Num() >
                                0 ||
                            Module->GetSettings()
                                    .ExcludeComponentClassList.Num() > 0;
                        const bool bHasTagFilter =
                            Module->GetSettings().IncludeTagList.Num() > 0 ||
                            Module->GetSettings().ExcludeTagList.Num() > 0;

                        if (!bHasActorTypeFilter && !bHasClassPickerFilter &&
                            !bHasExcludeClassFilter && !bHasTagFilter) {
                          return LOCTEXT("PreviewLabel_NoActorTypes",
                                         "Preview: Select at least one Actor "
                                         "Type, enable Class Picker Filter, "
                                         "or use Exclude/Tag filters");
                        }

                        const int32 Count = Module->GetPreviewCount();
                        if (Count == 0) {
                          return LOCTEXT(
                              "PreviewLabel_NoMatch",
                              "Preview: 0 Actors match current filters");
                        }

                        return FText::Format(
                            LOCTEXT("PreviewLabel",
                                    "Preview: Will select {0} Actors"),
                            FText::AsNumber(Count));
                      })
                      .ColorAndOpacity_Lambda([Module]() {
                        if (!Module) {
                          return FSlateColor::UseForeground();
                        }

                        // Check if any filter is active
                        const bool bHasActorTypeFilter =
                            Module->GetSettings().ActorTypeMask != 0;
                        const bool bHasClassPickerFilter =
                            Module->GetSettings().bEnableClassPickerFilter &&
                            (Module->GetSettings()
                                  .SelectedActorClassPaths.Num() > 0 ||
                             Module->GetSettings()
                                  .SelectedComponentClassPaths.Num() > 0);
                        const bool bHasExcludeClassFilter =
                            Module->GetSettings().ExcludeActorClassList.Num() >
                                0 ||
                            Module->GetSettings()
                                    .ExcludeComponentClassList.Num() > 0;
                        const bool bHasTagFilter =
                            Module->GetSettings().IncludeTagList.Num() > 0 ||
                            Module->GetSettings().ExcludeTagList.Num() > 0;

                        if (!bHasActorTypeFilter && !bHasClassPickerFilter &&
                            !bHasExcludeClassFilter && !bHasTagFilter) {
                          return FSlateColor(FLinearColor(1.0f, 0.85f, 0.0f));
                        }

                        const int32 Count = Module->GetPreviewCount();
                        if (Count == 0) {
                          return FSlateColor::UseForeground();
                        }

                        return FSlateColor(FLinearColor(1.0f, 0.85f, 0.0f));
                      })]

           // ===== MIRROR ACTORS SECTION =====
           // Mirror selected actors around world origin on specified axes
           +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f, 6.0f, 6.0f, 2.0f)
               [SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("DetailsView.CategoryTop"))
                    .BorderBackgroundColor(
                        FLinearColor(0.02f, 0.02f, 0.02f, 1.0f))
                    .Padding(FMargin(6.0f, 8.0f))
                        [SNew(SHorizontalBox) +
                         SHorizontalBox::Slot()
                             .AutoWidth()
                             .VAlign(VAlign_Center)
                             .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                 [SNew(SImage)
                                      .Image(FAppStyle::GetBrush(
                                          "Icons.Transform"))
                                      .ColorAndOpacity(
                                          FSlateColor::UseForeground())] +
                         SHorizontalBox::Slot().VAlign(VAlign_Center)
                             [SNew(STextBlock)
                                  .Text(LOCTEXT("MirrorActorsLabel",
                                                "Mirror Actors"))
                                  .Font(FCoreStyle::GetDefaultFontStyle(
                                      "Bold", 10))]]] +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f)
               [SNew(STextBlock)
                    .Text(
                        LOCTEXT("MirrorExplanation",
                                "Mirror selected actors around world origin "
                                "(0,0,0). Each actor is mirrored individually, "
                                "preserving their relative arrangement."))
                    .AutoWrapText(true)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground())] +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f)
               [SNew(SHorizontalBox) +
                SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
                    [SNew(SCheckBox)
                         .IsChecked_Lambda([this]() {
                           return bMirrorX ? ECheckBoxState::Checked
                                           : ECheckBoxState::Unchecked;
                         })
                         .OnCheckStateChanged_Lambda(
                             [this](ECheckBoxState NewState) {
                               bMirrorX = (NewState == ECheckBoxState::Checked);
                             })[SNew(STextBlock)
                                    .Text(LOCTEXT("MirrorX", "Mirror X"))
                                    .Margin(FMargin(6.0f, 0.0f, 8.0f, 0.0f))]] +
                SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
                    [SNew(SCheckBox)
                         .IsChecked_Lambda([this]() {
                           return bMirrorY ? ECheckBoxState::Checked
                                           : ECheckBoxState::Unchecked;
                         })
                         .OnCheckStateChanged_Lambda(
                             [this](ECheckBoxState NewState) {
                               bMirrorY = (NewState == ECheckBoxState::Checked);
                             })[SNew(STextBlock)
                                    .Text(LOCTEXT("MirrorY", "Mirror Y"))
                                    .Margin(FMargin(6.0f, 0.0f, 8.0f, 0.0f))]] +
                SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
                    [SNew(SCheckBox)
                         .IsChecked_Lambda([this]() {
                           return bMirrorZ ? ECheckBoxState::Checked
                                           : ECheckBoxState::Unchecked;
                         })
                         .OnCheckStateChanged_Lambda([this](ECheckBoxState
                                                                NewState) {
                           bMirrorZ = (NewState == ECheckBoxState::Checked);
                         })[SNew(STextBlock)
                                .Text(LOCTEXT("MirrorZ",
                                              "Mirror Z (Warning: May place "
                                              "actors below ground)"))
                                .Margin(FMargin(6.0f, 0.0f, 8.0f, 0.0f))]]] +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f, 12.0f, 6.0f, 2.0f)
               [SNew(SCheckBox)
                    .IsChecked_Lambda([this]() {
                      return bDuplicateBeforeMirror ? ECheckBoxState::Checked
                                                    : ECheckBoxState::Unchecked;
                    })
                    .OnCheckStateChanged_Lambda([this](
                                                    ECheckBoxState NewState) {
                      bDuplicateBeforeMirror =
                          (NewState == ECheckBoxState::Checked);
                    })[SNew(STextBlock)
                           .Text(LOCTEXT("DuplicateBeforeMirror",
                                         "Duplicate actors before mirroring"))
                           .Margin(FMargin(6.0f, 0.0f, 8.0f, 0.0f))]] +
           SVerticalBox::Slot().AutoHeight().Padding(
               6.0f, 2.0f, 6.0f,
               2.0f)[SNew(SCheckBox)
                         .IsChecked_Lambda([this]() {
                           return bOrganizeDuplicates
                                      ? ECheckBoxState::Checked
                                      : ECheckBoxState::Unchecked;
                         })
                         .OnCheckStateChanged_Lambda(
                             [this](ECheckBoxState NewState) {
                               bOrganizeDuplicates =
                                   (NewState == ECheckBoxState::Checked);
                             })
                         .IsEnabled_Lambda([this]() {
                           return bDuplicateBeforeMirror;
                         })[SNew(STextBlock)
                                .Text(LOCTEXT("OrganizeDuplicates",
                                              "Organize duplicates into 'Move "
                                              "to Folder' path"))
                                .Margin(FMargin(6.0f, 0.0f, 8.0f, 0.0f))]] +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f, 2.0f, 6.0f, 6.0f)
               [SNew(STextBlock)
                    .Text(LOCTEXT("DuplicateWarning",
                                  "⚠ Caution: Duplicate mode creates copies. "
                                  "Original actors remain at their positions."))
                    .AutoWrapText(true)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                    .ColorAndOpacity(FLinearColor(1.0f, 0.8f, 0.0f, 1.0f))] +
           SVerticalBox::Slot().AutoHeight().Padding(6.0f)
               [SNew(SButton)
                    .ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>(
                        "PrimaryButton"))
                    .HAlign(HAlign_Center)
                    .ToolTipText(LOCTEXT("MirrorActorsTooltip",
                                         "Mirror selected actors around world "
                                         "origin on the enabled axes"))
                    .Text(LOCTEXT("MirrorActorsButton", "Mirror Actors"))
                    .ContentPadding(FMargin(16.0f, 6.0f))
                    .OnClicked_Lambda([this]() {
                      ExecuteMirrorActors();
                      return FReply::Handled();
                    })]];

  FModeToolkit::Init(InitToolkitHost);

  // Initialize class picker dropdowns with current level classes
  RefreshClassPickerOptions();

  // Initialize tag picker dropdowns with current level tags
  RefreshTagOptions();

  // Initialize volume picker dropdown with current level volumes
  RefreshVolumeOptions();

  // Restore selected classes from saved paths (if any)
  if (Module) {
    for (const FString &ClassPath : Module->GetSettings().SelectedActorClassPaths) {
      UClass *LoadedClass = LoadObject<UClass>(nullptr, *ClassPath);
      if (LoadedClass) {
        SelectedActorClasses.AddUnique(LoadedClass);
      }
    }

    for (const FString &ClassPath : Module->GetSettings().SelectedComponentClassPaths) {
      UClass *LoadedClass = LoadObject<UClass>(nullptr, *ClassPath);
      if (LoadedClass) {
        SelectedComponentClasses.AddUnique(LoadedClass);
      }
    }

    // Exclude class paths are now stored as arrays and don't need individual UI
    // restoration
  }
}

/**
 * Returns the unique name identifier for this toolkit.
 * Used by the editor to manage and track editor mode toolkits.
 *
 * @return The FName identifier for this toolkit
 */
FName FOrganisationEdModeToolkit::GetToolkitFName() const {
  return FName("OrganisationEdModeToolkit");
}

/**
 * Returns the display name for this toolkit shown in the editor UI.
 *
 * @return Localized text name of the toolkit
 */
FText FOrganisationEdModeToolkit::GetBaseToolkitName() const {
  return LOCTEXT("ToolkitName", "Organisation");
}

/**
 * Returns the editor mode associated with this toolkit.
 *
 * @return Pointer to the active FOrganisationEdMode, or nullptr if not active
 */
FEdMode *FOrganisationEdModeToolkit::GetEditorMode() const {
  return GLevelEditorModeTools().GetActiveMode(
      FOrganisationEdMode::EM_OrganisationModeId);
}

/**
 * Returns the main widget content for inline display in the editor.
 * This is the root widget containing all UI elements created in Init().
 *
 * @return Shared pointer to the toolkit's main widget
 */
TSharedPtr<SWidget> FOrganisationEdModeToolkit::GetInlineContent() const {
  return ToolkitWidget;
}

/**
 * Updates the folder path by replacing Floor_XX or FloorXX patterns with the
 * selected volume's number.
 */
void FOrganisationEdModeToolkit::UpdateFolderPathFromSelectedVolume() {
  FREVOWorldOrganiserModule *Module = GetSelectActorsModule();
  if (!Module) {
    UE_LOG(LogTemp, Warning, TEXT("UpdateFolderPath: No Module"));
    return;
  }

  AActor *CurrentVolume = Module->GetSettings().TargetVolumeActor.Get();
  if (!CurrentVolume) {
    UE_LOG(LogTemp, Warning, TEXT("UpdateFolderPath: No CurrentVolume"));
    return;
  }

  if (!Module->GetSettings().bAutoNameFolderFromVolume) {
    UE_LOG(LogTemp, Warning, TEXT("UpdateFolderPath: bAutoNameFolderFromVolume is FALSE"));
    return;
  }

  FString VolumeName = CurrentVolume->GetActorLabel();
  FString CurrentPath = Module->GetSettings().TargetFolderPath.ToString();
  
  UE_LOG(LogTemp, Warning, TEXT("UpdateFolderPath: VolumeName='%s', CurrentPath='%s'"), *VolumeName, *CurrentPath);

  // Extract number from volume name
  FString NumberStr;
  for (int32 i = VolumeName.Len() - 1; i >= 0; --i) {
    if (FChar::IsDigit(VolumeName[i])) {
      NumberStr.InsertAt(0, VolumeName[i]);
    } else if (!NumberStr.IsEmpty()) {
      break;
    }
  }

  if (!NumberStr.IsEmpty()) {
    int32 FloorNumber = FCString::Atoi(*NumberStr);
    FString FloorName = FString::Printf(TEXT("Floor_%02d"), FloorNumber);
    
    UE_LOG(LogTemp, Warning, TEXT("UpdateFolderPath: Extracted Number='%s', FloorName='%s'"), *NumberStr, *FloorName);

    // Check if current path has Floor_XX or FloorXX pattern and replace it
    FRegexPattern FloorPattern(TEXT("Floor_?\\d+"));
    FRegexMatcher Matcher(FloorPattern, CurrentPath);

    if (Matcher.FindNext()) {
      // Replace existing Floor_XX or FloorXX
      int32 StartPos = Matcher.GetMatchBeginning();
      int32 EndPos = Matcher.GetMatchEnding();
      CurrentPath.RemoveAt(StartPos, EndPos - StartPos);
      CurrentPath.InsertAt(StartPos, FloorName);
      UE_LOG(LogTemp, Warning, TEXT("UpdateFolderPath: Replaced pattern"));
    } else if (!CurrentPath.IsEmpty()) {
      // Append to existing path
      CurrentPath = CurrentPath + TEXT("/") + FloorName;
      UE_LOG(LogTemp, Warning, TEXT("UpdateFolderPath: Appended to path"));
    } else {
      // Set as new path
      CurrentPath = FloorName;
      UE_LOG(LogTemp, Warning, TEXT("UpdateFolderPath: Created new path"));
    }

    Module->GetSettings().TargetFolderPath = FName(*CurrentPath);
    UE_LOG(LogTemp, Warning, TEXT("UpdateFolderPath: Final='%s'"), *CurrentPath);
  } else {
    UE_LOG(LogTemp, Warning, TEXT("UpdateFolderPath: No number found in volume name"));
  }
}

/**
 * Refreshes the volume picker dropdown by scanning the current level for
 * REVSelectionVolume actors. Called automatically during initialization and
 * when the user clicks the Refresh button.
 */
void FOrganisationEdModeToolkit::RefreshVolumeOptions() {
  if (!GEditor) {
    return;
  }

  UWorld *World = GEditor->GetEditorWorldContext().World();
  if (!World) {
    return;
  }

  // Clear existing options
  VolumeOptions.Empty();
  SelectedVolume = nullptr;

  // Collect all REVSelectionVolume actors in the level
  TArray<AActor *> SortedVolumes;
  for (TActorIterator<AREVSelectionVolume> It(World); It; ++It) {
    AREVSelectionVolume *Volume = *It;
    if (Volume && !Volume->IsHiddenEd()) {
      SortedVolumes.Add(Volume);
    }
  }

  // Sort with natural/numeric ordering (e.g., Volume2 before Volume12)
  SortedVolumes.Sort([](const AActor &A, const AActor &B) {
    FString LabelA = A.GetActorLabel();
    FString LabelB = B.GetActorLabel();

    // Extract numeric suffix if present
    int32 NumA = 0;
    int32 NumB = 0;
    FString PrefixA, PrefixB;

    // Find where numbers start from the end
    int32 NumStartA = LabelA.Len();
    while (NumStartA > 0 && FChar::IsDigit(LabelA[NumStartA - 1])) {
      NumStartA--;
    }

    int32 NumStartB = LabelB.Len();
    while (NumStartB > 0 && FChar::IsDigit(LabelB[NumStartB - 1])) {
      NumStartB--;
    }

    if (NumStartA < LabelA.Len()) {
      PrefixA = LabelA.Left(NumStartA);
      NumA = FCString::Atoi(*LabelA.RightChop(NumStartA));
    } else {
      PrefixA = LabelA;
    }

    if (NumStartB < LabelB.Len()) {
      PrefixB = LabelB.Left(NumStartB);
      NumB = FCString::Atoi(*LabelB.RightChop(NumStartB));
    } else {
      PrefixB = LabelB;
    }

    // Compare prefix first, then number
    if (PrefixA != PrefixB) {
      return PrefixA < PrefixB;
    }
    return NumA < NumB;
  });

  // Build combo box options
  for (AActor *Volume : SortedVolumes) {
    VolumeOptions.Add(MakeShared<AActor *>(Volume));
  }

  // Refresh the combo box
  if (VolumeComboBox.IsValid()) {
    VolumeComboBox->RefreshOptions();

    // Try to restore current selection
    FREVOWorldOrganiserModule *Module = GetSelectActorsModule();
    if (Module) {
      AActor *CurrentVolume = Module->GetSettings().TargetVolumeActor.Get();
      if (CurrentVolume) {
        // Find matching option
        for (const TSharedPtr<AActor *> &Option : VolumeOptions) {
          if (Option.IsValid() && *Option == CurrentVolume) {
            VolumeComboBox->SetSelectedItem(Option);
            SelectedVolume = CurrentVolume;

            // Auto-update folder path if auto-floor naming is enabled
            if (Module->GetSettings().bAutoNameFolderFromVolume) {
              FString VolumeName = CurrentVolume->GetActorLabel();
              FString CurrentPath =
                  Module->GetSettings().TargetFolderPath.ToString();

              // Extract number from volume name
              FString NumberStr;
              for (int32 i = VolumeName.Len() - 1; i >= 0; --i) {
                if (FChar::IsDigit(VolumeName[i])) {
                  NumberStr.InsertAt(0, VolumeName[i]);
                } else if (!NumberStr.IsEmpty()) {
                  break;
                }
              }

              if (!NumberStr.IsEmpty()) {
                int32 FloorNumber = FCString::Atoi(*NumberStr);
                FString FloorName =
                    FString::Printf(TEXT("Floor_%02d"), FloorNumber);

                // Check if current path has a Floor_XX pattern and replace it
                FRegexPattern FloorPattern(TEXT("Floor_\\d{2}"));
                FRegexMatcher Matcher(FloorPattern, CurrentPath);

                if (Matcher.FindNext()) {
                  // Replace existing Floor_XX
                  int32 StartPos = Matcher.GetMatchBeginning();
                  int32 EndPos = Matcher.GetMatchEnding();
                  CurrentPath.RemoveAt(StartPos, EndPos - StartPos);
                  CurrentPath.InsertAt(StartPos, FloorName);
                } else if (!CurrentPath.IsEmpty()) {
                  // Append to existing path
                  CurrentPath = CurrentPath + TEXT("/") + FloorName;
                } else {
                  // Set as new path
                  CurrentPath = FloorName;
                }

                Module->GetSettings().TargetFolderPath = FName(*CurrentPath);
              }
            }
            break;
          }
        }
      }
    }
  }

  // Show notification
  if (SortedVolumes.Num() > 0) {
    FNotificationInfo Info(FText::Format(
        LOCTEXT("VolumeListRefreshed",
                "Volume list refreshed: {0} REVSelectionVolume(s) found"),
        FText::AsNumber(SortedVolumes.Num())));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
  } else {
    FNotificationInfo Info(LOCTEXT(
        "NoVolumesFound", "No REVSelectionVolume actors found in level"));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
  }
}

/**
 * Refreshes the class picker dropdown options by scanning the current level.
 * Collects all unique actor classes and component classes present in the editor
 * world. Populates ActorClassOptions and ComponentClassOptions arrays.
 */
void FOrganisationEdModeToolkit::RefreshClassPickerOptions() {
  if (!GEditor) {
    return;
  }

  UWorld *World = GEditor->GetEditorWorldContext().World();
  if (!World) {
    return;
  }

  // Clear existing options
  ActorClassOptions.Empty();
  ComponentClassOptions.Empty();

  // Reset selections
  SelectedActorClasses.Empty();
  SelectedComponentClasses.Empty();

  // Collect unique classes
  TSet<UClass *> UniqueActorClasses;
  TSet<UClass *> UniqueComponentClasses;

  // Iterate through all actors in the world
  for (TActorIterator<AActor> It(World); It; ++It) {
    AActor *Actor = *It;
    if (!Actor) {
      continue;
    }

    // Collect actor class and all parent classes up to AActor
    UClass *ActorClass = Actor->GetClass();
    while (ActorClass && ActorClass != AActor::StaticClass()) {
      UniqueActorClasses.Add(ActorClass);
      ActorClass = ActorClass->GetSuperClass();
    }

    // Collect component classes
    TInlineComponentArray<UActorComponent *> Components;
    Actor->GetComponents(Components);

    for (UActorComponent *Component : Components) {
      if (Component) {
        UClass *ComponentClass = Component->GetClass();
        if (ComponentClass) {
          UniqueComponentClasses.Add(ComponentClass);
        }
      }
    }
  }

  // Convert sets to sorted arrays
  TArray<UClass *> SortedActorClasses = UniqueActorClasses.Array();
  TArray<UClass *> SortedComponentClasses = UniqueComponentClasses.Array();

  // Sort alphabetically by class name
  SortedActorClasses.Sort([](const UClass &A, const UClass &B) {
    return A.GetName() < B.GetName();
  });

  SortedComponentClasses.Sort([](const UClass &A, const UClass &B) {
    return A.GetName() < B.GetName();
  });

  // Build combo box options - first entry is None (nullptr)
  ActorClassOptions.Add(MakeShared<UClass *>(nullptr));
  for (UClass *ActorClass : SortedActorClasses) {
    ActorClassOptions.Add(MakeShared<UClass *>(ActorClass));
  }

  ComponentClassOptions.Add(MakeShared<UClass *>(nullptr));
  for (UClass *ComponentClass : SortedComponentClasses) {
    ComponentClassOptions.Add(MakeShared<UClass *>(ComponentClass));
  }

  // Refresh the combo boxes
  if (ActorClassComboBox.IsValid()) {
    ActorClassComboBox->RefreshOptions();
    ActorClassComboBox->SetSelectedItem(ActorClassOptions[0]); // Select "None"
  }

  if (ComponentClassComboBox.IsValid()) {
    ComponentClassComboBox->RefreshOptions();
    ComponentClassComboBox->SetSelectedItem(
        ComponentClassOptions[0]); // Select "None"
  }

  if (ExcludeActorClassComboBox.IsValid()) {
    ExcludeActorClassComboBox->RefreshOptions();
    ExcludeActorClassComboBox->SetSelectedItem(nullptr);
  }

  if (ExcludeComponentClassComboBox.IsValid()) {
    ExcludeComponentClassComboBox->RefreshOptions();
    ExcludeComponentClassComboBox->SetSelectedItem(nullptr);
  }

  // Clear saved paths since we reset selections
  FREVOWorldOrganiserModule *Module = GetSelectActorsModule();
  if (Module) {
    Module->GetSettings().SelectedActorClassPaths.Empty();
    Module->GetSettings().SelectedComponentClassPaths.Empty();
  }

  // Show notification
  FNotificationInfo Info(FText::Format(
      LOCTEXT(
          "ClassListRefreshed",
          "Class lists refreshed: {0} actor classes, {1} component classes"),
      FText::AsNumber(SortedActorClasses.Num()),
      FText::AsNumber(SortedComponentClasses.Num())));
  Info.ExpireDuration = 3.0f;
  FSlateNotificationManager::Get().AddNotification(Info);
}

/**
 * Refreshes the tag picker dropdown options by scanning the current level.
 * Collects all unique tags present on actors in the editor world.
 * Populates TagOptions array.
 */
void FOrganisationEdModeToolkit::RefreshTagOptions() {
  if (!GEditor) {
    return;
  }

  UWorld *World = GEditor->GetEditorWorldContext().World();
  if (!World) {
    return;
  }

  // Clear existing options
  TagOptions.Empty();

  // Collect unique tags
  TSet<FName> UniqueTags;

  // Iterate through all actors in the world
  for (TActorIterator<AActor> It(World); It; ++It) {
    AActor *Actor = *It;
    if (!Actor) {
      continue;
    }

    // Collect all tags from this actor
    for (const FName &Tag : Actor->Tags) {
      if (!Tag.IsNone()) {
        // Exclude tags starting with "GnIdentifier"
        const FString TagString = Tag.ToString();
        if (!TagString.StartsWith(TEXT("GnIdentifier"))) {
          UniqueTags.Add(Tag);
        }
      }
    }
  }

  // Convert set to sorted array
  TArray<FName> SortedTags = UniqueTags.Array();

  // Sort alphabetically by tag name
  SortedTags.Sort([](const FName &A, const FName &B) {
    return A.ToString() < B.ToString();
  });

  // Build combo box options
  for (const FName &Tag : SortedTags) {
    TagOptions.Add(MakeShared<FName>(Tag));
  }

  // Refresh the combo boxes
  if (IncludeTagComboBox.IsValid()) {
    IncludeTagComboBox->RefreshOptions();
  }

  if (ExcludeTagComboBox.IsValid()) {
    ExcludeTagComboBox->RefreshOptions();
  }

  if (ReplaceTagComboBox.IsValid()) {
    ReplaceTagComboBox->RefreshOptions();
  }

  // Show notification
  FNotificationInfo Info(
      FText::Format(LOCTEXT("TagListRefreshed",
                            "Tag list refreshed: {0} unique tag(s) found"),
                    FText::AsNumber(SortedTags.Num())));
  Info.ExpireDuration = 3.0f;
  FSlateNotificationManager::Get().AddNotification(Info);
}

/**
 * Finds all actors with the currently selected tag and selects them in the
 * level. Moves the viewport camera to the first found actor.
 */
void FOrganisationEdModeToolkit::FindAndNavigateToActorsWithTag() {
  if (!GEditor) {
    return;
  }

  UWorld *World = GEditor->GetEditorWorldContext().World();
  if (!World) {
    return;
  }

  if (SelectedReplaceTag.IsNone()) {
    FNotificationInfo Info(LOCTEXT("FindActors_NoTag", "No tag selected."));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
    return;
  }

  // Clear current selection
  GEditor->SelectNone(false, false);

  TArray<AActor *> MatchingActors;
  for (TActorIterator<AActor> It(World); It; ++It) {
    AActor *Actor = *It;
    if (!Actor) {
      continue;
    }

    if (Actor->Tags.Contains(SelectedReplaceTag)) {
      if (!GEditor->CanSelectActor(Actor, true, false)) {
        continue;
      }
      GEditor->SelectActor(Actor, true, false);
      MatchingActors.Add(Actor);
    }
  }

  GEditor->NoteSelectionChange();

  if (MatchingActors.Num() > 0) {
    GEditor->MoveViewportCamerasToActor(*MatchingActors[0], false);

    FNotificationInfo Info(FText::Format(
        LOCTEXT("FindActors_Found", "Found {0} actor(s) with tag '{1}'"),
        FText::AsNumber(MatchingActors.Num()),
        FText::FromName(SelectedReplaceTag)));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
  } else {
    FNotificationInfo Info(FText::Format(
        LOCTEXT("FindActors_NotFound", "No actors found with tag '{0}'"),
        FText::FromName(SelectedReplaceTag)));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
  }
}

/**
 * Executes the mirror actors operation on currently selected actors.
 * Mirrors actor world positions around world origin on enabled axes.
 * Only modifies location - preserves rotation and scale.
 */
void FOrganisationEdModeToolkit::ExecuteMirrorActors() {
  if (!GEditor) {
    return;
  }

  // Validate that at least one axis is selected
  if (!bMirrorX && !bMirrorY && !bMirrorZ) {
    FNotificationInfo Info(LOCTEXT(
        "MirrorNoAxis", "Select at least one mirror axis (X, Y, or Z)."));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
    return;
  }

  // Validate "Organize Duplicates" settings
  if (bDuplicateBeforeMirror && bOrganizeDuplicates) {
    FREVOWorldOrganiserModule *Module =
        FModuleManager::GetModulePtr<FREVOWorldOrganiserModule>(
            "REVOWorldOrganiser");
    if (Module) {
      const FVolumeSelectionSettings &Settings = Module->GetSettings();
      if (!Settings.bMoveToFolder) {
        const FText ErrorMessage =
            LOCTEXT("OrganizeDuplicatesError",
                    "Error: 'Organize duplicates into Move to Folder path' is "
                    "enabled, but 'Move to Folder' is disabled.\n\n"
                    "Please enable 'Move to Folder' and set a target folder "
                    "path, or disable 'Organize duplicates'.");

        FMessageDialog::Open(EAppMsgType::Ok, ErrorMessage);
        return;
      }
    }
  }

  // Get selected actors
  TArray<AActor *> SelectedActors;
  for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It) {
    if (AActor *Actor = Cast<AActor>(*It)) {
      SelectedActors.Add(Actor);
    }
  }

  // Validate that actors are selected
  if (SelectedActors.Num() == 0) {
    FNotificationInfo Info(LOCTEXT(
        "MirrorNoActors", "No actors selected. Select actors to mirror."));
    Info.ExpireDuration = 3.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
    return;
  }

  // Safety confirmation dialog
  const FText ConfirmationMessage =
      bDuplicateBeforeMirror
          ? FText::Format(
                LOCTEXT(
                    "DuplicateAndMirrorConfirm",
                    "Duplicate and Mirror {0} actor(s) on axes: {1}{2}{3}?\n\n"
                    "This will create {0} new duplicated actor(s) and mirror "
                    "them around world origin (0,0,0)."),
                FText::AsNumber(SelectedActors.Num()),
                bMirrorX ? FText::FromString(TEXT("X ")) : FText::GetEmpty(),
                bMirrorY ? FText::FromString(TEXT("Y ")) : FText::GetEmpty(),
                bMirrorZ ? FText::FromString(TEXT("Z")) : FText::GetEmpty())
          : FText::Format(
                LOCTEXT("MirrorConfirm",
                        "Mirror {0} actor(s) on axes: {1}{2}{3}?\n\n"
                        "Actors will be mirrored around world origin (0,0,0). "
                        "Relative positions are preserved.\n"
                        "This operation can be undone with Ctrl+Z."),
                FText::AsNumber(SelectedActors.Num()),
                bMirrorX ? FText::FromString(TEXT("X ")) : FText::GetEmpty(),
                bMirrorY ? FText::FromString(TEXT("Y ")) : FText::GetEmpty(),
                bMirrorZ ? FText::FromString(TEXT("Z")) : FText::GetEmpty());

  EAppReturnType::Type ConfirmResult =
      FMessageDialog::Open(EAppMsgType::YesNo, ConfirmationMessage);

  if (ConfirmResult != EAppReturnType::Yes) {
    return;
  }

  // Optional warning for Z-axis mirroring
  if (bMirrorZ) {
    const FText WarningMessage = FText::Format(
        LOCTEXT("MirrorZWarning", "Mirroring {0} actor(s) on Z axis. This may "
                                  "place actors below ground. Continue?"),
        FText::AsNumber(SelectedActors.Num()));

    const EAppReturnType::Type Result =
        FMessageDialog::Open(EAppMsgType::YesNo, WarningMessage);

    if (Result != EAppReturnType::Yes) {
      return;
    }
  }

  // Create undo transaction
  const FText TransactionText =
      bDuplicateBeforeMirror
          ? LOCTEXT("DuplicateAndMirrorActors", "Duplicate and Mirror Actors")
          : LOCTEXT("MirrorActors", "Mirror Actors");
  FScopedTransaction Transaction(TransactionText);

  // Duplicate actors if requested
  TArray<AActor *> ActorsToMirror;
  if (bDuplicateBeforeMirror) {
    UWorld *World = GEditor->GetEditorWorldContext().World();
    if (!World) {
      return;
    }

    // Use editor's proper duplication which handles all components correctly
    // First, ensure only our target actors are selected
    GEditor->SelectNone(false, false);
    for (AActor *Actor : SelectedActors) {
      if (IsValid(Actor)) {
        GEditor->SelectActor(Actor, true, false);
      }
    }
    GEditor->NoteSelectionChange();

    // Get count before duplication
    const int32 OriginalCount = SelectedActors.Num();

    // Use editor's built-in duplication (Ctrl+W equivalent)
    GEditor->edactDuplicateSelected(World->GetCurrentLevel(), false);

    // After duplication, the duplicated actors are now selected
    // Collect the newly selected (duplicated) actors
    for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It) {
      if (AActor *DuplicatedActor = Cast<AActor>(*It)) {
        // Organize into folder if requested
        if (bOrganizeDuplicates) {
          FREVOWorldOrganiserModule *Module =
              FModuleManager::GetModulePtr<FREVOWorldOrganiserModule>(
                  "REVOWorldOrganiser");
          if (Module) {
            const FVolumeSelectionSettings &Settings = Module->GetSettings();
            if (Settings.bMoveToFolder && !Settings.TargetFolderPath.IsNone()) {
              DuplicatedActor->SetFolderPath(Settings.TargetFolderPath);
            }
          }
        }

        ActorsToMirror.Add(DuplicatedActor);
      }
    }
  } else {
    ActorsToMirror = SelectedActors;
  }

  // Mirror each actor (original or duplicated) around world origin (0,0,0)
  int32 MirroredCount = 0;
  for (AActor *Actor : ActorsToMirror) {
    if (!IsValid(Actor)) {
      continue;
    }

    // Mark actor for undo
    Actor->Modify();

    // Get current world location, rotation, and scale
    FVector PivotLocation = Actor->GetActorLocation();
    FRotator Rotation = Actor->GetActorRotation();
    FVector Scale = Actor->GetActorScale3D();

    // Mirror position around world origin (0,0,0)
    FVector MirroredLocation = PivotLocation;

    // Check if this is a standalone light actor (not blueprint with lights)
    bool bIsStandaloneLightActor = Actor->IsA<ALight>();

    FRotator MirroredRotation = Rotation;
    FVector MirroredScale = Scale;

    // Collect light components if this is a blueprint with lights
    TInlineComponentArray<ULightComponent *> LightComponents;
    if (!bIsStandaloneLightActor) {
      Actor->GetComponents(LightComponents);
    }

    if (bIsStandaloneLightActor) {
      // For standalone light actors, use vector-based rotation to avoid
      // direction flip
      FTransform OriginalTransform = Actor->GetActorTransform();
      FVector ForwardVector =
          OriginalTransform.GetRotation().GetForwardVector();
      FVector RightVector = OriginalTransform.GetRotation().GetRightVector();
      FVector UpVector = OriginalTransform.GetRotation().GetUpVector();

      if (bMirrorX) {
        MirroredLocation.X = -MirroredLocation.X;
        ForwardVector.X = -ForwardVector.X;
        RightVector.X = -RightVector.X;
        UpVector.X = -UpVector.X;
      }
      if (bMirrorY) {
        MirroredLocation.Y = -MirroredLocation.Y;
        ForwardVector.Y = -ForwardVector.Y;
        RightVector.Y = -RightVector.Y;
        UpVector.Y = -UpVector.Y;
      }
      if (bMirrorZ) {
        MirroredLocation.Z = -MirroredLocation.Z;
        ForwardVector.Z = -ForwardVector.Z;
        RightVector.Z = -RightVector.Z;
        UpVector.Z = -UpVector.Z;
      }

      FMatrix RotationMatrix =
          FRotationMatrix::MakeFromXZ(ForwardVector, UpVector);
      MirroredRotation = RotationMatrix.Rotator();
    } else {
      // For regular meshes, use scale mirroring with proper rotation
      float Pitch = Rotation.Pitch;
      float Yaw = Rotation.Yaw;
      float Roll = Rotation.Roll;

      if (bMirrorX) {
        MirroredLocation.X = -MirroredLocation.X;
        MirroredScale.Y = -MirroredScale.Y; // Negate perpendicular axis
        Yaw = 180.0f - Yaw;
      }
      if (bMirrorY) {
        MirroredLocation.Y = -MirroredLocation.Y;
        MirroredScale.X = -MirroredScale.X; // Negate perpendicular axis
        Yaw = 180.0f - Yaw;
      }
      if (bMirrorZ) {
        MirroredLocation.Z = -MirroredLocation.Z;
        MirroredScale.X = -MirroredScale.X; // Negate perpendicular axis
        Pitch = -Pitch;
      }

      MirroredRotation = FRotator(Pitch, Yaw, Roll);
    }

    // Apply the mirrored transform
    Actor->SetActorLocation(MirroredLocation, false, nullptr,
                            ETeleportType::TeleportPhysics);
    Actor->SetActorRotation(MirroredRotation, ETeleportType::TeleportPhysics);
    Actor->SetActorScale3D(MirroredScale);

    // If this is a blueprint with lights, adjust light component rotations to
    // preserve their direction
    if (LightComponents.Num() > 0) {
      for (ULightComponent *LightComp : LightComponents) {
        if (LightComp) {
          LightComp->Modify();
          FRotator LightRotation = LightComp->GetRelativeRotation();

          // Always flip light rotation when mirroring, regardless of original
          // scale UE rotation: X=Roll, Y=Pitch, Z=Yaw
          if (bMirrorX) {
            // Y scale was negated, flip light's Roll to compensate
            LightRotation.Roll = -LightRotation.Roll;
          }
          if (bMirrorY) {
            // X scale was negated, flip light's Pitch to compensate
            LightRotation.Pitch = -LightRotation.Pitch;
          }
          if (bMirrorZ) {
            // X scale was negated, flip light's Roll to compensate
            LightRotation.Roll = -LightRotation.Roll;
          }

          LightComp->SetRelativeRotation(LightRotation);
        }
      }
    }

    ++MirroredCount;
  }

  // Update selection to only show duplicated actors if duplication occurred
  if (bDuplicateBeforeMirror && ActorsToMirror.Num() > 0) {
    // Use false for bNoteSelectionChange to batch all selection changes
    // together
    GEditor->SelectNone(false, false);
    for (AActor *DuplicatedActor : ActorsToMirror) {
      if (IsValid(DuplicatedActor)) {
        GEditor->SelectActor(DuplicatedActor, true, false);
      }
    }
    // Notify selection change only once after all actors are selected
    GEditor->NoteSelectionChange();
  }

  // Show success notification
  const FText SuccessMessage =
      bDuplicateBeforeMirror
          ? FText::Format(
                LOCTEXT(
                    "DuplicateAndMirrorSuccess",
                    "Duplicated and mirrored {0} actor(s) on axes: {1}{2}{3}"),
                FText::AsNumber(MirroredCount),
                bMirrorX ? FText::FromString(TEXT("X ")) : FText::GetEmpty(),
                bMirrorY ? FText::FromString(TEXT("Y ")) : FText::GetEmpty(),
                bMirrorZ ? FText::FromString(TEXT("Z")) : FText::GetEmpty())
          : FText::Format(
                LOCTEXT("MirrorSuccess",
                        "Mirrored {0} actor(s) on axes: {1}{2}{3}"),
                FText::AsNumber(MirroredCount),
                bMirrorX ? FText::FromString(TEXT("X ")) : FText::GetEmpty(),
                bMirrorY ? FText::FromString(TEXT("Y ")) : FText::GetEmpty(),
                bMirrorZ ? FText::FromString(TEXT("Z")) : FText::GetEmpty());

  FNotificationInfo Info(SuccessMessage);
  Info.ExpireDuration = 3.0f;
  FSlateNotificationManager::Get().AddNotification(Info);
}

#undef LOCTEXT_NAMESPACE
