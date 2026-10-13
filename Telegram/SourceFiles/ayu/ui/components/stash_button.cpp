/*
This file is part of freshGram. The compose stash is ported from Forkgram
(https://github.com/forkgram/tdesktop), based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ayu/ui/components/stash_button.h"

#include "base/basic_types.h"
#include "ui/chat/chat_style.h"
#include "ui/painter.h"

#include "styles/style_ayu_styles.h"

#include <cmath>

namespace HistoryView {
namespace {

constexpr auto kStashDelay = crl::time(150);
constexpr auto kStashArrowDuration = crl::time(106);
constexpr auto kStashBounceDuration = crl::time(173);
constexpr auto kStashDuration = kStashDelay
	+ kStashArrowDuration
	+ kStashBounceDuration;

[[nodiscard]] float64 ArrowShift(float64 value) {
	const auto from = kStashDelay / float64(kStashDuration);
	const auto till = (kStashDelay + kStashArrowDuration)
		/ float64(kStashDuration);
	return (value <= from)
		? -1.
		: (value >= till)
		? 0.
		: ((value - till) / (till - from));
}

[[nodiscard]] float64 BubbleSwing(float64 value) {
	const auto from = (kStashDelay + kStashArrowDuration)
		/ float64(kStashDuration);
	if (value <= from) {
		return 0.;
	}
	const auto bounce = (value - from) / (1. - from);
	return std::sin(2 * M_PI * bounce) * (1. - bounce);
}

} // namespace

StashButton::StashButton(
	QWidget *parent,
	const style::TwoIconButton &st,
	const style::icon &arrow,
	const style::icon &arrowOver)
: JumpDownButton(parent, st)
, _st(st)
, _arrow(arrow)
, _arrowOver(arrowOver) {
}

void StashButton::setArrowShown(bool shown, float64 buttonVisible) {
	if (_arrowShown == shown) {
		return;
	}
	_arrowShown = shown;
	if (!shown) {
		_frozen = _animation.value(1.);
		_animation.stop();
		update();
		return;
	}
	const auto from = (buttonVisible > 0.) ? _frozen.value_or(1.) : 0.;
	_frozen = std::nullopt;
	if (from < 1.) {
		_animation.start(
			[=] { update(); },
			from,
			1.,
			kStashDuration * (1. - from));
	}
	update();
}

void StashButton::finishAnimating() {
	_frozen = std::nullopt;
	_animation.stop();
	update();
}

void StashButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto active = isOver() || isDown();
	(active ? _st.iconBelowOver : _st.iconBelow).paint(
		p,
		_st.iconPosition,
		width());
	paintRipple(p, _st.rippleAreaPosition.x(), _st.rippleAreaPosition.y());

	const auto value = _frozen.value_or(_animation.value(1.));
	const auto swing = BubbleSwing(value) * st::historyStashBubbleTravel;
	const auto dip = QPoint(0, int(base::SafeRound(swing)));
	(active ? _st.iconAboveOver : _st.iconAbove).paint(
		p,
		_st.iconPosition + dip,
		width());

	const auto shift = ArrowShift(value);
	if (shift <= -1.) {
		return;
	}
	p.setClipRect(QRect(
		st::historyStashArrowClipPosition + dip,
		st::historyStashArrowClipSize));
	const auto skip
		= int(base::SafeRound(shift * st::historyStashArrowTravel));
	(active ? _arrowOver : _arrow).paint(
		p,
		_st.iconPosition + dip + QPoint(0, skip),
		width());
}

object_ptr<StashButton> MakeStashButton(
		not_null<QWidget*> parent,
		not_null<const Ui::ChatStyle*> st,
		rpl::lifetime &lifetime) {
	return object_ptr<StashButton>(
		parent,
		st->value(lifetime, st::historyStash),
		st->value(lifetime, st::historyStashArrow),
		st->value(lifetime, st::historyStashArrowOver));
}

} // namespace HistoryView
