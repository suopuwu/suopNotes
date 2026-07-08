#include "Settings.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace {
constexpr int kDefaultWindowRadius = 12;
constexpr qreal kDefaultWindowOpacity = 0.9;
constexpr bool kDefaultBlurEnabled = false;
constexpr bool kDefaultGlassEnabled = false;
constexpr qreal kDefaultGlassSheenOpacity = 0.12;
constexpr qreal kDefaultGlassSheenStrength = 10;
constexpr qreal kDefaultGlassSheenSize = 0.18;
const char *kDefaultGlassTintColor = "#ffffff";
constexpr bool kDefaultHudAlwaysExpanded = false;
constexpr qreal kDefaultBorderWidth = 0;
constexpr qreal kDefaultScrollbarWidth = 8;
constexpr int kDefaultFontSize = 14;
const char *kDefaultBackgroundColor = "#2b2b2b";
const char *kDefaultBorderColor = "#00000000";
const char *kDefaultFontColor = "#ffffff";
const char *kDefaultFontFamily = "";

const char *kGroupAppearance = "Appearance";
const char *kKeyNotesFolder = "notesFolder";
const char *kKeyWindowRadius = "windowRadius";
const char *kKeyBackgroundColor = "backgroundColor";
const char *kKeyWindowOpacity = "windowOpacity";
const char *kKeyBlurEnabled = "blurEnabled";
const char *kKeyGlassEnabled = "glassEnabled";
const char *kKeyGlassSheenOpacity = "glassSheenOpacity";
const char *kKeyGlassSheenStrength = "glassSheenStrength";
const char *kKeyGlassSheenSize = "glassSheenSize";
const char *kKeyGlassTintColor = "glassTintColor";
const char *kKeyBorderWidth = "borderWidth";
const char *kKeyBorderColor = "borderColor";
const char *kKeyScrollbarWidth = "scrollbarWidth";
const char *kKeyFontSize = "fontSize";
const char *kKeyFontColor = "fontColor";
const char *kKeyFontFamily = "fontFamily";
const char *kKeyHudAlwaysExpanded = "hudAlwaysExpanded";

const char *kArrayOpenNotes = "OpenNotes";
const char *kKeyNoteId = "noteId";
const char *kKeyX = "x";
const char *kKeyY = "y";
const char *kKeyWidth = "width";
const char *kKeyHeight = "height";
const char *kKeyScreenName = "screenName";
const char *kKeyAlwaysOnTop = "alwaysOnTop";

QString noteOverrideGroup(const QString &noteId)
{
    return QStringLiteral("NoteOverride_%1").arg(noteId);
}

QColor colorFromString(const QString &text, const QColor &fallback)
{
    if (text.isEmpty())
        return fallback;
    const QColor color(text);
    return color.isValid() ? color : fallback;
}
}

Settings::Settings(QObject *parent)
    : Settings(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QLatin1String("/settings.ini"),
                parent)
{
}

Settings::Settings(const QString &configFilePath, QObject *parent)
    : QObject(parent)
{
    const QFileInfo info(configFilePath);
    QDir().mkpath(info.absolutePath());

    m_settings = new QSettings(configFilePath, QSettings::IniFormat, this);
    init();
}

Settings::~Settings() = default;

void Settings::init()
{
    if (m_settings)
        m_settings->sync();
}

QString Settings::defaultNotesFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QLatin1String("/notes");
}

QString Settings::notesFolder() const
{
    return m_settings->value(QString("%1/%2").arg(kGroupAppearance, kKeyNotesFolder), defaultNotesFolder()).toString();
}

void Settings::setNotesFolder(const QString &folder)
{
    m_settings->setValue(QString("%1/%2").arg(kGroupAppearance, kKeyNotesFolder), folder);
    m_settings->sync();
    emit notesFolderChanged();
}

int Settings::windowRadius() const
{
    return m_settings->value(QString("%1/%2").arg(kGroupAppearance, kKeyWindowRadius), kDefaultWindowRadius).toInt();
}

