// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SmartBunkerSelectionSubsystem.generated.h"

class ABunkerBase;

/**
 * 
 */

USTRUCT(BlueprintType)
struct FBunkerCandidate
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<ABunkerBase> Bunker = nullptr;

	/** Which slot on that bunker. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 SlotIndex = INDEX_NONE;

	/** Straight-line distance (cm) for now. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float Distance = 0.f;

	/** 0 if free, >0 if occupied (temporary; SmartObjects later). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float OccupancyPenalty = 0.f;

	/** Cosine of angle between viewer forward and direction to slot ([-1,1]). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float CosLaneAngle = 0.f;

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

	// Registry API (called by bunkers)
	void RegisterBunker(ABunkerBase* Bunker);
	void UnregisterBunker(ABunkerBase* Bunker);

	// Query API
	/** Build a small candidate set around Viewer (distance + occupancy prefilter + angle). */
	UFUNCTION(BlueprintCallable, Category="SBSS")
	void BuildCandidateSet(const AActor* Viewer, int32 MaxCandidates, TArray<FBunkerCandidate>& OutCandidates) const;

	/** Full Top-K with simple scoring (distance + angle, occupancy penalty). */
	UFUNCTION(BlueprintCallable, Category="SBSS")
	void GetTopK(const AActor* Viewer, int32 K, TArray<FBunkerCandidate>& OutTopK) const;

	// Debug controls (editable in project settings via CDO if you want)
	UPROPERTY(EditAnywhere, Category="SBSS|Debug")
	bool bDrawDebug = true;

	UPROPERTY(EditAnywhere, Category="SBSS|Debug")
	int32 DebugTopK = 3;

	UPROPERTY(EditAnywhere, Category="SBSS|Debug")
	float DebugMaxDistance = 3500.f;

	// Weights
	UPROPERTY(EditAnywhere, Category="SBSS|Scoring")
	float W_Distance = 1.0f;

	UPROPERTY(EditAnywhere, Category="SBSS|Scoring")
	float W_Angle = 0.7f;

	UPROPERTY(EditAnywhere, Category="SBSS|Scoring")
	float W_Occupancy = 0.8f;

private:
	UPROPERTY()
	TArray<TWeakObjectPtr<ABunkerBase>> Bunkers;

	// Helper
	static float Sigmoid01(float x, float k = 8.f, float c = 0.5f);
	void DebugDrawTopK(APawn* ViewerPawn);

};
