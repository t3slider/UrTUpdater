#include "download.h"

Download::Download(QString server, QString _updaterPath) : downloadServer(server), updaterPath(_updaterPath){
}

Download::~Download(){

}

void Download::init(){
    downloadInProgress = false;
    downloadedBytes = 0;

    url = new QUrl();
    url->setScheme("http");
    url->setHost(downloadServer);

    qDebug() << "downloadServer: " << downloadServer;

    http = new QNetworkAccessManager();
    connect(http, SIGNAL(finished(QNetworkReply*)), this, SLOT(downloadFinished(QNetworkReply*)));
}

void Download::setDownloadServer(QString server){
    downloadServer = server;
}

void Download::downloadFile(QString folder, QString file){
    QUrl fileUrl(*url);
    QNetworkRequest request;

    currentDownload = new QFile(updaterPath + folder + file);
    downloadedBytes = 0;
    currentFile = file;
    currentFolder = folder;

    fileUrl.setPath(updaterPath + folder + file);
    request.setUrl(fileUrl);

    // Check if we have to create the folder
    if(!currentFolder.isEmpty() && !QDir().exists(updaterPath + currentFolder)){
        if(!QDir().mkdir(updaterPath + currentFolder)){
            emit folderError(QString(updaterPath + currentFolder));
            exit(1);
        }
    }

    // If the file already exists, remove it
    if(currentDownload->exists()){
        currentDownload->remove();
    }

    // Open the file in write mode
    currentDownload->open(QIODevice::WriteOnly);

    reply = http->get(request);
    reply->setReadBufferSize(64 * 2014);
    downloadInProgress = true;

    connect(reply, SIGNAL(readyRead()), this, SLOT(filePart()));
    connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(downloadError(QNetworkReply::NetworkError)));
}

void Download::filePart(){
    int count = currentDownload->write(reply->readAll());
    downloadedBytes += count;
    emit bytesDownloaded(count);
}

void Download::downloadError(QNetworkReply::NetworkError code){
    emit dlError(code);
}
