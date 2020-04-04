// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <miner.h>

#include <amount.h>
#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/consensus.h>
#include <consensus/tx_verify.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <hash.h>
#include <crypto/scrypt.h>
#include <validation.h>
#include <net.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <pow.h>
#include <primitives/transaction.h>
#include <script/standard.h>
#include <timedata.h>
#include <util.h>
#include <utilmoneystr.h>
#include <validationinterface.h>

#include <algorithm>
#include <queue>
#include <utility>

#include <wallet/wallet.h>  // Thor: Forge
#include <rpc/server.h>     // Thor: Forge
#include <base58.h>         // Thor: Forge
#include <sync.h>           // Thor: Forge
#include <boost/thread.hpp> // LitecoinCash: Forge: Mining optimisations

static CCriticalSection cs_solution_vars;
std::atomic<bool> solutionFound;            // LitecoinCash: Forge: Mining optimisations: Thread-safe atomic flag to signal solution found (saves a slow mutex)
std::atomic<bool> earlyAbort;               // LitecoinCash: Forge: Mining optimisations: Thread-safe atomic flag to signal early abort needed
CHammerRange solvingRange;                     // LitecoinCash: Forge: Mining optimisations: The solving range (protected by mutex)
uint32_t solvingHammer;                        // LitecoinCash: Forge: Mining optimisations: The solving hammer (protected by mutex)

//////////////////////////////////////////////////////////////////////////////
//
// BitcoinMiner
//

//
// Unconfirmed transactions in the memory pool often depend on other
// transactions in the memory pool. When we select transactions from the
// pool, we select by highest fee rate of a transaction combined with all
// its ancestors.

uint64_t nLastBlockTx = 0;
uint64_t nLastBlockWeight = 0;

int64_t UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime = std::max(pindexPrev->GetMedianTimePast()+1, GetAdjustedTime());

    if (nOldTime < nNewTime)
        pblock->nTime = nNewTime;

    // Updating time can change work required on testnet:
    // Thor: Forge: Don't do this
    /*
    if (consensusParams.fPowAllowMinDifficultyBlocks)
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, consensusParams);
    */

    return nNewTime - nOldTime;
}

BlockAssembler::Options::Options() {
    blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);
    nBlockMaxWeight = DEFAULT_BLOCK_MAX_WEIGHT;
}

BlockAssembler::BlockAssembler(const CChainParams& params, const Options& options) : chainparams(params)
{
    blockMinFeeRate = options.blockMinFeeRate;
    // Limit weight to between 4K and MAX_BLOCK_WEIGHT-4K for sanity:
    nBlockMaxWeight = std::max<size_t>(4000, std::min<size_t>(MAX_BLOCK_WEIGHT - 4000, options.nBlockMaxWeight));
}

static BlockAssembler::Options DefaultOptions(const CChainParams& params)
{
    // Block resource limits
    // If neither -blockmaxsize or -blockmaxweight is given, limit to DEFAULT_BLOCK_MAX_*
    // If only one is given, only restrict the specified resource.
    // If both are given, restrict both.
    BlockAssembler::Options options;
    options.nBlockMaxWeight = gArgs.GetArg("-blockmaxweight", DEFAULT_BLOCK_MAX_WEIGHT);
    if (gArgs.IsArgSet("-blockmintxfee")) {
        CAmount n = 0;
        ParseMoney(gArgs.GetArg("-blockmintxfee", ""), n);
        options.blockMinFeeRate = CFeeRate(n);
    } else {
        options.blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);
    }
    return options;
}

BlockAssembler::BlockAssembler(const CChainParams& params) : BlockAssembler(params, DefaultOptions(params)) {}

void BlockAssembler::resetBlock()
{
    inBlock.clear();

    // Reserve space for coinbase tx
    nBlockWeight = 4000;
    nBlockSigOpsCost = 400;
    fIncludeWitness = false;
    fIncludeBCTs = true;    // Thor: Forge

    // These counters do not include coinbase tx
    nBlockTx = 0;
    nFees = 0;
}

