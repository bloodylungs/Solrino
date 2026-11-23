#pragma once

#include <QByteArray>

#include <optional>

/// The building blocks for Solrino messages, all from OpenSSL:
///   X25519 to agree on a secret with someone's public key,
///   HKDF-SHA256 to turn that secret into a one-time key,
///   ChaCha20-Poly1305 to encrypt (and notice any tampering),
///   Ed25519 to sign, so the receiver knows who really wrote it.
/// Keys are raw 32-byte values.
namespace chatterino::solrino::crypto {

struct KeyPair {
    QByteArray privateKey;
    QByteArray publicKey;
};

/// A fresh key pair for agreeing on secrets (X25519)
std::optional<KeyPair> newBoxKeys();

/// A fresh key pair for signing (Ed25519)
std::optional<KeyPair> newSignKeys();

/// The X25519 secret shared by our private key and their public key
std::optional<QByteArray> agree(const QByteArray &ourPrivate,
                                const QByteArray &theirPublic);

/// HKDF-SHA256
std::optional<QByteArray> deriveKey(const QByteArray &secret,
                                    const QByteArray &salt,
                                    const QByteArray &info, int length = 32);

/// ChaCha20-Poly1305. The result is the ciphertext with its 16-byte tag on the
/// end. Opening fails if anything was changed.
std::optional<QByteArray> seal(const QByteArray &key, const QByteArray &nonce,
                               const QByteArray &extra,
                               const QByteArray &plain);
std::optional<QByteArray> open(const QByteArray &key, const QByteArray &nonce,
                               const QByteArray &extra,
                               const QByteArray &sealed);

std::optional<QByteArray> sign(const QByteArray &privateKey,
                               const QByteArray &message);
bool verify(const QByteArray &publicKey, const QByteArray &message,
            const QByteArray &signature);

/// Random bytes from the system's secure generator
QByteArray randomBytes(int count);

QByteArray sha256(const QByteArray &data);

/// Data saved to disk is locked to this Windows user, so copying the file to
/// another account or computer doesn't let anyone read it. On other systems
/// the data is returned unchanged and the file's permissions protect it.
QByteArray protectForThisUser(const QByteArray &data);
std::optional<QByteArray> unprotectForThisUser(const QByteArray &data);

/// Wipes a secret from memory before it's let go
void wipe(QByteArray &secret);

}  // namespace chatterino::solrino::crypto
