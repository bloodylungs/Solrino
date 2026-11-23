#include "providers/moltorino/SolrinoCrypto.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

#include <memory>

#ifdef Q_OS_WIN
// clang-format off
#    include <Windows.h>
#    include <dpapi.h>
// clang-format on
#endif

namespace chatterino::solrino::crypto {

namespace {

constexpr int KEY_BYTES = 32;
constexpr int NONCE_BYTES = 12;
constexpr int TAG_BYTES = 16;
constexpr int SIGNATURE_BYTES = 64;

struct PkeyFree {
    void operator()(EVP_PKEY *key) const
    {
        EVP_PKEY_free(key);
    }
};
struct PkeyCtxFree {
    void operator()(EVP_PKEY_CTX *ctx) const
    {
        EVP_PKEY_CTX_free(ctx);
    }
};
struct CipherCtxFree {
    void operator()(EVP_CIPHER_CTX *ctx) const
    {
        EVP_CIPHER_CTX_free(ctx);
    }
};
struct MdCtxFree {
    void operator()(EVP_MD_CTX *ctx) const
    {
        EVP_MD_CTX_free(ctx);
    }
};
using Pkey = std::unique_ptr<EVP_PKEY, PkeyFree>;
using PkeyCtx = std::unique_ptr<EVP_PKEY_CTX, PkeyCtxFree>;
using CipherCtx = std::unique_ptr<EVP_CIPHER_CTX, CipherCtxFree>;
using MdCtx = std::unique_ptr<EVP_MD_CTX, MdCtxFree>;

const auto *bytes(const QByteArray &data)
{
    return reinterpret_cast<const unsigned char *>(data.constData());
}

auto *bytes(QByteArray &data)
{
    return reinterpret_cast<unsigned char *>(data.data());
}

std::optional<KeyPair> newKeys(int type)
{
    PkeyCtx ctx(EVP_PKEY_CTX_new_id(type, nullptr));
    if (!ctx || EVP_PKEY_keygen_init(ctx.get()) <= 0)
    {
        return std::nullopt;
    }
    EVP_PKEY *raw = nullptr;
    if (EVP_PKEY_keygen(ctx.get(), &raw) <= 0)
    {
        return std::nullopt;
    }
    Pkey key(raw);

    KeyPair pair;
    size_t length = KEY_BYTES;
    pair.privateKey.resize(KEY_BYTES);
    pair.publicKey.resize(KEY_BYTES);
    if (EVP_PKEY_get_raw_private_key(key.get(), bytes(pair.privateKey),
                                     &length) <= 0 ||
        length != KEY_BYTES)
    {
        return std::nullopt;
    }
    length = KEY_BYTES;
    if (EVP_PKEY_get_raw_public_key(key.get(), bytes(pair.publicKey),
                                    &length) <= 0 ||
        length != KEY_BYTES)
    {
        return std::nullopt;
    }
    return pair;
}

}  // namespace

std::optional<KeyPair> newBoxKeys()
{
    return newKeys(EVP_PKEY_X25519);
}

std::optional<KeyPair> newSignKeys()
{
    return newKeys(EVP_PKEY_ED25519);
}

std::optional<QByteArray> agree(const QByteArray &ourPrivate,
                                const QByteArray &theirPublic)
{
    if (ourPrivate.size() != KEY_BYTES || theirPublic.size() != KEY_BYTES)
    {
        return std::nullopt;
    }
    Pkey ours(EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr,
                                           bytes(ourPrivate), KEY_BYTES));
    Pkey theirs(EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr,
                                            bytes(theirPublic), KEY_BYTES));
    if (!ours || !theirs)
    {
        return std::nullopt;
    }
    PkeyCtx ctx(EVP_PKEY_CTX_new(ours.get(), nullptr));
    if (!ctx || EVP_PKEY_derive_init(ctx.get()) <= 0 ||
        EVP_PKEY_derive_set_peer(ctx.get(), theirs.get()) <= 0)
    {
        return std::nullopt;
    }
    size_t length = 0;
    if (EVP_PKEY_derive(ctx.get(), nullptr, &length) <= 0 ||
        length != KEY_BYTES)
    {
        return std::nullopt;
    }
    QByteArray secret(KEY_BYTES, '\0');
    if (EVP_PKEY_derive(ctx.get(), bytes(secret), &length) <= 0)
    {
        return std::nullopt;
    }
    // An all-zero result means their key was a bad one on purpose
    if (CRYPTO_memcmp(secret.constData(), QByteArray(KEY_BYTES, '\0').constData(),
                      KEY_BYTES) == 0)
    {
        return std::nullopt;
    }
    return secret;
}

