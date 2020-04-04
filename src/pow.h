// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POW_H
#define BITCOIN_POW_H

#include <consensus/params.h>

#include <stdint.h>

class CBlockHeader;
class CBlockIndex;
class uint256;
class CBlock;

struct HammerPopGraphPoint {
    int createdPop;
    int readyPop;
};

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params&);
unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params&);
unsigned int GetNextForgeWorkRequired(const CBlockIndex* pindexLast, const Consensus::Params& params);                       // Thor: Forge: Get the current Hammer Hash Target
bool CheckForgeProof(const CBlock* pblock, const Consensus::Params& params);                                                 // Thor: Forge: Check the forge proof for given block
bool GetNetworkForgeInfo(int& createdHammers, int& createdBCTs, int& readyHammers, int& readyBCTs, CAmount& potentialLifespanRewards, const Consensus::Params& consensusParams, bool recalcGraph = false); // Thor: Forge: Get count of all live and gestating BCTs on the network

/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits */
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params&);



#endif // BITCOIN_POW_H
