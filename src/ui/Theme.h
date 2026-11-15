// ---------------------------------------------------------------------------
// Theme.h - light / dark application themes (following the system by default).
// ---------------------------------------------------------------------------
#pragma once

#include <QColor>
#include <QString>

#include "core/Settings.h"

namespace tdms {
namespace theme {

struct Colors {
    QColor window;
    QColor windowText;
    QColor base;
    QColor altBase;
    QColor border;
    QColor borderStrong;
    QColor panel;
    QColor menubar;
    QColor menuBg;
    QColor hover;
    QColor pressed;
    QColor headerBg;
    QColor headerText;
    QColor selection;
    QColor selectionText;
    QColor muted;
    QColor buttonBg;
    QColor buttonText;
    QColor buttonBorder;
    QColor buttonHover;
    QColor buttonDisabled;
    QColor inputBg;
    QColor checkBorder;
    QColor scrollBg;
    QColor scrollHandle;
    QColor scrollHandleHover;
    QColor accent;
    QColor tooltipBg;
    QColor tooltipText;
    QColor canvas;
    QColor grid;
};

// True when the requested mode resolves to a dark palette.
bool isDark(ThemeMode mode);

// Effective darkness of the palette currently applied.
bool isDark();

// Applies palette + stylesheet. No-op when the resolved theme did not change.
void apply(ThemeMode mode);

const Colors& colors();

// Convenience accessors used by widgets that paint themselves (QWT canvas).
QColor canvasColor();
QColor gridColor();
QColor iconColor();
QColor mutedText();

// Renders the shared widget stylesheet for the current theme (dialogs using
// local stylesheets can reuse it instead of duplicating colours).
QString styleSheet();

} // namespace theme
} // namespace tdms
