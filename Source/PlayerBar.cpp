#include "PlayerBar.h"

namespace kp
{
static String tt (double s)
{
    const int t = (int) s;
    if (t >= 3600) return String (t / 3600) + ":" + String ((t / 60) % 60).paddedLeft ('0', 2) + ":" + String (t % 60).paddedLeft ('0', 2);
    return String (t / 60) + ":" + String (t % 60).paddedLeft ('0', 2);
}

PlayerBar::PlayerBar (AudioPlayer& p) : player (p)
{
    for (auto* b : { &prevB, &playB, &stopB, &nextB, &repeatB, &shuffleB })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    prevB.onClick = [this] { player.previous(); };
    playB.onClick = [this] { player.togglePause(); };
    stopB.onClick = [this] { player.stop(); };
    nextB.onClick = [this] { player.next(); };
    repeatB.onClick = [this]
    {
        player.repeat = player.repeat == AudioPlayer::Repeat::Off ? AudioPlayer::Repeat::All
                      : player.repeat == AudioPlayer::Repeat::All ? AudioPlayer::Repeat::One : AudioPlayer::Repeat::Off;
        update();
        if (onSettingsChanged) onSettingsChanged();
    };
    shuffleB.onClick = [this]
    {
        player.shuffle = ! player.shuffle;
        update();
        if (onSettingsChanged) onSettingsChanged();
    };

    volume.setSliderStyle (Slider::LinearHorizontal);
    volume.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    volume.setRange (0.0, 1.0, 0.01);
    volume.setValue (player.getVolume(), dontSendNotification);
    volume.setWantsKeyboardFocus (false);
    volume.setTooltip (String::fromUTF8 ("Hlasitosť"));
    volume.setColour (Slider::trackColourId, col::gold.withAlpha (0.8f));
    volume.setColour (Slider::backgroundColourId, col::line);
    volume.setColour (Slider::thumbColourId, col::text);
    volume.onValueChange = [this] { player.setVolume ((float) volume.getValue()); if (onSettingsChanged) onSettingsChanged(); };
    addAndMakeVisible (volume);
    update();
}

void PlayerBar::update()
{
    playB.setIcon (player.isPlaying() ? Icon::Pause : Icon::Play);
    repeatB.setIcon (player.repeat == AudioPlayer::Repeat::One ? Icon::RepeatOne : Icon::Repeat);
    repeatB.setActive (player.repeat != AudioPlayer::Repeat::Off);
    shuffleB.setActive (player.shuffle);
    volume.setValue (player.getVolume(), dontSendNotification);
    const bool has = player.getCount() > 0;
    for (auto* b : { &prevB, &stopB, &nextB }) b->setEnabled (has);
    if (player.isPlaying()) startTimerHz (10); else stopTimer();
    repaint();
}

void PlayerBar::resized()
{
    auto r = getLocalBounds().reduced (6, 5);
    for (auto* b : { &prevB, &playB, &stopB, &nextB }) b->setBounds (r.removeFromLeft (r.getHeight() + 4).withTrimmedRight (4));
    r.removeFromLeft (6);
    volume.setBounds (r.removeFromRight (100).reduced (0, 4));
    r.removeFromRight (6);
    shuffleB.setBounds (r.removeFromRight (r.getHeight() + 4).withTrimmedLeft (4));
    repeatB.setBounds (r.removeFromRight (r.getHeight() + 4).withTrimmedLeft (4));
    r.removeFromRight (8);
    timeArea = r.removeFromRight (96);
    titleArea = r.removeFromLeft (jmin (320, r.getWidth() / 2));
    r.removeFromLeft (10);
    progressArea = r.withSizeKeepingCentre (r.getWidth(), 6);
}

void PlayerBar::paint (Graphics& g)
{
    g.fillAll (col::panel);
    g.setColour (col::line);
    g.fillRect (0, 0, getWidth(), 1);

    const auto f = player.getCurrent();
    g.setFont (FontOptions (13.0f));
    if (f == File())
    {
        g.setColour (col::textDim);
        g.drawText (String::fromUTF8 ("Vyber zvukový súbor – hudba hrá ďalej, aj keď prezeráš iné súbory"), titleArea.withRight (progressArea.getRight()),
                    Justification::centredLeft, true);
        return;
    }
    g.setColour (col::text);
    String title = f.getFileNameWithoutExtension();
    if (player.getCount() > 1) title = String (player.getIndex() + 1) + "/" + String (player.getCount()) + "  " + title;
    g.drawText (title, titleArea, Justification::centredLeft, true);

    const double len = player.getLength();
    const float frac = len > 0 ? (float) jlimit (0.0, 1.0, player.getPosition() / len) : 0.0f;
    auto pr = progressArea.toFloat();
    g.setColour (col::line);
    g.fillRoundedRectangle (pr, 3.0f);
    g.setColour (col::gold);
    g.fillRoundedRectangle (pr.withWidth (pr.getWidth() * frac), 3.0f);
    g.setColour (col::text);
    g.fillEllipse (Rectangle<float> (10, 10).withCentre ({ pr.getX() + pr.getWidth() * frac, pr.getCentreY() }));

    g.setColour (col::textDim);
    g.setFont (FontOptions (12.5f));
    g.drawText (tt (player.getPosition()) + " / " + tt (len), timeArea, Justification::centredRight, false);
}

void PlayerBar::seekFrom (int x)
{
    if (player.getLength() <= 0) return;
    player.seek (jlimit (0.0, 1.0, (double) (x - progressArea.getX()) / jmax (1, progressArea.getWidth())) * player.getLength());
    repaint();
}

void PlayerBar::mouseDown (const MouseEvent& e)
{
    if (progressArea.expanded (0, 8).contains (e.getPosition())) seekFrom (e.x);
    else if (titleArea.contains (e.getPosition()) && onShowTrack && player.getCurrent() != File()) onShowTrack (player.getCurrent());
}

void PlayerBar::mouseDrag (const MouseEvent& e)
{
    if (progressArea.expanded (0, 8).contains (e.getMouseDownPosition())) seekFrom (e.x);
}
}
