// Copyright 2026, REVOLUTIONART All rights reserved

#include "REVSelectionVolume.h"
#include "Components/BoxComponent.h"
#include "Components/BrushComponent.h"
#include "Engine/CollisionProfile.h"

AREVSelectionVolume::AREVSelectionVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Disable ticking - this volume is static and editor-only
	PrimaryActorTick.bCanEverTick = false;
	
	// Don't include in builds
	bIsEditorOnlyActor = true;

	// Create box component for visible wireframe bounds
	BoxComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("SelectionBox"));
	BoxComponent->SetupAttachment(RootComponent);
	BoxComponent->SetBoxExtent(FVector(100.0f, 100.0f, 100.0f)); // 200x200x200 box
	BoxComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoxComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	BoxComponent->SetGenerateOverlapEvents(false);
	BoxComponent->SetCanEverAffectNavigation(false);
	BoxComponent->SetHiddenInGame(true);
	BoxComponent->bDrawOnlyIfSelected = false; // Always visible
	BoxComponent->ShapeColor = FColor::Yellow;
	
	// Configure the inherited brush component
	if (UBrushComponent* BrushComp = GetBrushComponent())
	{
		BrushComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BrushComp->SetHiddenInGame(true);
	}
}
