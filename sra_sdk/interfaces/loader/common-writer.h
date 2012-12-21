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

#ifndef _h_common_writer_
#define _h_common_writer_

#ifndef _h_klib_defs_
#include <klib/defs.h>
#endif

#ifndef _h_mmarray_
#include <loader/mmarray.h>
#endif

#ifndef _h_insdc_insdc_
#include <insdc/insdc.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*--------------------------------------------------------------------------
 * forwards
 */

struct VDBManager;
struct VDatabase;
struct KMemBank;
struct KBTree;
struct KLoadProgressbar;
struct ReaderFile;
struct CommonWriter;
struct SequenceWriter;
struct AlignmentWriter;
struct Reference;

/*--------------------------------------------------------------------------
 * CommonWriterSettings
 */
enum LoaderModes {
    mode_Archive,
    mode_Analysis
};

typedef struct CommonWriterSettings
{
    char const *inpath;
    char const *outpath;
    char const *tmpfs;
    
    struct KFile *noMatchLog;
    
    char const *schemaPath;
    char const *schemaIncludePath;
    
    char const *refXRefPath;
    
    char const *QualQuantizer;
    
    char const *refFilter;

    char const** refFiles; /* NULL-terminated array pointing to argv */
    
    char const *headerText;
    
    uint64_t maxAlignCount;
    size_t cache_size;

    unsigned errCount;
    unsigned maxErrCount;
    unsigned maxWarnCount_NoMatch;
    unsigned maxWarnCount_DupConflict;
    unsigned pid;
    unsigned minMatchCount; /* minimum number of matches to count as an alignment */
    int minMapQual;
    enum LoaderModes mode;
    uint32_t maxSeqLen;
    bool omit_aligned_reads;
    bool omit_reference_reads;
    bool no_real_output;
    bool expectUnsorted;
    bool noVerifyReferences;
    bool onlyVerifyReferences;
    bool useQUAL;
    bool limit2config;
    bool editAlignedQual;
    bool keepMismatchQual;
    bool acceptBadDups; /* accept spots with inconsistent PCR duplicate flags */
    bool acceptNoMatch; /* accept without any matching bases */
    bool noSpotAssembly;
    uint8_t alignedQualValue;
    bool allUnaligned; /* treat all records as unaligned */
    bool noColorSpace;
    bool noSecondary;
    bool hasTI;
    bool acceptHardClip;
} CommonWriterSettings;

/*--------------------------------------------------------------------------
 * SpotAssembler
 */

#define FRAG_CHUNK_SIZE (128)

typedef struct SpotAssembler {
    const struct KLoadProgressbar *progress[4];
    struct KBTree *key2id[NUM_ID_SPACES];
    char *key2id_names;
    struct MMArray *id2value;
    struct KMemBank *fragsBoth; /*** mate will be there soon ***/
    struct KMemBank *fragsOne;  /*** mate may not be found soon or even show up ***/
    int64_t spotId;
    int64_t primaryId;
    int64_t secondId;
    uint64_t alignCount;
    
    uint32_t idCount[NUM_ID_SPACES];
    uint32_t key2id_hash[NUM_ID_SPACES];
    
    unsigned key2id_max;
    unsigned key2id_name_max;
    unsigned key2id_name_alloc;
    unsigned key2id_count;
    
    unsigned key2id_name[NUM_ID_SPACES];
    /* this array is kept in name order */
    /* this maps the names to key2id and idCount */
    unsigned key2id_oid[NUM_ID_SPACES];
    
    unsigned pass;
    bool isColorSpace;
    
} SpotAssembler;

rc_t SetupContext(const CommonWriterSettings* settings, SpotAssembler *ctx, unsigned numfiles);
void ContextReleaseMemBank(SpotAssembler *ctx);
void ContextRelease(SpotAssembler *ctx);

rc_t GetKeyID(CommonWriterSettings* settings, SpotAssembler *const ctx, uint64_t *const rslt, bool *const wasInserted, char const key[], char const name[], unsigned const namelen);

