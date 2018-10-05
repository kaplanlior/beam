package com.mw.beam.beamwallet.core

/**
 * Created by vain onnellinen on 10/1/18.
 */
object Api {
    init {
        System.loadLibrary("wallet-jni")
    }

    external fun createWallet(dbPath: String, pass: String, seed: String): Wallet
    external fun openWallet(dbPath: String, pass: String): Wallet
    external fun isWalletInitialized(dbPath: String): Boolean
}