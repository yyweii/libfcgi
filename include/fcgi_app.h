#ifndef FCGI_APP_H_
#define FCGI_APP_H_

#include <boost/asio.hpp>
#include <boost/atomic.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>
#include <queue>
#include <string>

class FcgiRequest;

class FcgiApp {
 private:
  FcgiApp();
  virtual ~FcgiApp();

 public:
  static void new_instance();
  static void delete_instance();
  static FcgiApp *instance();

 public:
  void start(int thread_num);

  FcgiRequest *pop_request_blocking();
  FcgiRequest *pop_request_nonblocking();
  void push_request(FcgiRequest *);
  void free_request(FcgiRequest *);

  void decrease_connection_num();
  void reset_statistics();
  std::string statistics() const;

 private:
  void post_async_accept();
  void accept_handler(boost::asio::ip::tcp::socket *,
                      const boost::system::error_code &);
  void io_function();

 private:
  boost::asio::io_service _io_service;
  boost::asio::ip::tcp::acceptor *_acceptor;
  boost::thread_group _io_thread_group;

  boost::mutex _mutex;
  boost::condition_variable _cond;
  std::queue<FcgiRequest *> _queue;

  int _thread_num;
  int _dequeue_req_num;
  int _enqueue_req_num;
  boost::atomic<int> _connection_num;

  static FcgiApp *s_app;
};

#endif
