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

/** Third-person pawn that EXECUTES cover actions (spline-only). */
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

    /** Cover Component - bunker movement and cover logic */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Components")
    TObjectPtr<UBunkerCoverComponent> BunkerCoverComponent;

    /** BunkerAdvisor - suggests bunker+alpha to traverse to */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Components")
    TObjectPtr<UBunkerAdvisorComponent> BunkerAdvisorComponent;

    /** Peek Component - handles peeking */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Components")
    TObjectPtr<UPeekComponent> PeekComponent;

protected:
    // === IBunkerCoverInterface ===
    virtual void Cover_EnterNearest_Implementation() override;
    virtual void Cover_Exit_Implementation() override;
    virtual void Cover_Slide_Implementation(float Axis) override;
    virtual void Cover_Peek_Implementation(EPeekDirection Direction, bool bPressed) override;
    virtual void Pawn_Movement_Implementation(FVector2D Move) override;
    virtual void Pawn_MouseLook_Implementation(FVector2D Look) override;
    virtual void Pawn_ChangeBunkerStance_Implementation(bool bCrouching) override;

    // helpers
    bool FindNearbyBunkerAndAlpha(ABunkerBase*& OutBunker, float& OutAlpha) const;

    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

public:
    // === Auto-traverse glue (advisor → pawn) ===
    UFUNCTION()
    void HandleBeginTraverseTo(ABunkerBase* TargetBunker, float TargetAlpha);

    void StartMoveTo(const FVector& Dest);
    void PollArrivalAndEnter();        // timer callback
    bool CheckAndAutoVaultToward(const FVector& Dest); // optional vault

    FTimerHandle MovePollTimer;
    TWeakObjectPtr<ABunkerBase> PendingBunker;
    float PendingAlpha = 0.f;

    // Optional tuning
    UPROPERTY(EditAnywhere, Category="Movement|Vault") float VaultProbeDistance = 150.f;
    UPROPERTY(EditAnywhere, Category="Movement|Vault") float VaultMaxHeight = 90.f;
    UPROPERTY(EditAnywhere, Category="Movement|Vault") float VaultForwardImpulse = 400.f;
    UPROPERTY(EditAnywhere, Category="Movement|Vault") float VaultUpImpulse = 300.f;

    // Spline traversal tuning
    UPROPERTY(EditAnywhere, Category="Cover") float SlideStepAlpha = 0.10f;    // per key press
    UPROPERTY(EditAnywhere, Category="Cover") float EnterDistanceThreshold = 120.f;
    
    // Optional helpers for UI/blueprints
    UFUNCTION(BlueprintCallable, Category="Input") void DoMove(float Right, float Forward);
    UFUNCTION(BlueprintCallable, Category="Input") void DoLook(float Yaw, float Pitch);
    UFUNCTION(BlueprintCallable, Category="Input") void DoCrouchToggle();
    UFUNCTION(BlueprintCallable, Category="Bunker|Navigate") bool Nav_UpdateSuggestion();
    UFUNCTION(BlueprintCallable, Category="Bunker|Navigate") bool Nav_AcceptSuggestion();
};
