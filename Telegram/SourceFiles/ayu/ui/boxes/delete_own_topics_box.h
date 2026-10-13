// This is the source code of freshGram.
#pragma once

#include "data/data_msg_id.h"

#include <vector>

namespace Data {
class Forum;
} // namespace Data

namespace Ui {
class Show;
} // namespace Ui

namespace AyuUi {

void ShowDeleteOwnTopicsBox(
	not_null<Data::Forum*> forum,
	std::shared_ptr<Ui::Show> show,
	std::vector<MsgId> current,
	Fn<void(std::vector<MsgId>)> chosen);

} // namespace AyuUi
