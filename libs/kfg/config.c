/*===========================================================================
 *
 *                            PUBLIC DOMAIN NOTICE
 *               National Center for Biotechnology Information
 *
 *  This software/database is a "United States Government Work" under the
 *  terms of the United States Copyright Act.  It was written as part of
 *  the author's official duties as a United States Government employee and
 *  thus cannot be copyrighted.  This software/database is freely available
 *  to the public for use. The National Library of Medicine and the U.S.
 *  Government have not placed any restriction on its use or reproduction.
 *
 *  Although all reasonable efforts have been taken to ensure the accuracy
 *  and reliability of the software and data, the NLM and the U.S.
 *  Government do not and cannot warrant the performance or results that
 *  may be obtained by using this software or data. The NLM and the U.S.
 *  Government disclaim all warranties, express or implied, including
 *  warranties of performance, merchantability or fitness for any particular
 *  purpose.
 *
 *  Please cite the author in any work or product based on this material.
 *
 * ===========================================================================
 *
 */

#include <kfg/kfg-priv.h>

struct KfgConfigNamelist;
#define KNAMELIST_IMPL struct KfgConfigNamelist
#include <klib/namelist.h>
#include <klib/impl.h>

#include <klib/token.h>
#include <klib/container.h>
#include <klib/refcount.h>
#include <klib/text.h>
#include <klib/printf.h>
#include <klib/rc.h>
#include <klib/debug.h>
#include <klib/log.h> 
#include <klib/out.h> /* OUTMSG */
#include <klib/klib-priv.h>
#include <kfs/directory.h>
#include <kfs/file.h>
#include <kfs/dyload.h>
#include <kfs/mmap.h>
#include <os-native.h>
#include <vfs/path.h>
#include <strtol.h>
#include <sysalloc.h>
#include <os-native.h>

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <assert.h>
#include <va_copy.h>

#if !WINDOWS
    #include <sys/utsname.h>
#endif

#include "kfg-parse.h"
#include "config-tokens.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* the leaf name of the user's modifiable settings,
   must reside in the user's $HOME/.ncbi directory */
#define MAGIC_LEAF_NAME "user-settings.mkfg"

static bool s_disable_user_settings = false;


/*--------------------------------------------------------------------------
 * KConfig
 */
static
rc_t KConfigSever ( const KConfig *self );


/*--------------------------------------------------------------------------
 * KConfigIncluded
 *  node within configuration tree
 */

typedef struct KConfigIncluded KConfigIncluded;
struct KConfigIncluded
{
    BSTNode n;
    bool is_magic_file;
    char path [ 1 ];
};


static
void CC KConfigIncludedWhack ( BSTNode *n, void *ignore )
{
    free ( n );
}

static
int CC KConfigIncludedSort ( const BSTNode *item, const BSTNode *n )
{
    const KConfigIncluded *a = ( const KConfigIncluded* ) item;
    const KConfigIncluded *b = ( const KConfigIncluded* ) n;
    return strcmp ( a -> path, b -> path );
}

/*--------------------------------------------------------------------------
 * KConfigNode
 *  node within configuration tree
 */
struct KConfigNode
{
    BSTNode n;

    /* needs to hold a dependency reference to mgr */
    KConfig *mgr;

    /* uncounted reference to parent node */
    KConfigNode *dad;

    /* File node came from; null if created during runtime*/
    KConfigIncluded *came_from;

    /* named children - always unique */
    BSTree children;

    /* named attributes */
    BSTree attr;

    /* node value */
    char * val_buffer;
    String value;

    /* node name */
    String name;

    KRefcount refcount;

    bool internal;
    bool read_only;
    bool dirty;
};

/* replace this once we introduce attributes */
#define KConfigAttrWhack NULL


/* Whack
 */
static
void CC KConfigNodeWhack ( BSTNode *n, void * data )
{
    KConfigNode *self = ( KConfigNode* ) n;
    KConfig *mgr = data;

    if ( mgr == NULL )
    {
        /* just releasing reference */
        KConfigSever ( self -> mgr );
        self -> mgr = NULL;
        self -> read_only = false;
    }
    else
    {
        /* tearing down structure */
        BSTreeWhack ( & self -> children, KConfigNodeWhack, mgr );
        BSTreeWhack ( & self -> attr, KConfigAttrWhack, mgr );
        free ( self -> val_buffer );
        free ( self );
    }
}

/* Init
 */
static
void KConfigNodeInit ( KConfigNode *self, const String *name )
{
    /* clear out here, ignoring the string space */
    memset ( self, 0, sizeof * self );

    /* initialize name early for the sake of KRefcountInit */
    string_copy ( ( char* ) ( self + 1 ), name -> size + 1, name -> addr, name -> size );
    StringInit ( & self -> name, ( char* ) ( self + 1 ), name -> size, name -> len );
    StringInit ( & self -> value, "", 0, 0 );
    KRefcountInit ( & self -> refcount, 0, "KConfigNode", "init", self -> name . addr );
}

/* Make
 */
static
rc_t KConfigNodeMake ( KConfigNode **n, const String *name )
{
    /* don't use calloc, because of string space */
    KConfigNode *cn = malloc ( sizeof * cn + name -> size + 1 );
    if ( cn == NULL )
    {
        rc_t rc = RC ( rcKFG, rcNode, rcCreating, rcMemory, rcExhausted );
        PLOGERR (klogErr, (klogErr, rc, "Unable to create a config item for $(i)", "i=%S", name));
        return rc;
    }
    KConfigNodeInit ( cn, name );
    * n = cn;

    return 0;
}


/* Cmp
 * Sort
 */
static
int CC KConfigNodeCmp ( const void *item, const BSTNode *n )
{
    const String *a = ( const String* ) item;
    const KConfigNode *b = ( const KConfigNode* ) n;
    return StringCompare ( a, & b -> name );
}

static
int CC KConfigNodeSort ( const BSTNode *item, const BSTNode *n )
{
    const KConfigNode *a = ( const KConfigNode* ) item;
    const KConfigNode *b = ( const KConfigNode* ) n;
    return StringCompare ( & a -> name, & b -> name );
}


/* AddRef
 * Release
 *  all objects are reference counted
 *  NULL references are ignored
 */
LIB_EXPORT rc_t CC KConfigNodeAddRef ( const KConfigNode *self )
{
    if ( self != NULL )
    {
        switch ( KRefcountAdd ( & self -> refcount, "KConfigNode" ) )
        {
        case krefLimit:
            return RC ( rcKFG, rcNode, rcAttaching, rcRange, rcExcessive );
        }
    }
    return 0;
}

LIB_EXPORT rc_t CC KConfigNodeRelease ( const KConfigNode *self )
{
    if ( self != NULL )
    {
        switch ( KRefcountDrop ( & self -> refcount, "KConfigNode" ) )
        {
        case krefWhack:
            KConfigNodeWhack ( & ( ( KConfigNode* ) self ) -> n, NULL );
	    break;
        case krefLimit:
            return RC ( rcKFG, rcNode, rcReleasing, rcRange, rcExcessive );
        }
    }
    return 0;
}


/*--------------------------------------------------------------------------
 * KConfig
 *  configuration parameter manager
 */
struct KConfig
{
    BSTree tree;
    BSTree included;
    KDualRef refcount;
    KConfigIncluded *current_file;

    char * load_path;
    size_t load_path_sz_tmp;

    const char *magic_file_path;
    size_t magic_file_path_size;

    bool dirty;
    bool initialized;
};


rc_t KConfigAppendToLoadPath(KConfig *self, const char* chunk)
{
    rc_t rc = 0;
    size_t new_sz = 0;

    assert(self);

    if (chunk == NULL || chunk[0] == '\0') {
        return rc;
    }

    if (self->load_path == NULL) {
        self->load_path_sz_tmp = PATH_MAX;
        self->load_path = malloc(self->load_path_sz_tmp);
        if (self->load_path == NULL) {
            return RC ( rcKFG, rcMgr, rcCreating, rcMemory, rcExhausted );
        }
        self->load_path[0] = '\0';
    }

    new_sz = strlen(self->load_path) + 1 + strlen(chunk) + 1;
    if (self->load_path_sz_tmp < new_sz) {
        self->load_path_sz_tmp = 2 * new_sz;
        self->load_path = realloc(self->load_path, self->load_path_sz_tmp);
        if (self->load_path == NULL) {
            return RC ( rcKFG, rcMgr, rcCreating, rcMemory, rcExhausted );
        }
    }

    if (self->load_path[0] != '\0') {
        strcat(self->load_path, ":");
    }
    strcat(self->load_path, chunk);

    return rc;
}


/* Whack
 */
static
rc_t KConfigEmpty ( KConfig * self)
{
    if (self)
    {
        BSTreeWhack ( & self -> tree, KConfigNodeWhack, self );
        BSTreeWhack ( & self -> included, KConfigIncludedWhack, NULL );

        self -> magic_file_path_size = 0;
        free ( ( void* ) self -> magic_file_path );
        self -> magic_file_path = NULL;

        self->load_path_sz_tmp = 0;
        free ( self->load_path );
        self->load_path = NULL;
    }
    return 0;
}

static
rc_t KConfigWhack ( KConfig *self )
{
    KConfigEmpty (self);

    free ( self );

    return 0;
}

/* Init
 */
static
void KConfigInit ( KConfig *self, KConfigNode * root )
{
    KDualRefInit ( & self -> refcount, 1, 0, "KConfig", "init", "kfg" );
    BSTreeInit ( & self -> tree );
    self -> dirty = false;
    self -> initialized = false;
    BSTreeInit ( & self -> included );
    BSTreeInsert ( & self -> tree, & root -> n, KConfigNodeSort );
    self -> magic_file_path = NULL;
}


/* AddRef
 * Release
 */
LIB_EXPORT rc_t CC KConfigAddRef ( const KConfig *self )
{
    if ( self != NULL )
    {
        switch ( KDualRefAdd ( & self -> refcount, "KConfig" ) )
        {
        case krefLimit:
            return RC ( rcKFG, rcMgr, rcAttaching, rcRange, rcExcessive );
        }
    }
    return 0;
}

LIB_EXPORT rc_t CC KConfigRelease ( const KConfig *self )
{
    if ( self != NULL )
    {
        switch ( KDualRefDrop ( & self -> refcount, "KConfig" ) )
        {
        case krefWhack:
            return KConfigWhack ( ( KConfig* ) self );
        case krefLimit:
            return RC ( rcKFG, rcMgr, rcReleasing, rcRange, rcExcessive );
        }
    }
    return 0;
}

