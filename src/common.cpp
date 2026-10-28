/*
 * QtPurple - a Qt front end for libpurple
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "common.h"

#include <QColor>
#include <QDesktopServices>
#include <QFileInfo>
#include <QStringList>
#include <QFile>
#include <QHash>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QPixmap>
#include <QStandardPaths>

namespace QtPurple {

QColor statusColor(PurpleStatusPrimitive prim)
{
    switch (prim) {
    case PURPLE_STATUS_AVAILABLE:
    case PURPLE_STATUS_MOBILE:
    case PURPLE_STATUS_TUNE:
    case PURPLE_STATUS_MOOD:
        return QColor(0x4e, 0x9a, 0x06);    /* green  */
    case PURPLE_STATUS_AWAY:
    case PURPLE_STATUS_EXTENDED_AWAY:
        return QColor(0xf5, 0x79, 0x00);    /* orange */
    case PURPLE_STATUS_UNAVAILABLE:
        return QColor(0xcc, 0x00, 0x00);    /* red    */
    case PURPLE_STATUS_INVISIBLE:
        return QColor(0x72, 0x9f, 0xcf);    /* blue   */
    case PURPLE_STATUS_OFFLINE:
    case PURPLE_STATUS_UNSET:
    default:
        return QColor(0x88, 0x8a, 0x85);    /* gray   */
    }
}

QIcon statusIcon(PurpleStatusPrimitive prim, bool idle)
{
    static QHash<int, QIcon> cache;
    const int key = int(prim) * 2 + (idle ? 1 : 0);
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();

    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        QColor c = statusColor(prim);
        if (idle)
            c.setAlpha(110);
        if (prim == PURPLE_STATUS_INVISIBLE) {
            p.setPen(QPen(c, 2.0));
            p.setBrush(Qt::NoBrush);
        } else {
            p.setPen(QPen(c.darker(130), 1.0));
            p.setBrush(c);
        }
        p.drawEllipse(QRectF(3.0, 3.0, 10.0, 10.0));
    }
    QIcon icon(pm);
    cache.insert(key, icon);
    return icon;
}

QIcon chatIcon()
{
    static QIcon icon;
    if (!icon.isNull())
        return icon;

    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        QColor c(0x34, 0x65, 0xa4);
        p.setPen(QPen(c.darker(130), 1.0));
        p.setBrush(c);
        p.drawRoundedRect(QRectF(1.5, 2.5, 13.0, 9.0), 2.5, 2.5);
        QPolygonF tail;
        tail << QPointF(4.0, 11.0) << QPointF(4.0, 14.5) << QPointF(8.0, 11.0);
        p.drawPolygon(tail);
    }
    icon = QIcon(pm);
    return icon;
}

QIcon protocolIcon(PurpleAccount *account)
{
    static QHash<QString, QIcon> cache;
    if (!account)
        return QIcon();

    PurplePlugin *prpl = purple_find_prpl(purple_account_get_protocol_id(account));
    if (!prpl)
        return QIcon();
    PurplePluginProtocolInfo *info = PURPLE_PLUGIN_PROTOCOL_INFO(prpl);
    if (!info || !info->list_icon)
        return QIcon();

    const QString name = qs(info->list_icon(account, nullptr));
    if (name.isEmpty())
        return QIcon();

    auto it = cache.constFind(name);
    if (it != cache.constEnd())
        return it.value();

    /* Re-use Pidgin's protocol icons when they are installed. */
    QIcon icon;
    const QString rel = QStringLiteral("pixmaps/pidgin/protocols/16/%1.png").arg(name);
    const QString path = QStandardPaths::locate(QStandardPaths::GenericDataLocation, rel);
    if (!path.isEmpty() && QFile::exists(path))
        icon = QIcon(path);
    cache.insert(name, icon);
    return icon;
}

QString primitiveName(PurpleStatusPrimitive prim)
{
    return qs(purple_primitive_get_name_from_type(prim));
}

bool accountExists(PurpleAccount *account)
{
    return account && g_list_find(purple_accounts_get_all(), account) != nullptr;
}

bool connectionExists(PurpleConnection *gc)
{
    return gc && g_list_find(purple_connections_get_all(), gc) != nullptr;
}

QString accountDisplayName(PurpleAccount *account)
{
    if (!account)
        return QString();
    return QStringLiteral("%1 (%2)")
        .arg(qs(purple_account_get_username(account)),
             qs(purple_account_get_protocol_name(account)));
}

QString mnemonicToQt(const char *text)
{
    const QString in = qs(text);
    QString out;
    out.reserve(in.size() + 2);
    for (int i = 0; i < in.size(); ++i) {
        const QChar c = in.at(i);
        if (c == QLatin1Char('&')) {
            out += QLatin1String("&&");
        } else if (c == QLatin1Char('_')) {
            if (i + 1 < in.size() && in.at(i + 1) == QLatin1Char('_')) {
                out += QLatin1Char('_');
                ++i;
            } else {
                out += QLatin1Char('&');
            }
        } else {
            out += c;
        }
    }
    return out;
}

QString stripMnemonic(const char *text)
{
    const QString in = qs(text);
    QString out;
    out.reserve(in.size());
    for (int i = 0; i < in.size(); ++i) {
        const QChar c = in.at(i);
        if (c == QLatin1Char('_')) {
            if (i + 1 < in.size() && in.at(i + 1) == QLatin1Char('_')) {
                out += QLatin1Char('_');
                ++i;
            }
            continue;
        }
        out += c;
    }
    return out;
}

bool openLink(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    static const QStringList allowed = {
        QStringLiteral("http"), QStringLiteral("https"), QStringLiteral("ftp"),
        QStringLiteral("mailto"), QStringLiteral("xmpp"), QStringLiteral("irc"),
        QStringLiteral("ircs")
    };
    if (allowed.contains(scheme))
        return QDesktopServices::openUrl(url);
    if (scheme == QLatin1String("file")) {
        /* Never launch a local file from a link; just show where it is. */
        const QFileInfo fi(url.toLocalFile());
        return QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
    }
    return false;
}

PurpleStatusPrimitive accountPrimitive(PurpleAccount *account)
{
    if (!account || !purple_account_is_connected(account))
        return PURPLE_STATUS_OFFLINE;
    PurpleStatus *status = purple_account_get_active_status(account);
    if (!status)
        return PURPLE_STATUS_OFFLINE;
    return purple_status_type_get_primitive(purple_status_get_type(status));
}

} // namespace QtPurple
