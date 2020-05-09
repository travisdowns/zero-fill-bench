#include <inttypes.h>
#include <stddef.h>

constexpr size_t CACHE_LINE_BYTES = 64;

using buf_elem = int;
using cal_f = void(buf_elem* buf, size_t bufsz);

cal_f memset0;
cal_f memset1;
cal_f fill0;
cal_f fill1;
cal_f filln1;
cal_f avx0;
cal_f avx1;
cal_f avx01;
cal_f avx10;
cal_f fill00;
cal_f fill01;
cal_f fill11;
cal_f count0;
cal_f count1;
cal_f std_memcpy;