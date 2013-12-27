#include "urtupdater.h"
#include "ui_urtupdater.h"

UrTUpdater::UrTUpdater(QWidget *parent) : QMainWindow(parent), ui(new Ui::UrTUpdater)
{
    ui->setupUi(this);

    updaterVersion = "4.0.1";
    password = "";
    downloadServer = -1;
    gameEngine = -1;
    currentVersion = -1;
    configFileExists = false;
    threadStarted = false;

    QMenu *menuFile = menuBar()->addMenu("&File");
    QMenu *menuHelp = menuBar()->addMenu("&Help");

    QAction *actionVersion = menuFile->addAction("&Version Selection");
    connect(actionVersion, SIGNAL(triggered()), this, SLOT(versionSelection()));

    QAction *actionEngine = menuFile->addAction("&Engine Selection");
    connect(actionEngine, SIGNAL(triggered()), this, SLOT(engineSelection()));

    QAction *actionDlServer = menuFile->addAction("&Download Server Selection");
    connect(actionDlServer, SIGNAL(triggered()), this, SLOT(serverSelection()));

    QAction *actionChangelog = menuFile->addAction("&Changelog");
    //connect(actionChangelog, SIGNAL(triggered()), this, SLOT(openChangelogPage()));

    QAction *actionAbout = menuHelp->addAction("&About");
    //connect(actionAbout, SIGNAL(triggered()), this, SLOT(openAboutPage()));

    QAction *actionHelp = menuHelp->addAction("&Get help");
    //connect(actionHelp, SIGNAL(triggered()), this, SLOT(openHelpPage()));
    actionHelp->setShortcut(QKeySequence("Ctrl+H"));

    QAction *actionQuitter = menuFile->addAction("&Quit");
    connect(actionQuitter, SIGNAL(triggered()), this, SLOT(quit()));
    actionQuitter->setShortcut(QKeySequence("Ctrl+Q"));

    dlBar = new QProgressBar(this);
    dlBar->move(150, 400);
    dlBar->setMinimumWidth(450);
    dlBar->hide();

    connect(qApp, SIGNAL(aboutToQuit()), this, SLOT(quit()));

    init();
}

UrTUpdater::~UrTUpdater()
{
    delete ui;
}

void UrTUpdater::init(){

    updaterPath = getCurrentPath();

    // Check if this is the first launch of the updater
    if(!QFile::exists(updaterPath + URT_GAME_SUBDIR)){
        QMessageBox msg;
        int result;

        msg.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        msg.setIcon(QMessageBox::Information);
        msg.setText("The game " URT_GAME_NAME " will be installed in this path:\n" + updaterPath + "\n"
                    + "To change the installation path, please click on \"Cancel\" and copy this Updater to where you want it to be installed.");
        result = msg.exec();

        // If we want to quit
        if(result == QMessageBox::Cancel){
            quit();
        }

        // Create the game folder
        else {
            if(!QDir().mkdir(updaterPath + URT_GAME_SUBDIR)){
                folderError(QString(updaterPath + URT_GAME_SUBDIR));
            }
        }
    }

    parseLocalConfig();
    getManifest("versionInfo");
}

QString UrTUpdater::getPlatform()
{
    #ifdef Q_OS_MAC
    return "Mac";
    #endif

    #ifdef Q_OS_LINUX
    return "Linux";
    #endif

    #ifdef Q_OS_WIN32
    return "Windows";
    #endif

    return "Linux";
}

QString UrTUpdater::getCurrentPath(){
    QDir dir = QDir(QCoreApplication::applicationDirPath());

    // If we're on Mac, 'dir' will contain the path to the executable
    // inside of the Updater's bundle which isn't what we want.
    // We need to cd ../../..
    if(getPlatform() == "Mac"){
        dir.cdUp();
        dir.cdUp();
        dir.cdUp();
    }

    return dir.absolutePath() + "/";
}

void UrTUpdater::parseLocalConfig(){
    QDomDocument *dom = new QDomDocument();
    QFile *f = new QFile(updaterPath + URT_UPDATER_CFG);

    if(!f->open(QFile::ReadOnly)){
        delete f;
        configFileExists = false;
        return;
    }

    dom->setContent(f);

    QDomNode node = dom->firstChild();
    while(!node.isNull()){
        if(node.toElement().nodeName() == "UpdaterConfig"){
            QDomNode conf = node.firstChild();

            while(!conf.isNull()){
                if(conf.toElement().nodeName() == "DownloadServer"){
                    downloadServer = conf.toElement().text().toInt();
                }
                if(conf.toElement().nodeName() == "GameEngine"){
                    gameEngine = conf.toElement().text().toInt();
                }
                if(conf.toElement().nodeName() == "CurrentVersion"){
                    currentVersion = conf.toElement().text().toInt();
                }
                conf = conf.nextSibling();
            }
        }
        node = node.nextSibling();
    }

    configFileExists = true;

    f->close();

    delete f;
    delete dom;
}

