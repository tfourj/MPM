#ifndef ACTIONS_H
#define ACTIONS_H

#include <QString>

enum class ActionType {
    Shutdown,
    Restart,
    Suspend,
    Sleep,
    OpenExe,
    Lock
};

namespace ActionsRegistry {
    QString toString(ActionType type);
    bool fromString(const QString &str, ActionType &outType);
    bool execute(ActionType type, const QString &exePath = QString());
}

#endif // ACTIONS_H


