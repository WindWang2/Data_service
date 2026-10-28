#pragma once
#include <QWidget>
#include <QString>
#include <atomic>
#include "paleo_data/service.h"

class QComboBox; class QLineEdit; class QCheckBox; class QPushButton;
class QLabel; class QPlainTextEdit; class QTabWidget; class QStackedWidget;
class QTableWidget; class QThread; class CurvePlot; class ImagePreview; class SeismicTracePlot;

// A runnable tutorial: the displayed C++ and the execution path use the same
// request values and the public DataService interface. This is not a C++ REPL.
class ApiDemoWidget final : public QWidget {
    Q_OBJECT
public:
    explicit ApiDemoWidget(QWidget* parent=nullptr);
    ~ApiDemoWidget() override;
    void setProjectPath(const QString& path);
    void setExample(const QString& kind);
    void runExample();
    bool selectResource(long long id);
    bool isRunning() const;
    bool lastRunSucceeded() const;
    void requestCancel();
    QString lastResultJson() const;
    QString generatedCode() const;
signals:
    void busyChanged(bool busy);
    void runFinished(bool success);
private:
    struct Request;
    struct Result;
    QString projectPath_;
    std::vector<paleo::data::WellSummary> wells_;
    QComboBox *example_=nullptr, *well_=nullptr, *resource_=nullptr;
    QLineEdit *first_=nullptr, *last_=nullptr;
    QCheckBox *processed_=nullptr, *fullResolution_=nullptr;
    QPushButton *run_=nullptr, *cancel_=nullptr, *refresh_=nullptr;
    QLabel *summary_=nullptr, *firstLabel_=nullptr, *lastLabel_=nullptr, *resourceLabel_=nullptr, *wellLabel_=nullptr, *status_=nullptr;
    QPlainTextEdit *code_=nullptr, *json_=nullptr, *schema_=nullptr, *pathPoints_=nullptr;
    QTabWidget *codeTabs_=nullptr, *resultTabs_=nullptr;
    QStackedWidget* visuals_=nullptr;
    QTableWidget* wellTable_=nullptr;
    CurvePlot* curve_=nullptr;
    ImagePreview* image_=nullptr;
    SeismicTracePlot* tracePlot_=nullptr;
    QThread* worker_=nullptr;
    std::atomic_bool cancelRequested_{false};
    bool updating_=false, succeeded_=false;
    void reloadChoices();
    void reloadResources();
    void exampleChanged();
    void parametersChanged();
    void updateCode();
    void invalidateResult();
    void setBusy(bool busy);
    bool requestValues(Request& request, QString& error) const;
    void showResult(const Request& request, const Result& result);
};
