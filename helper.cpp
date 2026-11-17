/**********************************************************************
 *  helper.cpp
 **********************************************************************
 * Copyright (C) 2026-2026 MX Authors
 *
 * Authors: Adrian
 *          MX Linux <http://mxlinux.org>
 *          OpenAI Codex
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 **********************************************************************/

// Privileged helper for mx-user.
//
// Security model: this binary runs as root via pkexec. It exposes a fixed set
// of *named operations* (add-user, del-user, set-autologin, ...). Every command
// line it runs is built from internal constants; the only data accepted from the
// caller are scalar parameters (user/group names, a shell from the allow-list, a
// display-manager session token, and paths constrained to /home), each of which
// is validated before use. There is intentionally no generic "run this command"
// entry point, so a caller cannot inject paths, options, scripts, or commands.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStringList>

#include <cstdio>
#include <grp.h>
#include <pwd.h>

namespace
{
struct ProcessResult
{
    bool started = false;
    int exitCode = 1;
    QProcess::ExitStatus exitStatus = QProcess::NormalExit;
    QByteArray standardOutput;
    QByteArray standardError;
};

void writeAndFlush(FILE *stream, const QByteArray &data)
{
    if (!data.isEmpty()) {
        std::fwrite(data.constData(), 1, static_cast<size_t>(data.size()), stream);
        std::fflush(stream);
    }
}

void printError(const QString &message)
{
    writeAndFlush(stderr, message.toUtf8() + '\n');
}

[[nodiscard]] QByteArray readHelperInput()
{
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) {
        return {};
    }
    return input.readAll();
}

// --------------------------------------------------------------------------
// Parameter validation
// --------------------------------------------------------------------------

[[nodiscard]] bool matchesPattern(const QString &value, const QString &pattern)
{
    const QRegularExpression rx(QRegularExpression::anchoredPattern(pattern));
    return rx.match(value).hasMatch();
}

[[nodiscard]] bool isUserName(const QString &value)
{
    return matchesPattern(value, QStringLiteral(R"([A-Za-z_][A-Za-z0-9_-]*[$]?)"));
}

// Escape a validated user name for use as a literal in a sed regex. Names are
// limited to [A-Za-z0-9_-] plus an optional trailing '$', which is the only
// regex metacharacter they can contain.
[[nodiscard]] QString sedLiteral(const QString &user)
{
    QString escaped = user;
    return escaped.replace(QLatin1Char('$'), QLatin1String("[$]"));
}

[[nodiscard]] bool isGroupName(const QString &value)
{
    return matchesPattern(value, QStringLiteral(R"([A-Za-z0-9_][A-Za-z0-9_.-]*[$]?)"));
}

[[nodiscard]] bool isGroupList(const QString &value)
{
    if (value.isEmpty()) {
        return true; // empty list clears supplementary groups
    }
    const QStringList groups = value.split(',');
    for (const QString &group : groups) {
        if (!isGroupName(group)) {
            return false;
        }
    }
    return true;
}

// A display-manager session identifier such as "plasma", "plasmax11" or
// "plasma.desktop"; empty means "do not touch the session".
[[nodiscard]] bool isSessionToken(const QString &value)
{
    return value.isEmpty() || matchesPattern(value, QStringLiteral(R"([A-Za-z0-9._-]+)"));
}

[[nodiscard]] bool hasNoTraversal(const QString &path)
{
    if (path.contains(QChar(u'\0')) || path.contains('\n') || path.contains('\r')) {
        return false;
    }
    const QStringList parts = path.split('/');
    return !parts.contains(QStringLiteral(".."));
}

[[nodiscard]] bool isAbsoluteSafePath(const QString &path)
{
    if (!path.startsWith('/') || !hasNoTraversal(path)) {
        return false;
    }
    for (const QChar c : path) {
        if (c.unicode() < 0x20) {
            return false;
        }
    }
    return true;
}

// The canonical home of `user`, which must be /home/<user>: every operation
// here assumes that layout. If the account database records a different home,
// an old /home/<user> may be a leftover, and acting on it would change the
// wrong files; such accounts are refused. Empty on refusal.
[[nodiscard]] QString canonicalHome(const QString &user)
{
    const QString expected = QStringLiteral("/home/%1").arg(user);
    const passwd *pw = getpwnam(user.toLocal8Bit().constData());
    if (pw == nullptr) {
        printError(QStringLiteral("Unknown account: %1").arg(user));
        return {};
    }
    const QString recorded = QString::fromLocal8Bit(pw->pw_dir);
    const QString homeReal = QFileInfo(expected).canonicalFilePath();
    if (!homeReal.isEmpty() && QFileInfo(recorded).canonicalFilePath() == homeReal) {
        return homeReal;
    }
    if (QDir::cleanPath(recorded) != expected) {
        printError(QStringLiteral("The home folder of %1 is %2, not %3; this layout is not supported.")
                       .arg(user, recorded, expected));
    } else {
        printError(QStringLiteral("The home folder %1 does not exist.").arg(expected));
    }
    return {};
}

