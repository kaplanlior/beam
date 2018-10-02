// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "wallet/wallet_db.h"
#include "wallet/keystore.h"

#include <jni.h>

#if defined(__ANDROID__)
#include <android/log.h>
#define LOGI(...) \
  ((void)__android_log_print(ANDROID_LOG_INFO, "beamwallet::", __VA_ARGS__))

#define LOGE(...) \
  ((void)__android_log_print(ANDROID_LOG_ERROR, "beamwallet::", __VA_ARGS__))
#else
#define LOGI(...) {printf("beamwallet::"); printf(__VA_ARGS__); putc('\n', stdout);}
#define LOGE(...) {printf("beamwallet::"); printf(__VA_ARGS__); putc('\n', stderr);}
#endif

#define CONCAT1(prefix, class, function)    CONCAT2(prefix, class, function)
#define CONCAT2(prefix, class, function)    Java_ ## prefix ## _ ## class ## _ ## function

#define DEF2STR2(x) #x
#define DEF2STR(x) DEF2STR2(x)

#define BEAM_JAVA_PACKAGE(sep) 					com ## sep ## mw ## sep ## beam ## sep ## beamwallet ## sep ## core
#define BEAM_JAVA_PREFIX                        BEAM_JAVA_PACKAGE(_)
#define BEAM_JAVA_PATH                        	DEF2STR(BEAM_JAVA_PACKAGE(/))
#define BEAM_JAVA_API_INTERFACE(function)       CONCAT1(BEAM_JAVA_PREFIX, Api, function)
#define BEAM_JAVA_WALLET_INTERFACE(function)    CONCAT1(BEAM_JAVA_PREFIX, Wallet, function)

#define WALLET_FILENAME "wallet.db"
#define BBS_FILENAME "keys.bbs"

using namespace beam;

namespace
{
	struct JString
	{
		JString(JNIEnv *envVal, jstring nameVal)
			: env(envVal)
			, name(nameVal)
			, isCopy(JNI_FALSE)
			, data(env->GetStringUTFChars(name, &isCopy))
		{
		}

		~JString()
		{
			if (isCopy == JNI_TRUE)
			{
				env->ReleaseStringUTFChars(name, data);
			}
		}

		std::string value() const
		{
			return data;
		}
	private:
		JNIEnv* env;
		jstring name;
		jboolean isCopy;
		const char* data;
	};

	using WalletDBList = std::vector<IKeyChain::Ptr>;
	WalletDBList wallets;

    inline void setLongField(JNIEnv *env, jclass clazz, jobject obj, const char* name, jlong value)
    {
        env->SetLongField(obj, env->GetFieldID(clazz, name, "J"), value);
    }

    inline void setIntField(JNIEnv *env, jclass clazz, jobject obj, const char* name, jint value)
    {
        env->SetIntField(obj, env->GetFieldID(clazz, name, "I"), value);
    }

	inline void setHashField(JNIEnv *env, jclass clazz, jobject obj, const char* name, const ECC::uintBig& value)
	{
		jbyteArray hash = env->NewByteArray(ECC::uintBig::nBytes);
		jbyte* hashBytes = env->GetByteArrayElements(hash, NULL);

		std::memcpy(hashBytes, value.m_pData, ECC::uintBig::nBytes);

		env->SetObjectField(obj, env->GetFieldID(clazz, name, "[B"), hash);

		env->ReleaseByteArrayElements(hash, hashBytes, 0);
	}

    jobject regWallet(JNIEnv *env, jobject thiz, IKeyChain::Ptr wallet)
    {
	    wallets.push_back(wallet);

        jclass Wallet = env->FindClass(BEAM_JAVA_PATH "/Wallet");
	    jobject walletObj = env->AllocObject(Wallet);

        setLongField(env, Wallet, walletObj, "_this", wallets.size() - 1);

	    return walletObj;
    }

    IKeyChain::Ptr getWallet(JNIEnv *env, jobject thiz)
    {
	    jclass Wallet = env->FindClass(BEAM_JAVA_PATH "/Wallet");
	    jfieldID _this = env->GetFieldID(Wallet, "_this", "J");
	    return wallets[env->GetLongField(thiz, _this)];
    }

}


