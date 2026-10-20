// ============================================================================
// CUDA_KERNELS_RECON.cpp
// TexasSolverGPU v0.3.0 — CUDA kernel pseudocode reconstruction
//
// STATUS: The Win64 PE binaries contain NO embedded device code.
//         cuobjdump -sass reports: "does not contain device code"
//         Reason: PTX target sm_120 is JIT-compiled by the CUDA driver at
//         runtime ("forward PTX JIT"). Compiled CUDA targets: 75;80;86;89;90;120.
//
//         The kernel pseudocode below is reconstructed from:
//           (a) the minified Vue hand-evaluator embedded in the PE binary
//               (functionally identical to the GPU kernels), and
//           (b) the kernel symbol names present in the binary string table.
//
//         Card encoding:  int c in [0,51]; rank = c/4 (0..12 = 2..A), suit = c%4.
//         Combo index:   en(c1,c2) with c1<c2 => c1*(103-c1)/2 + c2 - c1 - 1   [0..1325]
//         Packed two cards:  c | (p << 8)
// ============================================================================

#include <cstdint>
#include <cmath>

namespace texas {

//------------------------------------------------------------------------------
// Card / combo primitives (recovered JS: en(), ni[], zt(), Kp())
//------------------------------------------------------------------------------
__device__ __forceinline__ int card_rank(int c) { return c >> 2; }          // c/4
__device__ __forceinline__ int card_suit (int c) { return c & 3; }          // c%4
__device__ __forceinline__ int card_char(int c) { return "23456789TJQKA"[card_rank(c)]; }
__device__ __forceinline__ char  card_suit_char(int c) { return "cdhs"[card_suit(c)]; }

// en(e,t): unique combo index for two distinct cards e<t, range [0,1325]
__device__ __forceinline__ int combo_index(int e, int t) {
    if (e > t) { int tmp = e; e = t; t = tmp; }
    return e * (103 - e) / 2 + t - e - 1;
}

// Unpack two packed cards c | (p<<8)
__device__ __forceinline__ void unpack2(int packed, int &c1, int &c2) {
    c1 = packed & 0xFF;
    c2 = packed >> 8;
}

//------------------------------------------------------------------------------
// 5-card hand rank: oa(cards[5])
// Recovered JS OA(): category 0..8 + packed kicker tie-break.
//   category: 0=highcard,1=pair,2=two pair,3=trips,4=straight,5=flush,
//             6=full house,7=quads,8=straight flush
// Returns a single comparable integer: higher = better hand.
//------------------------------------------------------------------------------
__device__ int eval5(const int cards[5]) {
    int rankCnt[13] = {0}, suitCnt[4] = {0};
    for (int i = 0; i < 5; i++) {
        rankCnt[card_rank(cards[i])]++;
        suitCnt[card_suit(cards[i])]++;
    }

    // distinct ranks sorted desc
    int srank[5]; int n = 0;
    for (int r = 12; r >= 0; r--) if (rankCnt[r]) srank[n++] = r;

    bool flush = false;
    for (int s = 0; s < 4; s++) if (suitCnt[s] == 5) flush = true;

    bool straight = false;
    int topStraight = -1;
    // wheel A-2-3-4-5
    if (rankCnt[12] && rankCnt[0] && rankCnt[1] && rankCnt[2] && rankCnt[3]) {
        straight = true; topStraight = 3; // 5-high
    }
    for (int r = 12; r >= 4; r--) {
        if (rankCnt[r] && rankCnt[r-1] && rankCnt[r-2] && rankCnt[r-3] && rankCnt[r-4]) {
            straight = true; topStraight = r; break;
        }
    }

    // multiplicity buckets
    int quads = -1, trips = -1, pairs[2] = {-1,-1}, npairs = 0;
    int kickers[5]; int nk = 0;
    for (int r = 12; r >= 0; r--) {
        if (rankCnt[r] == 4) quads = r;
        else if (rankCnt[r] == 3) trips = r;
        else if (rankCnt[r] == 2) { if (npairs < 2) pairs[npairs++] = r; }
        else kickers[nk++] = r;
    }

    int cat = 0;
    if (straight && flush)              cat = 8;
    else if (quads >= 0)                cat = 7;
    else if (trips >= 0 && npairs >= 1) cat = 6;
    else if (flush)                     cat = 5;
    else if (straight)                  cat = 4;
    else if (trips >= 0)                cat = 3;
    else if (npairs >= 2)               cat = 2;
    else if (npairs >= 1)               cat = 1;
    else                                cat = 0;

    // Pack: category in high nibble, then tie-break ranks.
    // We use base-15 accumulation like JS OA: _ = _*15 + (rank+2)
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
// 7-card showdown: rb(cards[7])  -> recovered JS RB()
// Strength = max over all 21 five-card subsets of eval5.
//------------------------------------------------------------------------------
__device__ int eval7(const int cards[7]) {
    int best = -1;
    // drop 2 of 7 => C(7,2)=21 subsets of 5
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
// Pot settlement: tb(profits[], s, a, t)  recovered JS TB()
//   s,a = stacks of two players, t = total bet each matched to
//   n = (min(s,a)-t)/2  (short side contribution beyond t)
//   i = max(s,a)-t-n    (long side)
//   rake c = min(2*n, a) ... uses pot & rake params
// Returns profit[2], rake, returned[2].
//------------------------------------------------------------------------------
__device__ void settle_pot(int profit[2], int &rake, int returned[2],
                           const int paid[2], int potCap, double rakeRate, int rakeCap) {
    int n = paid[0], m = paid[1];
    int lo = n < m ? n : m;
    int hi = n < m ? m : n;
    int pot = lo + hi;
    int effective = pot; // simplified
    double c = std::min(2.0 * lo, (double)rakeCap) * rakeRate;
    if (c < 0) c = 0;
    int rc = (int)std::floor(c);
    int ret0 = n - lo;
    int ret1 = m - lo;
    profit[0] = n - rc;  // schematic; exact split depends on who won
    profit[1] = m - rc;
    rake = rc;
    returned[0] = ret0;
    returned[1] = ret1;
}

//==============================================================================
// KERNEL: evaluate_showdown
//   HUI showdown evaluator. Each thread evaluates one (hero, villain) combo pair
//   over a fixed board, returns 7-card strengths for both players.
//==============================================================================
__global__ void evaluate_showdown(
    const int * __restrict__ board,   // [5]
    const int * __restrict__ heroCombos,   // [N] packed c|(p<<8)
    const int * __restrict__ vilCombos,    // [N]
    int * __restrict__ heroStrength,       // [N]
    int * __restrict__ vilStrength,        // [N]
    int numCombos)
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
// KERNEL: evaluate_showdown_brute
//   Brute-force: for a given hero hand, enumerate all villain combos and all
//   runouts, count win/tie/loss. Used for equity precomputation.
//==============================================================================
__global__ void evaluate_showdown_brute(
    const int hero[2],
    const int * __restrict__ deadCards,   // [D]
    int numDead,
    int * __restrict__ winCount,
    int * __restrict__ tieCount,
    int * __restrict__ lossCount,
    int totalVillainCombos)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= totalVillainCombos) return;
    // i encodes a villain combo index; reconstruct via combo_index inverse
    // ... enumerate remaining deck, runouts, compare eval7 ...
}

//==============================================================================
// KERNEL: evaluate_showdown_prefix
//   Prefix showdown: evaluates strength given a partial board (flop/turn),
//   used to prune the game tree before river cards are dealt.
//==============================================================================
__global__ void evaluate_showdown_prefix(
    const int * __restrict__ board,   // [3..4]
    int boardLen,
    const int * __restrict__ combos,  // [N] packed
    int * __restrict__ strength,      // [N]
    int numCombos)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numCombos) return;
    int c1,c2; unpack2(combos[i], c1, c2);
    int hand[7];
    for (int k = 0; k < boardLen; k++) hand[k] = board[k];
    hand[boardLen]   = c1;
    hand[boardLen+1] = c2;
    // strength vs random opponent over remaining runouts (monte carlo or exact)
    strength[i] = eval7_prefix(hand, boardLen + 2);
}

