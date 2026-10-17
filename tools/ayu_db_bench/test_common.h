// This is the source code of freshGram.
#pragma once

#include "ayu/data/message_types.h"
#include "ayu/libs/sqlite/sqlite3.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace test {

using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point from) {
	return std::chrono::duration<double, std::milli>(Clock::now() - from).count();
}

long long Scalar(sqlite3 *db, const std::string &sql) {
	sqlite3_stmt *statement = nullptr;
	auto result = -1LL;
	if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) == SQLITE_OK
		&& sqlite3_step(statement) == SQLITE_ROW) {
		result = sqlite3_column_int64(statement, 0);
	} else {
		std::printf("  sql failed: %s: %s\n", sql.substr(0, 80).c_str(), sqlite3_errmsg(db));
	}
	sqlite3_finalize(statement);
	return result;
}

void Exec(sqlite3 *db, const std::string &sql) {
	char *error = nullptr;
	if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
		std::printf("  sql failed: %s: %s\n", sql.substr(0, 80).c_str(), error ? error : "?");
		sqlite3_free(error);
	}
}

std::string Text(sqlite3_stmt *s, int i) {
	const auto data = reinterpret_cast<const char*>(sqlite3_column_text(s, i));
	return data ? std::string(data, sqlite3_column_bytes(s, i)) : std::string();
}

std::vector<char> Blob(sqlite3_stmt *s, int i) {
	const auto data = static_cast<const char*>(sqlite3_column_blob(s, i));
	return data ? std::vector<char>(data, data + sqlite3_column_bytes(s, i)) : std::vector<char>();
}

// The old wide row, read by column names the way the old mapping did.
bool ReadOld(sqlite3 *db, const std::string &table, const std::string &where, AyuMessageBase &m) {
	sqlite3_stmt *s = nullptr;
	const auto sql = "SELECT fakeId, userId, dialogId, groupedId, peerId, fromId, topicId, messageId, date, "
		"flags, editDate, views, fwdFlags, fwdFromId, fwdName, fwdDate, fwdPostAuthor, replyFlags, "
		"replyMessageId, replyPeerId, replyTopId, replyForumTopic, replySerialized, entityCreateDate, "
		"text, textEntities, mediaPath, hqThumbPath, documentType, documentSerialized, thumbsSerialized, "
		"documentAttributesSerialized, mimeType FROM " + table + " WHERE " + where
		+ " ORDER BY fakeId DESC LIMIT 1";
	if (sqlite3_prepare_v2(db, sql.c_str(), -1, &s, nullptr) != SQLITE_OK) {
		std::printf("  prepare failed: %s\n", sqlite3_errmsg(db));
		return false;
	}
	auto ok = false;
	if (sqlite3_step(s) == SQLITE_ROW) {
		auto i = 0;
		m.fakeId = sqlite3_column_int64(s, i++);
		m.userId = sqlite3_column_int64(s, i++);
		m.dialogId = sqlite3_column_int64(s, i++);
		m.groupedId = sqlite3_column_int64(s, i++);
		m.peerId = sqlite3_column_int64(s, i++);
		m.fromId = sqlite3_column_int64(s, i++);
		m.topicId = sqlite3_column_int64(s, i++);
		m.messageId = sqlite3_column_int(s, i++);
		m.date = sqlite3_column_int(s, i++);
		m.flags = sqlite3_column_int(s, i++);
		m.editDate = sqlite3_column_int(s, i++);
		m.views = sqlite3_column_int(s, i++);
		m.fwdFlags = sqlite3_column_int(s, i++);
		m.fwdFromId = sqlite3_column_int64(s, i++);
		m.fwdName = Text(s, i++);
		m.fwdDate = sqlite3_column_int(s, i++);
		m.fwdPostAuthor = Text(s, i++);
		m.replyFlags = sqlite3_column_int(s, i++);
		m.replyMessageId = sqlite3_column_int(s, i++);
		m.replyPeerId = sqlite3_column_int64(s, i++);
		m.replyTopId = sqlite3_column_int(s, i++);
		m.replyForumTopic = sqlite3_column_int(s, i++) != 0;
		m.replySerialized = Blob(s, i++);
		m.entityCreateDate = sqlite3_column_int(s, i++);
		m.text = Text(s, i++);
		m.textEntities = Blob(s, i++);
		m.mediaPath = Text(s, i++);
		m.hqThumbPath = Text(s, i++);
		m.documentType = sqlite3_column_int(s, i++);
		m.documentSerialized = Blob(s, i++);
		m.thumbsSerialized = Blob(s, i++);
		m.documentAttributesSerialized = Blob(s, i++);
		m.mimeType = Text(s, i++);
		m.postAuthor.clear();
		m.replyMarkupSerialized.clear();
		ok = true;
	}
	sqlite3_finalize(s);
	return ok;
}

