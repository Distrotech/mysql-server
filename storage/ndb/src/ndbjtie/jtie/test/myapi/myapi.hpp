/*
 Copyright (C) 2009 Sun Microsystems, Inc.
 All rights reserved. Use is subject to license terms.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; version 2 of the License.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/
/*
 * myapi.hpp
 */

#ifndef _myapi
#define _myapi

#include <stdio.h> // not using namespaces yet

/*
 * This test uses the C99 exact-width type aliases s int8_t, uint8_t,
 * ... int64_t, uint64_t as defined in <stdint.h>.
 *
 * Unfortunately, some C/C++ compiler still lack a stdint.h header file.
 * (For instance, MS Visual Studio until VS2010.)  We delegate to a helper
 * file that handles the absence of the <stdint.h> (without introducing
 * a dependency upon JTie or NDB API).
 */
#include "mystdint.h"

#include "helpers.hpp"

// XXX reference returns
//extern [const] int& f0(); -> ByteBuffer
//extern [const] int* f0(); -> ByteBuffer, but no way of knowing size
//extern [const] char* f0(); -> ByteBuffer, but no way of knowing size
//extern const char* f0(); -> String, assuming 0-terminated and copy-semantics

extern void f0();

// ----------------------------------------------------------------------
// [const] void/char * [const] result/parameter types
// ----------------------------------------------------------------------

// non-NULL-returning/accepting functions

extern const void * s010();
extern const char * s012();
extern void * s030();
extern char * s032();
extern const void * const s050();
extern const char * const s052();
extern void * const s070();
extern char * const s072();

extern void s110(const void * p0);
extern void s112(const char * p0);
extern void s130(void * p0);
extern void s132(char * p0);
extern void s150(const void * const p0);
extern void s152(const char * const p0);
extern void s170(void * const p0);
extern void s172(char * const p0);

// NULL-returning/accepting functions

extern const void * s210();
extern const char * s212();
extern void * s230();
extern char * s232();
extern const void * const s250();
extern const char * const s252();
extern void * const s270();
extern char * const s272();

extern void s310(const void * p0);
extern void s312(const char * p0);
extern void s330(void * p0);
extern void s332(char * p0);
extern void s350(const void * const p0);
extern void s352(const char * const p0);
extern void s370(void * const p0);
extern void s372(char * const p0);

// ----------------------------------------------------------------------
// all primitive result/parameter types
// ----------------------------------------------------------------------

extern const bool f11(const bool p0);
extern const char f12(const char p0);
extern const signed char f13(const signed char p0);
extern const unsigned char f14(const unsigned char p0);
extern const signed short f15(const signed short p0);
extern const unsigned short f16(const unsigned short p0);
extern const signed int f17(const signed int p0);
extern const unsigned int f18(const unsigned int p0);
extern const signed long f19(const signed long p0);
extern const unsigned long f20(const unsigned long p0);
extern const signed long long f21(const signed long long p0);
extern const unsigned long long f22(const unsigned long long p0);
extern const float f23(const float p0);
extern const double f24(const double p0);
extern const long double f25(const long double p0);

extern bool f31(bool p0);
extern char f32(char p0);
extern signed char f33(signed char p0);
extern unsigned char f34(unsigned char p0);
extern signed short f35(signed short p0);
extern unsigned short f36(unsigned short p0);
extern signed int f37(signed int p0);
extern unsigned int f38(unsigned int p0);
extern signed long f39(signed long p0);
extern unsigned long f40(unsigned long p0);
extern signed long long f41(signed long long p0);
extern unsigned long long f42(unsigned long long p0);
extern float f43(float p0);
extern double f44(double p0);
extern long double f45(long double p0);

// ----------------------------------------------------------------------
// all fixed-size primitive result/parameter types
// ----------------------------------------------------------------------

extern const bool f011();
extern const char f012();
extern const int8_t f013();
extern const uint8_t f014();
extern const int16_t f015();
extern const uint16_t f016();
extern const int32_t f017();
extern const uint32_t f018();
extern const int64_t f021();
extern const uint64_t f022();
extern const float f023();
extern const double f024();

extern bool f031();
extern char f032();
extern int8_t f033();
extern uint8_t f034();
extern int16_t f035();
extern uint16_t f036();
extern int32_t f037();
extern uint32_t f038();
extern int64_t f041();
extern uint64_t f042();
extern float f043();
extern double f044();

