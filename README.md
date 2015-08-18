# About

This contains two example of SQLite virtual tables. I wrote this for a magazine
article a long time ago. One example create a virtual table out of the file
system. Another is just a skeleton to use as a blank slate.

The filesystem virtual table gives you a relational interface into your
filesystem. With it you can search for files with SQL like so:

```sql
-- Look for stuff
select
  prot, uid, gid, size/(1024*1024) as 'size (Mb)', 
  dev, path || '/' || name as file 
from fs 
where 
    path match '/usr/lib, /var/log'
    and ((name like '%.so') || (name like '%.log'))
    order by size desc;  
```

The table has the following available columns:

```
  "name  text, "  /* col 0  : name             */
  "path  text, "  /* col 1  : path             */
  "type  int,  "  /* col 2  : type             */
  "size  int,  "  /* col 3  : size             */
  "uid   int,  "  /* col 4  : uid              */
  "gid   int,  "  /* col 5  : gid              */
  "prot  int,  "  /* col 6  : protection bits  */
  "mtime int,  "  /* col 7  : modified time    */
  "ctime int,  "  /* col 8  : create time      */
  "atime int,  "  /* col 9  : access time      */
  "dev   int,  "  /* col 10 : device           */
  "nlink int,  "  /* col 11 : number of links  */
  "inode int   "  /* col 12 : dir inode        */
  "dir   int   "  /* col 13 : dir inode        */
```

# Building

You must have the Apache Portable Runtime and the SQLite libraries installed on
your system and within you search path.

## Linux, MinGW and Unix-like Systems:

```
autogen.sh
./configure
make
```

You only have to run the first two lines once. You may need to use the `--prefix`
switch to point to where your standard /include and /lib files are
(e.g. `--prefix=/usr` will use `/usr/include` and `/usr/lib respectively`. )

The end result will be a shared library called libvtable.so and an executable
named program (from `main.c`). If `libvtable.so` is not in your library search
path, you will need to specify it using `LD_LIBRARY_PATH`, e.g.

```
export LD_LIBRARY_PATH=`pwd`
```

Then you can run the program, or run the `test.sql` script using the `sqlite3`
command line utility as follows:

```
sqlite3 db < test.sql
```

## Windows MS Visual C++

If you want to use the virtual table as a dynamically loadable module, you will
need to create a DLL project that contains the following files:

```
lib.c example.c fs.c common.c
```

Then create a console application that uses main.c. This must link to the SQLite
and Apache Portable Runtime (1.0 or greater).

The example.c file contains a bare-bones working example of a virtual table, so
if you don't have APR for Windows handy and just want to get something to
compile, you can use this as a starting point.
