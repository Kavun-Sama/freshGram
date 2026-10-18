/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_lock_widgets.h"

#include "ayu/ayu_settings.h"
#include "ayu/features/fake_passcode/fake_passcode.h"
#include "ayu/features/passcode_guard/passcode_guard.h"
#include "ayu/utils/caps_lock.h"
#include "ayu/ayu_ui_settings.h"
#include "lang_auto.h"
#include "storage/details/storage_passcode_kdf.h"
#include "ui/effects/shake_animation.h"
#include "ui/painter.h"
#include "base/platform/base_platform_info.h"
#include "base/call_delayed.h"
#include "base/system_unlock.h"
#include "lang/lang_keys.h"
#include "storage/storage_domain.h"
#include "mainwindow.h"
#include "core/application.h"
#include "api/api_text_entities.h"
#include "ui/text/text.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/toast/toast.h"
#include "ui/ui_utility.h"
#include "window/window_controller.h"
#include "window/window_slide_animation.h"
#include "window/window_session_controller.h"
#include "main/main_domain.h"
#include "styles/style_layers.h"
#include "styles/style_passcode_box.h"
#include "styles/style_window_lock_widgets.h"

namespace Window {
namespace {

constexpr auto kSystemUnlockDelay = crl::time(1000);
constexpr auto kAttemptsWarning = 3;

} // namespace

namespace {

struct PrecomputedKeys {
	Storage::PrecomputedPasscodeKey real;
	std::optional<AyuFakePasscode::Match> fake;
	Storage::PrecomputedPasscodeKey fakeReal;
};

[[nodiscard]] Storage::PasscodeCheck CheckOf(Storage::StartResult result) {
	return (result == Storage::StartResult::Success)
		? Storage::PasscodeCheck::Correct
		: (result == Storage::StartResult::KeyUnavailable)
		? Storage::PasscodeCheck::Failed
		: Storage::PasscodeCheck::Wrong;
}

[[nodiscard]] PasscodeAttempt TryPasscodeWith(
		const QString &passcode,
		const PrecomputedKeys *keys) {
	AyuPasscodeGuard::EnsureLoaded();
	if (passcode.isEmpty()) {
		return PasscodeAttempt::Empty;
	} else if (!passcodeCanTry()) {
		return PasscodeAttempt::Flood;
	}
	const auto utf8 = passcode.toUtf8();
	auto &domain = Core::App().domain();
	auto check = Storage::PasscodeCheck::Wrong;
	if (domain.started()) {
		const auto &local = domain.local();
		const auto input = local.passcodeKdfInput();
		check = (keys && input && keys->real.matches(input->salt, input->kdf))
			? local.checkPasscodeKey(keys->real)
			: local.checkPasscodeResult(utf8);
	} else {
		check = CheckOf(keys
			? domain.start(utf8, keys->real)
			: domain.start(utf8));
	}
	if (check == Storage::PasscodeCheck::Failed) {
		// WHY: a key that could not be derived says nothing about the passcode.
		return PasscodeAttempt::Unavailable;
	}
	auto correct = (check == Storage::PasscodeCheck::Correct);
	if (!correct) {
		const auto fake = keys ? keys->fake : AyuFakePasscode::TryMatch(utf8);
		if (fake) {
			if (!fake->unlocks()) {
				AyuFakePasscode::RunSilent(fake->actions);
			} else {
				correct = domain.started()
					|| (CheckOf(keys
						? domain.start(fake->realPasscode, keys->fakeReal)
						: domain.start(fake->realPasscode))
						== Storage::PasscodeCheck::Correct);
				if (correct) {
					AyuFakePasscode::SetPending(fake->id);
				}
			}
		}
	}
	if (!correct) {
		return AyuPasscodeGuard::RegisterFailure()
			? PasscodeAttempt::Wiped
			: PasscodeAttempt::Wrong;
	}
	return PasscodeAttempt::Correct;
}

} // namespace

PasscodeAttempt TryPasscode(const QString &passcode) {
	return TryPasscodeWith(passcode, nullptr);
}

void TryPasscodeAsync(
		const QString &passcode,
		Fn<void(PasscodeAttempt)> done) {
	AyuPasscodeGuard::EnsureLoaded();
	if (passcode.isEmpty() || !passcodeCanTry()) {
		done(TryPasscode(passcode));
		return;
	}
	const auto utf8 = passcode.toUtf8();
	const auto real = Core::App().domain().local().passcodeKdfInput();
	const auto fakeInput = AyuFakePasscode::MatchKdfInput();
	crl::async([=] {
		const auto derive = [&](const QByteArray &value) {
			return Storage::PrecomputedPasscodeKey{
				.salt = real->salt,
				.kdf = real->kdf,
				.key = Storage::DerivePasscodeKey(
					value,
					real->salt,
					real->kdf),
			};
		};
		auto keys = std::make_shared<PrecomputedKeys>();
		if (real) {
			keys->real = derive(utf8);
		}
		keys->fake = AyuFakePasscode::MatchWithKey(
			fakeInput,
			Storage::DerivePasscodeKey(utf8, fakeInput.salt, fakeInput.kdf));
		// WHY: the same work is done whatever passcode was entered,
		// so the time of the answer does not tell a fake one.
		const auto unlocks = keys->fake && keys->fake->unlocks();
		if (real) {
			keys->fakeReal = derive(unlocks ? keys->fake->realPasscode : utf8);
		}
		crl::on_main([=] {
			done(TryPasscodeWith(passcode, keys.get()));
		});
	});
}

LockWidget::LockWidget(QWidget *parent, not_null<Controller*> window)
: RpWidget(parent)
, _window(window) {
	show();
}

LockWidget::~LockWidget() = default;

not_null<Controller*> LockWidget::window() const {
	return _window;
}

void LockWidget::setInnerFocus() {
	setFocus();
}

void LockWidget::showAnimated(QPixmap oldContentCache) {
	_showAnimation = nullptr;

	showChildren();
	setInnerFocus();
	auto newContentCache = Ui::GrabWidget(this);
	hideChildren();

	_showAnimation = std::make_unique<Window::SlideAnimation>();
	_showAnimation->setRepaintCallback([=] { update(); });
	_showAnimation->setFinishedCallback([=] { showFinished(); });
	_showAnimation->setPixmaps(oldContentCache, newContentCache);
	_showAnimation->start();

	show();
}

void LockWidget::showFinished() {
	showChildren();
	_window->widget()->setInnerFocus();
	_showAnimation = nullptr;
	if (const auto controller = _window->sessionController()) {
		controller->clearSectionStack();
	}
}

void LockWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	if (_showAnimation) {
		_showAnimation->paintContents(p);
		return;
	}
	paintContent(p);
}

