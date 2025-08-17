// Components/BunkerCoverComponent.h
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/UnrealNetwork.h"
#include "Types/CoverTypes.h"
#include "BunkerCoverComponent.generated.h"

class ABunkerBase;
class UCoverSplineComponent;
class ACharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCoverAlphaChanged, float, NewAlpha);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCoverStanceChanged, ECoverStance, NewStance, EExposureState, NewExposure);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCoverPeekChanged, EPeekDirection, NewPeek, bool, bIsPeeking);

UCLASS(ClassGroup=(Cover), meta=(BlueprintSpawnableComponent))
class BUNKERED_API UBunkerCoverComponent : public UActorComponent
{
  GENERATED_BODY()
public:
  UBunkerCoverComponent();

  virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

  // ---- Public API (spline-only) ----
  UFUNCTION(BlueprintCallable, Category="Cover") bool EnterCoverAtAlpha(ABunkerBase* Bunker, float Alpha);
  UFUNCTION(BlueprintCallable, Category="Cover") void ExitCover();
  UFUNCTION(BlueprintCallable, Category="Cover") bool SlideAlongCover(float DeltaAlpha);

  UFUNCTION(BlueprintCallable, Category="Cover") bool SetStance(ECoverStance NewStance);
  UFUNCTION(BlueprintCallable, Category="Cover") bool SetPeek(EPeekDirection Direction, bool bEnable);
  UFUNCTION(BlueprintCallable, Category="Cover") bool SetExposureState(EExposureState NewExposure);

  UFUNCTION(BlueprintPure,   Category="Cover") bool IsInCover() const { return CurrentBunker != nullptr; }
  UFUNCTION(BlueprintPure,   Category="Cover") float GetCoverAlpha() const { return CoverAlpha; }
  UFUNCTION(BlueprintPure,   Category="Cover") ABunkerBase* GetCurrentBunker() const { return CurrentBunker; }
  UFUNCTION(BlueprintPure,   Category="Cover") ECoverStance GetStance() const { return Stance; }
  UFUNCTION(BlueprintPure,   Category="Cover") EExposureState GetExposureState() const { return Exposure; }
  UFUNCTION(BlueprintPure,   Category="Cover") EPeekDirection GetPeek() const { return Peek; }

  // Delegates for UI/Anim
  UPROPERTY(BlueprintAssignable) FCoverAlphaChanged   OnCoverAlphaChanged;
  UPROPERTY(BlueprintAssignable) FCoverStanceChanged  OnStanceChanged;
  UPROPERTY(BlueprintAssignable) FCoverPeekChanged    OnPeekChanged;

  virtual void BeginPlay() override;

  // ---- Server RPCs ----
  UFUNCTION(Server, Reliable) void Server_EnterCoverAtAlpha(ABunkerBase* Bunker, float Alpha);
  UFUNCTION(Server, Reliable) void Server_ExitCover();
  UFUNCTION(Server, Reliable) void Server_SlideAlongCover(float DeltaAlpha);
  UFUNCTION(Server, Reliable) void Server_SetStance(ECoverStance NewStance);
  UFUNCTION(Server, Reliable) void Server_SetPeek(EPeekDirection Direction, bool bEnable);
  UFUNCTION(Server, Reliable) void Server_SetExposure(EExposureState NewExposure);

  // ---- Replicated state ----
  UPROPERTY(ReplicatedUsing=OnRep_Bunker) ABunkerBase*   CurrentBunker = nullptr;
  UPROPERTY(ReplicatedUsing=OnRep_StanceExposure) ECoverStance   Stance   = ECoverStance::Crouch;
  UPROPERTY(ReplicatedUsing=OnRep_StanceExposure) EExposureState Exposure = EExposureState::Hidden;
  UPROPERTY(ReplicatedUsing=OnRep_Peek)           EPeekDirection Peek     = EPeekDirection::None;
  UPROPERTY(ReplicatedUsing=OnRep_CoverAlpha)     float          CoverAlpha = 0.f; // 0..1

  // RepNotifies
  UFUNCTION() void OnRep_Bunker();
  UFUNCTION() void OnRep_CoverAlpha();
  UFUNCTION() void OnRep_StanceExposure();
  UFUNCTION() void OnRep_Peek();

private:
  TWeakObjectPtr<ACharacter> OwnerCharacter;

  void SnapOwnerToCoverAlpha();

  bool IsStanceAllowedAtAlpha(ECoverStance InStance, float Alpha) const;
  bool IsPeekAllowedAtAlpha(EPeekDirection InPeek, float Alpha, float& OutMaxDepthCm) const;
};
