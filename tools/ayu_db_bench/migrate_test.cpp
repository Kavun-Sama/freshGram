// This is the source code of freshGram.
#include "ayu/data/ayu_encrypted_vfs.h"
#include "ayu/data/ayu_legacy_copy.h"
#include "ayu/data/ayu_migration_v4.h"
#include "ayu/data/message_rows.h"
#include "ayu/data/message_schema.h"
#include "test_common.h"
#include "ayu/data/ayu_database_report.h"
#include "ayu/libs/sqlite/sqlite3.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

using namespace test;

struct SchemaVersion {
	int id;
	int version;
};

int main(int argc, char **argv) {
	setvbuf(stdout, nullptr, _IONBF, 0);
	if (argc < 3) {
		std::printf("usage: %s <database> <scratch directory>\n", argv[0]);
		return 1;
	}
	using namespace sqlite_orm;
	const auto importMode = (argc > 3) && std::string(argv[3]) == "import";
	const auto original = std::string(argv[2]) + "/migrate_original.db";
	const auto work = std::string(argv[2]) + (importMode ? "/import_work.sqlite" : "/migrate_work.db");
	unsigned char masterKey[32];
	std::memset(masterKey, 7, sizeof(masterKey));
	if (importMode) {
		std::remove(work.c_str());
		std::remove((work + "-journal").c_str());
		if (!AyuEncryptedVfs::Register(masterKey, sizeof(masterKey), work.c_str())) {
			std::printf("register failed\n");
			return 1;
		}
	}
	std::system(("cp '" + std::string(argv[1]) + "' '" + original + "'").c_str());
	AddSynthetic(original);
	if (!importMode) {
		std::system(("cp '" + original + "' '" + work + "'").c_str());
	}

	auto storage = make_storage(
		work,
		AyuMessageSchema::DeletedKeyIndex(),
		AyuMessageSchema::DeletedTopicIndex(),
		AyuMessageSchema::EditedKeyIndex(),
		AyuMessageSchema::DeletedTable(),
		AyuMessageSchema::EditedTable(),
		make_table<SchemaVersion>(
			"SchemaVersion",
			make_column("id", &SchemaVersion::id, primary_key()),
			make_column("version", &SchemaVersion::version)));
	sqlite3 *db = nullptr;
	storage.on_open = [&](sqlite3 *handle) {
		db = handle;
		if (importMode) {
			Exec(handle, "PRAGMA page_size = 4096");
			int reserve = AyuEncryptedVfs::kReserveBytes;
			sqlite3_file_control(handle, "main", SQLITE_FCNTL_RESERVE_BYTES, &reserve);
			Exec(handle, "PRAGMA auto_vacuum = INCREMENTAL");
			Exec(handle, "PRAGMA journal_mode = TRUNCATE");
			Exec(handle, "PRAGMA synchronous = NORMAL");
			Exec(handle, "PRAGMA locking_mode = EXCLUSIVE");
			Exec(handle, "PRAGMA mmap_size = 0");
			Exec(handle, "PRAGMA secure_delete = ON");
			Exec(handle, "PRAGMA cache_size = -65536");
		}
	};
	storage.open_forever();
	if (!importMode) {
		Exec(db, "PRAGMA journal_mode = MEMORY");
		Exec(db, "PRAGMA synchronous = OFF");
	}
	auto from = Clock::now();
	try {
		const auto synced = storage.sync_schema(true);
		for (const auto &[name, state] : synced) {
			std::printf("  sync %s: %d\n", name.c_str(), int(state));
		}
	} catch (const std::exception &error) {
		std::printf("sync_schema failed: %s\n", error.what());
		return 4;
	}
	std::printf("sync_schema: %.0f ms\n", MsSince(from));

	sqlite3 *old = nullptr;
	sqlite3_open_v2(original.c_str(), &old, SQLITE_OPEN_READONLY, nullptr);
	const auto source = importMode ? old : db;
	const auto oldRows = Scalar(source, "SELECT count(*) FROM DeletedMessage");
	const auto oldKeys = Scalar(source, "SELECT count(*) FROM (SELECT 1 FROM DeletedMessage GROUP BY userId, dialogId, messageId)");
	const auto oldEdited = Scalar(source, "SELECT count(*) FROM EditedMessage");
	const auto oldTextBytes = Scalar(source, "SELECT sum(length(CAST(text AS BLOB))) FROM DeletedMessage "
		"WHERE fakeId IN (SELECT max(fakeId) FROM DeletedMessage GROUP BY userId, dialogId, messageId)");
	std::printf("before: %lld deleted rows (%lld keys), %lld edited, %lld text bytes, file pages %lld\n",
		oldRows, oldKeys, oldEdited, oldTextBytes, Scalar(db, "PRAGMA page_count"));

	from = Clock::now();
	if (importMode) {
		Exec(db, "ATTACH DATABASE '" + original + "' AS legacy");
		Exec(db, "BEGIN IMMEDIATE");
		const auto copied = AyuLegacyCopy::Run(db, "legacy");
		if (!copied.ok) {
			std::printf("IMPORT FAILED: %s\n", copied.error.c_str());
			Exec(db, "ROLLBACK");
			return 2;
		}
		Exec(db, "INSERT OR REPLACE INTO SchemaVersion (id, version) VALUES (1, 4)");
		Exec(db, "COMMIT");
		Exec(db, "DETACH DATABASE legacy");
		std::printf("import: %.0f ms (deleted %lld, edited %lld, others %lld)\n", MsSince(from),
			copied.deleted, copied.edited, copied.others);
		std::printf("after the import: pages %lld, free %lld\n", Scalar(db, "PRAGMA page_count"), Scalar(db, "PRAGMA freelist_count"));
	} else {
		Exec(db, "BEGIN");
		const auto result = AyuMigrationV4::Run(db);
		if (!result.ok) {
			std::printf("MIGRATION FAILED: %s\n", result.error.c_str());
			Exec(db, "ROLLBACK");
			return 2;
		}
		Exec(db, "COMMIT");
		std::printf("migration: %.0f ms (deleted %lld, edited %lld)\n", MsSince(from), result.deleted, result.edited);
		std::printf("after the copy: pages %lld, free %lld\n", Scalar(db, "PRAGMA page_count"), Scalar(db, "PRAGMA freelist_count"));
		from = Clock::now();
		Exec(db, "VACUUM");
		std::printf("vacuum: %.0f ms, pages %lld, free %lld\n", MsSince(from),
			Scalar(db, "PRAGMA page_count"), Scalar(db, "PRAGMA freelist_count"));
	}
	const auto newTextBytes = Scalar(db, "SELECT sum(length(CAST(text AS BLOB))) FROM DeletedMessageV2");
	std::printf("text bytes: %lld -> %lld (%s)\n", oldTextBytes, newTextBytes,
		oldTextBytes == newTextBytes ? "same" : "DIFFERENT");

	// Rows against the untouched original, through the real mappings.
	auto generator = std::mt19937_64(12345);
	auto keys = std::vector<std::tuple<long long, long long, long long>>();
	{
		sqlite3_stmt *s = nullptr;
		sqlite3_prepare_v2(db, "SELECT userId, dialogId, messageId FROM DeletedMessageV2 WHERE abs(random()) % 400 = 0 LIMIT 6000", -1, &s, nullptr);
		while (sqlite3_step(s) == SQLITE_ROW) {
			keys.emplace_back(sqlite3_column_int64(s, 0), sqlite3_column_int64(s, 1), sqlite3_column_int64(s, 2));
		}
		sqlite3_finalize(s);
	}
	auto bad = 0;
	for (const auto message : {1LL, 2LL, 3LL}) {
		keys.emplace_back(1LL, -777LL, message);
	}
	for (const auto &[user, dialog, message] : keys) {
		auto expected = AyuMessageBase();
		const auto condition = "userId = " + std::to_string(user) + " AND dialogId = " + std::to_string(dialog)
			+ " AND messageId = " + std::to_string(message);
		if (!ReadOld(old, "DeletedMessage", condition, expected)) {
			++bad;
			continue;
		}
		const auto found = storage.get_all<DeletedMessageRow>(
			where(
				column<DeletedMessageRow>(&DeletedMessageRow::userId) == user and
				column<DeletedMessageRow>(&DeletedMessageRow::dialogId) == dialog and
				column<DeletedMessageRow>(&DeletedMessageRow::messageId) == int(message)));
		if (found.size() != 1) {
			++bad;
			continue;
		}
		const auto &row = found.front();
		auto actual = AyuMessageRows::FromRow(row);
		expected.fakeId = actual.fakeId = 0;
		if (Differences(expected, actual, false)) {
			++bad;
		}
		const auto again = AyuMessageRows::ToRow(AyuMessageRows::FromRow(row));
		if (again.extra != row.extra || again.editDate != row.editDate || again.text != row.text) {
			++bad;
		}
	}
	std::printf("compared %zu random deleted messages with the original: %d bad\n", keys.size(), bad);

	// Edited revisions: the same key and revision number must give the same text.
	auto badEdited = 0;
	auto checkedEdited = 0;
	{
		// The revisions of the synthetic message, in the order of the old identifiers.
		for (auto nth = 0; nth != 3; ++nth) {
			auto expected = AyuMessageBase();
			const auto condition = "userId = 1 AND dialogId = -777 AND messageId = 4 AND fakeId = "
				"(SELECT fakeId FROM EditedMessage WHERE userId = 1 AND dialogId = -777 AND messageId = 4 "
				"ORDER BY fakeId LIMIT 1 OFFSET " + std::to_string(nth) + ")";
			++checkedEdited;
			const auto rows = storage.get_all<EditedMessageRow>(
				where(
					column<EditedMessageRow>(&EditedMessageRow::userId) == 1 and
					column<EditedMessageRow>(&EditedMessageRow::dialogId) == -777 and
					column<EditedMessageRow>(&EditedMessageRow::messageId) == 4),
				order_by(column<EditedMessageRow>(&EditedMessageRow::fakeId)));
			if (!ReadOld(old, "EditedMessage", condition, expected) || rows.size() != 3) {
				++badEdited;
				continue;
			}
			auto actual = AyuMessageRows::FromRow(rows[nth]);
			expected.fakeId = actual.fakeId = 0;
			if (Differences(expected, actual, false)) {
				++badEdited;
			}
		}
	}
	{
		sqlite3_stmt *s = nullptr;
		sqlite3_prepare_v2(db, "SELECT userId, dialogId, messageId, fakeId, (SELECT count(*) FROM EditedMessageV2 y WHERE y.userId = x.userId AND y.dialogId = x.dialogId AND y.messageId = x.messageId AND y.fakeId < x.fakeId) FROM EditedMessageV2 x WHERE abs(random()) % 100 = 0 LIMIT 6000", -1, &s, nullptr);
		while (sqlite3_step(s) == SQLITE_ROW) {
			const auto user = sqlite3_column_int64(s, 0);
			const auto dialog = sqlite3_column_int64(s, 1);
			const auto message = sqlite3_column_int64(s, 2);
			const auto fakeId = sqlite3_column_int64(s, 3);
			const auto nth = sqlite3_column_int(s, 4);
			auto expected = AyuMessageBase();
			const auto condition = "userId = " + std::to_string(user) + " AND dialogId = " + std::to_string(dialog)
				+ " AND messageId = " + std::to_string(message)
				+ " AND fakeId = (SELECT fakeId FROM EditedMessage WHERE userId = " + std::to_string(user)
				+ " AND dialogId = " + std::to_string(dialog) + " AND messageId = " + std::to_string(message)
				+ " ORDER BY fakeId LIMIT 1 OFFSET " + std::to_string(nth) + ")";
			++checkedEdited;
			if (!ReadOld(old, "EditedMessage", condition, expected)) {
				++badEdited;
				continue;
			}
			const auto row = storage.get_pointer<EditedMessageRow>(fakeId);
			if (!row) {
				++badEdited;
				continue;
			}
			auto actual = AyuMessageRows::FromRow(*row);
			expected.fakeId = actual.fakeId = 0;
			if (Differences(expected, actual, false)) {
				++badEdited;
			}
		}
		sqlite3_finalize(s);
	}
	std::printf("compared %d random revisions with the original: %d bad\n", checkedEdited, badEdited);
	sqlite3_close(old);

	// The queries as the application builds them.
	const auto user = Scalar(db, "SELECT userId FROM DeletedMessageV2 GROUP BY userId ORDER BY count(*) DESC LIMIT 1");
	const auto dialog = Scalar(db, "SELECT dialogId FROM DeletedMessageV2 WHERE userId = " + std::to_string(user)
		+ " GROUP BY dialogId ORDER BY count(*) DESC LIMIT 1");
	const auto minId = 0, maxId = 0, topicId = 0;
	const auto windowQuery = get_all<DeletedMessageRow>(
		where(
			column<DeletedMessageRow>(&DeletedMessageRow::userId) == user and
			column<DeletedMessageRow>(&DeletedMessageRow::dialogId) == dialog and
			(column<DeletedMessageRow>(&DeletedMessageRow::topicId) == topicId or topicId == 0) and
			(column<DeletedMessageRow>(&DeletedMessageRow::messageId) > minId or minId == 0) and
			(column<DeletedMessageRow>(&DeletedMessageRow::messageId) < maxId or maxId == 0)),
		order_by(column<DeletedMessageRow>(&DeletedMessageRow::messageId)).desc(),
		limit(50));
	std::printf("app window query: %s\n", storage.dump(windowQuery, false).c_str());
	{
		sqlite3_stmt *s = nullptr;
		sqlite3_prepare_v2(db, ("EXPLAIN QUERY PLAN " + storage.dump(windowQuery, false)).c_str(), -1, &s, nullptr);
		while (sqlite3_step(s) == SQLITE_ROW) {
			std::printf("    %s\n", reinterpret_cast<const char*>(sqlite3_column_text(s, 3)));
		}
		sqlite3_finalize(s);
	}
	from = Clock::now();
	auto rows = 0;
	for (auto i = 0; i != 30; ++i) {
		rows = int(storage.get_all<DeletedMessageRow>(
			where(
				column<DeletedMessageRow>(&DeletedMessageRow::userId) == user and
				column<DeletedMessageRow>(&DeletedMessageRow::dialogId) == dialog and
				(column<DeletedMessageRow>(&DeletedMessageRow::topicId) == topicId or topicId == 0) and
				(column<DeletedMessageRow>(&DeletedMessageRow::messageId) > minId or minId == 0) and
				(column<DeletedMessageRow>(&DeletedMessageRow::messageId) < maxId or maxId == 0)),
			order_by(column<DeletedMessageRow>(&DeletedMessageRow::messageId)).desc(),
			limit(50)).size());
	}
	std::printf("window via the mapping: %.3f ms per call (%d rows)\n", MsSince(from) / 30, rows);

	const auto pattern = std::string("%zzqqxx%");
	from = Clock::now();
	const auto found = storage.get_all<DeletedMessageRow>(
		where(
			column<DeletedMessageRow>(&DeletedMessageRow::userId) == user and
			like(column<DeletedMessageRow>(&DeletedMessageRow::text), pattern, "\\")),
		order_by(column<DeletedMessageRow>(&DeletedMessageRow::date)).desc(),
		limit(100));
	std::printf("search everywhere via the mapping: %.0f ms (%zu rows)\n", MsSince(from), found.size());

	from = Clock::now();
	const auto ids = storage.select(
		distinct(column<DeletedMessageRow>(&DeletedMessageRow::dialogId)),
		where(column<DeletedMessageRow>(&DeletedMessageRow::userId) == user));
	std::printf("dialog list via the mapping: %.1f ms (%zu dialogs)\n", MsSince(from), ids.size());

	{
		from = Clock::now();
		sqlite3_stmt *s = nullptr;
		sqlite3_prepare_v2(db, "WITH RECURSIVE dialogs(id) AS ("
			"SELECT min(dialogId) FROM DeletedMessageV2 WHERE userId = ?1 "
			"UNION ALL "
			"SELECT (SELECT min(dialogId) FROM DeletedMessageV2 WHERE userId = ?1 AND dialogId > dialogs.id) "
			"FROM dialogs WHERE id IS NOT NULL) SELECT id FROM dialogs WHERE id IS NOT NULL", -1, &s, nullptr);
		sqlite3_bind_int64(s, 1, user);
		auto hopped = std::vector<long long>();
		while (sqlite3_step(s) == SQLITE_ROW) {
			hopped.push_back(sqlite3_column_int64(s, 0));
		}
		sqlite3_finalize(s);
		auto expected = std::vector<long long>(ids.begin(), ids.end());
		std::sort(expected.begin(), expected.end());
		std::sort(hopped.begin(), hopped.end());
		std::printf("dialog list by hops: %.2f ms (%zu dialogs, %s)\n", MsSince(from), hopped.size(),
			hopped == expected ? "same" : "DIFFERENT");
		if (hopped != expected) {
			return 5;
		}
	}
	std::printf("\n%s", AyuDatabaseReport::Build(db, {false, false}).c_str());
	std::remove(original.c_str());
	if (!importMode) {
		std::remove(work.c_str());
	}
	return (bad || badEdited) ? 3 : 0;
}
