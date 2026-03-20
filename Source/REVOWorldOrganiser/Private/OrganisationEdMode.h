// Copyright 2026, REVOLUTIONART All rights reserved

#pragma once

#include "EdMode.h"

class FOrganisationEdMode : public FEdMode
{
public:
    static const FEditorModeID EM_OrganisationModeId;

    FOrganisationEdMode();
    virtual ~FOrganisationEdMode() override;

    virtual void Enter() override;
    virtual void Exit() override;
    virtual bool UsesToolkits() const override;
};