// Resolve `requested` against `user`'s home (/home/<user>), following symlinks,
// and confirm the result is the home itself or strictly inside it. On success,
// *safe receives the canonical, symlink-resolved path (no trailing slash).
//
// This binds every file operation to the named user's own home, so a caller
// cannot reach another user's home (cross-user) or escape /home through a symlink
// (the canonical path of e.g. ~/link -> /etc lands outside the home and is
// rejected). Because containment is checked against the resolved home directory
// rather than a regex over the path, usernames are matched verbatim, including
// machine accounts ending in '$'.
[[nodiscard]] bool resolveWithinHome(const QString &user, const QString &requested, QString *safe)
{
    if (!isUserName(user) || requested.contains(QChar(u'\0')) || requested.contains('\n')
        || requested.contains('\r')) {
        return false;
    }
    const QString homeReal = canonicalHome(user);
    if (homeReal.isEmpty()) {
        return false;
    }
    const QString clean = QDir::cleanPath(requested);
    if (!clean.startsWith('/')) {
        return false;
    }

    const auto contained = [&homeReal](const QString &p) {
        return p == homeReal || p.startsWith(homeReal + '/');
    };

    QString resolved = QFileInfo(clean).canonicalFilePath();
    if (resolved.isEmpty()) {
        // Target does not exist yet (e.g. ~/.recently-used): validate its parent,
        // which must exist and resolve inside the home, then re-attach the name.
        const QString name = QFileInfo(clean).fileName();
        if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..")) {
            return false;
        }
        const QString parentReal = QFileInfo(QFileInfo(clean).path()).canonicalFilePath();
        if (parentReal.isEmpty() || !contained(parentReal)) {
            return false;
        }
        resolved = parentReal + '/' + name;
    } else if (!contained(resolved)) {
        return false;
    }

    if (safe) {
        *safe = resolved;
    }
    return true;
}

// Resolve `requested` as an entry *inside* `user`'s home without following its
// final component. Only the containing directory is symlink-resolved and must be
// the home or lie within it; the final name is re-attached verbatim. On success,
// *safe receives <canonical parent>/<name>.
//
// Use this for operations that act on the entry itself rather than on what it
// points to (deletion): if the entry is a symlink, `rm -rf` on this path unlinks
// the symlink instead of recursively deleting its target. The home directory
// itself can never be returned, since the result always has a final component
// below a contained parent.
[[nodiscard]] bool resolveEntryWithinHome(const QString &user, const QString &requested, QString *safe)
{
    if (!isUserName(user) || requested.contains(QChar(u'\0')) || requested.contains('\n')
        || requested.contains('\r')) {
        return false;
    }
    const QString homeReal = canonicalHome(user);
    if (homeReal.isEmpty()) {
        return false;
    }
    const QString clean = QDir::cleanPath(requested);
    if (!clean.startsWith('/')) {
        return false;
    }
    const QString name = QFileInfo(clean).fileName();
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..")) {
        return false;
    }
    const QString parentReal = QFileInfo(QFileInfo(clean).path()).canonicalFilePath();
    if (parentReal.isEmpty() || (parentReal != homeReal && !parentReal.startsWith(homeReal + '/'))) {
        return false;
    }
    if (safe) {
        *safe = parentReal + '/' + name;
    }
    return true;
}

// rsync destinations are restricted to the only places the GUI is meant to copy
// into: anywhere under /home (another user's profile) or under a removable-media
// mount root. This is an allowlist, not a denylist — a root-run rsync (including
// the destructive sync/--delete-after mode) therefore can never target an
// unexpected top-level location such as /opt, /srv, /tmp, /run or a bare mount root.
[[nodiscard]] bool isAllowedCopyDestination(const QString &path)
{
    static const QStringList roots {"/home", "/media", "/mnt", "/run/media"};
    for (const QString &root : roots) {
        if (path.startsWith(root + '/')) {
            return true;
        }
    }
    return false;
}

// Validate an rsync destination: absolute, traversal-free, and within an allowed
// destination root (checked on the symlink-resolved path so a symlink into, say,
// /etc cannot slip through).
[[nodiscard]] bool resolveCopyDestination(const QString &requested, QString *safe)
{
    if (!isAbsoluteSafePath(requested)) {
        return false;
    }
    const QString clean = QDir::cleanPath(requested);
    QString resolved = QFileInfo(clean).canonicalFilePath();
    if (resolved.isEmpty()) {
        // rsync creates the final directory if its parent exists.
        const QString name = QFileInfo(clean).fileName();
        if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..")) {
            return false;
        }
        const QString parentReal = QFileInfo(QFileInfo(clean).path()).canonicalFilePath();
        if (parentReal.isEmpty()) {
            return false;
        }
        resolved = parentReal + '/' + name;
    }
    if (!isAllowedCopyDestination(resolved)) {
        return false;
    }
    if (safe) {
        *safe = resolved;
    }
    return true;
}

[[nodiscard]] bool isAllowedShell(const QString &path)
{
    if (!matchesPattern(path, QStringLiteral(R"(/[A-Za-z0-9._/-]+)")) || !hasNoTraversal(path)) {
        return false;
    }
    const QFileInfo info(path);
    if (!info.exists() || !info.isExecutable()) {
        return false;
    }
    // Prefer membership in /etc/shells; if it is unreadable, accept any existing
    // executable that already passed the character/traversal checks above.
    QFile shells(QStringLiteral("/etc/shells"));
    if (shells.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!shells.atEnd()) {
            const QString line = QString::fromUtf8(shells.readLine()).trimmed();
            if (line == path) {
                return true;
            }
        }
        return false;
    }
    return true;
}

// --------------------------------------------------------------------------
// Tool resolution and execution
// --------------------------------------------------------------------------

[[nodiscard]] const QHash<QString, QStringList> &toolPaths()
{
    static const QHash<QString, QStringList> commands {
        {"addgroup", {"/usr/sbin/addgroup", "/sbin/addgroup", "/usr/bin/addgroup"}},
        {"adduser", {"/usr/sbin/adduser", "/sbin/adduser", "/usr/bin/adduser"}},
        {"delgroup", {"/usr/sbin/delgroup", "/sbin/delgroup", "/usr/bin/delgroup"}},
        {"deluser", {"/usr/sbin/deluser", "/sbin/deluser", "/usr/bin/deluser"}},
        {"find", {"/usr/bin/find", "/bin/find"}},
        {"gpasswd", {"/usr/bin/gpasswd", "/sbin/gpasswd", "/bin/gpasswd"}},
        {"grep", {"/usr/bin/grep", "/bin/grep"}},
        {"groupadd", {"/usr/sbin/groupadd", "/sbin/groupadd", "/usr/bin/groupadd"}},
        {"groupdel", {"/usr/sbin/groupdel", "/sbin/groupdel", "/usr/bin/groupdel"}},
        {"groupmod", {"/usr/sbin/groupmod", "/sbin/groupmod", "/usr/bin/groupmod"}},
        {"killall", {"/usr/bin/killall", "/bin/killall"}},
        {"passwd", {"/usr/bin/passwd", "/sbin/passwd", "/bin/passwd"}},
        {"rm", {"/usr/bin/rm", "/bin/rm"}},
        {"rsync", {"/usr/bin/rsync"}},
        {"sed", {"/usr/bin/sed", "/bin/sed"}},
        {"setpriv", {"/usr/bin/setpriv", "/bin/setpriv"}},
        {"timeout", {"/usr/bin/timeout", "/bin/timeout"}},
        {"useradd", {"/usr/sbin/useradd", "/sbin/useradd", "/usr/bin/useradd"}},
        {"userdel", {"/usr/sbin/userdel", "/sbin/userdel", "/usr/bin/userdel"}},
        {"usermod", {"/usr/sbin/usermod", "/sbin/usermod", "/usr/bin/usermod"}},
    };
    return commands;
}

