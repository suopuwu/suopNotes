#include <catch2/catch_test_macros.hpp>

#include "core/PositionEventService.h"

SCENARIO("mergeLivePosition overwrites x/y/screenName for the matching note, preserving its size",
         "[position_event_service]")
{
    GIVEN("a session list with two open notes")
    {
        QList<Settings::NoteSessionState> notes;
        Settings::NoteSessionState a;
        a.noteId = "aaa";
        a.x = 10;
        a.y = 20;
        a.width = 400;
        a.height = 500;
        a.screenName = "DP-1";
        notes.append(a);

        Settings::NoteSessionState b;
        b.noteId = "bbb";
        b.x = 1;
        b.y = 2;
        b.width = 260;
        b.height = 260;
        b.screenName = "HDMI-A-1";
        notes.append(b);

        WHEN("a KWin drag-finished event reports a new position for note bbb")
        {
            const auto result = PositionEventService::mergeLivePosition(notes, "suopNotes-bbb", 900, 700, "DP-2");

            THEN("only bbb's x/y/screenName change, and its size is preserved")
            {
                CHECK(result.noteId == "bbb");
                CHECK(result.x == 900);
                CHECK(result.y == 700);
                CHECK(result.screenName == "DP-2");
                CHECK(result.width == 260);
                CHECK(result.height == 260);
            }
        }
    }
}

SCENARIO("mergeLivePosition ignores window titles that aren't suopNotes note windows",
         "[position_event_service]")
{
    GIVEN("an arbitrary window title not matching the \"suopNotes-<id>\" convention")
    {
        WHEN("mergeLivePosition is called with it")
        {
            const auto result = PositionEventService::mergeLivePosition({}, "some other window", 5, 5, "DP-1");

            THEN("the returned state has an empty noteId, signaling \"ignore this\"")
            {
                CHECK(result.noteId.isEmpty());
            }
        }
    }
}

SCENARIO("mergeLivePosition ignores SelectionToolbar's own pseudo-note title",
         "[position_event_service]")
{
    GIVEN("SelectionToolbar.qml's singleton window, titled \"suopNotes-selectionToolbar\" "
          "(a fixed KWinRuleApplier rule-group key, never a real note)")
    {
        WHEN("a KWin drag-finished event fires for it, same as it would for a real note")
        {
            const auto result =
                PositionEventService::mergeLivePosition({}, "suopNotes-selectionToolbar", 100, 200, "DP-1");

            THEN("the returned state has an empty noteId, signaling \"ignore this\" — "
                 "regression test: this used to be upserted as a real note with id "
                 "\"selectionToolbar\", which on the next launch spawned a phantom NoteWindow "
                 "sharing the toolbar's own window title, so KWin's title-matching move script "
                 "dragged both windows together")
            {
                CHECK(result.noteId.isEmpty());
            }
        }
    }
}

SCENARIO("mergeLivePosition ignores columnOptionsPopup's own pseudo-note title",
         "[position_event_service]")
{
    GIVEN("SelectionToolbar.qml's columnOptionsPopup child window, titled "
          "\"suopNotes-columnOptionsPopup\" (a fixed KWinRuleApplier rule-group key, "
          "never a real note)")
    {
        WHEN("a KWin drag-finished event fires for it, same as it would for a real note")
        {
            const auto result =
                PositionEventService::mergeLivePosition({}, "suopNotes-columnOptionsPopup", 100, 200, "DP-1");

            THEN("the returned state has an empty noteId, signaling \"ignore this\" — "
                 "regression test: this is the exact same bug selectionToolbar's own test "
                 "above guards against, reproduced here once columnOptionsPopup gained its "
                 "own right-click-drag capability without the matching exclusion")
            {
                CHECK(result.noteId.isEmpty());
            }
        }
    }
}

SCENARIO("mergeLivePosition ignores the shared tooltip window's own pseudo-note title",
         "[position_event_service]")
{
    GIVEN("SelectionToolbar.qml's shared tooltipWindow, titled "
          "\"suopNotes-toolbarTooltip\" (a fixed KWinRuleApplier rule-group key, "
          "never a real note)")
    {
        WHEN("a KWin drag-finished event fires for it, same as it would for a real note")
        {
            const auto result =
                PositionEventService::mergeLivePosition({}, "suopNotes-toolbarTooltip", 100, 200, "DP-1");

            THEN("the returned state has an empty noteId, signaling \"ignore this\" — "
                 "same defense-in-depth reasoning as selectionToolbar/columnOptionsPopup's "
                 "own tests above: this window has no drag area of its own, but it's still "
                 "hooked by the KWin watcher script purely by its \"suopNotes-\" title "
                 "prefix, so a compositor-level move (e.g. a Meta+drag keybinding) could "
                 "still fire this event")
            {
                CHECK(result.noteId.isEmpty());
            }
        }
    }
}

SCENARIO("mergeLivePosition handles a note not yet present in the session list",
         "[position_event_service]")
{
    GIVEN("an empty session list — e.g. a race where the event arrives before this note was ever opened")
    {
        WHEN("mergeLivePosition is called for a note id with no existing entry")
        {
            const auto result = PositionEventService::mergeLivePosition({}, "suopNotes-fresh-id", 42, 84, "DP-1");

            THEN("it returns a fresh state for that id with the reported position and default size")
            {
                CHECK(result.noteId == "fresh-id");
                CHECK(result.x == 42);
                CHECK(result.y == 84);
                CHECK(result.screenName == "DP-1");
                CHECK(result.width == 260);
                CHECK(result.height == 260);
            }
        }
    }
}
