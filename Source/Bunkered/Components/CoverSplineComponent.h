// Components/CoverSplineComponent.h
#pragma once
#include "CoreMinimal.h"
#include "Components/SplineComponent.h"
#include "Types/CoverTypes.h" // ECoverStance, EPeekDirection
#include "CoverSplineComponent.generated.h"

USTRUCT(BlueprintType)
struct FPeekRegion
{
  GENERATED_BODY()
  UPROPERTY(EditAnywhere) float TMin = 0.f; // normalized [0..1]
  UPROPERTY(EditAnywhere) float TMax = 1.f;
  UPROPERTY(EditAnywhere) EPeekDirection Allowed = EPeekDirection::None; // Left/Right; None=disallow
  UPROPERTY(EditAnywhere) float MaxDepthCm = 30.f;
};

USTRUCT(BlueprintType)
struct FStanceRegion
{
  GENERATED_BODY()
  UPROPERTY(EditAnywhere) float TMin = 0.f;
  UPROPERTY(EditAnywhere) float TMax = 1.f;
  UPROPERTY(EditAnywhere) ECoverStance Required = ECoverStance::Crouch;
};

UCLASS(ClassGroup=(Cover), meta=(BlueprintSpawnableComponent))
class BUNKERED_API UCoverSplineComponent : public USplineComponent
{
  GENERATED_BODY()
public:
  // Usable sub-range inside the spline [0..1]
  UPROPERTY(EditAnywhere, Category="Cover|Bounds") float Tmin = 0.f;
  UPROPERTY(EditAnywhere, Category="Cover|Bounds") float Tmax = 1.f;

  // Outward = (Up × Tangent) if true, else its negative (flip if needed).
  UPROPERTY(EditAnywhere, Category="Cover|Orientation") bool bOutwardIsLeft = true;

  // Authoring
  UPROPERTY(EditAnywhere, Category="Cover|Authoring") TArray<FPeekRegion>   PeekRegions;
  UPROPERTY(EditAnywhere, Category="Cover|Authoring") TArray<FStanceRegion> StanceRegions;

  // ---- T-based (internal) ----
  UFUNCTION(BlueprintCallable, Category="Cover|Spline") float     FindClosestT(const FVector& World) const;
  UFUNCTION(BlueprintCallable, Category="Cover|Spline") FTransform GetWorldTransformAtT(float T) const;
  UFUNCTION(BlueprintCallable, Category="Cover|Spline") FVector    GetTangentAtT(float T) const;
  UFUNCTION(BlueprintCallable, Category="Cover|Spline") FVector    GetOutwardAtT(float T) const;
  UFUNCTION(BlueprintCallable, Category="Cover|Spline") bool       IsPeekAllowedAt(float T, EPeekDirection Dir, float& OutMaxDepthCm) const;
  UFUNCTION(BlueprintCallable, Category="Cover|Spline") ECoverStance RequiredStanceAt(float T) const;

  FORCEINLINE float ClampT(float T) const { return FMath::Clamp(T, Tmin, Tmax); }

  // ---- Alpha aliases (public-facing) ----
  UFUNCTION(BlueprintCallable, Category="Cover|Spline") float     FindClosestAlpha(const FVector& World) const { return FindClosestT(World); }
  UFUNCTION(BlueprintCallable, Category="Cover|Spline") FTransform GetWorldTransformAtAlpha(float Alpha) const { return GetWorldTransformAtT(Alpha); }
  UFUNCTION(BlueprintCallable, Category="Cover|Spline") FVector    GetTangentAtAlpha(float Alpha) const { return GetTangentAtT(Alpha); }
  UFUNCTION(BlueprintCallable, Category="Cover|Spline") FVector    GetOutwardAtAlpha(float Alpha) const { return GetOutwardAtT(Alpha); }
  UFUNCTION(BlueprintCallable, Category="Cover|Spline") bool       IsPeekAllowedAtAlpha(float Alpha, EPeekDirection Dir, float& OutMaxDepthCm) const { return IsPeekAllowedAt(Alpha, Dir, OutMaxDepthCm); }
  UFUNCTION(BlueprintCallable, Category="Cover|Spline") ECoverStance RequiredStanceAtAlpha(float Alpha) const { return RequiredStanceAt(Alpha); }

  // Refresh spline components when moved/updated
  virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
  virtual void PostEditComponentMove(bool bFinished) override;
};
