// This is the source code of freshGram.
#include "ayu/ui/boxes/story_compose_box.h"

#include "ayu/features/stories/story_poster.h"
#include "ayu/ui/boxes/story_privacy_box.h"
#include "base/random.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_story.h"
#include "data/data_user.h"
#include "editor/photo_editor_layer_widget.h"
#include "editor/video/video_editor_layer.h"
#include "lang/lang_keys.h"
#include "lang_auto.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_ayu_styles.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtGui/QPainter>

#include <array>

namespace AyuUi {
namespace {

using AyuStories::Audience;

constexpr auto kPeriods = std::array<TimeId, 4>{
	6 * 3600,
	12 * 3600,
	24 * 3600,
	48 * 3600,
};
constexpr auto kDefaultPeriod = 24 * 3600;

// The choice made in the window, kept by the account and the author.
struct SavedChoice {
	TextWithTags caption;
	Audience audience = Audience::Contacts;
	std::vector<UserId> excepted;
	std::vector<UserId> selected;
	TimeId period = kDefaultPeriod;
	bool pinned = false;
	bool protect = false;
	std::vector<int> albums;
};

// WHY: the choice survives closing the window, until the app is closed.
[[nodiscard]] base::flat_map<std::pair<uint64, PeerId>, SavedChoice> &Saved() {
	static auto result = base::flat_map<
		std::pair<uint64, PeerId>,
		SavedChoice>();
	return result;
}

[[nodiscard]] std::pair<uint64, PeerId> SavedKey(not_null<PeerData*> peer) {
	return { peer->session().uniqueId(), peer->id };
}

[[nodiscard]] std::vector<UserId> UserIds(
		const std::vector<not_null<UserData*>> &users) {
	auto result = std::vector<UserId>();
	result.reserve(users.size());
	for (const auto &user : users) {
		result.push_back(peerToUser(user->id));
	}
	return result;
}

[[nodiscard]] std::vector<not_null<UserData*>> Users(
		not_null<Main::Session*> session,
		const std::vector<UserId> &ids) {
	auto result = std::vector<not_null<UserData*>>();
	for (const auto &id : ids) {
		if (const auto user = session->data().userLoaded(id)) {
			result.push_back(user);
		}
	}
	return result;
}

[[nodiscard]] std::vector<not_null<PeerData*>> Authors(
		not_null<PeerData*> initial) {
	const auto session = &initial->session();
	auto result = std::vector<not_null<PeerData*>>();
	const auto add = [&](not_null<PeerData*> peer) {
		if (peer->canPostStories() && !ranges::contains(result, peer)) {
			result.push_back(peer);
		}
	};
	result.push_back(initial);
	add(session->user());
	session->data().enumerateBroadcasts([&](not_null<ChannelData*> channel) {
		add(channel);
	});
	session->data().enumerateGroups([&](not_null<PeerData*> group) {
		add(group);
	});
	return result;
}

[[nodiscard]] Ui::PreparedFile CloneFile(const Ui::PreparedFile &file) {
	auto result = Ui::PreparedFile(file.path);
	result.displayName = file.displayName;
	result.content = file.content;
	result.size = file.size;
	if (file.information) {
		result.information = std::make_unique<Ui::PreparedFileInformation>(
			*file.information);
	}
	result.preview = file.preview;
	result.shownDimensions = file.shownDimensions;
	result.originalDimensions = file.originalDimensions;
	result.type = file.type;
	result.sendLargePhotos = file.sendLargePhotos;
	result.animationJob = file.animationJob;
	return result;
}

[[nodiscard]] bool CanEdit(const Ui::PreparedFile &file) {
	using Type = Ui::PreparedFile::Type;
	return (file.type == Type::Photo)
		|| (file.type == Type::Video
			&& file.information
			&& file.canEditVideo());
}

void FillStoryComposeBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		std::shared_ptr<Ui::PreparedFile> file) {
	box->setTitle(tr::ayu_StoryNew());

	struct State {
		explicit State(not_null<PeerData*> peer)
		: poster(&peer->session())
		, author(peer) {
		}

		AyuStories::Poster poster;
		Ui::PreparedList list;
		uint64 fileKey = base::RandomValue<uint64>();
		rpl::variable<not_null<PeerData*>> author;
		std::vector<not_null<UserData*>> excepted;
		std::vector<not_null<UserData*>> selected;
		std::vector<int> albums;
		rpl::event_stream<> usersChanged;
		base::unique_qptr<Ui::PopupMenu> menu;
		bool busy = false;
		bool posted = false;
	};
	const auto session = &peer->session();
	const auto state = box->lifetime().make_state<State>(peer);
	state->list.files.push_back(std::move(*file));
	const auto current = [=]() -> Ui::PreparedFile& {
		return state->list.files.front();
	};

	const auto i = Saved().find(SavedKey(peer));
	const auto saved = (i != end(Saved())) ? i->second : SavedChoice();
	state->excepted = Users(session, saved.excepted);
	state->selected = Users(session, saved.selected);
	state->albums = saved.albums;

	const auto authors = Authors(peer);
	if (authors.size() > 1) {
		const auto author = box->addRow(
			object_ptr<Ui::LinkButton>(box, QString()),
			st::storyRowPadding);
		state->author.value(
		) | rpl::on_next([=](not_null<PeerData*> value) {
			author->setText(tr::ayu_StoryAuthor(
				tr::now,
				lt_name,
				value->isSelf()
					? tr::lng_stories_my_name(tr::now)
					: value->name()));
		}, author->lifetime());
		author->setClickedCallback([=] {
			state->menu = base::make_unique_q<Ui::PopupMenu>(
				author,
				st::popupMenuWithIcons);
			for (const auto &item : authors) {
				const auto text = item->isSelf()
					? tr::lng_stories_my_name(tr::now)
					: item->name();
				state->menu->addAction(text, [=] {
					if (state->author.current() == item) {
						return;
					}
					state->albums.clear();
					state->author = item;
					AyuStories::CheckCanPost(
						session,
						item,
						[] {},
						crl::guard(box, [=](QString type) {
							controller->showToast(
								AyuStories::ErrorText(type));
						}));
				});
			}
			state->menu->popup(QCursor::pos());
		});
	}

	const auto preview = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(box, st::storyPreviewHeight),
		st::storyRowPadding);
	preview->paintRequest(
	) | rpl::on_next([=] {
		const auto &image = current().preview;
		if (image.isNull()) {
			return;
		}
		auto p = QPainter(preview);
		const auto scaled = image.size().scaled(
			preview->size(),
			Qt::KeepAspectRatio);
		p.drawImage(
			QRect(
				(preview->width() - scaled.width()) / 2,
				(preview->height() - scaled.height()) / 2,
				scaled.width(),
				scaled.height()),
			image);
	}, preview->lifetime());

