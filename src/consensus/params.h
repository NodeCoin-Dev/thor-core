// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_PARAMS_H
#define BITCOIN_CONSENSUS_PARAMS_H

#include <uint256.h>
#include <limits>
#include <map>
#include <string>

#include <script/script.h>  // Thor: Needed for CScript
#include <amount.h>         // Thor: Needed for CAmount

namespace Consensus {

enum DeploymentPos
{
    DEPLOYMENT_TESTDUMMY,
    DEPLOYMENT_CSV, // Deployment of BIP68, BIP112, and BIP113.
    DEPLOYMENT_SEGWIT, // Deployment of BIP141, BIP143, and BIP147.
    DEPLOYMENT_FORGE,    // Thor: Forge: Deployment
    DEPLOYMENT_FORGE_1_1,    // Thor: Forge: 1.1 Deployment
    DEPLOYMENT_FORGE_1_2,    // Thor: Forge: 1.1 Deployment
    // NOTE: Also add new deployments to VersionBitsDeploymentInfo in versionbits.cpp
    MAX_VERSION_BITS_DEPLOYMENTS
};

/**
 * Struct for each individual consensus rule change using BIP9.
 */
struct BIP9Deployment {
    /** Bit position to select the particular bit in nVersion. */
    int bit;
    /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
    int64_t nStartTime;
    /** Timeout/expiry MedianTime for the deployment attempt. */
    int64_t nTimeout;

    /** Constant for nTimeout very far in the future. */
    static constexpr int64_t NO_TIMEOUT = std::numeric_limits<int64_t>::max();

    /** Special value for nStartTime indicating that the deployment is always active.
     *  This is useful for testing, as it means tests don't need to deal with the activation
     *  process (which takes at least 3 BIP9 intervals). Only tests that specifically test the
     *  behaviour during activation cannot use this. */
    static constexpr int64_t ALWAYS_ACTIVE = -1;
};

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    uint256 hashGenesisBlock;
    int nSubsidyHalvingInterval;
    /** Block height at which BIP16 becomes active */
    int BIP16Height;
    /** Block height and hash at which BIP34 becomes active */
    int BIP34Height;
    uint256 BIP34Hash;
    /** Block height at which BIP65 becomes active */
    int BIP65Height;
    /** Block height at which BIP66 becomes active */
    int BIP66Height;
    /**
     * Minimum blocks including miner confirmation of the total of 2016 blocks in a retargeting period,
     * (nPowTargetTimespan / nPowTargetSpacing) which is also used for BIP9 deployments.
     * Examples: 1916 for 95%, 1512 for testchains.
     */
    uint32_t nRuleChangeActivationThreshold;
    uint32_t nMinerConfirmationWindow;
    BIP9Deployment vDeployments[MAX_VERSION_BITS_DEPLOYMENTS];
    /** Proof of work parameters */
    uint256 powLimit;
    bool fPowAllowMinDifficultyBlocks;
    bool fPowNoRetargeting;
    int64_t nPowTargetSpacing;
    int64_t nPowTargetTimespan;
    int64_t DifficultyAdjustmentInterval() const { return nPowTargetTimespan / nPowTargetSpacing; }
    uint256 nMinimumChainWork;
    uint256 defaultAssumeValid;

    // Thor: General consensus params
    uint32_t powForkTime;               // Time of PoW hash method change
    int lastScryptBlock;                // Height of last scrypt block
    int slowStartBlocks;                // Scale post-fork block reward over this many blocks
    int totalMoneySupplyHeight;         // Height at which TMS is reached, do not issue rewards past this point
    uint256 powLimitSHA;                // Initial hash target at fork
//    CAmount premineAmount;              // Premine amount
//    CScript premineOutputScript;        // Premine output script


    // Thor: Forge-related consensus params
    CAmount minHammerCost;                 // Minimum cost of a hammer, used when no more block rewards
    int hammerCostFactor;                  // Hammer cost is block_reward/hammerCostFactor
    std::string hammerCreationAddress;     // Unspendable address for hammer creation
    std::string forgeCommunityAddress;   // Community fund address
    int communityContribFactor;         // Optionally, donate bct_value/maxCommunityContribFactor to community fund
    int hammerGestationBlocks;             // The number of blocks for a new hammer to ready
    int hammerLifespanBlocks;              // The number of blocks a hammer lives for after maturation
    uint256 powLimitForge;               // Highest (easiest) hammer hash target
    uint256 powLimitForge2;  
    uint32_t forgeNonceMarker;           // Nonce marker for forgemined blocks
    int minForgeCheckBlock;              // Don't bother checking below this height for Forge blocks (not used for consensus/validation checks, just efficiency when looking for potential BCTs)
    int forgeTargetAdjustAggression;     // Snap speed for hammer hash target adjustment EMA
    int forgeBlockSpacingTarget;         // Target Forge block frequency (1 out of this many blocks should be Forge)
    int forgeBlockSpacingTargetTypical;  // Observed Hive block frequency (1 out of this many blocks are observed to be Hive)
    int forgeBlockSpacingTargetTypical_1_1;

   // Thor: Forge 1.1-related consensus fields
    int minK;                           // Minimum chainwork scale for Forge blocks (see Forge whitepaper section 5)
    int maxK;                           // Maximum chainwork scale for Forge blocks (see Forge whitepaper section 5)
    int minK2;                           // Minimum chainwork scale for Forge blocks (see Forge whitepaper section 5)
    int maxK2;                           // Maximum chainwork scale for Forge blocks (see Forge whitepaper section 5)
    double maxForgeDiff;                 // Forge difficulty at which max chainwork bonus is awarded
    int maxKPow;                        // Maximum chainwork scale for PoW blocks
    double powSplit1;                   // Below this Forge difficulty threshold, PoW block chainwork bonus is halved
    double powSplit2;                   // Below this Forge difficulty threshold, PoW block chainwork bonus is halved again
    int maxConsecutiveForgeBlocks;       // Maximum hive blocks that can occur consecutively before a PoW block is required
    int forgeDifficultyWindow;           // How many blocks the SMA averages over in hive difficulty adjust
    int forgeDifficultyWindow2; 
};
} // namespace Consensus

#endif // BITCOIN_CONSENSUS_PARAMS_H
