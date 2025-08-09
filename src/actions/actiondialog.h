#ifndef ACTIONDIALOG_H
#define ACTIONDIALOG_H

#include <QDialog>
#include "actions.h"

class QLineEdit;
class QComboBox;
class QDialogButtonBox;

class ActionDialog : public QDialog
{
    Q_OBJECT
public:
    struct Result {
        QString customName;
        ActionType type;
        QString expectedMessage;
        QString exePath; // only for OpenExe
    };

    explicit ActionDialog(QWidget *parent = nullptr);
    void setInitial(const Result &init);
    Result getResult() const;

private slots:
    void onTypeChanged(int index);
    void onBrowseExe();

private:
    QLineEdit *m_nameEdit;
    QComboBox *m_typeCombo;
    QLineEdit *m_msgEdit;
    QLineEdit *m_exeEdit;
    QDialogButtonBox *m_buttons;
    QWidget *m_exeRow;
    void buildUi();
};

#endif // ACTIONDIALOG_H


