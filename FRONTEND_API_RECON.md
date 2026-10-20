# FRONTEND_API_RECON.md
# TexasSolverGPU v0.3.0 — Frontend & API Reconstruction

**Source:** `../target_bin/TexasSolverGPU-0.3.0-win64/TexasSolverGpu_131.exe` (and `legacy_126`, `TexasSolverGpu` loader).
**Method:** Minified Vue/Vite JavaScript embedded as printable strings inside the Win64 PE executables. Recovered via `strings -n 4`; the routing dispatcher `dT(method,path)` and the request/response builders were extracted directly.

> NOTE: There are no `.js`, `.pak`, or `.asar` files on disk. The frontend is a bundled Vue 3 app (Vite) compiled into the PE binary as string data, executed inside a WebView2 (`window.chrome.webview`). Communication with the native solver host uses either the **native bridge** (`window.chrome.webview.postMessage`, RPC id `rpc_<ts>_<seq>`) or a fallback **HTTP.request** bridge. The API base defaults to `http://127.0.0.1:6006`.

---

## 1. Transport & Auth

| Item | Detail |
|------|--------|
| API base | `http://127.0.0.1:6006` ( overridable via `?apiBase=` ) |
| Transport | `bridge` (default) or `http` (via `?transport=`) |
| Bridge | `window.chrome.webview.postMessage(JSON.stringify({id,method,params}))` |
| Auth | `bridge.ping` → `bridge_token`; sent as `bridge_token` param; retry on `UNAUTHORIZED` |
| RPC envelope | `{ id: "rpc_<ts>_<seq>", method, params }` |
| Response envelope | `{ ok: bool, result: any, error: {message, code} }` |
| Timeouts | `solver.init`/`solver.allocate`/`solver.memoryUsage`/`solver.export.currentStreet` = 120s; `solver.solve.start`/`solver.solve.preflight`/`desktop.ranges.load`/`desktop.strategy.importNode` = 30s; default 5s |

---

## 2. Endpoint Map (dispatcher `dT`)

| Method | Path | Internal method name | Notes |
|--------|------|----------------------|-------|
| POST | `/api/init` | `solver.init` | Builds & uploads the solve init template (ranges, pot, stack, bet sizes) |
| POST | `/api/allocate` | `solver.allocate` | Allocates GPU solver memory after init |
| GET | `/api/memory-usage` (compress=auto\|true\|false) | `solver.memoryUsage` | GPU memory report |
| GET | `/api/environment/check` | `solver.environment.check` | Driver/GPU/CUDA environment probe |
| POST | `/api/gpu-solve` | `solver.solve.start` | Starts GPU solve (iterations, target exploitability) |
| POST | `/api/solve/preflight` | `solver.solve.preflight` | Preflight check before solve |
| GET | `/api/solve/status` | `solver.solve.status` | Poll: running/iteration/exploitability/phase |
| POST | `/api/solve/stop` | `solver.solve.stop` | Request stop |
| POST | `/api/apply-history` | `solver.history.apply` | Apply an action history to a node |
| GET | `/api/results` | `solver.node.results` | Fetch solved node strategy/EVs |
| POST | `/api/actions-after` | `solver.node.actionsAfter` | Actions available after a node |
| GET | `/api/current-player` | `solver.node.currentPlayer` | Actor to act |
| GET | `/api/current-board` | `solver.node.currentBoard` | Board cards |
| POST | `/api/lock/current` | `solver.lock.current.set` | Set node-lock for next solve |
| GET | `/api/lock/current` | `solver.lock.current.get` | Get current lock |
| POST | `/api/lock/unlock` | `solver.lock.current.clear` | Clear lock |
| GET | `/api/lock/list` | `solver.lock.list` | List locks |
| POST | `/api/lock/clear` | `solver.lock.clearAll` | Clear all locks |
| GET | `/api/possible-cards` | `solver.cards.possible` | Possible hole cards |
| GET | `/api/num-actions` | `solver.node.numActions` | Action count |
| POST | `/api/parameters/export` | `desktop.parameters.export` | Export parameter file |
| POST | `/api/parameters/open-folder` | `desktop.parameters.openFolder` | Open params dir |
| POST | `/api/external/open` | `desktop.external.open` | Open external URL |
| POST | `/api/parameters/load` | `desktop.parameters.load` | Load parameters |
| POST | `/api/ranges/load` | `desktop.ranges.load` | Load range JSON (path in body) |
| POST | `/api/settings/load` | `desktop.settings.load` | Load settings |
| POST | `/api/settings/save` | `desktop.settings.save` | Save settings |
| GET | `/api/parameters/list?path=` | `desktop.parameters.list` | List parameter dir |
| GET | `/api/ranges/list?path=` | `desktop.ranges.list` | List range dir |
| GET | `/api/private-cards/<n>` | `solver.cards.private` | Private cards for seat n |
| POST | `/api/export/current-street` | `solver.export.currentStreet` | Export current street snapshot (`{history, max_nodes}`) |
| POST | `/api/export/current-street-file` | `desktop.strategy.exportCurrentStreetFile` | Export to file |
| POST | `/api/export/full-file` | `desktop.strategy.exportFullFile` | Full strategy export (`parameter_schema_version:2`) |
| GET | `/api/export/full-file/progress` | `desktop.strategy.exportFullFileProgress` | Export progress |
| POST | `/api/import/full-file` | `desktop.strategy.importFullFile` | Import full strategy |
| POST | `/api/import/node` | `desktop.strategy.importNode` | Import single node |
| POST | `/api/import/close` | `desktop.strategy.closeImport` | Close import |

