// Copyright 2026, REVOLUTIONART All rights reserved

#include "REVOWorldOrganiserStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Interfaces/IPluginManager.h"

TSharedPtr<FSlateStyleSet> FREVOWorldOrganiserStyle::StyleSet = nullptr;

void FREVOWorldOrganiserStyle::Initialize()
{
    if (StyleSet.IsValid())
    {
        return;
    }

    StyleSet = MakeShared<FSlateStyleSet>(GetStyleSetName());

    // Find the plugin's Resources folder
    TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("REVOWorldOrganiser"));
    if (Plugin.IsValid())
    {
        FString ContentDir = Plugin->GetBaseDir() / TEXT("Resources");
        StyleSet->SetContentRoot(ContentDir);
        
        // Register the custom toolbar icon (40x40 is standard for toolbar icons)
        // Try PNG first, will work with both PNG and SVG files
        FString IconPath = ContentDir / TEXT("Organisation_Mode_Icon.png");
        StyleSet->Set("REVOWorldOrganiser.ToolbarIcon", 
            new FSlateImageBrush(IconPath, FVector2D(40.f, 40.f)));
    }

    FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
}

void FREVOWorldOrganiserStyle::Shutdown()
{
    if (StyleSet.IsValid())
    {
        FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
        StyleSet.Reset();
    }
}

TSharedPtr<ISlateStyle> FREVOWorldOrganiserStyle::Get()
{
    return StyleSet;
}

FName FREVOWorldOrganiserStyle::GetStyleSetName()
{
    static FName StyleName(TEXT("REVOWorldOrganiserStyle"));
    return StyleName;
}
