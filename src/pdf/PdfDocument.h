#pragma once

#include <QObject>
#include <QPdfDocument>
#include <QSizeF>
#include <QString>

namespace pano {

// Thin, non-owning-by-Qt wrapper around QPdfDocument with file identity info.
// The wrapped document is only ever read; PANO never writes to the source PDF.
class PdfDocument : public QObject
{
    Q_OBJECT

public:
    explicit PdfDocument(QObject *parent = nullptr);

    bool load(const QString &filePath, QString *errorMessage = nullptr);
    void close();

    bool isLoaded() const;
    QString filePath() const;
    QString fileName() const;
    QString sha256() const;
    qint64 fileSize() const;
    QString title() const;

    int pageCount() const;
    QSizeF pagePointSize(int pageIndex) const;

    QPdfDocument *pdf() const;
    QString lastError() const;

signals:
    void documentChanged();

private:
    QPdfDocument m_pdf;
    QString m_filePath;
    QString m_sha256;
    qint64 m_fileSize = 0;
    QString m_lastError;
};

QString pdfErrorText(QPdfDocument::Error error);

} // namespace pano
