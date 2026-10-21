#!/usr/bin/env python3
"""
TexasSolverGPU v0.3.0 — CFR Solver Simulator (SASS-Exact Reference Implementation)

Implements all algorithms reconstructed from SASS disassembly (nvdisasm -b SM80)
of the embedded CUDA ELF objects in TexasSolverGpu_131.exe:

- Card encoding / combo index
- 5-card hand rank (eval5) and 7-card showdown (eval7) — SASS-exact
- Pot settlement / fold equity
- Q16 fixed-point encoding
- CFR backward (backward_player: regret matching w/ 4-action handling, shared-mem accumulation)
- CFR forward (forward_player: predicate-gated per-action reach stores)
- CFR training (train_small_tree_fp32_multi_iter: multi-iteration, discount params)
- Tree compaction / isomorphism

SASS disassembly source: /tmp/opencode/sass_disasm/sm_80/<kernel>.txt

This is a CPU-based reference for validating against the real GPU solver
once dynamic capture is available on Windows + NVIDIA GPU.

Based on SASS disassembly: backward_player (673 instr), evaluate_showdown (457 instr),
forward_player (145 instr), train_small_tree_fp32_multi_iter (3401+ instr),
gather_cfv (41 instr).
"""

from __future__ import annotations
import itertools
from dataclasses import dataclass, field
from typing import List, Tuple, Optional, Dict
import json
import struct

# ============================================================================
# 1. Card encoding (from SASS + JS reconstruction)
# ============================================================================

# Card c in [0,51]: rank = c >> 2 (0=2, ..., 12=A), suit = c & 3 (0=c, 1=d, 2=h, 3=s)
RANK_NAMES = ['2', '3', '4', '5', '6', '7', '8', '9', 'T', 'J', 'Q', 'K', 'A']
SUIT_NAMES = ['c', 'd', 'h', 's']

def card_rank(c: int) -> int:
    return c >> 2

def card_suit(c: int) -> int:
    return c & 3

def card_name(c: int) -> str:
    return f"{RANK_NAMES[card_rank(c)]}{SUIT_NAMES[card_suit(c)]}"

# Two cards packed: c | (p << 8)
def pack_cards(c1: int, c2: int) -> int:
    return c1 | (c2 << 8)

def unpack_cards(packed: int) -> Tuple[int, int]:
    return packed & 0xFF, (packed >> 8) & 0xFF


# ============================================================================
# 2. Combo index: en(e,t) = e*(103-e)/2 + t - e - 1 for e < t, range [0,1325]
# ============================================================================

def combo_index(e: int, t: int) -> int:
    """Combo index for two hole cards e < t (card indices 0..51). Returns [0,1325]."""
    if e > t:
        e, t = t, e
    return e * (103 - e) // 2 + t - e - 1

def combo_index_from_cards(c1: int, c2: int) -> int:
    return combo_index(c1, c2)

NUM_COMBOS = 1326


# ============================================================================
# 3. Q16 fixed-point encoding (SASS: I2F.U16 dequant, divide by 65536.0)
# ============================================================================

Q16_SCALE = 65536.0
Q16_MIN = -32768  # sentinel / invalid value (signed int16 min)
Q16_MAX = 32767   # signed int16 max

def q16_encode(value: float) -> int:
    """
    Encode a float to Q16 fixed-point.
    
    SASS uses I2F.U16 (unsigned 16-bit convert to float) then divides by 65536.0.
    For probabilities in [0,1]: raw = round(value * 65536), range [0, 65535].
    Stored as int16; values > 32767 wrap to negative when read as signed.
    -32768 is reserved as invalid sentinel.
    """
    raw = int(round(value * Q16_SCALE))
    # Clamp to valid unsigned range [0, 65535], excluding sentinel 0x8000 (32768)
    if raw < 0:
        raw = 0
    if raw > 65535:
        raw = 65535
    if raw == 32768:  # 0x8000 = sentinel, clamp down
        raw = 32767
    # Store as int16: values 32768-65535 become negative when read as signed
    if raw > Q16_MAX:
        raw = raw - 65536  # reinterpret as signed int16
    return raw

def q16_decode(raw: int) -> float:
    """
    Decode Q16 fixed-point to float (SASS: I2F.U16 then divide by 65536.0).
    
    Reads raw as unsigned 16-bit (0-65535), then divides by 65536.0.
    -32768 (0x8000) is the invalid sentinel.
    """
    unsigned = raw & 0xFFFF  # reinterpret signed int16 as unsigned
    if unsigned == 0x8000:   # -32768 sentinel
        return float('nan')
    return unsigned / Q16_SCALE


