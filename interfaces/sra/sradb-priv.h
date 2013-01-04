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

#ifndef _h_sra_sradb_priv_
#define _h_sra_sradb_priv_

#ifndef _h_sra_sradb_
#include <sra/sradb.h>
#endif

#ifndef _h_sra_srapath_
#include <sra/srapath.h>
#endif

#ifndef _h_sra_path_extern_
#include <sra/path-extern.h>
#endif

#ifndef _h_sra_sch_extern_
#include <sra/sch-extern.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif


/*--------------------------------------------------------------------------
 * forwards
 */
struct KFile;
struct KDirectory;
struct KDBManager;
struct KTable;
struct VDBManager;
struct VTable;
struct VSchema;
struct SRAPath;


/*--------------------------------------------------------------------------
 * SRAMgr
 *  opaque handle to SRA library
 */


/* GetSRAPath
 *  retrieve a reference to SRAPath object in use - may be NULL
 * UseSRAPath
 *  provide an SRAPath object for use - attaches a new reference
 */
SRA_EXTERN rc_t CC SRAMgrGetSRAPath ( const SRAMgr *self, struct SRAPath **path );
SRA_EXTERN rc_t CC SRAMgrUseSRAPath ( const SRAMgr *self, struct SRAPath *path );

/* FlushPath
 *  flushes cached files under given path
 *
 *  "path" [ IN ] [ IN ] - identifies path to flush from cache
 */
SRA_EXTERN rc_t CC SRAMgrFlushRepPath ( struct SRAMgr const *self, const char *path );
SRA_EXTERN rc_t CC SRAMgrFlushVolPath ( struct SRAMgr const *self, const char *path );

/* FlushRun
 *  flushes cached files on a given run
 *
 *  "accession" [ IN ] - NUL terminated accession key
 */
SRA_EXTERN rc_t CC SRAMgrFlushRun ( struct SRAMgr const *self, const char *accession );

/* RunBGTasks
 *  perform single pass of garbage collection tasks and exit.
 *  also retrieves and processes update messages.
 */
SRA_EXTERN rc_t CC SRAMgrRunBGTasks ( struct SRAMgr const *self );

/* GetVDBManager
 *  returns a new reference to VDBManager used by SRAMgr
 */
SRA_EXTERN rc_t CC SRAMgrGetVDBManagerRead ( const SRAMgr *self, struct VDBManager const **vmgr );
SRA_EXTERN rc_t CC SRAMgrGetVDBManagerUpdate ( SRAMgr *self, struct VDBManager **vmgr );

/* GetKDBManager
 *  returns a new reference to KDBManager used indirectly by SRAMgr
 */
SRA_EXTERN rc_t CC SRAMgrGetKDBManagerRead ( const SRAMgr *self, struct KDBManager const **kmgr );
SRA_EXTERN rc_t CC SRAMgrGetKDBManagerUpdate ( SRAMgr *self, struct KDBManager **kmgr );

/* ModDate
 *  return a modification timestamp for table
 */
SRA_EXTERN rc_t CC SRAMgrVGetTableModDate ( const SRAMgr *self,
    KTime_t *mtime, const char *spec, va_list args );

SRA_EXTERN rc_t CC SRAMgrGetTableModDate ( const SRAMgr *self,
    KTime_t *mtime, const char *spec, ... );

/* ConfigReload
 *  update SRAPath object
 */
SRA_EXTERN rc_t CC SRAMgrConfigReload( const SRAMgr *self, struct KDirectory const *wd );


/*--------------------------------------------------------------------------
 * SRATable
 */

/* OpenAltTableRead
 *  opens a table within a database structure with a specific name
 */
SRA_EXTERN rc_t CC SRAMgrOpenAltTableRead ( const SRAMgr *self,
    const SRATable **tbl, const char *altname, const char *spec, ... );

/* GetVTable
 *  returns a new reference to underlying VTable
 */
