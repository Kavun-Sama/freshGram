/*
This file is part of freshGram. The compose stash is ported from Forkgram
(https://github.com/forkgram/tdesktop), based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "ui/controls/jump_down_button.h"
#include "ui/effects/animations.h"

namespace Ui {
class ChatStyle;
} // namespace Ui

namespace HistoryView {

class StashButton final : public Ui::JumpDownButton {
public:
	StashButton(
		QWidget *parent,
		const style::TwoIconButton &st,
		const style::icon &arrow,
		const style::icon &arrowOver);

	void setArrowShown(bool shown, float64 buttonVisible);
	void finishAnimating();

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const style::TwoIconButton &_st;
	const style::icon &_arrow;
	const style::icon &_arrowOver;
	Ui::Animations::Simple _animation;
	std::optional<float64> _frozen;
	bool _arrowShown = false;

};

[[nodiscard]] object_ptr<StashButton> MakeStashButton(
	not_null<QWidget*> parent,
	not_null<const Ui::ChatStyle*> st,
	rpl::lifetime &lifetime);

} // namespace HistoryView