// Thor: Forge: If forgeProofScript is passed, create a Forge block instead of a PoW block
std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn, bool fMineWitnessTx, const CScript* forgeProofScript)
{
    int64_t nTimeStart = GetTimeMicros();

    resetBlock();

    pblocktemplate.reset(new CBlockTemplate());

    if(!pblocktemplate.get())
        return nullptr;
    pblock = &pblocktemplate->block; // pointer for convenience

    // Add dummy coinbase tx as first transaction
    pblock->vtx.emplace_back();
    pblocktemplate->vTxFees.push_back(-1); // updated at end
    pblocktemplate->vTxSigOpsCost.push_back(-1); // updated at end

    LOCK2(cs_main, mempool.cs);
    CBlockIndex* pindexPrev = chainActive.Tip();
    assert(pindexPrev != nullptr);

    // Thor: Forge: Make sure Forge is enabled if a Forge block is requested
    if (forgeProofScript && !IsForgeEnabled(pindexPrev, chainparams.GetConsensus()))
        throw std::runtime_error(
            "Error: The Forge is not yet enabled on the network"
        );

    nHeight = pindexPrev->nHeight + 1;

    pblock->nVersion = ComputeBlockVersion(pindexPrev, chainparams.GetConsensus());
    // -regtest only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (chainparams.MineBlocksOnDemand())
        pblock->nVersion = gArgs.GetArg("-blockversion", pblock->nVersion);

    pblock->nTime = GetAdjustedTime();
    const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();

    nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST)
                       ? nMedianTimePast
                       : pblock->GetBlockTime();

    // Decide whether to include witness transactions
    // This is only needed in case the witness softfork activation is reverted
    // (which would require a very deep reorganization) or when
    // -promiscuousmempoolflags is used.
    // TODO: replace this with a call to main to assess validity of a mempool
    // transaction (which in most cases can be a no-op).
    fIncludeWitness = IsWitnessEnabled(pindexPrev, chainparams.GetConsensus()) && fMineWitnessTx;

    int nPackagesSelected = 0;
    int nDescendantsUpdated = 0;
    // Thor: Don't include BCTs in forgemined blocks
    if (forgeProofScript)
        fIncludeBCTs = false;

    addPackageTxs(nPackagesSelected, nDescendantsUpdated);

    int64_t nTime1 = GetTimeMicros();

    nLastBlockTx = nBlockTx;
    nLastBlockWeight = nBlockWeight;

    // Thor: Forge: Create appropriate coinbase tx for pow or Forge block
    if (forgeProofScript) {
        CMutableTransaction coinbaseTx;

        // 1 vin with empty prevout
        coinbaseTx.vin.resize(1);
        coinbaseTx.vin[0].prevout.SetNull();
        coinbaseTx.vin[0].scriptSig = CScript() << nHeight << OP_0;

        // vout[0]: Forge proof
        coinbaseTx.vout.resize(2);
        coinbaseTx.vout[0].scriptPubKey = *forgeProofScript;
        coinbaseTx.vout[0].nValue = 0;

        // vout[1]: Gold :)
        coinbaseTx.vout[1].scriptPubKey = scriptPubKeyIn;
        coinbaseTx.vout[1].nValue = nFees + GetBlockSubsidy(nHeight, chainparams.GetConsensus());

        // vout[2]: Coinbase commitment
        pblock->vtx[0] = MakeTransactionRef(std::move(coinbaseTx));
        pblocktemplate->vchCoinbaseCommitment = GenerateCoinbaseCommitment(*pblock, pindexPrev, chainparams.GetConsensus());
        pblocktemplate->vTxFees[0] = -nFees;
    } else {
        CMutableTransaction coinbaseTx;
        coinbaseTx.vin.resize(1);
        coinbaseTx.vin[0].prevout.SetNull();
        coinbaseTx.vout.resize(1);
        coinbaseTx.vout[0].scriptPubKey = scriptPubKeyIn;
        coinbaseTx.vout[0].nValue = nFees + GetBlockSubsidy(nHeight, chainparams.GetConsensus());
        coinbaseTx.vin[0].scriptSig = CScript() << nHeight << OP_0;
        pblock->vtx[0] = MakeTransactionRef(std::move(coinbaseTx));
        pblocktemplate->vchCoinbaseCommitment = GenerateCoinbaseCommitment(*pblock, pindexPrev, chainparams.GetConsensus());
        pblocktemplate->vTxFees[0] = -nFees;
    }

    LogPrintf("CreateNewBlock(): block weight: %u txs: %u fees: %ld sigops %d\n", GetBlockWeight(*pblock), nBlockTx, nFees, nBlockSigOpsCost);

    // Fill in header
    pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
    UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);

    // Thor: Forge: Choose correct nBits depending on whether a Forge block is requested
    if (forgeProofScript)
        pblock->nBits = GetNextForgeWorkRequired(pindexPrev, chainparams.GetConsensus());
    else
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, chainparams.GetConsensus());

    // Thor: Forge: Set nonce marker for forgemined blocks
    pblock->nNonce = forgeProofScript ? chainparams.GetConsensus().forgeNonceMarker : 0;
    pblocktemplate->vTxSigOpsCost[0] = WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*pblock->vtx[0]);

    CValidationState state;
    if (!TestBlockValidity(state, chainparams, *pblock, pindexPrev, false, false)) {
        throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, FormatStateMessage(state)));
    }

    int64_t nTime2 = GetTimeMicros();

    LogPrint(BCLog::BENCH, "CreateNewBlock() packages: %.2fms (%d packages, %d updated descendants), validity: %.2fms (total %.2fms)\n", 0.001 * (nTime1 - nTimeStart), nPackagesSelected, nDescendantsUpdated, 0.001 * (nTime2 - nTime1), 0.001 * (nTime2 - nTimeStart));

    return std::move(pblocktemplate);
}

