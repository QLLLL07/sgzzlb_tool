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
    QPushButton* evaluateButton_ = nullptr;
    QPushButton* recommendButton_ = nullptr;
    QPushButton* reloadButton_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QTextEdit* report_ = nullptr;
    QTableWidget* recommendations_ = nullptr;

    QVector<Hero> heroes_;
    Hero slots_[3];
    QVector<QJsonObject> recommendationCache_;
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
        evaluateButton_ = new QPushButton(QStringLiteral("评估"), panel);
        recommendButton_ = new QPushButton(QStringLiteral("推荐 Top 10"), panel);
        clearButton_ = new QPushButton(QStringLiteral("清空槽位"), panel);
        controls->addWidget(evaluateButton_);
        controls->addWidget(recommendButton_);
        controls->addWidget(clearButton_);
        controls->addStretch();
        layout->addLayout(controls);

        tabs_ = new QTabWidget(panel);
        report_ = new QTextEdit(tabs_);
        report_->setReadOnly(true);
        report_->setPlaceholderText(QStringLiteral("选择三名武将后进行评估"));
        tabs_->addTab(report_, QStringLiteral("评估结果"));

        recommendations_ = new QTableWidget(0, 7, tabs_);
        recommendations_->setHorizontalHeaderLabels({QStringLiteral("排名"), QStringLiteral("综合分"),
                                                     QStringLiteral("胜率%"), QStringLiteral("规则分"),
                                                     QStringLiteral("兵种"), QStringLiteral("统御"), QStringLiteral("阵容")});
        recommendations_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        recommendations_->setSelectionBehavior(QAbstractItemView::SelectRows);
        recommendations_->setSelectionMode(QAbstractItemView::SingleSelection);
        recommendations_->verticalHeader()->setVisible(false);
        recommendations_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        recommendations_->horizontalHeader()->setStretchLastSection(true);
        tabs_->addTab(recommendations_, QStringLiteral("推荐 Top-N"));
        layout->addWidget(tabs_, 1);

        connect(evaluateButton_, &QPushButton::clicked, this, [this] { evaluate(); });
        connect(recommendButton_, &QPushButton::clicked, this, [this] { recommend(); });
        connect(clearButton_, &QPushButton::clicked, this, [this] { clearSlots(); });
        connect(recommendations_, &QTableWidget::cellDoubleClicked, this,
                [this](int row, int) { loadRecommendation(row); });
        setActiveSlot(0);
        return panel;
    }

    void setBusy(bool busy) {
        busy_ = busy;
        reloadButton_->setEnabled(!busy);
        evaluateButton_->setEnabled(!busy);
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
            clearSlots();
            applyFilter();
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
        runTask(QStringLiteral("评估中..."), [id0, id1, id2, troop] {
            return callJson([=] { return evaluate_team_troop(id0, id1, id2, troop); });
        }, [this](const QJsonDocument& doc) { showReport(doc.object()); });
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
                << "  场均承伤 " << number(battle.value("avgDmgTaken").toDouble(), 0) << "\n";
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
            const QString values[] = {
                QString::number(row + 1), number(entry.value("total").toDouble()),
                number(entry.value("winRate").toDouble() * 100.0), number(entry.value("rule").toDouble()),
                entry.value("troop").toString(), QString::number(entry.value("cost").toInt()), names.join(QStringLiteral(" / "))
            };
            for (int column = 0; column < 7; ++column)
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
