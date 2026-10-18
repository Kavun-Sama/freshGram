// This is the source code of freshGram.
#include "ayu/ui/settings/passcode_security.h"

#include "ayu/ayu_settings.h"
#include "ayu/utils/passcode_strength.h"
#include "base/call_delayed.h"
#include "base/platform/base_platform_info.h"
#include "core/application.h"
#include "lang/lang_keys.h"
#include "lang_auto.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings/cloud_password/settings_cloud_password_common.h"
#include "settings/settings_builder.h"
#include "storage/details/storage_passcode_kdf.h"
#include "storage/storage_domain.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace AyuUi {
namespace {

using namespace Settings::Builder;
using Storage::PasscodeKdfLevel;

constexpr auto kStrengthSegments = 4;

struct Choice {
	int value = 0;
	QString text;
};

[[nodiscard]] QString OffText() {
	return tr::lng_settings_auto_night_mode_off(tr::now);
}

[[nodiscard]] QString KdfLevelName(PasscodeKdfLevel level) {
	switch (level) {
	case PasscodeKdfLevel::Standard: return tr::ayu_PasscodeKdfStandard(tr::now);
	case PasscodeKdfLevel::Strong: return tr::ayu_PasscodeKdfStrong(tr::now);
	case PasscodeKdfLevel::Maximum: return tr::ayu_PasscodeKdfMaximum(tr::now);
	}
	return QString();
}

[[nodiscard]] QString KdfLevelAbout(PasscodeKdfLevel level) {
	switch (level) {
	case PasscodeKdfLevel::Standard:
		return tr::ayu_PasscodeKdfStandardAbout(tr::now);
	case PasscodeKdfLevel::Strong:
		return tr::ayu_PasscodeKdfStrongAbout(tr::now);
	case PasscodeKdfLevel::Maximum:
		return tr::ayu_PasscodeKdfMaximumAbout(tr::now);
	}
	return QString();
}

void PasscodeKdfBox(
		not_null<Ui::GenericBox*> box,
		Fn<void()> changed) {
	box->setTitle(tr::ayu_PasscodeKdf());
	const auto content = box->verticalLayout();
	auto &local = Core::App().domain().local();
	const auto current = Storage::PasscodeKdfLevelOf(local.passcodeKdf());
	const auto supported = Storage::PasscodeKdfArgon2Supported();
	const auto group = std::make_shared<Ui::RadiobuttonGroup>(int(current));

	Ui::AddSkip(content);
	for (const auto level : {
			PasscodeKdfLevel::Standard,
			PasscodeKdfLevel::Strong,
			PasscodeKdfLevel::Maximum }) {
		const auto radio = content->add(
			object_ptr<Ui::Radiobutton>(
				content,
				group,
				int(level),
				KdfLevelName(level),
				st::defaultBoxCheckbox),
			st::boxRowPadding + style::margins(0, 0, 0, st::boxOptionListSkip));
		const auto enabled = supported
			|| (level == PasscodeKdfLevel::Standard);
		radio->setDisabled(!enabled);
		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				KdfLevelAbout(level),
				st::boxDividerLabel),
			st::boxRowPadding
				+ style::margins(
					st::defaultBoxCheckbox.textPosition.x(),
					0,
					0,
					st::boxOptionListSkip));
	}
	if (!supported) {
		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				tr::ayu_PasscodeKdfUnsupported(),
				st::boxDividerLabel),
			st::boxRowPadding);
	}
	Ui::AddSkip(content);
	Ui::AddDividerText(content, tr::ayu_PasscodeKdfAbout());
	Ui::AddSkip(content);

	const auto field = Settings::CloudPassword::AddPasswordField(
		content,
		tr::lng_passcode_enter_old(),
		QString());
	const auto error = Settings::CloudPassword::AddError(content, field);

	const auto busy = std::make_shared<bool>(false);
	const auto fail = [=](const QString &text) {
		error->setText(text);
		error->show();
		field->selectAll();
		field->setFocus();
		field->showError();
	};
	const auto save = [=] {
		const auto level = PasscodeKdfLevel(group->current());
		if (*busy) {
			return;
		} else if (level == current) {
			box->closeBox();
			return;
		} else if (!passcodeCanTry()) {
			fail(tr::lng_flood_error(tr::now));
			return;
		}
		const auto passcode = field->text().toUtf8();
		auto &domain = Core::App().domain().local();
		if (passcode.isEmpty()) {
			field->showError();
			return;
		} else if (!domain.checkPasscode(passcode)) {
			cSetPasscodeBadTries(cPasscodeBadTries() + 1);
			cSetPasscodeLastTry(crl::now());
			fail(tr::lng_passcode_wrong(tr::now));
			return;
		}
		cSetPasscodeBadTries(0);
		*busy = true;
		error->setText(tr::ayu_PasscodeKdfApplying(tr::now));
		error->show();
		// WHY: the key derivation blocks, let the status paint first.
		base::call_delayed(crl::time(50), box, [=] {
			Core::App().domain().local().setPasscode(
				passcode,
				Storage::PasscodeKdfFor(level));
			Core::App().localPasscodeChanged();
			box->showToast(tr::ayu_PasscodeKdfChanged(tr::now));
			if (changed) {
				changed();
			}
			box->closeBox();
		});
	};
	QObject::connect(field, &Ui::MaskedInputField::submitted, save);
	box->setFocusCallback([=] { field->setFocusFast(); });
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace

