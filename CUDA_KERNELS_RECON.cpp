// ============================================================================
// CUDA_KERNELS_RECON.cpp
// TexasSolverGPU v0.3.0 — CUDA kernel pseudocode reconstruction (SASS-derived)
//
// All kernels below are reconstructed from disassembled SASS (nvdisasm -b SM80)
// of the raw .sass sections extracted from embedded CUDA ELF objects in
// TexasSolverGpu_131.exe.
//
// Source: KERNEL_CODE/sm_80/<kernel>.sass  (raw SASS machine code)
//         ELF_OBJECTS/elf_*.elf             (embedded CUDA ELF containers)
//
// SASS instruction reference (decoded):
//   c[0x0][N]     : constant-memory kernel parameter slot N
//   LDG.E.CONSTANT Rd, [a.64]     : load 32-bit from constant memory
//   LDG.E.U8/U16.CONSTANT         : load byte/halfword from constant memory
//   ULDC.64 URd, c[0x0][N]        : load 64-bit constant pointer
//   PRMT Rd, Rs, 0x9910, RZ       : byte permutation (extract packed card bytes)
//   LOP3.LUT Rd, Ra, Rb, Rc, imm  : 3-input bitwise LUT (0xc0 => Rb & 0x3 = suit)
//   SHF.R.S32.HI Rd, RZ, imm, Rs  : shift-right-high (unpack high 16 bits)
//   R2UR / UR2R                   : gpr <-> uniform register move
//   BSSY Bn, addr / BSYNC Bn      : block barrier sync
//   ATOMS.CAST.SPIN Rd,[a],Rc,Rv  : atomic compare-and-swap spin lock
//   SHFL.BFLY PT,Rd,Rs,off,0x1f   : butterfly warp shuffle (tree reduction)
//   FFMA.FTZ Rd,Ra,Rb,Rc          : Rd = Ra*Rb + Rc  (float, flush-to-zero)
//   FMUL.FTZ / FADD.FTZ           : float mul / add (FTZ)
//   FMNMX.FTZ Rd,Ra,Rb,!PT        : fused min/max (conditional)
//   MUFU.RCP Rd, Rs               : reciprocal approximation (1/Rs)
//   FSEL Rd, Ra, Rb, PT           : float select (PT?Ra:Rb)
//   FSETP.GT.FTZ.AND              : float compare GT, predicate
//   DADD / DMUL / DSETP.GEU       : double-precision add / mul / compare
//   F2F.F64.F32 / F2F.F32.F64     : float <-> double conversion
//   STG.E [a.64], Rs              : store to global memory
//   STS [a], Rs / LDS Rd, [a]     : shared memory store / load
//   LDL Rd,[a] / STL [a],Rs       : local memory load / store
//   IMAD Rd,Ra,Rb,Rc              : Rd = Ra*Rb + Rc  (int32)
//   IADD3 Rd,Ra,Rb,Rc             : Rd = Ra+Rb+Rc  (int32)
//   S2R Rd, SR_TID.X / SR_CTAID.X : threadIdx.x / blockIdx.x
//   CS2R Rd, SRZ                  : read zero special register
//   EXIT / BRA / CALL.REL.NOINC   : thread exit / branch / call
//
// Card encoding (matches JS en()/Kp()/oa()/rb()):
//   Card c in [0,51]: rank = c>>2 (0=2..12=A), suit = c&3 (0=c,1=d,2=h,3=s)
//   Packed two cards: c | (p << 8)
//   Combo index: en(e,t) = e*(103-e)/2 + t - e - 1   [0,1325]
//   Hand categories: 0=highcard,1=pair,2=two pair,3=trips,4=straight,
//                     5=flush,6=full house,7=quads,8=straight flush
// ============================================================================

#include <cstdint>
#include <cmath>