[[nodiscard]] QString resolveBinary(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isExecutable()) {
            return candidate;
        }
    }
    return {};
}

[[nodiscard]] QString resolveTool(const QString &key)
{
    return resolveBinary(toolPaths().value(key));
}

[[nodiscard]] bool toolAvailable(const QString &key)
{
    return !resolveTool(key).isEmpty();
}

[[nodiscard]] ProcessResult runProcess(const QString &program, const QStringList &args, const QByteArray &input = {})
{
    ProcessResult result;

    QProcess process;
    process.start(program, args, QIODevice::ReadWrite);
    if (!process.waitForStarted()) {
        result.standardError = QString("Failed to start %1").arg(program).toUtf8();
        result.exitCode = 127;
        return result;
    }

    result.started = true;
    if (!input.isEmpty()) {
        process.write(input);
    }
    process.closeWriteChannel();
    process.waitForFinished(-1);

    result.exitStatus = process.exitStatus();
    result.exitCode = process.exitCode();
    result.standardOutput = process.readAllStandardOutput();
    result.standardError = process.readAllStandardError();
    return result;
}

[[nodiscard]] int relayResult(const ProcessResult &result)
{
    writeAndFlush(stdout, result.standardOutput);
    writeAndFlush(stderr, result.standardError);
    if (!result.started) {
        return result.exitCode;
    }
    return result.exitStatus == QProcess::NormalExit ? result.exitCode : 1;
}

// Run an allow-listed tool with a fixed argument list and relay its output.
int runTool(const QString &key, const QStringList &args, const QByteArray &input = {})
{
    const QString program = resolveTool(key);
    if (program.isEmpty()) {
        printError(QString("Command is not available: %1").arg(key));
        return 127;
    }
    return relayResult(runProcess(program, args, input));
}

// Run an allow-listed tool whose stdout/stderr go straight to the helper's own,
// so the caller sees output while the tool runs (e.g. rsync progress) instead of
// all at once when it exits. Use runTool() when output must be captured first.
int runToolStreaming(const QString &key, const QStringList &args)
{
    const QString program = resolveTool(key);
    if (program.isEmpty()) {
        printError(QString("Command is not available: %1").arg(key));
        return 127;
    }
    QProcess process;
    process.setProcessChannelMode(QProcess::ForwardedChannels);
    process.start(program, args);
    if (!process.waitForStarted()) {
        printError(QString("Failed to start %1").arg(program));
        return 127;
    }
    process.closeWriteChannel();
    process.waitForFinished(-1);
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : 1;
}

// --------------------------------------------------------------------------
// Display-manager autologin configuration
// --------------------------------------------------------------------------

const QString LightdmConf = QStringLiteral("/etc/lightdm/lightdm.conf");
const QString SddmKdeConf = QStringLiteral("/etc/sddm.conf.d/kde_settings.conf");
const QString SddmConf = QStringLiteral("/etc/sddm.conf");
const QString PlasmaConf = QStringLiteral("/etc/plasmalogin.conf.d/autologin.conf");

// Returns the active sddm configuration file (kde variant preferred), or empty.
[[nodiscard]] QString sddmConfFile()
{
    if (QFileInfo::exists(SddmKdeConf)) {
        return SddmKdeConf;
    }
    if (QFileInfo::exists(SddmConf)) {
        return SddmConf;
    }
    return {};
}

// SDDM reads /usr/lib/sddm/sddm.conf.d, then /etc/sddm.conf.d (each directory's
// *.conf in alphabetical order), then /etc/sddm.conf; later values win. With
// `localOnly`, the package-owned /usr/lib drop-ins are left out (they are never
// edited). Only existing files are returned.
[[nodiscard]] QStringList sddmConfFiles(bool localOnly)
{
    QStringList dirs {QStringLiteral("/etc/sddm.conf.d")};
    if (!localOnly) {
        dirs.prepend(QStringLiteral("/usr/lib/sddm/sddm.conf.d"));
    }
    QStringList files;
    for (const QString &dirPath : std::as_const(dirs)) {
        const QDir dir(dirPath);
        for (const QString &name : dir.entryList({QStringLiteral("*.conf")}, QDir::Files, QDir::Name)) {
            files << dir.filePath(name);
        }
    }
    if (QFileInfo::exists(SddmConf)) {
        files << SddmConf;
    }
    return files;
}

// --------------------------------------------------------------------------
// Operations
// --------------------------------------------------------------------------

