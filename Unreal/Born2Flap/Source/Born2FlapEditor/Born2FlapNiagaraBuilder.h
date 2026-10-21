#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "Born2FlapNiagaraBuilder.generated.h"

UCLASS()
class UBorn2FlapNiagaraBuilderCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UBorn2FlapNiagaraBuilderCommandlet();

    virtual int32 Main(const FString& Params) override;
};
