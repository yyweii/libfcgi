#ifndef FCGI_TYPES_H_
#define FCGI_TYPES_H_

#include <map>
#include <vector>

struct FcgiParam {
  FcgiParam() : _name(NULL), _value(NULL), _name_len(0), _value_len(0) {}
  FcgiParam(char *n, char *v, int nl, int vl)
      : _name(n), _value(v), _name_len(nl), _value_len(vl) {}

  char *_name;
  char *_value;
  int _name_len;
  int _value_len;
};

typedef std::vector<FcgiParam> ParamsVector;

typedef std::map<std::string, std::string> ParamsMap;

#endif