void LockWidget::paintContent(QPainter &p) {
	p.fillRect(rect(), st::windowBg);
}

PasscodeLockWidget::PasscodeLockWidget(
	QWidget *parent,
	not_null<Controller*> window)
: LockWidget(parent, window)
, _passcode(this, st::passcodeInputWithButtons, tr::lng_passcode_ph())
, _submit(this, tr::lng_passcode_submit(), st::passcodeSubmit)
, _logout(this, tr::lng_passcode_logout(tr::now))
, _clockTimer([=] { updateClock(); })
, _statusTimer([=] { updateStatus(); }) {
	AyuPasscodeGuard::EnsureLoaded();
	setupEye();
	updateClock();
	updateStatus();
	_passcode->installEventFilter(this);
	AyuSettings::getInstance().lockScreenShowClockValue(
	) | rpl::skip(1) | rpl::on_next([=] {
		updateLayout();
		update();
	}, lifetime());
	connect(_passcode, &Ui::MaskedInputField::changed, [=] { changed(); });
	connect(_passcode, &Ui::MaskedInputField::submitted, [=] { submit(); });
	_submit->setClickedCallback([=] { submit(); });
	_logout->setClickedCallback([=] {
		window->showLogoutConfirmation();
	});

	using namespace rpl::mappers;
	if (Core::App().settings().systemUnlockEnabled()) {
		_systemUnlockAvailable = base::SystemUnlockStatus(
			true
		) | rpl::map([](base::SystemUnlockAvailability status) {
			return status.withBiometrics
				? SystemUnlockType::Biometrics
				: status.withCompanion
				? SystemUnlockType::Companion
				: status.available
				? SystemUnlockType::Default
				: SystemUnlockType::None;
		});
		if (Core::App().domain().started()) {
			_systemUnlockAllowed = _systemUnlockAvailable.value();
			setupSystemUnlock();
		} else {
			setupSystemUnlockInfo();
		}
	}
}

