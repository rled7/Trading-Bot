#pragma once
#include <functional>
#include <string>
#include <cmath>
#include <cstdio>

/* run_test and section are defined in test_main.cpp.
   Each test file takes them by reference. */
void run_test(const char *name, std::function<void()> fn);
void section (const char *title);

/* Canonical type for the test runner parameter */
using RawTestFn = void(const char*, std::function<void()>);
using TestRunner = RawTestFn&;

/* assertion helpers */
inline void _chk(bool c,const char*e,const char*f,int l){
    if(!c){char m[512];snprintf(m,512,"%s:%d  ASSERT(%s)",f,l,e);throw std::string(m);}
}
#define CHK(e)          _chk((bool)(e),#e,__FILE__,__LINE__)
#define CHK_GT(a,b)     _chk((a)>(b),  #a ">" #b,  __FILE__,__LINE__)
#define CHK_GE(a,b)     _chk((a)>=(b), #a ">=" #b, __FILE__,__LINE__)
#define CHK_LE(a,b)     _chk((a)<=(b), #a "<=" #b, __FILE__,__LINE__)
#define CHK_LT(a,b)     _chk((a)<(b),  #a "<" #b,  __FILE__,__LINE__)
#define CHK_EQ(a,b)     _chk((a)==(b), #a "==" #b, __FILE__,__LINE__)
#define CHK_NEAR(a,b,t) _chk(std::fabs((a)-(b))<=(t),#a "~=" #b,__FILE__,__LINE__)
#define CHK_FALSE(e)    _chk(!(bool)(e),"!" #e,__FILE__,__LINE__)
