#pragma once

#include <QWidget>

#include <functional>

#include "common/LibraryTypes.h"

class QComboBox;
class QDragMoveEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSplitter;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace pano {

class LibraryManager;
class SettingsManager;

// Library tab: navigation tree, book list with search/sort, and the selected
// book's details/actions. It never opens a document itself; MainWindow owns
// reader tabs and sessions.
class LibraryView : public QWidget
{
    Q_OBJECT

public:
    explicit LibraryView(LibraryManager *manager, SettingsManager *settings = nullptr,
                         QWidget *parent = nullptr);

    void refresh();
    void refreshBooks();
    void selectBook(const QString &bookId);
    void importBooks();
    void importFiles(const QStringList &paths);
    void setBeforeTrashHandler(std::function<bool(const QString &)> handler)
    {
        m_beforeTrash = std::move(handler);
    }

signals:
    void bookOpenRequested(const QString &bookId);
    void statusMessage(const QString &message);

private slots:
    void onNavigationChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    void onBookSelectionChanged();
    void onBookActivated();
    void toggleFavorite();
    void moveToTrash();
    void restoreBook();
    void deletePermanently();
    void changeSource();
    void addBookToShelf();
    void createShelf();
    void renameShelf();
    void deleteShelf();
    void showBookContextMenu(const QPoint &position);
    void showNavContextMenu(const QPoint &position);
    void moveShelfDialog();
    void onViewModeToggled(bool cover);
    void onSplitterMoved();
    void onBreadcrumbActivated(const QString &target);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    struct Navigation {
        enum Kind { All, Favorites, Shelf, Trash } kind = All;
        QString shelfId;
    };

    void buildUi();
    void rebuildNavigation();
    void refreshInfo();
    Navigation currentNavigation() const;
    void saveExpandedState();
    void restoreExpandedState();
    QString selectedBookId() const;
    void setStatus(const QString &message);
    BookInfo selectedBook() const;
    bool askImportMode(ImportMode *mode);
    QWidget *buildInfoPane();
    void applyViewMode(bool cover);
    void updateBreadcrumb();
    void selectShelf(const QString &shelfId);
    QString shelfPath(const QString &shelfId) const;
    QString currentShelfId() const;

    LibraryManager *m_manager = nullptr;
    SettingsManager *m_settings = nullptr;
    QTreeWidget *m_navigation = nullptr;
    QLineEdit *m_search = nullptr;
    QComboBox *m_sort = nullptr;
    QListWidget *m_books = nullptr;
    QSplitter *m_splitter = nullptr;
    QLabel *m_breadcrumb = nullptr;
    QToolButton *m_viewToggle = nullptr;
    QWidget *m_infoPane = nullptr;
    QLabel *m_cover = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_author = nullptr;
    QLabel *m_meta = nullptr;
    QLabel *m_source = nullptr;
    QLabel *m_counts = nullptr;
    QPushButton *m_openButton = nullptr;
    QPushButton *m_favoriteButton = nullptr;
    QPushButton *m_trashButton = nullptr;
    QPushButton *m_restoreButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QPushButton *m_sourceButton = nullptr;
    QPushButton *m_shelfButton = nullptr;
    QToolButton *m_importButton = nullptr;
    QToolButton *m_newShelfButton = nullptr;
    QToolButton *m_renameShelfButton = nullptr;
    QToolButton *m_deleteShelfButton = nullptr;
    bool m_coverMode = true;
    std::function<bool(const QString &)> m_beforeTrash;
};

} // namespace pano
