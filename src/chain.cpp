// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>    // Thor: Forge
#include <util.h>    // Thor: Forge
#include <rpc/blockchain.h>     // Thor: Forge 1.1
#include <validation.h>         // Thor: Forge 1.1

/**
 * CChain implementation
 */
void CChain::SetTip(CBlockIndex *pindex) {
    if (pindex == nullptr) {
        vChain.clear();
        return;
    }
    vChain.resize(pindex->nHeight + 1);
    while (pindex && vChain[pindex->nHeight] != pindex) {
        vChain[pindex->nHeight] = pindex;
        pindex = pindex->pprev;
    }
}

CBlockLocator CChain::GetLocator(const CBlockIndex *pindex) const {
    int nStep = 1;
    std::vector<uint256> vHave;
    vHave.reserve(32);

    if (!pindex)
        pindex = Tip();
    while (pindex) {
        vHave.push_back(pindex->GetBlockHash());
        // Stop when we have added the genesis block.
        if (pindex->nHeight == 0)
            break;
        // Exponentially larger steps back, plus the genesis block.
        int nHeight = std::max(pindex->nHeight - nStep, 0);
        if (Contains(pindex)) {
            // Use O(1) CChain index if possible.
            pindex = (*this)[nHeight];
        } else {
            // Otherwise, use O(log n) skiplist.
            pindex = pindex->GetAncestor(nHeight);
        }
        if (vHave.size() > 10)
            nStep *= 2;
    }

    return CBlockLocator(vHave);
}

const CBlockIndex *CChain::FindFork(const CBlockIndex *pindex) const {
    if (pindex == nullptr) {
        return nullptr;
    }
    if (pindex->nHeight > Height())
        pindex = pindex->GetAncestor(Height());
    while (pindex && !Contains(pindex))
        pindex = pindex->pprev;
    return pindex;
}

CBlockIndex* CChain::FindEarliestAtLeast(int64_t nTime) const
{
    std::vector<CBlockIndex*>::const_iterator lower = std::lower_bound(vChain.begin(), vChain.end(), nTime,
        [](CBlockIndex* pBlock, const int64_t& time) -> bool { return pBlock->GetBlockTimeMax() < time; });
    return (lower == vChain.end() ? nullptr : *lower);
}

/** Turn the lowest '1' bit in the binary representation of a number into a '0'. */
int static inline InvertLowestOne(int n) { return n & (n - 1); }

/** Compute what height to jump back to with the CBlockIndex::pskip pointer. */
int static inline GetSkipHeight(int height) {
    if (height < 2)
        return 0;

    // Determine which height to jump back to. Any number strictly lower than height is acceptable,
    // but the following expression seems to perform well in simulations (max 110 steps to go back
    // up to 2**18 blocks).
    return (height & 1) ? InvertLowestOne(InvertLowestOne(height - 1)) + 1 : InvertLowestOne(height);
}

const CBlockIndex* CBlockIndex::GetAncestor(int height) const
{
    if (height > nHeight || height < 0) {
        return nullptr;
    }

    const CBlockIndex* pindexWalk = this;
    int heightWalk = nHeight;
    while (heightWalk > height) {
        int heightSkip = GetSkipHeight(heightWalk);
        int heightSkipPrev = GetSkipHeight(heightWalk - 1);
        if (pindexWalk->pskip != nullptr &&
            (heightSkip == height ||
             (heightSkip > height && !(heightSkipPrev < heightSkip - 2 &&
                                       heightSkipPrev >= height)))) {
            // Only follow pskip if pprev->pskip isn't better than pskip->pprev.
            pindexWalk = pindexWalk->pskip;
            heightWalk = heightSkip;
        } else {
            assert(pindexWalk->pprev);
            pindexWalk = pindexWalk->pprev;
            heightWalk--;
        }
    }
    return pindexWalk;
}

CBlockIndex* CBlockIndex::GetAncestor(int height)
{
    return const_cast<CBlockIndex*>(static_cast<const CBlockIndex*>(this)->GetAncestor(height));
}

void CBlockIndex::BuildSkip()
{
    if (pprev)
        pskip = pprev->GetAncestor(GetSkipHeight(nHeight));
}

