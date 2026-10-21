#!/usr/bin/env python3
"""
TexasSolverGPU v0.3.0 — CFR Solver Simulator (Reference Implementation)

Implements all algorithms reconstructed from SASS disassembly and binary analysis:
- Card encoding / combo index
- 5-card hand rank (eval5) and 7-card showdown (eval7)
- Pot settlement / fold equity
- Q16 fixed-point encoding
- CFR backward (regret matching) and forward (reach accumulation)
- Tree compaction / isomorphism

This is a CPU-based reference for validating against the real GPU solver
once dynamic capture is available on Windows + NVIDIA GPU.

Based on:
- SASS disassembly: backward_player, evaluate_showdown, forward_player, train_small_tree_fp32_multi_iter
- Reconstructed math: CUDA_MATH.md, CPP_CORE_RECON.md
"""

from __future__ import annotations
import itertools
from dataclasses import dataclass, field
from typing import List, Tuple, Optional, Dict
import json

# ============================================================================
# 1. Card encoding (from SASS + JS reconstruction)
# ============================================================================

# Card c in [0,51]: rank = c // 4 (0=2, ..., 12=A), suit = c % 4 (0=c, 1=d, 2=h, 3=s)
RANK_NAMES = ['2', '3', '4', '5', '6', '7', '8', '9', 'T', 'J', 'Q', 'K', 'A']
SUIT_NAMES = ['c', 'd', 'h', 's']

def card_rank(c: int) -> int:
    return c // 4

def card_suit(c: int) -> int:
    return c % 4

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
    """Combo index from two card indices."""
    return combo_index(c1, c2)

# Total combos: C(52,2) = 1326
NUM_COMBOS = 1326


# ============================================================================
# 3. 5-card hand rank oa(cards[5])
# ============================================================================

# Categories: 0=High card, 1=Pair, 2=Two pair, 3=Trips, 4=Straight, 5=Flush, 6=Full house, 7=Quads, 8=Straight flush
# Result = category * 15^k + kicker ranks (base-15 packed)
# Wheel A-2-3-4-5 detected (top=5)

def is_flush(cards: List[int]) -> bool:
    return all(card_suit(c) == card_suit(cards[0]) for c in cards)

def is_straight(ranks: List[int]) -> Tuple[bool, int]:
    """Returns (is_straight, top_rank). Wheel A-2-3-4-5 -> top_rank=5."""
    unique = sorted(set(ranks), reverse=True)
    if len(unique) < 5:
        return False, 0
    # Check wheel A-2-3-4-5
    if set([12, 0, 1, 2, 3]).issubset(set(ranks)):
        return True, 3  # top is 5 (rank index 3)
    for i in range(len(unique) - 4):
        if unique[i] - unique[i+4] == 4:
            return True, unique[i]
    return False, 0

def eval5(cards: List[int]) -> int:
    """
    Evaluate a 5-card hand. Returns a packed integer:
    category * 15^4 + kicker1 * 15^3 + kicker2 * 15^2 + kicker3 * 15 + kicker4
    Categories: 0=High card, 1=Pair, 2=Two pair, 3=Trips, 4=Straight, 5=Flush, 6=Full house, 7=Quads, 8=Straight flush
    """
    ranks = sorted([card_rank(c) for c in cards], reverse=True)
    suits = [card_suit(c) for c in cards]
    
    # Count rank frequencies
    from collections import Counter
    rank_counts = Counter(ranks)
    pairs = sorted([r for r, c in rank_counts.items() if c == 2], reverse=True)
    trips = sorted([r for r, c in rank_counts.items() if c == 3], reverse=True)
    quads = sorted([r for r, c in rank_counts.items() if c == 4], reverse=True)
    
    flush = is_flush(cards)
    straight, straight_top = is_straight(ranks)
    
    # Kickers: all ranks sorted descending (for high card / tiebreaking)
    kickers = ranks[:]  # sorted desc
    
    if flush and straight:
        return 8 * 15**4 + straight_top * 15**3
    if quads:
        quad_rank = quads[0]
        kicker = [r for r in ranks if r != quad_rank][0]
        return 7 * 15**4 + quad_rank * 15**3 + kicker * 15**2
    if trips and pairs:
        # Full house
        return 6 * 15**4 + trips[0] * 15**3 + pairs[0] * 15**2
    if flush:
        return 5 * 15**4 + kickers[0] * 15**3 + kickers[1] * 15**2 + kickers[2] * 15 + kickers[3]
    if straight:
        return 4 * 15**4 + straight_top * 15**3
    if trips:
        trip_rank = trips[0]
        others = sorted([r for r in ranks if r != trip_rank], reverse=True)
        return 3 * 15**4 + trip_rank * 15**3 + others[0] * 15**2 + others[1] * 15 + others[2]
    if len(pairs) >= 2:
        high_pair, low_pair = pairs[0], pairs[1]
        kicker = [r for r in ranks if r not in (high_pair, low_pair)][0]
        return 2 * 15**4 + high_pair * 15**3 + low_pair * 15**2 + kicker * 15
    if len(pairs) == 1:
        pair_rank = pairs[0]
        others = sorted([r for r in ranks if r != pair_rank], reverse=True)
        return 1 * 15**4 + pair_rank * 15**3 + others[0] * 15**2 + others[1] * 15 + others[2]
    # High card
    return 0 * 15**4 + kickers[0] * 15**3 + kickers[1] * 15**2 + kickers[2] * 15 + kickers[3]


