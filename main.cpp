#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOffscreenSurface>
#include <QPushButton>
#include <QProcess>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QSurfaceFormat>
#include <QSet>
#include <QUrl>
#include <QDesktopServices>
#include <QDateTime>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

class Launcher : public QWidget
{
public:
    Launcher()
    {
        setWindowTitle("Atrim Linux");
        resize(620, 360);

        auto *layout = new QVBoxLayout(this);

        auto *intro = new QLabel(
            "<b>Atrim Linux</b><br>"
            "Install Atrim into an existing Amnesia: The Dark Descent folder."
        );
        intro->setWordWrap(true);

        folderLabel = new QLabel("No game folder selected");
        folderLabel->setWordWrap(true);
        folderLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

        auto *repoLink = new QLabel(
            "<a href=\"https://github.com/roozerxc/atrim\">"
            "Atrim GitHub repository</a>"
        );
        repoLink->setOpenExternalLinks(true);

        auto *scanButton = new QPushButton("Scan Steam and Heroic locations");
        auto *browseButton = new QPushButton("Choose game folder…");

        versionBox = new QComboBox;
        versionBox->addItem(
            "Stable — v1.4.5-win32",
            "https://github.com/roozerxc/atrim/releases/download/"
            "v1.4.5-win32/v1.4.5-win32-release.zip"
        );
        versionBox->addItem(
            "Beta — v1.4.6-win32",
            "https://github.com/roozerxc/atrim/releases/download/"
            "v1.4.6-win32-beta/v1.4.6-win32-beta.zip"
        );
        versionBox->addItem(
            "Beta — v1.4.6-win64",
            "https://github.com/roozerxc/atrim/releases/download/"
            "v1.4.6-win32-beta/v1.4.6-win64-beta.zip"
        );

        auto *installButton = new QPushButton("Install selected Atrim version");
        auto *glButton = new QPushButton("Check OpenGL");

        statusLabel = new QLabel;
        statusLabel->setWordWrap(true);

        layout->addWidget(intro);
        layout->addWidget(repoLink);
        layout->addWidget(folderLabel);
        layout->addWidget(scanButton);
        layout->addWidget(browseButton);
        layout->addWidget(new QLabel("Atrim version:"));
        layout->addWidget(versionBox);
        layout->addWidget(installButton);
        layout->addWidget(glButton);
        layout->addWidget(statusLabel);

        connect(scanButton, &QPushButton::clicked,
                this, [this] { scanLocations(); });

        connect(browseButton, &QPushButton::clicked,
                this, [this] { chooseFolder(); });

        connect(installButton, &QPushButton::clicked,
                this, [this] { installSelected(); });

        connect(glButton, &QPushButton::clicked,
                this, [this] { checkOpenGL(); });
    }

private:
    QLabel *folderLabel = nullptr;
    QLabel *statusLabel = nullptr;
    QComboBox *versionBox = nullptr;
    QString selectedFolder;

    static bool isAmnesiaInstall(const QString &path)
    {
        const QDir dir(path);

        // Retail installations vary between Steam, GOG, DVD, and older ports.
        return QFileInfo(dir.filePath("Amnesia.exe")).isFile()
            || QFileInfo(dir.filePath("amnesia-Win32-Release.exe")).isFile()
            || QFileInfo(dir.filePath("amnesia-x64-Release.exe")).isFile()
            || QFileInfo(dir.filePath("resources.cfg")).isFile();
    }

    void selectFolder(const QString &path)
    {
        selectedFolder = QDir(path).absolutePath();
        folderLabel->setText("Selected: " + selectedFolder);
        statusLabel->setText("Game folder selected.");
    }

    void chooseFolder()
    {
        const QString path = QFileDialog::getExistingDirectory(
            this,
            "Choose Amnesia game folder",
            QDir::homePath()
        );

        if (path.isEmpty())
            return;

        if (!isAmnesiaInstall(path)) {
            const auto answer = QMessageBox::question(
                this,
                "Folder not recognized",
                "This folder was not confidently identified as an "
                "Amnesia installation. Use it anyway?",
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No
            );

            if (answer != QMessageBox::Yes)
                return;
        }

        selectFolder(path);
    }

