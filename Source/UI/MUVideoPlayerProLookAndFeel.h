#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Thème sombre appliqué à tout l'éditeur (juce::AudioProcessorEditor::
// setLookAndFeel()), pour que les composants au look par défaut de JUCE
// (juce::PopupMenu, juce::TabbedComponent, la ScrollBar...) s'intègrent à la
// charte graphique du plugin au lieu de garder l'apparence claire par défaut
// de LookAndFeel_V4 -- correctif systémique plutôt que des setColour() au
// cas par cas sur chaque composant standard rencontré.
class MUVideoPlayerProLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // Même teinte d'accent que HitPoint::colour par défaut (0x3B94E5) et
    // TimelineComponent, pour rester cohérent partout où quelque chose doit
    // ressortir (sélection, survol, timecode).
    static constexpr uint32_t accentColourArgb = 0xFF3B94E5;
    static constexpr uint32_t panelColourArgb = 0xFF2A2A2A;
    static constexpr uint32_t backgroundColourArgb = 0xFF1E1E1E;

    MUVideoPlayerProLookAndFeel()
    {
        setColourScheme(getDarkColourScheme());

        const juce::Colour accent(accentColourArgb);
        const juce::Colour panel(panelColourArgb);
        const juce::Colour background(backgroundColourArgb);

        setColour(juce::PopupMenu::backgroundColourId, panel);
        setColour(juce::PopupMenu::textColourId, juce::Colours::white);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, accent);
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

        setColour(juce::TextButton::buttonColourId, panel);
        setColour(juce::TextButton::buttonOnColourId, accent.withAlpha(0.6f));
        setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        setColour(juce::TextButton::textColourOnId, juce::Colours::white);

        setColour(juce::ScrollBar::thumbColourId, juce::Colours::white.withAlpha(0.3f));
        setColour(juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);

        setColour(juce::TextEditor::backgroundColourId, background);
        setColour(juce::TextEditor::textColourId, juce::Colours::white);
        setColour(juce::TextEditor::outlineColourId, accent);
        setColour(juce::TextEditor::focusedOutlineColourId, accent);

        setColour(juce::ComboBox::backgroundColourId, panel);
        setColour(juce::ComboBox::textColourId, juce::Colours::white);
        setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        setColour(juce::ComboBox::arrowColourId, juce::Colours::white);

        setColour(juce::DrawableButton::backgroundColourId, panel);
        setColour(juce::DrawableButton::backgroundOnColourId, accent.withAlpha(0.6f));
    }

    // Taille fixe plutôt que proportionnelle à la hauteur du bouton (le
    // comportement par défaut de LookAndFeel_V4) : sur les petits boutons de
    // la barre de zoom (16-20px de large), la police proportionnelle calculée
    // ne laissait pas assez de place pour un seul caractère ("-", "+", "▾")
    // et JUCE l'affichait tronqué en "…", illisible et ressemblant à une
    // icône indéfinie plutôt qu'au caractère attendu.
    juce::Font getTextButtonFont(juce::TextButton&, int) override
    {
        return juce::Font(juce::FontOptions(13.0f).withStyle("Bold"));
    }

    // LookAndFeel_V4::drawButtonBackground() ne distingue le survol (pas
    // encore pressé) du repos que par un contrasting(0.05f) -- trop subtil
    // pour être perceptible sur un fond déjà sombre (retour utilisateur :
    // "je ne vois pas l'effet hover"). Éclaircit nettement au survol au lieu
    // du léger contraste par défaut.
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        juce::Colour adjusted = backgroundColour;

        if (shouldDrawButtonAsDown)
            adjusted = adjusted.contrasting(0.2f);
        else if (shouldDrawButtonAsHighlighted)
            adjusted = adjusted.brighter(0.4f);

        LookAndFeel_V4::drawButtonBackground(g, button, adjusted, false, shouldDrawButtonAsDown);
    }
};
