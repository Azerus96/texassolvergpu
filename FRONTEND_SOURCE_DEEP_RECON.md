# TexasSolverGPU v0.3.0 — Frontend Source Code Deep Recon

**Source:** `all_strings.txt` (embedded Vue 3 frontend, 32,289 lines)
**Target:** `TexasSolverGpu_131.exe` (WebView2 embedded frontend at `http://127.0.0.1:6006`)

---

## 1. Frontend Architecture

- **Framework:** Vue 3 (Composition API, `<script setup>`)
- **Transport:** WebView2 `window.chrome.webview.postMessage(JSON.stringify({id, method, params}))`
- **API Base:** `http://127.0.0.1:6006` (overridable via `?apiBase=`)
- **Transport modes:** `bridge` (default, WebView2 postMessage) or `http` (via `?transport=`)
- **Auth:** `bridge.ping` (1500 ms timeout) → `bridge_token`; retry on `UNAUTHORIZED`
- **RPC envelope:** `{ id: "rpc_<ts>_<seq>", method, params }`
- **Response envelope:** `{ ok: bool, result: any, error: {message, code} }`
- **Persistence:** IndexedDB database `texassolver-gpu` (version 1), object stores `batch_manifests` and `batch_results`

---

## 2. Vue 3 Components

| Component | Role |
|-----------|------|
| `PlayPanel` (`ya` class) | Main solver controller — complete solve flow |
| `RunSolverPanel` | Solver run control (iterations, target exploitability, VRAM preflight) |
| `BatchSolverPanel` | Batch solver for multiple boards (1-25 boards) |
| `QuickStart` | Preflop scenario picker and quick solve |
| `StrategyMatrix` | 13x13 hand grid visualization with strategy/EV heatmap |
| `StrategyInspector` | Detailed strategy view for a specific seat/hand |
| `PlayHandFeedback` | GTO vs off-policy decision feedback |
| `PlayHandTransition` | Hand-to-hand transition with auto-deal countdown |
| `PracticeDrills` | Practice drill state management (localStorage: `texassolver.practiceDrills.v1`) |

---

## 3. PlayPanel (`ya` class) — Complete Solve Flow

### Constructor State
```js
constructor(apiBase) {
    this.apiBase = apiBase;           // e.g. "http://127.0.0.1:6006" or "bridge"
    this.mayBeRunning = false;        // solver busy flag
    this.cards = [[], []];            // per-seat private cards
    this.payload = null;              // last init payload
    this.entryPaid = null;            // per-seat entry paid
}
```

### Solve Flow (`ya.prepare`)
1. `ensureIdle()` → `GET /api/solve/status` — throws if solver running or in busy phase
2. Validate postflop contribution base (`paid[0]==paid[1]`, sum <= starting_pot)
3. `POST /api/init` (`solver.init`) — init template with ranges, board, pot, stack, rake, bet sizes
4. `POST /api/allocate` (`solver.allocate`) — allocate GPU memory
5. `GET /api/environment/check` (`solver.environment.check`) — preflight VRAM check
6. If severity == "critical" → throw "Not enough GPU memory"
7. `mayBeRunning = true`
8. `POST /api/gpu-solve` (`solver.solve.start`) — `{max_iterations, target_exploitability, compute_initial_exploitability: false}`
9. Poll `GET /api/solve/status` every 250ms until `running == false`
10. `GET /api/results` (`solver.node.results`) for seats 0 and 1
11. Returns `{cards, status}`

### Direct Native Calls (bypassing HTTP)
- `solver.policy(data, cards, num_actions, options)` → `{probs: Float32Array[1326], evs: Float32Array|null}`
- `solver.ensureIdle()`, `solver.stop()`, `solver.confirmStopped()`
- `solver.node(history)`, `solver.possibleCards()`

---

## 4. API Endpoint Map (from dT table + frontend)

### Solver Endpoints (38 methods via dT table)