[[nodiscard]] int opAddUser(const QStringList &args)
{
    // <username> <shell> [<comma-separated groups>]
    if (args.size() < 2 || args.size() > 3) {
        printError(QStringLiteral("add-user requires <username> <shell> [groups]"));
        return 1;
    }
    const QString user = args.at(0);
    const QString shell = args.at(1);
    const QString groups = args.size() == 3 ? args.at(2) : QString();
    if (!isUserName(user) || !isAllowedShell(shell) || !isGroupList(groups)) {
        printError(QStringLiteral("add-user: invalid parameters"));
        return 1;
    }

    if (toolAvailable(QStringLiteral("adduser"))) {
        // Detect the option spelling supported by this adduser implementation.
        const ProcessResult help = runProcess(resolveTool(QStringLiteral("adduser")), {QStringLiteral("--help")});
        const QString helpText = QString::fromUtf8(help.standardOutput + help.standardError);
        const QString commentOption
            = helpText.contains(QLatin1String("--comment")) ? QStringLiteral("--comment") : QStringLiteral("--gecos");
        const QString allowBadNames = helpText.contains(QLatin1String("--allow-bad-names"))
            ? QStringLiteral("--allow-bad-names")
            : QStringLiteral("--force-badname");
        return runTool(QStringLiteral("adduser"),
                       {"--disabled-login", allowBadNames, "--shell", shell, commentOption, user, user});
    }
    if (toolAvailable(QStringLiteral("useradd"))) {
        QStringList useraddArgs {"-m", "-s", shell, "-c", user};
        if (!groups.isEmpty()) {
            useraddArgs << "-G" << groups;
        }
        useraddArgs << user;
        return runTool(QStringLiteral("useradd"), useraddArgs);
    }
    printError(QStringLiteral("No suitable user creation tool found (adduser or useradd)"));
    return 127;
}

[[nodiscard]] int opSetPassword(const QStringList &args)
{
    if (args.size() != 1 || !isUserName(args.constFirst())) {
        printError(QStringLiteral("set-password requires exactly one valid username"));
        return 1;
    }
    return runTool(QStringLiteral("passwd"), {args.constFirst()}, readHelperInput());
}

[[nodiscard]] int opGroupMember(const QStringList &args, bool add)
{
    // <username> <group>
    if (args.size() != 2 || !isUserName(args.at(0)) || !isGroupName(args.at(1))) {
        printError(QStringLiteral("group membership change requires <username> <group>"));
        return 1;
    }
    return runTool(QStringLiteral("gpasswd"), {add ? "-a" : "-d", args.at(0), args.at(1)});
}

[[nodiscard]] int opSetGroups(const QStringList &args)
{
    // <username> <comma-separated groups (may be empty)>
    if (args.isEmpty() || args.size() > 2 || !isUserName(args.at(0))) {
        printError(QStringLiteral("set-groups requires <username> [groups]"));
        return 1;
    }
    const QString groups = args.size() == 2 ? args.at(1) : QString();
    if (!isGroupList(groups)) {
        printError(QStringLiteral("set-groups: invalid group list"));
        return 1;
    }
    return runTool(QStringLiteral("usermod"), {"-G", groups, args.at(0)});
}

[[nodiscard]] int opDelUser(const QStringList &args)
{
    // <username> [--remove-home]
    if (args.isEmpty() || !isUserName(args.constFirst())) {
        printError(QStringLiteral("del-user requires a valid username"));
        return 1;
    }
    const QString user = args.constFirst();
    const bool removeHome = args.contains(QStringLiteral("--remove-home"));
    if (toolAvailable(QStringLiteral("deluser"))) {
        return runTool(QStringLiteral("deluser"),
                       removeHome ? QStringList {"--remove-home", user} : QStringList {user});
    }
    if (toolAvailable(QStringLiteral("userdel"))) {
        return runTool(QStringLiteral("userdel"), removeHome ? QStringList {"-r", user} : QStringList {user});
    }
    printError(QStringLiteral("No suitable user removal tool found (deluser or userdel)"));
    return 127;
}

[[nodiscard]] int opKillUser(const QStringList &args)
{
    if (args.size() != 1 || !isUserName(args.constFirst())) {
        printError(QStringLiteral("kill-user requires exactly one valid username"));
        return 1;
    }
    const QString user = args.constFirst();
    // Best effort: terminate, then force-kill any remaining processes.
    runTool(QStringLiteral("timeout"), {"5s", "killall", "-w", "-u", user});
    runTool(QStringLiteral("timeout"), {"5s", "killall", "-9", "-w", "-u", user});
    return 0;
}

[[nodiscard]] int opAddGroup(const QStringList &args)
{
    // <groupname> <system|user>
    if (args.size() != 2 || !isGroupName(args.at(0))) {
        printError(QStringLiteral("add-group requires <groupname> <system|user>"));
        return 1;
    }
    const QString group = args.at(0);
    const bool systemGroup = args.at(1) == QLatin1String("system");
    if (!systemGroup && args.at(1) != QLatin1String("user")) {
        printError(QStringLiteral("add-group: scope must be 'system' or 'user'"));
        return 1;
    }
    if (toolAvailable(QStringLiteral("addgroup"))) {
        return runTool(QStringLiteral("addgroup"),
                       systemGroup ? QStringList {group, "--system"} : QStringList {group, "--quiet"});
    }
    if (toolAvailable(QStringLiteral("groupadd"))) {
        return runTool(QStringLiteral("groupadd"),
                       systemGroup ? QStringList {"--system", group} : QStringList {group});
    }
    printError(QStringLiteral("No suitable group creation tool found (addgroup or groupadd)"));
    return 127;
}

[[nodiscard]] int opDelGroup(const QStringList &args)
{
    if (args.size() != 1 || !isGroupName(args.constFirst())) {
        printError(QStringLiteral("del-group requires exactly one valid group name"));
        return 1;
    }
    const QString group = args.constFirst();
    if (toolAvailable(QStringLiteral("delgroup"))) {
        return runTool(QStringLiteral("delgroup"), {group});
    }
    if (toolAvailable(QStringLiteral("groupdel"))) {
        return runTool(QStringLiteral("groupdel"), {group});
    }
    printError(QStringLiteral("No suitable group removal tool found (delgroup or groupdel)"));
    return 127;
}