void UrTUpdater::saveLocalConfig(){
    QFile *f = new QFile(updaterPath + URT_UPDATER_CFG);
    QXmlStreamWriter *xml = new QXmlStreamWriter();

    if(!f->open(QFile::WriteOnly)){
        QMessageBox::critical(0, "Could not write the config file", "Could not write the Updater config file inside of the game folder. Your updater preferences won't be saved.");
        delete f;
        return;
    }

    xml->setDevice(f);
    xml->writeStartDocument();
    xml->writeStartElement("UpdaterConfig");

    xml->writeStartElement("DownloadServer");
    xml->writeCharacters(QString::number(downloadServer));
    xml->writeEndElement();

    xml->writeStartElement("GameEngine");
    xml->writeCharacters(QString::number(gameEngine));
    xml->writeEndElement();

    xml->writeStartElement("CurrentVersion");
    xml->writeCharacters(QString::number(currentVersion));
    xml->writeEndElement();

    xml->writeEndElement();
    xml->writeEndDocument();

    f->close();

    qDebug() << "Local config saved." << endl;

    delete f;
    delete xml;
}

void UrTUpdater::getManifest(QString query){
    QUrl APIUrl(URT_API_LINK);
    QUrlQuery url;
    QNetworkRequest apiRequest(APIUrl);
    QNetworkAccessManager *apiManager = new QNetworkAccessManager(this);

    apiRequest.setHeader(QNetworkRequest::ContentTypeHeader,"application/x-www-form-urlencoded;charset=utf-8");
    url.addQueryItem("platform", getPlatform());
    url.addQueryItem("query", query);
    url.addQueryItem("password", password);

    apiAnswer = apiManager->post(apiRequest, url.query(QUrl::FullyEncoded).toUtf8());
    connect(apiAnswer, SIGNAL(finished()), this, SLOT(parseAPIAnswer()));
    connect(apiAnswer, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(networkError(QNetworkReply::NetworkError)));
}

void UrTUpdater::parseAPIAnswer(){
    QByteArray  apiByteAnswer = apiAnswer->readAll();
    QString     apiData = QString(apiByteAnswer);

    parseManifest(apiData);
}