void PasscodeLockWidget::setupSystemUnlockInfo() {
	const auto macos = [&] {
		return _systemUnlockAvailable.value(
		) | rpl::map([](SystemUnlockType type) {
			return (type == SystemUnlockType::Biometrics)
				? tr::lng_passcode_touchid()
				: (type == SystemUnlockType::Companion)
				? tr::lng_passcode_applewatch()
				: tr::lng_passcode_systempwd();
		}) | rpl::flatten_latest();
	};
	auto text = Platform::IsWindows()
		? tr::lng_passcode_winhello()
		: macos();
	const auto info = Ui::CreateChild<Ui::FlatLabel>(
		this,
		std::move(text),
		st::passcodeSystemUnlockLater);
	_logout->geometryValue(
	) | rpl::on_next([=](QRect logout) {
		info->resizeToWidth(width()
			- st::boxRowPadding.left()
			- st::boxRowPadding.right());
		info->moveToLeft(
			st::boxRowPadding.left(),
			logout.y() + logout.height() + st::passcodeSystemUnlockSkip);
	}, info->lifetime());
	info->showOn(_systemUnlockAvailable.value(
	) | rpl::map(rpl::mappers::_1 != SystemUnlockType::None));
}

void PasscodeLockWidget::setupSystemUnlock() {
	windowActiveValue() | rpl::skip(1) | rpl::filter([=](bool active) {
		return active
			&& !_systemUnlockSuggested
			&& !_systemUnlockCooldown.isActive();
	}) | rpl::on_next([=](bool) {
		[[maybe_unused]] auto refresh = base::SystemUnlockStatus();
		suggestSystemUnlock();
	}, lifetime());

	const auto button = Ui::CreateChild<Ui::IconButton>(
		_passcode.data(),
		st::passcodeSystemUnlock);
	if (!Platform::IsWindows()) {
		using namespace base;
		_systemUnlockAllowed.value(
		) | rpl::on_next([=](SystemUnlockType type) {
			const auto icon = (type == SystemUnlockType::Biometrics)
				? &st::passcodeSystemTouchID
				: (type == SystemUnlockType::Companion)
				? &st::passcodeSystemAppleWatch
				: &st::passcodeSystemSystemPwd;
			button->setIconOverride(icon, icon);
		}, button->lifetime());
	}
	button->showOn(_systemUnlockAllowed.value(
	) | rpl::map(rpl::mappers::_1 != SystemUnlockType::None));
	_passcode->sizeValue() | rpl::on_next([=](QSize size) {
		button->moveToRight(
			st::passcodeEye.width,
			size.height() - button->height());
	}, button->lifetime());
	button->setClickedCallback([=] {
		const auto delay = st::passcodeSystemUnlock.ripple.hideDuration;
		base::call_delayed(delay, this, [=] {
			suggestSystemUnlock();
		});
	});
}