rc_t WriteSoloFragments(const CommonWriterSettings* settings, SpotAssembler *ctx, struct SequenceWriter *seq);
rc_t AlignmentUpdateSpotInfo(SpotAssembler *ctx, struct AlignmentWriter *align);
rc_t SequenceUpdateAlignInfo(SpotAssembler *ctx, struct SequenceWriter *seq);

/*--------------------------------------------------------------------------
 * ctx_value_t, FragmentInfo
 */
typedef struct {
    uint32_t primaryId[2];
    uint32_t spotId;
    uint32_t fragmentId;
    uint8_t  platform;
    uint8_t  pId_ext[2];
    uint8_t  spotId_ext;
    uint8_t  alignmentCount[2]; /* 0..254; 254: saturated max; 255: special meaning "too many" */
    uint8_t  unmated: 1,
             pcr_dup: 1,
             has_a_read: 1,
             unaligned_1: 1,
             unaligned_2: 1;
} ctx_value_t;

#define CTX_VALUE_SET_P_ID(O,N,V) do { int64_t tv = (V); (O).primaryId[N] = (uint32_t)tv; (O).pId_ext[N] = tv >> 32; } while(0);
#define CTX_VALUE_GET_P_ID(O,N) ((((int64_t)((O).pId_ext[N])) << 32) | (O).primaryId[N])

#define CTX_VALUE_SET_S_ID(O,V) do { int64_t tv = (V); (O).spotId = (uint32_t)tv; (O).spotId_ext = tv >> 32; } while(0);
#define CTX_VALUE_GET_S_ID(O) ((((int64_t)(O).spotId_ext) << 32) | (O).spotId)

typedef struct FragmentInfo {
    uint64_t ti;
    uint32_t readlen;
    uint8_t  aligned;
    uint8_t  is_bad;
    uint8_t  orientation;
    uint8_t  otherReadNo;
    uint8_t  sglen;
    uint8_t  cskey;
} FragmentInfo;

rc_t OpenKBTree(const CommonWriterSettings* settings, struct KBTree **const rslt, unsigned n, unsigned max);
rc_t GetKeyIDOld(const CommonWriterSettings* settings, SpotAssembler* const ctx, uint64_t *const rslt, bool *const wasInserted, char const key[], char const name[], unsigned const namelen);

void COPY_QUAL(uint8_t D[], uint8_t const S[], unsigned const L, bool const R);
void COPY_READ(INSDC_dna_text D[], INSDC_dna_text const S[], unsigned const L, bool const R);

bool platform_cmp(char const platform[], char const test[]);
rc_t CheckLimitAndLogError(CommonWriterSettings* settings);
void RecordNoMatch(const CommonWriterSettings* settings, char const readName[], char const refName[], uint32_t const refPos);
rc_t LogDupConflict(CommonWriterSettings* settings, char const readName[]);
rc_t LogNoMatch(CommonWriterSettings* settings, char const readName[], char const refName[], unsigned rpos, unsigned matches);
void EditAlignedQualities(const CommonWriterSettings* settings, uint8_t qual[], bool const hasMismatch[], unsigned readlen);
void EditUnalignedQualities(uint8_t qual[], bool const hasMismatch[], unsigned readlen);

/*--------------------------------------------------------------------------
 * CommonWriter
 */
typedef struct CommonWriter {
    CommonWriterSettings settings;
    SpotAssembler ctx;
    struct Reference* ref;
    struct SequenceWriter* seq;
    struct AlignmentWriter* align;
} CommonWriter;

rc_t CommonWriterInit(CommonWriter* self, struct VDBManager *mgr, struct VDatabase *db, const CommonWriterSettings* settings);

rc_t CommonWriterArchive(CommonWriter* self, const struct ReaderFile *, bool *had_alignments, bool *had_sequences);

rc_t CommonWriterWhack(CommonWriter* self, bool const commit);

/*TODO"remove*/
rc_t ArchiveFile(const struct ReaderFile *reader, 
                 CommonWriterSettings* G,
                 struct SpotAssembler *ctx, 
                 struct Reference *ref, 
                 struct SequenceWriter *seq, 
                 struct AlignmentWriter *align,
                 bool *had_alignments, 
                 bool *had_sequences);

#ifdef __cplusplus
}
#endif

#endif /* _h_common_writer_ */
