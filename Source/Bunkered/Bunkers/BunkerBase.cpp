// Fill out your copyright notice in the Description page of Project Settings.


#include "Bunkers/BunkerBase.h"
#include "DataAsset/BunkerMetaData.h"
#include "Subsystem/SmartBunkerSelectionSubsystem.h"

ABunkerBase::ABunkerBase()
{
	PrimaryActorTick.bCanEverTick = false;

	RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(RootSceneComponent);

	BunkerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	BunkerMesh->SetupAttachment(RootSceneComponent);
	BunkerMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
}

bool ABunkerBase::IsSlotOccupied(int32 Index) const
{
	return Slots.IsValidIndex(Index) ? Slots[Index].bOccupied : true;
}

FTransform ABunkerBase::GetSlotWorldTransform(int32 Index) const
{
	if (!Slots.IsValidIndex(Index)) return GetActorTransform();

	UActorComponent* AC = Slots[Index].SlotPoint.GetComponent(const_cast<ABunkerBase*>(this));
	const USceneComponent* Comp = Cast<USceneComponent>(AC);
	return Comp ? Comp->GetComponentTransform() : GetActorTransform();
}

FVector ABunkerBase::GetSlotNormal(int32 Index) const
{
	if (!Slots.IsValidIndex(Index)) return GetActorForwardVector();

	UActorComponent* AC = Slots[Index].SlotPoint.GetComponent(const_cast<ABunkerBase*>(this));
	const USceneComponent* Comp = Cast<USceneComponent>(AC);
	// Designer orients Arrow X (Forward) outward from cover
	return Comp ? Comp->GetForwardVector() : GetActorForwardVector();
}

bool ABunkerBase::TryClaimSlot(int32 Index, AActor* Claimant)
{
	if (!Slots.IsValidIndex(Index) || Slots[Index].bOccupied) return false;
	Slots[Index].bOccupied = true;
	return true;
}

void ABunkerBase::ReleaseSlot(int32 Index, AActor* /*Claimant*/)
{
	if (!Slots.IsValidIndex(Index)) return;
	Slots[Index].bOccupied = false;
}

int32 ABunkerBase::FindNearestFreeSlot(const FVector& FromLocation, float MaxDist) const
{
	int32 BestIdx = -1;
	float BestDistSq = FMath::Square(MaxDist);

	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FBunkerCoverSlot& S = Slots[i];
		if (S.bOccupied) continue;

		UActorComponent* AC = S.SlotPoint.GetComponent(const_cast<ABunkerBase*>(this));
		const USceneComponent* Comp = Cast<USceneComponent>(AC);
		if (!Comp) continue;

		const float DistSq = FVector::DistSquared(FromLocation, Comp->GetComponentLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestIdx = i;
		}
	}
	return BestIdx;
}

void ABunkerBase::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* W = GetWorld())
	{
		if (auto* SBSS = W->GetSubsystem<USmartBunkerSelectionSubsystem>())
		{
			SBSS->RegisterBunker(this);
		}
	}
}

void ABunkerBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* W = GetWorld())
	{
		if (auto* SBSS = W->GetSubsystem<USmartBunkerSelectionSubsystem>())
		{
			SBSS->UnregisterBunker(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void ABunkerBase::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		UActorComponent* AC = Slots[i].SlotPoint.GetComponent(this); // non-const ok here
		if (!AC)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s: Slot %d has no SlotPoint assigned. Will fall back to bunker origin."),
				*GetName(), i);
		}
	}

	if (bDrawDebug) { DrawDebug(); }}
#endif

void ABunkerBase::DrawDebug() const
{
	if (!bDrawDebug) return;

	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FBunkerCoverSlot& S = Slots[i];

		UActorComponent* AC = S.SlotPoint.GetComponent(const_cast<ABunkerBase*>(this));
		const USceneComponent* Comp = Cast<USceneComponent>(AC);
		if (!Comp) continue;

		const FVector P = Comp->GetComponentLocation();
		const FVector F = Comp->GetForwardVector();
		const FVector U = Comp->GetUpVector();

		const FColor C = S.bOccupied ? OccupiedColor : FreeColor;

		DrawDebugLine(GetWorld(), P, P + F * 60.f, C, false, DebugDuration, 0, 2.f);
		DrawDebugLine(GetWorld(), P, P + U * 40.f, FColor::Blue, false, DebugDuration, 0, 1.f);

		const FString Label = FString::Printf(TEXT("[%d] %s (%s)"),
			i, *S.SlotName.ToString(), S.bRightSide ? TEXT("Right") : TEXT("Left"));
		DrawDebugString(GetWorld(), P + FVector(0,0,18.f), Label, nullptr, C, DebugDuration, false);
	}
}

