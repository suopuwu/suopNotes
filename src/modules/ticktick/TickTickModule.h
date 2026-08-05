#pragma once

#include <QObject>
#include <QString>

#include "TickTickAuth.h"
#include "TickTickNotes.h"
#include "TickTickSync.h"
#include "TickTickTokenStore.h"

// TickTick sync module — the first of the app's optional feature modules.
//
// What lives where, honestly stated. The C++ is fully self-contained:
// everything under src/modules/ticktick/ (including this module's own QML in
// qml/), with the only traces outside being one option() in the top-level
// CMakeLists.txt, guarded blocks in src/CMakeLists.txt and
// tests/qml/CMakeLists.txt, and one #ifdef island in main.cpp — core C++ has
// zero TickTick code, and the "tt:" id prefix never escapes this directory.
// The QML side is looser: the settings section, sync footer and list title
// are module-owned files here, Loader-referenced by URL from shared QML, but
// the task note's editing behavior (compose slot chrome and key handling,
// the completion fade, multi-line-selection guards) is threaded through
// NoteWindow.qml behind `tickTick !== null` guards. A build with the module
// OFF is clean and correct; removing the module from the SOURCE means also
// unpicking those NoteWindow.qml sections.
//
// Design context lives in .task/ticktick-proposal.md. The two decisions that
// shape this class:
//
//  - A TickTick-backed note is a SECOND KIND of note, not a local note kept in
//    sync with a remote one. TickTick owns the content; the local file is a
//    cache, never a peer. That is what removes conflict resolution from the
//    design entirely — there is only ever one authority, so there is nothing to
//    merge (which matters because TickTick exposes no modification timestamp to
//    merge against).
//
//  - Ownership is carried in the note id itself. Every id-keyed subsystem in
//    the app (window geometry, per-note appearance overrides, KWin rules) is
//    storage-agnostic and stores an opaque id string, so a prefixed id flows
//    through all of them unchanged while reconcile can still tell whose note
//    it is.
//
// This is currently the module skeleton: it establishes the build seam and the
// id scheme. Auth, the API client and the cache are Phases 3-5.
class TickTickModule : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool secureStorageAvailable READ secureStorageAvailable CONSTANT)
    // The module is the single object main.cpp knows about; everything else it
    // owns is reached through it, so adding a part later costs no new context
    // property and no further change to main.cpp.
    Q_PROPERTY(TickTickAuth *auth READ auth CONSTANT)
    Q_PROPERTY(TickTickSync *sync READ sync CONSTANT)

public:
    explicit TickTickModule(QObject *parent = nullptr);

    // Test seam, matching the explicit-path constructors on Settings,
    // TickTickTokenStore and TickTickNotes. Uses the config-file-only secret
    // backend, so a test can never reach the developer's real keyring — where
    // it would otherwise overwrite their actual TickTick credentials, since the
    // service name is the same one the app uses.
    TickTickModule(const QString &configFilePath,
                   const QString &cacheDirPath,
                   QObject *parent = nullptr);

    TickTickAuth *auth();

    TickTickSync *sync();

    // The note provider, for registering with NoteStore in main().
    TickTickNotes *notes();

    // Whether this build can put the access token in the platform secret store
    // instead of plaintext config. Reported rather than assumed because
    // qtkeychain is an optional dependency: absent it, the module degrades to
    // plaintext with a warning rather than disabling itself. That degradation
    // is also why qtkeychain's presence cannot double as this module's on/off
    // switch — the two are separate concerns.
    //
    // It matters more here than it looks: the access token is valid for 180
    // days (measured, not documented) and carries full task read/write, so it
    // is a real credential rather than a short-lived artifact.
    bool secureStorageAvailable() const;

    // --- note id ownership ---
    //
    // Pure, side-effect-free, and static so they can be specified without
    // constructing the module or touching the network — the same split the
    // codebase already uses for PositionEventService::mergeLivePosition,
    // buildKWinRuleKeys and buildAutostartDesktopEntry.

    // The marker that makes a note id self-describing. Kept as a function
    // rather than a literal sprinkled around so the scheme has one definition.
    static QString prefix();

    // True for ids this module is responsible for. Anything else belongs to the
    // local note store and must be left alone — note reconcile treats the local
    // store's enumeration as authoritative, so a module note that answered
    // false here would be force-closed the moment anything refreshed.
    static bool ownsNoteId(const QString &noteId);

    // Wraps a locally-minted uuid into an owned note id. The uuid stays local
    // and stable for the note's whole life; the remote task id is mapped to it
    // inside the module rather than being used as the key, so the note survives
    // the task being recreated remotely.
    static QString makeNoteId(const QString &localUuid);

    // Inverse of makeNoteId. Returns an empty string for an id this module does
    // not own, so callers get one unambiguous "not mine" answer rather than
    // having to pre-check.
    static QString localUuidFor(const QString &noteId);

private:
    // Declared in dependency order: the store outlives the auth object that
    // holds a reference to it, since members are destroyed in reverse.
    TickTickTokenStore m_tokenStore;
    TickTickAuth m_auth;
    TickTickNotes m_notes;
    TickTickSync m_sync;
};