extern void f111(const bool);
extern void f112(const char);
extern void f113(const int8_t);
extern void f114(const uint8_t);
extern void f115(const int16_t);
extern void f116(const uint16_t);
extern void f117(const int32_t);
extern void f118(const uint32_t);
extern void f121(const int64_t);
extern void f122(const uint64_t);
extern void f123(const float);
extern void f124(const double);

extern void f131(bool);
extern void f132(char);
extern void f133(int8_t);
extern void f134(uint8_t);
extern void f135(int16_t);
extern void f136(uint16_t);
extern void f137(int32_t);
extern void f138(uint32_t);
extern void f141(int64_t);
extern void f142(uint64_t);
extern void f143(float);
extern void f144(double);

// ----------------------------------------------------------------------
// references of primitive result/parameter types
// ----------------------------------------------------------------------

extern const bool & f211();
extern const char & f212();
extern const int8_t & f213();
extern const uint8_t & f214();
extern const int16_t & f215();
extern const uint16_t & f216();
extern const int32_t & f217();
extern const uint32_t & f218();
extern const int64_t & f221();
extern const uint64_t & f222();
extern const float & f223();
extern const double & f224();

extern bool & f231();
extern char & f232();
extern int8_t & f233();
extern uint8_t & f234();
extern int16_t & f235();
extern uint16_t & f236();
extern int32_t & f237();
extern uint32_t & f238();
extern int64_t & f241();
extern uint64_t & f242();
extern float & f243();
extern double & f244();

extern void f311(const bool &);
extern void f312(const char &);
extern void f313(const int8_t &);
extern void f314(const uint8_t &);
extern void f315(const int16_t &);
extern void f316(const uint16_t &);
extern void f317(const int32_t &);
extern void f318(const uint32_t &);
extern void f321(const int64_t &);
extern void f322(const uint64_t &);
extern void f323(const float &);
extern void f324(const double &);

extern void f331(bool &);
extern void f332(char &);
extern void f333(int8_t &);
extern void f334(uint8_t &);
extern void f335(int16_t &);
extern void f336(uint16_t &);
extern void f337(int32_t &);
extern void f338(uint32_t &);
extern void f341(int64_t &);
extern void f342(uint64_t &);
extern void f343(float &);
extern void f344(double &);

// ----------------------------------------------------------------------
// pointers to primitive result/parameter types (array size == 1)
// ----------------------------------------------------------------------

extern const bool * f411();
extern const char * f412();
extern const int8_t * f413();
extern const uint8_t * f414();
extern const int16_t * f415();
extern const uint16_t * f416();
extern const int32_t * f417();
extern const uint32_t * f418();
extern const int64_t * f421();
extern const uint64_t * f422();
extern const float * f423();
extern const double * f424();

extern bool * f431();
extern char * f432();
extern int8_t * f433();
extern uint8_t * f434();
extern int16_t * f435();
extern uint16_t * f436();
extern int32_t * f437();
extern uint32_t * f438();
extern int64_t * f441();
extern uint64_t * f442();
extern float * f443();
extern double * f444();

extern const bool * const f451();
extern const char * const f452();
extern const int8_t * const f453();
extern const uint8_t * const f454();
extern const int16_t * const f455();
extern const uint16_t * const f456();
extern const int32_t * const f457();
extern const uint32_t * const f458();
extern const int64_t * const f461();
extern const uint64_t * const f462();
extern const float * const f463();
extern const double * const f464();

extern bool * const f471();
extern char * const f472();
extern int8_t * const f473();
extern uint8_t * const f474();
extern int16_t * const f475();
extern uint16_t * const f476();
extern int32_t * const f477();
extern uint32_t * const f478();
extern int64_t * const f481();
extern uint64_t * const f482();
extern float * const f483();
extern double * const f484();

extern void f511(const bool *);
extern void f512(const char *);
extern void f513(const int8_t *);
extern void f514(const uint8_t *);
extern void f515(const int16_t *);
extern void f516(const uint16_t *);
extern void f517(const int32_t *);
extern void f518(const uint32_t *);
extern void f521(const int64_t *);
extern void f522(const uint64_t *);
extern void f523(const float *);
extern void f524(const double *);

extern void f531(bool *);
extern void f532(char *);
extern void f533(int8_t *);
extern void f534(uint8_t *);
extern void f535(int16_t *);
extern void f536(uint16_t *);
extern void f537(int32_t *);
extern void f538(uint32_t *);
extern void f541(int64_t *);
extern void f542(uint64_t *);
extern void f543(float *);
extern void f544(double *);

