#ifndef MPM_CRYPTO_WIN_H
#define MPM_CRYPTO_WIN_H

#include <QByteArray>

// Encrypts with user scope (default DPAPI scope). Only decryptable by the same user.
QByteArray dpapiEncryptUserScope(const QByteArray &plain);

// Encrypts with machine scope so any user (including services) on the same machine can decrypt.
QByteArray dpapiEncryptMachineScope(const QByteArray &plain);

// Decrypts data that was encrypted by either user or machine scope. Caller must provide the raw cipher (not base64)?
// Our utilities store base64 for portability in INI; these helpers mirror that behavior.
QByteArray dpapiDecryptBase64(const QByteArray &cipherBase64);

#endif // MPM_CRYPTO_WIN_H