void Settings::setWindowRadius(int radius)
{
    m_settings->setValue(QString("%1/%2").arg(kGroupAppearance, kKeyWindowRadius), radius);
    m_settings->sync();
    emit windowRadiusChanged();
}

QColor Settings::backgroundColor() const
{
    const QString stored = m_settings->value(QString("%1/%2").arg(kGroupAppearance, kKeyBackgroundColor)).toString();
    return colorFromString(stored, QColor(kDefaultBackgroundColor));
}

void Settings::setBackgroundColor(const QColor &color)
{
    m_settings->setValue(QString("%1/%2").arg(kGroupAppearance, kKeyBackgroundColor), color.name(QColor::HexArgb));
    m_settings->sync();
    emit backgroundColorChanged();
}

qreal Settings::windowOpacity() const
{
    return m_settings->value(QString("%1/%2").arg(kGroupAppearance, kKeyWindowOpacity), kDefaultWindowOpacity).toReal();
}

void Settings::setWindowOpacity(qreal opacity)
{
    m_settings->setValue(QString("%1/%2").arg(kGroupAppearance, kKeyWindowOpacity), opacity);
    m_settings->sync();
    emit windowOpacityChanged();
}

bool Settings::blurEnabled() const
{
    return m_settings->value(QString("%1/%2").arg(kGroupAppearance, kKeyBlurEnabled), kDefaultBlurEnabled).toBool();
}

void Settings::setBlurEnabled(bool enabled)
{
    m_settings->setValue(QString("%1/%2").arg(kGroupAppearance, kKeyBlurEnabled), enabled);
    m_settings->sync();
    emit blurEnabledChanged();
}

bool Settings::glassEnabled() const
{
    return m_settings->value(QString("%1/%2").arg(kGroupAppearance, kKeyGlassEnabled), kDefaultGlassEnabled).toBool();
}

void Settings::setGlassEnabled(bool enabled)
{
    m_settings->setValue(QString("%1/%2").arg(kGroupAppearance, kKeyGlassEnabled), enabled);
    m_settings->sync();
    emit glassEnabledChanged();
}

qreal Settings::glassSheenOpacity() const
{
    return m_settings->value(QString("%1/%2").arg(kGroupAppearance, kKeyGlassSheenOpacity), kDefaultGlassSheenOpacity)
        .toReal();
}

void Settings::setGlassSheenOpacity(qreal opacity)
{
    m_settings->setValue(QString("%1/%2").arg(kGroupAppearance, kKeyGlassSheenOpacity), opacity);
    m_settings->sync();
    emit glassSheenOpacityChanged();
}

qreal Settings::glassSheenStrength() const
{
    return m_settings->value(QString("%1/%2").arg(kGroupAppearance, kKeyGlassSheenStrength), kDefaultGlassSheenStrength)
        .toReal();
}

void Settings::setGlassSheenStrength(qreal strength)
{
    m_settings->setValue(QString("%1/%2").arg(kGroupAppearance, kKeyGlassSheenStrength), strength);
    m_settings->sync();
    emit glassSheenStrengthChanged();
}

qreal Settings::glassSheenSize() const
{
    return m_settings->value(QString("%1/%2").arg(kGroupAppearance, kKeyGlassSheenSize), kDefaultGlassSheenSize)
        .toReal();
}

void Settings::setGlassSheenSize(qreal size)
{
    m_settings->setValue(QString("%1/%2").arg(kGroupAppearance, kKeyGlassSheenSize), size);
    m_settings->sync();
    emit glassSheenSizeChanged();
}

QColor Settings::glassTintColor() const
{
    const QString stored = m_settings->value(QString("%1/%2").arg(kGroupAppearance, kKeyGlassTintColor)).toString();
    return colorFromString(stored, QColor(kDefaultGlassTintColor));
}

void Settings::setGlassTintColor(const QColor &color)
{
    m_settings->setValue(QString("%1/%2").arg(kGroupAppearance, kKeyGlassTintColor), color.name(QColor::HexArgb));
    m_settings->sync();
    emit glassTintColorChanged();
}

