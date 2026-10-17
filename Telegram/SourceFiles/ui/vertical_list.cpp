/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/vertical_list.h"

#include "lang/lang_text_entity.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ayu/ui/fagram/fagram_cards.h"
#include "ayu/ui/fagram/fagram_design.h"
#include "styles/style_ayu_styles.h"
#include "styles/style_layers.h"

namespace Ui {

void AddSkip(not_null<Ui::VerticalLayout*> container) {
	AddSkip(container, st::defaultVerticalListSkip);
}

void AddSkip(not_null<Ui::VerticalLayout*> container, int skip) {
	container->add(object_ptr<Ui::FixedHeightWidget>(container, skip));
}

void AddDivider(
		not_null<Ui::VerticalLayout*> container,
		const style::DividerBar &st) {
	if (FA::Ui::Enabled() && FA::Ui::InCards(container)) {
		// Cards are separated by an empty gap instead of a gray band.
		container->add(object_ptr<Ui::FixedHeightWidget>(
			container,
			st::fagramDividerGap));
		return;
	}
	container->add(object_ptr<Ui::BoxContentDivider>(
		container,
		st::boxDividerHeight,
		st));
}

not_null<Ui::FlatLabel*> AddDividerText(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text,
		const style::margins &margins,
		const style::DividerLabel &st,
		RectParts parts) {
	return AddDividerText(
		container,
		std::move(text) | rpl::map(tr::marked),
		margins,
		st,
		parts);
}

not_null<Ui::FlatLabel*> AddDividerText(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<TextWithEntities> text,
		const style::margins &margins,
		const style::DividerLabel &st,
		RectParts parts) {
	auto label = object_ptr<Ui::FlatLabel>(
		container,
		std::move(text),
		st.label);
	const auto result = label.data();
	if (FA::Ui::Enabled() && FA::Ui::InCards(container)) {
		// A description below a card has no band around it.
		container->add(std::move(label), st::fagramDescriptionPadding);
		return result;
	}
	container->add(object_ptr<Ui::DividerLabel>(
		container,
		std::move(label),
		margins,
		st.bar,
		parts));
	return result;
}

not_null<Ui::FlatLabel*> AddSubsectionTitle(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text,
		style::margins addPadding,
		const style::FlatLabel *st) {
	return container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			std::move(text),
			st ? *st : st::defaultSubsectionTitle),
		st::defaultSubsectionTitlePadding + addPadding);
}

} // namespace Ui