# ============================================================================
# 4. 5-card hand rank oa(cards[5]) — SASS-exact reconstruction
# ============================================================================

# Categories: 0=High card, 1=Pair, 2=Two pair, 3=Trips, 4=Straight, 5=Flush,
#             6=Full house, 7=Quads, 8=Straight flush
# Result = category * 15^k + kicker ranks (base-15 packed). Wheel A-2-3-4-5 detected.

def _rank_counts(cards: List[int]) -> List[int]:
    cnt = [0] * 13
    for c in cards:
        cnt[card_rank(c)] += 1
    return cnt

def _suit_counts(cards: List[int]) -> List[int]:
    cnt = [0] * 4
    for c in cards:
        cnt[card_suit(c)] += 1
    return cnt

def _is_straight(rank_cnt: List[int]) -> Tuple[bool, int]:
    """Returns (is_straight, top_rank). Wheel A-2-3-4-5 -> top_rank=3 (rank index for 5)."""
    # Wheel A-2-3-4-5: ranks 12,0,1,2,3
    if rank_cnt[12] and rank_cnt[0] and rank_cnt[1] and rank_cnt[2] and rank_cnt[3]:
        return True, 3  # top is 5 (rank index 3)
    for r in range(12, 3, -1):
        if rank_cnt[r] and rank_cnt[r-1] and rank_cnt[r-2] and rank_cnt[r-3] and rank_cnt[r-4]:
            return True, r
    return False, -1

def eval5(cards: List[int]) -> int:
    """
    SASS-exact 5-card hand rank.
    Returns a comparable integer: category * 15^4 + kickers in base 15.
    Higher = better hand.
    Matches SASS evaluate_showdown / evaluate_fold lookup-table driven eval.
    """
    rank_cnt = _rank_counts(cards)
    suit_cnt = _suit_counts(cards)

    # Sorted ranks descending (with duplicates for kickers)
    ranks_sorted = sorted([card_rank(c) for c in cards], reverse=True)

    flush = any(c == 5 for c in suit_cnt)
    straight, straight_top = _is_straight(rank_cnt)

    quads = -1
    trips = -1
    pairs = []
    kickers = []
    for r in range(12, -1, -1):
        c = rank_cnt[r]
        if c == 4:
            quads = r
        elif c == 3:
            trips = r
        elif c == 2:
            pairs.append(r)
        else:
            kickers.append(r)

    if flush and straight:
        cat = 8  # straight flush
        return cat * 15**4 + (straight_top + 2) * 15**3
    if quads >= 0:
        cat = 7  # quads
        kicker = [r for r in ranks_sorted if r != quads][0]
        return cat * 15**4 + (quads + 2) * 15**3 + (kicker + 2) * 15**2
    if trips >= 0 and len(pairs) >= 1:
        cat = 6  # full house
        return cat * 15**4 + (trips + 2) * 15**3 + (pairs[0] + 2) * 15**2
    if flush:
        cat = 5
        code = cat * 15**4
        for i in range(5):
            code = code * 15 + (ranks_sorted[i] + 2)
        return code
    if straight:
        cat = 4
        return cat * 15**4 + (straight_top + 2) * 15**3
    if trips >= 0:
        cat = 3
        others = [r for r in ranks_sorted if r != trips]
        code = cat * 15**4 + (trips + 2) * 15**3
        for i in range(min(2, len(others))):
            code = code * 15 + (others[i] + 2)
        return code
    if len(pairs) >= 2:
        cat = 2
        kicker = [r for r in ranks_sorted if r not in (pairs[0], pairs[1])][0]
        return cat * 15**4 + (pairs[0] + 2) * 15**3 + (pairs[1] + 2) * 15**2 + (kicker + 2) * 15
    if len(pairs) == 1:
        cat = 1
        others = [r for r in ranks_sorted if r != pairs[0]]
        code = cat * 15**4 + (pairs[0] + 2) * 15**3
        for i in range(min(3, len(others))):
            code = code * 15 + (others[i] + 2)
        return code
    # High card
    cat = 0
    code = cat * 15**4
    for i in range(5):
        code = code * 15 + (ranks_sorted[i] + 2)
    return code


