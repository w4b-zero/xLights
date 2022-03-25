#include "SketchEffect.h"

#include "BulkEditControls.h"
#include "RenderBuffer.h"
#include "SketchEffectDrawing.h"
#include "SketchPanel.h"

#include "../../include/sketch-64.xpm"
#include "../../include/sketch-48.xpm"
#include "../../include/sketch-32.xpm"
#include "../../include/sketch-24.xpm"
#include "../../include/sketch-16.xpm"

#include <wx/image.h>

#include <algorithm>
#include <cstdint>

namespace
{
    struct LinearInterpolater {
        double operator()(double t) const
        {
            return t;
        }
    };

    template<class T>
    double interpolate(double x, double loIn, double loOut, double hiIn, double hiOut, const T& interpolater)
    {
        return (loIn != hiIn)
                   ? (loOut + (hiOut - loOut) * interpolater((x - loIn) / (hiIn - loIn)))
                   : ((loOut + hiOut) / 2);
    }
}

SketchEffect::SketchEffect(int id) :
    RenderableEffect(id, "Sketch", sketch_16_xpm, sketch_24_xpm, sketch_32_xpm, sketch_48_xpm, sketch_64_xpm)
{
}

SketchEffect::~SketchEffect()
{

}

void SketchEffect::Render(Effect* /*effect*/, SettingsMap& settings, RenderBuffer& buffer )
{
    std::string sketchDef = settings.Get("TEXTCTRL_SketchDef", "");
    bool motionEnabled = std::stoi(settings.Get("CHECKBOX_MotionEnabled", "0"));
    int thickness = std::stoi(settings.Get("SLIDER_Thickness", "1"));
    int motionPercentage = std::stoi(settings.Get("SLIDER_MotionPercentage", "100"));

    if (sketchDef.empty())
        return;
    m_sketch = SketchEffectSketch::SketchFromString(sketchDef);

    double progress = buffer.GetEffectTimeIntervalPosition(1.f);

    //
    // RenderBuffer --> wxImage
    //
    int bw = buffer.BufferWi;
    int bh = buffer.BufferHt;
    std::vector<uint8_t> rgb(bw * 3 * bh);
    std::vector<uint8_t> alpha(bw * bh);
    xlColor* px = buffer.GetPixels();
    int pxIndex = 0;
    int rgbIndex = 0;
    int alphaIndex = 0;
    for ( int y = 0; y < bh; ++y )
    {
        for ( int x = 0; x < bw; ++x, ++pxIndex )
        {
            rgb[rgbIndex++] = px[pxIndex].Red();
            rgb[rgbIndex++] = px[pxIndex].Green();
            rgb[rgbIndex++] = px[pxIndex].Green();
            alpha[alphaIndex++] = px[pxIndex].Alpha();
        }
    }
    wxImage img(bw, bh, rgb.data(), alpha.data(), true);

    //
    // rendering sketch via wxGraphicsContext
    //
    renderSketch(img, progress, thickness, motionEnabled, 0.01*motionPercentage);

    //
    // wxImage --> RenderBuffer
    //
    for ( int y = 0; y < bh; ++y )
    {
        for (int x = 0; x < bw; ++x, ++px)
        {
            px->red = img.GetRed(x, y);
            px->green = img.GetGreen(x, y);
            px->blue = img.GetBlue(x, y);
            px->alpha = img.GetAlpha(x, y);
        }
    }
}

void SketchEffect::SetDefaultParameters()
{
    SketchPanel* p = (SketchPanel*)panel;

    SetTextValue(p->TextCtrl_SketchDef, SketchEffectSketch::DefaultSketchString());

    p->BitmapButton_Thickness->SetActive(false);
    p->BitmapButton_MotionPercentage->SetActive(false);

    SetCheckBoxValue(p->CheckBox_MotionEnabled, false);

    SetSliderValue(p->Slider_Thickness, 1);
    SetSliderValue(p->Slider_MotionPercentage, 100);
}

bool SketchEffect::needToAdjustSettings( const std::string& /*version*/ )
{
    return false;
}

void SketchEffect::adjustSettings( const std::string& version, Effect* effect, bool removeDefaults/*=true*/ )
{
    // give the base class a chance to adjust any settings
    if ( RenderableEffect::needToAdjustSettings( version ) )
    {
        RenderableEffect::adjustSettings( version, effect, removeDefaults );
    }
}

std::list<std::string> SketchEffect::CheckEffectSettings(const SettingsMap& /*settings*/, AudioManager* /*media*/, Model* /*model*/, Effect* /*eff*/, bool /*renderCache*/ )
{
    return std::list<std::string>();
}

void SketchEffect::RemoveDefaults( const std::string& version, Effect* effect )
{

}

xlEffectPanel* SketchEffect::CreatePanel( wxWindow* parent )
{
    return new SketchPanel( parent );
}

void SketchEffect::renderSketch(wxImage& img, double progress, int lineThickness, bool hasMotion, double motionPercentage)
{
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(img));
    auto paths = m_sketch.paths();
    wxSize sz(img.GetSize());

    // In order for the animation to both "draw in" and "draw out" we adjust the [0,1] range
    double maxProgress = hasMotion ? (1. + motionPercentage) : 1.;
    double adjustedProgress = interpolate(progress, 0., 0., 1., maxProgress, LinearInterpolater());

    double totalLength = 0.;
    for (const auto& path : paths)
        totalLength += path->Length();
    
    double cumulativeLength = 0.;
    int i = 0;
    const wxColor colors[] = { *wxRED, *wxGREEN, *wxBLUE }; // todo - shouldn't be hard-coded colors!!
    for (auto iter = paths.cbegin(); iter != paths.cend(); ++iter, ++i)
    {
        wxPen pen(colors[i % 3], lineThickness);
        gc->SetPen(pen);
        double pathLength = (*iter)->Length();
        double percentageAtEndOfThisPath = (cumulativeLength + pathLength) / totalLength;
    
        if (!hasMotion && percentageAtEndOfThisPath <= adjustedProgress)
            (*iter)->drawEntirePath(gc.get(), sz);
        else {
            double percentageAtStartOfThisPath = cumulativeLength / totalLength;
            double percentageThroughThisPath = (adjustedProgress - percentageAtStartOfThisPath) / (percentageAtEndOfThisPath - percentageAtStartOfThisPath);
            if (!hasMotion)
                (*iter)->drawPartialPath(gc.get(), sz, std::nullopt, percentageThroughThisPath);
            else {
                double drawPercentageThroughThisPath = (adjustedProgress - motionPercentage - percentageAtStartOfThisPath) / (percentageAtEndOfThisPath - percentageAtStartOfThisPath);
                drawPercentageThroughThisPath = std::clamp(drawPercentageThroughThisPath, 0., 1.);

                (*iter)->drawPartialPath(gc.get(), sz, drawPercentageThroughThisPath, percentageThroughThisPath);
            }
        }
        cumulativeLength += pathLength;
    }
}
