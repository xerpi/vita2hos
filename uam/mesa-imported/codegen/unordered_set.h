#ifndef __NV50_UNORDERED_SET_H__
#define __NV50_UNORDERED_SET_H__

#if (__cplusplus >= 201103L)
#include <unordered_set>
#else
#include <tr1/unordered_set>
#endif

namespace nv50_ir {

#if __cplusplus >= 201103L
using std::unordered_set;
#else
using std::tr1::unordered_set;
#endif

} // namespace nv50_ir

#endif // __NV50_UNORDERED_SET_H__
