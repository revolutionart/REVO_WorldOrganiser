// Copyright 2026, REVOLUTIONART All rights reserved

#pragma once

#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

class FREVOWorldOrganiserCommands : public TCommands<FREVOWorldOrganiserCommands>
{
public:
    FREVOWorldOrganiserCommands()
        : TCommands<FREVOWorldOrganiserCommands>(
            TEXT("REVOWorldOrganiser"),
            NSLOCTEXT("REVOWorldOrganiser", "REVOWorldOrganiser", "Select Actors In Volume"),
            NAME_None,
            FAppStyle::GetAppStyleSetName())
    {
    }

    virtual void RegisterCommands() override;

    TSharedPtr<FUICommandInfo> REVOWorldOrganiserAll;
    TSharedPtr<FUICommandInfo> REVOWorldOrganiserBlueprints;
    TSharedPtr<FUICommandInfo> REVOWorldOrganiserLights;
};
