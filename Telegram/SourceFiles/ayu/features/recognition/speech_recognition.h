// This is the source code of freshGram.
//
// Local speech recognition: voice messages, round videos and music are
// transcribed on this device with whisper.cpp.
#pragma once

#include <QtCore/QString>

#include <atomic>
#include <memory>

namespace AyuRecognition {

struct SpeechRequest {
	QString audioPath;
	QString modelPath;
	QString modelSha256;
	QString language; // Empty for the automatic detection.
};

struct SpeechResult {
	bool ok = false;
	bool cancelled = false;
	QString text;
	QString language;
	QString error;
};

// The engine is compiled in and the processor can run it.
[[nodiscard]] bool SpeechRecognitionAvailable();

// Runs in a background thread. Both callbacks are called in the main
// thread, set the returned flag to cancel the work.
std::shared_ptr<std::atomic<bool>> TranscribeAsync(
	SpeechRequest request,
	Fn<void(int percent)> progress,
	Fn<void(SpeechResult)> done);

} // namespace AyuRecognition
