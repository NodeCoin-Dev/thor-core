// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>
#include <util.h>
#include <core_io.h>            // Thor: Forge
#include <script/standard.h>    // Thor: Forge
#include <base58.h>             // Thor: Forge
#include <pubkey.h>             // Thor: Forge
#include <hash.h>               // Thor: Forge
#include <sync.h>               // Thor: Forge
#include <validation.h>         // Thor: Forge
#include <utilstrencodings.h>   // Thor: Forge

HammerPopGraphPoint hammerPopGraph[1024*40];       // Thor: Forge

// Thor: DarkGravity V3 (https://github.com/dashpay/dash/blob/master/src/pow.cpp#L82)
// By Evan Duffield <evan@dash.org>
unsigned int DarkGravityWave(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);   // Thor: Note we use the sha256 pow limit here!
    int64_t nPastBlocks = 24;

    // Thor: Allow minimum difficulty blocks if we haven't seen a block for ostensibly 10 blocks worth of time
    if (params.fPowAllowMinDifficultyBlocks && pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 10)
        return bnPowLimit.GetCompact();

    // LitecoinCash: Forge 1.1: Skip over Forgemined blocks at tip
    if (IsForge11Enabled(pindexLast, params)) {
        while (pindexLast->GetBlockHeader().IsForgeMined(params)) {
            //LogPrintf("DarkGravityWave: Skipping forgemined block at %i\n", pindex->nHeight);
            assert(pindexLast->pprev); // should never fail
            pindexLast = pindexLast->pprev;
        }
    }

    // Thor: Make sure we have at least (nPastBlocks + 1) blocks since the fork, otherwise just return powLimitSHA
    if (!pindexLast || pindexLast->nHeight - params.lastScryptBlock < nPastBlocks)
        return bnPowLimit.GetCompact();

    const CBlockIndex *pindex = pindexLast;
    arith_uint256 bnPastTargetAvg;

    for (unsigned int nCountBlocks = 1; nCountBlocks <= nPastBlocks; nCountBlocks++) {
        // Thor: Forge: Skip over Forgemined blocks; we only want to consider PoW blocks
        while (pindex->GetBlockHeader().IsForgeMined(params)) {
            //LogPrintf("DarkGravityWave: Skipping forgemined block at %i\n", pindex->nHeight);
            assert(pindex->pprev); // should never fail
            pindex = pindex->pprev;
        }

        arith_uint256 bnTarget = arith_uint256().SetCompact(pindex->nBits);
        if (nCountBlocks == 1) {
            bnPastTargetAvg = bnTarget;
        } else {
            // NOTE: that's not an average really...
            bnPastTargetAvg = (bnPastTargetAvg * nCountBlocks + bnTarget) / (nCountBlocks + 1);
        }

        if(nCountBlocks != nPastBlocks) {
            assert(pindex->pprev); // should never fail
            pindex = pindex->pprev;
        }
    }

    arith_uint256 bnNew(bnPastTargetAvg);

    int64_t nActualTimespan = pindexLast->GetBlockTime() - pindex->GetBlockTime();
    // NOTE: is this accurate? nActualTimespan counts it for (nPastBlocks - 1) blocks only...
    int64_t nTargetTimespan = nPastBlocks * params.nPowTargetSpacing;

    if (nActualTimespan < nTargetTimespan/3)
        nActualTimespan = nTargetTimespan/3;
    if (nActualTimespan > nTargetTimespan*3)
        nActualTimespan = nTargetTimespan*3;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= nTargetTimespan;

    // Thor : Limit "High Hash" Attacks... Progressively lower mining difficulty if too high...
    if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 30){
	//LogPrintf("DarkGravityWave: 30 minutes without a block !! Resetting difficulty ! OLD Target = %s\n", bnNew.ToString());
	bnNew = bnPowLimit;
	//LogPrintf("DarkGravityWave: 30 minutes without a block !! Resetting difficulty ! NEW Target = %s\n", bnNew.ToString());
    }

    else if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 25){
	//LogPrintf("DarkGravityWave: 25 minutes without a block. OLD Target = %s\n", bnNew.ToString());
	bnNew *= 100000;
	//LogPrintf("DarkGravityWave: 25 minutes without a block. NEW Target = %s\n", bnNew.ToString());
    }

    else if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 20){
	//LogPrintf("DarkGravityWave: 20 minutes without a block. OLD Target = %s\n", bnNew.ToString());
	bnNew *= 10000;
	//LogPrintf("DarkGravityWave: 20 minutes without a block. NEW Target = %s\n", bnNew.ToString());
    }

    else if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 15){
	//LogPrintf("DarkGravityWave: 15 minutes without a block. OLD Target = %s\n", bnNew.ToString());
	bnNew *= 1000;
	//LogPrintf("DarkGravityWave: 15 minutes without a block. NEW Target = %s\n", bnNew.ToString());
    }

    else if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 10){
	//LogPrintf("DarkGravityWave: 10 minutes without a block. OLD Target = %s\n", bnNew.ToString());
	bnNew *= 100;
	//LogPrintf("DarkGravityWave: 10 minutes without a block. NEW Target = %s\n", bnNew.ToString());
    }

    else {
	bnNew = bnNew;
	//LogPrintf("DarkGravityWave: no stale tip over 10m detected yet so target = %s\n", bnNew.ToString());
    }

    if (bnNew > bnPowLimit) {
        bnNew = bnPowLimit;
	//LogPrintf("DarkGravityWave: target too low, so target is minimum which is = %s\n", bnNew.ToString());
    }

    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequiredLTC(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    // Thor: This fixes an issue where a 51% attack can change difficulty at will.
    // Go back the full period unless it's the first retarget after genesis. Code courtesy of Art Forz
    int blockstogoback = params.DifficultyAdjustmentInterval()-1;
    if ((pindexLast->nHeight+1) != params.DifficultyAdjustmentInterval())
        blockstogoback = params.DifficultyAdjustmentInterval();

    // Go back by what we want to be 14 days worth of blocks
    const CBlockIndex* pindexFirst = pindexLast;
    for (int i = 0; pindexFirst && i < blockstogoback; i++)
        pindexFirst = pindexFirst->pprev;

    assert(pindexFirst);

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);

    // LitecoinCash: If past fork time, use Dark Gravity Wave
    if (pindexLast->nHeight >= params.lastScryptBlock)
        return DarkGravityWave(pindexLast, pblock, params);
    else
        return GetNextWorkRequiredLTC(pindexLast, pblock, params);
}



unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    arith_uint256 bnNew;
    arith_uint256 bnOld;
    bnNew.SetCompact(pindexLast->nBits);
    bnOld = bnNew;
    // Thor: intermediate uint256 can overflow by 1 bit
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    bool fShift = bnNew.bits() > bnPowLimit.bits() - 1;
    if (fShift)
        bnNew >>= 1;
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;
    if (fShift)
        bnNew <<= 1;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}

// LitecoinCash: Forge 1.1: SMA Forge Difficulty Adjust
unsigned int GetNextForge11WorkRequired(const CBlockIndex* pindexLast, const Consensus::Params& params) {
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimitForge);

    arith_uint256 hammerHashTarget = 0;
    int forgeBlockCount = 0;
    int totalBlockCount = 0;

    // Step back till we have found 24 hive blocks, or we ran out...
    while (forgeBlockCount < params.forgeDifficultyWindow && pindexLast->pprev && pindexLast->nHeight >= params.minForgeCheckBlock) {
        if (pindexLast->GetBlockHeader().IsForgeMined(params)) {
            hammerHashTarget += arith_uint256().SetCompact(pindexLast->nBits);
            forgeBlockCount++;
        }
	totalBlockCount++;
        pindexLast = pindexLast->pprev;
    }

    if (forgeBlockCount == 0) {
        LogPrintf("GetNextForge11WorkRequired: No previous forge blocks found.\n");
        return bnPowLimit.GetCompact();
    }

    hammerHashTarget /= forgeBlockCount;    // Average the hammer hash targets in window

    // Retarget
    hammerHashTarget *= forgeBlockCount;
    hammerHashTarget /= forgeBlockCount;

    if (hammerHashTarget > bnPowLimit)
        hammerHashTarget = bnPowLimit;

    return hammerHashTarget.GetCompact();
}

