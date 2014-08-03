/*

    Copyright (C) 2013  Hong Jen Yee (PCMan) <pcman.tw@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "mainwindow.h"

#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QAction>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QSplitter>
#include <QToolButton>
#include <QShortcut>
#include <QKeySequence>
#include <QDebug>

#include "tabpage.h"
#include "filelauncher.h"
#include "filemenu.h"
#include "bookmarkaction.h"
#include "fileoperation.h"
#include "utilities.h"
#include "filepropsdialog.h"
#include "pathedit.h"
#include "ui_about.h"
#include "application.h"
#include "path.h"

// #include "qmodeltest/modeltest.h"

using namespace Fm;

namespace PCManFM {

MainWindow::MainWindow(FmPath* path):
  QMainWindow(),
  fileLauncher_(this) {

  Settings& settings = static_cast<Application*>(qApp)->settings();
  setAttribute(Qt::WA_DeleteOnClose);
  // setup user interface
  ui.setupUi(this);

  // add a context menu for showing browse history to back and forward buttons
  QToolButton* forwardButton = static_cast<QToolButton*>(ui.toolBar->widgetForAction(ui.actionGoForward));
  forwardButton->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(forwardButton, SIGNAL(customContextMenuRequested(QPoint)), SLOT(onBackForwardContextMenu(QPoint)));
  QToolButton* backButton = static_cast<QToolButton*>(ui.toolBar->widgetForAction(ui.actionGoBack));
  backButton->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(backButton, SIGNAL(customContextMenuRequested(QPoint)), SLOT(onBackForwardContextMenu(QPoint)));

  // tabbed browsing interface
  ui.tabBar->setDocumentMode(true);
  ui.tabBar->setTabsClosable(true);
  ui.tabBar->setElideMode(Qt::ElideRight);
  ui.tabBar->setExpanding(false);
  connect(ui.tabBar, SIGNAL(currentChanged(int)), SLOT(onTabBarCurrentChanged(int)));
  connect(ui.tabBar, SIGNAL(tabCloseRequested(int)), SLOT(onTabBarCloseRequested(int)));
  connect(ui.stackedWidget, SIGNAL(widgetRemoved(int)), SLOT(onStackedWidgetWidgetRemoved(int)));

  // side pane
  ui.sidePane->setIconSize(QSize(settings.sidePaneIconSize(), settings.sidePaneIconSize()));
  ui.sidePane->setMode(Fm::SidePane::ModePlaces);
  connect(ui.sidePane, SIGNAL(chdirRequested(int, FmPath*)), SLOT(onSidePaneChdirRequested(int, FmPath*)));

  // path bar
  pathEntry = new Fm::PathEdit(this);
  connect(pathEntry, SIGNAL(returnPressed()), SLOT(onPathEntryReturnPressed()));
  ui.toolBar->insertWidget(ui.actionGo, pathEntry);

  // add filesystem info to status bar
  fsInfoLabel = new QLabel(ui.statusbar);
  ui.statusbar->addPermanentWidget(fsInfoLabel);

  // setup the splitter
  ui.splitter->setStretchFactor(1, 1); // only the right pane can be stretched
  QList<int> sizes;
  sizes.append(150);
  sizes.append(300);
  ui.splitter->setSizes(sizes);

  // load bookmark menu
  bookmarks = fm_bookmarks_dup();
  g_signal_connect(bookmarks, "changed", G_CALLBACK(onBookmarksChanged), this);
  loadBookmarksMenu();

  // Fix the menu groups which is not done by Qt designer
  // To my suprise, this was supported in Qt designer 3 :-(
  QActionGroup* group = new QActionGroup(ui.menu_View);
  group->setExclusive(true);
  group->addAction(ui.actionIconView);
  group->addAction(ui.actionCompactView);
  group->addAction(ui.actionThumbnailView);
  group->addAction(ui.actionDetailedList);

  group = new QActionGroup(ui.menuSorting);
  group->setExclusive(true);
  group->addAction(ui.actionByFileName);
  group->addAction(ui.actionByMTime);
  group->addAction(ui.actionByFileSize);
  group->addAction(ui.actionByFileType);
  group->addAction(ui.actionByOwner);

  group = new QActionGroup(ui.menuSorting);
  group->setExclusive(true);
  group->addAction(ui.actionAscending);
  group->addAction(ui.actionDescending);

  // create shortcuts
  QShortcut* shortcut = new QShortcut(Qt::CTRL + Qt::Key_L, this);
  connect(shortcut, SIGNAL(activated()), pathEntry, SLOT(setFocus()));
  shortcut = new QShortcut(Qt::ALT + Qt::Key_D, this);
  connect(shortcut, SIGNAL(activated()), pathEntry, SLOT(setFocus()));

  shortcut = new QShortcut(Qt::CTRL + Qt::Key_Tab, this);
  connect(shortcut, SIGNAL(activated()), SLOT(onShortcutNextTab()));
  shortcut = new QShortcut(Qt::CTRL + Qt::SHIFT + Qt::Key_Tab, this);
  connect(shortcut, SIGNAL(activated()), SLOT(onShortcutPrevTab()));

  int i;
  for(i = 0; i < 10; ++i) {
    shortcut = new QShortcut(QKeySequence(Qt::ALT + Qt::Key_0 + i), this);
    connect(shortcut, SIGNAL(activated()), SLOT(onShortcutJumpToTab()));
    shortcut = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_0 + i), this);
    connect(shortcut, SIGNAL(activated()), SLOT(onShortcutJumpToTab()));
  }

  QShortcut *backspaceShortcut = new QShortcut(QKeySequence(Qt::Key_Backspace), this);
  connect(backspaceShortcut, SIGNAL(activated()), SLOT(on_actionGoUp_triggered()));

  if(path)
    addTab(path);
}

MainWindow::~MainWindow() {
  if(bookmarks)
    g_object_unref(bookmarks);
}

void MainWindow::chdir(FmPath* path) {
  TabPage* page = currentPage();

  if(page) {
    page->chdir(path, true);
    updateUIForCurrentPage();
  }
}

// add a new tab
void MainWindow::addTab(FmPath* path) {
  Settings& settings = static_cast<Application*>(qApp)->settings();

  TabPage* newPage = new TabPage(path, this);
  newPage->setFileLauncher(&fileLauncher_);
  int index = ui.stackedWidget->addWidget(newPage);
  connect(newPage, SIGNAL(titleChanged(QString)), SLOT(onTabPageTitleChanged(QString)));
  connect(newPage, SIGNAL(statusChanged(int, QString)), SLOT(onTabPageStatusChanged(int, QString)));
  connect(newPage, SIGNAL(openDirRequested(FmPath*, int)), SLOT(onTabPageOpenDirRequested(FmPath*, int)));
  connect(newPage, SIGNAL(sortFilterChanged()), SLOT(onTabPageSortFilterChanged()));

  ui.tabBar->insertTab(index, newPage->title());

  if(!settings.alwaysShowTabs()) {
    ui.tabBar->setVisible(ui.tabBar->count() > 1);
  }
}

void MainWindow::onPathEntryReturnPressed() {
  QString text = pathEntry->text();
  QByteArray utext = text.toUtf8();
  FmPath* path = fm_path_new_for_display_name(utext);
  chdir(path);
  fm_path_unref(path);
}

void MainWindow::on_actionGoUp_triggered() {
  TabPage* page = currentPage();

  if(page) {
    page->up();
    updateUIForCurrentPage();
  }
}

void MainWindow::on_actionGoBack_triggered() {
  TabPage* page = currentPage();

  if(page) {
    page->backward();
    updateUIForCurrentPage();
  }
}

void MainWindow::on_actionGoForward_triggered() {
  TabPage* page = currentPage();

  if(page) {
    page->forward();
    updateUIForCurrentPage();
  }
}

void MainWindow::on_actionHome_triggered() {
  chdir(fm_path_get_home());
}

void MainWindow::on_actionReload_triggered() {
  currentPage()->reload();
}

void MainWindow::on_actionGo_triggered() {
  onPathEntryReturnPressed();
}

void MainWindow::on_actionNewTab_triggered() {
  FmPath* path = currentPage()->path();
  addTab(path);
}

void MainWindow::on_actionNewWin_triggered() {
  FmPath* path = currentPage()->path();
  Application* app = static_cast<Application*>(qApp);
  MainWindow* newWin = new MainWindow(path);
  newWin->resize(app->settings().windowWidth(), app->settings().windowHeight());
  newWin->show();
}

void MainWindow::on_actionCloseTab_triggered() {
  closeTab(ui.tabBar->currentIndex());
}

void MainWindow::on_actionCloseWindow_triggered() {
  // FIXME: should we save state here?
  close();
  // the window will be deleted automatically on close
}

void MainWindow::on_actionFileProperties_triggered() {
  TabPage* page = currentPage();

  if(page) {
    FmFileInfoList* files = page->selectedFiles();

    if(files) {
      Fm::FilePropsDialog::showForFiles(files);
      fm_file_info_list_unref(files);
    }
  }
}

void MainWindow::on_actionFolderProperties_triggered() {
  TabPage* page = currentPage();

  if(page) {
    FmFolder* folder = page->folder();

    if(folder) {
      FmFileInfo* info = fm_folder_get_info(folder);

      if(info)
        Fm::FilePropsDialog::showForFile(info);
    }
  }
}

void MainWindow::on_actionShowHidden_triggered(bool checked) {
  TabPage* tabPage = currentPage();
  tabPage->setShowHidden(checked);
  static_cast<Application*>(qApp)->settings().setShowHidden(checked);
}

void MainWindow::on_actionByFileName_triggered(bool checked) {
  currentPage()->sort(Fm::FolderModel::ColumnFileName, currentPage()->sortOrder());
}

void MainWindow::on_actionByMTime_triggered(bool checked) {
  currentPage()->sort(Fm::FolderModel::ColumnFileMTime, currentPage()->sortOrder());
}

void MainWindow::on_actionByOwner_triggered(bool checked) {
  currentPage()->sort(Fm::FolderModel::ColumnFileOwner, currentPage()->sortOrder());
}

void MainWindow::on_actionByFileSize_triggered(bool checked) {
  currentPage()->sort(Fm::FolderModel::ColumnFileSize, currentPage()->sortOrder());
}

void MainWindow::on_actionByFileType_triggered(bool checked) {
  currentPage()->sort(Fm::FolderModel::ColumnFileType, currentPage()->sortOrder());
}

void MainWindow::on_actionAscending_triggered(bool checked) {
  currentPage()->sort(currentPage()->sortColumn(), Qt::AscendingOrder);
}

void MainWindow::on_actionDescending_triggered(bool checked) {
  currentPage()->sort(currentPage()->sortColumn(), Qt::DescendingOrder);
}

void MainWindow::on_actionCaseSensitive_triggered(bool checked) {
  currentPage()->setSortCaseSensitive(checked);
}

void MainWindow::on_actionFolderFirst_triggered(bool checked) {
  currentPage()->setSortFolderFirst(checked);
}

void MainWindow::on_actionComputer_triggered() {
  FmPath* path = fm_path_new_for_uri("computer:///");
  chdir(path);
  fm_path_unref(path);
}

void MainWindow::on_actionApplications_triggered() {
  chdir(fm_path_get_apps_menu());
}

void MainWindow::on_actionTrash_triggered() {
  chdir(fm_path_get_trash());
}

void MainWindow::on_actionNetwork_triggered() {
  FmPath* path = fm_path_new_for_uri("network:///");
  chdir(path);
  fm_path_unref(path);
}

void MainWindow::on_actionDesktop_triggered() {
  chdir(fm_path_get_desktop());
}

void MainWindow::on_actionAddToBookmarks_triggered() {
  TabPage* page = currentPage();

  if(page) {
    FmPath* cwd = page->path();

    if(cwd) {
      char* dispName = fm_path_display_basename(cwd);
      fm_bookmarks_insert(bookmarks, cwd, dispName, -1);
      g_free(dispName);
    }
  }
}

void MainWindow::on_actionEditBookmarks_triggered() {
  Application* app = static_cast<Application*>(qApp);
  app->editBookmarks();
}

void MainWindow::on_actionAbout_triggered() {
  // the about dialog
  class AboutDialog : public QDialog {
  public:
    explicit AboutDialog(QWidget* parent = 0, Qt::WindowFlags f = 0) {
      ui.setupUi(this);
    }
  private:
    Ui::AboutDialog ui;
  };
  AboutDialog dialog(this);
  dialog.exec();
}

void MainWindow::on_actionIconView_triggered() {
  currentPage()->setViewMode(Fm::FolderView::IconMode);
}

void MainWindow::on_actionCompactView_triggered() {
  currentPage()->setViewMode(Fm::FolderView::CompactMode);
}

void MainWindow::on_actionDetailedList_triggered() {
  currentPage()->setViewMode(Fm::FolderView::DetailedListMode);
}

void MainWindow::on_actionThumbnailView_triggered() {
  currentPage()->setViewMode(Fm::FolderView::ThumbnailMode);
}

void MainWindow::onTabBarCloseRequested(int index) {
  closeTab(index);
}

void MainWindow::closeTab(int index) {
  QWidget* page = ui.stackedWidget->widget(index);
  if(page) {
    ui.stackedWidget->removeWidget(page); // this does not delete the page widget
    delete page;
    // NOTE: we do not remove the tab here.
    // it'll be donoe in onStackedWidgetWidgetRemoved()
  }
}


void MainWindow::onTabBarCurrentChanged(int index) {
  ui.stackedWidget->setCurrentIndex(index);
  updateUIForCurrentPage();
}

void MainWindow::updateStatusBarForCurrentPage() {
  TabPage* tabPage = currentPage();
  QString text = tabPage->statusText(TabPage::StatusTextSelectedFiles);
  if(text.isEmpty())
    text = tabPage->statusText(TabPage::StatusTextNormal);
  ui.statusbar->showMessage(text);

  text = tabPage->statusText(TabPage::StatusTextFSInfo);
  fsInfoLabel->setText(text);
  fsInfoLabel->setVisible(!text.isEmpty());
}

void MainWindow::updateViewMenuForCurrentPage() {
  TabPage* tabPage = currentPage();

  if(tabPage) {
    // update menus. FIXME: should we move this to another method?
    ui.actionShowHidden->setChecked(tabPage->showHidden());

    // view mode
    QAction* modeAction = NULL;

    switch(tabPage->viewMode()) {
    case Fm::FolderView::IconMode:
      modeAction = ui.actionIconView;
      break;

    case Fm::FolderView::CompactMode:
      modeAction = ui.actionCompactView;
      break;

    case Fm::FolderView::DetailedListMode:
      modeAction = ui.actionDetailedList;
      break;

    case Fm::FolderView::ThumbnailMode:
      modeAction = ui.actionThumbnailView;
      break;
    }

    Q_ASSERT(modeAction != NULL);
    modeAction->setChecked(true);

    // sort menu
    QAction* sortActions[Fm::FolderModel::NumOfColumns];
    sortActions[Fm::FolderModel::ColumnFileName] = ui.actionByFileName;
    sortActions[Fm::FolderModel::ColumnFileMTime] = ui.actionByMTime;
    sortActions[Fm::FolderModel::ColumnFileSize] = ui.actionByFileSize;
    sortActions[Fm::FolderModel::ColumnFileType] = ui.actionByFileType;
    sortActions[Fm::FolderModel::ColumnFileOwner] = ui.actionByOwner;
    sortActions[tabPage->sortColumn()]->setChecked(true);

    if(tabPage->sortOrder() == Qt::AscendingOrder)
      ui.actionAscending->setChecked(true);
    else
      ui.actionDescending->setChecked(true);

    ui.actionCaseSensitive->setChecked(tabPage->sortCaseSensitive());
    ui.actionFolderFirst->setChecked(tabPage->sortFolderFirst());
  }
}

void MainWindow::updateUIForCurrentPage() {
  TabPage* tabPage = currentPage();

  if(tabPage) {
    setWindowTitle(tabPage->title());
    pathEntry->setText(tabPage->pathName());
    ui.statusbar->showMessage(tabPage->statusText());
    fsInfoLabel->setText(tabPage->statusText(TabPage::StatusTextFSInfo));
    tabPage->folderView()->childView()->setFocus();

    // update side pane
    ui.sidePane->setCurrentPath(tabPage->path());

    // update back/forward/up toolbar buttons
    ui.actionGoUp->setEnabled(tabPage->canUp());
    ui.actionGoBack->setEnabled(tabPage->canBackward());
    ui.actionGoForward->setEnabled(tabPage->canForward());

    updateViewMenuForCurrentPage();
    updateStatusBarForCurrentPage();
  }
}


void MainWindow::onStackedWidgetWidgetRemoved(int index) {
  // qDebug("onStackedWidgetWidgetRemoved: %d", index);
  // need to remove associated tab from tabBar
  ui.tabBar->removeTab(index);
  if(ui.tabBar->count() == 0) { // this is the last one
    deleteLater(); // destroy the whole window
    // qDebug("delete window");
  }
  else {
    Settings& settings = static_cast<Application*>(qApp)->settings();
    if(!settings.alwaysShowTabs() && ui.tabBar->count() == 1) {
      ui.tabBar->setVisible(false);
    }
  }
}

void MainWindow::onTabPageTitleChanged(QString title) {
  TabPage* tabPage = static_cast<TabPage*>(sender());
  int index = ui.stackedWidget->indexOf(tabPage);

  if(index >= 0)
    ui.tabBar->setTabText(index, title);

  if(tabPage == currentPage())
    setWindowTitle(title);
}

void MainWindow::onTabPageStatusChanged(int type, QString statusText) {
  TabPage* tabPage = static_cast<TabPage*>(sender());
  if(tabPage == currentPage()) {
    switch(type) {
    case TabPage::StatusTextNormal:
    case TabPage::StatusTextSelectedFiles: {
      // FIXME: updating the status text so frequently is a little bit ineffiecient
      QString text = statusText = tabPage->statusText(TabPage::StatusTextSelectedFiles);
      if(text.isEmpty())
        ui.statusbar->showMessage(tabPage->statusText(TabPage::StatusTextNormal));
      else
        ui.statusbar->showMessage(text);
      break;
    }
    case TabPage::StatusTextFSInfo:
      fsInfoLabel->setText(tabPage->statusText(TabPage::StatusTextFSInfo));
      fsInfoLabel->setVisible(!statusText.isEmpty());
      break;
    }
  }
}

void MainWindow::onTabPageOpenDirRequested(FmPath* path, int target) {
  switch(target) {
  case View::OpenInCurrentView:
    chdir(path);
    break;

  case View::OpenInNewTab:
    addTab(path);
    break;

  case View::OpenInNewWindow: {
    Application* app = static_cast<Application*>(qApp);
    MainWindow* newWin = new MainWindow(path);
    // TODO: apply window size from app->settings
    newWin->resize(app->settings().windowWidth(), app->settings().windowHeight());
    newWin->show();
    break;
  }
  }
}

void MainWindow::onTabPageSortFilterChanged() {
  TabPage* tabPage = static_cast<TabPage*>(sender());

  if(tabPage == currentPage()) {
    updateViewMenuForCurrentPage();
  }
}


void MainWindow::onSidePaneChdirRequested(int type, FmPath* path) {
  chdir(path);
}

void MainWindow::loadBookmarksMenu() {
  GList* l = fm_bookmarks_get_all(bookmarks);
  QAction* before = ui.actionAddToBookmarks;

  for(; l; l = l->next) {
    FmBookmarkItem* item = reinterpret_cast<FmBookmarkItem*>(l->data);
    BookmarkAction* action = new BookmarkAction(item);
    connect(action, SIGNAL(triggered(bool)), SLOT(onBookmarkActionTriggered()));
    ui.menu_Bookmarks->insertAction(before, action);
  }

  ui.menu_Bookmarks->insertSeparator(before);
}

void MainWindow::onBookmarksChanged(FmBookmarks* bookmarks, MainWindow* pThis) {
  // delete existing items
  QList<QAction*> actions = pThis->ui.menu_Bookmarks->actions();
  QList<QAction*>::const_iterator it = actions.begin();
  QList<QAction*>::const_iterator last_it = actions.end() - 2;

  while(it != last_it) {
    QAction* action = *it;
    ++it;
    pThis->ui.menu_Bookmarks->removeAction(action);
  }

  pThis->loadBookmarksMenu();
}

void MainWindow::onBookmarkActionTriggered() {
  BookmarkAction* action = static_cast<BookmarkAction*>(sender());
  FmPath* path = action->path();

  if(path)
    chdir(path);
}

void MainWindow::on_actionCopy_triggered() {
  TabPage* page = currentPage();
  FmPathList* paths = page->selectedFilePaths();
  copyFilesToClipboard(paths);
  fm_path_list_unref(paths);
}

void MainWindow::on_actionCut_triggered() {
  TabPage* page = currentPage();
  FmPathList* paths = page->selectedFilePaths();
  cutFilesToClipboard(paths);
  fm_path_list_unref(paths);
}

void MainWindow::on_actionPaste_triggered() {
  pasteFilesFromClipboard(currentPage()->path(), this);
}

void MainWindow::on_actionDelete_triggered() {
  Application* app = static_cast<Application*>(qApp);
  Settings& settings = app->settings();
  TabPage* page = currentPage();
  FmPathList* paths = page->selectedFilePaths();

  if(settings.useTrash())
    FileOperation::trashFiles(paths, settings.confirmDelete(), this);
  else
    FileOperation::deleteFiles(paths, settings.confirmDelete(), this);

  fm_path_list_unref(paths);
}

void MainWindow::on_actionRename_triggered() {
  TabPage* page = currentPage();
  FmFileInfoList* files = page->selectedFiles();

  for(GList* l = fm_file_info_list_peek_head_link(files); l; l = l->next) {
    FmFileInfo* file = FM_FILE_INFO(l->data);
    Fm::renameFile(file, NULL);
  }
  fm_file_info_list_unref(files);
}


void MainWindow::on_actionSelectAll_triggered() {
  currentPage()->selectAll();
}

void MainWindow::on_actionInvertSelection_triggered() {
  currentPage()->invertSelection();
}

void MainWindow::on_actionPreferences_triggered() {
  Application* app = reinterpret_cast<Application*>(qApp);
  app->preferences(QString());
}

/*
void MainWindow::changeEvent(QEvent* event) {
  switch(event->type()) {
    case QEvent::StyleChange:
      break;
  }
  QWidget::changeEvent(event);
}
*/

