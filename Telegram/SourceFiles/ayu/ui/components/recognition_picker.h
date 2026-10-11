// This is the source code of freshGram.
//
// The area picker over a photo in the media viewer: drag the corners,
// press "Recognize" and the recognized text is shown over the photo.
#pragma once

#include "ui/rp_widget.h"

#include <QtGui/QImage>

#include <atomic>
#include <memory>

namespace Ui {
class RoundButton;
class ScrollArea;
class FlatLabel;
} // namespace Ui

namespace AyuUi {

class RecognitionPicker final : public Ui::RpWidget {
public:
	// The image is what is shown in the viewer (already rotated), the
	// callback returns the rectangle it occupies in the parent coordinates.
	RecognitionPicker(
		not_null<Ui::RpWidget*> parent,
		QImage image,
		Fn<QRect()> contentRect,
		QString languages,
		Fn<void()> closeRequested);
	~RecognitionPicker();

	void cancelOrClose();
	void refreshLayout();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	enum class State {
		Selecting,
		Working,
		Result,
	};
	enum class Part {
		None,
		Move,
		TopLeft,
		Top,
		TopRight,
		Right,
		BottomRight,
		Bottom,
		BottomLeft,
		Left,
	};

	[[nodiscard]] QRectF selectionRect() const;
	void setSelectionRect(QRectF rect);
	[[nodiscard]] Part partAt(QPoint position) const;
	void updateCursor(Part part);
	void updateControls();
	void recognize();
	void showText(const QString &text, bool copyable);

	const QImage _image;
	const Fn<QRect()> _contentRect;
	const QString _languages;
	const Fn<void()> _closeRequested;

	State _state = State::Selecting;
	// Relative to the content rectangle, from 0 to 1.
	QRectF _selection = QRectF(0.05, 0.05, 0.9, 0.9);
	Part _dragging = Part::None;
	QPoint _dragStart;
	QRectF _dragStartRect;
	QString _status;
	QString _resultText;

	std::shared_ptr<std::atomic<bool>> _cancel;
	int _generation = 0;

	Ui::RoundButton *_recognize = nullptr;
	Ui::RoundButton *_cancelButton = nullptr;
	Ui::RpWidget *_panel = nullptr;
	Ui::ScrollArea *_scroll = nullptr;
	Ui::FlatLabel *_text = nullptr;
	Ui::RoundButton *_copy = nullptr;
	Ui::RoundButton *_close = nullptr;

};

} // namespace AyuUi
