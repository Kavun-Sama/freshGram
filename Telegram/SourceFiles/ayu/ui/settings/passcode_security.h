// This is the source code of freshGram.
#pragma once

namespace Ui {
class PasswordInput;
class VerticalLayout;
} // namespace Ui

namespace Settings::Builder {
class SectionBuilder;
} // namespace Settings::Builder

namespace Storage {
enum class PasscodeCheck : uchar;
} // namespace Storage

namespace AyuUi {

// Derives the key on a thread, the result comes on the main thread.
void CheckPasscodeAsync(
	const QByteArray &passcode,
	Fn<void(Storage::PasscodeCheck)> done);

// The new passcode keeps the current key protection.
void SetPasscodeAsync(const QByteArray &passcode, Fn<void(bool)> done);

void AddPasscodeStrength(
	not_null<Ui::VerticalLayout*> content,
	not_null<Ui::PasswordInput*> field);

void AddPasscodeSecuritySettings(Settings::Builder::SectionBuilder &builder);

} // namespace AyuUi