// LitecoinCash: Forge 1.1: SMA Forge Difficulty Adjust
unsigned int GetNextForge12WorkRequired(const CBlockIndex* pindexLast, const Consensus::Params& params) {
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimitForge2);

    arith_uint256 hammerHashTarget = 0;
    int forgeBlockCount = 0;
    int totalBlockCount = 0;

    // Step back till we have found 24 hive blocks, or we ran out...
    while (forgeBlockCount < params.forgeDifficultyWindow2 && pindexLast->pprev && pindexLast->nHeight >= params.minForgeCheckBlock) {
        if (pindexLast->GetBlockHeader().IsForgeMined(params)) {
            hammerHashTarget += arith_uint256().SetCompact(pindexLast->nBits);
            forgeBlockCount++;
        }
	totalBlockCount++;
        pindexLast = pindexLast->pprev;
    }

    if (forgeBlockCount == 0) {
        LogPrintf("GetNextForge11WorkRequired: No previous forge blocks found.\n");
        return bnPowLimit.GetCompact();
    }

    hammerHashTarget /= forgeBlockCount;    // Average the hammer hash targets in window

    // Retarget
    hammerHashTarget *= forgeBlockCount;
    hammerHashTarget /= forgeBlockCount;

    if (hammerHashTarget > bnPowLimit)
        hammerHashTarget = bnPowLimit;

    return hammerHashTarget.GetCompact();
}

unsigned int GetNextForge13WorkRequired(const CBlockIndex* pindexLast, const Consensus::Params& params) {
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimitForge2);

    arith_uint256 hammerHashTarget = 0;
    int forgeBlockCount = 0;
    int targetBlockCount = params.forgeDifficultyWindow2 / params.forgeBlockSpacingTarget;

    CBlockHeader block;
    for(int i = 0; i < params.forgeDifficultyWindow2; i++) {
        if (!pindexLast->pprev || pindexLast->nHeight < params.minForgeCheckBlock) {   // Not enough sampling window
            LogPrintf("GetNextForge13WorkRequired: Not enough blocks in sampling window.\n");
            return bnPowLimit.GetCompact();
        }

        block = pindexLast->GetBlockHeader();
        if (block.IsForgeMined(params)) {
            hammerHashTarget += arith_uint256().SetCompact(pindexLast->nBits);
            forgeBlockCount++;
        }
        pindexLast = pindexLast->pprev;
    }

    if (forgeBlockCount == 0)
        return bnPowLimit.GetCompact();

    hammerHashTarget /= forgeBlockCount;    // Average the hammer hash targets in window

    // Retarget
    hammerHashTarget *= targetBlockCount;
    hammerHashTarget /= forgeBlockCount;

    if (hammerHashTarget > bnPowLimit)
        hammerHashTarget = bnPowLimit;

    return hammerHashTarget.GetCompact();
}

// Thor: Forge: Get the current Hammer Hash Target
unsigned int GetNextForgeWorkRequired(const CBlockIndex* pindexLast, const Consensus::Params& params) {
   // LitecoinCash: Hive 1.1: Use SMA diff adjust
    if ((IsForge11Enabled(pindexLast, params)) && (!IsForge12Enabled(pindexLast, params)) $$ (!IsForge13Enabled(pindexLast, params))) 
        return GetNextForge11WorkRequired(pindexLast, params);

    if ((IsForge11Enabled(pindexLast, params)) && (IsForge12Enabled(pindexLast, params)) $$ (!IsForge13Enabled(pindexLast, params)))
	return GetNextForge12WorkRequired(pindexLast, params);

    if (IsForge13Enabled(pindexLast->nHeight))
        return GetNextForge13WorkRequired(pindexLast, params);


    const arith_uint256 bnPowLimit = UintToArith256(params.powLimitForge);
    const arith_uint256 bnPowLimit2 = UintToArith256(params.powLimitForge2);
    const arith_uint256 bnImpossible = 0;
    arith_uint256 hammerHashTarget;

    //LogPrintf("GetNextForgeWorkRequired: Height     = %i\n", pindexLast->nHeight);

    int numPowBlocks = 0;
    CBlockHeader block;
    while (true) {
        if (!pindexLast->pprev || pindexLast->nHeight < params.minForgeCheckBlock) {   // Ran out of blocks without finding a Forge block? Return min target
            LogPrintf("GetNextForgeWorkRequired: No forgemined blocks found in history\n");
            //LogPrintf("GetNextForgeWorkRequired: This target= %s\n", bnPowLimit.ToString());
            if (IsForge12Enabled(pindexLast, params))
            	return bnPowLimit2.GetCompact();
	    else
		return bnPowLimit.GetCompact();
        }

        block = pindexLast->GetBlockHeader();
        if (block.IsForgeMined(params)) {  // Found the last Forge block; pick up its hammer hash target
            hammerHashTarget.SetCompact(block.nBits);
            break;
        }

        pindexLast = pindexLast->pprev;
        numPowBlocks++;
    }

    //LogPrintf("GetNextForgeWorkRequired: powBlocks  = %i\n", numPowBlocks);
    if (numPowBlocks == 0)
        return bnImpossible.GetCompact();

    //LogPrintf("GetNextForgeWorkRequired: Last target= %s\n", hammerHashTarget.ToString());

	// Apply EMA
	int interval = params.forgeTargetAdjustAggression / params.forgeBlockSpacingTarget;
	hammerHashTarget *= (interval - 1) * params.forgeBlockSpacingTarget + numPowBlocks + numPowBlocks;
	hammerHashTarget /= (interval + 1) * params.forgeBlockSpacingTarget;

	// Clamp to min difficulty
	if ((hammerHashTarget > bnPowLimit2) && (IsForge12Enabled(pindexLast, params)))
		hammerHashTarget = bnPowLimit2;
	if ((hammerHashTarget > bnPowLimit2) && (!IsForge12Enabled(pindexLast, params)))
		hammerHashTarget = bnPowLimit;

    //LogPrintf("GetNextForgeWorkRequired: This target= %s\n", hammerHashTarget.ToString());

    return hammerHashTarget.GetCompact();
}

