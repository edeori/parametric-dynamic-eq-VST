#pragma once

#include <juce_core/juce_core.h>

namespace licensing
{
class MachineFingerprint
{
public:
    static juce::String createMachineIdHash();
};
}
