// Components/BunkerAdvisorComponent.cpp
#include "Components/BunkerAdvisorComponent.h"
#include "Components/BunkerCoverComponent.h"
#include "Bunkers/BunkerBase.h"
#include "Components/CoverSplineComponent.h"
#include "Components/DecalComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"

UBunkerAdvisorComponent::UBunkerAdvisorComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UBunkerAdvisorComponent::UpdateIndicator()
{
    if (!bShowSuggestionIndicator)
    {
        ClearIndicator();
        return;
    }

    if (!SuggestedCandidate.IsValid())
    {
        ClearIndicator();
        return;
    }

    if (!SuggestDecal)
    {
        SuggestDecal = NewObject<UDecalComponent>(GetOwner());
        if (!SuggestDecal) return;
        SuggestDecal->RegisterComponent();
        SuggestDecal->SetDecalMaterial(SuggestIndicatorMaterial);
        SuggestDecal->DecalSize = FVector(SuggestIndicatorSize, SuggestIndicatorSize, SuggestIndicatorSize);
        SuggestDecal->SetFadeScreenSize(0.0f);
    }

    const FTransform& T = SuggestedCandidate.AnchorTransform;

    const FVector Loc = T.GetLocation();
    const FRotator Rot = FRotator(-90.f, 0.f, 0.f);

    SuggestDecal->SetWorldLocation(Loc + FVector(0,0,5.f));
    SuggestDecal->SetWorldRotation(Rot);
    SuggestDecal->DecalSize = FVector(SuggestIndicatorSize, SuggestIndicatorSize, SuggestIndicatorSize);
    SuggestDecal->SetVisibility(true, true);
}

void UBunkerAdvisorComponent::ClearIndicator()
{
    if (SuggestDecal)
    {
        SuggestDecal->SetVisibility(false, true);
    }
}

void UBunkerAdvisorComponent::BeginPlay()
{
    Super::BeginPlay();
    OwnerCharacter = Cast<ACharacter>(GetOwner());
    if (OwnerCharacter.IsValid())
    {
        CoverComp = OwnerCharacter->FindComponentByClass<UBunkerCoverComponent>();
    }
    KnownEnemies.Reset();
    for (AActor* E : KnownEnemies_BP)
    {
        if (E) KnownEnemies.Add(E);
    }
}

bool UBunkerAdvisorComponent::UpdateSuggestion()
{
    if (bManualOverride && SuggestedCandidate.IsValid())
    {
        if (SuggestedCandidate.Bunker && SuggestedCandidate.Bunker->GetCoverSpline())
        {
            SuggestedCandidate.AnchorTransform =
                SuggestedCandidate.Bunker->GetCoverSpline()->GetWorldTransformAtAlpha(SuggestedCandidate.Alpha);
        }
        UpdateIndicator();
        return true;
    }

    TArray<FBunkerCandidate> Candidates;
    GatherCandidates(Candidates);

    FBunkerCandidate Best; float BestScore = -FLT_MAX;
    for (const FBunkerCandidate& C : Candidates)
    {
        const float S = ScoreCandidate(C);
        if (S > BestScore)
        {
            BestScore = S;
            Best      = C;
            Best.Score= S;
        }
    }

    const bool bChanged =
        (Best.Bunker != SuggestedCandidate.Bunker) ||
        !FMath::IsNearlyEqual(Best.Alpha, SuggestedCandidate.Alpha, 1e-3f);

    SuggestedCandidate = Best;

    if (bChanged && SuggestedCandidate.IsValid())
    {
        OnSuggestedBunkerChanged.Broadcast(SuggestedCandidate);
        UpdateIndicator();
    }
    else if (!SuggestedCandidate.IsValid())
    {
        ClearIndicator();
    }

    return SuggestedCandidate.IsValid();
}

bool UBunkerAdvisorComponent::AcceptSuggestion()
{
    if (!SuggestedCandidate.IsValid() || !OwnerCharacter.IsValid())
        return false;

    if (IsCloseEnoughToEnter(SuggestedCandidate))
    {
        if (CoverComp.IsValid())
        {
            const bool bServer = OwnerCharacter->HasAuthority();
            if (bServer) CoverComp->EnterCoverAtAlpha(SuggestedCandidate.Bunker.Get(), SuggestedCandidate.Alpha);
            else         CoverComp->Server_EnterCoverAtAlpha(SuggestedCandidate.Bunker.Get(), SuggestedCandidate.Alpha);

            UpdateSuggestion(); // compute next suggestion
            return true;
        }
        return false;
    }

    // request traversal
    OnBeginTraverseTo.Broadcast(SuggestedCandidate.Bunker.Get(), SuggestedCandidate.Alpha);
    return true;
}

void UBunkerAdvisorComponent::SetManualSuggestion(ABunkerBase* Bunker, float Alpha)
{
    bManualOverride = (Bunker != nullptr);

    SuggestedCandidate.Bunker = Bunker;
    SuggestedCandidate.Alpha  = FMath::Clamp(Alpha, 0.f, 1.f);
    SuggestedCandidate.AnchorTransform =
        (Bunker && Bunker->GetCoverSpline())
        ? Bunker->GetCoverSpline()->GetWorldTransformAtAlpha(SuggestedCandidate.Alpha)
        : FTransform::Identity;

    if (bManualOverride)
    {
        OnSuggestedBunkerChanged.Broadcast(SuggestedCandidate);
        UpdateIndicator();
    }
}