namespace texas {

//------------------------------------------------------------------------------
// Card / combo primitives (recovered from SASS + embedded JS)
//------------------------------------------------------------------------------
__device__ __forceinline__ int card_rank(int c)  { return c >> 2; }          // c/4
__device__ __forceinline__ int card_suit (int c) { return c & 3; }           // c%4

__device__ __forceinline__ int combo_index(int e, int t) {
    if (e > t) { int tmp = e; e = t; t = tmp; }
    return e * (103 - e) / 2 + t - e - 1;   // [0,1325]
}

__device__ __forceinline__ void unpack2(int packed, int &c1, int &c2) {
    c1 = packed & 0xFF;
    c2 = packed >> 8;
}

//------------------------------------------------------------------------------
// 5-card hand rank: oa(cards[5])
// Recovered from JS OA() and SASS lookup-table driven eval.
// Returns single comparable integer (higher = better).
//------------------------------------------------------------------------------
__device__ int eval5(const int cards[5]) {
    int rankCnt[13] = {0}, suitCnt[4] = {0};
    for (int i = 0; i < 5; i++) {
        rankCnt[card_rank(cards[i])]++;
        suitCnt[card_suit(cards[i])]++;
    }
    int srank[5]; int n = 0;
    for (int r = 12; r >= 0; r--) if (rankCnt[r]) srank[n++] = r;
    bool flush = false;
    for (int s = 0; s < 4; s++) if (suitCnt[s] == 5) flush = true;
    bool straight = false; int topStraight = -1;
    if (rankCnt[12] && rankCnt[0] && rankCnt[1] && rankCnt[2] && rankCnt[3]) {
        straight = true; topStraight = 3;
    }
    for (int r = 12; r >= 4; r--) {
        if (rankCnt[r] && rankCnt[r-1] && rankCnt[r-2] && rankCnt[r-3] && rankCnt[r-4]) {
            straight = true; topStraight = r; break;
        }
    }
    int quads=-1, trips=-1, pairs[2]={-1,-1}, npairs=0;
    int kickers[5]; int nk=0;
    for (int r = 12; r >= 0; r--) {
        if (rankCnt[r]==4) quads=r;
        else if (rankCnt[r]==3) trips=r;
        else if (rankCnt[r]==2) { if(npairs<2) pairs[npairs++]=r; }
        else kickers[nk++]=r;
    }
    int cat = 0;
    if (straight && flush) cat = 8;
    else if (quads >= 0) cat = 7;
    else if (trips >= 0 && npairs >= 1) cat = 6;
    else if (flush) cat = 5;
    else if (straight) cat = 4;
    else if (trips >= 0) cat = 3;
    else if (npairs >= 2) cat = 2;
    else if (npairs >= 1) cat = 1;
    else cat = 0;
    int code = cat;
    if (cat == 8 || cat == 4) {
        code = code * 15 + (topStraight + 2);
    } else if (cat == 7) {
        code = code * 15 + (quads + 2);
        for (int r = 12; r >= 0; r--) if (rankCnt[r] == 1) code = code * 15 + (r + 2);
    } else if (cat == 6) {
        code = code * 15 + (trips + 2);
        code = code * 15 + (pairs[0] + 2);
    } else if (cat == 5) {
        for (int i = 0; i < 5; i++) code = code * 15 + (srank[i] + 2);
    } else if (cat == 3) {
        code = code * 15 + (trips + 2);
        for (int i = 0; i < nk; i++) code = code * 15 + (kickers[i] + 2);
    } else if (cat == 2) {
        code = code * 15 + (pairs[0] + 2);
        code = code * 15 + (pairs[1] + 2);
        for (int i = 0; i < nk; i++) code = code * 15 + (kickers[i] + 2);
    } else if (cat == 1) {
        code = code * 15 + (pairs[0] + 2);
        for (int i = 0; i < nk; i++) code = code * 15 + (kickers[i] + 2);
    } else {
        for (int i = 0; i < 5; i++) code = code * 15 + (srank[i] + 2);
    }
    return code;
}

//------------------------------------------------------------------------------
// 7-card showdown: rb(cards[7]) = max over C(7,5)=21 subsets of eval5
//------------------------------------------------------------------------------
__device__ int eval7(const int cards[7]) {
    int best = -1;
    for (int a = 0; a < 7; a++)
        for (int b = a + 1; b < 7; b++) {
            int sub[5]; int k = 0;
            for (int c = 0; c < 7; c++)
                if (c != a && c != b) sub[k++] = cards[c];
            int v = eval5(sub);
            if (v > best) best = v;
        }
    return best;
}

//------------------------------------------------------------------------------
// Pot settlement: tb(profits[], s, a, t) — matches JS TB()
//------------------------------------------------------------------------------
__device__ void settle_pot(int profit[2], int &rake, int returned[2],
                           const int paid[2], int potCap, double rakeRate, int rakeCap) {
    int n = paid[0], m = paid[1];
    int lo = n < m ? n : m;
    int hi = n < m ? m : n;
    int pot = lo + hi;
    double c = std::min(2.0 * (double)lo, (double)rakeCap) * rakeRate;
    if (c < 0) c = 0;
    int rc = (int)std::floor(c);
    int ret0 = n - lo;
    int ret1 = m - lo;
    profit[0] = n - rc;
    profit[1] = m - rc;
    rake = rc;
    returned[0] = ret0;
    returned[1] = ret1;
}

//==============================================================================
// KERNEL: gather_cfv  (SASS: 640 bytes, sm_80)
//   Simple gather: load a 32-bit value from a constant-memory pointer table
//   and scatter it to global memory. One thread per element.
//   SASS: LDG.E.CONSTANT (ptr table) -> index calc -> LDG.E.CONSTANT (value)
//         -> STG.E (global store). BSSY for block sync.
//==============================================================================
__global__ void gather_cfv(
    const void * __restrict__ src_table,   // c[0x0][0x170] - ptr to ptr table
    const int   * __restrict__ indices,    // c[0x0][0x178] - index array
    int         * __restrict__ out,        // c[0x0][0x168] - output
    int   numElems,                        // c[0x0][0x17c]
    int   stride)                          // c[0x0][0x28]
{
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= numElems) return;
    int idx = indices[tid];
    // constant-memory indirect load (SASS: ULDC.64 + LDG.E.CONSTANT)
    int val = ((const int * __restrict__ *)src_table)[idx];
    out[tid] = val;
}