std::optional<QByteArray> deriveKey(const QByteArray &secret,
                                    const QByteArray &salt,
                                    const QByteArray &info, int length)
{
    PkeyCtx ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr));
    if (!ctx || EVP_PKEY_derive_init(ctx.get()) <= 0 ||
        EVP_PKEY_CTX_set_hkdf_md(ctx.get(), EVP_sha256()) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_salt(ctx.get(), bytes(salt),
                                    static_cast<int>(salt.size())) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_key(ctx.get(), bytes(secret),
                                   static_cast<int>(secret.size())) <= 0 ||
        EVP_PKEY_CTX_add1_hkdf_info(ctx.get(), bytes(info),
                                    static_cast<int>(info.size())) <= 0)
    {
        return std::nullopt;
    }
    QByteArray out(length, '\0');
    auto size = static_cast<size_t>(length);
    if (EVP_PKEY_derive(ctx.get(), bytes(out), &size) <= 0 ||
        size != static_cast<size_t>(length))
    {
        return std::nullopt;
    }
    return out;
}

std::optional<QByteArray> seal(const QByteArray &key, const QByteArray &nonce,
                               const QByteArray &extra,
                               const QByteArray &plain)
{
    if (key.size() != KEY_BYTES || nonce.size() != NONCE_BYTES)
    {
        return std::nullopt;
    }
    CipherCtx ctx(EVP_CIPHER_CTX_new());
    int length = 0;
    if (!ctx ||
        EVP_EncryptInit_ex(ctx.get(), EVP_chacha20_poly1305(), nullptr,
                           bytes(key), bytes(nonce)) != 1 ||
        EVP_EncryptUpdate(ctx.get(), nullptr, &length, bytes(extra),
                          static_cast<int>(extra.size())) != 1)
    {
        return std::nullopt;
    }
    QByteArray out(plain.size() + TAG_BYTES, '\0');
    int written = 0;
    if (EVP_EncryptUpdate(ctx.get(), bytes(out), &written, bytes(plain),
                          static_cast<int>(plain.size())) != 1)
    {
        return std::nullopt;
    }
    int last = 0;
    if (EVP_EncryptFinal_ex(ctx.get(), bytes(out) + written, &last) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_GET_TAG, TAG_BYTES,
                            bytes(out) + written + last) != 1)
    {
        return std::nullopt;
    }
    out.resize(written + last + TAG_BYTES);
    return out;
}

std::optional<QByteArray> open(const QByteArray &key, const QByteArray &nonce,
                               const QByteArray &extra,
                               const QByteArray &sealed)
{
    if (key.size() != KEY_BYTES || nonce.size() != NONCE_BYTES ||
        sealed.size() < TAG_BYTES)
    {
        return std::nullopt;
    }
    const auto body = sealed.left(sealed.size() - TAG_BYTES);
    auto tag = sealed.right(TAG_BYTES);

    CipherCtx ctx(EVP_CIPHER_CTX_new());
    int length = 0;
    if (!ctx ||
        EVP_DecryptInit_ex(ctx.get(), EVP_chacha20_poly1305(), nullptr,
                           bytes(key), bytes(nonce)) != 1 ||
        EVP_DecryptUpdate(ctx.get(), nullptr, &length, bytes(extra),
                          static_cast<int>(extra.size())) != 1)
    {
        return std::nullopt;
    }
    QByteArray out(body.size() + TAG_BYTES, '\0');
    int written = 0;
    if (EVP_DecryptUpdate(ctx.get(), bytes(out), &written, bytes(body),
                          static_cast<int>(body.size())) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_SET_TAG, TAG_BYTES,
                            bytes(tag)) != 1)
    {
        return std::nullopt;
    }
    int last = 0;
    // This is where tampering is caught
    if (EVP_DecryptFinal_ex(ctx.get(), bytes(out) + written, &last) != 1)
    {
        wipe(out);
        return std::nullopt;
    }
    out.resize(written + last);
    return out;
}

