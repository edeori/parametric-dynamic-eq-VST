#pragma once

#include <juce_graphics/juce_graphics.h>

// Four-function pattern copied verbatim from the sibling Moth Production plugins
// (MultiSqueezeFonts, PhaseLockFonts, StringLifeFonts) -- title/uiLight/uiMedium/numeric,
// each backed by one of the four shared house fonts. Don't hand-roll a different structure.
namespace ParaDynFonts
{
    juce::Font title (float size);
    juce::Font uiLight (float size);
    juce::Font uiMedium (float size);
    juce::Font numeric (float size);
}