extern void f551(const bool * const);
extern void f552(const char * const);
extern void f553(const int8_t * const);
extern void f554(const uint8_t * const);
extern void f555(const int16_t * const);
extern void f556(const uint16_t * const);
extern void f557(const int32_t * const);
extern void f558(const uint32_t * const);
extern void f561(const int64_t * const);
extern void f562(const uint64_t * const);
extern void f563(const float * const);
extern void f564(const double * const);

extern void f571(bool * const);
extern void f572(char * const);
extern void f573(int8_t * const);
extern void f574(uint8_t * const);
extern void f575(int16_t * const);
extern void f576(uint16_t * const);
extern void f577(int32_t * const);
extern void f578(uint32_t * const);
extern void f581(int64_t * const);
extern void f582(uint64_t * const);
extern void f583(float * const);
extern void f584(double * const);

// ----------------------------------------------------------------------
// pointers to primitive result/parameter types (array size == 0)
// ----------------------------------------------------------------------

extern const bool * f611();
extern const char * f612();
extern const int8_t * f613();
extern const uint8_t * f614();
extern const int16_t * f615();
extern const uint16_t * f616();
extern const int32_t * f617();
extern const uint32_t * f618();
extern const int64_t * f621();
extern const uint64_t * f622();
extern const float * f623();
extern const double * f624();

extern bool * f631();
extern char * f632();
extern int8_t * f633();
extern uint8_t * f634();
extern int16_t * f635();
extern uint16_t * f636();
extern int32_t * f637();
extern uint32_t * f638();
extern int64_t * f641();
extern uint64_t * f642();
extern float * f643();
extern double * f644();

extern const bool * const f651();
extern const char * const f652();
extern const int8_t * const f653();
extern const uint8_t * const f654();
extern const int16_t * const f655();
extern const uint16_t * const f656();
extern const int32_t * const f657();
extern const uint32_t * const f658();
extern const int64_t * const f661();
extern const uint64_t * const f662();
extern const float * const f663();
extern const double * const f664();

extern bool * const f671();
extern char * const f672();
extern int8_t * const f673();
extern uint8_t * const f674();
extern int16_t * const f675();
extern uint16_t * const f676();
extern int32_t * const f677();
extern uint32_t * const f678();
extern int64_t * const f681();
extern uint64_t * const f682();
extern float * const f683();
extern double * const f684();

extern void f711(const bool *);
extern void f712(const char *);
extern void f713(const int8_t *);
extern void f714(const uint8_t *);
extern void f715(const int16_t *);
extern void f716(const uint16_t *);
extern void f717(const int32_t *);
extern void f718(const uint32_t *);
extern void f721(const int64_t *);
extern void f722(const uint64_t *);
extern void f723(const float *);
extern void f724(const double *);

extern void f731(bool *);
extern void f732(char *);
extern void f733(int8_t *);
extern void f734(uint8_t *);
extern void f735(int16_t *);
extern void f736(uint16_t *);
extern void f737(int32_t *);
extern void f738(uint32_t *);
extern void f741(int64_t *);
extern void f742(uint64_t *);
extern void f743(float *);
extern void f744(double *);

extern void f751(const bool * const);
extern void f752(const char * const);
extern void f753(const int8_t * const);
extern void f754(const uint8_t * const);
extern void f755(const int16_t * const);
extern void f756(const uint16_t * const);
extern void f757(const int32_t * const);
extern void f758(const uint32_t * const);
extern void f761(const int64_t * const);
extern void f762(const uint64_t * const);
extern void f763(const float * const);
extern void f764(const double * const);

extern void f771(bool * const);
extern void f772(char * const);
extern void f773(int8_t * const);
extern void f774(uint8_t * const);
extern void f775(int16_t * const);
extern void f776(uint16_t * const);
extern void f777(int32_t * const);
extern void f778(uint32_t * const);
extern void f781(int64_t * const);
extern void f782(uint64_t * const);
extern void f783(float * const);
extern void f784(double * const);

// ----------------------------------------------------------------------
// object result/parameter types
// ----------------------------------------------------------------------

struct B0 {
    B0() : d0(21), d0c(-21) {
        TRACE("B0()");
    };

