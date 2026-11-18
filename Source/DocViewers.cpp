#include "DocViewers.h"

namespace kp
{
std::unique_ptr<DocViewer> createFontViewer();
std::unique_ptr<DocViewer> createPdfViewer();
std::unique_ptr<DocViewer> createModelViewer();
std::unique_ptr<DocViewer> createCadViewer();
std::unique_ptr<DocViewer> createTextViewer();
std::unique_ptr<DocViewer> createTableViewer();
std::unique_ptr<DocViewer> createSlidesViewer();
std::unique_ptr<DocViewer> createAppViewer();

std::unique_ptr<DocViewer> createViewer (Kind k)
{
    if (k == Kind::Font)  return createFontViewer();
    if (k == Kind::Pdf)   return createPdfViewer();
    if (k == Kind::Model) return createModelViewer();
    if (k == Kind::Cad)   return createCadViewer();
    if (k == Kind::Text)  return createTextViewer();
    if (k == Kind::Table) return createTableViewer();
    if (k == Kind::Slides) return createSlidesViewer();
    if (k == Kind::App)   return createAppViewer();
    return nullptr;
}
}
