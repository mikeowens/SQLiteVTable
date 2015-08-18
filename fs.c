#include <stdio.h>
#include <stdlib.h>

/* Apache Portable Runtime file info.*/
#include <apr-1.0/apr_file_io.h>

#include <sqlite3ext.h>
SQLITE_EXTENSION_INIT3

/** This file implements a SQLite virtual table that can read a file
 *  system. That is, the file system looks like a single table in SQLite. It
 *  uses the Apache Portable Runtime to interface with file system and/or OS.
 */

typedef struct vtab vtab;
typedef struct vtab_cursor vtab_cursor;
typedef struct filenode filenode;

/* Utility functions. */
static void deallocate_filenode(struct filenode* p);
static void deallocate_dirpath(vtab_cursor *p_cur);
static int next_directory(vtab_cursor *p_cur);
static struct filenode* move_up_directory(vtab_cursor *p_cur);
static int next_directory(vtab_cursor *p_cur);
static const char* file_type_name(int type);

/* DDL defining the structure of the virtual table. */
static const char* ddl = "create table fs ("
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
")";

/* TODO

1. Make constructor with recursive flag -- set to zero to create an ls
   implementation. recursive will be find.

create virtual table find as using filesystem('recursive=true');
create virtual table ls as using filesystem('recursive=false');

2. Add hidden field : prune or exclude. Then you can specify paths to ignore

3. Handle inode index case.
*/

/* vtab: represents a virtual table. */
struct vtab
{
    sqlite3_vtab base;
    sqlite3 *db;
    apr_pool_t* pool;
};

/** filenode: represents a single file entry. It contains the APR machinery to
 *  point to a file entry (dirent), its encompassing directory (dir), and that
 *  directory's parent (parent). The full path of the directory is given by
 *  path.
 */
struct filenode
{
    struct filenode* parent;
    apr_finfo_t dirent;
    apr_dir_t *dir;    
    char *path;
};

/** vtab_cursor: represents a cursor used to iterate over a result set.
 *
 * The data structure arrangment is as follows: For any given SQL query on this
 * virtual table, we iterate over each value in the the search_paths
 * string. search_paths is a comma-delimited string of top-level directories to
 * search through.

 * root_path points to the current directory in search_paths that is being
 * searched. root_node is a filenode structure containing the APR stuff we need
 * to search root_path. root_node is potentially a linked list of subnodes --
 * one node for each child directory we descend into. As we descend into the
 * root_path, we add one filenode child for each directory level we descend into
 * (see diagram below).
 *
 * current_node points to the current directory we are searching (bottom of
 * filenode list).
 *
 * The search logic is simple: we start at the top directory (root_path) and
 * search entry by entry. When we find an entry that is a directory, we descend
 * into it and search it (add a child filenode, point current_node to it, and
 * keep searching). When we have searched through the directory, we pop the
 * filenode from the bottom of the list, point current_node to its parent, and
 * resume searching. We keep going until we have searched through all of
 * root_node. Then we update root_node to the next top-level directory in
 * search_path, and start over. We repeat for all directories in search_paths.
 *
 * For example, say we were handling the query:
 *
 *               SELECT * FROM filesystem 
 *               WHERE path match("/usr,/home,/var");
 *
 * Diagrammatically, the state of our cursor data structure when currently at
 * /usr/lib/firefox/icons/mozicon16.xpm would be as follows:
 *
 * "/usr,/home,/var" <== search_paths
 *    |
 *  root_path 
 *    |
 *  root_node --> filenode1<--+         -->     +- /usr
 *                            |                 |
 *                       filenode2<--+     -->   +--+- /lib
 *                                   |              |
 *                              filenode3<--+  -->   +--+- /firefox
 *                                          |           |
 *                    current_node --> filenode4  -->   +--+- /icons
 *                         |                               |
 *                         +-- dirent               -->    +-- mozicon16.xpm
 */

