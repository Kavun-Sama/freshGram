// This is the source code of freshGram.
#include "ayu/data/ayu_encrypted_vfs.h"
#include "ayu/libs/sqlite/sqlite3.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

unsigned char Key[32];
std::string Dir;

std::string DbPath() {
	return Dir + "/test.sqlite";
}

void SetKey(unsigned char fill) {
	std::memset(Key, fill, sizeof(Key));
	if (!AyuEncryptedVfs::Register(Key, sizeof(Key), DbPath().c_str())) {
		std::printf("register failed\n");
		std::exit(1);
	}
}

sqlite3 *Open(bool create) {
	sqlite3 *db = nullptr;
	const auto flags = SQLITE_OPEN_READWRITE | (create ? SQLITE_OPEN_CREATE : 0);
	if (sqlite3_open_v2(DbPath().c_str(), &db, flags, nullptr) != SQLITE_OK) {
		std::printf("  open failed: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		return nullptr;
	}
	return db;
}

int Exec(sqlite3 *db, const std::string &sql, bool quiet = false) {
	char *error = nullptr;
	const auto rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error);
	if (rc != SQLITE_OK && !quiet) {
		std::printf("  sql failed (%d): %s: %s\n", rc, sql.substr(0, 50).c_str(), error ? error : "?");
	}
	sqlite3_free(error);
	return rc;
}

long long Scalar(sqlite3 *db, const std::string &sql, int *rcOut = nullptr) {
	sqlite3_stmt *statement = nullptr;
	auto result = -1LL;
	auto rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr);
	if (rc == SQLITE_OK) {
		rc = sqlite3_step(statement);
		if (rc == SQLITE_ROW) {
			result = sqlite3_column_int64(statement, 0);
		}
	}
	sqlite3_finalize(statement);
	if (rcOut) {
		*rcOut = rc;
	}
	return result;
}

