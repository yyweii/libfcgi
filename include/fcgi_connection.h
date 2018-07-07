#ifndef FCGI_CONNECTION_H_
#define FCGI_CONNECTION_H_

#include <boost/asio.hpp>
#include <memory>
#include <mutex>
#include "fcgi_record.h"
class FcgiRequest;
enum class ParseRecordError;

class FcgiConnection : public std::enable_shared_from_this<FcgiConnection> {
 public:
  FcgiConnection(boost::asio::ip::tcp::socket *);
  virtual ~FcgiConnection();

 public:
  void post_async_read();

  bool stdout(int request_id, boost::asio::const_buffers_1 &);
  bool end_stdout(int request_id);
  bool reply(int request_id, uint32_t code, bool close);

 private:
  void close();
  void shutdown();

  void read_handler(const boost::system::error_code &,
                    size_t bytes_transferred);
  void write_handler(const boost::system::error_code &,
                     size_t bytes_transferred);
  void post_async_write();

  ParseRecordError parse_record();
  ParseRecordError parse_begin_request_record();
  ParseRecordError parse_abort_request_record();
  ParseRecordError parse_get_values_record();
  ParseRecordError parse_params_record();
  ParseRecordError parse_stdin_record();
  ParseRecordError parse_data_record();

  int deal_request();

 private:
  boost::asio::ip::tcp::socket *_sock;
  FcgiRecordReader _reader;
  FcgiRecordWriter _writer;
  FcgiRequest *_req;
  bool _has_pending_write;
  bool _close_on_finish_write;
  std::mutex _mutex;
};

#endif
