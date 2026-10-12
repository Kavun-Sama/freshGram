// This is the source code of freshGram.
//
// Local text recognition on photos with Tesseract.
#pragma once

#include <QtCore/QHash>
#include <QtGui/QImage>

#include <atomic>
#include <memory>

namespace AyuRecognition {

struct TextRequest {
	QImage image;
	// "auto" or the languages joined with '+', for example "rus+eng".
	QString languages;
	QStringList installed;
	QHash<QString, QString> hashes;
};

struct TextResult {
	bool ok = false;
	bool cancelled = false;
	QString text;
	QString languages; // What was really used.
	QString error;
};

// The engine is compiled in.
[[nodiscard]] bool TextRecognitionAvailable();

// Runs in a background thread. The callback is called in the main thread,
// set the returned flag to cancel the work.
std::shared_ptr<std::atomic<bool>> RecognizeTextAsync(
	TextRequest request,
	Fn<void(TextResult)> done);

} // namespace AyuRecognition