struct vtab_cursor
{
    sqlite3_vtab_cursor base;
    apr_pool_t* pool;
    apr_pool_t* tmp_pool;
    apr_status_t status;

    /* String containing directories to search. This contains multiple values if
     * SQL uses match operator on patch column -- SELECT * FROM fs WHERE path
     * match '/home, /tmp'; Otherwise, this value is by default the root file
     * system (/).
     */
    const char* search_paths; 

    /* Points to the current path being searched in search_paths */
    const char* root_path;

    /* This filenode corresponds to the root_path*/
    struct filenode* root_node;

    /* This filenode corresponds to the child dir in root_path currently being
     * searched.
     */
    struct filenode* current_node;

    /* Number of rows searched. */
    int count;

    /* Whether we have reached the end of the result set. */
    int eof;
};

/*-------------------------------------------------------------------*/
/* Virtual table functions                                           */
/*-------------------------------------------------------------------*/

static int vt_destructor(sqlite3_vtab *p_svt);

static int vt_create( sqlite3 *db,
                      void *pAux,
                      int argc, const char *const*argv,
                      sqlite3_vtab **pp_vt,
                      char **pzErr )
{
    int rc = SQLITE_OK;
    vtab* p_vt;

    /* Allocate the sqlite3_vtab/vtab structure itself */
    p_vt = (vtab*)sqlite3_malloc(sizeof(*p_vt));

    if(p_vt == NULL)
    {
        return SQLITE_NOMEM;
    }
    
    p_vt->db = db;
    
    apr_pool_create(&p_vt->pool, NULL);

    /* Declare the vtable's structure */
    rc = sqlite3_declare_vtab(db, ddl);

    /* Success. Set *pp_vt and return */
    *pp_vt = &p_vt->base;

    return SQLITE_OK;
}

static int vt_destructor(sqlite3_vtab *p_svt)
{
    vtab *p_vt = (vtab*)p_svt;

    /* Free the APR pool */
    apr_pool_destroy(p_vt->pool);

    /* Free the SQLite structure */
    sqlite3_free(p_vt);

    return SQLITE_OK;
}

static int vt_connect( sqlite3 *db, void *p_aux,
                       int argc, const char *const*argv,
                       sqlite3_vtab **pp_vt, char **pzErr )
{
    return vt_create(db, p_aux, argc, argv, pp_vt, pzErr);
}

static int vt_disconnect(sqlite3_vtab *pVtab)
{
    return vt_destructor(pVtab);
}

static int vt_destroy(sqlite3_vtab *p_vt)
{
    return vt_destructor(p_vt);
}

static int vt_open(sqlite3_vtab *p_svt, sqlite3_vtab_cursor **pp_cursor)
{
    vtab* p_vt         = (vtab*)p_svt;
    p_vt->base.zErrMsg = NULL;
    vtab_cursor *p_cur = (vtab_cursor*)sqlite3_malloc(sizeof(vtab_cursor));

    /* Allocate pools */
    apr_pool_create_ex(&p_cur->pool, p_vt->pool, NULL, NULL);
    apr_pool_create_ex(&p_cur->tmp_pool, p_vt->pool, NULL, NULL);

    /* Initialize the root node */

    p_cur->root_node         = malloc(sizeof(struct filenode));
    p_cur->root_node->parent = NULL;
    p_cur->root_node->path   = NULL;
    p_cur->current_node      = p_cur->root_node;
    p_cur->search_paths  = NULL;
    p_cur->root_path = 0;

    *pp_cursor = (sqlite3_vtab_cursor*)p_cur;

    return (p_cur ? SQLITE_OK : SQLITE_NOMEM);
}

static int vt_close(sqlite3_vtab_cursor *cur)
{
    vtab_cursor *p_cur = (vtab_cursor*)cur;

    /* Free all filenodes, if any exist. */
    deallocate_dirpath(p_cur);

    /* Free the APR pools */
    apr_pool_destroy(p_cur->pool);
    apr_pool_destroy(p_cur->tmp_pool);    

    /* Free path match term */
    if(p_cur->search_paths != NULL)
    {
        free((void*)p_cur->search_paths);
        p_cur->search_paths = NULL;
    }

    /* Free cursor struct. */
    sqlite3_free(p_cur);

    return SQLITE_OK;
}

