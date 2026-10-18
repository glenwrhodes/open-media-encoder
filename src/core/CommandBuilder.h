#pragma once

#include "ExportSettings.h"
#include "MediaInfo.h"

#include <QList>
#include <QPair>
#include <QSize>
#include <QStringList>

using TimeRanges = QList<QPair<double, double>>; // [start, end) in seconds

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

    // Results of the analysis steps that run before an encode (see EncodeTask).
    TimeRanges keepRanges;    // remove silence: the parts kept, in seconds from the in point
    QString captionsSrt;      // generated captions to add as a subtitle track
    QString captionsAss;      // the same captions styled for the output frame, to burn in
    QString captionsLanguage; // ISO 639-1 code of the captions ("en"), empty when unknown
    double sizeScale = 1.0;   // target file size: bitrate correction after an encode came out too big
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

// Builds a command that decodes only the source audio (same range, stitching and silence cuts as the
// encode), runs it through audioFilter (may be empty) and writes it with outputArgs.
BuildResult buildAudioExtract(const BuildRequest &req, const QString &audioFilter, const QStringList &outputArgs);

bool usesTwoPass(const ExportSettings &s);
double outputDuration(const BuildRequest &req);
// Frame size of the encoded video (after crop and scaling).
QSize outputFrameSize(const BuildRequest &req);
// Video bitrate (Mbps) that keeps the output under the target file size, or 0 with an error.
double targetSizeBitrate(const BuildRequest &req, QString *error = nullptr);
// The ranges that remain once silences are removed from a clip of the given length.
TimeRanges keepRangesFromSilences(const TimeRanges &silences, double duration, double padding);
// select/aselect expression that keeps the given ranges.
QString rangeSelectExpr(const TimeRanges &ranges);
// Whether the output container can carry a subtitle track, and the subtitle codec it uses.
QString subtitleCodecFor(const ExportSettings &s);
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
// Global options a hardware encoder needs before the inputs (VA-API device), empty for others.
QStringList hardwareDeviceArgs(const QString &codec);
// The VA-API render node to encode on (Linux), empty when there is none.
QString vaapiDevice();

} // namespace CommandBuilder
