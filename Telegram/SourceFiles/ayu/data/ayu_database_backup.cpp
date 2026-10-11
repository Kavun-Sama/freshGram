#include "ayu/data/ayu_database_backup.h"

#include "ayu/ayu_settings.h"
#include "ayu/data/ayu_database.h"
#include "ayu/data/ayu_vault.h"
#include "ayu/libs/sqlite/sqlite3.h"
#include "base/timer.h"
#include "logs.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSaveFile>

#include <crl/crl_async.h>
#include <crl/crl_on_main.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace AyuDatabaseBackup {
namespace {

constexpr auto kHour = crl::time(60 * 60 * 1000);

std::atomic<bool> Running = false;
std::mutex BackupMutex;
// Nothing is rotated out until the settings are known.
std::atomic<bool> Configured = false;
std::atomic<int> KeepCount = 5;
std::atomic<int> IntervalHours = 6;
std::unique_ptr<base::Timer> PeriodicTimer;

struct Entry {
	QString path;
	qint64 rows = 0;
};

[[nodiscard]] QString MainPath() {
	return "./tdata/ayudata.enc";
}

[[nodiscard]] QString Directory() {
	return "./tdata/ayu_backups";
}

struct Handle {
	sqlite3 *db = nullptr;

	~Handle() {
		if (db) {
			sqlite3_close(db);
		}
	}
};

[[nodiscard]] bool OpenMemory(Handle &handle, const QByteArray &plain) {
	if (sqlite3_open_v2(
			":memory:",
			&handle.db,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
			nullptr) != SQLITE_OK) {
		return false;
	}
	const auto size = sqlite3_int64(plain.size());
	const auto buffer = static_cast<unsigned char*>(sqlite3_malloc64(size));
	if (!buffer) {
		return false;
	}
	memcpy(buffer, plain.constData(), size_t(size));
	return sqlite3_deserialize(
		handle.db,
		"main",
		buffer,
		size,
		size,
		SQLITE_DESERIALIZE_FREEONCLOSE | SQLITE_DESERIALIZE_RESIZEABLE)
		== SQLITE_OK;
}

[[nodiscard]] bool Exec(sqlite3 *db, const QString &sql) {
	const auto utf8 = sql.toUtf8();
	return sqlite3_exec(db, utf8.constData(), nullptr, nullptr, nullptr)
		== SQLITE_OK;
}

[[nodiscard]] qint64 ScalarInt(sqlite3 *db, const QString &sql) {
	const auto utf8 = sql.toUtf8();
	sqlite3_stmt *statement = nullptr;
	if (sqlite3_prepare_v2(db, utf8.constData(), -1, &statement, nullptr)
		!= SQLITE_OK) {
		return -1;
	}
	auto result = qint64(-1);
	if (sqlite3_step(statement) == SQLITE_ROW) {
		result = sqlite3_column_int64(statement, 0);
	}
	sqlite3_finalize(statement);
	return result;
}

[[nodiscard]] bool QuickCheckOk(sqlite3 *db) {
	sqlite3_stmt *statement = nullptr;
	if (sqlite3_prepare_v2(db, "PRAGMA quick_check(1)", -1, &statement, nullptr)
		!= SQLITE_OK) {
		return false;
	}
	auto ok = false;
	if (sqlite3_step(statement) == SQLITE_ROW) {
		const auto text = sqlite3_column_text(statement, 0);
		ok = text && (QByteArray(reinterpret_cast<const char*>(text)) == "ok");
	}
	sqlite3_finalize(statement);
	return ok;
}

[[nodiscard]] bool TableExists(sqlite3 *db, const QString &name) {
	return ScalarInt(
		db,
		QString("SELECT count(*) FROM sqlite_master "
			"WHERE type='table' AND name='%1'").arg(name)) == 1;
}

[[nodiscard]] bool Verify(const QString &path, qint64 *rows = nullptr) {
	const auto plain = AyuVault::ReadFile(path, true);
	if (!plain || plain->size() < 15 || !plain->startsWith("SQLite format 3")) {
		return false;
	}
	auto handle = Handle();
	if (!OpenMemory(handle, *plain) || !QuickCheckOk(handle.db)) {
		return false;
	}
	auto total = qint64(0);
	for (const auto &table : { "EditedMessage", "DeletedMessage" }) {
		if (!TableExists(handle.db, table)) {
			return false;
		}
		total += std::max<qint64>(
			0,
			ScalarInt(handle.db, QString("SELECT count(*) FROM %1").arg(table)));
	}
	if (rows) {
		*rows = total;
	}
	return true;
}

[[nodiscard]] std::vector<Entry> List() {
	auto names = QDir(Directory()).entryList(
		QStringList() << "ayudata_*.enc",
		QDir::Files,
		QDir::Name);
	std::reverse(names.begin(), names.end());
	auto result = std::vector<Entry>();
	for (const auto &name : names) {
		auto entry = Entry();
		entry.path = Directory() + '/' + name;
		const auto parts = name.chopped(4).split('_');
		if (parts.size() >= 4) {
			entry.rows = parts.back().toLongLong();
		}
		result.push_back(std::move(entry));
	}
	return result;
}

void ScrubSecrets(const QString &path) {
	const auto plain = AyuVault::ReadFile(path, true);
	auto handle = Handle();
	if (!plain || !OpenMemory(handle, *plain)) {
		return;
	}
	auto dirty = false;
	for (const auto &table : { "SecretMessage", "SecretChat", "SecretState" }) {
		if (TableExists(handle.db, table)
			&& ScalarInt(
				handle.db,
				QString("SELECT count(*) FROM %1").arg(table)) != 0) {
			dirty = true;
		}
	}
	if (!dirty) {
		return;
	}
	static_cast<void>(Exec(handle.db, QString("PRAGMA secure_delete = ON")));
	for (const auto &table : { "SecretMessage", "SecretChat", "SecretState" }) {
		if (TableExists(handle.db, table)) {
			static_cast<void>(
				Exec(handle.db, QString("DELETE FROM %1").arg(table)));
		}
	}
	static_cast<void>(Exec(handle.db, QString("VACUUM")));
	auto size = sqlite3_int64();
	const auto data = sqlite3_serialize(handle.db, "main", &size, 0);
	if (data) {
		AyuVault::WriteSealedFile(path, QByteArray(
			reinterpret_cast<const char*>(data),
			qsizetype(size)));
		sqlite3_free(data);
	}
}

void Rotate() {
	const auto list = List();
	auto richest = -1;
	for (auto i = 0; i != int(list.size()); ++i) {
		if (richest < 0 || list[i].rows > list[richest].rows) {
			richest = i;
		}
	}
	if (!Configured) {
		return;
	}
	for (auto i = KeepCount.load(); i < int(list.size()); ++i) {
		if (i != richest) {
			QFile::remove(list[i].path);
		} else {
			ScrubSecrets(list[i].path);
		}
	}
}

[[nodiscard]] bool UpToDate() {
	const auto list = List();
	if (list.empty()) {
		return false;
	}
	const auto newest = QFileInfo(list.front().path).lastModified();
	const auto info = QFileInfo(MainPath());
	return !info.exists() || info.lastModified() <= newest;
}

} // namespace

