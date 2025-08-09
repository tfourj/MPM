#include "actiondialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFileDialog>

static QStringList actionTypeStrings()
{
    return {"Shutdown", "Restart", "Suspend", "Sleep", "OpenExe", "Lock"};
}

ActionDialog::ActionDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Configure Action");
    buildUi();
}

void ActionDialog::buildUi()
{
    auto *layout = new QVBoxLayout(this);

    // Name
    {
        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel("Name (MQTT suffix):", this));
        m_nameEdit = new QLineEdit(this);
        row->addWidget(m_nameEdit);
        layout->addLayout(row);
    }

    // Type
    {
        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel("Type:", this));
        m_typeCombo = new QComboBox(this);
        m_typeCombo->addItems(actionTypeStrings());
        row->addWidget(m_typeCombo);
        layout->addLayout(row);
        connect(m_typeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &ActionDialog::onTypeChanged);
    }

    // Message
    {
        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel("Expected MQTT message:", this));
        m_msgEdit = new QLineEdit(this);
        m_msgEdit->setText("PRESS");
        row->addWidget(m_msgEdit);
        layout->addLayout(row);
    }

    // Exe row
    {
        auto *row = new QHBoxLayout();
        m_exeRow = new QWidget(this);
        auto *inner = new QHBoxLayout(m_exeRow);
        inner->setContentsMargins(0,0,0,0);
        inner->addWidget(new QLabel("Executable:", this));
        m_exeEdit = new QLineEdit(this);
        inner->addWidget(m_exeEdit);
        auto *browse = new QPushButton("Browse...", this);
        inner->addWidget(browse);
        layout->addWidget(m_exeRow);
        connect(browse, &QPushButton::clicked, this, &ActionDialog::onBrowseExe);
    }

    // Buttons
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    onTypeChanged(m_typeCombo->currentIndex());
}

void ActionDialog::onTypeChanged(int)
{
    const QString typeStr = m_typeCombo->currentText();
    const bool isOpen = typeStr.compare("OpenExe", Qt::CaseInsensitive) == 0;
    m_exeRow->setVisible(isOpen);
}

void ActionDialog::setInitial(const Result &init)
{
    m_nameEdit->setText(init.customName);
    m_msgEdit->setText(init.expectedMessage);
    m_exeEdit->setText(init.exePath);
    const QString typeStr = ActionsRegistry::toString(init.type);
    int idx = actionTypeStrings().indexOf(typeStr);
    if (idx < 0) idx = 0;
    m_typeCombo->setCurrentIndex(idx);
}

ActionDialog::Result ActionDialog::getResult() const
{
    ActionType type;
    const QString typeStr = m_typeCombo->currentText();
    if (!ActionsRegistry::fromString(typeStr, type)) type = ActionType::Shutdown;
    return { m_nameEdit->text().trimmed(), type, m_msgEdit->text().trimmed(), m_exeEdit->text().trimmed() };
}

void ActionDialog::onBrowseExe()
{
    const QString path = QFileDialog::getOpenFileName(this, "Select executable", QString(),
                                                      "Executables (*.exe);;All files (*.*)");
    if (!path.isEmpty()) m_exeEdit->setText(path);
}


