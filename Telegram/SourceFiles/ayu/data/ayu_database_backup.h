#pragma once

#include <QtCore/QString>

namespace AyuDatabaseBackup {

[[nodiscard]] bool create();
[[nodiscard]] bool restore(int &index);
void startPeriodic();
void removeAll();
void purgeUser(long long userId);
void configure(int keepCount, int intervalHours);

} // namespace AyuDatabaseBackup