    B0(const B0 & b0) : d0(b0.d0), d0c(b0.d0c) {
        TRACE("B0(const B0 &)");
        ABORT_ERROR("!USE OF COPY CONSTRUCTOR!");
    };

    virtual ~B0() {
    };

    B0 & operator=(const B0 & p) {
        TRACE("B0 & operator=(const B0 &)");
        (void)p;
        ABORT_ERROR("!USE OF ASSIGNMENT OPERATOR!");
        return *this;
    }

    // ----------------------------------------------------------------------

    static int32_t d0s;
    static const int32_t d0sc;
    int32_t d0;
    const int32_t d0c;

    // ----------------------------------------------------------------------

    static int32_t f0s() {
        TRACE("int32_t B0::f0s()");
        return 20;
    };

    int32_t f0n() const {
        TRACE("int32_t B0::f0n()");
        return 21;
    };

    virtual int32_t f0v() const {
        TRACE("int32_t B0::f0v()");
        return 22;
    };

};

struct B1 : public B0 {
    B1() : d0(31), d0c(-31) {
        TRACE("B1()");
    };

    B1(const B1 & b1) : B0(b1), d0(b1.d0), d0c(b1.d0c) {
        TRACE("B1(const B1 &)");
        ABORT_ERROR("!USE OF COPY CONSTRUCTOR!");
    };

    virtual ~B1() {
    };

    B1 & operator=(const B1 & p) {
        TRACE("B1 & operator=(const B1 &)");
        (void)p;
        ABORT_ERROR("!USE OF ASSIGNMENT OPERATOR!");
        return *this;
    }

    // ----------------------------------------------------------------------

    static int32_t d0s;
    static const int32_t d0sc;
    int32_t d0;
    const int32_t d0c;

    // ----------------------------------------------------------------------

    static int32_t f0s() {
        TRACE("int32_t B1::f0s()");
        return 30;
    };

    int32_t f0n() const {
        TRACE("int32_t B1::f0n()");
        return 31;
    };

    virtual int32_t f0v() const {
        TRACE("int32_t B1::f0v()");
        return 32;
    };
};

struct A {
    static A * a;

    A() : d0(11), d0c(-11) {
        TRACE("A()");
    };

    A(int i) : d0(11), d0c(-11) {
        TRACE("A(int)");
        (void)i;
    };

    A(const A & a) : d0(a.d0), d0c(a.d0c) {
        TRACE("A(const A &)");
        ABORT_ERROR("!USE OF COPY CONSTRUCTOR!");
    };

    virtual ~A() {
        TRACE("~A()");
    };

    A & operator=(const A & p) {
        TRACE("A & operator=(const A &)");
        (void)p;
        ABORT_ERROR("!USE OF ASSIGNMENT OPERATOR!");
        return *this;
    }

    // ----------------------------------------------------------------------

    static int32_t d0s;
    static const int32_t d0sc;
    int32_t d0;
    const int32_t d0c;

    // ----------------------------------------------------------------------

    static A * deliver_ptr() {
        TRACE("A * A::deliver_ptr()");
        return a;
    };

    static A * deliver_null_ptr() {
        TRACE("A * A::deliver_null_ptr()");
        return NULL;
    };

    static A & deliver_ref() {
        TRACE("A & A::deliver_ref()");
        return *a;
    };

    static A & deliver_null_ref() {
        TRACE("A & A::deliver_null_ref()");
        return *((A *)0);
    };

    static void take_ptr(A * o) {
        TRACE("void A::take_ptr(A *)");
        if (o != A::a) ABORT_ERROR("void A::take_ptr(A *)");
    };

    static void take_null_ptr(A * o) {
        TRACE("void A::take_null_ptr(A *)");
        if (o != NULL) ABORT_ERROR("void A::take_null_ptr(A *)");
    };

    static void take_ref(A & o) {
        TRACE("void A::take_ref(A &)");
        if (&o != A::a) ABORT_ERROR("void A::take_ref(A &)");
    };

    static void take_null_ref(A & o) {
        TRACE("void A::take_null_ref(A &)");
        if (&o != NULL) ABORT_ERROR("void A::take_null_ref(A &)");
    };

    static void print(A * p0) {
        TRACE("void A::print(A *)");
        // in case of problems with %p
        //printf("    p0 = %lx\n", (unsigned long)p0);
        printf("    p0 = %p\n", (void*)p0);
    };

    // ----------------------------------------------------------------------

    B0 * newB0() const {
        TRACE("B0 A::newB0()");
        return new B0();
    };

