#include <iostream>
#include <mutex>
#include <set>
#include <string_view>
#include <thread>

#include <ctime>

#include <QCoreApplication>

#include <fuse.h>
#include <unistd.h>

#include <fmt/core.h>

#include "irc.hpp"

namespace kq {

static irc::client* irc_client;
void client_main(std::string_view nick,
    std::string_view server, int port = 6667);

using namespace std::literals::string_view_literals;

int getattr(char const* path, struct stat* st)
{
    DEBUG("{}", path);
    st->st_uid = getuid();
    st->st_gid = getgid();
    st->st_mtime = 0;
    st->st_atime = 0;
    st->st_ctime = 0;

    if (path == "/"sv) {
        st->st_mode = S_IFDIR | 0400;
        st->st_nlink = 2;
    } else if (path[1] == '#') {
        auto p = QByteArray(path + 1).split('/');
        std::scoped_lock lock{irc_client->get_sync()};


        auto it = irc_client->get_channels().find(p.front());
        if (it == irc_client->get_channels().cend())
            return -ENOENT;

        if (p.size() == 2) {
            st->st_mode = S_IFREG | 0400;
            st->st_nlink = 1;
            if (p.last() == "messages") {
                st->st_size = it->second.raw_messages.size();
            } else if (p.last() == "topic") {
                st->st_size = it->second.topic.size();
            } else if (p.last() == "users") {
                st->st_size = 0;
                for(auto const& u : it->second.users)
                    st->st_size += u.size();
                st->st_size += it->second.users.size();
            } else {
                return -ENOENT;
            }
        } else {
            st->st_mode = S_IFDIR | 0400;
            st->st_nlink = 2;
            if (it->second.messages.size()) {
                auto time = it->second.messages.back().when
                    .toMSecsSinceEpoch() / 1000;
                st->st_mtime = time;
                st->st_atime = time;
                st->st_ctime = time;
            }
        }
    } else {
        std::scoped_lock lock{irc_client->get_sync()};
        auto it = irc_client->get_queries().find(path+1);
        if (it == irc_client->get_queries().cend())
            return -ENOENT;
        auto time = it->second.messages.back().when
            .toMSecsSinceEpoch() / 1000;
        st->st_mtime = time;
        st->st_atime = time;
        st->st_ctime = time;
        
        st->st_mode = S_IFREG | 0400;
        st->st_nlink = 1;
        st->st_size = it->second.raw_messages.size();
    }

    return 0;
}

int readdir(char const* path, void* buffer,
    fuse_fill_dir_t filler, off_t offset, fuse_file_info* fi)
{
    DEBUG("{}", path);
    filler(buffer, ".", nullptr, 0);
    filler(buffer, "..", nullptr, 0);

    if (path == "/"sv) {
        std::set<std::string> channels;
        std::set<std::string> queries;
        {
            std::scoped_lock lock{irc_client->get_sync()};
            for (auto const& [channel, messages] :
                irc_client->get_channels()) {
                
                queries.insert(channel.toStdString());
            }
            for (auto const& [who, messages] :
                irc_client->get_queries()) {
                
                queries.insert(who.toStdString());
            }
        }
        for (auto const& q : queries) {
            filler(buffer, q.c_str(), nullptr, 0);
        }
    } else if (path[1] == '#') {
        std::scoped_lock lock{irc_client->get_sync()};
        auto it = irc_client->get_channels().find(path+1);
        if (it == irc_client->get_channels().cend())
            return -ENOENT;
        
        filler(buffer, "users", nullptr, 0);
        filler(buffer, "topic", nullptr, 0);
        filler(buffer, "messages", nullptr, 0);
    } else {
        return -EINVAL;
    }
    return 0;
}

int read(char const* path, char* buffer, size_t size,
    off_t offset, fuse_file_info *fi)
{
    std::string msg;
    DEBUG("path: {}, offset: {}", path, offset);

    if (path[1] == '#') {
        auto split = QByteArray(path+1).split('/');
        
        std::scoped_lock lock{irc_client->get_sync()};
        auto it = irc_client->get_channels().find(split[0]);
        if (it == irc_client->get_channels().cend())
            return -ENOENT;
        if (split[1] == "messages") {
            msg = it->second.raw_messages.toStdString();
        } else if (split[1] == "topic") {
            msg = it->second.topic.toStdString();
            msg += '\n';
        } else if (split[1] == "users") {
            msg.reserve(it->second.users.size() * 8);
            for (auto const& u : it->second.users) {
                msg += u.toStdString();
                msg += '\n';
            }
        } else {
            return -ENOENT;
        }
    } else {
        std::scoped_lock lock{irc_client->get_sync()};
        auto const& queries = irc_client->get_queries();
        auto it = queries.find(path + 1);

        if (it == queries.cend())
            return -ENOENT;

        msg = it->second.raw_messages.toStdString();
    }

    if (offset >= msg.size())
        return 0;

    auto bytes_to_copy = std::min(size,
        msg.size() - offset);
    auto begin = msg.cbegin() + offset;
    std::copy(begin, begin + bytes_to_copy, buffer);
    return bytes_to_copy;
}

int write(char const* path, char const* buffer, size_t size,
    off_t offset, fuse_file_info *fi)
{
    DEBUG("path: {}, offset: {}", path, offset);

    if (path[1] == '#') {
        auto p = QByteArray(path + 1).split('/');
        if (p.size() != 2)
            return -EINVAL;

        std::scoped_lock lock{irc_client->get_sync()};
        auto const channels = irc_client->get_channels();
        auto it = channels.find(p.front());

        if (it == channels.cend())
            return -ENOENT;
        if (p.back() != "messages")
            return -EINVAL;

        auto data = QByteArray(buffer, size);
        irc_client->say(p.front(), data);
    } else {
        std::scoped_lock lock{irc_client->get_sync()};
        auto const& queries = irc_client->get_queries();
        auto it = queries.find(path + 1);
        if (it == queries.cend())
        return -ENOENT;
        auto data = QByteArray(buffer, size);
        DEBUG("data({}): {}", data.size(),
            data.toStdString());
        irc_client->say(path + 1, data);
    }

    return size;
}

int mkdir(char const* path, mode_t)
{
    DEBUG("path: {}", path);

    if (path[1] != '#')
        return -EINVAL;
    
    QByteArray p(path+1);
    if (p.count('/'))
        return -EINVAL;

    std::scoped_lock lock{irc_client->get_sync()};
    irc_client->join_channel(p);

    return 0;
}

int rmdir(char const* path)
{
    DEBUG("path: {}", path);

    if (path[1] != '#')
        return -EINVAL;

    QByteArray p(path+1);
    if (p.count('/'))
        return -EINVAL;

    std::scoped_lock lock{irc_client->get_sync()};
    irc_client->part_channel(p);

    return 0;
}

} // kq

int main(int argc, char** argv)
{
    fuse_operations ops = {
        .getattr = &kq::getattr,
        .mkdir   = &kq::mkdir,
        .rmdir   = &kq::rmdir,
        .read    = &kq::read,
        .write   = &kq::write,
        .readdir = &kq::readdir,
    };

    if (argc < 4)
        return EXIT_FAILURE;

    std::thread client{ [nick = argv[1], host = argv[2]]{
        kq::client_main(nick, host);
    } };

    argv[2] = argv[0];
    return fuse_main(argc - 2, argv + 2, &ops, nullptr);
    QCoreApplication::quit();
    client.join();
}

void kq::client_main(std::string_view nick,
    std::string_view server, int port)
{
    fmt::print("{} @ {}\n", nick, server);
    int fake_argc = 1;
    char fake_argv_val[2] = { "." };
    char* fake_argv[2] = { fake_argv_val, nullptr };
    QCoreApplication app(fake_argc, fake_argv);

    qRegisterMetaType<kq::irc::message_data>("message_data");

    kq::irc::settings settings = {
        QString::fromStdString(std::string{server}),
        static_cast<quint16>(port),
        QString::fromStdString(std::string{nick})
    };
    irc_client = new kq::irc::client(settings, &app);

    app.exec();
}

