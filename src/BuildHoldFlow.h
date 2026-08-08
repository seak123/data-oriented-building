// 建造流程 / Build hold flow
//
// 玩家"举着"一个物件预览时，每帧从镜头射线出发：收集吸附候选 -> 按优先级选最优 ->
// 摆到吸附位姿 -> 校验合法性 -> 预览变绿/变红。确认后落地。
// While the player holds a piece in preview, every frame: raycast from the camera,
// gather snap candidates, pick the best by priority, place at the snapped pose,
// validate, and tint the preview green or red. Commit on confirm.
//
// 同一套流程也驱动对已有物件的选中 / 移动 / 拆除 / 修理 / 涂装。
// The same flow also drives select / move / demolish / repair / paint on existing pieces.
//
// 参照实现，说明玩法机制，不含任何专有源码。
// Reference implementation illustrating the mechanic; no proprietary source.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BuildHoldFlow.generated.h"

class UBuildSnapComponent;
class ABuildPiece;

/**
 * 建造模式 / Build system mode.
 * 同一套持有流程在不同模式下解释玩家输入的方式不同。
 * One flow, several interpretations of the player's input.
 */
UENUM()
enum class EBuildMode : uint8
{
	Place,      // 放置新物件 / placing a new piece
	Move,       // 搬移已有物件 / relocating an existing piece
	Demolish,   // 拆除 / demolishing
	Repair,     // 修理 / repairing
	Paint,      // 涂装 / painting
};

/**
 * 一个吸附候选 / One snap candidate from this frame's raycast.
 * 手上物件的"主动点"配到场景物件的"被动点"。
 * Pairs an ACTIVE snap point on the held piece with a PASSIVE point on an existing one.
 */
USTRUCT()
struct FSnapCandidate
{
	GENERATED_BODY()

	// 优先级：值越小越优先（地基 > 墙面 > 自由摆放）
	// Priority — lower wins (foundation beats wall face beats free placement)
	UPROPERTY() int32 Priority = 0;

	// 手上物件的吸附点 / Snap point on the held piece
	UPROPERTY() UBuildSnapComponent* ActiveSnapPoint = nullptr;

	// 被吸附的场景物件的吸附点 / Snap point on the target piece in the world
	UPROPERTY() UBuildSnapComponent* PassiveSnapPoint = nullptr;
};

/**
 * 放置被拒绝的原因 / Why a placement is illegal.
 * ！！！必须把"为什么不能放"精确回给玩家，否则玩家只会看到一个红色方块而不知所措。
 * The precise reason must reach the player — otherwise all they see is a red box
 * with no idea what's wrong. 每个原因对应一句具体提示文案。
 * Each reason maps to a specific on-screen message.
 */
UENUM()
enum class EPlaceRejectReason : uint8
{
	None,
	Overlapping,       // 与已有物件或地形重叠 / overlaps an existing piece or terrain
	NoSupport,         // 承重不足（会悬空/坍塌）/ insufficient support — it would float or collapse
	ReachBuildLimit,   // 达到个人建造数量上限 / personal build-count limit reached
	OutOfArea,         // 超出可建造区域（他人领地/禁建区）/ outside the buildable area
	NoSnapTarget,      // 该物件必须吸附才能放 / this piece may only be placed snapped
	NotEnoughMaterial, // 材料不足 / insufficient materials
	Blocked,           // 被角色/生物挡住 / a character or creature is in the way
};

/**
 * 建造持有流程 / The hold flow subsystem.
 */
UCLASS()
class UBuildHoldFlow : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// 开始举起某配置的物件 / Start holding a piece of the given config
	void BeginHold(int32 ConfigTypeId);
	void EndHold();

	// 每帧刷新预览位姿与合法性 / Refresh preview pose and legality each frame
	void TickPreview(float DeltaTime);

	// 玩家手动旋转（吸附时按吸附点朝向对齐，自由摆放时按角度步进）
	// Manual rotation — aligned to the snap point when snapped, stepped by angle when free
	void RotatePreview(float DeltaYaw);

	// 确认放置；失败时回出原因 / Commit; returns the reason on failure
	bool CommitPlace(EPlaceRejectReason& OutReason);

	// 已有物件的操作 / Operations on existing pieces
	void SelectPiece(const FGuid& PieceGuid);
	bool CanBeMoved(const FGuid& PieceGuid, EPlaceRejectReason& OutReason) const;
	bool CanBeDeleted(const FGuid& PieceGuid) const;
	bool NeedRepairing(const FGuid& PieceGuid) const;

protected:
	/**
	 * 从镜头射线收集所有吸附候选 / Gather every snap candidate under the camera ray.
	 * 一次射线可能同时命中多个可吸附点（角落处尤其常见），所以是收集而非取第一个。
	 * A single ray often hits several viable points at once — corners especially —
	 * so candidates are collected rather than taking the first hit.
	 */
	void GatherSnapCandidates(TArray<FSnapCandidate>& OutCandidates) const;

	// 取优先级最高的候选，算出吸附后的位姿 / Pick the highest-priority candidate and derive the pose
	bool ResolveBestSnap(const TArray<FSnapCandidate>& Candidates, FTransform& OutPose) const;

	/**
	 * 逐条校验放置规则 / Validate placement rules in order.
	 * 顺序有讲究：先做便宜的检查（数量上限、区域），后做昂贵的（重叠扫描、承重试算），
	 * Order matters: cheap checks first (count limit, area), expensive last
	 * (overlap sweep, trial support calculation),
	 * 因为这是每帧都在跑的。
	 * because this runs every single frame while holding.
	 */
	EPlaceRejectReason ValidatePlacement(const FTransform& Pose) const;

private:
	EBuildMode Mode = EBuildMode::Place;
	int32      HoldingConfigId = 0;

	FTransform PreviewPose;
	bool       bPreviewValid = false;

	// 当前选中的已有物件 / Currently selected existing piece
	FGuid      SelectedPieceGuid;

	// 个人在当前世界的建造数量上限 / Per-player build-count cap in this world
	UPROPERTY(EditAnywhere, Category = "Build|Rule") int32 Val_PersonalBuildLimit = 3000;

	// 自由摆放时的旋转步进角度 / Rotation step when placing freely
	UPROPERTY(EditAnywhere, Category = "Build|Rule") float Val_FreeRotateStep = 15.f;
};