void PasscodeLockWidget::suggestSystemUnlock() {
	InvokeQueued(this, [=] {
		if (_systemUnlockSuggested) {
			return;
		}
		_systemUnlockCooldown.cancel();

		using namespace base;
		_systemUnlockAllowed.value(
		) | rpl::filter(
			rpl::mappers::_1 != SystemUnlockType::None
		) | rpl::take(1) | rpl::on_next([=] {
			const auto weak = base::make_weak(this);
			const auto done = [weak](SystemUnlockResult result) {
				crl::on_main([=] {
					if (const auto strong = weak.get()) {
						strong->systemUnlockDone(result);
					}
				});
			};
			SuggestSystemUnlock(
				this,
				(::Platform::IsWindows()
					? tr::lng_passcode_winhello_unlock(tr::now)
					: tr::lng_passcode_touchid_unlock(tr::now)),
				done);
		}, _systemUnlockSuggested);
	});
}

void PasscodeLockWidget::systemUnlockDone(base::SystemUnlockResult result) {
	if (result == base::SystemUnlockResult::Success) {
		Core::App().unlockPasscode();
		return;
	}
	_systemUnlockCooldown.callOnce(kSystemUnlockDelay);
	_systemUnlockSuggested.destroy();
	if (result == base::SystemUnlockResult::FloodError) {
		_error = tr::lng_flood_error(tr::now);
		_passcode->setFocusFast();
		updateStatus();
	}
}

void PasscodeLockWidget::setupEye() {
	_eye = Ui::CreateChild<Ui::IconButton>(_passcode.data(), st::passcodeEye);
	_passcode->sizeValue() | rpl::on_next([=](QSize size) {
		_eye->moveToRight(0, size.height() - _eye->height());
	}, _eye->lifetime());
	_eye->setClickedCallback([=] {
		const auto shown = (_passcode->echoMode() == QLineEdit::Normal);
		_passcode->setEchoMode(shown
			? QLineEdit::Password
			: QLineEdit::Normal);
		const auto icon = shown ? nullptr : &st::passcodeEyeHidden;
		_eye->setIconOverride(icon, icon);
		_passcode->setFocusFast();
	});
}

void PasscodeLockWidget::updateClock() {
	const auto now = QDateTime::currentDateTime();
	_clock = QLocale().toString(now.time(), QLocale::ShortFormat);
	_date = langDayOfWeekFull(now.date())
		+ u", "_q
		+ langDayOfMonthFull(now.date());
	const auto seconds = now.time().second();
	_clockTimer.callOnce((60 - seconds) * crl::time(1000));
	update();
}

void PasscodeLockWidget::updateStatus() {
	const auto wait = AyuPasscodeGuard::WaitLeft();
	const auto left = AyuPasscodeGuard::AttemptsBeforeWipe();
	_statusTimer.cancel();
	if (_busy) {
		_status = tr::ayu_PasscodeChecking(tr::now);
		_statusError = false;
	} else if (wait > 0) {
		_status = tr::ayu_PasscodeRetryIn(
			tr::now,
			lt_seconds,
			QString::number((wait + 999) / 1000));
		_statusError = true;
		_statusTimer.callOnce(((wait - 1) % 1000) + 1);
	} else if (!_error.isEmpty()) {
		_status = _error;
		_statusError = true;
	} else if (_capsLock) {
		_status = tr::ayu_PasscodeCapsLock(tr::now);
		_statusError = false;
	} else {
		_status = QString();
		_statusError = false;
	}
	if (!_busy && left >= 0 && cPasscodeBadTries() > 0) {
		const auto attempts = tr::ayu_PasscodeAttemptsLeft(
			tr::now,
			lt_attempts,
			QString::number(left));
		_status = _status.isEmpty() ? attempts : (_status + '\n' + attempts);
		_statusError = _statusError || (left <= kAttemptsWarning);
	}
	update();
}

void PasscodeLockWidget::updateCapsLock() {
	if (!AyuPlatform::CapsLockSupported()) {
		return;
	}
	const auto on = AyuPlatform::CapsLockOn();
	if (_capsLock != on) {
		_capsLock = on;
		updateStatus();
	}
}

