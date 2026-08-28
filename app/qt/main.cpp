// main.cpp - Native Qt Widgets front end for the squad builder.
#include "api.hpp"

#include <QApplication>
#include <QAbstractItemView>
#include <QByteArray>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QList>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QTextStream>
#include <QThread>
#include <QToolButton>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>
#include <stdexcept>
#include <utility>

namespace {

struct Hero {
    int id = -1;
    QString name;
    QString kingdom;
    QString role;
    int cost = 0;
    QString aptitude;
};

struct Tactic {
    QString name;
    QString type;
    QString category;
    QString quality;
};

struct TaskResult {
    QJsonDocument document;
    QString error;
};

class ApiTask final : public QThread {
public:
    explicit ApiTask(std::function<QJsonDocument()> work, QObject* parent = nullptr)
        : QThread(parent), work_(std::move(work)) {}

    TaskResult result;

protected:
    void run() override {
        try {
            result.document = work_();
        } catch (const std::exception& e) {
            result.error = QString::fromUtf8(e.what());
        } catch (...) {
            result.error = QStringLiteral("发生未知错误");
        }
    }

private:
    std::function<QJsonDocument()> work_;
};

QJsonDocument callJson(const std::function<const char*()>& call) {
    const char* raw = call();
    if (!raw) throw std::runtime_error("核心接口返回空指针");
    const QByteArray json(raw);
    free_string(raw);

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError)
        throw std::runtime_error((QStringLiteral("核心接口返回无效 JSON: ") + error.errorString()).toStdString());
    return doc;
}

QString findDefaultDataFile() {
    const QString file = QStringLiteral("data.json");
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QList<QDir> roots = {
        QDir::current(),
        appDir,
        QDir(appDir.filePath(QStringLiteral(".."))),
        QDir(appDir.filePath(QStringLiteral("../..")))
    };
    for (const QDir& root : roots) {
        const QString candidate = root.filePath(QStringLiteral("data/") + file);
        if (QFileInfo::exists(candidate)) return QDir::cleanPath(candidate);
    }
    return QDir::current().filePath(QStringLiteral("data/") + file);
}

QString number(double value, int decimals = 1) {
    return QString::number(value, 'f', decimals);
}

} // namespace

class MainWindow final : public QMainWindow {
public:
    MainWindow() {
        setWindowTitle(QStringLiteral("三国志·战略版 配将工具"));
        resize(1200, 760);
        buildUi();
        loadData();
    }

protected:
    void closeEvent(QCloseEvent* event) override {
        if (busy_) {
            statusBar()->showMessage(QStringLiteral("当前计算尚未完成，请稍候"));
            event->ignore();
            return;
        }
        event->accept();
    }

private:
    QLineEdit* dataPath_ = nullptr;
    QComboBox* kingdom_ = nullptr;
    QComboBox* role_ = nullptr;
    QComboBox* cost_ = nullptr;
    QLineEdit* search_ = nullptr;
    QTableWidget* heroesTable_ = nullptr;
    QToolButton* slotButtons_[3]{};
    QComboBox* troop_ = nullptr;
    QComboBox* mainHero_ = nullptr;
    QPushButton* evaluateButton_ = nullptr;
    QPushButton* referencesButton_ = nullptr;
    QPushButton* recommendButton_ = nullptr;
    QPushButton* reloadButton_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QTextEdit* report_ = nullptr;
    QTableWidget* recommendations_ = nullptr;

    QComboBox* accountCombo_ = nullptr;
    QLineEdit* accountPath_ = nullptr;
    QPushButton* createAccountButton_ = nullptr;
    QPushButton* accountRecommendButton_ = nullptr;
    QPushButton* saveAccountsButton_ = nullptr;
    QPushButton* loadAccountsButton_ = nullptr;
    QLineEdit* accountHeroSearch_ = nullptr;
    QTableWidget* accountHeroes_ = nullptr;
    QTableWidget* ownedHeroes_ = nullptr;
    QSpinBox* heroStars_ = nullptr;
    QPushButton* addHeroButton_ = nullptr;
    QPushButton* removeHeroButton_ = nullptr;
    QLineEdit* tacticSearch_ = nullptr;
    QTableWidget* availableTactics_ = nullptr;
    QTableWidget* ownedTactics_ = nullptr;
    QPushButton* addTacticButton_ = nullptr;
    QPushButton* removeTacticButton_ = nullptr;

    QVector<Hero> heroes_;
    QVector<Tactic> tactics_;
    Hero slots_[3];
    QVector<QJsonObject> recommendationCache_;
    QJsonObject currentAccount_;
    bool updatingAccounts_ = false;
    bool accountStoreAutoLoaded_ = false;
    int activeSlot_ = 0;
    bool busy_ = false;

    void buildUi() {
        auto* central = new QWidget(this);
        auto* layout = new QVBoxLayout(central);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(8);

        auto* sourceRow = new QHBoxLayout;
        sourceRow->addWidget(new QLabel(QStringLiteral("数据文件"), central));
        dataPath_ = new QLineEdit(findDefaultDataFile(), central);
        dataPath_->setClearButtonEnabled(true);
        sourceRow->addWidget(dataPath_, 1);
        auto* browse = new QToolButton(central);
        browse->setText(QStringLiteral("..."));
        browse->setToolTip(QStringLiteral("选择数据文件"));
        sourceRow->addWidget(browse);
        reloadButton_ = new QPushButton(QStringLiteral("重载数据"), central);
        sourceRow->addWidget(reloadButton_);
        layout->addLayout(sourceRow);

        auto* splitter = new QSplitter(Qt::Horizontal, central);
        splitter->setChildrenCollapsible(false);
        splitter->addWidget(buildHeroPanel());
        splitter->addWidget(buildWorkspace());
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 2);
        layout->addWidget(splitter, 1);
        setCentralWidget(central);
        statusBar()->showMessage(QStringLiteral("准备加载数据..."));

