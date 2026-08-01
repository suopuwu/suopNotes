#pragma once

#include <QObject>
#include <QString>

#include "TickTickAuth.h"
#include "TickTickTokenStore.h"

// TickTick sync module — the first of the app's optional feature modules, and
// deliberately shaped so removing it is a delete rather than an unpicking:
// everything it owns lives under src/modules/ticktick/, and the only traces
// outside this directory are one option() in the top-level CMakeLists.txt, one
// guarded block in src/CMakeLists.txt, and one #ifdef island in main.cpp.
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

public:
    explicit TickTickModule(QObject *parent = nullptr);

    TickTickAuth *auth();

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
};