static int vt_eof(sqlite3_vtab_cursor *cur)
{
    return ((vtab_cursor*)cur)->eof;
}

static int vt_next(sqlite3_vtab_cursor *cur)
{
    vtab_cursor *p_cur = (vtab_cursor*)cur;
    vtab* p_vt         = (vtab*)cur->pVtab;

    /** This is a rather involved function. It is the core of this virtual
     *  table. This function recursively reads down into a directory. It
     *  automatically decends into a directory when it finds one, and
     *  automatically ascends out of it when it has read all of its entries.

     *  The logic is as follows:

     *  The p_cur->current_node (a filenode struct) points to the current file
     *  entry (given by the APR handle p_cur->current_node->dirent) and its
     *  associated directory (given by the APR handle
     *  p_cur->current_node->dirent)

     *  We attempt to read the next entry in p_cur->current_node->dir, filling
     *  the p_cur->current_node->dirent. If we succeed, then we have either a
     *  file or a directory. If we have a directory, then we descend into it. If
     *  we have a file, we proceed as usual. In either case, the dirent entry
     *  will consitute a row in the result set, as we have done as much as we
     *  have to and can exit the function. Our only job is to get to the next
     *  valid file or directory entry to return as the current row in the
     *  rowset.

     *  If there are no more entries in the current directory, then we
     *  deallocate p_cur->current_node and proceed up one directory (given by
     *  p_cur->current_node->parent). We thus set p_cur->current_node to
     *  p_cur->current_node->parent, and start over again.
     */

read_next_entry:

    /** First, check for a special case where the top level directory is
     *  actually a top-level file. In this case, we rely that
     *  p_cur->current_node->dir == NULL (set by next_directory()). If this is
     *  true, resort to next_directory().
     */

    if(p_cur->current_node->dir == NULL)
    {
        return next_directory(p_cur);
    }

    /* Read the next directory entry. */

    /* Increment the current row count. */
    p_cur->count += 1;

    struct filenode* d = p_cur->current_node;
    struct filenode* prev_d = d;

reread_next_entry:

    /* Read the next entry in the directory (d->dir). Fills the d->dirent member. */
    if(apr_dir_read( &d->dirent, 
                     APR_FINFO_DIRENT|APR_FINFO_PROT|APR_FINFO_TYPE|
                     APR_FINFO_NAME|APR_FINFO_SIZE, 
                     d->dir) != APR_SUCCESS )
    {
        /** If we get here, the call failed. There are no more entries in
         *  directory. 
         */

        /** If we are at the top level directory */
        if(d->parent == NULL)
        {
            /** We are done with this directory. See if there is another
             *  top-level directory to search.
             *
             *  If there is not another directory, next_directory() will have set
             *  eof=1. We are at the end of the result set. If there is another
             *  directory to search, then it will load the next dirent for
             *  us. Either way, we have nothing left to do here.
             */
            return next_directory(p_cur);
        }
        else
        {
            /** There is a parent directory that we still have to search
             *  through. Free this filenode and resume iterating through the
             *  parent.
             */ 
            d = move_up_directory(p_cur);

            /* Start over, reading the next entry from parent. */
            goto read_next_entry;
        }        
    }

    /* If the current dirent is a directory, then descend into it. */
    if(d->dirent.filetype == APR_DIR)
    {     
        /* Skip . and .. entries */
        if(d->dirent.name != NULL)
        {
            if(strcmp(d->dirent.name, ".") == 0 || strcmp(d->dirent.name, "..") == 0)
            {
                goto read_next_entry;
            }
        }

        /* Create a new child directory node */

        /* Combine the path and file names to get full path */

        char path[1024];
        sprintf(&path[0], "%s/%s", d->path, d->dirent.name);

        /* Allocate space for new filenode and initlialize members. */
        d              = malloc(sizeof(struct filenode));
        d->path        = strdup(path);
        d->parent      = p_cur->current_node;

        /* See note ZERO-FILL DIRENT below. */
        memset(&d->dirent, 0, sizeof(apr_finfo_t));

        /* Set current pointer to it. */
        p_cur->current_node = d;

        /* Clear the pool memory associated with the path string allocated above. */
        apr_pool_clear(p_cur->tmp_pool);
        
        /* Open the directory */
        if((p_cur->status = apr_dir_open(&d->dir, d->path, p_cur->pool)) != APR_SUCCESS)
        {
            /* Problem. Couldn't open directory. */

            fprintf( stderr, "Failed to open directory: %s\n", 
                     p_cur->current_node->path );

            /* Set this to null to move_up_directory() doesn't try to
             * apr_close() it (->core dump) */
            p_cur->current_node->dir = NULL;

            /* Skip to next entry */
            deallocate_filenode(d);
            p_cur->current_node = d = prev_d;
            goto reread_next_entry;
        }

        /* Else we were able to open directory. Update the currnet dirent info
        ** to that of the opened directory. This is our next row in the result
        ** set.
        */
        apr_stat( &d->dirent, d->path, 
                  APR_FINFO_DIRENT|APR_FINFO_TYPE|APR_FINFO_NAME, 
                  p_cur->pool );
    }

    return SQLITE_OK;
}