int opRewriteHomePaths(const QStringList &args)
{
    // <owner-user> <dir under owner's home> <old-user> <new-user>:
    // rewrite "home/<old>" -> "home/<new>" in files under <dir>.
    QString dir;
    if (args.size() != 4 || !resolveWithinHome(args.at(0), args.at(1), &dir) || !isUserName(args.at(2))
        || !isUserName(args.at(3))) {
        printError(QStringLiteral("rewrite-home-paths requires <owner-user> <dir under that home> <old-user> <new-user>"));
        return 1;
    }
    // Only rewrite text files that actually contain the old path. The `grep -I`
    // test makes find skip binary files (SQLite databases, caches, browser
    // profiles, images, ...), which `sed -i` would otherwise corrupt by treating
    // them as text. find runs sed (batched via +) only on files the grep test
    // selected.
    //
    // The old name must end the directory component: it has to be followed by
    // '/', the end of the line, whitespace, a quote, or a delimiter used in
    // config syntax (lists, key=value, URLs, markup). Anything else may continue
    // a different directory name, so "home/alice" leaves "home/alice2",
    // "home/alice.bak" and "home/alice+old" alone. The terminator is kept.
    // '%' is absent because in a URL "alice%20x" names a different directory;
    // braces are absent because find's "-exec ... +" rejects any argument other
    // than the final one containing "{}".
    const QString pattern = QStringLiteral("home/%1").arg(args.at(2));
    const QString script = QString(R"(s#home/%1([]/"'[:space:]:;,=<>()&?[]|$)#home/%2\1#g)")
                               .arg(sedLiteral(args.at(2)), args.at(3));

    // Run the whole find/grep/sed pass as the owner, never as root. The tree is
    // writable by that account, which could otherwise swap a directory for a
    // symlink between find selecting a file and the batched `sed -i` rewriting
    // it, and have root edit a file outside the home (e.g. /etc/passwd, which
    // contains "home/<user>:"). As the owner, sed can only change what the owner
    // could change anyway.
    const passwd *pw = getpwnam(args.at(0).toLocal8Bit().constData());
    const QString find = resolveTool(QStringLiteral("find"));
    const QString grep = resolveTool(QStringLiteral("grep"));
    const QString sed = resolveTool(QStringLiteral("sed"));
    if (pw == nullptr || pw->pw_uid == 0 || find.isEmpty() || grep.isEmpty() || sed.isEmpty()) {
        printError(QStringLiteral("rewrite-home-paths: cannot run as %1").arg(args.at(0)));
        return 1;
    }
    // find needs a working directory the owner can reach; the inherited one may
    // be the administrator's private home.
    QDir::setCurrent(QStringLiteral("/"));
    return runTool(QStringLiteral("setpriv"),
                   {QStringLiteral("--reuid=%1").arg(pw->pw_uid), QStringLiteral("--regid=%1").arg(pw->pw_gid),
                    "--init-groups", "--inh-caps=-all", "--reset-env", "--", find, dir, "-type", "f", "-exec", grep, "-IqF", "-e",
                    pattern, "{}", ";", "-exec", sed, "-i", "-E", script, "{}", "+"});
}

[[nodiscard]] int opRenameUser(const QStringList &args)
{
    // <old-user> <new-user>
    if (args.size() != 2 || !isUserName(args.at(0)) || !isUserName(args.at(1))) {
        printError(QStringLiteral("rename-user requires <old-user> <new-user>"));
        return 1;
    }
    const QString oldName = args.at(0);
    const QString newName = args.at(1);

    const int renameCode = runTool(
        QStringLiteral("usermod"),
        {"--login", newName, "--move-home", "--home", QStringLiteral("/home/%1").arg(newName), oldName});
    if (renameCode != 0) {
        return renameCode; // critical failure; do not attempt the cosmetic follow-ups
    }

    // Update the GECOS/full-name field if it referenced the old name (whole word).
    gid_t primaryGid = 0;
    bool havePrimaryGid = false;
    if (const passwd *pw = getpwnam(newName.toLocal8Bit().constData())) {
        primaryGid = pw->pw_gid;
        havePrimaryGid = true;
        const QString gecos = QString::fromLocal8Bit(pw->pw_gecos);
        const QRegularExpression boundary(
            QString(R"((?<![A-Za-z0-9_-])%1(?![A-Za-z0-9_-]))").arg(QRegularExpression::escape(oldName)));
        QString updated = gecos;
        updated.replace(boundary, newName);
        if (updated != gecos) {
            runTool(QStringLiteral("usermod"), {"-c", updated, newName});
        }
    }

    // Rename the user's personal group if it shared the old name (best effort).
    // Only when it is this account's primary group: a namesake group that is not
    // (e.g. a project group, with the account's primary group being "users")
    // belongs to whoever uses it and keeps its name.
    const group *gr = getgrnam(oldName.toLocal8Bit().constData());
    if (havePrimaryGid && gr != nullptr && gr->gr_gid == primaryGid) {
        runTool(QStringLiteral("groupmod"), {"--new-name", newName, oldName});
    }

    // Fix "home/<old>" references inside the (now relocated) home directory.
    opRewriteHomePaths({newName, QStringLiteral("/home/%1").arg(newName), oldName, newName});

    return 0;
}

[[nodiscard]] int opRemoveHomePath(const QStringList &args)
{
    // <user> <path under that user's home>
    QString safe;
    // The final component is not resolved, so a symlinked entry (e.g. ~/.mozilla
    // -> ~/Documents, or ~/.recently-used -> .) is unlinked, never its target.
    if (args.size() != 2 || !resolveEntryWithinHome(args.at(0), args.at(1), &safe)) {
        printError(QStringLiteral("remove-home-path requires <user> <path under that user's home>"));
        return 1;
    }
    return runTool(QStringLiteral("rm"), {"-rf", "--", safe});
}

[[nodiscard]] int opCleanOpenOfficeLocks(const QStringList &args)
{
    // <user> <home dir>
    QString safe;
    if (args.size() != 2 || !resolveWithinHome(args.at(0), args.at(1), &safe)) {
        printError(QStringLiteral("clean-openoffice-locks requires <user> <home dir>"));
        return 1;
    }
    return runTool(QStringLiteral("find"),
                   {safe + "/.openoffice.org", "-type", "f", "-name", ".lock", "-delete"});
}

[[nodiscard]] int opRsyncCopy(const QStringList &args)
{
    // <from-user> <from-dir under that home> <to-dir> <sync|copy> [<to-user>]
    //
    // With <to-user>, this is a copy into that account: <to-dir> must resolve
    // inside its home, and everything rsync writes is owned by that account. All
    // of this is checked before anything is transferred, so a destination that
    // links out of the home (e.g. ~/Documents -> /media/data/Documents) is
    // refused up front rather than copied into and then left with the source
    // account's ownership. <to-dir> must lie under an allowed destination root
    // in both modes; without <to-user> it is a browsed directory and ownership
    // is preserved.
    QString fromSafe;
    QString toSafe;
    if ((args.size() != 4 && args.size() != 5) || !isUserName(args.at(0))
        || (args.size() == 5 && !isUserName(args.at(4)))) {
        printError(QStringLiteral(
            "rsync-copy requires <from-user> <from-dir under that home> <to-dir> <sync|copy> [<to-user>]"));
        return 1;
    }
    // Check the account homes first and stop at once: their message explains an
    // unsupported layout and must be the last line (the GUI shows only that).
    if (canonicalHome(args.at(0)).isEmpty() || (args.size() == 5 && canonicalHome(args.at(4)).isEmpty())) {
        return 1;
    }
    if (!resolveWithinHome(args.at(0), args.at(1), &fromSafe)) {
        printError(QStringLiteral("The source %1 is not inside the home folder of %2. Nothing was copied.")
                       .arg(args.at(1), args.at(0)));
        return 1;
    }
    const bool sync = args.at(3) == QLatin1String("sync");
    if (!sync && args.at(3) != QLatin1String("copy")) {
        printError(QStringLiteral("rsync-copy: mode must be 'sync' or 'copy'"));
        return 1;
    }
    const bool toAccount = args.size() == 5;
    uid_t toUid = 0;
    gid_t toGid = 0;
    if (toAccount) {
        const QString &toUser = args.at(4);
        // IDs are copied out: the static passwd buffer is reused by later lookups.
        const passwd *toPw = getpwnam(toUser.toLocal8Bit().constData());
        if (toPw == nullptr || toPw->pw_uid == 0) {
            printError(QStringLiteral("rsync-copy: invalid destination account"));
            return 1;
        }
        toUid = toPw->pw_uid;
        toGid = toPw->pw_gid;
        QString allowed;
        if (!resolveCopyDestination(args.at(2), &allowed) || !resolveWithinHome(toUser, allowed, &toSafe)) {
            printError(QStringLiteral("The destination %1 is not inside the home folder of %2 (it may link to "
                                      "another location). Nothing was copied.")
                           .arg(args.at(2), toUser));
            return 1;
        }
    } else if (!resolveCopyDestination(args.at(2), &toSafe)) {
        printError(QStringLiteral("rsync-copy: destination %1 is not allowed").arg(args.at(2)));
        return 1;
    }
    // A destination inside the source (or the reverse) would copy a tree into
    // itself, and in sync mode delete source files.
    if (toSafe == fromSafe || toSafe.startsWith(fromSafe + '/') || fromSafe.startsWith(toSafe + '/')) {
        printError(QStringLiteral("The source and destination folders overlap. Nothing was copied."));
        return 1;
    }
    QStringList rsyncArgs {"-a", "--info=progress2"};
    if (sync) {
        rsyncArgs << "--delete-after";
    }
    if (toAccount) {
        // Numeric IDs: the account's login group need not share its name.
        rsyncArgs << QStringLiteral("--chown=%1:%2").arg(toUid).arg(toGid);
    }
    // Preserve rsync's "copy directory contents" semantics: the source always
    // carried a trailing slash in the original call.
    rsyncArgs << (fromSafe + '/') << toSafe;
    return runToolStreaming(QStringLiteral("rsync"), rsyncArgs);
}

// In every section of the INI-style file `path` whose header is one of
// `sections`, replace each uncommented "key=<value>" line with an empty
// "key=", leaving all other lines untouched. Sections are tracked at their
// actual header lines, so comments mentioning a section and adjacent matching
// sections are handled. The file is rewritten atomically, and only if changed.
[[nodiscard]] bool clearIniValue(const QString &path, const QStringList &sections, const QString &key,
                                 const QString &value)
{
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        printError(QStringLiteral("Cannot read %1").arg(path));
        return false;
    }
    const QByteArray contents = in.readAll();
    if (in.error() != QFileDevice::NoError) {
        printError(QStringLiteral("Cannot read %1").arg(path)); // never rewrite from a partial read
        return false;
    }
    in.close();
    QStringList lines = QString::fromUtf8(contents).split('\n');
    if (!lines.isEmpty() && lines.constLast().isEmpty()) {
        lines.removeLast(); // trailing newline; re-added on write
    }

    bool inSection = false;
    bool changed = false;
    for (QString &line : lines) {
        const QString t = line.trimmed();
        if (t.startsWith('[')) {
            inSection = sections.contains(t);
            continue;
        }
        if (!inSection || t.startsWith('#') || t.startsWith(';')) {
            continue;
        }
        const qsizetype eq = t.indexOf('=');
        if (eq > 0 && t.left(eq).trimmed() == key && t.mid(eq + 1).trimmed() == value) {
            line = key + '=';
            changed = true;
        }
    }
    if (!changed) {
        return true;
    }

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)
        || out.write((lines.join('\n') + '\n').toUtf8()) < 0 || !out.commit()) {
        printError(QStringLiteral("Cannot write %1").arg(path));
        return false;
    }
    return true;
}

