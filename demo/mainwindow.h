#pragma once
#include <QMainWindow>
#include <atomic>
#include "paleo_data/service.h"
class QComboBox;class QListWidget;class QTableWidget;class QLineEdit;class QCheckBox;class QTabWidget;class QLabel;class QPushButton;class QProgressBar;class QThread;class QPlainTextEdit;class QAction;class CurvePlot;class ImagePreview;class QCloseEvent;class ApiDemoWidget;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent=nullptr);
    ~MainWindow() override;
    bool openProjectPath(const QString& path, bool showFailureDialog=true);
    void selectKind(const QString& kind);
    void showApiDemo(const QString& example=QString());
    void showDataManager();
    ApiDemoWidget* apiDemo() const { return apiDemo_; }
protected:
    void closeEvent(QCloseEvent*) override;
private:
    paleo::data::DataService service_;
    std::vector<paleo::data::WellSummary> wells_;
    std::vector<paleo::data::DataEntry> entries_;
    std::vector<paleo::data::SourceEntry> sources_;
    std::vector<paleo::data::IssueEntry> issues_;
    paleo::data::DataTable currentTable_;
    paleo::data::CurveSlice currentCurve_;
    QComboBox *wellBox_=nullptr,*kindBox_=nullptr;
    QCheckBox* relevantWellsBox_=nullptr;
    QLabel* wellSummaryLabel_=nullptr;
    QTabWidget* modeTabs_=nullptr;
    ApiDemoWidget* apiDemo_=nullptr;
    QListWidget* entryList_=nullptr;
    QTableWidget *dataTable_=nullptr,*sourcesTable_=nullptr,*issuesTable_=nullptr;
    QLineEdit *topEdit_=nullptr,*bottomEdit_=nullptr,*filterEdit_=nullptr;
    QCheckBox *processedBox_=nullptr,*unresolvedBox_=nullptr;
    QTabWidget* tabs_=nullptr;
    QLabel *projectLabel_=nullptr,*pageLabel_=nullptr,*detailLabel_=nullptr;
    QPushButton *cancelButton_=nullptr;
    QProgressBar* progress_=nullptr;
    QPlainTextEdit* issueDetails_=nullptr;
    CurvePlot* curvePlot_=nullptr;
    ImagePreview* imagePreview_=nullptr;
    QThread* worker_=nullptr;
    std::atomic_bool cancelled_{false};
    std::vector<QAction*> lockedActions_;
    long long pageOffset_=0,totalRows_=0;
    bool refreshing_=false,closeWhenIdle_=false;
    void setupUi();
    void newProject();
    void chooseOpenProject();
    void importDirectory();
    void refreshAll();
    void showTutorials();
    void refreshWellSelector(long long preferredWellId);
    void refreshEntries();
    void refreshSourcesAndIssues();
    void querySelected();
    void showError(const std::string& error);
    bool ready();
    bool requireIdle();
    std::string currentKind() const;
    long long currentWellId() const;
    const paleo::data::DataEntry* currentEntry() const;
    bool depthRange(double& top,double& bottom);
    void fillTable(QTableWidget*,const paleo::data::DataTable&);
    void runImport(const QString& path,long long sourceId,const paleo::data::ParseOptions& mapping,bool copySources,bool verifyUnchangedFiles=false,int parserThreads=2);
    void finishImport(bool ok,const paleo::data::ImportReport&,const std::string&);
    void setBusy(bool busy);
    void observeWorker();
    void editWell(bool create);
    void assignTimeDepthWell();
    void editInterval(bool create);
    void editCurveValue();
    void cleanCurve();
    void resetCurve();
    void deleteSelected();
    void resolveIssue();
    void reimportSelectedSource();
    void openSource();
    void showDocument();
    void backupDatabase();
    long long selectedSourceId() const;
    void editMap();
    void editSection(bool create);
    void extractSection();
    void editSeismicMetadata();
    void extractTimeSlice();
    void relocateSource();
};