static int vt_column(sqlite3_vtab_cursor *cur, sqlite3_context *ctx, int col)
{
    vtab_cursor *p_cur = (vtab_cursor*)cur;
    struct filenode* d = p_cur->current_node;

    /* Just return the ordinal of the column requested. */
    switch(col)
    {
        /* col 0: file name */
        case 0:
        {
            /* Will be present if entry is a file */
            if(d->dirent.name != NULL)
            {
                sqlite3_result_text( ctx, 
                                     d->dirent.name,
                                     strlen(d->dirent.name),
                                     SQLITE_STATIC );

                break;
            }

            /* Will be present if entry is a directory */
            if(d->dirent.fname != NULL)
            {
                sqlite3_result_text( ctx, 
                                     d->dirent.fname,
                                     strlen(d->dirent.fname),
                                     SQLITE_STATIC );

                break;
            }

            /* Shouldn't happen: no value. */
            sqlite3_result_text(ctx, "", 0, SQLITE_STATIC);

            break;
        }

        /* col 1: file path */
        case 1:
        {
            if(d->path != NULL)
            {
                int  len = 0;
                char path[PATH_MAX];

                /* If this entry is a top-level file */
                if(d->dir == NULL)
                {
                    /** Then the full path is the path of the file name. Get
                     *  length of path up to the filename. The -1 strips
                     *  trailing separator
                     */
                    len = (int)apr_filepath_name_get(d->path) - (int)d->path - 1;
                }
                else
                {
                    /* Copy the entire path as it is the full directory */
                    len = strlen(d->path);
                }

                strncpy(path, d->path, len);
                path[len] = '\0';

                sqlite3_result_text( ctx, 
                                     path,
                                     strlen(path),
                                     SQLITE_STATIC );
            }
            else
            {
                /* No value. */
                sqlite3_result_text(ctx, "", 0, SQLITE_STATIC);
            }

            break;
        }

        /* col 2: file type */
        case 2:
        {
            sqlite3_result_int(ctx, d->dirent.filetype);

            break;
        }

        /* col 3: file size */
        case 3:
        {
            sqlite3_result_int(ctx, d->dirent.size);

            break;
        }

        /* col 4: uid */
        case 4:
        {
            sqlite3_result_int(ctx, d->dirent.user);

            break;
        }

        /* col 5: gid */
        case 5:
        {
            sqlite3_result_int(ctx, d->dirent.group);

            break;
        }

        /* col 6: protection bits */
        case 6:
        {
            sqlite3_result_int(ctx, d->dirent.protection);

            break;
        }

        /* col 7: modified time */
        case 7:
        {
            sqlite3_result_int64(ctx, d->dirent.mtime);

            break;
        }

        /* col 8: create time */
        case 8:
        {
            sqlite3_result_int64(ctx, d->dirent.ctime);

            break;
        }

        /* col 9: access time */
        case 9:
        {
            sqlite3_result_int64(ctx, d->dirent.atime);

            break;
        }

        /* col 10: device */
        case 10:
        {
            sqlite3_result_int(ctx, d->dirent.device);

            break;
        }

        /* col 11: number of links */
        case 11:
        {
            sqlite3_result_int(ctx, d->dirent.nlink);

            break;
        }

        /* col 12: inode */
        case 12:
        {
            sqlite3_result_int(ctx, d->dirent.inode);

            break;
        }

        /* col 13: dir inode */
        case 13:
        {
            if(p_cur->current_node->parent != NULL)
            {
                sqlite3_result_int(ctx, p_cur->current_node->parent->dirent.inode);
            }
            else
            {
                sqlite3_result_int(ctx, 0);
            }

            break;
        }

        default:
        {
            sqlite3_result_text(ctx, "", 0, SQLITE_STATIC);
        }
    }

    return SQLITE_OK;
}

