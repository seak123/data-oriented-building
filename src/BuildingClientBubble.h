// 增量复制通道：基于 FastArray 的脏集复制，带每帧预算 + 弱网自适应节流。
// 参照实现，不含任何专有源码。
#pragma once

#include "CoreMinimal.h"
#include "BuildingClientBubble.generated.h"

/**
 * 服务器侧：即便建筑已是"数据"，几千个也不能一帧全同步。
 * 这里用每帧"变更/删除预算"控制节流，并在探测到丢包时收缩预算，让通道优雅降级。
 */
UCLASS()
class UBuildingClientBubble : public UObject
{
	GENERATED_BODY()

public:
	void ServerTick();

	void MarkEntityDirty(uint32 EntityIndex) { DirtyEntities.Add(EntityIndex); }
	void SetBadNetwork(bool bBad) { bBadNetwork = bBad; }

protected:
	// 本帧允许标记多少个变更：正常给足，探测到丢包则大幅收缩，避免加剧拥塞
	int32 GetChangesBudgetThisFrame() const
	{
		return bBadNetwork ? Val_MaxChangesPerUpdate_BadNet   // 弱网：如 5
		                   : Val_MaxChangesPerUpdate;         // 正常：如 50
	}

	void MarkAgentDirtyForReplication(uint32 EntityIndex);

#pragma region 节流调参
	UPROPERTY(Config) int32 Val_MaxChangesPerUpdate        = 50;
	UPROPERTY(Config) int32 Val_MaxChangesPerUpdate_BadNet = 5;
	// 删除单独给更大的预算：删除包小、但要尽快让客户端把废弃实体清掉
	UPROPERTY(Config) int32 Val_MaxDeletesPerUpdate        = 500;
#pragma endregion

private:
	bool           bBadNetwork = false;
	TArray<uint32> DirtyEntities;   // 脏集，实际项目里按优先级（距离/可见性）排序
};

inline void UBuildingClientBubble::ServerTick()
{
	int32 Budget = GetChangesBudgetThisFrame();
	for (uint32 Entity : DirtyEntities)
	{
		if (Budget-- <= 0)
		{
			break; // 剩下的留到下一帧
		}
		MarkAgentDirtyForReplication(Entity);
	}
	// 删除走各自的预算；此处略。
}
