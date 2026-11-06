#include "singletons/SolrinoProfileImport.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QString>

namespace chatterino {

namespace {

/// Removes object[parent][key], keeping the rest of object[parent].
void removeNested(QJsonObject &object, const QString &parent,
                  const QString &key)
{
    auto child = object.value(parent).toObject();
    child.remove(key);
    object.insert(parent, child);
}

QJsonObject readObject(const QString &path, bool *ok)
{
    QFile file(path);
    QJsonParseError error{};
    QJsonDocument document;
    if (file.open(QIODevice::ReadOnly))
    {
        document = QJsonDocument::fromJson(file.readAll(), &error);
    }
    *ok = file.isOpen() && error.error == QJsonParseError::NoError &&
          document.isObject();
    return document.object();
}

bool writeObject(const QString &path, const QJsonObject &object)
{
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly))
    {
        return false;
    }
    output.write(QJsonDocument(object).toJson());
    return output.commit();
}

void markAccountsImported(QJsonObject &settings)
{
    auto solrino = settings.value(QStringLiteral("solrino")).toObject();
    solrino.insert(QStringLiteral("accountsImported"), true);
    settings.insert(QStringLiteral("solrino"), solrino);
}

bool hasTwitchAccount(const QJsonObject &settings)
{
    const auto accounts = settings.value(QStringLiteral("accounts")).toObject();
    for (auto it = accounts.begin(); it != accounts.end(); ++it)
    {
        if (it.key().startsWith(QStringLiteral("uid")))
        {
            return true;
        }
    }
    return false;
}

}  // namespace

bool importOldProfileOnce(const QString &oldSettingsDir,
                          const QString &newSettingsDir)
{
    const QDir oldDir(oldSettingsDir);
    const QDir newDir(newSettingsDir);
    const auto newSettingsPath = newDir.filePath(QStringLiteral("settings.json"));
    if (QFileInfo::exists(newSettingsPath))
    {
        return false;
    }

    QFile oldSettings(oldDir.filePath(QStringLiteral("settings.json")));
    if (!oldSettings.open(QIODevice::ReadOnly))
    {
        return false;
    }

    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(oldSettings.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        return false;
    }

    auto settings = document.object();

    removeNested(settings, QStringLiteral("misc"),
                 QStringLiteral("currentVersion"));
    markAccountsImported(settings);

    // Lets Solrino's default theme apply on first launch, and starts on the
    // basic layout until the person picks another.
    auto appearance = settings.value(QStringLiteral("appearance")).toObject();
    appearance.remove(QStringLiteral("theme"));
    appearance.remove(QStringLiteral("tabDirection"));
    appearance.remove(QStringLiteral("tabVisibility"));
    settings.insert(QStringLiteral("appearance"), appearance);

    if (!QDir().mkpath(newSettingsDir))
    {
        return false;
    }

    // The layout goes first, so a failure can't leave settings marked as done
    // with the tabs missing.
    const auto oldLayout = oldDir.filePath(QStringLiteral("window-layout.json"));
    const auto newLayout = newDir.filePath(QStringLiteral("window-layout.json"));
    if (QFileInfo::exists(oldLayout) && !QFileInfo::exists(newLayout))
    {
        QFile layoutFile(oldLayout);
        if (!layoutFile.open(QIODevice::ReadOnly))
        {
            return false;
        }
        const auto layoutBytes = layoutFile.readAll();
        auto layout = QJsonDocument::fromJson(layoutBytes).object();

        // Don't inherit a minimized window, or the app would open hidden
        auto windows = layout.value(QStringLiteral("windows")).toArray();
        for (auto entry : windows)
        {
            auto window = entry.toObject();
            window.remove(QStringLiteral("state"));
            entry = window;
        }
        layout.insert(QStringLiteral("windows"), windows);

        QSaveFile layoutOutput(newLayout);
        if (!layoutOutput.open(QIODevice::WriteOnly))
        {
            return false;
        }
        layoutOutput.write(layout.isEmpty() ? layoutBytes
                                            : QJsonDocument(layout).toJson());
        if (!layoutOutput.commit())
        {
            return false;
        }
    }

    // Custom commands and per-user data. Nice to have, so a failure isn't fatal.
    for (const auto *name : {"commands.json", "user-data.json"})
    {
        const auto from = oldDir.filePath(QLatin1String(name));
        const auto to = newDir.filePath(QLatin1String(name));
        if (QFileInfo::exists(from) && !QFileInfo::exists(to))
        {
            QFile::copy(from, to);
        }
    }

    QSaveFile output(newSettingsPath);
    if (!output.open(QIODevice::WriteOnly))
    {
        return false;
    }
    output.write(QJsonDocument(settings).toJson());
    return output.commit();
}

void applyFirstRunImportAnswer(const QString &rootDir,
                               const QString &newSettingsDir,
                               bool newProfileMeansFresh)
{
    const auto answerPath =
        QDir(rootDir).filePath(QStringLiteral("first-run-import.txt"));
    QFile answer(answerPath);
    const bool answered = answer.open(QIODevice::ReadOnly);
    const auto folder =
        answered ? QString::fromUtf8(answer.readAll()).trimmed() : QString{};
    answer.close();
    if (answered)
    {
        QFile::remove(answerPath);
    }

    if (!answered && !newProfileMeansFresh)
    {
        return;
    }

    const auto settingsPath =
        QDir(newSettingsDir).filePath(QStringLiteral("settings.json"));
    if (QFileInfo::exists(settingsPath))
    {
        return;
    }
    if (!folder.isEmpty() &&
        importOldProfileOnce(QDir(folder).filePath(QStringLiteral("Settings")),
                             newSettingsDir))
    {
        return;
    }

    // Keeps the older account import from running on this profile later
    QDir().mkpath(newSettingsDir);
    QJsonObject fresh;
    markAccountsImported(fresh);
    writeObject(settingsPath, fresh);
}

bool importOldAccountsOnce(const QString &oldSettingsDir,
                           const QString &newSettingsDir)
{
    const auto newPath = QDir(newSettingsDir).filePath(QStringLiteral("settings.json"));
    bool ok = false;
    auto settings = readObject(newPath, &ok);
    if (!ok || settings.value(QStringLiteral("solrino"))
                   .toObject()
                   .value(QStringLiteral("accountsImported"))
                   .toBool())
    {
        return false;
    }

    const auto old = readObject(
        QDir(oldSettingsDir).filePath(QStringLiteral("settings.json")), &ok);
    bool imported = false;
    if (ok)
    {
        if (!hasTwitchAccount(settings) && hasTwitchAccount(old))
        {
            settings.insert(QStringLiteral("accounts"),
                            old.value(QStringLiteral("accounts")));
            imported = true;
        }

        auto moltorino = settings.value(QStringLiteral("moltorino")).toObject();
        const auto oldAuth = old.value(QStringLiteral("moltorino"))
                                 .toObject()
                                 .value(QStringLiteral("auth"));
        if (!moltorino.contains(QStringLiteral("auth")) && !oldAuth.isUndefined())
        {
            moltorino.insert(QStringLiteral("auth"), oldAuth);
            settings.insert(QStringLiteral("moltorino"), moltorino);
            imported = true;
        }
    }

    markAccountsImported(settings);
    return writeObject(newPath, settings) && imported;
}

}  // namespace chatterino
