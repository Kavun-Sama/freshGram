// This is the source code of freshGram.
#include "ayu/features/passcode_guard/lock_background.h"

#include "ayu/ayu_settings.h"
#include "ui/image/image_prepare.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtGui/QImageReader>
#include <QtGui/QImageWriter>
#include <QtGui/QPainter>

namespace AyuLockBackground {
namespace {

constexpr auto kMaxFileSize = qint64(30 * 1024 * 1024);
constexpr auto kMaxSide = 3840;
constexpr auto kMaxSourceSide = 16384;
constexpr auto kMaxSourcePixels = qint64(40'000'000);
constexpr auto kBlurDownscale = 4;
constexpr auto kBlurRadius = 12;

struct State {
	QImage source;
	bool loading = false;
	bool failed = false;
	QImage prepared;
	QSize preparedSize;
	bool preparedBlur = false;
	int preparedDim = 0;
	bool preparing = false;
	int generation = 0;
	rpl::event_stream<> updates;
};

[[nodiscard]] State &GetState() {
	static auto result = State();
	return result;
}

[[nodiscard]] QString FilePath() {
	return cWorkingDir() + u"tdata/lock_background.jpg"_q;
}

// WHY: the file is read before the passcode is entered, so only the
// JPEG decoder is ever used and the sizes are checked before decoding.
[[nodiscard]] QImage Read(const QString &path, const QByteArray &format) {
	if (QFileInfo(path).size() > kMaxFileSize) {
		return QImage();
	}
	auto reader = QImageReader(path, format);
	reader.setAutoTransform(true);
	if (!format.isEmpty()) {
		reader.setAutoDetectImageFormat(false);
	} else {
		const auto detected = reader.format().toLower();
		if (detected != "jpeg"
			&& detected != "jpg"
			&& detected != "png"
			&& detected != "webp") {
			return QImage();
		}
	}
	const auto size = reader.size();
	if (size.isEmpty()
		|| size.width() > kMaxSourceSide
		|| size.height() > kMaxSourceSide
		|| qint64(size.width()) * size.height() > kMaxSourcePixels) {
		return QImage();
	}
	if (size.width() > kMaxSide || size.height() > kMaxSide) {
		reader.setScaledSize(size.scaled(
			kMaxSide,
			kMaxSide,
			Qt::KeepAspectRatio));
	}
	auto result = reader.read();
	if (result.isNull()) {
		return QImage();
	}
	if (result.width() > kMaxSide || result.height() > kMaxSide) {
		result = result.scaled(
			kMaxSide,
			kMaxSide,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}
	return Images::Opaque(std::move(result));
}

[[nodiscard]] QImage Prepare(QImage source, QSize size, bool blur, int dim) {
	auto result = source.scaled(
		size,
		Qt::KeepAspectRatioByExpanding,
		Qt::SmoothTransformation);
	if (result.isNull()) {
		return QImage();
	}
	result = result.copy(
		(result.width() - size.width()) / 2,
		(result.height() - size.height()) / 2,
		size.width(),
		size.height());
	if (blur) {
		const auto small = QSize(
			std::max(size.width() / kBlurDownscale, 1),
			std::max(size.height() / kBlurDownscale, 1));
		result = Images::BlurLargeImage(
			result.scaled(
				small,
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation),
			kBlurRadius
		).scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	}
	result = std::move(result).convertToFormat(
		QImage::Format_ARGB32_Premultiplied);
	if (dim > 0) {
		auto p = QPainter(&result);
		p.fillRect(
			result.rect(),
			QColor(0, 0, 0, std::clamp(dim, 0, 100) * 255 / 100));
	}
	return result;
}

void Load() {
	auto &state = GetState();
	if (state.loading || state.failed || !state.source.isNull()) {
		return;
	}
	state.loading = true;
	const auto generation = state.generation;
	crl::async([=, path = FilePath()] {
		auto image = Read(path, "jpg");
		crl::on_main([=, image = std::move(image)]() mutable {
			auto &state = GetState();
			if (state.generation != generation) {
				return;
			}
			state.loading = false;
			state.failed = image.isNull();
			state.source = std::move(image);
			state.updates.fire({});
		});
	});
}

} // namespace

void Import(const QString &path, Fn<void(bool)> done) {
	crl::async([=, target = FilePath()] {
		auto image = Read(path, QByteArray());
		auto ok = !image.isNull();
		if (ok) {
			const auto temp = target + u".new"_q;
			auto writer = QImageWriter(temp, "jpg");
			writer.setQuality(90);
			ok = writer.write(image);
			if (ok) {
				QFile::remove(target);
				ok = QFile::rename(temp, target);
			}
			if (!ok) {
				QFile::remove(temp);
			}
		}
		crl::on_main([=] {
			if (ok) {
				Clear();
				GetState().updates.fire({});
			}
			done(ok);
		});
	});
}

void Remove() {
	QFile::remove(FilePath());
	auto &settings = AyuSettings::getInstance();
	if (settings.lockScreenBackground() == 2) {
		settings.setLockScreenBackground(0);
	}
	Clear();
	GetState().updates.fire({});
}

QImage Prepared(QSize size, bool blur, int dim) {
	auto &state = GetState();
	if (size.isEmpty()) {
		return QImage();
	} else if (state.source.isNull()) {
		Load();
		return QImage();
	}
	if (state.preparedSize != size
		|| state.preparedBlur != blur
		|| state.preparedDim != dim) {
		if (!state.preparing) {
			state.preparing = true;
			const auto generation = state.generation;
			crl::async([=, source = state.source] {
				auto image = Prepare(source, size, blur, dim);
				crl::on_main([=, image = std::move(image)]() mutable {
					auto &state = GetState();
					if (state.generation != generation) {
						return;
					}
					state.preparing = false;
					state.prepared = std::move(image);
					state.preparedSize = size;
					state.preparedBlur = blur;
					state.preparedDim = dim;
					state.updates.fire({});
				});
			});
		}
	}
	return state.prepared;
}

rpl::producer<> Updates() {
	return GetState().updates.events();
}

void Clear() {
	auto &state = GetState();
	++state.generation;
	state.source = QImage();
	state.prepared = QImage();
	state.preparedSize = QSize();
	state.loading = false;
	state.failed = false;
	state.preparing = false;
}

} // namespace AyuLockBackground
