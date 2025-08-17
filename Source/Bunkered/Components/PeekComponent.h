// Components/PeekComponent.h
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/CoverTypes.h"
#include "PeekComponent.generated.h"

class USphereComponent;
class UBunkerCoverComponent;

UENUM()
enum class EPeekMode : uint8 { None, Burst, Hold, Toggle };

USTRUCT()
struct FPeekSettings {
  GENERATED_BODY()
  UPROPERTY(EditDefaultsOnly) float MaxOffsetCm = 28.f;
  UPROPERTY(EditDefaultsOnly) float MinBurstOffsetCm = 12.f;
  UPROPERTY(EditDefaultsOnly) float TimeOut = 0.12f;
  UPROPERTY(EditDefaultsOnly) float TimeIn  = 0.10f;
  UPROPERTY(EditDefaultsOnly) float CameraTiltDeg = 6.f;
  UPROPERTY(EditDefaultsOnly) float MarginClearanceCm = 2.5f;
  UPROPERTY(EditDefaultsOnly) float RefractorySec = 0.12f;
  UPROPERTY(EditDefaultsOnly) float DoubleTapWindowSec = 0.25f;
  UPROPERTY(EditDefaultsOnly) float BurstTapMaxSec    = 0.15f;
};

USTRUCT()
struct FPeekNetState {
  GENERATED_BODY()
  UPROPERTY() EPeekDirection Dir = EPeekDirection::None;
  UPROPERTY() EPeekMode      Mode = EPeekMode::None;
  UPROPERTY() uint8          PeekDepthQ = 0; // 0..255
};

USTRUCT()
struct FPeekAnchor {
  GENERATED_BODY()
  UPROPERTY() FVector EdgeRight   = FVector::RightVector; // outward axis
  UPROPERTY() FVector PlaneNormal = FVector::RightVector; // same as outward
  UPROPERTY() float   PlaneD      = 0.f;                  // plane eq: n·x + d = 0
  UPROPERTY() FVector SlotOrigin  = FVector::ZeroVector;  // anchor on spline
};

UCLASS(ClassGroup=(Cover), meta=(BlueprintSpawnableComponent))
class BUNKERED_API UPeekComponent : public UActorComponent
{
  GENERATED_BODY()
public:
  UPeekComponent();

  UPROPERTY(EditDefaultsOnly, Category="Peek") FPeekSettings Settings;
  UPROPERTY(EditDefaultsOnly, Category="Peek|Proxies") float   HeadRadius   = 10.f;
  UPROPERTY(EditDefaultsOnly, Category="Peek|Proxies") FVector HeadLocal    = FVector(0, 10, 65);
  UPROPERTY(EditDefaultsOnly, Category="Peek|Proxies") FVector ShoulderLocal= FVector(0, 15, 50);
  UPROPERTY(EditDefaultsOnly, Category="Peek|Proxies") FVector GunLocal     = FVector(25, 18, 45);

  // Input hooks (PC forwards Q/E)
  void HandlePeekInput(EPeekDirection Dir, bool bPressed);

protected:
  virtual void BeginPlay() override;
  virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

  UFUNCTION(Server, Reliable) void Server_BeginPeek(EPeekDirection Dir, EPeekMode Mode, uint8 ClientDepthHint);
  UFUNCTION(Server, Reliable) void Server_StopPeek();
  UFUNCTION() void OnRep_Net();

  UFUNCTION() void RefreshAnchorFromCoverAlpha(float NewAlpha);

private:
  TWeakObjectPtr<UBunkerCoverComponent> Cover;
  UPROPERTY(ReplicatedUsing=OnRep_Net) FPeekNetState Net;

  // exposure proxies (server collision only)
  UPROPERTY() USphereComponent* HeadHit = nullptr;
  UPROPERTY() USphereComponent* ShoulderHit = nullptr;
  UPROPERTY() USphereComponent* GunHit = nullptr;

  FPeekAnchor Anchor;
  float AlphaLocal = 0.f; // 0..1 depth (not cover alpha)
  double LastRetractTime = -1.0;
  double LastPressL = -10.0, LastPressR = -10.0;
  bool bHoldCandidate = false; double HoldStartTime = 0.0;
  FTimerHandle OutHandle, InHandle;

  void EnsureProxies();
  void UpdateProxies(float DepthAlpha); // cosmetic + server query enable
  float ComputeDepthAlphaMinForClearance() const;
  void StartOut(EPeekMode Mode, float TargetDepthAlpha);
  void StartIn();
  void ApplyCameraTilt();

  bool CanPeekNow() const;
  bool IsDirAllowed(EPeekDirection Dir) const;
};
