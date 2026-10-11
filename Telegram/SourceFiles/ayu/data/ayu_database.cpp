// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/data/ayu_database.h"

#include "ayu/data/ayu_database_backup.h"
#include "ayu/data/ayu_vault.h"
#include "ayu/data/entities.h"
#include "ayu/libs/sqlite/sqlite3.h"
#include "ayu/libs/sqlite/sqlite_orm.h"
#include "ayu/utils/id_search.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "crl/crl_async.h"
#include "crl/crl_on_main.h"
#include "main/main_domain.h"
#include "storage/storage_domain.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <system_error>
#include <thread>

struct sqlite3_api_routines;

namespace {

sqlite3 *MemoryHandle = nullptr;

int captureMemoryHandle(sqlite3 *db, char **, const sqlite3_api_routines *) {
	if (!MemoryHandle) {
		MemoryHandle = db;
	}
	return SQLITE_OK;
}

const auto kMemoryHandleCapture = [] {
	sqlite3_auto_extension(
		reinterpret_cast<void(*)(void)>(&captureMemoryHandle));
	return true;
}();

} // namespace

using namespace sqlite_orm;
auto storage = make_storage(
	":memory:",
	make_table<SchemaVersion>(
		"SchemaVersion",
		make_column("id", &SchemaVersion::id, primary_key()),
		make_column("version", &SchemaVersion::version)
	),
	make_index("idx_deleted_message_userId_dialogId_topicId_messageId",
			   column<DeletedMessage>(&DeletedMessage::userId),
			   column<DeletedMessage>(&DeletedMessage::dialogId),
			   column<DeletedMessage>(&DeletedMessage::topicId),
			   column<DeletedMessage>(&DeletedMessage::messageId)),
	make_index("idx_deleted_message_userId_dialogId_messageId",
			   column<DeletedMessage>(&DeletedMessage::userId),
			   column<DeletedMessage>(&DeletedMessage::dialogId),
			   column<DeletedMessage>(&DeletedMessage::messageId)),
	make_index("idx_edited_message_userId_dialogId_messageId",
			   column<EditedMessage>(&EditedMessage::userId),
			   column<EditedMessage>(&EditedMessage::dialogId),
			   column<EditedMessage>(&EditedMessage::messageId)),
	make_table<DeletedMessage>(
		"DeletedMessage",
		make_column("fakeId", &DeletedMessage::fakeId, primary_key().autoincrement()),
		make_column("userId", &DeletedMessage::userId),
		make_column("dialogId", &DeletedMessage::dialogId),
		make_column("groupedId", &DeletedMessage::groupedId),
		make_column("peerId", &DeletedMessage::peerId),
		make_column("fromId", &DeletedMessage::fromId),
		make_column("topicId", &DeletedMessage::topicId),
		make_column("messageId", &DeletedMessage::messageId),
		make_column("date", &DeletedMessage::date),
		make_column("flags", &DeletedMessage::flags),
		make_column("editDate", &DeletedMessage::editDate),
		make_column("views", &DeletedMessage::views),
		make_column("fwdFlags", &DeletedMessage::fwdFlags),
		make_column("fwdFromId", &DeletedMessage::fwdFromId),
		make_column("fwdName", &DeletedMessage::fwdName),
		make_column("fwdDate", &DeletedMessage::fwdDate),
		make_column("fwdPostAuthor", &DeletedMessage::fwdPostAuthor),
		make_column("replyFlags", &DeletedMessage::replyFlags),
		make_column("replyMessageId", &DeletedMessage::replyMessageId),
		make_column("replyPeerId", &DeletedMessage::replyPeerId),
		make_column("replyTopId", &DeletedMessage::replyTopId),
		make_column("replyForumTopic", &DeletedMessage::replyForumTopic),
		make_column("replySerialized", &DeletedMessage::replySerialized),
		make_column("entityCreateDate", &DeletedMessage::entityCreateDate),
		make_column("text", &DeletedMessage::text),
		make_column("textEntities", &DeletedMessage::textEntities),
		make_column("mediaPath", &DeletedMessage::mediaPath),
		make_column("hqThumbPath", &DeletedMessage::hqThumbPath),
		make_column("documentType", &DeletedMessage::documentType),
		make_column("documentSerialized", &DeletedMessage::documentSerialized),
		make_column("thumbsSerialized", &DeletedMessage::thumbsSerialized),
		make_column("documentAttributesSerialized", &DeletedMessage::documentAttributesSerialized),
		make_column("mimeType", &DeletedMessage::mimeType)
	),
	make_table<EditedMessage>(
		"EditedMessage",
		make_column("fakeId", &EditedMessage::fakeId, primary_key().autoincrement()),
		make_column("userId", &EditedMessage::userId),
		make_column("dialogId", &EditedMessage::dialogId),
		make_column("groupedId", &EditedMessage::groupedId),
		make_column("peerId", &EditedMessage::peerId),
		make_column("fromId", &EditedMessage::fromId),
		make_column("topicId", &EditedMessage::topicId),
		make_column("messageId", &EditedMessage::messageId),
		make_column("date", &EditedMessage::date),
		make_column("flags", &EditedMessage::flags),
		make_column("editDate", &EditedMessage::editDate),
		make_column("views", &EditedMessage::views),
		make_column("fwdFlags", &EditedMessage::fwdFlags),
		make_column("fwdFromId", &EditedMessage::fwdFromId),
		make_column("fwdName", &EditedMessage::fwdName),
		make_column("fwdDate", &EditedMessage::fwdDate),
		make_column("fwdPostAuthor", &EditedMessage::fwdPostAuthor),
		make_column("replyFlags", &EditedMessage::replyFlags),
		make_column("replyMessageId", &EditedMessage::replyMessageId),
		make_column("replyPeerId", &EditedMessage::replyPeerId),
		make_column("replyTopId", &EditedMessage::replyTopId),
		make_column("replyForumTopic", &EditedMessage::replyForumTopic),
		make_column("replySerialized", &EditedMessage::replySerialized),
		make_column("entityCreateDate", &EditedMessage::entityCreateDate),
		make_column("text", &EditedMessage::text),
		make_column("textEntities", &EditedMessage::textEntities),
		make_column("mediaPath", &EditedMessage::mediaPath),
		make_column("hqThumbPath", &EditedMessage::hqThumbPath),
		make_column("documentType", &EditedMessage::documentType),
		make_column("documentSerialized", &EditedMessage::documentSerialized),
		make_column("thumbsSerialized", &EditedMessage::thumbsSerialized),
		make_column("documentAttributesSerialized", &EditedMessage::documentAttributesSerialized),
		make_column("mimeType", &EditedMessage::mimeType)
	),
	make_table<DeletedDialog>(
		"DeletedDialog",
		make_column("fakeId", &DeletedDialog::fakeId, primary_key().autoincrement()),
		make_column("userId", &DeletedDialog::userId),
		make_column("dialogId", &DeletedDialog::dialogId),
		make_column("peerId", &DeletedDialog::peerId),
		make_column("folderId", &DeletedDialog::folderId),
		make_column("topMessage", &DeletedDialog::topMessage),
		make_column("lastMessageDate", &DeletedDialog::lastMessageDate),
		make_column("flags", &DeletedDialog::flags),
		make_column("entityCreateDate", &DeletedDialog::entityCreateDate)
	),
	make_table<KeptDialog>(
		"KeptDialog",
		make_column("fakeId", &KeptDialog::fakeId, primary_key().autoincrement()),
		make_column("userId", &KeptDialog::userId),
		make_column("dialogId", &KeptDialog::dialogId),
		make_column("kind", &KeptDialog::kind),
		make_column("title", &KeptDialog::title),
		make_column("username", &KeptDialog::username),
		make_column("accessHash", &KeptDialog::accessHash),
		make_column("folderId", &KeptDialog::folderId),
		make_column("lastMessageDate", &KeptDialog::lastMessageDate),
		make_column("lost", &KeptDialog::lost)
	),
	make_table<DeletedMarkup>(
		"DeletedMarkup",
		make_column("fakeId", &DeletedMarkup::fakeId, primary_key().autoincrement()),
		make_column("userId", &DeletedMarkup::userId),
		make_column("dialogId", &DeletedMarkup::dialogId),
		make_column("messageId", &DeletedMarkup::messageId),
		make_column("markup", &DeletedMarkup::markup),
		make_column("entityCreateDate", &DeletedMarkup::entityCreateDate)
	),
	make_table<DeletedExtra>(
		"DeletedExtra",
		make_column("fakeId", &DeletedExtra::fakeId, primary_key().autoincrement()),
		make_column("userId", &DeletedExtra::userId),
		make_column("dialogId", &DeletedExtra::dialogId),
		make_column("messageId", &DeletedExtra::messageId),
		make_column("reactions", &DeletedExtra::reactions),
		make_column("repliesCount", &DeletedExtra::repliesCount),
		make_column("commentsChannelId", &DeletedExtra::commentsChannelId),
		make_column("commentsRootId", &DeletedExtra::commentsRootId),
		make_column("commentsReadTill", &DeletedExtra::commentsReadTill),
		make_column("commentsMaxId", &DeletedExtra::commentsMaxId),
		make_column("repliers", &DeletedExtra::repliers),
		make_column("entityCreateDate", &DeletedExtra::entityCreateDate)
	),
	make_table<KeptTopic>(
		"KeptTopic",
		make_column("fakeId", &KeptTopic::fakeId, primary_key().autoincrement()),
		make_column("userId", &KeptTopic::userId),
		make_column("dialogId", &KeptTopic::dialogId),
		make_column("rootId", &KeptTopic::rootId),
		make_column("title", &KeptTopic::title),
		make_column("colorId", &KeptTopic::colorId),
		make_column("iconId", &KeptTopic::iconId),
		make_column("creatorId", &KeptTopic::creatorId),
		make_column("date", &KeptTopic::date),
		make_column("flags", &KeptTopic::flags)
	),
	make_index("idx_known_user_userId_peerId",
			   column<KnownUser>(&KnownUser::userId),
			   column<KnownUser>(&KnownUser::peerId)),
	make_index("idx_secret_chat_userId_chatId",
			   column<SecretChatRow>(&SecretChatRow::userId),
			   column<SecretChatRow>(&SecretChatRow::chatId)),
	make_index("idx_secret_message_userId_chatId_randomId",
			   column<SecretMessageRow>(&SecretMessageRow::userId),
			   column<SecretMessageRow>(&SecretMessageRow::chatId),
			   column<SecretMessageRow>(&SecretMessageRow::randomId)),
	make_table<LocalPin>(
		"LocalPin",
		make_column("fakeId", &LocalPin::fakeId, primary_key().autoincrement()),
		make_column("userId", &LocalPin::userId),
		make_column("peerId", &LocalPin::peerId),
		make_column("position", &LocalPin::position)
	),
	make_table<KnownUser>(
		"KnownUser",
		make_column("fakeId", &KnownUser::fakeId, primary_key().autoincrement()),
		make_column("userId", &KnownUser::userId),
		make_column("peerId", &KnownUser::peerId),
		make_column("accessHash", &KnownUser::accessHash),
		make_column("firstName", &KnownUser::firstName),
		make_column("lastName", &KnownUser::lastName),
		make_column("username", &KnownUser::username),
		make_column("updatedAt", &KnownUser::updatedAt)
	),
	make_table<SecretChatRow>(
		"SecretChat",
		make_column("fakeId", &SecretChatRow::fakeId, primary_key().autoincrement()),
		make_column("userId", &SecretChatRow::userId),
		make_column("chatId", &SecretChatRow::chatId),
		make_column("accessHash", &SecretChatRow::accessHash),
		make_column("peerUserId", &SecretChatRow::peerUserId),
		make_column("creator", &SecretChatRow::creator),
		make_column("state", &SecretChatRow::state),
		make_column("keyData", &SecretChatRow::keyData),
		make_column("fingerprint", &SecretChatRow::fingerprint),
		make_column("myIn", &SecretChatRow::myIn),
		make_column("myOut", &SecretChatRow::myOut),
		make_column("hisIn", &SecretChatRow::hisIn),
		make_column("hisLayer", &SecretChatRow::hisLayer),
		make_column("date", &SecretChatRow::date),
		make_column("lastDate", &SecretChatRow::lastDate),
		make_column("unread", &SecretChatRow::unread)
	),
	make_table<SecretMessageRow>(
		"SecretMessage",
		make_column("fakeId", &SecretMessageRow::fakeId, primary_key().autoincrement()),
		make_column("userId", &SecretMessageRow::userId),
		make_column("chatId", &SecretMessageRow::chatId),
		make_column("randomId", &SecretMessageRow::randomId),
		make_column("outgoing", &SecretMessageRow::outgoing),
		make_column("date", &SecretMessageRow::date),
		make_column("kind", &SecretMessageRow::kind),
		make_column("seqIn", &SecretMessageRow::seqIn),
		make_column("seqOut", &SecretMessageRow::seqOut),
		make_column("text", &SecretMessageRow::text),
		make_column("payload", &SecretMessageRow::payload)
	),
	make_table<PeekedStatusRow>(
		"PeekedStatus",
		make_column("fakeId", &PeekedStatusRow::fakeId, primary_key().autoincrement()),
		make_column("userId", &PeekedStatusRow::userId),
		make_column("targetId", &PeekedStatusRow::targetId),
		make_column("kind", &PeekedStatusRow::kind),
		make_column("time", &PeekedStatusRow::time),
		make_column("checkedAt", &PeekedStatusRow::checkedAt)
	),
	make_table<PeekRestoreRow>(
		"PeekRestore",
		make_column("fakeId", &PeekRestoreRow::fakeId, primary_key().autoincrement()),
		make_column("userId", &PeekRestoreRow::userId),
		make_column("option", &PeekRestoreRow::option),
		make_column("flags", &PeekRestoreRow::flags),
		make_column("always", &PeekRestoreRow::always),
		make_column("never", &PeekRestoreRow::never)
	),
	make_table<SecretStateRow>(
		"SecretState",
		make_column("fakeId", &SecretStateRow::fakeId, primary_key().autoincrement()),
		make_column("userId", &SecretStateRow::userId),
		make_column("qts", &SecretStateRow::qts),
		make_column("date", &SecretStateRow::date)
	),
	make_table<RegexFilter>(
		"RegexFilter",
		make_column("id", &RegexFilter::id, primary_key()),
		make_column("text", &RegexFilter::text),
		make_column("enabled", &RegexFilter::enabled),
		make_column("reversed", &RegexFilter::reversed),
		make_column("caseInsensitive", &RegexFilter::caseInsensitive),
		make_column("dialogId", &RegexFilter::dialogId)
	),
	make_table<RegexFilterGlobalExclusion>(
		"RegexFilterGlobalExclusion",
		make_column("fakeId", &RegexFilterGlobalExclusion::fakeId, primary_key().autoincrement()),
		make_column("dialogId", &RegexFilterGlobalExclusion::dialogId),
		make_column("filterId", &RegexFilterGlobalExclusion::filterId)
	),
	make_table<SpyMessageRead>(
		"SpyMessageRead",
		make_column("fakeId", &SpyMessageRead::fakeId, primary_key().autoincrement()),
		make_column("userId", &SpyMessageRead::userId),
		make_column("dialogId", &SpyMessageRead::dialogId),
		make_column("messageId", &SpyMessageRead::messageId),
		make_column("entityCreateDate", &SpyMessageRead::entityCreateDate)
	),
	make_table<SpyMessageContentsRead>(
		"SpyMessageContentsRead",
		make_column("fakeId", &SpyMessageContentsRead::fakeId, primary_key().autoincrement()),
		make_column("userId", &SpyMessageContentsRead::userId),
		make_column("dialogId", &SpyMessageContentsRead::dialogId),
		make_column("messageId", &SpyMessageContentsRead::messageId),
		make_column("entityCreateDate", &SpyMessageContentsRead::entityCreateDate)
	)
);

