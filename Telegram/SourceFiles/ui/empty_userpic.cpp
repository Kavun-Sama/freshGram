/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/empty_userpic.h"

#include "info/channel_statistics/earn/earn_icons.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/animation_value.h"
#include "ui/emoji_config.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"
#include "styles/style_widgets.h" // style::IconButton
#include "styles/style_info.h" // st::topBarCall

#include <QtCore/QMutex>
#include <QtSvg/QSvgRenderer>

#include "ayu/ayu_settings.h"
#include "ayu/ayu_ui_settings.h"
#include "ayu/ui/ayu_userpic.h"


namespace Ui {
namespace {

[[nodiscard]] QColor UserpicBgColor() {
	auto bg = QColor(st::sideBarBg->c);
	const style::color *references[] = {
		&st::windowBg,
		&st::dialogsBgOver,
		&st::dialogsBgActive,
	};
	auto highest = 0.f;
	auto closest = 1.f;
	for (const auto reference : references) {
		const auto lightness = float(QColor((*reference)->c).lightnessF());
		highest = std::max(highest, lightness);
		closest = std::min(closest, std::abs(lightness - float(bg.lightnessF())));
	}
	if (highest >= 0.5f || closest >= 0.05f) {
		return bg;
	}
	bg.setHslF(
		std::max(float(bg.hslHueF()), 0.f),
		bg.hslSaturationF(),
		std::min(highest + 0.13f, 1.f));
	return bg;
}


[[nodiscard]] QBrush GradientBrush(
		int y,
		int size,
		const style::color &color1,
		const style::color &color2) {
	auto gradient = QLinearGradient(0, y, 0, y + size);
	gradient.setStops({
		{ 0., color1->c },
		{ 1., color2->c }
	});
	return QBrush(gradient);
}

[[nodiscard]] QBrush SpecialUserpicBg(
		int y,
		int size,
		const style::color &color1,
		const style::color &color2) {
	return AyuUiSettings::isTelegramDesign()
		? GradientBrush(y, size, color1, color2)
		: QBrush(UserpicBgColor());
}

[[nodiscard]] const style::color &SpecialUserpicFg(
		const style::color &telegram) {
	return AyuUiSettings::isTelegramDesign()
		? telegram
		: st::windowBgActive;
}

[[nodiscard]] bool IsExternal(const QString &name) {
	return !name.isEmpty()
		&& (name.front() == QChar(0))
		&& QStringView(name).mid(1) == u"external"_q;
}

[[nodiscard]] bool IsInaccessible(const QString &name) {
	return !name.isEmpty()
		&& (name.front() == QChar(0))
		&& QStringView(name).mid(1) == u"inaccessible"_q;
}

void PaintSavedMessagesInner(
		QPainter &p,
		int x,
		int y,
		int size,
		const style::color &fg) {
	// |<----width----->|
	//
	// XXXXXXXXXXXXXXXXXX  ---
	// X                X   |
	// X                X   |
	// X                X   |
	// X                X height
	// X       XX       X   |     ---
	// X     XX  XX     X   |      |
	// X   XX      XX   X   |     add
	// X XX          XX X   |      |
	// XX              XX  ---    ---

	const auto thinkness = base::SafeRound(
		size * (AyuUiSettings::isTelegramDesign() ? 0.055 : 0.045));
	const auto increment = int(thinkness) % 2 + (size % 2);
	const auto width = base::SafeRound(size * 0.15) * 2 + increment;
	const auto height = base::SafeRound(size * 0.19) * 2 + increment;
	const auto add = base::SafeRound(size * 0.064);

	const auto left = x + (size - width) / 2;
	const auto top = y + (size - height) / 2;
	const auto right = left + width;
	const auto bottom = top + height;
	const auto middle = (left + right) / 2;
	const auto half = (top + bottom) / 2;

	p.setBrush(Qt::NoBrush);
	auto pen = fg->p;
	pen.setWidthF(thinkness);
	pen.setCapStyle(Qt::FlatCap);

	{
		// XXXXXXXXXXXXXXXXXX
		// X                X
		// X                X
		// X                X
		// X                X
		// X                X

		pen.setJoinStyle(Qt::RoundJoin);
		p.setPen(pen);
		QPainterPath path;
		path.moveTo(left, half);
		path.lineTo(left, top);
		path.lineTo(right, top);
		path.lineTo(right, half);
		p.drawPath(path);
	}
	{
		// X                X
		// X       XX       X
		// X     XX  XX     X
		// X   XX      XX   X
		// X XX          XX X
		// XX              XX

		pen.setJoinStyle(Qt::MiterJoin);
		p.setPen(pen);
		QPainterPath path;
		path.moveTo(left, half);
		path.lineTo(left, bottom);
		path.lineTo(middle, bottom - add);
		path.lineTo(right, bottom);
		path.lineTo(right, half);
		p.drawPath(path);
	}
}

void PaintIconInner(
		QPainter &p,
		int x,
		int y,
		int size,
		int defaultSize,
		const style::icon &icon,
		const style::color &fg) {
	if (size == defaultSize) {
		const auto rect = QRect{ x, y, size, size };
		icon.paintInCenter(
			p,
			rect,
			fg->c);
	} else {
		p.save();
		const auto ratio = size / float64(defaultSize);
		p.translate(x + size / 2., y + size / 2.);
		p.scale(ratio, ratio);
		const auto skip = defaultSize;
		const auto rect = QRect{ -skip, -skip, 2 * skip, 2 * skip };
		icon.paintInCenter(
			p,
			rect,
			fg->c);
		p.restore();
	}
}

void PaintRepliesMessagesInner(
		QPainter &p,
		int x,
		int y,
		int size,
		const style::color &fg) {
	PaintIconInner(
		p,
		x,
		y,
		size,
		st::defaultDialogRow.photoSize,
		st::dialogsRepliesUserpic,
		fg);
}

void PaintHiddenAuthorInner(
		QPainter &p,
		int x,
		int y,
		int size,
		const style::color &fg) {
	PaintIconInner(
		p,
		x,
		y,
		size,
		st::defaultDialogRow.photoSize,
		st::dialogsHiddenAuthorUserpic,
		fg);
}

void PaintMyNotesInner(
		QPainter &p,
		int x,
		int y,
		int size,
		const style::color &fg) {
	PaintIconInner(
		p,
		x,
		y,
		size,
		st::defaultDialogRow.photoSize,
		st::dialogsMyNotesUserpic,
		fg);
}

void PaintCurrencyInner(
		QPainter &p,
		int x,
		int y,
		int size,
		const style::color &fg) {
	auto svg = QSvgRenderer(Ui::Earn::CurrencySvgColored(fg->c));
	const auto skip = size / 5;
	svg.render(&p, QRect(x, y, size, size).marginsRemoved(
		{ skip, skip, skip, skip }));
}

void PaintExternalMessagesInner(
		QPainter &p,
		int x,
		int y,
		int size,
		const style::color &fg) {
	PaintIconInner(
		p,
		x,
		y,
		size,
		st::msgPhotoSize,
		st::topBarCall.icon,
		fg);
}

void PaintInaccessibleAccountInner(
		QPainter &p,
		int x,
		int y,
		int size,
		const style::color &fg) {
	if (size > st::defaultDialogRow.photoSize) {
		PaintIconInner(
			p,
			x,
			y,
			size,
			st::infoProfilePhotoInnerSize,
			st::infoProfileInaccessibleUserpic,
			fg);
	} else {
		PaintIconInner(
			p,
			x,
			y,
			size,
			st::defaultDialogRow.photoSize,
			st::dialogsInaccessibleUserpic,
			fg);
	}
}

[[nodiscard]] QImage Generate(int size, Fn<void(QPainter&)> callback) {
	auto result = QImage(
		QSize(size, size) * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		callback(p);
	}
	return result;
}

} // namespace

EmptyUserpic::EmptyUserpic(const BgColors &colors, const QString &name)
: _colors(colors) {
	fillString(name);
}

QString EmptyUserpic::ExternalName() {
	return QChar(0) + u"external"_q;
}

QString EmptyUserpic::InaccessibleName() {
	return QChar(0) + u"inaccessible"_q;
}

uint8 EmptyUserpic::ColorIndex(uint64 id) {
	return DecideColorIndex(id);
}

EmptyUserpic::BgColors EmptyUserpic::UserpicColor(uint8 colorIndex) {
	const EmptyUserpic::BgColors colors[] = {
		{ st::historyPeer1UserpicBg, st::historyPeer1UserpicBg2 },
		{ st::historyPeer2UserpicBg, st::historyPeer2UserpicBg2 },
		{ st::historyPeer3UserpicBg, st::historyPeer3UserpicBg2 },
		{ st::historyPeer4UserpicBg, st::historyPeer4UserpicBg2 },
		{ st::historyPeer5UserpicBg, st::historyPeer5UserpicBg2 },
		{ st::historyPeer6UserpicBg, st::historyPeer6UserpicBg2 },
		{ st::historyPeer7UserpicBg, st::historyPeer7UserpicBg2 },
		{ st::historyPeer8UserpicBg, st::historyPeer8UserpicBg2 },
	};
	return colors[ColorIndexToPaletteIndex(colorIndex)];
}

void EmptyUserpic::paint(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		Fn<void()> paintBackground) const {
	x = style::RightToLeft() ? (outerWidth - x - size) : x;

	const auto fontsize = (size * 13) / 33;
	auto font = st::historyPeerUserpicFont->f;
	font.setPixelSize(fontsize);

	PainterHighQualityEnabler hq(p);
	p.setBrush(SpecialUserpicBg(y, size, _colors.color1, _colors.color2));
	p.setPen(Qt::NoPen);
	paintBackground();

	const auto &fg = SpecialUserpicFg(st::historyPeerUserpicFg);
	if (IsExternal(_string)) {
		PaintExternalMessagesInner(p, x, y, size, fg);
	} else if (IsInaccessible(_string)) {
		PaintInaccessibleAccountInner(
			p,
			x,
			y,
			size,
			fg);
	} else {
		p.setFont(font);
		p.setBrush(Qt::NoBrush);
		p.setPen(fg);
		p.drawText(
			QRect(x, y, size, size),
			_string,
			QTextOption(style::al_center));
	}
}

void EmptyUserpic::paintCircle(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size) const {
	paint(p, x, y, outerWidth, size, [&] {
		AyuUserpic::PaintShape(p, x, y, size);
	});
}

void EmptyUserpic::paintRounded(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		int radius) const {
	paint(p, x, y, outerWidth, size, [&] {
		if (AyuSettings::getInstance().singleCornerRadius()) {
			AyuUserpic::PaintShape(p, x, y, size);
		} else {
			p.drawRoundedRect(x, y, size, size, radius, radius);
		}
	});
}

void EmptyUserpic::paintSquare(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size) const {
	paint(p, x, y, outerWidth, size, [&] {
		AyuUserpic::PaintShape(p, x, y, size);
	});
}

void EmptyUserpic::paintMonoforum(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size) const {
	paint(p, x, y, outerWidth, size, [&] {
		if (AyuSettings::getInstance().singleCornerRadius()) {
			AyuUserpic::PaintShape(p, x, y, size);
		} else {
			PaintMonoforumShape(p, QRect(x, y, size, size));
		}
	});
}

void EmptyUserpic::PaintSavedMessages(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size) {
	const auto bg = SpecialUserpicBg(
		y,
		size,
		st::historyPeerSavedMessagesBg,
		st::historyPeerSavedMessagesBg2);
	const auto &fg = SpecialUserpicFg(st::historyPeerUserpicFg);
	PaintSavedMessages(p, x, y, outerWidth, size, bg, fg);
}

void EmptyUserpic::PaintSavedMessages(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		QBrush bg,
		const style::color &fg) {
	x = style::RightToLeft() ? (outerWidth - x - size) : x;

	PainterHighQualityEnabler hq(p);
	p.setBrush(std::move(bg));
	p.setPen(Qt::NoPen);
	AyuUserpic::PaintShape(p, x, y, size);

	PaintSavedMessagesInner(p, x, y, size, fg);
}

QImage EmptyUserpic::GenerateSavedMessages(int size) {
	return Generate(size, [&](QPainter &p) {
		PaintSavedMessages(p, 0, 0, size, size);
	});
}

void EmptyUserpic::PaintRepliesMessages(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size) {
	const auto bg = SpecialUserpicBg(
		y,
		size,
		st::historyPeerSavedMessagesBg,
		st::historyPeerSavedMessagesBg2);
	const auto &fg = SpecialUserpicFg(st::historyPeerUserpicFg);
	PaintRepliesMessages(p, x, y, outerWidth, size, bg, fg);
}

void EmptyUserpic::PaintRepliesMessages(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		QBrush bg,
		const style::color &fg) {
	x = style::RightToLeft() ? (outerWidth - x - size) : x;

	PainterHighQualityEnabler hq(p);
	p.setBrush(bg);
	p.setPen(Qt::NoPen);
	AyuUserpic::PaintShape(p, x, y, size);

	PaintRepliesMessagesInner(p, x, y, size, fg);
}

QImage EmptyUserpic::GenerateRepliesMessages(int size) {
	return Generate(size, [&](QPainter &p) {
		PaintRepliesMessages(p, 0, 0, size, size);
	});
}

void EmptyUserpic::PaintHiddenAuthor(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size) {
	const auto bg = SpecialUserpicBg(
		y,
		size,
		st::premiumButtonBg2,
		st::premiumButtonBg3);
	const auto &fg = SpecialUserpicFg(st::premiumButtonFg);
	PaintHiddenAuthor(p, x, y, outerWidth, size, bg, fg);
}

void EmptyUserpic::PaintHiddenAuthor(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		QBrush bg,
		const style::color &fg) {
	x = style::RightToLeft() ? (outerWidth - x - size) : x;

	PainterHighQualityEnabler hq(p);
	p.setBrush(bg);
	p.setPen(Qt::NoPen);
	AyuUserpic::PaintShape(p, x, y, size);

	PaintHiddenAuthorInner(p, x, y, size, fg);
}

QImage EmptyUserpic::GenerateHiddenAuthor(int size) {
	return Generate(size, [&](QPainter &p) {
		PaintHiddenAuthor(p, 0, 0, size, size);
	});
}

void EmptyUserpic::PaintMyNotes(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size) {
	const auto bg = SpecialUserpicBg(
		y,
		size,
		st::historyPeerSavedMessagesBg,
		st::historyPeerSavedMessagesBg2);
	const auto &fg = SpecialUserpicFg(st::historyPeerUserpicFg);
	PaintMyNotes(p, x, y, outerWidth, size, bg, fg);
}

void EmptyUserpic::PaintMyNotes(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		QBrush bg,
		const style::color &fg) {
	x = style::RightToLeft() ? (outerWidth - x - size) : x;

	PainterHighQualityEnabler hq(p);
	p.setBrush(bg);
	p.setPen(Qt::NoPen);
	AyuUserpic::PaintShape(p, x, y, size);

	PaintMyNotesInner(p, x, y, size, fg);
}

QImage EmptyUserpic::GenerateMyNotes(int size) {
	return Generate(size, [&](QPainter &p) {
		PaintMyNotes(p, 0, 0, size, size);
	});
}

void EmptyUserpic::PaintCurrency(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size) {
	auto bg = QLinearGradient(x, y, x, y + size);
	bg.setStops({
		{ 0., st::historyPeerSavedMessagesBg->c },
		{ 1., st::historyPeerSavedMessagesBg2->c }
	});
	const auto &fg = st::historyPeerUserpicFg;
	PaintCurrency(p, x, y, outerWidth, size, QBrush(bg), fg);
}

void EmptyUserpic::PaintCurrency(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		QBrush bg,
		const style::color &fg) {
	x = style::RightToLeft() ? (outerWidth - x - size) : x;

	PainterHighQualityEnabler hq(p);
	p.setBrush(bg);
	p.setPen(Qt::NoPen);
	p.drawEllipse(x, y, size, size);

	PaintCurrencyInner(p, x, y, size, fg);
}

QImage EmptyUserpic::GenerateCurrency(int size) {
	return Generate(size, [&](QPainter &p) {
		PaintCurrency(p, 0, 0, size, size);
	});
}

std::pair<uint64, uint64> EmptyUserpic::uniqueKey() const {
	const auto first = (uint64(0xFFFFFFFFU) << 32)
		| anim::getPremultiplied(_colors.color1->c);
	auto second = uint64(0);
	memcpy(
		&second,
		_string.constData(),
		std::min(sizeof(second), size_t(_string.size()) * sizeof(QChar)));
	return { first, second };
}

QPixmap EmptyUserpic::generate(int size) {
	auto result = QImage(
		QSize(size, size) * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	result.fill(Qt::transparent);
	{
		auto p = QPainter(&result);
		paintCircle(p, 0, 0, size, size);
	}
	return Ui::PixmapFromImage(std::move(result));
}

void EmptyUserpic::fillString(const QString &name) {
	if (IsExternal(name) || IsInaccessible(name)) {
		_string = name;
		return;
	}
	QList<QString> letters;
	QList<int> levels;

	auto level = 0;
	auto letterFound = false;
	auto ch = name.constData(), end = ch + name.size();
	while (ch != end) {
		auto emojiLength = 0;
		if (Ui::Emoji::Find(ch, end, &emojiLength)) {
			ch += emojiLength;
		} else if (ch->isHighSurrogate()) {
			++ch;
			if (ch != end && ch->isLowSurrogate()) {
				++ch;
			}
		} else if (!letterFound && ch->isLetterOrNumber()) {
			letterFound = true;
			if (ch + 1 != end && Ui::Text::IsDiacritic(*(ch + 1))) {
				letters.push_back(QString(ch, 2));
				levels.push_back(level);
				++ch;
			} else {
				letters.push_back(QString(ch, 1));
				levels.push_back(level);
			}
			++ch;
		} else {
			if (*ch == ' ') {
				level = 0;
				letterFound = false;
			} else if (letterFound && *ch == '-') {
				level = 1;
				letterFound = true;
			}
			++ch;
		}
	}

	// We prefer the second letter to be after ' ', but it can also be after '-'.
	_string = QString();
	if (!letters.isEmpty()) {
		_string += letters.front();
		auto bestIndex = 0;
		auto bestLevel = 2;
		for (auto i = letters.size(); i != 1;) {
			if (levels[--i] < bestLevel) {
				bestIndex = i;
				bestLevel = levels[i];
			}
		}
		if (bestIndex > 0) {
			_string += letters[bestIndex];
		}
	}
	_string = _string.toUpper();
}

EmptyUserpic::~EmptyUserpic() = default;

void PaintMonoforumShape(QPainter &p, QRect rect) {
	p.drawEllipse(rect);

	auto path = QPainterPath();
	path.moveTo(
		rect.x() + rect.width() * 0.5,
		rect.y() + rect.height() * 0.5);
	path.arcTo(
		QRectF(
			rect.x() - rect.width() * 0.5,
			rect.y(),
			rect.width(),
			rect.height()),
		0,
		-90);
	path.arcTo(
		QRectF(
			rect.x() - rect.width() * 0.25,
			rect.y() - rect.height() * 2,
			rect.width() * 0.5,
			rect.height() * 3),
		-90,
		45);
	path.lineTo(
		rect.x() + rect.width() * 0.5,
		rect.y() + rect.height() * 0.5);
	p.drawPath(path);
}

QImage MonoforumShapeMask(QSize size) {
	auto result = QImage(size, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);

	QPainter p(&result);
	PainterHighQualityEnabler hq(p);
	p.setBrush(Qt::white);
	p.setPen(Qt::NoPen);

	PaintMonoforumShape(p, QRect(QPoint(), size));

	p.end();

	return result;
}

const QImage &MonoforumShapeMaskCached(QSize size) {
	const auto key = (uint64(uint32(size.width())) << 32)
		| uint64(uint32(size.height()));

	static auto Masks = base::flat_map<uint64, QImage>();
	static auto Mutex = QMutex();
	auto lock = QMutexLocker(&Mutex);
	const auto i = Masks.find(key);
	if (i != end(Masks)) {
		return i->second;
	}
	lock.unlock();

	auto mask = MonoforumShapeMask(size);

	lock.relock();
	return Masks.emplace(key, std::move(mask)).first->second;
}

QImage ApplyMonoforumShape(QImage image) {
	const auto size = image.size();
	auto mask = MonoforumShapeMaskCached(size);

	constexpr auto format = QImage::Format_ARGB32_Premultiplied;
	if (image.format() != format) {
		image = std::move(image).convertToFormat(format);
	}
	auto p = QPainter(&image);
	p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
	p.drawImage(
		QRect(QPoint(), image.size() / image.devicePixelRatio()),
		mask);
	p.end();

	return image;
}

} // namespace Ui