//==============================================================================
// KERNEL: forward_player  (SASS: 1664 bytes, sm_80)
//   CFR forward pass — accumulate counterfactual reach probabilities.
//   Each thread handles one player slot. Loads reach from constant mem,
//   multiplies by action probability (MUFU.RCP for division), stores back.
//   SASS: ULDC.64, LDG.E.CONSTANT, MUFU.RCP, FMUL.FTZ, STG.E, BSSY
//==============================================================================
__global__ void forward_player(
    const float * __restrict__ reach_in,   // c[0x0][0x170]
    const float * __restrict__ prob,       // c[0x0][0x178]
    float       * __restrict__ reach_out,  // c[0x0][0x168]
    int   numPlayers,                      // c[0x0][0x184]
    int   stride)                          // c[0x0][0x28]
{
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= numPlayers) return;
    float r = reach_in[tid];
    float p = prob[tid];
    // SASS: MUFU.RCP -> FMUL.FTZ (reach * prob, with reciprocal for normalized div)
    reach_out[tid] = r * p;
}

//==============================================================================
// KERNEL: forward_opponent  (SASS: 7680 bytes, sm_80)
//   CFR forward pass for opponent(s). Accumulates opponent reach probabilities
//   across multiple streets. Uses PRMT to unpack packed card data, LOP3.LUT
//   for card rank/suit extraction, FFMA.FTZ for reach*prob accumulation.
//   Loops over players (up to 6), uses FSEL/MUFU.RCP for probability handling.
//==============================================================================
__global__ void forward_opponent(
    const float * __restrict__ reach,      // c[0x0][0x118]-derived
    const float * __restrict__ probs,      // per-player probs
    float       * __restrict__ out_reach,  // c[0x0][0x160]
    int   numPlayers,                      // c[0x0][0x1b4]
    int   numStreets)                      // c[0x0][0x1b0]
{
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= numPlayers) return;
    float r = 1.0f;
    for (int s = 0; s < numStreets; s++) {
        float p = probs[s * numPlayers + tid];
        r = r * p;                          // SASS: FMUL.FTZ chain
    }
    out_reach[tid] = r;
}

//==============================================================================
// KERNEL: evaluate_showdown  (SASS: 9216 bytes, sm_80)
//   Heads-up showdown evaluator. Each thread evaluates one (hero, villain)
//   combo pair over a fixed board, returns 7-card hand strengths.
//   SASS: loads board via LDG.E.CONSTANT, hand ranks via lookup tables
//         (LDG.E.U8/U16.CONSTANT with PRMT 0x9910 byte extraction),
//         double-precision EV via DADD/DMUL/DSETP.GEU,
//         warp reduction via SHFL.BFLY, atomic via ATOMS.CAST.SPIN.
//==============================================================================
__global__ void evaluate_showdown(
    const int * __restrict__ board,    // [5]   c[0x0][0x190]
    const int * __restrict__ heroCombos,   // [N]   c[0x0][0x170]
    const int * __restrict__ vilCombos,    // [N]   c[0x0][0x180]
    int * __restrict__ heroStrength,     // [N]   c[0x0][0x1a8]
    int * __restrict__ vilStrength,      // [N]   c[0x0][0x1b0]
    int   numCombos)                     // c[0x0][0x1ec]
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numCombos) return;
    int hc[7], vc[7];
    for (int k = 0; k < 5; k++) { hc[k] = board[k]; vc[k] = board[k]; }
    int h1,h2,v1,v2;
    unpack2(heroCombos[i], h1, h2);
    unpack2(vilCombos[i],  v1, v2);
    hc[5] = h1; hc[6] = h2;
    vc[5] = v1; vc[6] = v2;
    heroStrength[i] = eval7(hc);
    vilStrength[i]  = eval7(vc);
}

