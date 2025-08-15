// Bunkers/BunkerBase.cpp
#include "BunkerBase.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/ArrowComponent.h"
#include "Engine/Engine.h"
#include "Misc/DataValidation.h"

#if WITH_EDITOR
#include "Components/ArrowComponent.h"
#include "ScopedTransaction.h"
#endif

ABunkerBase::ABunkerBase()
{
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;

    Bunker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
    Bunker->SetupAttachment(RootComponent);
    Bunker->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Bunker->SetCanEverAffectNavigation(false);
}

int32 ABunkerBase::FindClosestValidSlot(const FVector& WorldLocation, float MaxDist, int32& OutExactIndex) const
{
    OutExactIndex = INDEX_NONE;

    const float MaxDistSq = FMath::Square(MaxDist);
    float BestSq = MaxDistSq;
    int32 BestIdx = INDEX_NONE;

    for (int32 i = 0; i < Slots.Num(); ++i)
    {
        const FTransform WT = GetSlotWorldTransform(i);
        const float DistSq = FVector::DistSquared(WT.GetLocation(), WorldLocation);
        if (DistSq < BestSq)
        {
            BestSq = DistSq;
            BestIdx = i;
        }
    }

    OutExactIndex = BestIdx;
    return BestIdx;
}

FTransform ABunkerBase::GetSlotWorldTransform(int32 SlotIndex) const
{
    check(Slots.IsValidIndex(SlotIndex));
    const FCoverSlot& Slot = Slots[SlotIndex];

    if (Slot.bUseComponentTransform)
    {
        // Resolve the referenced component on this actor
        if (UActorComponent* AC = Slot.SlotPoint.GetComponent(const_cast<ABunkerBase*>(this)))
        {
            if (USceneComponent* SC = Cast<USceneComponent>(AC))
            {
                return SC->GetComponentTransform();
            }
        }
    }

    // Fallback: local anchor relative to bunker actor
    return Slot.LocalAnchor * GetActorTransform();
}

static void LogEditorNote(const FString& Msg)
{
#if !NO_LOGGING
    UE_LOG(LogTemp, Warning, TEXT("%s"), *Msg);
#endif
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Yellow, Msg);
    }
}

void ABunkerBase::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    RebuildSlotsFromChildren(); // always rebuild from Arrow children
}


void ABunkerBase::TagAllArrowChildrenAsSlots()
{
    TArray<UArrowComponent*> Arrows;
    GetComponents<UArrowComponent>(Arrows);

    int32 Tagged = 0;
    for (UArrowComponent* Arrow : Arrows)
    {
        if (!Arrow) continue;
        if (!SlotTag.IsNone() && !Arrow->ComponentHasTag(SlotTag))
        {
            Arrow->ComponentTags.Add(SlotTag);
            ++Tagged;
        }
    }

    LogEditorNote(FString::Printf(TEXT("[Bunker] Tagged %d Arrow component(s) with %s"), Tagged, *SlotTag.ToString()));
}

#if WITH_EDITOR
void ABunkerBase::RebuildSlotsFromChildren()
{
    UE_LOG(LogTemp, Warning, TEXT("[Bunker] Rebuilding slots from arrow components only."));

    Slots.Empty();

    // Find all ArrowComponents on this actor
    TArray<UArrowComponent*> Arrows;
    GetComponents<UArrowComponent>(Arrows);

    if (Arrows.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Bunker] No ArrowComponents found, cannot create slots."));
        return;
    }

    for (UArrowComponent* Arrow : Arrows)
    {
        if (!Arrow || Arrow == RootComponent) continue;

        FCoverSlot NewSlot;
        NewSlot.SlotName = Arrow->GetFName();

        // Properly set FComponentReference to point to this Arrow
        NewSlot.SlotPoint.OtherActor = this;
        NewSlot.SlotPoint.ComponentProperty = Arrow->GetFName();
        NewSlot.bUseComponentTransform = true;

        NewSlot.LocalAnchor = FTransform::Identity;
        Slots.Add(NewSlot);

        UE_LOG(LogTemp, Warning, TEXT("[Bunker] Added slot from arrow: %s"), *Arrow->GetName());
    }

    UE_LOG(LogTemp, Warning, TEXT("[Bunker] Rebuild complete: %d slots created."), Slots.Num());
}
#endif

#if WITH_EDITOR
EDataValidationResult ABunkerBase::IsDataValid(FDataValidationContext& Context) const
{
    // Don’t hard-fail. A bunker can exist without slots; just warn designers.
    if (Slots.Num() == 0)
    {
        Context.AddWarning(FText::FromString(TEXT(
            "Bunker has no slots. Add Arrow Components or then click 'Rebuild Slots From Children'." )));
    }
    return EDataValidationResult::Valid;
}
#endif