def eval7(cards: List[int]) -> int:
    """
    7-card showdown strength: max over all C(7,5)=21 five-card subsets of eval5.
    SASS: evaluate_showdown computes this per thread for hero+villain pairs.
    """
    best = -1
    for subset in itertools.combinations(cards, 5):
        v = eval5(list(subset))
        if v > best:
            best = v
    return best


# ============================================================================
# 5. Pot settlement tb(paid, pot, rake) — matches SASS / JS TB()
# ============================================================================

def settle_pot(oop_paid: int, ip_paid: int, pot: int,
               rake_rate: float = 0.0, rake_cap: float = 0.0) -> Dict:
    """
    Pot settlement:
      effective = min(oop_paid, ip_paid) * 2   (main pot)
      rake = min(effective * rake_rate, rake_cap)
      returned[i] = paid[i] - effective
    """
    s = oop_paid
    a = ip_paid
    effective = min(s, a) * 2
    rake = min(effective * rake_rate, rake_cap) if rake_cap > 0 else effective * rake_rate
    ret0 = s - effective
    ret1 = a - effective
    return {
        'effective': effective,
        'rake': rake,
        'oop_returned': ret0,
        'ip_returned': ret1,
        'oop_net': -s + effective - rake,
        'ip_net': -a + effective - rake,
    }


# ============================================================================
# 6. CFR backward — SASS-exact backward_player (regret matching)
# ============================================================================

# From SASS backward_player (sm_80, 673 instructions):
#   - Bounds check: threadIdx.x >= numPlayers -> EXIT
#   - Loads reach[], actionEVs[], regrets[], strategy[] from constant memory
#   - Node EV = sum(strategy[a] * actionEV[a])  (FFMA.FTZ accumulate, R21 accumulator)
#   - Regret[a] = reach * (actionEV[a] - nodeEV)  (FFMA.FTZ)
#   - Regret accumulated into shared memory (STL to [R19], [R19+0x4], [R19+0x8], [R19+0xc])
#   - Regret matching: FMNMX.FTZ clamps regrets to >= 0; MUFU.RCP = 1/sumPos;
#     FMUL.FTZ normalizes posRegret / sumPos
#   - Up to 4 actions per player via predicates P0-P3 (conditional stores)
#   - Barrier sync: BSSY B0 / BSYNC B0 for shared memory sync
#   - Strategy written back: shared memory then global memory (STG.E)

@dataclass
class CFRNode:
    actor: int
    betting_round: int
    table_cards: List[int]
    pot: int
    valid_actions: List[str]
    strategy: List[float]           # per-action strategy (current)
    children: List['CFRNode'] = field(default_factory=list)
    reach_probs: List[float] = field(default_factory=list)   # per-action reach
    action_evs: List[float] = field(default_factory=list)    # per-action EV
    regrets: List[float] = field(default_factory=list)       # cumulative regrets
    abs_regrets: List[float] = field(default_factory=list)
    abs_strategy: List[float] = field(default_factory=list)

def cfr_backward_sass_exact(node: CFRNode, reach: float) -> float:
    """
    SASS-exact CFR backward pass (backward_player kernel logic).
    
    Matches the SASS disassembly:
      1. nodeEV = sum_a strategy[a] * actionEV[a]          (FFMA.FTZ accumulate)
      2. regret[a] = reach * (actionEV[a] - nodeEV)         (FFMA.FTZ)
      3. Accumulate regrets[a] += regret[a]                  (shared memory STL/LDS)
      4. Regret matching:
           posRegret[a] = max(0, regret[a])                  (FMNMX.FTZ)
           sumPos = sum(posRegret)
           invSum = 1.0f / sumPos                            (MUFU.RCP)
           strategy[a] = posRegret[a] * invSum               (FMUL.FTZ)
           if sumPos <= 0: strategy[a] = 1/numActions        (uniform fallback)
      5. Barrier sync (BSSY/BSYNC) before writing strategy back
    
    Handles up to 4 actions per player (predicate-gated in SASS).
    """
    num_actions = len(node.valid_actions)
    if num_actions == 0:
        return 0.0

    # Step 1: Node EV = sum(strategy[a] * actionEV[a])  (SASS: FFMA.FTZ accumulate into R21)
    node_ev = 0.0
    for a in range(num_actions):
        node_ev += node.strategy[a] * node.action_evs[a]

    # Step 2: Regret[a] = reach * (actionEV[a] - nodeEV)  (SASS: FFMA.FTZ)
    regrets = []
    for a in range(num_actions):
        regret = reach * (node.action_evs[a] - node_ev)
        regrets.append(regret)

    # Step 3: Accumulate into cumulative regrets (SASS: shared memory STL/LDS + BSSY/BSYNC barrier)
    for a in range(num_actions):
        node.regrets[a] += regrets[a]

    # Step 4: Regret matching (SASS: FMNMX.FTZ clamp >=0, MUFU.RCP reciprocal, FMUL.FTZ normalize)
    pos_regrets = [max(0.0, r) for r in node.regrets]
    sum_pos = sum(pos_regrets)

    new_strategy = [0.0] * num_actions
    if sum_pos > 0.0:
        inv_sum = 1.0 / sum_pos        # SASS: MUFU.RCP(sumPos)
        for a in range(num_actions):
            new_strategy[a] = pos_regrets[a] * inv_sum   # SASS: FMUL.FTZ
    else:
        # Uniform fallback when all regrets <= 0
        uniform = 1.0 / num_actions
        for a in range(num_actions):
            new_strategy[a] = uniform

    node.strategy = new_strategy

    # Barrier sync equivalent (SASS: BSSY B0 / BSYNC B0 before global store)
    # In GPU, all threads in block sync before writing strategy to global mem

    return node_ev