static int vt_rowid(sqlite3_vtab_cursor *cur, sqlite_int64 *p_rowid)
{
    vtab_cursor *p_cur = (vtab_cursor*)cur;
    struct filenode* d = p_cur->current_node;

    /* Use the inode as the rowid. */
    *p_rowid = d->dirent.inode;

    return SQLITE_OK;
}

static int vt_filter( sqlite3_vtab_cursor *p_vtc, 
                      int idxNum, const char *idxStr,
                      int argc, sqlite3_value **argv )
{
    /* Initialize the cursor structure. */
    vtab_cursor *p_cur = (vtab_cursor*)p_vtc;
    vtab *p_vt         = (vtab*)p_vtc->pVtab;

    if(argc > 0)
    {
        p_cur->search_paths = strdup(sqlite3_value_text(argv[0]));
    }
    else
    {
        /* Start search at root file system. */
        p_cur->search_paths = strdup("/");
    }

    /* p_cur->search_paths is a comma-delimited list of directories to
    ** search. p_cur->root_path keeps track of the current directory we are
    ** searching. We move to the next in the list using next_directory(), which
    ** searces the root_path string for the next directory. 
    */

    /* Place root_path to beginning of search path string. */
    p_cur->root_path = p_cur->search_paths;

    /* Zero rows returned thus far. */
    p_cur->count = 0;

    /* Have not reached end of set. */
    p_cur->eof = 0;

    /* Load first directory to search. */
    return next_directory(p_cur);


    return SQLITE_OK;
}

/** Returns the index of the constraint that matches the input specifications, if
 * any. Returns -1 otherwise. 
 *
 * Input specification:

 * col: index of column (as defined in virtual table schema).
 * opmask: a bitmask of constraint operations.

 * if there is an input constraint for column col which uses any of the
 * constraint operations in opmaks, then the function will return the index of
 * the input constraint, or -1 otherwise.
 */
int has_constraint(sqlite3_index_info *p_info, int col, int opmask)
{
    int i;
    for(i = 0; i < p_info->nConstraint; i++)
    {
        if(p_info->aConstraint[i].iColumn == col)
        {
            /* If specific operations are specified */
            if(opmask != 0)
            {
                /* Check that at least one is satisfied */
                if(p_info->aConstraint[i].op & opmask)
                {
                    /*
                    printf( "col=%i op=%i\n", 
                            p_info->aConstraint[i].iColumn, 
                            p_info->aConstraint[i].op );
                    */

                    return i;
                }
            }

            return i;
        }
    }
    
    return -1;
}

