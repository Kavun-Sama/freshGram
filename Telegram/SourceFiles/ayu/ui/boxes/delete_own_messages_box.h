// This is the source code of freshGram.
#pragma once

#include "base/basic_types.h"
#include "data/data_msg_id.h"

#include <vector>

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Data {
class Forum;
} // namespace Data

namespace AyuUi {

struct DeleteOwnRange {
	TimeId minDate = 0;
	TimeId maxDate = 0;
	int limit = 0;
	std::vector<MsgId> topics;

	[[nodiscard]] bool empty() const {
		return !minDate && !maxDate && !limit && topics.empty();
	}
};

void FillDeleteOwnMessagesBox(
	not_null<Ui::GenericBox*> box,
	Data::Forum *forum,
	Fn<void(DeleteOwnRange, Fn<void()>)> chosen);

} // namespace AyuUi
