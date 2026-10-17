// This is the source code of freshGram.
#include "ayu/data/ayu_database_report.h"
#include "ayu/libs/sqlite/sqlite3.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
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
		std::printf("  sql failed: %s (%s)\n", sql.substr(0, 60).c_str(), error ? error : "?");
		sqlite3_free(error);
	}
}

long long Scalar(sqlite3 *db, const std::string &sql) {
	sqlite3_stmt *statement = nullptr;
	auto result = 0LL;
	if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) == SQLITE_OK
		&& sqlite3_step(statement) == SQLITE_ROW) {
		result = sqlite3_column_int64(statement, 0);
	}
	sqlite3_finalize(statement);
	return result;
}

// freshGram declares this index, the original AyuGram databases lack it.
void AddMissingIndex(sqlite3 *db) {
	const auto from = Clock::now();
	Exec(db, "CREATE INDEX IF NOT EXISTS idx_deleted_message_userId_dialogId_messageId "
		"ON DeletedMessage (userId, dialogId, messageId)");
	std::printf("  creating the index freshGram adds on load: %.0f ms\n\n", MsSince(from));
}

void DropFileCache(const std::string &path) {
	const auto fd = open(path.c_str(), O_RDONLY);
	if (fd >= 0) {
		fsync(fd);
		posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
		close(fd);
	}
}

