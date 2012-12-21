/*==============================================================================
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

#include "shared.h"

#include <klib/defs.h>
#include <klib/callconv.h>

#include <klib/rc.h>
#include <klib/out.h>
#include <klib/log.h>
#include <klib/debug.h> /* DBGMSG */
#include <klib/status.h>
#include <klib/text.h>
#include <klib/printf.h>
#include <klib/namelist.h>

#include <kfs/defs.h>
#include <kfs/file.h>
#include <kfs/directory.h>
#include <kfs/sra.h>
#include <kfs/lockfile.h>
#include <kfs/buffile.h>
#include <vfs/manager.h>

#include <krypto/key.h>
#include <krypto/encfile.h>
#include <krypto/wgaencrypt.h>

#include <kapp/args.h>
#include <kapp/main.h>

#include <assert.h>
#include <string.h>
#include <stdint.h>


#define OPTION_FORCE   "force"
#define OPTION_SRA     "decrypt-sra-files"
#define ALIAS_FORCE    "f"
#define ALIAS_SRA      NULL

#define MY_MAX_PATH        4096


bool ForceFlag = false;
bool TmpFoundFlag = false;
bool UseStdin = false;
bool UseStdout = false;

/* for wga decrypt */
char Password [4096 + 2];
size_t PasswordSize;

/* for encfile encrypt/decrypt */
KKey Key;

const char * ForceUsage[] = 
{ "force overwrite of existing files", NULL };

/*
 * option  control flags
 */

static const char EncExt1[] = ".nenc";
static const char EncExt2[] = ".ncbi_enc";
static const char TmpExt[] = ".vdb-decrypt-tmp";
static const char TmpLockExt[] = ".vdb-decrypt-tmp.lock";

/* Usage
 */
rc_t CC UsageSummary (const char * progname)
{
    rc_t rc;
    {
        rc = KOutMsg (
            /*345679012345678901234567890123456789012345678901234567890123456789012345678*/
            "\n"
            "Usage:\n"
            "  %s [options] <source-file>\n"
            "  %s [options] <source-file> <destination-file>\n"
            "  %s [options] <source-file> <destination-directory>\n"
            "  %s [options] <directory>\n",
            progname, progname, progname, progname);
    }
#if DIRECTORY_TO_DIRECTORY_SUPPORTED
    if (rc == 0)
        rc = KOutMsg (
            "  %s [options] <source-directory> <destination-directory>\n",
            progname);
#endif
        if (rc == 0)
    {
        rc = KOutMsg (
            "\n"
            "Summary:\n"
            "  %scrypt a file or all the files (recursively) in a directory\n\n",
            De);
    }
    return rc;
}

