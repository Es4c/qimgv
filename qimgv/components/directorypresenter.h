#pragma once

#include <QObject>
#include <memory>
#include <type_traits>
#include <climits>
#include "gui/idirectoryview.h"
#include "directorymodel.h"
#include <QMimeData>

class DirectoryPresenter : public QObject {
    Q_OBJECT
public:
    explicit DirectoryPresenter(QObject *parent = nullptr);

    // 模板函数：保持插件动态加载的同时，用函数指针连接（编译期检查信号签名）
    template <typename ViewType>
    void setView(const std::shared_ptr<ViewType> &_view) {
        static_assert(std::is_base_of_v<QObject, ViewType>, "ViewType 必须继承自 QObject");
        static_assert(std::is_base_of_v<IDirectoryView, ViewType>, "ViewType 必须实现 IDirectoryView");
        if(view)
            return;
        view = _view;
        if(model)
            view->populate(mShowDirs ? qMin(static_cast<int>(model->totalCount()), INT_MAX) : qMin(static_cast<int>(model->fileCount()), INT_MAX));
        connect(_view.get(), &ViewType::itemActivated, this, &DirectoryPresenter::onItemActivated);
        connect(_view.get(), &ViewType::draggedOut,    this, &DirectoryPresenter::onDraggedOut);
        connect(_view.get(), &ViewType::draggedOver,   this, &DirectoryPresenter::onDraggedOver);
        connect(_view.get(), &ViewType::droppedInto,   this, &DirectoryPresenter::onDroppedInto);
    }

    void setModel(const std::shared_ptr<DirectoryModel> &newModel);
    void unsetModel();

    void selectAndFocus(int index);
    void selectAndFocus(const QString &path);

    void onFileRemoved(const QString &filePath, int index);
    void onFileRenamed(const QString &fromPath, int indexFrom, const QString &toPath, int indexTo);
    void onFileAdded(const QString &filePath);
    void onFileModified(const QString &filePath);

    void onDirRemoved(const QString &dirPath, int index);
    void onDirRenamed(const QString &fromPath, int indexFrom, const QString &toPath, int indexTo);
    void onDirAdded(const QString &dirPath);

    bool showDirs();
    void setShowDirs(bool mode);

    QList<QString> selectedPaths() const;


signals:
    void dirActivated(QString dirPath);
    void fileActivated(QString filePath);
    void draggedOut(QList<QString>);
    void droppedInto(QList<QString>, QString);

public slots:
    void reloadModel();

private slots:
    void populateView();
    void onItemActivated(int absoluteIndex);
    void onDraggedOut();
    void onDraggedOver(int index);

    void onDroppedInto(const QMimeData *data, QObject *source, int targetIndex);
private:
    std::shared_ptr<IDirectoryView> view = nullptr;
    std::shared_ptr<DirectoryModel> model = nullptr;
    bool mShowDirs = false;
    
    // 辅助函数：将文件索引转换为视图中的绝对索引
    int fileIndexToViewIndex(int fileIndex) const;
};