qreal Settings::borderWidth() const
{
    return m_settings->value(QString("%1/%2").arg(kGroupAppearance, kKeyBorderWidth), kDefaultBorderWidth).toReal();
}

void Settings::setBorderWidth(qreal width)
{
    m_settings->setValue(QString("%1/%2").arg(kGroupAppearance, kKeyBorderWidth), width);
    m_settings->sync();
    emit borderWidthChanged();
}

QColor Settings::borderColor() const
{
    const QString stored = m_settings->value(QString("%1/%2").arg(kGroupAppearance, kKeyBorderColor)).toString();
    return colorFromString(stored, QColor(kDefaultBorderColor));
}

void Settings::setBorderColor(const QColor &color)
{
    m_settings->setValue(QString("%1/%2").arg(kGroupAppearance, kKeyBorderColor), color.name(QColor::HexArgb));
    m_settings->sync();
    emit borderColorChanged();
}

qreal Settings::scrollbarWidth() const
{
    return m_settings->value(QString("%1/%2").arg(kGroupAppearance, kKeyScrollbarWidth), kDefaultScrollbarWidth)
        .toReal();
}

void Settings::setScrollbarWidth(qreal width)
{
    m_settings->setValue(QString("%1/%2").arg(kGroupAppearance, kKeyScrollbarWidth), width);
    m_settings->sync();
    emit scrollbarWidthChanged();
}

int Settings::fontSize() const
{
    return m_settings->value(QString("%1/%2").arg(kGroupAppearance, kKeyFontSize), kDefaultFontSize).toInt();
}

void Settings::setFontSize(int size)
{
    m_settings->setValue(QString("%1/%2").arg(kGroupAppearance, kKeyFontSize), size);
    m_settings->sync();
    emit fontSizeChanged();
}

QColor Settings::fontColor() const
{
    const QString stored = m_settings->value(QString("%1/%2").arg(kGroupAppearance, kKeyFontColor)).toString();
    return colorFromString(stored, QColor(kDefaultFontColor));
}

void Settings::setFontColor(const QColor &color)
{
    m_settings->setValue(QString("%1/%2").arg(kGroupAppearance, kKeyFontColor), color.name(QColor::HexArgb));
    m_settings->sync();
    emit fontColorChanged();
}

QString Settings::fontFamily() const
{
    return m_settings->value(QString("%1/%2").arg(kGroupAppearance, kKeyFontFamily), QString(kDefaultFontFamily)).toString();
}

void Settings::setFontFamily(const QString &family)
{
    m_settings->setValue(QString("%1/%2").arg(kGroupAppearance, kKeyFontFamily), family);
    m_settings->sync();
    emit fontFamilyChanged();
}

bool Settings::hudAlwaysExpanded() const
{
    return m_settings->value(QString("%1/%2").arg(kGroupAppearance, kKeyHudAlwaysExpanded), kDefaultHudAlwaysExpanded).toBool();
}

void Settings::setHudAlwaysExpanded(bool alwaysExpanded)
{
    m_settings->setValue(QString("%1/%2").arg(kGroupAppearance, kKeyHudAlwaysExpanded), alwaysExpanded);
    m_settings->sync();
    emit hudAlwaysExpandedChanged();
}

QList<Settings::NoteSessionState> Settings::openNotes() const
{
    QList<NoteSessionState> notes;
    const int count = m_settings->beginReadArray(kArrayOpenNotes);
    notes.reserve(count);
    for (int i = 0; i < count; ++i) {
        m_settings->setArrayIndex(i);
        NoteSessionState state;
        state.noteId = m_settings->value(kKeyNoteId).toString();
        state.x = m_settings->value(kKeyX, 0).toInt();
        state.y = m_settings->value(kKeyY, 0).toInt();
        state.width = m_settings->value(kKeyWidth, 260).toInt();
        state.height = m_settings->value(kKeyHeight, 260).toInt();
        state.screenName = m_settings->value(kKeyScreenName).toString();
        state.alwaysOnTop = m_settings->value(kKeyAlwaysOnTop, false).toBool();
        notes.append(state);
    }
    m_settings->endArray();
    return notes;
}