---

## 3. Solve Flow (recovered from `BatchSolverPanel` / `RunSolverPanel`)

```
1. POST /api/init      body = initTemplate  (see §4)
2. POST /api/allocate  body = {}             (allocates GPU memory)
3. POST /api/gpu-solve body = {max_iterations, target_exploitability, compute_initial_exploitability:false}
4. GET  /api/solve/status  (poll)            -> {running, iteration, elapsed_ms, exploitability, phase, last_error}
   repeat until running == false
5. POST /api/solve/stop body = []            (clear/stop)
6. GET  /api/results                            -> node strategy + EVs
7. POST /api/export/current-street body={history:[], max_nodes} -> {payload: snapshot}
```

Per-board loop (batch): for each board → init → allocate → gpu-solve → poll status → stop → results → export current-street → persist snapshot JSON.

---

## 4. JSON Schemas (exact structures recovered)

### 4.1 Default client config (`Ce` — the tree-config defaults)

```json
{
  "oopRange": [1,1,...1326],          // Float32Array length 1326, preflop OOP range weights
  "ipRange":  [1,1,...1326],          // Float32Array length 1326, preflop IP range weights
  "boardText": "Qs Jh 2h",
  "startingPot": 50,
  "effectiveStack": 250,
  "rakeRate": 0,
  "rakeCap": 0,
  "oopFlopBet": "50,100",
  "oopFlopRaise": "50,100",
  "ipFlopBet": "50,100",
  "ipFlopRaise": "50,100",
  "oopTurnBet": "100",
  "oopTurnRaise": "100",
  "ipTurnBet": "100",
  "ipTurnRaise": "100",
  "oopRiverBet": "100",
  "oopRiverRaise": "100",
  "ipRiverBet": "100",
  "ipRiverRaise": "100",
  "oopTurnDonk": "100",
  "oopRiverDonk": "100",
  "maxRaiseNumber": 3,
  "addAllinThreshold": 200,
  "addAllinFlopIp": true,
  "addAllinTurnIp": true,
  "addAllinRiverIp": true,
  "addAllinFlopOop": true,
  "addAllinTurnOop": true,
  "addAllinRiverOop": true
}
```
*Bet sizes are comma-separated percent-of-pot strings, e.g. `"33, 67, 100"` (parsed by `Ts` → `"33%,67%,100%"`). `addAllinThreshold` stored as integer chips, converted `/100` to fraction.*

### 4.2 POST /api/init  — init template (`_e(board)`)

Sent as body of `solver.init`. This is the GPU solver configuration.

```json
{
  "oop_range": [float x1326],            // normalized OOP preflop range (sums to 1)
  "ip_range":  [float x1326],            // normalized IP preflop range
  "board": [int],                        // board card indices (0-51), 3-5 cards
  "starting_pot": int,                   // chips
  "effective_stack": int,                // chips
  "rake_rate": float,                    // fraction
  "rake_cap": float,                     // chips
  "oop_flop_bet": "33%,67%,100%",
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
  "max_raise_number": 3,                 // 0-255
  "add_allin_threshold": 2.0,            // fraction of pot
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

### 4.3 POST /api/gpu-solve  — solve start (`solver.solve.start`)

```json
{
  "max_iterations": int,                 // default 1000, min 1
  "target_exploitability": float,        // percent, default 0.5, min 0
  "compute_initial_exploitability": bool // default false
}
```

### 4.4 GET /api/solve/status  — poll response (`solver.solve.status`)

```json
{
  "running": bool,
  "iteration": int,
  "elapsed_ms": int,
  "exploitability": float | null,        // percent
  "phase": "allocating|initializing|solving|finalizing|stopping|finished|stopped|error|failed",
  "last_error": string | null
}
```

### 4.5 GET /api/results  — node results (`solver.node.results`)

Snapshot returned for the current node:

```json
{
  "seat": int,                           // acting seat
  "seatNames": ["SB / BTN","BB"],        // length num_players
  "board": [int],                        // card indices
  "paid": [int x num_players],           // chips contributed
  "pot": int,                            // sum(paid)
  "labels": ["Fold","Call","Check","Raise 100",...],
  "ranges": [ [float x1326], [float x1326] ],   // per-seat normalized range
  "cards": [int],                        // Uint16Array card indices (seat-specific)
  "probabilities": [float],              // strategy probs: [num_actions x 1326] flattened
  "evs": [float | null]                  // per-action EV in cents/100 (null if not available)
}
```
*EVs are stored as `L/100` (BB units). The `actionsAfter` endpoint returns the labels for the current player.*

### 4.6 POST /api/lock/current  — node lock (`solver.lock.current.set`)

```json
{
  // lock applied to a specific node/hand range; forces the solver to fix strategy
  // for the listed combos on the next solve iteration
  "hand": "AhKs",                        // or combo index
  "values": [float x num_actions],       // locked probabilities
  "weight": float                        // combo weight
}
```
*NodeLock UI supports dump-style: `{hand, weight, values[]}`. `solver.lock.current.get`/`list`/`clear`/`clearAll` manage the lock store.*

### 4.7 POST /api/export/current-street

```json
// request
{ "history": [], "max_nodes": 20000 }