rc_t CC Usage (const Args * args)
{
    const char * progname = UsageDefaultName;
    const char * fullpath = UsageDefaultName;
    char const *const file_crypt = de[0] == 'd'
                                 ? "file to decrypt"
                                 : "file to encrypt";
    char const *const dir_crypt  = de[0] == 'd'
                                 ? "directory to decrypt"
                                 : "directory to encrypt";
    char const *const pline[] = {
        file_crypt, NULL,
        "name of resulting file", NULL,
        "directory of resulting file", NULL,
        dir_crypt, NULL
    };

    rc_t rc, orc;

    if (args == NULL)
        rc = RC (rcApp, rcArgv, rcAccessing, rcSelf, rcNull);
    else
        rc = ArgsProgram (args, &fullpath, &progname);

    orc = UsageSummary (progname);
    if (rc == 0)
        rc = orc;

    KOutMsg ("Parameters:\n");
    HelpParamLine ("source-file"          , pline);
    HelpParamLine ("destination-file"     , pline + 2);
    HelpParamLine ("destination-directory", pline + 4);
    HelpParamLine ("directory"            , pline + 6);
    KOutMsg ("\nOptions:\n");
    HelpOptionLine (ALIAS_FORCE, OPTION_FORCE, NULL, ForceUsage);
    CryptOptionLines ();
    HelpOptionsStandard ();

    {   /* forcing editor alignment */
        /*   12345678901234567890123456789012345678901234567890123456789012345678901234567890*/
        KOutMsg (
            "\n"
            "Details:\n"
            "  With a single parameter, %scryptions are in place, replacing files with\n"
            "  new files that are %scrypted, unless the files are already %scrypted.\n"
            "\n", de, de, de);
        KOutMsg (
            "  With two parameters, %scrypted copies of files are created, without\n"
            "  changing the original files. If a file is already %scrypted, it will\n"
            "  only be copied.\n"
            "\n", de, de);
#if 0
        KOutMsg (
            "  When a single file is specified, it is replaced with its %scrypted copy.\n"
            "\n", de);
        KOutMsg (
            "  A single directory can be specified and all files within that directory\n"
            "  and all subdirectories will be replaced with %scrypted versions.\n"
            "\n", de);
        KOutMsg (
            "  Files that are already %scrypted will remain unchanged\n"
            "\n", de);
#endif
        if (de[0] == 'd') KOutMsg (
            "  NCBI Archive files that contain NCBI database objects will not be\n"
            "  decrypted unless the %s option is used. As these objects can be used without\n"
            "  decryption it is recommended they remain encrypted.\n"
            "\n", OPTION_SRA);
        KOutMsg (
            "  Hidden files (that begin with a '.') having the extension %s or\n"
            "  %s are considered temporary files used by %s and\n"
            "  are not %scrypted. If encountered during in-place %scryption, these files\n"
            "  will be considered busy and skipped, unless the --%s option was given.\n"
            "\n", TmpExt, TmpLockExt, progname, de, de, OPTION_FORCE);
        KOutMsg (
            "  In all other cases no file extensions are considered or changed.\n"
            "\n");
        KOutMsg (
            "  Missing directories in the destination path will be created.\n"
            "\n");
        KOutMsg (
            "  Already existing destination files will cause the program to end with\n"
            "  an error and will be left unchanged unless the --%s option is used to\n"
            "  force the files to be overwritten.\n"
            "\n", OPTION_FORCE);
        KOutMsg ("\n"
                 "Password:\n"
                 "  The password is not given on the command line. It must be in a\n"
                 "  a file that should be readable only by the user running the program.\n"
                 "\n"
                 "  The location of that file is looked for in these places, in this order:\n"
                 "    1. An environment variable VDB_PWFILE\n"
                 "    2. The VDB Configuration file option 'krypto/pwfile'\n"
                 "\n");
    }
    HelpVersion (fullpath, KAppVersion());

    return rc;
}





/*
 * determine the archive type for KFile f with pathname name
 *
 * This could be extended to handle tar files with a larger head size and
 * and some less simple checks for a tar header block at the start of
 * the file.
 */
ArcScheme ArchiveTypeCheck (const KFile * f)
{
    size_t num_read;
    rc_t rc;
    char head [128];

    rc = KFileReadAll (f, 0, head, sizeof head, &num_read);
    if (rc)
    {
        LOGERR (klogErr, rc, "Unable to read head of decrypted file");
        return arcError;
    }

    rc = KFileIsSRA (head, num_read);
    if (rc == 0)
        return arcSRAFile;

    return arcNone;
}


/*
 * Copy a file from a const KFile * to a KFile * with the paths for the two
 * for logging purposes
 *
 * return rc_t = 0 for success
 * return rc_t != 0 for failure
 */
rc_t CopyFile (const KFile * src, KFile * dst, const char * source, const char * dest)
{
    rc_t rc;
    uint8_t	buff	[256 * 1024];
    size_t	num_read;
    size_t      num_writ;
    uint64_t	pos;

    for (pos = 0; ; pos += num_read)
    {
        rc = Quitting ();
        if (rc)
        {
            LOGMSG (klogFatal, "Received quit");
            break;
        }

        rc = KFileReadAll (src, pos, buff, sizeof (buff), &num_read);
        if (rc)
        {
            PLOGERR (klogErr,
                     (klogErr, rc,
                      "Failed to read from file $(F) at $(P)",
                      "F=%s,P=%lu", source, pos));
            break;
        }
        
        if (num_read == 0)
            break;

        rc = KFileWriteAll (dst, pos, buff, num_read, &num_writ);
        if (rc)
        {
            PLOGERR (klogErr,
                     (klogErr, rc,
                      "Failed to write to file $(F) at $(P)",
                      "F=%s,P=%lu", dest, pos));
            break;
        }
        
        if (num_writ != num_read)
        {
            rc = RC (rcExe, rcFile, rcWriting, rcFile, rcInsufficient);
            PLOGERR (klogErr,
                     (klogErr, rc,
                      "Failed to write all to file $(F) at $(P)",
                      "F=%s,P=%lu", dest, pos));
            break;
        }
    }
    return rc;
}