void MainWindow::onBackForwardContextMenu(QPoint pos) {
  // show a popup menu for browsing history here.
  QToolButton* btn = static_cast<QToolButton*>(sender());
  TabPage* page = currentPage();
  FmPath* currentPath = page->path();
  QMenu menu;
  Q_FOREACH(const BrowseHistoryItem& item, currentPage()->browseHistory()) {
    Fm::Path path = item.path();
    QAction* action = menu.addAction(path.displayName());
    if(fm_path_equal(item.path(), currentPath)) {
      // make the current path bold and checked
      action->setCheckable(true);
      action->setChecked(true);
      QFont font = menu.font();
      font.setBold(true);
      action->setFont(font);
    }
  }
  QAction* selectedAction = menu.exec(btn->mapToGlobal(pos));
  if(selectedAction) {
    int index = menu.actions().indexOf(selectedAction);
    page->jumpToHistory(index);
    updateUIForCurrentPage();
  }
}

void MainWindow::updateFromSettings(Settings& settings) {
  // apply settings

  // menu
  ui.actionDelete->setText(settings.useTrash() ? tr("&Move to Trash") : tr("&Delete"));

  // side pane
  ui.sidePane->setIconSize(QSize(settings.sidePaneIconSize(), settings.sidePaneIconSize()));

  // tabs
  ui.tabBar->setTabsClosable(settings.showTabClose());

  if(!settings.alwaysShowTabs()) {
    ui.tabBar->setVisible(ui.tabBar->count() > 1);
  }

  // all tab pages
  int n = ui.stackedWidget->count();

  for(int i = 0; i < n; ++i) {
    TabPage* page = static_cast<TabPage*>(ui.stackedWidget->widget(i));
    page->updateFromSettings(settings);
  }
}

