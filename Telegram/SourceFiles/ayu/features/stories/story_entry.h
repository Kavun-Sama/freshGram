// This is the source code of freshGram.
#pragma once

#include "window/window_peer_menu.h"

class PeerData;

namespace Window {
class SessionController;
} // namespace Window

namespace AyuStories {

// Checks the limit, asks for a photo or a video and opens the window of the story.
void StartPosting(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

void AddPostStoryAction(
	PeerData *peer,
	not_null<Window::SessionController*> controller,
	const Window::PeerMenuCallback &addCallback);

} // namespace AyuStories