static
KConfig *KConfigAttach ( const KConfig *self )
{
    if ( self != NULL )
    {
        switch ( KDualRefAddDep ( & self -> refcount, "KConfig" ) )
        {
        case krefLimit:
            return NULL;
        }
    }
    return ( KConfig* ) self;
}

static
rc_t KConfigSever ( const KConfig *self )
{
    if ( self != NULL )
    {
        switch ( KDualRefDropDep ( & self -> refcount, "KConfig" ) )
        {
        case krefWhack:
            return KConfigWhack ( ( KConfig* ) self );
        case krefLimit:
            return RC ( rcKFG, rcMgr, rcReleasing, rcRange, rcExcessive );
        }
    }
    return 0;
}


/* init_token_source
 */
static
rc_t init_token_source ( KTokenText *tt, KTokenSource *src,
                         char *full, size_t fsize, const char *srcpath, const char *path, va_list args )
{
    size_t num_writ;
    rc_t rc = 0;

    if (args == NULL)
        num_writ = string_copy ( full, fsize, path, string_size ( path ));
    else
        rc = string_vprintf ( full, fsize, & num_writ, path, args );
    if ( rc == 0 )
    {
        String text, fpath;
        StringInit ( & text, full, num_writ, string_len ( full, num_writ ) );
        StringInitCString ( & fpath, srcpath );
        KTokenTextInit ( tt, & text, & fpath );
        KTokenSourceInit ( src, tt );
    }
    return rc;
}

/* Find
 */
static
KToken *KConfigNodeFind ( const KConfigNode *self, const KConfigNode **n, KTokenSource *src, KToken *t )
{
    * n = NULL;

    while ( t -> id != eEndOfInput )
    {
        switch ( t -> id )
        {
        case ePeriod:
            break;
        case eDblPeriod:
            if ( self -> dad == NULL )
                return NULL;
            self = self -> dad;
            break;
        case eDecimal:
        case eHex:
        case eOctal:
        case eIdent:
        case eName:
            self = ( const KConfigNode* ) BSTreeFind
                ( & self -> children, &t -> str, KConfigNodeCmp );
            if ( self == NULL )
                return t;
            break;
        default:
            * n = self;
            return t;
        }

        if ( KTokenizerNext ( kPOSIXPathTokenizer, src, t ) -> id != eFwdSlash )
            break;

        KTokenizerNext ( kPOSIXPathTokenizer, src, t );
    }

    * n = self;
    return t;
}

/* Create
 */
static
KToken *KConfigNodeCreate ( KConfigNode *self, KConfigNode **n, KTokenSource *src, KToken *t, 
                            KConfigIncluded *current_file )
{
    bool created = false;
    KConfigNode * nself;
    * n = NULL;

    while ( t -> id != eEndOfInput )
    {
        switch ( t -> id )
        {
        case ePeriod:
            break;
        case eDblPeriod:
            if ( self -> dad == NULL )
                return NULL;
            self = self -> dad;
            break;
        case eDecimal:
        case eHex:
        case eOctal:
        case eName:
        case eIdent:
            nself = ( KConfigNode* ) BSTreeFind
                ( & self -> children, & t -> str, KConfigNodeCmp );
            if ( nself == NULL )
            {
                KConfigNode *child;
                rc_t rc = KConfigNodeMake ( & child, & t -> str );
                if ( rc != 0 )
                    return t;
                BSTreeInsert ( & self -> children, & child -> n, KConfigNodeSort );  
                child -> dad = self;
                self = child;
                created = true;
            }
            else
            {
                self = nself;
            }
            break;
        default:
            * n = self;
            return t;
        }

        if ( KTokenizerNext ( kPOSIXPathTokenizer, src, t ) -> id != eFwdSlash )
            break;

        KTokenizerNext ( kPOSIXPathTokenizer, src, t );
    }

    if ( created )
        self -> came_from = current_file;

    * n = self;
    return t;
}


/* OpenNodeRead
 * VOpenNodeRead
 *  opens a configuration node
 *
 *  "node" [ OUT ] - return parameter for indicated configuration node
 *
 *  "path" [ IN, NULL OKAY ] - optional path for specifying named
 *  node within configuration hierarchy. paths will be interpreted as
 *  if they were file system paths, using '/' as separator. the
 *  special values NULL and "" are interpreted as "."
 */
static
rc_t KConfigNodeVOpenNodeReadInt ( const KConfigNode *self, const KConfig *mgr,
                                   const KConfigNode **node, const char *path, va_list args )
{
    rc_t rc;

    if ( node == NULL )
    {
        rc = RC ( rcKFG, rcNode, rcOpening, rcParam, rcNull );
        PLOGERR (klogErr, (klogErr, rc, "faile to provide node to open $(n)", "n=%s", path));
    }
    else
    {
        if ( self == NULL )
        {
            rc = RC ( rcKFG, rcNode, rcOpening, rcSelf, rcNull );
            PLOGERR (klogErr, (klogErr, rc, "failed to provide node reference for opening $(n)", "n=%s", path));
        }
        else
        {
            if ( path == NULL || path [ 0 ] == 0 )
            {
                * node = self;
                rc = 0;
            }
            else
            {
                KTokenText tt;
                KTokenSource src;
                char full [ 4096 ];
        
                rc = init_token_source ( & tt, & src, full, sizeof full, "", path, args );
                if ( rc == 0 )
                {
                    /* look ahead */
                    KToken t;

                    /* skip over fwd slashes */
                    do
                        KTokenizerNext ( kPOSIXPathTokenizer, & src, & t );
                    while ( t.id == eFwdSlash );

                    /* follow path */
                    if ( KConfigNodeFind ( self, node, & src, & t ) == NULL )
                    {
                        rc = RC ( rcKFG, rcNode, rcOpening, rcPath, rcInvalid );
                        PLOGERR (klogErr, (klogErr, rc, "bad path $(p)", "p=%s", path));
                    }
                    if ( ( self = * node ) == NULL )
                    {
                        rc = RC ( rcKFG, rcNode, rcOpening, rcPath, rcNotFound );
                        /* don't complain about this
                           PLOGERR (klogErr, (klogErr, rc, "can't find symbol $(p)", "p=%s", path));
                        */
                    }
                    else if ( t . id != eEndOfInput )
                    {
                        rc = RC ( rcKFG, rcNode, rcOpening, rcPath, rcInvalid );
                        PLOGERR (klogErr, (klogErr, rc, "bad path $(p)", "p=%s", path));
                    }
                }
            }

            if ( rc == 0 )
            {
                /* open node for read */
                if ( self -> read_only )
                {
                    assert ( self -> mgr == mgr );
                    return KConfigNodeAddRef ( self );
                }

                /* check to see if already open */
                if ( atomic32_read ( & self -> refcount ) == 0 )
                {
                    ( ( KConfigNode* ) self ) -> mgr = KConfigAttach ( mgr );
                    ( ( KConfigNode* ) self ) -> read_only = true;
                    return KConfigNodeAddRef ( self );
                }

                rc = RC ( rcKFG, rcNode, rcOpening, rcNode, rcBusy );
            }
        }

        * node = NULL;
    }

    return rc;
}

LIB_EXPORT rc_t CC KConfigNodeVOpenNodeRead ( const KConfigNode *self,
                                              const KConfigNode **node, const char *path, va_list args )
{
    if ( self != NULL )
        return KConfigNodeVOpenNodeReadInt ( self, self -> mgr, node, path, args );

    if ( node == NULL )
        return RC ( rcKFG, rcNode, rcOpening, rcParam, rcNull );

    * node = NULL;
    return RC ( rcKFG, rcNode, rcOpening, rcSelf, rcNull );
}

LIB_EXPORT rc_t CC KConfigNodeOpenNodeRead ( const KConfigNode *self,
                                             const KConfigNode **node, const char *path, ... )
{
    rc_t rc;
    va_list args;

    va_start ( args, path );
    rc = KConfigNodeVOpenNodeRead ( self, node, path, args );
    va_end ( args );

    return rc;
}

LIB_EXPORT rc_t CC KConfigVOpenNodeRead ( const KConfig *self,
                                          const KConfigNode **node, const char *path, va_list args )
{
    rc_t rc;

    if ( node == NULL )
        rc = RC ( rcKFG, rcMgr, rcOpening, rcParam, rcNull );
    else
    {
        if ( self == NULL )
            rc = RC ( rcKFG, rcMgr, rcOpening, rcSelf, rcNull );
        else if (self->tree.root == NULL)
            rc = RC ( rcKFG, rcMgr, rcOpening, rcPath, rcNotFound );
        else
        {
            return KConfigNodeVOpenNodeReadInt
                ( (const KConfigNode *) self -> tree . root, self, node, path, args );
        }

        * node = NULL;
    }

    return rc;
}

LIB_EXPORT rc_t CC KConfigOpenNodeRead ( const KConfig *self,
                                         const KConfigNode **node, const char *path, ... )
{
    rc_t rc;
    va_list args;

    va_start ( args, path );
    rc = KConfigVOpenNodeRead ( self, node, path, args );
    va_end ( args );

    return rc;
}


/* OpenNodeUpdate
 * VOpenNodeUpdate
 *  opens a configuration node
 *
 *  "node" [ OUT ] - return parameter for indicated configuration node
 *
 *  "path" [ IN, NULL OKAY ] - optional path for specifying named
 *  node within configuration hierarchy. paths will be interpreted as
 *  if they were file system paths, using '/' as separator. the
 *  special values NULL and "" are interpreted as "."
 */
static
rc_t KConfigNodeVOpenNodeUpdateInt ( KConfigNode *self, KConfig *mgr,
                                     KConfigNode **node, const char *path, va_list args )
{
    rc_t rc;

    if ( node == NULL )
        rc = RC ( rcKFG, rcNode, rcOpening, rcParam, rcNull );
    else
    {
        if ( self == NULL )
            rc = RC ( rcKFG, rcNode, rcOpening, rcSelf, rcNull );
        else
        {
            if ( path == NULL || path [ 0 ] == 0 )
            {
                * node = self;
                rc = 0;
            }
            else
            {
                KTokenText tt;
                KTokenSource src;
                char full [ 4096 ];
        
                rc = init_token_source ( & tt, & src, full, sizeof full, "", path, args );
                if ( rc == 0 )
                {
                    /* look ahead */
                    KToken t;

                    do
                        KTokenizerNext ( kPOSIXPathTokenizer, & src, & t );
                    while ( t.id == eFwdSlash);

                    /* follow path */
                    assert ( mgr != NULL );
                    if ( KConfigNodeCreate ( self, node, & src, & t, mgr -> current_file ) == NULL )
                        return RC ( rcKFG, rcNode, rcOpening, rcPath, rcInvalid );
                    if ( ( self = * node ) == NULL )
                        rc = RC ( rcKFG, rcNode, rcOpening, rcMemory, rcExhausted );
                    else if ( t . id != eEndOfInput )
                        rc = RC ( rcKFG, rcNode, rcOpening, rcPath, rcInvalid );
                }
            }

            if ( rc == 0 )
            {
                /* check to see if internal */
                if ( self -> internal )
                    rc = RC ( rcKFG, rcNode, rcOpening, rcNode, rcReadonly );
                else
                {
                    /* check to see if open */
                    if ( atomic32_read ( & self -> refcount ) == 0 )
                    {
                        self -> mgr = KConfigAttach ( mgr );
                        assert ( ! self -> read_only );
                        return KConfigNodeAddRef ( self );
                    }

                    rc = RC ( rcKFG, rcNode, rcOpening, rcNode, rcBusy );
                }
            }
        }

        * node = NULL;
    }

    return rc;
}