// response
{ "payload": <snapshot JSON> }
```

### 4.8 POST /api/export/full-file

```json
// request body exported by the frontend
{
  "parameter_schema_version": 2,
  "units": { "chip_amounts": "integer_solver_units", "rake_rate": "fraction" },
  "exported_at": "ISO timestamp",
  "config": { ...Ce defaults... },
  "tree_built": bool,
  "solver_status": { phase, iteration, exploitability, ... }
}
```

### 4.9 POST /api/ranges/load

```json
// request
{ "path": "play/spin_go/2p_spin_go_100bb.json" }
// response text: the range policy JSON (type "action", player 0 root, preflop tree)
```

### 4.10 GET /api/private-cards/<seat>

Returns possible hole cards for the given seat (integer seat index).

---

## 5. Internal Solver Methods (native side, not HTTP endpoints)

Recovered from string table — these are the CUDA/GPU solver stage names invoked by the native host:

- `solver.init`, `solver.allocate`
- `solver.solve.3p.construct`   (3-player solver tree construction)
- `solver.solve.3p.run`         (3-player GPU solve kernel launch)
- `solver.solve.2p.construct`
- `solver.solve.2p.run`
- `solver.solve.preflight`
- `solver.solve.start`, `solver.solve.status`, `solver.solve.stop`
- `solver.lock.current.set/get/clear`, `solver.lock.list`, `solver.lock.clearAll`
- `solver.solve.cpu_locked`, `solver.solve.cpu_locked.finalize`

---

## 6. CUDA Kernels (referenced in binary)

These kernel symbol names appear in the binary (PTX target `sm_120`, JIT-compiled at runtime — **no embedded cubin**, so `cuobjdump -sass` reports "does not contain device code"):

`evaluate_fold`, `evaluate_fold_fp32`, `evaluate_showdown`, `evaluate_showdown_brute`,
`evaluate_showdown_prefix`, `evaluate_fold_showdown_prefix`, `evaluate_fold_3p`,
`evaluate_showdown_brute_3p`, `evaluate_showdown_prefix_3p`, `evaluate_fold_abstract`,
`evaluate_showdown_abstract`.

Compiled CUDA targets: `75;80;86;89;90;120`; PTX target: `120`.

---

## 7. JS Poker Primitives (mirror of GPU kernel logic)

Recovered from embedded minified JS — functionally equivalent to the CUDA evaluators:

- **Combo index:** `en(e,t) = e*(103-e)/2 + t - e - 1` for two card indices `e<t`, range `[0,1325]`
- **Card → index:** `ni[en(c,p)] = c | (p << 8)`; suit = `c % 4`, rank = `c / 4`
- **Hand parser `Kp("AhKs")`** → two card indices; validates `[2-9TJQKA][cdhs]` ×2
- **`oa(cards[5])`** → 5-card hand rank (category 0-8: high card … straight flush, with kicker tie-break packed into a single integer)
- **`rb(cards[7])`** → 7-card showdown strength = max over all 21 five-card subsets of `oa`
- **`tb(profits, s, a, t)`** → pot settlement / rake: `n=(min(s,a)-t)/2`, `i=max(s,a)-t-n`, profit/returned/rake
- **`cb(s,a,t)`** → player contributions to pot

See `CUDA_KERNELS_RECON.cpp` and `CUDA_MATH.md` for the translated kernel pseudocode.

---

## 8. Authentication / Bridge Flow (summary)

```
App mount
  → ?apiBase=http://127.0.0.1:6006, ?transport=bridge
  → Ds.request(path, method, body)
       → dT(path,method) resolves internal method name
       → if desktop.* method: callBridgeAuthed(method, {apiBase,path,method,body,...})
       → else: callBridgeAuthed("http.request", {apiBase,path,method,body})
       → fallback to raw HTTP.request on NOT_IMPLEMENTED
  → bridge.ping → bridge_token (cached)
  → postMessage({id, method, params:{...,bridge_token}})
  → listen on window.chrome.webview "message", match by id, resolve/reject
```
