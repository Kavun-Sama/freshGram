// This is the source code of freshGram.
#include "ayu/features/recognition/text_translation.h"

#include "ayu/features/recognition/recognition_models.h"
#include "base/timer.h"
#include "logs.h"

#ifdef FRESHGRAM_LOCAL_TRANSLATE
#include <ctranslate2/logging.h>
#include <ctranslate2/models/model_reader.h>
#include <ctranslate2/translator.h>
#include <sentencepiece_processor.h>
#endif // FRESHGRAM_LOCAL_TRANSLATE

#include <QtCore/QDataStream>
#include <QtCore/QFile>
#include <QtCore/QThread>

#include <algorithm>
#include <map>
#include <mutex>

namespace AyuRecognition {
namespace {

constexpr auto kMaxSentence = 500;

[[nodiscard]] bool IsTerminator(QChar c) {
	switch (c.unicode()) {
	case '.':
	case '!':
	case '?':
	case 0x2026:
	case 0x3002:
	case 0xFF01:
	case 0xFF1F:
		return true;
	}
	return false;
}

[[nodiscard]] bool IsWideTerminator(QChar c) {
	return (c.unicode() == 0x3002)
		|| (c.unicode() == 0xFF01)
		|| (c.unicode() == 0xFF1F);
}

[[nodiscard]] bool IsCloser(QChar c) {
	switch (c.unicode()) {
	case '"':
	case '\'':
	case ')':
	case ']':
	case '}':
	case 0xBB:
	case 0x2019:
	case 0x201D:
	case 0x300D:
		return true;
	}
	return false;
}

[[nodiscard]] bool IsLineBreak(QChar c) {
	return (c == '\n')
		|| (c == '\r')
		|| (c == QChar::LineSeparator)
		|| (c == QChar::ParagraphSeparator);
}

[[nodiscard]] bool HasLetter(const QString &text) {
	return std::any_of(text.begin(), text.end(), [](QChar c) {
		return c.isLetter();
	});
}

} // namespace

std::vector<TextPart> SplitForTranslation(const QString &text) {
	auto result = std::vector<TextPart>();
	const auto size = int(text.size());
	auto i = 0;
	while (i < size) {
		auto from = i;
		while (i < size && text[i].isSpace()) {
			++i;
		}
		if (i > from) {
			result.push_back({ text.mid(from, i - from), false });
		}
		if (i >= size) {
			break;
		}
		from = i;
		while (i < size) {
			const auto c = text[i++];
			if (IsLineBreak(c)) {
				--i;
				break;
			} else if (IsTerminator(c)) {
				while (i < size && (IsTerminator(text[i]) || IsCloser(text[i]))) {
					++i;
				}
				if (i >= size || text[i].isSpace() || IsWideTerminator(c)) {
					break;
				}
			} else if (c.isSpace() && (i - from) >= kMaxSentence) {
				break;
			}
		}
		auto till = i;
		while (till > from && text[till - 1].isSpace()) {
			--till;
		}
		i = till;
		const auto sentence = text.mid(from, till - from);
		result.push_back({ sentence, HasLetter(sentence) });
	}
	return result;
}

#ifdef FRESHGRAM_LOCAL_TRANSLATE

namespace {

constexpr auto kContainerMagic = "FGTMODEL";
constexpr auto kMagicSize = 8;
constexpr auto kMaxSections = 16;
constexpr auto kMaxNameSize = 64;
constexpr auto kMaxSectionSize = qint64(400) * 1024 * 1024;
constexpr auto kMaxLoaded = 2;
constexpr auto kIdleRelease = 3 * 60 * crl::time(1000);

struct Section {
	qint64 offset = 0;
	qint64 size = 0;
};

using Sections = std::map<QString, Section>;

[[nodiscard]] bool IsKnownSection(const QString &name) {
	static const auto names = QStringList{
		u"model.bin"_q,
		u"config.json"_q,
		u"shared_vocabulary.json"_q,
		u"shared_vocabulary.txt"_q,
		u"source_vocabulary.json"_q,
		u"source_vocabulary.txt"_q,
		u"target_vocabulary.json"_q,
		u"target_vocabulary.txt"_q,
		u"source.spm"_q,
		u"target.spm"_q,
	};
	return names.contains(name);
}

// Magic, a count, then the name and the size of each file, then the data.
[[nodiscard]] std::optional<Sections> ReadSections(QFile &file) {
	auto magic = QByteArray(kMagicSize, Qt::Uninitialized);
	if (file.read(magic.data(), kMagicSize) != kMagicSize
		|| magic != QByteArray(kContainerMagic, kMagicSize)) {
		return std::nullopt;
	}
	auto stream = QDataStream(&file);
	stream.setByteOrder(QDataStream::LittleEndian);
	auto count = quint32();
	stream >> count;
	if (stream.status() != QDataStream::Ok
		|| !count
		|| count > kMaxSections) {
		return std::nullopt;
	}
	auto names = std::vector<std::pair<QString, qint64>>();
	for (auto i = quint32(); i != count; ++i) {
		auto length = quint16();
		stream >> length;
		if (stream.status() != QDataStream::Ok
			|| !length
			|| length > kMaxNameSize) {
			return std::nullopt;
		}
		auto name = QByteArray(length, Qt::Uninitialized);
		auto size = quint64();
		if (stream.readRawData(name.data(), length) != length) {
			return std::nullopt;
		}
		stream >> size;
		if (stream.status() != QDataStream::Ok
			|| size > quint64(kMaxSectionSize)) {
			return std::nullopt;
		}
		names.emplace_back(QString::fromLatin1(name), qint64(size));
	}
	auto result = Sections();
	auto offset = file.pos();
	for (const auto &[name, size] : names) {
		if (!IsKnownSection(name) || result.contains(name)) {
			return std::nullopt;
		}
		result.emplace(name, Section{ offset, size });
		offset += size;
	}
	if (offset != file.size()) {
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] std::optional<std::string> ReadSection(
		QFile &file,
		const Sections &sections,
		const QString &name) {
	const auto i = sections.find(name);
	if (i == end(sections)) {
		return std::nullopt;
	}
	auto result = std::string(size_t(i->second.size), '\0');
	if (!file.seek(i->second.offset)
		|| file.read(result.data(), i->second.size) != i->second.size) {
		return std::nullopt;
	}
	return result;
}

class ViewBuffer final : public std::streambuf {
public:
	explicit ViewBuffer(const std::string &data) {
		const auto begin = const_cast<char*>(data.data());
		setg(begin, begin, begin + data.size());
	}

protected:
	pos_type seekoff(
			off_type offset,
			std::ios_base::seekdir direction,
			std::ios_base::openmode which) override {
		if (!(which & std::ios_base::in)) {
			return pos_type(off_type(-1));
		}
		auto target = offset;
		if (direction == std::ios_base::cur) {
			target += (gptr() - eback());
		} else if (direction == std::ios_base::end) {
			target += (egptr() - eback());
		}
		if (target < 0 || target > (egptr() - eback())) {
			return pos_type(off_type(-1));
		}
		setg(eback(), eback() + target, egptr());
		return pos_type(target);
	}

	pos_type seekpos(
			pos_type position,
			std::ios_base::openmode which) override {
		return seekoff(off_type(position), std::ios_base::beg, which);
	}

};

class ViewStream final : public std::istream {
public:
	explicit ViewStream(const std::string &data)
	: std::istream(nullptr)
	, _buffer(data) {
		rdbuf(&_buffer);
	}

private:
	ViewBuffer _buffer;

};

class ContainerReader final : public ctranslate2::models::ModelReader {
public:
	explicit ContainerReader(std::string id) : _id(std::move(id)) {
	}

	void add(std::string name, std::string content) {
		_files.emplace(std::move(name), std::move(content));
	}

	std::string get_model_id() const override {
		return _id;
	}

	std::unique_ptr<std::istream> get_file(
			const std::string &filename,
			const bool binary) override {
		const auto i = _files.find(filename);
		return (i == end(_files))
			? nullptr
			: std::make_unique<ViewStream>(i->second);
	}

private:
	std::string _id;
	std::map<std::string, std::string> _files;

};

struct Loaded {
	QString key;
	std::unique_ptr<ctranslate2::Translator> translator;
	sentencepiece::SentencePieceProcessor source;
	sentencepiece::SentencePieceProcessor target;
};

std::mutex CacheMutex;
std::vector<std::shared_ptr<Loaded>> Cache;
std::unique_ptr<base::Timer> IdleTimer;

void ReleaseModels() {
	const auto lock = std::lock_guard(CacheMutex);
	Cache.clear();
}

void ScheduleRelease() {
	if (!IdleTimer) {
		IdleTimer = std::make_unique<base::Timer>(ReleaseModels);
	}
	IdleTimer->callOnce(kIdleRelease);
}

[[nodiscard]] std::shared_ptr<Loaded> Load(
		const TranslationModel &model,
		QString &error) {
	const auto key = model.path + '|' + model.sha256;
	const auto lock = std::lock_guard(CacheMutex);
	for (auto i = begin(Cache); i != end(Cache); ++i) {
		if ((*i)->key == key) {
			auto result = *i;
			std::rotate(i, i + 1, end(Cache));
			return result;
		}
	}
	if (!ModelFileIntact(model.path, model.sha256)) {
		error = u"The model failed the integrity check."_q;
		return nullptr;
	}
	auto file = QFile(model.path);
	if (!file.open(QIODevice::ReadOnly)) {
		error = u"Could not open the model."_q;
		return nullptr;
	}
	const auto sections = ReadSections(file);
	if (!sections) {
		error = u"The model file is damaged."_q;
		return nullptr;
	}
	auto result = std::make_shared<Loaded>();
	result->key = key;
	auto spmSource = ReadSection(file, *sections, u"source.spm"_q);
	auto spmTarget = ReadSection(file, *sections, u"target.spm"_q);
	if (!spmSource || !spmTarget
		|| !result->source.LoadFromSerializedProto(*spmSource).ok()
		|| !result->target.LoadFromSerializedProto(*spmTarget).ok()) {
		error = u"Could not load the tokenizer."_q;
		return nullptr;
	}
	auto reader = std::make_shared<ContainerReader>(
		model.path.toStdString());
	for (const auto &[name, section] : *sections) {
		if (name == u"source.spm"_q || name == u"target.spm"_q) {
			continue;
		}
		auto content = ReadSection(file, *sections, name);
		if (!content) {
			error = u"The model file is damaged."_q;
			return nullptr;
		}
		reader->add(name.toStdString(), std::move(*content));
	}
	try {
		ctranslate2::set_log_level(ctranslate2::LogLevel::Off);
		auto loader = ctranslate2::models::ModelLoader(reader);
		loader.device = ctranslate2::Device::CPU;
		loader.compute_type = ctranslate2::ComputeType::DEFAULT;
		result->translator = std::make_unique<ctranslate2::Translator>(
			loader,
			ctranslate2::ReplicaPoolConfig{
				.num_threads_per_replica = size_t(
					std::clamp(QThread::idealThreadCount() / 2, 1, 4)),
			});
	} catch (const std::exception &e) {
		LOG(("Translation Error: could not load the model: %1"
			).arg(QString::fromUtf8(e.what())));
		error = u"Could not load the model."_q;
		return nullptr;
	}
	Cache.push_back(result);
	if (Cache.size() > kMaxLoaded) {
		Cache.erase(begin(Cache));
	}
	return result;
}

[[nodiscard]] std::vector<std::string> Translate(
		Loaded &loaded,
		const std::vector<std::string> &sentences) {
	auto result = sentences;
	auto indices = std::vector<size_t>();
	auto sources = std::vector<std::vector<std::string>>();
	for (auto i = size_t(); i != sentences.size(); ++i) {
		auto pieces = std::vector<std::string>();
		if (loaded.source.Encode(sentences[i], &pieces).ok()
			&& !pieces.empty()) {
			pieces.push_back("</s>");
			indices.push_back(i);
			sources.push_back(std::move(pieces));
		}
	}
	if (sources.empty()) {
		return result;
	}
	auto options = ctranslate2::TranslationOptions();
	options.beam_size = 4;
	options.max_decoding_length = 512;
	const auto translated = loaded.translator->translate_batch(
		sources,
		options,
		16);
	for (auto i = size_t(); i != indices.size(); ++i) {
		auto text = std::string();
		if (loaded.target.Decode(translated[i].output(), &text).ok()
			&& !text.empty()) {
			result[indices[i]] = std::move(text);
		}
	}
	return result;
}

[[nodiscard]] TranslationResult Run(
		const TranslationJob &job,
		const std::atomic<bool> &cancelled) {
	auto result = TranslationResult();
	if (job.chain.empty()) {
		result.error = u"No model."_q;
		return result;
	}
	auto models = std::vector<std::shared_ptr<Loaded>>();
	for (const auto &model : job.chain) {
		auto loaded = Load(model, result.error);
		if (!loaded) {
			return result;
		}
		models.push_back(std::move(loaded));
	}
	auto parts = std::vector<std::vector<TextPart>>();
	auto current = std::vector<std::string>();
	for (const auto &text : job.texts) {
		parts.push_back(SplitForTranslation(text));
		for (const auto &part : parts.back()) {
			if (part.translate) {
				current.push_back(part.text.toStdString());
			}
		}
	}
	try {
		for (const auto &loaded : models) {
			if (cancelled.load()) {
				result.cancelled = true;
				return result;
			}
			current = Translate(*loaded, current);
		}
	} catch (const std::exception &e) {
		LOG(("Translation Error: %1").arg(QString::fromUtf8(e.what())));
		result.error = u"The translation failed."_q;
		return result;
	}
	auto next = size_t();
	for (const auto &list : parts) {
		auto text = QString();
		for (const auto &part : list) {
			text += part.translate
				? QString::fromStdString(current[next++])
				: part.text;
		}
		result.texts.push_back(std::move(text));
	}
	result.ok = true;
	return result;
}

} // namespace

#endif // FRESHGRAM_LOCAL_TRANSLATE

bool TextTranslationAvailable() {
#ifdef FRESHGRAM_LOCAL_TRANSLATE
	return true;
#else // FRESHGRAM_LOCAL_TRANSLATE
	return false;
#endif // !FRESHGRAM_LOCAL_TRANSLATE
}

std::shared_ptr<std::atomic<bool>> TranslateTextsAsync(
		TranslationJob job,
		Fn<void(TranslationResult)> done) {
	auto cancelled = std::make_shared<std::atomic<bool>>(false);
#ifdef FRESHGRAM_LOCAL_TRANSLATE
	crl::async([=] {
		auto result = Run(job, *cancelled);
		crl::on_main([=, result = std::move(result)]() mutable {
			ScheduleRelease();
			done(std::move(result));
		});
	});
#else // FRESHGRAM_LOCAL_TRANSLATE
	crl::on_main([=] {
		auto result = TranslationResult();
		result.error = u"Not available in this build."_q;
		done(std::move(result));
	});
#endif // !FRESHGRAM_LOCAL_TRANSLATE
	return cancelled;
}

} // namespace AyuRecognition
