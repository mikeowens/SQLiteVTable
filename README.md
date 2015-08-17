= About

This contains two example of SQLite virtual tables. I wrote this for a magazine
article a long time ago. One example create a virtual table out of the file
system. Another is just a skeleton to use as a blank slate.

= Building

You must have the Apache Portable Runtime and the SQLite libraries installed on
your system and within you search path.

== Linux, MinGW and Unix-like Systems:

  autogen.sh
  ./configure
  make

You only have to run the first two lines once. You may need to use the --prefix
switch to point to where your standard /include and /lib files are
(e.g. --prefix=/usr will use /usr/include and /usr/lib respectively. )

The end result will be a shared library called libvtable.so and an executable
named program (from main.c). If libvtable.so is not in your library search path,
you will need to specify it using LD_LIBRARY_PATH, e.g.

  export LD_LIBRARY_PATH=`pwd`

Then you can run the program, or run the test.sql script using the sqlite3
command line utility as follows:

sqlite3 < test.sql

== Windows MS Visual C++

If you want to use the virtual table as a dynamically loadable module, you will
need to create a DLL project that contains the following files:

  lib.c example.c fs.c common.c

Then create a console application that uses main.c. This must link to the SQLite
and Apache Portable Runtime (1.0 or greater).

The example.c file contains a bare-bones working example of a virtual table, so
if you don't have APR for Windows handy and just want to get something to
compile, you can use this as a starting point.


