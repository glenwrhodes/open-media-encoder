#pragma once

#include "ExportSettings.h"
#include "MediaInfo.h"

#include <QStringList>

struct BuildRequest {
    QStringList inputs;         // more than one input = stitched (concatenated) source
    MediaInfo source;           // info for the first input
    double totalDuration = 0.0; // duration of all inputs combined (0 = use source.duration)
    ExportSettings settings;
    QString outputPath;
    double inPoint = -1.0;  // seconds, < 0 = start of media
    double outPoint = -1.0; // seconds, < 0 = end of media
    int pass = 0;           // 0 = single pass, 1/2 = two-pass encoding
    QString passLogFile;    // relative to workDir for x265 compatibility
    bool hwDecode = false;
    QString workDir;        // where helper files (text, concat lists) are written
    QString ffmpegFontFile; // optional override for overlay text font
    QStringList inputArgs;  // extra demuxer options before the main -i (e.g. image sequence rate)
};

struct BuildResult {
    QStringList args;
    QString error;
    QStringList tempFiles;
    double duration = 0.0; // expected output duration in seconds
    bool ok() const { return error.isEmpty(); }
};

namespace CommandBuilder {

BuildResult build(const BuildRequest &req);
// Builds a single-frame grab at `time` seconds, written to `outImage` (BMP).
// When applyEffects is false the raw source frame is returned.
BuildResult buildPreview(const BuildRequest &req, double time, const QString &outImage, bool applyEffects);

bool usesTwoPass(const ExportSettings &s);
double outputDuration(const BuildRequest &req);
QString resolvePixelFormat(const ExportSettings &s, const MediaInfo &src);
QString resolveProfile(const ExportSettings &s, const QString &pixFmt);
QString videoFilterGraph(const BuildRequest &req, bool preview, QStringList *tempFiles, QString *error);

QString escapeFilterValue(const QString &value);
QString fpsToRational(double fps);
// True when both paths name the same file (works for files that don't exist yet).
bool samePath(const QString &a, const QString &b);
QString defaultOutputPath(const QString &sourcePath, const ExportSettings &s, const QString &outputDir);
QString uniqueOutputPath(const QString &path);
QString firstSequenceFile(const QString &pattern);
qint64 timecodeToFrames(const QString &tc, int fps);
QString framesToTimecode(qint64 frames, int fps);
QString findDefaultFont();

} // namespace CommandBuilder