void Settings::writeOpenNotes(const QList<NoteSessionState> &notes)
{
    m_settings->beginWriteArray(kArrayOpenNotes);
    for (int i = 0; i < notes.size(); ++i) {
        m_settings->setArrayIndex(i);
        const NoteSessionState &state = notes.at(i);
        m_settings->setValue(kKeyNoteId, state.noteId);
        m_settings->setValue(kKeyX, state.x);
        m_settings->setValue(kKeyY, state.y);
        m_settings->setValue(kKeyWidth, state.width);
        m_settings->setValue(kKeyHeight, state.height);
        m_settings->setValue(kKeyScreenName, state.screenName);
        m_settings->setValue(kKeyAlwaysOnTop, state.alwaysOnTop);
    }
    m_settings->endArray();
    m_settings->sync();
}

void Settings::setOpenNotes(const QList<NoteSessionState> &notes)
{
    writeOpenNotes(notes);
    emit openNotesChanged();
}

void Settings::upsertNoteState(const NoteSessionState &state)
{
    QList<NoteSessionState> notes = openNotes();
    bool found = false;
    for (NoteSessionState &existing : notes) {
        if (existing.noteId == state.noteId) {
            existing = state;
            found = true;
            break;
        }
    }
    if (!found)
        notes.append(state);

    writeOpenNotes(notes);
    emit openNotesChanged();
}

void Settings::removeNoteState(const QString &noteId)
{
    QList<NoteSessionState> notes = openNotes();
    const qsizetype removed = notes.removeIf([&noteId](const NoteSessionState &state) {
        return state.noteId == noteId;
    });
    if (removed == 0)
        return;

    writeOpenNotes(notes);
    emit openNotesChanged();
}

Settings::NoteAppearanceOverride Settings::noteOverride(const QString &noteId) const
{
    NoteAppearanceOverride override;
    const QString group = noteOverrideGroup(noteId);

    override.hasWindowRadius = m_settings->value(group + "/hasWindowRadius", false).toBool();
    override.windowRadius = m_settings->value(group + "/windowRadius", override.windowRadius).toInt();

    override.hasBackgroundColor = m_settings->value(group + "/hasBackgroundColor", false).toBool();
    override.backgroundColor =
        colorFromString(m_settings->value(group + "/backgroundColor").toString(), QColor());

    override.hasWindowOpacity = m_settings->value(group + "/hasWindowOpacity", false).toBool();
    override.windowOpacity = m_settings->value(group + "/windowOpacity", override.windowOpacity).toReal();

    override.hasBlurEnabled = m_settings->value(group + "/hasBlurEnabled", false).toBool();
    override.blurEnabled = m_settings->value(group + "/blurEnabled", false).toBool();

    override.hasGlassEnabled = m_settings->value(group + "/hasGlassEnabled", false).toBool();
    override.glassEnabled = m_settings->value(group + "/glassEnabled", false).toBool();

    override.hasGlassSheenOpacity = m_settings->value(group + "/hasGlassSheenOpacity", false).toBool();
    override.glassSheenOpacity = m_settings->value(group + "/glassSheenOpacity", override.glassSheenOpacity).toReal();

    override.hasGlassSheenStrength = m_settings->value(group + "/hasGlassSheenStrength", false).toBool();
    override.glassSheenStrength =
        m_settings->value(group + "/glassSheenStrength", override.glassSheenStrength).toReal();

    override.hasGlassSheenSize = m_settings->value(group + "/hasGlassSheenSize", false).toBool();
    override.glassSheenSize = m_settings->value(group + "/glassSheenSize", override.glassSheenSize).toReal();

    override.hasGlassTintColor = m_settings->value(group + "/hasGlassTintColor", false).toBool();
    override.glassTintColor = colorFromString(m_settings->value(group + "/glassTintColor").toString(), QColor());

    override.hasBorderWidth = m_settings->value(group + "/hasBorderWidth", false).toBool();
    override.borderWidth = m_settings->value(group + "/borderWidth", override.borderWidth).toReal();

    override.hasBorderColor = m_settings->value(group + "/hasBorderColor", false).toBool();
    override.borderColor = colorFromString(m_settings->value(group + "/borderColor").toString(), QColor());

    override.hasScrollbarWidth = m_settings->value(group + "/hasScrollbarWidth", false).toBool();
    override.scrollbarWidth = m_settings->value(group + "/scrollbarWidth", override.scrollbarWidth).toReal();

    override.hasFontSize = m_settings->value(group + "/hasFontSize", false).toBool();
    override.fontSize = m_settings->value(group + "/fontSize", override.fontSize).toInt();

    override.hasFontColor = m_settings->value(group + "/hasFontColor", false).toBool();
    override.fontColor = colorFromString(m_settings->value(group + "/fontColor").toString(), QColor());

    override.hasFontFamily = m_settings->value(group + "/hasFontFamily", false).toBool();
    override.fontFamily = m_settings->value(group + "/fontFamily", override.fontFamily).toString();

    return override;
}

