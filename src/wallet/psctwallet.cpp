// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/psctwallet.h>

TransactionError FillPSCT(const CWallet* pwallet, PartiallySignedTransaction& psctx, bool& complete, int sighash_type, bool sign, bool bip32derivs)
{
    LOCK(pwallet->cs_wallet);
    // Get all of the previous transactions
    complete = true;
    for (unsigned int i = 0; i < psctx.tx->vin.size(); ++i) {
        const CTxIn& txin = psctx.tx->vin[i];
        PSCTInput& input = psctx.inputs.at(i);

        if (PSCTInputSigned(input)) {
            continue;
        }

        // Verify input looks sane. This will check that we have at most one uxto, witness or non-witness.
        if (!input.IsSane()) {
            return TransactionError::INVALID_PSCT;
        }

        // If we have no utxo, grab it from the wallet.
        if (!input.non_witness_utxo && input.witness_utxo.IsNull()) {
            const uint256& txhash = txin.prevout.hash;
            const auto it = pwallet->mapWallet.find(txhash);
            if (it != pwallet->mapWallet.end()) {
                const CWalletTx& wtx = it->second;
                // We only need the non_witness_utxo, which is a superset of the witness_utxo.
                //   The signing code will switch to the smaller witness_utxo if this is ok.
                input.non_witness_utxo = wtx.tx;
            }
        }

        // Get the Sighash type
        if (sign && input.sighash_type > 0 && input.sighash_type != sighash_type) {
            return TransactionError::SIGHASH_MISMATCH;
        }

        complete &= SignPSCTInput(HidingSigningProvider(pwallet, !sign, !bip32derivs), psctx, i, sighash_type);
    }

    // Fill in the bip32 keypaths and redeemscripts for the outputs so that hardware wallets can identify change
    for (unsigned int i = 0; i < psctx.tx->vout.size(); ++i) {
        const CTxOut& out = psctx.tx->vout.at(i);
        PSCTOutput& psct_out = psctx.outputs.at(i);

        // Fill a SignatureData with output info
        SignatureData sigdata;
        psct_out.FillSignatureData(sigdata);

        MutableTransactionSignatureCreator creator(psctx.tx.get_ptr(), 0, out.nValue, 1);
        ProduceSignature(HidingSigningProvider(pwallet, true, !bip32derivs), creator, out.scriptPubKey, sigdata);
        psct_out.FromSignatureData(sigdata);
    }

    return TransactionError::OK;
}