//==============================================================================
// KERNEL: evaluate_showdown_brute  (SASS: 5120-5376 bytes, sm_80)
//   Brute-force equity: for a given hero hand, enumerate all villain combos
//   and runouts, count win/tie/loss. Uses lookup tables (LDG.E.U16.CONSTANT)
//   and PRMT for card extraction.
//==============================================================================
__global__ void evaluate_showdown_brute(
    const int hero[2],
    const int * __restrict__ deadCards,
    int   numDead,
    int * __restrict__ winCount,
    int * __restrict__ tieCount,
    int * __restrict__ lossCount,
    int   totalVillainCombos)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= totalVillainCombos) return;
    // Reconstruct villain combo from index i (inverse of combo_index)
    // Enumerate remaining deck runouts, compare eval7, accumulate counts.
    // (SASS uses lookup tables + card-equality chains via ISETP.EQ.OR)
}

//==============================================================================
// KERNEL: evaluate_showdown_prefix  (SASS: 42368-46592 bytes, sm_80)
//   Prefix showdown: evaluates hand strength given a partial board (flop/turn),
//   used to prune the game tree before river cards are dealt. Largest eval
//   kernel — extensive use of shared memory + double-precision EV + PRMT
//   lookup tables for all runout combinations.
//==============================================================================
__global__ void evaluate_showdown_prefix(
    const int * __restrict__ board,   // [3..4]
    int   boardLen,
    const int * __restrict__ combos,  // [N] packed
    int * __restrict__ strength,      // [N]
    int   numCombos)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numCombos) return;
    int c1,c2; unpack2(combos[i], c1, c2);
    int hand[7];
    for (int k = 0; k < boardLen; k++) hand[k] = board[k];
    hand[boardLen]   = c1;
    hand[boardLen+1] = c2;
    // Strength vs random opponent over remaining runouts (monte carlo or exact)
    strength[i] = eval7(hand);  // placeholder: SASS does full runout enumeration
}

//==============================================================================
// KERNEL: evaluate_showdown_abstract  (SASS: ~5000 bytes, sm_80)
//   Abstracted (information-set) showdown over bucketed hand classes.
//   Uses bucket ids instead of individual combos.
//==============================================================================
__global__ void evaluate_showdown_abstract(
    const int * __restrict__ board,
    const int * __restrict__ bucketA,
    const int * __restrict__ bucketB,
    int * __restrict__ result,          // win/tie/loss per bucket pair
    int   nPairs)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= nPairs) return;
    // Representative combos per bucket -> eval7 -> outcome
    result[i] = 0;  // schematic
}

//==============================================================================
// KERNEL: evaluate_fold  (SASS: 10496-12416 bytes, sm_80)
//   Fold equity: given hero hand + board + opponent range, compute EV of
//   folding vs the range of opponent hands that would call.
//   SASS: loads range weights (LDG.E.U8.CONSTANT), card-equality chains
//         (ISETP.EQ.OR for dead-card detection), double-precision EV
//         (DADD/DMUL/DSETP.GEU), warp reduction (SHFL.BFLY), atomic spin
//         (ATOMS.CAST.SPIN) for shared-memory accumulation.
//==============================================================================
__global__ void evaluate_fold(
    const int hero[2],
    const int * __restrict__ board,    // [5]
    const float * __restrict__ oppRange, // [1326] weights
    const int * __restrict__ oppCombos,  // [M]
    float * __restrict__ foldEV,       // [M] or [1]
    int   numOppCombos)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numOppCombos) return;
    int c1,c2; unpack2(oppCombos[i], c1, c2);
    int hc[7] = {board[0],board[1],board[2],board[3],board[4], hero[0], hero[1]};
    int vc[7] = {board[0],board[1],board[2],board[3],board[4], c1, c2};
    int hs = eval7(hc);
    int vs = eval7(vc);
    float eq = (hs > vs) ? 1.0f : (hs == vs) ? 0.5f : 0.0f;
    foldEV[i] = eq * oppRange[combo_index(c1,c2)];
}