namespace {

std::recursive_mutex DatabaseMutex;
bool DatabaseReady = false;
std::atomic<int> PurgeDays = 0;
std::atomic<sqlite3_int64> SavedChanges = 0;
std::atomic<bool> SavePending = false;
std::unique_ptr<base::Timer> SaveTimer;
bool FinishLegacyMigrationPending = false;

constexpr auto kBusyTimeoutMs = 5000;
constexpr auto kSqliteBusy = 5;
constexpr auto kSqliteLocked = 6;
constexpr auto kSqliteCorrupt = 11;
constexpr auto kSqliteNotADatabase = 26;
constexpr auto kSaveDelay = crl::time(3000);

[[nodiscard]] QString snapshotPath() {
	return "./tdata/ayudata.enc";
}

[[nodiscard]] QString legacyPath() {
	return "./tdata/ayudata.db";
}

bool saveSnapshot();

void scheduleSave() {
	if (SavePending.exchange(true)) {
		return;
	}
	crl::on_main([] {
		if (!SaveTimer) {
			SaveTimer = std::make_unique<base::Timer>([] {
				crl::async([] {
					[[maybe_unused]] const auto saved = saveSnapshot();
				});
			});
		}
		SaveTimer->callOnce(kSaveDelay);
	});
}

void noteChanges() {
	if (MemoryHandle
		&& sqlite3_total_changes64(MemoryHandle) != SavedChanges.load()) {
		scheduleSave();
	}
}

template<typename Result, typename Callback>
Result run(const char *what, Result fallback, Callback &&callback) {
	std::lock_guard lock(DatabaseMutex);
	if (!DatabaseReady) {
		return fallback;
	}
	try {
		Result result = callback();
		noteChanges();
		return result;
	} catch (const std::exception &ex) {
		LOG(("[AyuGram] Database: failed to %1: %2").arg(QString::fromUtf8(what), QString::fromUtf8(ex.what())));
	} catch (...) {
		LOG(("[AyuGram] Database: failed to %1.").arg(QString::fromUtf8(what)));
	}
	return fallback;
}

template<typename Callback>
void runVoid(const char *what, Callback &&callback) {
	run<bool>(what, false, [&] {
		callback();
		return true;
	});
}

template<typename Callback>
void inTransaction(Callback &&callback) {
	storage.begin_transaction();
	try {
		callback();
		storage.commit();
	} catch (...) {
		try {
			storage.rollback();
		} catch (...) {
		}
		throw;
	}
}

bool saveSnapshot() {
	SavePending = false;
	auto plain = QByteArray();
	{
		std::lock_guard lock(DatabaseMutex);
		if (!DatabaseReady || !MemoryHandle) {
			return false;
		}
		auto size = sqlite3_int64();
		const auto data = sqlite3_serialize(MemoryHandle, "main", &size, 0);
		if (!data) {
			return false;
		}
		plain = QByteArray(
			reinterpret_cast<const char*>(data),
			qsizetype(size));
		sqlite3_free(data);
		SavedChanges = sqlite3_total_changes64(MemoryHandle);
	}
	if (!AyuVault::WriteSealedFile(snapshotPath(), plain)) {
		SavedChanges = -1;
		return false;
	}
	return true;
}

void renameDatabaseFiles(const QString &suffix) {
	for (const auto &extension : {QString(), QString("-shm"), QString("-wal")}) {
		const auto from = legacyPath() + extension;
		if (QFile::exists(from)) {
			QFile::rename(from, QString("./tdata/ayudata_%1.db%2").arg(suffix, extension));
		}
	}
	if (QFile::exists(snapshotPath())) {
		QFile::rename(snapshotPath(), QString("./tdata/ayudata_%1.enc").arg(suffix));
	}
}

void copyDatabaseFiles(const QString &suffix) {
	if (QFile::exists(snapshotPath())) {
		QFile::copy(snapshotPath(), QString("./tdata/ayudata_%1.enc").arg(suffix));
	}
}

[[noreturn]] void throwSqlite(int code) {
	throw std::system_error(
		std::error_code(code, sqlite_orm::get_sqlite_error_category()));
}

void resetMemoryDatabase() {
	sqlite3 *empty = nullptr;
	if (sqlite3_open_v2(":memory:", &empty, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
		sqlite3_close(empty);
		return;
	}
	if (const auto backup = sqlite3_backup_init(MemoryHandle, "main", empty, "main")) {
		sqlite3_backup_step(backup, -1);
		sqlite3_backup_finish(backup);
	}
	sqlite3_close(empty);
}

void importLegacyDatabase() {
	sqlite3 *legacy = nullptr;
	const auto path = legacyPath().toUtf8();
	if (sqlite3_open_v2(path.constData(), &legacy, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
		const auto code = legacy ? sqlite3_extended_errcode(legacy) : SQLITE_CANTOPEN;
		sqlite3_close(legacy);
		throwSqlite(code);
	}
	sqlite3_busy_timeout(legacy, kBusyTimeoutMs);
	sqlite3_exec(legacy, "PRAGMA journal_mode = DELETE", nullptr, nullptr, nullptr);
	auto code = SQLITE_ERROR;
	if (const auto backup = sqlite3_backup_init(MemoryHandle, "main", legacy, "main")) {
		sqlite3_backup_step(backup, -1);
		code = sqlite3_backup_finish(backup);
	} else {
		code = sqlite3_errcode(MemoryHandle);
	}
	sqlite3_close(legacy);
	if (code != SQLITE_OK) {
		throwSqlite(code);
	}
}

void loadSnapshot(bool &imported) {
	if (QFile::exists(snapshotPath())) {
		const auto plain = AyuVault::ReadFile(snapshotPath(), true);
		if (!plain || plain->isEmpty()) {
			throwSqlite(kSqliteNotADatabase);
		}
		const auto size = sqlite3_int64(plain->size());
		const auto buffer = static_cast<unsigned char*>(sqlite3_malloc64(size));
		if (!buffer) {
			throw std::bad_alloc();
		}
		memcpy(buffer, plain->constData(), size_t(size));
		const auto code = sqlite3_deserialize(
			MemoryHandle,
			"main",
			buffer,
			size,
			size,
			SQLITE_DESERIALIZE_FREEONCLOSE | SQLITE_DESERIALIZE_RESIZEABLE);
		if (code != SQLITE_OK) {
			throwSqlite(code);
		}
	} else if (QFile::exists(legacyPath())) {
		importLegacyDatabase();
		imported = true;
	}
}

void sealLegacyCopies(const QString &directory, const QString &pattern) {
	for (const auto &info : QDir(directory).entryInfoList(QStringList() << pattern, QDir::Files)) {
		if (const auto plain = AyuVault::ReadFile(info.absoluteFilePath())) {
			auto target = info.absoluteFilePath();
			target.chop(3);
			if (AyuVault::WriteSealedFile(target + ".enc", *plain)) {
				QFile::remove(info.absoluteFilePath());
				QFile::remove(info.absoluteFilePath() + "-wal");
				QFile::remove(info.absoluteFilePath() + "-shm");
			}
		}
	}
}

void finishLegacyMigration() {
	for (const auto &extension : {QString(), QString("-shm"), QString("-wal")}) {
		QFile::remove(legacyPath() + extension);
	}
	sealLegacyCopies("./tdata", "ayudata_*.db");
	sealLegacyCopies("./tdata/ayu_backups", "ayudata_*.db");
}

}

namespace AyuMigrations {

void migrateToV1(decltype(storage) &storage) {
	// drop RegexFilter table as we've added primary_key()
	try {
		storage.drop_table_if_exists("RegexFilter");
		LOG(("Migration to V1 successful."));
	} catch (const std::exception &ex) {
		LOG(("Migration to V1 failed: %1").arg(ex.what()));
	}
}

void migrateToV2(decltype(storage) &storage) {
	storage.remove_all<DeletedMessage>(
		where(not_in(
			column<DeletedMessage>(&DeletedMessage::fakeId),
			select(
				max(column<DeletedMessage>(&DeletedMessage::fakeId)),
				group_by(
					column<DeletedMessage>(&DeletedMessage::userId),
					column<DeletedMessage>(&DeletedMessage::dialogId),
					column<DeletedMessage>(&DeletedMessage::messageId)
				)
			)
		))
	);
	LOG(("Migration to V2 successful."));
}

void migrateToV3(decltype(storage) &storage) {
	storage.remove_all<KeptDialog>(
		where(column<KeptDialog>(&KeptDialog::lost) == 0));
	LOG(("Migration to V3 successful."));
}

}

void runMigrations(decltype(storage) &storage) {
	constexpr int kLatestVersion = 3;

	const std::map<int, Fn<void(decltype(storage) &)>> migrations = {
		{1, AyuMigrations::migrateToV1},
		{2, AyuMigrations::migrateToV2},
		{3, AyuMigrations::migrateToV3},
	};

	int currentVersion = 0;
	try {
		if (auto versionRow = storage.get_pointer<SchemaVersion>(1)) {
			currentVersion = versionRow->version;
		} else {
			storage.insert(SchemaVersion{1, 0});
		}
	} catch (...) {
		LOG(("No SchemaVersion, assuming 0"));
		storage.insert(SchemaVersion{1, 0});
	}

	if (currentVersion >= kLatestVersion) {
		LOG(("Database is ok"));
		return;
	}

	LOG(("Database version: %1. Latest version: %2.").arg(currentVersion).arg(kLatestVersion));

	for (int v = currentVersion + 1; v <= kLatestVersion; ++v) {
		if (migrations.contains(v)) {
			try {
				LOG(("Migration for version: %1").arg(v));
				storage.begin_transaction();

				migrations.at(v)(storage);

				storage.update_all(set(c(&SchemaVersion::version) = v), where(c(&SchemaVersion::id) == 1));
				storage.commit();
				LOG(("Applied migration for version: %1.").arg(v));
			} catch (...) {
				try {
					storage.rollback();
				} catch (...) {
				}
				LOG(("Failed to apply migration for version: %1.").arg(v));
				AyuDatabase::backupCurrentDatabase();

				return;
			}
		}
	}
}

namespace AyuDatabase {

void moveCurrentDatabase() {
	renameDatabaseFiles(QString::number(base::unixtime::now()));
}

void backupCurrentDatabase() {
	copyDatabaseFiles(QString("backup_%1").arg(base::unixtime::now()));
}

namespace {

bool prepareStorage() {
	if (!MemoryHandle) {
		throw std::runtime_error("The database handle is missing.");
	}
	auto imported = false;
	loadSnapshot(imported);
	const auto before = sqlite3_total_changes64(MemoryHandle);

	storage.sync_schema(true);

	runMigrations(storage);

	storage.sync_schema(true);

	sqlite3_exec(MemoryHandle, "PRAGMA secure_delete = ON", nullptr, nullptr, nullptr);
	try {
		storage.busy_timeout(kBusyTimeoutMs);
	} catch (const std::exception &ex) {
		LOG(("[AyuGram] Database: failed to apply pragmas: %1").arg(ex.what()));
	}
	if (imported) {
		FinishLegacyMigrationPending = true;
	}
	return imported || (sqlite3_total_changes64(MemoryHandle) != before);
}

}

void initialize() {
	std::lock_guard lock(DatabaseMutex);
	const auto &domain = Core::App().domain();
	const auto key = domain.started() ? domain.local().localKey() : nullptr;
	if (!key) {
		LOG(("[AyuGram] Database: the local key is unavailable."));
		return;
	}
	AyuVault::SetKey(key);
	if (DatabaseReady) {
		crl::async([] {
			[[maybe_unused]] const auto saved = saveSnapshot();
		});
		return;
	}

	auto retriedBusy = false;
	auto resetCorrupted = false;
	auto restoreIndex = 0;
	while (true) {
		try {
			const auto changed = prepareStorage();
			DatabaseReady = true;
			SavedChanges = sqlite3_total_changes64(MemoryHandle);
			auto saved = QFile::exists(snapshotPath());
			if (changed || !saved) {
				saved = saveSnapshot();
			}
			if (FinishLegacyMigrationPending && saved) {
				FinishLegacyMigrationPending = false;
				finishLegacyMigration();
			}
			crl::async([] {
				AyuVault::SealFilesIn("./tdata/ayu_media");
				[[maybe_unused]] const auto created = AyuDatabaseBackup::create();
			});
			AyuDatabaseBackup::startPeriodic();
			if (const auto days = PurgeDays.load()) {
				purgeOlderThan(days);
			}
			return;
		} catch (const std::system_error &ex) {
			const auto code = ex.code().value() & 0xFF;
			const auto sqliteError = (ex.code().category() == sqlite_orm::get_sqlite_error_category());
			LOG(("[AyuGram] Database initialization failed (%1): %2").arg(code).arg(ex.what()));
			if (sqliteError && (code == kSqliteBusy || code == kSqliteLocked) && !retriedBusy) {
				retriedBusy = true;
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				continue;
			}
			if (sqliteError && (code == kSqliteCorrupt || code == kSqliteNotADatabase)) {
				moveCurrentDatabase();
				resetMemoryDatabase();
				if (AyuDatabaseBackup::restore(restoreIndex)) {
					continue;
				} else if (!resetCorrupted) {
					resetCorrupted = true;
					continue;
				}
			}
			break;
		} catch (const std::exception &ex) {
			LOG(("[AyuGram] Database initialization failed: %1").arg(ex.what()));
			break;
		}
	}

	LOG(("[AyuGram] Database is unavailable, messages will not be saved in this session."));
}

void purgeOlderThan(int days) {
	if (days <= 0) {
		PurgeDays = 0;
		return;
	}
	PurgeDays = days;
	crl::async([=] {
		const auto cutoff = int(base::unixtime::now() - int64(days) * 86400);
		runVoid("purge old messages", [&] {
			inTransaction([&] {
				storage.remove_all<DeletedMessage>(
					where(column<DeletedMessage>(&DeletedMessage::entityCreateDate) < cutoff));
				storage.remove_all<EditedMessage>(
					where(column<EditedMessage>(&EditedMessage::entityCreateDate) < cutoff));
				storage.remove_all<DeletedExtra>(
					where(column<DeletedExtra>(&DeletedExtra::entityCreateDate) < cutoff));
				storage.remove_all<DeletedMarkup>(
					where(column<DeletedMarkup>(&DeletedMarkup::entityCreateDate) < cutoff));
			});
		});
	});
}

void addEditedMessage(const EditedMessage &message, int maxRevisions) {
	runVoid("save edited message", [&] {
		inTransaction([&] {
			const auto saved = storage.count<EditedMessage>(
				where(
					column<EditedMessage>(&EditedMessage::userId) == message.userId and
					column<EditedMessage>(&EditedMessage::dialogId) == message.dialogId and
					column<EditedMessage>(&EditedMessage::messageId) == message.messageId
				));
			if (!maxRevisions || saved < maxRevisions) {
				storage.insert(message);
			}
		});
	});
}

std::vector<EditedMessage> getEditedMessages(ID userId, ID dialogId, ID messageId, ID minId, ID maxId, int totalLimit) {
	return run<std::vector<EditedMessage>>("load edited messages", {}, [&] {
		return storage.get_all<EditedMessage>(
			where(
				column<EditedMessage>(&EditedMessage::userId) == userId and
				column<EditedMessage>(&EditedMessage::dialogId) == dialogId and
				column<EditedMessage>(&EditedMessage::messageId) == messageId and
				(column<EditedMessage>(&EditedMessage::fakeId) > minId or minId == 0) and
				(column<EditedMessage>(&EditedMessage::fakeId) < maxId or maxId == 0)
			),
			order_by(column<EditedMessage>(&EditedMessage::fakeId)).desc(),
			limit(totalLimit)
		);
	});
}

bool hasRevisions(ID userId, ID dialogId, ID messageId) {
	return run<bool>("check edited messages", false, [&] {
		return !storage.select(
			columns(column<EditedMessage>(&EditedMessage::messageId)),
			where(
				column<EditedMessage>(&EditedMessage::userId) == userId and
				column<EditedMessage>(&EditedMessage::dialogId) == dialogId and
				column<EditedMessage>(&EditedMessage::messageId) == messageId
			),
			limit(1)
		).empty();
	});
}

void addDeletedMessages(const std::vector<DeletedMessage> &messages) {
	if (messages.empty()) {
		return;
	}
	runVoid("save deleted messages", [&] {
		inTransaction([&] {
			for (const auto &message : messages) {
				const auto exists = storage.count<DeletedMessage>(
					where(
						column<DeletedMessage>(&DeletedMessage::userId) == message.userId and
						column<DeletedMessage>(&DeletedMessage::dialogId) == message.dialogId and
						column<DeletedMessage>(&DeletedMessage::messageId) == message.messageId
					)
				) > 0;
				if (!exists) {
					storage.insert(message);
				}
			}
		});
	});
}

void addDeletedExtras(const std::vector<DeletedExtra> &extras) {
	if (extras.empty()) {
		return;
	}
	runVoid("save deleted extras", [&] {
		inTransaction([&] {
			for (const auto &extra : extras) {
				storage.remove_all<DeletedExtra>(
					where(
						column<DeletedExtra>(&DeletedExtra::userId) == extra.userId and
						column<DeletedExtra>(&DeletedExtra::dialogId) == extra.dialogId and
						column<DeletedExtra>(&DeletedExtra::messageId) == extra.messageId
					)
				);
				storage.insert(extra);
			}
		});
	});
}

void addDeletedMarkups(const std::vector<DeletedMarkup> &markups) {
	if (markups.empty()) {
		return;
	}
	runVoid("save deleted markups", [&] {
		inTransaction([&] {
			for (const auto &markup : markups) {
				storage.remove_all<DeletedMarkup>(
					where(
						column<DeletedMarkup>(&DeletedMarkup::userId) == markup.userId and
						column<DeletedMarkup>(&DeletedMarkup::dialogId) == markup.dialogId and
						column<DeletedMarkup>(&DeletedMarkup::messageId) == markup.messageId
					)
				);
				storage.insert(markup);
			}
		});
	});
}

std::vector<DeletedMarkup> getDeletedMarkups(ID userId, ID dialogId) {
	return run<std::vector<DeletedMarkup>>("load deleted markups", {}, [&] {
		return storage.get_all<DeletedMarkup>(
			where(
				column<DeletedMarkup>(&DeletedMarkup::userId) == userId and
				column<DeletedMarkup>(&DeletedMarkup::dialogId) == dialogId
			));
	});
}

std::vector<DeletedExtra> getDeletedExtras(ID userId, ID dialogId) {
	return run<std::vector<DeletedExtra>>("load deleted extras", {}, [&] {
		return storage.get_all<DeletedExtra>(
			where(
				column<DeletedExtra>(&DeletedExtra::userId) == userId and
				column<DeletedExtra>(&DeletedExtra::dialogId) == dialogId
			));
	});
}

void addDeletedMessage(const DeletedMessage &message) {
	addDeletedMessages({message});
}

std::vector<DeletedMessage> getDeletedMessages(ID userId, ID dialogId, ID topicId, ID minId, ID maxId, int totalLimit, const std::string &searchQuery) {
	return run<std::vector<DeletedMessage>>("load deleted messages", {}, [&] {
		if (searchQuery.empty()) {
			return storage.get_all<DeletedMessage>(
				where(
					column<DeletedMessage>(&DeletedMessage::userId) == userId and
					column<DeletedMessage>(&DeletedMessage::dialogId) == dialogId and
					(column<DeletedMessage>(&DeletedMessage::topicId) == topicId or topicId == 0) and
					(column<DeletedMessage>(&DeletedMessage::messageId) > minId or minId == 0) and
					(column<DeletedMessage>(&DeletedMessage::messageId) < maxId or maxId == 0)
				),
				order_by(column<DeletedMessage>(&DeletedMessage::messageId)).desc(),
				limit(totalLimit)
			);
		}

		std::string escaped;
		escaped.reserve(searchQuery.size());
		for (const auto c : searchQuery) {
			if (c == '%' || c == '_' || c == '\\') {
				escaped += '\\';
			}
			escaped += c;
		}
		const auto pattern = "%" + escaped + "%";
		const auto idQuery = AyuIdSearch::Parse(
			QString::fromStdString(searchQuery).trimmed());
		const auto senderId = idQuery.valid() ? ID(idQuery.id) : ID(-1);
		return storage.get_all<DeletedMessage>(
			where(
				column<DeletedMessage>(&DeletedMessage::userId) == userId and
				column<DeletedMessage>(&DeletedMessage::dialogId) == dialogId and
				(column<DeletedMessage>(&DeletedMessage::topicId) == topicId or topicId == 0) and
				(column<DeletedMessage>(&DeletedMessage::messageId) > minId or minId == 0) and
				(column<DeletedMessage>(&DeletedMessage::messageId) < maxId or maxId == 0) and
				(like(column<DeletedMessage>(&DeletedMessage::text), pattern, "\\") or
				column<DeletedMessage>(&DeletedMessage::fromId) == senderId)
			),
			order_by(column<DeletedMessage>(&DeletedMessage::messageId)).desc(),
			limit(totalLimit)
		);
	});
}

std::vector<DeletedMessage> searchDeletedMessages(ID userId, ID dialogId, ID topicId, ID fromId, const std::string &searchQuery, int totalLimit, bool withMedia) {
	return run<std::vector<DeletedMessage>>("search deleted messages", {}, [&] {
		std::string escaped;
		escaped.reserve(searchQuery.size());
		for (const auto c : searchQuery) {
			if (c == '%' || c == '_' || c == '\\') {
				escaped += '\\';
			}
			escaped += c;
		}
		const auto pattern = "%" + escaped + "%";
		return storage.get_all<DeletedMessage>(
			where(
				column<DeletedMessage>(&DeletedMessage::userId) == userId and
				(column<DeletedMessage>(&DeletedMessage::dialogId) == dialogId or dialogId == 0) and
				(column<DeletedMessage>(&DeletedMessage::topicId) == topicId or topicId == 0) and
				(column<DeletedMessage>(&DeletedMessage::fromId) == fromId or fromId == 0) and
				(column<DeletedMessage>(&DeletedMessage::documentType) == 1 or !withMedia) and
				like(column<DeletedMessage>(&DeletedMessage::text), pattern, "\\")
			),
			order_by(column<DeletedMessage>(&DeletedMessage::date)).desc(),
			limit(totalLimit)
		);
	});
}

bool hasDeletedMessages(ID userId, ID dialogId, ID topicId) {
	return run<bool>("check deleted messages", false, [&] {
		return !storage.select(
			columns(column<DeletedMessage>(&DeletedMessage::dialogId)),
			where(
				column<DeletedMessage>(&DeletedMessage::userId) == userId and
				column<DeletedMessage>(&DeletedMessage::dialogId) == dialogId and
				(column<DeletedMessage>(&DeletedMessage::topicId) == topicId or topicId == 0)
			),
			limit(1)
		).empty();
	});
}

std::vector<ID> getDeletedDialogIds(ID userId) {
	return run<std::vector<ID>>("load deleted dialogs", {}, [&] {
		return storage.select(
			distinct(column<DeletedMessage>(&DeletedMessage::dialogId)),
			where(column<DeletedMessage>(&DeletedMessage::userId) == userId));
	});
}

void removeDeletedMessage(ID userId, ID dialogId, ID messageId) {
	runVoid("remove deleted message", [&] {
		storage.remove_all<DeletedExtra>(
			where(
				column<DeletedExtra>(&DeletedExtra::userId) == userId and
				column<DeletedExtra>(&DeletedExtra::dialogId) == dialogId and
				column<DeletedExtra>(&DeletedExtra::messageId) == messageId
			)
		);
		storage.remove_all<DeletedMarkup>(
			where(
				column<DeletedMarkup>(&DeletedMarkup::userId) == userId and
				column<DeletedMarkup>(&DeletedMarkup::dialogId) == dialogId and
				column<DeletedMarkup>(&DeletedMarkup::messageId) == messageId
			)
		);
		storage.remove_all<DeletedMessage>(
			where(
				column<DeletedMessage>(&DeletedMessage::userId) == userId and
				column<DeletedMessage>(&DeletedMessage::dialogId) == dialogId and
				column<DeletedMessage>(&DeletedMessage::messageId) == messageId
			)
		);
	});
}

void clearDeletedMessages(ID userId, ID dialogId, ID topicId) {
	runVoid("clear deleted messages", [&] {
		if (!topicId) {
			storage.remove_all<DeletedExtra>(
				where(
					column<DeletedExtra>(&DeletedExtra::userId) == userId and
					column<DeletedExtra>(&DeletedExtra::dialogId) == dialogId
				)
			);
			storage.remove_all<DeletedMarkup>(
				where(
					column<DeletedMarkup>(&DeletedMarkup::userId) == userId and
					column<DeletedMarkup>(&DeletedMarkup::dialogId) == dialogId
				)
			);
		}
		storage.remove_all<DeletedMessage>(
			where(
				column<DeletedMessage>(&DeletedMessage::userId) == userId and
				column<DeletedMessage>(&DeletedMessage::dialogId) == dialogId and
				(column<DeletedMessage>(&DeletedMessage::topicId) == topicId or topicId == 0)
			)
		);
	});
}

void clearAllDeleted() {
	runVoid("clear all deleted", [&] {
		inTransaction([&] {
			storage.remove_all<DeletedExtra>();
			storage.remove_all<DeletedMarkup>();
			storage.remove_all<DeletedMessage>();
			storage.remove_all<DeletedDialog>();
			storage.remove_all<KeptTopic>();
			storage.remove_all<KeptDialog>();
		});
	});
	AyuDatabaseBackup::removeAll();
}

template<typename Callback>
void forEachUserTable(Callback &&callback) {
	callback(std::type_identity<DeletedExtra>());
	callback(std::type_identity<DeletedMarkup>());
	callback(std::type_identity<DeletedMessage>());
	callback(std::type_identity<EditedMessage>());
	callback(std::type_identity<DeletedDialog>());
	callback(std::type_identity<KeptTopic>());
	callback(std::type_identity<KeptDialog>());
	callback(std::type_identity<KnownUser>());
	callback(std::type_identity<LocalPin>());
	callback(std::type_identity<SecretMessageRow>());
	callback(std::type_identity<SecretChatRow>());
	callback(std::type_identity<SecretStateRow>());
	callback(std::type_identity<PeekedStatusRow>());
	callback(std::type_identity<PeekRestoreRow>());
	callback(std::type_identity<SpyMessageRead>());
	callback(std::type_identity<SpyMessageContentsRead>());
}

void clearUserData(ID userId) {
	runVoid("clear user data", [&] {
		inTransaction([&] {
			forEachUserTable([&](auto tag) {
				using Row = typename decltype(tag)::type;
				storage.remove_all<Row>(
					where(column<Row>(&Row::userId) == userId));
			});
		});
		storage.vacuum();
	});
	AyuDatabaseBackup::purgeUser(userId);
	flush();
}

void clearEverything() {
	runVoid("clear everything", [&] {
		inTransaction([&] {
			forEachUserTable([&](auto tag) {
				using Row = typename decltype(tag)::type;
				storage.remove_all<Row>();
			});
		});
		storage.vacuum();
	});
	AyuDatabaseBackup::removeAll();
	flush();
}

bool exportSealed(QByteArray &sealed, qint64 &rows) {
	if (!AyuVault::HasKey()) {
		return false;
	}
	auto plain = QByteArray();
	{
		std::lock_guard lock(DatabaseMutex);
		if (!DatabaseReady || !MemoryHandle) {
			return false;
		}
		auto size = sqlite3_int64();
		const auto data = sqlite3_serialize(MemoryHandle, "main", &size, 0);
		if (!data) {
			return false;
		}
		plain = QByteArray(
			reinterpret_cast<const char*>(data),
			qsizetype(size));
		sqlite3_free(data);
		rows = 0;
		try {
			rows = storage.count<DeletedMessage>()
				+ storage.count<EditedMessage>();
		} catch (...) {
		}
	}
	sealed = AyuVault::Seal(plain);
	return true;
}

void flush() {
	[[maybe_unused]] const auto saved = saveSnapshot();
}

void finish() {
	SaveTimer = nullptr;
	flush();
}

void saveKeptDialog(const KeptDialog &dialog) {
	runVoid("save kept dialog", [&] {
		inTransaction([&] {
			storage.remove_all<KeptDialog>(
				where(
					column<KeptDialog>(&KeptDialog::userId) == dialog.userId and
					column<KeptDialog>(&KeptDialog::dialogId) == dialog.dialogId
				)
			);
			storage.insert(dialog);
		});
	});
}

void saveKeptTopics(const std::vector<KeptTopic> &topics) {
	if (topics.empty()) {
		return;
	}
	runVoid("save kept topics", [&] {
		inTransaction([&] {
			for (const auto &topic : topics) {
				storage.remove_all<KeptTopic>(
					where(
						column<KeptTopic>(&KeptTopic::userId) == topic.userId and
						column<KeptTopic>(&KeptTopic::dialogId) == topic.dialogId and
						column<KeptTopic>(&KeptTopic::rootId) == topic.rootId
					)
				);
				storage.insert(topic);
			}
		});
	});
}

std::vector<KeptTopic> getKeptTopics(ID userId, ID dialogId) {
	return run<std::vector<KeptTopic>>("load kept topics", {}, [&] {
		return storage.get_all<KeptTopic>(
			where(
				column<KeptTopic>(&KeptTopic::userId) == userId and
				column<KeptTopic>(&KeptTopic::dialogId) == dialogId
			));
	});
}

std::vector<KeptTopic> getKeptTopicsFor(ID userId) {
	return run<std::vector<KeptTopic>>("load all kept topics", {}, [&] {
		return storage.get_all<KeptTopic>(
			where(column<KeptTopic>(&KeptTopic::userId) == userId));
	});
}

void removeKeptTopic(ID userId, ID dialogId, ID rootId) {
	runVoid("remove kept topic", [&] {
		storage.remove_all<KeptTopic>(
			where(
				column<KeptTopic>(&KeptTopic::userId) == userId and
				column<KeptTopic>(&KeptTopic::dialogId) == dialogId and
				column<KeptTopic>(&KeptTopic::rootId) == rootId
			)
		);
	});
}

void removeKeptTopics(ID userId, ID dialogId) {
	runVoid("remove kept topics", [&] {
		storage.remove_all<KeptTopic>(
			where(
				column<KeptTopic>(&KeptTopic::userId) == userId and
				column<KeptTopic>(&KeptTopic::dialogId) == dialogId
			)
		);
	});
}

void syncKeptDialogs(const std::vector<KeptDialog> &dialogs) {
	if (dialogs.empty()) {
		return;
	}
	runVoid("sync kept dialogs", [&] {
		inTransaction([&] {
			for (const auto &dialog : dialogs) {
				const auto lost = storage.count<KeptDialog>(
					where(
						column<KeptDialog>(&KeptDialog::userId) == dialog.userId and
						column<KeptDialog>(&KeptDialog::dialogId) == dialog.dialogId and
						column<KeptDialog>(&KeptDialog::lost) == 1
					));
				if (lost) {
					continue;
				}
				storage.remove_all<KeptDialog>(
					where(
						column<KeptDialog>(&KeptDialog::userId) == dialog.userId and
						column<KeptDialog>(&KeptDialog::dialogId) == dialog.dialogId
					)
				);
				storage.insert(dialog);
			}
		});
	});
}

std::vector<KeptDialog> getKeptDialogs(ID userId) {
	return run<std::vector<KeptDialog>>("load kept dialogs", {}, [&] {
		return storage.get_all<KeptDialog>(
			where(column<KeptDialog>(&KeptDialog::userId) == userId));
	});
}

void removeKeptDialog(ID userId, ID dialogId) {
	runVoid("remove kept dialog", [&] {
		storage.remove_all<KeptDialog>(
			where(
				column<KeptDialog>(&KeptDialog::userId) == userId and
				column<KeptDialog>(&KeptDialog::dialogId) == dialogId
			)
		);
	});
}

std::vector<ID> getLocalPins(ID userId) {
	return run<std::vector<ID>>("load local pins", {}, [&] {
		auto result = std::vector<ID>();
		const auto rows = storage.get_all<LocalPin>(
			where(column<LocalPin>(&LocalPin::userId) == userId),
			order_by(column<LocalPin>(&LocalPin::position)));
		result.reserve(rows.size());
		for (const auto &row : rows) {
			result.push_back(row.peerId);
		}
		return result;
	});
}

void saveLocalPins(ID userId, const std::vector<ID> &peers) {
	runVoid("save local pins", [&] {
		inTransaction([&] {
			storage.remove_all<LocalPin>(
				where(column<LocalPin>(&LocalPin::userId) == userId));
			auto position = 0;
			for (const auto peerId : peers) {
				storage.insert(LocalPin{ 0, userId, peerId, position++ });
			}
		});
	});
}

void saveKnownUsers(const std::vector<KnownUser> &users) {
	if (users.empty()) {
		return;
	}
	runVoid("save known users", [&] {
		inTransaction([&] {
			for (const auto &user : users) {
				const auto same = [&](const KnownUser &row) {
					return row.accessHash == user.accessHash
						&& row.firstName == user.firstName
						&& row.lastName == user.lastName
						&& row.username == user.username;
				};
				const auto sameUser = where(
					column<KnownUser>(&KnownUser::userId) == user.userId and
					column<KnownUser>(&KnownUser::peerId) == user.peerId);
				const auto existing = storage.get_all<KnownUser>(sameUser, limit(1));
				if (!existing.empty() && same(existing.front())) {
					continue;
				}
				storage.remove_all<KnownUser>(sameUser);
				storage.insert(user);
			}
		});
	});
}

std::optional<KnownUser> getKnownUser(ID userId, ID peerId) {
	return run<std::optional<KnownUser>>("load known user", std::nullopt, [&] {
		auto rows = storage.get_all<KnownUser>(
			where(
				column<KnownUser>(&KnownUser::userId) == userId and
				column<KnownUser>(&KnownUser::peerId) == peerId
			),
			limit(1));
		return rows.empty()
			? std::optional<KnownUser>()
			: std::optional<KnownUser>(std::move(rows.front()));
	});
}

void saveSecretChat(const SecretChatRow &chat) {
	runVoid("save secret chat", [&] {
		inTransaction([&] {
			storage.remove_all<SecretChatRow>(
				where(
					column<SecretChatRow>(&SecretChatRow::userId) == chat.userId and
					column<SecretChatRow>(&SecretChatRow::chatId) == chat.chatId
				)
			);
			storage.insert(chat);
		});
	});
}

std::vector<SecretChatRow> getSecretChats(ID userId) {
	return run<std::vector<SecretChatRow>>("load secret chats", {}, [&] {
		return storage.get_all<SecretChatRow>(
			where(column<SecretChatRow>(&SecretChatRow::userId) == userId));
	});
}

void removeSecretChat(ID userId, int chatId) {
	runVoid("remove secret chat", [&] {
		inTransaction([&] {
			storage.remove_all<SecretChatRow>(
				where(
					column<SecretChatRow>(&SecretChatRow::userId) == userId and
					column<SecretChatRow>(&SecretChatRow::chatId) == chatId
				)
			);
			storage.remove_all<SecretMessageRow>(
				where(
					column<SecretMessageRow>(&SecretMessageRow::userId) == userId and
					column<SecretMessageRow>(&SecretMessageRow::chatId) == chatId
				)
			);
		});
	});
}

bool addSecretMessage(const SecretMessageRow &message) {
	return run<bool>("save secret message", false, [&] {
		const auto exists = storage.count<SecretMessageRow>(
			where(
				column<SecretMessageRow>(&SecretMessageRow::userId) == message.userId and
				column<SecretMessageRow>(&SecretMessageRow::chatId) == message.chatId and
				column<SecretMessageRow>(&SecretMessageRow::randomId) == message.randomId
			)
		) > 0;
		if (exists) {
			return false;
		}
		storage.insert(message);
		return true;
	});
}

void updateSecretMessage(const SecretMessageRow &message) {
	runVoid("update secret message", [&] {
		storage.update_all(
			set(
				assign(&SecretMessageRow::kind, message.kind),
				assign(&SecretMessageRow::outgoing, message.outgoing),
				assign(&SecretMessageRow::seqIn, message.seqIn),
				assign(&SecretMessageRow::seqOut, message.seqOut),
				assign(&SecretMessageRow::date, message.date),
				assign(&SecretMessageRow::text, message.text),
				assign(&SecretMessageRow::payload, message.payload)
			),
			where(
				column<SecretMessageRow>(&SecretMessageRow::userId) == message.userId and
				column<SecretMessageRow>(&SecretMessageRow::chatId) == message.chatId and
				column<SecretMessageRow>(&SecretMessageRow::randomId) == message.randomId
			)
		);
	});
}

std::vector<SecretMessageRow> getSecretMessages(ID userId, int chatId) {
	return run<std::vector<SecretMessageRow>>("load secret messages", {}, [&] {
		return storage.get_all<SecretMessageRow>(
			where(
				column<SecretMessageRow>(&SecretMessageRow::userId) == userId and
				column<SecretMessageRow>(&SecretMessageRow::chatId) == chatId
			),
			order_by(column<SecretMessageRow>(&SecretMessageRow::fakeId)).asc()
		);
	});
}

void removeSecretMessage(ID userId, int chatId, ID randomId) {
	runVoid("remove secret message", [&] {
		storage.remove_all<SecretMessageRow>(
			where(
				column<SecretMessageRow>(&SecretMessageRow::userId) == userId and
				column<SecretMessageRow>(&SecretMessageRow::chatId) == chatId and
				column<SecretMessageRow>(&SecretMessageRow::randomId) == randomId
			)
		);
	});
}

void clearSecretMessages(ID userId, int chatId) {
	runVoid("clear secret messages", [&] {
		storage.remove_all<SecretMessageRow>(
			where(
				column<SecretMessageRow>(&SecretMessageRow::userId) == userId and
				column<SecretMessageRow>(&SecretMessageRow::chatId) == chatId
			)
		);
	});
}

void savePeekedStatus(const PeekedStatusRow &row) {
	runVoid("save peeked status", [&] {
		inTransaction([&] {
			storage.remove_all<PeekedStatusRow>(
				where(
					column<PeekedStatusRow>(&PeekedStatusRow::userId) == row.userId and
					column<PeekedStatusRow>(&PeekedStatusRow::targetId) == row.targetId
				)
			);
			storage.insert(row);
		});
	});
}

std::vector<PeekedStatusRow> getPeekedStatus(ID userId, ID targetId) {
	return run<std::vector<PeekedStatusRow>>("load peeked status", {}, [&] {
		return storage.get_all<PeekedStatusRow>(
			where(
				column<PeekedStatusRow>(&PeekedStatusRow::userId) == userId and
				column<PeekedStatusRow>(&PeekedStatusRow::targetId) == targetId
			)
		);
	});
}

void savePeekRestore(const PeekRestoreRow &row) {
	runVoid("save peek restore", [&] {
		inTransaction([&] {
			storage.remove_all<PeekRestoreRow>(
				where(column<PeekRestoreRow>(&PeekRestoreRow::userId) == row.userId));
			storage.insert(row);
		});
	});
}

void saveSecretState(const SecretStateRow &row) {
	runVoid("save secret state", [&] {
		inTransaction([&] {
			storage.remove_all<SecretStateRow>(
				where(column<SecretStateRow>(&SecretStateRow::userId) == row.userId));
			storage.insert(row);
		});
	});
}

std::vector<SecretStateRow> getSecretState(ID userId) {
	return run<std::vector<SecretStateRow>>("load secret state", {}, [&] {
		return storage.get_all<SecretStateRow>(
			where(column<SecretStateRow>(&SecretStateRow::userId) == userId));
	});
}

std::vector<PeekRestoreRow> getPeekRestore(ID userId) {
	return run<std::vector<PeekRestoreRow>>("load peek restore", {}, [&] {
		return storage.get_all<PeekRestoreRow>(
			where(column<PeekRestoreRow>(&PeekRestoreRow::userId) == userId));
	});
}

void clearPeekRestore(ID userId) {
	runVoid("clear peek restore", [&] {
		storage.remove_all<PeekRestoreRow>(
			where(column<PeekRestoreRow>(&PeekRestoreRow::userId) == userId));
	});
}

template<typename T>
std::vector<T> getAllT() {
	return run<std::vector<T>>("load all", {}, [&] {
		return storage.get_all<T>();
	});
}

std::vector<RegexFilter> getAllRegexFilters() {
	return getAllT<RegexFilter>();
}

std::vector<RegexFilterGlobalExclusion> getAllFiltersExclusions() {
	return getAllT<RegexFilterGlobalExclusion>();
}

std::vector<RegexFilter> getExcludedByDialogId(ID dialogId) {
	return run<std::vector<RegexFilter>>("load excluded filters", {}, [&] {
		return storage.get_all<RegexFilter>(
			where(in(&RegexFilter::id,
					 storage.select(columns(&RegexFilterGlobalExclusion::filterId),
									where(is_equal(&RegexFilterGlobalExclusion::dialogId, dialogId))
					 )
			))
		);
	});
}

int getCount() {
	return run<int>("count filters", 0, [&] {
		return storage.count<RegexFilter>();
	});
}

RegexFilter getById(std::vector<char> id) {
	return run<RegexFilter>("load filter", RegexFilter{}, [&] {
		return storage.get<RegexFilter>(
			where(column<RegexFilter>(&RegexFilter::id) == std::move(id))
		);
	});
}

std::vector<RegexFilter> getShared() {
	return run<std::vector<RegexFilter>>("load shared filters", {}, [&] {
		return storage.get_all<RegexFilter>(
			where(is_null(column<RegexFilter>(&RegexFilter::dialogId)))
		);
	});
}

std::vector<RegexFilter> getByDialogId(ID dialogId) {
	return run<std::vector<RegexFilter>>("load dialog filters", {}, [&] {
		return storage.get_all<RegexFilter>(
			where(column<RegexFilter>(&RegexFilter::dialogId) == dialogId)
		);
	});
}

void addRegexFilter(const RegexFilter &filter) {
	runVoid("save regex filter", [&] {
		inTransaction([&] {
			storage.replace(filter); // we're using replace as we set std::vector<char> as primary key
		});
	});
}

void addRegexExclusion(const RegexFilterGlobalExclusion &exclusion) {
	runVoid("save regex filter exclusion", [&] {
		inTransaction([&] {
			storage.insert(exclusion);
		});
	});
}

void updateRegexFilter(const RegexFilter &filter) {
	runVoid("update regex filter", [&] {
		storage.update_all(
			set(
				c(&RegexFilter::text) = filter.text,
				c(&RegexFilter::enabled) = filter.enabled,
				c(&RegexFilter::reversed) = filter.reversed,
				c(&RegexFilter::caseInsensitive) = filter.caseInsensitive,
				c(&RegexFilter::dialogId) = filter.dialogId
			),
			where(c(&RegexFilter::id) == filter.id)
		);
	});
}

void deleteFilter(const std::vector<char> &id) {
	runVoid("delete regex filter", [&] {
		storage.remove_all<RegexFilter>(
			where(column<RegexFilter>(&RegexFilter::id) == id)
		);
	});
}

void deleteExclusionsByFilterId(const std::vector<char> &id) {
	runVoid("delete regex filter exclusions", [&] {
		storage.remove_all<RegexFilterGlobalExclusion>(
			where(column<RegexFilterGlobalExclusion>(&RegexFilterGlobalExclusion::filterId) == id)
		);
	});
}

void deleteExclusion(ID dialogId, std::vector<char> filterId) {
	runVoid("delete regex filter exclusion", [&] {
		storage.remove_all<RegexFilterGlobalExclusion>(
			where(column<RegexFilterGlobalExclusion>(&RegexFilterGlobalExclusion::filterId) == filterId and
				column<RegexFilterGlobalExclusion>(&RegexFilterGlobalExclusion::dialogId) == dialogId
			)
		);
	});
}

void deleteAllFilters() {
	runVoid("delete all regex filters", [&] {
		storage.remove_all<RegexFilter>();
	});
}

void deleteAllExclusions() {
	runVoid("delete all regex filter exclusions", [&] {
		storage.remove_all<RegexFilterGlobalExclusion>();
	});
}

bool hasFilters() {
	return run<bool>("check regex filters", false, [&] {
		return !storage.select(
			columns(column<RegexFilter>(&RegexFilter::id)),
			limit(1)
		).empty();
	});
}

bool hasPerDialogFilters() {
	return run<bool>("check per dialog filters", false, [&] {
		return
			!storage.select(
				columns(column<RegexFilter>(&RegexFilter::id)),
				where(is_not_null(column<RegexFilter>(&RegexFilter::dialogId))),
				limit(1)
			).empty() ||
			!storage.select(
				columns(column<RegexFilterGlobalExclusion>(&RegexFilterGlobalExclusion::fakeId)),
				limit(1)
			).empty();
	});
}

}
