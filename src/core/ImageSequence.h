#pragma once

#include <QString>
#include <QStringList>

// A numbered run of still images (shot_0001.png, shot_0002.png, ...) treated as one video source.
struct ImageSequence {
    bool valid = false;
    QString pattern;   // absolute FFmpeg pattern, e.g. C:/renders/shot_%04d.png
    QString firstFile; // absolute path of the first frame
    int start = 0;     // first frame number
    int count = 0;     // contiguous frames from `start`
    double fps = 24.0;

    // FFmpeg demuxer options that must precede `-i pattern`.
    QStringList inputArgs() const;
    // e.g. "shot_[0001-0240].png"
    QString displayName() const;
    // Base name for outputs, e.g. "shot" for shot_%04d.png.
    QString baseName() const;

    // Finds the contiguous numbered sequence that contains `anyFrame`. Requires at least two frames.
    static ImageSequence detect(const QString &anyFrame);
    // True when `files` are all frames of one detected sequence.
    static bool filesFormSequence(const QStringList &files, ImageSequence *out = nullptr);
    static bool isImageFile(const QString &path);
};