static int vt_best_index(sqlite3_vtab *tab, sqlite3_index_info *p_info)
{
    /** Here we specify what index constraints we want to handle. That is, there
     *  might be some columns with particular constraints in which we can help 
     *  SQLite narrow down the result set.
     
     *  For example, take the "path match(x,y,z)" where x, y, and z are
     *  directories. In this case, we can narrow our search to just these
     *  directories instead of the entire file system. This can be a significant
     *  optimization. So, we want to handle that constraint. To do so, we would
     *  look for two specific input conditions:

     *    1. p_info->aConstraint[i].iColumn == 1
     *    2. p_info->aConstraint[i].op == SQLITE_INDEX_CONSTRAINT_MATCH 
      
     *  The first states that the path column is being used in one of the input
     *  constraints and the second states that the constraint involves the match
     *  operator.

     *  An even more specific search would be for name='xxx', in which case we
     *  can limit the search to a single file, it it exists.

     *  What we have to do here is look for all of our index searches and select
     *  the narrowest. We can only pick one, so obviously we want the one that
     *  is the most specific, which leads to the smallest result set.

     #define SQLITE_INDEX_CONSTRAINT_EQ    2
     #define SQLITE_INDEX_CONSTRAINT_GT    4
     #define SQLITE_INDEX_CONSTRAINT_LE    8
     #define SQLITE_INDEX_CONSTRAINT_LT    16
     #define SQLITE_INDEX_CONSTRAINT_GE    32
     #define SQLITE_INDEX_CONSTRAINT_MATCH 64
     */

    int i = 0;
    int ops = SQLITE_INDEX_CONSTRAINT_MATCH | SQLITE_INDEX_CONSTRAINT_EQ;

    /** If their is a name (column 0) constraint in the WHERE clause 
     *  and it uses the match or equals operator
     */
    if((i = has_constraint(p_info, 0, ops)) > -1)
    {
        /* Then we want the value to be passed to xFilter() */
        p_info->aConstraintUsage[i].argvIndex = 1;
    }

    /** If their is a path constraint in the WHERE clause (column 1 is
     *  specified) and it uses the match operator
     */
    if((i = has_constraint(p_info, 1, ops)) > -1)
    {
        /* Then we want the value to be passed to xFilter() */
        p_info->aConstraintUsage[i].argvIndex = 1;
    }

    return SQLITE_OK;
}

void vt_match_function(sqlite3_context* ctx, int argc, sqlite3_value** argv)
{
    /* Debugging:
    printf("Match function: %i\n", argc);

    int i;
    for(i = 0; i < argc; i++)
    {
        printf("  arg=%s\n", sqlite3_value_text(argv[i]));
    }
    */

    /* By our implementation, everything matches. The only time match() should
     * be called on this table is in conjunction with the path column. When that
     * happens, we catch the value in xFilter() (if/when they are run through
     * xBestIndex()), which then collects all of the values and constructs the
     * directory list to search for this query (p_cursor->search_paths).

     * If match is applied to any other columns in this table, then it will
     * always succeed, no matter what.
     */

    sqlite3_result_int(ctx, 1);
}

int vt_find_function( sqlite3_vtab *pVtab,
                      int nArg,
                      const char *zName,
                      void (**pxFunc)(sqlite3_context*,int,sqlite3_value**),
                      void **ppArg )
{
    /* Register the match function */
    if(strcmp(zName, "match") == 0)
    {
        *pxFunc = vt_match_function;

        return 1;
    }

    return SQLITE_OK;
}

