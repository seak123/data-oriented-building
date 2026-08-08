// 建造流程（BuildHoldFlow）：玩家"举着"一个物件预览时，每帧从镜头射线出发，
// 收集可吸附候选、按优先级排序、校验放置合法性，确认后落地。
// 参照实现，说明放置 gameplay，不含任何专有源码。
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BuildHoldFlow.generated.h"

class UBuildSnapComponent;
class ABuildPiece;

// 一次射线里命中的一个吸附候选：手上物件的"主动吸附点"配到场景物件的"被动吸附点"
struct FSnapCandidate
{
	int32                 Priority = 0;        // 值越小优先级越高
	UBuildSnapComponent*  ActiveSnapPoint = nullptr;  // 手上物件的吸附点
	UBuildSnapComponent*  PassiveSnapPoint = nullptr; // 被吸附的场景物件的吸附点
};

// 放置为什么不合法
UENUM()
enum class EPlaceRejectReason : uint8
{
	None,
	Overlapping,       // 和已有物件/地形重叠
	NoSupport,         // 承重不足（悬空）
	ReachBuildLimit,   // 达到个人建造数量上限
	OutOfArea,         // 超出可建造区域
};

/**
 * 建造持有流程子系统。
 * 每帧：射线 -> 收集吸附候选 -> 取最优 -> 摆到吸附位姿 -> 校验 -> 显示预览（绿=可放/红=不可放）。
 * 玩家确认后 CommitPlace；也支持对已有物件的选中/移动/拆除/修理。
 */
UCLASS()
class UBuildHoldFlow : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// 开始举着某个配置的物件进入预览
	void BeginHold(int32 ConfigTypeId);

	// 每帧刷新预览位姿与合法性
	void TickPreview(float DeltaTime);

	// 确认放置；不合法则返回原因
	bool CommitPlace(EPlaceRejectReason& OutReason);

	// 对已有物件的操作
	void SelectPiece(const FGuid& PieceGuid);
	bool CanBeMoved(const FGuid& PieceGuid, EPlaceRejectReason& OutReason) const;
	bool CanBeDeleted(const FGuid& PieceGuid) const;

protected:
	// 从镜头射线收集所有吸附候选
	void GatherSnapCandidates(TArray<FSnapCandidate>& OutCandidates) const;

	// 取优先级最高的候选，算出吸附后的位姿
	bool ResolveBestSnap(const TArray<FSnapCandidate>& Candidates, FTransform& OutPose) const;

	// 校验：重叠 / 承重 / 数量上限 / 区域
	EPlaceRejectReason ValidatePlacement(const FTransform& Pose) const;

private:
	int32     HoldingConfigId = 0;
	FTransform PreviewPose;
	bool       bPreviewValid = false;

	// 个人建造数量上限（在当前世界）
	UPROPERTY(EditAnywhere, Category = "Build|Rule")
	int32 Val_PersonalBuildLimit = 3000;
};
