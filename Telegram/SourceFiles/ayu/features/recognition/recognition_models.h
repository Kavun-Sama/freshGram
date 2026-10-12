// This is the source code of freshGram.
//
// Models for the local speech and text recognition are downloaded from
// a Telegram channel, like the spellchecker dictionaries.
#pragma once

#include "storage/storage_cloud_blob.h"

#include <QtCore/QHash>

#include <optional>

namespace Main {
class Session;
} // namespace Main

namespace AyuRecognition {

enum class ModelKind {
	Whisper,
	Tesseract,
	TesseractAux, // The data for the automatic language detection.
};

struct Model : Storage::CloudBlob::Blob {
	ModelKind kind = ModelKind::Whisper;
	QString file; // The file name, both in the channel and on disk.
	QString code; // Language code of a Tesseract model.
	QString sha256;
	QString channel;
};

[[nodiscard]] QString ModelsPath();
[[nodiscard]] QString TessdataPath();
[[nodiscard]] QString ModelPath(const Model &model);
[[nodiscard]] bool ModelExists(const Model &model);
bool RemoveModel(const Model &model);

[[nodiscard]] std::vector<Model> Models(ModelKind kind);
[[nodiscard]] rpl::producer<> ModelsChanged();
void RefreshManifest(not_null<Main::Session*> session);

[[nodiscard]] MTP::DedicatedLoader::Location ModelLocation(
	const Model &model);

[[nodiscard]] std::optional<Model> SelectedWhisperModel();

// Checks the SHA-256 of a file, remembered until it changes; thread-safe.
[[nodiscard]] bool ModelFileIntact(
	const QString &path,
	const QString &sha256);

[[nodiscard]] QHash<QString, QString> TesseractHashes();

// The value of the setting meaning "choose the languages automatically".
inline constexpr auto kAutoOcrLanguages = "auto"_cs;

// Enabled and downloaded Tesseract languages joined with '+'. Returns
// "auto" if the automatic detection is chosen and its data is downloaded.
[[nodiscard]] QString EnabledOcrLanguages();

// All downloaded Tesseract languages.
[[nodiscard]] QStringList InstalledOcrLanguages();

class ModelLoader : public Storage::CloudBlob::BlobLoader {
public:
	ModelLoader(
		QObject *parent,
		not_null<Main::Session*> session,
		const Model &model,
		Fn<void()> destroyCallback);

	void destroy() override;

	rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	void unpack(const QString &path) override;
	void fail() override;

	const Model _model;
	// Be sure to always call it in the main thread.
	Fn<void()> _destroyCallback;
	rpl::lifetime _lifetime;

};

// The downloads go on after the models box is closed.
[[nodiscard]] ModelLoader *ModelDownload(int id);
void StartModelDownload(
	not_null<Main::Session*> session,
	const Model &model);
void CancelModelDownload(int id);
[[nodiscard]] rpl::producer<int> ModelDownloadFinished();

// Selects the model when its download finishes.
void WantModel(const Model &model);

[[nodiscard]] rpl::producer<bool> ManifestFinished();

} // namespace AyuRecognition
