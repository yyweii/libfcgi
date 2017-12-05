#include "fcgi_app.h"
#include <boost/bind.hpp>
#include <boost/make_shared.hpp>
#include <boost/thread/lock_guard.hpp>
#include <sstream>
#include "fcgi_connection.h"
#include "fcgi_protocol.h"
#include "fcgi_request.h"
using namespace boost::asio;
using namespace boost::asio::ip;
using namespace boost::system;

FcgiApp *FcgiApp::s_app = NULL;

FcgiApp::FcgiApp()
    : _acceptor(NULL),
      _thread_num(1),
      _dequeue_req_num(0),
      _enqueue_req_num(0),
      _connection_num(0) {}

FcgiApp::~FcgiApp() {
  _acceptor->close();
  _io_service.stop();
  _io_thread_group.join_all();
  delete _acceptor;
  while (!_queue.empty()) {
    FcgiRequest *req = _queue.front();
    _queue.pop();
    delete req;
  }
}

void FcgiApp::new_instance() { s_app = new FcgiApp; }
void FcgiApp::delete_instance() { delete s_app; }
FcgiApp *FcgiApp::instance() { return s_app; }

void FcgiApp::post_async_accept() {
  tcp::socket *sock = new tcp::socket(_io_service);
  _acceptor->async_accept(
      *sock, boost::bind(&FcgiApp::accept_handler, this, sock, _1));
}

void FcgiApp::accept_handler(tcp::socket *sock, const error_code &rc) {
  if (!rc) {
    socket_base::linger option(true, 30);
    sock->set_option(option);

    boost::shared_ptr<FcgiConnection> conn =
        boost::make_shared<FcgiConnection>(sock);
    conn->post_async_read();
    ++_connection_num;
  }
  post_async_accept();
}

void FcgiApp::io_function() { _io_service.run(); }

FcgiRequest *FcgiApp::pop_request_blocking() {
  FcgiRequest *req = NULL;
  boost::unique_lock<boost::mutex> guard(_mutex);
  while (_queue.empty()) {
    _cond.wait(guard);
  }
  req = _queue.front();
  _queue.pop();
  ++_dequeue_req_num;
  return req;
}

FcgiRequest *FcgiApp::pop_request_nonblocking() {
  FcgiRequest *req = NULL;
  boost::unique_lock<boost::mutex> guard(_mutex);
  if (!_queue.empty()) {
    req = _queue.front();
    _queue.pop();
    ++_dequeue_req_num;
  }
  return req;
}

void FcgiApp::push_request(FcgiRequest *req) {
  {
    boost::unique_lock<boost::mutex> guard(_mutex);
    _queue.push(req);
    ++_enqueue_req_num;
  }
  _cond.notify_one();
}

void FcgiApp::free_request(FcgiRequest *req) { delete req; }

void FcgiApp::start(int thread_num) {
  _acceptor = new tcp::acceptor(_io_service, tcp::v4(), FCGI_LISTENSOCK_FILENO);
  post_async_accept();

  _thread_num = thread_num;
  for (int i = 0; i < thread_num; ++i) {
    _io_thread_group.create_thread(boost::bind(&FcgiApp::io_function, this));
  }
}

void FcgiApp::decrease_connection_num() { --_connection_num; }

void FcgiApp::reset_statistics() {
  boost::unique_lock<boost::mutex> guard(_mutex);
  _enqueue_req_num = 0;
  _dequeue_req_num = 0;
}

std::string FcgiApp::statistics() const {
  std::ostringstream oss;
  oss << "thread_num=" << _thread_num;
  oss << " connection_num=" << _connection_num;
  oss << " enqueue_num=" << _enqueue_req_num;
  oss << " dequeue_num=" << _dequeue_req_num;
  return oss.str();
}
