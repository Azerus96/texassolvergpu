# CPP_CORE_RECON.md
# TexasSolverGPU v0.3.0 — C++ Core Data Structures Reconstruction

**Source:** `strings` + `objdump -d` on the PE binaries, cross-referenced with the embedded minified JS state machine (`yo`, `Wp`, `ai`, `oi`, snapshot builders). The JS objects mirror the C++ `GameState` / `Node` structs passed between the Vue frontend and the native GPU solver.

> NOTE: The PE is stripped; `objdump -d` gives raw x86-64 disassembly without RTTI/debug info. Class layouts below are reconstructed from field access patterns in the JS (e.g. `e.paid[a]`, `e.ranges[a][E]`, `e.node.childrens[t]`, `snapshot.ranges`, `snapshot.probabilities`) and from the API request/response schemas in FRONTEND_API_RECON.md.

---

## 1. Card / combo primitives (shared with CUDA)

```cpp
constexpr int NUM_CARDS   = 52;   // 0..51 ; rank=c/4, suit=c%4
constexpr int NUM_COMBOS  = 1326; // C(52,2) preflop starting hands
constexpr int NUM_HCLASSES= 169;  // 13 pairs + 78 suited + 78 offsuit

// dense combo index for two cards c1<c2
inline int comboIndex(int c1, int c2) {
    if (c1 > c2) std::swap(c1, c2);
    return c1 * (103 - c1) / 2 + c2 - c1 - 1;   // [0,1325]
}

// packed two cards:  c | (p << 8)
```

---

## 2. GameState

Reconstructed from `yo(seats, stack, ante, s)` — the preflop root state — and mutated by `Wp(state, action)` (6-max) / `SB` (HU). One `GameState` exists per solver seat perspective.

```cpp
struct GameState {
    // ---- table / seats ----
    int            numSeats;          // 2 (HU) or 6 (6-max)
    int            seats[6];          // seat ids, e.g. {UTG,MP,CO,BTN,SB,BB}
    int            stack;             // effective stack in chips (e.g. 10000)
    int            ante;              // ante per player
    int            postflopOrder[6];  // postflop action order (HU:[1,0], 3:[1,2,0], 6:[4,5,0,1,2,3])

    // ---- preflop posting ----
    int            wager[6];          // amount wagered this street
    int            paid[6];           // total chips paid into pot (ante + wager)
    bool           folded[6];         // folded flag
    bool           pending[6];        // still to act this street
    bool           canRaise[6];       // can still raise (reopened)
    int            actor;             // seat whose turn it is
    int            lastRaise;         // last raise size (for min-raise rule)
    std::vector<std::string> history; // action history, e.g. "0:C/1:R100"

    // ---- ranges ----
    float          ranges[6][NUM_COMBOS];   // per-seat normalized combo weights [0..1]

    // ---- street phase ----
    // "preflop" | "flop" | "multiway" | "showdown" | "fold"
    char           phase[16];

    // derived / computed
    float          pot;               // sum(paid)
};
```

Construction defaults (`yo`):
- `wager[SB]=50, wager[BB]=100` (HU) / `paid=[ante+wager]`
- `ranges[seat][*] = 1` (all combos weight 1, normalized to sum 1)
- `phase = "preflop"`, `actor = 0`, `lastRaise = 100`

---

## 3. Node (game-tree node)

Reconstructed from the policy JSON shape (`{type, player, valid_actions, strategy, betting_round, table_cards, pot, childrens}`) and `Ma(policy, state)` lookup via `si(state) = history.join("/")`.

```cpp
struct Strategy {
    std::vector<std::string> card_strings;   // e.g. "AhKs"
    std::vector<std::vector<float>> strategy_probs;   // per combo, per action
    std::vector<float> reach_probs;          // optional
};

struct Node {
    int            actor;            // seat to act
    int            player;           // (HU) 0 or 1
    int            bettingRound;     // 0=preflop
    std::string    tableCards;       // board text ""
    double         pot;
    std::vector<std::string> valid_actions;  // ["Fold","Call","Raise 100",...]
    Strategy*      strategy;         // preflop fixed policy (may be null)
    Node*          comboNode;        // alternate combo-node strategy
    std::vector<Node*> childrens;    // one child per valid_action
    int            type;             // "action" | "chance"
};
```

Tree lookup key: `si(state) = state.history joined by "/"`. Root key = `"root"`. `Ma(policy, state)` returns `policy.nodes[si(state)]`; throws if missing or `actor` mismatch.

Action application (`Wp` / `SB`):
- Fold → `phase="fold"`, `folded[actor]=true`
- Call → match `paid[actor]` to opponent's paid
- Check → requires `paid[0]==paid[1]`
- All-In → `paid[actor] = stack`
- Raise N → `paid[actor] = N`, update `lastRaise`, re-open `canRaise` for others
- Street end → if all paid equal → `phase = "flop"`/`"multiway"`/`"showdown"`; if one remains → `phase="fold"`

---

## 4. Solver snapshot (results) — `snapshot`

Returned by `GET /api/results` and `POST /api/export/current-street`. This is the structure the GPU kernels write into.

