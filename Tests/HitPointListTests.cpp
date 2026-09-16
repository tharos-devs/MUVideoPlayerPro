#include <map>
#include <vector>

#include <juce_core/juce_core.h>
#include "../Source/Transport/HitPointList.h"

class HitPointListTests final : public juce::UnitTest
{
public:
    HitPointListTests() : juce::UnitTest("HitPointList", "Transport") {}

    void runTest() override
    {
        beginTest("add() assigns a stable increasing id and a default '#N' label, keeps chronological order");
        {
            HitPointList list;
            const int id1 = list.add(5000);
            const int id2 = list.add(1000);
            const int id3 = list.add(3000);

            expect(id1 != id2 && id2 != id3 && id1 != id3);

            const auto& items = list.items();
            expectEquals((int) items.size(), 3);
            // Triés chronologiquement, pas dans l'ordre d'ajout.
            expectEquals(items[0].timeMs, 1000);
            expectEquals(items[0].id, id2);
            expectEquals(items[1].timeMs, 3000);
            expectEquals(items[1].id, id3);
            expectEquals(items[2].timeMs, 5000);
            expectEquals(items[2].id, id1);

            expectEquals(items[0].label, juce::String("#" + juce::String(id2)));
        }

        beginTest("id is never reused after a delete, even if it frees up the '#N' the deleted point had");
        {
            HitPointList list;
            const int id1 = list.add(1000); // "#1"
            const int id2 = list.add(2000); // "#2"
            list.add(3000);                 // "#3"

            list.remove(id2);
            const int newId = list.add(2500);

            // Sans ça, un nouveau point ajouté après suppression du "#2"
            // redeviendrait "#3" et entrerait en collision avec le "#3" déjà
            // présent (dérivé de la taille de la liste plutôt que de l'id).
            expect(newId != id1 && newId != id2);
            const juce::String newLabel = "#" + juce::String(newId);

            bool foundDuplicateLabel = false;
            std::map<juce::String, int> labelCounts;
            for (const auto& hitPoint : list.items())
                labelCounts[hitPoint.label]++;
            for (const auto& [label, count] : labelCounts)
                if (count > 1)
                    foundDuplicateLabel = true;

            expect(!foundDuplicateLabel);
            expect(newLabel == "#4"); // 3 déjà attribués (1,2,3), jamais réutilisés
        }

        beginTest("remove() drops the point by id, ignores unknown ids");
        {
            HitPointList list;
            const int id1 = list.add(1000);
            const int id2 = list.add(2000);

            list.remove(id1);
            expectEquals((int) list.items().size(), 1);
            expectEquals(list.items()[0].id, id2);

            list.remove(9999); // id inconnu : no-op
            expectEquals((int) list.items().size(), 1);
        }

        beginTest("rename() trims, falls back to 'Hit <id>' when cleared, no-ops on unknown id");
        {
            HitPointList list;
            const int id = list.add(1000);

            list.rename(id, "  Chorus  ");
            expectEquals(list.items()[0].label, juce::String("Chorus"));

            list.rename(id, "   ");
            expectEquals(list.items()[0].label, juce::String("Hit " + juce::String(id)));

            list.rename(9999, "Ignored"); // id inconnu : no-op
            expectEquals((int) list.items().size(), 1);
        }

        beginTest("setTimeMs() clamps to >= 0 and re-sorts");
        {
            HitPointList list;
            const int id1 = list.add(1000);
            const int id2 = list.add(2000);

            list.setTimeMs(id2, -500);
            expectEquals(list.items()[0].id, id2);
            expectEquals(list.items()[0].timeMs, 0);
            expectEquals(list.items()[1].id, id1);
        }

        beginTest("setColour() updates only the targeted point");
        {
            HitPointList list;
            const int id1 = list.add(1000);
            const int id2 = list.add(2000);
            const uint32_t defaultColour = list.items()[0].colour;

            list.setColour(id2, 0xFF0000);

            for (const auto& hitPoint : list.items())
            {
                if (hitPoint.id == id2)
                    expectEquals((int) hitPoint.colour, (int) 0xFF0000);
                else
                    expectEquals((int) hitPoint.colour, (int) defaultColour);
            }
            juce::ignoreUnused(id1);
        }

        beginTest("restoreFrom() sorts chronologically and backfills id == 0 entries without colliding with existing ids");
        {
            HitPointList list;
            std::vector<HitPoint> loaded;

            HitPoint legacyA; // enregistré avant l'existence du champ id
            legacyA.id = 0;
            legacyA.label = "Old A";
            legacyA.timeMs = 4000;
            loaded.push_back(legacyA);

            HitPoint withId;
            withId.id = 5;
            withId.label = "Kept";
            withId.timeMs = 1000;
            loaded.push_back(withId);

            HitPoint legacyB;
            legacyB.id = 0;
            legacyB.label = "Old B";
            legacyB.timeMs = 2000;
            loaded.push_back(legacyB);

            list.restoreFrom(std::move(loaded));

            const auto& items = list.items();
            expectEquals((int) items.size(), 3);
            // Ordre chronologique.
            expectEquals(items[0].label, juce::String("Kept"));
            expectEquals(items[1].label, juce::String("Old B"));
            expectEquals(items[2].label, juce::String("Old A"));

            // Les id à 0 sont remplacés par de vrais id, distincts entre eux et
            // de l'id existant (5) -- jamais 0 ni doublon.
            std::vector<int> ids;
            for (const auto& hitPoint : items)
                ids.push_back(hitPoint.id);
            for (int id : ids)
                expect(id != 0);
            expect(ids[0] != ids[1] && ids[1] != ids[2] && ids[0] != ids[2]);
        }

        beginTest("clear() empties the list");
        {
            HitPointList list;
            list.add(1000);
            list.add(2000);
            list.clear();
            expect(list.items().empty());
        }
    }
};

static HitPointListTests hitPointListTests;

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