#ifdef __cplusplus
extern "C" {
#endif

JNIEXPORT jobject JNICALL BEAM_JAVA_API_INTERFACE(createWallet)(JNIEnv *env, jobject thiz, 
	jstring appDataStr, jstring passStr, jstring seed)
{
	LOGI("creating wallet...");

	auto appData = JString(env, appDataStr).value();
	auto pass = JString(env, passStr).value();

	auto wallet = Keychain::init(
		appData + "/" WALLET_FILENAME,
		pass,
		SecString(JString(env, seed).value()).hash());

	if(wallet)
	{
		LOGI("wallet successfully created.");

		{
			IKeyStore::Options options;
			options.flags = IKeyStore::Options::local_file | IKeyStore::Options::enable_all_keys;
			options.fileName = appData + "/" BBS_FILENAME;

			IKeyStore::Ptr keystore = IKeyStore::create(options, pass.data(), pass.size());

			// generate default address
			WalletAddress defaultAddress = {};
			defaultAddress.m_own = true;
			defaultAddress.m_label = "default";
			defaultAddress.m_createTime = getTimestamp();
			defaultAddress.m_duration = std::numeric_limits<uint64_t>::max();
			keystore->gen_keypair(defaultAddress.m_walletID);
			keystore->save_keypair(defaultAddress.m_walletID, true);

			wallet->saveAddress(defaultAddress);
		}

		return regWallet(env, thiz, wallet);
	}

	LOGE("wallet creation error.");

	return nullptr;
}

JNIEXPORT jboolean JNICALL BEAM_JAVA_API_INTERFACE(isWalletInitialized)(JNIEnv *env, jobject thiz, 
	jstring appData)
{
	LOGI("checking if wallet exists...");

	return Keychain::isInitialized(JString(env, appData).value() + "/" WALLET_FILENAME) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jobject JNICALL BEAM_JAVA_API_INTERFACE(openWallet)(JNIEnv *env, jobject thiz, 
	jstring appData, jstring pass)
{
	LOGI("opening wallet...");

	auto wallet = Keychain::open(JString(env, appData).value() + "/" WALLET_FILENAME, JString(env, pass).value());

	if(wallet)
	{
		LOGI("wallet successfully opened.");

		return regWallet(env, thiz, wallet);
	}

	LOGE("wallet not opened.");

	return nullptr;
}

JNIEXPORT void JNICALL BEAM_JAVA_WALLET_INTERFACE(closeWallet)(JNIEnv *env, jobject thiz)
{
	LOGI("closing wallet...");

	getWallet(env, thiz).reset();
}

JNIEXPORT void JNICALL BEAM_JAVA_WALLET_INTERFACE(checkWalletPassword)(JNIEnv *env, jobject thiz,
	int index, jstring passStr)
{
	LOGI("changing wallet password...");

	getWallet(env, thiz)->changePassword(JString(env, passStr).value());
}

JNIEXPORT jobject JNICALL BEAM_JAVA_WALLET_INTERFACE(getSystemState)(JNIEnv *env, jobject thiz,
	int index)
{
	LOGI("getting System State...");

	Block::SystemState::ID stateID = {};
	getWallet(env, thiz)->getSystemStateID(stateID);

	jclass SystemState = env->FindClass(BEAM_JAVA_PATH "/SystemState");
	jobject systemState = env->AllocObject(SystemState);

	setLongField(env, SystemState, systemState, "height", stateID.m_Height);
	setHashField(env, SystemState, systemState, "hash", stateID.m_Hash);

	return systemState;
}

JNIEXPORT jobject JNICALL BEAM_JAVA_WALLET_INTERFACE(getUtxos)(JNIEnv *env, jobject thiz)
{
	LOGI("getting System State...");

	jclass Utxo = env->FindClass(BEAM_JAVA_PATH "/Utxo");
	std::vector<jobject> utxosVec;

	getWallet(env, thiz)->visit([&](const Coin& coin)->bool
	{
		jobject utxo = env->AllocObject(Utxo);

        setLongField(env, Utxo, utxo, "id", coin.m_id);
        setLongField(env, Utxo, utxo, "amount", coin.m_amount);
        setIntField(env, Utxo, utxo, "status", coin.m_status);
        setLongField(env, Utxo, utxo, "createHeight", coin.m_createHeight);
        setLongField(env, Utxo, utxo, "maturity", coin.m_maturity);
        setIntField(env, Utxo, utxo, "keyType", static_cast<jint>(coin.m_key_type));
        setLongField(env, Utxo, utxo, "confirmHeight", coin.m_confirmHeight);
        setHashField(env, Utxo, utxo, "confirmHash", coin.m_confirmHash);
        setLongField(env, Utxo, utxo, "lockHeight", coin.m_lockedHeight);

        // TODO: add m_createTxId m_spentTxId here

		utxosVec.push_back(utxo);

		return true;
	});

	jobjectArray utxos = env->NewObjectArray(utxosVec.size(), Utxo, NULL);

	for (int i = 0; i < utxosVec.size(); ++i)
		env->SetObjectArrayElement(utxos, i, utxosVec[i]);

	return utxos;
}

#ifdef __cplusplus
}
#endif