// This is the source code of freshGram.
#include "ayu/features/stories/story_poster.h"

#include "api/api_sending.h"
#include "api/api_text_entities.h"
#include "apiwrap.h"
#include "base/random.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "lang_auto.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "storage/file_upload.h"
#include "ui/text/text_entity.h"

namespace AyuStories {
namespace {

class PrepareTask final : public Task {
public:
	PrepareTask(
		FileLoadTask::Args &&args,
		Fn<void(std::shared_ptr<FilePrepareResult>)> done)
	: _task(std::move(args))
	, _done(std::move(done)) {
	}

	void process() override {
		_task.process({ .generateGoodThumbnail = false });
	}
	void finish() override {
		_done(_task.peekResult());
	}

private:
	FileLoadTask _task;
	Fn<void(std::shared_ptr<FilePrepareResult>)> _done;

};

[[nodiscard]] QVector<MTPInputPrivacyRule> PrivacyRules(Audience audience) {
	switch (audience) {
	case Audience::Everyone:
		return { MTP_inputPrivacyValueAllowAll() };
	case Audience::CloseFriends:
		return { MTP_inputPrivacyValueAllowCloseFriends() };
	case Audience::Contacts:
		break;
	}
	return { MTP_inputPrivacyValueAllowContacts() };
}

} // namespace

struct Poster::Active {
	Draft draft;
	Callbacks callbacks;
	FullMsgId uploadId;
	uint64 dataId = 0;
	bool video = false;
	QString filemime;
	QVector<MTPDocumentAttribute> attributes;
};

void CheckCanPost(
		not_null<Main::Session*> session,
		not_null<PeerData*> peer,
		Fn<void()> allowed,
		Fn<void(QString)> failed) {
	session->api().request(
		MTPstories_CanSendStory(peer->input())
	).done([=](const MTPstories_CanSendStoryCount &result) {
		const auto remains
			= result.c_stories_canSendStoryCount().vcount_remains().v;
		if (remains > 0) {
			allowed();
		} else {
			failed(u"STORIES_TOO_MUCH"_q);
		}
	}).fail([=](const MTP::Error &error) {
		failed(error.type());
	}).send();
}

Poster::Poster(not_null<Main::Session*> session)
: _session(session)
, _api(&session->mtp())
, _prepareQueue(std::make_unique<TaskQueue>()) {
	subscribeToUploader();
}

Poster::~Poster() {
	cancel();
}

bool Poster::posting() const {
	return _active != nullptr;
}

void Poster::subscribeToUploader() {
	const auto &uploader = _session->uploader();

	uploader.photoReady(
	) | rpl::on_next([=](const Storage::UploadedMedia &data) {
		if (!_active || (_active->uploadId != data.fullId)) {
			return;
		}
		uploaded(_token, MTP_inputMediaUploadedPhoto(
			MTP_flags(0),
			data.info.file,
			MTP_vector<MTPInputDocument>(QVector<MTPInputDocument>()),
			MTPint(),
			MTPInputDocument()));
	}, _lifetime);

	uploader.documentReady(
	) | rpl::on_next([=](const Storage::UploadedMedia &data) {
		if (!_active || (_active->uploadId != data.fullId)) {
			return;
		}
		using Flag = MTPDinputMediaUploadedDocument::Flag;
		const auto flags = data.info.thumb ? Flag::f_thumb : Flag();
		uploaded(_token, MTP_inputMediaUploadedDocument(
			MTP_flags(flags),
			data.info.file,
			data.info.thumb.value_or(MTPInputFile()),
			MTP_string(_active->filemime),
			MTP_vector<MTPDocumentAttribute>(_active->attributes),
			MTP_vector<MTPInputDocument>(),
			MTPInputPhoto(),
			MTP_int(0),
			MTP_int(0)));
	}, _lifetime);

	uploader.photoProgress(
	) | rpl::on_next([=](const FullMsgId &id) {
		if (!_active
			|| (_active->uploadId != id)
			|| !_active->dataId
			|| !_active->callbacks.progress) {
			return;
		}
		_active->callbacks.progress(
			_session->data().photo(_active->dataId)->progress());
	}, _lifetime);

	uploader.documentProgress(
	) | rpl::on_next([=](const FullMsgId &id) {
		if (!_active
			|| (_active->uploadId != id)
			|| !_active->dataId
			|| !_active->callbacks.progress) {
			return;
		}
		_active->callbacks.progress(
			_session->data().document(_active->dataId)->progress());
	}, _lifetime);

	const auto failed = [=](const FullMsgId &id) {
		if (_active && (_active->uploadId == id)) {
			fail(_token, tr::lng_attach_failed(tr::now));
		}
	};
	uploader.photoFailed() | rpl::on_next(failed, _lifetime);
	uploader.documentFailed() | rpl::on_next(failed, _lifetime);
}

void Poster::post(Draft &&draft, Callbacks &&callbacks) {
	cancel();
	const auto token = ++_token;
	const auto type = draft.file.type;
	const auto video = (type == Ui::PreparedFile::Type::Video);
	const auto peer = draft.peer;
	_active = std::make_unique<Active>(Active{
		.draft = std::move(draft),
		.callbacks = std::move(callbacks),
		.video = video,
	});
	auto &file = _active->draft.file;
	_prepareQueue->addTask(std::make_unique<PrepareTask>(
		FileLoadTask::Args{
			.session = _session,
			.filepath = file.path,
			.content = file.content,
			.information = std::move(file.information),
			.videoCover = nullptr,
			.type = video ? SendMediaType::File : SendMediaType::Photo,
			.to = FileLoadTo(
				peer->id,
				Api::SendOptions(),
				FullReplyTo(),
				MsgId()),
			.caption = TextWithTags(),
			.spoiler = false,
			.album = nullptr,
			.forceFile = false,
			.idOverride = 0,
			.displayName = file.displayName,
		},
		[=](std::shared_ptr<FilePrepareResult> result) {
			prepared(token, std::move(result));
		}));
}

void Poster::prepared(
		uint64 token,
		std::shared_ptr<FilePrepareResult> result) {
	if (!_active || (_token != token)) {
		return;
	}
	if (!result) {
		fail(token, tr::lng_attach_failed(tr::now));
		return;
	}
	if (_active->video) {
		if (result->filemime != u"video/mp4"_q) {
			fail(token, tr::ayu_StoryVideoFormat(tr::now));
			return;
		}
		result->document.match([&](const MTPDdocument &data) {
			_active->attributes = data.vattributes().v;
		}, [](const auto &) {
		});
		_active->filemime = result->filemime;
	} else if (result->type != SendMediaType::Photo) {
		fail(token, tr::lng_attach_failed(tr::now));
		return;
	}
	_active->dataId = result->id;
	_active->uploadId = FullMsgId(
		_active->draft.peer->id,
		_session->data().nextLocalMessageId());
	_session->uploader().upload(_active->uploadId, result);
}

void Poster::uploaded(uint64 token, MTPInputMedia media) {
	if (!_active || (_token != token)) {
		return;
	}
	send(token, std::move(media));
}

void Poster::send(uint64 token, MTPInputMedia media) {
	const auto &draft = _active->draft;
	using Flag = MTPstories_SendStory::Flag;
	auto flags = MTPstories_SendStory::Flags(0);
	if (draft.pinned) {
		flags |= Flag::f_pinned;
	}
	if (draft.protect) {
		flags |= Flag::f_noforwards;
	}
	if (draft.period > 0) {
		flags |= Flag::f_period;
	}
	auto entities = Api::EntitiesToMTP(
		_session,
		TextUtilities::ConvertTextTagsToEntities(draft.caption.tags),
		Api::ConvertOption::SkipLocal);
	if (!draft.caption.text.isEmpty()) {
		flags |= Flag::f_caption;
		if (!entities.v.isEmpty()) {
			flags |= Flag::f_entities;
		}
	}
	_api.request(MTPstories_SendStory(
		MTP_flags(flags),
		draft.peer->input(),
		std::move(media),
		MTPVector<MTPMediaArea>(),
		MTP_string(draft.caption.text),
		entities,
		MTP_vector<MTPInputPrivacyRule>(PrivacyRules(draft.audience)),
		MTP_long(base::RandomValue<uint64>()),
		MTP_int(draft.period),
		MTPInputPeer(),
		MTPint(),
		MTPVector<MTPint>(),
		MTPInputDocument()
	)).done([=](const MTPUpdates &result) {
		if (_token != token || !_active) {
			return;
		}
		_session->api().applyUpdates(result);
		const auto done = std::move(_active->callbacks.done);
		_active = nullptr;
		if (done) {
			done();
		}
	}).fail([=](const MTP::Error &error) {
		fail(token, error.type());
	}).send();
}

void Poster::fail(uint64 token, const QString &text) {
	if (!_active || (_token != token)) {
		return;
	}
	const auto failed = std::move(_active->callbacks.failed);
	if (_active->uploadId) {
		_session->uploader().cancel(_active->uploadId);
	}
	_active = nullptr;
	if (failed) {
		failed(text);
	}
}

void Poster::cancel() {
	if (!_active) {
		return;
	}
	++_token;
	if (_active->uploadId) {
		_session->uploader().cancel(_active->uploadId);
	}
	_active = nullptr;
}

} // namespace AyuStories
