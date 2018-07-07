#ifndef FCGI_RECORD_H_
#define FCGI_RECORD_H_

#include <stdint.h>
#include <boost/asio/buffer.hpp>
#include "fcgi_types.h"

class FcgiRecordReader {
 public:
  FcgiRecordReader();
  virtual ~FcgiRecordReader();
  FcgiRecordReader(const FcgiRecordReader &) = delete;
  FcgiRecordReader &operator=(const FcgiRecordReader &) = delete;

 public:
  bool can_read() const;
  int complete_length() const;
  bool is_complete() const;

  bool has_valid_head() const;
  int version() const;
  int type() const;
  int request_id() const;
  int content_length() const;
  int padding_length() const;

  int role() const;
  int flags() const;

  void params(ParamsVector &) const;
  boost::asio::const_buffers_1 content() const;

 public:
  boost::asio::mutable_buffers_1 buf() const;
  bool buf_full() const;
  void next_record();
  void clear_complete_record();
  void transferred(int);

 private:
  char *_buf;
  int _len;
  int _idx;
};

class FcgiRecordWriter {
 public:
  FcgiRecordWriter();
  virtual ~FcgiRecordWriter();
  FcgiRecordWriter(const FcgiRecordWriter &) = delete;
  FcgiRecordWriter &operator=(const FcgiRecordWriter &) = delete;

 public:
  boost::asio::const_buffers_1 buf() const;
  bool buf_empty() const;
  void transferred(int);
  bool stdout(int request_id, boost::asio::const_buffers_1 &);
  bool end_stdout(int request_id);
  bool reply(int request_id, uint32_t code);

 private:
  void set_version(int);
  void set_type(int);
  void set_request_id(int);
  void set_content_length(int);
  void set_content(boost::asio::const_buffers_1 &);
  void set_padding(int);
  void set_app_status(uint32_t);
  void set_protocol_status(int);

  bool can_write(int len) const;
  void next_record();

  int complete_length() const;
  int content_length() const;
  int padding_length() const;

 private:
  char *_buf;
  int _len;
};

#endif