    B1 * newB1() const {
        TRACE("B1 A::newB1()");
        return new B1();
    };

    static int32_t f0s() {
        TRACE("int32_t A::f0s()");
        return 10;
    };

    int32_t f0n() const {
        TRACE("int32_t A::f0n()");
        return 11;
    };

    virtual int32_t f0v() const {
        TRACE("int32_t A::f0v()");
        return 12;
    };

    void del(B0 & b) {
        TRACE("void A::del(B0 &)");
        delete &b;
    };

    void del(B1 & b) {
        TRACE("void A::del(B1 &)");
        delete &b;
    };

    // ----------------------------------------------------------------------
    // varying number of result/parameters
    // ----------------------------------------------------------------------

    void g0c() const {
        TRACE("void A::g0c()");
    };

    void g1c(int8_t p0) const {
        TRACE("void A::g1c(int8_t)");
        if (p0 != 1) ABORT_ERROR("wrong arg value");
    };

    void g2c(int8_t p0, int16_t p1) const {
        TRACE("void A::g2c(int8_t, int16_t)");
        if (p0 != 1) ABORT_ERROR("wrong arg value");
        if (p1 != 2) ABORT_ERROR("wrong arg value");
    };

    void g3c(int8_t p0, int16_t p1, int32_t p2) const {
        TRACE("void A::g3c(int8_t, int16_t, int32_t)");
        if (p0 != 1) ABORT_ERROR("wrong arg value");
        if (p1 != 2) ABORT_ERROR("wrong arg value");
        if (p2 != 3) ABORT_ERROR("wrong arg value");
    };

    void g0() {
        TRACE("void A::g0()");
    };

    void g1(int8_t p0) {
        TRACE("void A::g1(int8_t)");
        if (p0 != 1) ABORT_ERROR("wrong arg value");
    };

    void g2(int8_t p0, int16_t p1) {
        TRACE("void A::g2(int8_t, int16_t)");
        if (p0 != 1) ABORT_ERROR("wrong arg value");
        if (p1 != 2) ABORT_ERROR("wrong arg value");
    };

    void g3(int8_t p0, int16_t p1, int32_t p2) {
        TRACE("void A::g3(int8_t, int16_t, int32_t)");
        if (p0 != 1) ABORT_ERROR("wrong arg value");
        if (p1 != 2) ABORT_ERROR("wrong arg value");
        if (p2 != 3) ABORT_ERROR("wrong arg value");
    };

    // ----------------------------------------------------------------------

    int32_t g0rc() const {
        TRACE("int32_t A::g0rc()");
        return 0;
    };

    int32_t g1rc(int8_t p0) const {
        TRACE("int32_t A::g1rc(int8_t)");
        if (p0 != 1) ABORT_ERROR("wrong arg value");
        return p0;
    };

    int32_t g2rc(int8_t p0, int16_t p1) const {
        TRACE("int32_t A::g2rc(int8_t, int16_t)");
        if (p0 != 1) ABORT_ERROR("wrong arg value");
        if (p1 != 2) ABORT_ERROR("wrong arg value");
        return p0 + p1;
    };

    int32_t g3rc(int8_t p0, int16_t p1, int32_t p2) const {
        TRACE("int32_t A::g3rc(int8_t, int16_t, int32_t)");
        if (p0 != 1) ABORT_ERROR("wrong arg value");
        if (p1 != 2) ABORT_ERROR("wrong arg value");
        if (p2 != 3) ABORT_ERROR("wrong arg value");
        return p0 + p1 + p2;
    };

    int32_t g0r() {
        TRACE("int32_t A::g0r()");
        return 0;
    };

    int32_t g1r(int8_t p0) {
        TRACE("int32_t A::g1r(int8_t)");
        if (p0 != 1) ABORT_ERROR("wrong arg value");
        return p0;
    };

    int32_t g2r(int8_t p0, int16_t p1) {
        TRACE("int32_t A::g2r(int8_t, int16_t)");
        if (p0 != 1) ABORT_ERROR("wrong arg value");
        if (p1 != 2) ABORT_ERROR("wrong arg value");
        return p0 + p1;
    };

    int32_t g3r(int8_t p0, int16_t p1, int32_t p2) {
        TRACE("int32_t A::g3r(int8_t, int16_t, int32_t)");
        if (p0 != 1) ABORT_ERROR("wrong arg value");
        if (p1 != 2) ABORT_ERROR("wrong arg value");
        if (p2 != 3) ABORT_ERROR("wrong arg value");
        return p0 + p1 + p2;
    };
};

