// Copyright 2026 Vee-Qor. All Rights Reserved.


#include "SampledTraceComponent.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Kismet/KismetSystemLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogSampledTraceComponent, All, All);

namespace SampledTrace
{
constexpr float NearlyParallelDotThreshold = 0.98f;
constexpr int32 MaxOwnerTransformHistorySamples = 64;
}

USampledTraceComponent::USampledTraceComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}

void USampledTraceComponent::BeginPlay()
{
    Super::BeginPlay();

    EnsureOwnerReferences();
}

void USampledTraceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!IsAnyTraceWindowActive())
    {
        NextTraceWindowHandle = 0;
        SetComponentTickEnabled(false);
        return;
    }

    RecordOwnerTransformSample();

    TArray<int32> WindowHandles;
    ActiveWindows.GetKeys(WindowHandles);

    for (const int32 Handle : WindowHandles)
    {
        TickTraceWindow(Handle);
    }
}

int32 USampledTraceComponent::BeginTraceWindow(const FSampledTraceSettings& InSettings, const FSampledTraceWindowParams& InParams)
{
    const UWorld* World = GetWorld();
    if (!World) return INDEX_NONE;

    if (!EnsureOwnerReferences())
    {
        UE_LOG(LogSampledTraceComponent, Warning, TEXT("OwnerMesh is null."));
        return INDEX_NONE;
    }

    if (!InSettings.IsValid())
    {
        UE_LOG(LogSampledTraceComponent, Warning, TEXT("Trace window settings is invalid."));
        return INDEX_NONE;
    }

    if (!InParams.IsValid())
    {
        UE_LOG(LogSampledTraceComponent, Warning, TEXT("Trace window params is invalid."));
        return INDEX_NONE;
    }

    const int32 TraceWindowHandle = NextTraceWindowHandle++;

    FSampledTraceActiveWindow& TraceWindow = ActiveWindows.Add(TraceWindowHandle);
    TraceWindow.Settings = InSettings;
    TraceWindow.Params = InParams;
    TraceWindow.PrevPlaybackTime = InParams.WindowStartTime;
    TraceWindow.CurrentPlaybackTime = InParams.WindowStartTime;
    TraceWindow.RuntimeStartWorldTime = World->GetTimeSeconds();

    if (InSettings.bIgnoreSelf)
    {
        TraceWindow.ActorsToIgnore.Add(OwnerCharacter);
    }

    FString ErrorMessage;
    if (!CacheWindowSamples(TraceWindow, ErrorMessage))
    {
        UE_LOG(LogSampledTraceComponent, Warning, TEXT("CacheWindowSamples failed: %s"), *ErrorMessage);
        ActiveWindows.Remove(TraceWindowHandle);
        return INDEX_NONE;
    }

    RecordOwnerTransformSample();
    SetComponentTickEnabled(true);

    UE_LOG(LogSampledTraceComponent, Log, TEXT("Trace window started. Handle=%d Samples=%d"), TraceWindowHandle, TraceWindow.CachedSamples.Num());

    return TraceWindowHandle;
}

int32 USampledTraceComponent::BeginTraceWindowFromMontage(const FSampledTraceSettings& InSettings, UAnimMontage* Montage, float WindowStartTime,
    float WindowEndTime)
{
    FSampledTraceWindowParams Params;
    Params.Montage = Montage;
    Params.WindowStartTime = WindowStartTime;
    Params.WindowEndTime = WindowEndTime;

    return BeginTraceWindow(InSettings, Params);
}

void USampledTraceComponent::EndTraceWindow(int32 Handle)
{
    const int32 RemovedCount = ActiveWindows.Remove(Handle);
    if (RemovedCount == 0) return;

    const FAnimNotifyEvent* NotifyEventToRemove = nullptr;

    for (const TPair<const FAnimNotifyEvent*, int32>& Pair : NotifyEventToTraceWindowHandle)
    {
        if (Pair.Value == Handle)
        {
            NotifyEventToRemove = Pair.Key;
            break;
        }
    }

    if (NotifyEventToRemove)
    {
        NotifyEventToTraceWindowHandle.Remove(NotifyEventToRemove);
    }

    OnTraceWindowEnded.Broadcast(Handle);

    if (ActiveWindows.IsEmpty())
    {
        NextTraceWindowHandle = 0;
        OwnerTransformHistory.Reset();
        SetComponentTickEnabled(false);
    }
}