/*
 * determine the encryption type for KFile f with pathname name
 */
static
rc_t EncryptionTypeCheck (const KFile * f, const char * name, EncScheme * scheme)
{
    size_t num_read;
    rc_t rc;
    char head [128];

    assert (f != NULL);
    assert (name != NULL);

    rc = KFileReadAll (f, 0, head, sizeof head, &num_read);
    if (rc)
    {
        PLOGERR (klogErr, (klogErr, rc, "Unable to read head of "
                           "'$(F)'", "F=%s", name));
        *scheme = encError;
        return rc;
    }

    rc = KFileIsEnc (head, num_read);
    if (rc == 0)
        *scheme = encEncFile;
    else
    {
        rc = KFileIsWGAEnc (head, num_read);
        if (rc == 0)
            *scheme = encWGAEncFile;
        else
            *scheme = encNone;
    }
    return 0;
}


/*
 * Check a file path name for ending in the extension used by this program
 *
 * return true if it ends with the extension and false if it does not
 */
static
bool IsTmpFile (const char * path)
{
    const char * pc;

    pc = strrchr (path, '.');
    if (pc == NULL)
        return false;

    if (strcmp (pc, TmpExt) == 0)
        return true;

    pc = string_chr (path, pc - path, '.');
    if (pc == NULL)
        return false;

    return (strcmp (pc, TmpLockExt) == 0);
}


static
rc_t FileInPlace (KDirectory * cwd, const char * leaf)
{
    rc_t rc;
    bool is_tmp;

    rc = 0;
    is_tmp = IsTmpFile (leaf);

    if (is_tmp)
    {
        TmpFoundFlag = true;
        if (ForceFlag)
            ; /* LOG OVERWRITE */
        else
            ; /* LOG TMP */
    }
    if (!is_tmp || ForceFlag)
    {
        char temp [MY_MAX_PATH];


        rc = KDirectoryResolvePath (cwd, false, temp, sizeof temp, ".%s%s",
                                    leaf, TmpExt);

        if (rc)
            PLOGERR (klogErr, (klogErr, rc, "unable to resolve '.$(S)$(E)'",
                               "S=%s,E=%s",leaf,TmpExt));
        else
        {
            KPathType kpt;
            uint32_t kcm;

            kcm = kcmCreate|kcmParents;
            kpt = KDirectoryPathType (cwd, temp);
            if (kpt != kptNotFound)
            {
                /* log busy */
                if (ForceFlag)
                {
                    kcm = kcmInit|kcmParents;
                    /* log force */
                    kpt = kptNotFound;
                }
            }

            if (kpt == kptNotFound)
            {
                const KFile * infile;

                rc = KDirectoryOpenFileRead (cwd, &infile, "%s", leaf);
                if (rc)
                    PLOGERR (klogErr, (klogErr, rc, "Unable to resolve '$(F)'",
                                       "F=%s",leaf));
                else
                {
                    EncScheme scheme;

                    rc = EncryptionTypeCheck (infile, leaf, &scheme);
                    if (rc == 0)
                    {
                        bool do_this_file;

                        do_this_file = DoThisFile (infile, scheme);

                        if (do_this_file)
                        {
                            KFile * outfile;

                            rc = KDirectoryCreateExclusiveAccessFile (cwd, &outfile,
                                                                      false, 0600, kcm,
                                                                      temp);

                            if (rc)
                                ;
                            else
                            {
                                const KFile * Infile;
                                KFile * Outfile;

                                rc = CryptFile (infile, &Infile, outfile, &Outfile, scheme);

                                if (rc == 0)
                                {
                                    rc = CopyFile (Infile, Outfile, leaf, temp);

                                    if (rc == 0)
                                    {
                                        uint32_t access;
                                        KTime_t date;

                                        rc = KDirectoryAccess (cwd, &access, "%s", leaf);
                                        if (rc == 0)
                                            rc = KDirectoryDate (cwd, &date, "%s", leaf);

                                        KFileRelease (infile);
                                        KFileRelease (outfile);
                                        KFileRelease (Infile);
                                        KFileRelease (Outfile);

                                        if (rc == 0)
                                        {
                                            rc = KDirectoryRename (cwd, true, temp, leaf);
                                            if (rc)
                                                LOGERR (klogErr, rc, "error renaming");
                                            else
                                            {
                                                /*rc =*/
                                                KDirectorySetAccess (cwd, false, access,
                                                                     0777, "%s", leaf);
                                                KDirectorySetDate (cwd, false, date, "%s", leaf);
                                                /* gonna ignore an error here I think */
                                                return rc;
                                            }
                                        }
                                    }
                                }
                                KFileRelease (outfile);
                            }
                        }
                    }
                    KFileRelease (infile);
                }
            }
        }
    }
    return rc;
}


