// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu_ui_settings.h"

#include <utility>

namespace AyuUiSettings {

QString monoFont;
double wideMultiplier = 1.0;
bool materialSwitches;
IconSet currentIconSet = IconSet::Materialgram;
DesignStyle currentDesignStyle = DesignStyle::Materialgram;
int avatarCorners = kMaxAvatarCorners;

void setMonoFont(QString newFont) {
	monoFont = std::move(newFont);
}

QString getMonoFont() {
	return monoFont;
}

void setWideMultiplier(double val) {
	wideMultiplier = val;
}

bool isWideMultiplied() {
	return abs(wideMultiplier - 1.0) > 0.01;
}

int getWideMultiplied(int width, double mult) {
	if (!isWideMultiplied()) {
		return width;
	}
	const auto res = width * (wideMultiplier * mult);
	return std::max(width, static_cast<int>(std::round(res)));
}

void setMaterialSwitches(bool val) {
	materialSwitches = val;
}

bool isMaterialSwitches() {
	return materialSwitches;
}

void setIconSet(int val) {
	currentIconSet = (val >= 0 && val < kIconSetCount)
		? IconSet(val)
		: IconSet::Materialgram;
}

IconSet iconSet() {
	return currentIconSet;
}

void setDesignStyle(int val) {
	currentDesignStyle = (val >= 0 && val < kDesignStyleCount)
		? DesignStyle(val)
		: DesignStyle::Materialgram;
}

DesignStyle designStyle() {
	return currentDesignStyle;
}

int getDesignValue(int material, int telegram) {
	return (currentDesignStyle == DesignStyle::Materialgram)
		? material
		: telegram;
}

void setAvatarCorners(int val) {
	avatarCorners = val;
}

int getAvatarCorners() {
	return avatarCorners;
}

}
