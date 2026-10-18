// This is the source code of freshGram.
#include "storage/details/storage_passcode_kdf.h"

#include "base/openssl_help.h"
#include "mtproto/mtproto_auth_key.h"
#include "storage/details/storage_file_utilities.h"

#include <openssl/opensslv.h>

#if OPENSSL_VERSION_NUMBER >= 0x30200000L
#include <openssl/core_names.h>
#include <openssl/kdf.h>
#define FRESHGRAM_HAS_ARGON2
#endif // OPENSSL_VERSION_NUMBER >= 0x30200000L

#include <QtCore/QDataStream>

#include <mutex>

namespace Storage {
namespace {

constexpr auto kMagic = 0x314B4746; // "FGK1"
constexpr auto kMaxMemoryKiB = quint32(1024 * 1024);
constexpr auto kMaxIterations = quint32(16);
constexpr auto kMaxLanes = quint32(4);
constexpr auto kMaxCached = 4;

struct CachedKey {
	QByteArray tag;
	MTP::AuthKeyPtr key;
};

std::mutex KeyCacheMutex;
std::vector<CachedKey> KeyCache;

[[nodiscard]] bool Valid(const PasscodeKdf &kdf) {
	switch (kdf.type) {
	case PasscodeKdfType::Pbkdf2:
		return true;
	case PasscodeKdfType::Argon2id:
		return (kdf.memoryKiB >= 8 * kdf.lanes)
			&& (kdf.memoryKiB <= kMaxMemoryKiB)
			&& (kdf.iterations >= 1)
			&& (kdf.iterations <= kMaxIterations)
			&& (kdf.lanes >= 1)
			&& (kdf.lanes <= kMaxLanes);
	}
	return false;
}

[[nodiscard]] QByteArray CacheTag(
		const QByteArray &passcode,
		const QByteArray &salt,
		const PasscodeKdf &kdf) {
	const auto params = SerializePasscodeKdf(kdf);
	const auto hash = openssl::Sha256(
		bytes::make_span(passcode),
		bytes::make_span(salt),
		bytes::make_span(params));
	return QByteArray(
		reinterpret_cast<const char*>(hash.data()),
		int(hash.size()));
}

[[nodiscard]] MTP::AuthKeyPtr FindCached(const QByteArray &tag) {
	auto lock = std::lock_guard(KeyCacheMutex);
	for (const auto &entry : KeyCache) {
		if (entry.tag == tag) {
			return entry.key;
		}
	}
	return nullptr;
}

[[nodiscard]] MTP::AuthKeyPtr Argon2(
		const QByteArray &passcode,
		const QByteArray &salt,
		const PasscodeKdf &kdf) {
#ifdef FRESHGRAM_HAS_ARGON2
	const auto algorithm = EVP_KDF_fetch(nullptr, "ARGON2ID", nullptr);
	if (!algorithm) {
		LOG(("Passcode Error: Argon2id is not available."));
		return nullptr;
	}
	const auto context = EVP_KDF_CTX_new(algorithm);
	EVP_KDF_free(algorithm);
	if (!context) {
		return nullptr;
	}
	auto iterations = uint32_t(kdf.iterations);
	auto lanes = uint32_t(kdf.lanes);
	auto threads = uint32_t(1);
	auto memory = uint32_t(kdf.memoryKiB);
	auto password = passcode;
	auto saltCopy = salt;
	OSSL_PARAM params[] = {
		OSSL_PARAM_construct_octet_string(
			OSSL_KDF_PARAM_PASSWORD,
			password.data(),
			size_t(password.size())),
		OSSL_PARAM_construct_octet_string(
			OSSL_KDF_PARAM_SALT,
			saltCopy.data(),
			size_t(saltCopy.size())),
		OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ITER, &iterations),
		OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_THREADS, &threads),
		OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_LANES, &lanes),
		OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_MEMCOST, &memory),
		OSSL_PARAM_construct_end(),
	};
	auto key = MTP::AuthKey::Data{ { gsl::byte{} } };
	const auto ok = EVP_KDF_derive(
		context,
		reinterpret_cast<unsigned char*>(key.data()),
		key.size(),
		params);
	EVP_KDF_CTX_free(context);
	bytes::set_with_const(bytes::make_detached_span(password), gsl::byte{});
	if (ok != 1) {
		LOG(("Passcode Error: Argon2id derivation failed."));
		return nullptr;
	}
	return std::make_shared<MTP::AuthKey>(key);