void UrTUpdater::parseManifest(QString data){
    QDomDocument* dom = new QDomDocument();
    dom->setContent(data);

    filesToDownload.clear();
    downloadServers.clear();
    enginesList.clear();
    versionsList.clear();
    newsList.clear();

    QDomNode node = dom->firstChild();

    while(!node.isNull()){
        if(node.toElement().nodeName() == "Updater"){
            QDomNode updater = node.firstChild();

            while(!updater.isNull()){

                if(updater.toElement().nodeName() == "VersionInfo"){
                    QDomNode versionInfo = updater.firstChild();

                    while(!versionInfo.isNull()){
                        if(versionInfo.nodeName() == "VersionNumber"){
                            versionNumber = versionInfo.toElement().text();
                        }
                        if(versionInfo.nodeName() == "ReleaseDate"){
                            releaseDate = versionInfo.toElement().text();
                        }
                        versionInfo = versionInfo.nextSibling();
                    }
                }

                else if(updater.toElement().nodeName() == "NewsList"){
                    QDomNode newsListNode = updater.firstChild();

                    while(!newsListNode.isNull()){
                        if(newsListNode.nodeName() == "NewsText"){
                            newsList.append(newsListNode.toElement().text());
                        }
                        newsListNode = newsListNode.nextSibling();
                    }
                }

                else if(updater.toElement().nodeName() == "ServerList"){
                    QDomNode serverListNode = updater.firstChild();

                    while(!serverListNode.isNull()){
                        if(serverListNode.nodeName() == "Server"){
                            QDomNode serverNode = serverListNode.firstChild();
                            int     serverId;
                            QString serverURL;
                            QString serverName;
                            QString serverLocation;
                            serverInfo_s si;

                            while(!serverNode.isNull()){
                                if(serverNode.nodeName() == "ServerName"){
                                    serverName = serverNode.toElement().text();
                                }
                                if(serverNode.nodeName() == "ServerURL"){
                                    serverURL = serverNode.toElement().text();
                                }
                                if(serverNode.nodeName() == "ServerLocation"){
                                    serverLocation = serverNode.toElement().text();
                                }
                                if(serverNode.nodeName() == "ServerId"){
                                    serverId = serverNode.toElement().text().toInt();
                                }
                                serverNode = serverNode.nextSibling();
                            }

                            si.serverId = serverId;
                            si.serverName = serverName;
                            si.serverURL = serverURL;
                            si.serverLocation = serverLocation;
                            downloadServers.append(si);
                        }
                        serverListNode = serverListNode.nextSibling();
                    }
                }

                else if(updater.toElement().nodeName() == "EngineList"){
                    QDomNode engineListNode = updater.firstChild();

                    while(!engineListNode.isNull()){
                        if(engineListNode.nodeName() == "Engine"){
                            QDomNode engineNode = engineListNode.firstChild();
                            int     engineId;
                            QString engineDir;
                            QString engineName;
                            engineInfo_s ei;

                            while(!engineNode.isNull()){
                                if(engineNode.nodeName() == "EngineName"){
                                    engineName = engineNode.toElement().text();
                                }
                                if(engineNode.nodeName() == "EngineDir"){
                                    engineDir = engineNode.toElement().text();
                                }
                                if(engineNode.nodeName() == "EngineId"){
                                    engineId = engineNode.toElement().text().toInt();
                                }
                                engineNode = engineNode.nextSibling();
                            }

                            ei.engineId = engineId;
                            ei.engineName = engineName;
                            ei.engineDir = engineDir;
                            enginesList.append(ei);
                        }
                        engineListNode = engineListNode.nextSibling();
                    }
                }

                else if(updater.toElement().nodeName() == "VersionList"){
                    QDomNode versionListNode = updater.firstChild();

                    while(!versionListNode.isNull()){
                        if(versionListNode.nodeName() == "Version"){
                            QDomNode versionNode = versionListNode.firstChild();
                            int     versionId;
                            QString versionName;
                            versionInfo_s vi;

                            while(!versionNode.isNull()){
                                if(versionNode.nodeName() == "VersionName"){
                                    versionName = versionNode.toElement().text();
                                }
                                if(versionNode.nodeName() == "VersionId"){
                                    versionId = versionNode.toElement().text().toInt();
                                }
                                versionNode = versionNode.nextSibling();
                            }

                            vi.versionId = versionId;
                            vi.versionName = versionName;
                            versionsList.append(vi);
                        }
                        versionListNode = versionListNode.nextSibling();
                    }
                }

                else if(updater.toElement().nodeName() == "Files"){
                    QDomNode files = updater.firstChild();

                    while(!files.isNull()){
                        if(files.nodeName() == "File"){
                            QDomNode fileInfo = files.firstChild();
                            QString fileDir;
                            QString fileName;
                            QString fileMd5;
                            QString fileSize;
                            bool mustDownload = false;

                            while(!fileInfo.isNull()){
                                if(fileInfo.nodeName() == "FileDir"){
                                    fileDir = fileInfo.toElement().text();
                                }
                                if(fileInfo.nodeName() == "FileName"){
                                    fileName = fileInfo.toElement().text();
                                }
                                if(fileInfo.nodeName() == "FileMD5"){
                                    fileMd5 = fileInfo.toElement().text();
                                }
                                if(fileInfo.nodeName() == "FileSize"){
                                    fileSize = fileInfo.toElement().text();
                                }
                                fileInfo = fileInfo.nextSibling();
                            }

                            QString filePath(updaterPath + fileDir + fileName);
                            QFile* f = new QFile(filePath);

                            // If the file does not exist, it must be downloaded.
                            if(!f->exists()){
                                mustDownload = true;
                            }

                            // If the md5 string is empty, it means that the API wants
                            // us to delete this file if needed
                            else if(!fileName.isEmpty() && fileMd5.isEmpty()){
                                QFile::remove(filePath);
                            }

                            // Check the file's md5sum to see if it needs to be updated.
                            else if(!fileName.isEmpty() && !fileMd5.isEmpty()){
                                if(getMd5Sum(f) != fileMd5){
                                    mustDownload = true;
                                    qDebug() << "md5 file: " << getMd5Sum(f) << ", " << fileMd5 << endl;
                                }
                            }

                            qDebug() << "fileDir:" << fileDir << endl;
                            qDebug() << "fileName: " << fileName << endl;
                            qDebug() << "fileMd5: " << fileMd5 << endl;

                            if(mustDownload){
                                fileInfo_s fi;
                                fi.fileName = fileName;
                                fi.filePath = fileDir;
                                fi.fileMd5 = fileMd5;
                                fi.fileSize = fileSize;
                                filesToDownload.append(fi);
                            }

                            delete f;
                        }
                        files = files.nextSibling();
                    }
                }
                updater = updater.nextSibling();
            }
        }
        node = node.nextSibling();
    }

    checkDownloadServer();
    checkGameEngine();
    checkVersion();
    drawNews();

    if(!threadStarted){
        startDlThread();
    }

    downloadFiles();

    delete dom;
}