    void scanLocations()
    {
        const QString home = QDir::homePath();

        QStringList roots = {
            home + "/.steam/steam/steamapps/common",
            home + "/.local/share/Steam/steamapps/common",
            home + "/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common",
            home + "/Games",
            home + "/.config/heroic",
            home + "/Games/Heroic"
        };

        const QStringList steamRoots = {
            home + "/.steam/steam",
            home + "/.local/share/Steam",
            home + "/.var/app/com.valvesoftware.Steam/.local/share/Steam"
        };

        const QRegularExpression pathRe(
            R"rx("path"\s*"([^"]+)")rx"
        );

        for (const QString &steamRoot : steamRoots) {
            QFile file(steamRoot + "/steamapps/libraryfolders.vdf");
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;

            const QString text = QString::fromUtf8(file.readAll());
            auto matches = pathRe.globalMatch(text);

            while (matches.hasNext()) {
                QString library = matches.next().captured(1);
                library.replace("\\", "/");
                roots << QDir(library).filePath("steamapps/common");
            }
        }

        for (const QStorageInfo &volume : QStorageInfo::mountedVolumes()) {
            const QString mount = QDir::cleanPath(volume.rootPath());

            if (mount.startsWith("/mnt/")
                || mount.startsWith("/media/")
                || mount.startsWith("/run/media/")) {
                roots << mount;
            }
        }

        roots.removeDuplicates();

        QStringList found;
        QSet<QString> visited;
        int visitedCount = 0;
        constexpr int maxVisited = 20000;
        constexpr int maxDepth = 5;

        std::function<void(const QString &, int)> walk =
            [&](const QString &path, int depth) {
                if (visitedCount >= maxVisited || depth < 0)
                    return;

                const QString canonical =
                    QFileInfo(path).canonicalFilePath();

                if (canonical.isEmpty() || visited.contains(canonical))
                    return;

                visited.insert(canonical);

                QDir dir(canonical);
                if (!dir.exists())
                    return;

                ++visitedCount;

                if (isAmnesiaInstall(canonical)) {
                    found << canonical;
                    return;
                }

                if (depth == 0)
                    return;

                const QFileInfoList children = dir.entryInfoList(
                    QDir::Dirs
                    | QDir::NoDotAndDotDot
                    | QDir::Readable
                    | QDir::NoSymLinks,
                    QDir::Name
                );

                for (const QFileInfo &child : children) {
                    const QString name = child.fileName();

                    if (name == ".cache"
                        || name == "shadercache"
                        || name == "compatdata"
                        || name == "workshop") {
                        continue;
                    }

                    walk(child.absoluteFilePath(), depth - 1);

                    if (visitedCount >= maxVisited)
                        return;
                }
            };

        for (const QString &root : roots) {
            walk(root, maxDepth);

            if (visitedCount >= maxVisited)
                break;
        }

        found.removeDuplicates();
        found.sort(Qt::CaseInsensitive);

        if (found.isEmpty()) {
            statusLabel->setText(
                "No installation found. Choose the game folder manually."
            );
            return;
        }

        bool accepted = false;

        const QString selected = QInputDialog::getItem(
            this,
            "Atrim Linux installations found",
            "Choose the game folder:",
            found,
            0,
            false,
            &accepted
        );

        if (accepted && !selected.isEmpty())
            selectFolder(selected);
    }

    void installSelected()
    {
        if (selectedFolder.isEmpty()) {
            QMessageBox::warning(
                this,
                "No game folder",
                "Choose or scan for an Amnesia game folder first."
            );
            return;
        }

        const QString url = versionBox->currentData().toString();
        const QString version = versionBox->currentText();

        const auto answer = QMessageBox::question(
            this,
            "Confirm installation",
            "Install " + version + " into:\n\n"
            + selectedFolder
            + "\n\nExisting files will be backed up first.",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );

        if (answer != QMessageBox::Yes)
            return;

        const QString tempRoot =
            QDir::tempPath() + "/atrim-linux-install";

        QDir(tempRoot).removeRecursively();
        QDir().mkpath(tempRoot);

        const QString archive = tempRoot + "/atrim.zip";
        const QString extracted = tempRoot + "/extracted";

        statusLabel->setText("Downloading " + version + "…");
        QApplication::processEvents();

        QProcess curl;
        curl.setProgram("curl");
        curl.setArguments({
            "--fail",
            "--location",
            "--silent",
            "--show-error",
            "--output",
            archive,
            url
        });
        curl.start();

        if (!curl.waitForFinished(-1)
            || curl.exitStatus() != QProcess::NormalExit
            || curl.exitCode() != 0) {
            QMessageBox::critical(
                this,
                "Download failed",
                curl.readAllStandardError()
            );
            QDir(tempRoot).removeRecursively();
            return;
        }

        QDir().mkpath(extracted);

        statusLabel->setText("Extracting archive…");
        QApplication::processEvents();

        QProcess unzip;
        unzip.setProgram("unzip");
        unzip.setArguments({"-q", archive, "-d", extracted});
        unzip.start();

        if (!unzip.waitForFinished(-1)
            || unzip.exitStatus() != QProcess::NormalExit
            || unzip.exitCode() != 0) {
            QMessageBox::critical(
                this,
                "Extraction failed",
                unzip.readAllStandardError()
            );
            QDir(tempRoot).removeRecursively();
            return;
        }

        const QString backupRoot =
            QDir::homePath()
            + "/.local/share/atrim-backups/"
            + QDateTime::currentDateTime().toString("yyyyMMdd-hHmmss");

        QDir().mkpath(backupRoot);

        // Back up Amnesia user data from this Heroic Proton prefix.
        const QString saveSource =
            QDir::homePath()
            + "/Games/Heroic/Prefixes/Amnesia  The Dark Descent/"
              "pfx/drive_c/users/steamuser/Documents/Amnesia";
        if (QFileInfo(saveSource).isDir()) {
            QProcess saveBackup;
            saveBackup.setProgram("cp");
            saveBackup.setArguments({
                "-a",
                saveSource,
                backupRoot + "/Amnesia-saves"
            });
            saveBackup.start();

            if (!saveBackup.waitForFinished(-1)
                || saveBackup.exitStatus() != QProcess::NormalExit
                || saveBackup.exitCode() != 0) {
                QMessageBox::critical(
                    this,
                    "Save backup failed",
                    saveBackup.readAllStandardError()
                );
                QDir(tempRoot).removeRecursively();
                return;
            }
        }

        const QStringList files = {
            "SDL.dll",
            "amnesia-Win32-Release.exe",
            "amnesia-x64-Release.exe",
            "amnesia.bmp"
        };

        for (const QString &file : files) {
            const QString source = selectedFolder + "/" + file;

            if (QFileInfo::exists(source)) {
                QDir().mkpath(backupRoot);
                QFile::copy(source, backupRoot + "/" + file);
            }
        }

        statusLabel->setText("Installing files…");
        QApplication::processEvents();

        QProcess copy;
        copy.setProgram("cp");
        copy.setArguments({
            "-a",
            extracted + "/.",
            selectedFolder + "/"
        });
        copy.start();

        if (!copy.waitForFinished(-1)
            || copy.exitStatus() != QProcess::NormalExit
            || copy.exitCode() != 0) {
            QMessageBox::critical(
                this,
                "Installation failed",
                copy.readAllStandardError()
            );
            QDir(tempRoot).removeRecursively();
            return;
        }

        QDir(tempRoot).removeRecursively();

        statusLabel->setText(
            "Installation complete.\nBackup saved at:\n" + backupRoot
        );

        QMessageBox::information(
            this,
            "Atrim installed",
            "Atrim was installed successfully.\n\n"
            "On Linux, launch the Windows executable through Proton or "
            "another compatible Windows runtime."
        );
    }

    void checkOpenGL()
    {
        QSurfaceFormat requested;
        requested.setRenderableType(QSurfaceFormat::OpenGL);
        requested.setVersion(2, 1);

        QOpenGLContext context;
        context.setFormat(requested);

        if (!context.create()) {
            statusLabel->setText(
                "Could not create an OpenGL context."
            );
            return;
        }

        QOffscreenSurface surface;
        surface.setFormat(context.format());
        surface.create();

        if (!surface.isValid()) {
            statusLabel->setText(
                "Could not create an offscreen OpenGL surface."
            );
            return;
        }

        if (!context.makeCurrent(&surface)) {
            statusLabel->setText(
                "Could not make the OpenGL context current."
            );
            return;
        }

        QOpenGLFunctions *gl = context.functions();

        if (!gl) {
            context.doneCurrent();
            statusLabel->setText(
                "Could not get OpenGL functions."
            );
            return;
        }

        gl->initializeOpenGLFunctions();

        const auto getString = [gl](GLenum name) {
            const GLubyte *value = gl->glGetString(name);

            return value
                ? QString::fromLatin1(
                    reinterpret_cast<const char *>(value)
                  )
                : QStringLiteral("Unavailable");
        };

        const QString details =
            "OpenGL context created."
            "\nVendor: " + getString(GL_VENDOR)
            + "\nRenderer: " + getString(GL_RENDERER)
            + "\nVersion: " + getString(GL_VERSION);

        context.doneCurrent();
        statusLabel->setText(details);
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    Launcher window;
    window.show();

    return app.exec();
}
