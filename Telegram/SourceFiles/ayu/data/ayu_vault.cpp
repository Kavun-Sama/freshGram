// This file is part of freshGram.
#include "ayu/data/ayu_vault.h"

#include "mtproto/mtproto_auth_key.h"
#include "storage/details/storage_file_utilities.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QSaveFile>

#include <atomic>
#include <mutex>

namespace AyuVault {
namespace {

using namespace Storage::details;

constexpr auto kMagic = "AYV1";
constexpr auto kMagicSize = 4;

std::mutex KeyMutex;
MTP::AuthKeyPtr Key;

[[nodiscard]] MTP::AuthKeyPtr GetKey() {
	const auto lock = std::lock_guard(KeyMutex);
	return Key;
}

} // namespace

void SetKey(MTP::AuthKeyPtr key) {
	const auto lock = std::lock_guard(KeyMutex);
	Key = std::move(key);
}

bool HasKey() {
	return GetKey() != nullptr;
}

bool IsSealed(const QByteArray &bytes) {
	return bytes.size() > kMagicSize
		&& !memcmp(bytes.constData(), kMagic, kMagicSize);
}

QByteArray Seal(const QByteArray &plain) {
	const auto key = GetKey();
	if (!key) {
		return plain;
	}
	auto descriptor = EncryptedDescriptor(plain.size() + sizeof(quint32));
	descriptor.stream << plain;
	const auto encrypted = PrepareEncrypted(descriptor, key);
	auto result = QByteArray();
	result.reserve(kMagicSize + encrypted.size());
	result.append(kMagic, kMagicSize);
	result.append(encrypted);
	return result;
}

bool Unseal(
		const QByteArray &sealed,
		QByteArray &plain,
		bool requireSealed) {
	if (!IsSealed(sealed)) {
		if (requireSealed) {
			return false;
		}
		plain = sealed;
		return true;
	}
	const auto key = GetKey();
	if (!key) {
		return false;
	}
	auto descriptor = EncryptedDescriptor();
	if (!DecryptLocal(descriptor, sealed.mid(kMagicSize), key)) {
		return false;
	}
	auto result = QByteArray();
	descriptor.stream >> result;
	if (descriptor.stream.status() != QDataStream::Ok) {
		return false;
	}
	plain = std::move(result);
	return true;
}

bool WriteFile(const QString &path, const QByteArray &plain) {
	auto file = QSaveFile(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	}
	const auto bytes = Seal(plain);
	return (file.write(bytes) == bytes.size()) && file.commit();
}

bool WriteSealedFile(const QString &path, const QByteArray &plain) {
	return HasKey() && WriteFile(path, plain);
}

std::optional<QByteArray> ReadFile(const QString &path, bool requireSealed) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return std::nullopt;
	}
	const auto bytes = file.readAll();
	auto plain = QByteArray();
	if (!Unseal(bytes, plain, requireSealed)) {
		return std::nullopt;
	}
	return plain;
}

void SealFilesIn(const QString &directory) {
	if (!HasKey()) {
		return;
	}
	const auto dir = QDir(directory);
	for (const auto &info : dir.entryInfoList(QDir::Files)) {
		auto head = QByteArray();
		{
			auto file = QFile(info.absoluteFilePath());
			if (!file.open(QIODevice::ReadOnly)) {
				continue;
			}
			head = file.read(kMagicSize + 1);
		}
		if (IsSealed(head)) {
			continue;
		}
		if (const auto plain = ReadFile(info.absoluteFilePath())) {
			WriteFile(info.absoluteFilePath(), *plain);
		}
	}
}

} // namespace AyuVault
