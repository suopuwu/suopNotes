#include "SpellChecker.h"

#include <QFile>

#ifdef SUOPNOTES_HAVE_SONNET

#include <Sonnet/Speller>

SonnetSpellCheckerBackend::SonnetSpellCheckerBackend(const QString &language)
    // Empty language string picks the user's configured default language
    // automatically — no manual language setup needed.
    : m_speller(new Sonnet::Speller(language))
{
}

SonnetSpellCheckerBackend::~SonnetSpellCheckerBackend()
{
    delete m_speller;
}

bool SonnetSpellCheckerBackend::isValid() const
{
    return m_speller->isValid();
}

bool SonnetSpellCheckerBackend::isMisspelled(const QString &word) const
{
    return m_speller->isMisspelled(word);
}

QStringList SonnetSpellCheckerBackend::suggestions(const QString &word) const
{
    return m_speller->suggest(word);
}

#endif // SUOPNOTES_HAVE_SONNET

#ifdef SUOPNOTES_HAVE_HUNSPELL

#include <hunspell/hunspell.hxx>

HunspellSpellCheckerBackend::HunspellSpellCheckerBackend(const QString &affPath, const QString &dicPath)
{
    // Hunspell's own constructor doesn't cleanly signal "file not found",
    // so check existence ourselves first and leave m_hunspell null rather
    // than risk an unusable or crashing instance.
    if (QFile::exists(affPath) && QFile::exists(dicPath)) {
        m_hunspell = new Hunspell(affPath.toLocal8Bit().constData(), dicPath.toLocal8Bit().constData());
    }
}

HunspellSpellCheckerBackend::~HunspellSpellCheckerBackend()
{
    delete m_hunspell;
}

bool HunspellSpellCheckerBackend::isValid() const
{
    return m_hunspell != nullptr;
}

bool HunspellSpellCheckerBackend::isMisspelled(const QString &word) const
{
    if (!m_hunspell) {
        return false;
    }
    // Hunspell::spell() returns true for CORRECT — the inverse of this
    // codebase's isMisspelled naming.
    return !m_hunspell->spell(word.toStdString());
}

QStringList HunspellSpellCheckerBackend::suggestions(const QString &word) const
{
    if (!m_hunspell) {
        return {};
    }

    QStringList result;
    for (const std::string &suggestion : m_hunspell->suggest(word.toStdString())) {
        result.append(QString::fromStdString(suggestion));
    }
    return result;
}

#endif // SUOPNOTES_HAVE_HUNSPELL

FallbackSpellCheckerBackend::FallbackSpellCheckerBackend(std::unique_ptr<SpellCheckerBackend> primary,
                                                           std::unique_ptr<SpellCheckerBackend> fallback)
    : m_primary(std::move(primary)), m_fallback(std::move(fallback))
{
}

SpellCheckerBackend *FallbackSpellCheckerBackend::activeBackend() const
{
    if (m_primary && m_primary->isValid()) {
        return m_primary.get();
    }
    if (m_fallback) {
        return m_fallback.get();
    }
    return nullptr;
}

bool FallbackSpellCheckerBackend::isValid() const
{
    SpellCheckerBackend *active = activeBackend();
    return active != nullptr && active->isValid();
}

bool FallbackSpellCheckerBackend::isMisspelled(const QString &word) const
{
    SpellCheckerBackend *active = activeBackend();
    return active ? active->isMisspelled(word) : false;
}

QStringList FallbackSpellCheckerBackend::suggestions(const QString &word) const
{
    SpellCheckerBackend *active = activeBackend();
    return active ? active->suggestions(word) : QStringList{};
}

UserDictionaryFilteringBackend::UserDictionaryFilteringBackend(std::unique_ptr<SpellCheckerBackend> inner)
    : m_inner(std::move(inner))
{
}

void UserDictionaryFilteringBackend::setInner(std::unique_ptr<SpellCheckerBackend> inner)
{
    m_inner = std::move(inner);
}

void UserDictionaryFilteringBackend::setUserWords(const QStringList &words)
{
    m_userWordsLower.clear();
    for (const QString &word : words) {
        m_userWordsLower.insert(word.toLower());
    }
}

bool UserDictionaryFilteringBackend::isValid() const
{
    return m_inner ? m_inner->isValid() : false;
}

bool UserDictionaryFilteringBackend::isMisspelled(const QString &word) const
{
    if (m_userWordsLower.contains(word.toLower())) {
        return false;
    }
    return m_inner ? m_inner->isMisspelled(word) : false;
}

QStringList UserDictionaryFilteringBackend::suggestions(const QString &word) const
{
    return m_inner ? m_inner->suggestions(word) : QStringList{};
}
