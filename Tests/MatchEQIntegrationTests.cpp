#include "../Source/PluginProcessor.h"
#include "../Source/MatchEQPanel.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    void require (bool condition, const char* message)
    {
        if (! condition) throw std::runtime_error (message);
        std::cout << "  ok: " << message << '\n';
    }

    void set (ParaDynAudioProcessor& p, const juce::String& id, float value)
    {
        auto* parameter = p.apvts.getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    juce::XmlElement profiles()
    {
        juce::XmlElement xml ("MatchEQ");
        xml.setAttribute ("version", 1);
        for (const auto* name : { "Reference", "Target" })
        {
            auto* profile = xml.createNewChildElement (name);
            profile->setAttribute ("version", 1);
            profile->setAttribute ("sampleRate", 48000.0);
            profile->setAttribute ("seconds", 3.0);
            for (int i = 0; i < MatchEQ::pointCount; ++i)
            {
                const auto tilt = juce::String (name) == "Reference" ? 3.0 * std::log10 (MatchEQ::frequency (i) / 1000.0) : 0.0;
                profile->createNewChildElement ("Bin")->setAttribute ("db", -30.0 + tilt);
            }
        }
        return xml;
    }

    void checkChildBounds (juce::Component& component)
    {
        for (auto* child : component.getChildren())
        {
            if (! child->isVisible()) continue;
            if (dynamic_cast<MatchEQPanel*> (&component) != nullptr)
                require (child->getWidth() > 0 && child->getHeight() > 0 && component.getLocalBounds().contains (child->getBounds()),
                         "Match panel control fits within panel");
            if (dynamic_cast<MatchEQPanel*> (child) != nullptr) checkChildBounds (*child);
            if (auto* viewport = dynamic_cast<juce::Viewport*> (child))
                if (auto* content = viewport->getViewedComponent())
                    require (viewport->getViewArea().getHeight() >= content->getHeight(),
                             "band labels remain above the horizontal scrollbar");
        }
    }

    void testProcessor (const juce::String& snapshots)
    {
        auto p = std::make_unique<ParaDynAudioProcessor>();
        p->setRateAndBufferSizeDetails (48000.0, 512);
        p->prepareToPlay (48000.0, 512);
        require (p->applyMatchEQ().failed() && p->getNumBands() == 0, "invalid match leaves processor parameters untouched");
        p->addBand (700.0f);
        set (*p, ParamIDs::gain (0), 4.0f);
        set (*p, ParamIDs::dynamicEnabled (0), 1.0f);
        set (*p, ParamIDs::mute (0), 1.0f);
        set (*p, ParamIDs::solo (8), 1.0f); // even hidden stale solos must not suppress matched bands
        set (*p, ParamIDs::output, -3.0f);
        const auto learned = profiles();
        p->getMatchEQ().restoreState (&learned);
        require (p->applyMatchEQ().wasOk() && p->getNumBands() == 16, "Apply Match creates 16 active bands");
        require (! p->getBandParams (0).dynamicEnabled && ! p->isBandSoloed (8), "Match clears dynamics and hidden solo state");
        require (p->apvts.getRawParameterValue (ParamIDs::mute (0))->load() < 0.5f, "matched band is audible");
        require (std::abs (p->apvts.getRawParameterValue (ParamIDs::output)->load() + 3.0f) < 0.01f, "Match preserves output gain");

        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto copied = std::make_unique<ParaDynAudioProcessor>();
        copied->setStateInformation (state.getData(), (int) state.getSize());
        require (copied->getMatchEQ().getStatus().hasReference && copied->getMatchEQ().getStatus().hasTarget,
                 "DAW state copy includes both learned profiles");
        require (copied->getNumBands() == 16 && ! copied->canUndoMatchEQ(), "DAW state preserves matched bands without stale Undo");
        require (p->undoMatchEQ() && p->getNumBands() == 1, "Undo restores previous band count");
        require (std::abs (p->getBandParams (0).gainDb - 4.0f) < 0.02f && p->getBandParams (0).dynamicEnabled,
                 "Undo restores gain and dynamic mode");
        require (p->isBandSoloed (8) && p->apvts.getRawParameterValue (ParamIDs::mute (0))->load() > 0.5f,
                 "Undo restores mute and solo parameters");
        require (! p->undoMatchEQ(), "Undo is a single-use snapshot");

        // At the actual audio output, verify gain against the independently
        // fitted transfer function, including APVTS quantisation and EQBand.
        for (int factor : { 0, 1, 2, 3 })
        {
            set (*p, ParamIDs::oversampling, (float) factor);
            require (p->applyMatchEQ().wasOk(), "Match supports each oversampling mode");
            for (double hz : { 100.0, 1000.0, 7000.0 })
            {
                double inputPower = 0.0, outputPower = 0.0;
                juce::AudioBuffer<float> buffer (2, 512);
                juce::MidiBuffer midi;
                for (int block = 0; block < 120; ++block)
                {
                    for (int i = 0; i < 512; ++i)
                    {
                        const auto x = 0.05f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * (block * 512 + i) / 48000.0);
                        buffer.setSample (0, i, x); buffer.setSample (1, i, x);
                        if (block >= 80) inputPower += x * x;
                    }
                    p->processBlock (buffer, midi);
                    for (int i = 0; i < 512; ++i)
                    {
                        const auto y = buffer.getSample (0, i);
                        if (! std::isfinite (y)) throw std::runtime_error ("non-finite processor output");
                        if (block >= 80) outputPower += y * y;
                    }
                }
                double expected = -3.0;
                for (int slot : p->getActiveBandSlots())
                {
                    const auto params = p->getBandParams (slot);
                    expected += MatchEQ::responseDb ({ params.frequencyHz, params.gainDb, params.q }, hz, 48000.0 * (1 << factor));
                }
                require (std::abs (10.0 * std::log10 (outputPower / inputPower) - expected) < 0.15,
                         "processor audio follows matched EQ at every oversampling factor");
            }
        }

        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        for (int width : { 991, 1080, 1800 })
        {
            editor->setSize (width, juce::roundToInt (width * 715.0 / 1180.0));
            juce::Thread::sleep (120);
            juce::Timer::callPendingTimersSynchronously();
            checkChildBounds (*editor);
            if (snapshots.isNotEmpty())
            {
                const auto directory = juce::File (snapshots);
                directory.createDirectory();
                juce::FileOutputStream out (directory.getChildFile ("match-eq-" + juce::String (width) + ".png"));
                require (out.openedOk(), "snapshot output opens");
                out.setPosition (0);
                out.truncate();
                juce::PNGImageFormat png;
                require (png.writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds()), out),
                         "editor snapshot renders");
            }
        }
        editor.reset();
        auto legacyXml = p->apvts.copyState().createXml();
        juce::AudioProcessor::copyXmlToBinary (*legacyXml, state);
        p->setStateInformation (state.getData(), (int) state.getSize());
        require (! p->getMatchEQ().getStatus().hasReference && ! p->canUndoMatchEQ(),
                 "legacy DAW project loads without stale reference or Undo");
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        testProcessor (argc > 1 ? juce::String (argv[1]) : juce::String());
        std::cout << "All Match EQ integration tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
