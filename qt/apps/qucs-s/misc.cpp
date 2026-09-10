/*
 * Qucs-S, ported to EwokOS - value/text helpers.
 */

#include "misc.h"

#include <math.h>

// The SPICE-ish suffixes Qucs accepts in a property field.  "Meg" has to be
// tried before the single letters: a bare "M" is mega here (upstream's
// text2double treats it that way), while "m" stays milli.
static const struct { const char *s; double f; } kPrefixes[] = {
    { "f",   1e-15 },
    { "p",   1e-12 },
    { "n",   1e-9  },
    { "u",   1e-6  },
    { "\xc2\xb5", 1e-6 },                 // UTF-8 micro sign
    { "m",   1e-3  },
    { "k",   1e3   },
    { "K",   1e3   },
    { "Meg", 1e6   },
    { "M",   1e6   },
    { "G",   1e9   },
    { "T",   1e12  },
};

double str2num(const QString &text, bool *ok)
{
    if (ok)
        *ok = false;

    const QString s = text.trimmed();
    int i = 0;
    const int n = s.size();

    // Leading number: sign, digits, dot, and an exponent.  'e' belongs to the
    // exponent only when a digit or a sign follows it - "1e3" is 1000, while
    // the 'e' of a unit such as "1 Vpeak" is not.
    if (i < n && (s.at(i) == QLatin1Char('+') || s.at(i) == QLatin1Char('-')))
        i++;
    int digits = 0;
    while (i < n && s.at(i).isDigit()) { i++; digits++; }
    if (i < n && s.at(i) == QLatin1Char('.')) {
        i++;
        while (i < n && s.at(i).isDigit()) { i++; digits++; }
    }
    if (digits == 0)
        return 0.0;
    if (i + 1 < n && (s.at(i) == QLatin1Char('e') || s.at(i) == QLatin1Char('E'))) {
        int j = i + 1;
        if (j < n && (s.at(j) == QLatin1Char('+') || s.at(j) == QLatin1Char('-')))
            j++;
        if (j < n && s.at(j).isDigit()) {
            while (j < n && s.at(j).isDigit())
                j++;
            i = j;
        }
    }

    bool conv = false;
    double value = s.left(i).toDouble(&conv);
    if (!conv)
        return 0.0;

    // Optional suffix.  "4k7" is 4.7k, so digits may follow the prefix; the
    // rest ("Ohm", "F", " Hz"...) is a unit and is ignored, as upstream does.
    const QString rest = s.mid(i);
    if (!rest.isEmpty()) {
        const unsigned count = sizeof(kPrefixes) / sizeof(kPrefixes[0]);
        for (unsigned p = 0; p < count; p++) {
            const QString pre = QLatin1String(kPrefixes[p].s);
            if (!rest.startsWith(pre, Qt::CaseSensitive))
                continue;
            QString frac = rest.mid(pre.size());
            int k = 0;
            while (k < frac.size() && frac.at(k).isDigit())
                k++;
            // Only a unit may follow those digits ("10nF"); anything else
            // means this letter was not a prefix at all.
            const QString tail = frac.mid(k).trimmed();
            if (!tail.isEmpty() && !tail.at(0).isLetter())
                continue;
            value *= kPrefixes[p].f;
            if (k > 0) {
                double d = frac.left(k).toDouble();
                value += d * kPrefixes[p].f / pow(10.0, k);
            }
            break;
        }
    }

    if (ok)
        *ok = true;
    return value;
}

QString num2str(double value, int prec)
{
    if (!isfinite(value))
        return QString("?");
    if (value == 0.0)
        return QString("0");

    const double a = fabs(value);
    static const struct { double lo; const char *s; double f; } scales[] = {
        { 1e12,  "T",   1e-12 },
        { 1e9,   "G",   1e-9  },
        { 1e6,   "Meg", 1e-6  },
        { 1e3,   "k",   1e-3  },
        { 1.0,   "",    1.0   },
        { 1e-3,  "m",   1e3   },
        { 1e-6,  "u",   1e6   },
        { 1e-9,  "n",   1e9   },
        { 1e-12, "p",   1e12  },
        { 1e-15, "f",   1e15  },
    };

    const unsigned count = sizeof(scales) / sizeof(scales[0]);
    for (unsigned i = 0; i < count; i++) {
        if (a < scales[i].lo)
            continue;
        const double scaled = value * scales[i].f;
        // Up to `prec` decimals, but drop the ones that carry nothing: 4.700
        // reads "4.7" and 100.000 reads "100".
        QString num = QString::number(scaled, 'g', prec < 1 ? 1 : prec);
        if (num.indexOf(QLatin1Char('.')) >= 0 && num.indexOf(QLatin1Char('e')) < 0) {
            while (num.endsWith(QLatin1Char('0')))
                num.chop(1);
            if (num.endsWith(QLatin1Char('.')))
                num.chop(1);
        }
        return num + QLatin1String(scales[i].s);
    }
    // Below femto: plain exponent, still trimmed.
    return QString::number(value, 'g', prec < 1 ? 1 : prec);
}

QString num2strUnit(double value, const QString &unit, int prec)
{
    if (unit.isEmpty())
        return num2str(value, prec);
    return num2str(value, prec) + QLatin1Char(' ') + unit;
}

double niceTicks(double lo, double hi, int count, double *first)
{
    if (!(hi > lo)) {
        if (first)
            *first = lo;
        return 1.0;
    }
    if (count < 2)
        count = 2;

    const double raw = (hi - lo) / count;
    const double exp10 = floor(log10(raw));
    const double base = pow(10.0, exp10);
    const double norm = raw / base;
    double step;
    if (norm <= 1.0)
        step = 1.0;
    else if (norm <= 2.0)
        step = 2.0;
    else if (norm <= 5.0)
        step = 5.0;
    else
        step = 10.0;
    step *= base;

    if (first)
        *first = ceil(lo / step) * step;
    return step;
}

QString axisNum(double value)
{
    const double a = fabs(value);
    if (a != 0.0 && (a >= 1e4 || a < 1e-3))
        return num2str(value, 4);
    // Integer-valued ticks read better without a decimal point.
    if (value == floor(value) && a < 1e15)
        return QString::number((long long)value);
    return QString::number(value, 'g', 4);
}
