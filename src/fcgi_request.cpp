#include "fcgi_request.h"
#include "fcgi_connection.h"
#include "fcgi_protocol.h"
using namespace boost::asio;

FcgiRequest::FcgiRequest() : _request_id(0), _role(0), _flags(0) {}

FcgiRequest::~FcgiRequest() {}

int FcgiRequest::request_id() const { return _request_id; }

void FcgiRequest::set_request_id(int id) { _request_id = id; }

int FcgiRequest::role() const { return _role; }

void FcgiRequest::set_role(int role) { _role = role; }

int FcgiRequest::flags() const { return _flags; }

void FcgiRequest::set_flags(int flags) { _flags = flags; }

const ParamsMap &FcgiRequest::params() const { return _params; }

void FcgiRequest::add_params(const ParamsVector &vec) {
  for (ParamsVector::const_iterator it = vec.begin(); it != vec.end(); ++it) {
    const FcgiParam &p = *it;

    _params[std::string(p._name, p._name_len)] =
        std::string(p._value, p._value_len);
  }
}

bool FcgiRequest::get_param(const char *name, std::string &value) const {
  ParamsMap::const_iterator it = _params.find(name);
  if (it == _params.end()) return false;
  value = it->second;
  return true;
}

bool FcgiRequest::get_param(const std::string &name, std::string &value) const {
  return get_param(name.c_str(), value);
}

const std::string &FcgiRequest::stdin() const { return _stdin; }

void FcgiRequest::add_stdin_data(const const_buffer &buf) {
  _stdin += std::string(buffer_cast<const char *>(buf), buffer_size(buf));
}

void FcgiRequest::set_connection(boost::weak_ptr<FcgiConnection> ptr) {
  _conn = ptr;
}

bool FcgiRequest::stdout(const std::string &str) {
  const_buffers_1 buf(str.c_str(), str.size());
  return stdout(buf);
}

bool FcgiRequest::stdout(const_buffers_1 &buf) {
  boost::shared_ptr<FcgiConnection> conn = _conn.lock();
  bool ret = false;
  if (conn != NULL) ret = conn->stdout(request_id(), buf);
  return ret;
}

bool FcgiRequest::end_stdout() {
  boost::shared_ptr<FcgiConnection> conn = _conn.lock();
  bool ret = false;
  if (conn != NULL) ret = conn->end_stdout(request_id());
  return ret;
}

bool FcgiRequest::reply(uint32_t code) {
  boost::shared_ptr<FcgiConnection> conn = _conn.lock();
  bool ret = false;
  if (conn != NULL) {
    ret = conn->reply(request_id(), code);
    if (!(flags() & FCGI_KEEP_CONN)) {
      conn->set_close_on_finish_write();
    }
  }
  return ret;
}
