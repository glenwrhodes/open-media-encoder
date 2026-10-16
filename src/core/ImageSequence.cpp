#include "ImageSequence.h"

#include "CommandBuilder.h"
#include "Formats.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace {

struct NameParts {
    QString prefix;
    QString digits;
    QString suffix; // extension including the dot
};

// Splits "shot_0012.png" into "shot_", "0012", ".png" (the last run of digits before the extension).
bool splitName(const QString &fileName, NameParts *out)
{
    static const QRegularExpression re(QStringLiteral("^(.*?)(\\d+)(\\.[^.]+)$"));
    const auto m = re.match(fileName);
    if (!m.hasMatch())
        return false;
    out->prefix = m.captured(1);
    out->digits = m.captured(2);
    out->suffix = m.captured(3);
    return true;
}

} // namespace

bool ImageSequence::isImageFile(const QString &path) { return Formats::mediaKind(path) == MediaKind::Image; }

QStringList ImageSequence::inputArgs() const
{
    return {QStringLiteral("-framerate"), CommandBuilder::fpsToRational(fps), QStringLiteral("-start_number"),
            QString::number(start)};
}

QString ImageSequence::displayName() const
{
    NameParts first;
    if (!splitName(QFileInfo(firstFile).fileName(), &first))
        return QFileInfo(pattern).fileName();
    const QString lastDigits = pattern.contains(QStringLiteral("%0"))
                                   ? QStringLiteral("%1").arg(start + count - 1, first.digits.size(), 10, QChar('0'))
                                   : QString::number(start + count - 1);
    return QStringLiteral("%1[%2-%3]%4").arg(first.prefix, first.digits, lastDigits, first.suffix);
}

QString ImageSequence::baseName() const
{
    NameParts parts;
    QString base = splitName(QFileInfo(firstFile).fileName(), &parts) ? parts.prefix : QFileInfo(firstFile).baseName();
    while (!base.isEmpty() && (base.endsWith('_') || base.endsWith('-') || base.endsWith('.') || base.endsWith(' ')))
        base.chop(1);
    return base.isEmpty() ? QStringLiteral("sequence") : base;
}

ImageSequence ImageSequence::detect(const QString &anyFrame)
{
    ImageSequence seq;
    const QFileInfo fi(anyFrame);
    NameParts parts;
    if (!fi.exists() || !isImageFile(anyFrame) || !splitName(fi.fileName(), &parts))
        return seq;

    // Zero-padded names ("0012") use a fixed width; plain numbers ("12") use %d.
    const bool padded = parts.digits.size() > 1 && parts.digits.startsWith('0');
    const int width = parts.digits.size();

    QSet<int> numbers;
    int widthMatches = 0;
    const QStringList names = fi.dir().entryList(QDir::Files);
    for (const QString &name : names) {
        NameParts p;
        if (!splitName(name, &p) || p.prefix != parts.prefix ||
            p.suffix.compare(parts.suffix, Qt::CaseInsensitive) != 0)
            continue;
        if (padded && p.digits.size() != width)
            continue;
        if (!padded && p.digits.size() > 1 && p.digits.startsWith('0'))
            continue;
        if (p.digits.size() == width)
            ++widthMatches;
        bool ok = false;
        const int n = p.digits.toInt(&ok);
        if (ok)
            numbers.insert(n);
    }
    // All frames share one width (e.g. 1000..1240 without leading zeros): treat as fixed width.
    const bool fixedWidth = padded || widthMatches == numbers.size();

    // The contiguous run containing the chosen frame (FFmpeg stops at the first gap).
    const int chosen = parts.digits.toInt();
    int first = chosen;
    while (numbers.contains(first - 1))
        --first;
    int last = chosen;
    while (numbers.contains(last + 1))
        ++last;
    if (last - first + 1 < 2)
        return seq;

    QString prefix = parts.prefix;
    prefix.replace('%', QStringLiteral("%%"));
    // Built by concatenation: QString::arg() would treat "%0" in "%0Nd" as a placeholder.
    const QString counter =
        fixedWidth ? QStringLiteral("%0") + QString::number(width) + QLatin1Char('d') : QStringLiteral("%d");
    seq.pattern = fi.dir().absoluteFilePath(prefix + counter + parts.suffix);
    seq.start = first;
    seq.count = last - first + 1;
    const QString firstDigits =
        fixedWidth ? QStringLiteral("%1").arg(first, width, 10, QChar('0')) : QString::number(first);
    seq.firstFile = fi.dir().absoluteFilePath(parts.prefix + firstDigits + parts.suffix);
    seq.valid = QFileInfo::exists(seq.firstFile);
    return seq;
}

bool ImageSequence::filesFormSequence(const QStringList &files, ImageSequence *out)
{
    if (files.size() < 2)
        return false;
    for (const QString &f : files)
        if (!isImageFile(f))
            return false;
    QStringList sorted = files;
    std::sort(sorted.begin(), sorted.end());
    const ImageSequence seq = detect(sorted.first());
    if (!seq.valid)
        return false;
    // Every selected file must belong to the detected run.
    const QString dir = QFileInfo(seq.pattern).absolutePath();
    for (const QString &f : files) {
        const QFileInfo fi(f);
        if (QDir(fi.absolutePath()) != QDir(dir))
            return false;
        NameParts p;
        NameParts ref;
        if (!splitName(fi.fileName(), &p) || !splitName(QFileInfo(seq.firstFile).fileName(), &ref) ||
            p.prefix != ref.prefix || p.suffix.compare(ref.suffix, Qt::CaseInsensitive) != 0)
            return false;
        const int n = p.digits.toInt();
        if (n < seq.start || n >= seq.start + seq.count)
            return false;
    }
    if (out)
        *out = seq;
    return true;
}
