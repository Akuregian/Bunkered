// Bunkers/BunkerBase.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BunkerBase.generated.h"

class UStaticMeshComponent;
class UCoverSplineComponent;

/**
 * Spline-only bunker. Auto-initializes its CoverSpline to have a single
 * “outer” point (to the actor’s +Right side) at a default Z height,
 * and removes the engine’s default center point.
 */
UCLASS(Blueprintable)
class BUNKERED_API ABunkerBase : public AActor
{
    GENERATED_BODY()
public:
    ABunkerBase();

    UFUNCTION(BlueprintPure, Category="Cover")
    UCoverSplineComponent* GetCoverSpline() const { return CoverSpline; }

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    USceneComponent* Root;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    UStaticMeshComponent* Bunker;

    /** Continuous cover path (designer edits this). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cover")
    UCoverSplineComponent* CoverSpline = nullptr;

    /** Height (world Z) offset for newly seeded outer point. */
    UPROPERTY(EditAnywhere, Category="Cover|Defaults", meta=(ClampMin="0.0"))
    float DefaultCoverPointZ = 44.f;

    /** Extra outward margin (uu) beyond bunker bounds when seeding an outer point. */
    UPROPERTY(EditAnywhere, Category="Cover|Defaults", meta=(ClampMin="0.0"))
    float DefaultOutwardMargin = 30.f;

    /** If true, auto-clean & seed the spline in editor builds. */
    UPROPERTY(EditAnywhere, Category="Cover|Defaults")
    bool bAutoInitSplinePoints = true;

#if WITH_EDITOR
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
    void InitializeCoverSplineDefaults(); // cleans center, seeds outer point if needed
#endif
};