void UBunkerAdvisorComponent::ClearManualSuggestion()
{
    bManualOverride = false;
}

void UBunkerAdvisorComponent::GatherCandidates(TArray<FBunkerCandidate>& Out) const
{
    Out.Reset();
    if (!OwnerCharacter.IsValid()) return;

    const FVector Origin = OwnerCharacter->GetActorLocation();
    const float R2 = FMath::Square(SearchRadius);

    ABunkerBase* CurrentB = nullptr;
    if (CoverComp.IsValid() && CoverComp->IsInCover())
    {
        CurrentB = CoverComp->GetCurrentBunker();
    }

    TArray<AActor*> Bunkers;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), ABunkerBase::StaticClass(), Bunkers);

    for (AActor* A : Bunkers)
    {
        ABunkerBase* B = Cast<ABunkerBase>(A);
        if (!B) continue;
        UCoverSplineComponent* S = B->GetCoverSpline();
        if (!S) continue;

        // HARD EXCLUDE: never suggest the bunker we’re currently in
        if (B == CurrentB) continue;

        // Sample a few sensible alphas: endpoints and nearest
        const float A0 = S->FindClosestAlpha(Origin);
        TArray<float> Alphas = { S->Tmin, A0, S->Tmax };
        // Deduplicate
        Alphas.Sort();
        for (int32 i=Alphas.Num()-1; i>0; --i)
            if (FMath::IsNearlyEqual(Alphas[i], Alphas[i-1], 1e-3f)) Alphas.RemoveAt(i);

        for (float Alpha : Alphas)
        {
            const FTransform WT = S->GetWorldTransformAtAlpha(Alpha);
            const FVector Loc = WT.GetLocation();
            if (FVector::DistSquared(Origin, Loc) > R2) continue;

            FBunkerCandidate C;
            C.Bunker = B;
            C.Alpha  = Alpha;
            C.AnchorTransform = WT;
            Out.Add(C);
        }
    }
}

float UBunkerAdvisorComponent::ScoreCandidate(const FBunkerCandidate& Candidate) const
{
    if (!OwnerCharacter.IsValid()) return -FLT_MAX;

    const FVector From = OwnerCharacter->GetActorLocation();
    const FVector To   = Candidate.AnchorTransform.GetLocation();

    float Score = 0.f;

    // Distance penalty (linear)
    Score -= Weights.DistancePenalty * DistancePenalty(From, To);

    // Forward direction bias
    Score += Weights.ForwardBias * ForwardAlignmentBonus(From, To);

    // Exposure penalty
    if (IsAnchorExposedToEnemies(Candidate))
    {
        Score -= Weights.ExposedPenalty;
    }

    // Stance comfort: prefer spots matching current stance requirement
    if (CoverComp.IsValid() && Candidate.Bunker && Candidate.Bunker->GetCoverSpline())
    {
        const ECoverStance Current = CoverComp->GetStance();
        const ECoverStance Req = Candidate.Bunker->GetCoverSpline()->RequiredStanceAtAlpha(Candidate.Alpha);
        if (Req == Current) Score += Weights.StanceComfortBonus;
    }

    // Novelty: avoid re-suggesting very close alpha on the same bunker
    if (CoverComp.IsValid() && CoverComp->IsInCover() && Candidate.Bunker == CoverComp->GetCurrentBunker())
    {
        const float DeltaA = FMath::Abs(Candidate.Alpha - CoverComp->GetCoverAlpha());
        if (DeltaA < 0.05f) Score -= Weights.NoveltyBias;
    }

    return Score;
}

float UBunkerAdvisorComponent::DistancePenalty(const FVector& From, const FVector& To) const
{
    return FVector::Dist(From, To) * 0.01f; // 1 point per 100uu, scaled by Weights.DistancePenalty
}

float UBunkerAdvisorComponent::ForwardAlignmentBonus(const FVector& From, const FVector& To) const
{
    if (!bUsePawnForwardForBias || !OwnerCharacter.IsValid()) return 0.f;
    const FVector Forward = OwnerCharacter->GetActorForwardVector().GetSafeNormal();
    const FVector Dir     = (To - From).GetSafeNormal();
    return FMath::Clamp(FVector::DotProduct(Forward, Dir), -1.f, 1.f);
}

bool UBunkerAdvisorComponent::IsAnchorExposedToEnemies(const FBunkerCandidate& Candidate) const
{
    if (KnownEnemies.Num() == 0) return false;

    const FVector Anchor = Candidate.AnchorTransform.GetLocation();

    for (const TWeakObjectPtr<AActor>& Enemy : KnownEnemies)
    {
        if (!Enemy.IsValid()) continue;

        const FVector Eye = Enemy->GetActorLocation() + FVector(0,0,60.f);
        FHitResult HR;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(BunkerAdvVis), false, Enemy.Get());

        const bool bHit = GetWorld()->LineTraceSingleByChannel(HR, Eye, Anchor, VisibilityChannel, Params);

        // If line is clear, or hits the candidate bunker itself, consider exposed
        if (!bHit || HR.GetActor() == Candidate.Bunker.Get())
        {
            return true;
        }
    }
    return false;
}

bool UBunkerAdvisorComponent::IsCloseEnoughToEnter(const FBunkerCandidate& Candidate) const
{
    if (!OwnerCharacter.IsValid()) return false;
    const float Dist = FVector::Dist(OwnerCharacter->GetActorLocation(), Candidate.AnchorTransform.GetLocation());
    return Dist <= EnterDistanceThreshold;
}
