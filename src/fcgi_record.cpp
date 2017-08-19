#include "fcgi_record.h"
#include <limits.h>
#include "fcgi_protocol.h"
using namespace boost::asio;

static const int FCGI_RECORD_MAX_LEN = 1024 * 128 * 8;
static const int FCGI_CONTENT_MAX_LEN = 65528;
static int AlignInt8(unsigned n) { return (n + 7) & (UINT_MAX - 7); }

FcgiRecordReader::FcgiRecordReader()
    : _buf((char *)malloc(FCGI_RECORD_MAX_LEN)), _len(0), _idx(0) {}

FcgiRecordReader::~FcgiRecordReader() { free(_buf); }

bool FcgiRecordReader::can_read() const {
  return has_valid_head() && is_complete();
}

int FcgiRecordReader::complete_length() const {
  return FCGI_HEADER_LEN + content_length() + padding_length();
}

bool FcgiRecordReader::is_complete() const { return complete_length() <= _len; }

bool FcgiRecordReader::has_valid_head() const {
  return FCGI_HEADER_LEN <= _len;
}

int FcgiRecordReader::version() const {
  const FCGI_Header *head = (FCGI_Header *)(_buf + _idx);
  return head->version;
}

int FcgiRecordReader::type() const {
  const FCGI_Header *head = (FCGI_Header *)(_buf + _idx);
  return head->type;
}

int FcgiRecordReader::request_id() const {
  const FCGI_Header *head = (FCGI_Header *)(_buf + _idx);
  return (int(head->requestIdB1) << 8) + head->requestIdB0;
}

int FcgiRecordReader::content_length() const {
  const FCGI_Header *head = (FCGI_Header *)(_buf + _idx);
  return (int(head->contentLengthB1) << 8) + head->contentLengthB0;
}

int FcgiRecordReader::padding_length() const {
  const FCGI_Header *head = (FCGI_Header *)(_buf + _idx);
  return head->paddingLength;
}

int FcgiRecordReader::role() const {
  const FCGI_BeginRequestRecord *record =
      (FCGI_BeginRequestRecord *)(_buf + _idx);
  const FCGI_BeginRequestBody &b = record->body;
  return (int(b.roleB1) << 8) + b.roleB0;
}

int FcgiRecordReader::flags() const {
  const FCGI_BeginRequestRecord *record =
      (FCGI_BeginRequestRecord *)(_buf + _idx);
  const FCGI_BeginRequestBody &b = record->body;
  return b.flags;
}

void FcgiRecordReader::params(ParamsVector &vec) const {
  int content_len = content_length();
  unsigned char *next = (unsigned char *)(_buf + _idx + FCGI_HEADER_LEN);

  while (0 < content_len) {
    int param_len = 0;
    FcgiParam param;

    if ((*next >> 7) == 0) {
      param._name_len = *next;
      next += 1;
      ++param_len;
    } else {
      param._name_len = int(*next & 0x7f) << 24;
      next += 1;
      param._name_len += int(*next) << 16;
      next += 1;
      param._name_len += int(*next) << 8;
      next += 1;
      param._name_len += int(*next);
      next += 1;

      param_len += 4;
    }
    param_len += param._name_len;

    if ((*next >> 7) == 0) {
      param._value_len = *next;
      next += 1;
      ++param_len;
    } else {
      param._value_len = int(*next & 0x7f) << 24;
      next += 1;
      param._value_len += int(*next) << 16;
      next += 1;
      param._value_len += int(*next) << 8;
      next += 1;
      param._value_len += int(*next);
      next += 1;

      param_len += 4;
    }
    param_len += param._value_len;

    param._name = (char *)next;
    next += param._name_len;
    param._value = (char *)next;
    next += param._value_len;
    vec.push_back(param);

    content_len -= param_len;
  }
}

const_buffers_1 FcgiRecordReader::content() const {
  return const_buffers_1(_buf + _idx + FCGI_HEADER_LEN, content_length());
}

mutable_buffers_1 FcgiRecordReader::buf() const {
  return mutable_buffers_1(_buf + _idx + _len,
                           std::max(0, FCGI_RECORD_MAX_LEN - _idx - _len));
}

bool FcgiRecordReader::buf_full() const {
  return FCGI_RECORD_MAX_LEN <= _idx + _len;
}

void FcgiRecordReader::next_record() {
  const int total = complete_length();
  _idx += total;
  _len -= total;
}

void FcgiRecordReader::clear_complete_record() {
  if (_idx != 0) {
    if (0 != _len) memmove(_buf, _buf + _idx, _len);
    _idx = 0;
  }
}

void FcgiRecordReader::transferred(int len) { _len += len; }

////////////////////////////////////////////////////////////////////////////
FcgiRecordWriter::FcgiRecordWriter()
    : _buf((char *)malloc(FCGI_RECORD_MAX_LEN)), _len(0) {}

FcgiRecordWriter::~FcgiRecordWriter() { free(_buf); }

void FcgiRecordWriter::set_version(int v) {
  FCGI_Header *head = (FCGI_Header *)(_buf + _len);
  head->version = v;
}

