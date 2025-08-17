// Components/PeekComponent.cpp
#include "Components/PeekComponent.h"
#include "Components/BunkerCoverComponent.h"
#include "Bunkers/BunkerBase.h"
#include "Components/CoverSplineComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/SpringArmComponent.h"
#include "Net/UnrealNetwork.h"

UPeekComponent::UPeekComponent()
{
  PrimaryComponentTick.bCanEverTick = false;
  SetIsReplicatedByDefault(true);
}

void UPeekComponent::BeginPlay()
{
  Super::BeginPlay();
  if (ACharacter* C = Cast<ACharacter>(GetOwner()))
  {
    Cover = C->FindComponentByClass<UBunkerCoverComponent>();
    if (Cover.IsValid())
    {
      Cover->OnCoverAlphaChanged.AddDynamic(this, &UPeekComponent::RefreshAnchorFromCoverAlpha);
      RefreshAnchorFromCoverAlpha(Cover->GetCoverAlpha());
    }
  }
  EnsureProxies();
}

void UPeekComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);
  DOREPLIFETIME(UPeekComponent, Net);
}

void UPeekComponent::EnsureProxies()
{
  if (HeadHit) return;
  auto* Owner = GetOwner();
  auto mk = [&](const FName& N, float R)->USphereComponent*{
    auto* S = NewObject<USphereComponent>(Owner, N);
    S->SetupAttachment(Owner->GetRootComponent());
    S->InitSphereRadius(R);
    S->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    S->RegisterComponent();
    return S;
  };
  HeadHit     = mk(TEXT("HeadHit"),    HeadRadius);
  ShoulderHit = mk(TEXT("ShoulderHit"),HeadRadius * 1.2f);
  GunHit      = mk(TEXT("GunHit"),     HeadRadius * 0.8f);
}

void UPeekComponent::RefreshAnchorFromCoverAlpha(float NewAlpha)
{
  if (!Cover.IsValid() || !Cover->IsInCover()) return;
  if (ABunkerBase* B = Cover->GetCurrentBunker())
  {
    if (UCoverSplineComponent* S = B->GetCoverSpline())
    {
      const FTransform WT = S->GetWorldTransformAtAlpha(NewAlpha);
      Anchor.SlotOrigin   = WT.GetLocation();
      const FVector Out   = S->GetOutwardAtAlpha(NewAlpha);
      Anchor.EdgeRight    = Out;
      Anchor.PlaneNormal  = Out;
      Anchor.PlaneD       = -FVector::DotProduct(Anchor.PlaneNormal, Anchor.SlotOrigin);
    }
  }
}

bool UPeekComponent::CanPeekNow() const
{
  if (!Cover.IsValid() || !Cover->IsInCover()) return false;
  const double Now = GetWorld()->GetTimeSeconds();
  return (Now - LastRetractTime) >= Settings.RefractorySec;
}

bool UPeekComponent::IsDirAllowed(EPeekDirection Dir) const
{
  if (!Cover.IsValid() || !Cover->IsInCover()) return false;
  if (Cover->GetPeek() != EPeekDirection::None) return false;

  float MaxDepth=0.f;
  return Cover->GetCurrentBunker() &&
         Cover->GetCurrentBunker()->GetCoverSpline() &&
         Cover->GetCurrentBunker()->GetCoverSpline()->IsPeekAllowedAtAlpha(Cover->GetCoverAlpha(), Dir, MaxDepth);
}

void UPeekComponent::HandlePeekInput(EPeekDirection Dir, bool bPressed)
{
  if (!Cover.IsValid() || Dir == EPeekDirection::None) return;
  const double Now = GetWorld()->GetTimeSeconds();

  if (bPressed)
  {
    if (!CanPeekNow() || !IsDirAllowed(Dir)) return;

    const double& Last = (Dir==EPeekDirection::Left) ? LastPressL : LastPressR;
    const bool bDouble = (Now - Last) <= 0.25;
    if (Dir==EPeekDirection::Left) LastPressL = Now; else LastPressR = Now;

    const EPeekMode Mode = bDouble ? EPeekMode::Toggle : EPeekMode::Hold;
    bHoldCandidate = !bDouble;
    HoldStartTime  = Now;

    // depth hint (0..255) ~ 40% of max for quick start
    const uint8 HintQ = (uint8)FMath::RoundToInt(255.f * 0.4f);
    Server_BeginPeek(Dir, Mode, HintQ);
  }
  else
  {
    if (Net.Mode == EPeekMode::Toggle) return; // stay out
    const double Held = Now - HoldStartTime;
    if (bHoldCandidate && Held < 0.15) // tap = quick burst
    {
      Server_BeginPeek(Net.Dir, EPeekMode::Burst, (uint8)FMath::RoundToInt(255.f * 0.4f));
    }
    else
    {
      Server_StopPeek();
    }
    bHoldCandidate = false;
  }
}

void UPeekComponent::Server_BeginPeek_Implementation(EPeekDirection Dir, EPeekMode Mode, uint8 ClientDepthHint)
{
  if (!Cover.IsValid() || !Cover->IsInCover()) return;
  if (!Cover->SetPeek(Dir, true)) return;

  Net.Dir  = Dir;
  Net.Mode = Mode;

  const float HintA = ClientDepthHint / 255.f;
  const float Amin  = ComputeDepthAlphaMinForClearance();
  const float A     = FMath::Clamp(FMath::Max(HintA, Amin), 0.f, 1.f);
  Net.PeekDepthQ    = (uint8)FMath::RoundToInt(255.f * A);

  OnRep_Net();
  StartOut(Mode, A);
}

