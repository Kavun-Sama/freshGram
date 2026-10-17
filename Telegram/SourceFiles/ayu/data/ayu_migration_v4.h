// This is the source code of freshGram.
#pragma once

#include <string>

struct sqlite3;

namespace AyuMigrationV4 {

struct Result {
	bool ok = false;
	bool migrated = false;
	long long deleted = 0;
	long long edited = 0;
	double milliseconds = 0.;
	std::string error;
};

// Copies the old wide message tables into the compact ones and drops them.
// The compact tables must exist already, the caller owns the transaction.
[[nodiscard]] Result Run(sqlite3 *db);

} // namespace AyuMigrationV4