// Thor: Forge: Grant forge-mined blocks bonus work value - they get the work value of
// their own block plus that of the PoW block behind them
arith_uint256 GetBlockProof(const CBlockIndex& block)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    bool verbose = false;//LogAcceptCategory(BCLog::FORGE);

    arith_uint256 bnTarget;
    bool fNegative;
    bool fOverflow;

    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget == 0)
        return 0;

    // We need to compute 2**256 / (bnTarget+1), but we can't represent 2**256
    // as it's too large for an arith_uint256. However, as 2**256 is at least as large
    // as bnTarget+1, it is equal to ((2**256 - bnTarget - 1) / (bnTarget+1)) + 1,
    // or ~bnTarget / (bnTarget+1) + 1.
    arith_uint256 bnTargetScaled = (~bnTarget / (bnTarget + 1)) + 1;

    if (block.GetBlockHeader().IsForgeMined(consensusParams)) {
        assert(block.pprev);

        // LitecoinCash: Hive 1.1: Set bnPreviousTarget from nBits in most recent pow block, not just assuming it's one back. Note this logic is still valid for Hive 1.0 so doesn't need to be gated.
        CBlockIndex* pindexTemp = block.pprev;
        while (pindexTemp->GetBlockHeader().IsForgeMined(consensusParams)) {
            assert(pindexTemp->pprev);
            pindexTemp = pindexTemp->pprev;
        }

        arith_uint256 bnPreviousTarget;
        bnPreviousTarget.SetCompact(pindexTemp->nBits, &fNegative, &fOverflow);
        if (fNegative || fOverflow || bnPreviousTarget == 0)
            return 0;
        bnTargetScaled += (~bnPreviousTarget / (bnPreviousTarget + 1)) + 1;


        // Forge 1.1: Enable bonus chainwork for Forge blocks
        if ((IsForge11Enabled(&block, consensusParams)) && (!IsForge12Enabled(&block, consensusParams))) {
             if (verbose) {
            		LogPrintf("**** FORGE-1.1: ENABLING BONUS CHAINWORK ON FORGE BLOCK %s\n", block.GetBlockHash().ToString());
            		LogPrintf("**** Initial block chainwork = %s\n", bnTargetScaled.ToString());
	     }
            double forgeDiff = GetDifficulty(&block, true);                                  // Current forge diff
            if (verbose) LogPrintf("**** Forge diff = %.12f\n", forgeDiff);
            unsigned int k = floor(std::min(forgeDiff/consensusParams.maxForgeDiff, 1.0) * (consensusParams.maxK - consensusParams.minK) + consensusParams.minK);

            bnTargetScaled *= k;
	    if (verbose) {
		    LogPrintf("**** k = %d\n", k);
		    LogPrintf("**** Final scaled chainwork =  %s\n", bnTargetScaled.ToString());
	    }
        }

	// Forge 1.2: Enable bonus chainwork for Forge blocks
        if (IsForge12Enabled(&block, consensusParams)) {
          
            double forgeDiff = GetDifficulty(&block, true);
          
            unsigned int k = floor(std::min(forgeDiff/consensusParams.maxForgeDiff, 1.0) * (consensusParams.maxK2 - consensusParams.minK2) + consensusParams.minK2);

            bnTargetScaled *= k;

        }

    // Forge 1.1: Enable bonus chainwork for PoW blocks
    } else if ((IsForge11Enabled(&block, consensusParams)) && (!IsForge12Enabled(&block, consensusParams))) {
	if (verbose) {
        	LogPrintf("**** FORGE-1.1: CHECKING FOR BONUS CHAINWORK ON POW BLOCK %s\n", block.GetBlockHash().ToString());
        	LogPrintf("**** Initial block chainwork = %s\n", bnTargetScaled.ToString());
	}

        // Find last forge block
        CBlockIndex *currBlock = block.pprev;
        int blocksSinceForge;
        double lastForgeDifficulty = 0;

        for (blocksSinceForge = 0; blocksSinceForge < consensusParams.maxKPow; blocksSinceForge++) {
            if (currBlock->GetBlockHeader().IsForgeMined(consensusParams)) {
                lastForgeDifficulty = GetDifficulty(currBlock, true);
                if (verbose) LogPrintf("**** Got last Forge diff = %.12f, at %s\n", lastForgeDifficulty, currBlock->GetBlockHash().ToString());
                break;
            }

            assert(currBlock->pprev);
            currBlock = currBlock->pprev;
        }

        if (verbose) LogPrintf("**** Pow blocks since last Forge block = %d\n", blocksSinceForge);

        // Apply k scaling
        unsigned int k = consensusParams.maxKPow - blocksSinceForge;
        if (lastForgeDifficulty < consensusParams.powSplit1)
            k = k >> 1;
        if (lastForgeDifficulty < consensusParams.powSplit2)
            k = k >> 1;

        if (k < 1)
            k = 1;

        bnTargetScaled *= k;

	if (verbose) {
        	LogPrintf("**** k = %d\n", k);
        	LogPrintf("**** Final scaled chainwork =  %s\n", bnTargetScaled.ToString());
	}

    } else if (IsForge12Enabled(&block, consensusParams)) {

        CBlockIndex *currBlock = block.pprev;
        int blocksSinceForge;
        double lastForgeDifficulty = 0;

        for (blocksSinceForge = 0; blocksSinceForge < consensusParams.maxKPow; blocksSinceForge++) {
            if (currBlock->GetBlockHeader().IsForgeMined(consensusParams)) {
                lastForgeDifficulty = GetDifficulty(currBlock, true);

                break;
            }

            assert(currBlock->pprev);
            currBlock = currBlock->pprev;
        }

        unsigned int k = consensusParams.maxKPow - blocksSinceForge;
        if (lastForgeDifficulty < consensusParams.powSplit1)
            k = k >> 1;
        if (lastForgeDifficulty < consensusParams.powSplit2)
            k = k >> 1;

        if (k < 1)
            k = 1;

        bnTargetScaled *= k;

    }

    return bnTargetScaled;
}

