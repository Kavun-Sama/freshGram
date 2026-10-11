// This file is part of freshGram.
#pragma once

#include <memory>
#include <optional>

namespace MTP {
class AuthKey;
using AuthKeyPtr = std::shared_ptr<AuthKey>;
} // namespace MTP

namespace AyuVault {

void SetKey(MTP::AuthKeyPtr key);
[[nodiscard]] bool HasKey();

[[nodiscard]] bool IsSealed(const QByteArray &bytes);
[[nodiscard]] QByteArray Seal(const QByteArray &plain);
[[nodiscard]] bool Unseal(
	const QByteArray &sealed,
	QByteArray &plain,
	bool requireSealed = false);

bool WriteFile(const QString &path, const QByteArray &plain);
bool WriteSealedFile(const QString &path, const QByteArray &plain);
[[nodiscard]] std::optional<QByteArray> ReadFile(
	const QString &path,
	bool requireSealed = false);
void SealFilesIn(const QString &directory);

} // namespace AyuVault