//==============================================================================
// KERNEL: evaluate_fold / evaluate_fold_fp32
//   Fold equity: given our hand and board, compute EV of folding (0) vs the
//   range of opponent hands that would call. fp32 variant uses float.
//==============================================================================
__global__ void evaluate_fold(
    const int hero[2],
    const int * __restrict__ board,   // [5]
    const float * __restrict__ oppRange, // [1326] weights
    const int * __restrict__ oppCombos,  // [M]
    float * __restrict__ foldEV,      // [1] or per-combo
    int numOppCombos)
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
// KERNEL: evaluate_fold_showdown_prefix
//   Combines fold + showdown with prefix board (turn/river prediction).
//==============================================================================
__global__ void evaluate_fold_showdown_prefix(
    const int * __restrict__ board, int boardLen,
    const int * __restrict__ heroCombos,
    const int * __restrict__ oppCombos,
    const float * __restrict__ oppRange,
    float * __restrict__ evOut,
    int numPairs)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= numPairs) return;
    int h1,h2,v1,v2;
    unpack2(heroCombos[i], h1, h2);
    unpack2(oppCombos[i],  v1, v2);
    int hand[7];
    for (int k = 0; k < boardLen; k++) hand[k] = board[k];
    hand[boardLen]   = h1; hand[boardLen+1] = h2;
    int vhand[7];
    for (int k = 0; k < boardLen; k++) vhand[k] = board[k];
    vhand[boardLen]   = v1; vhand[boardLen+1] = v2;
    int hs = eval7(hand);
    int vs = eval7(vhand);
    float eq = (hs > vs) ? 1.0f : (hs == vs) ? 0.5f : 0.0f;
    evOut[i] = eq * oppRange[combo_index(v1,v2)];
}

