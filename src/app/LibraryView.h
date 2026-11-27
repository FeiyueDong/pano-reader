#pragma once

#include <QWidget>

#include "library/LibraryTypes.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace pano {

class LibraryManager;

// Library tab: navigation tree, book list with search/sort, and the selected
// book's details/actions. It never opens a document itself; MainWindow owns
// reader tabs and sessions.
class LibraryView : public QWidget
{
    Q_OBJECT

public:
    explicit LibraryView(LibraryManager *manager, QWidget *parent = nullptr);

    void refresh();
    void refreshBooks();
    void selectBook(const QString &bookId);
    void importBooks();
    void importFiles(const QStringList &paths);

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

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
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

    LibraryManager *m_manager = nullptr;
    QTreeWidget *m_navigation = nullptr;
    QLineEdit *m_search = nullptr;
    QComboBox *m_sort = nullptr;
    QListWidget *m_books = nullptr;
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
    QPushButton *m_newShelfButton = nullptr;
    QPushButton *m_renameShelfButton = nullptr;
    QPushButton *m_deleteShelfButton = nullptr;
};

} // namespace pano
