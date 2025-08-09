// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"

/**
 * Used for debugging and logging
 */

#define DEBUG(Time, Color, Message) \
if (GEngine) { \
GEngine->AddOnScreenDebugMessage(-1, Time, Color, Message); \
}

class BUNKERED_API LoggingMacros
{
};
