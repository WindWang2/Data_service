#include "mainwindow.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

void MainWindow::showTutorials() {
    const QStringList roots = {
        QCoreApplication::applicationDirPath(),
        QDir(QCoreApplication::applicationDirPath()).filePath("../.."),
        QDir::current().filePath("paleo_data_service")
    };
    QString root;
    for (const QString& candidate : roots) {
        if (QFileInfo::exists(QDir(candidate).filePath("docs/cpp-reading-guide.md"))) {
            root = candidate;
            break;
        }
    }
    if (root.isEmpty()) {
        showError("未找到案例文档，请保留运行包中的 docs 和 examples 文件夹。");
        return;
    }

    auto* dialog = new QDialog(this);
    dialog->setObjectName("tutorialHelp");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("C++ 使用案例 · 中文注释与完整源代码"));
    dialog->resize(1080, 780);
    auto* layout = new QVBoxLayout(dialog);
    auto* note = new QLabel(QStringLiteral("每个代码案例都是独立的 main.cpp。选择案例阅读或复制；运行参数见“案例目录与运行方法”。修改案例只操作临时工区。"));
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* actions = new QHBoxLayout;
    auto* choice = new QComboBox;
    choice->setObjectName("tutorialChoice");
    choice->addItem(QStringLiteral("入门：怎样读懂一次调用"), "docs/cpp-reading-guide.md");
    choice->addItem(QStringLiteral("案例目录与运行方法"), "examples/getting_started/README.md");
    const QStringList directories = {"01_list_wells", "02_curve_by_name", "03_curve_statistics",
        "04_read_intervals", "05_read_time_depth", "06_feature_table", "07_export_curve_json", "08_edit_temporary_project"};
    const QStringList labels = {QStringLiteral("01 · 列出井与已有资料"), QStringLiteral("02 · 按井名和曲线名读取完整数组"),
        QStringLiteral("03 · 深度筛选与均值计算"), QStringLiteral("04 · 读取分层与岩性"),
        QStringLiteral("05 · 读取时深关系"), QStringLiteral("06 · 多曲线特征表"),
        QStringLiteral("07 · 将完整数据序列化为 JSON"), QStringLiteral("08 · 导入、清理、修改、重开验证")};
    for (int i = 0; i < directories.size(); ++i)
        choice->addItem(labels[i], "examples/getting_started/" + directories[i] + "/main.cpp");
    actions->addWidget(choice, 1);
    auto* copy = new QPushButton(QStringLiteral("复制全文"));
    actions->addWidget(copy);
    layout->addLayout(actions);
    auto* pages = new QStackedWidget;
    auto* article = new QTextBrowser;
    article->setOpenLinks(false);
    auto* code = new QPlainTextEdit;
    code->setReadOnly(true);
    code->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    code->setLineWrapMode(QPlainTextEdit::NoWrap);
    pages->addWidget(article);
    pages->addWidget(code);
    layout->addWidget(pages, 1);
    auto* fileLabel = new QLabel;
    fileLabel->setWordWrap(true);
    layout->addWidget(fileLabel);

    auto load = [root, choice, article, code, pages, fileLabel, dialog] {
        const QString relative = choice->currentData().toString();
        const QString path = QDir(root).filePath(relative);
        QFile file(path);
        const QString content = file.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(file.readAll()) : QStringLiteral("文件暂不可读：") + path;
        dialog->setProperty("copyText", content);
        fileLabel->setText(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()));
        if (relative.endsWith(".md")) {
            article->setMarkdown(content);
            pages->setCurrentWidget(article);
        } else {
            code->setPlainText(content);
            pages->setCurrentWidget(code);
        }
    };
    connect(choice, &QComboBox::currentIndexChanged, dialog, load);
    connect(copy, &QPushButton::clicked, dialog, [dialog] {
        QApplication::clipboard()->setText(dialog->property("copyText").toString());
    });
    // 文档内的案例链接直接选择对应源码，不跳出教程窗口。
    connect(article, &QTextBrowser::anchorClicked, dialog, [choice](const QUrl& url) {
        const QString path = url.toString();
        for (int i = 0; i < choice->count(); ++i) {
            QString known = choice->itemData(i).toString();
            if (path.endsWith(known) || (known.contains("examples/getting_started/")
                && path.endsWith(known.mid(QString("examples/getting_started/").size())))) {
                choice->setCurrentIndex(i);
                break;
            }
        }
    });
    load();
    dialog->show();
}