static
rc_t FileToFile (const KDirectory * sd, const char * source, 
                 KDirectory *dd, const char * dest)
{
    const KFile * infile;
    rc_t rc;
    uint32_t access;
    KTime_t date;
    bool is_tmp;


    if ((sd == dd) && (strcmp (source, dest) == 0))
        return FileInPlace (dd, dest);

    /*
     * A Hack to make stdin/stout work within KFS
     */
    if (UseStdin)
    {
        const KFile * iinfile;
        rc = KFileMakeStdIn (&iinfile);
        if (rc == 0)
        {
            rc = KBufReadFileMakeRead (&infile, iinfile, 64 * 1024);
            KFileRelease (iinfile);
            if (rc == 0)
            {
                access = 0640;
                date = 0;
                goto stdin_shortcut;
            }
            LOGERR (klogErr, rc, "error wrapping stdin");
            return rc;
        }
    }
    rc = 0;
    is_tmp = IsTmpFile (source);

    if (is_tmp)
    {
        TmpFoundFlag = true;
        if (ForceFlag)
            ; /* LOG OVERWRITE */
        else
                ; /* LOG TMP */
    }
    if (!is_tmp || ForceFlag)
    {
        rc = KDirectoryAccess (sd, &access, "%s", source);
        if (rc)
            LOGERR (klogErr, rc, "Error check permission of source");

        else
        {
            rc = KDirectoryDate (sd, &date, "%s", source);
            if (rc)
                LOGERR (klogErr, rc, "Error check date of source");

            else
            {
                rc = KDirectoryOpenFileRead (sd, &infile, "%s", source);
                if (rc)
                    PLOGERR (klogErr, (klogErr, rc,
                                       "Error opening source file '$(S)'",
                                       "S=%s", source));
                else
                {
                    EncScheme scheme;

                stdin_shortcut:
                    rc = EncryptionTypeCheck (infile, source, &scheme);
                    if (rc == 0)
                    {
                        KFile * outfile;
                        uint32_t kcm;

                        /*
                         * Hack to support stdout before VFS is complete enough to use here
                         */
                        if (UseStdout)
                        {
                            rc = KFileMakeStdOut (&outfile);
                            if (rc)
                                LOGERR (klogErr, rc, "error wrapping stdout");
                        }
                        else
                        {
                            kcm = ForceFlag ? kcmInit|kcmParents : kcmCreate|kcmParents;

                            rc = KDirectoryCreateFile (dd, &outfile, false, 0600, kcm, "%s", dest);
                            if (rc)
                                PLOGERR (klogErr,(klogErr, rc, "error opening output '$(O)'",
                                                  "O=%s", dest));
                        }
                        if (rc == 0)
                        {
                            const KFile * Infile;
                            KFile * Outfile;

                            rc = CryptFile (infile, &Infile, outfile, &Outfile, scheme);
                            if (rc == 0)
                            {
                                rc = CopyFile (Infile, Outfile, source, dest);
                                if (rc == 0)
                                {
                                    rc = KDirectorySetAccess (dd, false, access, 0777,
                                                              "%s", dest);

                                    if (rc == 0 && date != 0)
                                        rc = KDirectorySetDate (dd, false, date, "%s", dest);
                                }
                                KFileRelease (Infile);
                                KFileRelease (Outfile);
                            }
                            KFileRelease (outfile);
                        }
                    }
                    KFileRelease (infile);
                }
            }
        }
    }
    return rc;
}


