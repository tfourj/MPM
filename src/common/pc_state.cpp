#include "pc_state.h"

#include <cstddef>
#include <windows.h>
#include <wtsapi32.h>

namespace {

// MinGW's wtsapi32.h lacks WTSSessionInfoEx and WTSINFOEXW, so mirror them from the Windows SDK
const WTS_INFO_CLASS kWtsSessionInfoEx = static_cast<WTS_INFO_CLASS>(25);
const LONG kWtsSessionStateLock = 0;

struct WtsInfoExLevel1 {
	ULONG SessionId;
	WTS_CONNECTSTATE_CLASS SessionState;
	LONG SessionFlags;
	WCHAR WinStationName[33];
	WCHAR UserName[21];
	WCHAR DomainName[18];
	LARGE_INTEGER LogonTime;
	LARGE_INTEGER ConnectTime;
	LARGE_INTEGER DisconnectTime;
	LARGE_INTEGER LastInputTime;
	LARGE_INTEGER CurrentTime;
	DWORD IncomingBytes;
	DWORD OutgoingBytes;
	DWORD IncomingFrames;
	DWORD OutgoingFrames;
	DWORD IncomingCompressedBytes;
	DWORD OutgoingCompressedBytes;
};

struct WtsInfoEx {
	DWORD Level;
	union {
		WtsInfoExLevel1 level1;
	} Data;
};

} // namespace

QByteArray PcState::payload(State state)
{
	switch (state) {
	case State::Unlocked: return QByteArrayLiteral("unlocked");
	case State::Locked:   return QByteArrayLiteral("locked");
	case State::Sleep:    return QByteArrayLiteral("sleep");
	}
	return QByteArrayLiteral("unlocked");
}

PcState::State PcState::sessionState(unsigned long sessionId)
{
	// No session attached to the console, e.g. while switching users
	if (sessionId == 0xFFFFFFFF) return State::Locked;
	LPWSTR buffer = nullptr;
	DWORD bytes = 0;
	// Assume unlocked when Windows cannot tell, MPM itself is running after all
	if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sessionId, kWtsSessionInfoEx, &buffer, &bytes)) {
		return State::Unlocked;
	}
	State state = State::Unlocked;
	const auto *info = reinterpret_cast<const WtsInfoEx *>(buffer);
	// Only the fields up to UserName are read
	const size_t needed = offsetof(WtsInfoEx, Data) + offsetof(WtsInfoExLevel1, DomainName);
	if (info && bytes >= needed && info->Level == 1) {
		const WtsInfoExLevel1 &l1 = info->Data.level1;
		// The sign in screen has no user, which counts as locked
		if (l1.UserName[0] == L'\0' || l1.SessionFlags == kWtsSessionStateLock) state = State::Locked;
	}
	WTSFreeMemory(buffer);
	return state;
}

PcState::State PcState::consoleSessionState()
{
	return sessionState(WTSGetActiveConsoleSessionId());
}

PcState::State PcState::ownSessionState()
{
	DWORD sessionId = 0;
	if (!ProcessIdToSessionId(GetCurrentProcessId(), &sessionId)) return State::Unlocked;
	return sessionState(sessionId);
}
