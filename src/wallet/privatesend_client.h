// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PRIVATESENDCLIENT_H
#define PRIVATESENDCLIENT_H

#include <interfaces/chain.h>
#include <modules/masternode/masternode.h>
#include <modules/privatesend/privatesend.h>

struct CompactTallyItem;

class CPrivateSendClient;
class CReserveKey;
class CWallet;
class CConnman;

static const int DENOMS_COUNT_MAX                   = 100;

static const int MIN_PRIVATESEND_ROUNDS             = 2;
static const int MIN_PRIVATESEND_AMOUNT             = 2;
static const int MIN_PRIVATESEND_LIQUIDITY          = 0;
static const int MAX_PRIVATESEND_ROUNDS             = 16;
static const int MAX_PRIVATESEND_AMOUNT             = MAX_MONEY / COIN;
static const int MAX_PRIVATESEND_LIQUIDITY          = 100;
static const int DEFAULT_PRIVATESEND_ROUNDS         = 2;
static const int DEFAULT_PRIVATESEND_AMOUNT         = 1000;
static const int DEFAULT_PRIVATESEND_LIQUIDITY      = 0;

static const bool DEFAULT_PRIVATESEND_MULTISESSION  = false;

// Warn user if mixing in gui or try to create backup if mixing in daemon mode
// when we have only this many keys left
static const int PRIVATESEND_KEYS_THRESHOLD_WARNING = 100;
// Stop mixing completely, it's too dangerous to continue when we have only this many keys left
static const int PRIVATESEND_KEYS_THRESHOLD_STOP    = 50;

class CKeyHolderStorage
{
private:
    std::vector<std::unique_ptr<CReserveKey> > storage;
    mutable CCriticalSection cs_storage;

public:
    CScript AddKey(CWallet *pwalletIn);
    void KeepAll();
    void ReturnAll();

};

class CPendingDsaRequest
{
private:
    static const int TIMEOUT = 15;

    CService addr;
    CDarksendAccept dsa;
    int64_t nTimeCreated;

public:
    CPendingDsaRequest():
        addr(CService()),
        dsa(CDarksendAccept()),
        nTimeCreated(0)
    {}

    CPendingDsaRequest(const CService& addr_, const CDarksendAccept& dsa_):
        addr(addr_),
        dsa(dsa_)
    { nTimeCreated = GetTime(); }

    CService GetAddr() { return addr; }
    CDarksendAccept GetDSA() { return dsa; }
    bool IsExpired() { return GetTime() - nTimeCreated > TIMEOUT; }

    friend bool operator==(const CPendingDsaRequest& a, const CPendingDsaRequest& b)
    {
        return a.addr == b.addr && a.dsa == b.dsa;
    }
    friend bool operator!=(const CPendingDsaRequest& a, const CPendingDsaRequest& b)
    {
        return !(a == b);
    }
    explicit operator bool() const
    {
        return *this != CPendingDsaRequest();
    }
};

/** Used to keep track of current status of mixing pool
 */
class CPrivateSendClient : public CPrivateSendBase
{
private:
    CWallet* m_wallet;
    // Keep track of the used Masternodes
    std::vector<COutPoint> vecMasternodesUsed;

    std::vector<CAmount> vecDenominationsSkipped;
    std::vector<COutPoint> vecOutPointLocked;

    int nCachedLastSuccessBlock;
    int nMinBlocksToWait; // how many blocks to wait after one successful mixing tx in non-multisession mode

    // Keep track of current block height
    int nCachedBlockHeight;

    int nEntriesCount;
    bool fLastEntryAccepted;

    std::string strLastMessage;
    std::string strAutoDenomResult;

    masternode_info_t infoMixingMasternode;
    CMutableTransaction txMyCollateral; // client side collateral
    CPendingDsaRequest pendingDsaRequest;

    CKeyHolderStorage keyHolderStorage; // storage for keys used in PrepareDenominate

    /// Check for process
    void CheckPool();
    void CompletedTransaction(PoolMessage nMessageID);

