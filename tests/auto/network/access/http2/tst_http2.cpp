/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the test suite of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL-EXCEPT$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <QtTest/QtTest>

#include "http2srv.h"

#include <QtNetwork/qnetworkaccessmanager.h>
#include <QtNetwork/qnetworkrequest.h>
#include <QtNetwork/qnetworkreply.h>
#include <QtCore/qglobal.h>
#include <QtCore/qobject.h>
#include <QtCore/qthread.h>
#include <QtCore/qurl.h>

#ifndef QT_NO_SSL
#ifndef QT_NO_OPENSSL
#include <QtNetwork/private/qsslsocket_openssl_symbols_p.h>
#endif // NO_OPENSSL
#endif // NO_SSL


#include <cstdlib>

// At the moment our HTTP/2 imlpementation requires ALPN and this means OpenSSL.
#if !defined(QT_NO_OPENSSL) && OPENSSL_VERSION_NUMBER >= 0x10002000L && !defined(OPENSSL_NO_TLSEXT)
#define QT_ALPN
#endif

QT_BEGIN_NAMESPACE

class tst_Http2 : public QObject
{
    Q_OBJECT
public:
    tst_Http2();
    ~tst_Http2();
private slots:
    // Tests:
    void singleRequest();
    void multipleRequests();
    void flowControlClientSide();
    void flowControlServerSide();

protected slots:
    // Slots to listen to our in-process server:
    void serverStarted(quint16 port);
    void clientPrefaceOK();
    void clientPrefaceError();
    void serverSettingsAcked();
    void invalidFrame();
    void invalidRequest(quint32 streamID);
    void decompressionFailed(quint32 streamID);
    void receivedRequest(quint32 streamID);
    void receivedData(quint32 streamID);
    void windowUpdated(quint32 streamID);
    void replyFinished();

private:
    void clearHTTP2State();
    // Run event for 'ms' milliseconds.
    // The default value '5000' is enough for
    // small payload.
    void runEventLoop(int ms = 5000);
    void stopEventLoop();
    // TODO: different parameters like client/server settings ...
    Http2Server *newServer(const Http2Settings &serverSettings);
    // Send a get or post request, depending on a payload (empty or not).
    void sendRequest(int streamNumber,
                     QNetworkRequest::Priority priority = QNetworkRequest::NormalPriority,
                     const QByteArray &payload = QByteArray());

    quint16 serverPort = 0;
    QThread *workerThread = nullptr;
    QNetworkAccessManager manager;

    QEventLoop eventLoop;
    QTimer timer;

    int nRequests = 0;

    int windowUpdates = 0;
    bool prefaceOK = false;
    bool serverGotSettingsACK = false;

    static const Http2Settings defaultServerSettings;
};

const Http2Settings tst_Http2::defaultServerSettings{{Http2::Settings::MAX_CONCURRENT_STREAMS_ID, 100}};

tst_Http2::tst_Http2()
    : workerThread(new QThread)
{
    workerThread->start();

    timer.setInterval(10000);
    timer.setSingleShot(true);

    connect(&timer, SIGNAL(timeout()), &eventLoop, SLOT(quit()));
}

tst_Http2::~tst_Http2()
{
    workerThread->quit();
    workerThread->wait(5000);

    if (workerThread->isFinished()) {
        delete workerThread;
    } else {
        connect(workerThread, &QThread::finished,
                workerThread, &QThread::deleteLater);
    }
}

void tst_Http2::singleRequest()
{
#ifndef QT_ALPN
    QSKIP("This test requires ALPN support");
#endif
    clearHTTP2State();

    serverPort = 0;
    nRequests = 1;

    auto srv = newServer(defaultServerSettings);

    QMetaObject::invokeMethod(srv, "startServer", Qt::QueuedConnection);
    runEventLoop();

    QVERIFY(serverPort != 0);

    const QUrl url(QString("https://127.0.0.1:%1/index.html").arg(serverPort));

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::HTTP2AllowedAttribute, QVariant(true));

    auto reply = manager.get(request);
    connect(reply, &QNetworkReply::finished, this, &tst_Http2::replyFinished);
    // Since we're using self-signed certificates,
    // ignore SSL errors:
    reply->ignoreSslErrors();

    runEventLoop();

    QVERIFY(nRequests == 0);
    QVERIFY(prefaceOK);
    QVERIFY(serverGotSettingsACK);

    QCOMPARE(reply->error(), QNetworkReply::NoError);
    QVERIFY(reply->isFinished());

    QMetaObject::invokeMethod(srv, "deleteLater", Qt::QueuedConnection);
}

