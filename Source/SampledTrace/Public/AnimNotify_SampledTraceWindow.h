// Copyright 2026 Vee-Qor. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SampledTraceTypes.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotify_SampledTraceWindow.generated.h"

UCLASS()
class SAMPLEDTRACE_API UAnimNotify_SampledTraceWindow : public UAnimNotifyState
{
    GENERATED_BODY()

public:
    virtual FString GetNotifyName_Implementation() const override;
    virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
        const FAnimNotifyEventReference& EventReference) override;
    virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

private:
    UPROPERTY(EditAnywhere, Category="SampledTrace")
    FSampledTraceSettings TraceSettings;
};