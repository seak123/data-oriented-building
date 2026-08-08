// 建造物件（actor 侧 gameplay 层）：一个 BasePiece 由若干可插拔功能组件组合而成，
// 具体物件（门 / 熔炉 / 篝火 / 储物箱 / 孵化器…）继承它并初始化各自的独特数据。
// 参照实现，说明 gameplay 组合方式，不含任何专有源码。
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BuildPiece.generated.h"

class UBuildSnapComponent;
class UCombinableSlotComponent;
class UBuildInteractComponent;

// 拆除原因：影响掉落/回收/存盘等后续处理
UENUM()
enum class EPieceDestroyReason : uint8
{
	PlayerDemolish, // 玩家主动拆除
	Damaged,        // 被打掉
	Replaced,       // 被替换
};

/**
 * 建造物件基类。
 * ！！！物件的行为不写死在类里，而是"需要什么就挂什么组件"——
 * 一堵墙只需要吸附 + 承重，一扇门额外需要交互，一个熔炉再加生产。
 */
UCLASS()
class ABuildPiece : public AActor, public IBuildingUnit
{
	GENERATED_BODY()

public:
	ABuildPiece();

	virtual void OnConstruction(const FTransform& Transform) override;

	// 通用初始化（所有物件都走）
	void InitBaseData(const FBuildingPieceSyncData& SyncData);
	// 各物件的独特初始化（篝火/储物箱/熔炉各不相同）——子类覆写
	virtual void InitDistinctiveData() {}

	// 交互入口：子类覆写以实现"开关门""点火""存取物品"等
	virtual void OnInteract(class APlayerCharacter* Player, int32 InteractionId) {}

	// 拆除：把资源回收 + 通知承重系统重算
	void Destroy(EPieceDestroyReason Reason, class APlayerCharacter* ByPlayer);

protected:
#pragma region 可插拔功能组件
	// 吸附点：决定这个物件能吸到谁、别人能吸到它
	UPROPERTY(VisibleAnywhere, Category = "Build|Components")
	TArray<UBuildSnapComponent*> SnapComponents;

	// 组合槽：接受/排斥特定物件（如"墙上的槽只接受门/窗"）
	UPROPERTY(VisibleAnywhere, Category = "Build|Components")
	TArray<UCombinableSlotComponent*> Slots;

	// 交互区：玩家靠近时冒出上下文交互项
	UPROPERTY(VisibleAnywhere, Category = "Build|Components")
	UBuildInteractComponent* InteractComponent = nullptr;
#pragma endregion

	// 配置 ID，驱动这个物件"是什么"
	UPROPERTY(Replicated)
	int32 ConfigTypeId = 0;
};
