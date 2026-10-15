#ifdef QT_QML_DEBUG
#include <QtQuick>
#endif

#include <sailfishapp.h>
#include <QGuiApplication>
#include <QLocale>
#include <QQmlContext>
#include <QQuickView>
#include <QScopedPointer>
#include <QTranslator>

#include "controller/appcontroller.h"
#include "map/mapcanvas.h"

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));

    QTranslator translator;
    const QString translationsDir = SailfishApp::pathTo(QStringLiteral("translations")).toLocalFile();
    if (translator.load(QLocale(), QStringLiteral("harbour-edgesync"), QStringLiteral("-"), translationsDir))
        app->installTranslator(&translator);

    // Sailjail whitelists (and persists) ~/.local/share/<OrganizationName>/
    // <ApplicationName> from the .desktop file's [X-Sailjail] values.
    // QStandardPaths::AppDataLocation, where the activities and the database
    // live, is built from these two, so they must match harbour-edgesync.desktop
    // exactly or the app writes somewhere Sailjail never keeps.
    app->setOrganizationName(QStringLiteral("io.github.jarnose"));
    app->setApplicationName(QStringLiteral("edgesync"));

    // The base map under a ride's route: a C++ item, because the vector tiles
    // come to tens of thousands of points. See src/map/mapcanvas.h and
    // docs/base-map.md.
    qmlRegisterType<MapCanvas>("io.github.jarnose.edgesync", 1, 0, "MapCanvas");

    QScopedPointer<QQuickView> view(SailfishApp::createView());

    AppController controller;
    view->rootContext()->setContextProperty(QStringLiteral("AppController"), &controller);

    view->setSource(SailfishApp::pathToMainQml());
    view->show();

    return app->exec();
}
