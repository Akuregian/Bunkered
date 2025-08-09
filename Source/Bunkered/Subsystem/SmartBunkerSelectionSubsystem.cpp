// Fill out your copyright notice in the Description page of Project Settings.


#include "Subsystem/SmartBunkerSelectionSubsystem.h"
#include "Algo/Sort.h"
#include "Bunkers/BunkerBase.h"

void USmartBunkerSelectionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Bunkers.Reserve(64);
}

void USmartBunkerSelectionSubsystem::Deinitialize()
{
	Bunkers.Empty();
	Super::Deinitialize();
}

void USmartBunkerSelectionSubsystem::RegisterBunker(ABunkerBase* Bunker)
{
	if (!Bunker) return;
	Bunkers.AddUnique(TWeakObjectPtr<ABunkerBase>(Bunker));
}

void USmartBunkerSelectionSubsystem::UnregisterBunker(ABunkerBase* Bunker)
{
	if (!Bunker) return;
	Bunkers.Remove(TWeakObjectPtr<ABunkerBase>(Bunker));
}

float USmartBunkerSelectionSubsystem::Sigmoid01(float x, float k, float c)
{
	// maps x in [0,1] through a soft knee around c
	const float t = 1.f / (1.f + FMath::Exp(-k * (x - c)));
	return FMath::Clamp(t, 0.f, 1.f);
}

void USmartBunkerSelectionSubsystem::DebugDrawTopK(APawn* ViewerPawn)
{
	if (!ViewerPawn) return;

	TArray<FBunkerCandidate> Top;
	GetTopK(ViewerPawn, DebugTopK, Top);

	for (int32 i = 0; i < Top.Num(); ++i)
	{
		const FBunkerCandidate& C = Top[i];
		if (!IsValid(C.Bunker)) continue;

		const FTransform SlotXf = C.Bunker->GetSlotWorldTransform(C.SlotIndex);
		const FVector P = SlotXf.GetLocation();

		// Line to viewer
		DrawDebugLine(GetWorld(), ViewerPawn->GetActorLocation(), P, FColor::Yellow, false, 0.f, 0, 2.f);
		// Marker
		DrawDebugSphere(GetWorld(), P, 18.f, 12, FColor::Yellow, false, 0.f, 0, 2.f);
		// Text
		const FString Txt = FString::Printf(TEXT("%s  Top[%d]  S=%.2f  d=%.0f  cosθ=%.2f  slot=%d"),
			*C.Bunker->GetName(), i, C.Score, C.Distance, C.CosLaneAngle, C.SlotIndex);
		DrawDebugString(GetWorld(), P + FVector(0,0,24.f), Txt, nullptr, FColor::Yellow, 0.f, false);
	}
}

void USmartBunkerSelectionSubsystem::BuildCandidateSet(const AActor* Viewer, int32 MaxCandidates, TArray<FBunkerCandidate>& OutCandidates) const
{
	OutCandidates.Reset();
	if (!Viewer) return;

	const FVector VLoc = Viewer->GetActorLocation();
	const FVector VFwd = Viewer->GetActorForwardVector();
	const float MaxDistSq = DebugMaxDistance * DebugMaxDistance;

	TArray<FBunkerCandidate> Temp;
	for (const TWeakObjectPtr<ABunkerBase>& WB : Bunkers)
	{
		const ABunkerBase* B = WB.Get();
		if (!B) continue;

		const int32 SlotCount = B->GetSlotCount();
		for (int32 i = 0; i < SlotCount; ++i)
		{
			if (B->IsSlotOccupied(i)) continue;

			const FTransform T = B->GetSlotWorldTransform(i);
			const FVector P = T.GetLocation();
			const float DistSq = FVector::DistSquared(VLoc, P);
			if (DistSq > MaxDistSq) continue;

			FBunkerCandidate C;
			C.Bunker = const_cast<ABunkerBase*>(B);
			C.SlotIndex = i;
			C.Distance = FMath::Sqrt(DistSq);
			const FVector DirTo = (P - VLoc).GetSafeNormal();
			C.CosLaneAngle = FVector::DotProduct(VFwd, DirTo);
			C.OccupancyPenalty = 0.f; // will be >0 once SmartObjects/teammate rules apply

			Temp.Add(C);
		}
	}

	// Keep at most MaxCandidates nearest (cheap prefilter).
	Temp.Sort([](const FBunkerCandidate& A, const FBunkerCandidate& B){ return A.Distance < B.Distance; });
	if (MaxCandidates > 0 && Temp.Num() > MaxCandidates)
	{
		Temp.SetNum(MaxCandidates);
	}

	OutCandidates = MoveTemp(Temp);
}

