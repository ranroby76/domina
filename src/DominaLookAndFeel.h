
// ============================================================================
//  C:\workspace\Domina\src\DominaLookAndFeel.h
//  Domina - single source of truth for every colour and font (Fanan)
//
//  ARCHETYPE: Modern Flat. Near-black, info-dense, no textures.
//  PERSONALITY COLOUR: light sea blue #45B4EE. Everything else is greyscale, except the
//  locator amber, which is a functional marker (DAW convention), not decoration.
// ============================================================================

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class DominaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // ---- design tokens ------------------------------------------------------
    const juce::Colour windowBg     { 0xFF0E0F11 };
    const juce::Colour panelBg      { 0xFF17181C };
    const juce::Colour insetBg      { 0xFF08090B };
    const juce::Colour gridLine     { 0xFF25272C };

    // The same grey the keyboard's naturals use, so the header chip and the mute
    // buttons read as part of the same instrument rather than holes cut in it.
    const juce::Colour keyGrey      { 0xFF33333B };
    const juce::Colour gridBar      { 0xFF3A3D45 };

    // THE one personality colour. Not const: the LED strip beside the seed knobs
    // swaps it at runtime, and every component reads it live at paint time, so
    // changing it here plus a repaint is the whole mechanism.
    juce::Colour accent    { 0xFF45B4EE };
    juce::Colour accentDim { 0xFF1F5E80 };

    // Six, all picked to carry against the dark panel. Index 0 is the default.
    static constexpr int kAccentCount = 6;

    static juce::Colour accentChoice (int i) noexcept
    {
        static const juce::Colour c[kAccentCount] =
        {
            juce::Colour (0xFF45B4EE),   // blue
            juce::Colour (0xFFE8C547),   // yellow
            juce::Colour (0xFF56C271),   // green
            juce::Colour (0xFFE05B5B),   // red
            juce::Colour (0xFFEE8B3A),   // orange
            juce::Colour (0xFFA77BE8)    // purple
        };
        return c[juce::jlimit (0, kAccentCount - 1, i)];
    }

    // The dim variant is derived rather than hand-picked, so a new colour in the
    // table above needs no second entry that could fall out of step with it.
    void setAccent (int index)
    {
        accent    = accentChoice (index);
        accentDim = accent.withMultipliedSaturation (0.9f).withMultipliedBrightness (0.42f);
        applyAccentColours();
    }
    const juce::Colour locator      { 0xFFE0A32F };   // functional marker

    const juce::Colour textPrimary  { 0xFFEDEDF0 };
    const juce::Colour textSecond   { 0xFF9DA0A8 };
    const juce::Colour textMuted    { 0xFF5E626B };

    // Slider value boxes are Labels, and JUCE fills them square. Anything with a
    // real background colour gets a pill instead; the knob NAME labels are
    // transparent, so they fall through untouched.
    void drawLabel (juce::Graphics& g, juce::Label& l) override
    {
        const auto bg = l.findColour (juce::Label::backgroundColourId);
        auto r = l.getLocalBounds().toFloat();

        if (! bg.isTransparent())
        {
            g.setColour (bg);
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        }

        if (! l.isBeingEdited())
        {
            g.setColour (l.findColour (juce::Label::textColourId)
                          .withMultipliedAlpha (l.isEnabled() ? 1.0f : 0.5f));
            g.setFont (getLabelFont (l));
            g.drawFittedText (l.getText(),
                              l.getLocalBounds().reduced ((int) (r.getHeight() * 0.35f), 0),
                              l.getJustificationType(), 1, 1.0f);
        }
    }

    DominaLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, windowBg);
        setColour (juce::Slider::textBoxTextColourId,         textPrimary);
        setColour (juce::Slider::textBoxBackgroundColourId,   insetBg);
        setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
        applyAccentColours();
        setColour (juce::Slider::rotarySliderOutlineColourId, gridLine);
        setColour (juce::Label::textColourId,                 textSecond);
        setColour (juce::ComboBox::backgroundColourId,        insetBg);
        setColour (juce::ComboBox::textColourId,              textPrimary);
        setColour (juce::ComboBox::outlineColourId,           gridLine);
        setColour (juce::ComboBox::arrowColourId,             textSecond);
        setColour (juce::PopupMenu::backgroundColourId,       panelBg);
        setColour (juce::PopupMenu::textColourId,             textPrimary);
        setColour (juce::ToggleButton::textColourId,          textSecond);
        setColour (juce::ToggleButton::tickDisabledColourId,  gridLine);
        setColour (juce::TextButton::buttonColourId,          panelBg);
        setColour (juce::TextButton::textColourOffId,         textPrimary);
    }

    // Tabular figures for every numeric readout.
    juce::Font numericFont (float h) const
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h,
                                              juce::Font::plain));
    }

    // Everything that has to be pushed into JUCE's colour IDs, in one place, so
    // the constructor and a later colour change cannot disagree.
    void applyAccentColours()
    {
        setColour (juce::Slider::rotarySliderFillColourId,         accent);
        setColour (juce::Slider::thumbColourId,                    accent);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accentDim);
        setColour (juce::ToggleButton::tickColourId,               accent);
    }

    juce::Font labelFont (float h) const
    {
        return juce::Font (juce::FontOptions (h));
    }

    juce::Font getLabelFont (juce::Label& l) override
    {
        return labelFont (juce::jmin (13.0f, (float) l.getHeight() * 0.8f));
    }

    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        return labelFont (juce::jmin (14.0f, (float) box.getHeight() * 0.6f));
    }

    // ---- rotary -------------------------------------------------------------
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float pos, float startAngle, float endAngle,
                           juce::Slider& s) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const float thick = juce::jmax (3.0f, radius * 0.20f);
        const float angle = startAngle + pos * (endAngle - startAngle);

        juce::Path track;
        track.addCentredArc (cx, cy, radius - thick * 0.5f, radius - thick * 0.5f,
                             0.0f, startAngle, endAngle, true);
        g.setColour (gridLine);
        g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        if (pos > 0.001f)
        {
            juce::Path fill;
            fill.addCentredArc (cx, cy, radius - thick * 0.5f, radius - thick * 0.5f,
                                0.0f, startAngle, angle, true);
            g.setColour (s.isEnabled() ? accent : accentDim);
            g.strokePath (fill, juce::PathStrokeType (thick, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
        }

        // pointer
        juce::Path ptr;
        const float ptrLen = radius * 0.45f;
        ptr.addRoundedRectangle (-1.5f, -radius + thick + 1.0f, 3.0f, ptrLen, 1.5f);
        ptr.applyTransform (juce::AffineTransform::rotation (angle).translated (cx, cy));
        g.setColour (textPrimary);
        g.fillPath (ptr);
    }

    // ---- linear -------------------------------------------------------------
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float, float,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                                    0.0f, 0.0f, style, s);
            return;
        }

        const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height)
                           .reduced (0.0f, height * 0.28f);
        g.setColour (insetBg);
        g.fillRoundedRectangle (r, 2.0f);

        const float w = juce::jlimit (0.0f, r.getWidth(), sliderPos - r.getX());
        if (w > 0.5f)
        {
            g.setColour (s.isEnabled() ? accent : accentDim);
            g.fillRoundedRectangle (r.withWidth (w), 2.0f);
        }

        g.setColour (gridLine);
        g.drawRoundedRectangle (r, 2.0f, 1.0f);
    }

    // ---- toggle -------------------------------------------------------------
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                           bool shouldDrawHighlighted, bool) override
    {
        auto r = b.getLocalBounds().toFloat();
        const float boxW = 34.0f;
        auto box = r.removeFromLeft (boxW).reduced (1.0f, r.getHeight() * 0.22f);

        g.setColour (b.getToggleState() ? accentDim : insetBg);
        g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
        g.setColour (shouldDrawHighlighted ? textSecond : gridLine);
        g.drawRoundedRectangle (box, box.getHeight() * 0.5f, 1.0f);

        const float d = box.getHeight() - 4.0f;
        const float kx = b.getToggleState() ? box.getRight() - d - 2.0f : box.getX() + 2.0f;
        g.setColour (b.getToggleState() ? accent : textMuted);
        g.fillEllipse (kx, box.getY() + 2.0f, d, d);

        g.setColour (textSecond);
        g.setFont (labelFont (12.0f));
        g.drawText (b.getButtonText(), r.reduced (6.0f, 0.0f),
                    juce::Justification::centredLeft, false);
    }
};



