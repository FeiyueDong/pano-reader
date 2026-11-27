#pragma once

#include "annotation/Annotation.h"

#include <QDateTime>
#include <QString>
#include <QVector>

namespace pano {

class PdfDocument;

// Per-document annotation store. The directory is a sibling of the PDF named
// "<file>.pano" and contains document.json, annotations.json and assets/.
// The PDF SHA-256 is the identity check: a mismatch stops all loading and
// saving, and existing data is never silently rebound or overwritten.
class AnnotationStore
{
public:
    struct Manifest {
        int schemaVersion = 1;
        QString documentId;
        QString pdfFileName;
        QString pdfSha256;
        qint64 pdfSizeBytes = 0;
        QString pdfPath;
        QString createdAt;
        QString updatedAt;
    };

    AnnotationStore() = default;

    static QString directoryForPdf(const QString &pdfPath);

    // Binds this store to the PDF. An absent data directory is a valid "new
    // store" state; an existing directory must carry a readable, matching
    // document.json, otherwise this returns false and leaves data untouched.
    bool attachToDocument(const PdfDocument &document, QString *error);

    bool isValid() const { return m_attached; }
    bool isNewStore() const { return m_newStore; }
    QString directory() const { return m_directory; }
    QString documentFilePath() const;
    QString annotationsFilePath() const;
    QString assetsDirectory() const;
    QString documentId() const { return m_manifest.documentId; }
    const Manifest &manifest() const { return m_manifest; }

    bool loadAnnotations(int pageCount, QVector<Annotation> *annotations, QString *error) const;
    bool save(const QVector<Annotation> &annotations, QString *error);

    // Copies bytes into assets/ and returns the path relative to the store
    // directory (for example "assets/capture-1234.png").
    bool writeAsset(const QString &prefix, const QString &extension, const QByteArray &bytes,
                    QString *relativePath, QString *error);

    QString absoluteAssetPath(const QString &relativePath) const;

private:
    bool readManifest(const QString &path, QString *error);
    bool writeManifest(QString *error);
    bool ensureDirectories(QString *error);

    QString m_directory;
    Manifest m_manifest;
    bool m_attached = false;
    bool m_newStore = false;
};

} // namespace pano
