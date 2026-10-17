// This is the source code of freshGram.
#include "ayu/ui/boxes/story_compose_box.h"

#include "ayu/features/stories/story_poster.h"
#include "data/data_peer.h"
#include "editor/photo_editor_layer_widget.h"
#include "lang/lang_keys.h"
#include "lang_auto.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/padding_wrap.h"
#include "window/window_session_controller.h"
#include "styles/style_ayu_styles.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtGui/QPainter>

#include <array>

namespace AyuUi {
namespace {

constexpr auto kPeriods = std::array<TimeId, 4>{
	6 * 3600,
	12 * 3600,
	24 * 3600,
	48 * 3600,
};

[[nodiscard]] QString ErrorText(const QString &type) {
	return (type == u"STORIES_TOO_MUCH"_q)
		? tr::ayu_StoryLimit(tr::now)
		: type;
}

void FillStoryComposeBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		std::shared_ptr<Ui::PreparedFile> file) {
	box->setTitle(tr::ayu_StoryNew());

	struct State {
		explicit State(not_null<Main::Session*> session)
		: poster(session) {
		}

		AyuStories::Poster poster;
		bool busy = false;
	};
	const auto session = &peer->session();
	const auto state = box->lifetime().make_state<State>(session);

	const auto preview = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(box, st::storyPreviewHeight),
		st::storyRowPadding);
	preview->paintRequest(
	) | rpl::on_next([=] {
		const auto &image = file->preview;
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

	if (file->type == Ui::PreparedFile::Type::Photo) {
		const auto edit = box->addRow(
			object_ptr<Ui::LinkButton>(box, tr::ayu_StoryEdit(tr::now)),
			st::storyRowPadding);
		edit->setClickedCallback([=] {
			Editor::OpenWithPreparedFile(
				box,
				controller->uiShow(),
				file.get(),
				st::sendMediaPreviewSize,
				[=](bool ok) {
					if (ok) {
						preview->update();
					}
				},
				0,
				QSize(720, 1280),
				false);
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

	const auto audience = std::make_shared<Ui::RadiobuttonGroup>(
		int(AyuStories::Audience::Contacts));
	if (peer->isSelf()) {
		Ui::AddSubsectionTitle(box->verticalLayout(), tr::ayu_StoryAudience());
		const auto add = [&](AyuStories::Audience value, const QString &text) {
			box->addRow(
				object_ptr<Ui::Radiobutton>(
					box,
					audience,
					int(value),
					text,
					st::defaultCheckbox),
				st::storyRowPadding);
		};
		add(AyuStories::Audience::Contacts, tr::ayu_StoryContacts(tr::now));
		add(
			AyuStories::Audience::CloseFriends,
			tr::ayu_StoryCloseFriends(tr::now));
		add(AyuStories::Audience::Everyone, tr::ayu_StoryEveryone(tr::now));
	}

	Ui::AddSubsectionTitle(box->verticalLayout(), tr::ayu_StoryDuration());
	const auto period = std::make_shared<Ui::RadiobuttonGroup>(2);
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
	const auto pinned = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			tr::ayu_StoryKeep(tr::now),
			false,
			st::defaultCheckbox),
		st::storyRowPadding);
	const auto protect = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			tr::ayu_StoryProtect(tr::now),
			false,
			st::defaultCheckbox),
		st::storyRowPadding);

	box->addButton(tr::ayu_StoryPost(), [=] {
		if (state->busy) {
			return;
		}
		state->busy = true;
		controller->showToast(tr::ayu_StoryPosting(tr::now));
		auto draft = AyuStories::Draft{
			.peer = peer,
			.file = std::move(*file),
			.caption = caption->getTextWithTags(),
			.audience = peer->isSelf()
				? AyuStories::Audience(audience->current())
				: AyuStories::Audience::Everyone,
			.period = kPeriods[period->current()],
			.pinned = pinned->checked(),
			.protect = protect->checked(),
		};
		state->poster.post(std::move(draft), {
			.done = crl::guard(box, [=] {
				controller->showToast(tr::ayu_StoryPosted(tr::now));
				box->closeBox();
			}),
			.failed = crl::guard(box, [=](QString text) {
				controller->showToast(ErrorText(text));
				box->closeBox();
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