#else // FRESHGRAM_HAS_ARGON2
	LOG(("Passcode Error: built without Argon2id support."));
	return nullptr;
#endif // FRESHGRAM_HAS_ARGON2
}

} // namespace

bool PasscodeKdfArgon2Supported() {
#ifdef FRESHGRAM_HAS_ARGON2
	static const auto result = [] {
		const auto algorithm = EVP_KDF_fetch(nullptr, "ARGON2ID", nullptr);
		EVP_KDF_free(algorithm);
		return (algorithm != nullptr);
	}();
	return result;
#else // FRESHGRAM_HAS_ARGON2
	return false;
#endif // FRESHGRAM_HAS_ARGON2
}

PasscodeKdf PasscodeKdfFor(PasscodeKdfLevel level) {
	switch (level) {
	case PasscodeKdfLevel::Standard:
		return {};
	case PasscodeKdfLevel::Strong:
		return {
			.type = PasscodeKdfType::Argon2id,
			.memoryKiB = 64 * 1024,
			.iterations = 3,
			.lanes = 1,
		};
	case PasscodeKdfLevel::Maximum:
		return {
			.type = PasscodeKdfType::Argon2id,
			.memoryKiB = 256 * 1024,
			.iterations = 4,
			.lanes = 1,
		};
	}
	return {};
}

PasscodeKdfLevel PasscodeKdfLevelOf(const PasscodeKdf &kdf) {
	if (kdf.type != PasscodeKdfType::Argon2id) {
		return PasscodeKdfLevel::Standard;
	}
	return (kdf.memoryKiB >= PasscodeKdfFor(PasscodeKdfLevel::Maximum).memoryKiB)
		? PasscodeKdfLevel::Maximum
		: PasscodeKdfLevel::Strong;
}

QByteArray SerializePasscodeKdf(const PasscodeKdf &kdf) {
	if (kdf.type == PasscodeKdfType::Pbkdf2) {
		return QByteArray();
	}
	auto result = QByteArray();
	auto stream = QDataStream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream
		<< quint32(kMagic)
		<< quint32(kdf.type)
		<< kdf.memoryKiB
		<< kdf.iterations
		<< kdf.lanes;
	return result;
}

std::optional<PasscodeKdf> DeserializePasscodeKdf(const QByteArray &data) {
	if (data.isEmpty()) {
		return PasscodeKdf();
	}
	auto stream = QDataStream(data);
	stream.setVersion(QDataStream::Qt_5_1);
	auto magic = quint32();
	auto type = quint32();
	auto result = PasscodeKdf();
	stream
		>> magic
		>> type
		>> result.memoryKiB
		>> result.iterations
		>> result.lanes;
	if (stream.status() != QDataStream::Ok
		|| magic != quint32(kMagic)
		|| type != quint32(PasscodeKdfType::Argon2id)) {
		return std::nullopt;
	}
	result.type = PasscodeKdfType::Argon2id;
	if (!Valid(result)) {
		return std::nullopt;
	}
	return result;
}

MTP::AuthKeyPtr DerivePasscodeKey(
		const QByteArray &passcode,
		const QByteArray &salt,
		const PasscodeKdf &kdf) {
	if (passcode.isEmpty()) {
		return details::CreateLocalKey(passcode, salt);
	} else if (!Valid(kdf)) {
		return nullptr;
	} else if (auto cached = FindCached(CacheTag(passcode, salt, kdf))) {
		return cached;
	} else if (kdf.type == PasscodeKdfType::Pbkdf2) {
		return details::CreateLocalKey(passcode, salt);
	}
	return Argon2(passcode, salt, kdf);
}

void PrecomputePasscodeKey(
		const QByteArray &passcode,
		const QByteArray &salt,
		const PasscodeKdf &kdf) {
	if (passcode.isEmpty()) {
		return;
	}
	auto tag = CacheTag(passcode, salt, kdf);
	if (FindCached(tag)) {
		return;
	}
	auto key = DerivePasscodeKey(passcode, salt, kdf);
	if (!key) {
		return;
	}
	auto lock = std::lock_guard(KeyCacheMutex);
	if (KeyCache.size() >= kMaxCached) {
		KeyCache.erase(KeyCache.begin());
	}
	KeyCache.push_back({ std::move(tag), std::move(key) });
}

void ClearPrecomputedPasscodeKeys() {
	auto lock = std::lock_guard(KeyCacheMutex);
	KeyCache.clear();
}

} // namespace Storage