void tst_Http2::multipleRequests()
{
#ifndef QT_ALPN
    QSKIP("This test requires ALPN support");
#endif
    clearHTTP2State();

    serverPort = 0;
    nRequests = 10;

    auto srv = newServer(defaultServerSettings);

    QMetaObject::invokeMethod(srv, "startServer", Qt::QueuedConnection);

    runEventLoop();
    QVERIFY(serverPort != 0);

    // Just to make the order a bit more interesting
    // we'll index this randomly:
    QNetworkRequest::Priority priorities[] = {QNetworkRequest::HighPriority,
                                              QNetworkRequest::NormalPriority,
                                              QNetworkRequest::LowPriority};



    for (int i = 0; i < nRequests; ++i)
        sendRequest(i, priorities[std::rand() % 3]);

    runEventLoop();

    QVERIFY(nRequests == 0);
    QVERIFY(prefaceOK);
    QVERIFY(serverGotSettingsACK);

    QMetaObject::invokeMethod(srv, "deleteLater", Qt::QueuedConnection);
}

void tst_Http2::flowControlClientSide()
{
#ifndef QT_ALPN
    QSKIP("This test requires ALPN support");
#endif
    // Create a server but impose limits:
    // 1. Small MAX frame size, so we test CONTINUATION frames.
    // 2. Small client windows so server responses cause client streams
    //    to suspend and server sends WINDOW_UPDATE frames.
    // 3. Few concurrent streams, to test protocol handler can resume
    //    suspended requests.

    using namespace Http2;

    clearHTTP2State();

    serverPort = 0;
    nRequests = 10;
    windowUpdates = 0;

    const Http2Settings serverSettings = {{Settings::MAX_CONCURRENT_STREAMS_ID, 3}};

    auto srv = newServer(serverSettings);

    const QByteArray respond(int(Http2::defaultSessionWindowSize * 100), 'x');
    srv->setResponseBody(respond);

    QMetaObject::invokeMethod(srv, "startServer", Qt::QueuedConnection);

    runEventLoop();
    QVERIFY(serverPort != 0);

    for (int i = 0; i < nRequests; ++i)
        sendRequest(i);

    runEventLoop(10000);

    QVERIFY(nRequests == 0);
    QVERIFY(prefaceOK);
    QVERIFY(serverGotSettingsACK);
    QVERIFY(windowUpdates > 0);

    QMetaObject::invokeMethod(srv, "deleteLater", Qt::QueuedConnection);
}

void tst_Http2::flowControlServerSide()
{
#ifndef QT_ALPN
    QSKIP("This test requires ALPN support");
#endif
    // Quite aggressive test:
    // low MAX_FRAME_SIZE forces a lot of small DATA frames,
    // payload size exceedes stream/session RECV window sizes
    // so that our implementation should deal with WINDOW_UPDATE
    // on a session/stream level correctly + resume/suspend streams
    // to let all replies finish without any error.
    using namespace Http2;

    clearHTTP2State();

    serverPort = 0;
    nRequests = 30;

    const Http2Settings serverSettings = {{Settings::MAX_CONCURRENT_STREAMS_ID, 7}};

    auto srv = newServer(serverSettings);

    const QByteArray payload(int(Http2::defaultSessionWindowSize * 1000), 'x');

    QMetaObject::invokeMethod(srv, "startServer", Qt::QueuedConnection);

    runEventLoop();
    QVERIFY(serverPort != 0);

    for (int i = 0; i < nRequests; ++i)
        sendRequest(i, QNetworkRequest::NormalPriority, payload);

    runEventLoop(120000);

    QVERIFY(nRequests == 0);
    QVERIFY(prefaceOK);
    QVERIFY(serverGotSettingsACK);

    QMetaObject::invokeMethod(srv, "deleteLater", Qt::QueuedConnection);
    srv = nullptr;
}

void tst_Http2::serverStarted(quint16 port)
{
    serverPort = port;
    stopEventLoop();
}

void tst_Http2::clearHTTP2State()
{
    windowUpdates = 0;
    prefaceOK = false;
    serverGotSettingsACK = false;
}

