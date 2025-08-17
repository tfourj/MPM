#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "actions/actiondialog.h"
#include <QMessageBox>
#include <QMenu>
#include <QClipboard>
#include <QApplication>
#include <QSignalBlocker>

void MainWindow::onAddAction()
{
    ActionDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    const auto res = dlg.getResult();
    UserActionCfg a{ res.customName, res.type, res.expectedMessage, res.exePath };
    m_actions.push_back(a);
    saveActions();
    refreshActionsList();
}

void MainWindow::onEditSelectedAction()
{
    const int row = ui->listWidgetActions->currentRow();
    if (row < 0 || row >= m_actions.size()) return;
    ActionDialog dlg(this);
    ActionDialog::Result init{ m_actions[row].customName, m_actions[row].type, m_actions[row].expectedMessage, m_actions[row].exePath };
    dlg.setInitial(init);
    if (dlg.exec() != QDialog::Accepted) return;
    const auto res = dlg.getResult();
    m_actions[row] = UserActionCfg{ res.customName, res.type, res.expectedMessage, res.exePath };
    saveActions();
    refreshActionsList();
}

void MainWindow::onDeleteSelectedAction()
{
    const int row = ui->listWidgetActions->currentRow();
    if (row < 0 || row >= m_actions.size()) return;
    if (QMessageBox::question(this, "Delete Action", "Are you sure?") != QMessageBox::Yes) return;
    m_actions.removeAt(row);
    saveActions();
    refreshActionsList();
}

void MainWindow::loadActions()
{
    m_actions.clear();
    int size = m_settings.beginReadArray("actions");
    for (int i = 0; i < size; ++i) {
        m_settings.setArrayIndex(i);
        UserActionCfg a;
        a.customName = m_settings.value("name").toString();
        a.expectedMessage = m_settings.value("message", "PRESS").toString();
        a.exePath = m_settings.value("exePath").toString();
        QString typeStr = m_settings.value("type", "Shutdown").toString();
        ActionType t;
        if (!ActionsRegistry::fromString(typeStr, t)) t = ActionType::Shutdown;
        a.type = t;
        if (!a.customName.isEmpty()) m_actions.push_back(a);
    }
    m_settings.endArray();
    refreshActionsList();
}

void MainWindow::saveActions()
{
    m_settings.beginWriteArray("actions");
    for (int i = 0; i < m_actions.size(); ++i) {
        m_settings.setArrayIndex(i);
        m_settings.setValue("name", m_actions[i].customName);
        m_settings.setValue("message", m_actions[i].expectedMessage);
        m_settings.setValue("exePath", m_actions[i].exePath);
        m_settings.setValue("type", ActionsRegistry::toString(m_actions[i].type));
    }
    m_settings.endArray();
}

void MainWindow::refreshActionsList()
{
    if (!ui->listWidgetActions) return;
    const QSignalBlocker blocker(ui->listWidgetActions);
    const int previousRow = ui->listWidgetActions->currentRow();
    ui->listWidgetActions->clear();
    for (const auto &a : m_actions) {
        ui->listWidgetActions->addItem(a.customName);
    }
    int row = previousRow >= 0 ? previousRow : 0;
    if (row >= ui->listWidgetActions->count()) row = ui->listWidgetActions->count() - 1;
    if (row < 0) row = 0;
    if (ui->listWidgetActions->count() > 0) {
        ui->listWidgetActions->setCurrentRow(row);
    }
    updateActionsFooter();
}

void MainWindow::updateActionsFooter()
{
    if (!ui->listWidgetActions) return;
    const int row = ui->listWidgetActions->currentRow();
    if (row >= 0 && row < m_actions.size()) {
        const auto &a = m_actions[row];
        const QString username = ui->lineEditUsername->text().trimmed();
        const QString topic = username.isEmpty()
            ? QString("mqttpowermanager/%1/").arg("<username>") + a.customName
            : QString("mqttpowermanager/%1/").arg(username) + a.customName;
        ui->labelActionsInfo->setText("Expected MQTT message: " + a.expectedMessage +
            " | Topic: " + topic);
    } else {
        ui->labelActionsInfo->setText("Expected MQTT message: ");
    }
}

void MainWindow::onActionsContextMenu(const QPoint &pos)
{
    if (!ui->listWidgetActions) return;
    const int row = ui->listWidgetActions->currentRow();
    if (row < 0 || row >= m_actions.size()) return;
    const auto &a = m_actions[row];

    QMenu menu(this);
    QAction *copyTopic = menu.addAction("Copy MQTT topic");
    QAction *copyPublish = menu.addAction("Copy publish command");

    QAction *chosen = menu.exec(ui->listWidgetActions->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    const QString username = ui->lineEditUsername->text().trimmed();
    const QString topic = username.isEmpty()
        ? QString("mqttpowermanager/%1/").arg("<username>") + a.customName
        : QString("mqttpowermanager/%1/").arg(username) + a.customName;

    QString textToCopy;
    if (chosen == copyTopic) {
        textToCopy = topic;
    } else if (chosen == copyPublish) {
        textToCopy = QString("mosquitto_pub -h %1 -p %2 -t \"%3\" -m \"%4\"")
                         .arg(ui->lineEditHost->text().trimmed())
                         .arg(ui->spinBoxPort->value())
                         .arg(topic)
                         .arg(a.expectedMessage);
    }

    if (!textToCopy.isEmpty()) {
        QClipboard *clipboard = QApplication::clipboard();
        clipboard->setText(textToCopy);
    }
}


