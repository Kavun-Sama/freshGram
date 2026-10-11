// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

namespace AyuUiSettings {

inline constexpr int kMaxAvatarCorners = 23;

void setMonoFont(QString newFont);
QString getMonoFont();

void setWideMultiplier(double val);

bool isWideMultiplied();
int getWideMultiplied(int width, double mult);

void setMaterialSwitches(bool val);
bool isMaterialSwitches();

enum class IconSet {
	Telegram = 0,
	Materialgram = 1,
	Fagram = 2,
};
inline constexpr int kIconSetCount = 3;

void setIconSet(int val);
IconSet iconSet();

enum class DesignStyle {
	Telegram = 0,
	Materialgram = 1,
	Fagram = 2,
};
inline constexpr int kDesignStyleCount = 3;

void setDesignStyle(int val);
DesignStyle designStyle();
int getDesignValue(int material, int telegram);

void setAvatarCorners(int val);
int getAvatarCorners();

}
