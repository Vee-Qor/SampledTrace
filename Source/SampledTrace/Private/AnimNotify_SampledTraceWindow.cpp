// Copyright 2026 Vee-Qor. All Rights Reserved.


#include "AnimNotify_SampledTraceWindow.h"

#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "SampledTraceComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogSampledTraceWindow, All, All);

FString UAnimNotify_SampledTraceWindow::GetNotifyName_Implementation() const
{
    return "SampledTraceWindow";
}

void UAnimNotify_SampledTraceWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
    const FAnimNotifyEventReference& EventReference)
{
    Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

    if (!MeshComp) return;

    const UWorld* World = MeshComp->GetWorld();
    if (!World || !World->IsGameWorld()) return;

    const AActor* Owner = MeshComp->GetOwner();
    if (!Owner) return;

    USampledTraceComponent* SampledTraceComponent = Owner->FindComponentByClass<USampledTraceComponent>();
    if (!SampledTraceComponent)
    {
        UE_LOG(LogSampledTraceWindow, Warning, TEXT("NotifyBegin: SampledTraceComponent is null."));
        return;
    }

    UAnimMontage* Montage = Cast<UAnimMontage>(Animation);
    if (!Montage)
    {
        UE_LOG(LogSampledTraceWindow, Warning, TEXT("NotifyBegin: Montage is null."));
        return;
    }

    const FAnimNotifyEvent* NotifyEvent = EventReference.GetNotify();
    if (!NotifyEvent)
    {
        UE_LOG(LogSampledTraceWindow, Warning, TEXT("NotifyBegin: NotifyEvent is null."));
        return;
    }

    FSampledTraceWindowParams WindowParams;
    WindowParams.Montage = Montage;
    WindowParams.WindowStartTime = NotifyEvent->GetTriggerTime();
    WindowParams.WindowEndTime = WindowParams.WindowStartTime + TotalDuration;

    SampledTraceComponent->BeginTraceWindowForNotify(NotifyEvent, TraceSettings, WindowParams);
}

void UAnimNotify_SampledTraceWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
    Super::NotifyEnd(MeshComp, Animation, EventReference);

    if (!MeshComp) return;

    const UWorld* World = MeshComp->GetWorld();
    if (!World || !World->IsGameWorld()) return;

    const AActor* Owner = MeshComp->GetOwner();
    if (!Owner) return;

    USampledTraceComponent* SampledTraceComponent = Owner->FindComponentByClass<USampledTraceComponent>();
    if (!SampledTraceComponent)
    {
        UE_LOG(LogSampledTraceWindow, Warning, TEXT("NotifyEnd: SampledTraceComponent is null."));
        return;
    }

    const FAnimNotifyEvent* NotifyEvent = EventReference.GetNotify();
    if (!NotifyEvent)
    {
        UE_LOG(LogSampledTraceWindow, Warning, TEXT("NotifyEnd: NotifyEvent is null."));
        return;
    }

    SampledTraceComponent->EndTraceWindowForNotify(NotifyEvent);
}