// LitecoinCash: Hive: Use this to compute estimated hashes for GetNetworkHashPS()
arith_uint256 GetNumHashes(const CBlockIndex& block)
{
    arith_uint256 bnTarget;
    bool fNegative;
    bool fOverflow;

    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget == 0 || block.GetBlockHeader().IsForgeMined(Params().GetConsensus()))
        return 0;

    // We need to compute 2**256 / (bnTarget+1), but we can't represent 2**256
    // as it's too large for an arith_uint256. However, as 2**256 is at least as large
    // as bnTarget+1, it is equal to ((2**256 - bnTarget - 1) / (bnTarget+1)) + 1,
    // or ~bnTarget / (bnTarget+1) + 1.
    return (~bnTarget / (bnTarget + 1)) + 1;
}

int64_t GetBlockProofEquivalentTime(const CBlockIndex& to, const CBlockIndex& from, const CBlockIndex& tip, const Consensus::Params& params)
{
    arith_uint256 r;
    int sign = 1;
    if (to.nChainWork > from.nChainWork) {
        r = to.nChainWork - from.nChainWork;
    } else {
        r = from.nChainWork - to.nChainWork;
        sign = -1;
    }
    r = r * arith_uint256(params.nPowTargetSpacing) / GetBlockProof(tip);
    if (r.bits() > 63) {
        return sign * std::numeric_limits<int64_t>::max();
    }
    return sign * r.GetLow64();
}

/** Find the last common ancestor two blocks have.
 *  Both pa and pb must be non-nullptr. */
const CBlockIndex* LastCommonAncestor(const CBlockIndex* pa, const CBlockIndex* pb) {
    if (pa->nHeight > pb->nHeight) {
        pa = pa->GetAncestor(pb->nHeight);
    } else if (pb->nHeight > pa->nHeight) {
        pb = pb->GetAncestor(pa->nHeight);
    }

    while (pa != pb && pa && pb) {
        pa = pa->pprev;
        pb = pb->pprev;
    }

    // Eventually all chain branches meet at the genesis block.
    assert(pa == pb);
    return pa;
}
