// This is the source code of freshGram.
#include "ayu/features/translator/local_translate_provider.h"

#include "ayu/features/recognition/recognition_models.h"
#include "ayu/features/recognition/text_translation.h"
#include "base/weak_ptr.h"
#include "spellcheck/platform/platform_language.h"

#include <QtCore/QRegularExpression>

#include <map>

namespace Ayu::Translator {
namespace {

using namespace AyuRecognition;

[[nodiscard]] bool IsKeptEntity(EntityType type) {
	switch (type) {
	case EntityType::Url:
	case EntityType::Email:
	case EntityType::Hashtag:
	case EntityType::Cashtag:
	case EntityType::Mention:
	case EntityType::MentionName:
	case EntityType::CustomEmoji:
	case EntityType::BotCommand:
	case EntityType::Phone:
	case EntityType::BankCard:
	case EntityType::Code:
	case EntityType::Pre:
		return true;
	}
	return false;
}

[[nodiscard]] QString Placeholder(int index) {
	return u"~%1~"_q.arg(index + 1);
}

struct Masked {
	QString text;
	std::vector<EntityInText> kept;
};

[[nodiscard]] Masked Mask(const TextWithEntities &source) {
	auto kept = std::vector<EntityInText>();
	for (const auto &entity : source.entities) {
		if (IsKeptEntity(entity.type())
			&& entity.length() > 0
			&& entity.offset() >= 0
			&& entity.offset() + entity.length() <= source.text.size()) {
			kept.push_back(entity);
		}
	}
	ranges::sort(kept, ranges::less(), &EntityInText::offset);
	auto result = Masked();
	auto from = 0;
	for (const auto &entity : kept) {
		if (entity.offset() < from) {
			continue;
		}
		result.text += source.text.mid(from, entity.offset() - from);
		result.text += Placeholder(int(result.kept.size()));
		result.kept.push_back(entity);
		from = entity.offset() + entity.length();
	}
	result.text += source.text.mid(from);
	return result;
}

[[nodiscard]] TextWithEntities Restore(
		const QString &translated,
		const QString &original,
		const std::vector<EntityInText> &kept) {
	static const auto pattern = QRegularExpression(u"~\\s*(\\d+)\\s*~"_q);
	auto result = TextWithEntities();
	auto used = std::vector<bool>(kept.size(), false);
	const auto append = [&](int index) {
		const auto &entity = kept[index];
		result.entities.push_back(EntityInText(
			entity.type(),
			int(result.text.size()),
			entity.length(),
			entity.data()));
		result.text += original.mid(entity.offset(), entity.length());
		used[index] = true;
	};
	auto from = 0;
	auto i = pattern.globalMatch(translated);
	while (i.hasNext()) {
		const auto match = i.next();
		const auto index = match.captured(1).toInt() - 1;
		if (index < 0 || index >= int(kept.size()) || used[index]) {
			continue;
		}
		result.text += translated.mid(from, match.capturedStart() - from);
		append(index);
		from = match.capturedEnd();
	}
	result.text += translated.mid(from);
	for (auto index = 0; index != int(kept.size()); ++index) {
		if (!used[index]) {
			result.text += ' ';
			append(index);
		}
	}
	return result;
}

struct Direction {
	std::vector<Model> chain;
	bool missing = false;
};

[[nodiscard]] Direction ChooseDirection(
		const std::vector<Model> &installed,
		const QString &from,
		const QString &to) {
	const auto find = [&](const QString &code) -> const Model* {
		for (const auto &model : installed) {
			if (model.code == code) {
				return &model;
			}
		}
		return nullptr;
	};
	auto result = Direction();
	if (from.isEmpty()) {
		const Model *only = nullptr;
		for (const auto &model : installed) {
			if (model.code.endsWith('-' + to)) {
				if (only) {
					result.missing = true;
					return result;
				}
				only = &model;
			}
		}
		if (only) {
			result.chain.push_back(*only);
		} else {
			result.missing = true;
		}
		return result;
	}
	if (const auto direct = find(from + '-' + to)) {
		result.chain.push_back(*direct);
		return result;
	}
	const auto first = find(from + u"-en"_q);
	const auto second = find(u"en-"_q + to);
	if (first && second && from != u"en"_q && to != u"en"_q) {
		result.chain.push_back(*first);
		result.chain.push_back(*second);
	} else {
		result.missing = true;
	}
	return result;
}

class Provider final
	: public Ui::TranslateProvider
	, public base::has_weak_ptr {
public:
	~Provider() {
		for (const auto &flag : _flags) {
			flag->store(true);
		}
	}

	[[nodiscard]] bool supportsMessageId() const override {
		return false;
	}

	void request(
			Ui::TranslateProviderRequest request,
			LanguageId to,
			Fn<void(Ui::TranslateProviderResult)> done) override {
		requestBatch(
			{ std::move(request) },
			to,
			[done = std::move(done)](
					int,
					Ui::TranslateProviderResult result) {
				done(std::move(result));
			},
			[] {});
	}

	void requestBatch(
			std::vector<Ui::TranslateProviderRequest> requests,
			const LanguageId &to,
			Fn<void(int, Ui::TranslateProviderResult)> doneOne,
			Fn<void()> doneAll) override {
		if (requests.empty()) {
			doneAll();
			return;
		}
		struct Group {
			TranslationJob job;
			std::vector<int> indices;
		};
		struct State {
			std::vector<Masked> masked;
			std::vector<TextWithEntities> sources;
			int remaining = 0;
			Fn<void(int, Ui::TranslateProviderResult)> doneOne;
			Fn<void()> doneAll;
		};
		const auto fail = [](Ui::TranslateProviderError error) {
			return Ui::TranslateProviderResult{ .error = error };
		};
		auto state = std::make_shared<State>();
		state->doneOne = std::move(doneOne);
		state->doneAll = std::move(doneAll);
		const auto weak = base::make_weak(this);
		const auto finish = [=](int index, Ui::TranslateProviderResult result) {
			if (!weak) {
				return;
			}
			state->doneOne(index, std::move(result));
			if (!--state->remaining) {
				state->doneAll();
			}
		};
		const auto target = to.twoLetterCode();
		const auto installed = InstalledTranslateModels();
		auto groups = std::map<QString, Group>();
		state->remaining = int(requests.size());
		state->masked.resize(requests.size());
		state->sources.resize(requests.size());
		auto immediate = std::vector<std::pair<int, Ui::TranslateProviderResult>>();
		for (auto i = 0; i != int(requests.size()); ++i) {
			auto &source = state->sources[i];
			source = std::move(requests[i].text);
			auto from = QString();
#ifndef TDESKTOP_DISABLE_SPELLCHECK
			const auto detected = Platform::Language::Recognize(source.text);
			if (detected.known()) {
				from = detected.twoLetterCode();
			}
#endif // !TDESKTOP_DISABLE_SPELLCHECK
			if (!from.isEmpty() && from == target) {
				immediate.emplace_back(
					i,
					Ui::TranslateProviderResult{ .text = source });
				continue;
			}
			const auto direction = ChooseDirection(installed, from, target);
			if (direction.missing) {
				immediate.emplace_back(
					i,
					fail(Ui::TranslateProviderError::LocalLanguagePackMissing));
				continue;
			}
			state->masked[i] = Mask(source);
			auto key = QString();
			for (const auto &model : direction.chain) {
				key += model.code + ';';
			}
			auto &group = groups[key];
			if (group.job.chain.empty()) {
				for (const auto &model : direction.chain) {
					group.job.chain.push_back({
						.path = ModelPath(model),
						.sha256 = model.sha256,
					});
				}
			}
			group.job.texts.push_back(state->masked[i].text);
			group.indices.push_back(i);
		}
		for (auto &entry : immediate) {
			crl::on_main(weak, [
				=,
				index = entry.first,
				result = std::move(entry.second)
			]() mutable {
				finish(index, std::move(result));
			});
		}
		for (auto &entry : groups) {
			auto &group = entry.second;
			auto flag = TranslateTextsAsync(
				std::move(group.job),
				[=, indices = std::move(group.indices)](
						TranslationResult result) {
					if (!weak) {
						return;
					}
					for (auto k = 0; k != int(indices.size()); ++k) {
						const auto index = indices[k];
						if (!result.ok || k >= result.texts.size()) {
							finish(
								index,
								fail(Ui::TranslateProviderError::Unknown));
							continue;
						}
						finish(index, Ui::TranslateProviderResult{
							.text = Restore(
								result.texts[k],
								state->sources[index].text,
								state->masked[index].kept),
						});
					}
				});
			_flags.push_back(std::move(flag));
		}
	}

private:
	std::vector<std::shared_ptr<std::atomic<bool>>> _flags;

};

} // namespace

std::unique_ptr<Ui::TranslateProvider> CreateLocalTranslateProvider() {
	return std::make_unique<Provider>();
}

} // namespace Ayu::Translator