LIB_EXPORT rc_t CC KConfigNodeVOpenNodeUpdate ( KConfigNode *self,
                                                KConfigNode **node, const char *path, va_list args )
{
    if ( self != NULL )
        return KConfigNodeVOpenNodeUpdateInt ( self, self -> mgr, node, path, args );

    if ( node == NULL )
        return RC ( rcKFG, rcNode, rcOpening, rcParam, rcNull );

    * node = NULL;
    return RC ( rcKFG, rcNode, rcOpening, rcSelf, rcNull );
}

LIB_EXPORT rc_t CC KConfigNodeOpenNodeUpdate ( KConfigNode *self,
                                               KConfigNode **node, const char *path, ... )
{
    rc_t rc;
    va_list args;

    va_start ( args, path );
    rc = KConfigNodeVOpenNodeUpdate ( self, node, path, args );
    va_end ( args );

    return rc;
}

LIB_EXPORT rc_t CC KConfigVOpenNodeUpdate ( KConfig *self,
                                            KConfigNode **node, const char *path, va_list args )
{
    rc_t rc;

    if ( node == NULL )
        rc = RC ( rcKFG, rcMgr, rcOpening, rcParam, rcNull );
    else
    {
        if ( self == NULL )
            rc = RC ( rcKFG, rcMgr, rcOpening, rcSelf, rcNull );
        else if (self->tree.root == NULL)
            rc = RC ( rcKFG, rcMgr, rcOpening, rcSelf, rcCorrupt );
        else
        {
            return KConfigNodeVOpenNodeUpdateInt
                ( ( KConfigNode* ) self -> tree . root, self, node, path, args );
        }

        * node = NULL;
    }

    return rc;
}

LIB_EXPORT rc_t CC KConfigOpenNodeUpdate ( KConfig *self,
                                           KConfigNode **node, const char *path, ... )
{
    rc_t rc;
    va_list args;

    va_start ( args, path );
    rc = KConfigVOpenNodeUpdate ( self, node, path, args );
    va_end ( args );

    return rc;
}


/* Read
 *  read a node value
 *
 *  "offset" [ IN ] - initial offset into configuration
 *
 *  "buffer" [ OUT ] and "bsize" [ IN ] - return buffer for read
 *
 *  "num_read" [ OUT ] - number of bytes actually read
 *
 *  "remaining" [ OUT, NULL OKAY ] - optional return parameter for
 *  the number of bytes remaining to be read.
 *  specifically, "offset" + "num_read" + "remaining" == sizeof node data
 */
LIB_EXPORT rc_t CC KConfigNodeRead ( const KConfigNode *self,
                                     size_t offset, char *buffer, size_t bsize,
                                     size_t *num_read, size_t *remaining )
{
    rc_t rc;
    size_t dummy;

    if ( remaining == NULL )
        remaining = & dummy;

    if ( num_read == NULL )
        rc = RC ( rcKFG, rcNode, rcReading, rcParam, rcNull );
    else
    {
        if ( self == NULL )
            rc = RC ( rcKFG, rcNode, rcReading, rcSelf, rcNull );
        else if ( buffer == NULL && bsize != 0 )
            rc = RC ( rcKFG, rcNode, rcReading, rcBuffer, rcNull );
        else if ( offset >= self -> value . size )
            rc = 0;
        else
        {
            size_t avail = * remaining = self -> value . size - offset;
            if ( avail > bsize )
                avail = bsize;
            if ( avail > 0 )
                memcpy ( buffer, & self -> value . addr [ offset ], avail );
            * num_read = avail;
            * remaining -= avail;
            return 0;
        }

        * num_read = 0;
    }

    * remaining = 0;

    return rc;
}

static
void KConfigNodeSetDirty ( KConfigNode *self )
{
    KConfig *mgr = self -> mgr;

    if ( mgr == NULL )
        self -> dirty = true;
    else if ( mgr -> initialized )
        self -> dirty = mgr -> dirty = true;
}

/* Write
 *  write a node value or attribute
 *  replaces anything already there
 *
 *  "buffer" [ IN ] and "size" [ IN ] - new value data
 */
LIB_EXPORT rc_t CC KConfigNodeWrite ( KConfigNode *self, const char *buffer, size_t size )
{
    rc_t rc;

    if ( self == NULL )
        rc = RC ( rcKFG, rcNode, rcWriting, rcSelf, rcNull );
    else if ( self -> read_only || self -> internal )
        rc = RC ( rcKFG, rcNode, rcWriting, rcSelf, rcReadonly );
    else if ( size == 0 )
    {
        free ( self -> val_buffer ), self -> val_buffer = NULL;
        StringInit ( & self -> value, "", 0, 0 );
        rc = 0;
    }
    else if ( buffer == NULL )
        rc = RC ( rcKFG, rcNode, rcWriting, rcBuffer, rcNull );
    else
    {
        if ( size != self -> value . size )
        {
            void *new_buffer = realloc ( self -> val_buffer, size + 1 );
            if ( new_buffer == NULL )
                return RC ( rcKFG, rcNode, rcWriting, rcMemory, rcExhausted );
            self -> val_buffer = new_buffer;
            self -> value . size = size;
            self -> value . addr = new_buffer;
        }

        assert ( self -> val_buffer != NULL );
        string_copy ( self -> val_buffer, self -> value . size + 1, buffer, size );
        self -> value . len = string_len ( self -> val_buffer, size );
        
        KConfigNodeSetDirty ( self );
        
        rc = 0;
    }

    return rc;
}


/* Append
 *  append data to value
 *
 *  "buffer" [ IN ] and "size" [ IN ] - value data to be appended
 */
LIB_EXPORT rc_t CC KConfigNodeAppend ( KConfigNode *self, const char *buffer, size_t size )
{
    rc_t rc;

    if ( self == NULL )
        rc = RC ( rcKFG, rcNode, rcWriting, rcSelf, rcNull );
    else if ( self -> read_only || self -> internal )
        rc = RC ( rcKFG, rcNode, rcWriting, rcSelf, rcReadonly );
    else if ( size == 0 )
        rc = 0;
    else if ( buffer == NULL )
        rc = RC ( rcKFG, rcNode, rcWriting, rcBuffer, rcNull );
    else
    {
        void *new_buffer = realloc ( self -> val_buffer, self -> value . size + size + 1 );
        if ( new_buffer == NULL )
            return RC ( rcKFG, rcNode, rcWriting, rcMemory, rcExhausted );
        self -> val_buffer = new_buffer;
        string_copy ( & self -> val_buffer [ self -> value . size ], self -> value . size + size + 1, buffer, size );
        self -> value . size += size;
        self -> value . len = string_len ( self -> val_buffer, self -> value . size );
        
        KConfigNodeSetDirty ( self );

        rc = 0;
    }

    return rc;
}


/* ReadAttr
 *  reads as NUL-terminated string
 *
 *  "name" [ IN ] - NUL terminated attribute name
 *
 *  "buffer" [ OUT ] and "bsize" - return parameter for attribute value
 *
 *  "size" [ OUT ] - return parameter giving size of string
 *  not including NUL byte. the size is set both upon success
 *  and insufficient buffer space error.
 */
LIB_EXPORT rc_t CC KConfigNodeReadAttr ( const KConfigNode *self, const char *name,
                                         char *buffer, size_t bsize, size_t *size )
{
    PLOGMSG (klogFatal, (klogFatal, "$(F) unimplemented", "F=%s", __func__));
    return -1;
}


/* WriteAttr
 *  writes NUL-terminated string
 *
 *  "name" [ IN ] - NUL terminated attribute name
 *
 *  "value" [ IN ] - NUL terminated attribute value
 */
LIB_EXPORT rc_t CC KConfigNodeWriteAttr ( KConfigNode *self,
                                          const char *name, const char *value )
{
    PLOGMSG (klogFatal, (klogFatal, "$(F) unimplemented", "F=%s", __func__));
    return -1;
}


/* Drop
 * VDrop
 *  drop some or all node content
 */
LIB_EXPORT rc_t CC KConfigNodeDropAll ( KConfigNode *self )
{
    PLOGMSG (klogFatal, (klogFatal, "$(F) unimplemented", "F=%s", __func__));
    return -1;
}

LIB_EXPORT rc_t CC KConfigNodeDropAttr ( KConfigNode *self, const char *attr )
{
    PLOGMSG (klogFatal, (klogFatal, "$(F) unimplemented", "F=%s", __func__));
    return -1;
}

LIB_EXPORT rc_t CC KConfigNodeVDropChild ( KConfigNode *self, const char *path, va_list args )
{
    PLOGMSG (klogFatal, (klogFatal, "$(F) unimplemented", "F=%s", __func__));
    return -1;
}

LIB_EXPORT rc_t CC KConfigNodeDropChild ( KConfigNode *self, const char *path, ... )
{
    PLOGMSG (klogFatal, (klogFatal, "$(F) unimplemented", "F=%s", __func__));
    return -1;
}


/* Rename
 *  renames a contained object
 *
 *  "from" [ IN ] - NUL terminated string in UTF-8
 *  giving simple name of existing attr
 *
 *  "to" [ IN ] - NUL terminated string in UTF-8
 *  giving new simple attr name
 */
LIB_EXPORT rc_t CC KConfigNodeRenameAttr ( KConfigNode *self, const char *from, const char *to )
{
    PLOGMSG (klogFatal, (klogFatal, "$(F) unimplemented", "F=%s", __func__));
    return -1;
}

