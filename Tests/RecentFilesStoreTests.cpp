#include <juce_core/juce_core.h>
#include "../Source/Transport/RecentFilesStore.h"

namespace
{
// Un fichier JSON temporaire par test, jamais le vrai fichier de
// l'utilisateur (~/Library/Application Support/MUVideoPlayerPro/recent_files.json).
juce::File tempStorageFile()
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
               .getChildFile("MUVideoPlayerProTests")
               .getChildFile("recent_files_" + juce::String(juce::Random::getSystemRandom().nextInt64()) + ".json");
}
}

class RecentFilesStoreTests final : public juce::UnitTest
{
public:
    RecentFilesStoreTests() : juce::UnitTest("RecentFilesStore", "Transport") {}

    void runTest() override
    {
        beginTest("Missing file loads as empty, no crash");
        {
            const juce::File file = tempStorageFile();
            RecentFilesStore store(file);
            expect(store.paths().empty());
        }

        beginTest("add() prepends (most recent first)");
        {
            const juce::File file = tempStorageFile();
            RecentFilesStore store(file);

            store.add("/a.mp4");
            store.add("/b.mp4");
            store.add("/c.mp4");

            const auto& paths = store.paths();
            expectEquals((int) paths.size(), 3);
            expectEquals(paths[0], juce::String("/c.mp4"));
            expectEquals(paths[1], juce::String("/b.mp4"));
            expectEquals(paths[2], juce::String("/a.mp4"));

            file.deleteRecursively();
        }

        beginTest("add() of an already-present path moves it to front instead of duplicating");
        {
            const juce::File file = tempStorageFile();
            RecentFilesStore store(file);

            store.add("/a.mp4");
            store.add("/b.mp4");
            store.add("/a.mp4"); // ré-ouvre /a.mp4

            const auto& paths = store.paths();
            expectEquals((int) paths.size(), 2);
            expectEquals(paths[0], juce::String("/a.mp4"));
            expectEquals(paths[1], juce::String("/b.mp4"));

            file.deleteRecursively();
        }

        beginTest("add() caps at 5 entries, dropping the oldest");
        {
            const juce::File file = tempStorageFile();
            RecentFilesStore store(file);

            for (int i = 1; i <= 7; ++i)
                store.add("/" + juce::String(i) + ".mp4");

            const auto& paths = store.paths();
            expectEquals((int) paths.size(), 5);
            expectEquals(paths[0], juce::String("/7.mp4"));
            expectEquals(paths[4], juce::String("/3.mp4")); // /1.mp4 et /2.mp4 ont été évincés

            file.deleteRecursively();
        }

        beginTest("add() ignores an empty path");
        {
            const juce::File file = tempStorageFile();
            RecentFilesStore store(file);

            store.add("");
            expect(store.paths().empty());

            file.deleteRecursively();
        }

        beginTest("clear() empties the list and persists that");
        {
            const juce::File file = tempStorageFile();
            RecentFilesStore store(file);

            store.add("/a.mp4");
            store.clear();
            expect(store.paths().empty());

            RecentFilesStore reloaded(file);
            expect(reloaded.paths().empty());

            file.deleteRecursively();
        }

        beginTest("Persists across instances (round-trip through the JSON file)");
        {
            const juce::File file = tempStorageFile();

            {
                RecentFilesStore store(file);
                store.add("/a.mp4");
                store.add("/b.mp4");
            }

            RecentFilesStore reloaded(file);
            const auto& paths = reloaded.paths();
            expectEquals((int) paths.size(), 2);
            expectEquals(paths[0], juce::String("/b.mp4"));
            expectEquals(paths[1], juce::String("/a.mp4"));

            file.deleteRecursively();
        }

        beginTest("Malformed JSON loads as empty rather than crashing");
        {
            const juce::File file = tempStorageFile();
            file.getParentDirectory().createDirectory();
            file.replaceWithText("not valid json {{{");

            RecentFilesStore store(file);
            expect(store.paths().empty());

            file.deleteRecursively();
        }
    }
};

static RecentFilesStoreTests recentFilesStoreTests;

int main()
{
    juce::UnitTestRunner runner;
    runner.runAllTests();

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* result = runner.getResult(i);
        if (result->failures > 0)
            return 1;
    }

    return 0;
}
