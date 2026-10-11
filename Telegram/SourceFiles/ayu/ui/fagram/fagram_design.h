// This file is part of freshGram. The Fagram design pieces in this
// directory are ported from FAgram Desktop
// (https://github.com/fagramdesktop/fadesktop), licensed under GPLv3.
#pragma once

#include "ayu/ayu_ui_settings.h"

namespace FA::Ui {

[[nodiscard]] inline bool Enabled() {
	return AyuUiSettings::designStyle() == AyuUiSettings::DesignStyle::Fagram;
}

} // namespace FA::Ui
