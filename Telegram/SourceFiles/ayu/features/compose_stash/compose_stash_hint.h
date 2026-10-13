/*
This file is part of freshGram. The compose stash is ported from Forkgram
(https://github.com/forkgram/tdesktop), based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

class QWidget;

namespace Ui::Toast {
class Instance;
} // namespace Ui::Toast

namespace HistoryView::Controls {

struct StashHintDescriptor {
	Fn<QWidget*()> toastParent;
	Fn<QString()> fieldText;
	Fn<bool()> canUse;
};

class StashHintManager final : public base::has_weak_ptr {
public:
	explicit StashHintManager(StashHintDescriptor &&descriptor);
	~StashHintManager();

	[[nodiscard]] static bool ShouldShow();

	void trackChange(bool userDriven);
	void markUsed();
	void hide();

private:
	struct CutMemory {
		uint64_t hash = 0;
		int length = 0;
		crl::time at = 0;
	};

	void maybeShow();

	const StashHintDescriptor _data;
	base::weak_ptr<Ui::Toast::Instance> _toast;
	std::optional<CutMemory> _memory;
	int _peakLength = 0;
	uint64_t _peakHash = 0;

};

} // namespace HistoryView::Controls
