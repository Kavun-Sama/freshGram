/*
 * This is the source code of AyuGram for Desktop.
 *
 * We do not and cannot prevent the use of our code,
 * but be respectful and credit the original author.
 *
 * Copyright @Radolyn, 2025
 */
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "ui/text/text_entity.h"
#include "ui/toast/toast.h"

class PeerData;

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class Show;
class TranslateProvider;
} // namespace Ui

namespace Ayu {

// Opens the settings of the translate button of the message field.
void OpenTranslateSettings(not_null<Window::SessionController*> controller);

[[nodiscard]] bool WorthTranslating(const TextWithEntities &text);

class ComposeTranslator final : public base::has_weak_ptr {
public:
	ComposeTranslator();
	~ComposeTranslator();

	[[nodiscard]] bool busy() const;

	void cancel();

	// Calls done with the translated text, or with nothing on failure.
	void start(
		std::shared_ptr<Ui::Show> show,
		not_null<PeerData*> peer,
		TextWithEntities text,
		Fn<void(std::optional<TextWithEntities>)> done,
		bool silent = false);

	void remember(const QString &translated);
	void textChanged(const QString &text);
	[[nodiscard]] bool isRemembered(const QString &text) const;

private:
	void request(
		std::shared_ptr<Ui::Show> show,
		not_null<PeerData*> peer,
		TextWithEntities text,
		Fn<void(std::optional<TextWithEntities>)> done,
		bool silent);
	void finish();

	std::unique_ptr<Ui::TranslateProvider> _provider;
	QString _providerIdentity;
	QString _remembered;
	base::weak_ptr<Ui::Toast::Instance> _progress;
	base::Timer _timeout;
	base::Timer _progressDelay;
	int _requestId = 0;
	bool _busy = false;

};

} // namespace Ayu
