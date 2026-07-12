#pragma once

#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>

// Seam over the actual spell-checking backend (Sonnet, when available) so
// callers and tests don't depend on a real dictionary being installed.
class SpellCheckerBackend
{
public:
    virtual ~SpellCheckerBackend() = default;
    // False if no working dictionary is available at all (e.g. Sonnet found
    // no installed hunspell dictionary) — callers should treat this as
    // "spellcheck unavailable" and skip highlighting entirely rather than
    // flagging every word as misspelled.
    virtual bool isValid() const = 0;
    virtual bool isMisspelled(const QString &word) const = 0;
    virtual QStringList suggestions(const QString &word) const = 0;
};

// Real backend: wraps a long-lived Sonnet::Speller. Only compiled when
// SUOPNOTES_HAVE_SONNET is defined (see src/CMakeLists.txt) — Sonnet is an
// optional dependency, same convention as SUOPNOTES_HAVE_KWINDOWSYSTEM for
// KF6WindowSystem elsewhere in this codebase.
#ifdef SUOPNOTES_HAVE_SONNET
namespace Sonnet
{
class Speller;
}

class SonnetSpellCheckerBackend : public SpellCheckerBackend
{
public:
    // Empty (default) language picks the user's system-configured default
    // automatically. Pass an explicit language (e.g. "en_US") to force a
    // specific dictionary regardless of system configuration.
    explicit SonnetSpellCheckerBackend(const QString &language = QString());
    ~SonnetSpellCheckerBackend() override;

    bool isValid() const override;
    bool isMisspelled(const QString &word) const override;
    QStringList suggestions(const QString &word) const override;

private:
    // Constructed once here (in the .cpp), not per-call — building a
    // Sonnet::Speller loads a dictionary and is not cheap. pImpl-style
    // pointer keeps the real Sonnet header out of this widely-included
    // header file.
    Sonnet::Speller *m_speller;
};
#endif

// Real backend: wraps a long-lived Hunspell instance pointed at an explicit
// .aff/.dic pair on disk. Only compiled when SUOPNOTES_HAVE_HUNSPELL is
// defined (see src/CMakeLists.txt) — Hunspell is an optional dependency,
// same convention as SUOPNOTES_HAVE_SONNET above. Used both directly (e.g.
// against a bundled dictionary shipped with the app) and as the fallback
// half of a FallbackSpellCheckerBackend.
#ifdef SUOPNOTES_HAVE_HUNSPELL
class Hunspell;

class HunspellSpellCheckerBackend : public SpellCheckerBackend
{
public:
    // affPath/dicPath are real filesystem paths to a .aff/.dic pair. If
    // either file doesn't exist or Hunspell fails to load them, isValid()
    // returns false and isMisspelled()/suggestions() are safe no-ops —
    // mirrors SonnetSpellCheckerBackend's own "gracefully degrade, never
    // crash on a missing dictionary" contract.
    HunspellSpellCheckerBackend(const QString &affPath, const QString &dicPath);
    ~HunspellSpellCheckerBackend() override;

    bool isValid() const override;
    bool isMisspelled(const QString &word) const override;
    QStringList suggestions(const QString &word) const override;

private:
    Hunspell *m_hunspell = nullptr; // null if construction failed
};
#endif

// Decorator: tries `primary` first; if primary->isValid() is false,
// delegates everything to `fallback` instead. isValid() is true if EITHER
// is valid. Real use: primary = Sonnet (respects the user's
// system-configured dictionary/language), fallback = a bundled Hunspell
// dictionary shipped with the app, so spellcheck still works on a machine
// with no system dictionary installed at all. Not gated behind any ifdef —
// it doesn't care what concrete backend types it's holding.
class FallbackSpellCheckerBackend : public SpellCheckerBackend
{
public:
    FallbackSpellCheckerBackend(std::unique_ptr<SpellCheckerBackend> primary,
                                 std::unique_ptr<SpellCheckerBackend> fallback);

    bool isValid() const override;
    bool isMisspelled(const QString &word) const override;
    QStringList suggestions(const QString &word) const override;

private:
    // Returns primary if primary->isValid(), else fallback (which may
    // itself be invalid or null).
    SpellCheckerBackend *activeBackend() const;

    std::unique_ptr<SpellCheckerBackend> m_primary;
    std::unique_ptr<SpellCheckerBackend> m_fallback;
};

// Decorator: wraps an inner backend; words in the user-added set are NEVER
// considered misspelled, regardless of what the inner backend thinks (an
// app-level personal dictionary, independent of which underlying
// backend/language is active). Case-insensitive matching. The inner backend
// is swappable via setInner() (e.g. when the user changes their spellcheck
// language) so callers can hold a long-lived pointer to THIS object without
// it ever dangling — only the inner backend gets replaced, not this
// wrapper.
class UserDictionaryFilteringBackend : public SpellCheckerBackend
{
public:
    explicit UserDictionaryFilteringBackend(std::unique_ptr<SpellCheckerBackend> inner);

    void setInner(std::unique_ptr<SpellCheckerBackend> inner);
    void setUserWords(const QStringList &words); // replaces the whole set; case-insensitive storage/lookup

    bool isValid() const override;
    bool isMisspelled(const QString &word) const override;
    QStringList suggestions(const QString &word) const override;

private:
    std::unique_ptr<SpellCheckerBackend> m_inner;
    QSet<QString> m_userWordsLower; // lowercased for case-insensitive contains()
};
