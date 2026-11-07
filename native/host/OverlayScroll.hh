#pragma once
class QAbstractScrollArea;
namespace bts {
// A scrollbar that appears the moment a panel outgrows its viewport takes
// eight pixels of width away from the content, so every row in the panel
// reflows: captions re-elide, fields shrink and the whole column looks as if
// it jumped. Folding a section is enough to trigger it. Float the bar over the
// viewport's right edge instead, the way macOS overlay scrollbars do, and the
// content keeps the panel's full width whether it scrolls or not.
void overlay_scrollbar(QAbstractScrollArea *area);
}