void USampledTraceComponent::EndAllTraceWindows()
{
    if (ActiveWindows.IsEmpty()) return;

    TArray<int32> WindowHandles;
    ActiveWindows.GetKeys(WindowHandles);

    for (const int32 Handle : WindowHandles)
    {
        OnTraceWindowEnded.Broadcast(Handle);
    }

    NextTraceWindowHandle = 0;
    ActiveWindows.Reset();
    NotifyEventToTraceWindowHandle.Reset();
    OwnerTransformHistory.Reset();
    SetComponentTickEnabled(false);
}

bool USampledTraceComponent::IsTraceWindowActive(int32 WindowHandle) const
{
    return ActiveWindows.Contains(WindowHandle);
}

int32 USampledTraceComponent::BeginTraceWindowForNotify(const FAnimNotifyEvent* NotifyEvent, const FSampledTraceSettings& InSettings,
    const FSampledTraceWindowParams& InParams)
{
    if (!NotifyEvent) return INDEX_NONE;

    const int32 TraceWindowHandle = BeginTraceWindow(InSettings, InParams);
    if (TraceWindowHandle == INDEX_NONE) return INDEX_NONE;

    NotifyEventToTraceWindowHandle.FindOrAdd(NotifyEvent) = TraceWindowHandle;
    return TraceWindowHandle;
}

void USampledTraceComponent::EndTraceWindowForNotify(const FAnimNotifyEvent* NotifyEvent)
{
    if (!NotifyEvent) return;

    const int32* FoundHandle = NotifyEventToTraceWindowHandle.Find(NotifyEvent);
    if (!FoundHandle) return;

    EndTraceWindow(*FoundHandle);
}

bool USampledTraceComponent::IsAnyTraceWindowActive() const
{
    return !ActiveWindows.IsEmpty();
}

bool USampledTraceComponent::CacheWindowSamples(FSampledTraceActiveWindow& Window, FString& OutError)
{
    const FSampledTraceSettings& Settings = Window.Settings;
    const FSampledTraceWindowParams& Params = Window.Params;

    Window.CachedSamples.Reset();

    for (float MontageTime = Params.WindowStartTime; MontageTime < Params.WindowEndTime + KINDA_SMALL_NUMBER; MontageTime += Settings.SampleInterval)
    {
        UAnimSequence* Sequence = nullptr;
        float SequenceTime = 0.0f;

        if (!ResolveMontageTimeToSequence(Params.Montage, MontageTime, Sequence, SequenceTime, OutError))
        {
            return false;
        }

        FSampledTraceSample Sample;
        Sample.Time = MontageTime;
        Sample.Points.Reset();
        Sample.Points.Reserve(Settings.TraceSockets.Num());

        for (const FName SocketName : Settings.TraceSockets)
        {
            FVector LocalPoint = FVector::ZeroVector;
            if (!EvaluateSocketLocalPositionAtSequenceTime(Sequence, SocketName, SequenceTime, LocalPoint, OutError))
            {
                return false;
            }

            Sample.Points.Add(LocalPoint);
        }

        Window.CachedSamples.Add(MoveTemp(Sample));
    }

    if (Window.CachedSamples.IsEmpty())
    {
        OutError = "No cached samples were generated";
        return false;
    }

    return true;
}