// Set `values` (key, value pairs) in every section of the INI-style file `path`
// whose header is one of `sections`, preserving all other lines and comments.
// Within a matching section, the first uncommented "key=" line is rewritten and
// later duplicates are dropped; a missing key is added right after the header.
// If no matching section exists, sections.first() is appended with the values.
// The file must already exist; it is replaced atomically.
[[nodiscard]] bool setIniValues(const QString &path, const QStringList &sections,
                                const QList<QPair<QString, QString>> &values)
{
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        printError(QStringLiteral("Cannot read %1").arg(path));
        return false;
    }
    const QByteArray contents = in.readAll();
    if (in.error() != QFileDevice::NoError) {
        printError(QStringLiteral("Cannot read %1").arg(path)); // never rewrite from a partial read
        return false;
    }
    in.close();
    QStringList lines = QString::fromUtf8(contents).split('\n');
    if (!lines.isEmpty() && lines.constLast().isEmpty()) {
        lines.removeLast(); // trailing newline; re-added on write
    }

    const auto isHeader = [](const QString &line) { return line.trimmed().startsWith('['); };
    const auto keyOf = [](const QString &line) -> QString {
        const QString t = line.trimmed();
        if (t.startsWith('#') || t.startsWith(';')) {
            return {};
        }
        const qsizetype eq = t.indexOf('=');
        return eq > 0 ? t.left(eq).trimmed() : QString();
    };

    bool foundSection = false;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (!sections.contains(lines.at(i).trimmed())) {
            continue;
        }
        foundSection = true;
        const qsizetype header = i;
        QList<QPair<QString, QString>> missing;
        for (const auto &[key, value] : values) {
            bool set = false;
            for (qsizetype j = header + 1; j < lines.size() && !isHeader(lines.at(j));) {
                if (keyOf(lines.at(j)) == key) {
                    if (set) {
                        lines.removeAt(j); // duplicate would override the value set above
                        continue;
                    }
                    lines[j] = key + '=' + value;
                    set = true;
                }
                ++j;
            }
            if (!set) {
                missing.append({key, value});
            }
        }
        for (qsizetype k = 0; k < missing.size(); ++k) {
            lines.insert(header + 1 + k, missing.at(k).first + '=' + missing.at(k).second);
        }
        // Continue scanning after this section's header; inserted lines are
        // plain key=value lines, never headers.
        i = header;
    }
    if (!foundSection) {
        if (!lines.isEmpty() && !lines.constLast().trimmed().isEmpty()) {
            lines.append(QString());
        }
        lines.append(sections.constFirst());
        for (const auto &[key, value] : values) {
            lines.append(key + '=' + value);
        }
    }

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)
        || out.write((lines.join('\n') + '\n').toUtf8()) < 0 || !out.commit()) {
        printError(QStringLiteral("Cannot write %1").arg(path));
        return false;
    }
    return true;
}

