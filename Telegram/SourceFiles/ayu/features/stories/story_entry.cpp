// This is the source code of freshGram.
#include "ayu/features/stories/story_entry.h"

#include "ayu/features/stories/story_poster.h"
#include "ayu/ui/boxes/story_compose_box.h"
#include "core/file_utilities.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "lang_auto.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "storage/storage_media_prepare.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_menu_icons.h"

namespace AyuStories {
namespace {

[[nodiscard]] QString ErrorText(const QString &type) {
	return (type == u"STORIES_TOO_MUCH"_q)
		? tr::ayu_StoryLimit(tr::now)
		: type;
}

void ChooseFile(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	const auto callback = crl::guard(controller, [=](
			FileDialog::OpenResult &&result) {
		const auto check = [](const Ui::PreparedList &list) {
			if (list.files.size() != 1) {
				return false;
			}
			const auto type = list.files.front().type;
			return (type == Ui::PreparedFile::Type::Photo)
				|| (type == Ui::PreparedFile::Type::Video);
		};
		const auto showError = [=](tr::phrase<> text) {
			controller->showToast(text(tr::now));
		};
		auto list = Storage::PreparedFileFromFilesDialog(
			std::move(result),
			check,
			showError,
			st::sendMediaPreviewSize,
			controller->session().premium());
		if (!list) {
			return;
		}
		AyuUi::ShowStoryComposeBox(
			controller,
			peer,
			std::move(list->files.front()));
	});
	FileDialog::GetOpenPath(
		controller->content().get(),
		tr::ayu_StoryChoose(tr::now),
		FileDialog::PhotoVideoFilesFilter(),
		callback);
}

} // namespace

void StartPosting(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	CheckCanPost(
		&controller->session(),
		peer,
		crl::guard(controller, [=] {
			ChooseFile(controller, peer);
		}),
		crl::guard(controller, [=](QString type) {
			controller->showToast(ErrorText(type));
		}));
}

void AddPostStoryAction(
		PeerData *peer,
		not_null<Window::SessionController*> controller,
		const Window::PeerMenuCallback &addCallback) {
	if (!peer || !peer->canPostStories()) {
		return;
	}
	const auto strong = not_null<PeerData*>(peer);
	addCallback(tr::ayu_StoryAdd(tr::now), [=] {
		StartPosting(controller, strong);
	}, &st::menuIconAdd);
}

} // namespace AyuStories
