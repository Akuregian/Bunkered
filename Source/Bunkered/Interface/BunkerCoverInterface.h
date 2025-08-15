// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "BunkerCoverInterface.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UBunkerCoverInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Input-intent surface. PlayerController calls these; pawn executes them.
 * Using BlueprintNativeEvent so you can call via IBunkerCoverInterface::Execute_* from PC.
 */

class BUNKERED_API IBunkerCoverInterface
{
	GENERATED_BODY()
public:
	
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Cover|Input")
	void EnterSlotOnBunker();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Cover|Input")
	void SlotTransition(int32 Delta); // -1 left, +1 right

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Cover|Input")
	void SetSlotStance(ECoverStance Stance);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Cover|Input")
	void SlotPeek(EPeekDirection Direction, bool bPressed);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Pawn|Input")
	void Pawn_Movement(FVector2D Move);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Pawn|Input")
	void Pawn_MouseLook(FVector2D Look);
	
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Pawn|Input")
	void Pawn_ChangeBunkerStance(bool bCrouching);
	
};

