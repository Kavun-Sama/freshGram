/*
 * This is the source code of AyuGram for Desktop.
 *
 * We do not and cannot prevent the use of our code,
 * but be respectful and credit the original author.
 *
 * Copyright @Radolyn, 2025
 */
#include "ayu/features/translator/compose_translate.h"

#include "ayu/ayu_settings.h"
#include "ayu/secret/secret_peer.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "lang/translate_provider.h"
#include "main/main_session.h"
#include "spellcheck/spellcheck_types.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/show.h"
#include "ui/toast/toast.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "ayu/ui/settings/settings_chats.h"

namespace Ayu {

ComposeTranslator::ComposeTranslator() = default;

ComposeTranslator::~ComposeTranslator() = default;

void OpenTranslateSettings(not_null<Window::SessionController*> controller) {
	controller->window().setHighlightControlId(
		u"ayu/showTranslateButtonInMessageField"_q);
	controller->showSettings(Settings::AyuChatsId());
}

bool ComposeTranslator::busy() const {
	return _busy;
}

void ComposeTranslator::start(
		std::shared_ptr<Ui::Show> show,
		not_null<PeerData*> peer,
		TextWithEntities text,
		Fn<void(std::optional<TextWithEntities>)> done) {
	if (_busy) {
		return;
	}
	if (text.text.trimmed().isEmpty()) {
		show->showToast(tr::ayu_TranslateComposeEmpty(tr::now));
		return;
	}
	if (!AyuSecret::IsSecretPeerId(peer->id)) {
		request(std::move(show), peer, std::move(text), std::move(done));
		return;
	}
	// Secret chats: the text is sent to the provider's server, so ask first.
	const auto weak = base::make_weak(this);
	const auto providerIdentity = Ui::TranslateProviderIdentity();
	show->showBox(Ui::MakeConfirmBox({
		.text = tr::ayu_SecretTranslateWarning(
			tr::now,
			lt_provider,
			Ui::TranslateProviderName()),
		.confirmed = [=, text = std::move(text), done = std::move(done)](
				Fn<void()> close) mutable {
			close();
			const auto strong = weak.get();
			if (!strong
				|| strong->_busy
				|| Ui::TranslateProviderIdentity() != providerIdentity) {
				return;
			}
			strong->request(show, peer, std::move(text), std::move(done));
		},
		.confirmText = tr::lng_context_translate(tr::now),
	}));
}

void ComposeTranslator::request(
		std::shared_ptr<Ui::Show> show,
		not_null<PeerData*> peer,
		TextWithEntities text,
		Fn<void(std::optional<TextWithEntities>)> done) {
	const auto identity = Ui::TranslateProviderIdentity();
	if (!_provider || _providerIdentity != identity) {
		_provider = Ui::CreateTranslateProvider(&peer->session());
		_providerIdentity = identity;
	}
	if (!_provider) {
		show->showToast(tr::ayu_TranslateComposeFailed(tr::now));
		return;
	}
	_busy = true;
	const auto id = ++_requestId;
	const auto weak = base::make_weak(this);
	const auto progress = show->showToast(Ui::Toast::Config{
		.text = TextWithEntities{
			tr::ayu_TranslateComposeProgress(tr::now),
		},
		.infinite = true,
	});
	_provider->request(
		Ui::TranslateProviderRequest{ .text = std::move(text) },
		LanguageId::FromName(AyuSettings::getInstance().translateComposeTo()),
		[=](Ui::TranslateProviderResult result) {
			const auto strong = weak.get();
			if (!strong || strong->_requestId != id) {
				return;
			}
			strong->_busy = false;
			if (const auto toast = progress.get()) {
				toast->hide();
			}
			if (result.error != Ui::TranslateProviderError::None
				|| !result.text
				|| result.text->text.trimmed().isEmpty()) {
				show->showToast(tr::ayu_TranslateComposeFailed(tr::now));
				done(std::nullopt);
				return;
			}
			show->showToast(tr::ayu_TranslateComposeDone(tr::now));
			done(std::move(result.text));
		});
}

} // namespace Ayu
