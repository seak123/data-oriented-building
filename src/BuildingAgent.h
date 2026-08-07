// 建筑实体：片段（Fragment）组合 + 复制/瞬态数据分离。
// 参照实现，用于说明数据导向设计，不含任何专有源码。
#pragma once

#include "CoreMinimal.h"
#include "BuildingAgent.generated.h"

#pragma region 可组合片段
// 一个建筑"是什么"，取决于它挂了哪些片段。片段都是小 POD，便于紧凑排布、批量遍历。
USTRUCT() struct FTransformFragment { GENERATED_BODY() FVector Location = FVector::ZeroVector; FQuat Rotation = FQuat::Identity; };
USTRUCT() struct FHealthFragment    { GENERATED_BODY() float Current = 0.f; float Max = 0.f; };
USTRUCT() struct FTypeFragment      { GENERATED_BODY() int32 ConfigId = 0; };
USTRUCT() struct FSupportFragment   { GENERATED_BODY() float SupportValue = 0.f; }; // 承重值
#pragma endregion

/**
 * 瞬态数据：不需要复制、只在本端存在的运行期状态。
 * ！！！放到 TSharedPtr 里、挂在热数组之外，避免 agent 在 FastArray 中拷来拷去时被一起搬运。
 */
struct FBuildingTransient
{
	TSharedPtr<struct FStreamableHandle> PieceAssetHandle; // 异步加载句柄
	class AProxyActor*                   BoundProxy = nullptr; // 当前绑定的共享 Actor（可能为空）
	bool                                 bSelected = false;    // 客户端：是否被选中
	uint32                               LastSavedVersion = 0; // 存盘版本号，用于增量存盘
};

/**
 * 复制体（agent）：只放"必须过网络"的数据，刻意保持紧凑。
 * 这个结构会被放进 FastArray 每帧遍历（表现 + 复制），所以越小越好、越 cache 友好越好。
 */
USTRUCT()
struct FBuildingAgent
{
	GENERATED_BODY()

#pragma region 需要复制
	UPROPERTY() FTransformFragment Transform;
	UPROPERTY() FHealthFragment    Health;
	UPROPERTY() FTypeFragment      Type;
	UPROPERTY() FSupportFragment   Support;
#pragma endregion

	// 不复制：挂在热数组之外，agent 拷贝时不会被拖着走
	TSharedPtr<FBuildingTransient> Transient;

	// 只用 NetID / 版本号判等，避免逐字段比较
	bool operator==(const FBuildingAgent& O) const
	{
		return Type.ConfigId == O.Type.ConfigId
		    && Transform.Location.Equals(O.Transform.Location);
	}
};