LIB_EXPORT rc_t CC KConfigNodeRenameChild ( KConfigNode *self, const char *from, const char *to )
{
    PLOGMSG (klogFatal, (klogFatal, "$(F) unimplemented", "F=%s", __func__));
    return -1;
}


/*--------------------------------------------------------------------------
 * KConfig
 */

static
rc_t
update_node ( KConfig* self, const char* key, const char* value, bool internal )
{
    KConfigNode * node;
    rc_t rc = KConfigVOpenNodeUpdate ( self, &node, key, NULL);
    if (rc == 0)
    {
/*        pLogMsg(klogInfo, "updating config key $(KEY) with '$(VALUE)'", 
                          "KEY=%s,VALUE=%s", 
                          key, value);*/
        rc = KConfigNodeWrite (node, value, string_size(value));
        node -> internal = internal;
        KConfigNodeRelease ( node );
    }
    return rc;
}

static
rc_t write_nvp(void * self, const char* name, size_t nameLen, VNamelist* values)
{   /* concatenate all values from the namelist and put the result into config under the given name */
    uint32_t count;
    uint32_t size=0;
    uint32_t concatTo=0;
    uint32_t i;
    const String* nameStr;

    char* buf;
    rc_t rc=VNameListCount(values, &count);
    if (rc != 0)
    {
        return rc;
    }
    for (i=0; i < count; ++i)
    {
        const char* val;
        rc=VNameListGet(values, i, &val);
        if (rc != 0)
        {
            return rc;
        }
        size+=string_size(val);
    }

    buf=(char*)malloc(size+1);
    if (buf == 0)
    {
        return RC ( rcKFG, rcMgr, rcLoading, rcMemory, rcExhausted );
    }

    concatTo=0;
    for (i=0; i < count; ++i)
    {
        const char* val;
        rc=VNameListGet(values, i, &val);
        if (rc != 0)
        {
            free(buf);
            return rc;
        }
        string_copy(buf+concatTo, size+1-concatTo, val, string_size(val));
        concatTo+=string_size(val);
    }
    buf[size]=0;

    {
        String tmp;
        StringInit(&tmp, name, nameLen, nameLen);
        StringCopy(&nameStr, &tmp);
    }
    rc = update_node((KConfig *)self, nameStr->addr, buf, false);
    StringWhack(nameStr);
    free(buf);
    return rc;
}

static
bool look_up_var(void * self, struct KFGParseBlock* pb)
{
    const KConfigNode* node;
    rc_t rc = KConfigOpenNodeRead((KConfig*)self, &node, "%.*s", pb->tokenLength-3, pb->tokenText+2);
    if (rc == 0)
    {
        pb->tokenText   = node->value.addr; 
        pb->tokenLength = node->value.len;
        pb->tokenId     = kfgVAR_REF;
    }
    KConfigNodeRelease(node);
    return rc == 0;
}

static
void CC report_error(KFGScanBlock* sb, const char* msg)
{
	pLogMsg(klogErr, "$(file):$(line):$(column): error: token='$(token)', msg='$(msg)'", 
				 	 "file=%s,line=%d,column=%d,token=%.*s,msg=%s", 
				 	 sb->file, 
                     sb->lastToken->line_no, 
                     sb->lastToken->column_no, 
                     sb->lastToken->tokenLength, 
                     sb->lastToken->tokenText, 
                     msg);
}

/*
 * Set up the parameter block and start parsing lines
 */
static
rc_t parse_file ( KConfig * self, const char* path, const char * src )
{
    KFGParseBlock pb;
    KFGScanBlock sb;
    rc_t rc;

    pb.tokenLength  = 0;
    pb.line_no      = 0;
    pb.column_no    = 0;

    sb.self         = self;
    sb.file         = path;
    sb.write_nvp    = write_nvp;
    sb.look_up_var  = look_up_var;
    sb.report_error = report_error;

    rc = KFGScan_yylex_init(&sb, src);
    if (rc == 0)
    {
        KFG_parse(&pb, &sb); /* may have reported parse errors into log, but we should have been able to extract enough data to proceed regardless */
        KFGScan_yylex_destroy(&sb);
    }

    return rc;
}

/* LoadFile
 * loads a configuration file
 */
LIB_EXPORT rc_t CC KConfigLoadFile ( KConfig * self, const char * path, const KFile * file )
{
    rc_t rc;

    if ( self == NULL )
        rc = RC ( rcKFG, rcMgr, rcLoading, rcSelf, rcNull );
    else if ( file == NULL )
        rc = RC ( rcKFG, rcMgr, rcLoading, rcFile, rcNull );
    else
    {
        const KMMap * mm;
        bool entry_initialized = self -> initialized;
        self -> initialized = false;

        /* populate file-specific predefined nodes */
#define UPDATE_NODES(dir, file)                             \
        rc = update_node(self, "kfg/dir", dir, false );     \
        if (rc == 0)                                        \
            rc = update_node(self, "kfg/name", file, false )

        if ( path == NULL || path [ 0 ] == 0)
        {
            path = "UNSPECIFIED";
            UPDATE_NODES ( "", "" );
        }
        else
        {
            KDirectory* dir;
            rc = KDirectoryNativeDir(&dir);
            if (rc == 0 )
            {
                char buff [ 4096 ];
                rc = KDirectoryResolvePath ( dir, true, buff, sizeof buff, "%.*s", strlen(path), path );
                if ( rc == 0 )
                {
                    char* name = strrchr (buff, '/');
                    if (name == NULL)
                    {   /* no dir name */
                        UPDATE_NODES("", buff);
                    }
                    else
                    {
                        *name=0; /* nul-terminate dir name; file name follows the 0 */
                        UPDATE_NODES(buff, name+1);
                    }
                }
                KDirectoryRelease ( dir );
            }
            else
            {
                update_node(self, "kfg/dir", "", false);
                update_node(self, "kfg/name", "", false);
            }
        }
#undef UPDATE_NODES


        rc = KMMapMakeRead ( & mm, file );
        if ( rc == 0 )
        {
            size_t size;
            const void * ptr;
            rc = KMMapAddrRead ( mm, & ptr );
            if ( rc == 0 )
                rc = KMMapSize ( mm, & size );
            if ( rc == 0 )
            {
                /* make a 0-terminated copy for parsing */
                char* buf=malloc(size+1);
                if (buf == 0)
                {
                    rc = RC ( rcKFG, rcMgr, rcLoading, rcMemory, rcExhausted );
                }
                else
                {
                    string_copy(buf, size+1, ptr, size);
                    buf[size]=0;

                    /* Parse the path to populate: */
                    /* update_node(self, "kfg/dir", dir, false);*/
                    /* update_node(self, "kfg/name", name, false);*/

                    /* parse config file */
                    rc = parse_file ( self, path, buf );
                    free(buf);
                }
            }

            KMMapRelease ( mm );
        }
        self -> initialized = entry_initialized;
    }

    return rc;
}


typedef struct PrintBuff PrintBuff;
struct PrintBuff
{
    KFile *f;
    /* total bytes flushed to the file*/
    size_t flushed;
    /* total bytes in buffer */
    size_t buffered;
    
    rc_t rc;
    
    /* buffer */
    char buffer [ 32 * 1024 ];
    
};

/* Init
 *  initialize your structure    
 */
static
void PrintBuffInit ( PrintBuff *pb, KFile *f )
{
    assert ( pb != NULL );
    pb -> f = f;
    pb -> flushed = 0;
    pb -> buffered = 0;
    pb -> rc = 0;
}

/* Flush
 * Write buffer out to file
 */
static rc_t PrintBuffFlush ( PrintBuff *self )
{
    rc_t rc = 0;

    assert ( self != NULL );
    if ( self -> buffered != 0 )
    {
        size_t num_writ;
        rc = KFileWriteAll ( self -> f, self -> flushed, self -> buffer, self -> buffered, & num_writ );
        
        if ( rc == 0 )
        {
            if ( num_writ != self -> buffered )
                rc = RC ( rcKFG, rcBuffer, rcFlushing, rcTransfer, rcIncomplete );
            else
            {
                self -> flushed += num_writ;
                self -> buffered = 0;
            }
        }
    }
    return self -> rc = rc;
}

/* Print
 *  printf style writing to the buffer
 */
static
rc_t PrintBuffPrint ( PrintBuff *self, const char *fmt, ... )
{
    rc_t rc;
    size_t num_writ;
    va_list args1, args2;

    assert ( self != NULL );
    assert ( fmt != NULL );
    assert ( fmt [ 0 ] != 0 );

    va_start ( args1, fmt );
    va_copy ( args2, args1 );

    rc = string_vprintf ( & self -> buffer [ self -> buffered ], 
                          sizeof self -> buffer - self -> buffered, & num_writ, fmt, args1 );
    if ( rc == 0 )
        self -> buffered += num_writ;
    else if ( GetRCObject ( rc ) == rcBuffer && GetRCState ( rc ) == rcInsufficient )
    {
        rc = PrintBuffFlush ( self );
        if ( rc == 0 )
        {
            rc = string_vprintf ( & self -> buffer [ self -> buffered ],
                                  sizeof self -> buffer - self -> buffered, & num_writ, fmt, args2 );
            if ( rc == 0 )
                self -> buffered += num_writ;
        }
    }

    va_end ( args2 );
    va_end ( args1 );

    return self -> rc = rc;
    
}

char ToHex(uint32_t i)
{
    if (i <= 9)
        return '0' + i;
    return 'A' + (i - 10);
}

static
rc_t CC PrintBuffPrintQuoted ( PrintBuff *self, const String* data )
{
    rc_t rc = PrintBuffPrint(self, "\"");
    const char* str = (const char*)(data->addr);
    uint32_t i;
    for ( i = 0; i < StringLength(data); ++i )
    {
        if (rc != 0)
            break;
        if (str[i] < ' ')
        {
            rc = PrintBuffPrint(self, "\\x%c%c", ToHex(str[i]/16), ToHex(str[i]%16) );
        }
        else
        {
            switch (str[i])
            {
            case '"':
                rc = PrintBuffPrint(self, "\\\"");
                break;
            default:
                rc = PrintBuffPrint(self, "%c", str[i]);
            }
        }
    }
    if (rc == 0)
        rc = PrintBuffPrint(self, "\"");
    return rc;
}

static
bool KConfigNodePrintPath ( KConfigNode *self, PrintBuff *pb )
{
    if ( self -> dad == NULL )
        PrintBuffPrint ( pb, "%S", & self -> name );
    else
    {
        if ( KConfigNodePrintPath ( self -> dad, pb ) )
            return true;

        PrintBuffPrint ( pb, "/%S", & self -> name );
    }
    return pb -> rc != 0;
}