void AddPasscodeStrength(
		not_null<Ui::VerticalLayout*> content,
		not_null<Ui::PasswordInput*> field) {
	const auto &font = st::boxDividerLabel.style.font;
	const auto barHeight = st::lineWidth * 4;
	const auto skip = st::lineWidth * 6;
	const auto widget = content->add(object_ptr<Ui::FixedHeightWidget>(
		content,
		skip + barHeight + skip + font->height));
	const auto text = std::make_shared<QString>();
	const auto level = std::make_shared<AyuPasscode::Strength>();
	const auto refresh = [=] {
		const auto value = field->text();
		*text = AyuPasscode::StrengthText(value);
		*level = AyuPasscode::StrengthForBits(
			AyuPasscode::EstimateBits(value));
		widget->update();
	};
	QObject::connect(field, &Ui::MaskedInputField::changed, refresh);
	widget->paintRequest(
	) | rpl::on_next([=] {
		if (text->isEmpty()) {
			return;
		}
		auto p = QPainter(widget);
		auto hq = PainterHighQualityEnabler(p);
		const auto width = field->width();
		const auto left = (widget->width() - width) / 2;
		const auto gap = st::lineWidth * 4;
		const auto segment = (width - gap * (kStrengthSegments - 1))
			/ kStrengthSegments;
		const auto filled = int(*level);
		const auto &active = (*level == AyuPasscode::Strength::Weak)
			? st::boxTextFgError
			: st::activeButtonBg;
		p.setPen(Qt::NoPen);
		for (auto i = 0; i != kStrengthSegments; ++i) {
			p.setBrush((i < filled) ? active : st::windowBgOver);
			p.drawRoundedRect(
				QRect(left + i * (segment + gap), skip, segment, barHeight),
				barHeight / 2.,
				barHeight / 2.);
		}
		p.setFont(font);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(left, skip + barHeight + skip, width, font->height),
			*text,
			style::al_left);
	}, widget->lifetime());
}

