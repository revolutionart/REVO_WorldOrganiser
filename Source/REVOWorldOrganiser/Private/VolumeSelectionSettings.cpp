// Copyright 2026, REVOLUTIONART All rights reserved

#include "VolumeSelectionSettings.h"

#include "Engine/World.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace {
TArray<TSharedPtr<FJsonValue>> StringsToJson(const TArray<FString> &Values) {
  TArray<TSharedPtr<FJsonValue>> Result;
  for (const FString &Value : Values) {
    Result.Add(MakeShared<FJsonValueString>(Value));
  }
  return Result;
}

TArray<TSharedPtr<FJsonValue>> NamesToJson(const TArray<FName> &Values) {
  TArray<TSharedPtr<FJsonValue>> Result;
  for (const FName &Value : Values) {
    Result.Add(MakeShared<FJsonValueString>(Value.ToString()));
  }
  return Result;
}

void JsonToStrings(const TArray<TSharedPtr<FJsonValue>> &Array,
                   TArray<FString> &OutValues) {
  OutValues.Reset();
  for (const TSharedPtr<FJsonValue> &Value : Array) {
    OutValues.Add(Value->AsString());
  }
}

void JsonToNames(const TArray<TSharedPtr<FJsonValue>> &Array,
                 TArray<FName> &OutValues) {
  OutValues.Reset();
  for (const TSharedPtr<FJsonValue> &Value : Array) {
    OutValues.Add(FName(*Value->AsString()));
  }
}
} // namespace

FString VolumeSelectionSettings::SerializeToJson(
    const FVolumeSelectionSettings &Settings) {
  TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();

  Root->SetStringField(TEXT("SelectionMode"),
                       StaticEnum<EVolumeSelectionMode>()->GetValueAsString(
                           Settings.SelectionMode));
  Root->SetBoolField(TEXT("UseComponentBounds"), Settings.bUseComponentBounds);

  Root->SetNumberField(TEXT("ContainsFullyThreshold"),
                       Settings.ContainsFullyThreshold);

  Root->SetNumberField(TEXT("ActorTypeMask"), Settings.ActorTypeMask);

  Root->SetArrayField(TEXT("ExcludeActorClassList"),
                      StringsToJson(Settings.ExcludeActorClassList));
  Root->SetArrayField(TEXT("ExcludeComponentClassList"),
                      StringsToJson(Settings.ExcludeComponentClassList));
  Root->SetArrayField(TEXT("IncludeTagList"),
                      NamesToJson(Settings.IncludeTagList));
  Root->SetArrayField(TEXT("ExcludeTagList"),
                      NamesToJson(Settings.ExcludeTagList));

  // Class Picker Filter
  Root->SetBoolField(TEXT("EnableClassPickerFilter"),
                     Settings.bEnableClassPickerFilter);
  Root->SetArrayField(TEXT("SelectedActorClassPaths"),
                      StringsToJson(Settings.SelectedActorClassPaths));
  Root->SetArrayField(TEXT("SelectedComponentClassPaths"),
                      StringsToJson(Settings.SelectedComponentClassPaths));

  Root->SetBoolField(TEXT("MoveToFolder"), Settings.bMoveToFolder);
  Root->SetStringField(TEXT("TargetFolderPath"),
                       Settings.TargetFolderPath.ToString());
  Root->SetBoolField(TEXT("AutoNameFolderFromVolume"),
                     Settings.bAutoNameFolderFromVolume);
  Root->SetBoolField(TEXT("PreserveFolderStructure"),
                     Settings.bPreserveFolderStructure);

  Root->SetStringField(TEXT("TargetDataLayer"),
                       Settings.TargetDataLayer
                           ? Settings.TargetDataLayer->GetPathName()
                           : FString());

  FString Output;
  const TSharedRef<TJsonWriter<>> Writer =
      TJsonWriterFactory<>::Create(&Output);
  FJsonSerializer::Serialize(Root, Writer);
  return Output;
}

bool VolumeSelectionSettings::DeserializeFromJson(
    const FString &JsonString, FVolumeSelectionSettings &OutSettings) {
  TSharedPtr<FJsonObject> Root;
  const TSharedRef<TJsonReader<>> Reader =
      TJsonReaderFactory<>::Create(JsonString);
  if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) {
    return false;
  }

  const FString SelectionModeString =
      Root->GetStringField(TEXT("SelectionMode"));
  if (StaticEnum<EVolumeSelectionMode>()->GetValueByNameString(
          SelectionModeString) != INDEX_NONE) {
    OutSettings.SelectionMode = static_cast<EVolumeSelectionMode>(
        StaticEnum<EVolumeSelectionMode>()->GetValueByNameString(
            SelectionModeString));
  }

  OutSettings.bUseComponentBounds =
      Root->GetBoolField(TEXT("UseComponentBounds"));

  if (Root->HasField(TEXT("ContainsFullyThreshold"))) {
    OutSettings.ContainsFullyThreshold = static_cast<float>(
        Root->GetNumberField(TEXT("ContainsFullyThreshold")));
  }

  OutSettings.ActorTypeMask =
      static_cast<uint32>(Root->GetNumberField(TEXT("ActorTypeMask")));

  if (Root->HasField(TEXT("ExcludeActorClassList"))) {
    JsonToStrings(Root->GetArrayField(TEXT("ExcludeActorClassList")),
                  OutSettings.ExcludeActorClassList);
  }
  if (Root->HasField(TEXT("ExcludeComponentClassList"))) {
    JsonToStrings(Root->GetArrayField(TEXT("ExcludeComponentClassList")),
                  OutSettings.ExcludeComponentClassList);
  }
  if (Root->HasField(TEXT("IncludeTagList"))) {
    JsonToNames(Root->GetArrayField(TEXT("IncludeTagList")),
                OutSettings.IncludeTagList);
  }
  if (Root->HasField(TEXT("ExcludeTagList"))) {
    JsonToNames(Root->GetArrayField(TEXT("ExcludeTagList")),
                OutSettings.ExcludeTagList);
  }

  // Class Picker Filter
  if (Root->HasField(TEXT("EnableClassPickerFilter"))) {
    OutSettings.bEnableClassPickerFilter =
        Root->GetBoolField(TEXT("EnableClassPickerFilter"));
  }
  if (Root->HasField(TEXT("SelectedActorClassPaths"))) {
    JsonToStrings(Root->GetArrayField(TEXT("SelectedActorClassPaths")),
                  OutSettings.SelectedActorClassPaths);
  }
  if (Root->HasField(TEXT("SelectedComponentClassPaths"))) {
    JsonToStrings(Root->GetArrayField(TEXT("SelectedComponentClassPaths")),
                  OutSettings.SelectedComponentClassPaths);
  }

  OutSettings.bMoveToFolder = Root->GetBoolField(TEXT("MoveToFolder"));
  OutSettings.TargetFolderPath =
      FName(*Root->GetStringField(TEXT("TargetFolderPath")));
  OutSettings.bAutoNameFolderFromVolume =
      Root->GetBoolField(TEXT("AutoNameFolderFromVolume"));
  OutSettings.bPreserveFolderStructure =
      Root->GetBoolField(TEXT("PreserveFolderStructure"));

  const FString DataLayerPath = Root->GetStringField(TEXT("TargetDataLayer"));
  if (!DataLayerPath.IsEmpty()) {
    OutSettings.TargetDataLayer =
        LoadObject<UDataLayerAsset>(nullptr, *DataLayerPath);
  } else {
    OutSettings.TargetDataLayer = nullptr;
  }

  return true;
}
