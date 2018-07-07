#include "fcgi_app.h"
#include <algorithm>
#include <functional>
#include <iterator>
#include <memory>
#include <sstream>
#include "fcgi_connection.h"
#include "fcgi_protocol.h"
#include "fcgi_request.h"
using namespace std::placeholders;
using namespace boost::asio;
using namespace boost::asio::ip;
using namespace boost::system;

FcgiApp *FcgiApp::s_app = nullptr;

FcgiApp::FcgiApp()
    : _acceptor(nullptr),
      _thread_num(1),
      _dequeue_req_num(0),
      _enqueue_req_num(0),
      _connection_num(0) {}

FcgiApp::~FcgiApp() {
  _acceptor->close();
  _io_service.stop();
  std::for_each(std::begin(_io_thread_group), std::end(_io_thread_group),
                [](auto &t) { t.join(); });
  delete _acceptor;
  while (!_queue.empty()) {
    auto req = _queue.front();
    _queue.pop();
    delete req;
  }
}

void FcgiApp::new_instance() { s_app = new FcgiApp; }
void FcgiApp::delete_instance() { delete s_app; }
FcgiApp *FcgiApp::instance() { return s_app; }

void FcgiApp::post_async_accept() {
  auto sock = new tcp::socket(_io_service);
  _acceptor->async_accept(*sock,
                          std::bind(&FcgiApp::accept_handler, this, sock, _1));
}

void FcgiApp::accept_handler(tcp::socket *sock, const error_code &rc) {
  if (!rc) {
    socket_base::linger option(true, 30);
    sock->set_option(option);

    auto conn = std::make_shared<FcgiConnection>(sock);
    conn->post_async_read();
    _connection_num.fetch_add(1, std::memory_order_relaxed);
  }
  post_async_accept();
}

void FcgiApp::io_function() { _io_service.run(); }

FcgiRequest *FcgiApp::pop_request_blocking() {
  FcgiRequest *req = nullptr;
  std::unique_lock<std::mutex> guard(_mutex);
  while (_queue.empty()) {
    _cond.wait(guard);
  }
  req = _queue.front();
  _queue.pop();
  ++_dequeue_req_num;
  return req;
}

FcgiRequest *FcgiApp::pop_request_nonblocking() {
  FcgiRequest *req = nullptr;
  std::unique_lock<std::mutex> guard(_mutex);
  if (!_queue.empty()) {
    req = _queue.front();
    _queue.pop();
    ++_dequeue_req_num;
  }
  return req;
}

void FcgiApp::push_request(FcgiRequest *req) {
  {
    std::unique_lock<std::mutex> guard(_mutex);
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
  std::generate_n(
      std::back_insert_iterator<decltype(_io_thread_group)>(_io_thread_group),
      _thread_num,
      [this]() { return std::thread(&FcgiApp::io_function, this); });
}

void FcgiApp::decrease_connection_num() {
  _connection_num.fetch_sub(1, std::memory_order_relaxed);
}

void FcgiApp::reset_statistics() {
  std::unique_lock<std::mutex> guard(_mutex);
  _enqueue_req_num = 0;
  _dequeue_req_num = 0;
}

std::string FcgiApp::statistics() const {
  std::ostringstream oss;
  oss << "thread_num=" << _thread_num;
  oss << " connection_num=" << _connection_num.load(std::memory_order_relaxed);
  oss << " enqueue_num=" << _enqueue_req_num;
  oss << " dequeue_num=" << _dequeue_req_num;
  return oss.str();
}
