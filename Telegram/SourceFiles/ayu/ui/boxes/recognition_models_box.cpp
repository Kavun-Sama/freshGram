// This is the source code of freshGram.
//
// The list of downloadable recognition models, a sibling of the
// "Manage dictionaries" box of the spellchecker.
#include "ayu/ui/boxes/recognition_models_box.h"

#include "ayu/ayu_settings.h"
#include "base/event_filter.h"
#include "lang/lang_keys.h"
#include "lang_auto.h"
#include "main/main_session.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/multi_select.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/effects/animations.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_menu_icons.h"

namespace AyuUi {
namespace {

using namespace AyuRecognition;
using namespace Storage::CloudBlob;

constexpr auto kMaxQueryLength = 15;

[[nodiscard]] QString ModelTitle(const Model &model) {
	return (model.kind == ModelKind::Whisper)
		? (u"Whisper "_q + model.name)
		: model.name;
}

[[nodiscard]] BlobState ComputeState(const Model &model, bool enabled) {
	if (!ModelExists(model)) {
		return Available{ model.size };
	}
	return enabled ? BlobState(Active()) : BlobState(Ready());
}

class Inner final : public Ui::RpWidget {
public:
	Inner(
		QWidget *parent,
		not_null<Main::Session*> session,
		ModelKind kind);

	[[nodiscard]] QString enabledValue() const;
	[[nodiscard]] Fn<void(const QString &)> queryCallback() const;

private:
	void setupContent(not_null<Main::Session*> session);
	void addRow(
		not_null<Ui::VerticalLayout*> content,
		not_null<Main::Session*> session,
		const Model &model,
		bool enabled,
		bool automatic = false);

	const ModelKind _kind;
	std::vector<int> _enabledIds;
	std::vector<Model> _models;
	int _autoId = 0;
	rpl::event_stream<QStringView> _query;
	rpl::event_stream<int> _selected;
	rpl::event_stream<> _autoOn;
	rpl::event_stream<> _languageOn;
	Fn<void(const QString &)> _queryCallback;

};

Inner::Inner(
	QWidget *parent,
	not_null<Main::Session*> session,
	ModelKind kind)
: RpWidget(parent)
, _kind(kind) {
	setupContent(session);
}

Fn<void(const QString &)> Inner::queryCallback() const {
	return _queryCallback;
}

QString Inner::enabledValue() const {
	if (_autoId && ranges::contains(_enabledIds, _autoId)) {
		return kAutoOcrLanguages.utf16();
	}
	auto result = QStringList();
	for (const auto &model : _models) {
		if (!ranges::contains(_enabledIds, model.id)
			|| !ModelExists(model)) {
			continue;
		}
		result.push_back((_kind == ModelKind::Whisper)
			? model.file
			: model.code);
		if (_kind == ModelKind::Whisper) {
			break;
		}
	}
	return result.join('+');
}

void Inner::addRow(
		not_null<Ui::VerticalLayout*> content,
		not_null<Main::Session*> session,
		const Model &model,
		bool enabled,
		bool automatic) {
	const auto id = model.id;
	const auto title = automatic
		? tr::ayu_OcrAuto(tr::now)
		: ModelTitle(model);
	const auto single = (_kind == ModelKind::Whisper);
	const auto languages = (_kind == ModelKind::Tesseract) && !automatic;
	enabled = enabled && ModelExists(model);
	if (enabled) {
		_enabledIds.push_back(id);
	}

	const auto wrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			content,
			object_ptr<Ui::SettingsButton>(
				content,
				rpl::single(title),
				st::dictionariesSectionButton)));
	const auto button = wrap->entity();

	const auto index = std::vector<QString>{ title, model.name, model.code };
	_query.events(
	) | rpl::on_next([=](QStringView string) {
		wrap->toggle(
			ranges::any_of(index, [&](const QString &s) {
				return s.startsWith(string, Qt::CaseInsensitive);
			}),
			anim::type::instant);
	}, button->lifetime());

