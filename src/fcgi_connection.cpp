#include "fcgi_connection.h"
#include <assert.h>
#include <functional>
#include "fcgi_app.h"
#include "fcgi_protocol.h"
#include "fcgi_request.h"
using namespace std::placeholders;
using namespace boost::asio;
using namespace boost::asio::ip;
using namespace boost::system;

enum class ParseRecordError {
  Ok,
  Head,
  Version,
  Type,
  NotComplete,
  Multiplex,
  Protocol,
  EndParams,
  EndStdIn,
  AbortRequest,
};

FcgiConnection::FcgiConnection(tcp::socket *sock)
    : _sock(sock),
      _req(nullptr),
      _has_pending_write(false),
      _close_on_finish_write(false) {}

FcgiConnection::~FcgiConnection() {
  close();
  FcgiApp::instance()->decrease_connection_num();
  delete _sock;
  delete _req;
}

void FcgiConnection::post_async_read() {
  _sock->async_read_some(_reader.buf(), std::bind(&FcgiConnection::read_handler,
                                                  shared_from_this(), _1, _2));
}

void FcgiConnection::post_async_write() {
  const_buffers_1 buf(_writer.buf());
  if (buffer_size(buf) == 0) {
    _has_pending_write = false;
  } else {
    _sock->async_write_some(buf, std::bind(&FcgiConnection::write_handler,
                                           shared_from_this(), _1, _2));
    _has_pending_write = true;
  }
}

void FcgiConnection::close() {
  error_code ec;
  _sock->close(ec);
}

void FcgiConnection::shutdown() {
  error_code ec;
  _sock->shutdown(tcp::socket::shutdown_both, ec);
}

bool FcgiConnection::stdout(int request_id, boost::asio::const_buffers_1 &buf) {
  std::lock_guard<std::mutex> guard(_mutex);

  bool ret = _writer.stdout(request_id, buf);
  if (ret && !_has_pending_write) {
    post_async_write();
  }

  return ret;
}

bool FcgiConnection::end_stdout(int request_id) {
  std::lock_guard<std::mutex> guard(_mutex);

  bool ret = _writer.end_stdout(request_id);
  if (ret && !_has_pending_write) {
    post_async_write();
  }

  return ret;
}

bool FcgiConnection::reply(int request_id, uint32_t code, bool close) {
  std::lock_guard<std::mutex> guard(_mutex);

  bool ret = _writer.reply(request_id, code);
  _close_on_finish_write = close;
  if (ret && !_has_pending_write) {
    post_async_write();
  }

  return ret;
}

void FcgiConnection::read_handler(const error_code &rc,
                                  size_t bytes_transferred) {
  if (!rc) {
    _reader.transferred(bytes_transferred);

    while (_reader.can_read()) {
      switch (parse_record()) {
        case ParseRecordError::Ok:
        case ParseRecordError::EndParams:
          _reader.next_record();
          break;
        case ParseRecordError::Head:
        case ParseRecordError::Version:
        case ParseRecordError::Type:
        case ParseRecordError::Multiplex:
        case ParseRecordError::Protocol:
        case ParseRecordError::AbortRequest:
          return;
        case ParseRecordError::NotComplete:
          break;
        case ParseRecordError::EndStdIn:
          deal_request();
          _reader.next_record();
          break;
        default:
          assert(false);
          return;
      }
    }

    _reader.clear_complete_record();

    if (_reader.buf_full()) {
    } else {
      post_async_read();
    }
  } else {
  }
}

void FcgiConnection::write_handler(const error_code &rc,
                                   size_t bytes_transferred) {
  if (!rc) {
    std::lock_guard<std::mutex> guard(_mutex);
    _writer.transferred(bytes_transferred);
    if (_close_on_finish_write && _writer.buf_empty()) {
      shutdown();
    } else {
      post_async_write();
    }
  } else {
  }
}

ParseRecordError FcgiConnection::parse_record() {
  if (_reader.version() != FCGI_VERSION_1) return ParseRecordError::Version;

  switch (_reader.type()) {
    case FCGI_BEGIN_REQUEST:
      return parse_begin_request_record();
    case FCGI_ABORT_REQUEST:
      return parse_abort_request_record();
    case FCGI_GET_VALUES:
      return parse_get_values_record();
    case FCGI_PARAMS:
      return parse_params_record();
    case FCGI_STDIN:
      return parse_stdin_record();
    case FCGI_DATA:
      return parse_data_record();
    default:
      return ParseRecordError::Type;
  }

  return ParseRecordError::Ok;
}

ParseRecordError FcgiConnection::parse_begin_request_record() {
  if (_req != nullptr) return ParseRecordError::Multiplex;

  _req = new FcgiRequest;
  _req->set_request_id(_reader.request_id());
  _req->set_role(_reader.role());
  _req->set_flags(_reader.flags());

  return ParseRecordError::Ok;
}

ParseRecordError FcgiConnection::parse_params_record() {
  if (_req == nullptr) return ParseRecordError::Protocol;
  if (_req->request_id() != _reader.request_id())
    return ParseRecordError::Multiplex;

  ParamsVector vec;
  _reader.params(vec);
  if (vec.empty()) return ParseRecordError::EndParams;

  _req->add_params(vec);
  return ParseRecordError::Ok;
}

ParseRecordError FcgiConnection::parse_stdin_record() {
  if (_req == nullptr) return ParseRecordError::Protocol;
  if (_req->request_id() != _reader.request_id())
    return ParseRecordError::Multiplex;

  const const_buffer buf(_reader.content());
  if (buffer_size(buf) == 0) return ParseRecordError::EndStdIn;

  _req->add_stdin_data(buf);
  return ParseRecordError::Ok;
}

ParseRecordError FcgiConnection::parse_data_record() {
  assert(false);
  return ParseRecordError::Protocol;
}

ParseRecordError FcgiConnection::parse_abort_request_record() {
  assert(false);
  return ParseRecordError::AbortRequest;
}

ParseRecordError FcgiConnection::parse_get_values_record() {
  assert(false);
  return ParseRecordError::Protocol;
}

int FcgiConnection::deal_request() {
  _req->set_connection(weak_from_this());
  FcgiApp::instance()->push_request(_req);
  _req = nullptr;
  return 0;
}