/* Structure to map virtual table functions to sqlite core. */
static sqlite3_module fs_module = 
{
    0,                /* iVersion */
    vt_create,        /* xCreate       - create a vtable */
    vt_connect,       /* xConnect      - associate a vtable with a connection */
    vt_best_index,    /* xBestIndex    - best index */
    vt_disconnect,    /* xDisconnect   - disassociate a vtable with a connection */
    vt_destroy,       /* xDestroy      - destroy a vtable */
    vt_open,          /* xOpen         - open a cursor */
    vt_close,         /* xClose        - close a cursor */
    vt_filter,        /* xFilter       - configure scan constraints */
    vt_next,          /* xNext         - advance a cursor */
    vt_eof,           /* xEof          - inidicate end of result set*/
    vt_column,        /* xColumn       - read data */
    vt_rowid,         /* xRowid        - read data */
    NULL,             /* xUpdate       - write data */
    NULL,             /* xBegin        - begin transaction */
    NULL,             /* xSync         - sync transaction */
    NULL,             /* xCommit       - commit transaction */
    NULL,             /* xRollback     - rollback transaction */
    vt_find_function, /* xFindFunction - function overloading */
    NULL,             /* xRename       - function overloading */
    NULL,             /* xSavepoint    - function overloading */
    NULL,             /* xRelease      - function overloading */
    NULL              /* xRollbackto   - function overloading */
};

/* Used to register virtual table module. */
int fs_register( sqlite3* db, 
                 char **pzErrMsg, 
                 const sqlite3_api_routines* pApi )
{
    SQLITE_EXTENSION_INIT2(pApi);

    /* Initliaze the Apache Portable Runtime. */
    apr_initialize();

    /* Arrange to have it cleaned up at exit. */
    atexit(apr_terminate);

    return sqlite3_create_module(db, "filesystem", &fs_module, NULL);
}

/*-------------------------------------------------------------------*/
/* Support functions                                                 */
/*-------------------------------------------------------------------*/

/* Maps an APR file type to a name. */
static const char* file_type_name(int type)
{
    switch(type)
    {
        case APR_REG:
            return "regfile";
        case APR_DIR:
            return "dir";
        case APR_CHR:
            return "character device";
        case APR_BLK:
            return "block device";
        case APR_PIPE:
            return "pipe";
        case APR_LNK:
            return "link";
        case APR_SOCK:
            return "socket";
        default:
            return "unknown";
    }
}

/* Cleanup filenode */
static void deallocate_filenode(struct filenode* p)
{
    free(p->path);
    free(p);
}

/* Cleanup a filenode list -- only happens in error conditions where we have to
 * abort a search. */
static void deallocate_dirpath(vtab_cursor *p_cur)
{
    if(p_cur->current_node == NULL)
    {
        return;
    }

    struct filenode* current_node = p_cur->current_node;

    while(current_node != p_cur->root_node)
    {
        current_node = p_cur->current_node->parent;
        deallocate_filenode(p_cur->current_node);
        p_cur->current_node = current_node;
    }

    deallocate_filenode(p_cur->root_node);

    p_cur->current_node = NULL;
    p_cur->root_node    = NULL;
}

/* Move current_node to point to parent node */
static struct filenode* move_up_directory(vtab_cursor *p_cur)
{
    struct filenode* d = p_cur->current_node;
 
    /* Get current pointer to parent */
    p_cur->current_node = d->parent;

    /* Close current directory */
    if(d->dir != NULL)
    {
        if(d->dirent.pool != NULL)
        {
            apr_dir_close(d->dir);
        }
    }

    /* Free memory associated with current directory. */
    deallocate_filenode(d);

    /* Update d to point to parent (now current directory) */
    return p_cur->current_node;
}

/* Update root_dir to point to the next top-level directory in
 * search_paths. Update cursor accordingly. 
 */
