#pragma once

// 
// A simple structure that holds a char buffer containing a valid C string
//
template <int N>
struct char_buffer
{
public:
  char data[N];
public:
  inline char_buffer<N>&
  operator=(const char *str)
  {
    strlcpy(this->data, str, N);
    return *this;
  }
  
  template <int M> 
  inline char_buffer<N>&
  operator=(const char_buffer<M> &str)
  {
    strlcpy(this->data, str.data, N);
    return *this;
  }
  
  inline bool operator==(const char *str) {
    return strcmp(this->data,str)==0 ;
  }

  template <int M> 
  inline bool operator==(const char_buffer<M> &str) {
    return strcmp(this->data, str->data)==0 ;
  }

  inline char *c_str() { return this->data; }
  inline int length() { return strlen(this->data); }
  inline bool empty() { return this->data[0]==0; }
};