std::optional<QByteArray> sign(const QByteArray &privateKey,
                               const QByteArray &message)
{
    if (privateKey.size() != KEY_BYTES)
    {
        return std::nullopt;
    }
    Pkey key(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                          bytes(privateKey), KEY_BYTES));
    MdCtx ctx(EVP_MD_CTX_new());
    if (!key || !ctx ||
        EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) !=
            1)
    {
        return std::nullopt;
    }
    size_t length = SIGNATURE_BYTES;
    QByteArray signature(SIGNATURE_BYTES, '\0');
    if (EVP_DigestSign(ctx.get(), bytes(signature), &length, bytes(message),
                       static_cast<size_t>(message.size())) != 1 ||
        length != SIGNATURE_BYTES)
    {
        return std::nullopt;
    }
    return signature;
}

bool verify(const QByteArray &publicKey, const QByteArray &message,
            const QByteArray &signature)
{
    if (publicKey.size() != KEY_BYTES || signature.size() != SIGNATURE_BYTES)
    {
        return false;
    }
    Pkey key(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
                                         bytes(publicKey), KEY_BYTES));
    MdCtx ctx(EVP_MD_CTX_new());
    if (!key || !ctx ||
        EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr,
                             key.get()) != 1)
    {
        return false;
    }
    return EVP_DigestVerify(ctx.get(), bytes(signature), SIGNATURE_BYTES,
                            bytes(message),
                            static_cast<size_t>(message.size())) == 1;
}

QByteArray randomBytes(int count)
{
    QByteArray out(count, '\0');
    if (RAND_bytes(bytes(out), count) != 1)
    {
        // Never fall back to weak randomness
        qFatal("Solrino couldn't get secure random bytes");
    }
    return out;
}

QByteArray sha256(const QByteArray &data)
{
    QByteArray out(32, '\0');
    unsigned int length = 0;
    EVP_Digest(data.constData(), static_cast<size_t>(data.size()), bytes(out),
               &length, EVP_sha256(), nullptr);
    return out;
}

QByteArray protectForThisUser(const QByteArray &data)
{
#ifdef Q_OS_WIN
    DATA_BLOB in{static_cast<DWORD>(data.size()),
                 reinterpret_cast<BYTE *>(const_cast<char *>(data.constData()))};
    DATA_BLOB out{};
    if (CryptProtectData(&in, L"Solrino messages", nullptr, nullptr, nullptr,
                         CRYPTPROTECT_UI_FORBIDDEN, &out))
    {
        QByteArray result(reinterpret_cast<const char *>(out.pbData),
                          static_cast<qsizetype>(out.cbData));
        LocalFree(out.pbData);
        return result;
    }
    return {};
#else
    return data;
#endif
}

std::optional<QByteArray> unprotectForThisUser(const QByteArray &data)
{
#ifdef Q_OS_WIN
    DATA_BLOB in{static_cast<DWORD>(data.size()),
                 reinterpret_cast<BYTE *>(const_cast<char *>(data.constData()))};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out))
    {
        return std::nullopt;
    }
    QByteArray result(reinterpret_cast<const char *>(out.pbData),
                      static_cast<qsizetype>(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
#else
    return data;
#endif
}

void wipe(QByteArray &secret)
{
    if (!secret.isEmpty())
    {
        OPENSSL_cleanse(secret.data(), static_cast<size_t>(secret.size()));
    }
    secret.clear();
}

}  // namespace chatterino::solrino::crypto
