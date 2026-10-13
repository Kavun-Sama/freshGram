// This is the source code of freshGram.
#pragma once

#include "base/basic_types.h"

namespace Ui {
class GenericBox;
} // namespace Ui

namespace AyuUi {

struct DeleteOwnRange {
	TimeId minDate = 0;
	TimeId maxDate = 0;
	int limit = 0;

	[[nodiscard]] bool empty() const {
		return !minDate && !maxDate && !limit;
	}
};

void FillDeleteOwnMessagesBox(
	not_null<Ui::GenericBox*> box,
	Fn<void(DeleteOwnRange, Fn<void()>)> chosen);

} // namespace AyuUi
