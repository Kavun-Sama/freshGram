// This is the source code of freshGram.
#pragma once

#include "ui/chat/attach/attach_prepare.h"

class PeerData;

namespace Window {
class SessionController;
} // namespace Window

namespace AyuUi {

void ShowStoryComposeBox(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	Ui::PreparedFile &&file);

} // namespace AyuUi
