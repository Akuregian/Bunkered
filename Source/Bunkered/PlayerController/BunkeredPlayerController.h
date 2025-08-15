// BunkeredPlayerController.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "BunkeredPlayerController.generated.h"

class UInputMappingContext;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;

/**
 * Centralizes input. Forwards to the possessed pawn via BunkerCoverInterface.
 */
UCLASS()
class BUNKERED_API ABunkeredPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    ABunkeredPlayerController();

protected:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;

    // Mapping context
    UPROPERTY(EditDefaultsOnly, Category="Input")
    TArray<UInputMappingContext*> DefaultMappingContexts;

    // Axis actions
    UPROPERTY(EditDefaultsOnly, Category="Input")
    UInputAction* MoveAction = nullptr;

    UPROPERTY(EditDefaultsOnly, Category="Input")
    UInputAction* LookAction = nullptr;

    // Cover actions
    UPROPERTY(EditDefaultsOnly, Category="Input") UInputAction* EnterSlotOnBunkerAction    = nullptr;
    UPROPERTY(EditDefaultsOnly, Category="Input") UInputAction* ChangeStanceAction = nullptr;

private:
    // Helpers to dispatch to the interface
    void OnMove(const FInputActionValue& Value);
    void OnLook(const FInputActionValue& Value);

    void OnEnterSlotOnBunker();
    void OnStanceChange();

    UObject* GetPawnObject() const { return GetPawn(); }
};