// ----------------------------------------------------------------------

inline void h0() {
    TRACE("void h0()");
}

inline void h1(int8_t p0) {
    TRACE("void h1(int8_t)");
    if (p0 != 1) ABORT_ERROR("wrong arg value");
}

inline void h2(int8_t p0, int16_t p1) {
    TRACE("void h2(int8_t, int16_t)");
    if (p0 != 1) ABORT_ERROR("wrong arg value");
    if (p1 != 2) ABORT_ERROR("wrong arg value");
}

inline void h3(int8_t p0, int16_t p1, int32_t p2) {
    TRACE("void h3(int8_t, int16_t, int32_t)");
    if (p0 != 1) ABORT_ERROR("wrong arg value");
    if (p1 != 2) ABORT_ERROR("wrong arg value");
    if (p2 != 3) ABORT_ERROR("wrong arg value");
}

inline int32_t h0r() {
    TRACE("int32_t h0r()");
    return 0;
}

inline int32_t h1r(int8_t p0) {
    TRACE("int32_t h1r(int8_t)");
    if (p0 != 1) ABORT_ERROR("wrong arg value");
    return p0;
}

inline int32_t h2r(int8_t p0, int16_t p1) {
    TRACE("int32_t h2r(int8_t, int16_t)");
    if (p0 != 1) ABORT_ERROR("wrong arg value");
    if (p1 != 2) ABORT_ERROR("wrong arg value");
    return p0 + p1;
}

inline int32_t h3r(int8_t p0, int16_t p1, int32_t p2) {
    TRACE("int32_t h3r(int8_t, int16_t, int32_t)");
    if (p0 != 1) ABORT_ERROR("wrong arg value");
    if (p1 != 2) ABORT_ERROR("wrong arg value");
    if (p2 != 3) ABORT_ERROR("wrong arg value");
    return  p0 + p1 + p2;
}

// ----------------------------------------------------------------------
// const/non-const member functions and object result/parameter types
// ----------------------------------------------------------------------

struct C0 {
    C0() {
        TRACE("C0()");
    }

    C0(const C0 & o) {
        TRACE("C0(const C0 &)");
        (void)o;
        ABORT_ERROR("!USE OF COPY CONSTRUCTOR!");
    }

    virtual ~C0() {
        TRACE("~C0()");
    }

    C0 & operator=(const C0 & o) {
        TRACE("C0 & operator=(const C0 &)");
        (void)o;
        ABORT_ERROR("!USE OF ASSIGNMENT OPERATOR!");
        return *this;
    }

    // ----------------------------------------------------------------------

    static C0 * const c;
    static const C0 * const cc;
    //printf("    cp = %p, c = %p, cc = %p\n", cp, C0::c, C0::cc);

    void print() const {
        TRACE("void C0::print()");
        printf("    p0 = %p\n", this);
    }

    const C0 * deliver_C0Cp() const {
        TRACE("const C0 * C0::deliver_C0Cp()");
        return cc;
    }

    const C0 & deliver_C0Cr() const {
        TRACE("const C0 & C0::deliver_C0Cr()");
        return *cc;
    }

    void take_C0Cp(const C0 * cp) const {
        TRACE("void C0::take_C0Cp(const C0 *)");
        if (cp != C0::c && cp != C0::cc) ABORT_ERROR("cp != C0::c && cp != C0::cc");
    }

    void take_C0Cr(const C0 & cp) const {
        TRACE("void C0::take_C0Cr(const C0 &)");
        if (&cp != C0::c && &cp != C0::cc) ABORT_ERROR("&cp != C0::c && &cp != C0::cc");
    }

    C0 * deliver_C0p() {
        TRACE("C0 * C0::deliver_C0p()");
        return c;
    }

    C0 & deliver_C0r() {
        TRACE("C0 & C0::deliver_C0r()");
        return *c;
    }

    void take_C0p(C0 * p) {
        TRACE("void C0::take_C0p(C0 *)");
        if (p != C0::c) ABORT_ERROR("p != C0::c");
    }

    void take_C0r(C0 & p) {
        TRACE("void C0::take_C0r(C0 &)");
        if (&p != C0::c) ABORT_ERROR("&p != C0::c");
    }

    // ----------------------------------------------------------------------

