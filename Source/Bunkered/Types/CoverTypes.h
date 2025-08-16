// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "CoverTypes.generated.h"

/**
 * 
 */

UENUM(BlueprintType)
enum class ECoverStance : uint8
{
	Stand UMETA(DisplayName="Stand"),
	Crouch UMETA(DisplayName="Crouch"),
	Prone UMETA(DisplayName="Prone")
};

// Peek directions for the Slot
UENUM(BlueprintType)
enum class EPeekDirection : uint8
{
	None  UMETA(DisplayName="None"),
	Left  UMETA(DisplayName="Left"),
	Right UMETA(DisplayName="Right"),
	Over  UMETA(DisplayName="Over")
};

UENUM(BlueprintType)
enum class EExposureState : uint8
{
	Hidden     UMETA(DisplayName="Hidden"),
	Peeking    UMETA(DisplayName="Peeking"),     // micro exposure (lean)
	Exposed    UMETA(DisplayName="Exposed")      // fully out (e.g., shooting position)
};

USTRUCT(BlueprintType)
struct FCoverSlot
{
	GENERATED_BODY()

	/** Optional component reference. If set and resolvable, this component's world transform is used for the slot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover Slot")
	FComponentReference SlotPoint;

	/** If true and SlotPoint resolves to a scene component, we use that component's world transform for this slot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover Slot")
	bool bUseComponentTransform = true;

	/** Fallback/local anchor (relative to bunker actor). Used if SlotPoint is invalid or bUseComponentTransform=false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover Slot")
	FTransform LocalAnchor = FTransform::Identity;

	/** Which stances are permitted at this slot. Empty = all allowed (designer convenience). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover Slot")
	TArray<ECoverStance> AllowedStances;

	/** Which peek directions are allowed. Empty = all allowed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover Slot")
	TArray<EPeekDirection> AllowedPeeks;

	/** Radius used to "snap-in" to this slot when entering cover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover Slot", meta=(ClampMin="0.0", UIMin="0.0"))
	float EntryRadius = 75.f;

	/** Optional label to help designers identify the slot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover Slot")
	FName SlotName = NAME_None;

	/** Optional: convenience flags for your game feel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover Slot")
	bool bRightSide = false;

	/** Peek offsets for animation/IK tuning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover Slot")
	float LateralPeekOffset = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover Slot")
	float VerticalPeekOffset = 20.f;
};