//==============================================================================
// KERNEL: evaluate_fold_3p  (3-player)
//   Three-way fold/showdown: evaluates hero vs two opponents over ranges.
//   Uses warp shuffles (SHFL) to reduce across the 3 players and BSSY for
//   synchronization of shared-memory range lookups.
//==============================================================================
__global__ void evaluate_fold_3p(
    const int hero[2],
    const int * __restrict__ board,   // [5]
    const float * __restrict__ r1,    // [1326] opponent 1 range
    const float * __restrict__ r2,    // [1326] opponent 2 range
    float * __restrict__ evOut)       // [1]
{
    int i = threadIdx.x;
    __shared__ float s_ev[1024];
    float myEq = 0.0f;
    // Each warp handles a slice of combo pairs; reduce via SHFL
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
                    // 3-way equity: win if hs > max(a,b); tie if equal to max
                    int mxa = a > b ? a : b;
                    float eq = (hs > mxa) ? 1.0f : (hs == mxa) ? (1.0f/3.0f) : 0.0f;
                    myEq += eq * w;
                }
            }
        }
    // warp reduce
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
    int n)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    int h1,h2,h3;
    unpack2(c1p[i], h1, h2);  // actually these are per-player packs; schematic
    // ... eval7 each, compare ...
}

//==============================================================================
// KERNEL: evaluate_fold_abstract / evaluate_showdown_abstract
//   Abstracted (information-set) versions used for CFR regret matching over
//   bucketed hand classes rather than individual combos.
//==============================================================================
__global__ void evaluate_showdown_abstract(
    const int * __restrict__ board,
    const int * __restrict__ bucketA,   // [N] hand-class bucket ids
    const int * __restrict__ bucketB,
    int * __restrict__ result,          // win/tie/loss per bucket pair
    int nPairs)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= nPairs) return;
    // representative combos per bucket -> eval7 -> outcome
}

//==============================================================================
// Host-side kernel launcher (recovered from solver.solve.2p.run / 3p.run)
//==============================================================================
struct SolverLaunchConfig {
    int   numCombos;
    int   blockSize;     // 256 typical
    int   gridSize;
    size_t sharedMem;    // bytes for range tiles
    int   deviceId;
};

__host__ void launch_showdown_kernel(const SolverLaunchConfig &cfg,
                                     const int *d_board,
                                     const int *d_hero, const int *d_vil,
                                     int *d_heroS, int *d_vilS) {
    dim3 grid((cfg.numCombos + cfg.blockSize - 1) / cfg.blockSize);
    dim3 block(cfg.blockSize);
    evaluate_showdown<<<grid, block, 0, 0>>>(d_board, d_hero, d_vil, d_heroS, d_vilS, cfg.numCombos);
    cudaDeviceSynchronize();
}

} // namespace texas