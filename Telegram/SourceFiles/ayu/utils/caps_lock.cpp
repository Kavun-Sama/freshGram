// This is the source code of freshGram.
#include "ayu/utils/caps_lock.h"

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined Q_OS_MAC // Q_OS_WIN
#include <CoreGraphics/CoreGraphics.h>
#endif // Q_OS_WIN || Q_OS_MAC

namespace AyuPlatform {

bool CapsLockSupported() {
#if defined Q_OS_WIN || defined Q_OS_MAC
	return true;
#else // Q_OS_WIN || Q_OS_MAC
	return false;
#endif // Q_OS_WIN || Q_OS_MAC
}

bool CapsLockOn() {
#ifdef Q_OS_WIN
	return (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
#elif defined Q_OS_MAC // Q_OS_WIN
	const auto flags = CGEventSourceFlagsState(
		kCGEventSourceStateCombinedSessionState);
	return (flags & kCGEventFlagMaskAlphaShift) != 0;
#else // Q_OS_WIN || Q_OS_MAC
	return false;
#endif // Q_OS_WIN || Q_OS_MAC
}

} // namespace AyuPlatform
