AC_DEFUN([OPM_F90_COMPILATION_SYSTEM],
[AC_REQUIRE([AC_PROG_FC_C_O])dnl
 AC_REQUIRE([AC_FC_WRAPPERS])dnl
 AC_REQUIRE([AC_FC_LIBRARY_LDFLAGS])dnl
])[]dnl

AC_DEFUN([OPM_AGMG],dnl
[AC_ARG_WITH([agmg],dnl
 [AS_HELP_STRING([--with-agmg=SRCDIR],dnl
  [Include DOUBLE PRECISION version Notay's of AGMG Algebraic
   Multigrid solver from specified source location SRCDIR. Note: this
   option requires a complete, working Fortran 90 environment.])],
 [],dnl
 [with_agmg=no])[]dnl

 AS_IF([test x"$with_agmg" != x"no" -a -d "$with_agmg"],dnl
 [AS_IF([test -f "$with_agmg/dagmg.f90"],dnl
  [AC_SUBST([AGMG_SRCDIR], [$with_agmg])[]dnl
   AC_DEFINE([HAVE_AGMG], [1],dnl
             [Define to `1' if Notay's AGMG solver is included])[]dnl
   build_agmg="yes"],dnl
  [build_agmg="no"])],dnl
 [build_agmg="no"])[]dnl

 AS_IF([test x"$build_agmg" = x"yes"],dnl
   [AC_REQUIRE([OPM_F90_COMPILATION_SYSTEM])], [:])[]dnl

 AM_CONDITIONAL([BUILD_AGMG], [test x"$build_agmg" = x"yes"])
])[]dnl