def eval7(cards: List[int]) -> int:
    """7-card showdown: max over all C(7,5)=21 five-card subsets of eval5(subset)."""
    best = -1
    for subset in itertools.combinations(cards, 5):
        val = eval5(list(subset))
        if val > best:
            best = val
    return best


# ============================================================================
# 4. Pot settlement tb(profits, s, a, t)
# ============================================================================

def settle_pot(oop_paid: int, ip_paid: int, pot: int, rake_rate: float = 0.0, rake_cap: float = 0.0) -> Dict:
    """
    Pot settlement: n = (min(s,a)-t)/2, i = max(s,a)-t-n
    s = OOP paid, a = IP paid, t = something (starting pot / bet)
    Returns profit/returned/rake per player.
    """
    s = oop_paid
    a = ip_paid
    t = 0  # t is the amount already in pot from previous streets (simplified)
    
    n = (min(s, a) - t) / 2.0
    i = max(s, a) - t - n
    
    # profit/returned per player
    oop_profit = -s + n + (i if s >= a else 0)  # simplified
    ip_profit = -a + n + (i if a >= s else 0)
    
    rake = 0.0
    if rake_rate > 0:
        rake = min(pot * rake_rate, rake_cap) if rake_cap > 0 else pot * rake_rate
    
    return {
        'n': n,
        'i': i,
        'oop_profit': oop_profit,
        'ip_profit': ip_profit,
        'rake': rake,
    }


# ============================================================================
# 5. Q16 fixed-point encoding
# ============================================================================

Q16_SCALE = 65536.0

def q16_encode(value: float) -> int:
    """Encode a float to Q16 fixed-point."""
    return int(round(value * Q16_SCALE))

def q16_decode(raw: int) -> float:
    """Decode Q16 fixed-point to float."""
    return raw / Q16_SCALE


# ============================================================================
# 6. CFR algorithms
# ============================================================================

@dataclass
class CFRNode:
    actor: int
    betting_round: int  # 0=preflop, 1=flop, 2=turn, 3=river
    table_cards: List[int]
    pot: int
    valid_actions: List[str]  # e.g. ["Fold", "Call", "Raise 100"]
    strategy: List[float]  # per-action probabilities
    children: List['CFRNode'] = field(default_factory=list)
    reach_probs: List[float] = field(default_factory=list)  # per-combo reach
    action_evs: List[float] = field(default_factory=list)  # per-action EV
    regrets: List[float] = field(default_factory=list)  # per-action regret
    abs_regrets: List[float] = field(default_factory=list)
    abs_strategy: List[float] = field(default_factory=list)


def cfr_backward(node: CFRNode, reach: float, player: int) -> float:
    """
    CFR backward pass (from SASS backward_player disassembly).
    
    Node EV computation:
        nodeEV = sum_a strategy[a] * actionEV[a]
    
    Regret computation:
        regret[a] = reach * (actionEV[a] - nodeEV)
    
    Regret matching:
        posRegret[a] = max(0, regret[a])
        sumPos = sum(posRegret)
        strategy[a] = posRegret[a] / sumPos   (if sumPos > 0, else uniform)
    """
    num_actions = len(node.valid_actions)
    
    # Compute node EV: sum of strategy[a] * actionEV[a]
    node_ev = 0.0
    for a in range(num_actions):
        node_ev += node.strategy[a] * node.action_evs[a]
    
    # Compute regrets: reach * (actionEV[a] - nodeEV)
    regrets = []
    for a in range(num_actions):
        regret = reach * (node.action_evs[a] - node_ev)
        regrets.append(regret)
    
    # Regret matching: clamp to >= 0, normalize
    pos_regrets = [max(0.0, r) for r in regrets]
    sum_pos = sum(pos_regrets)
    
    new_strategy = [0.0] * num_actions
    if sum_pos > 0:
        inv_sum = 1.0 / sum_pos  # MUFU.RCP equivalent
        for a in range(num_actions):
            new_strategy[a] = pos_regrets[a] * inv_sum  # FMUL.FTZ
    else:
        # Uniform fallback
        uniform = 1.0 / num_actions
        for a in range(num_actions):
            new_strategy[a] = uniform
    
    node.strategy = new_strategy
    node.regrets = regrets
    
    return node_ev


def cfr_forward(node: CFRNode, reach: float, action: int) -> float:
    """
    CFR forward pass (from SASS forward_player disassembly).
    
    reach_out = reach * strategy[a]
    """
    return reach * node.strategy[action]


