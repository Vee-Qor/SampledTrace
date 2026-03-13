// Copyright 2026 Vee-Qor. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "SampledTraceTypes.generated.h"

UENUM(BlueprintType)
enum class ESampledTraceMode : uint8
{
    SweepBetweenSamples,
    SamplePoseOnly
};

UENUM(BlueprintType)
enum class ESampledTraceQueryMode : uint8
{
    ObjectTypes,
    Channel
};

USTRUCT()
struct FSampledTraceOwnerTransformSample
{
    GENERATED_BODY()

    UPROPERTY()
    float WorldTime = 0.0f;

    UPROPERTY()
    FTransform MeshTransform = FTransform::Identity;
};

USTRUCT(BlueprintType)
struct FSampledTraceSample
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SampledTrace")
    float Time = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SampledTrace")
    TArray<FVector> Points;
};

USTRUCT(BlueprintType)
struct FSampledTraceSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SampledTrace")
    ESampledTraceMode TraceMode = ESampledTraceMode::SweepBetweenSamples;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SampledTrace")
    ESampledTraceQueryMode QueryMode = ESampledTraceQueryMode::ObjectTypes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SampledTrace",
        meta=(EditCondition="QueryMode == ESampledTraceQueryMode::ObjectTypes", EditConditionHides))
    TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SampledTrace", Category="SampledTrace",
        meta=(EditCondition="QueryMode == ESampledTraceQueryMode::Channel", EditConditionHides))
    TEnumAsByte<ETraceTypeQuery> TraceChannel = UEngineTypes::ConvertToTraceType(ECC_Visibility);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SampledTrace")
    bool bTraceComplex = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SampledTrace")
    bool bIgnoreSelf = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SampledTrace")
    bool bOnlyUniqueActors = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SampledTrace")
    TArray<FName> TraceSockets;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SampledTrace", meta=(ClampMin="0.001"))
    float SampleInterval = 0.01f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SampledTrace", meta=(ClampMin="0.1"))
    float TraceThickness = 12.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SampledTrace",
        meta=(ClampMin="0", EditCondition="TraceMode == ESampledTraceMode::SamplePoseOnly", EditConditionHides))
    int32 PoseInterpolationSteps = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SampledTrace")
    bool bDrawTraceQueries = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SampledTrace")
    bool bDrawCachedSamples = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SampledTrace")
    bool bDrawProcessedSegments = false;

    bool IsValid() const
    {
        const bool bValid =
            TraceSockets.Num() >= 2
            && SampleInterval > KINDA_SMALL_NUMBER;

        if (!bValid) return false;

        for (const FName SocketName : TraceSockets)
        {
            if (SocketName.IsNone()) return false;
        }

        if (QueryMode == ESampledTraceQueryMode::ObjectTypes)
        {
            return !ObjectTypes.IsEmpty();
        }

        return true;
    }
};

class UAnimMontage;

USTRUCT(BlueprintType)
struct FSampledTraceWindowParams
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category="SampledTrace")
    TObjectPtr<UAnimMontage> Montage = nullptr;

    UPROPERTY(BlueprintReadWrite, Category="SampledTrace")
    float WindowStartTime = 0.0f;

    UPROPERTY(BlueprintReadWrite, Category="SampledTrace")
    float WindowEndTime = 0.0f;

    bool IsValid() const
    {
        return
            Montage
            && WindowEndTime > WindowStartTime;
    }
};

USTRUCT()
struct FSampledTraceActiveWindow
{
    GENERATED_BODY()

    UPROPERTY()
    FSampledTraceSettings Settings;

    UPROPERTY()
    FSampledTraceWindowParams Params;

    UPROPERTY()
    TArray<FSampledTraceSample> CachedSamples;

    UPROPERTY()
    TArray<TObjectPtr<AActor>> ActorsToIgnore;

    UPROPERTY()
    TSet<TObjectPtr<AActor>> HitActors;

    float RuntimeStartWorldTime = 0.0f;
    float PrevPlaybackTime = 0.0f;
    float CurrentPlaybackTime = 0.0f;
};