	if (CanEdit(current())) {
		const auto edit = box->addRow(
			object_ptr<Ui::LinkButton>(box, tr::ayu_StoryEdit(tr::now)),
			st::storyRowPadding);
		edit->setClickedCallback([=] {
			if (state->busy) {
				return;
			}
			const auto done = [=](bool ok) {
				if (ok) {
					// The edited file is uploaded again.
					state->fileKey = base::RandomValue<uint64>();
					preview->update();
				}
			};
			if (current().type == Ui::PreparedFile::Type::Photo) {
				Editor::OpenWithPreparedFile(
					box,
					controller->uiShow(),
					&current(),
					st::sendMediaPreviewSize,
					done,
					0,
					QSize(720, 1280),
					false);
			} else {
				Editor::OpenWithPreparedVideoFile(
					box,
					controller->uiShow(),
					&state->list,
					0,
					st::sendMediaPreviewSize,
					done);
			}
		});
	}

	const auto premium = session->premium();
	const auto limit = session->appConfig().get<int>(
		premium
			? u"story_caption_length_limit_premium"_q
			: u"story_caption_length_limit_default"_q,
		premium ? 2048 : 200);
	const auto caption = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			Ui::InputField::Mode::MultiLine,
			tr::ayu_StoryCaption()),
		st::storyRowPadding);
	caption->setMaxLength(limit);
	caption->setTextWithTags(saved.caption);

	const auto audienceWrap = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box,
			object_ptr<Ui::VerticalLayout>(box)),
		style::margins());
	const auto audienceInner = audienceWrap->entity();
	const auto audience = std::make_shared<Ui::RadiobuttonGroup>(
		int(saved.audience));
	Ui::AddSubsectionTitle(audienceInner, tr::ayu_StoryAudience());
	const auto addAudience = [&](Audience value, const QString &text) {
		audienceInner->add(
			object_ptr<Ui::Radiobutton>(
				audienceInner,
				audience,
				int(value),
				text,
				st::defaultCheckbox),
			st::storyRowPadding);
	};
	addAudience(Audience::Everyone, tr::ayu_StoryEveryone(tr::now));
	addAudience(Audience::Contacts, tr::ayu_StoryContacts(tr::now));
	addAudience(
		Audience::CloseFriends,
		tr::ayu_StoryCloseFriends(tr::now));
	addAudience(Audience::Selected, tr::ayu_StorySelected(tr::now));

	const auto usersWrap = audienceInner->add(
		object_ptr<Ui::SlideWrap<Ui::LinkButton>>(
			audienceInner,
			object_ptr<Ui::LinkButton>(audienceInner, QString()),
			st::storyRowPadding));
	const auto usersLink = usersWrap->entity();
	rpl::combine(
		audience->value(),
		state->usersChanged.events_starting_with({})
	) | rpl::on_next([=](int value, auto) {
		const auto chosen = Audience(value);
		usersWrap->toggle(
			chosen != Audience::CloseFriends,
			anim::type::instant);
		const auto selected = (chosen == Audience::Selected);
		const auto count = QString::number(selected
			? state->selected.size()
			: state->excepted.size());
		usersLink->setText(selected
			? tr::ayu_StoryChooseUsers(tr::now, lt_amount, count)
			: tr::ayu_StoryExcept(tr::now, lt_amount, count));
	}, usersLink->lifetime());
	usersLink->setClickedCallback([=] {
		const auto selected = (Audience(audience->current())
			== Audience::Selected);
		ShowStoryUsersBox(
			controller,
			selected
				? tr::ayu_StorySelectedTitle()
				: tr::ayu_StoryExceptTitle(),
			selected ? state->selected : state->excepted,
			crl::guard(box, [=](std::vector<not_null<UserData*>> users) {
				(selected ? state->selected : state->excepted)
					= std::move(users);
				state->usersChanged.fire({});
			}));
	});
	audienceWrap->toggleOn(state->author.value(
	) | rpl::map([](not_null<PeerData*> author) {
		return author->isSelf();
	}));
	audienceWrap->finishAnimating();

	// Only Premium may choose how long a story is visible.
	const auto period = std::make_shared<Ui::RadiobuttonGroup>([&] {
		const auto i = ranges::find(kPeriods, saved.period);
		return (i != end(kPeriods))
			? int(i - begin(kPeriods))
			: int(ranges::find(kPeriods, kDefaultPeriod) - begin(kPeriods));
	}());
	if (premium) {
		Ui::AddSubsectionTitle(
			box->verticalLayout(),
			tr::ayu_StoryDuration());
		const auto periodTexts = std::array<QString, 4>{
			tr::ayu_Story6h(tr::now),
			tr::ayu_Story12h(tr::now),
			tr::ayu_Story24h(tr::now),
			tr::ayu_Story48h(tr::now),
		};
		for (auto i = 0; i != int(kPeriods.size()); ++i) {
			box->addRow(
				object_ptr<Ui::Radiobutton>(
					box,
					period,
					i,
					periodTexts[i],
					st::defaultCheckbox),
				st::storyRowPadding);
		}
	}
	const auto pinned = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			tr::ayu_StoryKeep(tr::now),
			saved.pinned,
			st::defaultCheckbox),
		st::storyRowPadding);
	const auto protect = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			tr::ayu_StoryProtect(tr::now),
			saved.protect,
			st::defaultCheckbox),
		st::storyRowPadding);

	const auto albumsWrap = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box,
			object_ptr<Ui::VerticalLayout>(box)),
		style::margins());
	const auto albumsInner = albumsWrap->entity();
	albumsWrap->hide(anim::type::instant);
	state->author.value(
	) | rpl::map([=](not_null<PeerData*> author) {
		return session->data().stories().albumsListValue(author->id);
	}) | rpl::flatten_latest(
	) | rpl::on_next([=](const std::vector<Data::StoryAlbum> &list) {
		state->albums.erase(
			ranges::remove_if(state->albums, [&](int id) {
				return !ranges::contains(list, id, &Data::StoryAlbum::id);
			}),
			end(state->albums));
		albumsInner->clear();
		if (list.empty()) {
			albumsWrap->hide(anim::type::instant);
			return;
		}
		Ui::AddSubsectionTitle(albumsInner, tr::ayu_StoryAlbums());
		for (const auto &album : list) {
			const auto id = album.id;
			const auto checkbox = albumsInner->add(
				object_ptr<Ui::Checkbox>(
					albumsInner,
					album.title,
					ranges::contains(state->albums, id),
					st::defaultCheckbox),
				st::storyRowPadding);
			checkbox->checkedChanges(
			) | rpl::on_next([=](bool checked) {
				state->albums.erase(
					ranges::remove(state->albums, id),
					end(state->albums));
				if (checked) {
					state->albums.push_back(id);
					// WHY: the stories of the albums are on the profile.
					pinned->setChecked(true);
				}
			}, checkbox->lifetime());
		}
		albumsWrap->show(anim::type::instant);
	}, albumsWrap->lifetime());

	const auto status = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			box,
			object_ptr<Ui::FlatLabel>(box, QString(), st::boxLabel),
			st::storyRowPadding),
		style::margins());
	status->hide(anim::type::instant);
	const auto setStatus = [=](std::optional<float64> progress) {
		if (!progress) {
			status->hide(anim::type::instant);
			return;
		}
		status->entity()->setText(tr::ayu_StoryUploading(
			tr::now,
			lt_percent,
			QString::number(int(std::round(*progress * 100)))));
		status->show(anim::type::instant);
	};

	const auto save = [=] {
		auto &choice = Saved()[SavedKey(state->author.current())];
		choice = SavedChoice{
			.caption = state->posted
				? TextWithTags()
				: caption->getTextWithTags(),
			.audience = Audience(audience->current()),
			.excepted = UserIds(state->excepted),
			.selected = UserIds(state->selected),
			.period = kPeriods[period->current()],
			.pinned = pinned->checked(),
			.protect = protect->checked(),
			.albums = state->albums,
		};
	};
	box->boxClosing() | rpl::on_next(save, box->lifetime());

	box->addButton(tr::ayu_StoryPost(), [=] {
		if (state->busy) {
			return;
		}
		const auto author = state->author.current();
		const auto chosen = author->isSelf()
			? Audience(audience->current())
			: Audience::Everyone;
		if (chosen == Audience::Selected && state->selected.empty()) {
			controller->showToast(tr::ayu_StoryNoUsers(tr::now));
			return;
		}
		state->busy = true;
		setStatus(0.);
		auto list = Ui::PreparedList();
		list.files.push_back(CloneFile(current()));
		Storage::ApplyModifications(list);
		auto users = std::vector<not_null<UserData*>>();
		if (chosen == Audience::Selected) {
			users = state->selected;
		} else if (chosen != Audience::CloseFriends && author->isSelf()) {
			users = state->excepted;
		}
		auto draft = AyuStories::Draft{
			.peer = author,
			.file = std::move(list.files.front()),
			.fileKey = state->fileKey,
			.caption = caption->getTextWithTags(),
			.audience = chosen,
			.users = std::move(users),
			.albums = state->albums,
			.period = premium
				? kPeriods[period->current()]
				: kDefaultPeriod,
			.pinned = pinned->checked(),
			.protect = protect->checked(),
		};
		state->poster.post(std::move(draft), {
			.progress = crl::guard(box, [=](float64 value) {
				setStatus(value);
			}),
			.done = crl::guard(box, [=] {
				state->posted = true;
				controller->showToast(tr::ayu_StoryPosted(tr::now));
				box->closeBox();
			}),
			.failed = crl::guard(box, [=](QString type) {
				// The window stays, the next try sends the same media.
				state->busy = false;
				setStatus(std::nullopt);
				controller->showToast(AyuStories::ErrorText(type));
			}),
		});
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace

void ShowStoryComposeBox(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		Ui::PreparedFile &&file) {
	controller->show(Box(
		FillStoryComposeBox,
		controller,
		peer,
		std::make_shared<Ui::PreparedFile>(std::move(file))));
}

} // namespace AyuUi
