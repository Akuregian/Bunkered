// Components/BunkerAdvisorComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/CoverTypes.h"
#include "BunkerAdvisorComponent.generated.h"

class ABunkerBase;
class UBunkerCoverComponent;
class ACharacter;
class UDecalComponent;

/** Tunable weights for scoring bunker candidates */
USTRUCT(BlueprintType)
struct FBunkerScoringWeights
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scoring", meta=(ClampMin="0.0")) float DistancePenalty = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scoring", meta=(ClampMin="0.0")) float ForwardBias     = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scoring", meta=(ClampMin="0.0")) float ExposedPenalty  = 2.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scoring", meta=(ClampMin="0.0")) float StanceComfortBonus = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scoring") float NoveltyBias = 0.1f;
};

/** Result for a single bunker spline candidate */
USTRUCT(BlueprintType)
struct FBunkerCandidate
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) TObjectPtr<ABunkerBase> Bunker = nullptr;
    UPROPERTY(BlueprintReadOnly) float Alpha = 0.f; // 0..1 on bunker spline
    UPROPERTY(BlueprintReadOnly) float Score = -FLT_MAX;
    UPROPERTY(BlueprintReadOnly) FTransform AnchorTransform = FTransform::Identity;

    bool IsValid() const { return Bunker != nullptr; }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSuggestedBunkerChanged, const FBunkerCandidate&, NewSuggestion);
/** Traversal request: pawn should move toward AnchorTransform then enter at Alpha. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBeginTraverseTo, ABunkerBase*, TargetBunker, float, TargetAlpha);

/** Bunker Advisor: scores and suggests (Bunker, Alpha) spline candidates. */
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

    /** Accept current suggestion. If close, enter; otherwise broadcast traverse request. */
    UFUNCTION(BlueprintCallable, Category="Bunker|Advise")
    bool AcceptSuggestion();

    /** Manual override */
    UFUNCTION(BlueprintCallable, Category="Bunker|Advise")
    void SetManualSuggestion(ABunkerBase* Bunker, float Alpha);
    UFUNCTION(BlueprintCallable, Category="Bunker|Advise")
    void ClearManualSuggestion();

    /** Settings */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bunker|Advise") float SearchRadius = 3000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bunker|Advise") FBunkerScoringWeights Weights;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bunker|Advise") TEnumAsByte<ECollisionChannel> VisibilityChannel = ECC_Visibility;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bunker|Advise") bool bUsePawnForwardForBias = true;

    /** Designer enemy list (BP-exposed) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bunker|Advise") TArray<AActor*> KnownEnemies_BP;

    /** Event: suggestion changed */
    UPROPERTY(BlueprintAssignable, Category="Bunker|Advise|Events") FOnSuggestedBunkerChanged OnSuggestedBunkerChanged;
    /** Event: traversal requested */
    UPROPERTY(BlueprintAssignable, Category="Bunker|Advise|Events") FOnBeginTraverseTo OnBeginTraverseTo;

    // --- Suggestion indicator (optional) ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bunker|Advise|Indicator") bool bShowSuggestionIndicator = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bunker|Advise|Indicator", meta=(ClampMin="16.0")) float SuggestIndicatorSize = 64.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bunker|Advise|Indicator") UMaterialInterface* SuggestIndicatorMaterial = nullptr;
    UPROPERTY(Transient) UDecalComponent* SuggestDecal = nullptr;
    void UpdateIndicator();
    void ClearIndicator();
    // ---------------------------------------

protected:
    virtual void BeginPlay() override;

private:
    TWeakObjectPtr<ACharacter> OwnerCharacter;
    TWeakObjectPtr<UBunkerCoverComponent> CoverComp;
    TArray<TWeakObjectPtr<AActor>> KnownEnemies;

    FBunkerCandidate SuggestedCandidate;
    bool bManualOverride = false;

    void GatherCandidates(TArray<FBunkerCandidate>& Out) const;
    float ScoreCandidate(const FBunkerCandidate& Candidate) const;
    bool  IsAnchorExposedToEnemies(const FBunkerCandidate& Candidate) const;

    float DistancePenalty(const FVector& From, const FVector& To) const;
    float ForwardAlignmentBonus(const FVector& From, const FVector& To) const;
    bool  IsCloseEnoughToEnter(const FBunkerCandidate& Candidate) const;

    UPROPERTY(EditAnywhere, Category="Bunker|Advise") float EnterDistanceThreshold = 120.f;
};