static
bool CC WriteDirtyNode ( BSTNode *n, void *data )
{
    KConfigNode *self = ( KConfigNode * ) n;
    PrintBuff *pb = data;

    if ( self -> dirty || ( self -> came_from != NULL && self -> came_from -> is_magic_file ) )
    {
        if ( KConfigNodePrintPath ( self, pb ) )
            return true;

        if ( PrintBuffPrint ( pb, " = " ) != 0 )
            return true;
            
        if ( PrintBuffPrintQuoted ( pb, &self->value ) != 0 )
            return true;
            
        if ( PrintBuffPrint ( pb, "\n" ) != 0 )
            return true;
    }

    return BSTreeDoUntil ( & self -> children, false, WriteDirtyNode, pb );
}

static
void CC UnsetDirtyNode ( BSTNode *n, void *data )
{
    KConfigNode *self = ( KConfigNode * ) n;

    if ( self -> dirty )
        self -> dirty = false;

    BSTreeForEach ( & self -> children, false, UnsetDirtyNode, data );
}

static
rc_t path_to_magic_file ( const KConfig *self, char *path, size_t buffer_size, size_t *path_size )
{
    const KConfigNode *node;
    rc_t rc = KConfigOpenNodeRead ( self, & node, "NCBI_SETTINGS" );

    if ( rc == 0 )
    {
        size_t num_read, remaining;
        rc = KConfigNodeRead ( node, 0, path, buffer_size - 1, & num_read, & remaining );

        if ( rc == 0 && remaining != 0 )
           rc = RC ( rcKFG, rcNode, rcReading, rcBuffer, rcInsufficient );

        path[num_read] = '\0';
        
        KConfigNodeRelease ( node );
    }
    return rc;
}

/* Commit
 *  commits changes to user's private configuration file
 */
LIB_EXPORT rc_t CC KConfigCommit ( KConfig *self )
{
    rc_t rc;
    size_t path_size;
    char magic_file_path [ 4096 ];

    if ( self == NULL )
        return RC ( rcKFG, rcData, rcCommitting, rcSelf, rcNull );

    /* Must only commit dirty nodes*/
    if ( ! self -> dirty )
        return 0;

    rc = path_to_magic_file ( self, magic_file_path, sizeof magic_file_path, & path_size );
    if ( rc == 0 )
    {
        char tmp_file_path [ 4096 ];
        rc = string_printf ( tmp_file_path, sizeof tmp_file_path, NULL, "%s.tmp", magic_file_path );
        if ( rc == 0 )
        {
            KDirectory *dir;        
            
            /* get current directory */
            rc = KDirectoryNativeDir ( & dir );
            if ( rc == 0 )
            {
                KFile *tmp;

                /* create temp magic file */
                rc = KDirectoryCreateFile ( dir, & tmp, false, 0600, kcmInit | kcmParents, tmp_file_path );

                if ( rc == 0 )
                {
                    PrintBuff pb;
                    PrintBuffInit ( & pb, tmp );

                    /* issue warning */
                    rc = PrintBuffPrint ( & pb, "## auto-generated configuration file - DO NOT EDIT ##\n\n" );
                    if ( rc == 0 )
                    {
                        /* flush nodes to file */
                        if ( BSTreeDoUntil ( & self -> tree, false, WriteDirtyNode, & pb ) )
                            /* failure */
                            rc = pb . rc;
                        else 
                            rc = PrintBuffFlush ( & pb );
                    }
                        
                    KFileRelease ( tmp );
                        
                    if ( rc == 0 )
                    {
                        /* Rename file */
                        rc = KDirectoryRename ( dir, true, tmp_file_path, magic_file_path  );
                        if ( rc == 0 )
                        {
                            /* clear dirty flags */
                            BSTreeForEach (  & self -> tree, false, UnsetDirtyNode, & pb);
                                
                            self -> dirty = false;
                        }
                    }
                        
                    if ( rc != 0 )
                        KDirectoryRemove ( dir, true, tmp_file_path );
                }
            
                /* release dir */
                KDirectoryRelease ( dir );
            }
        }
    }
    return rc;
}


static
rc_t record_magic_path ( KConfig *self, const KDirectory *dir, const char *path, size_t sz )
{
    char buff [ 4096 ];
    rc_t rc = KDirectoryResolvePath ( dir, true, buff, sizeof buff, "%.*s", ( int ) sz, path );
    if ( rc == 0 )
    {
        char *magic_file_path;
        sz = strlen ( buff );
        magic_file_path = malloc ( sz + 1 );
        if ( magic_file_path == NULL )
            rc = RC ( rcKFG, rcMgr, rcLoading, rcMemory, rcExhausted );
        else
        {
            free ( (void*) self -> magic_file_path );
            self -> magic_file_path = magic_file_path;
            self -> magic_file_path_size = sz;
            memcpy ( magic_file_path, buff, sz + 1 );
        }
    }

    return rc;
}


static
rc_t make_include_path ( KConfigIncluded **p, const KDirectory *dir, const char *path, size_t sz, bool is_magic )
{
    char buff [ 4096 ];
    rc_t rc = KDirectoryResolvePath ( dir, true, buff, sizeof buff, "%.*s", ( int ) sz, path );
    if ( rc == 0 )
    {
        KConfigIncluded *include;
        sz = strlen ( buff );
        include = malloc ( sizeof * include + sz );
        if ( include == NULL )
            rc = RC ( rcKFG, rcMgr, rcLoading, rcMemory, rcExhausted );
        else
        {
            include -> is_magic_file = is_magic;
            string_copy ( include -> path, sz + sizeof include -> path, buff, sz );
            * p = include;
            return 0;
        }
    }
    * p = NULL;
    return rc;
}


static
bool load_from_file_path ( KConfig *self, const KDirectory *dir, const char *path, size_t sz, bool is_magic )
{
    rc_t rc;
    const KFile *cfg_file;

    /* record magic file path, regardless of whether it exists as a file */
    if ( is_magic )
        record_magic_path ( self, dir, path, sz );
    
    DBGMSG( DBG_KFG, DBG_FLAG(DBG_KFG), ( "KFG: try to load from file '%.*s'\n", (int)sz, path ) );
    rc = KDirectoryOpenFileRead ( dir, & cfg_file, "%.*s", ( int ) sz, path );
    if ( rc == 0 )
    {
        KConfigIncluded *include;
        rc = make_include_path ( & include, dir, path, sz, is_magic );
        if ( rc == 0 )
        {
            BSTNode *existing;
            if ( BSTreeInsertUnique ( & self -> included, & include -> n, & existing, KConfigIncludedSort ) != 0 )
                free ( include );
            else
            {
                self -> current_file = include;
                rc = KConfigLoadFile ( self, include -> path, cfg_file );
                self -> current_file = NULL;
                if ( rc != 0 )
                {
                    BSTreeUnlink ( & self -> included, & include -> n );
                    free ( include );
                }
            }
        }

        KFileRelease ( cfg_file );
    }
    return ( rc == 0 ) ? true : false;
}

typedef struct scan_config_path_data scan_config_path_data;
struct scan_config_path_data
{
    KConfig *self;
    bool loaded;
};

static
rc_t CC scan_config_path ( const KDirectory *dir, uint32_t type, const char *name, void *data )
{
    scan_config_path_data * pb = data;
    switch ( type )
    {
    case kptFile:
    case kptFile | kptAlias:
    {
        size_t sz = strlen ( name );
        if ( sz >= 5 && strcase_cmp ( & name [ sz - 4 ], 4, ".kfg", 4, 4 ) == 0 )
            pb -> loaded |= load_from_file_path ( pb -> self, dir, name, sz, false );

        break;
    }}

    return 0;
}

static
bool scan_config_dir ( KConfig *self, const KDirectory *dir )
{
    scan_config_path_data pb;

    pb . self = self;
    pb . loaded = false;

    KDirectoryVVisit ( dir, false, scan_config_path, & pb, ".", NULL );

    return pb . loaded;
}

static
bool load_from_dir_path ( KConfig *self, const KDirectory *dir, const char *path, size_t sz )
{
    bool loaded = false;
    const KDirectory *cfg_dir;
    rc_t rc = KDirectoryOpenDirRead ( dir, & cfg_dir, false, "%.*s", ( uint32_t ) sz, path );
    if ( rc == 0 )
    {
        DBGMSG( DBG_KFG, DBG_FLAG(DBG_KFG), ( "KFG: try to load from directory '%.*s'\n", (int)sz, path ) );
        loaded = scan_config_dir ( self, cfg_dir );
        KDirectoryRelease ( cfg_dir );
    }
    return loaded;
}

static
bool load_from_path ( KConfig *self, const KDirectory * dir, const char *path, size_t sz )
{
    bool loaded = false;
    const char *naughty = string_chr ( path, sz, '%' );
    if ( naughty == NULL && sz != 0 )
    {
        DBGMSG( DBG_KFG, DBG_FLAG(DBG_KFG), ( "KFG: try to load from path '%.*s'\n", (int)sz, path ) );
        switch ( KDirectoryPathType ( dir, "%.*s", ( int ) sz, path ) & ~ kptAlias )
        {
        case kptFile:
            loaded = load_from_file_path ( self, dir, path, sz, false );
            break;
        case kptDir:
            loaded = load_from_dir_path ( self, dir, path, sz );
            break;
        }
    }
    return loaded;
}

static
bool load_from_path_list ( KConfig *self, const KDirectory *dir, const char *path )
{
    bool loaded = false;
    const char *end = path + strlen ( path );
    while ( path < end )
    {
        const char *sep = string_chr ( path, end - path, ':' );
        if ( sep == NULL )
            sep = end;
        if ( load_from_path ( self, dir, path, sep - path ) )
            loaded = true;
        path = sep + 1;
    }
    return loaded;
}

static
bool load_from_env_variable ( KConfig *self, const KDirectory *dir )
{
    const char * env_list [] =
    {
        "KLIB_CONFIG",
        "VDB_CONFIG",
        "VDBCONFIG"
    };
    
    int i;
    bool loaded = false;
    for ( i = 0; ! loaded && i < sizeof env_list / sizeof env_list [ 0 ]; ++ i )
    {
        const char *eval = getenv ( env_list [ i ] );
        DBGMSG( DBG_KFG, DBG_FLAG(DBG_KFG), ( "KFG: try to load from env. var '%s'\n", env_list[ i ] ) );
        if ( eval != NULL && eval [ 0 ] != 0 )
        {
            rc_t rc = 0;
            DBGMSG( DBG_KFG, DBG_FLAG(DBG_KFG), ( "KFG: try to load from env. var '%s'\n", eval ) );
            rc = KConfigAppendToLoadPath(self, eval);
            loaded = load_from_path_list ( self, dir, eval );
            if ( loaded )
                DBGMSG( DBG_KFG, DBG_FLAG(DBG_KFG), ( "KFG: found from env. var '%s'\n", eval ) );
        }
    }

    return loaded;
}

