// This is the source code of freshGram.
#pragma once

#include "mtproto/sender.h"
#include "storage/localimageloader.h"
#include "ui/chat/attach/attach_prepare.h"

namespace Main {
class Session;
} // namespace Main

class PeerData;

namespace AyuStories {

enum class Audience {
	Contacts,
	CloseFriends,
	Everyone,
};

struct Draft {
	not_null<PeerData*> peer;
	Ui::PreparedFile file;
	TextWithTags caption;
	Audience audience = Audience::Contacts;
	// Seconds, zero leaves the choice to the server.
	TimeId period = 24 * 3600;
	bool pinned = false;
	bool protect = false;
};

struct Callbacks {
	Fn<void(float64)> progress;
	Fn<void()> done;
	Fn<void(QString)> failed;
};

// Asks whether the peer can post one more story now.
void CheckCanPost(
	not_null<Main::Session*> session,
	not_null<PeerData*> peer,
	Fn<void()> allowed,
	Fn<void(QString)> failed);

// Uploads one photo or video and posts it as a story, one at a time.
class Poster final {
public:
	explicit Poster(not_null<Main::Session*> session);
	~Poster();

	void post(Draft &&draft, Callbacks &&callbacks);
	void cancel();

	[[nodiscard]] bool posting() const;

private:
	struct Active;

	void subscribeToUploader();
	void prepared(uint64 token, std::shared_ptr<FilePrepareResult> result);
	void uploaded(uint64 token, MTPInputMedia media);
	void send(uint64 token, MTPInputMedia media);
	void fail(uint64 token, const QString &text);

	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	std::unique_ptr<TaskQueue> _prepareQueue;
	std::unique_ptr<Active> _active;
	uint64 _token = 0;
	rpl::lifetime _lifetime;

};

} // namespace AyuStories