void USmartBunkerSelectionSubsystem::GetTopK(const AActor* Viewer, int32 K, TArray<FBunkerCandidate>& OutTopK) const
{
	TArray<FBunkerCandidate> Cands;
	BuildCandidateSet(Viewer, 64, Cands);
	if (Cands.Num() == 0 || K <= 0) { OutTopK.Reset(); return; }

	// --- score (same as you have) ---
	for (FBunkerCandidate& C : Cands)
	{
		const float NormDist = FMath::Clamp(C.Distance / FMath::Max(10.f, DebugMaxDistance), 0.f, 1.f);
		const float DistTerm = 1.f - Sigmoid01(NormDist, 8.f, 0.45f);
		const float AngleTerm = (C.CosLaneAngle * 0.5f) + 0.5f;
		const float OccTerm = 1.f - FMath::Clamp(C.OccupancyPenalty, 0.f, 1.f);
		C.Score = W_Distance * DistTerm + W_Angle * AngleTerm + W_Occupancy * OccTerm;
	}

	// --- keep only the best slot per bunker ---
	TMap<ABunkerBase*, FBunkerCandidate> BestByBunker;
	for (const FBunkerCandidate& C : Cands)
	{
		if (!C.Bunker) continue;
		if (FBunkerCandidate* Existing = BestByBunker.Find(C.Bunker))
		{
			if (C.Score > Existing->Score) { *Existing = C; }
		}
		else
		{
			BestByBunker.Add(C.Bunker, C);
		}
	}

	TArray<FBunkerCandidate> Best;
	BestByBunker.GenerateValueArray(Best);
	Best.Sort([](const FBunkerCandidate& A, const FBunkerCandidate& B){ return A.Score > B.Score; });

	if (K < Best.Num()) Best.SetNum(K, EAllowShrinking::No);
	OutTopK = MoveTemp(Best);
}



void USmartBunkerSelectionSubsystem::GetTopKForBunker(const AActor* Viewer, ABunkerBase* Bunker, int32 K,
	TArray<FBunkerCandidate>& OutTopK) const
{
	OutTopK.Reset();
	if (!Viewer || !Bunker || K <= 0) return;

	// Build candidates only from this bunker (skip occupied slots)
	TArray<FBunkerCandidate> Cands;
	const int32 SlotCount = Bunker->GetSlotCount();
	const FVector VLoc = Viewer->GetActorLocation();
	const FVector VFwd = Viewer->GetActorForwardVector();

	for (int32 i = 0; i < SlotCount; ++i)
	{
		if (Bunker->IsSlotOccupied(i)) continue; // only free slots
		const FTransform T = Bunker->GetSlotWorldTransform(i);
		const FVector P = T.GetLocation();

		FBunkerCandidate C;
		C.Bunker = Bunker;
		C.SlotIndex = i;
		C.Distance = FVector::Distance(VLoc, P);
		C.CosLaneAngle = FVector::DotProduct(VFwd, (P - VLoc).GetSafeNormal());
		C.OccupancyPenalty = 0.f;
		Cands.Add(C);
	}

	// Score same as global path so tuning remains centralized
	for (FBunkerCandidate& C : Cands)
	{
		const float NormDist = FMath::Clamp(C.Distance / FMath::Max(10.f, DebugMaxDistance), 0.f, 1.f);
		const float DistTerm = 1.f - Sigmoid01(NormDist, 8.f, 0.45f);
		const float AngleTerm = (C.CosLaneAngle * 0.5f) + 0.5f;
		const float OccTerm = 1.f - FMath::Clamp(C.OccupancyPenalty, 0.f, 1.f);
		C.Score = W_Distance * DistTerm + W_Angle * AngleTerm + W_Occupancy * OccTerm;
	}

	Cands.Sort([](const FBunkerCandidate& A, const FBunkerCandidate& B){ return A.Score > B.Score; });
	if (Cands.Num() > K) Cands.SetNum(K);
	OutTopK = MoveTemp(Cands);
}

void USmartBunkerSelectionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bDrawDebug || !GetWorld()) return;

	APawn* ViewerPawn = nullptr;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			if (PC->IsLocalController())
			{
				ViewerPawn = PC->GetPawn();
				break;
			}
		}
	}
	DebugDrawTopK(ViewerPawn);
}