// 承重系统 / Structural Support System
//
// 建造物件不能凭空悬浮：承重值从"接地"或"供给点"出发，沿着相互接触的物件向外传播，
// 每传播一段就按距离和方向衰减；低于最小承重的物件会坍塌。
// Pieces cannot float in mid-air: support originates from grounded (or supply) pieces and
// propagates outward through touching pieces, decaying with distance and direction.
// A piece whose support falls below its minimum collapses.
//
// 参照实现，说明玩法机制，不含任何专有源码。
// Reference implementation illustrating the mechanic; contains no proprietary source.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BuildSupportSystem.generated.h"

/**
 * 某个物件与周围环境的接触情况 / How a piece touches the world around it.
 * 这是承重传播的"源"判定：碰到地形或供给源，就直接拿满承重。
 * This decides whether a piece is a support SOURCE: touching terrain or a supply
 * point grants it full support outright.
 */
USTRUCT()
struct FOverlapInfo
{
	GENERATED_BODY()

	// 是否接触地形（接地即为承重源）/ Touching terrain — grounded pieces are sources
	UPROPERTY() bool bOverlapTerrain = false;

	// 是否接触"供给承重"的特殊物件（如图腾/地基）/ Touching a support-supplying object (totem, foundation)
	UPROPERTY() bool bSupplySupport = false;

	// 与之接触的其他物件 / Neighbouring pieces this one touches
	TSet<FGuid> OverlapPieces;
};

/**
 * 单个物件的承重参数（来自配置表）/ Per-piece support parameters, driven by config.
 */
USTRUCT()
struct FSupportParams
{
	GENERATED_BODY()

	// 低于此值即坍塌 / Below this value the piece collapses
	UPROPERTY() float MinSupport = 0.f;
	// 承重上限（接地物件直接拿到这个值）/ Cap; grounded pieces receive exactly this
	UPROPERTY() float MaxSupport = 0.f;

	// 垂直方向传播损耗（往上叠越高越弱）/ Loss when propagating vertically (stacking upward)
	UPROPERTY() float VerticalLoss = 0.f;
	// 水平方向传播损耗（往外悬挑越远越弱）/ Loss when propagating horizontally (cantilevering out)
	UPROPERTY() float HorizontalLoss = 0.f;
};

/**
 * 承重传播系统 / Support propagation system.
 *
 * ！！！这是一个"全局收敛"问题：A 撑着 B、B 撑着 C，改动任何一个都会波及一片。
 * 因此采用迭代松弛：每轮用邻居的当前承重重算自己，直到全场稳定（变化 < 阈值）。
 * This is a global convergence problem — A supports B supports C, so one edit ripples
 * outward. Solved by iterative relaxation: each pass recomputes a piece from its
 * neighbours' current values until the whole field is stable (delta < epsilon).
 *
 * ！！！并且必须分帧摊销：一次性算几千个物件会直接卡死主线程。
 * And it must be amortized across frames — recomputing thousands of pieces in one
 * tick would stall the game thread outright.
 */
UCLASS()
class UBuildSupportSystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;

	// 物件增删/移动后，标记需要重算 / Mark dirty after a piece is added, moved, or removed
	void MarkSupportDirty(const FGuid& PieceGuid);

protected:
	/**
	 * 计算单个物件的承重 / Compute one piece's support value.
	 *
	 * 规则 / Rules:
	 *  1. 接地或接触供给源 -> 直接给 MaxSupport（它是源头）
	 *     Grounded or touching a supply -> MaxSupport outright (it is a source).
	 *  2. 否则从每个接触到的邻居传播，取最优值：
	 *     Otherwise propagate from each touching neighbour and take the best:
	 *       - 邻居自己都承重不足的，不能提供承重（防止"烂撑烂"）
	 *         A neighbour below its own minimum provides nothing (no propping-up by rubble).
	 *       - 损耗按方向在水平/垂直之间插值，并随中心距离放大
	 *         Loss lerps between horizontal and vertical by direction, scaled by centre distance.
	 *  3. 有 2 个及以上下方支撑点时给予加成（多点支撑更稳）
	 *     Two or more support points below grant a bonus — multi-point support is sturdier.
	 *  4. 最终不超过 MaxSupport / Result is clamped to MaxSupport.
	 */
	float CalculateSupport(const FGuid& PieceGuid) const;

	// 一轮迭代：重算所有脏物件，返回是否已收敛 / One relaxation pass; returns true when stable
	bool RelaxOnce();

	// 承重不足 -> 坍塌 / Below minimum -> collapse
	void CollapsePiece(const FGuid& PieceGuid);

private:
	// 每帧最多处理多少个物件，把开销摊到多帧 / Per-frame budget, amortizing cost across frames
	UPROPERTY(EditAnywhere, Category = "Build|Support")
	int32 Val_FrameLoopCount = 300;

	// 收敛阈值：两轮之间变化小于它就认为稳定 / Convergence epsilon between passes
	UPROPERTY(EditAnywhere, Category = "Build|Support")
	float Val_StableEpsilon = 0.1f;

	// GM 开关：方便现场关掉承重排查问题 / GM toggle to disable support for debugging
	bool bSupportEnabled = true;

	TSet<FGuid>                 DirtyPieces;
	TMap<FGuid, FOverlapInfo>   OverlapCache;   // 接触关系缓存 / cached contact info
	TMap<FGuid, float>          TempSupport;    // 迭代中的临时值 / in-flight values during relaxation
};

/**
 * 传播损耗的核心公式 / The core propagation formula.
 *
 * 方向 t：0 = 完全水平（悬挑），1 = 完全垂直（堆叠）；损耗在两者之间插值。
 * Direction t: 0 = purely horizontal (cantilever), 1 = purely vertical (stack);
 * the loss factor lerps between the two.
 * 再乘以中心距离——离支撑点越远，能借到的承重越少。
 * Then scaled by centre distance — the further from the support, the less you inherit.
 */
inline float PropagateSupport(float NeighbourSupport, const FSupportParams& Params,
                              float DirectionT, float CentreDistance)
{
	const float Loss = FMath::Lerp(Params.HorizontalLoss, Params.VerticalLoss, DirectionT);
	return NeighbourSupport - Loss * CentreDistance * NeighbourSupport;
}
