// This is the source code of freshGram.
#pragma once

namespace Ui {
class PasswordInput;
class VerticalLayout;
} // namespace Ui

namespace Settings::Builder {
class SectionBuilder;
} // namespace Settings::Builder

namespace AyuUi {

void AddPasscodeStrength(
	not_null<Ui::VerticalLayout*> content,
	not_null<Ui::PasswordInput*> field);

void AddPasscodeSecuritySettings(Settings::Builder::SectionBuilder &builder);

} // namespace AyuUi