void USampledTraceComponent::DebugDrawCachedSamples(const FSampledTraceActiveWindow& Window) const
{
    const UWorld* World = GetWorld();
    if (!World || Window.CachedSamples.IsEmpty()) return;

    TArray<FVector> PrevWorldPoints;
    PrevWorldPoints.Reserve(Window.Settings.TraceSockets.Num());

    for (const FSampledTraceSample& Sample : Window.CachedSamples)
    {
        const FTransform MeshTransform = GetClosestRecordedMeshTransform(Window, Sample.Time);

        TArray<FVector> CurrentWorldPoints;
        CurrentWorldPoints.Reserve(Sample.Points.Num());

        for (const FVector& LocalPoint : Sample.Points)
        {
            const FVector WorldPoint = MeshTransform.TransformPosition(LocalPoint);
            CurrentWorldPoints.Add(WorldPoint);

            DrawDebugSphere(World, WorldPoint, 4.0f, 8, FColor::Red, false, 0.0f, 0, 1.0f);
        }

        for (int32 PointIndex = 0; PointIndex + 1 < CurrentWorldPoints.Num(); ++PointIndex)
        {
            DrawDebugLine(World, CurrentWorldPoints[PointIndex], CurrentWorldPoints[PointIndex + 1], FColor::White, false, 0.0f, 0, 1.0f);
        }

        if (PrevWorldPoints.Num() == CurrentWorldPoints.Num())
        {
            for (int32 PointIndex = 0; PointIndex < CurrentWorldPoints.Num(); ++PointIndex)
            {
                DrawDebugLine(World, PrevWorldPoints[PointIndex], CurrentWorldPoints[PointIndex], FColor::White, false, 0.0f, 0, 1.0f);
            }
        }

        PrevWorldPoints = MoveTemp(CurrentWorldPoints);
    }
}

void USampledTraceComponent::DebugDrawProcessedSegments(const FSampledTraceActiveWindow& Window, int32 FromSegmentIndex, int32 ToSegmentIndex) const
{
    const UWorld* World = GetWorld();
    if (!World || Window.CachedSamples.Num() < 2) return;

    for (int32 SegmentIndex = FromSegmentIndex; SegmentIndex <= ToSegmentIndex; ++SegmentIndex)
    {
        if (!Window.CachedSamples.IsValidIndex(SegmentIndex) || !Window.CachedSamples.IsValidIndex(SegmentIndex + 1)) continue;

        const FSampledTraceSample& SampleA = Window.CachedSamples[SegmentIndex];
        const FSampledTraceSample& SampleB = Window.CachedSamples[SegmentIndex + 1];

        const FTransform MeshTransformA = GetClosestRecordedMeshTransform(Window, SampleA.Time);
        const FTransform MeshTransformB = GetClosestRecordedMeshTransform(Window, SampleB.Time);

        const int32 PointCount = FMath::Min(SampleA.Points.Num(), SampleB.Points.Num());
        if (PointCount < 2) continue;

        TArray<FVector> WorldPointsA;
        WorldPointsA.Reserve(PointCount);

        TArray<FVector> WorldPointsB;
        WorldPointsB.Reserve(PointCount);

        for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
        {
            WorldPointsA.Add(MeshTransformA.TransformPosition(SampleA.Points[PointIndex]));
            WorldPointsB.Add(MeshTransformB.TransformPosition(SampleB.Points[PointIndex]));
        }

        for (int32 PointIndex = 0; PointIndex + 1 < PointCount; ++PointIndex)
        {
            DrawDebugLine(World, WorldPointsA[PointIndex], WorldPointsA[PointIndex + 1], FColor::White, false, 1.0f, 0, 1.5f);
            DrawDebugLine(World, WorldPointsB[PointIndex], WorldPointsB[PointIndex + 1], FColor::Green, false, 1.0f, 0, 1.5f);
        }

        for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
        {
            DrawDebugLine(World, WorldPointsA[PointIndex], WorldPointsB[PointIndex], FColor::Blue, false, 1.0f, 0, 3.0f);
        }
    }
}

void USampledTraceComponent::RecordOwnerTransformSample()
{
    const UWorld* World = GetWorld();
    if (!World || !OwnerMesh) return;

    FSampledTraceOwnerTransformSample Sample;
    Sample.WorldTime = World->GetTimeSeconds();
    Sample.MeshTransform = OwnerMesh->GetComponentTransform();

    OwnerTransformHistory.Add(MoveTemp(Sample));

    if (OwnerTransformHistory.Num() > SampledTrace::MaxOwnerTransformHistorySamples)
    {
        OwnerTransformHistory.RemoveAt(0, 1, EAllowShrinking::No);
    }
}