bool PasscodeLockWidget::eventFilter(QObject *o, QEvent *e) {
	if (o == _passcode.data()) {
		const auto type = e->type();
		if (type == QEvent::KeyPress
			|| type == QEvent::KeyRelease
			|| type == QEvent::FocusIn) {
			// WHY: the key state changes after the event is delivered.
			crl::on_main(this, [=] { updateCapsLock(); });
		}
	}
	return LockWidget::eventFilter(o, e);
}

void PasscodeLockWidget::setBusy(bool busy) {
	if (_busy == busy) {
		return;
	}
	_busy = busy;
	_passcode->setDisabled(busy);
	_submit->setDisabled(busy);
	if (!busy) {
		_passcode->setFocusFast();
	}
	updateStatus();
}

void PasscodeLockWidget::shake() {
	_shakeAnimation.stop();
	_shakeAnimation.start(Ui::DefaultShakeCallback([=](int shift) {
		_shift = shift;
		updateLayout();
	}), 0., 1., st::shakeDuration);
}

void PasscodeLockWidget::paintContent(QPainter &p) {
	LockWidget::paintContent(p);

	auto hq = PainterHighQualityEnabler(p);
	auto tint = st::activeButtonBg->c;
	tint.setAlpha(40);
	auto clear = tint;
	clear.setAlpha(0);
	auto gradient = QRadialGradient(
		QPointF(width() / 2., height() * 0.3),
		std::max(width(), height()) * 0.75);
	gradient.setColorAt(0., tint);
	gradient.setColorAt(1., clear);
	p.fillRect(rect(), gradient);

	if (_showClock) {
		const auto &clock = st::passcodeClockFont;
		p.setPen(st::windowFg);
		p.setFont(clock);
		p.drawText(
			QRect(0, _clockTop, width(), clock->height),
			_clock,
			style::al_top);
		p.setPen(st::windowSubTextFg);
		p.setFont(st::passcodeDateFont);
		p.drawText(
			QRect(
				0,
				_clockTop + clock->height + st::passcodeClockSkip,
				width(),
				st::passcodeDateFont->height),
			_date,
			style::al_top);
	}

	const auto radius = AyuUiSettings::isTelegramDesign()
		? st::passcodeCardRadius
		: st::passcodeCardRadiusMaterial;
	p.setPen(QPen(st::shadowFg->c, st::lineWidth));
	p.setBrush(st::windowBg);
	p.drawRoundedRect(_card, radius, radius);

	const auto size = st::passcodeLockSize;
	const auto lock = QRect((width() - size) / 2, _lockTop, size, size);
	p.setPen(Qt::NoPen);
	p.setBrush(st::activeButtonBg);
	p.drawEllipse(lock);
	st::passcodeLockIcon.paintInCenter(p, lock);

	p.setFont(st::passcodeHeaderFont);
	p.setPen(st::windowFg);
	p.drawText(
		QRect(_card.x(), _titleTop, _card.width(), st::passcodeHeaderFont->height),
		tr::lng_passcode_enter(tr::now),
		style::al_top);

	if (!_status.isEmpty()) {
		p.setFont(st::passcodeStatusFont);
		p.setPen(_statusError ? st::boxTextFgError : st::windowSubTextFg);
		p.drawText(
			QRect(_card.x(), _statusTop, _card.width(), st::passcodeStatusHeight),
			_status,
			style::al_center);
	}
}

void PasscodeLockWidget::submit() {
	if (_busy) {
		return;
	}
	setBusy(true);
	TryPasscodeAsync(_passcode->text(), crl::guard(this, [=](
			PasscodeAttempt result) {
		setBusy(false);
		switch (result) {
		case PasscodeAttempt::Empty:
			_passcode->showError();
			return;
		case PasscodeAttempt::Flood:
			_passcode->showError();
			shake();
			updateStatus();
			return;
		case PasscodeAttempt::Wrong:
			error();
			return;
		case PasscodeAttempt::Wiped:
			_error = tr::ayu_PasscodeWiped(tr::now);
			_passcode->clear();
			updateStatus();
			return;
		case PasscodeAttempt::Unavailable:
			_error = tr::ayu_PasscodeKeyUnavailable(tr::now);
			_passcode->showError();
			updateStatus();
			return;
		case PasscodeAttempt::Correct:
			break;
		}
		Core::App().unlockPasscode(); // Destroys this widget.
	}));
}

