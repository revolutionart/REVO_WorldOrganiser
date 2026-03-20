// Copyright 2026, REVOLUTIONART All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "REVSelectionVolume.generated.h"

class UBoxComponent;

/**
 * REVSelectionVolume
 * 
 * A simple, editor-only volume used for selecting actors within a defined region.
 * This volume has no collision, no gameplay effects, and exists purely for 
 * volume-based actor selection in the editor.
 * Uses a UBoxComponent for visible wireframe bounds.
 */
UCLASS()
class REVOWORLDORGANISER_API AREVSelectionVolume : public AVolume
{
	GENERATED_BODY()

public:
	AREVSelectionVolume(const FObjectInitializer& ObjectInitializer);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Selection Volume")
	UBoxComponent* BoxComponent;
};
