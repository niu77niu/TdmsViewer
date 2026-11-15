// ---------------------------------------------------------------------------
// Icons.h - theme aware, vector drawn action icons.
// ---------------------------------------------------------------------------
#pragma once

#include <QColor>
#include <QIcon>
#include <QString>
#include <QStringList>

namespace tdms {
namespace icons {

// Returns a cached icon rendered from an embedded SVG at the requested colour.
QIcon get(const QString& name, const QColor& color, int size = 24);

// Uses the current theme's foreground colour.
QIcon get(const QString& name);

QStringList names();

} // namespace icons
} // namespace tdms
