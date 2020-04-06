// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Copyright (c) 2019 Antoine Brûlé
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/merkle.h>

#include <tinyformat.h>
#include <util.h>
#include <utilstrencodings.h>
#include <base58.h> // Thor: Needed for DecodeDestination()

#include <assert.h>

#include <chainparamsseeds.h>

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "April 3rd 2020";
    const CScript genesisOutputScript = CScript() << ParseHex("0470c79ad62e55df43ba196b12e302deb220a69dd200e22bf1cc0db2912f526a3142135c216b296a1e3da108e4383f78be3643406eae38003dc757a853396e8522") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

void CChainParams::UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    consensus.vDeployments[d].nStartTime = nStartTime;
    consensus.vDeployments[d].nTimeout = nTimeout;
}

/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";
        consensus.nSubsidyHalvingInterval = 8400000;
        consensus.BIP16Height = 0; // enforce BIP16 at start !
        consensus.BIP34Height = 999000000; // never happens
        consensus.BIP65Height = 999000000; // never happens
        consensus.BIP66Height = 999000000; // never happens
        consensus.powLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 3840;
        consensus.nPowTargetSpacing = 10; // total target of 5.1 seconds per block with the forge
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1920; // 75% of 256
        consensus.nMinerConfirmationWindow = 2560; // ( nPowTargetTimespan / nPowTargetSpacing ) * 4
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1485561600; // January 28, 2017
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1517356801; // January 31st, 2018

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE; // active from the start
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        // Thor: Forge: Deployment
        consensus.vDeployments[Consensus::DEPLOYMENT_FORGE].bit = 7;
        consensus.vDeployments[Consensus::DEPLOYMENT_FORGE].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE; // active from the start
        consensus.vDeployments[Consensus::DEPLOYMENT_FORGE].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        // Thor: Forge 1.1: Deployment
        consensus.vDeployments[Consensus::DEPLOYMENT_FORGE_1_1].bit = 9;
        consensus.vDeployments[Consensus::DEPLOYMENT_FORGE_1_1].nStartTime = 1585901581;  // lala
        consensus.vDeployments[Consensus::DEPLOYMENT_FORGE_1_1].nTimeout = 1617437580;  // lala + 1 an

        // Thor: Forge 1.2: Deployment
        consensus.vDeployments[Consensus::DEPLOYMENT_FORGE_1_2].bit = 10;
        consensus.vDeployments[Consensus::DEPLOYMENT_FORGE_1_2].nStartTime = 1586476800; // April 10th 2020
        consensus.vDeployments[Consensus::DEPLOYMENT_FORGE_1_2].nTimeout = 1618012800;

        // Thor fields
        consensus.powForkTime = 1585891944;
        consensus.lastScryptBlock = 0;
        consensus.powLimitSHA = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.slowStartBlocks = 0;                   // Scale post-fork block reward up over this many blocks

        consensus.totalMoneySupplyHeight = 75600000;

        // Thor: Forge: Consensus Fields
        consensus.minHammerCost = 10000;                       // Minimum cost of a hammer, used when no more block rewards
        consensus.hammerCostFactor = 2500;                     // Hammer cost is block_reward/hammerCostFactor
        consensus.hammerCreationAddress = "LReateLitecoinCashWorkerBeeXcMGLjb";        // Unspendable address for hammer creation
        consensus.forgeCommunityAddress = "LQwqxWJ7EwMwrZiiDNv1JbgFaCch79V25n";      // Community fund address
        consensus.communityContribFactor = 10;              // Optionally, donate bct_value/maxCommunityContribFactor to community fund
        consensus.hammerGestationBlocks = 48*24;               // The number of blocks for a new hammer to ready
        consensus.hammerLifespanBlocks = 48*24*14;             // The number of blocks a hammer lives for after maturation
        consensus.powLimitForge = uint256S("0fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");  // Highest (easiest) hammer hash target
        consensus.powLimitForge2 = uint256S("7ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.minForgeCheckBlock = 125;
        consensus.forgeTargetAdjustAggression = 30;          // Snap speed for hammer hash target adjustment EMA
        consensus.forgeBlockSpacingTarget = 2;               // Target Forge block frequency (1 out of this many blocks should be Forgemined)
        consensus.forgeBlockSpacingTargetTypical = 3;
        consensus.forgeBlockSpacingTargetTypical_1_1 = 2;
        consensus.forgeNonceMarker = 192;                    // Nonce marker for forgemined blocks

        // Thor: Forge 1.1-related consensus fields
        consensus.minK = 2;                                 // Minimum chainwork scale for Forge blocks (see Forge whitepaper section 5)
        consensus.maxK = 16;                                 // Maximum chainwork scale for Forge blocks (see Forge whitepaper section 5)
        consensus.maxForgeDiff = 0.006;                      // Forge difficulty at which max chainwork bonus is awarded
        consensus.maxKPow = 5;                              // Maximum chainwork scale for PoW blocks
        consensus.powSplit1 = 0.005;                        // Below this Forge difficulty threshold, PoW block chainwork bonus is halved
        consensus.powSplit2 = 0.0025;                       // Below this Forge difficulty threshold, PoW block chainwork bonus is halved again
        consensus.maxConsecutiveForgeBlocks = 2;             // Maximum hive blocks that can occur consecutively before a PoW block is required
        consensus.forgeDifficultyWindow = 36;                // How many blocks the SMA averages over in hive difficulty adjust


	// Thor: Forge 1.2-related consensus fields
        consensus.minK2 = 1;
        consensus.maxK2 = 7; 
        consensus.forgeDifficultyWindow2 = 24;





        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x0000000000000000000000000000000000000000000000000000000000000000");  // Thor new blockchain

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0xb0c144e2906661f58c9862721fb07f7595b05b42368fef10e4c21468ce4d69d2"); // Thor: Genesis block

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xa4;
        pchMessageStart[1] = 0x3d;
        pchMessageStart[2] = 0xdc;
        pchMessageStart[3] = 0x28;
        nDefaultPort = 7777;
        nPruneAfterHeight = 1000000;

        genesis = CreateGenesisBlock(1585891944, 50581, 0x1e0ffff0, 1, 5 * COIN * COIN_SCALE);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0xb0c144e2906661f58c9862721fb07f7595b05b42368fef10e4c21468ce4d69d2"));
        assert(genesis.hashMerkleRoot == uint256S("0x1c223325e3add97854e33a24deaec44f375223d1cc7ea5851672337cec6aa2d8"));

        // Note that of those with the service bits flag, most only support a subset of possible options
        //vSeeds.emplace_back("seeds.litecoinca.sh");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,48);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,5);
        base58Prefixes[SCRIPT_ADDRESS2] = std::vector<unsigned char>(1,50);
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1,176);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        bech32_hrp = "thor";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));
        //vFixedSeeds.clear(); // no seeds yet ----> now yes
        //vSeeds.clear();

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;

        checkpointData = {
            {
                {  0, uint256S("0xb0c144e2906661f58c9862721fb07f7595b05b42368fef10e4c21468ce4d69d2")},
            }
        };

        chainTxData = ChainTxData{
            // Data at genesis block.
            1585891944, // * UNIX timestamp of last known number of transactions
            0,   // * total number of transactions between genesis and that timestamp
                        //   (the tx=... number in the SetBestChain debug.log lines)
            0.0        // * estimated number of transactions per second after that timestamp
        };
    }
};

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        consensus.nSubsidyHalvingInterval = 8400000;
        consensus.BIP16Height = 0; // always enforce P2SH BIP16 on regtest
        consensus.BIP34Height = 999000000;
        consensus.BIP65Height = 999000000;
        consensus.BIP66Height = 999000000;
        consensus.powLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 3840; //
        consensus.nPowTargetSpacing = 10;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1920; // Require 75% of last 40 blocks to activate rulechanges
        consensus.nMinerConfirmationWindow = 2560;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1535587200; // August 30, 2018
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1535587200 + 31536000; // Start + 1 year

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1535587200; // August 30, 2018
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1535587200 + 31536000; // Start + 1 year

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE; //1535587200; // August 30, 2018
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT; //1535587200 + 31536000; // Start + 1 year

        // Thor: Forge: Deployment
        consensus.vDeployments[Consensus::DEPLOYMENT_FORGE].bit = 7;
        consensus.vDeployments[Consensus::DEPLOYMENT_FORGE].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE; // active from the start
        consensus.vDeployments[Consensus::DEPLOYMENT_FORGE].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        // Thor: Forge 1.1: Deployment
        consensus.vDeployments[Consensus::DEPLOYMENT_FORGE_1_1].bit = 9;
        consensus.vDeployments[Consensus::DEPLOYMENT_FORGE_1_1].nStartTime = 1583211600;  // March 3, 2020
        consensus.vDeployments[Consensus::DEPLOYMENT_FORGE_1_1].nTimeout = 1614747600;  // March 3, 2021

        // Thor fields
        consensus.powForkTime = 1585891944;                 // Time of PoW hash method change (block 100)
        consensus.lastScryptBlock = 0;                    // Height of last scrypt block
        consensus.powLimitSHA = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");   // Initial hash target at fork
        consensus.slowStartBlocks = 0;                     // Scale post-fork block reward up over this many blocks
        consensus.totalMoneySupplyHeight = 75600000;

        // Thor: Forge: Consensus Fields
        consensus.minHammerCost = 10000;                       // Minimum cost of a hammer, used when no more block rewards
        consensus.hammerCostFactor = 2500;                     // Hammer cost is block_reward/hammerCostFactor
        consensus.hammerCreationAddress = "tEstNetCreateLCCWorkerHammerXXXYq6T3r";        // Unspendable address for hammer creation
        consensus.forgeCommunityAddress = "t9ctP2rDfvnqUr9kmo2nb1LEDpu1Lc5sQn";      // Community fund address
        consensus.communityContribFactor = 10;              // Optionally, donate bct_value/maxCommunityContribFactor to community fund
        consensus.hammerGestationBlocks = 24;               // The number of blocks for a new hammer to ready 24 times faster for testnet
        consensus.hammerLifespanBlocks = 24*14;             // The number of blocks a hammer lives for after maturation 24 times faster for testnet
        consensus.powLimitForge = uint256S("0fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");  // Highest (easiest) hammer hash target
        consensus.minForgeCheckBlock = 1;                  // Don't bother checking below this height for Forge blocks (not used for consensus/validation checks, just efficiency when looking for potential BCTs)
        consensus.forgeTargetAdjustAggression = 30;          // Snap speed for hammer hash target adjustment EMA
        consensus.forgeBlockSpacingTarget = 2;               // Target Forge block frequency (1 out of this many blocks should be Forgemined)
        consensus.forgeBlockSpacingTargetTypical = 3;
        consensus.forgeBlockSpacingTargetTypical_1_1 = 2;
        consensus.forgeNonceMarker = 192;                    // Nonce marker for forgemined blocks

        // Thor: Forge 1.1-related consensus fields
        consensus.minK = 1;                                 // Minimum chainwork scale for Forge blocks (see Forge whitepaper section 5)
        consensus.maxK = 7;                                 // Maximum chainwork scale for Forge blocks (see Forge whitepaper section 5)
        consensus.maxForgeDiff = 0.002;                      // Forge difficulty at which max chainwork bonus is awarded
        consensus.maxKPow = 5;                              // Maximum chainwork scale for PoW blocks
        consensus.powSplit1 = 0.001;                        // Below this Forge difficulty threshold, PoW block chainwork bonus is halved
        consensus.powSplit2 = 0.0005;                       // Below this Forge difficulty threshold, PoW block chainwork bonus is halved again
        consensus.maxConsecutiveForgeBlocks = 2;             // Maximum hive blocks that can occur consecutively before a PoW block is required
        consensus.forgeDifficultyWindow = 24;                // How many blocks the SMA averages over in hive difficulty adjust

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x0000000000000000000000000000000000000000000000000000000000000000");  // Thor

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0xb0c144e2906661f58c9862721fb07f7595b05b42368fef10e4c21468ce4d69d2"); // Thor: 0

	pchMessageStart[0] = 0xe9;
        pchMessageStart[1] = 0x26;
        pchMessageStart[2] = 0x1c;
        pchMessageStart[3] = 0x6e;
        nDefaultPort = 57777;
        nPruneAfterHeight = 10000;

        genesis = CreateGenesisBlock(1585891944, 50581, 0x1e0ffff0, 1, 5 * COIN * COIN_SCALE);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0xb0c144e2906661f58c9862721fb07f7595b05b42368fef10e4c21468ce4d69d2"));
        assert(genesis.hashMerkleRoot == uint256S("0x1c223325e3add97854e33a24deaec44f375223d1cc7ea5851672337cec6aa2d8"));

        vFixedSeeds.clear();
        //vSeeds.emplace_back("testseeds.litecoinca.sh");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,127);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SCRIPT_ADDRESS2] = std::vector<unsigned char>(1,58);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "tthor";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));
	//vFixedSeeds.clear(); // No seeds yet... ---->  and now yes !!!!
        //vSeeds.clear();	

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;

        checkpointData = (CCheckpointData) {
            {
                {0, uint256S("0xb0c144e2906661f58c9862721fb07f7595b05b42368fef10e4c21468ce4d69d2")},
            }
        };

        chainTxData = ChainTxData{
            1585891944,
            0,
            0.0
        };
    }
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        consensus.nSubsidyHalvingInterval = 150;
        consensus.BIP16Height = 0; // always enforce P2SH BIP16 on regtest
        consensus.BIP34Height = 100000000; // BIP34 has not activated on regtest (far in the future so block v1 are not rejected in tests)
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1351; // BIP65 activated on regtest (Used in rpc activation tests)
        consensus.BIP66Height = 1251; // BIP66 activated on regtest (Used in rpc activation tests)
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 3.5 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 2.5 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        // Thor fields
        consensus.powForkTime = 1585891944;                 // Time of PoW hash method change (block 100)
        consensus.lastScryptBlock = 0;                    // Height of last scrypt block
        consensus.powLimitSHA = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");   // Initial hash target at fork
        consensus.slowStartBlocks = 0;                     // Scale post-fork block reward up over this many blocks

        consensus.totalMoneySupplyHeight = 7560000;

        consensus.forgeNonceMarker = 192;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

	pchMessageStart[0] = 0xc9;
        pchMessageStart[1] = 0xe6;
        pchMessageStart[2] = 0xbc;
        pchMessageStart[3] = 0xda;
        nDefaultPort = 57666;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1585891944, 1, 0x207fffff, 1, 5 * COIN * COIN_SCALE);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0xeda05b309d31e56ace5fce691cda67165124f8adf89a0f15c6de5e6ac72be2bf"));
        assert(genesis.hashMerkleRoot == uint256S("0x1c223325e3add97854e33a24deaec44f375223d1cc7ea5851672337cec6aa2d8"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;

        checkpointData = {
            {
                {0, uint256S("eda05b309d31e56ace5fce691cda67165124f8adf89a0f15c6de5e6ac72be2bf")}
            }
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SCRIPT_ADDRESS2] = std::vector<unsigned char>(1,58);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "rthor";
    }
};

static std::unique_ptr<CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<CChainParams> CreateChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CChainParams>(new CMainParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CChainParams>(new CRegTestParams());
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(network);
}

void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    globalChainParams->UpdateVersionBitsParameters(d, nStartTime, nTimeout);
}
