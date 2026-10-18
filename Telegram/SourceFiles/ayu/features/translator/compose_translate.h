/*
 * This is the source code of AyuGram for Desktop.
 *
 * We do not and cannot prevent the use of our code,
 * but be respectful and credit the original author.
 *
 * Copyright @Radolyn, 2025
 */
#pragma once

#include "base/weak_ptr.h"
#include "ui/text/text_entity.h"

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

class ComposeTranslator final : public base::has_weak_ptr {
public:
	ComposeTranslator();
	~ComposeTranslator();

	[[nodiscard]] bool busy() const;

	// Calls done with the translated text, or with nothing on failure.
	void start(
		std::shared_ptr<Ui::Show> show,
		not_null<PeerData*> peer,
		TextWithEntities text,
		Fn<void(std::optional<TextWithEntities>)> done);

private:
	void request(
		std::shared_ptr<Ui::Show> show,
		not_null<PeerData*> peer,
		TextWithEntities text,
		Fn<void(std::optional<TextWithEntities>)> done);

	std::unique_ptr<Ui::TranslateProvider> _provider;
	QString _providerIdentity;
	int _requestId = 0;
	bool _busy = false;

};

} // namespace Ayu
