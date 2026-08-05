#include "TickTickPrefs.h"

#include <QSettings>

namespace {
const QString kGroup = QStringLiteral("sync");
const QString kKeyProjectId = QStringLiteral("projectId");
const QString kKeyProjectName = QStringLiteral("projectName");
const QString kKeyIntervalMinutes = QStringLiteral("intervalMinutes");
const QString kKeyLastSyncTime = QStringLiteral("lastSyncTime");
const QString kKeyMigratedToTaskList = QStringLiteral("migratedToTaskList");
}

TickTickPrefs::TickTickPrefs(const QString &configFilePath)
    : m_configFilePath(configFilePath)
{
}

QString TickTickPrefs::projectId() const
{
    QSettings settings(m_configFilePath, QSettings::IniFormat);
    settings.beginGroup(kGroup);
    return settings.value(kKeyProjectId).toString();
}

QString TickTickPrefs::projectName() const
{
    QSettings settings(m_configFilePath, QSettings::IniFormat);
    settings.beginGroup(kGroup);
    return settings.value(kKeyProjectName).toString();
}

void TickTickPrefs::setProject(const QString &projectId, const QString &projectName)
{
    QSettings settings(m_configFilePath, QSettings::IniFormat);
    settings.beginGroup(kGroup);
    settings.setValue(kKeyProjectId, projectId);
    settings.setValue(kKeyProjectName, projectName);
    settings.sync();
}

int TickTickPrefs::syncIntervalMinutes() const
{
    QSettings settings(m_configFilePath, QSettings::IniFormat);
    settings.beginGroup(kGroup);
    const QVariant stored = settings.value(kKeyIntervalMinutes);
    if (!stored.isValid())
        return kDefaultSyncIntervalMinutes;
    // Clamped on read as well as on write: the stored value can come from a
    // hand-edited file, and this is the number a timer interval is built from.
    return clampSyncIntervalMinutes(stored.toInt());
}

void TickTickPrefs::setSyncIntervalMinutes(int minutes)
{
    QSettings settings(m_configFilePath, QSettings::IniFormat);
    settings.beginGroup(kGroup);
    settings.setValue(kKeyIntervalMinutes, clampSyncIntervalMinutes(minutes));
    settings.sync();
}

QDateTime TickTickPrefs::lastSyncTime() const
{
    QSettings settings(m_configFilePath, QSettings::IniFormat);
    settings.beginGroup(kGroup);
    return settings.value(kKeyLastSyncTime).toDateTime();
}

void TickTickPrefs::setLastSyncTime(const QDateTime &time)
{
    QSettings settings(m_configFilePath, QSettings::IniFormat);
    settings.beginGroup(kGroup);
    settings.setValue(kKeyLastSyncTime, time);
    settings.sync();
}

bool TickTickPrefs::migratedToTaskList() const
{
    QSettings settings(m_configFilePath, QSettings::IniFormat);
    settings.beginGroup(kGroup);
    return settings.value(kKeyMigratedToTaskList, false).toBool();
}

void TickTickPrefs::setMigratedToTaskList(bool migrated)
{
    QSettings settings(m_configFilePath, QSettings::IniFormat);
    settings.beginGroup(kGroup);
    settings.setValue(kKeyMigratedToTaskList, migrated);
    settings.sync();
}

int TickTickPrefs::clampSyncIntervalMinutes(int minutes)
{
    // 0 is a real choice ("never"), not a too-small interval, so it is passed
    // through rather than raised to the floor.
    if (minutes <= 0)
        return 0;
    if (minutes < kMinSyncIntervalMinutes)
        return kMinSyncIntervalMinutes;
    if (minutes > kMaxSyncIntervalMinutes)
        return kMaxSyncIntervalMinutes;
    return minutes;
}
