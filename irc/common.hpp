#ifndef COMMON_HPP
#define COMMON_HPP

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <fmt/core.h>

namespace kq::irc
{

struct settings
{
    QString host;
    quint16 port;
    QString nick;
};

struct message_data
{
    QString who;
    QString type;
    QString where;
    QString message;
    QDateTime when;
};

template<typename... Ts>
QByteArray fmtQByteArray(Ts&&... ts) {
    return QByteArray::fromStdString(fmt::format(std::forward<Ts>(ts)...));
}

#define DEBUG(FMT, ...) fmt::print("{}:{}:{}: " FMT "\n", \
    __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define BARK flog("{}()", __FUNCTION__)

}


#endif // COMMON_HPP
