# Data-Oriented Building System

A reference design for an **open-world building system** where players place a very large
number of persistent objects, and where a per-object `Actor` simply does not scale. The core
idea: represent building pieces as **lightweight entities composed of fragments** (an
ECS/Mass-style model), render them **instanced**, replicate them with a **delta channel**,
and only promote an entity to a real `Actor` **on demand** from a shared pool.

> **About this repository.** Original *clean-room* write-up of an architecture I designed and
> owned in a commercial title. **No proprietary source** — the code below is illustrative
> reference code written to explain the approach and the trade-offs.

---

## 1. Problem

Players build homes freely; buildings persist **permanently** and a single base can hold
**thousands** of pieces. One `AActor` per piece blows up on three axes at once:

- **CPU/memory** — thousands of actors, components, and tick registrations;
- **network** — thousands of replicated actors saturating the channel;
- **rendering** — thousands of draw calls.

The system also had to survive the core building *rules* being redesigned several times, so
the architecture had to be reshapeable without rewrites.

---

## 2. Architecture — entities, not actors

```
   Persistence (DB)  <--dump/load-->  Entity Store (fragments)  <--delta-->  Clients
                                            |         |
                              +-------------+         +--------------+
                              |                                      |
                     Representation                            Proxy Pool
                     (Instanced Static Mesh)             (shared Actors, on demand)
                              |                                      |
                     one draw batch per mesh              only near/interacted pieces
                                                          get a real Actor
```

- **Entity = a set of fragments.** A building piece is data: a handful of small `struct`
  fragments (transform, health, type, support, coating…). No actor, no components, until it
  needs one.
- **Representation layer** draws pieces as **Instanced Static Meshes** — thousands of walls =
  a few draw batches, not thousands.
- **Proxy pool** hands out a small number of shared `Actor` "proxies" only to pieces the
  player is near or interacting with (collision, interaction, UI). Everything else stays pure
  data.
- **Persistence** dumps/loads entities to a compact record with **version numbers** for
  incremental saving.

This is the heart of the optimization story: **the number of live Actors is decoupled from
the number of buildings.**

---

## 3. Fragment composition + replicated/transient split

The design decision that keeps memory tight and iteration fast: **only data that must sync
lives in the replicated agent; everything else lives in a transient side-struct held by
pointer**, so the replicated array stays small and cache-friendly to iterate.

```cpp
// --- Small composable fragments: a piece "is" whatever fragments it carries. ---
USTRUCT() struct FTransformFragment { GENERATED_BODY() FVector Location; FQuat Rotation; };
USTRUCT() struct FHealthFragment    { GENERATED_BODY() float Current = 0.f; float Max = 0.f; };
USTRUCT() struct FTypeFragment      { GENERATED_BODY() int32 ConfigId = 0; };
USTRUCT() struct FSupportFragment   { GENERATED_BODY() float SupportValue = 0.f; };

// --- The replicated agent: ONLY what must go over the wire. Kept deliberately compact
//     so the fast-array of these iterates fast and costs little bandwidth. ---
USTRUCT()
struct FBuildingAgent : public FReplicatedAgentBase
{
    GENERATED_BODY()

    UPROPERTY() FTransformFragment Transform;
    UPROPERTY() FHealthFragment    Health;
    UPROPERTY() FTypeFragment      Type;
    UPROPERTY() FSupportFragment   Support;

    // Non-replicated, per-instance runtime state lives OFF the hot array, behind a pointer,
    // so copying agents around the fast-array never drags this along.
    TSharedPtr<FBuildingTransient> Transient;
};
```

**Why the split:** the replicated array is walked every frame for representation and
replication. Keeping cold data (asset handles, bound proxy, editor/selection state) out of it
made the hot path smaller and faster, and cut per-object memory.

---

## 4. Delta replication with throttling + weak-net adaptation

Even as data, thousands of pieces can't all sync at once. I replicate through a **fast-array
delta channel** with a **per-frame change/delete budget** that *shrinks under packet loss* so
the channel degrades gracefully.

```cpp
int UBuildingClientBubble::GetChangesBudgetThisFrame() const
{
    // Under good conditions push a healthy batch; under detected loss, throttle hard
    // so we stop making congestion worse.
    return bBadNetwork ? MaxChangesPerUpdate_BadNet   // e.g. 5
                       : MaxChangesPerUpdate;         // e.g. 50
}

void UBuildingClientBubble::ServerTick()
{
    int Budget = GetChangesBudgetThisFrame();
    for (FMassEntityHandle Entity : DirtyEntities)   // dirty set, priority-ordered
    {
        if (Budget-- <= 0) break;                    // rest waits for next tick
        MarkAgentDirtyForReplication(Entity);
    }
    // Deletes have their own (larger) budget — cheap to send, important to apply promptly.
}
```

---

## 5. On-demand actors from a shared proxy pool

Interaction, collision, and UI still need a real `Actor` — but only for the handful of pieces
the player is actually near. A subsystem lends **shared** proxy actors and reclaims them.

```cpp
// Bind an entity to a pooled Actor when it comes into interaction range; reclaim on exit.
FProxyHandle UProxyPool::Acquire(FMassEntityHandle Entity, FName Reason)
{
    AProxyActor* Proxy = FreeList.Num() ? FreeList.Pop()
                       : (LiveCount < MaxProxies ? SpawnNewProxy() : nullptr);
    if (!Proxy) return {};                     // pool exhausted: stays pure data, no actor

    Proxy->BindToEntity(Entity);               // pull transform/mesh/collision from fragments
    Bindings.Add(Entity, { Proxy, /*lock*/1 });
    return MakeHandle(Entity);
}

void UProxyPool::Release(FProxyHandle Handle, FName Reason)
{
    if (FBinding* B = Bindings.Find(Handle.Entity); B && --B->LockCount == 0)
    {
        B->Proxy->UnbindFromEntity();
        FreeList.Push(B->Proxy);               // returned to the pool, not destroyed
        Bindings.Remove(Handle.Entity);
    }
}
```

`MaxProxies` caps the number of real actors regardless of how many thousands of buildings
exist — the whole point.

---

## 6. Modularity workflow (why the team could scale on it)

Building *behaviours* are small, composable pieces at three levels, so new objects are
assembled, not coded from scratch:

1. **Fragments** — data a piece carries (support, coating, produce state…).
2. **Components** — pluggable behaviours (snapping, interaction, level-up, production…).
3. **Entity types** — a door, a furnace, an incubator = a specific combination of the above.

This is the open/closed principle as a *workflow*: adding a new building object touches no
framework code, which is what let other engineers and designers add a large variety of
objects efficiently.

---

## 7. Results

- **Actor count decoupled from building count** — sync and rendering pressure dropped sharply
  because most pieces are data + instanced meshes, not actors.
- The system **survived multiple redesigns of the building rules** without foundational
  rewrites, thanks to fragment/component composition.
- The modular workflow let the team **keep shipping new building objects and features**
  throughout the project.

## 8. What this demonstrates

Data-oriented / ECS thinking applied to a real scaling problem; comfort across replication,
rendering (instancing), memory layout, and pooling; and architecture designed for **constant
change** and for **other people to extend**.
