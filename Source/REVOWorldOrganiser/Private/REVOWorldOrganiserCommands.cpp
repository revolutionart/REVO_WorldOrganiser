// Copyright 2026, REVOLUTIONART All rights reserved

#include "REVOWorldOrganiserCommands.h"

#define LOCTEXT_NAMESPACE "REVOWorldOrganiser"

void FREVOWorldOrganiserCommands::RegisterCommands()
{
    UI_COMMAND(REVOWorldOrganiserAll, "Select In Volume (All)", "Select all actors inside the selected volume.", EUserInterfaceActionType::Button, FInputChord());
    UI_COMMAND(REVOWorldOrganiserBlueprints, "Select In Volume (Blueprints)", "Select blueprint actors inside the selected volume.", EUserInterfaceActionType::Button, FInputChord());
    UI_COMMAND(REVOWorldOrganiserLights, "Select In Volume (Lights)", "Select light actors inside the selected volume.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