FTransform USampledTraceComponent::GetClosestRecordedMeshTransform(const FSampledTraceActiveWindow& Window, float PlaybackTime) const
{
    if (!OwnerMesh || OwnerTransformHistory.IsEmpty())
    {
        return OwnerMesh ? OwnerMesh->GetComponentTransform() : FTransform::Identity;
    }

    const float TargetWorldTime = Window.RuntimeStartWorldTime + (PlaybackTime - Window.Params.WindowStartTime);

    const FSampledTraceOwnerTransformSample* BestSample = nullptr;
    float BestAbsDelta = TNumericLimits<float>::Max();

    for (const FSampledTraceOwnerTransformSample& HistorySample : OwnerTransformHistory)
    {
        const float AbsDelta = FMath::Abs(HistorySample.WorldTime - TargetWorldTime);
        if (AbsDelta < BestAbsDelta)
        {
            BestAbsDelta = AbsDelta;
            BestSample = &HistorySample;
        }
    }

    return BestSample ? BestSample->MeshTransform : OwnerMesh->GetComponentTransform();
}

void USampledTraceComponent::TickTraceWindow(int32 WindowHandle)
{
    FSampledTraceActiveWindow* TraceWindow = ActiveWindows.Find(WindowHandle);
    if (!TraceWindow) return;

    if (!TickActiveTraceWindow(*TraceWindow, WindowHandle)) return;

    if (TraceWindow->Settings.bDrawCachedSamples)
    {
        DebugDrawCachedSamples(*TraceWindow);
    }
}

bool USampledTraceComponent::TickActiveTraceWindow(FSampledTraceActiveWindow& Window, int32 WindowHandle)
{
    const UWorld* World = GetWorld();
    if (!World) return false;

    if (Window.CachedSamples.Num() < 2)
    {
        EndTraceWindow(WindowHandle);
        return false;
    }

    const FSampledTraceSettings& Settings = Window.Settings;
    const FSampledTraceWindowParams& Params = Window.Params;

    const float ElapsedWorldTime = World->GetTimeSeconds() - Window.RuntimeStartWorldTime;

    Window.PrevPlaybackTime = Window.CurrentPlaybackTime;
    Window.CurrentPlaybackTime = Params.WindowStartTime + ElapsedWorldTime;

    const float ClampedPrevTime = FMath::Clamp(Window.PrevPlaybackTime, Params.WindowStartTime, Params.WindowEndTime);
    const float ClampedCurrTime = FMath::Clamp(Window.CurrentPlaybackTime, Params.WindowStartTime, Params.WindowEndTime);

    const int32 PrevSegmentIndex = FMath::Clamp(FMath::FloorToInt((ClampedPrevTime - Params.WindowStartTime) / Settings.SampleInterval), 0.0f,
        Window.CachedSamples.Num() - 2);
    const int32 CurrSegmentIndex = FMath::Clamp(FMath::FloorToInt((ClampedCurrTime - Params.WindowStartTime) / Settings.SampleInterval), 0.0f,
        Window.CachedSamples.Num() - 2);

    FSampledTraceWindowExecutionContext ExecutionContext;
    ExecutionContext.TraceWindowHandle = WindowHandle;

    ProcessTraceWindow(Window, ExecutionContext, PrevSegmentIndex, CurrSegmentIndex);
    BroadcastTraceHitResults(Window, ExecutionContext);

    if (Settings.bDrawProcessedSegments)
    {
        DebugDrawProcessedSegments(Window, PrevSegmentIndex, CurrSegmentIndex);
    }

    if (Window.CurrentPlaybackTime >= Params.WindowEndTime)
    {
        EndTraceWindow(WindowHandle);
        return false;
    }

    return true;
}

void USampledTraceComponent::ProcessTraceWindow(const FSampledTraceActiveWindow& Window, FSampledTraceWindowExecutionContext& ExecutionContext,
    int32 FromSegmentIndex, int32 ToSegmentIndex) const
{
    switch (Window.Settings.TraceMode)
    {
        case ESampledTraceMode::SweepBetweenSamples: ProcessTraceWindowSegments(Window, ExecutionContext, FromSegmentIndex, ToSegmentIndex);
            break;

        case ESampledTraceMode::SamplePoseOnly: ProcessTraceWindowSamplePoses(Window, ExecutionContext, FromSegmentIndex, ToSegmentIndex);
            break;

        default: ProcessTraceWindowSegments(Window, ExecutionContext, FromSegmentIndex, ToSegmentIndex);
            break;
    }
}