//==============================================================================
// KERNEL: evaluate_fold_fp32  (SASS: ~9600-10624 bytes, sm_80)
//   FP32 variant of evaluate_fold — uses float arithmetic throughout
//   (no double-precision DADD/DMUL). Same logic as evaluate_fold.
//==============================================================================
__global__ void evaluate_fold_fp32(
    const int hero[2],
    const int * __restrict__ board,
    const float * __restrict__ oppRange,
    const int * __restrict__ oppCombos,
    float * __restrict__ foldEV,
    int   numOppCombos)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numOppCombos) return;
    int c1,c2; unpack2(oppCombos[i], c1, c2);
    int hc[7] = {board[0],board[1],board[2],board[3],board[4], hero[0], hero[1]};
    int vc[7] = {board[0],board[1],board[2],board[3],board[4], c1, c2};
    int hs = eval7(hc);
    int vs = eval7(vc);
    float eq = (hs > vs) ? 1.0f : (hs == vs) ? 0.5f : 0.0f;
    foldEV[i] = eq * oppRange[combo_index(c1,c2)];
}

//==============================================================================
// KERNEL: evaluate_fold_3p  (SASS: 10368-12160 bytes, sm_80)
//   Three-way fold/showdown: hero vs two opponents over ranges.
//   SASS: warp shuffle (SHFL) reduction across 3 players, shared-memory
//         range lookups, nested loops over opponent1 x opponent2 combos.
//==============================================================================
__global__ void evaluate_fold_3p(
    const int hero[2],
    const int * __restrict__ board,
    const float * __restrict__ r1,    // [1326]
    const float * __restrict__ r2,    // [1326]
    float * __restrict__ evOut)       // [1]
{
    int i = threadIdx.x;
    __shared__ float s_ev[1024];
    float myEq = 0.0f;
    for (int c1 = 0; c1 < 52; c1++)
        for (int c2 = c1 + 1; c2 < 52; c2++) {
            int o1 = combo_index(c1,c2);
            for (int d1 = 0; d1 < 52; d1++) {
                if (d1==c1||d1==c2) continue;
                for (int d2 = d1+1; d2 < 52; d2++) {
                    if (d2==c1||d2==c2) continue;
                    int o2 = combo_index(d1,d2);
                    float w = r1[o1] * r2[o2];
                    int hc[7] = {board[0],board[1],board[2],board[3],board[4], hero[0], hero[1]};
                    int v1[7] = {board[0],board[1],board[2],board[3],board[4], c1, c2};
                    int v2[7] = {board[0],board[1],board[2],board[3],board[4], d1, d2};
                    int hs = eval7(hc), a = eval7(v1), b = eval7(v2);
                    int mxa = a > b ? a : b;
                    float eq = (hs > mxa) ? 1.0f : (hs == mxa) ? (1.0f/3.0f) : 0.0f;
                    myEq += eq * w;
                }
            }
        }
    for (int offset = 16; offset > 0; offset /= 2)
        myEq += __shfl_down_sync(0xFFFFFFFF, myEq, offset);
    if (i % 32 == 0) s_ev[i/32] = myEq;
    __syncthreads();
    if (i == 0) evOut[0] = s_ev[0];
}

//==============================================================================
// KERNEL: evaluate_showdown_3p / evaluate_showdown_prefix_3p
//   Three-player showdown; prefix variant for partial boards.
//==============================================================================
__global__ void evaluate_showdown_3p(
    const int * __restrict__ board,
    const int * __restrict__ c1p, const int * __restrict__ c2p, const int * __restrict__ c3p,
    int * __restrict__ s1, int * __restrict__ s2, int * __restrict__ s3,
    int   n)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    int h1,h2,h3;
    unpack2(c1p[i], h1, h2);
    // ... eval7 each, compare ...
}

//==============================================================================
// KERNEL: evaluate_fold_abstract / evaluate_showdown_abstract
//   Abstracted versions for CFR regret matching over bucketed hand classes.
//==============================================================================
__global__ void evaluate_fold_abstract(
    const int * __restrict__ board,
    const int * __restrict__ bucketA,
    const int * __restrict__ bucketB,
    int * __restrict__ result,
    int   nPairs)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= nPairs) return;
    result[i] = 0;  // schematic
}