	struct State {
		base::unique_qptr<ModelLoader> loader;
		rpl::variable<BlobState> state;
		rpl::event_stream<bool> forced;
		rpl::lifetime loaderLifetime;
	};
	const auto state = button->lifetime().make_state<State>();
	state->state = ComputeState(model, enabled);

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		button,
		state->state.value() | rpl::map([](const BlobState &value) {
			return StateDescription(value, tr::ayu_ModelEnabled);
		}),
		st::settingsUpdateState);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	label->show();
	label->moveToLeft(
		st::settingsUpdateStatePosition.x(),
		st::settingsUpdateStatePosition.y());

	state->state.value(
	) | rpl::on_next([=](const BlobState &value) {
		const auto toggled = v::is<Active>(value) ? 1. : 0.;
		const auto over = !button->isDisabled()
			&& (button->isDown() || button->isOver());
		if (toggled == 0. && !over) {
			label->setTextColorOverride(std::nullopt);
		} else {
			label->setTextColorOverride(anim::color(
				over ? st::contactsStatusFgOver : st::contactsStatusFg,
				st::contactsStatusFgOnline,
				toggled));
		}
	}, label->lifetime());

	button->toggleOn(
		rpl::single(enabled) | rpl::then(state->forced.events()));

	const auto setEnabled = [=](bool value) {
		if (value) {
			if (!ranges::contains(_enabledIds, id)) {
				_enabledIds.push_back(id);
			}
		} else {
			_enabledIds.erase(
				ranges::remove(_enabledIds, id),
				end(_enabledIds));
		}
	};
	const auto stopLoader = [=] {
		state->loaderLifetime.destroy();
		state->loader = nullptr;
	};

	button->toggledValue(
	) | rpl::on_next([=](bool toggled) {
		setEnabled(toggled);
		if (toggled) {
			if (ModelExists(model)) {
				state->state = ComputeState(model, true);
				if (single) {
					_selected.fire_copy(id);
				} else if (automatic) {
					_autoOn.fire({});
				} else if (languages) {
					_languageOn.fire({});
				}
				return;
			} else if (state->loader) {
				return;
			}
			state->loader = base::make_unique_q<ModelLoader>(
				QCoreApplication::instance(),
				session,
				model,
				crl::guard(button, [=] {
					stopLoader();
					if (ModelExists(model)) {
						const auto on = button->toggled();
						state->state = ComputeState(model, on);
						if (on && single) {
							_selected.fire_copy(id);
						} else if (on && automatic) {
							_autoOn.fire({});
						} else if (on && languages) {
							_languageOn.fire({});
						}
					} else {
						state->state = BlobState(Failed());
						state->forced.fire(false);
					}
				}));
			state->loader->state(
			) | rpl::on_next([=](const BlobState &value) {
				state->state = value;
			}, state->loaderLifetime);
		} else {
			stopLoader();
			if (!v::is<Failed>(state->state.current())) {
				state->state = ComputeState(model, false);
			}
		}
	}, button->lifetime());

	if (single) {
		_selected.events(
		) | rpl::on_next([=](int selectedId) {
			if (selectedId != id && button->toggled()) {
				state->forced.fire(false);
			}
		}, button->lifetime());
	}
	if (automatic) {
		_languageOn.events(
		) | rpl::on_next([=] {
			if (button->toggled()) {
				state->forced.fire(false);
			}
		}, button->lifetime());
	} else if (languages) {
		_autoOn.events(
		) | rpl::on_next([=] {
			if (button->toggled()) {
				state->forced.fire(false);
			}
		}, button->lifetime());
	}

	const auto contextMenu = button->lifetime()
		.make_state<base::unique_qptr<Ui::PopupMenu>>();
	const auto showMenu = [=] {
		if (!ModelExists(model)) {
			return false;
		}
		*contextMenu = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::popupMenuWithIcons);
		contextMenu->get()->addAction(
			tr::ayu_ModelRemove(tr::now),
			[=] {
				RemoveModel(model);
				setEnabled(false);
				state->state = ComputeState(model, false);
				state->forced.fire(false);
			},
			&st::menuIconDelete);
		contextMenu->get()->popup(QCursor::pos());
		return true;
	};
	base::install_event_filter(button, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::ContextMenu && showMenu()) {
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});
}