void UrTUpdater::startDlThread(){
    if(threadStarted){
        return;
    }

    dlThread = new QThread();
    dl = new Download(getServerUrlById(downloadServer), updaterPath);
    dl->moveToThread(dlThread);

    connect(dlThread, SIGNAL(started()), dl, SLOT(init()));
    connect(dlThread, SIGNAL(finished()), dlThread, SLOT(deleteLater()));

    connect(dl, SIGNAL(dlError(QNetworkReply::NetworkError)), this, SLOT(networkError(QNetworkReply::NetworkError)));
    connect(dl, SIGNAL(folderError(QString)), this, SLOT(folderError(QString)));
    connect(dl, SIGNAL(fileDownloaded()), this, SLOT(fileDownloaded()));
    connect(dl, SIGNAL(bytesDownloaded(int)), this, SLOT(bytesDownloaded(int)));

    connect(this, SIGNAL(dlFile(QString,QString)), dl, SLOT(downloadFile(QString, QString)));

    threadStarted = true;
    dlThread->start();
}

void UrTUpdater::downloadFiles(){
    if(filesToDownload.size() <= 0){
        dlBar->setRange(0, 100);
        dlBar->setValue(100);
        dlBar->show();

        qDebug() << "done!";
        return;
    }

    if(filesToDownload.size() > 0){
        qDebug() << "files to dl";
        currentFile = filesToDownload.takeFirst();
        dlBar->setRange(0, currentFile.fileSize.toInt());
        dlBar->setValue(0);
        dlBar->show();

        emit dlFile(currentFile.filePath, currentFile.fileName);
    }
}

void UrTUpdater::fileDownloaded(){
    if(filesToDownload.size() > 0){
        currentFile = filesToDownload.takeFirst();
        dlBar->setRange(0, currentFile.fileSize.toInt());
        dlBar->setValue(0);

        emit dlFile(currentFile.filePath, currentFile.fileName);
    }

    else {
        dlBar->setRange(0, 100);
        dlBar->setValue(100);
        dlBar->show();
        filesToDownload.clear();
        getManifest("versionInfo");
    }
}

void UrTUpdater::checkDownloadServer(){
    QList<serverInfo_s>::iterator li;
    bool found = false;

    // Check if the download server that is stored in the config file still exists
    for(li = downloadServers.begin(); li != downloadServers.end(); ++li){
        if(li->serverId == downloadServer){
            found = true;
        }
    }

    // If the engine isn't available anymore, pick the first one in the list
    if(!found){
        downloadServer = downloadServers.at(0).serverId;
    }
}

void UrTUpdater::checkGameEngine(){
    QList<engineInfo_s>::iterator li;
    bool found = false;

    // Check if the engine that is stored in the config file still exists
    for(li = enginesList.begin(); li != enginesList.end(); ++li){
        if(li->engineId == gameEngine){
            found = true;
        }
    }

    // If the server isn't a mirror anymore, pick the first one in the list
    if(!found){
        gameEngine = enginesList.at(0).engineId;
    }
}

void UrTUpdater::checkVersion(){
    QList<versionInfo_s>::iterator li;
    bool found = false;

    // Check if the version that is stored in the config file still exists
    for(li = versionsList.begin(); li != versionsList.end(); ++li){
        if(li->versionId == currentVersion){
            found = true;
        }
    }

    // If the version isn't available anymore, pick the first one in the list
    if(!found){
        currentVersion = versionsList.at(0).versionId;
    }
}

void UrTUpdater::drawNews(){
    QList<QString>::iterator li;
    int i = 0;

    for(li = newsList.begin(); li != newsList.end(); ++li, i++){
        QLabel* news = new QLabel(this);

        news->move(170, 50 + (i*26));
        news->setMinimumWidth(450);
        news->setText(*li);
        news->setOpenExternalLinks(true);
        news->setVisible(true);
    }
}