```cpp
struct NodeSnapshot {
    int            seat;             // acting seat (hero perspective)
    std::vector<std::string> seatNames;   // ["SB / BTN","BB"] / 6 names
    std::vector<int>   board;        // card indices
    std::vector<int>   paid;         // per-seat chips
    int            pot;              // sum(paid)
    std::vector<std::string> labels;        // action labels at this node
    // per-seat range (Float32Array length 1326)
    std::vector<float> ranges[6];      // ranges[seat][combo]
    // representative cards (Uint16Array)
    std::vector<int>   cards;        // packed c | (p<<8)
    // strategy probabilities: [numActions x 1326] flattened row-major
    std::vector<float> probabilities;  // probabilities[action*1326 + combo]
    // per-action EV in BB (divided by 100); may be null
    std::vector<float> evs;            // evs[action*1326 + combo] or empty
};
```

Builder `oi(state, node)` fills this from `Ti(node)` (per-combo probabilities) and `Yn(node)` (preflop strategy map). `IB(snapshot)` computes total serialized bytes:
```
bytes = sum(ranges[seat].byteLength) + cards.byteLength
      + probabilities.byteLength + (evs ? evs.byteLength : 0)
```

---

## 5. Action / decision record

```cpp
struct Decision {
    int    seat;
    std::string label;     // "Fold"/"Call"/"Check"/"All-In"/"Raise N"
    int    street;         // 0=preflop, else board length
    NodeSnapshot* snapshot; // state snapshot before this action
    int    selected;       // chosen action index (for drills/review)
    int    rngRoll;        // [0,100) RNG roll for balanced GTO sampling
    // advice (computed by QA)
    int    recommended;    // best action index
    float  evLossBB;       // EV loss of chosen vs best (BB)
    std::string verdict;   // "best"/"mixed"/"inaccuracy"/"mistake"/"blunder"
};
```

---

## 6. Solve parameters (send to native backend via POST /api/init)

```cpp
struct SolveConfig {
    // ranges (normalized to sum 1 over 1326)
    float oop_range[NUM_COMBOS];
    float ip_range [NUM_COMBOS];
    std::vector<int> board;          // 3-5 card indices
    int   starting_pot;              // chips
    int   effective_stack;           // chips
    float rake_rate;                 // fraction
    float rake_cap;                  // chips
    // bet sizes (comma-separated % pot, e.g. "33,67,100")
    std::string oop_flop_bet, oop_flop_raise;
    std::string ip_flop_bet,  ip_flop_raise;
    std::string oop_turn_bet, oop_turn_raise;
    std::string ip_turn_bet,  ip_turn_raise;
    std::string oop_river_bet, oop_river_raise;
    std::string ip_river_bet,  ip_river_raise;
    bool    donk_option;
    std::string oop_turn_donk, oop_river_donk;
    int     max_raise_number;        // 0..255 (default 3)
    float   add_allin_threshold;     // fraction
    float   force_allin_threshold;   // 0.2
    bool    add_allin_flop_ip, add_allin_turn_ip, add_allin_river_ip;
    bool    add_allin_flop_oop, add_allin_turn_oop, add_allin_river_oop;
    float   merging_threshold;       // 0.1
    int     num_players;             // 2
};
```

---

## 7. Solve request / status (wire)

```cpp
// POST /api/gpu-solve  (solver.solve.start)
struct SolveRequest {
    int              max_iterations;       // >=1 (default 1000)
    double           target_exploitability;// percent (default 0.5)
    bool             compute_initial_exploitability; // default false
};

// GET /api/solve/status  (solver.solve.status)
struct SolveStatus {
    bool     running;
    int      iteration;
    int      elapsed_ms;
    double   exploitability;        // percent, may be absent
    std::string phase;              // allocating|initializing|solving|finalizing|
                                    // stopping|finished|stopped|error|failed
    std::string last_error;
};
```

---

## 8. Memory layout notes (from objdump/strings)

- `NUM_COMBOS = 1326` range arrays are contiguous `float[1326]` (4KB each) — matches GPU `ranges` buffers.
- `probabilities` is flattened `[numActions x 1326]` row-major — directly mappable to a CUDA 2D grid (action × combo).
- `cards` as `Uint16Array` packed `c | (p<<8)` — matches CUDA `unpack2`.
- The `Node` tree is keyed by history string; `childrens` array is parallel to `valid_actions`.
- The snapshot is the structure serialized to JSON and persisted as `snapshotJsonText` in batch jobs (`Er` persistence size limit, `exportMaxNodes` cap default 20000).

---

## 9. Field access summary (JS → C++ mapping)

| JS expression | C++ member |
|---------------|------------|
| `e.paid[a]`, `e.wager[s]` | `GameState::paid[]`, `::wager[]` |
| `e.ranges[a][E]` | `GameState::ranges[seat][combo]` |
| `e.node.childrens[t]` | `Node::childrens[actionIdx]` |
| `e.node.valid_actions` | `Node::valid_actions` |
| `e.node.strategy.strategy_probs[c]` | `Node::Strategy::strategy_probs[combo]` |
| `snapshot.ranges[seat]` | `NodeSnapshot::ranges[seat]` |
| `snapshot.probabilities[w*a.length+i]` | `NodeSnapshot::probabilities[action*1326+combo]` |
| `snapshot.evs[w*a.length+i]` | `NodeSnapshot::evs[action*1326+combo]` |
| `snapshot.cards` | `NodeSnapshot::cards` (packed `c\|p<<8`) |
| `si(e) = e.history.join("/")` | tree lookup key |
