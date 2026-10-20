// This is the source code of freshGram.
#pragma once

#include <optional>

namespace AyuMetadata {

enum class MediaKind : uchar {
	None,
	Image,
	Video,
};

[[nodiscard]] MediaKind KindOf(const QString &path, const QString &mime);

[[nodiscard]] std::optional<QByteArray> StripImage(const QByteArray &bytes);

[[nodiscard]] bool StripVideo(const QString &source, const QString &target);

[[nodiscard]] QString NewTempVideoPath(const QString &extension);

} // namespace AyuMetadata
