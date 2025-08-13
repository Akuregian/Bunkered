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

    /** Actions (assign in BP/Defaults) */
    UPROPERTY(EditDefaultsOnly, Category="Input")
    UInputAction* SuggestAction = nullptr;

    UPROPERTY(EditDefaultsOnly, Category="Input")
    UInputAction* AcceptSuggestionAction = nullptr; // bind this to F

    // handlers
    void OnSuggest();
    void OnAccept();

    // Axis actions
    UPROPERTY(EditDefaultsOnly, Category="Input")
    UInputAction* MoveAction = nullptr;

    UPROPERTY(EditDefaultsOnly, Category="Input")
    UInputAction* LookAction = nullptr;

    // Cover actions
    UPROPERTY(EditDefaultsOnly, Category="Input") UInputAction* ToggleCoverAction    = nullptr;
    UPROPERTY(EditDefaultsOnly, Category="Input") UInputAction* MoveSlotLeftAction  = nullptr;
    UPROPERTY(EditDefaultsOnly, Category="Input") UInputAction* MoveSlotRightAction = nullptr;

    UPROPERTY(EditDefaultsOnly, Category="Input") UInputAction* PeekLeftAction  = nullptr;
    UPROPERTY(EditDefaultsOnly, Category="Input") UInputAction* PeekRightAction = nullptr;
    UPROPERTY(EditDefaultsOnly, Category="Input") UInputAction* PeekOverAction  = nullptr;

    UPROPERTY(EditDefaultsOnly, Category="Input") UInputAction* StandAction  = nullptr;
    UPROPERTY(EditDefaultsOnly, Category="Input") UInputAction* CrouchAction = nullptr;
    UPROPERTY(EditDefaultsOnly, Category="Input") UInputAction* ProneAction  = nullptr;

private:
    // Helpers to dispatch to the interface
    void OnMove(const FInputActionValue& Value);
    void OnLook(const FInputActionValue& Value);

    void OnEnterSlotOnBunker();
    void OnMoveSlotLeft();
    void OnMoveSlotRight();

    void OnPeekLeft_Pressed();  void OnPeekLeft_Released();
    void OnPeekRight_Pressed(); void OnPeekRight_Released();
    void OnPeekOver_Pressed();  void OnPeekOver_Released();

    void OnStand(); void OnCrouch(); void OnProne();

    UObject* GetPawnObject() const { return GetPawn(); }
};