static next_path(vtab_cursor* p_cur)
{
    /* Free the path value of the current filenode */
    if(p_cur->current_node->path != NULL)
    {
        free(p_cur->current_node->path);
        p_cur->current_node->path = NULL;
    }

    /* If root_path is empty */
    if(p_cur->root_path == NULL)
    {
        /* There is nothing more to get. */
        return 0;
    }

    /* Find the next delimiter */
    const char* ptr = strchr(p_cur->root_path, ',');

    /* Eat blank spaces */
    while(isblank(*p_cur->root_path)){p_cur->root_path++;}

    /* If we have another delimiter */
    if(ptr != NULL)
    {
        /* Then extract the string between root_path and delimiter */
        int len = (ptr - p_cur->root_path);
        p_cur->current_node->path = strndup(p_cur->root_path, len);

        /* Move root_path past delimiter (start of next value) */
        p_cur->root_path += len + 1;
    }
    else
    {
        /* This is the last match. If there is anything left */
        if(strlen(p_cur->root_path) > 0)
        {
            /* Use it. */
            p_cur->current_node->path = strdup(p_cur->root_path);
        }

        p_cur->root_path = NULL;
    }

    /* Trim right space */
    rtrim(p_cur->current_node->path);

    return 1;
}

/* Update cursor to point to next top-level directory to search, if any. */
static int next_directory(vtab_cursor *p_cur)
{
    vtab *p_vt = (vtab*)((sqlite3_vtab_cursor*)p_cur)->pVtab;

    /* Get the next path name in the search list. If there isn't next_path()
     * will return 0, as do we. */
    if(next_path(p_cur) == 0)
    {
        /* No more directories to search. End of result set. */
        p_cur->eof = 1;

        return SQLITE_OK;
    }

    /* Now try to open the directory. If it can be opended, set up the dirent to
    ** return as a row in the rowset.
    */

    /* ZERO-FILL DIRENT: Very important to zero-fill here, otherwise we may have
    ** dirent.fname and/or dirent.name members pointing to invalid addresses
    ** after apr_stat(). Our code depends in being able to check NULL status of
    ** these members, so all pointers must be NULL by default.
    */
    memset(&p_cur->current_node->dirent, 0, sizeof(apr_finfo_t));

    /* Check to see if the directory exists */
    p_cur->status = apr_stat( &p_cur->current_node->dirent, 
                              p_cur->current_node->path, 
                              APR_FINFO_TYPE, p_cur->pool );

    if(p_cur->status != APR_SUCCESS)
    {
        /* Directory does not exist */
        p_cur->eof = 1;

        if(p_vt->base.zErrMsg != NULL)
        {
            sqlite3_free(p_vt->base.zErrMsg);
        }

        printf( "Invalid directory: %s\n", p_cur->current_node->path );

        p_vt->base.zErrMsg = sqlite3_mprintf( "Invalid directory: %s", 
                                              p_cur->current_node->path );

        return SQLITE_ERROR;
    }
    else 
    {
        /* If this entry is a directory, then open it */
        if(p_cur->current_node->dirent.filetype == APR_DIR)
        {
            p_cur->status = apr_dir_open( &p_cur->current_node->dir, 
                                          p_cur->current_node->path, p_cur->pool);

            if(p_cur->status != APR_SUCCESS)
            {
                /* Could not open directory */
                p_cur->eof = 1;
                
                if(p_vt->base.zErrMsg != NULL)
                {
                    sqlite3_free(p_vt->base.zErrMsg);
                }
                
                printf("Could not open directory: %s\n", p_cur->current_node->path );
                
                p_vt->base.zErrMsg = sqlite3_mprintf( "Could not open directory: %s", 
                                                      p_cur->current_node->path );
                
                return SQLITE_ERROR;
            }
        }
        else
        {
            /** Set dir to NULL to indicate that this entry is NOT a
             *  directory. In this case, we have a top-level file, not a
             *  top-level directory. vt_next() will pick up on this and do the
             *  Right Thing.
             */
            p_cur->current_node->dir = NULL;
        }
    }

    /** Move cursor to first row: get the directory information on the top level
     *  directory. 
     */
    apr_stat( &p_cur->current_node->dirent, 
              p_cur->current_node->path, 
              APR_FINFO_DIRENT|APR_FINFO_TYPE|APR_FINFO_NAME, p_cur->pool );

    return SQLITE_OK;
}