QString UrTUpdater::getMd5Sum(QFile* file)
{
    if (file->exists() && file->open(QIODevice::ReadOnly))
    {
        QByteArray content = file->readAll();
        QByteArray hashed = QCryptographicHash::hash(content, QCryptographicHash::Md5);
        file->close();

        return hashed.toHex().data();
    }

    return "";
}

void UrTUpdater::networkError(QNetworkReply::NetworkError code){
    QString error = "";
    bool critical = false;

    switch(code){
        case QNetworkReply::ConnectionRefusedError:
            error = "Error: the remote server refused the connection. Please try again later.";
            critical = true;
            break;

        case QNetworkReply::RemoteHostClosedError:
            error = "Error: the remote server closed the connection prematurely. Please try again later.";
            break;

        case QNetworkReply::HostNotFoundError:
            error = "Error: the remote server could not be found. Please check your internet connection!";
            critical = true;
            break;

        case QNetworkReply::TimeoutError:
            error = "Error: the connection to the remote server timed out. Please try again.";
            break;

        case QNetworkReply::TemporaryNetworkFailureError:
            error = "Error: the connection to the remote server was broken due to disconnection from the network. Please try again.";
            break;

        case QNetworkReply::ContentNotFoundError:
            error = "Error: the remote content could not be found. Please report this issue on our website: http://www.urbanterror.info";
            break;

        case QNetworkReply::UnknownNetworkError:
            error = "Error: an unknown network-related error was encountered. Please try again";
            break;

        case QNetworkReply::UnknownContentError:
            error = "Error: an unknown content-related error was encountered. Please try again";
            break;

        default:
        case QNetworkReply::NoError:
            break;
    }

    if(!error.isEmpty()){
        if(critical == true){
            QMessageBox::critical(0, "Download error", error);
            quit();
        }
        else {
            QMessageBox::information(0, "Download error", error);
        }
    }
}

void UrTUpdater::folderError(QString folder){
    QMessageBox::critical(this, folder + " file/folder", "Could not create the file/folder " + folder + ".\n"
                          + "Please move the updater to a folder where it has sufficient permissions.");
    quit();
}

void UrTUpdater::serverSelection(){
    ServerSelection *serverSel = new ServerSelection(this);

    connect(serverSel, SIGNAL(serverSelected(int)), this, SLOT(setDownloadServer(int)));

    serverSel->currentServer = downloadServer;
    serverSel->downloadServers = downloadServers;
    serverSel->init();
    serverSel->exec();
}

void UrTUpdater::engineSelection(){
    EngineSelection* engineSel = new EngineSelection(this);

    connect(engineSel, SIGNAL(engineSelected(int)), this, SLOT(setEngine(int)));

    engineSel->currentEngine = gameEngine;
    engineSel->enginesList = enginesList;
    engineSel->init();
    engineSel->exec();
}

void UrTUpdater::versionSelection(){
    VersionSelection* versionSel = new VersionSelection(this);

    connect(versionSel, SIGNAL(versionSelected(int)), this, SLOT(setVersion(int)));
    connect(versionSel, SIGNAL(passwordEntered(QString)), this, SLOT(setPassword(QString)));

    versionSel->currentVersion = currentVersion;
    versionSel->versionsList = versionsList;
    versionSel->password = password;
    versionSel->init();
    versionSel->exec();
}

void UrTUpdater::setDownloadServer(int server){
    downloadServer = server;
    saveLocalConfig();
}

void UrTUpdater::setEngine(int engine){
    gameEngine = engine;
    saveLocalConfig();
}

void UrTUpdater::setVersion(int version){
    currentVersion = version;
    saveLocalConfig();
}

void UrTUpdater::setPassword(QString pw){
    password = pw;
    getManifest("versionInfo");
    versionSelection();
}

QString UrTUpdater::getServerUrlById(int id){
    QList<serverInfo_s>::iterator li;

    for(li = downloadServers.begin(); li != downloadServers.end(); ++li){
        if(li->serverId == id){
            return li->serverURL;
        }
    }

    return "";
}

void UrTUpdater::bytesDownloaded(int bytes){
    downloadedBytes += bytes;
    dlBar->setValue(downloadedBytes);
    qDebug() << "bytes dl: " << bytes << ", " << downloadedBytes;
}

void UrTUpdater::quit(){
    saveLocalConfig();
    exit(0);
}