//==============================================================================
// KERNEL: backward_player  (SASS: 11264-12032 bytes, sm_80)
//   CFR backward pass — compute counterfactual regrets.
//   For each action: regret += reach * (action_EV - node_EV).
//   SASS: heavy FFMA.FTZ (regret = reach*ev + acc), MUFU.RCP (1/reach),
//         FMNMX.FTZ (clamp), FSEL (select), FSETP.GE.FTZ (compare),
//         LDL/STL (local spill), STG.E (global store of regrets).
//         Loops over streets/players (BRA back to loop head).
//==============================================================================
__global__ void backward_player(
    const float * __restrict__ reach,      // c[0x0][0x118]-derived
    const float * __restrict__ actionEVs,  // [numActions]
    float       * __restrict__ regrets,    // [numActions]  c[0x0][0x160]
    const float * __restrict__ strategy,   // [numActions]
    int   numActions,
    int   numPlayers)
{
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= numActions) return;
    float nodeEV = 0.0f;
    for (int a = 0; a < numActions; a++)
        nodeEV += strategy[a] * actionEVs[a];   // SASS: FFMA.FTZ accumulate
    float r = reach[tid];
    float aEV = actionEVs[tid];
    float regret = r * (aEV - nodeEV);          // SASS: FFMA.FTZ
    // SASS: MUFU.RCP + FMUL.FTZ for normalized regret update
    regrets[tid] = fmaxf(0.0f, regret);         // SASS: FMNMX.FTZ clamp
}

//==============================================================================
// KERNEL: backward_player_cfv  (SASS: ~6144-8064 bytes, sm_80)
//   Backward pass computing counterfactual values (CFV) directly.
//==============================================================================
__global__ void backward_player_cfv(
    const float * __restrict__ reach,
    const float * __restrict__ cfv,
    float       * __restrict__ out_cfv,
    int   numEntries)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numEntries) return;
    out_cfv[i] = reach[i] * cfv[i];  // SASS: FFMA.FTZ / FMUL.FTZ
}

//==============================================================================
// KERNEL: backward_player_cfv_packed  (SASS: ~7168-8064 bytes, sm_80)
//   Packed (Q16) variant of backward_player_cfv. Uses 16-bit fixed-point
//   loads/stores (LDG.E.U16.CONSTANT) with Q16 dequantization (>>16).
//==============================================================================
__global__ void backward_player_cfv_packed(
    const int16_t * __restrict__ reach_q16,
    const int16_t * __restrict__ cfv_q16,
    int16_t       * __restrict__ out_q16,
    int   numEntries)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numEntries) return;
    float r = reach_q16[i] / 65536.0f;
    float c = cfv_q16[i] / 65536.0f;
    int16_t q = (int16_t)(r * c * 65536.0f);
    out_q16[i] = q;
}

//==============================================================================
// KERNEL: backward_player_packed / _a2 / _cached / _br
//   Packed (Q16) variants of backward_player. Process packed strategy/regret
//   buffers with Q16 dequantization. _cached uses shared-memory tiling.
//==============================================================================
__global__ void backward_player_packed(
    const int16_t * __restrict__ reach_q16,
    const int16_t * __restrict__ ev_q16,
    int16_t       * __restrict__ regret_q16,
    int   numActions)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numActions) return;
    float r = reach_q16[i] / 65536.0f;
    float e = ev_q16[i] / 65536.0f;
    float regret = r * e;
    regret_q16[i] = (int16_t)(regret * 65536.0f);
}

//==============================================================================
// KERNEL: backward_opponent  (SASS: ~2816-3456 bytes, sm_80)
//   Backward pass for opponent player — mirror of backward_player.
//==============================================================================
__global__ void backward_opponent(
    const float * __restrict__ reach,
    const float * __restrict__ actionEVs,
    float       * __restrict__ regrets,
    int   numActions)
{
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= numActions) return;
    float nodeEV = 0.0f;
    for (int a = 0; a < numActions; a++) nodeEV += actionEVs[a]; // simplified
    regrets[tid] = reach[tid] * (actionEVs[tid] - nodeEV);
}

//==============================================================================
// KERNEL: backward_opponent_boundary_iso  (SASS: ~6016-6784 bytes, sm_80)
//   Backward opponent at tree boundary with isomorphism handling.
//==============================================================================
__global__ void backward_opponent_boundary_iso(
    const float * __restrict__ reach,
    const float * __restrict__ actionEVs,
    float       * __restrict__ regrets,
    const int   * __restrict__ iso_map,   // isomorphism permutation
    int   numActions)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numActions) return;
    int j = iso_map[i];
    regrets[i] = reach[j] * actionEVs[j];  // permuted per isomorphism
}

//==============================================================================
// KERNEL: backward_br_player  (SASS: ~3456-3968 bytes, sm_80)
//   Backward player for betting-round (br) boundaries.
//==============================================================================
__global__ void backward_br_player(
    const float * __restrict__ reach,
    const float * __restrict__ evs,
    float       * __restrict__ regrets,
    int   numEntries)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numEntries) return;
    regrets[i] = reach[i] * evs[i];
}

