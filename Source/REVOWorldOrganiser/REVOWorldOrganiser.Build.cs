// Copyright 2026, REVOLUTIONART All rights reserved

using UnrealBuildTool;

public class REVOWorldOrganiser : ModuleRules
{
    public REVOWorldOrganiser(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                "InputCore"
            });

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "UnrealEd",
                "EditorFramework",
                "LevelEditor",
                "ToolMenus",
                "EditorStyle",
                "Json",
                "JsonUtilities",
                "DataLayerEditor",
                "Niagara",
                "CinematicCamera",
                "Projects"
            });
    }
}
