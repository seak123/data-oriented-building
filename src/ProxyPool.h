// Proxy 对象池：只有玩家靠近/交互的建筑才"升格"成真正的 Actor，
// 其余永远只是数据。参照实现，不含任何专有源码。
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ProxyPool.generated.h"

struct FEntityHandle { uint32 Index = 0; uint32 Serial = 0; };
struct FProxyHandle  { FEntityHandle Entity; bool IsValid() const { return Entity.Serial != 0; } };

/**
 * 借出/回收共享的 AProxyActor。
 * ！！！MAX_PROXIES 给活跃 Actor 数量封顶 —— 无论世界里有几千个建筑，
 * 真正的 Actor 数量都被这个上限锁死，这正是整套优化的目的。
 */
UCLASS()
class UProxyPool : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// 建筑进入交互范围：借一个池化 Actor 绑上去
	FProxyHandle Acquire(FEntityHandle Entity, FName Reason);

	// 离开交互范围：解绑并归还到池子（不销毁）
	void Release(FProxyHandle Handle, FName Reason);

protected:
	// 池空时按需新建，直到 MAX_PROXIES 上限；到顶则返回空句柄，该建筑保持纯数据
	class AProxyActor* SpawnNewProxy();

	// 活跃 Actor 上限
	UPROPERTY(EditAnywhere, Category = "Building|Proxy")
	int32 Val_MaxProxies = 128;

private:
	struct FBinding
	{
		class AProxyActor* Proxy = nullptr;
		int32              LockCount = 0; // 支持多方（碰撞/交互/UI）同时持有
	};

	TArray<class AProxyActor*>       FreeList;   // 空闲池
	TMap<uint32, FBinding>          Bindings;    // Entity.Index -> 绑定信息
	int32                           LiveCount = 0;
};
