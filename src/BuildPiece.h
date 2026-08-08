// 建造物件 / Build piece (actor-side gameplay layer)
//
// 一个建造物件由"基类 + 若干可插拔功能组件"组合而成；具体物件（门/熔炉/储物箱/孵化器…）
// 继承基类并初始化各自的独特数据。墙只需要吸附与承重，门额外要交互，熔炉再加生产。
// A build piece is a base actor plus pluggable functional components. Concrete objects
// (door / furnace / storage box / incubator …) subclass it and initialize their own
// distinctive data. A wall needs snapping and support; a door adds interaction; a
// furnace adds production on top.
//
// 参照实现，说明玩法组合方式，不含任何专有源码。
// Reference implementation illustrating the composition model; no proprietary source.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BuildPiece.generated.h"

class UBuildSnapComponent;
class UCombinableSlotComponent;
class UBuildInteractComponent;
class UPieceLevelUpComponent;
class APlayerCharacter;

/**
 * 拆除原因 / Why a piece was removed.
 * 不同原因决定了材料是否返还、是否播放坍塌表现、是否记录到日志。
 * The reason determines whether materials are refunded, whether the collapse VFX
 * plays, and what gets written to the world log.
 */
UENUM()
enum class EPieceDestroyReason : uint8
{
	PlayerDemolish,   // 玩家主动拆除（返还材料）/ Player demolished — refund materials
	Damaged,          // 被打坏 / Destroyed by damage
	SupportCollapse,  // 承重不足坍塌 / Collapsed from insufficient support
	Replaced,         // 被替换（如升级换模型）/ Replaced, e.g. upgraded to a new mesh
	Expired,          // 到期消失（临时物件）/ Expired (temporary pieces)
};

/**
 * 建造物件基类 / Base build piece.
 *
 * ！！！核心决策：物件的能力不写死在类继承树里，而是"需要什么挂什么组件"。
 * Key decision: capability is NOT encoded in the inheritance tree — a piece carries
 * exactly the components it needs.
 * 这样策划新增一个物件时，绝大多数情况只是"选组件 + 填配置"，不需要动框架代码；
 * Adding a new object is then usually just picking components and filling in config,
 * with no framework code touched;
 * 这也是整套建造工作流能让全团队并行产出物件的根本原因。
 * this is precisely what let the whole team author objects in parallel.
 */
UCLASS()
class ABuildPiece : public AActor, public IBuildingUnit
{
	GENERATED_BODY()

public:
	ABuildPiece();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutProps) const override;

	// 通用初始化：所有物件都走这条 / Common init — every piece runs this
	void InitBaseData(const struct FPieceSyncData& SyncData);

	/**
	 * 各物件的独特初始化 / Per-object distinctive init.
	 * 篝火要挂火焰特效与光源，储物箱要建背包容器，熔炉要恢复生产队列——
	 * A campfire attaches flame VFX and a light; a storage box builds its container;
	 * a furnace restores its production queues —
	 * 由子类覆写，基类不关心具体是什么物件。
	 * overridden by subclasses; the base class stays agnostic.
	 */
	virtual void InitDistinctiveData() {}

	/**
	 * 交互入口 / Interaction entry point.
	 * 同一个物件可以有多个交互项（开门/上锁/拆除），用 InteractionId 区分。
	 * One piece can expose several interactions (open, lock, demolish), told apart
	 * by InteractionId.
	 */
	virtual void OnInteract(APlayerCharacter* Player, int32 InteractionId) {}

	// 拆除：回收材料 + 通知承重系统重算周边 / Remove: refund, then trigger a support recompute
	void DestroyPiece(EPieceDestroyReason Reason, APlayerCharacter* ByPlayer);

#pragma region 可插拔功能组件 / Pluggable functional components
	/**
	 * 吸附点 / Snap points.
	 * 分主动与被动两侧：手上物件用"主动点"去找场景物件的"被动点"。
	 * Two sides: the held piece's ACTIVE points seek existing pieces' PASSIVE points.
	 * 每个点带优先级，决定多个候选同时命中时优先吸哪个（如地基优先于墙面）。
	 * Each point carries a priority deciding which candidate wins when several hit
	 * at once (a foundation outranks a wall face).
	 */
	UPROPERTY(VisibleAnywhere, Category = "Build|Components")
	TArray<UBuildSnapComponent*> SnapComponents;

	/**
	 * 组合槽 / Combinable slots.
	 * 用于"墙上开门""墙上装窗"这类嵌入关系：槽会接受或排斥特定类型的物件。
	 * Models embedding relationships — a doorway or window in a wall. A slot accepts
	 * or rejects specific piece types.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Build|Components")
	TArray<UCombinableSlotComponent*> Slots;

	/**
	 * 交互区 / Interaction region.
	 * 玩家进入区域时冒出上下文交互项；离开时收起。
	 * Surfaces context actions when a player enters, and clears them on exit.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Build|Components")
	UBuildInteractComponent* InteractComponent = nullptr;

	// 升级组件：物件可升阶（更高血量/更好外观/更快生产）/ Level-up: more HP, better look, faster output
	UPROPERTY(VisibleAnywhere, Category = "Build|Components")
	UPieceLevelUpComponent* LevelUpComponent = nullptr;
#pragma endregion

protected:
#pragma region 状态 / State
	// 配置 ID：驱动这个物件"是什么" / Config ID — what this object actually is
	UPROPERTY(Replicated) int32 ConfigTypeId = 0;

	// 预览态：举着还没放下时为 true，此时不参与碰撞与承重
	// Preview mode: true while being held; excluded from collision and support
	UPROPERTY() bool bInPreview = false;

	// 预览用的半透明模型 / Translucent preview meshes
	UPROPERTY() TArray<UMeshComponent*> PreviewMeshComponents;

	// 涂装：正反面可分别上色 / Coating: front and back faces can be painted separately
	UPROPERTY(Replicated) int32 PositiveCoatId = 0;
	UPROPERTY(Replicated) int32 NegativeCoatId = 0;

	// 建造碰撞检测缩放：略小于本体，避免相邻物件互相判定重叠
	// Collision check scale — slightly smaller than the mesh so neighbours don't
	// falsely report overlap with each other
	UPROPERTY(EditAnywhere, Category = "Build") float Val_OverlapCheckScale = 0.8f;
#pragma endregion
};

/**
 * 具体物件示例 / A concrete object.
 * 一扇门的全部 gameplay 就是一次覆写——这正是组合模型的收益。
 * A door's entire gameplay is a single override — the payoff of the composition model.
 */
UCLASS()
class ADoorPiece : public ABuildPiece
{
	GENERATED_BODY()

public:
	virtual void InitDistinctiveData() override;
	virtual void OnInteract(APlayerCharacter* Player, int32 InteractionId) override
	{
		ToggleOpen();   // 开 / 关 —— Open / close
	}

protected:
	void ToggleOpen();

	UPROPERTY(ReplicatedUsing = OnRep_Open) bool bOpen = false;
	UFUNCTION() void OnRep_Open();   // 客户端播开关门动画 / play the door animation on clients
};