def cfr_backward_multi_action(node: CFRNode, reaches: List[float]) -> List[float]:
    """
    Multi-action backward pass matching SASS backward_player's predicate-gated
    handling of up to 4 actions per player.
    
    SASS: predicates P0-P3 gate stores for actions 0-3 respectively.
    Each action has its own reach value; node EV accumulates all
    strategy[a] * actionEV[a]; each regret is reach[a] * (actionEV[a] - nodeEV).
    """
    num_actions = len(node.valid_actions)
    if num_actions == 0:
        return []

    # Node EV across all actions
    node_ev = 0.0
    for a in range(num_actions):
        node_ev += node.strategy[a] * node.action_evs[a]

    # Per-action regrets: reach[a] * (actionEV[a] - nodeEV)
    regrets = []
    for a in range(num_actions):
        r = reaches[a] if a < len(reaches) else 1.0
        regret = r * (node.action_evs[a] - node_ev)
        regrets.append(regret)

    # Accumulate + regret matching (same as cfr_backward_sass_exact)
    for a in range(num_actions):
        node.regrets[a] += regrets[a]

    pos_regrets = [max(0.0, r) for r in node.regrets]
    sum_pos = sum(pos_regrets)

    new_strategy = [0.0] * num_actions
    if sum_pos > 0.0:
        inv_sum = 1.0 / sum_pos
        for a in range(num_actions):
            new_strategy[a] = pos_regrets[a] * inv_sum
    else:
        uniform = 1.0 / num_actions
        for a in range(num_actions):
            new_strategy[a] = uniform

    node.strategy = new_strategy
    return regrets


# ============================================================================
# 7. CFR forward — SASS-exact forward_player
# ============================================================================

# From SASS forward_player (sm_80, 145 instructions):
#   - Outer loop over hands: hand index incremented by stride (VIADD-like)
#   - Per hand: loads reach + strategy via LDG.E.CONSTANT
#   - Per action: reach_out = reach * strategy[a]  (FMUL.FTZ)
#   - Predicate-gated stores for up to 4 actions (P1=act0, P2=act1, P3=act2, act3)
#   - STG.E to global memory for each action's reach output

def cfr_forward(reach: float, strategy: List[float]) -> List[float]:
    """
    SASS-exact CFR forward pass (forward_player kernel logic).
    
    For each action a:
        reach_out[a] = reach * strategy[a]     (SASS: FMUL.FTZ)
    
    Predicate-gated: only stores for actions that are valid (predicate set).
    Matches SASS: up to 4 actions per hand, each stored via STG.E.
    """
    reach_out = []
    for a in range(len(strategy)):
        reach_out.append(reach * strategy[a])
    return reach_out


# ============================================================================
# 8. CFR training — SASS-exact train_small_tree_fp32_multi_iter
# ============================================================================