void UPeekComponent::Server_StopPeek_Implementation()
{
  if (!Cover.IsValid()) return;
  Cover->SetPeek(Net.Dir, false);
  Net.Dir = EPeekDirection::None; Net.Mode = EPeekMode::None; Net.PeekDepthQ = 0;
  OnRep_Net();
  LastRetractTime = GetWorld()->GetTimeSeconds();
}

void UPeekComponent::OnRep_Net()
{
  AlphaLocal = Net.PeekDepthQ / 255.f;
  UpdateProxies(AlphaLocal);
  ApplyCameraTilt();
}

float UPeekComponent::ComputeDepthAlphaMinForClearance() const
{
  const FVector n = Anchor.PlaneNormal.GetSafeNormal();
  const float   ndote = FVector::DotProduct(n, Anchor.EdgeRight);
  const FVector M0 = GetOwner()->GetActorTransform().TransformPosition(GunLocal);
  const float δ0 = FVector::DotProduct(n, M0) + Anchor.PlaneD;
  const float num = Settings.MarginClearanceCm - δ0;
  const float den = FMath::Max(Settings.MaxOffsetCm * ndote, 1e-3f);
  return FMath::Clamp(num/den, 0.f, 1.f);
}

void UPeekComponent::StartOut(EPeekMode Mode, float TargetDepthAlpha)
{
  GetWorld()->GetTimerManager().ClearTimer(OutHandle);
  const float T  = Settings.TimeOut;
  const float A0 = AlphaLocal;
  const float A1 = (Mode==EPeekMode::Burst || Mode==EPeekMode::Toggle)
                 ? FMath::Max(TargetDepthAlpha, Settings.MinBurstOffsetCm/Settings.MaxOffsetCm)
                 : FMath::Max(A0,                Settings.MinBurstOffsetCm/Settings.MaxOffsetCm);
  const double Beg = GetWorld()->GetTimeSeconds();

  GetWorld()->GetTimerManager().SetTimer(OutHandle, [this, Beg, T, A0, A1, Mode]()
  {
    const double t = FMath::Clamp((GetWorld()->GetTimeSeconds()-Beg)/T, 0.0, 1.0);
    const float a  = A0 + (A1-A0) * (1 - FMath::Pow(1 - t, 3));
    Net.PeekDepthQ = (uint8)FMath::RoundToInt(255.f * a);
    OnRep_Net();

    if (t >= 1.0)
    {
      GetWorld()->GetTimerManager().ClearTimer(OutHandle);
      if (Net.Mode == EPeekMode::Burst) StartIn();
    }
  }, 0.0f, true);
}

void UPeekComponent::StartIn()
{
  GetWorld()->GetTimerManager().ClearTimer(InHandle);
  const float T  = Settings.TimeIn;
  const float A0 = AlphaLocal;
  const double Beg = GetWorld()->GetTimeSeconds();

  GetWorld()->GetTimerManager().SetTimer(InHandle, [this, Beg, T, A0]()
  {
    const double t = FMath::Clamp((GetWorld()->GetTimeSeconds()-Beg)/T, 0.0, 1.0);
    const float a  = A0 * FMath::Pow(1 - t, 3);
    Net.PeekDepthQ = (uint8)FMath::RoundToInt(255.f * a);
    OnRep_Net();

    if (t >= 1.0)
    {
      GetWorld()->GetTimerManager().ClearTimer(InHandle);
      Server_StopPeek();
    }
  }, 0.0f, true);
}

void UPeekComponent::UpdateProxies(float DepthAlpha)
{
  const FVector Off = Anchor.EdgeRight * (DepthAlpha * Settings.MaxOffsetCm);
  const FTransform T = GetOwner()->GetActorTransform();

  auto place=[&](USphereComponent* S, const FVector& Local){
    if (!S) return;
    S->SetWorldLocation(T.TransformPosition(Local + Off));
    const bool bActive = (Net.Dir != EPeekDirection::None && DepthAlpha > 0.f);
    S->SetCollisionEnabled( (bActive && GetOwner()->HasAuthority()) ? ECollisionEnabled::QueryOnly
                                                                    : ECollisionEnabled::NoCollision );
  };
  place(HeadHit,     HeadLocal);
  place(ShoulderHit, ShoulderLocal);
  place(GunHit,      GunLocal);
}

void UPeekComponent::ApplyCameraTilt()
{
  if (ACharacter* C = Cast<ACharacter>(GetOwner()))
  {
    if (USpringArmComponent* Boom = C->FindComponentByClass<USpringArmComponent>())
    {
      const float Sign = (Net.Dir==EPeekDirection::Left) ? -1.f : (Net.Dir==EPeekDirection::Right ? +1.f : 0.f);
      const float Roll = Sign * Settings.CameraTiltDeg * AlphaLocal;
      FRotator R = Boom->GetRelativeRotation(); R.Roll = Roll; Boom->SetRelativeRotation(R);
      Boom->SocketOffset = FVector(0, Sign * AlphaLocal * Settings.MaxOffsetCm, 0);
    }
  }
}
