// Components/CoverSplineComponent.cpp
#include "Components/CoverSplineComponent.h"
#include "DrawDebugHelpers.h"

static float ToNormalizedT(const USplineComponent* S, float InputKey)
{
  const float Dist = S->GetDistanceAlongSplineAtSplineInputKey(InputKey);
  const float Len  = FMath::Max(S->GetSplineLength(), 1e-3f);
  return FMath::Clamp(Dist / Len, 0.f, 1.f);
}

float UCoverSplineComponent::FindClosestT(const FVector& World) const
{
  const float Key = FindInputKeyClosestToWorldLocation(World);
  return ClampT(ToNormalizedT(this, Key));
}

FTransform UCoverSplineComponent::GetWorldTransformAtT(float T) const
{
  const float Len  = GetSplineLength();
  const float Dist = ClampT(T) * Len;
  return GetTransformAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World, true);
}

FVector UCoverSplineComponent::GetTangentAtT(float T) const
{
  const float Len  = GetSplineLength();
  const float Dist = ClampT(T) * Len;
  return GetTangentAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World).GetSafeNormal();
}

FVector UCoverSplineComponent::GetOutwardAtT(float T) const
{
  const FVector Tg  = GetTangentAtT(T);
  const FVector Up  = FVector::UpVector;
  const FVector Left= Up ^ Tg; // Up × Tangent
  return (bOutwardIsLeft ? Left : -Left).GetSafeNormal();
}

bool UCoverSplineComponent::IsPeekAllowedAt(float T, EPeekDirection Dir, float& OutMaxDepthCm) const
{
  OutMaxDepthCm = 0.f;
  if (Dir == EPeekDirection::None || Dir == EPeekDirection::Over) return false;
  const float x = ClampT(T);
  bool bAllowed = false;
  for (const FPeekRegion& R : PeekRegions)
  {
    if (x >= R.TMin && x <= R.TMax)
    {
      const bool DirOk =
          (R.Allowed == Dir) ||
          (R.Allowed == EPeekDirection::Left  && Dir == EPeekDirection::Left) ||
          (R.Allowed == EPeekDirection::Right && Dir == EPeekDirection::Right);
      if (DirOk) { bAllowed = true; OutMaxDepthCm = FMath::Max(OutMaxDepthCm, R.MaxDepthCm); }
    }
  }
  return bAllowed;
}

ECoverStance UCoverSplineComponent::RequiredStanceAt(float T) const
{
  const float x = ClampT(T);
  for (const FStanceRegion& R : StanceRegions)
  {
    if (x >= R.TMin && x <= R.TMax) return R.Required;
  }
  return ECoverStance::Crouch; // default
}

#if WITH_EDITOR
void UCoverSplineComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
  Super::PostEditChangeProperty(PropertyChangedEvent);
  UpdateSpline();
}

void UCoverSplineComponent::PostEditComponentMove(bool bFinished)
{
  Super::PostEditComponentMove(bFinished);
  UpdateSpline();
}

static float SmoothStep01(float x) { x = FMath::Clamp(x, 0.f, 1.f); return x*x*(3.f - 2.f*x); }

float UCoverSplineComponent::GetEdgeAssistMultiplier(float Alpha, float SlideAxis) const
{
  if (FMath::IsNearlyZero(SlideAxis)) return 1.f;

  const float Band = FMath::Clamp(EdgeAssist.EdgeBandT, 0.0f, 0.25f);
  const float MaxM = FMath::Max(EdgeAssist.MaxBoost, EdgeAssist.MinBoost);
  const float MinM = FMath::Max(1.f, EdgeAssist.MinBoost);

  float bestBoost = 1.f;

  // Look for nearest boundary in slide direction
  // If sliding left, the interesting edges are region TMin; if right, TMax.
  for (const FPeekRegion& R : PeekRegions) // assume you already have PeekRegions on the spline
  {
    const float EdgeT = (SlideAxis < 0.f) ? R.TMin : R.TMax;
    const float d = FMath::Abs(Alpha - EdgeT);
    if (d <= Band)
    {
      // 0 at far edge of band, 1 at the boundary
      const float t = 1.f - (d / Band);
      const float k = SmoothStep01(t);
      const float m = FMath::Lerp(MinM, MaxM, k);
      bestBoost = FMath::Max(bestBoost, m);
    }
  }

  return bestBoost;
}

