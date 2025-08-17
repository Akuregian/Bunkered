// Types/CoverTypes.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "CoverTypes.generated.h"

UENUM(BlueprintType)
enum class ECoverStance : uint8
{
	Stand  UMETA(DisplayName="Stand"),
	Crouch UMETA(DisplayName="Crouch"),
	Prone  UMETA(DisplayName="Prone")
};

UENUM(BlueprintType)
enum class EPeekDirection : uint8
{
	None  UMETA(DisplayName="None"),
	Left  UMETA(DisplayName="Left"),
	Right UMETA(DisplayName="Right"),
	Over  UMETA(DisplayName="Over") // reserved, not used in spline peeking
};

UENUM(BlueprintType)
enum class EExposureState : uint8
{
	Hidden  UMETA(DisplayName="Hidden"),
	Peeking UMETA(DisplayName="Peeking"),
	Exposed UMETA(DisplayName="Exposed")
};