void FcgiRecordWriter::set_type(int t) {
  FCGI_Header *head = (FCGI_Header *)(_buf + _len);
  head->type = t;
}

void FcgiRecordWriter::set_request_id(int id) {
  FCGI_Header *head = (FCGI_Header *)(_buf + _len);
  head->requestIdB1 = (id & 0x0000ff00) >> 8;
  head->requestIdB0 = id & 0x000000ff;
}

void FcgiRecordWriter::set_content_length(int contentLen) {
  FCGI_Header *head = (FCGI_Header *)(_buf + _len);
  head->contentLengthB1 = (contentLen & 0x0000ff00) >> 8;
  head->contentLengthB0 = contentLen & 0x000000ff;
}

void FcgiRecordWriter::set_content(const_buffers_1 &content) {
  char *body = _buf + _len + FCGI_HEADER_LEN;
  unsigned contentLen = buffer_size(content);
  memcpy(body, buffer_cast<const char *>(content), contentLen);
  set_content_length(contentLen);
  set_padding(AlignInt8(contentLen) - contentLen);
}

void FcgiRecordWriter::set_padding(int paddingLen) {
  FCGI_Header *head = (FCGI_Header *)(_buf + _len);
  head->paddingLength = paddingLen;
}

void FcgiRecordWriter::set_app_status(uint32_t code) {
  FCGI_EndRequestRecord *record = (FCGI_EndRequestRecord *)(_buf + _len);
  FCGI_EndRequestBody &b = record->body;
  b.appStatusB3 = (code & 0xff000000) >> 24;
  b.appStatusB2 = (code & 0x00ff0000) >> 16;
  b.appStatusB1 = (code & 0x0000ff00) >> 8;
  b.appStatusB0 = (code & 0x000000ff);
}

void FcgiRecordWriter::set_protocol_status(int code) {
  FCGI_EndRequestRecord *record = (FCGI_EndRequestRecord *)(_buf + _len);
  FCGI_EndRequestBody &b = record->body;
  b.protocolStatus = code;
}

const_buffers_1 FcgiRecordWriter::buf() const {
  return const_buffers_1(_buf, _len);
}

bool FcgiRecordWriter::buf_empty() const { return _len == 0; }

void FcgiRecordWriter::next_record() { _len += complete_length(); }

bool FcgiRecordWriter::can_write(int len) const {
  return len <= FCGI_RECORD_MAX_LEN - _len;
}

int FcgiRecordWriter::complete_length() const {
  return FCGI_HEADER_LEN + content_length() + padding_length();
}

int FcgiRecordWriter::content_length() const {
  const FCGI_Header *head = (FCGI_Header *)(_buf + _len);
  return (int(head->contentLengthB1) << 8) + head->contentLengthB0;
}

int FcgiRecordWriter::padding_length() const {
  const FCGI_Header *head = (FCGI_Header *)(_buf + _len);
  return head->paddingLength;
}

void FcgiRecordWriter::transferred(int len) {
  _len -= len;
  memmove(_buf, _buf + len, _len);
}

bool FcgiRecordWriter::stdout(int request_id,
                              boost::asio::const_buffers_1 &buf) {
  int buf_len = buffer_size(buf);
  int record_num = buf_len / FCGI_CONTENT_MAX_LEN;
  int bytes_required = (FCGI_HEADER_LEN + FCGI_CONTENT_MAX_LEN) * record_num;
  int last_record_len = buf_len % FCGI_CONTENT_MAX_LEN;
  if (last_record_len != 0) {
    ++record_num;
    bytes_required += FCGI_HEADER_LEN + AlignInt8(last_record_len);
  }

  if (!can_write(bytes_required)) return false;

  const char *b = buffer_cast<const char *>(buf);
  while (0 < buf_len) {
    int record_len = std::min(FCGI_CONTENT_MAX_LEN, buf_len);

    set_version(FCGI_VERSION_1);
    set_type(FCGI_STDOUT);
    set_request_id(request_id);
    const_buffers_1 content_buf(b, record_len);
    set_content(content_buf);

    b += record_len;
    buf_len -= record_len;

    next_record();
  }

  return true;
}

bool FcgiRecordWriter::end_stdout(int request_id) {
  int bytes_required = FCGI_HEADER_LEN;

  if (!can_write(bytes_required)) return false;

  set_version(FCGI_VERSION_1);
  set_type(FCGI_STDOUT);
  set_request_id(request_id);
  set_content_length(0);
  set_padding(0);

  next_record();
  return true;
}

bool FcgiRecordWriter::reply(int request_id, uint32_t code) {
  int bytes_required = FCGI_HEADER_LEN + 8;

  if (!can_write(bytes_required)) return false;

  set_version(FCGI_VERSION_1);
  set_type(FCGI_END_REQUEST);
  set_request_id(request_id);
  set_content_length(8);
  set_padding(0);
  set_app_status(code);
  set_protocol_status(FCGI_REQUEST_COMPLETE);

  next_record();
  return true;
}
