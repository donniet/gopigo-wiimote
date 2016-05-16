#ifndef __SIMPLE_SOCKET_HPP__
#define __SIMPLE_SOCKET_HPP__

#include <memory>
#include <map>
#include <thread>
#include <sstream>
#include <mutex>

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <poll.h>

using std::map;
using std::thread;
using std::shared_ptr;
using std::make_shared;
using std::mutex;
using std::unique_lock;
using std::unique_ptr;
using std::stringstream;
using std::string;

template<typename T>
struct default_stringer {
  string operator()(T const & in) {
    stringstream sstr;
    sstr << in;
    return sstr.str();
  }
};

template<typename T, typename Stringer = default_stringer<T>>
class simple_socket_server {
private:
  unique_ptr<thread> paccept_thread;

  map<int, struct sockaddr_in> connections;
  mutex connections_mutex;
  Stringer stringer;

  int sockfd;
  sockaddr_in serv_addr;
protected:
  void accept_thread() {
    int newfd;
    struct sockaddr_in cli_addr;
    socklen_t socklen = sizeof(cli_addr);

    while((newfd = ::accept(sockfd, (sockaddr *)&cli_addr, &socklen)) > 0) {
      unique_lock<mutex> lock(connections_mutex);
      connections.insert({newfd, cli_addr});
    }
    ::close(sockfd);
  }
  void send_to_all(string const & str) {
    struct pollfd pfd;
    unique_lock<mutex> lock(connections_mutex);
    auto i = connections.begin();
    while(i != connections.end()) {
      int fd = i->first;

      bzero(&pfd, sizeof(pollfd));
      pfd.fd = fd;
      pfd.events = POLLHUP;
      pfd.revents = 0;

      if (::poll(&pfd, 1, 0) >= 0 && pfd.revents == 0 && ::write(fd, str.c_str(), str.size()) == str.size()) {
        ++i;
      } else {
        ::close(fd);
        auto j = i;
        ++i;

        connections.erase(j);
      }
    }
  }
public:
  simple_socket_server(int portno, Stringer stringer = Stringer())
    : stringer(stringer)
  {
    sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
    bzero((char*)&serv_addr, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    if (::bind(sockfd, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
      throw std::logic_error("could not bind to socket");
    }

    if (::listen(sockfd, 5) < 0) {
      throw std::logic_error("could not listen on socket");
    }

    paccept_thread = new thread(&simple_socket_server::accept_thread, this);
  }
  simple_socket_server(simple_socket_server<T> const &) = delete;
  simple_socket_server(simple_socket_server<T> &&) = delete;
  void send(T const & t) {
    send_to_all(stringer(t));
  }
  void close() {
    unique_lock<mutex> lock(connections_mutex);
    auto i = connections.begin();
    while(i != connections.end()) {
      auto j = i;
      ++i;

      ::close(j->first);
      connections.erase(j);
    }
    ::close(sockfd);
    paccept_thread->join();
  }
  ~simple_socket_server() {
    close();
  }

};

#endif