void USampledTraceComponent::ProcessTraceWindowSegments(const FSampledTraceActiveWindow& Window, FSampledTraceWindowExecutionContext& ExecutionContext,
    int32 FromSegmentIndex, int32 ToSegmentIndex) const
{
    if (Window.CachedSamples.Num() < 2) return;

    for (int32 SegmentIndex = FromSegmentIndex; SegmentIndex <= ToSegmentIndex; ++SegmentIndex)
    {
        if (!Window.CachedSamples.IsValidIndex(SegmentIndex) || !Window.CachedSamples.IsValidIndex(SegmentIndex + 1)) continue;

        const FSampledTraceSample& SampleA = Window.CachedSamples[SegmentIndex];
        const FSampledTraceSample& SampleB = Window.CachedSamples[SegmentIndex + 1];

        const FTransform MeshTransformA = GetClosestRecordedMeshTransform(Window, SampleA.Time);
        const FTransform MeshTransformB = GetClosestRecordedMeshTransform(Window, SampleB.Time);

        const int32 PointCount = FMath::Min(SampleA.Points.Num(), SampleB.Points.Num());
        if (PointCount < 2) continue;

        for (int32 PointIndex = 0; PointIndex + 1 < PointCount; ++PointIndex)
        {
            const FVector SectionStartA = MeshTransformA.TransformPosition(SampleA.Points[PointIndex]);
            const FVector SectionEndA = MeshTransformA.TransformPosition(SampleA.Points[PointIndex + 1]);

            const FVector SectionStartB = MeshTransformB.TransformPosition(SampleB.Points[PointIndex]);
            const FVector SectionEndB = MeshTransformB.TransformPosition(SampleB.Points[PointIndex + 1]);

            SweepSectionBetweenSamples(Window, ExecutionContext, SectionStartA, SectionEndA, SectionStartB, SectionEndB);
        }
    }
}

void USampledTraceComponent::ProcessTraceWindowSamplePoses(const FSampledTraceActiveWindow& Window, FSampledTraceWindowExecutionContext& ExecutionContext,
    int32 FromSegmentIndex, int32 ToSegmentIndex) const
{
    if (Window.CachedSamples.IsEmpty()) return;

    const int32 PoseInterpolationSteps = FMath::Max(0, Window.Settings.PoseInterpolationSteps);

    if (PoseInterpolationSteps == 0)
    {
        for (int32 SampleIndex = FromSegmentIndex; SampleIndex <= ToSegmentIndex + 1; ++SampleIndex)
        {
            if (!Window.CachedSamples.IsValidIndex(SampleIndex)) continue;

            TraceSamplePoseSections(Window, ExecutionContext, Window.CachedSamples[SampleIndex]);
        }

        return;
    }

    for (int32 SegmentIndex = FromSegmentIndex; SegmentIndex <= ToSegmentIndex; ++SegmentIndex)
    {
        if (!Window.CachedSamples.IsValidIndex(SegmentIndex) || !Window.CachedSamples.IsValidIndex(SegmentIndex + 1)) continue;

        const FSampledTraceSample& SampleA = Window.CachedSamples[SegmentIndex];
        const FSampledTraceSample& SampleB = Window.CachedSamples[SegmentIndex + 1];

        const int32 StepCount = PoseInterpolationSteps + 2;

        for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
        {
            if (SegmentIndex > FromSegmentIndex && StepIndex == 0) continue;

            const float Alpha = static_cast<float>(StepIndex) / static_cast<float>(StepCount - 1);
            TraceInterpolatedSamplePose(Window, ExecutionContext, SampleA, SampleB, Alpha);
        }
    }
}

void USampledTraceComponent::TraceSamplePoseSections(const FSampledTraceActiveWindow& Window, FSampledTraceWindowExecutionContext& ExecutionContext,
    const FSampledTraceSample& Sample) const
{
    if (Sample.Points.Num() < 2) return;

    const FTransform MeshTransform = GetClosestRecordedMeshTransform(Window, Sample.Time);

    TArray<FVector> WorldPoints;
    WorldPoints.Reserve(Sample.Points.Num());

    for (const FVector LocalPoint : Sample.Points)
    {
        WorldPoints.Add(MeshTransform.TransformPosition(LocalPoint));
    }

    for (int32 PointIndex = 0; PointIndex + 1 < WorldPoints.Num(); ++PointIndex)
    {
        TraceSectionAtPose(Window, ExecutionContext, WorldPoints[PointIndex], WorldPoints[PointIndex + 1]);
    }
}

