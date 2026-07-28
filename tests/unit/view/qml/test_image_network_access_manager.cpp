#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "view/qml/image_network_access_manager.hpp"

using namespace anime_land::qml;

TEST(ImageNetworkAccessManager, DisablesHttp2BeforeDispatch) {
    ImageNetworkAccessManager network;
    QNetworkRequest request(
        QUrl(QStringLiteral("data:text/plain,cover-fixture")));
    request.setAttribute(QNetworkRequest::Http2DirectAttribute, true);

    QNetworkReply *reply = network.get(request);

    ASSERT_NE(reply, nullptr);
    EXPECT_FALSE(reply->request()
                     .attribute(QNetworkRequest::Http2AllowedAttribute, true)
                     .toBool());
    EXPECT_FALSE(reply->request()
                     .attribute(QNetworkRequest::Http2DirectAttribute, true)
                     .toBool());
    delete reply;
}

#define EXPAND_IN_MAIN_WITH_ARGS(argc, argv) \
    QCoreApplication qtApplication(argc, argv)
#include "common/common_main.hpp.in"
