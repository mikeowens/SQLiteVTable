#include <stdio.h>
#include <string.h>

#include "sqlite3ext.h"
//SQLITE_EXTENSION_INIT1

#include "example.h"
#include "fs.h"

int lib_init( sqlite3* db, 
              char **pzErrMsg, 
              const sqlite3_api_routines* pApi )
{
    SQLITE_EXTENSION_INIT2(pApi);

    if(example_register(db) != SQLITE_OK)
    {
        fprintf(stderr, "Failed to register example module\n");

        return SQLITE_ERROR;
    }

    /*
    if(fs_register(db) != SQLITE_OK)
    {
        fprintf(stderr, "Failed to register fs module\n");

        return SQLITE_ERROR;
    }
    */

    return SQLITE_OK;
}