static const char* su_cmd_subst(char opt, gpointer user_data) {
  return (const char*)user_data;
}

static FmAppCommandParseOption su_cmd_opts[] = {
  { 's', su_cmd_subst },
  { 0, NULL }
};

void MainWindow::on_actionOpenAsRoot_triggered() {
  TabPage* page = currentPage();

  if(page) {
    Application* app = static_cast<Application*>(qApp);
    Settings& settings = app->settings();

    if(!settings.suCommand().isEmpty()) {
      // run the su command
      // FIXME: it's better to get the filename of the current process rather than hard-code pcmanfm-qt here.
      QByteArray suCommand = settings.suCommand().toLocal8Bit();
      char* cmd = NULL;
      QByteArray programCommand = app->applicationFilePath().toLocal8Bit();
      programCommand += " %U";

      if(fm_app_command_parse(suCommand.constData(), su_cmd_opts, &cmd, gpointer(programCommand.constData())) == 0) {
        /* no %s found so just append to it */
        g_free(cmd);
        cmd = g_strconcat(suCommand.constData(), programCommand.constData(), NULL);
      }

      GAppInfo* appInfo = g_app_info_create_from_commandline(cmd, NULL, GAppInfoCreateFlags(0), NULL);
      g_free(cmd);

      if(appInfo) {
        FmPath* cwd = page->path();
        GError* err = NULL;
        char* uri = fm_path_to_uri(cwd);
        GList* uris = g_list_prepend(NULL, uri);

        if(!g_app_info_launch_uris(appInfo, uris, NULL, &err)) {
          QMessageBox::critical(this, tr("Error"), QString::fromUtf8(err->message));
          g_error_free(err);
        }

        g_list_free(uris);
        g_free(uri);
        g_object_unref(appInfo);
      }
    }
    else {
      // show an error message and ask the user to set the command
      QMessageBox::critical(this, tr("Error"), tr("Switch user command is not set."));
      app->preferences("advanced");
    }
  }
}