void Inner::setupContent(not_null<Main::Session*> session) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto selected = [&] {
		const auto &settings = AyuSettings::getInstance();
		return (_kind == ModelKind::Whisper)
			? QStringList{ settings.whisperModel() }
			: settings.ocrLanguages().split('+', Qt::SkipEmptyParts);
	}();

	const auto loading = content->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				tr::ayu_ModelsLoading(),
				st::defaultFlatLabel)),
		st::boxRowPadding);
	const auto built = content->lifetime().make_state<bool>(false);
	const auto buildRows = [=] {
		if (*built) {
			return;
		}
		auto models = Models(_kind);
		if (models.empty()) {
			return;
		}
		*built = true;
		loading->hide(anim::type::instant);
		_models = models;
		if (_kind == ModelKind::Tesseract) {
			const auto aux = Models(ModelKind::TesseractAux);
			if (!aux.empty()) {
				_autoId = aux.front().id;
				addRow(
					content,
					session,
					aux.front(),
					selected.contains(kAutoOcrLanguages.utf16()),
					true);
			}
		}
		const auto autoChosen = selected.contains(kAutoOcrLanguages.utf16());
		for (const auto &model : models) {
			const auto key = (_kind == ModelKind::Whisper)
				? model.file
				: model.code;
			addRow(
				content,
				session,
				model,
				!autoChosen && selected.contains(key));
		}
		content->resizeToWidth(st::boxWidth);
	};
	buildRows();
	ModelsChanged(
	) | rpl::on_next(buildRows, content->lifetime());
	RefreshManifest(session);

	_queryCallback = [=](const QString &query) {
		if (query.size() >= kMaxQueryLength) {
			return;
		}
		_query.fire_copy(query);
	};

	content->resizeToWidth(st::boxWidth);
	Ui::ResizeFitChild(this, content);
}

} // namespace

RecognitionModelsBox::RecognitionModelsBox(
	QWidget*,
	not_null<Main::Session*> session,
	ModelKind kind)
: _session(session)
, _kind(kind) {
}

void RecognitionModelsBox::setInnerFocus() {
	if (_setInnerFocus) {
		_setInnerFocus();
	}
}

void RecognitionModelsBox::prepare() {
	const auto multiSelect = Ui::CreateChild<Ui::MultiSelect>(
		this,
		st::defaultMultiSelect,
		tr::lng_participant_filter());
	multiSelect->resizeToWidth(st::boxWidth);
	multiSelect->moveToLeft(0, 0);

	const auto inner = setInnerWidget(
		object_ptr<Inner>(this, _session, _kind),
		st::boxScroll,
		multiSelect->height());
	multiSelect->setQueryChangedCallback(inner->queryCallback());
	_setInnerFocus = [=] {
		multiSelect->setInnerFocus();
	};

	setTitle((_kind == ModelKind::Whisper)
		? tr::ayu_ManageWhisperModels()
		: tr::ayu_ManageOcrLanguages());

	const auto kind = _kind;
	addButton(tr::lng_settings_save(), [=] {
		auto &settings = AyuSettings::getInstance();
		if (kind == ModelKind::Whisper) {
			settings.setWhisperModel(inner->enabledValue());
		} else {
			settings.setOcrLanguages(inner->enabledValue());
		}
		closeBox();
	});
	addButton(tr::lng_close(), [=] { closeBox(); });
}

} // namespace AyuUi