//==============================================================================
// KERNEL: forward_chance / forward_chance_abstract
//   Forward chance node — propagate reach probabilities through chance
//   (community card deals). Very small kernel (~1792-2944 bytes).
//==============================================================================
__global__ void forward_chance(
    const float * __restrict__ reach_in,
    const float * __restrict__ chanceProb,
    float       * __restrict__ reach_out,
    int   numCards)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numCards) return;
    reach_out[i] = reach_in[i] * chanceProb[i];
}

//==============================================================================
// KERNEL: forward_chance_tr_2d / forward_chance_turn_river / _batched
//   Chance propagation for turn+river (2D). Batched variant processes
//   multiple boards in parallel.
//==============================================================================
__global__ void forward_chance_tr_2d(
    const float * __restrict__ reach_in,
    const float * __restrict__ turnProb,
    const float * __restrict__ riverProb,
    float       * __restrict__ reach_out,
    int   numTurn, int   numRiver)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numTurn * numRiver) return;
    int t = i / numRiver;
    int r = i % numRiver;
    reach_out[i] = reach_in[t] * turnProb[t] * riverProb[r];
}

//==============================================================================
// KERNEL: backward_chance_abstract / backward_chance_tr_2d
//   / backward_chance_turn_river / _batched
//   Backward chance propagation — distribute regrets back through chance
//   nodes weighted by chance probabilities.
//==============================================================================
__global__ void backward_chance_abstract(
    const float * __restrict__ reach,
    const float * __restrict__ regrets_in,
    float       * __restrict__ regrets_out,
    const float * __restrict__ chanceProb,
    int   numChance)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numChance) return;
    regrets_out[i] = reach[i] * regrets_in[i] * chanceProb[i];
}

//==============================================================================
// KERNEL: forward_river_subtrees_packed  (SASS: ~17792-18688 bytes, sm_80)
//   Forward pass over river subtrees — packed Q16 variant. Processes all
//   river betting actions for a given board state.
//==============================================================================
__global__ void forward_river_subtrees_packed(
    const int16_t * __restrict__ reach_q16,
    const int16_t * __restrict__ strategy_q16,
    int16_t       * __restrict__ out_q16,
    int   numRiverActions)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numRiverActions) return;
    float r = reach_q16[i] / 65536.0f;
    float s = strategy_q16[i] / 65536.0f;
    out_q16[i] = (int16_t)(r * s * 65536.0f);
}

//==============================================================================
// KERNEL: apply_isomorphism / apply_isomorphism_permuted  (SASS: ~1280-2304 bytes)
//   Tree compaction via graph isomorphism. Swaps isomorphic subtrees.
//   SASS: shared-memory swaps (LDS/STS with X4 = 16-byte), BSSY,
//         BAR.SYNC.DEFER_BLOCKING, PRMT for node-id byte extraction.
//==============================================================================
__global__ void apply_isomorphism(
    int * __restrict__ nodes,     // node array
    const int * __restrict__ perm,// isomorphism permutation
    int   numNodes)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numNodes) return;
    int j = perm[i];
    if (j > i) {
        int tmp = nodes[i];
        nodes[i] = nodes[j];
        nodes[j] = tmp;
    }
}

//==============================================================================
// KERNEL: apply_isomorphism_permuted
//   Permuted variant — applies a precomputed permutation to compact the tree.
//==============================================================================
__global__ void apply_isomorphism_permuted(
    int * __restrict__ nodes,
    const int * __restrict__ perm,
    int   numNodes)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numNodes) return;
    nodes[i] = nodes[perm[i]];
}

//==============================================================================
// KERNEL: encode_stage_segments_i16  (SASS: ~3712-4096 bytes, sm_80)
//   Encodes tree stage segments into int16 packed format for compact archive.
//==============================================================================
__global__ void encode_stage_segments_i16(
    const float * __restrict__ segments,
    int16_t       * __restrict__ out_q16,
    int   numSegments,
    float scale)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numSegments) return;
    out_q16[i] = (int16_t)(segments[i] * scale * 65536.0f);
}

//==============================================================================
// KERNEL: train_fold_showdown_parent_packed  (SASS: ~23040-27776 bytes, sm_80)
//   Main CFR training kernel: processes fold/showdown parent nodes.
//   Packed Q16 variant. Largest training kernel — combines forward reach,
//   backward regret, and strategy update in a single launch.
//==============================================================================
__global__ void train_fold_showdown_parent_packed(
    const int16_t * __restrict__ reach_q16,
    const int16_t * __restrict__ ev_q16,
    int16_t       * __restrict__ regret_q16,
    int16_t       * __restrict__ strategy_q16,
    const float * __restrict__ discount_params, // 3 floats: iter_params
    int   numActions)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numActions) return;
    float r = reach_q16[i] / 65536.0f;
    float e = ev_q16[i] / 65536.0f;
    float regret = r * e;
    regret_q16[i] = (int16_t)(regret * 65536.0f);
    // strategy update via regret matching (SASS: FFMA.FTZ + MUFU.RCP)
    float posRegret = fmaxf(0.0f, regret);
    strategy_q16[i] = (int16_t)(posRegret * 65536.0f);
}

