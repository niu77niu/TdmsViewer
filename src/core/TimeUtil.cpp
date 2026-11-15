// ---------------------------------------------------------------------------
// TimeUtil.cpp
// ---------------------------------------------------------------------------
#include "TimeUtil.h"

#include <QDateTime>
#include <QLatin1Char>
#include <cmath>

namespace tdms {

namespace {

QString twoDigit(int value)
{
    return QStringLiteral("%1").arg(value, 2, 10, QLatin1Char('0'));
}

QString clockText(double seconds, bool withMillis)
{
    const bool negative = seconds < 0.0;
    double t = std::fabs(seconds);

    const int hours = int(t / 3600.0);
    t -= hours * 3600.0;
    const int minutes = int(t / 60.0);
    t -= minutes * 60.0;
    const int wholeSeconds = int(t);
    const int millis = int(std::lround((t - wholeSeconds) * 1000.0));
    const int ms = qBound(0, millis, 999);

    QString text;
    if (hours > 0)
        text = QStringLiteral("%1:%2:%3").arg(hours).arg(twoDigit(minutes), twoDigit(wholeSeconds));
    else
        text = QStringLiteral("%1:%2").arg(twoDigit(minutes), twoDigit(wholeSeconds));

    if (withMillis)
        text += QStringLiteral(".%1").arg(ms, 3, 10, QLatin1Char('0'));
    if (negative)
        text.prepend(QLatin1Char('-'));
    return text;
}

} // namespace

QString formatRelativeTick(double seconds, double span)
{
    if (span <= 0.0 || !std::isfinite(span))
        span = std::fabs(seconds);
    if (!std::isfinite(seconds))
        return QString();

    const double magnitude = std::fabs(seconds);

    if (span < 60.0) {
        int decimals = 1;
        if (span < 5.0)
            decimals = 3;
        else if (span < 20.0)
            decimals = 2;
        if (magnitude >= 1000.0)
            decimals = 0;
        return QString::number(seconds, 'f', decimals) + QStringLiteral(" s");
    }

    if (span < 3600.0)
        return clockText(seconds, false);

    return clockText(seconds, false);
}

QString formatAbsoluteTick(qint64 msecs, double spanMs)
{
    if (spanMs <= 0.0 || !std::isfinite(spanMs))
        spanMs = 60000.0;

    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(msecs);
    if (!dt.isValid())
        return QString();

    if (spanMs < 2000.0)
        return dt.toString(QStringLiteral("HH:mm:ss.zzz"));
    if (spanMs < 24.0 * 3600.0 * 1000.0)
        return dt.toString(QStringLiteral("HH:mm:ss"));
    if (spanMs < 7.0 * 24.0 * 3600.0 * 1000.0)
        return dt.toString(QStringLiteral("dd.MM HH:mm"));
    if (spanMs < 400.0 * 24.0 * 3600.0 * 1000.0)
        return dt.toString(QStringLiteral("dd.MM"));
    return dt.toString(QStringLiteral("MM.yyyy"));
}

QString formatElapsed(double seconds)
{
    if (!std::isfinite(seconds))
        return QString();

    if (std::fabs(seconds) < 60.0)
        return QStringLiteral("%1%2 s")
            .arg(seconds < 0.0 ? QString() : QStringLiteral("+"))
            .arg(QString::number(seconds, 'f', 3));

    return QStringLiteral("%1%2")
        .arg(seconds < 0.0 ? QString() : QStringLiteral("+"), clockText(seconds, true));
}

QString formatSampleInterval(double seconds)
{
    if (!(seconds > 0.0) || !std::isfinite(seconds))
        return QString();

    QString text;
    if (seconds >= 1.0) {
        text = QString::number(seconds, 'g', 6) + QStringLiteral(" s");
    } else if (seconds >= 1e-3) {
        text = QString::number(seconds * 1e3, 'g', 6) + QStringLiteral(" ms");
    } else if (seconds >= 1e-6) {
        text = QString::number(seconds * 1e6, 'g', 6) + QStringLiteral(" µs");
    } else {
        text = QString::number(seconds * 1e9, 'g', 6) + QStringLiteral(" ns");
    }

    const double rate = 1.0 / seconds;
    if (rate >= 0.01 && rate < 1e9)
        text += QStringLiteral("（%1 Hz）").arg(QString::number(rate, 'g', 6));
    return text;
}

QString formatDurationShort(double seconds)
{
    if (!(seconds > 0.0) || !std::isfinite(seconds))
        return QStringLiteral("0");
    if (seconds >= 1.0)
        return QString::number(seconds, 'g', 4);
    if (seconds >= 1e-3)
        return QString::number(seconds * 1e3, 'g', 4) + QStringLiteral(" ms");
    return QString::number(seconds * 1e6, 'g', 4) + QStringLiteral(" µs");
}

} // namespace tdms
