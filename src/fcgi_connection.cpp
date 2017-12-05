#include "fcgi_connection.h"
#include <assert.h>
#include <boost/bind.hpp>
#include <boost/thread/lock_guard.hpp>
#include "fcgi_app.h"
#include "fcgi_protocol.h"
#include "fcgi_request.h"
using namespace boost::asio;
using namespace boost::asio::ip;
using namespace boost::system;

enum ParseRecordError {
  ParseRecordError_Ok = 0,
  ParseRecordError_Head,
  ParseRecordError_Version,
  ParseRecordError_Type,
  ParseRecordError_NotComplete,
  ParseRecordError_Multiplex,
  ParseRecordError_Protocol,
  ParseRecordError_EndParams,
  ParseRecordError_EndStdIn,
  ParseRecordError_AbortRequest,
};

FcgiConnection::FcgiConnection(tcp::socket *sock)
    : _sock(sock),
      _req(NULL),
      _has_pending_write(false),
      _close_on_finish_write(false) {}

FcgiConnection::~FcgiConnection() {
  close();
  FcgiApp::instance()->decrease_connection_num();
  delete _sock;
  delete _req;
}

void FcgiConnection::post_async_read() {
  _sock->async_read_some(
      _reader.buf(),
      boost::bind(&FcgiConnection::read_handler, shared_from_this(), _1, _2));
}

void FcgiConnection::post_async_write() {
  const_buffers_1 buf(_writer.buf());
  if (buffer_size(buf) == 0) {
    _has_pending_write = false;
  } else {
    _sock->async_write_some(buf, boost::bind(&FcgiConnection::write_handler,
                                             shared_from_this(), _1, _2));
    _has_pending_write = true;
  }
}

void FcgiConnection::close() {
  error_code ec;
  _sock->close(ec);
}

bool FcgiConnection::stdout(int request_id, boost::asio::const_buffers_1 &buf) {
  boost::lock_guard<boost::mutex> guard(_mutex);

  bool ret = _writer.stdout(request_id, buf);
  if (ret && !_has_pending_write) {
    post_async_write();
  }

  return ret;
}

bool FcgiConnection::end_stdout(int request_id) {
  boost::lock_guard<boost::mutex> guard(_mutex);

  bool ret = _writer.end_stdout(request_id);
  if (ret && !_has_pending_write) {
    post_async_write();
  }

  return ret;
}

bool FcgiConnection::reply(int request_id, uint32_t code, bool close) {
  boost::lock_guard<boost::mutex> guard(_mutex);

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
        case ParseRecordError_Ok:
        case ParseRecordError_EndParams:
          _reader.next_record();
          break;
        case ParseRecordError_Head:
        case ParseRecordError_Version:
        case ParseRecordError_Type:
        case ParseRecordError_Multiplex:
        case ParseRecordError_Protocol:
        case ParseRecordError_AbortRequest:
          close();
          return;
        case ParseRecordError_NotComplete:
          break;
        case ParseRecordError_EndStdIn:
          deal_request();
          _reader.next_record();
          break;
        default:
          assert(false);
          close();
          return;
      }
    }

    _reader.clear_complete_record();

    if (_reader.buf_full()) {
      close();
    } else {
      post_async_read();
    }
  } else {
    close();
  }
}

void FcgiConnection::write_handler(const error_code &rc,
                                   size_t bytes_transferred) {
  if (!rc) {
    boost::lock_guard<boost::mutex> guard(_mutex);
    _writer.transferred(bytes_transferred);
    if (_close_on_finish_write && _writer.buf_empty()) {
      close();
    } else {
      post_async_write();
    }
  } else {
    close();
  }
}

int FcgiConnection::parse_record() {
  if (_reader.version() != FCGI_VERSION_1) return ParseRecordError_Version;

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
      return ParseRecordError_Type;
  }

  return ParseRecordError_Ok;
}

int FcgiConnection::parse_begin_request_record() {
  if (_req != NULL) return ParseRecordError_Multiplex;

  _req = new FcgiRequest;
  _req->set_request_id(_reader.request_id());
  _req->set_role(_reader.role());
  _req->set_flags(_reader.flags());

  return ParseRecordError_Ok;
}

int FcgiConnection::parse_params_record() {
  if (_req == NULL) return ParseRecordError_Protocol;
  if (_req->request_id() != _reader.request_id())
    return ParseRecordError_Multiplex;

  ParamsVector vec;
  _reader.params(vec);
  if (vec.empty()) return ParseRecordError_EndParams;

  _req->add_params(vec);
  return ParseRecordError_Ok;
}

int FcgiConnection::parse_stdin_record() {
  if (_req == NULL) return ParseRecordError_Protocol;
  if (_req->request_id() != _reader.request_id())
    return ParseRecordError_Multiplex;

  const const_buffer buf(_reader.content());
  if (buffer_size(buf) == 0) return ParseRecordError_EndStdIn;

  _req->add_stdin_data(buf);
  return ParseRecordError_Ok;
}

int FcgiConnection::parse_data_record() {
  assert(false);
  return ParseRecordError_Protocol;
}

int FcgiConnection::parse_abort_request_record() {
  assert(false);
  return ParseRecordError_AbortRequest;
}

int FcgiConnection::parse_get_values_record() {
  assert(false);
  return ParseRecordError_Protocol;
}

int FcgiConnection::deal_request() {
  _req->set_connection(shared_from_this());
  FcgiApp::instance()->push_request(_req);
  _req = NULL;
  return 0;
}