void tst_Http2::runEventLoop(int ms)
{
    timer.setInterval(ms);
    timer.start();
    eventLoop.exec();
}

void tst_Http2::stopEventLoop()
{
    timer.stop();
    eventLoop.quit();
}

Http2Server *tst_Http2::newServer(const Http2Settings &serverSettings)
{
    using namespace Http2;
    // Client's settings are fixed by qhttp2protocolhandler.
    const Http2Settings clientSettings = {{Settings::MAX_FRAME_SIZE_ID, quint32(Http2::maxFrameSize)},
                                          {Settings::ENABLE_PUSH_ID, quint32(0)}};
    auto srv = new Http2Server(serverSettings, clientSettings);

    using Srv = Http2Server;
    using Cl = tst_Http2;

    connect(srv, &Srv::serverStarted, this, &Cl::serverStarted);
    connect(srv, &Srv::clientPrefaceOK, this, &Cl::clientPrefaceOK);
    connect(srv, &Srv::clientPrefaceError, this, &Cl::clientPrefaceError);
    connect(srv, &Srv::serverSettingsAcked, this, &Cl::serverSettingsAcked);
    connect(srv, &Srv::invalidFrame, this, &Cl::invalidFrame);
    connect(srv, &Srv::invalidRequest, this, &Cl::invalidRequest);
    connect(srv, &Srv::receivedRequest, this, &Cl::receivedRequest);
    connect(srv, &Srv::receivedData, this, &Cl::receivedData);
    connect(srv, &Srv::windowUpdate, this, &Cl::windowUpdated);

    srv->moveToThread(workerThread);

    return srv;
}

void tst_Http2::sendRequest(int streamNumber,
                            QNetworkRequest::Priority priority,
                            const QByteArray &payload)
{
    static const QString urlAsString("https://127.0.0.1:%1/stream%2.html");

    const QUrl url(urlAsString.arg(serverPort).arg(streamNumber));
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::HTTP2AllowedAttribute, QVariant(true));
    request.setPriority(priority);

    QNetworkReply *reply = nullptr;
    if (payload.size())
        reply = manager.post(request, payload);
    else
        reply = manager.get(request);

    reply->ignoreSslErrors();
    connect(reply, &QNetworkReply::finished, this, &tst_Http2::replyFinished);
}

void tst_Http2::clientPrefaceOK()
{
    prefaceOK = true;
}

void tst_Http2::clientPrefaceError()
{
    prefaceOK = false;
}

void tst_Http2::serverSettingsAcked()
{
    serverGotSettingsACK = true;
}

void tst_Http2::invalidFrame()
{
}

void tst_Http2::invalidRequest(quint32 streamID)
{
    Q_UNUSED(streamID)
}

void tst_Http2::decompressionFailed(quint32 streamID)
{
    Q_UNUSED(streamID)
}

void tst_Http2::receivedRequest(quint32 streamID)
{
    qDebug() << "   server got a request on stream" << streamID;
    Http2Server *srv = qobject_cast<Http2Server *>(sender());
    Q_ASSERT(srv);
    QMetaObject::invokeMethod(srv, "sendResponse", Qt::QueuedConnection,
                              Q_ARG(quint32, streamID),
                              Q_ARG(bool, false /*non-empty body*/));
}

void tst_Http2::receivedData(quint32 streamID)
{
    qDebug() << "   server got a 'POST' request on stream" << streamID;
    Http2Server *srv = qobject_cast<Http2Server *>(sender());
    Q_ASSERT(srv);
    QMetaObject::invokeMethod(srv, "sendResponse", Qt::QueuedConnection,
                              Q_ARG(quint32, streamID),
                              Q_ARG(bool, true /*HEADERS only*/));
}

void tst_Http2::windowUpdated(quint32 streamID)
{
    Q_UNUSED(streamID)

    ++windowUpdates;
}

void tst_Http2::replyFinished()
{
    QVERIFY(nRequests);

    if (const auto reply = qobject_cast<QNetworkReply *>(sender()))
        QCOMPARE(reply->error(), QNetworkReply::NoError);

    --nRequests;
    if (!nRequests)
        stopEventLoop();
}

QT_END_NAMESPACE

QTEST_MAIN(tst_Http2)

#include "tst_http2.moc"
