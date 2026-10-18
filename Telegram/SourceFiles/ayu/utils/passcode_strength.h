// This is the source code of freshGram.
#pragma once

namespace AyuPasscode {

enum class Strength : uchar {
	None,
	Weak,
	Medium,
	Strong,
	VeryStrong,
};

[[nodiscard]] int EstimateBits(const QString &passcode);
[[nodiscard]] Strength StrengthForBits(int bits);
[[nodiscard]] QString StrengthText(const QString &passcode);

} // namespace AyuPasscode