| Method | Endpoint | Timeout | Description |
|--------|----------|---------|-------------|
| `solver.init` | POST /api/init | 120000 ms | Initialize solver with ranges, board, pot, stack |
| `solver.allocate` | POST /api/allocate | 120000 ms | Allocate GPU memory |
| `solver.memoryUsage` | GET /api/memory-usage | 30000 ms | Query GPU memory usage |
| `solver.environment.check` | GET /api/environment/check | 5000 ms | Preflight VRAM/CUDA check |
| `solver.solve.start` | POST /api/gpu-solve | 30000 ms | Start CFR training |
| `solver.solve.preflight` | POST /api/solve/preflight | 30000 ms | Preflight check |
| `solver.solve.status` | GET /api/solve/status | 5000 ms | Poll solver status |
| `solver.solve.stop` | POST /api/solve/stop | 5000 ms | Stop solver |
| `solver.solve.dispatch` | — | — | Internal dispatch |
| `solver.solve.2p.construct` | — | — | Construct 2-player solver |
| `solver.solve.2p.run` | — | — | Run 2-player solver |
| `solver.solve.3p.construct` | — | — | Construct 3-player solver |
| `solver.solve.3p.run` | — | — | Run 3-player solver |
| `solver.solve.cpu_locked` | — | — | CPU-locked solve |
| `solver.solve.cpu_locked.finalize` | — | — | CPU-locked finalize |
| `solver.finalize.compute` | — | — | Compute finalize |
| `solver.finalize.publish` | — | — | Publish finalize |
| `solver.host.allocate` | — | — | Host memory allocation |
| `solver.lock` | — | — | Lock query |
| `solver.lock.current.set` | POST /api/lock/current | 5000 ms | Set current lock |
| `solver.lock.current.get` | GET /api/lock/current | 5000 ms | Get current lock |
| `solver.lock.current.clear` | POST /api/lock/unlock | 5000 ms | Clear current lock |
| `solver.lock.list` | GET /api/lock/list | 5000 ms | List locks |
| `solver.lock.clearAll` | POST /api/lock/clear | 5000 ms | Clear all locks |
| `solver.node.results` | GET /api/results | 5000 ms | Get node results |
| `solver.node.actionsAfter` | POST /api/actions-after | 5000 ms | Get actions after history |
| `solver.node.currentPlayer` | GET /api/current-player | 5000 ms | Get current player |
| `solver.node.currentBoard` | GET /api/current-board | 5000 ms | Get current board |
| `solver.node.numActions` | GET /api/num-actions | 5000 ms | Get number of actions |
| `solver.cards.possible` | GET /api/possible-cards | 5000 ms | Get possible cards bitmask |
| `solver.cards.private` | GET /api/private-cards/<n> | 5000 ms | Get private cards |
| `solver.history.apply` | POST /api/apply-history | 5000 ms | Apply history |
| `solver.memoryUsage` | GET /api/memory-usage | 30000 ms | Memory usage |
| `solver.export.currentStreet` | POST /api/export/current-street | 120000 ms | Export current street |

### Desktop Endpoints
| Method | Endpoint | Timeout |
|--------|----------|---------|
| `desktop.parameters.list` | GET /api/parameters/list?path= | 5000 ms |
| `desktop.parameters.export` | POST /api/parameters/export | 5000 ms |
| `desktop.parameters.openFolder` | POST /api/parameters/open-folder | 5000 ms |
| `desktop.parameters.load` | POST /api/parameters/load | 5000 ms |
| `desktop.ranges.list` | GET /api/ranges/list?path= | 5000 ms |
| `desktop.ranges.load` | POST /api/ranges/load | 5000 ms |
| `desktop.settings.load` | POST /api/settings/load | 5000 ms |
| `desktop.settings.save` | POST /api/settings/save | 5000 ms |
| `desktop.external.open` | POST /api/external/open | 5000 ms |
| `desktop.strategy.exportCurrentStreetFile` | POST /api/export/current-street-file | null |
| `desktop.strategy.exportFullFile` | POST /api/export/full-file | null |
| `desktop.strategy.exportFullFileProgress` | GET /api/export/full-file/progress | null |
| `desktop.strategy.importFullFile` | POST /api/import/full-file | null |
| `desktop.strategy.importNode` | POST /api/import/node | 30000 ms |
| `desktop.strategy.closeImport` | POST /api/import/close | null |

### Strategy Archive Formats
- **AVRO** (recommended) — compact binary format with Q16 encoding
- **JSONL** — JSON lines format
- **JSON** — plain JSON format

---

## 5. Init Template Schema (`_e()` function)

```json
{
  "oop_range": [float x1326],           // default: all 1.0
  "ip_range": [float x1326],            // default: all 1.0
  "board": [int x3],                    // flop board card indices (0-51)
  "starting_pot": int,                  // must be positive integer
  "effective_stack": int,               // must be positive integer
  "rake_rate": float,                   // 0.0 to 1.0 (percent / 100)
  "rake_cap": float,                    // non-negative
  "oop_flop_bet": "33%,67%,100%",       // comma-separated bet sizes
  "oop_flop_raise": "33%,67%,100%",
  "ip_flop_bet": "33%,67%,100%",
  "ip_flop_raise": "33%,67%,100%",
  "oop_turn_bet": "100%",
  "oop_turn_raise": "100%",
  "ip_turn_bet": "100%",
  "ip_turn_raise": "100%",
  "oop_river_bet": "100%",
  "oop_river_raise": "100%",
  "ip_river_bet": "100%",
  "ip_river_raise": "100%",
  "donk_option": true,
  "oop_turn_donk": "100%",
  "oop_river_donk": "100%",
  "max_raise_number": 3,               // 0 to 255
  "add_allin_threshold": 1.0,          // percent (0-10 → *100)
  "force_allin_threshold": 0.2,
  "add_allin_flop_ip": true,
  "add_allin_turn_ip": true,
  "add_allin_river_ip": true,
  "add_allin_flop_oop": true,
  "add_allin_turn_oop": true,
  "add_allin_river_oop": true,
  "merging_threshold": 0.1,
  "num_players": 2
}
```