static
bool load_from_std_location ( KConfig *self, const KDirectory *dir )
{
    const char * std_locs [] =
    {
#if ! WINDOWS
        "/etc/ncbi",
#else
        "/c/ncbi",
#endif
    };

    rc_t rc = 0;

    int i;
    bool loaded = false;
    for ( i = 0; ! loaded && i < sizeof std_locs / sizeof std_locs [ 0 ]; ++ i )
    {
        DBGMSG( DBG_KFG, DBG_FLAG(DBG_KFG), ( "KFG: try to load from std. location '%s'\n", std_locs[ i ] ) );
        rc = KConfigAppendToLoadPath(self, std_locs [ i ]);
        loaded = load_from_path ( self, dir, std_locs [ i ], strlen ( std_locs [ i ] ));
    }
    if ( loaded )
        DBGMSG( DBG_KFG, DBG_FLAG(DBG_KFG), ( "KFG: found from std. location\n" ) );
    return loaded;
}

static
rc_t load_from_fs_location ( KConfig *self )
{
    KDyld *dyld;
    rc_t rc = KDyldMake ( & dyld );
    if ( rc == 0 )
    {
        const KDirectory *dir;
        rc = KDyldHomeDirectory ( dyld, & dir, ( fptr_t ) KConfigMake );
        if ( rc == 0 )
        {
            char resolved[PATH_MAX + 1];
            DBGMSG( DBG_KFG, DBG_FLAG(DBG_KFG), ( "KFG: try to load from dyn. loader\n" ) );

/* N.B. Duplication of ResolvePath here and in load_from_dir_path ? */
            if (KDirectoryResolvePath
                    (dir, true, resolved, sizeof resolved, "ncbi") == 0)
            {
                rc = KConfigAppendToLoadPath(self, resolved);
            }
            if ( load_from_dir_path ( self, dir, "ncbi", 4 ) )
                DBGMSG( DBG_KFG, DBG_FLAG(DBG_KFG), ( "KFG: found from dyn. loader\n" ) );
            KDirectoryRelease ( dir );
        }
        KDyldRelease ( dyld );
    }
    return rc;
}

LIB_EXPORT rc_t CC KConfigGetLoadPath ( const KConfig *self,
    const char **path )
{
    if (self == NULL) {
        return RC ( rcKFG, rcPath, rcListing, rcSelf, rcNull );
    }

    if (path == NULL) {
        return RC ( rcKFG, rcPath, rcListing, rcParam, rcNull );
    }

    *path = self->load_path;
    return 0;
}


static
bool load_from_home(KConfig *self, const KDirectory *dir)
{
    const char *home = getenv("HOME");
    DBGMSG( DBG_KFG, DBG_FLAG(DBG_KFG), ( "KFG: checking HOME\n" ) );

    if (home == NULL) {
        home = getenv("USERPROFILE");
    }

    if (home != NULL)
    {
        bool loaded;
        size_t num_writ, path_size;
        char path[PATH_MAX];
        rc_t rc = string_printf(path, sizeof path, &num_writ, "%s/.ncbi", home);
        if (rc != 0)
            return false;

        assert(num_writ < sizeof path);
        
        loaded = load_from_path ( self, dir, path, num_writ );
        if ( loaded )
        {
            DBGMSG( DBG_KFG, DBG_FLAG(DBG_KFG),
                ( "KFG: found from '%s'\n", path ) );
        }

        path_size = num_writ;
        rc = string_printf ( & path [ path_size ], sizeof path - path_size, & num_writ, "/%s", MAGIC_LEAF_NAME );
        if ( rc == 0 )
        {
            if ( load_from_file_path ( self, dir, path, path_size + num_writ, true ) )
                loaded = true;
        }
        return loaded;
    }
    else {
        DBGMSG( DBG_KFG, DBG_FLAG(DBG_KFG),
            ( "KFG: none of env{HOME}, env{USERPROFILE} is defined\n" ) );
    }

    return false;
}

static
void load_config_files ( KConfig *self, const KDirectory *dir )
{
    rc_t rc;
    bool loaded;
    KDirectory *wd;

    /* if user supplied a starting point, try that */
    if ( dir != NULL )
    {
        DBGMSG( DBG_KFG, DBG_FLAG(DBG_KFG), ( "KFG: try load from supplied directory\n" ) );

        loaded = scan_config_dir ( self, dir );
        if ( loaded )
            DBGMSG( DBG_KFG, DBG_FLAG(DBG_KFG), ( "KFG: found from supplied directory\n" ) );

        if ( load_from_file_path ( self, dir, MAGIC_LEAF_NAME, sizeof MAGIC_LEAF_NAME - 1, true ) )
            loaded = true;

        if ( loaded )
            return;
    }

    /* open up the native directory */
    rc = KDirectoryNativeDir ( & wd );
    if ( rc != 0 )
        return;

    /* try to load from environment variable */
    loaded = load_from_env_variable ( self, wd );

    /* try to load from standard locations */
    if ( ! loaded )
        loaded = load_from_std_location ( self, wd );

    /* check for config as the result of a user install
       i.e. not an admin installation */
    if ( ! loaded )
        load_from_fs_location ( self );

    if ( ! s_disable_user_settings )
        loaded |= load_from_home ( self, wd );

    KDirectoryRelease ( wd );

    if (self->load_path) {
        char* tmp = NULL;
        self->load_path_sz_tmp = strlen(self->load_path) + 1;
        tmp = realloc(self->load_path, self->load_path_sz_tmp);
        if (tmp) {
            self->load_path = tmp;
        }
    }
}

static
void add_predefined_nodes ( KConfig * self, const char *appname )
{
    size_t bytes;
    char buf [ 4096 ];
    const char *value;

    KDirectory *cwd;
    const KDirectory *dir;

#if ! WINDOWS
    struct utsname name;
#endif

    /* Path to libkfg.so */
    KDyld *dyld;
    rc_t rc = KDyldMake ( & dyld );
    if ( rc == 0 )
    {
        rc = KDyldHomeDirectory ( dyld, & dir, ( fptr_t ) KConfigMake );
        if ( rc == 0 )
        {
            KDirectoryResolvePath ( dir, true, buf, sizeof buf, "." );
            KDirectoryRelease ( dir );
        }
        KDyldRelease ( dyld );
    }
    update_node ( self, "vdb/lib/paths/kfg", rc == 0 ? buf : "", true );

    /* Architecture */ 
#if ! WINDOWS
    if (uname(&name) >= 0)
        update_node(self, "kfg/arch/name", name.nodename, true);
    else
#endif
        update_node ( self, "kfg/arch/name", "", true);

    string_printf(buf, sizeof(buf), &bytes, "%u", _ARCH_BITS);
    update_node ( self, "kfg/arch/bits", buf, true );

    /* *OS */
#if LINUX
    #define OS "linux"
#elif MAC 
    #define OS "mac"
#elif WINDOWS
    #define OS "win"
#elif SUN
    #define OS "sun"
#else
    #error unrecognized OS
#endif
    update_node ( self, "OS", OS, true );
#undef OS

    /* BUILD_LINKAGE */
#if _STATIC
    #define BUILD_LINKAGE "STATIC"
#else
    #define BUILD_LINKAGE "DYNAMIC"
#endif
    update_node ( self, "BUILD_LINKAGE", BUILD_LINKAGE, true );
#undef BUILD_LINKAGE

    /* BUILD */
#if _PROFILING
    #define BUILD "PROFILE"
#elif _DEBUGGING
    #define BUILD "DEBUG"
#else 
    #define BUILD "RELEASE"
#endif
    update_node(self, "BUILD", BUILD, true );
#undef BUILD

    cwd = NULL;

    /* PWD */
    rc = KDirectoryNativeDir ( & cwd );
    if ( rc == 0 )
        rc = KDirectoryResolvePath ( cwd, true, buf, sizeof buf, "." );
    update_node(self, "PWD", rc == 0 ? buf : "", true );

    /* APPPATH */
    if ( appname != NULL && rc == 0 )
    {
        bytes = string_size ( appname );
        value = string_rchr ( appname, bytes, '/' );
        if ( value == NULL )
            value = string_rchr ( appname, bytes, '\\' );
        if ( value != NULL )
            bytes = appname + bytes - value;
        rc = KDirectoryResolvePath ( cwd, true, buf, sizeof buf, "%s", appname);
        if ( rc == 0 && bytes != 0 )
            buf [ string_size(buf) - bytes ] = 0;    
        update_node(self, "APPPATH", rc == 0 ? buf : "", true );
    }

    /* APPNAME */
    rc = LogAppName(buf, sizeof(buf), &bytes);
    if ( rc == 0 )
        buf [ bytes ] = 0;
    update_node(self, "APPNAME", rc == 0 ? buf : "", true );

    /* Environment variables */
    /* some of the variables may be undefined, create nodes with empty values for them */
#define DEFINE_ENV(name)                                         \
    value=getenv(name);                                          \
    update_node(self, name, value == NULL ? "" : value, true )

    DEFINE_ENV("HOST");
    DEFINE_ENV("USER");
    value = getenv("HOME");
    if (value == NULL) 
    {   /* apparently on Windows, use USERPROFILE */
        value = getenv("USERPROFILE");
    }
    if (value == NULL)
    {
        update_node(self, "HOME", "", true );
        update_node(self, "NCBI_HOME", "", false );
        update_node(self, "NCBI_SETTINGS", "", false );
    }
    else
    {
        rc = KDirectoryResolvePath(cwd, true, buf, sizeof(buf), value);
        if (rc == 0)
        {
            size_t bytes2;
            char buf2 [ 4096 ];
        
            update_node(self, "HOME", buf, true );
            
            string_printf(buf2, sizeof(buf2), &bytes2, "%s/.ncbi", buf);
            update_node(self, "NCBI_HOME", buf2, false );    /* can be overridden by the user */ 
            
            string_printf(buf2, sizeof(buf2), &bytes2, "%s/.ncbi/%s", buf, MAGIC_LEAF_NAME);
            update_node(self, "NCBI_SETTINGS", buf2, false );    /* can be overridden by the user */ 
        }
        else
            LOGERR (klogErr, rc, "Unable to create a config item for $HOME");
    }

    DEFINE_ENV("VDB_ROOT");
    DEFINE_ENV("VDB_CONFIG");
#undef DEFINE_ENV

    KDirectoryRelease ( cwd );
}

