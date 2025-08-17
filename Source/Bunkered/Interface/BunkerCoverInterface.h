// Interface/BunkerCoverInterface.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Types/CoverTypes.h"
#include "BunkerCoverInterface.generated.h"

UINTERFACE(MinimalAPI)
class UBunkerCoverInterface : public UInterface
{
	GENERATED_BODY()
};

class BUNKERED_API IBunkerCoverInterface
{
	GENERATED_BODY()
public:
	/** Enter the nearest bunker at its closest CoverAlpha to the pawn. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Cover|Input")
	void Cover_EnterNearest();

	/** Exit current cover (if any). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Cover|Input")
	void Cover_Exit();

	/** Slide along the active bunker’s spline. Axis is -1..+1 from A/D or stick X. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Cover|Input")
	void Cover_Slide(float Axis);

	/** Start/stop edge peeking. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Cover|Input")
	void Cover_Peek(EPeekDirection Direction, bool bPressed);

	// Pawn movement/look passthrough (out-of-cover traversal & camera)
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Pawn|Input")
	void Pawn_Movement(FVector2D Move);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Pawn|Input")
	void Pawn_MouseLook(FVector2D Look);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Pawn|Input")
	void Pawn_ChangeBunkerStance(bool bCrouching);
};