SRA_EXTERN rc_t CC SRATableGetVTableRead ( const SRATable *self, struct VTable const **vtbl );
SRA_EXTERN rc_t CC SRATableGetVTableUpdate ( SRATable *self, struct VTable **vtbl );

/* GetKTable
 *  returns a new reference to underlying KTable
 */
SRA_EXTERN rc_t CC SRATableGetKTableRead ( const SRATable *self, struct KTable const **ktbl );
SRA_EXTERN rc_t CC SRATableGetKTableUpdate ( SRATable *self, struct KTable **ktbl );


/* MakeSingleFileArchive
 *  makes a single-file-archive file from an SRA table
 *
 *  contents are ordered by frequency and necessity of access
 *
 *  "lightweight" [ IN ] - when true, include only those components
 *  required for read and quality operations.
 *
 *  "ext" [OUT,NULL] - optional file name extension to use for file
 */
SRA_EXTERN rc_t CC SRATableMakeSingleFileArchive ( const SRATable *self,
    struct KFile const **sfa, bool lightweight, const char** ext );

/* SingleFileArchiveExt
 *  retrieve archive extension based on object in the spec
 */
SRA_EXTERN rc_t CC SRAMgrSingleFileArchiveExt(const SRAMgr *self,
    const char* spec, const bool lightweight, const char** ext);
/*--------------------------------------------------------------------------
 * SRAPath
 */

/* FindWithRepLen
 *  finds location of run within rep-server/volume matrix
 *  returns length of rep-server portion
 */
SRA_EXTERN rc_t CC SRAPathFindWithRepLen ( struct SRAPath const *self,
    const char *accession, char *path, size_t path_max, size_t *rep_len );



/*--------------------------------------------------------------------------
 * SRASchema
 */

SRA_SCH_EXTERN rc_t CC SRASchemaMake ( struct VSchema **schema, struct VDBManager const *mgr );


#if 0

/*--------------------------------------------------------------------------
 * SRATableData  - DEPRECATED
 *  a collection of spots with several data series, minimally including
 *  base or color calls and their quality ( confidence ) values, and
 *  optionally signal-related values ( signal, intensity, noise, ... ).
 */
union NucStrstr;

typedef struct SRASpotStructure SRASpotStructure;
struct SRASpotStructure
{ 
    /* preformatted query expression
       for fixed_seq when search is needed */
    union NucStrstr *q_str;

    /* read of fixed len if != 0
       either teminated by fixed_seq or by the end */
    uint16_t fixed_len;

    /* SRAReadTypes */
	uint8_t read_type;

    /* colorspace key */
    char cs_key;

    char fixed_seq [ 1024 ];

    /* label for the read */
    char read_label [ 54 ];

};

typedef struct SRASpotCoord SRASpotCoord;
struct SRASpotCoord
{
    uint32_t x, y, tile;
	uint32_t  lane;
	spotid_t id;

    /* prefix part of spotname */
	uint32_t platename_len;
	char spotname [ 1024 ];

};

typedef struct SRATableData SRATableData;
struct SRATableData
{
	uint64_t base_count;
	uint64_t spot_count;
	uint64_t bad_spot_count;
	spotid_t max_spotid;

    /* the spot is always fixed len read */
	uint32_t fixed_len;

    /* number of reads per spot */
	uint32_t num_reads;

    /* read mask containing bio reads */
	uint32_t read_mask_bio;

    /* read description */
	SRASpotStructure read_descr [ 32 ];
    uint16_t read_len [ 32 ];

    /* platform type and name */
	uint8_t platform;
	char platform_str [ 31 ];

	uint16_t prefix_len;

    /* spot coordinates */
	SRASpotCoord coord;

};

/* GetTableData
 *  returns a pointer to internal table data
 *  or NULL if "self" is invalid
 *
 * NB - THIS OBJECT IS NOT REFERENCE COUNTED
 */
SRA_EXTERN const SRATableData *CC SRATableGetTableData ( const SRATable *self );

#endif



#ifdef __cplusplus
}
#endif

#endif /* _h_sra_sradb_priv_ */