static
rc_t KConfigFill ( KConfig * self, const KDirectory * cfgdir, const char *appname, bool local)
{
    KConfigNode * root;
    String empty;
    rc_t rc;

    CONST_STRING ( & empty, "" );

    rc = KConfigNodeMake ( & root, & empty );
    if (rc == 0)
    {
        KConfigInit ( self, root );
        add_predefined_nodes ( self, appname );
        load_config_files ( self, cfgdir );
    }
    return rc;
}


extern rc_t ReportKfg ( const ReportFuncs *f, uint32_t indent );

/* "cfg" [ OUT ] - return parameter for mgr
   if ("local" == true) do not initialize G_kfg */
static
rc_t KConfigMakeImpl ( KConfig **cfg, const KDirectory * cfgdir, bool local )
{
    rc_t rc;
    const char *appname = NULL;

    static bool latch;
    if ( ! latch )
    {
        appname = ReportInitConfig ( ReportKfg );
        latch = true;
    }

    if ( cfg == NULL )
        rc = RC ( rcKFG, rcMgr, rcCreating, rcParam, rcNull );
    else
    {
        KConfig *mgr = calloc ( 1, sizeof * mgr );
        if ( mgr == NULL )
            rc = RC ( rcKFG, rcMgr, rcCreating, rcMemory, rcExhausted );
        else
        {
            rc = KConfigFill (mgr, cfgdir, appname, local);

            mgr -> initialized = true;
            if ( rc == 0 )
            {
                * cfg = mgr;
                return 0;
            }

            KConfigWhack ( mgr );
        }

        * cfg = NULL;
    }

    return rc;
}

/* call KConfigMake; do not initialize G_kfg */
LIB_EXPORT rc_t CC KConfigMakeLocal ( KConfig **cfg, const KDirectory * cfgdir )
{   return KConfigMakeImpl(cfg, cfgdir, true); }

/* Make
 *  create a process-global configuration manager
 *
 *  "cfg" [ OUT ] - return parameter for mgr
 */
LIB_EXPORT rc_t CC KConfigMake ( KConfig **cfg, const KDirectory * cfgdir )
{   return KConfigMakeImpl(cfg, cfgdir, false); }

/*--------------------------------------------------------------------------
 * KNamelist
 */
typedef struct KfgConfigNamelist KfgConfigNamelist;
struct KfgConfigNamelist
{
    KNamelist dad;
    size_t count;
    const char *namelist [ 1 ];
};
 
/* Whack
 */
static
rc_t CC KfgConfigNamelistWhack ( KfgConfigNamelist *self )
{
    free ( self );
    return 0;
}
 
/* Count
 */
static
rc_t CC KfgConfigNamelistCount ( const KfgConfigNamelist *self,
uint32_t *count )
{
    * count = ( uint32_t ) self -> count;
    return 0;
}
 
/* Get
 */
static
rc_t CC KfgConfigNamelistGet ( const KfgConfigNamelist *self,
    uint32_t idx, const char **name )
{
    if ( ( size_t ) idx >= self -> count )
        return RC ( rcDB, rcNamelist, rcAccessing, rcParam, rcInvalid );
    * name = self -> namelist [ idx ];
    return 0;
}
 
/* Make
 */
static KNamelist_vt_v1 vtKfgConfigNamelist =
{
    /* version 1.0 */
    1, 0,

    /* start minor version 0 methods */
    KfgConfigNamelistWhack,
    KfgConfigNamelistCount,
    KfgConfigNamelistGet
    /* end minor version 0 methods */
};
 
 static
 rc_t KfgConfigNamelistMake ( KNamelist **names, uint32_t count )
 {
     rc_t rc;
     KfgConfigNamelist *self = malloc ( sizeof * self -
         sizeof self -> namelist + count * sizeof self -> namelist [ 0 ] );
     if ( self == NULL )
         rc = RC ( rcKFG, rcMetadata, rcListing, rcMemory, rcExhausted );
     else
     {
         self -> count = 0;
         
         rc = KNamelistInit ( & self -> dad,
             ( const KNamelist_vt* ) & vtKfgConfigNamelist );
         if ( rc == 0 )
         {
             * names = & self -> dad;
             return 0;
         }
         
         free ( self );
     }
 
     return rc;
 }
 
/* List
 *  create metadata node listings
 */
static
void CC BSTNodeCount ( BSTNode *n, void *data )
{
    * ( uint32_t* ) data += 1;
}

static
void CC KConfigNodeGrabName ( BSTNode *n, void *data )
{
    KfgConfigNamelist *list = data;
    list -> namelist [ list -> count ++ ]
        = ( ( const KConfigNode* ) n ) -> name . addr;
}

/* ListChildren
 *  list all named children
 */
LIB_EXPORT rc_t CC KConfigNodeListChildren ( const KConfigNode *self,
    KNamelist **names )
{
    if ( names == NULL )
        return RC ( rcKFG, rcNode, rcListing, rcParam, rcNull );

    * names = NULL;

    if ( self != NULL )
    {
        rc_t rc;

        uint32_t count = 0;
        BSTreeForEach ( & self -> children, 0, BSTNodeCount, & count );

        rc = KfgConfigNamelistMake ( names, count );
        if ( rc == 0 )
            BSTreeForEach
                ( & self -> children, 0, KConfigNodeGrabName, * names );

        return rc;
    }

    return RC ( rcKFG, rcNode, rcListing, rcSelf, rcNull );
}

static
void CC KConfigGrabName ( BSTNode *n, void *data )
{
    KfgConfigNamelist *list = data;
    list -> namelist [ list -> count ++ ]
        = ( ( const KConfigIncluded* ) n ) -> path;
}

/* ListIncluded
 *  list all included files
 */
LIB_EXPORT rc_t CC KConfigListIncluded ( const KConfig *self,
    KNamelist **names )
{
    if ( names == NULL )
        return RC ( rcKFG, rcMgr, rcListing, rcParam, rcNull );

    * names = NULL;

    if ( self != NULL )
    {
        rc_t rc;

        uint32_t count = 0;
        BSTreeForEach ( & self -> included, 0, BSTNodeCount, & count );

        rc = KfgConfigNamelistMake ( names, count );
        if ( rc == 0 )
            BSTreeForEach
                ( & self -> included, 0, KConfigGrabName, * names );

        return rc;
    }

    return RC ( rcKFG, rcMgr, rcListing, rcSelf, rcNull );
}

/************** Internal node-reading helpers *************/

/* ReadNodeValueFixed
 * copy the node's value into the caller's fixed size buffer and 0-terminate
 */
static rc_t ReadNodeValueFixed ( const KConfigNode* self, char *buf, size_t bsize )
{
    size_t num_read, remaining;
    rc_t rc = KConfigNodeRead ( self, 0, buf, bsize - 1, &num_read, &remaining);
    if (rc == 0)
    {
        if (remaining != 0)
            rc = RC ( rcKFG, rcNode, rcReading, rcBuffer, rcInsufficient );
        else
            buf[num_read] = '\0';
    }        
    return rc;
}

/**********************************************************/
 
/* ReadBool
 *  read a boolean node value
 *
 * self [ IN ] - KConfigNode object
 * result [ OUT ] - return value (true if "TRUE", false if "FALSE"; rc != 0 if neither)
 *
 */
LIB_EXPORT rc_t CC KConfigNodeReadBool ( const KConfigNode *self, bool *result )
{
    rc_t rc;

    if ( result == NULL )
        rc = RC ( rcKFG, rcNode, rcReading, rcParam, rcNull );
    else
    {
        * result = false;

        if ( self == NULL )
            rc = RC ( rcKFG, rcNode, rcReading, rcSelf, rcNull );
        else
        {
            char buf [ 6 ];
            rc = ReadNodeValueFixed ( self, buf, sizeof buf );
            if ( rc == 0 )
            {
                switch ( tolower ( buf [ 0 ] ) )
                {
                case 'f':
                    if ( buf [ 1 ] == 0 )
                        return 0;
                    if ( strncasecmp ( buf, "false", sizeof buf ) == 0 )
                        return 0;
                    break;

                case 'n':
                    if ( buf [ 1 ] == 0 )
                        return 0;
                    if ( strncasecmp ( buf, "no", sizeof buf ) == 0 )
                        return 0;
                    break;

                case 't':
                    * result = true;
                    if ( buf [ 1 ] == 0 )
                        return 0;
                    if ( strncasecmp ( buf, "true", sizeof buf ) == 0 )
                        return 0;
                    break;

                case 'y':
                    * result = true;
                    if ( buf [ 1 ] == 0 )
                        return 0;
                    if ( strncasecmp ( buf, "yes", sizeof buf ) == 0 )
                        return 0;
                    break;
                }

                * result = false;
                rc = RC ( rcKFG, rcNode, rcReading, rcFormat, rcIncorrect );
            }
        }
    }

    return rc;
}

/* ReadI64
 *  read an integer node value
 *
 * self [ IN ] - KConfigNode object
 * result [ OUT ] - return value (rc != 0 if cannot be converted)
 *
 */
LIB_EXPORT rc_t CC KConfigNodeReadI64 ( const KConfigNode *self, int64_t *result )
{
    rc_t rc;

    if ( result == NULL )
        rc = RC ( rcKFG, rcNode, rcReading, rcParam, rcNull );
    else
    {
        * result = 0;

        if ( self == NULL )
            rc = RC ( rcKFG, rcNode, rcReading, rcSelf, rcNull );
        else
        {
            /* allow for leading zeros */
            char buf [ 256 ];
    
            rc = ReadNodeValueFixed(self, buf, sizeof(buf));
            if (rc == 0)
            {
                char* endptr;
                int64_t res = strtoi64(buf, &endptr, 0);
                if ( *endptr == '\0' )
                    *result = res;
                else
                    rc = RC(rcKFG, rcNode, rcReading, rcFormat, rcIncorrect);
            }
        }
    }
    return rc;
}

/* ReadU64
 *  read an unsiged node value
 *
 * self [ IN ] - KConfigNode object
 * result [ OUT ] - return value (rc != 0 if cannot be converted)
 *
 */
