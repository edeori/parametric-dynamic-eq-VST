#pragma once

#include <juce_core/juce_core.h>

namespace ParaDynConstants
{
    // Plugin formats require a stable parameter list. 100 bands is a generous
    // ceiling (user-requested) while keeping every control automatable; only
    // bands that are actually "active" are iterated in processBlock, so the
    // CPU cost stays proportional to bands in use, not to this ceiling.
    inline constexpr int maxBands = 100;
    inline constexpr float minFrequencyHz = 20.0f;
    inline constexpr float maxFrequencyHz = 20000.0f;
}

enum class FilterType
{
    Bell = 0,
    LowShelf,
    HighShelf,
    HighPass,
    LowPass
};

enum class ChannelMode
{
    Stereo = 0,
    Mid,
    Side,
    Left,
    Right
};

// Rolloff steepness for High Pass / Low Pass bands only -- meaningless for Bell/Shelf
// types, which stay a single 2-pole (12dB/oct) stage regardless. See SlopeFilter.h for
// how each step is built from cascaded 1-pole/2-pole stages.
enum class FilterSlope
{
    Slope6 = 0,
    Slope12,
    Slope18,
    Slope24
};

inline const juce::StringArray& filterTypeNames()
{
    static const juce::StringArray names { "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass" };
    return names;
}

inline const juce::StringArray& channelModeNames()
{
    static const juce::StringArray names { "Stereo", "Mid", "Side", "Left", "Right" };
    return names;
}

inline const juce::StringArray& oversamplingNames()
{
    static const juce::StringArray names { "Off", "2x", "4x", "8x" };
    return names;
}

inline const juce::StringArray& filterSlopeNames()
{
    static const juce::StringArray names { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct" };
    return names;
}

namespace ParamIDs
{
    inline constexpr auto bypass = "bypass";
    inline constexpr auto output = "output";
    inline constexpr auto oversampling = "oversampling";

    inline juce::String active (int i) { return "active" + juce::String (i); }
    inline juce::String frequency (int i) { return "frequency" + juce::String (i); }
    inline juce::String gain (int i) { return "gain" + juce::String (i); }
    inline juce::String q (int i) { return "q" + juce::String (i); }
    inline juce::String filterType (int i) { return "filterType" + juce::String (i); }
    inline juce::String filterSlope (int i) { return "filterSlope" + juce::String (i); }
    inline juce::String channelMode (int i) { return "channelMode" + juce::String (i); }
    inline juce::String dynamicEnabled (int i) { return "dynamicEnabled" + juce::String (i); }
    inline juce::String threshold (int i) { return "threshold" + juce::String (i); }
    inline juce::String ratio (int i) { return "ratio" + juce::String (i); }
    inline juce::String attack (int i) { return "attack" + juce::String (i); }
    inline juce::String release (int i) { return "release" + juce::String (i); }
    inline juce::String knee (int i) { return "knee" + juce::String (i); }
    inline juce::String solo (int i) { return "solo" + juce::String (i); }
    inline juce::String mute (int i) { return "mute" + juce::String (i); }
    // Per-band maximum dynamic gain reduction, in dB. 0 means "unlimited" (legacy
    // behaviour: the detector's reduction is applied verbatim). Appended to the
    // parameter layout AFTER the main per-band loop so existing host parameter
    // indices never shift. See createParameterLayout().
    inline juce::String range (int i) { return "range" + juce::String (i); }
}