int Differences(const AyuMessageBase &a, const AyuMessageBase &b, bool compareFakeId) {
	auto count = 0;
	const auto check = [&](bool same, const char *name) {
		if (!same) {
			if (count < 3) {
				std::printf("    differs: %s\n", name);
			}
			++count;
		}
	};
	check(!compareFakeId || a.fakeId == b.fakeId, "fakeId");
	check(a.userId == b.userId, "userId");
	check(a.dialogId == b.dialogId, "dialogId");
	check(a.groupedId == b.groupedId, "groupedId");
	check(a.peerId == b.peerId, "peerId");
	check(a.fromId == b.fromId, "fromId");
	check(a.topicId == b.topicId, "topicId");
	check(a.messageId == b.messageId, "messageId");
	check(a.date == b.date, "date");
	check(a.flags == b.flags, "flags");
	check(a.editDate == b.editDate, "editDate");
	check(a.views == b.views, "views");
	check(a.fwdFlags == b.fwdFlags, "fwdFlags");
	check(a.fwdFromId == b.fwdFromId, "fwdFromId");
	check(a.fwdName == b.fwdName, "fwdName");
	check(a.fwdDate == b.fwdDate, "fwdDate");
	check(a.fwdPostAuthor == b.fwdPostAuthor, "fwdPostAuthor");
	check(a.postAuthor == b.postAuthor, "postAuthor");
	check(a.replyFlags == b.replyFlags, "replyFlags");
	check(a.replyMessageId == b.replyMessageId, "replyMessageId");
	check(a.replyPeerId == b.replyPeerId, "replyPeerId");
	check(a.replyTopId == b.replyTopId, "replyTopId");
	check(a.replyForumTopic == b.replyForumTopic, "replyForumTopic");
	check(a.replySerialized == b.replySerialized, "replySerialized");
	check(a.replyMarkupSerialized == b.replyMarkupSerialized, "replyMarkupSerialized");
	check(a.entityCreateDate == b.entityCreateDate, "entityCreateDate");
	check(a.text == b.text, "text");
	check(a.textEntities == b.textEntities, "textEntities");
	check(a.mediaPath == b.mediaPath, "mediaPath");
	check(a.hqThumbPath == b.hqThumbPath, "hqThumbPath");
	check(a.documentType == b.documentType, "documentType");
	check(a.documentSerialized == b.documentSerialized, "documentSerialized");
	check(a.thumbsSerialized == b.thumbsSerialized, "thumbsSerialized");
	check(a.documentAttributesSerialized == b.documentAttributesSerialized, "documentAttributesSerialized");
	check(a.mimeType == b.mimeType, "mimeType");
	return count;
}

} // namespace test

namespace test {

inline void AddSynthetic(const std::string &original) {
	{
		sqlite3 *raw = nullptr;
		sqlite3_open_v2(original.c_str(), &raw, SQLITE_OPEN_READWRITE, nullptr);
		Exec(raw, "PRAGMA journal_mode = MEMORY");
		const auto row = [](const std::string &table, long long message, const std::string &text,
				const std::string &rare) {
			return "INSERT INTO " + table + " (userId, dialogId, groupedId, peerId, fromId, topicId, messageId, "
				"date, flags, editDate, views, fwdFlags, fwdFromId, fwdName, fwdDate, fwdPostAuthor, replyFlags, "
				"replyMessageId, replyPeerId, replyTopId, replyForumTopic, replySerialized, entityCreateDate, text, "
				"textEntities, mediaPath, hqThumbPath, documentType, documentSerialized, thumbsSerialized, "
				"documentAttributesSerialized, mimeType) VALUES (1, -777, 0, " + (rare.empty() ? "777" : "42")
				+ ", 5, 0, " + std::to_string(message) + ", 100, 7, 100, 3, "
				+ (rare.empty() ? "0, 0, '', 0, '', 0" : "5, 99, 'Fwd Name', 123, 'pa', 3")
				+ ", 10, " + (rare.empty() ? "0" : "555") + ", 11, 1, "
				+ (rare.empty() ? "X''" : "X'0102'") + ", 100, " + text + ", X'0A0B', '/', "
				+ (rare.empty() ? "''" : "'/thumb'") + ", 1, X'CC', "
				+ (rare.empty() ? "X''" : "X'AA'") + ", " + (rare.empty() ? "X''" : "X'BB'") + ", "
				+ (rare.empty() ? "''" : "'image/png'") + ")";
		};
		Exec(raw, row("DeletedMessage", 1, "'rare'", "x"));
		Exec(raw, row("DeletedMessage", 2, "'old duplicate'", "x"));
		Exec(raw, row("DeletedMessage", 2, "'new duplicate'", ""));
		Exec(raw, row("DeletedMessage", 3, "''", ""));
		Exec(raw, row("EditedMessage", 4, "'first'", ""));
		Exec(raw, row("EditedMessage", 4, "'second'", "x"));
		Exec(raw, row("EditedMessage", 4, "''", ""));
		sqlite3_close(raw);
	}
}

} // namespace test
