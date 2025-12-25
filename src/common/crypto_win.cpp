#include "crypto_win.h"

#include <windows.h>
#include <wincrypt.h>

static QByteArray dpapiProtect(const QByteArray &plain, DWORD flags)
{
	if (plain.isEmpty()) return QByteArray();
	DATA_BLOB in{};
	in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.constData()));
	in.cbData = static_cast<DWORD>(plain.size());
	DATA_BLOB out{};
	if (!CryptProtectData(&in, L"MPM", nullptr, nullptr, nullptr, flags, &out)) {
		return QByteArray();
	}
	QByteArray encrypted(reinterpret_cast<const char*>(out.pbData), static_cast<int>(out.cbData));
	if (out.pbData) LocalFree(out.pbData);
	return encrypted.toBase64();
}

QByteArray dpapiEncryptUserScope(const QByteArray &plain)
{
	return dpapiProtect(plain, 0);
}

QByteArray dpapiEncryptMachineScope(const QByteArray &plain)
{
	return dpapiProtect(plain, CRYPTPROTECT_LOCAL_MACHINE);
}

QByteArray dpapiDecryptBase64(const QByteArray &cipherBase64)
{
	if (cipherBase64.isEmpty()) return QByteArray();
	QByteArray cipher = QByteArray::fromBase64(cipherBase64);
	DATA_BLOB in{};
	in.pbData = reinterpret_cast<BYTE*>(cipher.data());
	in.cbData = static_cast<DWORD>(cipher.size());
	DATA_BLOB out{};
	LPWSTR description = nullptr;
	if (!CryptUnprotectData(&in, &description, nullptr, nullptr, nullptr, 0, &out)) {
		return QByteArray();
	}
	if (description) LocalFree(description);
	QByteArray plain(reinterpret_cast<const char*>(out.pbData), static_cast<int>(out.cbData));
	if (out.pbData) LocalFree(out.pbData);
	return plain;
}

 


