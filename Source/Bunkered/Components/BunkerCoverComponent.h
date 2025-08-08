// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BunkerCoverComponent.generated.h"

class ABunkerBase;

UENUM(BlueprintType)
enum class ECoverState : uint8 { None, Hug, Peek };

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class BUNKERED_API UBunkerCoverComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UBunkerCoverComponent();

	UFUNCTION(BlueprintCallable, Category="Cover")
	bool EnterCoverFromSBSS(int32 DesiredIndex = 0); // 0 = best pick

	UFUNCTION(BlueprintCallable, Category="Cover")
	bool EnterCover(ABunkerBase* Bunker, int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category="Cover")
	void ExitCover();

	UFUNCTION(BlueprintCallable, Category="Cover")
	void SetLeanAxis(float Axis); // [-1..1], negative = left

	UFUNCTION(BlueprintPure, Category="Cover")
	bool IsInCover() const { return State != ECoverState::None; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTick) override;

private:
	// current occupancy
	UPROPERTY()
	TWeakObjectPtr<ABunkerBase> CurrentBunker;

	UPROPERTY()
	int32 CurrentSlot = INDEX_NONE;

	UPROPERTY()
	ECoverState State = ECoverState::None;

	// cached base pose at hug
	FVector BaseLoc = FVector::ZeroVector;
	FRotator BaseRot = FRotator::ZeroRotator;

	// lean state
	float LeanAxis = 0.f;
	float CurrentLean = 0.f;

	// tuning
	UPROPERTY(EditAnywhere, Category="Cover|Tuning")
	float HugBackOffset = 30.f; // cm back from slot normal

	UPROPERTY(EditAnywhere, Category="Cover|Tuning")
	float LeanLateral = 28.f; // cm side max

	UPROPERTY(EditAnywhere, Category="Cover|Tuning")
	float LeanVertical = 8.f; // cm up max

	UPROPERTY(EditAnywhere, Category="Cover|Tuning")
	float LeanSpeed = 10.f; // interp speed

	bool ResolveSlotXf(FTransform& OutXf, FVector& OutNormal) const;
	void ApplyHugPose(const FTransform& SlotXf, const FVector& Normal);
	void ApplyLean(float DeltaTime, const FTransform& SlotXf, const FVector& Normal);
};
