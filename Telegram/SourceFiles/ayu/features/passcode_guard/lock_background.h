// This is the source code of freshGram.
#pragma once

namespace AyuLockBackground {

// Reads the chosen file, checks it and keeps it re-encoded as JPEG.
void Import(const QString &path, Fn<void(bool)> done);
void Remove();

// The image fitted to the size in device pixels, null while it is not ready.
[[nodiscard]] QImage Prepared(QSize size, bool blur, int dim);
[[nodiscard]] rpl::producer<> Updates();
void Clear();

} // namespace AyuLockBackground