    enum C0E { C0E0, C0E1 };

    static C0E deliver_C0E1() {
        TRACE("C0::C0E C0::deliver_C0E1()");
        return C0E1;
    };

    static void take_C0E1(C0E e) {
        TRACE("void C0::take_C0E1(C0::C0E)");
        if (e != C0E1) ABORT_ERROR("e != C0E1");
    };

    static const C0E deliver_C0E1c() {
        TRACE("const C0::C0E C0::deliver_C0E1c()");
        return C0E1;
    };

    static void take_C0E1c(const C0E e) {
        TRACE("void C0::take_C0E1c(const C0::C0E)");
        if (e != C0E1) ABORT_ERROR("e != C0E1");
    };
};

struct C1 : public C0 {
    C1() {
        TRACE("C1()");
    };

    C1(const C1 & o) : C0(o) {
        TRACE("C1(const C1 &)");
        (void)o;
        ABORT_ERROR("!USE OF COPY CONSTRUCTOR!");
    };

    virtual ~C1() {
        TRACE("~C1()");
    };

    C1 & operator=(const C1 & p) {
        TRACE("C1 & operator=(const C1 &)");
        (void)p;
        ABORT_ERROR("!USE OF ASSIGNMENT OPERATOR!");
        return *this;
    }

    // ----------------------------------------------------------------------

    static C1 * const c;
    static const C1 * const cc;
    //printf("    cp = %p, c = %p, cc = %p\n", cp, C1::c, C1::cc);

    const C1 * deliver_C1Cp() const {
        TRACE("const C1 * C1::deliver_C1Cp()");
        return cc;
    };

    const C1 & deliver_C1Cr() const {
        TRACE("const C1 & C1::deliver_C1Cr()");
        return *cc;
    };

    void take_C1Cp(const C1 * cp) const {
        TRACE("void C1::take_C1Cp(const C1 *)");
        if (cp != C1::c && cp != C1::cc) ABORT_ERROR("cp != C1::c && cp != C1::cc");
    };

    void take_C1Cr(const C1 & cp) const {
        TRACE("void C1::take_C1Cr(const C1 &)");
        if (&cp != C1::c && &cp != C1::cc) ABORT_ERROR("&cp != C1::c && &cp != C1::cc");
    };

    C1 * deliver_C1p() {
        TRACE("C1 * C1::deliver_C1p()");
        return c;
    };

    C1 & deliver_C1r() {
        TRACE("C1 & C1::deliver_C1r()");
        return *c;
    };

    void take_C1p(C1 * p) {
        TRACE("void C1::take_C1p(C1 *)");
        if (p != C1::c) ABORT_ERROR("p != C1::c");
    };

    void take_C1r(C1 & p) {
        TRACE("void C1::take_C1r(C1 &)");
        if (&p != C1::c) ABORT_ERROR("&p != C1::c");
    };
};

// ----------------------------------------------------------------------
// overriding and virtual/non-virtual functions
// ----------------------------------------------------------------------

struct D1;

struct D0 {
    int f_d0() { TRACE("D0::f_d0()"); return 20; }
    int f_nv() { TRACE("D0::f_nv()"); return 21; }
    virtual int f_v() { TRACE("D0::f_v()"); return 22; }
    static D1 * sub();
    static D0 d;
    virtual ~D0() {}
};

struct D1 : D0 {
    int f_d1() { TRACE("D0::f_d1()"); return 30; }
    int f_nv() { TRACE("D1::f_nv()"); return 31; }
    virtual int f_v() { TRACE("D1::f_v()"); return 32; }
    static D1 * sub();
    static D1 d;
    virtual ~D1() {}
};

struct D2 : D1 {
    int f_d2() { TRACE("D2::f_d2()"); return 40; }
    int f_nv() { TRACE("D2::f_nv()"); return 41; }
    virtual int f_v() { TRACE("D2::f_v()"); return 42; }
    static D1 * sub();
    static D2 d;
    virtual ~D2() {}
};

// d1class instance returns (casts unnecessary but for attention)
inline D1 * D0::sub() { TRACE("D1 * D0::sub()"); return ((D1*)&D1::d); }
inline D1 * D1::sub() { TRACE("D1 * D1::sub()"); return ((D1*)&D2::d); }
inline D1 * D2::sub() { TRACE("D1 * D2::sub()"); return NULL; }

// ----------------------------------------------------------------------

#endif // _myapi