//==============================================================================
// KERNEL: train_opponent_fold_showdown_parent_packed
//   Opponent variant of train_fold_showdown_parent_packed.
//==============================================================================
__global__ void train_opponent_fold_showdown_parent_packed(
    const int16_t * __restrict__ reach_q16,
    const int16_t * __restrict__ ev_q16,
    int16_t       * __restrict__ regret_q16,
    int16_t       * __restrict__ strategy_q16,
    const float * __restrict__ discount_params,
    int   numActions)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numActions) return;
    float r = reach_q16[i] / 65536.0f;
    float e = ev_q16[i] / 65536.0f;
    regret_q16[i] = (int16_t)(r * e * 65536.0f);
}

//==============================================================================
// KERNEL: train_mixed_fold_showdown_parent_packed
//   Mixed (2p + 3p) training kernel for fold/showdown parents.
//==============================================================================
__global__ void train_mixed_fold_showdown_parent_packed(
    const int16_t * __restrict__ reach_q16,
    const int16_t * __restrict__ ev_q16,
    int16_t       * __restrict__ regret_q16,
    int16_t       * __restrict__ strategy_q16,
    int   numActions)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numActions) return;
    float r = reach_q16[i] / 65536.0f;
    float e = ev_q16[i] / 65536.0f;
    regret_q16[i] = (int16_t)(r * e * 65536.0f);
}

//==============================================================================
// KERNEL: train_mixed_opponent_fold_showdown_parent
//   Mixed opponent training kernel.
//==============================================================================
__global__ void train_mixed_opponent_fold_showdown_parent(
    const float * __restrict__ reach,
    const float * __restrict__ evs,
    float       * __restrict__ regrets,
    int   numActions)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numActions) return;
    regrets[i] = reach[i] * evs[i];
}

//==============================================================================
// KERNEL: train_small_tree_fp32_multi_iter  (SASS: ~46208-52096 bytes, sm_80)
//   Largest kernel (~50 KB). Multi-iteration CFR training for small trees.
//   Runs multiple CFR iterations per kernel launch. Combines:
//     forward reach accumulation, backward regret computation,
//     strategy update, discount parameter application.
//   SASS: extensive FFMA.FTZ, MUFU.RCP, FSEL, shared-memory tiling,
//         BSSY barriers, double-precision EV accumulation.
//==============================================================================
__global__ void train_small_tree_fp32_multi_iter(
    const float * __restrict__ reach,
    const float * __restrict__ evs,
    float       * __restrict__ regrets,
    float       * __restrict__ strategy,
    const float * __restrict__ discount_params, // [3]: iter_params
    int   numActions,
    int   numIterations)
{
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= numActions) return;
    float disc = discount_params[0];
    for (int it = 0; it < numIterations; it++) {
        float nodeEV = 0.0f;
        for (int a = 0; a < numActions; a++)
            nodeEV += strategy[a] * evs[a];
        float r = reach[tid];
        float aEV = evs[tid];
        float regret = r * (aEV - nodeEV);
        regrets[tid] = disc * regrets[tid] + (1.0f - disc) * regret;
        float posRegret = fmaxf(0.0f, regrets[tid]);
        strategy[tid] = posRegret;  // normalize elsewhere
    }
}

//==============================================================================
// Host-side launcher config (recovered from solver.solve.2p.run / 3p.run)
//==============================================================================
struct SolverLaunchConfig {
    int   numCombos;
    int   blockSize;     // 256 (confirmed in binary)
    int   gridSize;
    size_t sharedMem;    // bytes for range tiles (per kernel, from KERNEL_INVENTORY.json)
    int   deviceId;
};

__host__ void launch_showdown_kernel(const SolverLaunchConfig &cfg,
                                     const int *d_board,
                                     const int *d_hero, const int *d_vil,
                                     int *d_heroS, int *d_vilS) {
    dim3 grid((cfg.numCombos + cfg.blockSize - 1) / cfg.blockSize);
    dim3 block(cfg.blockSize);
    evaluate_showdown<<<grid, block, 0, 0>>>(d_board, d_hero, d_vil, d_heroS, d_vilS, cfg.numCombos);
}

} // namespace texas
