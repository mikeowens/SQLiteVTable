#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apr-1/apr_file_info.h>
#include <sqlite3.h>

int main(int argc, char **argv)
{
    int rc, i, ncols; char *sql;
    sqlite3 *db;
    sqlite3_stmt *stmt;
    char buffer[256];
    const char *tail;

    apr_initialize();

    /* Arrange to have it cleaned up at exit. */
    atexit(apr_terminate);

    //apr_pool_t* pool = NULL;
    //apr_pool_create_ex(&pool, NULL, NULL, NULL);

    /* Open an in-memory database */
    if(sqlite3_open(":memory:", &db) != SQLITE_OK) { 
        exit(1); 
    }

    /* Enable loadable modules (DSOs) */
    sqlite3_enable_load_extension(db, 1);

    /* Load the fs virtual table. */
    char* msg;
    const char* lib = "/home/owensmk/data/code/projects/active/vaquero/src/test/vtable/libvtable";
    rc = sqlite3_load_extension(db, lib, "fs_register", &msg);

    if(rc != SQLITE_OK) { 
        printf("ERROR: %s\n", msg);
        exit(1); 
    }

    /* Create it. */
    rc = sqlite3_exec(db, "create virtual table f using filesystem", NULL, NULL, &msg);

    if(rc != SQLITE_OK) { 
        printf("ERROR: %s\n", msg);
        exit(1); 
    }

    /* Query it. */
    /*
    sql = "select inode,name,path,size,prot,uid,gid from f "
          "where path match '/var/log, /usr/lib, /usr/local, /var/lib' "
          "and name like '%.so'";
    */

    sql = "select inode,name,path,size,prot,uid,gid from f "
          "where path match '/var/log, /usr/lib, /usr/local, /var/lib'";

    rc = sqlite3_prepare(db, sql, (int)strlen(sql), &stmt, &tail);    

    if(rc != SQLITE_OK) { 
        printf("ERROR: %s\n", msg);
        exit(1); 
    }

    /* Iterate over results */
    ncols = sqlite3_column_count(stmt); 
    rc = sqlite3_step(stmt);

    /* Print column information 
    for(i=0; i < ncols; i++) {
        fprintf(stdout, "Column: name=%s, storage class=%i, declared=%s\n", 
                         sqlite3_column_name(stmt, i),
                         sqlite3_column_type(stmt, i),
                         sqlite3_column_decltype(stmt, i));
    }

    fprintf(stdout, "\n");
    */

    while(rc == SQLITE_ROW) 
    {
        int inode        = sqlite3_column_int(stmt,  0);
        const char* name = sqlite3_column_text(stmt, 1);
        const char* path = sqlite3_column_text(stmt, 2);
        int size         = sqlite3_column_int(stmt,  3);
        int prot         = sqlite3_column_int(stmt,  4);
        int uid          = sqlite3_column_int(stmt,  6);
        int gid          = sqlite3_column_int(stmt,  7);

        if(name != NULL) {
            fprintf( stderr, 
                     "%6i %-35s %-45s %-9i %5X %-5i %-5i\n", 
                     inode,name,path,size,prot,uid,gid );
        } 

        rc = sqlite3_step(stmt);
    }


    sqlite3_finalize(stmt);

    sqlite3_close(db);

    return 0;    
}