static
rc_t DoDir (const KDirectory * sd, KDirectory * dd)
{
    KNamelist * names;
    rc_t rc;

    rc = KDirectoryList (sd, &names, NULL, NULL, ".");
    if (rc)
        ;
    else
    {
        uint32_t count;

        rc = KNamelistCount (names, &count);
        if (rc)
            ;
        else
        {
            uint32_t idx;

            for (idx = 0; idx < count; ++idx)
            {
                const char * name;

                rc = KNamelistGet (names, idx, &name);
                if (rc)
                    ;
                else
                {
                    const KDirectory * nsd;
                    KDirectory * ndd;
                    KPathType kpt;

                    kpt = KDirectoryPathType (sd, name);

                    switch (kpt)
                    {
                    default:
                        break;

                    case kptFile:
                        if (sd == dd)
                            rc = FileInPlace (dd, name);
                        else
                            rc = FileToFile (sd, name, dd, name);
                        break;

                    case kptDir:
                        if (sd == dd)
                        {
                            rc = KDirectoryOpenDirUpdate (dd, &ndd, false, "%s", name);
                            if (rc)
                                ;
                            else
                            {
                                /* RECURSION */
                                rc = DoDir (ndd, ndd);
                                KDirectoryRelease (ndd);
                            }
                        }
                        else
                        {
                            rc = KDirectoryOpenDirRead (sd, &nsd, false, name);
                            if (rc)
                                ;
                            else
                            {
                                rc = KDirectoryCreateDir (dd, 0600, kcmOpen, "%s", name);
                                if (rc)
                                    ;
                                else
                                {                                    
                                    rc = KDirectoryOpenDirUpdate (dd, &ndd, false, "%s", name);
                                    if (rc)
                                        ;
                                    else
                                    {
                                        /* RECURSION */
                                        rc = DoDir (nsd, ndd);

                                        KDirectoryRelease (ndd);
                                    }
                                }
                                KDirectoryRelease (nsd);
                            }
                        }
                        break;
                    }
                }
            }
        }
        KNamelistRelease (names);
    }
    return rc;
}


