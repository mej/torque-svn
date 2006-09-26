m4_include([buildutils/tcl.m4])
m4_include([buildutils/ac_create_generic_config.m4])


dnl
dnl  Test to see whether h_errno is visible when netdb.h is included.
dnl  At least under HP-UX 10.x this is not the case unless 
dnl  XOPEN_SOURCE_EXTENDED is declared but then other nasty stuff happens.
dnl  The appropriate thing to do is to call this macro and then
dnl  if it is not available do a "extern int h_errno;" in the code.
dnl
dnl  This test was taken from the original OpenPBS buildsystem
dnl
AC_DEFUN(AC_DECL_H_ERRNO,
[AC_CACHE_CHECK([for h_errno declaration in netdb.h],
  ac_cv_decl_h_errno,
[AC_TRY_COMPILE([#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <netdb.h>
], [int _ZzQ = (int)(h_errno + 1);],
  ac_cv_decl_h_errno=yes, ac_cv_decl_h_errno=no)])
if test $ac_cv_decl_h_errno = yes; then
  AC_DEFINE(H_ERRNO_DECLARED, 1,
            [is the global int h_errno declared in netdb.h])
fi
])dnl


dnl
dnl  On some systems one needs to include sys/select.h to get the
dnl  definition of FD_* macros for select bitmask handling.
dnl 
dnl  This test was taken from the original OpenPBS buildsystem
dnl
AC_DEFUN(AC_DECL_FD_SET_SYS_SELECT_H,
[AC_CACHE_CHECK([for FD_SET declaration in sys/select.h],
  ac_cv_decl_fdset_sys_select_h,
[AC_EGREP_CPP(oh_yeah, [#include <sys/select.h> 
#ifdef FD_SETSIZE
oh_yeah
#endif
], ac_cv_decl_fdset_sys_select_h=yes, ac_cv_decl_fdset_sys_select_h=no)])
if test $ac_cv_decl_fdset_sys_select_h = yes; then
  AC_DEFINE(FD_SET_IN_SYS_SELECT_H, 1,
            [are FD_SET and friends defined in sys/select.h])
fi
])dnl


dnl
dnl  Old compilers do not support C9X style __func__ identifiers so
dnl  check if this one is able to.
dnl
dnl  This test was first written by Christopher Currie.
dnl
AC_DEFUN([AC_C_VAR_FUNC],
[AC_REQUIRE([AC_PROG_CC])
AC_CACHE_CHECK(whether $CC recognizes __func__, ac_cv_c_var_func,
AC_TRY_COMPILE(,[const char *s = __func__;],
AC_DEFINE(HAVE_FUNC, 1,
[Define if the C complier supports __func__]) ac_cv_c_var_func=yes,
ac_cv_c_var_func=no) )
])dnl


dnl
dnl  Some compilers do not support GNU style __FUNCTION__ identifiers so
dnl  check if this one is able to.
dnl
dnl  This test is just copy'n'paste of the above one.
dnl
AC_DEFUN([AC_C_VAR_FUNCTION],
[AC_REQUIRE([AC_PROG_CC])
AC_CACHE_CHECK(whether $CC recognizes __FUNCTION__, ac_cv_c_var_function,
AC_TRY_COMPILE(,[const char *s = __FUNCTION__;],
AC_DEFINE(HAVE_FUNCTION, 1,
[Define if the C complier supports __FUNCTION__]) ac_cv_c_var_function=yes,
ac_cv_c_var_function=no) )
])dnl


dnl
dnl largefile support
dnl
dnl We can't just use AC_SYS_LARGEFILE because that breaks kernel ABIs on Solaris.
dnl Instead, we just figure out if we have stat64() and stat64.st_mode.
AC_DEFUN([TAC_SYS_LARGEFILE],[
orig_CFLAGS="$CFLAGS"
AC_CHECK_FUNC(stat64,
  AC_DEFINE(HAVE_STAT64,1,[Define if stat64() is available]),
  [CFLAGS="$CFLAGS -D_LARGEFILE64_SOURCE"
   unset ac_cv_func_stat64
   AC_CHECK_FUNC(stat64,
    AC_DEFINE(HAVE_STAT64,1,[Define if stat64() is available]),
     [CFLAGS="$orig_CFLAGS"])])

AC_CHECK_MEMBER(struct stat64.st_mode,
  AC_DEFINE(HAVE_STRUCT_STAT64,1,[Define if struct stat64 is available]),
  CFLAGS="$CFLAGS -D_LARGEFILE64_SOURCE"
  unset ac_cv_member_struct_stat64_st_mode
  AC_CHECK_MEMBER(struct stat64.st_mode,
    AC_DEFINE(HAVE_STRUCT_STAT64,1,[Define if struct stat64 is available]),
                          [CFLAGS="$orig_CFLAGS"],
[#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>]),
[#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>])
])