void USampledTraceComponent::TraceSectionAtPose(const FSampledTraceActiveWindow& Window, FSampledTraceWindowExecutionContext& ExecutionContext,
    const FVector& SectionStart, const FVector& SectionEnd) const
{
    const FVector Center = (SectionStart + SectionEnd) * 0.5f;
    const FVector SectionVector = SectionEnd - SectionStart;
    const float SectionLength = SectionVector.Length();

    FVector SectionAxis = FVector::ForwardVector;
    if (SectionLength > KINDA_SMALL_NUMBER)
    {
        SectionAxis = SectionVector.GetSafeNormal();
    }

    FVector UpAxis = FVector::UpVector;
    if (FMath::Abs(FVector::DotProduct(SectionAxis, UpAxis)) > SampledTrace::NearlyParallelDotThreshold)
    {
        UpAxis = FVector::RightVector;
    }

    const FVector BoxUp = FVector::CrossProduct(SectionAxis, FVector::CrossProduct(UpAxis, SectionAxis)).GetSafeNormal();
    const FRotator BoxRotation = FRotationMatrix::MakeFromXZ(SectionAxis, BoxUp).Rotator();

    const FVector HalfSize(
        FMath::Max(SectionLength * 0.5f, 1.0f),
        FMath::Max(Window.Settings.TraceThickness * 0.5f, 1.0f),
        FMath::Max(Window.Settings.TraceThickness * 0.5f, 1.0f));

    TArray<FHitResult> HitResults;
    if (PerformBoxTrace(Window, Center, Center, HalfSize, BoxRotation, HitResults))
    {
        ExecutionContext.HitResults.Append(HitResults);
    }
}

void USampledTraceComponent::TraceInterpolatedSamplePose(const FSampledTraceActiveWindow& Window, FSampledTraceWindowExecutionContext& ExecutionContext,
    const FSampledTraceSample& SampleA, const FSampledTraceSample& SampleB, float Alpha) const
{
    const int32 PointCount = FMath::Min(SampleA.Points.Num(), SampleB.Points.Num());
    if (PointCount < 2) return;

    const float InterpolatedTime = FMath::Lerp(SampleA.Time, SampleB.Time, Alpha);
    const FTransform MeshTransform = GetClosestRecordedMeshTransform(Window, InterpolatedTime);

    TArray<FVector> WorldPoints;
    WorldPoints.Reserve(PointCount);

    for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
    {
        const FVector InterpolatedLocalPoint = FMath::Lerp(SampleA.Points[PointIndex], SampleB.Points[PointIndex], Alpha);
        WorldPoints.Add(MeshTransform.TransformPosition(InterpolatedLocalPoint));
    }

    for (int32 PointIndex = 0; PointIndex + 1 < WorldPoints.Num(); ++PointIndex)
    {
        TraceSectionAtPose(Window, ExecutionContext, WorldPoints[PointIndex], WorldPoints[PointIndex + 1]);
    }
}

void USampledTraceComponent::SweepSectionBetweenSamples(const FSampledTraceActiveWindow& Window, FSampledTraceWindowExecutionContext& ExecutionContext,
    const FVector& SectionStartA, const FVector& SectionEndA, const FVector& SectionStartB, const FVector& SectionEndB) const
{
    const FVector CenterA = (SectionStartA + SectionEndA) * 0.5f;
    const FVector CenterB = (SectionStartB + SectionEndB) * 0.5f;

    const FVector SectionVectorA = SectionEndA - SectionStartA;
    const FVector SectionVectorB = SectionEndB - SectionStartB;

    const float SectionLengthA = SectionVectorA.Length();
    const float SectionLengthB = SectionVectorB.Length();
    const float SectionLength = FMath::Max(SectionLengthA, SectionLengthB);

    const FVector SweepVector = CenterB - CenterA;
    const float SweepLength = SweepVector.Length();

    FVector SectionAxis = FVector::ForwardVector;
    if (SectionLengthA > KINDA_SMALL_NUMBER)
    {
        SectionAxis = SectionVectorA.GetSafeNormal();
    }
    else if (SectionLengthB > KINDA_SMALL_NUMBER)
    {
        SectionAxis = SectionVectorB.GetSafeNormal();
    }

    FVector SweepAxis = SweepLength > KINDA_SMALL_NUMBER ? SweepVector.GetSafeNormal() : FVector::UpVector;

    if (FMath::Abs(FVector::DotProduct(SectionAxis, SweepAxis)) > SampledTrace::NearlyParallelDotThreshold)
    {
        SweepAxis = FVector::UpVector;

        if (FMath::Abs(FVector::DotProduct(SectionAxis, SweepAxis)) > SampledTrace::NearlyParallelDotThreshold)
        {
            SweepAxis = FVector::RightVector;
        }
    }

    FVector BoxUp = FVector::CrossProduct(SectionAxis, SweepAxis).GetSafeNormal();
    if (BoxUp.IsNearlyZero())
    {
        BoxUp = FVector::UpVector;
    }
    const FRotator BoxRotation = FRotationMatrix::MakeFromXZ(SectionAxis, BoxUp).Rotator();

    const FVector HalfSize(
        FMath::Max(SectionLength * 0.5f, 1.0f),
        FMath::Max(Window.Settings.TraceThickness * 0.5f, 1.0f),
        FMath::Max(Window.Settings.TraceThickness * 0.5f, 1.0f));

    TArray<FHitResult> HitResults;
    if (PerformBoxTrace(Window, CenterA, CenterB, HalfSize, BoxRotation, HitResults))
    {
        ExecutionContext.HitResults.Append(HitResults);
    }
}