static
rc_t Start (KDirectory * cwd, const char * src, const char * dst)
{
    KPathType dtype;
    KPathType stype;
    char dpath [MY_MAX_PATH];
    char spath [MY_MAX_PATH];
    rc_t rc;
    bool using_stdin, using_stdout;

    /* limited anti oops checks */
    if (dst != NULL)
    {
        /* try to prevent file to file clash */
        if(strcmp (src,dst) == 0)
            dst = NULL;

        /* try to prevent file to dir clash */
        else
        {
            size_t s,d;

            s = string_size (src);
            d = string_size (dst);

            if (s > d)
            {
                if (string_cmp (src, s, dst, d, d) == 0)
                {
                    if ((src[d] == '/') ||
                        (src[d-1] == '/'))
                        dst = NULL;
                }
            }
        }
    }

    /*
     * This is a quick fix "hack"
     * A fully built out VFS should replace the KFS in use and eliminate this
     */
    using_stdin = (strcmp (src, "/dev/stdin") == 0);

    if (using_stdin)
    {
        if (dst == NULL)
        {
            rc = RC (rcExe, rcArgv, rcParsing, rcParam, rcNull);
            LOGERR (klogErr, rc, "Unable to handle stdin in place");
            return rc;
        }
        stype = kptFile;
        strcpy (spath, src);
        UseStdin = true;
        goto stdin_shortcut;
    }

/* TBD handle aliases */

    rc = KDirectoryResolvePath (cwd, false, spath, sizeof spath, "%s", src);
    if (rc)
    {
        LOGERR (klogErr, rc, "can't resolve source");
        return rc;
    }

    stype = KDirectoryPathType (cwd, spath);

    switch (stype)
    {
    case kptNotFound:
        rc = RC (rcExe, rcArgv, rcResolving, rcPath, rcNotFound);
        break;

    default:
    case kptBadPath:
        rc = RC (rcExe, rcArgv, rcResolving, rcPath, rcInvalid);
        break;

    case kptCharDev:
    case kptBlockDev:
    case kptFIFO:
    case kptZombieFile:
    case kptDataset:
    case kptDatatype:
        rc = RC (rcExe, rcArgv, rcResolving, rcPath, rcIncorrect);
        break;

    case kptFile:
    case kptDir:
        break;
    }
    if (rc)
    {
        PLOGERR (klogErr, (klogErr, rc, "can not use source '$(S)'", "S=%s", src));
        return rc;
    }

    if (dst == NULL)
    {
        if (stype == kptFile)
        {
            KDirectory * ndir;
            char * pc;

            pc = strrchr (spath, '/');
            if (pc == NULL)
            {
                pc = spath;
                ndir = cwd;
                rc = KDirectoryAddRef (cwd);
            }
            else if (pc == spath)
            {
                ++pc;
                ndir = cwd;
                rc = KDirectoryAddRef (cwd);
            }
            else
            {
                *pc++ = '\0';
                rc = KDirectoryOpenDirUpdate (cwd, &ndir, false, spath);
            }

            if (rc == 0)
            {
                rc = FileInPlace (ndir, pc);
                KDirectoryRelease (ndir);
            }
        }
        else
        {
            KDirectory * ndir;

            rc = KDirectoryOpenDirUpdate (cwd, &ndir, false, spath);
            if (rc)
                ;
            else
            {
                rc = DoDir (ndir, ndir);
                KDirectoryRelease (ndir);
            }
        }
    }
    else
    {
    stdin_shortcut:
        using_stdout = (strcmp (dst, "/dev/stdout") == 0);
        if (using_stdout == true)
        {
            dtype = kptFile;
            strcpy (dpath, dst);
            UseStdout = true;
            goto do_file;
        }
        rc = KDirectoryResolvePath (cwd, false, dpath, sizeof dpath, "%s", dst);
        if (rc)
        {
            LOGERR (klogErr, rc, "can't resolve destination");
            return rc;
        }
        dtype = KDirectoryPathType (cwd, dpath);
        switch (dtype)
        {
        default:
        case kptBadPath:
            rc = RC (rcExe, rcArgv, rcResolving, rcPath, rcInvalid);
            PLOGERR (klogErr, (klogErr, rc, "can not use destination  '$(S)'", "S=%s", dst));
            break;

        case kptCharDev:
        case kptBlockDev:
        case kptFIFO:
        case kptZombieFile:
        case kptDataset:
        case kptDatatype:
            rc = RC (rcExe, rcArgv, rcResolving, rcPath, rcIncorrect);
            PLOGERR (klogErr, (klogErr, rc, "can not use destination parameter '$(S)'", "S=%s", dst));
            break;

        case kptNotFound:
        {
            size_t z;

            z = strlen (dst) - 1;
            if ((dst[z] == '/') || (stype == kptDir))
                goto do_dir;
            else
                goto do_file;
        }

        case kptFile:
            if (!ForceFlag)
            {
                rc = RC (rcExe, rcArgv, rcParsing, rcFile, rcExists);
                PLOGERR (klogErr, (klogErr, rc, "can not over-write '$(F)' without --force",
                                   "F=%s", dpath));
                break;
            }
        do_file:
            if (stype == kptFile)
            {
                rc = FileToFile (cwd, spath, cwd, dpath);
            }
            else
            {
                rc = RC (rcExe, rcArgv, rcResolving, rcPath, rcIncorrect);
                LOGERR (klogErr, rc, "Can't do directory to file");
            }
            break;

        do_dir:
        case kptDir:
            if (stype == kptDir)
            {
#if DIRECTORY_TO_DIRECTORY_SUPPORTED
                const KDirectory * sdir;
                KDirectory * ddir;

                rc = KDirectoryOpenDirRead (cwd, &sdir, false, spath);
                if (rc)
                    ;
                else
                {
                    if (dtype == kptNotFound)
                        rc = KDirectoryCreateDir (cwd, 0775, kcmCreate|kcmParents,
                                                  "%s", dpath);
                    if (rc == 0)
                    {
                        rc = KDirectoryOpenDirUpdate (cwd, &ddir, false, dpath);
                        if (rc)
                            ;
                        else
                        {
                            rc = DoDir (sdir, ddir);
                            KDirectoryRelease (ddir);
                        }
                    }
                    KDirectoryRelease (sdir);
                }
#else
                rc = RC (rcExe, rcArgv, rcResolving, rcPath, rcIncorrect);
                LOGERR (klogErr, rc, "Can't do directory to directory");
#endif
            }
            else
            {
                KDirectory * ndir;
                const char * pc;

                if (dtype == kptNotFound)
                    rc = KDirectoryCreateDir (cwd, 0775, kcmCreate|kcmParents,
                                              "%s", dpath);
                if (rc == 0)
                {
                    rc = KDirectoryOpenDirUpdate (cwd, &ndir, false, dpath);
                    if (rc)
                        ;
                    else
                    {
                        pc = strrchr (spath, '/');
                        if (pc == NULL)
                            pc = spath;
                        else
                            ++pc;

                        rc = FileToFile (cwd, spath, ndir, pc);

                        KDirectoryRelease (ndir);
                    }
                }
            }
            break;
        }
    }
    return rc;
}


