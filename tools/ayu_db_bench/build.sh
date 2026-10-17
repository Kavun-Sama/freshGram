#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
src="$root/Telegram/SourceFiles"
out="${1:?the output directory is required}"
mkdir -p "$out"
if [ ! -f "$out/sqlite3.o" ]; then
	gcc -O2 -DSQLITE_ENABLE_DBSTAT_VTAB -DSQLITE_THREADSAFE=1 -c "$src/ayu/libs/sqlite/sqlite3.c" -o "$out/sqlite3.o"
fi
g++ -std=c++20 -O2 -I"$src" "$here/main.cpp" "$src/ayu/data/ayu_database_report.cpp" "$out/sqlite3.o" -o "$out/ayu_db_bench" -lcrypto -lpthread -ldl
g++ -std=c++20 -O1 -w -I"$src" -I"$src/ayu/libs/sqlite" "$here/migrate_test.cpp" "$src/ayu/data/ayu_migration_v4.cpp" "$src/ayu/data/message_rows.cpp" "$src/ayu/data/ayu_database_report.cpp" "$out/sqlite3.o" -o "$out/migrate_test" -lcrypto -lpthread -ldl
echo "$out/ayu_db_bench $out/migrate_test"