void BlockAssembler::onlyUnconfirmed(CTxMemPool::setEntries& testSet)
{
    for (CTxMemPool::setEntries::iterator iit = testSet.begin(); iit != testSet.end(); ) {
        // Only test txs not already in the block
        if (inBlock.count(*iit)) {
            testSet.erase(iit++);
        }
        else {
            iit++;
        }
    }
}

bool BlockAssembler::TestPackage(uint64_t packageSize, int64_t packageSigOpsCost) const
{
    // TODO: switch to weight-based accounting for packages instead of vsize-based accounting.
    if (nBlockWeight + WITNESS_SCALE_FACTOR * packageSize >= nBlockMaxWeight)
        return false;
    if (nBlockSigOpsCost + packageSigOpsCost >= MAX_BLOCK_SIGOPS_COST)
        return false;
    return true;
}

// Perform transaction-level checks before adding to block:
// - transaction finality (locktime)
// - preready witness (in case segwit transactions are added to mempool before
//   segwit activation)
bool BlockAssembler::TestPackageTransactions(const CTxMemPool::setEntries& package)
{
    const Consensus::Params& consensusParams = Params().GetConsensus(); // Thor: Forge

    for (const CTxMemPool::txiter it : package) {
        if (!IsFinalTx(it->GetTx(), nHeight, nLockTimeCutoff))
            return false;
        if (!fIncludeWitness && it->GetTx().HasWitness())
            return false;
        // Thor: Inhibit BCTs if required
        if (!fIncludeBCTs && it->GetTx().IsBCT(consensusParams, GetScriptForDestination(DecodeDestination(consensusParams.hammerCreationAddress))))
            return false;
    }
    return true;
}

void BlockAssembler::AddToBlock(CTxMemPool::txiter iter)
{
    pblock->vtx.emplace_back(iter->GetSharedTx());
    pblocktemplate->vTxFees.push_back(iter->GetFee());
    pblocktemplate->vTxSigOpsCost.push_back(iter->GetSigOpCost());
    nBlockWeight += iter->GetTxWeight();
    ++nBlockTx;
    nBlockSigOpsCost += iter->GetSigOpCost();
    nFees += iter->GetFee();
    inBlock.insert(iter);

    bool fPrintPriority = gArgs.GetBoolArg("-printpriority", DEFAULT_PRINTPRIORITY);
    if (fPrintPriority) {
        LogPrintf("fee %s txid %s\n",
                  CFeeRate(iter->GetModifiedFee(), iter->GetTxSize()).ToString(),
                  iter->GetTx().GetHash().ToString());
    }
}

int BlockAssembler::UpdatePackagesForAdded(const CTxMemPool::setEntries& alreadyAdded,
        indexed_modified_transaction_set &mapModifiedTx)
{
    int nDescendantsUpdated = 0;
    for (const CTxMemPool::txiter it : alreadyAdded) {
        CTxMemPool::setEntries descendants;
        mempool.CalculateDescendants(it, descendants);
        // Insert all descendants (not yet in block) into the modified set
        for (CTxMemPool::txiter desc : descendants) {
            if (alreadyAdded.count(desc))
                continue;
            ++nDescendantsUpdated;
            modtxiter mit = mapModifiedTx.find(desc);
            if (mit == mapModifiedTx.end()) {
                CTxMemPoolModifiedEntry modEntry(desc);
                modEntry.nSizeWithAncestors -= it->GetTxSize();
                modEntry.nModFeesWithAncestors -= it->GetModifiedFee();
                modEntry.nSigOpCostWithAncestors -= it->GetSigOpCost();
                mapModifiedTx.insert(modEntry);
            } else {
                mapModifiedTx.modify(mit, update_for_parent_inclusion(it));
            }
        }
    }
    return nDescendantsUpdated;
}