LIB_EXPORT rc_t CC KConfigNodeReadU64 ( const KConfigNode *self, uint64_t* result )
{
    rc_t rc;

    if ( result == NULL )
        rc = RC ( rcKFG, rcNode, rcReading, rcParam, rcNull );
    else
    {
        * result = 0;

        if ( self == NULL )
            rc = RC ( rcKFG, rcNode, rcReading, rcSelf, rcNull );
        else
        {
            /* allow for leading zeros */
            char buf [ 256 ];
    
            rc = ReadNodeValueFixed(self, buf, sizeof(buf));
            if (rc == 0)
            {
                char* endptr;
                int64_t res = strtou64(buf, &endptr, 0);
                if ( *endptr == '\0' )
                    *result = res;
                else
                    rc = RC(rcKFG, rcNode, rcReading, rcFormat, rcIncorrect);
            }
        }
    }
    return rc;
}

/* ReadF64
 *  read a floating point node value
 *
 * self [ IN ] - KConfigNode object
 * result [ OUT ] - return value (rc != 0 if cannot be converted)
 *
 */
LIB_EXPORT rc_t CC KConfigNodeReadF64( const KConfigNode *self, double* result )
{
    rc_t rc;

    if ( result == NULL )
        rc = RC ( rcKFG, rcNode, rcReading, rcParam, rcNull );
    else
    {
        * result = 0.0;

        if ( self == NULL )
            rc = RC ( rcKFG, rcNode, rcReading, rcSelf, rcNull );
        else
        {
            /* allow for leading zeros, trailing digits */
            char buf [ 256 ];

            rc = ReadNodeValueFixed(self, buf, sizeof(buf));
            if (rc == 0)
            {
                char* endptr;
                double res = strtod(buf, &endptr);
                if ( *endptr == '\0' )
                    *result = res;
                else
                    rc = RC(rcKFG, rcNode, rcReading, rcFormat, rcIncorrect);
            }
        }
    }
    return rc;
}

#if 0
/*** NB - code needs to be located in VFS ***/
/* ReadNodeValueFixed
 * Allocate a character buffer of sufficient size, copy the node's value into it, 0-terminate. Caller deallocates.
 */
static rc_t ReadNodeValueVariable( const KConfigNode* self, char** buf )
{
    size_t num_read, to_read;
    /* first we ask about the size to be read */
    rc_t rc = KConfigNodeRead ( self, 0, NULL, 0, &num_read, &to_read );
    if ( rc == 0 )
    {
        char* value = malloc( to_read + 1 );
        if ( value )
        {
            rc = ReadNodeValueFixed( self, value, to_read + 1 );
            if ( rc == 0 )
                *buf = value;
            else
                free (value);
        }
        else
            rc = RC( rcKFG, rcNode, rcReading, rcMemory, rcExhausted );
    }   
    return rc;
}

/* ReadFloat
 *  read a VPath node value
 *
 * self [ IN ] - KConfigNode object
 * result [ OUT ] - return value (rc != 0 if cannot be converted)
 *
 */
LIB_EXPORT rc_t CC KConfigNodeReadVPath ( const KConfigNode *self, struct VPath** result )
{
    rc_t rc;

    if ( result == NULL )
        rc = RC ( rcKFG, rcNode, rcReading, rcParam, rcNull );
    else
    {
        * result = NULL;

        if ( self == NULL )
            rc = RC ( rcKFG, rcNode, rcReading, rcSelf, rcNull );
        else
        {
            char* buf;
            rc = ReadNodeValueVariable ( self, &buf );
            if ( rc == 0)
            {
                rc = VPathMake(result, buf);
                free(buf);
            }
        }
    }
    return rc;
}
#endif

/* ReadString
 *  read a String node value
 *
 * self [ IN ] - KConfigNode object
 * result [ OUT ] - return value (rc != 0 if cannot be converted)
 *
 */
LIB_EXPORT rc_t CC KConfigNodeReadString ( const KConfigNode *self, String** result )
{
    rc_t rc;

    if ( result == NULL )
        rc = RC ( rcKFG, rcNode, rcReading, rcParam, rcNull );
    else
    {
        * result = NULL;

        if ( self == NULL )
            rc = RC ( rcKFG, rcNode, rcReading, rcSelf, rcNull );
        else
        {
            size_t num_read, to_read;

            /* first we ask about the size to be read */
            rc = KConfigNodeRead ( self, 0, NULL, 0, &num_read, &to_read );
            if ( rc == 0 )
            {
                String *value = malloc ( sizeof * value + to_read + 1 );
                if ( value == NULL )
                    rc = RC( rcKFG, rcNode, rcReading, rcMemory, rcExhausted );
                else
                {
                    /* TBD - this is broken for non-ascii strings
                       much better to be WITHIN the config.c implementation
                       and reach into the node value directly! */
                    StringInit ( value, (char*)( value + 1 ), to_read, to_read + 1 );
                    rc = ReadNodeValueFixed(self, (char*)value->addr, to_read + 1);
                    if ( rc == 0 )
                        *result = value;
                    else
                    {
                        rc = RC(rcKFG, rcNode, rcReading, rcFormat, rcIncorrect);
                        free ( value );
                    }
                }
            }
        }
    }    
    return rc;
}

/* this macro wraps a call to KConfigNodeGetXXX in a node-accessing
   code to implement the corresponding KConfigGetXXX function */
#define NODE_TO_CONFIG_ACCESSOR(fn) \
    const KConfigNode* node;                                \
    rc_t rc = KConfigOpenNodeRead ( self, &node, path );    \
    if ( rc == 0)                                           \
    {                                                       \
        rc_t rc2;                                           \
        rc = fn(node, result);                              \
        rc2 = KConfigNodeRelease(node);                     \
        if (rc == 0)                                        \
            rc = rc2;                                       \
    }                                                       \
    return rc;                                              
    
/* THESE FUNCTIONS ARE PROTECTED AGAINST BAD "self" AND "path"
   BY KConfigOpenNodeRead, BUT THE CONVERSION FUNCTIONS ARE NOT */
LIB_EXPORT rc_t CC KConfigReadBool ( const KConfig* self, const char* path, bool* result )
{
    NODE_TO_CONFIG_ACCESSOR(KConfigNodeReadBool);
}    
LIB_EXPORT rc_t CC KConfigReadI64 ( const KConfig* self, const char* path, int64_t* result )
{
    NODE_TO_CONFIG_ACCESSOR(KConfigNodeReadI64);
}
LIB_EXPORT rc_t CC KConfigReadU64 ( const KConfig* self, const char* path, uint64_t* result )
{
    NODE_TO_CONFIG_ACCESSOR(KConfigNodeReadU64);
}
LIB_EXPORT rc_t CC KConfigReadF64 ( const KConfig* self, const char* path, double* result )
{
    NODE_TO_CONFIG_ACCESSOR(KConfigNodeReadF64);
}
#if 0
LIB_EXPORT rc_t CC KConfigReadVPath ( const KConfig* self, const char* path, struct VPath** result )
{
    NODE_TO_CONFIG_ACCESSOR(KConfigNodeReadVPath);
}
#endif
LIB_EXPORT rc_t CC KConfigReadString ( const KConfig* self, const char* path, struct String** result )
{
    NODE_TO_CONFIG_ACCESSOR(KConfigNodeReadString);
}

#define DISP_RC(rc, msg) (void)((rc == 0) ? 0 : LOGERR(klogInt, rc, msg))
#define DISP_RC2(rc, name, msg) (void)((rc == 0) ? 0 : \
    PLOGERR(klogInt, (klogInt, rc, \
        "$(name): $(msg)", "name=%s,msg=%s", name, msg)))

static rc_t KConfigNodeReadData(const KConfigNode* self,
    char* buf, size_t blen, size_t* num_read)
{
    rc_t rc = 0;
    size_t remaining = 0;

    assert(buf && blen && num_read);

    rc = KConfigNodeRead(self, 0, buf, blen, num_read, &remaining);

    assert(remaining == 0); /* TODO overflow check */
    assert(*num_read <= blen);

    return rc;
}

static rc_t printIndent(int indent) {
    rc_t rc = 0;

    int i = 0;
    for (i = 0; i < indent * 2; ++i) {
        rc_t rc2 = OUTMSG((" "));
        if (rc == 0 && rc2 != 0) {
            rc = rc2;
        }
    }

    return rc;
}

static
rc_t KConfigNodePrint(const KConfigNode* self, int indent, const char* root)
{
    rc_t rc = 0;
    KNamelist* names = NULL;
    uint32_t count = 0;
    uint32_t i = 0;
    char data[512] = "";
    size_t num_data = 0;
    assert(self);

    printIndent(indent);
    OUTMSG(("<%s>", root));

    if (rc == 0) {
        rc_t rc = KConfigNodeReadData(self, data, sizeof data, &num_data);
        DISP_RC2(rc, "KConfigNodeReadData()", root);
        if (rc == 0 && num_data > 0) {
            OUTMSG(("%.*s", (int)num_data, data));
        }
    }

    if (rc == 0) {
        rc = KConfigNodeListChild(self, &names);
        DISP_RC2(rc, "KConfigNodeListChild()", root);
    }
    if (rc == 0) {
        rc = KNamelistCount(names, &count);
        DISP_RC2(rc, "KNamelistCount()", root);
    }


    if (rc == 0) {
        if (count > 0) {
            OUTMSG(("\n"));
        }
        for (i = 0; i < count; ++i) {
            const char* name = NULL;
            const KConfigNode* node = NULL;
            if (rc == 0) {
                rc = KNamelistGet(names, i, &name);
                DISP_RC2(rc, "KNamelistGet()", root);
            }
            if (rc == 0) {
                rc = KConfigNodeOpenNodeRead(self, &node, name);
                DISP_RC2(rc, "KConfigNodeOpenNodeRead()", name);
            }
            if (rc == 0) {
                KConfigNodePrint(node, indent + 1, name);
            }
            KConfigNodeRelease(node);
        }
    }

    if (count > 0) {
        printIndent(indent);
    }
    OUTMSG(("</%s>\n", root));

    KNamelistRelease(names);

    return rc;
}

LIB_EXPORT rc_t CC KConfigPrint(const KConfig* self) {
    rc_t rc = 0;

    const char root[] = "Config";

    if (self == NULL) {
        OUTMSG(("<%s>", root));
        OUTMSG(("KConfigPrint(const KConfig* self = NULL)\n"));
        OUTMSG(("</%s>\n", root));
    }
    else {
        const KConfigNode* node = NULL;
        if (rc == 0) {
            rc = KConfigOpenNodeRead(self, &node, "/");
            DISP_RC2(rc, "KConfigOpenNodeRead()", "/");
        }
        if (rc == 0) {
            KConfigNodePrint(node, 0, "Config");
        }
        KConfigNodeRelease(node);
    }

    return rc;
}

LIB_EXPORT void CC KConfigDisableUserSettings ( void )
{
    s_disable_user_settings = true;
}