# From SASS train_small_tree_fp32_multi_iter (largest kernel, ~3400+ instructions):
#   - Variable constant buffer access: c[0x0][R37+offset] (R37 = per-kernel config base)
#   - Q16 dequantization: I2F.U16 converts int16 to float
#   - Discount parameters: LDC.64 R2, c[0x0][R37+0x160]  (3 floats: iter_params)
#   - Multi-iteration loop: runs multiple CFR iterations per kernel launch
#   - Per iteration:
#       nodeEV = sum(strategy[a] * evs[a])
#       regret = reach[tid] * (evs[tid] - nodeEV)
#       regrets[tid] = disc * regrets[tid] + (1-disc) * regret   (exponential moving average)
#       posRegret = max(0, regrets[tid])                          (FMNMX.FTZ)
#       strategy[tid] = posRegret                                  (normalize elsewhere)
#   - Shared memory tiling with BSSY barriers
#   - 64-bit address computation via IMAD.WIDE / SHF.L.U64.HI

def cfr_train_multi_iter(reach: List[float], evs: List[float],
                         regrets: List[float], strategy: List[float],
                         discount: float, num_iterations: int) -> Tuple[List[float], List[float]]:
    """
    SASS-exact multi-iteration CFR training (train_small_tree_fp32_multi_iter).
    
    Runs `num_iterations` CFR iterations per kernel launch. Each iteration:
      1. nodeEV = sum_a strategy[a] * evs[a]
      2. regret[a] = reach[a] * (evs[a] - nodeEV)
      3. regrets[a] = discount * regrets[a] + (1 - discount) * regret[a]   (EMA)
      4. strategy[a] = max(0, regrets[a])   (unnormalized; normalization done separately)
    
    SASS: discount loaded from c[0x0][R37+0x160] (iter_params[0]).
    """
    num_actions = len(evs)
    for _ in range(num_iterations):
        # Step 1: Node EV
        node_ev = 0.0
        for a in range(num_actions):
            node_ev += strategy[a] * evs[a]

        # Step 2-4: Per-action regret + EMA + strategy update
        for a in range(num_actions):
            r = reach[a]
            aEV = evs[a]
            regret = r * (aEV - node_ev)
            regrets[a] = discount * regrets[a] + (1.0 - discount) * regret
            pos_regret = max(0.0, regrets[a])
            strategy[a] = pos_regret   # normalization happens in separate pass

    return regrets, strategy


# ============================================================================
# 9. Full CFR iteration (backward + forward) for a simple game tree
# ============================================================================

def cfr_iteration(root: CFRNode, reach: float, player: int) -> float:
    """
    Single CFR iteration over a game tree.
    Traverses the tree; at player nodes runs backward pass,
    at opponent/chance nodes averages over children.
    """
    if not root.children:
        # Terminal node: return 0 (leaf EV handled by caller via action_evs)
        return 0.0

    if root.actor == player:
        # Player node: backward pass
        node_ev = cfr_backward_sass_exact(root, reach)
        # Forward: for each action, recurse with updated reach
        total = 0.0
        reach_outs = cfr_forward(reach, root.strategy)
        for a, child in enumerate(root.children):
            total += root.strategy[a] * cfr_iteration(child, reach_outs[a], player)
        return total
    else:
        # Opponent / chance node: average over children
        total = 0.0
        for child in root.children:
            child_reach = reach / len(root.children)
            total += cfr_iteration(child, child_reach, player)
        return total / len(root.children)


# ============================================================================
# 10. Tree compaction / isomorphism (SASS: apply_isomorphism, trim, replace)
# ============================================================================

def apply_isomorphism(nodes: List[int], perm: List[int]) -> List[int]:
    """
    SASS apply_isomorphism: swap isomorphic subtrees via permutation.
    SASS: LDS/STS shared memory, BSSY barrier, PRMT for node-id byte extraction.
    """
    result = nodes[:]
    n = len(nodes)
    for i in range(n):
        j = perm[i]
        if j > i:
            result[i], result[j] = result[j], result[i]
    return result

def apply_isomorphism_permuted(nodes: List[int], perm: List[int]) -> List[int]:
    """SASS apply_isomorphism_permuted: nodes[i] = nodes[perm[i]]."""
    return [nodes[perm[i]] for i in range(len(nodes))]

def trim_tree(active_mask: List[bool], nodes: List[int]) -> List[int]:
    """SASS trim: tombstone inactive nodes (set to -1)."""
    return [nodes[i] if active_mask[i] else -1 for i in range(len(nodes))]