void Settings::setNoteOverride(const QString &noteId, const NoteAppearanceOverride &override)
{
    const QString group = noteOverrideGroup(noteId);

    m_settings->setValue(group + "/hasWindowRadius", override.hasWindowRadius);
    m_settings->setValue(group + "/windowRadius", override.windowRadius);

    m_settings->setValue(group + "/hasBackgroundColor", override.hasBackgroundColor);
    m_settings->setValue(group + "/backgroundColor", override.backgroundColor.name(QColor::HexArgb));

    m_settings->setValue(group + "/hasWindowOpacity", override.hasWindowOpacity);
    m_settings->setValue(group + "/windowOpacity", override.windowOpacity);

    m_settings->setValue(group + "/hasBlurEnabled", override.hasBlurEnabled);
    m_settings->setValue(group + "/blurEnabled", override.blurEnabled);

    m_settings->setValue(group + "/hasGlassEnabled", override.hasGlassEnabled);
    m_settings->setValue(group + "/glassEnabled", override.glassEnabled);

    m_settings->setValue(group + "/hasGlassSheenOpacity", override.hasGlassSheenOpacity);
    m_settings->setValue(group + "/glassSheenOpacity", override.glassSheenOpacity);

    m_settings->setValue(group + "/hasGlassSheenStrength", override.hasGlassSheenStrength);
    m_settings->setValue(group + "/glassSheenStrength", override.glassSheenStrength);

    m_settings->setValue(group + "/hasGlassSheenSize", override.hasGlassSheenSize);
    m_settings->setValue(group + "/glassSheenSize", override.glassSheenSize);

    m_settings->setValue(group + "/hasGlassTintColor", override.hasGlassTintColor);
    m_settings->setValue(group + "/glassTintColor", override.glassTintColor.name(QColor::HexArgb));

    m_settings->setValue(group + "/hasBorderWidth", override.hasBorderWidth);
    m_settings->setValue(group + "/borderWidth", override.borderWidth);

    m_settings->setValue(group + "/hasBorderColor", override.hasBorderColor);
    m_settings->setValue(group + "/borderColor", override.borderColor.name(QColor::HexArgb));

    m_settings->setValue(group + "/hasScrollbarWidth", override.hasScrollbarWidth);
    m_settings->setValue(group + "/scrollbarWidth", override.scrollbarWidth);

    m_settings->setValue(group + "/hasFontSize", override.hasFontSize);
    m_settings->setValue(group + "/fontSize", override.fontSize);

    m_settings->setValue(group + "/hasFontColor", override.hasFontColor);
    m_settings->setValue(group + "/fontColor", override.fontColor.name(QColor::HexArgb));

    m_settings->setValue(group + "/hasFontFamily", override.hasFontFamily);
    m_settings->setValue(group + "/fontFamily", override.fontFamily);

    m_settings->sync();
    emit noteOverrideChanged(noteId);
}

void Settings::clearNoteOverride(const QString &noteId)
{
    m_settings->remove(noteOverrideGroup(noteId));
    m_settings->sync();
    emit noteOverrideChanged(noteId);
}