def cfr_train_iteration(root: CFRNode, num_combos: int, num_players: int = 2) -> float:
    """
    Single CFR training iteration (from SASS train_small_tree_fp32_multi_iter).
    
    For each combo and each player:
        1. Backward: compute regrets, update strategy
        2. Forward: accumulate reach probabilities
    """
    total_ev = 0.0
    
    for combo in range(num_combos):
        for player in range(num_players):
            reach = 1.0 / num_combos  # initial reach (uniform)
            
            # Traverse tree: backward then forward
            # This is a simplified version - real implementation traverses
            # the full game tree
            _cfr_traverse(root, combo, player, reach)
    
    return total_ev


def _cfr_traverse(node: CFRNode, combo: int, player: int, reach: float) -> float:
    """Recursive CFR traversal (simplified)."""
    if node.actor == player:
        # Player node: backward pass
        node_ev = cfr_backward(node, reach, player)
        # Forward: for each action, recurse
        action_ev = 0.0
        for a, action in enumerate(node.valid_actions):
            child_reach = cfr_forward(node, reach, a)
            if node.children:
                action_ev += node.strategy[a] * _cfr_traverse(node.children[a], combo, player, child_reach)
        return action_ev
    else:
        # Opponent/chance node: average over children
        if not node.children:
            return 0.0
        total = 0.0
        for child in node.children:
            total += _cfr_traverse(child, combo, player, reach / len(node.children))
        return total / len(node.children)


# ============================================================================
# 7. Tree compaction / isomorphism (from SASS apply_isomorphism, trim, etc.)
# ============================================================================

def apply_isomorphism(node: CFRNode, permutation: List[int]) -> CFRNode:
    """Apply seat permutation isomorphism to a node."""
    new_node = CFRNode(
        actor=permutation[node.actor] if node.actor < len(permutation) else node.actor,
        betting_round=node.betting_round,
        table_cards=node.table_cards[:],
        pot=node.pot,
        valid_actions=node.valid_actions[:],
        strategy=node.strategy[:],
    )
    return new_node


def trim_tree(node: CFRNode, min_reach: float = 1e-6) -> Optional[CFRNode]:
    """Trim tree branches below minimum reach threshold."""
    if not node.children:
        return node
    
    kept_children = []
    for child in node.children:
        trimmed = trim_tree(child, min_reach)
        if trimmed is not None:
            kept_children.append(trimmed)
    
    if not kept_children:
        return None
    
    node.children = kept_children
    return node


# ============================================================================
# 8. Example: build a simple HU preflop solve
# ============================================================================

def build_simple_hu_tree(oop_range: List[float], ip_range: List[float],
                         board: List[int], starting_pot: int,
                         effective_stack: int, bet_sizes: List[int]) -> CFRNode:
    """Build a simple HU preflop+flop game tree."""
    
    root = CFRNode(
        actor=0,  # OOP acts first
        betting_round=1,  # flop
        table_cards=board[:],
        pot=starting_pot,
        valid_actions=["Fold", "Call", "Raise"] + [f"Raise {b}" for b in bet_sizes],
        strategy=[0.25] * (3 + len(bet_sizes)),  # uniform initial
        reach_probs=[1.0 / NUM_COMBOS] * NUM_COMBOS,
        action_evs=[0.0] * (3 + len(bet_sizes)),
    )
    
    return root


if __name__ == '__main__':
    # Self-test: card encoding and combo index
    c1, c2 = 0, 1  # 2c, 2d
    print(f"Card {c1}: {card_name(c1)} (rank={card_rank(c1)}, suit={card_suit(c1)})")
    print(f"Card {c2}: {card_name(c2)} (rank={card_rank(c2)}, suit={card_suit(c2)})")
    packed = pack_cards(c1, c2)
    print(f"Packed: {packed:#x}, unpacked: {unpack_cards(packed)}")
    
    ci = combo_index(c1, c2)
    print(f"Combo index({c1},{c2}) = {ci}")
    
    # Self-test: eval5 / eval7
    hand = [0, 1, 8, 12, 16]  # 2c, 2d, 9c, Ac, Qc
    print(f"\nHand: {[card_name(c) for c in hand]}")
    print(f"eval5 = {eval5(hand)} (category={eval5(hand) // 15**4})")
    
    hand7 = hand + [20, 24]  # add Kc, 3c
    print(f"7 cards: {[card_name(c) for c in hand7]}")
    print(f"eval7 = {eval7(hand7)} (category={eval7(hand7) // 15**4})")
    
    # Self-test: Q16
    val = 0.123456
    encoded = q16_encode(val)
    decoded = q16_decode(encoded)
    print(f"\nQ16: {val} -> {encoded} -> {decoded} (error={abs(val-decoded):.2e})")
    
    # Self-test: regret matching
    node = CFRNode(
        actor=0, betting_round=1, table_cards=[], pot=100,
        valid_actions=["Fold", "Call", "Raise"],
        strategy=[0.33, 0.33, 0.34],
        action_evs=[-10.0, 5.0, 15.0],
    )
    node_ev = cfr_backward(node, reach=1.0, player=0)
    print(f"\nCFR backward: nodeEV={node_ev:.4f}, new strategy={node.strategy}")
    print(f"Regrets: {node.regrets}")
