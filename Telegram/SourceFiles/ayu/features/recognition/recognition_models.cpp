// This is the source code of freshGram.
#include "ayu/features/recognition/recognition_models.h"

#include "ayu/ayu_settings.h"
#include "base/unique_qptr.h"
#include "base/weak_ptr.h"
#include "settings.h"
#include "logs.h"
#include "main/main_session.h"
#include "mtproto/dedicated_file_loader.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <map>

namespace AyuRecognition {
namespace {

constexpr auto kModelsChannel = "freshgram_models"_cs;
constexpr auto kFallbackManifestPostId = 22;
constexpr auto kMaxWhisperSize = int64(1200) * 1024 * 1024;
constexpr auto kMaxTesseractSize = int64(200) * 1024 * 1024;
constexpr auto kSha256Length = 64;

std::vector<Model> ModelsList;
bool ModelsLoadedFromCache = false;
rpl::event_stream<> ModelsListChanged;
rpl::event_stream<bool> ManifestFinishedStream;

struct Download {
	base::unique_qptr<ModelLoader> loader;
	std::shared_ptr<bool> active;
};

std::map<int, Download> Downloads;
std::map<int, Model> WantedModels;
rpl::event_stream<int> DownloadFinishedStream;

[[nodiscard]] QString ManifestCachePath() {
	return ModelsPath() + u"/manifest.json"_q;
}

[[nodiscard]] bool ParseLocation(
		const QString &text,
		QString &channel,
		int &postId) {
	const auto sep = text.indexOf('#');
	if (sep <= 0 || sep == text.size() - 1) {
		return false;
	}
	auto ok = false;
	const auto parsed = text.mid(sep + 1).toInt(&ok);
	if (!ok || parsed <= 0) {
		return false;
	}
	channel = text.left(sep);
	postId = parsed;
	return true;
}

[[nodiscard]] bool IsSha256(const QString &value) {
	if (value.size() != kSha256Length) {
		return false;
	}
	for (const auto ch : value) {
		if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool IsSafeFileName(const QString &name) {
	return !name.isEmpty()
		&& !name.contains('/')
		&& !name.contains('\\')
		&& !name.startsWith('.');
}

[[nodiscard]] std::vector<Model> ParseManifest(const QByteArray &bytes) {
	auto result = std::vector<Model>();
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(bytes, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		LOG(("Recognition Error: manifest parse failed: %1"
			).arg(error.errorString()));
		return result;
	}
	const auto list = document.object().value(u"models"_q).toArray();
	result.reserve(list.size());
	for (const auto &value : list) {
		const auto object = value.toObject();
		auto model = Model();
		model.id = object.value(u"id"_q).toInt();
		model.size = int64(object.value(u"size"_q).toDouble());
		model.name = object.value(u"name"_q).toString();
		model.file = object.value(u"file"_q).toString();
		model.code = object.value(u"code"_q).toString();
		model.sha256 = object.value(u"sha256"_q).toString().toLower();
		const auto kind = object.value(u"kind"_q).toString();
		if (kind == u"whisper"_q) {
			model.kind = ModelKind::Whisper;
		} else if (kind == u"tesseract"_q) {
			model.kind = ModelKind::Tesseract;
		} else if (kind == u"tesseract-aux"_q) {
			model.kind = ModelKind::TesseractAux;
		} else {
			continue;
		}
		const auto maxSize = (model.kind == ModelKind::Whisper)
			? kMaxWhisperSize
			: kMaxTesseractSize;
		if (!model.id
			|| model.size <= 0
			|| model.size > maxSize
			|| !IsSha256(model.sha256)
			|| model.name.isEmpty()
			|| !IsSafeFileName(model.file)
			|| (model.kind != ModelKind::Whisper
				&& (!IsSafeFileName(model.code)
					|| model.code.contains('+')))
			|| !ParseLocation(
				object.value(u"location"_q).toString(),
				model.channel,
				model.postId)
			|| model.channel.compare(
				kModelsChannel.utf16(),
				Qt::CaseInsensitive) != 0) {
			continue;
		}
		result.push_back(std::move(model));
	}
	return result;
}

void ApplyManifest(std::vector<Model> parsed) {
	if (parsed.empty()) {
		return;
	}
	ModelsList = std::move(parsed);
	ModelsListChanged.fire({});
}

void LoadCachedManifest() {
	if (ModelsLoadedFromCache) {
		return;
	}
	ModelsLoadedFromCache = true;
	auto file = QFile(ManifestCachePath());
	if (file.open(QIODevice::ReadOnly)) {
		auto parsed = ParseManifest(file.readAll());
		if (!parsed.empty()) {
			ModelsList = std::move(parsed);
		}
	}
}

[[nodiscard]] QString DownloadFolder(int id) {
	// The loader creates only the last level of its output folder.
	const auto parent = ModelsPath() + u"/.download"_q;
	QDir().mkpath(parent);
	return parent + '/' + QString::number(id);
}

[[nodiscard]] QString ShaOfFile(const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return QString();
	}
	auto hash = QCryptographicHash(QCryptographicHash::Sha256);
	if (!hash.addData(&file)) {
		return QString();
	}
	return QString::fromLatin1(hash.result().toHex());
}

void ApplyDownloaded(const Model &model) {
	auto &settings = AyuSettings::getInstance();
	switch (model.kind) {
	case ModelKind::Whisper:
		settings.setWhisperModel(model.file);
		break;
	case ModelKind::TesseractAux:
		settings.setOcrLanguages(kAutoOcrLanguages.utf16());
		break;
	case ModelKind::Tesseract: {
		const auto current = settings.ocrLanguages();
		if (current == kAutoOcrLanguages.utf16()) {
			break;
		}
		auto codes = current.split('+', Qt::SkipEmptyParts);
		if (!codes.contains(model.code)) {
			codes.push_back(model.code);
			settings.setOcrLanguages(codes.join('+'));
		}
	} break;
	}
}

void FinishDownload(const Model &model, const std::shared_ptr<bool> &active) {
	if (!*active) {
		return;
	}
	const auto id = model.id;
	Downloads.erase(id);
	const auto wanted = WantedModels.find(id);
	if (wanted != end(WantedModels)) {
		if (ModelExists(model)) {
			ApplyDownloaded(model);
		}
		WantedModels.erase(wanted);
	}
	DownloadFinishedStream.fire_copy(id);
}

class ManifestLoader final : public base::has_weak_ptr {
public:
	explicit ManifestLoader(base::weak_ptr<Main::Session> session);

	void start();

private:
	void resolved(const MTPInputChannel &channel);
	void download(int postId);
	void finish();

	MTP::WeakInstance _mtp;
	std::unique_ptr<MTP::DedicatedLoader> _loader;

};

std::shared_ptr<ManifestLoader> ActiveManifestLoader;

ManifestLoader::ManifestLoader(base::weak_ptr<Main::Session> session)
: _mtp(session) {
}

void ManifestLoader::start() {
	if (!_mtp.valid()) {
		finish();
		return;
	}
	const auto weak = base::make_weak(this);
	MTP::ResolveChannel(&_mtp, kModelsChannel.utf16(), [=](
			const MTPInputChannel &channel) {
		if (const auto strong = weak.get()) {
			strong->resolved(channel);
		}
	}, [=] {
		if (const auto strong = weak.get()) {
			strong->finish();
		}
	});
}

void ManifestLoader::resolved(const MTPInputChannel &channel) {
	const auto weak = base::make_weak(this);
	_mtp.send(
		MTPchannels_GetFullChannel(channel),
		[=](const MTPmessages_ChatFull &result) {
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			auto postId = kFallbackManifestPostId;
			result.match([&](const MTPDmessages_chatFull &data) {
				data.vfull_chat().match([&](const MTPDchannelFull &full) {
					if (const auto pinned = full.vpinned_msg_id()) {
						if (pinned->v > 0) {
							postId = pinned->v;
						}
					}
				}, [](const auto &) {});
			});
			strong->download(postId);
		},
		[=](const MTP::Error &) {
			if (const auto strong = weak.get()) {
				strong->download(kFallbackManifestPostId);
			}
		});
}

void ManifestLoader::download(int postId) {
	const auto folder = ModelsPath() + u"/.manifest"_q;
	QDir(folder).removeRecursively();
	QDir().mkpath(folder);
	const auto weak = base::make_weak(this);
	MTP::StartDedicatedLoader(
		&_mtp,
		MTP::DedicatedLoader::Location{
			.username = kModelsChannel.utf16(),
			.postId = postId,
		},
		folder,
		[=](std::unique_ptr<MTP::DedicatedLoader> loader) {
			const auto strong = weak.get();
			if (!strong) {
				return;
			} else if (!loader) {
				strong->finish();
				return;
			}
			strong->_loader = std::move(loader);
			strong->_loader->ready(
			) | rpl::on_next([=](const QString &path) {
				auto file = QFile(path);
				if (file.open(QIODevice::ReadOnly)) {
					const auto bytes = file.readAll();
					file.close();
					auto parsed = ParseManifest(bytes);
					if (!parsed.empty()) {
						auto cache = QFile(ManifestCachePath());
						if (cache.open(QIODevice::WriteOnly)) {
							cache.write(bytes);
						}
						ApplyManifest(std::move(parsed));
					}
				}
				QDir(QFileInfo(path).absolutePath()).removeRecursively();
				if (const auto strong = weak.get()) {
					strong->finish();
				}
			}, strong->_loader->lifetime());
			strong->_loader->failed(
			) | rpl::on_next([=] {
				if (const auto strong = weak.get()) {
					strong->finish();
				}
			}, strong->_loader->lifetime());
			strong->_loader->start();
		});
}

void ManifestLoader::finish() {
	crl::on_main([] {
		ActiveManifestLoader.reset();
		LoadCachedManifest();
		ManifestFinishedStream.fire(!ModelsList.empty());
	});
}

} // namespace

QString ModelsPath() {
	return cWorkingDir() + u"tdata/recognition"_q;
}

QString TessdataPath() {
	return ModelsPath() + u"/tessdata"_q;
}

QString ModelPath(const Model &model) {
	return (model.kind == ModelKind::Whisper)
		? (ModelsPath() + u"/whisper/"_q + model.file)
		: (TessdataPath() + '/' + model.code + u".traineddata"_q);
}

bool ModelExists(const Model &model) {
	const auto info = QFileInfo(ModelPath(model));
	return info.exists() && info.size() == model.size;
}

bool RemoveModel(const Model &model) {
	return QFile::remove(ModelPath(model));
}

std::vector<Model> Models(ModelKind kind) {
	LoadCachedManifest();
	auto result = std::vector<Model>();
	for (const auto &model : ModelsList) {
		if (model.kind == kind) {
			result.push_back(model);
		}
	}
	return result;
}

rpl::producer<> ModelsChanged() {
	return ModelsListChanged.events();
}

void RefreshManifest(not_null<Main::Session*> session) {
	if (ActiveManifestLoader) {
		return;
	}
	ActiveManifestLoader = std::make_shared<ManifestLoader>(
		base::make_weak(session));
	ActiveManifestLoader->start();
}

MTP::DedicatedLoader::Location ModelLocation(const Model &model) {
	return MTP::DedicatedLoader::Location{
		.username = model.channel,
		.postId = model.postId,
	};
}

QString SelectedWhisperModelPath() {
	const auto &file = AyuSettings::getInstance().whisperModel();
	if (file.isEmpty()) {
		return QString();
	}
	for (const auto &model : Models(ModelKind::Whisper)) {
		if (model.file == file) {
			return ModelExists(model) ? ModelPath(model) : QString();
		}
	}
	return QString();
}

QStringList InstalledOcrLanguages() {
	auto result = QStringList();
	for (const auto &model : Models(ModelKind::Tesseract)) {
		if (ModelExists(model)) {
			result.push_back(model.code);
		}
	}
	return result;
}

QString EnabledOcrLanguages() {
	const auto &value = AyuSettings::getInstance().ocrLanguages();
	if (value == kAutoOcrLanguages.utf16()) {
		for (const auto &model : Models(ModelKind::TesseractAux)) {
			if (ModelExists(model)) {
				return value;
			}
		}
		return QString();
	}
	const auto enabled = value.split('+', Qt::SkipEmptyParts);
	auto result = QStringList();
	for (const auto &model : Models(ModelKind::Tesseract)) {
		if (enabled.contains(model.code) && ModelExists(model)) {
			result.push_back(model.code);
		}
	}
	return result.join('+');
}

ModelLoader::ModelLoader(
	QObject *parent,
	not_null<Main::Session*> session,
	const Model &model,
	Fn<void()> destroyCallback)
: BlobLoader(
	parent,
	session,
	model.id,
	ModelLocation(model),
	DownloadFolder(model.id),
	model.size)
, _model(model)
, _destroyCallback(std::move(destroyCallback)) {
}

void ModelLoader::unpack(const QString &path) {
	const auto weak = base::make_weak(this);
	const auto model = _model;
	const auto folder = _folder;
	crl::async([=] {
		auto success = false;
		if (QFileInfo(path).size() == model.size
			&& ShaOfFile(path) == model.sha256) {
			const auto target = ModelPath(model);
			QDir().mkpath(QFileInfo(target).absolutePath());
			QFile::remove(target);
			success = QFile::rename(path, target)
				|| (QFile::copy(path, target) && QFile::remove(path));
		} else {
			LOG(("Recognition Error: '%1' failed the integrity check."
				).arg(model.file));
		}
		QDir(folder).removeRecursively();
		crl::on_main(weak, [=] {
			if (success) {
				destroy();
			} else {
				fail();
			}
		});
	});
}

void ModelLoader::destroy() {
	Expects(_destroyCallback);

	crl::on_main(_destroyCallback);
}

void ModelLoader::fail() {
	BlobLoader::fail();
	destroy();
}

ModelLoader *ModelDownload(int id) {
	const auto i = Downloads.find(id);
	return (i != end(Downloads)) ? i->second.loader.get() : nullptr;
}

void StartModelDownload(
		not_null<Main::Session*> session,
		const Model &model) {
	if (Downloads.contains(model.id)) {
		return;
	}
	const auto active = std::make_shared<bool>(true);
	Downloads.emplace(model.id, Download{
		.loader = base::make_unique_q<ModelLoader>(
			QCoreApplication::instance(),
			session,
			model,
			[=] { FinishDownload(model, active); }),
		.active = active,
	});
}

void CancelModelDownload(int id) {
	WantedModels.erase(id);
	const auto i = Downloads.find(id);
	if (i != end(Downloads)) {
		*i->second.active = false;
		Downloads.erase(i);
	}
}

rpl::producer<int> ModelDownloadFinished() {
	return DownloadFinishedStream.events();
}

void WantModel(const Model &model) {
	if (ModelExists(model)) {
		ApplyDownloaded(model);
	} else if (Downloads.contains(model.id)) {
		WantedModels.insert_or_assign(model.id, model);
	}
}

rpl::producer<bool> ManifestFinished() {
	return ManifestFinishedStream.events();
}

} // namespace AyuRecognition