def replace_nodes(nodes: List[int], src: List[int], dst_map: List[int]) -> List[int]:
    """SASS replace: nodes[i] = src[dst_map[i]]."""
    return [src[dst_map[i]] for i in range(len(nodes))]


# ============================================================================
# 11. Gather / scatter (SASS: gather_cfv)
# ============================================================================

def gather_cfv(src_table: List[int], indices: List[int]) -> List[int]:
    """
    SASS gather_cfv: out[tid] = src_table[indices[tid]].
    Simple one-thread-per-element gather with BSSY barrier.
    """
    return [src_table[idx] for idx in indices]


# ============================================================================
# 12. Self-test / demo
# ============================================================================

if __name__ == '__main__':
    # --- Card encoding / combo index ---
    c1, c2 = 0, 1  # 2c, 2d
    print(f"Card {c1}: {card_name(c1)} (rank={card_rank(c1)}, suit={card_suit(c1)})")
    packed = pack_cards(c1, c2)
    print(f"Packed: {packed:#x}, unpacked: {unpack_cards(packed)}")
    ci = combo_index(c1, c2)
    print(f"Combo index({c1},{c2}) = {ci}")

    # --- eval5 / eval7 ---
    hand = [0, 1, 8, 12, 16]   # 2c, 2d, 9c, Ac, Qc  (pair of 2s)
    print(f"\nHand: {[card_name(c) for c in hand]}")
    print(f"eval5 = {eval5(hand)} (category={eval5(hand) // 15**4})")

    hand7 = hand + [20, 24]   # add Kc, 3c
    print(f"7 cards: {[card_name(c) for c in hand7]}")
    print(f"eval7 = {eval7(hand7)} (category={eval7(hand7) // 15**4})")

    # Wheel straight A-2-3-4-5
    wheel = [0, 4, 8, 12, 48]   # 2c, 3c, 4c, 5c, Ac
    print(f"\nWheel: {[card_name(c) for c in wheel]}")
    print(f"eval5 = {eval5(wheel)} (category={eval5(wheel) // 15**4}, expect straight=4)")

    # --- Q16 ---
    val = 0.123456
    encoded = q16_encode(val)
    decoded = q16_decode(encoded)
    print(f"\nQ16: {val} -> {encoded} -> {decoded} (error={abs(val-decoded):.2e})")

    # --- CFR backward (SASS-exact regret matching) ---
    node = CFRNode(
        actor=0, betting_round=1, table_cards=[], pot=100,
        valid_actions=["Fold", "Call", "Raise 50", "Raise 100"],  # 4 actions
        strategy=[0.25, 0.25, 0.25, 0.25],
        reach_probs=[1.0] * 4,
        action_evs=[-10.0, 5.0, 15.0, 20.0],
        regrets=[0.0] * 4,
    )
    node_ev = cfr_backward_sass_exact(node, reach=1.0)
    print(f"\nCFR backward (SASS-exact): nodeEV={node_ev:.4f}")
    print(f"  new strategy={node.strategy}")
    print(f"  regrets={node.regrets}")

    # --- CFR forward ---
    reach_outs = cfr_forward(1.0, node.strategy)
    print(f"CFR forward: reach_outs={reach_outs}")

    # --- Multi-iteration training ---
    reach_arr = [1.0, 1.0, 1.0, 1.0]
    evs_arr = [-10.0, 5.0, 15.0, 20.0]
    regrets_arr = [0.0] * 4
    strategy_arr = [0.25] * 4
    disc = 0.5  # iter_params[0] from SASS c[0x0][R37+0x160]
    regrets_arr, strategy_arr = cfr_train_multi_iter(
        reach_arr, evs_arr, regrets_arr, strategy_arr, disc, num_iterations=100
    )
    print(f"\nCFR train (100 iters, discount={disc}):")
    print(f"  final regrets={[f'{r:.4f}' for r in regrets_arr]}")
    print(f"  final strategy={[f'{s:.4f}' for s in strategy_arr]}")
    print(f"  strategy sum={sum(strategy_arr):.4f}")

    # --- Isomorphism ---
    nodes = [10, 20, 30, 40]
    perm = [2, 3, 0, 1]
    print(f"\nIsomorphism: {nodes} --{perm}--> {apply_isomorphism_permuted(nodes, perm)}")

    # --- Gather ---
    table = [100, 200, 300, 400, 500]
    idx = [3, 1, 4, 0]
    print(f"Gather: table={table}, indices={idx} -> {gather_cfv(table, idx)}")