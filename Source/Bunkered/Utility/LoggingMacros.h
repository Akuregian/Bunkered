// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"

/**
 * Used for debugging and logging
*/

// You can change this to your own category if you like
#ifndef DEBUG_LOG_CATEGORY
#define DEBUG_LOG_CATEGORY LogTemp
#endif

#define DEBUG(Time, Color, Format, ...)                                                        \
do {                                                                                           \
const FString _DbgMsg = FString::Printf(Format, ##__VA_ARGS__);                            \
if (GEngine) {                                                                             \
GEngine->AddOnScreenDebugMessage(-1, Time, Color, _DbgMsg);                            \
}                                                                                          \
UE_LOG(DEBUG_LOG_CATEGORY, Log, TEXT("%s"), *_DbgMsg);                                     \
} while (0)


class BUNKERED_API LoggingMacros
{
};
