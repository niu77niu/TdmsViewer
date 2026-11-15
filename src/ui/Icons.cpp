// ---------------------------------------------------------------------------
// Icons.cpp - a tiny hand written icon set (24x24 grid, stroke based).
// ---------------------------------------------------------------------------
#include "Icons.h"

#include "Theme.h"

#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QVector>

namespace tdms {
namespace icons {

namespace {

struct IconDef {
    const char* name;
    const char* body;
};

// All paths are drawn in a 24x24 view box with a 1.7px round stroke.
const IconDef kDefs[] = {
    { "new",
      "<path d='M13 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V9z'/>"
      "<path d='M13 3v6h6'/><path d='M12 12v6M9 15h6'/>" },

    { "open",
      "<path d='M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z'/>"
      "<path d='M3 10h18'/>" },

    { "save",
      "<path d='M5 3h10l4 4v13a1 1 0 0 1-1 1H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z'/>"
      "<path d='M8 3v5h7V3'/><path d='M7 21v-6h10v6'/>" },

    { "save-as",
      "<path d='M5 3h9l4 4v6'/><path d='M5 3a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h6'/>"
      "<path d='M8 3v5h6V3'/><path d='M20.5 14.5l-4 4-2.5.5.5-2.5 4-4z'/>" },

    { "close",
      "<circle cx='12' cy='12' r='8.5'/><path d='M9.2 9.2l5.6 5.6M14.8 9.2l-5.6 5.6'/>" },

    { "import",
      "<path d='M12 3v10'/><path d='M8 9.5l4 4 4-4'/>"
      "<path d='M4 15v3a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-3'/>" },

    { "export",
      "<path d='M12 14V4'/><path d='M8 7.5l4-4 4 4'/>"
      "<path d='M4 15v3a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-3'/>" },

    { "merge",
      "<path d='M3 4.5h3.5l4 6.2'/><path d='M3 19.5h3.5l4-6.2'/>"
      "<path d='M10.5 12H19'/><path d='M16 8.6l3.5 3.4-3.5 3.4'/>" },

    { "add-group",
      "<path d='M3 7a2 2 0 0 1 2-2h4l2 2h6a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z'/>"
      "<path d='M12 11v6M9 14h6'/>" },

    { "rename",
      "<path d='M4 20h4L18.5 9.5a2.1 2.1 0 0 0-3-3L5 17v3z'/><path d='M14 6l4 4'/>" },

    { "delete",
      "<path d='M4 7h16'/><path d='M9.5 7V5.2a1 1 0 0 1 1-1h3a1 1 0 0 1 1 1V7'/>"
      "<path d='M6.5 7l.9 12a1.5 1.5 0 0 0 1.5 1.4h6.2A1.5 1.5 0 0 0 16.6 19l.9-12'/>"
      "<path d='M10 11v6M14 11v6'/>" },

    { "refresh",
      "<path d='M4.5 12a7.5 7.5 0 0 1 12.6-5.5L20 9'/><path d='M20 4.5V9h-4.5'/>"
      "<path d='M19.5 12a7.5 7.5 0 0 1-12.6 5.5L4 15'/><path d='M4 19.5V15h4.5'/>" },

    { "settings",
      "<circle cx='12' cy='12' r='3.1'/>"
      "<path d='M19.6 14.4a1.7 1.7 0 0 0 .3 1.9l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.9-.3"
      " 1.7 1.7 0 0 0-1 1.5v.2a2 2 0 1 1-4 0v-.2a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.9.3l-.1.1"
      "a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.9 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.2"
      "a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.9l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.9.3H9"
      "a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.2a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.9-.3l.1-.1"
      "a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.9V9a1.7 1.7 0 0 0 1.5 1h.2a2 2 0 1 1 0 4h-.2"
      "a1.7 1.7 0 0 0-1.5 1z'/>" },

    { "fit",
      "<path d='M9 4H4v5M15 4h5v5M9 20H4v-5M15 20h5v-5'/>"
      "<path d='M4 4l4.5 4.5M20 4l-4.5 4.5M4 20l4.5-4.5M20 20l-4.5-4.5'/>" },

    { "grid",
      "<rect x='3.5' y='3.5' width='17' height='17' rx='2'/>"
      "<path d='M3.5 9.5h17M3.5 15h17M9.5 3.5v17M15 3.5v17'/>" },

    { "legend",
      "<rect x='3' y='5' width='18' height='14' rx='2'/>"
      "<path d='M7 10h4M13 10h4M7 14h4M13 14h4'/>" },

    { "image",
      "<rect x='3' y='4' width='18' height='16' rx='2'/>"
      "<circle cx='8.5' cy='9' r='1.4'/><path d='M21 16.5l-5-5-6.5 6.5'/>" },

    { "info",
      "<circle cx='12' cy='12' r='8.5'/><path d='M12 11v6'/><path d='M12 7.6v.8'/>" },

    { "quit",
      "<path d='M12 3.5v8'/><path d='M7.2 6.6a7.5 7.5 0 1 0 9.6 0'/>" },

    { "sun",
      "<circle cx='12' cy='12' r='4'/>"
      "<path d='M12 2.5v2M12 19.5v2M2.5 12h2M19.5 12h2"
      "M5.3 5.3l1.4 1.4M17.3 17.3l1.4 1.4M18.7 5.3l-1.4 1.4M6.7 17.3l-1.4 1.4'/>" },

    { "moon",
      "<path d='M20 14.2A8.4 8.4 0 1 1 9.8 4a6.9 6.9 0 0 0 10.2 10.2z'/>" },

    { "monitor",
      "<rect x='3' y='4' width='18' height='12' rx='2'/><path d='M9 20h6M12 16v4'/>" },

    { "chart",
      "<path d='M4 20V4'/><path d='M4 20h16'/>"
      "<path d='M7.5 16l3.5-5 3 3 4.5-7'/>" },

    { "table",
      "<rect x='3' y='4' width='18' height='16' rx='2'/>"
      "<path d='M3 9.5h18M9.5 9.5V20M15 9.5V20'/>" },

    { "clock",
      "<circle cx='12' cy='12' r='8.5'/><path d='M12 7v5.3l3.4 2'/>" },

    { "reset",
      "<path d='M4 12a8 8 0 1 0 2.4-5.7'/><path d='M4 4.5V10h5.5'/>" },

    { "axes",
      // Frame plus tick marks on all four sides: "the four plot axes".
      "<rect x='6.5' y='6.5' width='11' height='11' rx='1'/>"
      "<path d='M6.5 3v3.5M17.5 3v3.5M6.5 17.5V21M17.5 17.5V21'/>"
      "<path d='M3 6.5h3.5M3 17.5h3.5M17.5 6.5H21M17.5 17.5H21'/>"
      "<path d='M8 20v-4M12 20v-7M16 20v-5'/><path d='M4 8h4M4 12h7M4 16h5'/>" },

    { "cube",
      "<path d='M12 3l8 4.5v9L12 21l-8-4.5v-9z'/>"
      "<path d='M12 12l8-4.5M12 12v9M12 12L4 7.5'/>" },
};

QHash<QString, QIcon> g_cache;

QString bodyOf(const QString& name)
{
    for (const IconDef& def : kDefs) {
        if (name == QLatin1String(def.name))
            return QString::fromUtf8(def.body);
    }
    return QString();
}

QString colorKey(const QColor& color)
{
    return QStringLiteral("%1,%2,%3").arg(color.red()).arg(color.green()).arg(color.blue());
}

} // namespace

QIcon get(const QString& name, const QColor& color, int size)
{
    const QString key = name + QLatin1Char('|') + colorKey(color) + QLatin1Char('|')
        + QString::number(size);
    const auto cached = g_cache.constFind(key);
    if (cached != g_cache.constEnd())
        return cached.value();

    const QString body = bodyOf(name);
    if (body.isEmpty())
        return QIcon();

    const QString svg = QStringLiteral(
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' "
        "stroke='%1' stroke-width='1.7' stroke-linecap='round' stroke-linejoin='round'>%2</svg>")
        .arg(color.name(), body);

    QSvgRenderer renderer(svg.toUtf8());
    if (!renderer.isValid())
        return QIcon();

    QIcon icon;
    for (int scale : { 1, 2 }) {
        QPixmap pixmap(size * scale, size * scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        renderer.render(&painter, QRectF(0, 0, size * scale, size * scale));
        painter.end();
        pixmap.setDevicePixelRatio(scale);
        icon.addPixmap(pixmap);
    }

    g_cache.insert(key, icon);
    return icon;
}

QIcon get(const QString& name)
{
    return get(name, theme::iconColor());
}

QStringList names()
{
    QStringList result;
    for (const IconDef& def : kDefs)
        result.append(QString::fromLatin1(def.name));
    return result;
}

} // namespace icons
} // namespace tdms
