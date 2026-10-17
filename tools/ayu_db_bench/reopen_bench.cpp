// This is the source code of freshGram.
#include "ayu/data/ayu_encrypted_vfs.h"
#include "ayu/libs/sqlite/sqlite3.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace {

using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point from) {
	return std::chrono::duration<double, std::milli>(Clock::now() - from).count();
}

void Exec(sqlite3 *db, const std::string &sql) {
	char *error = nullptr;
	if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
		std::printf("  sql failed: %s: %s\n", sql.substr(0, 60).c_str(), error ? error : "?");
		sqlite3_free(error);
	}
}

long long Scalar(sqlite3 *db, const std::string &sql) {
	sqlite3_stmt *statement = nullptr;
	auto result = -1LL;
	if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) == SQLITE_OK
		&& sqlite3_step(statement) == SQLITE_ROW) {
		result = sqlite3_column_int64(statement, 0);
	}
	sqlite3_finalize(statement);
	return result;
}

double Run(sqlite3 *db, const std::string &sql, long long *rows = nullptr) {
	const auto from = Clock::now();
	sqlite3_stmt *statement = nullptr;
	sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr);
	auto count = 0LL;
	while (sqlite3_step(statement) == SQLITE_ROW) {
		++count;
		for (auto i = 0; i != sqlite3_column_count(statement); ++i) {
			sqlite3_column_bytes(statement, i);
		}
	}
	sqlite3_finalize(statement);
	if (rows) {
		*rows = count;
	}
	return MsSince(from);
}

void DropFileCache(const std::string &path) {
	const auto fd = open(path.c_str(), O_RDONLY);
	if (fd >= 0) {
		fsync(fd);
		posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
		close(fd);
	}
}

sqlite3 *OpenDatabase(const std::string &path, int cacheKb) {
	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
		std::printf("open failed: %s\n", sqlite3_errmsg(db));
		return nullptr;
	}
	Exec(db, "PRAGMA journal_mode = TRUNCATE");
	Exec(db, "PRAGMA synchronous = NORMAL");
	Exec(db, "PRAGMA locking_mode = EXCLUSIVE");
	Exec(db, "PRAGMA mmap_size = 0");
	Exec(db, "PRAGMA secure_delete = ON");
	Exec(db, "PRAGMA cache_size = -" + std::to_string(cacheKb));
	return db;
}

} // namespace

int main(int argc, char **argv) {
	setvbuf(stdout, nullptr, _IONBF, 0);
	if (argc < 2) {
		std::printf("usage: %s <encrypted database> [plain]\n", argv[0]);
		return 1;
	}
	const auto path = std::string(argv[1]);
	const auto plain = argc > 2;
	if (!plain) {
		unsigned char key[32];
		std::memset(key, 7, sizeof(key));
		if (!AyuEncryptedVfs::Register(key, sizeof(key), path.c_str())) {
			return 1;
		}
	}
	std::printf("== %s ==\n", plain ? "plain file" : "encrypted file");
	for (const auto cacheKb : {65536, 131072}) {
		DropFileCache(path);
		auto from = Clock::now();
		auto db = OpenDatabase(path, cacheKb);
		if (!db) {
			return 1;
		}
		const auto tables = Scalar(db, "SELECT count(*) FROM sqlite_master");
		std::printf("-- cache %d MB: open and the schema %.1f ms (%lld objects)\n", cacheKb / 1024, MsSince(from), tables);
		const auto user = Scalar(db, "SELECT userId FROM DeletedMessageV2 GROUP BY userId ORDER BY count(*) DESC LIMIT 1");
		const auto dialog = Scalar(db, "SELECT dialogId FROM DeletedMessageV2 WHERE userId = " + std::to_string(user)
			+ " GROUP BY dialogId ORDER BY count(*) DESC LIMIT 1");
		const auto message = Scalar(db, "SELECT messageId FROM DeletedMessageV2 WHERE userId = " + std::to_string(user)
			+ " AND dialogId = " + std::to_string(dialog) + " ORDER BY messageId LIMIT 1 OFFSET 1000");
		const auto window = "SELECT * FROM DeletedMessageV2 WHERE userId = " + std::to_string(user)
			+ " AND dialogId = " + std::to_string(dialog) + " AND messageId > " + std::to_string(message)
			+ " ORDER BY messageId DESC LIMIT 50";
		DropFileCache(path);
		sqlite3_close(db);
		db = OpenDatabase(path, cacheKb);
		long long rows = 0;
		std::printf("  dialog window cold   %9.2f ms\n", Run(db, window, &rows));
		std::printf("  dialog window warm   %9.2f ms\n", Run(db, window, &rows));
		const auto hops = "WITH RECURSIVE dialogs(id) AS (SELECT min(dialogId) FROM DeletedMessageV2 WHERE userId = " + std::to_string(user)
			+ " UNION ALL SELECT (SELECT min(dialogId) FROM DeletedMessageV2 WHERE userId = " + std::to_string(user)
			+ " AND dialogId > dialogs.id) FROM dialogs WHERE id IS NOT NULL) SELECT id FROM dialogs WHERE id IS NOT NULL";
		std::printf("  dialog list cold     %9.2f ms\n", Run(db, hops, &rows));
		const auto search = "SELECT * FROM DeletedMessageV2 WHERE userId = " + std::to_string(user)
			+ " AND text LIKE '%zzqqxx%' ESCAPE '\\' ORDER BY date DESC LIMIT 100";
		DropFileCache(path);
		sqlite3_close(db);
		db = OpenDatabase(path, cacheKb);
		std::printf("  search all cold      %9.0f ms\n", Run(db, search, &rows));
		std::printf("  search all warm      %9.0f ms\n", Run(db, search, &rows));
		const auto searchOne = "SELECT * FROM DeletedMessageV2 WHERE userId = " + std::to_string(user)
			+ " AND dialogId = " + std::to_string(dialog) + " AND text LIKE '%zzqqxx%' ESCAPE '\\' ORDER BY date DESC LIMIT 100";
		std::printf("  search dialog warm   %9.0f ms\n", Run(db, searchOne, &rows));
		sqlite3_close(db);
	}

	auto db = OpenDatabase(path, 65536);
	const auto columns = std::string("userId, dialogId, messageId, topicId, fromId, groupedId, date, flags, editDate, views, "
		"replyMessageId, replyTopId, replyForumTopic, entityCreateDate, text, textEntities, mediaPath, documentType, "
		"documentSerialized, postAuthor, extra");
	for (const auto batch : {500, 500, 500, 10000}) {
		const auto from = Clock::now();
		Exec(db, "BEGIN");
		Exec(db, "INSERT INTO DeletedMessageV2 (" + columns + ") SELECT " + columns + " FROM DeletedMessageV2 LIMIT " + std::to_string(batch));
		Exec(db, "COMMIT");
		std::printf("  insert + commit %5d rows %9.1f ms\n", batch, MsSince(from));
	}
	const auto maxId = Scalar(db, "SELECT max(fakeId) FROM DeletedMessageV2");
	const auto from = Clock::now();
	Exec(db, "BEGIN");
	Exec(db, "DELETE FROM DeletedMessageV2 WHERE fakeId > " + std::to_string(maxId - 11500));
	Exec(db, "COMMIT");
	std::printf("  delete + commit 11500 rows %7.1f ms\n", MsSince(from));
	sqlite3_close(db);
	return 0;
}