**Validation:**
- `oop_range` and `ip_range` must be arrays of length 1326, values in [0,1]
- `starting_pot` and `effective_stack` must be positive integers
- `rake_rate` in [0,1], `rake_cap` >= 0
- `max_raise_number` integer in [0,255]
- `add_allin_threshold`: if <= 10, multiplied by 100 (converts bb to cents)
- `board` must be 3-5 unique card indices in [0,51]

---

## 6. Solve Status Schema (`GET /api/solve/status`)

```json
{
  "running": bool,
  "iteration": int,
  "max_iterations": int,
  "exploitability": float | null,
  "exploitability_iteration": int,
  "exploitability_elapsed_ms": int,
  "player0_exploitability": float | null,
  "player1_exploitability": float | null,
  "elapsed_ms": int,
  "stop_requested": bool,
  "phase": "preparing|allocating|initializing|solving|finalizing|stopping|finished|stopped|error|failed|idle",
  "last_error": string | null
}
```

**Phases:** `allocating`, `initializing`, `solving`, `finalizing`, `stopping`, `finished`, `stopped`, `error`, `failed`

**Poll interval:** 250ms (frontend), 200ms (RunSolverPanel status check)

---

## 7. Results Schema (`GET /api/results`)

```json
{
  "seat": int,
  "seatNames": ["SB / BTN", "BB"],
  "board": [int],
  "paid": [int x num_players],
  "pot": int,
  "labels": ["Fold", "Call", "Check", "Raise 100", ...],
  "ranges": [ [float x1326], [float x1326] ],
  "cards": [int],
  "probabilities": [float],         // [num_actions x 1326] flattened
  "evs": [float | null]             // per-action EV in cents/100
}
```

**EV scaling:** EV values are in cents/100 — divide by 100 to get BB.

---

## 8. Strategy Matrix Visualization

The `StrategyMatrix` component renders a 13x13 hand grid (A-K-Q-J-T-9-8-7-6-5-4-3-2 axes):

- **Hand classification:** pairs (e.g. "AA"), suited (e.g. "AKs"), offsuit (e.g. "AKo")
- **Combo weights:** pairs=6 combos, suited=4 combos, offsuit=12 combos
- **Normalization:** reach values normalized by max across all hands
- **Color scheme:**
  - Strategy mode: linear-gradient background by action probability
  - EV mode: red (negative EV) to green (positive EV) diverging colormap
- **Cell interaction:** hover/click to show exact strategy, keyboard navigation (Enter/Space)

---

## 9. Nodelock Designer

The nodelock feature allows users to override solver strategy with custom biases:

### Bias Modes
| Mode | Scope |
|------|-------|
| `global` | Apply bias to all nodes |
| `hand` | Apply bias to current hand only |
| `cell` | Apply bias to specific hand cell |
| `bias` | Per-action bias (fold, call/check, raise, allin) |

### Bias Application
- Sliders for each action (0-100%)
- "Normalize" button to make probabilities sum to 100%
- "Pure action" presets (100% single action)
- "Apply to all" / "Apply to category" / "Apply to cell" / "Apply to hand"
- Advanced mode: text editor for raw nodelock JSON

### Nodelock States
- `locked_nodes`: count of locked nodes
- `nodelock_vector_len`: length of bias vector
- `imported` / `read_only` flags for imported archives

---

## 10. Practice Drills

### Storage
- **localStorage key:** `texassolver.practiceDrills.v1`
- **Structure:** Array of drill objects with `id`, `spots`, `filters`

### Scoring
- **Verdicts:** `best`, `mixed`, `inaccuracy`, `mistake`, `blunder`
- **EV loss threshold:** 0.01 BB for "mistake", 0.1 BB for "blunder"
- **Match tracking:** `matched`, `deviated`, `unknown` decisions
- **Close only:** Skip decisions where top-2 EV gap > `closeEvBB` (default 0.05 BB)

### Hand Review
- Shows hero hand, villain hand, board, pot, positions
- Action-by-action replay with feedback
- "Next hand" / "Auto-deal after countdown" timer

---

## 11. Batch Solver

### Configuration
- **Max boards:** 25 (`batch.max_25_error` if exceeded)
- **Random boards:** 1-25 (`batch.randomCount`, default 5)
- **Export max nodes:** 100-100000 per export
- **Persistence limit:** 64 MB per batch (`Er = 64 * 1024 * 1024`)
- **Persistence warning:** 128 MB (`h$ = 128 * 1024 * 1024`)