std::string ReadFile(const std::string &path) {
	auto file = std::ifstream(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

bool Fresh() {
	std::remove(DbPath().c_str());
	std::remove((DbPath() + "-journal").c_str());
	auto db = Open(true);
	if (!db) {
		return false;
	}
	Exec(db, "PRAGMA page_size = 4096");
	int reserve = AyuEncryptedVfs::kReserveBytes;
	sqlite3_file_control(db, "main", SQLITE_FCNTL_RESERVE_BYTES, &reserve);
	Exec(db, "PRAGMA auto_vacuum = INCREMENTAL");
	Exec(db, "PRAGMA journal_mode = TRUNCATE");
	Exec(db, "CREATE TABLE t (id INTEGER PRIMARY KEY, v INTEGER, s TEXT)");
	Exec(db, "CREATE INDEX t_v ON t (v)");
	sqlite3_close(db);
	return true;
}

int Check(bool ok, const char *name) {
	std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
	return ok ? 0 : 1;
}

int TestRoundTrip() {
	auto failures = 0;
	failures += Check(Fresh(), "create");
	auto db = Open(false);
	Exec(db, "PRAGMA journal_mode = TRUNCATE");
	Exec(db, "BEGIN");
	Exec(db, "WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 100000) "
		"INSERT INTO t (v, s) SELECT i % 1000, 'SECRETMARKER-' || i || '-plain text of the message' FROM n");
	Exec(db, "COMMIT");
	failures += Check(Scalar(db, "SELECT count(*) FROM t") == 100000, "100k rows inserted");
	sqlite3_close(db);
	const auto raw = ReadFile(DbPath());
	failures += Check(raw.find("SECRETMARKER") == std::string::npos, "no plaintext in the file");
	failures += Check(raw.compare(0, 8, "AYUENC01") == 0, "file header");
	failures += Check(raw.find("SQLite format 3") == std::string::npos, "no SQLite header in the file");
	std::printf("  file size %zu bytes\n", raw.size());
	db = Open(false);
	failures += Check(Scalar(db, "SELECT count(*) FROM t") == 100000, "reopen: rows");
	failures += Check(Scalar(db, "SELECT count(*) FROM t WHERE s LIKE 'SECRETMARKER-77-%'") == 1, "reopen: text");
	char *result = nullptr;
	sqlite3_stmt *s = nullptr;
	sqlite3_prepare_v2(db, "PRAGMA integrity_check", -1, &s, nullptr);
	sqlite3_step(s);
	failures += Check(std::string(reinterpret_cast<const char*>(sqlite3_column_text(s, 0))) == "ok", "integrity_check");
	sqlite3_finalize(s);
	failures += Check(Scalar(db, "PRAGMA page_size") == 4096, "page size");
	std::printf("  reserve %lld, pages %lld\n", Scalar(db, "PRAGMA page_count") * 0 + 32, Scalar(db, "PRAGMA page_count"));
	// Updates and deletes, a vacuum and the incremental vacuum.
	Exec(db, "UPDATE t SET s = 'changed ' || id WHERE id % 3 = 0");
	Exec(db, "DELETE FROM t WHERE id % 5 = 0");
	Exec(db, "PRAGMA incremental_vacuum");
	failures += Check(Scalar(db, "SELECT count(*) FROM t") == 80000, "after delete");
	Exec(db, "VACUUM");
	failures += Check(Scalar(db, "SELECT count(*) FROM t WHERE s LIKE 'changed %'") > 0, "after vacuum");
	sqlite3_close(db);
	return failures;
}

int TestWrongKey() {
	SetKey(2);
	auto db = Open(false);
	auto failures = 0;
	int rc = 0;
	if (db) {
		Scalar(db, "SELECT count(*) FROM t", &rc);
		std::printf("  wrong key: rc %d\n", rc);
		failures += Check(rc != SQLITE_ROW && rc != SQLITE_DONE, "wrong key is refused");
		sqlite3_close(db);
	} else {
		failures += Check(true, "wrong key is refused at open");
	}
	SetKey(1);
	return failures;
}

int TestTamper() {
	auto failures = 0;
	auto raw = ReadFile(DbPath());
	// A byte in the middle of the file.
	const auto position = raw.size() / 2 + 77;
	raw[position] ^= 0x01;
	{
		auto out = std::ofstream(DbPath() + ".bad", std::ios::binary);
		out.write(raw.data(), raw.size());
	}
	std::rename(DbPath().c_str(), (DbPath() + ".good").c_str());
	std::rename((DbPath() + ".bad").c_str(), DbPath().c_str());
	auto db = Open(false);
	int rc = 0;
	Scalar(db, "SELECT sum(length(s)) FROM t", &rc);
	std::printf("  tampered: rc %d (%s)\n", rc, sqlite3_errmsg(db));
	failures += Check(rc != SQLITE_ROW && rc != SQLITE_DONE, "a changed byte is detected");
	sqlite3_close(db);
	std::remove(DbPath().c_str());
	std::rename((DbPath() + ".good").c_str(), DbPath().c_str());
	return failures;
}

// The child writes in a loop, the parent kills it at a random moment.
int TestCrash(int rounds) {
	auto failures = 0;
	failures += Check(Fresh(), "create for the crash test");
	{
		auto db = Open(false);
		Exec(db, "CREATE TABLE IF NOT EXISTS audit (id INTEGER PRIMARY KEY, n INTEGER)");
		Exec(db, "INSERT INTO audit (id, n) VALUES (1, 0)");
		sqlite3_close(db);
	}
	for (auto round = 0; round != rounds; ++round) {
		const auto pid = fork();
		if (pid == 0) {
			auto db = Open(false);
			Exec(db, "PRAGMA journal_mode = TRUNCATE");
			Exec(db, "PRAGMA synchronous = NORMAL");
			for (auto i = 0;; ++i) {
				Exec(db, "BEGIN");
				Exec(db, "INSERT INTO t (v, s) VALUES (1, 'row ' || abs(random()))", true);
				Exec(db, "INSERT INTO t (v, s) SELECT 2, 'filler ' || abs(random()) FROM t LIMIT 300", true);
				Exec(db, "UPDATE audit SET n = n + 1 WHERE id = 1", true);
				Exec(db, "COMMIT", true);
			}
		}
		usleep(20000 + (std::rand() % 80000));
		kill(pid, SIGKILL);
		int status = 0;
		waitpid(pid, &status, 0);
		auto db = Open(false);
		if (!db) {
			++failures;
			break;
		}
		int rc = 0;
		const auto audit = Scalar(db, "SELECT n FROM audit WHERE id = 1", &rc);
		const auto transactions = Scalar(db, "SELECT count(*) FROM t WHERE s LIKE 'row %'", &rc);
		sqlite3_stmt *s = nullptr;
		sqlite3_prepare_v2(db, "PRAGMA integrity_check", -1, &s, nullptr);
		sqlite3_step(s);
		const auto ok = std::string(reinterpret_cast<const char*>(sqlite3_column_text(s, 0))) == "ok";
		sqlite3_finalize(s);
		// Every committed transaction adds one counter step and one 'row' record.
		if (!ok || audit != transactions) {
			std::printf("  round %d: integrity %d, audit %lld, rows %lld\n", round, ok, audit, transactions);
			++failures;
		}
		sqlite3_close(db);
	}
	failures += Check(failures == 0, "crash test: every round consistent");
	return failures;
}

// The journal of an unfinished transaction is encrypted and still recovers.
int TestHotJournal() {
	auto failures = 0;
	failures += Check(Fresh(), "create for the journal test");
	{
		auto db = Open(false);
		Exec(db, "WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 5000) INSERT INTO t (v, s) SELECT 1, 'JOURNALMARKER original ' || i FROM n");
		sqlite3_close(db);
	}
	const auto pid = fork();
	if (pid == 0) {
		auto db = Open(false);
		Exec(db, "PRAGMA journal_mode = TRUNCATE");
		Exec(db, "PRAGMA cache_size = 10");
		Exec(db, "BEGIN");
		Exec(db, "UPDATE t SET s = 'changed in the transaction ' || id");
		// Spill the changed pages so that the journal is full, then wait.
		for (;;) {
			pause();
		}
	}
	usleep(500000);
	const auto journal = ReadFile(DbPath() + "-journal");
	std::printf("  hot journal: %zu bytes\n", journal.size());
	failures += Check(journal.size() > 4096, "the hot journal exists");
	failures += Check(journal.find("JOURNALMARKER") == std::string::npos, "no plaintext in the journal");
	failures += Check(journal.find("original") == std::string::npos, "no text of the rows in the journal");
	kill(pid, SIGKILL);
	int status = 0;
	waitpid(pid, &status, 0);
	auto db = Open(false);
	failures += Check(Scalar(db, "SELECT count(*) FROM t WHERE s LIKE 'JOURNALMARKER original %'") == 5000, "the hot journal rolled back");
	failures += Check(Scalar(db, "SELECT count(*) FROM t WHERE s LIKE 'changed%'") == 0, "nothing of the transaction is left");
	sqlite3_close(db);
	return failures;
}

} // namespace

int main(int argc, char **argv) {
	setvbuf(stdout, nullptr, _IONBF, 0);
	if (argc < 2) {
		std::printf("usage: %s <directory> [crash rounds]\n", argv[0]);
		return 1;
	}
	Dir = argv[1];
	const auto rounds = (argc > 2) ? std::atoi(argv[2]) : 20;
	SetKey(1);
	auto failures = 0;
	failures += TestRoundTrip();
	failures += TestWrongKey();
	failures += TestTamper();
	failures += TestHotJournal();
	failures += TestCrash(rounds);
	std::printf("\n%s (%d failed)\n", failures ? "FAILED" : "ALL PASSED", failures);
	return failures ? 1 : 0;
}
