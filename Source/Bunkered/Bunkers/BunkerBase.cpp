// Fill out your copyright notice in the Description page of Project Settings.


#include "Bunkers/BunkerBase.h"
#include "DataAsset/BunkerMetaData.h"
#include "SmartBunkerSelectionSystem/SmartBunkerSelectionSubsystem.h"

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
	if (!Slots.IsValidIndex(Index) || !Slots[Index].SlotPoint) return GetActorTransform();
	return Slots[Index].SlotPoint->GetComponentTransform();
}

FVector ABunkerBase::GetSlotNormal(int32 Index) const
{
	// Use the SlotPoint's forward vector as the "normal" (designer should orient arrows away from bunker)
	if (!Slots.IsValidIndex(Index) || !Slots[Index].SlotPoint) return GetActorForwardVector();
	return Slots[Index].SlotPoint->GetForwardVector();
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
		if (!S.SlotPoint || S.bOccupied) continue;

		const float DistSq = FVector::DistSquared(FromLocation, S.SlotPoint->GetComponentLocation());
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
	if (bDrawDebug) { DrawDebug(); }
}
#endif

void ABunkerBase::DrawDebug() const
{
	if (!bDrawDebug) return;

	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FBunkerCoverSlot& S = Slots[i];
		if (!S.SlotPoint) continue;

		const FVector P = S.SlotPoint->GetComponentLocation();
		const FVector F = S.SlotPoint->GetForwardVector();
		const FVector U = S.SlotPoint->GetUpVector();

		const FColor C = S.bOccupied ? OccupiedColor : FreeColor;

		// Axis
		DrawDebugLine(GetWorld(), P, P + F * 60.f, C, false, DebugDuration, 0, 2.f);
		DrawDebugLine(GetWorld(), P, P + U * 40.f, FColor::Blue, false, DebugDuration, 0, 1.f);

		// Label
		const FString Label = FString::Printf(TEXT("[%d] %s (%s)"),
			i,
			*S.SlotName.ToString(),
			S.bRightSide ? TEXT("Right") : TEXT("Left"));

		DrawDebugString(GetWorld(), P + FVector(0,0,18.f), Label, nullptr, C, DebugDuration, false);
	}
}