### Validation
- `batch.invalid_board_len`: board must be 3-5 cards
- `batch.duplicate_card_error`: no duplicate cards
- `board.must contain 3-5 unique card indices in [0,51]`

### IndexedDB Persistence
- **Database:** `texassolver-gpu` (version 1)
- **Object stores:** `batch_manifests` (key: "active"), `batch_results` (keyPath: "key")
- **Schema version:** `jp` (must match)
- **Batch lock:** `navigator.locks.request("texassolver-gpu-batch-persistence", {mode: "exclusive"})`
- **Storage estimate:** `navigator.storage.estimate()` — checks available space >= needed + 128 MB

### Batch Flow (per board)
1. `POST /api/init` → `POST /api/allocate` → `POST /api/gpu-solve`
2. Poll `GET /api/solve/status` every 250ms
3. `POST /api/solve/stop`
4. `GET /api/results` (seats 0 and 1)
5. `POST /api/export/current-street` with `{history: [], max_nodes}`
6. If "node limit exceeded" → retry with max_nodes=100000
7. Persist snapshot JSON to IndexedDB

---

## 12. Quick Start System

### Preset Formats
| Key | Name | Stack | Ante |
|-----|------|-------|------|
| `six` | Cash 6max 100bb | 10000 | 0 |
| `six50` | Cash 6max 50bb | 5000 | 0 |
| `sixAnte` | Cash 6max Ante 50bb | 10000 | 16 (0.16bb) |
| `spin2` | Spin & Go HU | variable | 0 |
| `spin3` | Spin & Go 3-max | variable | 0 |
| `hu` | HU Cash | 100bb | 0 |

### Scenario Loading
- Loads range files from `ranges/6max_range/<position>/<stack>/.../<position>_range.txt`
- Two range files required per scenario (OOP and IP positions)
- Computes flop pot and effective stack from range file path
- Chip scaling: scales pot/stack/rake_cap to common denominator

### Board Selection
- **RANDOM:** Random board generation with texture filtering
- **RANDOM_TYPE:** Random by texture type (RAINBOW, TWO_TONE, MONOTONE, LOW_CONNECTED, HIGH_DRY, ANY)
- **Manual:** Click on board preview cards

---

## 13. VRAM Preflight (`solver.environment.check`)

### Severity Levels
- `null` — OK
- `warning` — VRAM below recommended
- `critical` — Not enough GPU memory (throws error)

### UI States
- `vram-checking` — checking in progress
- `vram-preflight-critical` — critical warning
- `vram-preflight-warning` — warning
- Buttons: "Re-check", "Run Anyway", "Cancel"

### Logging
- `run.vram_log` — NVML source: estimated peak, free, headroom (GiB)
- `run.vram_log_fallback` — non-NVML source: estimated peak, available (GiB)

---

## 14. Chip Denominations

```js
[2500, 500, 100, 25, 5, 1]  // in bb
```

Colors:
| Denomination | Face | Edge | Ink |
|-------------|------|------|-----|
| 2500 | #56cda0 | #21644e | #073a30 |
| 500 | #ef8188 | #803843 | #4e1824 |
| 100 | #f2ecda | #837d6f | #233644 |
| 25 | #76c7ed | #326989 | #113d56 |
| 5 | #bb9dec | #655184 | #3b2459 |
| 1 | #d8a97a | #765136 | #452a19 |

---

## 15. Card Encoding (Frontend)

```js
// Suit mapping: 0=club, 1=diamond, 2=heart, 3=spade (four-color deck)
// Rank mapping: 0=2, 1=3, ..., 12=A

function zt(cardIndex) {
    const rank = "23456789TJQKA"[Math.floor(cardIndex / 4)];
    const suit = "cdhs"[cardIndex % 4];
    return rank + suit;
}

// Combo index: en(e,t) = e*(103-e)/2 + t - e - 1 for e < t, range [0,1325]
// Two cards packed: c | (p << 8)
```

---

## 16. Key Source File Paths (from binary strings)

| Path | Role |
|------|------|
| `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\gpu_solver.cpp` | Main GPU solver |
| `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\gpu_solver_3p.cpp` | 3-player GPU solver |
| `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\src\cuda_driver_api.cpp` | CUDA driver API wrapper |
| `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\include\poker\cuda_buffer.h` | CUDA buffer header |
| `C:\Users\bupticybee\projects\gpu_solver\solver-cpp\include\poker\cuda_host_memory.h` | CUDA host memory header |

**Author:** bupticybee
**Discord:** https://discord.gg/RtyD4vRy2e
**GitHub:** https://github.com/bupticybee
**Page:** https://bupticybee.github.io/texassolver_gpu_page/
