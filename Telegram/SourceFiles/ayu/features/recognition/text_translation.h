// This is the source code of freshGram.
#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

#include <atomic>
#include <memory>
#include <vector>

namespace AyuRecognition {

struct TextPart {
	QString text;
	bool translate = false;
};

// Sentences to translate and the whitespace between them, in order.
[[nodiscard]] std::vector<TextPart> SplitForTranslation(const QString &text);

struct TranslationModel {
	QString path;
	QString sha256;
};

struct TranslationJob {
	// One model, or two when the translation goes through English.
	std::vector<TranslationModel> chain;
	QStringList texts;
};

struct TranslationResult {
	bool ok = false;
	bool cancelled = false;
	QStringList texts;
	QString error;
};

// The engine is compiled in.
[[nodiscard]] bool TextTranslationAvailable();

// The callback is called in the main thread, set the flag to cancel.
std::shared_ptr<std::atomic<bool>> TranslateTextsAsync(
	TranslationJob job,
	Fn<void(TranslationResult)> done);

} // namespace AyuRecognition
