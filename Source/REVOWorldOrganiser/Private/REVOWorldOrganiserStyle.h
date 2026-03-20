// Copyright 2026, REVOLUTIONART All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

/**
 * Slate style set for REVOWorldOrganiser plugin custom icons and visuals.
 * Manages plugin-specific UI resources including toolbar icons.
 */
class FREVOWorldOrganiserStyle
{
public:
    static void Initialize();
    static void Shutdown();
    static TSharedPtr<class ISlateStyle> Get();
    static FName GetStyleSetName();

private:
    static TSharedPtr<class FSlateStyleSet> StyleSet;
};
