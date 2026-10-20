// This is the source code of freshGram.
#pragma once

class UserData;

namespace Window {
class SessionController;
} // namespace Window

namespace AyuUi {

// Chooses the contacts to show a story to or to hide it from.
void ShowStoryUsersBox(
	not_null<Window::SessionController*> controller,
	rpl::producer<QString> title,
	const std::vector<not_null<UserData*>> &selected,
	Fn<void(std::vector<not_null<UserData*>>)> done);

} // namespace AyuUi
