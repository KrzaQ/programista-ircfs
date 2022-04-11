#include <QRegularExpression>
#include <QTcpSocket>
#include <QTextStream>

#include "irc.hpp"

namespace kq::irc
{

base_client::base_client(
    settings const& config, QObject *parent
):
    QObject(parent),
    cfg{config},
    connection{}
{
    connect_to_irc();

    connect(this, &base_client::message,
            this, &base_client::handle_ping,
            Qt::QueuedConnection);
}

void base_client::connect_to_irc()
{
    if (connection) {
        connection->disconnect();
        connection->disconnectFromHost();
        delete connection;
    }

    input_buffer.clear();

    connection = new QTcpSocket(this);

    connect(connection, &QTcpSocket::connected, [this]{
        DEBUG("{}", "connected");
        identify();
    });

    connect(connection, &QTcpSocket::disconnected, [this]{
        DEBUG("{}", "disconnected");
        connect_to_irc();
    });

    connect(connection, &QTcpSocket::readyRead, [this]{
        on_read(connection->readAll());
    });

    connection->connectToHost(cfg.host, cfg.port);
}

void base_client::on_read(QByteArray const& buf)
{
    input_buffer += buf;
    parse_lines();
}

void base_client::parse_lines()
{
    static auto constexpr server_message =
        R"((?::([^@!\ ]*(?:(?:![^@]*)?@[^\ ]*)?)\ ))"
        R"(?([^\ ]+)((?:[^:\ ][^\ ]*)?(?:\ [^:\ ][^\ ]*))"
        R"({0,14})(?:\ :?(.*))?)";
    QRegularExpression re{server_message};

    while (true) {
        auto new_line_index = input_buffer.indexOf("\r\n");
        if (new_line_index == -1)
            break;

        QString line = input_buffer.left(new_line_index);
        input_buffer.remove(0, new_line_index + 2);

        auto m = re.match(line);
        if (m.hasMatch()) {
            message_data msg = {
                m.captured(1),
                m.captured(2),
                m.captured(3).trimmed(),
                m.captured(4),
                QDateTime::currentDateTime()
            };

            DEBUG("message {}. who: {}, where: {}, message:"
                " {}", msg.type.toStdString(),
                msg.who.toStdString(),
                msg.where.toStdString(),
                msg.message.toStdString());
            emit message(msg);

            if (msg.type == "001")
                DEBUG("{}", "Received message 001");
        }
    }
}

void base_client::identify()
{
    QByteArray msg;
    QTextStream(&msg) << "USER " << cfg.nick << " foo bar :"
        << cfg.nick;
    write_line(msg);
    msg.clear();
    QTextStream(&msg) << "NICK :" + cfg.nick;
    write_line(msg);
}

void base_client::handle_ping(message_data const& msg)
{
    if (msg.type != "PING")
        return;

    write_line("PONG: " + msg.message.toLatin1());
}

void base_client::write_line(QByteArray const& line)
{
    connection->write(line);
    connection->write("\r\n");
}

void base_client::say(
    QByteArray const& recipient,
    QByteArray const& message
) {
    DEBUG("saying '{}' to {}", message.toStdString(),
        recipient.toStdString());
    QByteArray msg;
    QTextStream(&msg) << "PRIVMSG " << recipient << " :"
        << message;
    write_line(msg);
}

void base_client::join(QByteArray const& channel)
{
    QByteArray msg;
    QTextStream(&msg) << "JOIN " << channel;
    write_line(msg);
}

void base_client::part(QByteArray const& channel)
{
    QByteArray msg;
    QTextStream(&msg) << "PART " << channel;
    write_line(msg);
}

client::client(settings const& settings, QObject* parent):
    QObject(parent),
    conn{ new base_client(settings, this) }
{
    QObject::connect(conn, &base_client::message, 
        this, &client::on_message,
        Qt::QueuedConnection);
}

void client::on_message(message_data const& msg)
{
    std::scoped_lock lock{sync};

    if (msg.type == "PRIVMSG") {
        if (msg.where.size() && msg.where[0] == '#') {
            DEBUG("append: {}", format_message(msg).toStdString());
            channel& ch = channels[msg.where.toUtf8()];
            ch.messages.push_back(msg);
            ch.raw_messages.append(format_message(msg));
        } else {
            auto nickname = msg.who.toUtf8().split('!')
                .front();
            query& q = queries[nickname];
            q.messages.push_back(msg);
            q.raw_messages.append(format_message(msg));
        }
    } else if (msg.type == "332") {
        // channel topic
        DEBUG("topic: {}", msg.message.toStdString());
        auto channel = '#' + msg.where.split('#').back()
            .toUtf8();
        channels[channel].topic = msg.message.toUtf8();
    } else if (msg.type == "353") {
        // channel users
        auto channel = '#' + msg.where.split('#').back()
            .toUtf8();
        for (auto const& u : msg.message.split(' ')) {
            if (!u.size())
                continue;
            DEBUG("Adding user: {}, channel: {}",
                u.toStdString(), channel.toStdString());
            channels[channel].users.insert(u.toUtf8());
        }
    } else if (msg.type == "JOIN") {
        auto nick = msg.who.split('!').front().toUtf8();
        channels[msg.message.toUtf8()].users.insert(nick);
        DEBUG("Adding user: {}, channel: {}",
            nick.toStdString(), msg.message.toStdString());
    } else if (msg.type == "PART") {
        auto nick = msg.who.split('!').front().toUtf8();
        channels[msg.where.toUtf8()].users.erase(nick);
        DEBUG("Removing user: {} channel: {}",
            nick.toStdString(), msg.where.toStdString());
    } else if (msg.type == "QUIT") {
        auto nick = msg.who.split('!').front().toUtf8();
        DEBUG("Removing user from all channels: {}",
            nick.toStdString());
        for (auto& [channel, data] : channels) {
            data.users.erase(nick);
        }
    }
}

void client::join_channel(QByteArray const& channel)
{
    QMetaObject::invokeMethod(conn, [=]{
        conn->join(channel);
    }, Qt::QueuedConnection);
    
    channels[channel]; 
}

void client::part_channel(QByteArray const& channel)
{
    QMetaObject::invokeMethod(conn, [=]{
        conn->part(channel);
    }, Qt::QueuedConnection);

    channels.erase(channel);
}

void client::say(QByteArray const& recipient,
    QByteArray const& message)
{
    for(auto const& line : message.split('\n')) {
        if (!line.size())
            continue;
        QMetaObject::invokeMethod(conn, [=]{
            conn->say(recipient, line);
        }, Qt::QueuedConnection);
        message_data msg;
        msg.message = line;
        msg.type = "PRIVMSG";
        msg.when = QDateTime::currentDateTime();
        msg.who = conn->config().nick;
        if (recipient[0] == '#') {
            channel& ch = channels[recipient];
            ch.messages.push_back(msg);
            ch.raw_messages.append(format_message(msg));
        } else {
            query& q = queries[recipient];
            q.messages.push_back(msg);
            q.raw_messages.append(format_message(msg));
        }
    }
}

QByteArray client::format_message(message_data const& msg)
{
    return fmtQByteArray("[{}] <{}>: {}\n",
        msg.when.toString("hh:mm:ss").toStdString(),
        msg.who.split('!').front().toStdString(),
        msg.message.toStdString()
    );
}

}