// Skip entries in mapTx that are already in a block or are present
// in mapModifiedTx (which implies that the mapTx ancestor state is
// stale due to ancestor inclusion in the block)
// Also skip transactions that we've already failed to add. This can happen if
// we consider a transaction in mapModifiedTx and it fails: we can then
// potentially consider it again while walking mapTx.  It's currently
// guaranteed to fail again, but as a belt-and-suspenders check we put it in
// failedTx and avoid re-evaluation, since the re-evaluation would be using
// cached size/sigops/fee values that are not actually correct.
bool BlockAssembler::SkipMapTxEntry(CTxMemPool::txiter it, indexed_modified_transaction_set &mapModifiedTx, CTxMemPool::setEntries &failedTx)
{
    assert (it != mempool.mapTx.end());
    return mapModifiedTx.count(it) || inBlock.count(it) || failedTx.count(it);
}

void BlockAssembler::SortForBlock(const CTxMemPool::setEntries& package, CTxMemPool::txiter entry, std::vector<CTxMemPool::txiter>& sortedEntries)
{
    // Sort package by ancestor count
    // If a transaction A depends on transaction B, then A's ancestor count
    // must be greater than B's.  So this is sufficient to validly order the
    // transactions for block inclusion.
    sortedEntries.clear();
    sortedEntries.insert(sortedEntries.begin(), package.begin(), package.end());
    std::sort(sortedEntries.begin(), sortedEntries.end(), CompareTxIterByAncestorCount());
}

// This transaction selection algorithm orders the mempool based
// on feerate of a transaction including all unconfirmed ancestors.
// Since we don't remove transactions from the mempool as we select them
// for block inclusion, we need an alternate method of updating the feerate
// of a transaction with its not-yet-selected ancestors as we go.
// This is accomplished by walking the in-mempool descendants of selected
// transactions and storing a temporary modified state in mapModifiedTxs.
// Each time through the loop, we compare the best transaction in
// mapModifiedTxs with the next transaction in the mempool to decide what
// transaction package to work on next.
void BlockAssembler::addPackageTxs(int &nPackagesSelected, int &nDescendantsUpdated)
{
    // mapModifiedTx will store sorted packages after they are modified
    // because some of their txs are already in the block
    indexed_modified_transaction_set mapModifiedTx;
    // Keep track of entries that failed inclusion, to avoid duplicate work
    CTxMemPool::setEntries failedTx;

    // Start by adding all descendants of previously added txs to mapModifiedTx
    // and modifying them for their already included ancestors
    UpdatePackagesForAdded(inBlock, mapModifiedTx);

    CTxMemPool::indexed_transaction_set::index<ancestor_score>::type::iterator mi = mempool.mapTx.get<ancestor_score>().begin();
    CTxMemPool::txiter iter;

    // Limit the number of attempts to add transactions to the block when it is
    // close to full; this is just a simple heuristic to finish quickly if the
    // mempool has a lot of entries.
    const int64_t MAX_CONSECUTIVE_FAILURES = 1000;
    int64_t nConsecutiveFailed = 0;

    while (mi != mempool.mapTx.get<ancestor_score>().end() || !mapModifiedTx.empty())
    {
        // First try to find a new transaction in mapTx to evaluate.
        if (mi != mempool.mapTx.get<ancestor_score>().end() &&
                SkipMapTxEntry(mempool.mapTx.project<0>(mi), mapModifiedTx, failedTx)) {
            ++mi;
            continue;
        }

        // Now that mi is not stale, determine which transaction to evaluate:
        // the next entry from mapTx, or the best from mapModifiedTx?
        bool fUsingModified = false;

        modtxscoreiter modit = mapModifiedTx.get<ancestor_score>().begin();
        if (mi == mempool.mapTx.get<ancestor_score>().end()) {
            // We're out of entries in mapTx; use the entry from mapModifiedTx
            iter = modit->iter;
            fUsingModified = true;
        } else {
            // Try to compare the mapTx entry to the mapModifiedTx entry
            iter = mempool.mapTx.project<0>(mi);
            if (modit != mapModifiedTx.get<ancestor_score>().end() &&
                    CompareTxMemPoolEntryByAncestorFee()(*modit, CTxMemPoolModifiedEntry(iter))) {
                // The best entry in mapModifiedTx has higher score
                // than the one from mapTx.
                // Switch which transaction (package) to consider
                iter = modit->iter;
                fUsingModified = true;
            } else {
                // Either no entry in mapModifiedTx, or it's worse than mapTx.
                // Increment mi for the next loop iteration.
                ++mi;
            }
        }

        // We skip mapTx entries that are inBlock, and mapModifiedTx shouldn't
        // contain anything that is inBlock.
        assert(!inBlock.count(iter));

        uint64_t packageSize = iter->GetSizeWithAncestors();
        CAmount packageFees = iter->GetModFeesWithAncestors();
        int64_t packageSigOpsCost = iter->GetSigOpCostWithAncestors();
        if (fUsingModified) {
            packageSize = modit->nSizeWithAncestors;
            packageFees = modit->nModFeesWithAncestors;
            packageSigOpsCost = modit->nSigOpCostWithAncestors;
        }

        if (packageFees < blockMinFeeRate.GetFee(packageSize)) {
            // Everything else we might consider has a lower fee rate
            return;
        }

        if (!TestPackage(packageSize, packageSigOpsCost)) {
            if (fUsingModified) {
                // Since we always look at the best entry in mapModifiedTx,
                // we must erase failed entries so that we can consider the
                // next best entry on the next loop iteration
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }

            ++nConsecutiveFailed;

            if (nConsecutiveFailed > MAX_CONSECUTIVE_FAILURES && nBlockWeight >
                    nBlockMaxWeight - 4000) {
                // Give up if we're close to full and haven't succeeded in a while
                break;
            }
            continue;
        }

        CTxMemPool::setEntries ancestors;
        uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
        std::string dummy;
        mempool.CalculateMemPoolAncestors(*iter, ancestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy, false);

        onlyUnconfirmed(ancestors);
        ancestors.insert(iter);

        // Test if all tx's are Final
        if (!TestPackageTransactions(ancestors)) {
            if (fUsingModified) {
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }
            continue;
        }

        // This transaction will make it in; reset the failed counter.
        nConsecutiveFailed = 0;

        // Package can be added. Sort the entries in a valid order.
        std::vector<CTxMemPool::txiter> sortedEntries;
        SortForBlock(ancestors, iter, sortedEntries);

        for (size_t i=0; i<sortedEntries.size(); ++i) {
            AddToBlock(sortedEntries[i]);
            // Erase from the modified set, if present
            mapModifiedTx.erase(sortedEntries[i]);
        }

        ++nPackagesSelected;

        // Update transactions that depend on each of these
        nDescendantsUpdated += UpdatePackagesForAdded(ancestors, mapModifiedTx);
    }
}

