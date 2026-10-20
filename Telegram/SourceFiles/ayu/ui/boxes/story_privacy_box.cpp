// This is the source code of freshGram.
#include "ayu/ui/boxes/story_privacy_box.h"

#include "boxes/peer_list_controllers.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"

namespace AyuUi {
namespace {

class Controller final : public ContactsBoxController {
public:
	Controller(
		not_null<Main::Session*> session,
		rpl::producer<QString> title,
		std::vector<not_null<PeerData*>> selected)
	: ContactsBoxController(session)
	, _title(std::move(title))
	, _selected(std::move(selected)) {
	}

	void rowClicked(not_null<PeerListRow*> row) override {
		delegate()->peerListSetRowChecked(row, !row->checked());
	}

protected:
	void prepareViewHook() override {
		delegate()->peerListSetTitle(std::move(_title));
		delegate()->peerListAddSelectedPeers(base::take(_selected));
	}

private:
	rpl::producer<QString> _title;
	std::vector<not_null<PeerData*>> _selected;

};

} // namespace

void ShowStoryUsersBox(
		not_null<Window::SessionController*> controller,
		rpl::producer<QString> title,
		const std::vector<not_null<UserData*>> &selected,
		Fn<void(std::vector<not_null<UserData*>>)> done) {
	auto peers = std::vector<not_null<PeerData*>>();
	peers.reserve(selected.size());
	for (const auto &user : selected) {
		peers.push_back(user);
	}
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->addButton(tr::lng_settings_save(), [=] {
			auto users = std::vector<not_null<UserData*>>();
			for (const auto &peer : box->collectSelectedRows()) {
				if (const auto user = peer->asUser()) {
					users.push_back(user);
				}
			}
			const auto callback = done;
			box->closeBox();
			callback(std::move(users));
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	};
	controller->show(Box<PeerListBox>(
		std::make_unique<Controller>(
			&controller->session(),
			std::move(title),
			std::move(peers)),
		std::move(initBox)));
}

} // namespace AyuUi
