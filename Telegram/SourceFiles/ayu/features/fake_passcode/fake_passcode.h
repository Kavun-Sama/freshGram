// This file is part of freshGram.
#pragma once

namespace AyuFakePasscode {

constexpr auto kMaxEntries = 8;

struct Actions {
	bool clearCache = false;
	bool clearCacheDb = false;
	bool clearCacheMedia = false;
	bool clearProxies = false;
	bool logoutDb = false;
	bool logoutMedia = false;
	bool deleteAll = false;
	QString command;
	std::vector<int> logoutAccounts;
};

struct Entry {
	int id = 0;
	QString name;
	QString passcode;
	bool unlocks = true;
	Actions actions;
};

struct Data {
	bool clearCacheOnLock = false;
	std::vector<Entry> list;
};

enum class AddResult : uchar {
	Ok,
	Empty,
	TooMany,
	TooShort,
	WrongPasscode,
	SameAsReal,
	Duplicate,
	Failed,
};

struct Match {
	int id = 0;
	QByteArray realPasscode;
	Actions actions;

	[[nodiscard]] bool unlocks() const {
		return !realPasscode.isEmpty();
	}
};

[[nodiscard]] std::optional<Match> TryMatch(const QByteArray &passcode);

void SetPending(int id);
void RunPending();
void RunSilent(const Actions &actions);

void OnLock();

[[nodiscard]] const Data &Current();
[[nodiscard]] AddResult Add(
	QString name,
	QString passcode,
	const QString &realPasscode,
	bool unlocks,
	Actions actions);
void Update(int id, QString name, Actions actions);
void Remove(int id);
void SetClearCacheOnLock(bool value);

void RealPasscodeChanged(const QByteArray &passcode);

} // namespace AyuFakePasscode
