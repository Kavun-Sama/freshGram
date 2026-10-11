// This is the source code of freshGram.
#include "ayu/features/recognition/local_transcribe.h"

#include "ayu/ayu_settings.h"
#include "ayu/features/recognition/recognition_models.h"
#include "ayu/features/recognition/speech_recognition.h"
#include "base/weak_ptr.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

namespace AyuRecognition {
namespace {

constexpr auto kMaxDocumentSize = int64(200) * 1024 * 1024;

struct Job {
	base::weak_ptr<Main::Session> session;
	FullMsgId id;
	DocumentId documentId = 0;
	QString model;
	QString temporary;
	std::shared_ptr<Data::DocumentMedia> media;
	std::shared_ptr<std::atomic<bool>> cancel;
	Fn<void(LocalTranscript)> done;
	rpl::lifetime lifetime;
};

base::flat_map<FullMsgId, std::shared_ptr<Job>> Jobs;

void Finish(
		const std::shared_ptr<Job> &job,
		LocalTranscript result) {
	if (!job->temporary.isEmpty()) {
		QFile::remove(job->temporary);
		job->temporary = QString();
	}
	const auto done = std::move(job->done);
	const auto alive = (job->session.get() != nullptr);
	job->lifetime.destroy();
	Jobs.erase(job->id);
	if (alive && done) {
		done(std::move(result));
	}
}

[[nodiscard]] LocalTranscript Failure(const QString &error) {
	auto result = LocalTranscript();
	result.status = LocalTranscribeStatus::Failed;
	result.error = error;
	return result;
}

void Recognize(const std::shared_ptr<Job> &job, const QString &path) {
	job->cancel = TranscribeAsync({
		.audioPath = path,
		.modelPath = job->model,
	}, nullptr, [=](SpeechResult result) {
		auto transcript = LocalTranscript();
		if (result.cancelled) {
			transcript.status = LocalTranscribeStatus::Cancelled;
		} else if (!result.ok) {
			transcript.status = LocalTranscribeStatus::Failed;
			transcript.error = result.error;
		} else {
			transcript.status = LocalTranscribeStatus::Done;
			transcript.text = result.text;
		}
		Finish(job, std::move(transcript));
	});
}

void Start(const std::shared_ptr<Job> &job) {
	const auto session = job->session.get();
	if (!session) {
		Finish(job, Failure(QString()));
		return;
	}
	const auto document = session->data().document(job->documentId);
	const auto media = job->media;
	if (!media) {
		Finish(job, Failure(u"No data."_q));
		return;
	}
	auto path = document->filepath(true);
	if (path.isEmpty() || !QFileInfo::exists(path)) {
		const auto bytes = media->bytes();
		if (bytes.isEmpty()) {
			Finish(job, Failure(u"No data."_q));
			return;
		}
		const auto folder = ModelsPath() + u"/.tmp"_q;
		QDir().mkpath(folder);
		path = folder + '/' + QString::number(job->documentId) + u".bin"_q;
		auto file = QFile(path);
		if (!file.open(QIODevice::WriteOnly)
			|| file.write(bytes) != bytes.size()) {
			Finish(job, Failure(u"Could not save the file."_q));
			return;
		}
		job->temporary = path;
	}
	Recognize(job, path);
}

} // namespace

bool LocalTranscriptionEnabled() {
	return (AyuSettings::getInstance().transcriptionProvider()
			== TranscriptionProvider::WhisperLocal)
		&& SpeechRecognitionAvailable();
}

void TranscribeLocally(
		not_null<HistoryItem*> item,
		Fn<void(LocalTranscript)> done) {
	const auto media = item->media();
	const auto document = media ? media->document() : nullptr;
	if (!document
		|| !(document->isVoiceMessage() || document->isVideoMessage())) {
		auto result = LocalTranscript();
		result.error = u"Not a voice message."_q;
		done(std::move(result));
		return;
	}
	const auto model = SelectedWhisperModelPath();
	if (model.isEmpty()) {
		auto result = LocalTranscript();
		result.status = LocalTranscribeStatus::NoModel;
		done(std::move(result));
		return;
	} else if (document->size > kMaxDocumentSize) {
		done(Failure(u"The file is too large."_q));
		return;
	}

	const auto session = &item->history()->session();
	const auto id = item->fullId();
	if (Jobs.contains(id)) {
		return;
	}
	const auto job = std::make_shared<Job>();
	job->session = base::make_weak(session);
	job->id = id;
	job->documentId = document->id;
	job->model = model;
	job->done = std::move(done);
	job->media = document->createMediaView();
	Jobs.emplace(id, job);

	if (job->media->loaded(true)) {
		Start(job);
		return;
	}
	document->save(::Data::FileOrigin(id), QString());
	session->downloaderTaskFinished(
	) | rpl::filter([=] {
		const auto session = job->session.get();
		return !session
			|| (job->media->loaded()
				|| !session->data().document(job->documentId)->loading());
	}) | rpl::take(1) | rpl::on_next([=] {
		Start(job);
	}, job->lifetime);
}

} // namespace AyuRecognition