// What the application does now: the whole file is copied into memory.
sqlite3 *LoadIntoMemory(const std::string &path) {
	sqlite3 *source = nullptr;
	sqlite3 *memory = nullptr;
	const auto from = Clock::now();
	if (sqlite3_open_v2(path.c_str(), &source, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
		std::printf("open failed: %s\n", sqlite3_errmsg(source));
		return nullptr;
	}
	sqlite3_open_v2(":memory:", &memory, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
	const auto backup = sqlite3_backup_init(memory, "main", source, "main");
	sqlite3_backup_step(backup, -1);
	sqlite3_backup_finish(backup);
	sqlite3_close(source);
	std::printf("legacy import into memory: %.0f ms\n", MsSince(from));
	return memory;
}

void FileBenchmarks(const std::string &copy) {
	std::printf("== File benchmarks on a scratch copy (SATA SSD, unencrypted, cache 64 MB) ==\n");
	DropFileCache(copy);
	sqlite3 *db = nullptr;
	auto from = Clock::now();
	sqlite3_open_v2(copy.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr);
	Exec(db, "PRAGMA cache_size = -65536");
	Exec(db, "PRAGMA journal_mode = DELETE");
	Exec(db, "PRAGMA synchronous = NORMAL");
	Exec(db, "PRAGMA mmap_size = 0");
	AddMissingIndex(db);
	Exec(db, "PRAGMA wal_checkpoint");
	const auto rows = Scalar(db, "SELECT count(*) FROM sqlite_master");
	std::printf("  open + first read of the schema: %.1f ms (%lld objects)\n", MsSince(from), rows);

	const auto top = [&](const char *sql) { return Scalar(db, sql); };
	const auto user = top("SELECT userId FROM DeletedMessage GROUP BY userId ORDER BY count(*) DESC LIMIT 1");
	const auto dialog = top(("SELECT dialogId FROM DeletedMessage WHERE userId = " + std::to_string(user)
		+ " GROUP BY dialogId ORDER BY count(*) DESC LIMIT 1").c_str());
	const auto message = top(("SELECT messageId FROM DeletedMessage WHERE userId = " + std::to_string(user)
		+ " AND dialogId = " + std::to_string(dialog) + " ORDER BY messageId LIMIT 1 OFFSET 1000").c_str());
	const auto window = "SELECT * FROM DeletedMessage WHERE userId = " + std::to_string(user)
		+ " AND dialogId = " + std::to_string(dialog)
		+ " AND messageId > " + std::to_string(message) + " ORDER BY messageId DESC LIMIT 50";

	for (auto pass = 0; pass != 2; ++pass) {
		if (!pass) {
			DropFileCache(copy);
			sqlite3_close(db);
			sqlite3_open_v2(copy.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr);
			Exec(db, "PRAGMA cache_size = -65536");
			Exec(db, "PRAGMA mmap_size = 0");
		}
		from = Clock::now();
		sqlite3_stmt *statement = nullptr;
		sqlite3_prepare_v2(db, window.c_str(), -1, &statement, nullptr);
		auto count = 0;
		while (sqlite3_step(statement) == SQLITE_ROW) {
			++count;
			for (auto i = 0; i != sqlite3_column_count(statement); ++i) {
				sqlite3_column_bytes(statement, i);
			}
		}
		sqlite3_finalize(statement);
		std::printf("  dialog window, 50 rows, %s: %.3f ms (%d rows)\n", pass ? "warm" : "cold", MsSince(from), count);
	}

	for (auto pass = 0; pass != 2; ++pass) {
		if (!pass) {
			DropFileCache(copy);
			sqlite3_close(db);
			sqlite3_open_v2(copy.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr);
			Exec(db, "PRAGMA cache_size = -65536");
			Exec(db, "PRAGMA mmap_size = 0");
		}
		from = Clock::now();
		const auto matches = Scalar(db, "SELECT count(*) FROM (SELECT * FROM DeletedMessage WHERE userId = "
			+ std::to_string(user) + " AND text LIKE '%zzqqxx%' ESCAPE '\\' ORDER BY date DESC LIMIT 100)");
		std::printf("  search everywhere, no matches, %s: %.0f ms (%lld)\n", pass ? "warm" : "cold", MsSince(from), matches);
	}

	Exec(db, "PRAGMA journal_mode = DELETE");
	const auto columns = [&] {
		auto list = std::string();
		sqlite3_stmt *statement = nullptr;
		sqlite3_prepare_v2(db, "PRAGMA table_info(DeletedMessage)", -1, &statement, nullptr);
		while (sqlite3_step(statement) == SQLITE_ROW) {
			if (!sqlite3_column_int(statement, 5)) {
				list += (list.empty() ? "" : ", ") + std::string("\"") + reinterpret_cast<const char*>(sqlite3_column_text(statement, 1)) + "\"";
			}
		}
		sqlite3_finalize(statement);
		return list;
	}();
	for (const auto batch : {500, 500, 500, 10000}) {
		from = Clock::now();
		Exec(db, "BEGIN");
		Exec(db, "INSERT INTO DeletedMessage (" + columns + ") SELECT " + columns + " FROM DeletedMessage LIMIT " + std::to_string(batch));
		Exec(db, "COMMIT");
		std::printf("  insert + commit of %d rows (journal DELETE, synchronous NORMAL): %.1f ms\n", batch, MsSince(from));
	}
	from = Clock::now();
	const auto maxId = Scalar(db, "SELECT max(fakeId) FROM DeletedMessage");
	Exec(db, "BEGIN");
	Exec(db, "DELETE FROM DeletedMessage WHERE fakeId > " + std::to_string(maxId - 11500));
	Exec(db, "COMMIT");
	std::printf("  delete + commit of 11500 rows: %.1f ms\n", MsSince(from));
	sqlite3_close(db);
	std::printf("\n");
}

} // namespace

int main(int argc, char **argv) {
	if (argc < 3) {
		std::printf("usage: %s <database> <scratch directory> [skip-report]\n", argv[0]);
		return 1;
	}
	const auto path = std::string(argv[1]);
	const auto scratch = std::string(argv[2]);
	const auto skipReport = argc > 3;
	if (!skipReport) {
		const auto memory = LoadIntoMemory(path);
		if (!memory) {
			return 1;
		}
		AddMissingIndex(memory);
		std::printf("%s", AyuDatabaseReport::Build(memory).c_str());
		sqlite3_close(memory);
	}
	const auto copy = scratch + "/bench.db";
	const auto command = "cp '" + path + "' '" + copy + "'";
	if (std::system(command.c_str()) != 0) {
		return 1;
	}
	FileBenchmarks(copy);
	std::remove(copy.c_str());
	return 0;
}