// Thor: Forge: Get count of all live and gestating BCTs on the network
bool GetNetworkForgeInfo(int& createdHammers, int& createdBCTs, int& readyHammers, int& readyBCTs, CAmount& potentialLifespanRewards, const Consensus::Params& consensusParams, bool recalcGraph) {
    int totalHammerLifespan = consensusParams.hammerLifespanBlocks + consensusParams.hammerGestationBlocks;
    createdHammers = createdBCTs = readyHammers = readyBCTs = 0;
    
    CBlockIndex* pindexPrev = chainActive.Tip();
    assert(pindexPrev != nullptr);
    int tipHeight = pindexPrev->nHeight;
        // LitecoinCash: Hive 1.1: Use correct typical spacing
    if (IsForge11Enabled(pindexPrev, consensusParams))
        potentialLifespanRewards = (consensusParams.hammerLifespanBlocks * GetBlockSubsidy(pindexPrev->nHeight, consensusParams)) / consensusParams.forgeBlockSpacingTargetTypical_1_1;
    else
        potentialLifespanRewards = (consensusParams.hammerLifespanBlocks * GetBlockSubsidy(pindexPrev->nHeight, consensusParams)) / consensusParams.forgeBlockSpacingTargetTypical;

    if (recalcGraph) {
        for (int i = 0; i < totalHammerLifespan; i++) {
            hammerPopGraph[i].createdPop = 0;
            hammerPopGraph[i].readyPop = 0;
        }
    }

    if (IsInitialBlockDownload())   // Refuse if we're downloading
        return false;

    // Count hammers in next blockCount blocks
    CBlock block;
    CScript scriptPubKeyBCF = GetScriptForDestination(DecodeDestination(consensusParams.hammerCreationAddress));
    CScript scriptPubKeyCF = GetScriptForDestination(DecodeDestination(consensusParams.forgeCommunityAddress));

    for (int i = 0; i < totalHammerLifespan; i++) {
        if (fHavePruned && !(pindexPrev->nStatus & BLOCK_HAVE_DATA) && pindexPrev->nTx > 0) {
            LogPrintf("! GetNetworkForgeInfo: Warn: Block not available (pruned data); can't calculate network hammer count.");
            return false;
        }

        if (!pindexPrev->GetBlockHeader().IsForgeMined(consensusParams)) {                          // Don't check Forgemined blocks (no BCTs will be found in them)
            if (!ReadBlockFromDisk(block, pindexPrev, consensusParams)) {
                LogPrintf("! GetNetworkForgeInfo: Warn: Block not available (not found on disk); can't calculate network hammer count.");
                return false;
            }
            int blockHeight = pindexPrev->nHeight;
            CAmount hammerCost = GetHammerCost(blockHeight, consensusParams);
            if (block.vtx.size() > 0) {
                for(const auto& tx : block.vtx) {
                    CAmount hammerFeePaid;
                    if (tx->IsBCT(consensusParams, scriptPubKeyBCF, &hammerFeePaid)) {                 // If it's a BCT, total its hammers
                        if (tx->vout.size() > 1 && tx->vout[1].scriptPubKey == scriptPubKeyCF) {    // If it has a community fund contrib...
                            CAmount donationAmount = tx->vout[1].nValue;
                            CAmount expectedDonationAmount = (hammerFeePaid + donationAmount) / consensusParams.communityContribFactor;  // ...check for valid donation amount
                            if (donationAmount != expectedDonationAmount)
                                continue;
                            hammerFeePaid += donationAmount;                                           // Add donation amount back to total paid
                        }
                        int hammerCount = hammerFeePaid / hammerCost;
                        if (i < consensusParams.hammerGestationBlocks) {
                            createdHammers += hammerCount;
                            createdBCTs++;
                        } else {
                            readyHammers += hammerCount; 
                            readyBCTs++;
                        }

                        // Add these hammers to pop graph
                        if (recalcGraph) {
                            /*
                            int hammerStart = blockHeight + consensusParams.hammerGestationBlocks;
                            int hammerStop = hammerStart + consensusParams.hammerLifespanBlocks;
                            hammerStart -= tipHeight;
                            hammerStop -= tipHeight;
                            for (int j = hammerStart; j < hammerStop; j++) {
                                if (j > 0 && j < totalHammerLifespan) {
                                    if (i < consensusParams.hammerGestationBlocks) // THIS IS WRONG
                                        hammerPopGraph[j].createdPop += hammerCount;
                                    else
                                        hammerPopGraph[j].readyPop += hammerCount;
                                }
                            }*/
                            int hammerBornBlock = blockHeight;
                            int hammerReadysBlock = hammerBornBlock + consensusParams.hammerGestationBlocks;
                            int hammerDiesBlock = hammerReadysBlock + consensusParams.hammerLifespanBlocks;
                            for (int j = hammerBornBlock; j < hammerDiesBlock; j++) {
                                int graphPos = j - tipHeight;
                                if (graphPos > 0 && graphPos < totalHammerLifespan) {
                                    if (j < hammerReadysBlock)
                                        hammerPopGraph[graphPos].createdPop += hammerCount;
                                    else
                                        hammerPopGraph[graphPos].readyPop += hammerCount;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!pindexPrev->pprev)     // Check we didn't run out of blocks
            return true;

        pindexPrev = pindexPrev->pprev;
    }

    return true;
}

// Thor: Forge: Check the forge proof for given block
bool CheckForgeProof(const CBlock* pblock, const Consensus::Params& consensusParams) {
    bool verbose = LogAcceptCategory(BCLog::FORGE);

  //  if (verbose)
        LogPrintf("********************* Forge: CheckForgeProof *********************\n");

    // Get height (a CBlockIndex isn't always available when this func is called, eg in reads from disk)
    int blockHeight;
    CBlockIndex* pindexPrev;
    {
        LOCK(cs_main);
        pindexPrev = mapBlockIndex[pblock->hashPrevBlock];
        blockHeight = pindexPrev->nHeight + 1;
    }
    if (!pindexPrev) {
        LogPrintf("CheckForgeProof: Couldn't get previous block's CBlockIndex!\n");
        return false;
    }
  //  if (verbose)
        LogPrintf("CheckForgeProof: nHeight             = %i\n", blockHeight);

    // Check forge is enabled on network
    if (!IsForgeEnabled(pindexPrev, consensusParams)) {
        LogPrintf("CheckForgeProof: Can't accept a Forge block; Forge is not yet enabled on the network.\n");
        return false;
    }

    // LitecoinCash: Hive 1.1: Check that there aren't too many consecutive Hive blocks
    if (IsForge11Enabled(pindexPrev, consensusParams)) {
        int forgeBlocksAtTip = 0;
        CBlockIndex* pindexTemp = pindexPrev;
        while (pindexTemp->GetBlockHeader().IsForgeMined(consensusParams)) {
            assert(pindexTemp->pprev);
            pindexTemp = pindexTemp->pprev;
            forgeBlocksAtTip++;
        }
        if (forgeBlocksAtTip >= consensusParams.maxConsecutiveForgeBlocks) {
            LogPrintf("CheckForgeProof: Too many Forge blocks without a POW block.\n");
            return false;
        }
    } else {
        if (pindexPrev->GetBlockHeader().IsForgeMined(consensusParams)) {
            LogPrint(BCLog::FORGE, "CheckForgeProof: Forge block must follow a POW block.\n");
            return false;
        }
    }

    // Block mustn't include any BCTs
    CScript scriptPubKeyBCF = GetScriptForDestination(DecodeDestination(consensusParams.hammerCreationAddress));
    if (pblock->vtx.size() > 1)
        for (unsigned int i=1; i < pblock->vtx.size(); i++)
            if (pblock->vtx[i]->IsBCT(consensusParams, scriptPubKeyBCF)) {
                LogPrintf("CheckForgeProof: Forgemined block contains BCTs!\n");
                return false;                
            }
    
    // Coinbase tx must be valid
    CTransactionRef txCoinbase = pblock->vtx[0];
    //LogPrintf("CheckForgeProof: Got coinbase tx: %s\n", txCoinbase->ToString());
    if (!txCoinbase->IsCoinBase()) {
        LogPrintf("CheckForgeProof: Coinbase tx isn't valid!\n");
        return false;
    }

    // Must have exactly 2 or 3 outputs
    if (txCoinbase->vout.size() < 2 || txCoinbase->vout.size() > 3) {
        LogPrintf("CheckForgeProof: Didn't expect %i vouts!\n", txCoinbase->vout.size());
        return false;
    }

    // vout[0] must be long enough to contain all encodings
    if (txCoinbase->vout[0].scriptPubKey.size() < 144) {
        LogPrintf("CheckForgeProof: vout[0].scriptPubKey isn't long enough to contain forge proof encodings\n");
        return false;
    }

    // vout[1] must start OP_RETURN OP_HAMMER (bytes 0-1)
    if (txCoinbase->vout[0].scriptPubKey[0] != OP_RETURN || txCoinbase->vout[0].scriptPubKey[1] != OP_HAMMER) {
        LogPrintf("CheckForgeProof: vout[0].scriptPubKey doesn't start OP_RETURN OP_HAMMER\n");
        return false;
    }

    // Grab the hammer nonce (bytes 3-6; byte 2 has value 04 as a size marker for this field)
    uint32_t hammerNonce = ReadLE32(&txCoinbase->vout[0].scriptPubKey[3]);
   // if (verbose)
        LogPrintf("CheckForgeProof: hammerNonce            = %i\n", hammerNonce);

    // Grab the bct height (bytes 8-11; byte 7 has value 04 as a size marker for this field)
    uint32_t bctClaimedHeight = ReadLE32(&txCoinbase->vout[0].scriptPubKey[8]);
  //  if (verbose)
        LogPrintf("CheckForgeProof: bctHeight           = %i\n", bctClaimedHeight);

    // Get community contrib flag (byte 12)
    bool communityContrib = txCoinbase->vout[0].scriptPubKey[12] == OP_TRUE;
 //   if (verbose)
        LogPrintf("CheckForgeProof: communityContrib    = %s\n", communityContrib ? "true" : "false");

    // Grab the txid (bytes 14-78; byte 13 has val 64 as size marker)
    std::vector<unsigned char> txid(&txCoinbase->vout[0].scriptPubKey[14], &txCoinbase->vout[0].scriptPubKey[14 + 64]);
    std::string txidStr = std::string(txid.begin(), txid.end());
  //  if (verbose)
        LogPrintf("CheckForgeProof: bctTxId             = %s\n", txidStr);

    // Check hammer hash against target
    std::string deterministicRandString = GetDeterministicRandString(pindexPrev);
  //  if (verbose)
        LogPrintf("CheckForgeProof: detRandString       = %s\n", deterministicRandString);
    arith_uint256 hammerHashTarget;
    hammerHashTarget.SetCompact(GetNextForgeWorkRequired(pindexPrev, consensusParams));
  //  if (verbose)
        LogPrintf("CheckForgeProof: hammerHashTarget       = %s\n", hammerHashTarget.ToString());
    std::string hashHex = (CHashWriter(SER_GETHASH, 0) << deterministicRandString << txidStr << hammerNonce).GetHash().GetHex();
    arith_uint256 hammerHash = arith_uint256(hashHex);
 //   if (verbose)
        LogPrintf("CheckForgeProof: hammerHash             = %s\n", hashHex);
    if (hammerHash >= hammerHashTarget) {
        LogPrintf("CheckForgeProof: Hammer does not meet hash target!\n");
        return false;
    }

    // Grab the message sig (bytes 79-end; byte 78 is size)
    std::vector<unsigned char> messageSig(&txCoinbase->vout[0].scriptPubKey[79], &txCoinbase->vout[0].scriptPubKey[79 + 65]);
  //  if (verbose)
        LogPrintf("CheckForgeProof: messageSig          = %s\n", HexStr(&messageSig[0], &messageSig[messageSig.size()]));
    
    // Grab the gold address from the gold vout
    CTxDestination goldDestination;
    if (!ExtractDestination(txCoinbase->vout[1].scriptPubKey, goldDestination)) {
        LogPrintf("CheckForgeProof: Couldn't extract gold address\n");
        return false;
    }
    if (!IsValidDestination(goldDestination)) {
        LogPrintf("CheckForgeProof: Gold address is invalid\n");
        return false;
    }
  //  if (verbose)
        LogPrintf("CheckForgeProof: goldAddress        = %s\n", EncodeDestination(goldDestination));

    // Verify the message sig
    const CKeyID *keyID = boost::get<CKeyID>(&goldDestination);
    if (!keyID) {
        LogPrintf("CheckForgeProof: Can't get pubkey for gold address\n");
        return false;
    }
    CHashWriter ss(SER_GETHASH, 0);
    ss << deterministicRandString;
    uint256 mhash = ss.GetHash();
    CPubKey pubkey;
    if (!pubkey.RecoverCompact(mhash, messageSig)) {
        LogPrintf("CheckForgeProof: Couldn't recover pubkey from hash\n");
        return false;
    }
    if (pubkey.GetID() != *keyID) {
        LogPrintf("CheckForgeProof: Signature mismatch! GetID() = %s, *keyID = %s\n", pubkey.GetID().ToString(), (*keyID).ToString());
        return false;
    }

    // Grab the BCT utxo
    bool deepDrill = false;
    uint32_t bctFoundHeight;
    CAmount bctValue;
    CScript bctScriptPubKey;
    {
        LOCK(cs_main);

        COutPoint outHammerCreation(uint256S(txidStr), 0);
        COutPoint outCommFund(uint256S(txidStr), 1);
        Coin coin;
        CTransactionRef bct = nullptr;
        CBlockIndex foundAt;

        if (pcoinsTip && pcoinsTip->GetCoin(outHammerCreation, coin)) {        // First try the UTXO set (this pathway will hit on incoming blocks)
         //   if (verbose)
                LogPrintf("CheckForgeProof: Using UTXO set for outHammerCreation\n");
            bctValue = coin.out.nValue;
            bctScriptPubKey = coin.out.scriptPubKey;
            bctFoundHeight = coin.nHeight;
        } else {                                                            // UTXO set isn't available when eg reindexing, so drill into block db (not too bad, since Alice put her BCT height in the coinbase tx)
          //  if (verbose)
                LogPrintf("! CheckForgeProof: Warn: Using deep drill for outHammerCreation\n");
            if (!GetTxByHashAndHeight(uint256S(txidStr), bctClaimedHeight, bct, foundAt, pindexPrev, consensusParams)) {
                LogPrintf("CheckForgeProof: Couldn't locate indicated BCT\n");
                return false;
            }
            deepDrill = true;
            bctFoundHeight = foundAt.nHeight;
            bctValue = bct->vout[0].nValue;
            bctScriptPubKey = bct->vout[0].scriptPubKey;
        }

        if (communityContrib) {
            CScript scriptPubKeyCF = GetScriptForDestination(DecodeDestination(consensusParams.forgeCommunityAddress));
            CAmount donationAmount;

            if(bct == nullptr) {                                                                // If we dont have a ref to the BCT
                if (pcoinsTip && pcoinsTip->GetCoin(outCommFund, coin)) {                       // First try UTXO set
                //    if (verbose)
                        LogPrintf("CheckForgeProof: Using UTXO set for outCommFund\n");
                    if (coin.out.scriptPubKey != scriptPubKeyCF) {                              // If we find it, validate the scriptPubKey and store amount
                        LogPrintf("CheckForgeProof: Community contrib was indicated but not found\n");
                        return false;
                    }
                    donationAmount = coin.out.nValue;
                } else {                                                                        // Fallback if we couldn't use UTXO set
                 //   if (verbose)
                        LogPrintf("! CheckForgeProof: Warn: Using deep drill for outCommFund\n");
                    if (!GetTxByHashAndHeight(uint256S(txidStr), bctClaimedHeight, bct, foundAt, pindexPrev, consensusParams)) {
                        LogPrintf("CheckForgeProof: Couldn't locate indicated BCT\n");           // Still couldn't find it
                        return false;
                    }
                    deepDrill = true;
                }
            }
            if(bct != nullptr) {                                                                // We have the BCT either way now (either from first or second drill). If got from UTXO set bct == nullptr still.
                if (bct->vout.size() < 2 || bct->vout[1].scriptPubKey != scriptPubKeyCF) {      // So Validate the scriptPubKey and store amount
                    LogPrintf("CheckForgeProof: Community contrib was indicated but not found\n");
                    return false;
                }
                donationAmount = bct->vout[1].nValue;
            }

            // Check for valid donation amount
            CAmount expectedDonationAmount = (bctValue + donationAmount) / consensusParams.communityContribFactor;
            if (donationAmount != expectedDonationAmount) {
                LogPrintf("CheckForgeProof: BCT pays community fund incorrect amount %i (expected %i)\n", donationAmount, expectedDonationAmount);
                return false;
            }

            // Update amount paid
            bctValue += donationAmount;
        }
    }

    if (bctFoundHeight != bctClaimedHeight) {
        LogPrintf("CheckForgeProof: Claimed BCT height of %i conflicts with found height of %i\n", bctClaimedHeight, bctFoundHeight);
        return false;
    }

    // Check hammer maturity
    int bctDepth = blockHeight - bctFoundHeight;
    if (bctDepth < consensusParams.hammerGestationBlocks) {
        LogPrintf("CheckForgeProof: Indicated BCT is created.\n");
        return false;
    }
    if (bctDepth > consensusParams.hammerGestationBlocks + consensusParams.hammerLifespanBlocks) {
        LogPrintf("CheckForgeProof: Indicated BCT is too old.\n");
        return false;
    }

    // Check for valid hammer creation script and get gold scriptPubKey from BCT
    CScript scriptPubKeyGold;
    if (!CScript::IsBCTScript(bctScriptPubKey, scriptPubKeyBCF, &scriptPubKeyGold)) {
        LogPrintf("CheckForgeProof: Indicated utxo is not a valid BCT script\n");
        return false;
    }

    CTxDestination goldDestinationBCT;
    if (!ExtractDestination(scriptPubKeyGold, goldDestinationBCT)) {
        LogPrintf("CheckForgeProof: Couldn't extract gold address from BCT UTXO\n");
        return false;
    }

    // Check BCT's gold address actually matches the claimed gold address
    if (goldDestination != goldDestinationBCT) {
        LogPrintf("CheckForgeProof: BCT's gold address does not match claimed gold address!\n");
        return false;
    }

    // Find hammer count
    CAmount hammerCost = GetHammerCost(bctFoundHeight, consensusParams);
    if (bctValue < consensusParams.minHammerCost) {
        LogPrintf("CheckForgeProof: BCT fee is less than the minimum possible hammer cost\n");
        return false;
    }
    if (bctValue < hammerCost) {
        LogPrintf("CheckForgeProof: BCT fee is less than the cost for a single hammer\n");
        return false;
    }
    unsigned int hammerCount = bctValue / hammerCost;
  //  if (verbose) {
        LogPrintf("CheckForgeProof: bctValue            = %i\n", bctValue);
        LogPrintf("CheckForgeProof: hammerCost             = %i\n", hammerCost);
        LogPrintf("CheckForgeProof: hammerCount            = %i\n", hammerCount);
  //  }
    
    // Check enough hammers were bought to include claimed hammerNonce
    if (hammerNonce >= hammerCount) {
        LogPrintf("CheckForgeProof: BCT did not create enough hammers for claimed nonce!\n");
        return false;
    }
    
  //  if (verbose)
    	LogPrintf("CheckForgeProof: Pass at %i%s\n", blockHeight, deepDrill ? " (used deepdrill)" : "");

    return true;
}
