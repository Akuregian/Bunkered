// BunkeredCharacter.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "Interface/BunkerCoverInterface.h"
#include "Types/CoverTypes.h"
#include "BunkeredCharacter.generated.h"

class UBunkerAdvisorComponent;
class UPeekComponent;
class ABunkerBase;
class UBunkerCoverComponent;
class USpringArmComponent;
class UCameraComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 * Third-person pawn that EXECUTES cover actions.
 * PlayerController owns input and calls the BunkerCoverInterface methods.
 */
UCLASS()
class BUNKERED_API ABunkeredCharacter : public ACharacter, public IBunkerCoverInterface
{
    GENERATED_BODY()

    /** Camera boom positioning the camera behind the character */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Camera, meta=(AllowPrivateAccess="true"))
    USpringArmComponent* CameraBoom;

    /** Follow camera */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Camera, meta=(AllowPrivateAccess="true"))
    UCameraComponent* FollowCamera;

public:
    ABunkeredCharacter();

    /** Bunker Cover Component - bunker movement and cover logic */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Components")
    TObjectPtr<UBunkerCoverComponent> BunkerCoverComponent;

    /* BunkerAdvisor - Suggest optimal bunkers o traverse to */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Components")
    TObjectPtr<UBunkerAdvisorComponent> BunkerAdvisorComponent;

    /* Peek Component - Handles all Logic for peeking */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Components")
    TObjectPtr<UPeekComponent> PeekComponent;

protected:
    // === IBunkerCoverInterface ===
    virtual void EnterSlotOnBunker_Implementation() override;
    virtual void SlotTransition_Implementation(int32 Delta) override;
    virtual void SetSlotStance_Implementation(ECoverStance Stance) override;
    virtual void SlotPeek_Implementation(EPeekDirection Direction, bool bPressed) override;
    virtual void Pawn_Movement_Implementation(FVector2D Move) override;
    virtual void Pawn_MouseLook_Implementation(FVector2D Look) override;
    virtual void Pawn_ChangeBunkerStance_Implementation(bool bCrouching) override;

    // Helper: find nearby bunker/slot (you can swap to EQS later)
    bool FindNearbyBunkerAndSlot(ABunkerBase*& OutBunker, int32& OutSlot) const;

    // No input binding here; PC handles inputs.
    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

public:

    // === Auto-traverse glue ===
    UFUNCTION()
    void HandleBeginTraverseTo(ABunkerBase* TargetBunker, int32 TargetSlot);

    void StartMoveTo(const FVector& Dest);
    void PollArrivalAndEnter();        // timer callback
    bool CheckAndAutoVaultToward(const FVector& Dest); // returns true if a vault was triggered

    FTimerHandle MovePollTimer;
    TWeakObjectPtr<ABunkerBase> PendingBunker;
    int32 PendingSlot = INDEX_NONE;

    // Optional tuning for auto-vault
    UPROPERTY(EditAnywhere, Category="Movement|Vault")
    float VaultProbeDistance = 150.f;     // how far ahead to probe

    UPROPERTY(EditAnywhere, Category="Movement|Vault")
    float VaultMaxHeight = 90.f;          // max obstacle height to auto-vault (uu)

    UPROPERTY(EditAnywhere, Category="Movement|Vault")
    float VaultForwardImpulse = 400.f;    // forward impulse when vaulting

    UPROPERTY(EditAnywhere, Category="Movement|Vault")
    float VaultUpImpulse = 300.f;         // vertical impulse when vaulting
    // ----- End auto-traverse glue -----
    
    // Optional ----- public helpers that UI/blueprints can call---------
    UFUNCTION(BlueprintCallable, Category="Input")
    void DoMove(float Right, float Forward);

    UFUNCTION(BlueprintCallable, Category="Input")
    void DoLook(float Yaw, float Pitch);

    UFUNCTION(BlueprintCallable, Category="Input")
    void DoCrouchToggle();
    
    UFUNCTION(BlueprintCallable, Category="Bunker|Navigate")
    bool Nav_UpdateSuggestion();

    UFUNCTION(BlueprintCallable, Category="Bunker|Navigate")
    bool Nav_AcceptSuggestion();
    // -----------------------------------------------------------------
};
