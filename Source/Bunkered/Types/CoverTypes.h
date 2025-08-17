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


// Quantization helpers for normalized [0..1] values replicated as bytes.
static FORCEINLINE uint8  Quantize01(float v) { return (uint8)FMath::Clamp(FMath::RoundToInt(v * 255.f), 0, 255); }
static FORCEINLINE float Dequantize01(uint8 q){ return float(q) / 255.f; }