void IncrementExtraNonce(CBlock* pblock, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock)
    {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight+1; // Height first in coinbase required for block.version=2
    CMutableTransaction txCoinbase(*pblock->vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
}

// Thor: Forge: Hammer management thread
void HammerKeeper(const CChainParams& chainparams) {
    const Consensus::Params& consensusParams = chainparams.GetConsensus();

    LogPrintf("HammerKeeper: Thread started\n");
    RenameThread("forge-hammerkeeper");

    int height;
    {
        LOCK(cs_main);
        height = chainActive.Tip()->nHeight;
    }

    try {
        while (true) {
            // LitecoinCash: Forge: Mining optimisations: Parameterised sleep time
            int sleepTime = std::max((int64_t) 1, gArgs.GetArg("-forgecheckdelay", DEFAULT_FORGE_CHECK_DELAY));
            MilliSleep(sleepTime);
            int newHeight;
            {
                LOCK(cs_main);
                newHeight = chainActive.Tip()->nHeight;
            }
            if (newHeight != height) {
                // Height changed; release the hammers!
                height = newHeight;
                try {
                    BusyHammers(consensusParams, height);
                } catch (const std::runtime_error &e) {
                    LogPrintf("! HammerKeeper: Error: %s\n", e.what());
                }
            }
        }
    } catch (const boost::thread_interrupted&) {
        LogPrintf("!!! HammerKeeper: FATAL: Thread interrupted\n");
        throw;
    }
}

// LitecoinCash: Forge: Mining optimisations: Thread to signal abort on new block
void AbortWatchThread(int height) {
    // Loop until any exit condition
    while (true) {
        // Yield to OS
        MilliSleep(1);

        // Check pre-existing abort conditions
        if (solutionFound.load() || earlyAbort.load())
            return;

        // Get tip height, keeping lock scope as short as possible
        int newHeight;
        {
            LOCK(cs_main);
            newHeight = chainActive.Tip()->nHeight;
        }

        // Check for abort from tip height change
        if (newHeight != height) {
            //LogPrintf("*** ABORT FIRE\n");
            earlyAbort.store(true);
            return;
        }
    }
}

// LitecoinCash: Forge: Mining optimisations: Thread to check a single bin
void CheckBin(int threadID, std::vector<CHammerRange> bin, std::string deterministicRandString, arith_uint256 hammerHashTarget) {
    // Iterate over ranges in this bin
    int checkCount = 0;
    for (std::vector<CHammerRange>::const_iterator it = bin.begin(); it != bin.end(); it++) {
        CHammerRange hammerRange = *it;
        //LogPrintf("THREAD #%i: Checking %i-%i in %s\n", threadID, hammerRange.offset, hammerRange.offset + hammerRange.count - 1, hammerRange.txid);
        // Iterate over hammers in this range
        for (int i = hammerRange.offset; i < hammerRange.offset + hammerRange.count; i++) {
            // Check abort conditions (Only every N hammers. The atomic load is expensive, but much cheaper than a mutex - esp on Windows, see https://www.arangodb.com/2015/02/comparing-atomic-mutex-rwlocks/)
            if(checkCount++ % 1000 == 0) {
                if (solutionFound.load() || earlyAbort.load()) {
                    //LogPrintf("THREAD #%i: Solution found elsewhere or early abort requested, ending early\n", threadID);
                    return;
                }
            }
            // Hash the hammer
            std::string hashHex = (CHashWriter(SER_GETHASH, 0) << deterministicRandString << hammerRange.txid << i).GetHash().GetHex();
            arith_uint256 hammerHash = arith_uint256(hashHex);
            // Compare to target and write out result if successful
            if (hammerHash < hammerHashTarget) {
                //LogPrintf("THREAD #%i: Solution found, returning\n", threadID);
                LOCK(cs_solution_vars);                                 // Expensive mutex only happens at write-out
                solutionFound.store(true);
                solvingRange = hammerRange;
                solvingHammer = i;
                return;
            }
        }
    }
    //LogPrintf("THREAD #%i: Out of tasks\n", threadID);
}



// Thor: Forge: Attempt to mint the next block
bool BusyHammers(const Consensus::Params& consensusParams, int height) {
    bool verbose = LogAcceptCategory(BCLog::FORGE);

    CBlockIndex* pindexPrev = chainActive.Tip();
    assert(pindexPrev != nullptr);

    // Sanity checks
    if (!IsForgeEnabled(pindexPrev, consensusParams)) {
        LogPrint(BCLog::FORGE, "BusyHammers: Skipping forge check: The Forge is not enabled on the network\n");
        return false;
    }
    if(!g_connman) {
        LogPrint(BCLog::FORGE, "BusyHammers: Skipping forge check: Peer-to-peer functionality missing or disabled\n");
        return false;
    }
    if (g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0) {
        LogPrint(BCLog::FORGE, "BusyHammers: Skipping forge check (not connected)\n");
        return false;
    }
    if (IsInitialBlockDownload()) {
        LogPrint(BCLog::FORGE, "BusyHammers: Skipping forge check (in initial block download)\n");
        return false;
    }
    // LitecoinCash: Forge 1.1: Check that there aren't too many consecutive Forge blocks
    if (IsForge11Enabled(pindexPrev, consensusParams)) {
        int forgeBlocksAtTip = 0;
        CBlockIndex* pindexTemp = pindexPrev;
        while (pindexTemp->GetBlockHeader().IsForgeMined(consensusParams)) {
            assert(pindexTemp->pprev);
            pindexTemp = pindexTemp->pprev;
            forgeBlocksAtTip++;
        }
        if (forgeBlocksAtTip >= consensusParams.maxConsecutiveForgeBlocks) {
            LogPrintf("BusyHammers: Skipping forge check (max Forge blocks without a POW block reached)\n");
            return false;
        }
    } else {
        // Check previous block wasn't forgemined
        if (pindexPrev->GetBlockHeader().IsForgeMined(consensusParams)) {
            LogPrintf("BusyHammers: Skipping forge check (Forge block must follow a POW block)\n");
            return false;
        }
    }

    // Get wallet
    JSONRPCRequest request;
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, true)) {
        LogPrint(BCLog::FORGE, "BusyHammers: Skipping forge check (wallet unavailable)\n");
        return false;
    }
    if (pwallet->IsLocked()) {
        LogPrint(BCLog::FORGE, "BusyHammers: Skipping forge check, wallet is locked\n");
        return false;
    }

    LogPrintf("********************* Forge: Hammers at work *********************\n");

    // Find deterministicRandString
    std::string deterministicRandString = GetDeterministicRandString(pindexPrev);
    if (verbose) LogPrintf("BusyHammers: deterministicRandString   = %s\n", deterministicRandString);

    // Find hammerHashTarget
    arith_uint256 hammerHashTarget;
    hammerHashTarget.SetCompact(GetNextForgeWorkRequired(pindexPrev, consensusParams));
    if (verbose) LogPrintf("BusyHammers: hammerHashTarget             = %s\n", hammerHashTarget.ToString());


    std::vector<CHammerCreationTransactionInfo> bcts = pwallet->GetBCTs(false, false, consensusParams);
    int totalHammers = 0;
    for (std::vector<CHammerCreationTransactionInfo>::const_iterator it = bcts.begin(); it != bcts.end(); it++) {
        CHammerCreationTransactionInfo bct = *it;

        if (bct.hammerStatus != "ready")
            continue;
       totalHammers += bct.hammerCount;
    }

    if (totalHammers == 0) {
        LogPrint(BCLog::FORGE, "BusyHammers: No mature hammers found\n");
        return false;
    }

    int coreCount = GetNumVirtualCores();
    int threadCount = gArgs.GetArg("-forgecheckthreads", DEFAULT_FORGE_THREADS);
    if (threadCount == -2)
        threadCount = std::max(1, coreCount - 1);
    else if (threadCount < 0 || threadCount > coreCount)
        threadCount = coreCount;
    else if (threadCount == 0)
        threadCount = 1;

    int hammersPerBin = ceil(totalHammers / (float)threadCount);  // We want to check this many hammers per thread

    // Bin the hammers according to desired thead count
    if (verbose) LogPrint(BCLog::FORGE, "BusyHammers: Binning %i hammers in %i bins (%i hammers per bin)\n", totalHammers, threadCount, hammersPerBin);
    std::vector<CHammerCreationTransactionInfo>::const_iterator bctIterator = bcts.begin();
    CHammerCreationTransactionInfo bct = *bctIterator;
    std::vector<std::vector<CHammerRange>> hammerBins;
    int hammerOffset = 0;                                      // Track offset in current BCT
    while(bctIterator != bcts.end()) {                      // Until we're out of BCTs
        std::vector<CHammerRange> currentBin;                  // Create a new bin
        int hammersInBin = 0;
        while (bctIterator != bcts.end()) {                 // Keep filling it until full
            int spaceLeft = hammersPerBin - hammersInBin;
            if (bct.hammerCount - hammerOffset <= spaceLeft) {    // If there's soom, add all the hammers from this BCT...
                CHammerRange range = {bct.txid, bct.goldAddress, bct.communityContrib, hammerOffset, bct.hammerCount - hammerOffset};
                currentBin.push_back(range);

                hammersInBin += bct.hammerCount - hammerOffset;
                hammerOffset = 0;

                do {                                        // ... and iterate to next BCT
                    bctIterator++;
                    if (bctIterator == bcts.end())
                        break;
                    bct = *bctIterator;
                } while (bct.hammerStatus != "mature");
            } else {                                        // Can't fit the whole thing to current bin; add what we can fit and let the rest go in next bin
                CHammerRange range = {bct.txid, bct.goldAddress, bct.communityContrib, hammerOffset, spaceLeft};
                currentBin.push_back(range);
                hammerOffset += spaceLeft;
                break;
            }

        }

        hammerBins.push_back(currentBin);
    }

    // Create a worker thread for each bin
    if (verbose) LogPrintf("BusyHammers: Running bins\n");
    solutionFound.store(false);
    earlyAbort.store(false);
    std::vector<std::vector<CHammerRange>>::const_iterator hammerBinIterator = hammerBins.begin();
    std::vector<boost::thread> binThreads;
    int64_t checkTime = GetTimeMillis();
    int binID = 0;
    while (hammerBinIterator != hammerBins.end()) {
        std::vector<CHammerRange> hammerBin = *hammerBinIterator;

        if (verbose) {
            LogPrintf("BusyHammers: Bin #%i\n", binID);
            std::vector<CHammerRange>::const_iterator hammerRangeIterator = hammerBin.begin();
            while (hammerRangeIterator != hammerBin.end()) {
                CHammerRange hammerRange = *hammerRangeIterator;
                LogPrintf("offset = %i, count = %i, txid = %s\n", hammerRange.offset, hammerRange.count, hammerRange.txid);
                hammerRangeIterator++;
            }
        }
        binThreads.push_back(boost::thread(CheckBin, binID++, hammerBin, deterministicRandString, hammerHashTarget));

        hammerBinIterator++;
    }

   // Add an extra thread to watch external abort conditions (eg new incoming block)
    bool useEarlyAbortThread = gArgs.GetBoolArg("-forgeearlyout", DEFAULT_FORGE_EARLY_OUT);
    if (verbose && useEarlyAbortThread)
        LogPrintf("BusyHammers: Will use early-abort thread\n");

    boost::thread* earlyAbortThread;
    if (useEarlyAbortThread)
        earlyAbortThread = new boost::thread(AbortWatchThread, height);

    // Wait for bin worker threads to find a solution or abort (in which case the others will all stop), or to run out of hammers
    for(auto& t:binThreads)
        t.join();

    checkTime = GetTimeMillis() - checkTime;

    // Handle early aborts
    if (useEarlyAbortThread) {
        if (earlyAbort.load()) {
            LogPrintf("BusyHammers: Chain state changed (check aborted after %ims)\n", checkTime);
            return false;
        } else {
            // We didn't abort; stop abort thread now
            earlyAbort.store(true);
            earlyAbortThread->join();
        }
    }

    // Check if a solution was found
    if (!solutionFound.load()) {
        LogPrintf("BusyHammers: No hammer meets hash target (%i hammers checked with %i threads in %ims)\n", totalHammers, threadCount, checkTime);
        return false;
    }

    LogPrintf("BusyHammers: Hammer meets hash target (check aborted after %ims). Solution with hammer #%i from BCT %s. Gold address is %s.\n", checkTime, solvingHammer, solvingRange.txid, solvingRange.goldAddress);

    // Assemble the Forge proof script
    std::vector<unsigned char> messageProofVec;
    std::vector<unsigned char> txidVec(solvingRange.txid.begin(), solvingRange.txid.end());
    CScript forgeProofScript;
    uint32_t bctHeight;
    {   // Don't lock longer than needed
        LOCK2(cs_main, pwallet->cs_wallet);

        CTxDestination dest = DecodeDestination(solvingRange.goldAddress);
        if (!IsValidDestination(dest)) {
            LogPrintf("BusyHammers: Gold destination invalid\n");
            return false;
        }

        const CKeyID *keyID = boost::get<CKeyID>(&dest);
        if (!keyID) {
            LogPrintf("BusyHammers: Wallet doesn't have privkey for gold destination\n");
            return false;
        }

        CKey key;
        if (!pwallet->GetKey(*keyID, key)) {
            LogPrintf("BusyHammers: Privkey unavailable\n");
            return false;
        }

        CHashWriter ss(SER_GETHASH, 0);
        ss << deterministicRandString;
        uint256 mhash = ss.GetHash();
        if (!key.SignCompact(mhash, messageProofVec)) {
            LogPrintf("BusyHammers: Couldn't sign the hammer proof!\n");
            return false;
        }
        if (verbose) LogPrintf("BusyHammers: messageSig                = %s\n", HexStr(&messageProofVec[0], &messageProofVec[messageProofVec.size()]));

        COutPoint out(uint256S(solvingRange.txid), 0);
        Coin coin;
        if (!pcoinsTip || !pcoinsTip->GetCoin(out, coin)) {
            LogPrintf("BusyHammers: Couldn't get the bct utxo!\n");
            return false;
        }
        bctHeight = coin.nHeight;
    }

    unsigned char hammerNonceEncoded[4];
    WriteLE32(hammerNonceEncoded, solvingHammer);
    std::vector<unsigned char> hammerNonceVec(hammerNonceEncoded, hammerNonceEncoded + 4);

    unsigned char bctHeightEncoded[4];
    WriteLE32(bctHeightEncoded, bctHeight);
    std::vector<unsigned char> bctHeightVec(bctHeightEncoded, bctHeightEncoded + 4);

    opcodetype communityContribFlag = solvingRange.communityContrib ? OP_TRUE : OP_FALSE;
    forgeProofScript << OP_RETURN << OP_HAMMER << hammerNonceVec << bctHeightVec << communityContribFlag << txidVec << messageProofVec;

    // Create gold script from gold address
    CScript goldScript = GetScriptForDestination(DecodeDestination(solvingRange.goldAddress));

    // Create a Forge block
    std::unique_ptr<CBlockTemplate> pblocktemplate(BlockAssembler(Params()).CreateNewBlock(goldScript, true, &forgeProofScript));
    if (!pblocktemplate.get()) {
        LogPrintf("BusyHammers: Couldn't create block\n");
        return false;
    }
    CBlock *pblock = &pblocktemplate->block;
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);  // Calc the merkle root

    // Make sure the new block's not stale
    {
        LOCK(cs_main);
        if (pblock->hashPrevBlock != chainActive.Tip()->GetBlockHash()) {
            LogPrintf("BusyHammers: Generated block is stale.\n");
            return false;
        }
    }

    if (verbose) {
        LogPrintf("BusyHammers: Block created:\n");
        LogPrintf("%s",pblock->ToString());
    }

    // Commit and propagate the block
    std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
    if (!ProcessNewBlock(Params(), shared_pblock, true, nullptr)) {
        LogPrintf("BusyHammers: Block wasn't accepted\n");
        return false;
    }

    LogPrintf("BusyHammers: ** Block mined\n");
    return true;
}
