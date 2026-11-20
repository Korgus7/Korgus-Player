#include "ImageEditor.h"
#include "ImageLoader.h"

namespace kp
{
ImageEditor::ImageEditor (const File& f) : file (f)
{
    setWantsKeyboardFocus (true);
    {
        auto ld = img::loadFull (f, 2560);
        original = ld.image;
        fullW = jmax (1, ld.origW > 0 ? ld.origW : original.getWidth()); fullH = jmax (1, ld.origH > 0 ? ld.origH : original.getHeight());
    }
    preview = img::downscale (original, 1400);
    adjusted = preview;

    auto setup = [this] (Slider& s, Label& l, const char* name)
    {
        s.setSliderStyle (Slider::LinearHorizontal);
        s.setRange (-100, 100, 1);
        s.setValue (0, dontSendNotification);
        s.setTextBoxStyle (Slider::TextBoxRight, false, 44, 20);
        s.setDoubleClickReturnValue (true, 0);
        s.setColour (Slider::trackColourId, col::gold.withAlpha (0.7f));
        s.setColour (Slider::backgroundColourId, col::line);
        s.onValueChange = [this] { updatePreview(); };
        l.setText (tr (name), dontSendNotification);
        l.setColour (Label::textColourId, col::text);
        addAndMakeVisible (s);
        addAndMakeVisible (l);
    };
    setup (brightness, bLabel, "Jas");
    setup (contrast, cLabel, "Kontrast");
    setup (saturation, sLabel, "Sýtosť");

    aspect.addItemList ({ tr ("Orezanie: voľné"), "1 : 1", "4 : 3", "3 : 2", "16 : 9", "3 : 4", "2 : 3", "9 : 16" }, 1);
    aspect.setSelectedItemIndex (0, dontSendNotification);
    aspect.onChange = [this] { applyAspect(); };
    addAndMakeVisible (aspect);

    saveCopy.setButtonText (tr ("Uložiť ako kópiu"));
    overwrite.setButtonText (tr ("Prepísať originál"));
    cancel.setButtonText (tr ("Zavrieť (Esc)"));
    resetB.setButtonText (tr ("Pôvodný stav"));
    saveCopy.onClick = [this] { save (false); };
    overwrite.onClick = [this] { save (true); };
    cancel.onClick = [this] { if (onClose) onClose(); };
    resetB.onClick = [this]
    {
        crop = { 0, 0, 1, 1 };
        brightness.setValue (0); contrast.setValue (0); saturation.setValue (0);
        aspect.setSelectedItemIndex (0);
        repaint();
    };
    for (auto* b : { &saveCopy, &overwrite, &cancel, &resetB }) addAndMakeVisible (*b);
    // sipky: posun vyrezu a zmena jeho velkosti po 1 pixeli (so Shiftom po 10), drzanim sa opakuje
    auto arrow = [this] (TextButton& b, const String& t, const String& tip, int dx, int dy, int dw, int dh)
    {
        b.setButtonText (t); b.setTooltip (tip); b.setRepeatSpeed (400, 50);
        b.onClick = [this, dx, dy, dw, dh] { const int k = ModifierKeys::currentModifiers.isShiftDown() ? 10 : 1; nudge (dx * k, dy * k, dw * k, dh * k); };
        b.setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    };
    arrow (mvL, String::charToString (0x25C0), tr ("Posunúť výrez doľava"), -1, 0, 0, 0);
    arrow (mvR, String::charToString (0x25B6), tr ("Posunúť výrez doprava"), 1, 0, 0, 0);
    arrow (mvU, String::charToString (0x25B2), tr ("Posunúť výrez hore"), 0, -1, 0, 0);
    arrow (mvD, String::charToString (0x25BC), tr ("Posunúť výrez dole"), 0, 1, 0, 0);
    arrow (wMinus, String::fromUTF8 ("\xe2\x88\x92"), tr ("Užší výrez"), 0, 0, -1, 0);
    arrow (wPlus, "+", tr ("Širší výrez"), 0, 0, 1, 0);
    arrow (hMinus, String::fromUTF8 ("\xe2\x88\x92"), tr ("Nižší výrez"), 0, 0, 0, -1);
    arrow (hPlus, "+", tr ("Vyšší výrez"), 0, 0, 0, 1);
    overwrite.setEnabled (f.hasFileExtension ("jpg;jpeg;jpe;jfif;png"));
}

void ImageEditor::resized()
{
    auto p = getLocalBounds().removeFromRight (panelW).reduced (14);
    p.removeFromTop (34);
    aspect.setBounds (p.removeFromTop (26));
    p.removeFromTop (8);
    {
        // riadok 1: posun (sipky), riadok 2: sirka - + a vyska - +
        p.removeFromTop (18);   // popis s velkostou vyrezu (kreslene v paint)
        auto r1 = p.removeFromTop (26); const int bw = (r1.getWidth() - 12) / 4;
        for (auto* b : { &mvL, &mvU, &mvD, &mvR }) { b->setBounds (r1.removeFromLeft (bw)); r1.removeFromLeft (4); }
        p.removeFromTop (6);
        auto r2 = p.removeFromTop (26);
        for (auto* b : { &wMinus, &wPlus, &hMinus, &hPlus }) { b->setBounds (r2.removeFromLeft (bw)); r2.removeFromLeft (4); }
        p.removeFromTop (16);
    }
    for (auto [s, l] : { std::pair<Slider*, Label*> { &brightness, &bLabel }, { &contrast, &cLabel }, { &saturation, &sLabel } })
    {
        l->setBounds (p.removeFromTop (20));
        s->setBounds (p.removeFromTop (26));
        p.removeFromTop (8);
    }
    p.removeFromTop (8);
    resetB.setBounds (p.removeFromTop (28));
    auto bottom = p.removeFromBottom (28 * 3 + 16);
    saveCopy.setBounds (bottom.removeFromTop (28));
    bottom.removeFromTop (8);
    overwrite.setBounds (bottom.removeFromTop (28));
    bottom.removeFromTop (8);
    cancel.setBounds (bottom.removeFromTop (28));
}

Rectangle<float> ImageEditor::imageArea() const
{
    auto area = getLocalBounds().withTrimmedRight (panelW).reduced (20).toFloat();
    if (! adjusted.isValid()) return area;
    return RectanglePlacement (RectanglePlacement::centred).appliedTo (adjusted.getBounds().toFloat(), area);
}

Rectangle<float> ImageEditor::cropOnScreen() const
{
    const auto a = imageArea();
    return { a.getX() + crop.getX() * a.getWidth(), a.getY() + crop.getY() * a.getHeight(), crop.getWidth() * a.getWidth(), crop.getHeight() * a.getHeight() };
}

Image ImageEditor::process (const Image& src) const
{
    if (! src.isValid()) return src;
    const float b = (float) brightness.getValue() / 100.0f * 0.5f;
    const float c = std::pow (2.0f, (float) contrast.getValue() / 60.0f);
    const float s = 1.0f + (float) saturation.getValue() / 100.0f;
    Image out = src.createCopy();
    if (b == 0.0f && c == 1.0f && s == 1.0f) return out;
    Image::BitmapData bd (out, Image::BitmapData::readWrite);
    uint8 lut[256];
    for (int i = 0; i < 256; ++i)
        lut[i] = (uint8) jlimit (0, 255, roundToInt ((((float) i / 255.0f - 0.5f) * c + 0.5f + b) * 255.0f));
    for (int y = 0; y < bd.height; ++y)
        for (int x = 0; x < bd.width; ++x)
        {
            auto col = bd.getPixelColour (x, y);
            float r = col.getFloatRed(), g = col.getFloatGreen(), bl = col.getFloatBlue();
            const float l = 0.299f * r + 0.587f * g + 0.114f * bl;
            r = jlimit (0.0f, 1.0f, l + (r - l) * s);
            g = jlimit (0.0f, 1.0f, l + (g - l) * s);
            bl = jlimit (0.0f, 1.0f, l + (bl - l) * s);
            bd.setPixelColour (x, y, Colour (lut[(int) (r * 255)], lut[(int) (g * 255)], lut[(int) (bl * 255)], col.getAlpha()));
        }
    return out;
}

void ImageEditor::updatePreview()
{
    adjusted = process (preview);
    repaint();
}

void ImageEditor::applyAspect()
{
    static const float ratios[] = { 0, 1.0f, 4.0f / 3, 3.0f / 2, 16.0f / 9, 3.0f / 4, 2.0f / 3, 9.0f / 16 };
    const float r = ratios[jlimit (0, 7, aspect.getSelectedItemIndex())];
    if (r <= 0 || ! preview.isValid()) { repaint(); return; }
    // najväčší výrez s daným pomerom v strede
    const float iw = (float) preview.getWidth(), ih = (float) preview.getHeight();
    float w = iw, h = iw / r;
    if (h > ih) { h = ih; w = ih * r; }
    crop = { (iw - w) / 2 / iw, (ih - h) / 2 / ih, w / iw, h / ih };
    repaint();
}

void ImageEditor::paint (Graphics& g)
{
    g.fillAll (Colour (0xff0e1116));
    g.setColour (col::panel);
    g.fillRect (getLocalBounds().removeFromRight (panelW));
    g.setColour (col::text);
    g.setFont (FontOptions (15.0f));
    g.drawText (tr ("Úpravy: ") + file.getFileName(), getLocalBounds().removeFromRight (panelW).reduced (14, 8).removeFromTop (24),
                Justification::centredLeft, true);
    if (! adjusted.isValid())
    {
        g.setColour (col::textDim);
        g.drawText (tr ("Obrázok sa nedá načítať"), getLocalBounds().withTrimmedRight (panelW), Justification::centred);
        return;
    }
    const auto a = imageArea();
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.drawImage (adjusted, a);
    // stmavenie mimo výrezu
    const auto c = cropOnScreen();
    g.setColour (Colours::black.withAlpha (0.55f));
    RectangleList<float> outside (a);
    outside.subtract (c);
    for (auto& r : outside) g.fillRect (r);
    g.setColour (Colours::white);
    g.drawRect (c, 1.5f);
    // tretiny
    g.setColour (Colours::white.withAlpha (0.35f));
    for (int i = 1; i < 3; ++i)
    {
        g.drawVerticalLine ((int) (c.getX() + c.getWidth() * i / 3), c.getY(), c.getBottom());
        g.drawHorizontalLine ((int) (c.getY() + c.getHeight() * i / 3), c.getX(), c.getRight());
    }
    {
        // velkost vyrezu v pixeloch originalu + popisy pod sipkami
        const int cw = roundToInt (crop.getWidth() * (float) fullW), ch = roundToInt (crop.getHeight() * (float) fullH);
        g.setColour (col::textDim); g.setFont (FontOptions (12.5f));
        g.drawText (tr ("Výrez: ") + String (cw) + " × " + String (ch) + " px", mvL.getBounds().getUnion (mvR.getBounds()).translated (0, -20).withHeight (18), Justification::centredLeft);
        g.drawText (tr ("Šírka"), wMinus.getBounds().getUnion (wPlus.getBounds()).translated (0, 26).withHeight (14), Justification::centred);
        g.drawText (tr ("Výška"), hMinus.getBounds().getUnion (hPlus.getBounds()).translated (0, 26).withHeight (14), Justification::centred);
    }
    g.setColour (col::textDim);
    g.setFont (FontOptions (12.0f));
    g.drawText (tr ("Ťahaním vyznač výrez · šípky na klávesnici = posun, Ctrl + šípky = veľkosť, so Shiftom po 10 px"),
                getLocalBounds().withTrimmedRight (panelW).removeFromBottom (20), Justification::centred);
}

void ImageEditor::mouseDown (const MouseEvent& e)
{
    dragStart = e.position;
    dragStartCrop = crop;
    moving = cropOnScreen().contains (e.position) && crop != Rectangle<float> (0, 0, 1, 1);
}

void ImageEditor::mouseDrag (const MouseEvent& e)
{
    const auto a = imageArea();
    if (a.isEmpty()) return;
    if (moving)
    {
        auto c = dragStartCrop.translated ((e.position.x - dragStart.x) / a.getWidth(), (e.position.y - dragStart.y) / a.getHeight());
        c.setX (jlimit (0.0f, 1.0f - c.getWidth(), c.getX()));
        c.setY (jlimit (0.0f, 1.0f - c.getHeight(), c.getY()));
        crop = c;
    }
    else
    {
        auto p0 = (dragStart - a.getPosition()) / Point<float> (a.getWidth(), a.getHeight());
        auto p1 = (e.position - a.getPosition()) / Point<float> (a.getWidth(), a.getHeight());
        p0 = { jlimit (0.0f, 1.0f, p0.x), jlimit (0.0f, 1.0f, p0.y) };
        p1 = { jlimit (0.0f, 1.0f, p1.x), jlimit (0.0f, 1.0f, p1.y) };
        static const float ratios[] = { 0, 1.0f, 4.0f / 3, 3.0f / 2, 16.0f / 9, 3.0f / 4, 2.0f / 3, 9.0f / 16 };
        const float r = ratios[jlimit (0, 7, aspect.getSelectedItemIndex())];
        if (r > 0)
        {
            // dodržať pomer strán v pixeloch
            const float pxW = std::abs (p1.x - p0.x) * a.getWidth();
            const float pxH = pxW / r;
            p1.y = p0.y + (p1.y >= p0.y ? 1.0f : -1.0f) * pxH / a.getHeight();
        }
        auto c = Rectangle<float> (p0, p1).getIntersection ({ 0, 0, 1, 1 });
        if (c.getWidth() > 0.01f && c.getHeight() > 0.01f) crop = c;
    }
    repaint();
}

bool ImageEditor::keyPressed (const KeyPress& key)
{
    if (key == KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    const int kc = key.getKeyCode(), k = key.getModifiers().isShiftDown() ? 10 : 1;
    const int dx = kc == KeyPress::leftKey ? -k : (kc == KeyPress::rightKey ? k : 0), dy = kc == KeyPress::upKey ? -k : (kc == KeyPress::downKey ? k : 0);
    if (dx == 0 && dy == 0) return false;
    if (key.getModifiers().isCtrlDown()) nudge (0, 0, dx, dy); else nudge (dx, dy, 0, 0);
    return true;
}

float ImageEditor::aspectRatio() const
{
    static const float ratios[] = { 0, 1.0f, 4.0f / 3, 3.0f / 2, 16.0f / 9, 3.0f / 4, 2.0f / 3, 9.0f / 16 };
    return ratios[jlimit (0, 7, aspect.getSelectedItemIndex())];
}

void ImageEditor::nudge (int dx, int dy, int dw, int dh)
{
    const float W = (float) fullW, H = (float) fullH;
    float x = crop.getX() * W, y = crop.getY() * H, w = crop.getWidth() * W, h = crop.getHeight() * H;
    const float r = aspectRatio();
    if (r > 0 && dw != 0) dh = roundToInt ((w + (float) dw) / r - h);   // pri pevnom pomere stran sa meni aj druha strana
    else if (r > 0 && dh != 0) dw = roundToInt ((h + (float) dh) * r - w);
    w = jlimit (8.0f, W, std::round (w) + (float) dw); h = jlimit (8.0f, H, std::round (h) + (float) dh);
    x = jlimit (0.0f, W - w, std::round (x) + (float) dx); y = jlimit (0.0f, H - h, std::round (y) + (float) dy);
    crop = { x / W, y / H, w / W, h / H };
    repaint();
}

void ImageEditor::save (bool overwriteOriginal)
{
    // plné rozlíšenie
    auto full = img::loadFull (file, 0).image;
    if (! full.isValid()) { if (onMessage) onMessage (tr ("Obrázok sa nedá načítať."), true); return; }
    const auto r = Rectangle<float> (crop.getX() * full.getWidth(), crop.getY() * full.getHeight(),
                                     crop.getWidth() * full.getWidth(), crop.getHeight() * full.getHeight()).toNearestInt()
                       .getIntersection (full.getBounds());
    Image out = process (full.getClippedImage (r).createCopy());

    const bool png = file.hasFileExtension ("png");
    File dst = file;
    if (! overwriteOriginal)
    {
        const auto ext = png ? String (".png") : String (".jpg");
        dst = file.getParentDirectory().getNonexistentChildFile (file.getFileNameWithoutExtension() + "_upravene", ext, false);
    }
    MemoryOutputStream mos;
    bool ok;
    if (png) { PNGImageFormat fmt; ok = fmt.writeImageToStream (out, mos); }
    else
    {
        JPEGImageFormat fmt;
        fmt.setQuality (0.93f);
        Image rgb (Image::RGB, out.getWidth(), out.getHeight(), true, SoftwareImageType());
        { Graphics g (rgb); g.fillAll (Colours::white); g.drawImageAt (out, 0, 0); }
        ok = fmt.writeImageToStream (rgb, mos);
    }
    if (ok) ok = dst.replaceWithData (mos.getData(), mos.getDataSize());
    if (! ok) { if (onMessage) onMessage (tr ("Uloženie sa nepodarilo."), true); return; }
    if (onSaved) onSaved (dst, overwriteOriginal);
}
}
