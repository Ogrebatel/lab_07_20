// Copyright 2018 Your Name <your_email>

#ifndef INCLUDE_HEADER_HPP_
#define INCLUDE_HEADER_HPP_

#include <iostream>
#include <boost/asio.hpp>
#include <boost/thread/pthread/recursive_mutex.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/core/null_deleter.hpp>
#include <mutex>
#include <thread>
#include <ctime>
#include <vector>
#include <string>

using std::thread;
using std::exception;
using sock = boost::asio::ip::tcp::socket;
using acceptor = boost::asio::ip::tcp::acceptor;
using endpoint = boost::asio::ip::tcp::endpoint;
namespace logging = boost::log;
#define TRUE 1
#define TIMEOUT 5
#define SIZE_FILE 10*1024*1024
const std::string type_exeption("read_until: Resource temporarily unavailable");


struct member{
    explicit member(boost::asio::io_service* service):
    my_socket(*service) {}
    sock my_socket;
    std::string name;
    bool clients_changed;
    time_t _lastTime;
};

class server{
public:
    server(){
        service = new boost::asio::io_service;
    }

    void start(){
        thread linker(&server::accept_connection, this);
        std::thread main(&server::main_loop, this);
        main.join();
        linker.join();
    }

//---------------------------- ENGINE --------------------------------

    void main_loop(){
        boost::asio::streambuf buffer{};
        log_init();
        while (TRUE) {
            my_lock.lock();
            reload_vector();
            if (client_list_changed)
                change_for_all();
            my_lock.unlock();

            for (auto it = clients.begin(); it != clients.end();) {
               // sleep(5);
                try {
                    if (!(*it)->my_socket.is_open()) throw 1;
                    (*it)->my_socket.non_blocking(true);
                    boost::asio::read_until((*it)->my_socket, buffer, '\n');
                }

                catch (exception &e) {
                    if (e.what() != type_exeption){
                        BOOST_LOG_TRIVIAL(info) << "client " << (*it)->name
                                                << " " << "disconnected: "
                                                << e.what();
                        (*it)->my_socket.close();
                        clients.erase(it);
                        continue;
                    }
                }

                std::string output(std::istreambuf_iterator<char>{&buffer},
                                   std::istreambuf_iterator<char>{});
                std::string request =
                output.substr(0, output.find_first_of('\n'));

                if (request.find("login") == 0) {
                    bool check = login_client(*it, request);
                    send_to_logged(*it, check);
                    (*it)->_lastTime = clock();
                } else if (request == "ping") {
                    bool check = ping_from_client(*it);
                    answer_to_ping(*it, check);
                    (*it)->_lastTime = clock();
                } else if (request == "clients") {
                    send_clients_list(*it);
                    (*it)->_lastTime = clock();
                } else {
                    time_t end = (clock() - (*it)->_lastTime)/CLOCKS_PER_SEC;
                    if (end > TIMEOUT)
                    {
                        BOOST_LOG_TRIVIAL(info) << "client " << (*it)->name
                        << " " << "disconnected: timeout";
                        (*it)->my_socket.close();
                        clients.erase(it);
                        continue;
                    }
                }
                it = (std::next(it));
            }
        }
    }

//--------------------------------------------------------------------

//---------------------------- LOGIN ---------------------------------

    bool login_client(std::shared_ptr<member> client,
                      const std::string& request){
        if (!client->name.empty())
            return false;
        client->name = request.substr(6, request.length());
        for (auto & _client : clients) _client->clients_changed = true;
        client->clients_changed = false;
        return true;
    }

    void send_to_logged(std::shared_ptr<member> client, bool check) {
        boost::asio::streambuf buffer{};
        std::ostream out(&buffer);
        if (check){
            out << "login ok\n";
            boost::asio::write(client->my_socket, buffer);
        } else{
            out << "you're already logged\n";
            boost::asio::write(client->my_socket, buffer);
        }
    }

//--------------------------------------------------------------------

//---------------------------- PING ----------------------------------

    bool ping_from_client(std::shared_ptr<member> client){
        return client->clients_changed;
    }

    void answer_to_ping(std::shared_ptr<member> client, bool check) {
        boost::asio::streambuf buffer{};
        std::ostream out(&buffer);
        if (check){
            out << "clients changed\n";
            boost::asio::write(client->my_socket, buffer);
            client->clients_changed = false;
        } else{
            out << "ping ok\n";
            boost::asio::write(client->my_socket, buffer);
        }
    }

//--------------------------------------------------------------------

//----------------------- SHOW CLIENTS -------------------------------
    void send_clients_list(std::shared_ptr<member> client){
        boost::asio::streambuf buffer{};
        std::ostream out(&buffer);
        std::string clients_list;
        for (auto & _client : clients)
            clients_list = clients_list + _client->name + " ";
        clients_list += '\n';
        out << clients_list;
        boost::asio::write(client->my_socket, buffer);
    }

//--------------------------------------------------------------------

    void bad_request(std::shared_ptr<member> client){
        boost::asio::streambuf buffer{};
        BOOST_LOG_TRIVIAL(info) << "client " << client->name
                                << ":" << " bad request";
    }

//---------------------------- ACCEPT ---------------------------------

    void accept_connection()
    {
        while (TRUE) {
            member new_member(service);
            acceptor acc(*service,
                    endpoint(boost::asio::ip::address::from_string("127.0.0.1"),
                             8001));
            acc.accept(new_member.my_socket);
            my_lock.lock();
            tmp_clients.emplace_back(std::make_shared<member>
            (std::move(new_member)));
            client_list_changed = true;
            my_lock.unlock();
            BOOST_LOG_TRIVIAL(trace) << "new client connected";
        }
    }

//--------------------------------------------------------------------

//---------------------------- RELOAD --------------------------------

    void reload_vector() {
        while (tmp_clients.size() != 0) {
        clients.emplace_back((tmp_clients[tmp_clients.size() - 1]));
        clients[clients.size() -1]->_lastTime = clock();
        tmp_clients.pop_back();
        }
    }

//--------------------------------------------------------------------


    void log_init()
    {
        boost::log::register_simple_formatter_factory
        <boost::log::trivial::severity_level, char>("Severity");
        logging::add_file_log // расширенная настройка
        (
        logging::keywords::file_name = "log_%N.log",
        logging::keywords::rotation_size = SIZE_FILE,
        logging::keywords::time_based_rotation =
        boost::log::sinks::file::rotation_at_time_point{0, 0, 0},
        logging::keywords::format = "[%TimeStamp%] [%Severity%] %Message%");

        logging::add_console_log
                (
                std::cout,
        logging::keywords::format = "[%TimeStamp%] [%Severity%]: %Message%");
        logging::add_common_attributes();
    }

    void change_for_all()
    {
        for (auto it = clients.begin(); it != clients.end(); ++it) {
            (*it)->clients_changed = true;
        }
        client_list_changed = false;
    }

    boost::asio::io_service* service;
    std::vector<std::shared_ptr<member>> clients;
    std::vector<std::shared_ptr<member>> tmp_clients;
    std::mutex my_lock;
    bool client_list_changed;
};

int main() {
    std::cout << clock();
    server test;
    test.start();
    return 0;
}

#endif // INCLUDE_HEADER_HPP_