void MainWindow::on_actionOpenTerminal_triggered() {
  TabPage* page = currentPage();
  if(page) {
    Application* app = static_cast<Application*>(qApp);
    app->openFolderInTerminal(page->path());
  }
}

void MainWindow::onShortcutNextTab() {
  int current = ui.tabBar->currentIndex();
  if(current < ui.tabBar->count() - 1)
    ui.tabBar->setCurrentIndex(current + 1);
  else
    ui.tabBar->setCurrentIndex(0);
}

void MainWindow::onShortcutPrevTab() {
  int current = ui.tabBar->currentIndex();
  if(current > 0)
    ui.tabBar->setCurrentIndex(current - 1);
  else
    ui.tabBar->setCurrentIndex(ui.tabBar->count() - 1);
}

// Switch to nth tab when Alt+n or Ctrl+n is pressed
void MainWindow::onShortcutJumpToTab() {
  QShortcut* shortcut = reinterpret_cast<QShortcut*>(sender());
  QKeySequence seq = shortcut->key();
  int keyValue = seq[0];
  // See the source code of QKeySequence and refer to the method:
  // QString QKeySequencePrivate::encodeString(int key, QKeySequence::SequenceFormat format).
  // Then we know how to test if a key sequence contains a modifier.
  // It's a shame that Qt has no API for this task.

  if((keyValue & Qt::ALT) == Qt::ALT) // test if we have Alt key pressed
    keyValue -= Qt::ALT;
  else if((keyValue & Qt::CTRL) == Qt::CTRL) // test if we have Ctrl key pressed
    keyValue -= Qt::CTRL;

  // now keyValue should contains '0' - '9' only
  int index;
  if(keyValue == '0')
    index = 9;
  else
    index = keyValue - '1';
  if(index < ui.tabBar->count())
    ui.tabBar->setCurrentIndex(index);
}

}
