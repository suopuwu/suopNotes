#pragma once

#include <QObject>
#include <QString>

// Shows a file to the user in their own file manager, SELECTED inside its
// folder — the "reveal"/"show in folder" gesture, not merely opening the
// folder it happens to be in (which the tray's own "Open Notes Folder" item
// already does, and which leaves the user to find the file themselves in a
// folder that may hold hundreds of notes).
//
// Qt has no API for this, and the two obvious-looking substitutes are both
// wrong: QDesktopServices::openUrl() on the .md file OPENS it, launching
// whatever the user's text editor is rather than a file manager, and on its
// folder it opens the folder with nothing selected. The freedesktop.org
// standard for the real gesture is the org.freedesktop.FileManager1 D-Bus
// interface's ShowItems method — implemented by Dolphin, Nautilus, Nemo,
// Thunar and PCManFM, and D-Bus-activatable, so it works even when no file
// manager is currently running.
//
// FileManager1 is a convention rather than a guarantee, so the fallback below
// is load-bearing, not a formality: on a desktop that implements no such
// service the user still gets their folder opened (exactly what the tray item
// does) instead of a menu row that silently does nothing.
class FileRevealer : public QObject
{
    Q_OBJECT
public:
    explicit FileRevealer(QObject *parent = nullptr);

    // Whether `filePath` is something that could be revealed at all. Pure, and
    // the same question a caller's own UI should gate on (the context menu
    // dims its row with this answer, via NoteStore::noteFilePath() returning
    // "" for the same cases).
    static bool canReveal(const QString &filePath);

    // Pure: the folder the fallback path opens for `filePath` — its containing
    // directory, absolute. Empty when canReveal() is false, so a failed reveal
    // can never degrade into opening the user's home directory or the process's
    // working directory out of nowhere.
    static QString containingFolder(const QString &filePath);

    // Asks the file manager to show `filePath` selected. Returns whether the
    // request was DISPATCHED — not whether a window appeared: the D-Bus call is
    // async (see the .cpp for why it must be), so the answer to "did the file
    // manager actually come up" simply isn't available on this call stack.
    // False therefore means only the early, knowable failure: there was no such
    // file to reveal in the first place.
    Q_INVOKABLE bool reveal(const QString &filePath);
};
