#include "pdf/PdfDocument.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

namespace pano {

QString pdfErrorText(QPdfDocument::Error error)
{
    switch (error) {
    case QPdfDocument::Error::None:
        return QString();
    case QPdfDocument::Error::FileNotFound:
        return QObject::tr("文件不存在或无法读取。");
    case QPdfDocument::Error::InvalidFileFormat:
        return QObject::tr("不是有效的 PDF 文件。");
    case QPdfDocument::Error::IncorrectPassword:
        return QObject::tr("PDF 已加密，当前版本不支持需要密码的文件。");
    case QPdfDocument::Error::UnsupportedSecurityScheme:
        return QObject::tr("PDF 使用了不支持的加密方式。");
    case QPdfDocument::Error::DataNotYetAvailable:
        return QObject::tr("PDF 数据尚未就绪。");
    case QPdfDocument::Error::Unknown:
        break;
    }
    return QObject::tr("PDF 打开失败（未知错误）。");
}

PdfDocument::PdfDocument(QObject *parent)
    : QObject(parent)
{
}

bool PdfDocument::load(const QString &filePath, QString *errorMessage)
{
    close();

    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        m_lastError = tr("文件不存在：%1").arg(filePath);
        if (errorMessage)
            *errorMessage = m_lastError;
        return false;
    }

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = tr("无法读取文件：%1").arg(file.errorString());
        if (errorMessage)
            *errorMessage = m_lastError;
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        m_lastError = tr("读取文件用于校验时失败。");
        if (errorMessage)
            *errorMessage = m_lastError;
        return false;
    }
    m_sha256 = QString::fromLatin1(hash.result().toHex());
    m_fileSize = file.size();
    file.close();

    const QPdfDocument::Error error = m_pdf.load(info.absoluteFilePath());
    if (error != QPdfDocument::Error::None) {
        m_pdf.close();
        m_lastError = pdfErrorText(error);
        if (errorMessage)
            *errorMessage = m_lastError;
        return false;
    }

    if (m_pdf.pageCount() <= 0) {
        m_pdf.close();
        m_lastError = tr("PDF 中没有可显示的页面。");
        if (errorMessage)
            *errorMessage = m_lastError;
        return false;
    }

    m_filePath = info.absoluteFilePath();
    m_lastError.clear();
    emit documentChanged();
    return true;
}

void PdfDocument::close()
{
    if (m_pdf.status() != QPdfDocument::Status::Null)
        m_pdf.close();
    const bool had = !m_filePath.isEmpty();
    m_filePath.clear();
    m_sha256.clear();
    m_fileSize = 0;
    m_lastError.clear();
    if (had)
        emit documentChanged();
}

bool PdfDocument::isLoaded() const
{
    return m_pdf.status() == QPdfDocument::Status::Ready && !m_filePath.isEmpty();
}

QString PdfDocument::filePath() const { return m_filePath; }
QString PdfDocument::sha256() const { return m_sha256; }
qint64 PdfDocument::fileSize() const { return m_fileSize; }

QString PdfDocument::fileName() const
{
    return m_filePath.isEmpty() ? QString() : QFileInfo(m_filePath).fileName();
}

QString PdfDocument::title() const
{
    const QVariant value = m_pdf.metaData(QPdfDocument::MetaDataField::Title);
    const QString title = value.toString().trimmed();
    return title.isEmpty() ? fileName() : title;
}

int PdfDocument::pageCount() const
{
    return isLoaded() ? m_pdf.pageCount() : 0;
}

QSizeF PdfDocument::pagePointSize(int pageIndex) const
{
    if (!isLoaded() || pageIndex < 0 || pageIndex >= m_pdf.pageCount())
        return QSizeF();
    return m_pdf.pagePointSize(pageIndex);
}

QPdfDocument *PdfDocument::pdf() const
{
    return const_cast<QPdfDocument *>(&m_pdf);
}

QString PdfDocument::lastError() const { return m_lastError; }

} // namespace pano