void UCoverSplineComponent::DebugDrawPeekRegions() const
{

  if (!bDebugDrawPeekRegions) return;

  auto DirColor = [](EPeekDirection Dir)->FColor {
    return (Dir == EPeekDirection::Left)  ? FColor::Red  :
           (Dir == EPeekDirection::Right) ? FColor::Blue :
                                            FColor::Silver;
  };

  for (const FPeekRegion& R : PeekRegions)
  {
    const FColor C = DirColor(R.Allowed);
    const float  T0 = ClampT(R.TMin);
    const float  T1 = ClampT(R.TMax);
    const float  Step = FMath::Clamp(DebugDrawStep, 0.001f, 0.25f);

    for (float t = T0; t <= T1; t += Step)
    {
      const FVector P   = GetWorldTransformAtT(t).GetLocation() + FVector(0,0,DebugDrawHeight);
      const FVector Out = GetOutwardAtT(t);

      // Small hash mark pointing outward, scaled a bit by MaxDepth for visibility
      const float   Mark = FMath::Max(10.f, R.MaxDepthCm * 0.3f);
      DrawDebugLine(GetWorld(), P, P + Out * Mark, C, false, DebugDrawDuration, 0, 3.f);
    }

    // Label the region in the middle
    const float   MidT = 0.5f * (T0 + T1);
    const FVector MidP = GetWorldTransformAtT(MidT).GetLocation() + FVector(0,0,DebugDrawHeight + 6.f);
    DrawDebugString(GetWorld(), MidP,
                    FString::Printf(TEXT("%s  [%.2f..%.2f]  Max%.0fcm"),
                        (R.Allowed==EPeekDirection::Left?TEXT("Left"):TEXT("Right")),
                        T0, T1, R.MaxDepthCm),
                    nullptr, C, DebugDrawDuration, false);
  }
}

void UCoverSplineComponent::CopyFrom(const USplineComponent* Src)
{
  if (!Src || Src == this) return;
  ClearSplinePoints(false);
  const int32 Num = Src->GetNumberOfSplinePoints();
  for (int32 i = 0; i < Num; ++i)
  {
    const FVector  P   = Src->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local);
    const FRotator R   = Src->GetRotationAtSplinePoint(i, ESplineCoordinateSpace::Local);
    const FVector  S   = Src->GetScaleAtSplinePoint(i);
    const auto     Typ = Src->GetSplinePointType(i);
    const FVector  Arr = Src->GetArriveTangentAtSplinePoint(i, ESplineCoordinateSpace::Local);
    const FVector  Lev = Src->GetLeaveTangentAtSplinePoint(i,  ESplineCoordinateSpace::Local);

    AddSplinePoint(P, ESplineCoordinateSpace::Local, false);
    SetRotationAtSplinePoint(i, R, ESplineCoordinateSpace::Local, false);
    SetScaleAtSplinePoint(i, S, false);
    SetSplinePointType(i, Typ, false);
    SetTangentsAtSplinePoint(i, Arr, Lev, ESplineCoordinateSpace::Local, false);
    
    // Optional if you care about custom up-vectors:
    // SetUpVectorAtSplinePoint(i, Src->GetUpVectorAtSplinePoint(i, ESplineCoordinateSpace::Local), ESplineCoordinateSpace::Local, false);
  }
  SetClosedLoop(Src->IsClosedLoop(), false);
  UpdateSpline();
}

#endif