bool USampledTraceComponent::PerformBoxTrace(const FSampledTraceActiveWindow& Window, const FVector& Start, const FVector& End, const FVector& HalfSize,
    const FRotator& Rotation, TArray<FHitResult>& OutHits) const
{
    const UWorld* World = GetWorld();
    if (!World) return false;

    const EDrawDebugTrace::Type DrawDebugTrace = Window.Settings.bDrawTraceQueries ? EDrawDebugTrace::ForDuration : EDrawDebugTrace::None;

    switch (Window.Settings.QueryMode)
    {
        case ESampledTraceQueryMode::ObjectTypes: return UKismetSystemLibrary::BoxTraceMultiForObjects(
                World,
                Start,
                End,
                HalfSize,
                Rotation,
                Window.Settings.ObjectTypes,
                Window.Settings.bTraceComplex,
                Window.ActorsToIgnore,
                DrawDebugTrace,
                OutHits,
                false,
                FLinearColor::Red,
                FLinearColor::Green,
                1.0f);

        case ESampledTraceQueryMode::Channel: return UKismetSystemLibrary::BoxTraceMulti(
                World,
                Start,
                End,
                HalfSize,
                Rotation,
                Window.Settings.TraceChannel,
                Window.Settings.bTraceComplex,
                Window.ActorsToIgnore,
                DrawDebugTrace,
                OutHits,
                false,
                FLinearColor::Red,
                FLinearColor::Green,
                1.0f);

        default: return false;
    }
}

void USampledTraceComponent::BroadcastTraceHitResults(FSampledTraceActiveWindow& Window, const FSampledTraceWindowExecutionContext& ExecutionContext)
{
    if (ExecutionContext.HitResults.IsEmpty()) return;

    TArray<FHitResult> ResultsToBroadcast = ExecutionContext.HitResults;

    if (Window.Settings.bOnlyUniqueActors)
    {
        TArray<FHitResult> UniqueHitResults;
        FilterAlreadyHitActors(Window, ResultsToBroadcast, UniqueHitResults);
        ResultsToBroadcast = MoveTemp(UniqueHitResults);
    }

    if (ResultsToBroadcast.IsEmpty()) return;

    OnTraceHitsDetected.Broadcast(ExecutionContext.TraceWindowHandle, Window.CurrentPlaybackTime, ResultsToBroadcast);
}

void USampledTraceComponent::FilterAlreadyHitActors(FSampledTraceActiveWindow& Window, const TArray<FHitResult>& InHitResults,
    TArray<FHitResult>& OutHitResults)
{
    OutHitResults.Reset();

    for (const FHitResult& HitResult : InHitResults)
    {
        AActor* HitActor = HitResult.GetActor();
        if (!HitActor) continue;

        if (Window.HitActors.Contains(HitActor)) continue;

        Window.HitActors.Add(HitActor);
        OutHitResults.Add(HitResult);
    }
}

