#pragma once

#include <QByteArray>

// Lock and sleep state of the PC, published on mqttpowermanager/<username>/state
namespace PcState {
	enum class State { Unlocked, Locked, Sleep };

	// Payload for the state topic: Unlocked, Locked or Sleep
	QByteArray payload(State state);

	// Lock state of a Windows session; Locked when nobody is logged on to it
	State sessionState(unsigned long sessionId);
	// Session on the physical console, used by the service which runs in session 0
	State consoleSessionState();
	// Session this process runs in, used by the GUI
	State ownSessionState();
}
