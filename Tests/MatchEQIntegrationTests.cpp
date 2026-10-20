#include "../Source/PluginProcessor.h"
#include "../Source/MatchEQPanel.h"
#include "../Source/SpectrumAnalyzer.h"
#include "../Source/BandEditor.h"
#include "MatchEQFixtures.h"
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

    void settleEditorTimers()
    {
        // There is no running native message loop in this console test. Drain
        // more than once so JUCE's timer thread can update newly registered timers
        // after its previously posted callback has been acknowledged.
        for (int pass = 0; pass < 4; ++pass)
        {
            juce::Timer::callPendingTimersSynchronously();
            juce::Thread::sleep (60);
        }
        juce::Timer::callPendingTimersSynchronously();
    }

    void checkChildBounds (juce::Component& component)
    {
        for (auto* child : component.getChildren())
        {
            if (! child->isVisible()) continue;
            if (dynamic_cast<MatchEQPanel*> (&component) != nullptr || dynamic_cast<BandEditor*> (&component) != nullptr)
            {
                if (child->getWidth() <= 0 || child->getHeight() <= 0 || ! component.getLocalBounds().contains (child->getBounds()))
                    std::cerr << "Control " << typeid (*child).name() << " " << child->getName()
                              << " bounds=" << child->getBounds().toString()
                              << " panel=" << component.getLocalBounds().toString() << '\n';
                require (child->getWidth() > 0 && child->getHeight() > 0 && component.getLocalBounds().contains (child->getBounds()),
                         "Match and band editor controls fit within their panel");
            }
            if (dynamic_cast<MatchEQPanel*> (child) != nullptr || dynamic_cast<BandEditor*> (child) != nullptr)
                checkChildBounds (*child);
            if (auto* viewport = dynamic_cast<juce::Viewport*> (child))
                if (auto* content = viewport->getViewedComponent())
                    require (viewport->getViewArea().getHeight() >= content->getHeight(),
                             "band labels remain above the horizontal scrollbar");
        }
    }

    double measureGain (ParaDynAudioProcessor& processor, double hz, float amplitude)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        double inPower = 0.0, outPower = 0.0;
        for (int block = 0; block < 240; ++block)
        {
            for (int i = 0; i < 512; ++i)
            {
                const auto x = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (block * 512 + i) / 48000.0);
                buffer.setSample (0, i, x); buffer.setSample (1, i, x);
                if (block >= 200) inPower += x * x;
            }
            processor.processBlock (buffer, midi);
            for (int i = 0; i < 512; ++i)
            {
                const auto y = buffer.getSample (0, i);
                if (! std::isfinite (y)) throw std::runtime_error ("non-finite dynamic processor output");
                if (block >= 200) outPower += y * y;
            }
        }
        return 10.0 * std::log10 (outPower / inPower);
    }

    void testDisplayResponse()
    {
        auto p = std::make_unique<ParaDynAudioProcessor>();
        p->setRateAndBufferSizeDetails (48000, 512);
        p->prepareToPlay (48000, 512);
        p->addBand (15000);
        set (*p, ParamIDs::gain (0), 6);
        set (*p, ParamIDs::q (0), 1.1f);
        SpectrumAnalyzer analyzer (*p);
        for (int factor : { 0, 1, 2, 3 })
        {
            set (*p, ParamIDs::oversampling, (float) factor);
            const auto actual = measureGain (*p, 12000, 0.02f);
            require (std::abs (actual - analyzer.getCorrectionResponseDb (12000, false)) < 0.15,
                     "production display response agrees with actual bell audio at each oversampling factor");
            require (std::abs (analyzer.getCorrectionResponseDb (15000, false) - 6.0f) < 0.001,
                     "production curve equals configured bell gain at its centre");
        }
        set (*p, ParamIDs::gain (0), -4);
        require (std::abs (analyzer.getCorrectionResponseDb (15000, false) + 4.0f) < 0.001,
                 "manual gain edit immediately updates production correction curve");
        set (*p, ParamIDs::mute (0), 1);
        require (std::abs (analyzer.getCorrectionResponseDb (15000, false)) < 0.001,
                 "production correction curve excludes muted bands");
        set (*p, ParamIDs::mute (0), 0);
        set (*p, ParamIDs::frequency (0), 1000);
        set (*p, ParamIDs::gain (0), -2);
        set (*p, ParamIDs::dynamicEnabled (0), 1);
        set (*p, ParamIDs::threshold (0), -35);
        set (*p, ParamIDs::ratio (0), 4);
        set (*p, ParamIDs::range (0), 3);
        const auto actual = measureGain (*p, 1000, 0.25f);
        require (p->getBandDynamicReductionDb (0) > 2.9f && p->getBandDynamicReductionDb (0) <= 3.001f,
                 "runtime dynamics reaches but never exceeds editable Range");
        require (std::abs (analyzer.getCorrectionResponseDb (1000, false) + 2) < 0.001
                 && std::abs (analyzer.getCorrectionResponseDb (1000, true) + 5) < 0.02
                 && std::abs (actual - analyzer.getCorrectionResponseDb (1000, true)) < 0.15,
                 "actual dynamic audio matches effective curve while configured curve stays editable");
        measureGain (*p, 1000, 0.00001f);
        require (p->getBandDynamicReductionDb (0) < 0.05f,
                 "dynamic correction releases between loud episodes");
    }

    void testAutomaticDynamicProcessor (const juce::String& snapshots)
    {
        using namespace MatchEQFixtures;
        auto p = std::make_unique<ParaDynAudioProcessor>();
        p->setRateAndBufferSizeDetails (48000, 512);
        p->prepareToPlay (48000, 512);
        p->addBand (800);
        require (p->getParameters().indexOf (p->apvts.getParameter (ParamIDs::range (0)))
                    > p->getParameters().indexOf (p->apvts.getParameter (ParamIDs::mute (ParaDynConstants::maxBands - 1))),
                 "new dynamic parameters are appended without shifting existing host indices");
        const std::vector<std::pair<juce::String, float>> oldSettings {
            { ParamIDs::dynamicEnabled (0), 1 }, { ParamIDs::threshold (0), -32 },
            { ParamIDs::ratio (0), 5 }, { ParamIDs::attack (0), 7 },
            { ParamIDs::release (0), 310 }, { ParamIDs::knee (0), 9 }, { ParamIDs::range (0), 4 }
        };
        for (const auto& item : oldSettings) set (*p, item.first, item.second);
        capture (p->getMatchEQ(), MatchEQ::Capture::reference, Signal::reference);
        capture (p->getMatchEQ(), MatchEQ::Capture::target, Signal::intermittentResonance);
        require (p->applyMatchEQ().wasOk(), "captured intermittent resonance applies to ordinary EQ bands");
        int dynamicSlot = -1;
        for (auto slot : p->getActiveBandSlots())
            if (p->getBandParams (slot).dynamicEnabled) { dynamicSlot = slot; break; }
        require (dynamicSlot >= 0, "Apply Match automatically enables a dynamic EQ band");
        const auto configured = p->getBandParams (dynamicSlot);
        require (configured.rangeDb > 0 && configured.gainDb < 0,
                 "automatic dynamic band splits static cut and bounded reduction");
        SpectrumAnalyzer analyzer (*p);
        measureGain (*p, configured.frequencyHz, 0.00001f);
        require (p->getBandDynamicReductionDb (dynamicSlot) < 0.05f,
                 "automatically chosen threshold leaves quiet passages uncompressed");
        const auto actual = measureGain (*p, configured.frequencyHz, 0.18f);
        require (p->getBandDynamicReductionDb (dynamicSlot) > 0.1f
                 && p->getBandDynamicReductionDb (dynamicSlot) <= configured.rangeDb + 0.001f,
                 "automatically chosen threshold reacts to resonant episodes within Range");
        require (std::abs (actual - analyzer.getCorrectionResponseDb (configured.frequencyHz, true)) < 0.2,
                 "automatic dynamic correction shown on graph matches measured audio");

        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto copied = std::make_unique<ParaDynAudioProcessor>();
        copied->setStateInformation (state.getData(), (int) state.getSize());
        const auto restored = copied->getBandParams (dynamicSlot);
        require (restored.dynamicEnabled && std::abs (restored.rangeDb - configured.rangeDb) < 0.001
                 && std::abs (restored.thresholdDb - configured.thresholdDb) < 0.001
                 && copied->getMatchEQ().getProfiles()[1].temporal.valid,
                 "project copy preserves automatic dynamics parameters and capture analysis");

        if (snapshots.isNotEmpty())
        {
            p->setSelectedBand (dynamicSlot);
            std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
            for (int width : { 991, 1080, 1800 })
            {
                editor->setSize (width, juce::roundToInt (width * 715.0 / 1180.0));
                settleEditorTimers();
                checkChildBounds (*editor);
                const auto directory = juce::File (snapshots);
                directory.createDirectory();
                juce::FileOutputStream out (directory.getChildFile ("dynamic-match-" + juce::String (width) + ".png"));
                out.setPosition (0); out.truncate();
                juce::PNGImageFormat png;
                require (out.openedOk() && png.writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds()), out),
                         "automatic dynamic Match editor snapshot renders");
            }
        }
        require (p->undoMatchEQ(), "automatic dynamic Match can be undone");
        for (const auto& item : oldSettings)
            require (std::abs (p->apvts.getRawParameterValue (item.first)->load() - item.second) < 0.02,
                     "Undo restores every prior dynamic parameter including Range");

        auto legacy = p->apvts.copyState();
        for (int i = legacy.getNumChildren(); --i >= 0;)
            if (legacy.getChild (i).getProperty ("id").toString().startsWith ("range")) legacy.removeChild (i, nullptr);
        const auto xml = legacy.createXml();
        juce::AudioProcessor::copyXmlToBinary (*xml, state);
        p->setStateInformation (state.getData(), (int) state.getSize());
        require (p->getBandParams (0).rangeDb == 0,
                 "legacy state without Range resets stale cap to unlimited");
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
            settleEditorTimers();
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
        testDisplayResponse();
        testAutomaticDynamicProcessor (argc > 1 ? juce::String (argv[1]) : juce::String());
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