// The autologin user SDDM will actually use, and the file that sets it. Parsed
// with QSettings exactly as the GUI reads it, so both always agree.
[[nodiscard]] QString sddmEffectiveUser(QString *source = nullptr)
{
    QString user;
    for (const QString &file : sddmConfFiles(false)) {
        const QSettings conf(file, QSettings::IniFormat);
        if (conf.contains(QStringLiteral("Autologin/User"))) {
            user = conf.value(QStringLiteral("Autologin/User")).toString();
            if (source) {
                *source = file;
            }
        }
    }
    return user;
}

// Create `path` (and its directory) as an empty 0644 file if it does not exist.
// An existing file, including one another tool creates meanwhile, is left as is.
[[nodiscard]] bool ensureConfigFile(const QString &path)
{
    if (QFileInfo::exists(path)) {
        return true;
    }
    QFile file(path);
    if (QDir().mkpath(QFileInfo(path).path()) && file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        if (file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup
                                | QFileDevice::ReadOther)) {
            return true;
        }
    } else if (QFileInfo::exists(path)) {
        return true;
    }
    printError(QStringLiteral("Cannot create %1").arg(path));
    return false;
}

[[nodiscard]] int opAutologinEnable(const QStringList &args)
{
    // <user> <kde-session> <default-session> <lightdm|sddm|plasmalogin>
    static const QStringList managers {QStringLiteral("lightdm"), QStringLiteral("sddm"),
                                       QStringLiteral("plasmalogin")};
    if (args.size() != 4 || !isUserName(args.at(0)) || !isSessionToken(args.at(1)) || !isSessionToken(args.at(2))
        || !managers.contains(args.at(3))) {
        printError(QStringLiteral(
            "autologin-enable requires <user> <kde-session> <default-session> <lightdm|sddm|plasmalogin>"));
        return 1;
    }
    const QString user = args.at(0);
    const QString kdeSession = args.at(1);
    const QString defaultSession = args.at(2);
    const QString manager = args.at(3);

    // Only the running display manager (detected by the GUI) is configured:
    // editing a leftover config of another one would report success while the
    // active one still asks for a password. Its config is created if missing.
    if (manager == QLatin1String("lightdm")) {
        if (!ensureConfigFile(LightdmConf)) {
            return 1;
        }
        return setIniValues(LightdmConf, {QStringLiteral("[Seat:*]"), QStringLiteral("[SeatDefaults]")},
                            {{QStringLiteral("autologin-user"), user}})
            ? 0
            : 1;
    }
    if (manager == QLatin1String("sddm")) {
        // Without kde_settings.conf or sddm.conf, use sddm.conf: SDDM reads it
        // last, after every drop-in.
        QString sddm = sddmConfFile();
        if (sddm.isEmpty()) {
            sddm = SddmConf;
            if (!ensureConfigFile(sddm)) {
                return 1;
            }
        }
        QList<QPair<QString, QString>> values {{QStringLiteral("User"), user}};
        const QString session = sddm == SddmKdeConf ? kdeSession : defaultSession;
        if (!session.isEmpty()) {
            values.append({QStringLiteral("Session"), session});
        }
        if (!setIniValues(sddm, {QStringLiteral("[Autologin]")}, values)) {
            return 1;
        }
        QString source;
        if (sddmEffectiveUser(&source) != user) {
            printError(QStringLiteral("SDDM autologin is overridden by %1; autologin was not enabled").arg(source));
            return 1;
        }
        return 0;
    }
    if (!ensureConfigFile(PlasmaConf)) {
        return 1;
    }
    return setIniValues(PlasmaConf, {QStringLiteral("[Autologin]")}, {{QStringLiteral("User"), user}}) ? 0 : 1;
}

