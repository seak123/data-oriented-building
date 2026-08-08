// 生产类建造物件 / Production building entities
//
// 熔炉、篝火、烧炭窑、磨坊这类物件共享同一套"燃料 + 原料 + 产物"三队列模型：
// 投入燃料持续燃烧，燃烧时消耗原料的工作量，工作量耗尽产出产物。
// Furnaces, campfires, kilns and mills share one model built from three queues —
// fuel, inputs, products. Fuel burns down over time; while burning it consumes the
// current input's workload; when the workload is exhausted a product is emitted.
//
// 参照实现，说明玩法机制，不含任何专有源码。
// Reference implementation illustrating the mechanic; contains no proprietary source.
#pragma once

#include "CoreMinimal.h"
#include "BuildPiece.h"
#include "ProductionEntity.generated.h"

/**
 * 通用生产实体基类 / Common production entity.
 *
 * ！！！设计要点：用"工作量(workload)"而不是"剩余秒数"来记录加工进度。
 * Key decision: progress is tracked as WORKLOAD, not as remaining seconds.
 * 因为加工速度会被很多东西影响——物件等级、帕鲁协助、buff、天气。
 * Because throughput is modified by many things: entity level, creature helpers,
 * buffs, weather. 用工作量记录，速度变化时不需要回头换算已进行的时间；
 * With workload, a change in speed needs no retro-conversion of elapsed time —
 * 每 tick 按"当前速率"扣减即可，天然支持中途变速与离线结算。
 * each tick simply subtracts at the current rate, which also makes mid-run speed
 * changes and offline settlement fall out naturally.
 */
UCLASS()
class AProductionEntity : public ABuildPiece
{
	GENERATED_BODY()

public:
	// 玩家投入燃料 / Player inserts fuel
	bool AddFuel(int32 FuelItemId);

	// 玩家投入原料（按配方）/ Player inserts an input by recipe
	bool AddInput(int32 RecipeId);

	// 取走产物 / Collect finished products
	bool TakeProducts(TArray<int32>& OutProducts);

	// 服务器每 tick 推进生产 / Server-side production tick
	void TickProduction(float DeltaSeconds);

protected:
	/**
	 * 推进当前燃料的燃烧 / Burn down the current fuel.
	 * 燃料耗尽则从队列取下一个；没有燃料就停止生产（火熄灭）。
	 * When exhausted, pop the next fuel from the queue; with no fuel, production
	 * halts and the fire goes out.
	 */
	bool ConsumeFuel(float DeltaSeconds);

	/**
	 * 推进当前原料的加工 / Advance the current input's processing.
	 * 扣减工作量；归零则产出产物，并尝试开始下一个原料。
	 * Subtract workload; on reaching zero emit the product and start the next input.
	 */
	void ConsumeWorkload(float DeltaSeconds);

	// 当前加工速率（受等级/协助/buff 影响）/ Current rate, modified by level, helpers, buffs
	float GetWorkRate() const;

#pragma region 燃料队列 / Fuel queue
	// 燃料道具 ID 队列 / Queue of fuel item IDs
	UPROPERTY(Replicated) TArray<int32> Fuels;

	// 每个燃料的原始可燃烧时长 / Original burn duration of each queued fuel
	UPROPERTY(Replicated) TArray<double> FuelsTime;

	// 队首燃料的剩余燃烧时间 / Remaining burn time of the head fuel
	UPROPERTY(Replicated) double FirstFuelTime = 0;

	// 燃料槽容量上限 / Fuel slot capacity
	UPROPERTY(EditAnywhere, Category = "Production") int32 Val_FuelMax = 0;
#pragma endregion

#pragma region 原料与产物 / Inputs and products
	// 原料队列，存配方 ID / Input queue, storing recipe IDs
	UPROPERTY(Replicated) TArray<int32> Inputs;

	// 每个原料的剩余工作量 / Remaining workload per queued input
	UPROPERTY(Replicated) TArray<int32> InputWorkloads;

	// 产物队列，等待玩家取走 / Finished products awaiting collection
	UPROPERTY(Replicated) TArray<int32> Products;
#pragma endregion

	/**
	 * 有些物件不需要燃料（如风车靠风、磨坊靠帕鲁推），
	 * 且产物直接占用原料格而不抛出。用同一套基类靠配置区分。
	 * Some entities need no fuel (a windmill runs on wind, a mill on creature labour),
	 * and their product occupies the input slot instead of being emitted. Same base
	 * class, differentiated by config.
	 */
	UPROPERTY(EditAnywhere, Category = "Production") bool bRequiresFuel = true;
	UPROPERTY(EditAnywhere, Category = "Production") bool bProductOccupiesInputSlot = false;
};

/**
 * 队列生产实体 / Queue-work entity.
 * 工作台就是"容量为 1 的队列生产实体"——这个统一让工作台/烹饪台/熔炉共用一套 UI 与逻辑。
 * A workbench is simply a queue-work entity with capacity 1 — unifying benches,
 * cooking stations and furnaces behind one UI and one code path.
 */
UCLASS()
class AQueueWorkEntity : public AProductionEntity
{
	GENERATED_BODY()

public:
	// 队列容量（工作台为 1）/ Queue capacity (1 for a plain workbench)
	UPROPERTY(EditAnywhere, Category = "Production") int32 Val_QueueCapacity = 1;

	// 排队中的每一项完成时都会播一次反馈 / Each completed queue item fires its own feedback
	void OnQueueItemFinished(int32 RecipeId);
};
