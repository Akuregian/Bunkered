#include "SmartBunkerSelectionSubsystem.h"
#include "Algo/Sort.h"
#include "Bunkers/BunkerBase.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"

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

void USmartBunkerSelectionSubsystem::DebugDrawTopK(APawn* ViewerPawn)
{
	if (!ViewerPawn) return;

	TArray<FBunkerCandidate> Top;
	GetTopKAcrossBunkers(ViewerPawn, DebugTopK, Top);

	for (int32 i = 0; i < Top.Num(); ++i)
	{
		const FBunkerCandidate& C = Top[i];
		if (!IsValid(C.Bunker)) continue;

		const FTransform SlotXf = C.Bunker->GetSlotWorldTransform(C.SlotIndex);
		const FVector P = SlotXf.GetLocation();

		DrawDebugLine(GetWorld(), ViewerPawn->GetActorLocation(), P, FColor::Yellow, false, 0.f, 0, 2.f);
		DrawDebugSphere(GetWorld(), P, 18.f, 12, FColor::Yellow, false, 0.f, 0, 2.f);
		const FString Txt = FString::Printf(TEXT("%s  Top[%d]  S=%.2f  d=%.0f  cosθ=%.2f  slot=%d"),
			*C.Bunker->GetName(), i, C.Score, C.Distance, C.CosForwardToSlot, C.SlotIndex);
		DrawDebugString(GetWorld(), P + FVector(0,0,24.f), Txt, nullptr, FColor::Yellow, 0.f, false);
	}
}

void USmartBunkerSelectionSubsystem::BuildCandidates(const AActor* Viewer, int32 MaxCandidates, TArray<FBunkerCandidate>& OutCandidates) const
{
	OutCandidates.Reset();
	if (!Viewer) return;

	const FVector VLoc = Viewer->GetActorLocation();
	const FVector VFwd = Viewer->GetActorForwardVector();
	const float MaxDistSq = DebugMaxDistance * DebugMaxDistance;
	const int32 Budget = (MaxCandidates > 0) ? MaxCandidates : PrefilterMaxCandidates;

	TArray<FBunkerCandidate> Temp;
	Temp.Reserve(Budget);

	for (const TWeakObjectPtr<ABunkerBase>& WB : Bunkers)
	{
		ABunkerBase* B = WB.Get();
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
			C.Bunker = B;
			C.SlotIndex = i;
			C.Distance = FMath::Sqrt(DistSq);
			const FVector DirTo = (P - VLoc).GetSafeNormal();
			C.CosForwardToSlot = FVector::DotProduct(VFwd, DirTo);
			C.OccupancyPenalty = 0.f; // >0 once SmartObjects/teammate rules apply

			Temp.Add(C);
		}
	}

	// Keep nearest N (cheap prefilter)
	Temp.Sort([](const FBunkerCandidate& A, const FBunkerCandidate& B){ return A.Distance < B.Distance; });
	if (Budget > 0 && Temp.Num() > Budget)
	{
		Temp.SetNum(Budget);
	}

	OutCandidates = MoveTemp(Temp);
}

void USmartBunkerSelectionSubsystem::GetTopKAcrossBunkers(const AActor* Viewer, int32 K, TArray<FBunkerCandidate>& OutTopK) const
{
	TArray<FBunkerCandidate> Cands;
	BuildCandidates(Viewer, PrefilterMaxCandidates, Cands);
	if (Cands.Num() == 0 || K <= 0)
	{
		OutTopK.Reset();
		return;
	}

	for (FBunkerCandidate& C : Cands)
	{
		const float NormDist = FMath::Clamp(C.Distance / FMath::Max(10.f, DebugMaxDistance), 0.f, 1.f);
		const float DistTerm = 1.f - Sigmoid01(NormDist, 8.f, 0.45f);
		const float AngleTerm = (C.CosForwardToSlot * 0.5f) + 0.5f;
		const float OccTerm = 1.f - FMath::Clamp(C.OccupancyPenalty, 0.f, 1.f);
		C.Score = W_Distance * DistTerm + W_Angle * AngleTerm + W_Occupancy * OccTerm;
	}

	// Keep only the best slot per bunker
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

void USmartBunkerSelectionSubsystem::GetTopKWithinBunker(const AActor* Viewer, ABunkerBase* Bunker, int32 K, TArray<FBunkerCandidate>& OutTopK) const
{
	OutTopK.Reset();
	if (!Viewer || !Bunker || K <= 0) return;

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
		C.CosForwardToSlot = FVector::DotProduct(VFwd, (P - VLoc).GetSafeNormal());
		C.OccupancyPenalty = 0.f;
		Cands.Add(C);
	}

	for (FBunkerCandidate& C : Cands)
	{
		const float NormDist = FMath::Clamp(C.Distance / FMath::Max(10.f, DebugMaxDistance), 0.f, 1.f);
		const float DistTerm = 1.f - Sigmoid01(NormDist, 8.f, 0.45f);
		const float AngleTerm = (C.CosForwardToSlot * 0.5f) + 0.5f;
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