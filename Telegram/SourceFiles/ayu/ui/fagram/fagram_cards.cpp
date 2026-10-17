/*
This file is part of FAgram Desktop,
the unofficial desktop client based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/fagramdesktop/fadesktop/blob/dev/LEGAL
*/
#include "ayu/ui/fagram/fagram_cards.h"

#include "ayu/ayu_card_segment.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/palette.h"
#include "styles/style_ayu_styles.h"

#include <QtGui/QPainterPath>

namespace FA::Ui {
namespace {

using Segment = AyuUiSettings::CardSegment;

constexpr auto kRowProperty = "fa_card_row";
constexpr auto kBlockProperty = "fa_card_block";

struct Item {
	QWidget *row = nullptr;
	int top = 0;
	int bottom = 0;
	bool isRow = false;
};

[[nodiscard]] bool IsRowType(const QWidget *widget) {
	return dynamic_cast<const ::Ui::SettingsButton*>(widget)
		|| dynamic_cast<const ::Ui::Checkbox*>(widget)
		|| widget->property(kRowProperty).toBool()
		|| widget->property(kBlockProperty).toBool();
}

// The buttons inside a block have no background of their own and are
// rounded only where they touch the rounded corners of the block.
void ApplyBlockSegments(not_null<QWidget*> block, Segment position) {
	const auto height = block->height();
	const auto roundTop = (position == Segment::Top
		|| position == Segment::Single);
	const auto roundBottom = (position == Segment::Bottom
		|| position == Segment::Single);
	const auto buttons = block->findChildren<::Ui::SettingsButton*>();
	for (const auto button : buttons) {
		if (button->isHidden()) {
			continue;
		}
		const auto top = button->mapTo(block, QPoint(0, 0)).y();
		const auto atTop = roundTop && (top <= 1);
		const auto atBottom = roundBottom
			&& (top + button->height() >= height - 1);
		button->setCardSegment(
			(atTop && atBottom)
			? Segment::Single
			: atTop
			? Segment::Top
			: atBottom
			? Segment::Bottom
			: Segment::Flat);
	}
}

// A wrapper (slide, fade, padding...) with a single row inside is a row.
[[nodiscard]] QWidget *FindRowWidget(QWidget *widget) {
	if (IsRowType(widget)) {
		return widget;
	}
	for (const auto child : widget->children()) {
		const auto inner = qobject_cast<QWidget*>(child);
		if (inner
			&& !inner->isHidden()
			&& inner->size() == widget->size()
			&& IsRowType(inner)) {
			return inner;
		}
	}
	return nullptr;
}

[[nodiscard]] ::Ui::VerticalLayout *FindNestedLayout(QWidget *widget) {
	if (const auto layout = dynamic_cast<::Ui::VerticalLayout*>(widget)) {
		return layout;
	}
	if (IsRowType(widget)) {
		return nullptr;
	}
	for (const auto child : widget->children()) {
		const auto layout = dynamic_cast<::Ui::VerticalLayout*>(child);
		if (layout && !layout->isHidden()) {
			return layout;
		}
	}
	return nullptr;
}

void Collect(
		not_null<QWidget*> root,
		not_null<::Ui::VerticalLayout*> layout,
		std::vector<Item> &items) {
	for (auto i = 0, count = layout->count(); i != count; ++i) {
		const auto widget = layout->widgetAt(i).get();
		if (widget->isHidden() || widget->height() <= 0) {
			continue;
		}
		if (const auto row = FindRowWidget(widget)) {
			const auto margin = layout->rowMargin(i);
			const auto y = widget->mapTo(root, QPoint(0, 0)).y();
			items.push_back({
				.row = row,
				.top = y - margin.top(),
				.bottom = y + widget->height() + margin.bottom(),
				.isRow = true,
			});
		} else if (const auto nested = FindNestedLayout(widget)) {
			Collect(root, nested, items);
		} else {
			const auto y = widget->mapTo(root, QPoint(0, 0)).y();
			items.push_back({
				.top = y,
				.bottom = y + widget->height(),
			});
		}
	}
}

class Backdrop final : public ::Ui::RpWidget {
public:
	Backdrop(
		QWidget *parent,
		not_null<::Ui::VerticalLayout*> container,
		int inset)
	: RpWidget(parent)
	, _container(container)
	, _inset(inset) {
		setAttribute(Qt::WA_TransparentForMouseEvents);
		container->sizeValue(
		) | rpl::on_next([=](QSize size) {
			setGeometry(0, 0, size.width(), size.height());
			update();
		}, lifetime());
		lower();
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto items = std::vector<Item>();
		Collect(_container, _container, items);

		auto p = QPainter(this);
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);

		const auto left = double(_inset);
		const auto width = double(this->width() - 2 * _inset);
		const auto halfGap = double(AyuUiSettings::CardSegmentHalfGap());
		const auto count = int(items.size());
		for (auto from = 0; from != count;) {
			if (!items[from].isRow) {
				++from;
				continue;
			}
			auto till = from + 1;
			while (till != count && items[till].isRow) {
				++till;
			}
			for (auto i = from; i != till; ++i) {
				const auto first = (i == from);
				const auto last = (i == till - 1);
				const auto position = (first && last)
					? Segment::Single
					: first
					? Segment::Top
					: last
					? Segment::Bottom
					: Segment::Middle;
				const auto top = first
					? items[i].top
					: ((items[i - 1].bottom + items[i].top) / 2);
				const auto bottom = last
					? items[i].bottom
					: ((items[i].bottom + items[i + 1].top) / 2);
				const auto rect = QRectF(
					left,
					top + (first ? 0. : halfGap),
					width,
					bottom - top - (first ? 0. : halfGap) - (last ? 0. : halfGap));
				p.drawPath(AyuUiSettings::CardSegmentPath(rect, position));
				if (items[i].row->property(kBlockProperty).toBool()) {
					ApplyBlockSegments(items[i].row, position);
				} else if (const auto button = dynamic_cast<
						::Ui::RippleButton*>(items[i].row)) {
					button->setCardSegment(position);
				}
			}
			from = till;
		}
	}

private:
	const not_null<::Ui::VerticalLayout*> _container;
	const int _inset;

};

} // namespace

void AttachCards(not_null<::Ui::VerticalLayout*> container) {
	if (InCards(container)) {
		return;
	}
	container->setProperty("fa_cards", true);
	const auto inset = st::fagramCardInset;
	container->setRowInset(inset);
	::Ui::CreateChild<Backdrop>(container.get(), container, inset);
}

void MarkAsBlock(not_null<QWidget*> widget) {
	widget->setProperty(kBlockProperty, true);
}

bool InCards(not_null<QWidget*> widget) {
	for (auto w = widget.get(); w; w = w->parentWidget()) {
		if (w->property("fa_cards").toBool()) {
			return true;
		}
	}
	return false;
}

} // namespace FA::Ui
