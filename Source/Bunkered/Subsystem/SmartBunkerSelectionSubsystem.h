// Copyright (c) YourStudio
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SmartBunkerSelectionSubsystem.generated.h"

class ABunkerBase;

/**
 * Stateless global scorer for bunker slots.
 * - BuildCandidates(): prefilters by distance/occupancy/angle
 * - GetTopKAcrossBunkers(): best single slot per bunker (global)
 * - GetTopKWithinBunker(): best free slots within one bunker
 *
 * Ownership: ABunkerBase owns claims. We never mutate occupancy here.
 */
USTRUCT(BlueprintType)
struct FBunkerCandidateDepricated
{
	GENERATED_BODY()

	/** Owning bunker actor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<ABunkerBase> Bunker = nullptr;

	/** Which slot on that bunker. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 SlotIndex = INDEX_NONE;

	/** Straight-line distance (cm). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float Distance = 0.f;

	/** 0 if free, >0 if occupied (placeholder for future smart-object rules). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float OccupancyPenalty = 0.f;

	/** Cosine of angle between viewer forward and direction to slot ([-1,1]). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float CosForwardToSlot = 0.f;

	/** Final score after weighting. Higher is better. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float Score = 0.f;
};

UCLASS()
class BUNKERED_API USmartBunkerSelectionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// Lifecycle
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Tick
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(USmartBunkerSelectionSubsystem, STATGROUP_Tickables); }

	// Registration (called by bunkers)
	void RegisterBunker(ABunkerBase* Bunker);
	void UnregisterBunker(ABunkerBase* Bunker);

	// Query API
	/** Build a small candidate set around Viewer (distance + occupancy prefilter + angle). */
	UFUNCTION(BlueprintCallable, Category="Cover|Query")
	void BuildCandidates(const AActor* Viewer, int32 MaxCandidates, TArray<FBunkerCandidateDepricated>& OutCandidates) const;

	/** Best single slot per bunker, globally sorted by score. */
	UFUNCTION(BlueprintCallable, Category="Cover|Query")
	void GetTopKAcrossBunkers(const AActor* Viewer, int32 K, TArray<FBunkerCandidateDepricated>& OutTopK) const;

	/** Top K free slots within a specific bunker. */
	UFUNCTION(BlueprintCallable, Category="Cover|Query")
	void GetTopKWithinBunker(const AActor* Viewer, ABunkerBase* Bunker, int32 K, TArray<FBunkerCandidateDepricated>& OutTopK) const;

	// Debug controls (editable on CDO)
	UPROPERTY(EditAnywhere, Category="Cover|Debug")
	bool bDrawDebug = true;

	UPROPERTY(EditAnywhere, Category="Cover|Debug", meta=(ClampMin=1))
	int32 DebugTopK = 3;

	UPROPERTY(EditAnywhere, Category="Cover|Debug")
	float DebugMaxDistance = 3500.f;

	UPROPERTY(EditAnywhere, Category="Cover|Query", meta=(ClampMin=1))
	int32 PrefilterMaxCandidates = 64;

	// Weights
	UPROPERTY(EditAnywhere, Category="Cover|Scoring")
	float W_Distance = 1.0f;

	UPROPERTY(EditAnywhere, Category="Cover|Scoring")
	float W_Angle = 0.7f;

	UPROPERTY(EditAnywhere, Category="Cover|Scoring")
	float W_Occupancy = 0.8f;

private:
	UPROPERTY()
	TArray<TWeakObjectPtr<ABunkerBase>> Bunkers;

	// Helpers
	static FORCEINLINE float Sigmoid01(float x, float k = 8.f, float c = 0.5f)
	{
		const float t = 1.f / (1.f + FMath::Exp(-k * (x - c)));
		return FMath::Clamp(t, 0.f, 1.f);
	}

	void DebugDrawTopK(APawn* ViewerPawn);
};