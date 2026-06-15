#pragma once

#include <QProcess>

enum struct QuietMode { No, Yes };

class Cmd : public QProcess
{
    Q_OBJECT
public:
    explicit Cmd(QObject *parent = nullptr);

    bool proc(const QString &cmd, const QStringList &args = {}, QString *output = nullptr,
              const QByteArray *input = nullptr, QuietMode quiet = QuietMode::No);
    bool run(const QString &cmd, QString *output = nullptr, const QByteArray *input = nullptr,
             QuietMode quiet = QuietMode::No);
    // Invoke a structured, validated operation in the privileged helper. The first
    // element of actionArgs is the operation name; the rest are its scalar parameters.
    bool runHelper(const QStringList &actionArgs, QString *output = nullptr, const QByteArray *input = nullptr,
                   QuietMode quiet = QuietMode::No);
    [[nodiscard]] QString getOut(const QString &cmd, QuietMode quiet = QuietMode::No);

    [[nodiscard]] QString readAllOutput() const;

signals:
    void done();
    void errorAvailable(const QString &err);
    void outputAvailable(const QString &out);

private slots:
    void handleStandardError();
    void handleStandardOutput();

private:
    QString elevationCommand;
    QString helper;
    QString outBuffer;

    static constexpr int EXIT_CODE_COMMAND_NOT_FOUND = 127;
    static constexpr int EXIT_CODE_PERMISSION_DENIED = 126;

    bool helperProc(const QStringList &helperArgs, QString *output = nullptr, const QByteArray *input = nullptr,
                    QuietMode quiet = QuietMode::No);
    void handleElevationError();
};