void PasscodeLockWidget::error() {
	_error = tr::lng_passcode_wrong(tr::now);
	_passcode->selectAll();
	_passcode->showError();
	shake();
	updateStatus();
}

void PasscodeLockWidget::changed() {
	if (!_error.isEmpty()) {
		_error = QString();
		updateStatus();
	}
}

void PasscodeLockWidget::resizeEvent(QResizeEvent *e) {
	updateLayout();
}

void PasscodeLockWidget::updateLayout() {
	const auto &padding = st::passcodeCardPadding;
	const auto field = _passcode->size();
	const auto headerHeight = st::passcodeHeaderFont->height;
	const auto cardWidth = std::min(
		field.width() + padding.left() + padding.right(),
		width());
	const auto cardHeight = padding.top()
		+ st::passcodeLockSize
		+ st::passcodeLockSkip
		+ headerHeight
		+ st::passcodeTitleSkip
		+ field.height()
		+ st::passcodeStatusHeight
		+ _submit->height()
		+ st::passcodeLogoutSkip
		+ _logout->height()
		+ padding.bottom();
	const auto clockHeight = st::passcodeClockFont->height
		+ st::passcodeClockSkip
		+ st::passcodeDateFont->height
		+ st::passcodeClockBottom;
	_showClock = AyuSettings::getInstance().lockScreenShowClock()
		&& (height() >= cardHeight + clockHeight + 2 * padding.top());
	const auto total = cardHeight + (_showClock ? clockHeight : 0);
	auto top = std::max((height() - total) / 2, 0);
	if (_showClock) {
		_clockTop = top;
		top += clockHeight;
	}
	_card = QRect((width() - cardWidth) / 2, top, cardWidth, cardHeight);
	auto y = top + padding.top();
	_lockTop = y;
	y += st::passcodeLockSize + st::passcodeLockSkip;
	_titleTop = y;
	y += headerHeight + st::passcodeTitleSkip;
	_passcode->moveToLeft((width() - field.width()) / 2 + _shift, y);
	y += field.height();
	_statusTop = y;
	y += st::passcodeStatusHeight;
	_submit->moveToLeft((width() - _submit->width()) / 2, y);
	y += _submit->height() + st::passcodeLogoutSkip;
	_logout->moveToLeft((width() - _logout->width()) / 2, y);
	update();
}

void PasscodeLockWidget::setInnerFocus() {
	LockWidget::setInnerFocus();
	_passcode->setFocusFast();
}

TermsLock TermsLock::FromMTP(
		Main::Session *session,
		const MTPDhelp_termsOfService &data) {
	const auto minAge = data.vmin_age_confirm();
	return {
		bytes::make_vector(data.vid().c_dataJSON().vdata().v),
		TextWithEntities {
			qs(data.vtext()),
			Api::EntitiesFromMTP(session, data.ventities().v) },
		(minAge ? std::make_optional(minAge->v) : std::nullopt),
		data.is_popup()
	};
}

TermsBox::TermsBox(
	QWidget*,
	const TermsLock &data,
	rpl::producer<QString> agree,
	rpl::producer<QString> cancel)
: _data(data)
, _agree(std::move(agree))
, _cancel(std::move(cancel)) {
}

TermsBox::TermsBox(
	QWidget*,
	const TextWithEntities &text,
	rpl::producer<QString> agree,
	rpl::producer<QString> cancel,
	bool attentionAgree)
: _data{ {}, text, std::nullopt, false }
, _agree(std::move(agree))
, _cancel(std::move(cancel))
, _attentionAgree(attentionAgree) {
}