bool USampledTraceComponent::ResolveMontageTimeToSequence(const UAnimMontage* Montage, float MontageTime, UAnimSequence*& OutSequence, float& OutSequenceTime,
    FString& OutError) const
{
    OutSequence = nullptr;
    OutSequenceTime = 0.0f;

    if (!Montage)
    {
        OutError = "Montage is null.";
        return false;
    }

    for (const FSlotAnimationTrack& SlotTrack : Montage->SlotAnimTracks)
    {
        for (const FAnimSegment& Segment : SlotTrack.AnimTrack.AnimSegments)
        {
            const float SegmentStart = Segment.StartPos;
            const float SegmentLength = Segment.GetLength();
            const float SegmentEnd = SegmentStart + SegmentLength;

            if (MontageTime < SegmentStart || MontageTime > SegmentEnd) continue;

            UAnimSequence* Sequence = Cast<UAnimSequence>(Segment.GetAnimReference());
            if (!Sequence)
            {
                OutError = FString::Printf(TEXT("Anim segment at montage time %.3f is not a UAnimSequence."), MontageTime);
                return false;
            }

            const float TimeIntoSegment = MontageTime - SegmentStart;
            const float SequenceTime = Segment.AnimStartTime + TimeIntoSegment * Segment.AnimPlayRate;

            OutSequence = Sequence;
            OutSequenceTime = SequenceTime;
            return true;
        }
    }

    OutError = FString::Printf(TEXT("Could not resolve montage time %.3f to an anim sequence segment."), MontageTime);
    return false;
}

bool USampledTraceComponent::EvaluateSocketLocalPositionAtSequenceTime(const UAnimSequence* Sequence, FName SocketName, float SequenceTime,
    FVector& OutLocalPosition, FString& OutError) const
{
    if (!Sequence)
    {
        OutError = "Sequence is null.";
        return false;
    }

    const USkeleton* Skeleton = Sequence->GetSkeleton();
    if (!Skeleton)
    {
        OutError = "Sequence has no skeleton.";
        return false;
    }

    const USkeletalMeshSocket* Socket = Skeleton->FindSocket(SocketName);
    if (!Socket)
    {
        OutError = FString::Printf(TEXT("Socket '%s' not found on skeleton."), *SocketName.ToString());
        return false;
    }

    const FReferenceSkeleton& ReferenceSkeleton = Skeleton->GetReferenceSkeleton();
    const int32 BoneIndex = ReferenceSkeleton.FindBoneIndex(Socket->BoneName);

    if (BoneIndex == INDEX_NONE)
    {
        OutError = FString::Printf(TEXT("Socket '%s' parent bone '%s' not found."), *SocketName.ToString(), *Socket->BoneName.ToString());
        return false;
    }

    TArray<int32> BoneChain;
    {
        int32 CurrentIndex = BoneIndex;
        while (CurrentIndex != INDEX_NONE)
        {
            BoneChain.Add(CurrentIndex);
            CurrentIndex = ReferenceSkeleton.GetParentIndex(CurrentIndex);
        }

        Algo::Reverse(BoneChain);

        if (!BoneChain.IsEmpty() && ReferenceSkeleton.GetParentIndex(BoneChain[0]) == INDEX_NONE)
        {
            BoneChain.RemoveAt(0);
        }
    }

    FTransform BoneRelativeToRoot = FTransform::Identity;


    for (const int32 ChainBoneIndex : BoneChain)
    {
        FTransform BoneLocal;
        Sequence->GetBoneTransform(BoneLocal, ChainBoneIndex, SequenceTime, false);

        BoneRelativeToRoot = BoneLocal * BoneRelativeToRoot;
    }

    const FTransform SocketLocal(Socket->RelativeRotation, Socket->RelativeLocation, Socket->RelativeScale);
    const FTransform SocketRelativeToRoot = SocketLocal * BoneRelativeToRoot;
    OutLocalPosition = SocketRelativeToRoot.GetLocation();

    return true;
}

bool USampledTraceComponent::EnsureOwnerReferences()
{
    if (!OwnerCharacter)
    {
        OwnerCharacter = Cast<ACharacter>(GetOwner());
    }

    if (!OwnerMesh && OwnerCharacter)
    {
        OwnerMesh = OwnerCharacter->GetMesh();
    }

    return OwnerMesh != nullptr;
}