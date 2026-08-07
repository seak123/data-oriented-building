// Data-Oriented Building — illustrative clean-room reference (no proprietary source).
// Fragment composition + replicated/transient split described in the README.
#pragma once

#include "CoreMinimal.h"

// --- Small composable fragments. A piece "is" whatever fragments it carries. ---
USTRUCT() struct FTransformFragment { GENERATED_BODY() FVector Location = FVector::ZeroVector; FQuat Rotation = FQuat::Identity; };
USTRUCT() struct FHealthFragment    { GENERATED_BODY() float Current = 0.f; float Max = 0.f; };
USTRUCT() struct FTypeFragment      { GENERATED_BODY() int32 ConfigId = 0; };
USTRUCT() struct FSupportFragment   { GENERATED_BODY() float SupportValue = 0.f; };

// Cold, per-instance runtime state kept OFF the replicated hot array (accessed by pointer).
struct FBuildingTransient
{
    TSharedPtr<struct FStreamableHandle> PieceAssetHandle;
    class AProxyActor* BoundProxy = nullptr;
    bool  bSelected = false;
    uint32 LastSavedVersion = 0;
};

// The replicated agent: ONLY what must cross the wire — kept compact for fast iteration.
USTRUCT()
struct FBuildingAgent
{
    GENERATED_BODY()

    UPROPERTY() FTransformFragment Transform;
    UPROPERTY() FHealthFragment    Health;
    UPROPERTY() FTypeFragment      Type;
    UPROPERTY() FSupportFragment   Support;

    // Not replicated: lives off the hot array so agent copies stay cheap.
    TSharedPtr<FBuildingTransient> Transient;

    bool operator==(const FBuildingAgent& O) const
    {
        return Type.ConfigId == O.Type.ConfigId
            && Transform.Location.Equals(O.Transform.Location);
    }
};