bool create() {
	if (!QFile::exists(MainPath()) || UpToDate()) {
		return false;
	}
	auto expected = false;
	if (!Running.compare_exchange_strong(expected, true)) {
		return false;
	}
	const auto guard = gsl::finally([] { Running = false; });
	const auto lock = std::lock_guard(BackupMutex);
	auto sealed = QByteArray();
	auto rows = qint64(0);
	if (!AyuDatabase::exportSealed(sealed, rows)) {
		return false;
	}
	QDir().mkpath(Directory());
	const auto name = QString("%1/ayudata_%2_%3.enc")
		.arg(Directory())
		.arg(QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss"))
		.arg(rows);
	auto file = QSaveFile(name);
	if (!file.open(QIODevice::WriteOnly)
		|| file.write(sealed) != sealed.size()
		|| !file.commit()) {
		return false;
	}
	QFile::setPermissions(
		name,
		QFileDevice::ReadOwner | QFileDevice::WriteOwner);
	Rotate();
	return true;
}

bool restore(int &index) {
	const auto list = List();
	for (auto i = index; i < int(list.size()); ++i) {
		if (!Verify(list[i].path)) {
			continue;
		}
		QFile::remove(MainPath());
		if (QFile::copy(list[i].path, MainPath())) {
			index = i + 1;
			LOG(("[AyuGram] Database restored from '%1'.").arg(list[i].path));
			return true;
		}
	}
	index = int(list.size());
	return false;
}

void removeAll() {
	const auto lock = std::lock_guard(BackupMutex);
	QDir(Directory()).removeRecursively();
	const auto moved = QDir("./tdata").entryList(
		QStringList() << "ayudata_*.enc",
		QDir::Files);
	for (const auto &name : moved) {
		QFile::remove("./tdata/" + name);
	}
}

void purgeUser(long long userId) {
	static const auto kTables = {
		"DeletedExtra", "DeletedMarkup", "DeletedMessage", "EditedMessage",
		"DeletedDialog", "KeptTopic", "KeptDialog", "KnownUser", "LocalPin",
		"SecretChat", "SecretMessage", "SecretState", "PeekedStatus",
		"PeekRestore", "SpyMessageRead", "SpyMessageContentsRead",
	};
	const auto lock = std::lock_guard(BackupMutex);
	for (const auto &entry : List()) {
		const auto plain = AyuVault::ReadFile(entry.path, true);
		auto handle = Handle();
		if (!plain || !OpenMemory(handle, *plain)) {
			QFile::remove(entry.path);
			continue;
		}
		static_cast<void>(Exec(handle.db, QString("PRAGMA secure_delete = ON")));
		for (const auto &table : kTables) {
			if (TableExists(handle.db, table)) {
				static_cast<void>(Exec(handle.db, QString(
					"DELETE FROM %1 WHERE userId = %2"
				).arg(table).arg(userId)));
			}
		}
		static_cast<void>(Exec(handle.db, QString("VACUUM")));
		auto size = sqlite3_int64();
		const auto data = sqlite3_serialize(handle.db, "main", &size, 0);
		if (!data) {
			QFile::remove(entry.path);
			continue;
		}
		const auto saved = AyuVault::WriteSealedFile(entry.path, QByteArray(
			reinterpret_cast<const char*>(data),
			qsizetype(size)));
		sqlite3_free(data);
		if (!saved) {
			QFile::remove(entry.path);
		}
	}
}

void startPeriodic() {
	if (PeriodicTimer) {
		return;
	}
	PeriodicTimer = std::make_unique<base::Timer>([] {
		if (!Configured) {
			const auto &settings = AyuSettings::getInstance();
			configure(
				settings.backupKeepCount(),
				settings.backupIntervalHours());
		}
		crl::async([] {
			[[maybe_unused]] const auto created = create();
		});
	});
	PeriodicTimer->callEach(IntervalHours.load() * kHour);
}

void configure(int keepCount, int intervalHours) {
	Configured = true;
	KeepCount = keepCount;
	IntervalHours = intervalHours;
	if (PeriodicTimer) {
		PeriodicTimer->callEach(intervalHours * kHour);
	}
}

} // namespace AyuDatabaseBackup