// Clear autologin only where it is set to exactly `user`; entries for other
// accounts are left untouched.
[[nodiscard]] int clearAutologinFor(const QString &user)
{
    int failed = 0;
    // Set an explicit empty value rather than deleting the line: lightdm.conf is
    // read after the lightdm.conf.d drop-ins, so a deleted key would let an
    // autologin-user set in a drop-in take effect again.
    if (QFileInfo::exists(LightdmConf)
        && !clearIniValue(LightdmConf, {QStringLiteral("[Seat:*]"), QStringLiteral("[SeatDefaults]")},
                          QStringLiteral("autologin-user"), user)) {
        failed = 1;
    }
    // Clear the user in every local SDDM file, since any of them may be the one
    // that wins; then confirm SDDM no longer resolves to this user.
    for (const QString &sddm : sddmConfFiles(true)) {
        failed |= runTool(QStringLiteral("sed"), {"-i", QString("s/^User=%1[[:space:]]*$/User=/").arg(sedLiteral(user)), sddm});
    }
    QString source;
    if (sddmEffectiveUser(&source) == user) {
        printError(QStringLiteral("SDDM autologin for %1 is still set in %2").arg(user, source));
        failed = 1;
    }
    if (QFileInfo::exists(PlasmaConf)) {
        failed |= runTool(QStringLiteral("sed"),
                {"-i", QString("s/^User=%1[[:space:]]*$/User=/").arg(sedLiteral(user)), PlasmaConf});
    }
    return failed != 0 ? 1 : 0;
}

[[nodiscard]] int opAutologinDisable(const QStringList &args)
{
    if (args.size() != 1 || !isUserName(args.constFirst())) {
        printError(QStringLiteral("autologin-disable requires one valid username"));
        return 1;
    }
    return clearAutologinFor(args.constFirst());
}

[[nodiscard]] int opAutologinRemoveUser(const QStringList &args)
{
    if (args.size() != 1 || !isUserName(args.constFirst())) {
        printError(QStringLiteral("autologin-remove-user requires one valid username"));
        return 1;
    }
    return clearAutologinFor(args.constFirst());
}

[[nodiscard]] int opAutologinRename(const QStringList &args)
{
    if (args.size() != 2 || !isUserName(args.at(0)) || !isUserName(args.at(1))) {
        printError(QStringLiteral("autologin-rename requires <old-user> <new-user>"));
        return 1;
    }
    const QString oldName = args.at(0);
    const QString newName = args.at(1);
    int failed = 0;
    if (QFileInfo::exists(LightdmConf)) {
        failed |= runTool(QStringLiteral("sed"),
                {"-i", "-E",
                 QString(R"(s/^([#[:space:]]*autologin-user=)%1([[:space:]]*)$/\1%2\2/)")
                     .arg(sedLiteral(oldName), newName),
                 LightdmConf});
    }
    for (const QString &sddm : sddmConfFiles(true)) {
        failed |= runTool(QStringLiteral("sed"),
                {"-i", QString("s/^User=%1[[:space:]]*$/User=%2/").arg(sedLiteral(oldName), newName), sddm});
    }
    if (QFileInfo::exists(PlasmaConf)) {
        failed |= runTool(QStringLiteral("sed"),
                {"-i", QString("s/^User=%1[[:space:]]*$/User=%2/").arg(sedLiteral(oldName), newName), PlasmaConf});
    }
    return failed != 0 ? 1 : 0;
}
} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    if (args.isEmpty()) {
        printError(QStringLiteral("Missing helper action"));
        return 1;
    }

    const QString action = args.constFirst();
    const QStringList rest = args.mid(1);

    if (action == QLatin1String("add-user")) {
        return opAddUser(rest);
    }
    if (action == QLatin1String("set-password")) {
        return opSetPassword(rest);
    }
    if (action == QLatin1String("group-add-member")) {
        return opGroupMember(rest, true);
    }
    if (action == QLatin1String("group-remove-member")) {
        return opGroupMember(rest, false);
    }
    if (action == QLatin1String("set-groups")) {
        return opSetGroups(rest);
    }
    if (action == QLatin1String("del-user")) {
        return opDelUser(rest);
    }
    if (action == QLatin1String("kill-user")) {
        return opKillUser(rest);
    }
    if (action == QLatin1String("add-group")) {
        return opAddGroup(rest);
    }
    if (action == QLatin1String("del-group")) {
        return opDelGroup(rest);
    }
    if (action == QLatin1String("rename-user")) {
        return opRenameUser(rest);
    }
    if (action == QLatin1String("remove-home-path")) {
        return opRemoveHomePath(rest);
    }
    if (action == QLatin1String("rewrite-home-paths")) {
        return opRewriteHomePaths(rest);
    }
    if (action == QLatin1String("clean-openoffice-locks")) {
        return opCleanOpenOfficeLocks(rest);
    }
    if (action == QLatin1String("rsync-copy")) {
        return opRsyncCopy(rest);
    }
    if (action == QLatin1String("autologin-enable")) {
        return opAutologinEnable(rest);
    }
    if (action == QLatin1String("autologin-disable")) {
        return opAutologinDisable(rest);
    }
    if (action == QLatin1String("autologin-remove-user")) {
        return opAutologinRemoveUser(rest);
    }
    if (action == QLatin1String("autologin-rename")) {
        return opAutologinRename(rest);
    }

    printError(QString("Unsupported helper action: %1").arg(action));
    return 1;
}