void AddPasscodeSecuritySettings(SectionBuilder &builder) {
	const auto controller = builder.controller();
	if (!controller) {
		return;
	}
	auto &settings = AyuSettings::getInstance();

	const auto addChoice = [&](
			QString id,
			tr::phrase<> title,
			const style::icon *icon,
			std::vector<Choice> choices,
			rpl::producer<int> value,
			Fn<int()> get,
			Fn<void(int)> set,
			rpl::producer<bool> shown = nullptr) {
		auto label = std::move(value) | rpl::map([=](int current) {
			for (const auto &choice : choices) {
				if (choice.value == current) {
					return choice.text;
				}
			}
			return QString();
		});
		builder.addButton({
			.id = std::move(id),
			.title = title(),
			.icon = { icon },
			.label = std::move(label),
			.onClick = [=] {
				auto texts = std::vector<QString>();
				auto selected = 0;
				for (const auto &choice : choices) {
					if (choice.value == get()) {
						selected = int(texts.size());
					}
					texts.push_back(choice.text);
				}
				controller->show(Box(SingleChoiceBox, SingleChoiceBoxArgs{
					.title = title(),
					.options = texts,
					.initialSelection = selected,
					.callback = [=](int index) {
						set(choices[index].value);
					},
				}));
			},
			.shown = shown ? std::move(shown) : rpl::single(true),
		});
	};
	const auto addToggle = [&](
			QString id,
			tr::phrase<> title,
			const style::icon *icon,
			bool value,
			Fn<void(bool)> set) {
		const auto button = builder.addButton({
			.id = std::move(id),
			.title = title(),
			.icon = { icon },
			.toggled = rpl::single(value),
		});
		if (button) {
			button->toggledChanges(
			) | rpl::on_next(set, button->lifetime());
		}
	};

	const auto kdfChanged = std::make_shared<rpl::event_stream<>>();
	builder.addSkip();
	builder.addSubsectionTitle(tr::ayu_PasscodeEncryption());
	builder.addButton({
		.id = u"passcode/kdf"_q,
		.title = tr::ayu_PasscodeKdf(),
		.icon = { &st::menuIconPermissions },
		.label = kdfChanged->events_starting_with({}) | rpl::map([] {
			return KdfLevelName(Storage::PasscodeKdfLevelOf(
				Core::App().domain().local().passcodeKdf()));
		}),
		.onClick = [=] {
			controller->show(Box(PasscodeKdfBox, [=] {
				kdfChanged->fire({});
			}));
		},
		.keywords = { u"argon2"_q, u"kdf"_q, u"encryption"_q, u"entropy"_q },
	});
	builder.addSkip();
	builder.addDividerText(tr::ayu_PasscodeKdfAbout());

	builder.addSkip();
	builder.addSubsectionTitle(tr::ayu_PasscodeRequirements());
	auto lengths = std::vector<Choice>{ { 0, OffText() } };
	for (const auto length : { 4, 6, 8, 10, 12 }) {
		lengths.push_back({ length, QString::number(length) });
	}
	addChoice(
		u"passcode/min-length"_q,
		tr::ayu_PasscodeMinLength,
		&st::menuIconEdit,
		std::move(lengths),
		settings.passcodeMinLengthValue(),
		[] { return AyuSettings::getInstance().passcodeMinLength(); },
		[](int value) {
			AyuSettings::getInstance().setPasscodeMinLength(value);
		});
	addToggle(
		u"passcode/mixed"_q,
		tr::ayu_PasscodeRequireMixed,
		&st::menuIconFont,
		settings.passcodeRequireMixed(),
		[](bool value) {
			AyuSettings::getInstance().setPasscodeRequireMixed(value);
		});
	builder.addSkip();
	builder.addDividerText(tr::ayu_PasscodeRequirementsAbout());

	builder.addSkip();
	builder.addSubsectionTitle(tr::ayu_PasscodeWrongInput());
	auto attempts = std::vector<Choice>{ { 0, OffText() } };
	for (const auto count : { 5, 10, 15, 20 }) {
		attempts.push_back({ count, QString::number(count) });
	}
	addChoice(
		u"passcode/wipe"_q,
		tr::ayu_PasscodeWipeAfter,
		&st::menuIconDelete,
		std::move(attempts),
		settings.passcodeWipeAttemptsValue(),
		[] { return AyuSettings::getInstance().passcodeWipeAttempts(); },
		[](int value) {
			AyuSettings::getInstance().setPasscodeWipeAttempts(value);
		});
	addChoice(
		u"passcode/wipe-action"_q,
		tr::ayu_PasscodeWipeAction,
		&st::menuIconLeave,
		{
			{ 0, tr::ayu_PasscodeWipeLogout(tr::now) },
			{ 1, tr::ayu_PasscodeWipeDeleteAll(tr::now) },
		},
		settings.passcodeWipeActionValue(),
		[] { return AyuSettings::getInstance().passcodeWipeAction(); },
		[](int value) {
			AyuSettings::getInstance().setPasscodeWipeAction(value);
		},
		settings.passcodeWipeAttemptsValue() | rpl::map(rpl::mappers::_1 > 0));
	builder.addSkip();
	builder.addDividerText(tr::ayu_PasscodeWipeAbout());

	builder.addSkip();
	builder.addSubsectionTitle(tr::ayu_PasscodeLockScreen());
	addToggle(
		u"passcode/clock"_q,
		tr::ayu_PasscodeShowClock,
		&st::menuIconSchedule,
		settings.lockScreenShowClock(),
		[](bool value) {
			AyuSettings::getInstance().setLockScreenShowClock(value);
		});
	addToggle(
		u"passcode/system-lock"_q,
		tr::ayu_PasscodeLockOnSystemLock,
		&st::menuIconLock,
		settings.lockOnSystemLock(),
		[](bool value) {
			AyuSettings::getInstance().setLockOnSystemLock(value);
		});
	addChoice(
		u"passcode/minimize"_q,
		tr::ayu_PasscodeLockOnMinimize,
		&st::menuIconCollapse,
		{
			{ -1, OffText() },
			{ 0, tr::ayu_PasscodeImmediately(tr::now) },
			{ 10, tr::lng_seconds(tr::now, lt_count, 10) },
			{ 60, tr::lng_minutes(tr::now, lt_count, 1) },
			{ 300, tr::lng_minutes(tr::now, lt_count, 5) },
		},
		settings.lockOnMinimizeValue(),
		[] { return AyuSettings::getInstance().lockOnMinimize(); },
		[](int value) {
			AyuSettings::getInstance().setLockOnMinimize(value);
		});
	if (!Platform::IsLinux()) {
		addChoice(
			u"passcode/capture"_q,
			tr::ayu_PasscodeHideFromCapture,
			&st::menuIconStealth,
			{
				{ 0, OffText() },
				{ 1, tr::ayu_PasscodeCaptureLocked(tr::now) },
				{ 2, tr::ayu_PasscodeCaptureAlways(tr::now) },
			},
			settings.hideFromCaptureValue(),
			[] { return AyuSettings::getInstance().hideFromCapture(); },
			[](int value) {
				AyuSettings::getInstance().setHideFromCapture(value);
			});
	}
	addChoice(
		u"passcode/notifications"_q,
		tr::ayu_PasscodeLockedNotifications,
		&st::menuIconNotifications,
		{
			{ 0, tr::ayu_PasscodeNotifyHidden(tr::now) },
			{ 1, tr::ayu_PasscodeNotifySilent(tr::now) },
			{ 2, tr::ayu_PasscodeNotifyNone(tr::now) },
		},
		settings.lockedNotificationsValue(),
		[] { return AyuSettings::getInstance().lockedNotifications(); },
		[](int value) {
			AyuSettings::getInstance().setLockedNotifications(value);
		});
	builder.addSkip();
}

} // namespace AyuUi
