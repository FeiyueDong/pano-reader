#include "common/LibraryTypes.h"

namespace pano {

QString importModeToString(ImportMode mode)
{
    return mode == ImportMode::Reference ? QStringLiteral("reference") : QStringLiteral("copy");
}

bool importModeFromString(const QString &value, ImportMode *mode)
{
    if (value == QLatin1String("copy")) {
        if (mode)
            *mode = ImportMode::Copy;
        return true;
    }
    if (value == QLatin1String("reference")) {
        if (mode)
            *mode = ImportMode::Reference;
        return true;
    }
    return false;
}

QString importModeDisplayName(ImportMode mode)
{
    return mode == ImportMode::Reference ? QStringLiteral("映射") : QStringLiteral("拷贝");
}

QString zoomModeToString(ZoomMode mode)
{
    switch (mode) {
    case ZoomMode::FitWidth:
        return QStringLiteral("fit_width");
    case ZoomMode::FitPage:
        return QStringLiteral("fit_page");
    case ZoomMode::Fixed:
        break;
    }
    return QStringLiteral("fixed");
}

bool zoomModeFromString(const QString &value, ZoomMode *mode)
{
    if (value == QLatin1String("fixed")) {
        if (mode)
            *mode = ZoomMode::Fixed;
        return true;
    }
    if (value == QLatin1String("fit_width")) {
        if (mode)
            *mode = ZoomMode::FitWidth;
        return true;
    }
    if (value == QLatin1String("fit_page")) {
        if (mode)
            *mode = ZoomMode::FitPage;
        return true;
    }
    return false;
}

QString zoomModeDisplayName(ZoomMode mode)
{
    switch (mode) {
    case ZoomMode::FitWidth:
        return QStringLiteral("适应宽度");
    case ZoomMode::FitPage:
        return QStringLiteral("适应页面");
    case ZoomMode::Fixed:
        break;
    }
    return QStringLiteral("固定比例");
}

int LibraryData::bookIndex(const QString &bookId) const
{
    for (int i = 0; i < books.size(); ++i) {
        if (books.at(i).id == bookId)
            return i;
    }
    return -1;
}

int LibraryData::shelfIndex(const QString &shelfId) const
{
    for (int i = 0; i < shelves.size(); ++i) {
        if (shelves.at(i).id == shelfId)
            return i;
    }
    return -1;
}

BookRecord *LibraryData::book(const QString &bookId)
{
    const int index = bookIndex(bookId);
    return index >= 0 ? &books[index] : nullptr;
}

const BookRecord *LibraryData::book(const QString &bookId) const
{
    const int index = bookIndex(bookId);
    return index >= 0 ? &books.at(index) : nullptr;
}

ShelfRecord *LibraryData::shelf(const QString &shelfId)
{
    const int index = shelfIndex(shelfId);
    return index >= 0 ? &shelves[index] : nullptr;
}

const ShelfRecord *LibraryData::shelf(const QString &shelfId) const
{
    const int index = shelfIndex(shelfId);
    return index >= 0 ? &shelves.at(index) : nullptr;
}

} // namespace pano