rpl::producer<> TermsBox::agreeClicks() const {
	return _agreeClicks.events();
}

rpl::producer<> TermsBox::cancelClicks() const {
	return _cancelClicks.events();
}

void TermsBox::prepare() {
	setTitle(tr::lng_terms_header());

	auto check = std::make_unique<Ui::CheckView>(st::defaultCheck, false);
	const auto ageCheck = check.get();
	const auto age = _data.minAge
		? Ui::CreateChild<Ui::PaddingWrap<Ui::Checkbox>>(
			this,
			object_ptr<Ui::Checkbox>(
				this,
				tr::lng_terms_age(tr::now, lt_count, *_data.minAge),
				st::defaultCheckbox,
				std::move(check)),
			st::termsAgePadding)
		: nullptr;
	if (age) {
		age->resizeToNaturalWidth(st::boxWideWidth);
	}

	const auto content = setInnerWidget(
		object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
			this,
			object_ptr<Ui::FlatLabel> (
				this,
				rpl::single(_data.text),
				st::termsContent),
			st::termsPadding),
		0,
		age ? age->height() : 0);
	const auto show = uiShow();
	content->entity()->setClickHandlerFilter([=](
			const ClickHandlerPtr &handler,
			Qt::MouseButton button) {
		const auto link = handler
			? handler->copyToClipboardText()
			: QString();
		if (TextUtilities::RegExpMention().match(link).hasMatch()) {
			_lastClickedMention = link;
			show->showToast(
				tr::lng_terms_agree_to_proceed(tr::now, lt_bot, link));
			return false;
		}
		return true;
	});

	const auto errorAnimationCallback = [=] {
		const auto check = ageCheck;
		const auto error = _ageErrorAnimation.value(
			_ageErrorShown ? 1. : 0.);
		if (error == 0.) {
			check->setUntoggledOverride(std::nullopt);
		} else {
			const auto color = anim::color(
				st::defaultCheck.untoggledFg,
				st::boxTextFgError,
				error);
			check->setUntoggledOverride(color);
		}
	};
	const auto toggleAgeError = [=](bool shown) {
		if (_ageErrorShown != shown) {
			_ageErrorShown = shown;
			_ageErrorAnimation.start(
				[=] { errorAnimationCallback(); },
				_ageErrorShown ? 0. : 1.,
				_ageErrorShown ? 1. : 0.,
				st::defaultCheck.duration);
		}
	};

	const auto &agreeStyle = _attentionAgree
		? st::attentionBoxButton
		: st::defaultBoxButton;
	addButton(std::move(_agree), [=] {}, agreeStyle)->clicks(
	) | rpl::filter([=] {
		if (age && !age->entity()->checked()) {
			toggleAgeError(true);
			return false;
		}
		return true;
	}) | rpl::to_empty | rpl::start_to_stream(_agreeClicks, lifetime());

	if (_cancel) {
		addButton(std::move(_cancel), [] {})->clicks(
		) | rpl::to_empty | rpl::start_to_stream(_cancelClicks, lifetime());
	}

	if (age) {
		age->entity()->checkedChanges(
		) | rpl::on_next([=] {
			toggleAgeError(false);
		}, age->lifetime());

		heightValue(
		) | rpl::on_next([=](int height) {
			age->moveToLeft(0, height - age->height());
		}, age->lifetime());
	}

	content->resizeToWidth(st::boxWideWidth);

	using namespace rpl::mappers;
	rpl::combine(
		content->heightValue(),
		age ? age->heightValue() : rpl::single(0),
		_1 + _2
	) | rpl::on_next([=](int height) {
		setDimensions(st::boxWideWidth, height);
	}, content->lifetime());
}

void TermsBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		_agreeClicks.fire({});
	} else {
		BoxContent::keyPressEvent(e);
	}
}

QString TermsBox::lastClickedMention() const {
	return _lastClickedMention;
}

} // namespace Window
