// This file is part of freshGram.
#include "ayu/features/fake_passcode/fake_passcode.h"

#include "ayu/data/messages_storage.h"
#include "base/random.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/version.h"
#include "data/data_session.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mtproto/mtproto_auth_key.h"
#include "mtproto/mtproto_proxy_data.h"
#include "storage/details/storage_file_utilities.h"
#include "storage/storage_domain.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>

namespace AyuFakePasscode {
namespace {

using namespace Storage::details;

constexpr auto kVersion = 2;
constexpr auto kSaltSize = 32;
constexpr auto kBlobPayload = 2048;
constexpr auto kMinUnlockLength = 6;
constexpr auto kMaxCommandLength = 256;
constexpr auto kLogoutTimeout = 10 * crl::time(1000);
constexpr auto kLogoutCheckEach = crl::time(300);

struct State {
	bool loaded = false;
	QByteArray salt;
	std::vector<QByteArray> blobs;
	Data data;
	int pendingId = 0;
};

struct Waiter {
	std::vector<base::weak_ptr<Main::Account>> accounts;
	Actions actions;
	crl::time started = 0;
};

[[nodiscard]] State &GetState() {
	static auto result = State();
	return result;
}

[[nodiscard]] Waiter &GetWaiter() {
	static auto result = Waiter();
	return result;
}

[[nodiscard]] base::Timer &GetWaitTimer() {
	static auto result = base::Timer();
	return result;
}

[[nodiscard]] QString BasePath() {
	return cWorkingDir() + u"tdata/"_q;
}

[[nodiscard]] QString FileName() {
	return u"dc_cache"_q;
}

[[nodiscard]] QString LegacyFileName() {
	return u"fake_passcodes"_q;
}

[[nodiscard]] MTP::AuthKeyPtr LocalKey() {
	return Core::App().domain().started()
		? Core::App().domain().local().localKey()
		: nullptr;
}

[[nodiscard]] QByteArray PackBlobs(const std::vector<QByteArray> &blobs) {
	auto result = QByteArray();
	auto stream = QDataStream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << qint32(blobs.size());
	for (const auto &blob : blobs) {
		stream << blob;
	}
	return result;
}

[[nodiscard]] bool UnpackBlobs(
		const QByteArray &packed,
		std::vector<QByteArray> &blobs) {
	auto stream = QDataStream(packed);
	stream.setVersion(QDataStream::Qt_5_1);
	auto count = qint32();
	stream >> count;
	if (stream.status() != QDataStream::Ok || count != kMaxEntries) {
		return false;
	}
	blobs.resize(count);
	for (auto &blob : blobs) {
		stream >> blob;
		if (blob.size() != blobs.front().size()) {
			return false;
		}
	}
	return stream.status() == QDataStream::Ok && !blobs.front().isEmpty();
}

void WriteActions(QDataStream &stream, const Actions &actions) {
	const auto flags = (actions.clearCache ? 1 : 0)
		| (actions.clearCacheDb ? 2 : 0)
		| (actions.clearCacheMedia ? 4 : 0)
		| (actions.clearProxies ? 8 : 0)
		| (actions.logoutDb ? 16 : 0)
		| (actions.logoutMedia ? 32 : 0)
		| (actions.deleteAll ? 64 : 0);
	stream
		<< qint32(flags)
		<< actions.command
		<< qint32(actions.logoutAccounts.size());
	for (const auto index : actions.logoutAccounts) {
		stream << qint32(index);
	}
}

[[nodiscard]] bool ReadActions(QDataStream &stream, Actions &actions) {
	auto flags = qint32();
	auto logouts = qint32();
	stream >> flags >> actions.command >> logouts;
	if (stream.status() != QDataStream::Ok
		|| logouts < 0
		|| logouts > Main::Domain::kPremiumMaxAccounts) {
		return false;
	}
	actions.clearCache = (flags & 1) != 0;
	actions.clearCacheDb = (flags & 2) != 0;
	actions.clearCacheMedia = (flags & 4) != 0;
	actions.clearProxies = (flags & 8) != 0;
	actions.logoutDb = (flags & 16) != 0;
	actions.logoutMedia = (flags & 32) != 0;
	actions.deleteAll = (flags & 64) != 0;
	for (auto i = 0; i != logouts; ++i) {
		auto index = qint32();
		stream >> index;
		actions.logoutAccounts.push_back(index);
	}
	return stream.status() == QDataStream::Ok;
}

[[nodiscard]] QByteArray PackData(const Data &data) {
	auto result = QByteArray();
	auto stream = QDataStream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream
		<< qint32(kVersion)
		<< qint32(data.clearCacheOnLock ? 1 : 0)
		<< qint32(data.list.size());
	for (const auto &entry : data.list) {
		stream
			<< qint32(entry.id)
			<< entry.name
			<< entry.passcode.toUtf8()
			<< qint32(entry.unlocks ? 1 : 0);
		WriteActions(stream, entry.actions);
	}
	return result;
}

[[nodiscard]] bool UnpackData(const QByteArray &packed, Data &data) {
	auto stream = QDataStream(packed);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = qint32();
	auto clearOnLock = qint32();
	auto count = qint32();
	stream >> version >> clearOnLock >> count;
	if (stream.status() != QDataStream::Ok
		|| version != kVersion
		|| count < 0
		|| count > kMaxEntries) {
		return false;
	}
	data = Data{ .clearCacheOnLock = (clearOnLock != 0) };
	for (auto i = 0; i != count; ++i) {
		auto entry = Entry();
		auto id = qint32();
		auto passcode = QByteArray();
		auto unlocks = qint32();
		stream >> id >> entry.name >> passcode >> unlocks;
		if (stream.status() != QDataStream::Ok
			|| !ReadActions(stream, entry.actions)) {
			return false;
		}
		entry.id = id;
		entry.passcode = QString::fromUtf8(passcode);
		entry.unlocks = (unlocks != 0);
		data.list.push_back(std::move(entry));
	}
	return stream.status() == QDataStream::Ok;
}

[[nodiscard]] bool ReadPlain(
		QByteArray &salt,
		std::vector<QByteArray> &blobs,
		QByteArray *encryptedData = nullptr) {
	auto file = FileReadDescriptor();
	if (!ReadFile(file, FileName(), BasePath())) {
		return false;
	}
	auto packed = QByteArray();
	auto encrypted = QByteArray();
	file.stream >> salt >> packed >> encrypted;
	if (!CheckStreamStatus(file.stream) || !UnpackBlobs(packed, blobs)) {
		return false;
	}
	if (encryptedData) {
		*encryptedData = encrypted;
	}
	return true;
}

void RemoveFileNamed(const QString &name) {
	const auto base = BasePath() + name;
	QFile::remove(base + 's');
	QFile::remove(base + '0');
	QFile::remove(base + '1');
}

void RemoveFile() {
	RemoveFileNamed(FileName());
	RemoveFileNamed(LegacyFileName());
}

void Load() {
	auto &state = GetState();
	if (state.loaded) {
		return;
	}
	const auto key = LocalKey();
	if (!key) {
		return;
	}
	state.loaded = true;
	state.salt = QByteArray();
	state.blobs.clear();
	state.data = Data();
	RemoveFileNamed(LegacyFileName());

	auto encrypted = QByteArray();
	if (!ReadPlain(state.salt, state.blobs, &encrypted)) {
		state.salt = QByteArray();
		state.blobs.clear();
		return;
	}
	auto decrypted = EncryptedDescriptor();
	auto packed = QByteArray();
	if (!DecryptLocal(decrypted, encrypted, key)) {
		state.blobs.clear();
		return;
	}
	decrypted.stream >> packed;
	if (!UnpackData(packed, state.data)) {
		state.blobs.clear();
		state.data = Data();
	}
}

[[nodiscard]] QByteArray MakeDecoy();

[[nodiscard]] bool WriteFile() {
	auto &state = GetState();
	const auto key = LocalKey();
	if (!key) {
		return false;
	}
	if (state.salt.isEmpty()) {
		state.salt.resize(kSaltSize);
		base::RandomFill(state.salt.data(), state.salt.size());
	}
	while (state.blobs.size() < size_t(kMaxEntries)) {
		state.blobs.push_back(MakeDecoy());
	}
	const auto packed = PackData(state.data);
	QDir().mkpath(BasePath());
	auto file = FileWriteDescriptor(FileName(), BasePath(), true);
	file.writeData(state.salt);
	file.writeData(PackBlobs(state.blobs));
	auto encrypted = EncryptedDescriptor(packed.size() + sizeof(quint32));
	encrypted.stream << packed;
	file.writeEncrypted(encrypted, key);
	return true;
}

[[nodiscard]] QByteArray MakeBlobWith(
		const Entry &entry,
		const QByteArray &realPasscode,
		const MTP::AuthKeyPtr &key) {
	auto inner = QByteArray();
	{
		auto stream = QDataStream(&inner, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << qint32(entry.id) << (entry.unlocks
			? realPasscode
			: QByteArray());
		WriteActions(stream, entry.unlocks ? Actions() : entry.actions);
	}
	const auto room = kBlobPayload - int(inner.size()) - 4;
	if (room < 0) {
		return QByteArray();
	}
	auto padding = QByteArray(room, Qt::Uninitialized);
	base::RandomFill(padding.data(), padding.size());
	auto blob = EncryptedDescriptor(kBlobPayload);
	blob.stream.writeRawData(inner.constData(), int(inner.size()));
	blob.stream << padding;
	return PrepareEncrypted(blob, key);
}

[[nodiscard]] QByteArray MakeBlob(
		const Entry &entry,
		const QByteArray &realPasscode,
		const QByteArray &salt) {
	return MakeBlobWith(
		entry,
		realPasscode,
		CreateLocalKey(entry.passcode.toUtf8(), salt));
}

[[nodiscard]] QByteArray MakeDecoy() {
	auto data = MTP::AuthKey::Data();
	base::RandomFill(data.data(), data.size());
	return MakeBlobWith(
		Entry(),
		QByteArray(),
		std::make_shared<MTP::AuthKey>(data));
}

[[nodiscard]] bool Rebuild(const QByteArray &realPasscode) {
	auto &state = GetState();
	state.salt.resize(kSaltSize);
	base::RandomFill(state.salt.data(), state.salt.size());
	state.blobs.clear();
	for (const auto &entry : state.data.list) {
		auto blob = MakeBlob(entry, realPasscode, state.salt);
		if (blob.isEmpty()) {
			return false;
		}
		state.blobs.push_back(std::move(blob));
	}
	return WriteFile();
}

[[nodiscard]] QString DefaultDownloadsDirectory() {
	return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
		+ '/'
		+ AppName.utf16()
		+ '/';
}

void RemoveDownloads() {
	if (Core::App().settings().downloadPath().isEmpty()
		&& Core::App().canReadDefaultDownloadPath()) {
		QDir(DefaultDownloadsDirectory()).removeRecursively();
	}
}

void ClearCache() {
	for (const auto &entry : Core::App().domain().accounts()) {
		if (const auto session = entry.account->maybeSession()) {
			session->data().cache().clear();
			session->data().cacheBigFile().clear();
		}
	}
	RemoveDownloads();
}

void RemoveFiles(const QStringList &patterns, bool directories) {
	const auto dir = QDir(BasePath());
	const auto filter = directories
		? (QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)
		: (QDir::Files | QDir::Hidden);
	for (const auto &info : dir.entryInfoList(patterns, filter)) {
		if (directories) {
			QDir(info.absoluteFilePath()).removeRecursively();
		} else {
			QFile::remove(info.absoluteFilePath());
		}
	}
}

void WipeCacheFiles() {
	RemoveFiles({ u"user_data*"_q }, true);
	RemoveDownloads();
}

void WipeMessageDatabaseFiles() {
	RemoveFiles({ u"ayudata*"_q }, false);
	RemoveFiles({ u"ayu_backups"_q }, true);
}

void WipeSavedMediaFiles() {
	RemoveFiles({ u"ayu_media"_q }, true);
}

void WipeSecretChatFiles() {
	RemoveFiles({ u"ayu_secret*"_q }, true);
}

void WipeSessionFiles() {
	const auto hex = u"[0-9A-F]"_q.repeated(16);
	RemoveFiles({
		u"key_data*"_q,
		u"ayu_accounts*"_q,
		FileName() + '*',
		LegacyFileName() + '*',
		hex + '*',
	}, false);
	RemoveFiles({ hex, u"account_#*"_q }, true);
	WipeCacheFiles();
	WipeMessageDatabaseFiles();
	WipeSavedMediaFiles();
	WipeSecretChatFiles();
}

void ClearProxies() {
	auto &proxy = Core::App().settings().proxy();
	if (proxy.list().empty() && !proxy.selected()) {
		return;
	}
	proxy.setList({});
	proxy.setProxyRotationPreferredIndices({});
	Core::App().setCurrentProxy(
		MTP::ProxyData(),
		MTP::ProxyData::Settings::System);
	Core::App().saveSettingsDelayed();
}

void RunCommand(const QString &command) {
	auto parts = QProcess::splitCommand(command);
	if (parts.isEmpty()) {
		return;
	}
	const auto program = parts.takeFirst();
	QProcess::startDetached(program, parts);
}

void FinishActions(const Actions &actions) {
	if (actions.clearProxies) {
		ClearProxies();
	}
	if (!actions.command.trimmed().isEmpty()) {
		RunCommand(actions.command.trimmed());
	}
}

void CheckLoggedOut() {
	auto &waiter = GetWaiter();
	const auto timedOut = (crl::now() - waiter.started) > kLogoutTimeout;
	auto waiting = false;
	for (const auto &weak : waiter.accounts) {
		const auto account = weak.get();
		if (!account || !account->sessionExists()) {
			continue;
		} else if (timedOut) {
			account->forcedLogOut();
		} else {
			waiting = true;
		}
	}
	if (waiting) {
		return;
	}
	GetWaitTimer().cancel();
	const auto actions = base::take(waiter.actions);
	waiter.accounts.clear();
	FinishActions(actions);
}

[[nodiscard]] ID SessionStorageId(not_null<Main::Session*> session) {
	return session->userId().bare & PeerId::kChatTypeMask;
}

void Run(const Actions &actions) {
	if (actions.clearCache || actions.deleteAll) {
		ClearCache();
	}
	if (actions.clearCache && actions.clearCacheDb) {
		AyuMessages::clearDeletedDatabase();
	}
	if (actions.clearCache && actions.clearCacheMedia) {
		AyuMessages::clearSavedMedia();
	}
	auto &waiter = GetWaiter();
	waiter = Waiter();
	auto userIds = std::vector<ID>();
	for (const auto &entry : Core::App().domain().accounts()) {
		const auto session = entry.account->maybeSession();
		if (session
			&& (actions.deleteAll
				|| ranges::contains(actions.logoutAccounts, entry.index))) {
			waiter.accounts.push_back(base::make_weak(entry.account.get()));
			userIds.push_back(SessionStorageId(session));
		}
	}
	if (actions.deleteAll) {
		AyuMessages::clearEverything();
	} else {
		for (const auto userId : userIds) {
			if (actions.logoutDb) {
				AyuMessages::clearUserDatabase(userId);
			}
			if (actions.logoutMedia) {
				AyuMessages::clearUserMedia(userId);
			}
		}
	}
	for (const auto &weak : waiter.accounts) {
		if (const auto account = weak.get()) {
			account->logOut();
		}
	}
	if (waiter.accounts.empty()) {
		FinishActions(actions);
		return;
	}
	waiter.actions = actions;
	waiter.started = crl::now();
	GetWaitTimer().setCallback(CheckLoggedOut);
	GetWaitTimer().callEach(kLogoutCheckEach);
}

} // namespace

std::optional<Match> TryMatch(const QByteArray &passcode) {
	if (passcode.isEmpty()) {
		return std::nullopt;
	}
	auto salt = QByteArray();
	auto blobs = std::vector<QByteArray>();
	const auto valid = ReadPlain(salt, blobs);
	const auto key = CreateLocalKey(
		passcode,
		valid ? salt : QByteArray(kSaltSize, '\0'));
	auto found = std::optional<Match>();
	for (const auto &blob : blobs) {
		auto decrypted = EncryptedDescriptor();
		if (!DecryptLocal(decrypted, blob, key) || found) {
			continue;
		}
		auto result = Match();
		auto id = qint32();
		decrypted.stream >> id >> result.realPasscode;
		if (decrypted.stream.status() == QDataStream::Ok
			&& ReadActions(decrypted.stream, result.actions)) {
			result.id = id;
			found = std::move(result);
		}
	}
	return found;
}

void SetPending(int id) {
	GetState().pendingId = id;
}

void RunPending() {
	const auto id = base::take(GetState().pendingId);
	if (!id) {
		return;
	}
	crl::on_main([=] {
		Load();
		const auto &list = GetState().data.list;
		const auto i = ranges::find(list, id, &Entry::id);
		if (i != end(list)) {
			Run(i->actions);
		}
	});
}

void RunSilent(const Actions &actions) {
	if (Core::App().domain().started()) {
		Run(actions);
		return;
	}
	if (actions.deleteAll) {
		WipeSessionFiles();
	} else if (actions.clearCache) {
		WipeCacheFiles();
		if (actions.clearCacheDb) {
			WipeMessageDatabaseFiles();
		}
		if (actions.clearCacheMedia) {
			WipeSavedMediaFiles();
		}
	}
	FinishActions(actions);
}

void OnLock() {
	Load();
	if (GetState().data.clearCacheOnLock) {
		ClearCache();
	}
}

const Data &Current() {
	Load();
	return GetState().data;
}

AddResult Add(
		QString name,
		QString passcode,
		const QString &realPasscode,
		bool unlocks,
		Actions actions) {
	Load();
	auto &state = GetState();
	if (!state.loaded || passcode.isEmpty()) {
		return AddResult::Empty;
	} else if (state.data.list.size() >= kMaxEntries) {
		return AddResult::TooMany;
	}
	const auto real = realPasscode.toUtf8();
	if (!Core::App().domain().local().checkPasscode(real)) {
		return AddResult::WrongPasscode;
	} else if (unlocks && passcode.size() < kMinUnlockLength) {
		return AddResult::TooShort;
	} else if (passcode == realPasscode) {
		return AddResult::SameAsReal;
	} else if (ranges::contains(state.data.list, passcode, &Entry::passcode)) {
		return AddResult::Duplicate;
	}
	actions.command = actions.command.left(kMaxCommandLength);
	auto id = 1;
	for (const auto &entry : state.data.list) {
		id = std::max(id, entry.id + 1);
	}
	state.data.list.push_back({
		.id = id,
		.name = std::move(name),
		.passcode = std::move(passcode),
		.unlocks = unlocks,
		.actions = std::move(actions),
	});
	if (!Rebuild(real)) {
		state.data.list.pop_back();
		state.loaded = false;
		return AddResult::Failed;
	}
	return AddResult::Ok;
}

void Update(int id, QString name, Actions actions) {
	Load();
	auto &state = GetState();
	const auto i = ranges::find(state.data.list, id, &Entry::id);
	if (i == end(state.data.list)) {
		return;
	}
	actions.command = actions.command.left(kMaxCommandLength);
	auto updated = *i;
	updated.name = std::move(name);
	updated.actions = std::move(actions);
	if (!updated.unlocks) {
		auto blob = MakeBlob(updated, QByteArray(), state.salt);
		if (blob.isEmpty()) {
			return;
		}
		state.blobs[i - begin(state.data.list)] = std::move(blob);
	}
	*i = std::move(updated);
	[[maybe_unused]] const auto written = WriteFile();
}

void Remove(int id) {
	Load();
	auto &state = GetState();
	const auto i = ranges::find(state.data.list, id, &Entry::id);
	if (i == end(state.data.list)) {
		return;
	}
	state.blobs.erase(state.blobs.begin() + (i - begin(state.data.list)));
	state.blobs.push_back(MakeDecoy());
	state.data.list.erase(i);
	[[maybe_unused]] const auto written = WriteFile();
}

void SetClearCacheOnLock(bool value) {
	Load();
	auto &state = GetState();
	if (!state.loaded || state.data.clearCacheOnLock == value) {
		return;
	}
	state.data.clearCacheOnLock = value;
	[[maybe_unused]] const auto written = WriteFile();
}

void RealPasscodeChanged(const QByteArray &passcode) {
	Load();
	auto &state = GetState();
	if (!state.loaded) {
		return;
	}
	if (passcode.isEmpty()) {
		state = State{ .loaded = true, .pendingId = state.pendingId };
		RemoveFile();
	} else if (!state.data.list.empty()) {
		[[maybe_unused]] const auto written = Rebuild(passcode);
	}
}

} // namespace AyuFakePasscode
