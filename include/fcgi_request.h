#ifndef FCGI_REQUEST_H_
#define FCGI_REQUEST_H_

#include <stdint.h>
#include <boost/asio/buffer.hpp>
#include <memory>
#include <string>
#include "fcgi_types.h"
class FcgiConnection;

class FcgiRequest {
 public:
  FcgiRequest();
  virtual ~FcgiRequest();
  FcgiRequest(const FcgiRequest &) = delete;
  FcgiRequest &operator=(const FcgiRequest &) = delete;

 public:
  int request_id() const;
  void set_request_id(int);
  int role() const;
  void set_role(int);
  int flags() const;
  void set_flags(int);
  const ParamsMap &params() const;
  void add_params(const ParamsVector &);
  bool get_param(const char *name, std::string &value) const;
  bool get_param(const std::string &name, std::string &value) const;
  const std::string &stdin() const;
  void add_stdin_data(const boost::asio::const_buffer &);

  void set_connection(std::weak_ptr<FcgiConnection>);

  bool stdout(boost::asio::const_buffers_1 &);
  bool stdout(const std::string &);
  bool end_stdout();
  bool reply(uint32_t code);

 private:
  std::weak_ptr<FcgiConnection> _conn;
  int _request_id;
  int _role;
  int _flags;

  ParamsMap _params;
  std::string _stdin;
};

#endif
