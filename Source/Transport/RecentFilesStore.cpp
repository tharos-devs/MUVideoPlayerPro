#include "RecentFilesStore.h"

#include <algorithm>

RecentFilesStore::RecentFilesStore() : RecentFilesStore(defaultStorageFile())
{
}

RecentFilesStore::RecentFilesStore(juce::File customStorageFile) : storageFile(std::move(customStorageFile))
{
    load();
}

juce::File RecentFilesStore::defaultStorageFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("MUVideoPlayerPro")
               .getChildFile("recent_files.json");
}

void RecentFilesStore::load()
{
    recentPaths.clear();

    if (!storageFile.existsAsFile())
        return;

    const juce::var parsed = juce::JSON::parse(storageFile);
    if (const auto* array = parsed.getArray())
        for (const auto& value : *array)
            if (value.isString())
                recentPaths.push_back(value.toString());
}

void RecentFilesStore::save() const
{
    storageFile.getParentDirectory().createDirectory();

    juce::Array<juce::var> array;
    for (const auto& path : recentPaths)
        array.add(path);

    storageFile.replaceWithText(juce::JSON::toString(juce::var(array)));
}

void RecentFilesStore::add(const juce::String& path)
{
    if (path.isEmpty())
        return;

    recentPaths.erase(std::remove(recentPaths.begin(), recentPaths.end(), path), recentPaths.end());
    recentPaths.insert(recentPaths.begin(), path);

    while ((int) recentPaths.size() > maxEntries)
        recentPaths.pop_back();

    save();
}

void RecentFilesStore::clear()
{
    recentPaths.clear();
    save();
}
