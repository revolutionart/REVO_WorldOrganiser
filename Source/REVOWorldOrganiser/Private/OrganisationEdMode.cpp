// Copyright 2026, REVOLUTIONART All rights reserved

#include "OrganisationEdMode.h"

#include "OrganisationEdModeToolkit.h"
#include "EditorModeManager.h"

#define LOCTEXT_NAMESPACE "OrganisationEdMode"

const FEditorModeID FOrganisationEdMode::EM_OrganisationModeId = TEXT("EM_OrganisationMode");

FOrganisationEdMode::FOrganisationEdMode() = default;

FOrganisationEdMode::~FOrganisationEdMode() = default;

void FOrganisationEdMode::Enter()
{
    FEdMode::Enter();

    if (!Toolkit.IsValid())
    {
        Toolkit = MakeShareable(new FOrganisationEdModeToolkit());
        Toolkit->Init(Owner->GetToolkitHost());
    }
}

void FOrganisationEdMode::Exit()
{
    FEdMode::Exit();
}

bool FOrganisationEdMode::UsesToolkits() const
{
    return true;
}

#undef LOCTEXT_NAMESPACE
