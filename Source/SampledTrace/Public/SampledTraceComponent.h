// Copyright 2026 Vee-Qor. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SampledTraceTypes.h"
#include "Components/ActorComponent.h"
#include "SampledTraceComponent.generated.h"

class UAnimSequence;
class ACharacter;
class USkeletalMeshComponent;
struct FAnimNotifyEvent;

struct FSampledTraceWindowExecutionContext
{
    int32 TraceWindowHandle = INDEX_NONE;
    TArray<FHitResult> HitResults;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSampledTraceWindowEndedSignature, int32, WindowHandle);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FSampledTraceHitsDetectedSignature, int32, WindowHandle, float, PlaybackTime, const TArray<FHitResult>&,
    HitResults);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class SAMPLEDTRACE_API USampledTraceComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USampledTraceComponent();

    UPROPERTY(BlueprintAssignable, Category="SampledTrace")
    FSampledTraceWindowEndedSignature OnTraceWindowEnded;

    UPROPERTY(BlueprintAssignable, Category="SampledTrace")
    FSampledTraceHitsDetectedSignature OnTraceHitsDetected;

    UFUNCTION(BlueprintCallable, Category="SampledTrace")
    int32 BeginTraceWindow(const FSampledTraceSettings& InSettings, const FSampledTraceWindowParams& InParams);

    UFUNCTION(BlueprintCallable, Category="SampledTrace")
    int32 BeginTraceWindowFromMontage(const FSampledTraceSettings& InSettings, UAnimMontage* Montage, float WindowStartTime, float WindowEndTime);

    UFUNCTION(BlueprintCallable, Category="SampledTrace")
    void EndTraceWindow(int32 WindowHandle);

    UFUNCTION(BlueprintCallable, Category="SampledTrace")
    void EndAllTraceWindows();

    UFUNCTION(BlueprintCallable, Category="SampledTrace")
    bool IsTraceWindowActive(int32 WindowHandle) const;

    UFUNCTION(BlueprintCallable, Category="SampledTrace")
    bool IsAnyTraceWindowActive() const;

    int32 BeginTraceWindowForNotify(const FAnimNotifyEvent* NotifyEvent, const FSampledTraceSettings& InSettings, const FSampledTraceWindowParams& InParams);
    void EndTraceWindowForNotify(const FAnimNotifyEvent* NotifyEvent);

private:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    bool EnsureOwnerReferences();
    bool CacheWindowSamples(FSampledTraceActiveWindow& Window, FString& OutError);

    void TickTraceWindow(int32 WindowHandle);
    bool TickActiveTraceWindow(FSampledTraceActiveWindow& Window, int32 WindowHandle);

    void ProcessTraceWindow(const FSampledTraceActiveWindow& Window, FSampledTraceWindowExecutionContext& ExecutionContext, int32 FromSegmentIndex,
        int32 ToSegmentIndex) const;
    void ProcessTraceWindowSegments(const FSampledTraceActiveWindow& Window, FSampledTraceWindowExecutionContext& ExecutionContext, int32 FromSegmentIndex,
        int32 ToSegmentIndex) const;
    void ProcessTraceWindowSamplePoses(const FSampledTraceActiveWindow& Window, FSampledTraceWindowExecutionContext& ExecutionContext, int32 FromSegmentIndex,
        int32 ToSegmentIndex) const;

    void TraceSamplePoseSections(const FSampledTraceActiveWindow& Window, FSampledTraceWindowExecutionContext& ExecutionContext,
        const FSampledTraceSample& Sample) const;
    void TraceInterpolatedSamplePose(const FSampledTraceActiveWindow& Window, FSampledTraceWindowExecutionContext& ExecutionContext,
        const FSampledTraceSample& SampleA, const FSampledTraceSample& SampleB, float Alpha) const;
    void TraceSectionAtPose(const FSampledTraceActiveWindow& Window, FSampledTraceWindowExecutionContext& ExecutionContext, const FVector& SectionStart,
        const FVector& SectionEnd) const;
    void SweepSectionBetweenSamples(const FSampledTraceActiveWindow& Window, FSampledTraceWindowExecutionContext& ExecutionContext,
        const FVector& SectionStartA, const FVector& SectionEndA, const FVector& SectionStartB, const FVector& SectionEndB) const;
    bool PerformBoxTrace(const FSampledTraceActiveWindow& Window, const FVector& Start, const FVector& End, const FVector& HalfSize, const FRotator& Rotation,
        TArray<FHitResult>& OutHits) const;

    void BroadcastTraceHitResults(FSampledTraceActiveWindow& Window, const FSampledTraceWindowExecutionContext& ExecutionContext);
    void FilterAlreadyHitActors(FSampledTraceActiveWindow& Window, const TArray<FHitResult>& InHitResults, TArray<FHitResult>& OutHitResults);

    void DebugDrawCachedSamples(const FSampledTraceActiveWindow& Window) const;
    void DebugDrawProcessedSegments(const FSampledTraceActiveWindow& Window, int32 FromSegmentIndex, int32 ToSegmentIndex) const;

    void RecordOwnerTransformSample();
    FTransform GetClosestRecordedMeshTransform(const FSampledTraceActiveWindow& Window, float PlaybackTime) const;

    bool ResolveMontageTimeToSequence(const UAnimMontage* Montage, float MontageTime, UAnimSequence*& OutSequence, float& OutSequenceTime,
        FString& OutError) const;
    bool EvaluateSocketLocalPositionAtSequenceTime(const UAnimSequence* Sequence, FName SocketName, float SequenceTime, FVector& OutLocalPosition,
        FString& OutError) const;

    UPROPERTY()
    TObjectPtr<ACharacter> OwnerCharacter;

    UPROPERTY()
    TObjectPtr<USkeletalMeshComponent> OwnerMesh;

    UPROPERTY()
    TArray<FSampledTraceOwnerTransformSample> OwnerTransformHistory;

    UPROPERTY()
    TMap<int32, FSampledTraceActiveWindow> ActiveWindows;
    TMap<const FAnimNotifyEvent*, int32> NotifyEventToTraceWindowHandle;

    int32 NextTraceWindowHandle = 0;
};