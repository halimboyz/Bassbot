#include "BassDsp.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace
{
    constexpr int kW = 760, kH = 380, kBar = 76;
    constexpr float kStart = juce::MathConstants<float>::pi * 1.25f, kEnd = juce::MathConstants<float>::pi * 2.75f;
    const juce::Colour kNavy { 0xff0b1d4a }, kLime { 0xff9dff4a }, kPink { 0xffff4fb0 };

    struct Spec { const char* id; const char* name; float lo, hi, def; bool isDb; };
    const Spec kSpecs[] = {
        { "in",     "IN",     -12.f, 24.f, 6.f,   true  },
        { "vowel",  "VOWEL",  0.f,   1.f,  0.40f, false },
        { "grit",   "GRIT",   0.f,   1.f,  0.45f, false },
        { "crunch", "CRUNCH", 0.f,   1.f,  0.60f, false },
        { "sub",    "SUB",    0.f,   1.f,  0.70f, false },
        { "out",    "OUT",    -24.f, 6.f,  0.f,   true  },
    };

    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout l;
        for (auto& s : kSpecs)
        {
            auto text = [db = s.isDb] (float v, int)
            {
                return db ? juce::String (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"
                          : juce::String (juce::roundToInt (v * 100.f)) + "%";
            };
            l.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { s.id, 1 }, s.name, juce::NormalisableRange<float> (s.lo, s.hi), s.def,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction (text)));
        }
        return l;
    }

    juce::Font fontBold (float h) { return juce::Font (juce::FontOptions ("Verdana", h, juce::Font::bold)); }

    void star (juce::Graphics& g, float cx, float cy, float s, juce::Colour c)
    {
        juce::Path p;
        p.startNewSubPath (cx, cy - s);
        p.quadraticTo (cx, cy, cx + s, cy);
        p.quadraticTo (cx, cy, cx, cy + s);
        p.quadraticTo (cx, cy, cx - s, cy);
        p.quadraticTo (cx, cy, cx, cy - s);
        p.closeSubPath();
        g.setColour (c);
        g.fillPath (p);
    }
}

//==============================================================================
class BassbotProcessor : public juce::AudioProcessor
{
public:
    BassbotProcessor()
        : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "STATE", makeLayout())
    {
        for (size_t i = 0; i < 6; ++i) raw[i] = apvts.getRawParameterValue (kSpecs[i].id);
    }

    const juce::String getName() const override { return "BASSBOT 2000"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void prepareToPlay (double sr, int) override { dsp.prepare (sr, 2); }
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto o = l.getMainOutputChannelSet();
        return (o == juce::AudioChannelSet::mono() || o == juce::AudioChannelSet::stereo()) && o == l.getMainInputChannelSet();
    }

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        const BassParams p { *raw[0], *raw[1], *raw[2], *raw[3], *raw[4], *raw[5] };
        dsp.process (b.getArrayOfWritePointers(), b.getNumChannels(), b.getNumSamples(), p);
        peak.store (juce::jmax (peak.load(), dsp.takePeak()));
    }

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation (juce::MemoryBlock& d) override
    {
        if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size))
            if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> peak { 0.f };

private:
    BassDsp dsp;
    std::atomic<float>* raw[6] {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassbotProcessor)
};

//==============================================================================
struct MetalLook : juce::LookAndFeel_V4
{
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override
    {
        const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const auto c = b.getCentre();
        const float R = b.getWidth() * 0.5f, arcR = R - 4.5f, ang = a0 + pos * (a1 - a0);
        const juce::PathStrokeType stroke (9.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path track, val;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.f, a0, a1, true);
        val.addCentredArc (c.x, c.y, arcR, arcR, 0.f, a0, ang, true);
        g.setColour (juce::Colour (0xff5a6478));
        g.strokePath (track, stroke);
        g.setColour (juce::Colour (0xff8dff2e));
        g.strokePath (val, stroke);

        const float kr = R - 9.f;
        const auto knob = juce::Rectangle<float> (kr * 2.f, kr * 2.f).withCentre (c);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (knob.translated (0.f, 3.f).expanded (1.f));

        // brushed-metal look: fixed light/dark wedges around the knob
        constexpr int n = 24;
        for (int i = 0; i < n; ++i)
        {
            const float t = 0.5f + 0.5f * std::sin ((float) i * juce::MathConstants<float>::twoPi / n * 2.f + 0.9f);
            const float f = juce::MathConstants<float>::twoPi / n;
            juce::Path wedge;
            wedge.addPieSegment (knob, (float) i * f, (float) (i + 1) * f + 0.03f, 0.f);
            g.setColour (juce::Colour (0xff7c869c).interpolatedWith (juce::Colour (0xfff7f9fd), t));
            g.fillPath (wedge);
        }
        g.setColour (juce::Colour (0xff3a4560));
        g.drawEllipse (knob, 1.2f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.35f), 0.f, knob.getY(),
                                                 juce::Colours::transparentWhite, 0.f, knob.getCentreY(), false));
        g.fillEllipse (knob.getX() + knob.getWidth() * 0.14f, knob.getY() + knob.getHeight() * 0.06f,
                       knob.getWidth() * 0.72f, knob.getHeight() * 0.42f);

        const auto dot = c + juce::Point<float> (std::sin (ang), -std::cos (ang)) * (kr - 8.f);
        g.setColour (kPink.withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (14.f, 14.f).withCentre (dot));
        g.setColour (juce::Colour (0xffff3fa4));
        g.fillEllipse (juce::Rectangle<float> (8.f, 8.f).withCentre (dot));
    }
};

class BassbotEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BassbotEditor (BassbotProcessor& p) : AudioProcessorEditor (p), proc (p)
    {
        setLookAndFeel (&look);
        setSize (kW, kH);

        for (size_t i = 0; i < knobs.size(); ++i)
        {
            auto& k = knobs[i];
            k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            k.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            k.slider.setRotaryParameters (kStart, kEnd, true);
            k.name.setText (kSpecs[i].name, juce::dontSendNotification);
            k.name.setFont (fontBold (11.f));
            k.name.setColour (juce::Label::textColourId, juce::Colours::white);
            k.name.setJustificationType (juce::Justification::centred);
            k.value.setFont (juce::Font (juce::FontOptions ("Courier New", 12.f, juce::Font::bold)));
            k.value.setColour (juce::Label::textColourId, kLime);
            k.value.setJustificationType (juce::Justification::centred);
            for (auto* c : { (juce::Component*) &k.slider, (juce::Component*) &k.name, (juce::Component*) &k.value })
                addAndMakeVisible (c);
            k.name.setInterceptsMouseClicks (false, false);
            k.value.setInterceptsMouseClicks (false, false);

            k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, kSpecs[i].id, k.slider);
            k.slider.onValueChange = [&k] { k.value.setText (k.slider.getTextFromValue (k.slider.getValue()), juce::dontSendNotification); };
            k.slider.onValueChange();
        }
        startTimerHz (30);
    }

    ~BassbotEditor() override { setLookAndFeel (nullptr); }

    void resized() override
    {
        const auto panel = getLocalBounds().reduced (10).withTrimmedTop (kBar).reduced (34, 0);
        const int colW = panel.getWidth() / 6, top = panel.getCentreY() - 74;
        for (int i = 0; i < 6; ++i)
        {
            const int cx = panel.getX() + colW * i + colW / 2;
            auto& k = knobs[(size_t) i];
            k.slider.setBounds (cx - 42, top, 84, 84);
            k.name.setBounds (cx - 45, top + 98, 90, 14);
            k.value.setBounds (cx - 34, top + 126, 68, 24);
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto full = getLocalBounds().toFloat();

        juce::ColourGradient bezel (juce::Colour (0xfff7f9ff), 0.f, 0.f, juce::Colour (0xffbfe9f7), full.getRight(), full.getBottom(), false);
        bezel.addColour (0.30, juce::Colour (0xffc3d0ea));
        bezel.addColour (0.50, juce::Colours::white);
        bezel.addColour (0.75, juce::Colour (0xffd7c4f2));
        g.setGradientFill (bezel);
        g.fillRoundedRectangle (full, 28.f);

        auto inner = full.reduced (10.f);
        juce::Path clip;
        clip.addRoundedRectangle (inner, 20.f);
        g.reduceClipRegion (clip);

        // top bar
        const auto bar = inner.removeFromTop ((float) kBar);
        juce::ColourGradient silver (juce::Colour (0xfffdfdff), 0.f, bar.getY(), juce::Colour (0xffe9effb), 0.f, bar.getBottom(), false);
        silver.addColour (0.45, juce::Colour (0xffdbe3f3));
        silver.addColour (0.50, juce::Colour (0xffb9c6e0));
        g.setGradientFill (silver);
        g.fillRect (bar);

        g.setColour (kNavy);
        g.setFont (fontBold (26.f));
        g.drawText ("BASSBOT", juce::Rectangle<float> (bar.getX() + 28.f, bar.getY() + 12.f, 190.f, 32.f), juce::Justification::centredLeft);
        const auto pill = juce::Rectangle<float> (bar.getX() + 222.f, bar.getY() + 17.f, 56.f, 24.f);
        juce::ColourGradient pg (juce::Colour (0xffffa6dc), 0.f, pill.getY(), juce::Colour (0xffff8fd0), 0.f, pill.getBottom(), false);
        pg.addColour (0.50, juce::Colour (0xffff6fc0));
        pg.addColour (0.52, kPink);
        g.setGradientFill (pg);
        g.fillRoundedRectangle (pill, 12.f);
        g.setColour (kNavy);
        g.setFont (fontBold (13.f));
        g.drawText ("2000", pill, juce::Justification::centred);
        g.setFont (juce::Font (juce::FontOptions ("Verdana", 10.f, juce::Font::plain)));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("VOICE \xc2\xb7 DRUMS \xc2\xb7 SAMPLES \xe2\x86\x92 BASS")),
                    juce::Rectangle<float> (bar.getX() + 28.f, bar.getY() + 46.f, 300.f, 14.f), juce::Justification::centredLeft);

        g.setFont (fontBold (10.f));
        g.drawText ("OUT", juce::Rectangle<float> (meterBox().getX() - 38.f, meterBox().getY(), 30.f, meterBox().getHeight()), juce::Justification::centredRight);
        drawMeter (g);

        // gunmetal panel
        juce::ColourGradient steel (juce::Colour (0xff7b8494), 0.f, inner.getY(), juce::Colour (0xff2e3440), 0.f, inner.getBottom(), false);
        steel.addColour (0.55, juce::Colour (0xff4a5160));
        g.setGradientFill (steel);
        g.fillRect (inner);
        for (int x = (int) inner.getX(); x < (int) inner.getRight(); x += 3)
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.drawVerticalLine (x, inner.getY(), inner.getBottom());
            g.setColour (juce::Colours::black.withAlpha (0.07f));
            g.drawVerticalLine (x + 1, inner.getY(), inner.getBottom());
        }
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f), 0.f, inner.getY(),
                                                 juce::Colours::white.withAlpha (0.02f), 0.f, inner.getY() + inner.getHeight() * 0.46f, false));
        g.fillRect (inner.withHeight (inner.getHeight() * 0.46f));

        star (g, inner.getX() + 27.f, inner.getBottom() - 27.f, 13.f, juce::Colours::white);
        star (g, inner.getRight() - 31.f, inner.getY() + 23.f, 9.f, juce::Colour (0xffff8fd0));
        star (g, inner.getRight() - 127.f, inner.getBottom() - 19.f, 7.f, kLime);

        for (auto& k : knobs)
        {
            g.setColour (juce::Colour (0xff071a3f));
            g.fillRoundedRectangle (k.value.getBounds().toFloat(), 6.f);
        }
    }

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label name, value;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach; // after slider: destroyed first
    };

    static juce::Rectangle<float> meterBox() { return { 550.f, 28.f, 172.f, 40.f }; }

    void drawMeter (juce::Graphics& g) const
    {
        const auto box = meterBox();
        g.setColour (juce::Colour (0xff071a3f));
        g.fillRoundedRectangle (box, 8.f);
        const float db = juce::Decibels::gainToDecibels (level, -60.f);
        for (int i = 0; i < 12; ++i)
        {
            const bool on = db > -33.f + (float) i * 3.f;
            const auto lit = i < 7 ? kLime : (i < 9 ? juce::Colour (0xffffe14a) : kPink);
            const auto seg = juce::Rectangle<float> (box.getX() + 10.f + (float) i * 13.f, box.getY() + 8.f, 9.f, 24.f);
            g.setColour (on ? lit : juce::Colour (0xff1b2f63));
            g.fillRoundedRectangle (seg, 3.f);
        }
    }

    void timerCallback() override
    {
        level = juce::jmax (proc.peak.exchange (0.f), level * 0.85f);
        repaint (meterBox().expanded (2.f).toNearestInt());
    }

    BassbotProcessor& proc;
    MetalLook look;
    std::array<Knob, 6> knobs;
    float level = 0.f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassbotEditor)
};

juce::AudioProcessorEditor* BassbotProcessor::createEditor() { return new BassbotEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BassbotProcessor(); }
