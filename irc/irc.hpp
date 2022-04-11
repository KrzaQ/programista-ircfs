#ifndef IRC_HPP
#define IRC_HPP

#include <map>
#include <mutex>
#include <set>
#include <vector>

#include <QByteArray>
#include <QObject>

#include "common.hpp"

class QTcpSocket;

namespace kq::irc
{

class base_client : public QObject
{
    Q_OBJECT
public:
    explicit base_client(
        settings const&,
        QObject *parent = nullptr
    );

    settings const& config() const { return cfg; }

public slots:
    void say(QByteArray const&, QByteArray const&);
    void join(QByteArray const&);
    void part(QByteArray const&);

    void write_line(QByteArray const&);

signals:
    void message(message_data const&);

private:
    void connect_to_irc();
    void on_read(QByteArray const&);
    void parse_lines();

    void identify();
    void handle_ping(message_data const&);

    settings cfg;
    QTcpSocket* connection;
    QByteArray input_buffer;
};

struct channel
{
    std::set<QByteArray> users;
    QByteArray topic;
    std::vector<message_data> messages;
    QByteArray raw_messages;
};

struct query
{
    std::vector<message_data> messages;
    QByteArray raw_messages;
};

class client : public QObject
{
    Q_OBJECT
public:
    explicit client(
        settings const&,
        QObject *parent = nullptr
    );

    void join_channel(QByteArray const&);
    void part_channel(QByteArray const&);

    void say(QByteArray const&, QByteArray const&);

    auto const& get_channels() const {
        return channels;
    }

    auto const& get_queries() const {
        return queries;
    }

    std::mutex& get_sync() const { return sync; }

public slots:
    void on_message(message_data const&);

private:
    static QByteArray format_message(message_data const&);

    std::map<QByteArray, channel> channels;
    std::map<QByteArray, query> queries;
    base_client* conn;
    std::mutex mutable sync;
};

}

#endif // IRC_HPP