        connect(browse, &QToolButton::clicked, this, [this] {
            const QString path = QFileDialog::getOpenFileName(
                this, QStringLiteral("选择数据文件"), dataPath_->text(),
                QStringLiteral("JSON 文件 (*.json);;所有文件 (*)"));
            if (!path.isEmpty()) dataPath_->setText(path);
        });
        connect(reloadButton_, &QPushButton::clicked, this, [this] { loadData(); });
        connect(kingdom_, &QComboBox::currentTextChanged, this, [this] { applyFilter(); });
        connect(role_, &QComboBox::currentTextChanged, this, [this] { applyFilter(); });
        connect(cost_, &QComboBox::currentTextChanged, this, [this] { applyFilter(); });
        connect(search_, &QLineEdit::textChanged, this, [this] { applyFilter(); });
    }

    QWidget* buildHeroPanel() {
        auto* panel = new QWidget(this);
        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(0, 0, 4, 0);

        auto* filters = new QGroupBox(QStringLiteral("筛选"), panel);
        auto* form = new QFormLayout(filters);
        kingdom_ = new QComboBox(filters);
        kingdom_->addItems({QStringLiteral("全部"), QStringLiteral("魏"), QStringLiteral("蜀"),
                            QStringLiteral("吴"), QStringLiteral("群"), QStringLiteral("晋"), QStringLiteral("汉")});
        role_ = new QComboBox(filters);
        role_->addItems({QStringLiteral("全部"), QStringLiteral("兵刃输出"), QStringLiteral("谋略输出"),
                         QStringLiteral("坦克"), QStringLiteral("治疗"), QStringLiteral("控制"), QStringLiteral("辅助")});
        cost_ = new QComboBox(filters);
        cost_->addItems({QStringLiteral("全部"), QStringLiteral("15"), QStringLiteral("16"),
                         QStringLiteral("17"), QStringLiteral("18"), QStringLiteral("19"), QStringLiteral("20")});
        search_ = new QLineEdit(filters);
        search_->setPlaceholderText(QStringLiteral("武将名称"));
        form->addRow(QStringLiteral("阵营"), kingdom_);
        form->addRow(QStringLiteral("定位"), role_);
        form->addRow(QStringLiteral("统御不超过"), cost_);
        form->addRow(QStringLiteral("搜索"), search_);
        layout->addWidget(filters);

        auto* title = new QLabel(QStringLiteral("武将列表（双击加入当前槽位）"), panel);
        title->setContentsMargins(0, 6, 0, 0);
        layout->addWidget(title);
        heroesTable_ = new QTableWidget(0, 5, panel);
        heroesTable_->setHorizontalHeaderLabels({QStringLiteral("武将"), QStringLiteral("阵营"),
                                                 QStringLiteral("统御"), QStringLiteral("定位"), QStringLiteral("适性")});
        heroesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        heroesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
        heroesTable_->setSelectionMode(QAbstractItemView::SingleSelection);
        heroesTable_->verticalHeader()->setVisible(false);
        heroesTable_->horizontalHeader()->setStretchLastSection(true);
        heroesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        heroesTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        heroesTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        heroesTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        layout->addWidget(heroesTable_, 1);
        connect(heroesTable_, &QTableWidget::cellDoubleClicked, this,
                [this](int row, int) { assignHero(row); });
        return panel;
    }

    QWidget* buildWorkspace() {
        auto* panel = new QWidget(this);
        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(4, 0, 0, 0);

        auto* slotsBox = new QGroupBox(QStringLiteral("阵容槽位"), panel);
        auto* slotsLayout = new QHBoxLayout(slotsBox);
        for (int i = 0; i < 3; ++i) {
            slotButtons_[i] = new QToolButton(slotsBox);
            slotButtons_[i]->setCheckable(true);
            slotButtons_[i]->setToolButtonStyle(Qt::ToolButtonTextOnly);
            slotButtons_[i]->setMinimumWidth(150);
            slotsLayout->addWidget(slotButtons_[i]);
            connect(slotButtons_[i], &QToolButton::clicked, this, [this, i] { setActiveSlot(i); });
        }
        slotsLayout->addStretch();
        layout->addWidget(slotsBox);

        auto* controls = new QHBoxLayout;
        controls->addWidget(new QLabel(QStringLiteral("兵种"), panel));
        troop_ = new QComboBox(panel);
        troop_->addItem(QStringLiteral("自动"), -1);
        troop_->addItem(QStringLiteral("骑兵"), 0);
        troop_->addItem(QStringLiteral("盾兵"), 1);
        troop_->addItem(QStringLiteral("弓兵"), 2);
        troop_->addItem(QStringLiteral("枪兵"), 3);
        controls->addWidget(troop_);
        controls->addWidget(new QLabel(QStringLiteral("主将"), panel));
        mainHero_ = new QComboBox(panel);
        mainHero_->addItem(QStringLiteral("槽位 1"), 0);
        mainHero_->addItem(QStringLiteral("槽位 2"), 1);
        mainHero_->addItem(QStringLiteral("槽位 3"), 2);
        controls->addWidget(mainHero_);
        evaluateButton_ = new QPushButton(QStringLiteral("评估"), panel);
        referencesButton_ = new QPushButton(QStringLiteral("多参考队"), panel);
        recommendButton_ = new QPushButton(QStringLiteral("推荐 Top 10"), panel);
        clearButton_ = new QPushButton(QStringLiteral("清空槽位"), panel);
        controls->addWidget(evaluateButton_);
        controls->addWidget(referencesButton_);
        controls->addWidget(recommendButton_);
        controls->addWidget(clearButton_);
        controls->addStretch();
        layout->addLayout(controls);

        tabs_ = new QTabWidget(panel);
        report_ = new QTextEdit(tabs_);
        report_->setReadOnly(true);
        report_->setPlaceholderText(QStringLiteral("选择三名武将后进行评估"));
        tabs_->addTab(report_, QStringLiteral("评估结果"));

        recommendations_ = new QTableWidget(0, 9, tabs_);
        recommendations_->setHorizontalHeaderLabels({QStringLiteral("排名"), QStringLiteral("综合分"),
                                                     QStringLiteral("胜率%"), QStringLiteral("平局%"),
                                                     QStringLiteral("误差%"), QStringLiteral("规则分"),
                                                     QStringLiteral("兵种"), QStringLiteral("统御"), QStringLiteral("阵容")});
        recommendations_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        recommendations_->setSelectionBehavior(QAbstractItemView::SelectRows);
        recommendations_->setSelectionMode(QAbstractItemView::SingleSelection);
        recommendations_->verticalHeader()->setVisible(false);
        recommendations_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        recommendations_->horizontalHeader()->setStretchLastSection(true);
        tabs_->addTab(recommendations_, QStringLiteral("推荐 Top-N"));
        tabs_->addTab(buildAccountPage(), QStringLiteral("我的账号"));
        layout->addWidget(tabs_, 1);

        connect(evaluateButton_, &QPushButton::clicked, this, [this] { evaluate(); });
        connect(referencesButton_, &QPushButton::clicked, this, [this] { evaluateReferences(); });
        connect(recommendButton_, &QPushButton::clicked, this, [this] { recommend(); });
        connect(clearButton_, &QPushButton::clicked, this, [this] { clearSlots(); });
        connect(recommendations_, &QTableWidget::cellDoubleClicked, this,
                [this](int row, int) { loadRecommendation(row); });
        setActiveSlot(0);
        return panel;
    }

    QTableWidget* makeTable(const QStringList& labels, QWidget* parent) {
        auto* table = new QTableWidget(0, labels.size(), parent);
        table->setHorizontalHeaderLabels(labels);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->verticalHeader()->setVisible(false);
        table->horizontalHeader()->setStretchLastSection(true);
        for (int i = 0; i < labels.size() - 1; ++i)
            table->horizontalHeader()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
        return table;
    }

    QWidget* buildAccountPage() {
        auto* page = new QWidget(tabs_);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(8, 8, 8, 8);

        auto* accountBox = new QGroupBox(QStringLiteral("账号与存档"), page);
        auto* accountLayout = new QHBoxLayout(accountBox);
        accountLayout->addWidget(new QLabel(QStringLiteral("当前账号"), accountBox));
        accountCombo_ = new QComboBox(accountBox);
        accountCombo_->setMinimumWidth(180);
        accountLayout->addWidget(accountCombo_);
        createAccountButton_ = new QPushButton(QStringLiteral("新建账号"), accountBox);
        accountLayout->addWidget(createAccountButton_);
        accountRecommendButton_ = new QPushButton(QStringLiteral("账号推荐 Top 10"), accountBox);
        accountLayout->addWidget(accountRecommendButton_);
        accountLayout->addSpacing(12);
        accountLayout->addWidget(new QLabel(QStringLiteral("存档文件"), accountBox));
        const QString stateRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        accountPath_ = new QLineEdit(QDir(stateRoot).filePath(QStringLiteral("local_accounts.json")), accountBox);
        accountPath_->setClearButtonEnabled(true);
        accountLayout->addWidget(accountPath_, 1);
        saveAccountsButton_ = new QPushButton(QStringLiteral("保存"), accountBox);
        loadAccountsButton_ = new QPushButton(QStringLiteral("导入"), accountBox);
        accountLayout->addWidget(saveAccountsButton_);
        accountLayout->addWidget(loadAccountsButton_);
        layout->addWidget(accountBox);

        auto* splitter = new QSplitter(Qt::Horizontal, page);
        splitter->setChildrenCollapsible(false);

        auto* heroesBox = new QGroupBox(QStringLiteral("我的武将"), splitter);
        auto* heroesLayout = new QVBoxLayout(heroesBox);
        accountHeroSearch_ = new QLineEdit(heroesBox);
        accountHeroSearch_->setPlaceholderText(QStringLiteral("搜索可添加武将"));
        heroesLayout->addWidget(accountHeroSearch_);
        accountHeroes_ = makeTable({QStringLiteral("武将"), QStringLiteral("阵营"), QStringLiteral("统御"), QStringLiteral("定位")}, heroesBox);
        heroesLayout->addWidget(accountHeroes_, 1);
        auto* heroControls = new QHBoxLayout;
        heroControls->addWidget(new QLabel(QStringLiteral("红度"), heroesBox));
        heroStars_ = new QSpinBox(heroesBox);
        heroStars_->setRange(0, 5);
        heroControls->addWidget(heroStars_);
        addHeroButton_ = new QPushButton(QStringLiteral("加入 / 更新"), heroesBox);
        removeHeroButton_ = new QPushButton(QStringLiteral("移除所选"), heroesBox);
        heroControls->addWidget(addHeroButton_);
        heroControls->addWidget(removeHeroButton_);
        heroControls->addStretch();
        heroesLayout->addLayout(heroControls);
        heroesLayout->addWidget(new QLabel(QStringLiteral("已拥有（选择后可修改红度）"), heroesBox));
        ownedHeroes_ = makeTable({QStringLiteral("武将"), QStringLiteral("阵营"), QStringLiteral("红度"), QStringLiteral("定位")}, heroesBox);
        heroesLayout->addWidget(ownedHeroes_, 1);
        splitter->addWidget(heroesBox);

        auto* tacticsBox = new QGroupBox(QStringLiteral("我的传承战法池"), splitter);
        auto* tacticsLayout = new QVBoxLayout(tacticsBox);
        tacticSearch_ = new QLineEdit(tacticsBox);
        tacticSearch_->setPlaceholderText(QStringLiteral("搜索可配装传承战法"));
        tacticsLayout->addWidget(tacticSearch_);
        availableTactics_ = makeTable({QStringLiteral("战法"), QStringLiteral("类型"), QStringLiteral("品质")}, tacticsBox);
        tacticsLayout->addWidget(availableTactics_, 1);
        auto* tacticControls = new QHBoxLayout;
        addTacticButton_ = new QPushButton(QStringLiteral("加入战法池"), tacticsBox);
        removeTacticButton_ = new QPushButton(QStringLiteral("移除所选"), tacticsBox);
        tacticControls->addWidget(addTacticButton_);
        tacticControls->addWidget(removeTacticButton_);
        tacticControls->addStretch();
        tacticsLayout->addLayout(tacticControls);
        tacticsLayout->addWidget(new QLabel(QStringLiteral("账号战法池（账号推荐只会使用这些战法）"), tacticsBox));
        ownedTactics_ = makeTable({QStringLiteral("战法"), QStringLiteral("类型"), QStringLiteral("品质")}, tacticsBox);
        tacticsLayout->addWidget(ownedTactics_, 1);
        splitter->addWidget(tacticsBox);
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 1);
        layout->addWidget(splitter, 1);

        connect(accountCombo_, &QComboBox::currentIndexChanged, this, [this] {
            if (!updatingAccounts_) loadCurrentAccount();
        });
        connect(createAccountButton_, &QPushButton::clicked, this, [this] { createAccount(); });
        connect(accountRecommendButton_, &QPushButton::clicked, this, [this] { recommendAccount(); });
        connect(saveAccountsButton_, &QPushButton::clicked, this, [this] { saveAccounts(); });
        connect(loadAccountsButton_, &QPushButton::clicked, this, [this] { loadAccounts(); });
        connect(accountHeroSearch_, &QLineEdit::textChanged, this, [this] { populateAccountHeroes(); });
        connect(tacticSearch_, &QLineEdit::textChanged, this, [this] { populateAvailableTactics(); });
        connect(addHeroButton_, &QPushButton::clicked, this, [this] { addOrUpdateAccountHero(); });
        connect(removeHeroButton_, &QPushButton::clicked, this, [this] { removeAccountHero(); });
        connect(ownedHeroes_, &QTableWidget::itemSelectionChanged, this, [this] { syncSelectedHeroStars(); });
        connect(addTacticButton_, &QPushButton::clicked, this, [this] { addAccountTactic(); });
        connect(removeTacticButton_, &QPushButton::clicked, this, [this] { removeAccountTactic(); });
        return page;
    }

    void setBusy(bool busy) {
        busy_ = busy;
        reloadButton_->setEnabled(!busy);
        evaluateButton_->setEnabled(!busy);
        referencesButton_->setEnabled(!busy);
        recommendButton_->setEnabled(!busy);
        clearButton_->setEnabled(!busy);
    }

    void runTask(const QString& status, std::function<QJsonDocument()> work,
                 std::function<void(const QJsonDocument&)> completed) {
        if (busy_) return;
        setBusy(true);
        statusBar()->showMessage(status);
        auto* task = new ApiTask(std::move(work), this);
        connect(task, &QThread::finished, this, [this, task, completed = std::move(completed)] {
            setBusy(false);
            const TaskResult result = task->result;
            task->deleteLater();
            if (!result.error.isEmpty()) {
                statusBar()->showMessage(QStringLiteral("操作失败：") + result.error);
                QMessageBox::warning(this, QStringLiteral("操作失败"), result.error);
                return;
            }
            completed(result.document);
        });
        task->start();
    }

    void loadData() {
        const QString path = QDir::cleanPath(dataPath_->text().trimmed());
        if (path.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("请指定数据文件"));
            return;
        }
        runTask(QStringLiteral("正在加载数据..."), [path] {
            const QByteArray nativePath = QFile::encodeName(path);
            const QJsonDocument loadResult = callJson([&] { return load_data(nativePath.constData()); });
            QJsonObject result;
            result.insert(QStringLiteral("load"), loadResult.object());
            result.insert(QStringLiteral("heroes"), callJson([] { return get_heroes(); }).array());
            result.insert(QStringLiteral("tactics"), callJson([] { return get_tactics(); }).array());
            return QJsonDocument(result);
        }, [this, path](const QJsonDocument& doc) {
            const QJsonObject result = doc.object();
            const QJsonArray listedHeroes = result.value(QStringLiteral("heroes")).toArray();
            if (listedHeroes.isEmpty()) {
                statusBar()->showMessage(QStringLiteral("数据加载失败：未得到武将列表"));
                QMessageBox::warning(this, QStringLiteral("数据加载失败"),
                                     QStringLiteral("核心未返回可用的武将列表。"));
                return;
            }
            heroes_.clear();
            for (const QJsonValue& value : listedHeroes) {
                const QJsonObject item = value.toObject();
                Hero hero;
                hero.id = item.value(QStringLiteral("id")).toInt(-1);
                hero.name = item.value(QStringLiteral("name")).toString();
                hero.kingdom = item.value(QStringLiteral("kingdom")).toString();
                hero.role = item.value(QStringLiteral("role")).toString();
                hero.cost = item.value(QStringLiteral("cost")).toInt();
                hero.aptitude = item.value(QStringLiteral("aptitude")).toString();
                if (hero.id >= 0) heroes_.push_back(hero);
            }
            tactics_.clear();
            for (const QJsonValue& value : result.value(QStringLiteral("tactics")).toArray()) {
                const QJsonObject item = value.toObject();
                Tactic tactic;
                tactic.name = item.value(QStringLiteral("name")).toString();
                tactic.type = item.value(QStringLiteral("type")).toString();
                tactic.category = item.value(QStringLiteral("category")).toString();
                tactic.quality = item.value(QStringLiteral("quality")).toString();
                if (!tactic.name.isEmpty()) tactics_.push_back(tactic);
            }
            clearSlots();
            applyFilter();
            populateAccountHeroes();
            populateAvailableTactics();
            if (!accountStoreAutoLoaded_ && QFileInfo::exists(accountPath_->text().trimmed())) autoLoadAccounts();
            else refreshAccountList();
            dataPath_->setText(path);
            const QJsonObject load = result.value(QStringLiteral("load")).toObject();
            if (load.value(QStringLiteral("ok")).toBool()) {
                statusBar()->showMessage(QStringLiteral("数据已加载：%1 名武将").arg(heroes_.size()));
            } else {
                statusBar()->showMessage(QStringLiteral("外部数据加载失败，已使用内置回退：%1")
                    .arg(load.value(QStringLiteral("error")).toString()));
            }
        });
    }

    void applyFilter() {
        if (!heroesTable_) return;
        heroesTable_->setRowCount(0);
        const QString kingdom = kingdom_->currentText();
        const QString role = role_->currentText();
        const QString query = search_->text().trimmed();
        const int maxCost = cost_->currentText() == QStringLiteral("全部") ? 999 : cost_->currentText().toInt();
        for (const Hero& hero : heroes_) {
            if (kingdom != QStringLiteral("全部") && hero.kingdom != kingdom) continue;
            if (role != QStringLiteral("全部") && hero.role != role) continue;
            if (hero.cost > maxCost) continue;
            if (!query.isEmpty() && !hero.name.contains(query, Qt::CaseInsensitive)) continue;
            const int row = heroesTable_->rowCount();
            heroesTable_->insertRow(row);
            auto* name = new QTableWidgetItem(hero.name);
            name->setData(Qt::UserRole, hero.id);
            heroesTable_->setItem(row, 0, name);
            heroesTable_->setItem(row, 1, new QTableWidgetItem(hero.kingdom));
            heroesTable_->setItem(row, 2, new QTableWidgetItem(QString::number(hero.cost)));
            heroesTable_->setItem(row, 3, new QTableWidgetItem(hero.role));
            heroesTable_->setItem(row, 4, new QTableWidgetItem(hero.aptitude));
        }
    }

    const Hero* heroById(int id) const {
        for (const Hero& hero : heroes_) if (hero.id == id) return &hero;
        return nullptr;
    }

    void setActiveSlot(int slot) {
        activeSlot_ = slot;
        for (int i = 0; i < 3; ++i) {
            const QString name = slots_[i].id >= 0 ? slots_[i].name : QStringLiteral("空");
            slotButtons_[i]->setText(QStringLiteral("槽%1: %2").arg(i + 1).arg(name));
            slotButtons_[i]->setChecked(i == activeSlot_);
        }
    }

    void assignHero(int row) {
        const auto* item = heroesTable_->item(row, 0);
        if (!item) return;
        const Hero* hero = heroById(item->data(Qt::UserRole).toInt());
        if (!hero) return;
        slots_[activeSlot_] = *hero;
        setActiveSlot(activeSlot_);
        statusBar()->showMessage(QStringLiteral("已将 %1 放入槽%2").arg(hero->name).arg(activeSlot_ + 1));
    }

    void clearSlots() {
        for (Hero& slot : slots_) slot = Hero{};
        setActiveSlot(0);
    }

    QString currentAccountId() const {
        return accountCombo_ ? accountCombo_->currentData().toString() : QString();
    }

    void populateAccountHeroes() {
        if (!accountHeroes_) return;
        const QString query = accountHeroSearch_->text().trimmed();
        accountHeroes_->setRowCount(0);
        for (const Hero& hero : heroes_) {
            if (!query.isEmpty() && !hero.name.contains(query, Qt::CaseInsensitive)) continue;
            const int row = accountHeroes_->rowCount();
            accountHeroes_->insertRow(row);
            auto* name = new QTableWidgetItem(hero.name);
            name->setData(Qt::UserRole, hero.id);
            accountHeroes_->setItem(row, 0, name);
            accountHeroes_->setItem(row, 1, new QTableWidgetItem(hero.kingdom));
            accountHeroes_->setItem(row, 2, new QTableWidgetItem(QString::number(hero.cost)));
            accountHeroes_->setItem(row, 3, new QTableWidgetItem(hero.role));
        }
    }

    void populateAvailableTactics() {
        if (!availableTactics_) return;
        const QString query = tacticSearch_->text().trimmed();
        availableTactics_->setRowCount(0);
        for (const Tactic& tactic : tactics_) {
            if (tactic.category != QStringLiteral("传承")) continue;
            if (!query.isEmpty() && !tactic.name.contains(query, Qt::CaseInsensitive)) continue;
            const int row = availableTactics_->rowCount();
            availableTactics_->insertRow(row);
            auto* name = new QTableWidgetItem(tactic.name);
            name->setData(Qt::UserRole, tactic.name);
            availableTactics_->setItem(row, 0, name);
            availableTactics_->setItem(row, 1, new QTableWidgetItem(tactic.type));
            availableTactics_->setItem(row, 2, new QTableWidgetItem(tactic.quality));
        }
    }

    void refreshAccountView(const QJsonObject& account) {
        currentAccount_ = account;
        if (!ownedHeroes_ || !ownedTactics_) return;
        ownedHeroes_->setRowCount(0);
        for (const QJsonValue& value : account.value(QStringLiteral("heroes")).toArray()) {
            const QJsonObject item = value.toObject();
            const Hero* hero = heroById(item.value(QStringLiteral("heroId")).toInt(-1));
            if (!hero) continue;
            const int row = ownedHeroes_->rowCount();
            ownedHeroes_->insertRow(row);
            auto* name = new QTableWidgetItem(hero->name);
            name->setData(Qt::UserRole, hero->id);
            ownedHeroes_->setItem(row, 0, name);
            ownedHeroes_->setItem(row, 1, new QTableWidgetItem(hero->kingdom));
            ownedHeroes_->setItem(row, 2, new QTableWidgetItem(QString::number(item.value(QStringLiteral("stars")).toInt())));
            ownedHeroes_->setItem(row, 3, new QTableWidgetItem(hero->role));
        }
        ownedTactics_->setRowCount(0);
        for (const QJsonValue& value : account.value(QStringLiteral("tactics")).toArray()) {
            const QJsonObject item = value.toObject();
            const int row = ownedTactics_->rowCount();
            ownedTactics_->insertRow(row);
            auto* name = new QTableWidgetItem(item.value(QStringLiteral("name")).toString());
            name->setData(Qt::UserRole, item.value(QStringLiteral("name")).toString());
            ownedTactics_->setItem(row, 0, name);
            ownedTactics_->setItem(row, 1, new QTableWidgetItem(item.value(QStringLiteral("type")).toString()));
            ownedTactics_->setItem(row, 2, new QTableWidgetItem(item.value(QStringLiteral("quality")).toString()));
        }
        heroStars_->setValue(0);
    }

    void refreshAccountList(const QString& selectedId = QString()) {
        runTask(QStringLiteral("正在读取账号..."), [] {
            return callJson([] { return list_local_accounts(); });
        }, [this, selectedId](const QJsonDocument& doc) {
            if (!doc.isArray()) return;
            const QString desired = selectedId.isEmpty() ? currentAccountId() : selectedId;
            updatingAccounts_ = true;
            accountCombo_->clear();
            for (const QJsonValue& value : doc.array()) {
                const QJsonObject account = value.toObject();
                accountCombo_->addItem(account.value(QStringLiteral("name")).toString(), account.value(QStringLiteral("id")).toString());
            }
            int index = accountCombo_->findData(desired);
            if (index < 0 && accountCombo_->count() > 0) index = 0;
            accountCombo_->setCurrentIndex(index);
            updatingAccounts_ = false;
            if (index >= 0) loadCurrentAccount();
            else refreshAccountView(QJsonObject());
        });
    }

    void loadCurrentAccount() {
        const QString id = currentAccountId();
        if (id.isEmpty()) { refreshAccountView(QJsonObject()); return; }
        runTask(QStringLiteral("正在读取账号详情..."), [id] {
            const QByteArray bytes = id.toUtf8();
            return callJson([&] { return get_local_account(bytes.constData()); });
        }, [this](const QJsonDocument& doc) {
            const QJsonObject account = doc.object();
            if (account.value(QStringLiteral("ok")).toBool()) refreshAccountView(account);
        });
    }

    bool prepareAccountPath() {
        const QString path = accountPath_->text().trimmed();
        if (path.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("无法保存"), QStringLiteral("请指定账号存档文件。"));
            return false;
        }
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
            QMessageBox::warning(this, QStringLiteral("无法保存"), QStringLiteral("无法创建账号存档目录。"));
            return false;
        }
        return true;
    }

    void createAccount() {
        bool accepted = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("新建账号"), QStringLiteral("账号名称"),
                                                   QLineEdit::Normal, QStringLiteral("我的账号"), &accepted).trimmed();
        if (!accepted) return;
        if (!prepareAccountPath()) return;
        runTask(QStringLiteral("正在创建账号..."), [name, path = accountPath_->text().trimmed()] {
            const QByteArray nameBytes = name.toUtf8();
            const QByteArray pathBytes = QFile::encodeName(path);
            const QJsonDocument created = callJson([&] { return create_local_account(nameBytes.constData()); });
            const QJsonDocument saved = callJson([&] { return save_local_accounts(pathBytes.constData()); });
            QJsonObject result = created.object();
            result.insert(QStringLiteral("saved"), saved.object().value(QStringLiteral("ok")).toBool());
            return QJsonDocument(result);
        }, [this](const QJsonDocument& doc) {
            const QJsonObject account = doc.object();
            if (!account.value(QStringLiteral("ok")).toBool()) return;
            refreshAccountList(account.value(QStringLiteral("id")).toString());
            statusBar()->showMessage(QStringLiteral("已创建账号 %1").arg(account.value(QStringLiteral("name")).toString()));
        });
    }

    void saveAccounts() {
        if (!prepareAccountPath()) return;
        const QString path = accountPath_->text().trimmed();
        runTask(QStringLiteral("正在保存账号..."), [path] {
            const QByteArray bytes = QFile::encodeName(path);
            return callJson([&] { return save_local_accounts(bytes.constData()); });
        }, [this](const QJsonDocument& doc) {
            const QJsonObject result = doc.object();
            statusBar()->showMessage(result.value(QStringLiteral("ok")).toBool()
                ? QStringLiteral("账号已保存") : QStringLiteral("账号保存失败：") + result.value(QStringLiteral("error")).toString());
        });
    }

    void loadAccounts() {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("导入账号存档"), accountPath_->text(),
                                                          QStringLiteral("JSON 文件 (*.json);;所有文件 (*)"));
        if (path.isEmpty()) return;
        accountPath_->setText(path);
        accountStoreAutoLoaded_ = true;
        runTask(QStringLiteral("正在导入账号..."), [path] {
            const QByteArray bytes = QFile::encodeName(path);
            return callJson([&] { return load_local_accounts(bytes.constData()); });
        }, [this](const QJsonDocument& doc) {
            const QJsonObject result = doc.object();
            if (!result.value(QStringLiteral("ok")).toBool()) {
                QMessageBox::warning(this, QStringLiteral("导入失败"), result.value(QStringLiteral("error")).toString());
                return;
            }
            refreshAccountList();
            statusBar()->showMessage(QStringLiteral("账号存档已导入"));
        });
    }

    void autoLoadAccounts() {
        const QString path = accountPath_->text().trimmed();
        accountStoreAutoLoaded_ = true;
        runTask(QStringLiteral("正在恢复本地账号..."), [path] {
            const QByteArray bytes = QFile::encodeName(path);
            return callJson([&] { return load_local_accounts(bytes.constData()); });
        }, [this](const QJsonDocument& doc) {
            const QJsonObject result = doc.object();
            if (result.value(QStringLiteral("ok")).toBool()) {
                refreshAccountList();
                statusBar()->showMessage(QStringLiteral("已恢复本地账号存档"));
            } else {
                refreshAccountList();
            }
        });
    }

    void persistAccountMutation(const QJsonDocument& doc) {
        const QJsonObject account = doc.object();
        if (!account.value(QStringLiteral("ok")).toBool()) {
            QMessageBox::warning(this, QStringLiteral("账号更新失败"), account.value(QStringLiteral("error")).toString());
            return;
        }
        refreshAccountView(account);
        statusBar()->showMessage(QStringLiteral("账号已更新并保存"));
    }

    void addOrUpdateAccountHero() {
        const QString accountId = currentAccountId();
        const int row = accountHeroes_->currentRow();
        if (accountId.isEmpty() || row < 0 || !accountHeroes_->item(row, 0)) {
            statusBar()->showMessage(QStringLiteral("请先创建账号并选择一名武将"));
            return;
        }
        if (!prepareAccountPath()) return;
        const int heroId = accountHeroes_->item(row, 0)->data(Qt::UserRole).toInt();
        const int stars = heroStars_->value();
        const QString path = accountPath_->text().trimmed();
        runTask(QStringLiteral("正在更新账号武将..."), [accountId, heroId, stars, path] {
            const QByteArray id = accountId.toUtf8();
            const QByteArray savePath = QFile::encodeName(path);
            const QJsonDocument result = callJson([&] { return set_local_account_hero(id.constData(), heroId, stars, 1); });
            callJson([&] { return save_local_accounts(savePath.constData()); });
            return result;
        }, [this](const QJsonDocument& doc) { persistAccountMutation(doc); });
    }

    void removeAccountHero() {
        const QString accountId = currentAccountId();
        const int row = ownedHeroes_->currentRow();
        if (accountId.isEmpty() || row < 0 || !ownedHeroes_->item(row, 0)) return;
        if (!prepareAccountPath()) return;
        const int heroId = ownedHeroes_->item(row, 0)->data(Qt::UserRole).toInt();
        const QString path = accountPath_->text().trimmed();
        runTask(QStringLiteral("正在移除账号武将..."), [accountId, heroId, path] {
            const QByteArray id = accountId.toUtf8();
            const QByteArray savePath = QFile::encodeName(path);
            const QJsonDocument result = callJson([&] { return set_local_account_hero(id.constData(), heroId, 0, 0); });
            callJson([&] { return save_local_accounts(savePath.constData()); });
            return result;
        }, [this](const QJsonDocument& doc) { persistAccountMutation(doc); });
    }

    void syncSelectedHeroStars() {
        const int row = ownedHeroes_->currentRow();
        if (row >= 0 && ownedHeroes_->item(row, 2)) heroStars_->setValue(ownedHeroes_->item(row, 2)->text().toInt());
    }

    void addAccountTactic() {
        const QString accountId = currentAccountId();
        const int row = availableTactics_->currentRow();
        if (accountId.isEmpty() || row < 0 || !availableTactics_->item(row, 0)) {
            statusBar()->showMessage(QStringLiteral("请先创建账号并选择一项传承战法"));
            return;
        }
        if (!prepareAccountPath()) return;
        const QString tactic = availableTactics_->item(row, 0)->data(Qt::UserRole).toString();
        const QString path = accountPath_->text().trimmed();
        runTask(QStringLiteral("正在加入战法池..."), [accountId, tactic, path] {
            const QByteArray id = accountId.toUtf8(), name = tactic.toUtf8(), savePath = QFile::encodeName(path);
            const QJsonDocument result = callJson([&] { return set_local_account_tactic(id.constData(), name.constData(), 1); });
            callJson([&] { return save_local_accounts(savePath.constData()); });
            return result;
        }, [this](const QJsonDocument& doc) { persistAccountMutation(doc); });
    }

    void removeAccountTactic() {
        const QString accountId = currentAccountId();
        const int row = ownedTactics_->currentRow();
        if (accountId.isEmpty() || row < 0 || !ownedTactics_->item(row, 0)) return;
        if (!prepareAccountPath()) return;
        const QString tactic = ownedTactics_->item(row, 0)->data(Qt::UserRole).toString();
        const QString path = accountPath_->text().trimmed();
        runTask(QStringLiteral("正在移除战法池..."), [accountId, tactic, path] {
            const QByteArray id = accountId.toUtf8(), name = tactic.toUtf8(), savePath = QFile::encodeName(path);
            const QJsonDocument result = callJson([&] { return set_local_account_tactic(id.constData(), name.constData(), 0); });
            callJson([&] { return save_local_accounts(savePath.constData()); });
            return result;
        }, [this](const QJsonDocument& doc) { persistAccountMutation(doc); });
    }

    void recommendAccount() {
        const QString accountId = currentAccountId();
        if (accountId.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("请先创建或导入账号"));
            return;
        }
        runTask(QStringLiteral("正在按账号武将与战法池推荐..."), [accountId] {
            const QByteArray id = accountId.toUtf8();
            return callJson([&] { return recommend_account_teams(id.constData(), 10); });
        }, [this](const QJsonDocument& doc) {
            const QJsonObject result = doc.object();
            if (!result.value(QStringLiteral("ok")).toBool()) {
                QMessageBox::warning(this, QStringLiteral("账号推荐失败"), result.value(QStringLiteral("error")).toString());
                return;
            }
            showRecommendations(result.value(QStringLiteral("recommendations")).toArray());
            statusBar()->showMessage(QStringLiteral("账号推荐完成：使用 %1 名武将、%2 项战法池")
                .arg(currentAccount_.value(QStringLiteral("heroes")).toArray().size())
                .arg(result.value(QStringLiteral("tacticPoolSize")).toInt()));
        });
    }

    bool completeTeam() const {
        return slots_[0].id >= 0 && slots_[1].id >= 0 && slots_[2].id >= 0;
    }

    void evaluate() {
        if (!completeTeam()) {
            statusBar()->showMessage(QStringLiteral("请先在三个槽位选满武将"));
            return;
        }
        const int id0 = slots_[0].id;
        const int id1 = slots_[1].id;
        const int id2 = slots_[2].id;
        const int troop = troop_->currentData().toInt();
        const int mainIdx = mainHero_->currentData().toInt();
        runTask(QStringLiteral("评估中..."), [id0, id1, id2, troop, mainIdx] {
            return callJson([=] { return evaluate_team_main(id0, id1, id2, troop, mainIdx); });
        }, [this](const QJsonDocument& doc) { showReport(doc.object()); });
    }

    void evaluateReferences() {
        if (!completeTeam()) {
            statusBar()->showMessage(QStringLiteral("请先在三个槽位选满武将"));
            return;
        }
        const int id0 = slots_[0].id;
        const int id1 = slots_[1].id;
        const int id2 = slots_[2].id;
        const int troop = troop_->currentData().toInt();
        const int mainIdx = mainHero_->currentData().toInt();
        runTask(QStringLiteral("多参考队评估中..."), [id0, id1, id2, troop, mainIdx] {
            return callJson([=] { return evaluate_team_references(id0, id1, id2, troop, mainIdx, 200); });
        }, [this](const QJsonDocument& doc) {
            const QJsonObject result = doc.object();
            if (!result.value(QStringLiteral("ok")).toBool()) {
                report_->setPlainText(QStringLiteral("多参考队评估失败：") + result.value(QStringLiteral("error")).toString());
                statusBar()->showMessage(QStringLiteral("多参考队评估失败"));
                return;
            }
            QString text;
            QTextStream out(&text);
            out << "多参考队评估：" << result.value("referenceCount").toInt() << " 套\n"
                << "平均胜率 " << number(result.value("averageWinRate").toDouble() * 100.0)
                << "%   最低胜率 " << number(result.value("minimumWinRate").toDouble() * 100.0) << "%\n\n";
            for (const QJsonValue& value : result.value("references").toArray()) {
                const QJsonObject row = value.toObject();
                out << row.value("name").toString() << "：胜率 " << number(row.value("winRate").toDouble() * 100.0)
                    << "%，平局 " << number(row.value("drawRate").toDouble() * 100.0)
                    << "%，95%区间 [" << number(row.value("winRateCi95Low").toDouble() * 100.0)
                    << "%, " << number(row.value("winRateCi95High").toDouble() * 100.0) << "%]\n";
            }
            out << "\n说明：参考队是固定模板，结果用于横向校验，不代表完整赛季环境。";
            report_->setPlainText(text);
            tabs_->setCurrentIndex(0);
            statusBar()->showMessage(QStringLiteral("多参考队评估完成"));
        });
    }

    void showReport(const QJsonObject& report) {
        if (!report.value(QStringLiteral("ok")).toBool()) {
            const QString error = report.value(QStringLiteral("error")).toString();
            report_->setPlainText(QStringLiteral("评估失败：") + error);
            statusBar()->showMessage(QStringLiteral("评估失败"));
            return;
        }
        const QJsonObject rule = report.value(QStringLiteral("ruleScore")).toObject();
        const QJsonObject battle = report.value(QStringLiteral("battle")).toObject();
        const QJsonArray names = report.value(QStringLiteral("names")).toArray();
        QStringList nameList;
        for (const QJsonValue& name : names) nameList << name.toString();

        QString text;
        QTextStream out(&text);
        out << "综合评分：" << number(report.value("total").toDouble()) << " / 100\n";
        out << "阵容：" << nameList.join(QStringLiteral(" / "))
            << "   主将：" << report.value("mainName").toString()
            << "   兵种：" << report.value("troop").toString()
            << "   统御：" << report.value("cost").toInt() << "/20\n\n";
        out << "【评分分解】\n  兵种适性 " << number(rule.value("aptitude").toDouble(), 0)
            << "  国家 " << number(rule.value("kingdom").toDouble(), 0)
            << "  角色覆盖 " << number(rule.value("role").toDouble(), 0)
            << "  统御 " << number(rule.value("cost").toDouble(), 0)
            << "  -> 规则分 " << number(rule.value("total").toDouble()) << "\n\n";
        out << "【武将定位】\n";
        for (const QJsonValue& value : report.value("roles").toArray()) {
            const QJsonObject item = value.toObject();
            out << "  " << item.value("name").toString() << "（" << item.value("role").toString()
                << "）：" << item.value("advice").toString() << "\n";
        }
        out << "\n【战法配装】\n";
        const QJsonArray tactics = report.value("tactics").toArray();
        for (int i = 0; i < names.size(); ++i) {
            QStringList tacticsForHero;
            if (i < tactics.size()) for (const QJsonValue& tactic : tactics.at(i).toArray()) tacticsForHero << tactic.toString();
            out << "  " << names.at(i).toString() << "："
                << (tacticsForHero.isEmpty() ? QStringLiteral("（无）") : tacticsForHero.join(QStringLiteral("、"))) << "\n";
        }
        if (battle.value("sims").toInt() > 0) {
            out << "\n【战斗统计】（vs 桃园）\n  模拟 " << battle.value("sims").toInt()
                << " 场：胜率 " << number(battle.value("winRate").toDouble() * 100.0)
                << "%   场均输出 " << number(battle.value("avgDmgDealt").toDouble(), 0)
                << "  场均承伤 " << number(battle.value("avgDmgTaken").toDouble(), 0) << "\n"
                << "  平局率 " << number(battle.value("drawRate").toDouble() * 100.0)
                << "%   胜率标准误 +/-" << number(battle.value("winRateStdError").toDouble() * 100.0) << "%\n"
                << "  95%区间 [" << number(battle.value("winRateCi95Low").toDouble() * 100.0)
                << "%, " << number(battle.value("winRateCi95High").toDouble() * 100.0)
                << "%]   种子 " << QString::number(static_cast<qulonglong>(battle.value("seed").toDouble())) << "\n"
                << "  说明：结果仅表示当前简化规则下对固定桃园参考队的比较。\n";
        }
        const QJsonArray synergies = report.value("synergies").toArray();
        if (!synergies.isEmpty()) {
            out << "\n【战法联动】\n";
            for (const QJsonValue& value : synergies) out << "  - " << value.toString() << "\n";
        }
        const QJsonArray advice = report.value("advice").toArray();
        if (!advice.isEmpty()) {
            out << "\n【队伍建议】\n";
            for (const QJsonValue& value : advice) out << "  - " << value.toString() << "\n";
        }
        report_->setPlainText(text);
        tabs_->setCurrentIndex(0);
        statusBar()->showMessage(QStringLiteral("评估完成"));
    }

    void recommend() {
        runTask(QStringLiteral("推荐计算中..."), [] {
            return callJson([] { return recommend_teams(10); });
        }, [this](const QJsonDocument& doc) {
            if (!doc.isArray()) {
                statusBar()->showMessage(QStringLiteral("推荐失败：结果格式错误"));
                QMessageBox::warning(this, QStringLiteral("推荐失败"),
                                     QStringLiteral("核心返回的推荐结果格式错误。"));
                return;
            }
            showRecommendations(doc.array());
        });
    }

    void showRecommendations(const QJsonArray& entries) {
        recommendationCache_.clear();
        recommendations_->setRowCount(0);
        for (const QJsonValue& value : entries) {
            const QJsonObject entry = value.toObject();
            const int row = recommendations_->rowCount();
            recommendationCache_.push_back(entry);
            recommendations_->insertRow(row);
            QStringList names;
            for (const QJsonValue& id : entry.value("heroes").toArray()) {
                const Hero* hero = heroById(id.toInt(-1));
                names << (hero ? hero->name : QStringLiteral("未知"));
            }
            if (!names.isEmpty()) names[0] = QStringLiteral("主") + names[0];
            const QString values[] = {
                QString::number(row + 1), number(entry.value("total").toDouble()),
                number(entry.value("winRate").toDouble() * 100.0),
                number(entry.value("drawRate").toDouble() * 100.0),
                number(entry.value("winRateStdError").toDouble() * 100.0),
                number(entry.value("rule").toDouble()),
                entry.value("troop").toString(), QString::number(entry.value("cost").toInt()), names.join(QStringLiteral(" / "))
            };
            for (int column = 0; column < 9; ++column)
                recommendations_->setItem(row, column, new QTableWidgetItem(values[column]));
        }
        tabs_->setCurrentIndex(1);
        statusBar()->showMessage(QStringLiteral("推荐完成：Top %1 队伍，双击可载入并评估").arg(entries.size()));
    }

    void loadRecommendation(int row) {
        if (row < 0 || row >= recommendationCache_.size()) return;
        const QJsonArray ids = recommendationCache_.at(row).value(QStringLiteral("heroes")).toArray();
        if (ids.size() != 3) return;
        for (int i = 0; i < 3; ++i) {
            const Hero* hero = heroById(ids.at(i).toInt(-1));
            if (!hero) return;
            slots_[i] = *hero;
        }
        mainHero_->setCurrentIndex(0);
        setActiveSlot(0);
        evaluate();
    }
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    return app.exec();
}