static
rc_t StartFileSystem (const char * src, const char * dst)
{
    VFSManager * vmanager;
    rc_t rc;
 
    rc = VFSManagerMake (&vmanager);
    if (rc)
        LOGERR (klogErr, rc, "Failed to open file system");

    else
    {
        rc = VFSManagerGetKryptoPassword (vmanager, Password, sizeof Password,
                                          &PasswordSize);
        if (rc != 0)
            LOGERR (klogErr, rc, "unable to obtain a password");

        else
        {
            rc = KKeyInitRead (&Key, kkeyAES128, Password, PasswordSize);
            if (rc)
                LOGERR (klogErr, rc, "Unable to make encryption/decryption key");

            else
            {
                KDirectory * cwd;

                rc = VFSManagerGetCWD (vmanager, &cwd);
                if (rc)
                    LOGERR (klogInt, rc, "unable to access current directory");

                else
                {
                    rc = Start (cwd, src, dst);

                    KDirectoryRelease (cwd);
                }
            }
        }
        VFSManagerRelease (vmanager);
    }
    return rc;
}


rc_t CommonMain (Args * args)
{
    rc_t rc;
    uint32_t ocount; /* we take the address of ocount but not pcount. */
    uint32_t pcount; /* does that help the compiler optimize? */

    rc = ArgsParamCount (args, &ocount);
    if (rc)
        LOGERR (klogInt, rc, "failed to count parameters");

    else if ((pcount = ocount) == 0)
        MiniUsage (args);

    else if (pcount > 2)
    {
        LOGERR (klogErr, rc, "too many parameters");
        MiniUsage(args);
    }
            
    else
    {
        const char * dst; /* we only take the address of one of these */
        const char * src;

        rc = ArgsOptionCount (args, OPTION_FORCE, &ocount);
        if (rc)
            LOGERR (klogInt, rc, "failed to examine force option");

        else
        {
            ForceFlag = (ocount > 0);

            /* -----
             * letting comp put src in register
             * only if it wants
             */
            rc = ArgsParamValue (args, 0, &dst);
            if (rc)
                LOGERR (klogInt, rc, "Failure to fetch "
                        "source parameter");

            else
            {
                src = dst;

                if (pcount == 1)
                    dst = NULL;

                else
                {
                    rc = ArgsParamValue (args, 1, &dst);
                    if (rc)
                        LOGERR (klogInt, rc, "Failure to fetch "
                                "destination parameter");
                }

                if (rc == 0)
                    rc = StartFileSystem (src, dst);
            }
        }
    }
    return rc;
}

/* EOF */
