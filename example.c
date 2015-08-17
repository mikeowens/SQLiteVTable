#include <stdio.h>

#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1

typedef struct example_vtab example_vtab;
typedef struct example_cursor example_cursor;

/* DDL defining the structure of the vtable */
static const char* ddl = "create table vtable (x integer, y int) ";

struct example_vtab
{
    sqlite3_vtab base;
    sqlite3 *db;
};

struct example_cursor
{
    sqlite3_vtab_cursor base;
    int count;
    int eof;
};

static int vt_destructor(sqlite3_vtab *pVtab)
{
    example_vtab *p = (example_vtab*)pVtab;
    sqlite3_free(p);

    return 0;
}

static int vt_create( sqlite3 *db,
                      void *p_aux,
                      int argc, char **argv,
                      sqlite3_vtab **pp_vt,
                      char **pzErr )
{
    int rc = SQLITE_OK;
    example_vtab* p_vt;

    /* Allocate the sqlite3_vtab/example_vtab structure itself */
    p_vt = (example_vtab*)sqlite3_malloc(sizeof(*p_vt));

    if(p_vt == NULL)
    {
        return SQLITE_NOMEM;
    }
    
    p_vt->db = db;
    
    /* Declare the vtable's structure */
    rc = sqlite3_declare_vtab(db, ddl);

    if(rc != SQLITE_OK)
    {
        vt_destructor((sqlite3_vtab*)p_vt);
        
        return SQLITE_ERROR;
    }

    /* Success. Set *pp_vt and return */
    *pp_vt = &p_vt->base;

    return SQLITE_OK;
}

static int vt_connect( sqlite3 *db, void *p_aux,
                       int argc, char **argv,
                       sqlite3_vtab **pp_vt, char **pzErr )
{
    return vt_create(db, p_aux, argc, argv, pp_vt, pzErr);
}

static int vt_disconnect(sqlite3_vtab *pVtab)
{
    return vt_destructor(pVtab);
}

static int vt_destroy(sqlite3_vtab *pVtab)
{
    int rc = SQLITE_OK;
    example_vtab *p = (example_vtab *)pVtab;

    if(rc == SQLITE_OK)
    {
        rc = vt_destructor(pVtab);
    }

    return rc;
}

static int vt_open(sqlite3_vtab *pVTab, sqlite3_vtab_cursor **pp_cursor)
{
    example_cursor *p_cur = 
        (example_cursor*)sqlite3_malloc(sizeof(example_cursor));
    *pp_cursor = (sqlite3_vtab_cursor*)p_cur;

    return (p_cur ? SQLITE_OK : SQLITE_NOMEM);
}

static int vt_close(sqlite3_vtab_cursor *cur)
{
    example_cursor *p_cur = (example_cursor*)cur;
    sqlite3_free(p_cur);

    return SQLITE_OK;
}

static int vt_eof(sqlite3_vtab_cursor *cur)
{
    return ((example_cursor*)cur)->eof;
}

static int vt_next(sqlite3_vtab_cursor *cur)
{
    example_cursor *p_cur = (example_cursor*)cur;

    /* Increment the current row count. */
    p_cur->count += 1;

    /* Arbitrary contstraint: when we get to 10 rows, then stop. */
    if(p_cur->count == 10)
    {
        p_cur->eof = 1;
    }

    return SQLITE_OK;
}

static int vt_column(sqlite3_vtab_cursor *cur, sqlite3_context *ctx, int i)
{
    /* Just return the ordinal of the column requested. */
    sqlite3_result_int(ctx, i);

    return SQLITE_OK;
}

static int vt_rowid(sqlite3_vtab_cursor *cur, sqlite_int64 *p_rowid)
{
    example_cursor *p_cur = (example_cursor*)cur;

    /* Just use the current row count as the rowid. */
    *p_rowid = p_cur->count;

    return SQLITE_OK;
}

static int vt_filter( sqlite3_vtab_cursor *p_vtc, 
                      int idxNum, const char *idxStr,
                      int argc, sqlite3_value **argv )
{
    int rc;
    int i;

    /* Initialize the cursor structure. */
    example_cursor *p_cur = (example_cursor*)p_vtc;

    /* Zero rows returned thus far. */
    p_cur->count          = 0;

    /* Have not reached end of set. */
    p_cur->eof            = 0;

    /* Move cursor to first row. */
    return vt_next(p_vtc);
}

/* Pretty involved. We don't implement in this example. */
static int vt_best_index(sqlite3_vtab *tab, sqlite3_index_info *pIdxInfo)
{
    return SQLITE_OK;
}

static sqlite3_module example_module = 
{
    0,              /* iVersion */
    vt_create,      /* xCreate       - create a vtable */
    vt_connect,     /* xConnect      - associate a vtable with a connection */
    vt_best_index,  /* xBestIndex    - best index */
    vt_disconnect,  /* xDisconnect   - disassociate a vtable with a connection */
    vt_destroy,     /* xDestroy      - destroy a vtable */
    vt_open,        /* xOpen         - open a cursor */
    vt_close,       /* xClose        - close a cursor */
    vt_filter,      /* xFilter       - configure scan constraints */
    vt_next,        /* xNext         - advance a cursor */
    vt_eof,         /* xEof          - inidicate end of result set*/
    vt_column,      /* xColumn       - read data */
    vt_rowid,       /* xRowid        - read data */
    NULL,           /* xUpdate       - write data */
    NULL,           /* xBegin        - begin transaction */
    NULL,           /* xSync         - sync transaction */
    NULL,           /* xCommit       - commit transaction */
    NULL,           /* xRollback     - rollback transaction */
    NULL,           /* xFindFunction - function overloading */
    NULL,           /* xRename       - function overloading */
    NULL,           /* xSavepoint    - function overloading */
    NULL,           /* xRelease      - function overloading */
    NULL            /* xRollbackto   - function overloading */
};

int example_init( sqlite3* db, 
                  char **pzErrMsg, 
                  const sqlite3_api_routines* pApi )
{
    SQLITE_EXTENSION_INIT2(pApi);

    return sqlite3_create_module(db, "example", &example_module, NULL);
}
