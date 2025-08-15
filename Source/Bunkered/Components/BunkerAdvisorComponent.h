// Components/BunkerAdvisorComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/CoverTypes.h"
#include "BunkerAdvisorComponent.generated.h"

class ABunkerBase;
class UBunkerCoverComponent;
class ACharacter;

/** Tunable weights for scoring bunker candidates */
USTRUCT(BlueprintType)
struct FBunkerScoringWeights
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scoring", meta=(ClampMin="0.0"))
    float DistancePenalty = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scoring", meta=(ClampMin="0.0"))
    float ForwardBias = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scoring", meta=(ClampMin="0.0"))
    float ExposedPenalty = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scoring", meta=(ClampMin="0.0"))
    float StanceComfortBonus = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scoring")
    float NoveltyBias = 0.1f;
};

/** Result for a single bunker slot candidate */
USTRUCT(BlueprintType)
struct FBunkerCandidate
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) TObjectPtr<ABunkerBase> Bunker = nullptr;
    UPROPERTY(BlueprintReadOnly) int32 SlotIndex = INDEX_NONE;
    UPROPERTY(BlueprintReadOnly) float Score = -FLT_MAX;
    UPROPERTY(BlueprintReadOnly) FTransform SlotTransform = FTransform::Identity;

    bool IsValid() const { return Bunker != nullptr && SlotIndex != INDEX_NONE; }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSuggestedBunkerChanged, const FBunkerCandidate&, NewSuggestion);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBeginTraverseTo, ABunkerBase*, TargetBunker, int32, TargetSlot);

/**
 * Bunker Advisor: scores and suggests bunker+slot; accepts suggestion by either entering immediately or
 * broadcasting a traverse event (your character moves, then enters on arrival).
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class BUNKERED_API UBunkerAdvisorComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UBunkerAdvisorComponent();

    UFUNCTION(BlueprintCallable, Category="Bunker|Advise")
    bool UpdateSuggestion();

    UFUNCTION(BlueprintPure, Category="Bunker|Advise")
    FBunkerCandidate GetSuggestion() const { return SuggestedCandidate; }

    /** Accept current suggestion. If far, broadcasts OnBeginTraverseTo. If close, enters cover. */
    UFUNCTION(BlueprintCallable, Category="Bunker|Advise")
    bool AcceptSuggestion();

    /** Set/clear manual suggestion override */
    UFUNCTION(BlueprintCallable, Category="Bunker|Advise")
    void SetManualSuggestion(ABunkerBase* Bunker, int32 SlotIndex);
    UFUNCTION(BlueprintCallable, Category="Bunker|Advise")
    void ClearManualSuggestion();

    /** Settings */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bunker|Advise")
    float SearchRadius = 3000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bunker|Advise")
    FBunkerScoringWeights Weights;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bunker|Advise")
    TEnumAsByte<ECollisionChannel> VisibilityChannel = ECC_Visibility;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bunker|Advise")
    bool bUsePawnForwardForBias = true;

    /** Designer-facing enemy list (BP-visible) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bunker|Advise")
    TArray<AActor*> KnownEnemies_BP;

    /** Fired when suggestion changes */
    UPROPERTY(BlueprintAssignable, Category="Bunker|Advise|Events")
    FOnSuggestedBunkerChanged OnSuggestedBunkerChanged;

    /** Fired when AcceptSuggestion needs traversal (not close enough to enter cover immediately) */
    UPROPERTY(BlueprintAssignable, Category="Bunker|Advise|Events")
    FOnBeginTraverseTo OnBeginTraverseTo;
    
    // --- Suggestion indicator (optional) ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bunker|Advise|Indicator")
    bool bShowSuggestionIndicator = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bunker|Advise|Indicator", meta=(ClampMin="16.0"))
    float SuggestIndicatorSize = 64.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bunker|Advise|Indicator")
    UMaterialInterface* SuggestIndicatorMaterial = nullptr;

    UPROPERTY(Transient)
    UDecalComponent* SuggestDecal = nullptr;

    void UpdateIndicator();
    void ClearIndicator();
    // ---------------------------------------

protected:
    virtual void BeginPlay() override;

private:
    TWeakObjectPtr<ACharacter> OwnerCharacter;
    TWeakObjectPtr<UBunkerCoverComponent> CoverComp;

    // internal weak refs for scoring (safer than raw pointers at runtime)
    TArray<TWeakObjectPtr<AActor>> KnownEnemies;

    FBunkerCandidate SuggestedCandidate;
    bool bManualOverride = false;

    void GatherCandidates(TArray<FBunkerCandidate>& Out) const;
    float ScoreCandidate(const FBunkerCandidate& Candidate) const;
    bool  IsSlotExposedToEnemies(const FBunkerCandidate& Candidate) const;

    float DistancePenalty(const FVector& From, const FVector& To) const;
    float ForwardAlignmentBonus(const FVector& From, const FVector& To) const;
    bool  IsCloseEnoughToEnter(const FBunkerCandidate& Candidate) const;
};
