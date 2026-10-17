/*
This file is part of FAgram Desktop,
the unofficial desktop client based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/fagramdesktop/fadesktop/blob/dev/LEGAL
*/
#pragma once


namespace Ui {
class VerticalLayout;
class RpWidget;
} // namespace Ui

namespace FA::Ui {

// Turns the rows of a vertical layout into the Fagram cards: neighbour
// rows without a divider, a title or a skip between them are joined into
// one rounded card. Call it before adding the rows, it narrows them by
// the card inset.
void AttachCards(not_null<::Ui::VerticalLayout*> container);

// Makes the whole widget a single row of the cards, buttons inside it
// are drawn without a background.
void MarkAsBlock(not_null<QWidget*> widget);

// Is the widget placed inside the layout prepared by AttachCards.
[[nodiscard]] bool InCards(not_null<QWidget*> widget);

} // namespace FA::Ui
