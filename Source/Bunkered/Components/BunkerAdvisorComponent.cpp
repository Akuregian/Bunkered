// Components/BunkerAdvisorComponent.cpp
#include "Components/BunkerAdvisorComponent.h"
#include "Components/BunkerCoverComponent.h"
#include "Bunkers/BunkerBase.h"
#include "Components/DecalComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"

UBunkerAdvisorComponent::UBunkerAdvisorComponent()
{
    PrimaryComponentTick.bCanEverTick = false; // no ticking needed
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

    // Create decal lazily
    if (!SuggestDecal)
    {
        SuggestDecal = NewObject<UDecalComponent>(GetOwner());
        if (!SuggestDecal) return;
        SuggestDecal->RegisterComponent();
        SuggestDecal->SetDecalMaterial(SuggestIndicatorMaterial);
        SuggestDecal->DecalSize = FVector(SuggestIndicatorSize, SuggestIndicatorSize, SuggestIndicatorSize);
        SuggestDecal->SetFadeScreenSize(0.0f); // never fade by screen size
    }

    const FTransform& T = SuggestedCandidate.SlotTransform;

    // Face the decal upward, centered on the slot location
    const FVector Loc = T.GetLocation();
    const FRotator Rot = FRotator(-90.f, 0.f, 0.f); // point decal down onto the ground

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
    // Sync BP-provided enemies into weak refs once
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
        SuggestedCandidate.SlotTransform = SuggestedCandidate.Bunker->GetSlotWorldTransform(SuggestedCandidate.SlotIndex);
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
            Best = C;
            Best.Score = S;
        }
    }

    const bool bChanged =
        (Best.Bunker != SuggestedCandidate.Bunker) ||
        (Best.SlotIndex != SuggestedCandidate.SlotIndex);

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

    // Close enough? enter immediately via cover component
    if (IsCloseEnoughToEnter(SuggestedCandidate))
    {
        if (CoverComp.IsValid())
        {
            const bool bOK = CoverComp->TryEnterCover(SuggestedCandidate.Bunker.Get(), SuggestedCandidate.SlotIndex);
            if (bOK)
            {
                // Immediately compute next suggestion for the UI
                UpdateSuggestion();
            }
            return bOK;
        }
        return false;
    }

    // Not close → ask character/PC to move (you handle movement & auto-enter on arrival)
    OnBeginTraverseTo.Broadcast(SuggestedCandidate.Bunker.Get(), SuggestedCandidate.SlotIndex);
    return true;
}

void UBunkerAdvisorComponent::SetManualSuggestion(ABunkerBase* Bunker, int32 SlotIndex)
{
    bManualOverride = (Bunker != nullptr && SlotIndex != INDEX_NONE);
    SuggestedCandidate.Bunker = Bunker;
    SuggestedCandidate.SlotIndex = SlotIndex;
    SuggestedCandidate.SlotTransform = (Bunker && SlotIndex != INDEX_NONE)
        ? Bunker->GetSlotWorldTransform(SlotIndex)
        : FTransform::Identity;

    if (bManualOverride)
    {
        OnSuggestedBunkerChanged.Broadcast(SuggestedCandidate);
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

    // Identify current bunker to EXCLUDE entirely
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
        if (!B || B->GetNumSlots() <= 0) continue;

        // HARD EXCLUDE: never suggest the bunker we’re currently in
        if (B == CurrentB) continue;

        for (int32 i = 0; i < B->GetNumSlots(); ++i)
        {
            const FTransform WT = B->GetSlotWorldTransform(i);
            const FVector Loc = WT.GetLocation();
            if (FVector::DistSquared(Origin, Loc) > R2) continue;

            FBunkerCandidate C;
            C.Bunker = B;
            C.SlotIndex = i;
            C.SlotTransform = WT;
            Out.Add(C);
        }
    }
}

float UBunkerAdvisorComponent::ScoreCandidate(const FBunkerCandidate& Candidate) const
{
    if (!OwnerCharacter.IsValid()) return -FLT_MAX;

    const FVector From = OwnerCharacter->GetActorLocation();
    const FVector To   = Candidate.SlotTransform.GetLocation();

    float Score = 0.f;

    // Distance penalty (linear)
    Score -= Weights.DistancePenalty * DistancePenalty(From, To);

    // Forward direction bias
    Score += Weights.ForwardBias * ForwardAlignmentBonus(From, To);

    // Exposure penalty
    if (IsSlotExposedToEnemies(Candidate))
    {
        Score -= Weights.ExposedPenalty;
    }

    // Stance comfort
    if (CoverComp.IsValid())
    {
        const ECoverStance Current = CoverComp->GetStance();
        const FCoverSlot& Slot = Candidate.Bunker->GetSlot(Candidate.SlotIndex);
        if (Slot.AllowedStances.Num() == 0 || Slot.AllowedStances.Contains(Current))
        {
            Score += Weights.StanceComfortBonus;
        }
    }

    // Novelty (avoid re-suggesting the exact same slot)
    if (CoverComp.IsValid() && CoverComp->IsInCover())
    {
        if (Candidate.Bunker.Get() == CoverComp->GetCurrentBunker() &&
            Candidate.SlotIndex == CoverComp->GetCurrentSlot())
        {
            Score -= Weights.NoveltyBias;
        }
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

bool UBunkerAdvisorComponent::IsSlotExposedToEnemies(const FBunkerCandidate& Candidate) const
{
    if (KnownEnemies.Num() == 0) return false;

    const FVector SlotLoc = Candidate.SlotTransform.GetLocation();

    for (const TWeakObjectPtr<AActor>& Enemy : KnownEnemies)
    {
        if (!Enemy.IsValid()) continue;

        const FVector Eye = Enemy->GetActorLocation() + FVector(0,0,60.f);
        FHitResult HR;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(BunkerAdvVis), false, Enemy.Get());

        const bool bHit = GetWorld()->LineTraceSingleByChannel(HR, Eye, SlotLoc, VisibilityChannel, Params);

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

    const float Dist = FVector::Dist(OwnerCharacter->GetActorLocation(), Candidate.SlotTransform.GetLocation());
    const FCoverSlot& Slot = Candidate.Bunker->GetSlot(Candidate.SlotIndex);
    const float Threshold = FMath::Max(100.f, Slot.EntryRadius + 30.f);

    return Dist <= Threshold;
}