    bool IsDenomSkipped(CAmount nDenomValue);

    bool WaitForAnotherBlock();

    // Make sure we have enough keys since last backup
    bool CheckAutomaticBackup();
    bool JoinExistingQueue(CAmount nBalanceNeedsAnonymized);
    bool StartNewQueue(CAmount nValueMin, CAmount nBalanceNeedsAnonymized);

    /// Create denominations
    bool CreateDenominated();
    bool CreateDenominated(interfaces::Chain::Lock &locked_chain, const CompactTallyItem& tallyItem, bool fCreateMixingCollaterals);

    /// Split up large inputs or make fee sized inputs
    bool MakeCollateralAmounts(interfaces::Chain::Lock &locked_chain);
    bool MakeCollateralAmounts(interfaces::Chain::Lock &locked_chain, const CompactTallyItem& tallyItem, bool fTryDenominated);

    /// As a client, submit part of a future mixing transaction to a Masternode to start the process
    bool SubmitDenominate();
    /// step 1: prepare denominated inputs and outputs
    bool PrepareDenominate(int nMinRounds, int nMaxRounds, std::string& strErrorRet, std::vector<CTxDSIn>& vecTxDSInRet, std::vector<CTxOut>& vecTxOutRet);
    /// step 2: send denominated inputs and outputs prepared in step 1
    bool SendDenominate(const std::vector<CTxDSIn>& vecTxDSIn, const std::vector<CTxOut>& vecTxOut);

    /// Get Masternode updates about the progress of mixing
    bool CheckPoolStateUpdate(PoolState nStateNew, int nEntriesCountNew, PoolStatusUpdate nStatusUpdate, PoolMessage nMessageID, int nSessionIDNew=0);
    // Set the 'state' value, with some logging and capturing when the state changed
    void SetState(PoolState nStateNew);

    /// As a client, check and sign the final transaction
    bool SignFinalTransaction(const CTransaction& finalTransactionNew, CNode* pnode);

    void RelayIn(const CDarkSendEntry& entry);

    void SetNull();

public:
    int nPrivateSendRounds;
    int nPrivateSendAmount;
    int nLiquidityProvider;
    bool fEnablePrivateSend;
    bool fPrivateSendMultiSession;

    int nCachedNumBlocks; //used for the overview screen
    bool fCreateAutoBackups; //builtin support for automatic backups

    explicit CPrivateSendClient(CWallet* pwallet) :
        m_wallet(pwallet),
        nCachedLastSuccessBlock(0),
        nMinBlocksToWait(1),
        txMyCollateral(CMutableTransaction()),
        nPrivateSendRounds(DEFAULT_PRIVATESEND_ROUNDS),
        nPrivateSendAmount(DEFAULT_PRIVATESEND_AMOUNT),
        nLiquidityProvider(DEFAULT_PRIVATESEND_LIQUIDITY),
        fEnablePrivateSend(false),
        fPrivateSendMultiSession(DEFAULT_PRIVATESEND_MULTISESSION),
        nCachedNumBlocks(std::numeric_limits<int>::max()),
        fCreateAutoBackups(true) { SetNull(); }

    void ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv);

    void ClearSkippedDenominations() { vecDenominationsSkipped.clear(); }

    void SetMinBlocksToWait(int nMinBlocksToWaitIn) { nMinBlocksToWait = nMinBlocksToWaitIn; }

    void ResetPool();

    void UnlockCoins();

    std::string GetStatus();

    bool GetMixingMasternodeInfo(masternode_info_t& mnInfoRet);

    bool IsMixingMasternode(const CNode* pnode);

    /// one-shot mixing attempt
    bool DoOnceDenominating();

    /// Passively run mixing in the background according to the configuration in settings
    bool DoAutomaticDenominating(interfaces::Chain::Lock &locked_chain);

    void ProcessPendingDsaRequest();

    void CheckTimeout();

    void UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork, bool fInitialDownload);
    void ClientTask();
};

#endif