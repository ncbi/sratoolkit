#!/usr/local/bin/perl -w

################################################################################
# N.B. Run "perl configuration-assistant.perl" if you see a message like:
# configuration-assistant.perl: /usr/local/bin/perl: bad interpreter: No such file or directory
################################################################################
my $VERSION = '1.1.2';
################################################################################

use strict;

use Cwd "getcwd";
use Fcntl ':mode';
use File::Basename qw(basename dirname);
use File::Path "mkpath";
use File::Spec;
use Getopt::Long "GetOptions";
use IO::Handle;
use Getopt::Long "GetOptions";

sub println { print @_; print "\n"; }

STDOUT->autoflush(1);

my $R = 1;
my $W = 2;
my $X = 4;
my $CREATED = 8;
my $FOUND = 16;
my $RWX = $FOUND + $R + $W + $X;

println "==========================================";
println "The SRA toolkit documentation page is";
println "http://www.ncbi.nlm.nih.gov/Traces/sra/std";
println "==========================================\n";

my %options;
Help(1) unless (GetOptions(\%options, 'fix', 'help', 'version', 'wordy'));
Help(0) if ($options{help});
Version() if ($options{version});

$options{references} = 1 if ($#ARGV >= 0);

println "cwd = '" . getcwd() . "'\n";

my $DECRYPTION_PKG;
{
    my ($fastq_dump, $fastq_dir) = FindBin("fastq-dump", "optional");
    my ($sam_dump, $sam_dir) = FindBin("sam-dump", "optional");
    if (! $fastq_dump && ! $sam_dump) {
        println "presuming to be run in decryption package";
        $DECRYPTION_PKG = 1;
    }
}

my ($VDB_CONFIG, $BIN_DIR) = FindBin("vdb-config");

my %refseq_cfg;
%refseq_cfg = CheckRefseqCfg() unless ($DECRYPTION_PKG);

umask 0002;

my $HOME_CFG_DIR;
my $fixed_config = 0;
$fixed_config |= DoRefseqCfg() if (! $DECRYPTION_PKG && ! $options{NCBI});
$fixed_config |= DoKryptoCfg();
DoSchemaCfg() unless ($DECRYPTION_PKG);
exit 0 if ($DECRYPTION_PKG);

if ((! defined $options{references}) && $fixed_config == 0 && 0)
{   $options{references} = 1 }
$options{references} = AskyN
       ("Would you like to test cSRA files for remote reference dependencies?",
        $fixed_config)
    unless ($options{references});
exit 0 unless ($options{references});

my $CHECK_ONLY = $refseq_cfg{refseq_dir_read_only} || $options{NCBI};

my ($ALIGN_INFO, $ALIGN_INFO_DIR) = FindBin("align-info");
Warn("using align-info and vdb-config located in different directories:\n"
        . "\t$ALIGN_INFO\n\t$VDB_CONFIG")
    if ($BIN_DIR ne $ALIGN_INFO_DIR);

my $WGET;
$WGET = FindWget() unless ($CHECK_ONLY);

my $got_packed;
my @packed;

if ($#ARGV > -1) {
    foreach (@ARGV) {
        Load($CHECK_ONLY, $refseq_cfg{refseq_dir}, $_);
    }
} else {
    while (1) {
        my $f = Ask("Enter cSRA file name (Press Enter to exit)");
        last unless ($f);
        Load($CHECK_ONLY, $refseq_cfg{refseq_dir}, $f);
    }
}

################################################################################
# FUNCTIONS #
################################################################################

sub DoRefseqCfg {
    my $fixed_config = 0;

    if ($refseq_cfg{refseq_dir}) {
        if ($refseq_cfg{FIX_paths}) {
            ($refseq_cfg{refseq_dir}, $refseq_cfg{refseq_dir_read_only})
                = AskRefseqChange($refseq_cfg{refseq_dir});
            unless ($refseq_cfg{refseq_dir_read_only}) {
                UpdateRefseqCfgNode($refseq_cfg{refseq_dir});
            }
        } elsif (not $refseq_cfg{refseq_dir_prm}) {
            unless (AskYn("Configuration is found\n"
                . "but the local reference repository $refseq_cfg{refseq_dir} "
                . "does not exist.\n"
                . "Do you want to create it?"))
            {
                println "Your tools will not work properly.";
                println "Remote reference sequences will not be resolved.";
                println "For more information visit SRA website.";
                exit 1;
            }
            my ($prm, $perm)
                = CheckDir($refseq_cfg{refseq_dir}, "create_missing");
            my $created = $prm & $CREATED;
            $prm &= $RWX;
            if ($prm != $RWX) {
                if ($^O ne 'cygwin' || $created != $CREATED) {
                    Fatal("Cannot create $refseq_cfg{refseq_dir}");
                    FixRefseqCfg(\%refseq_cfg,
                        "Local reference repository is read-only. ".
                        "Whould you like to change it?",
                        "You will not be able to upload reference sequences.\n"
                        . "For more information visit SRA website.");
                }
            }
            ++$fixed_config;
        }
    } else {
        unless (AskYn("Do you want to create a new configuration?")) {
            println "Your tools will not work properly without configuration.";
            println "For more information visit SRA website.";
            exit 1;
        }

        ($refseq_cfg{refseq_dir}, $refseq_cfg{refseq_dir_prm})
            = MakeRefseqDir();

        UpdateRefseqCfgNode($refseq_cfg{refseq_dir});
        ++$fixed_config;
    }

    unless ($refseq_cfg{refseq_dir_read_only}) {
        CleanEmptyFiles($refseq_cfg{refseq_dir});
    }
    
    if ($^O ne 'MSWin32' && $options{fix}) {
        FixRefseqDirPermissions($refseq_cfg{refseq_dir},
            $refseq_cfg{refseq_dir_read_only});
    }

    return $fixed_config;
}

sub Ask {
    my ($prompt) = @_;

    print "$prompt: ";

    my $in = <STDIN>;
    unless ($in) {
        println;
        return "";
    }

    chomp $in;
    return $in;
}

sub AskYn { return AskYN($_[0], 'yes');}
sub AskyN { return AskYN($_[0], 'no' );}

sub AskYN {
    my ($q, $yes) = @_;

    $yes = '' if ($yes eq 'no');

    print "$q ";

    if ($yes) {
        print "[Y/n] ";
    } else {
        print "[y/N] ";
    }

    my $in = <STDIN>;
    chomp $in;
    if ($in) {
        return $in eq 'Y' || $in eq 'y'
          || $in eq 'YES' || $in eq 'yes' || $in eq 'Yes';
    } else {
        return $yes;
    }
}

sub AskRefseqChange {
    my ($refseq) = @_;

    die unless ($refseq);

    my $force;

    while (1) {
        my $read = -r $refseq && -x $refseq;
        println "Your repository directory is $refseq.";
        my $dflt;
        if ($read) {
            println "It is read-only. You cannot add new sequences to it.";
            $dflt = "1";
        } else {
            println "You cannot read it.";
            $dflt = "2";
        }
        println "Make your choice:";
        println "1) Use existing repository";
        println "2) Use a different repository";
        println "3) exit the script";
        print "Your selection? [$dflt] ";

        my $in = <STDIN>;
        chomp $in;
        $in = $dflt unless ($in);

        if ($in eq "1") {
            unless ($read) {
                Fatal(
"Ask the owner of $refseq to allow you access $refseq directory\n" .
"Otherwise the dumpers will not be able to find reference files"
                );
            }
            return ($refseq, 'READ^ONLY');
        }

        if ($in eq "2") {
            last;
        }

        exit 0 if ($in eq "3");
    }

    my ($path, $perm) = MakeRefseqDir();

    return ($path, '');
}

sub Home {
    if ($ENV{HOME}) {
        return $ENV{HOME};
    } elsif ($ENV{USERPROFILE}) {
        return $ENV{USERPROFILE};
    } else {
        return '';
    }
}

sub MakeRefseqDir {
    my $deflt;
    if ($^O eq 'cygwin' and $ENV{USERPROFILE}) {
        $deflt = $ENV{USERPROFILE};
        $deflt =~ tr|\\|/|;
        $deflt =~ s|^([a-zA-Z]):/|/$1/|;
        if ($1) {
            $deflt = "/cygdrive$deflt";
        }
    } else {
        $deflt = Home();
    }
    unless ($deflt) {
        $deflt = getcwd();
    }
    $deflt = "." unless($deflt);
    $deflt = File::Spec->catdir($deflt, "ncbi", "refseq");

    while (1) {
        my $path;
        print "Specify installation directory for reference objects";
        if ($deflt) {
            print " [$deflt]";
        }
        print ": ";

        my $in = <STDIN>;
        unless ($in) {
            println;
            $path = "";
        }
        chomp $in;
        if ($in) {
            $path = $in;
        } elsif ($deflt) {
            $path = $deflt;
        }
        exit 1 unless ($path);
        my ($prm_org, $perm) = CheckDir($path, "create_missing");
        my $prm = $prm_org;
        my $created = $prm & $CREATED;
        $prm &= $RWX;
        if ($prm == $RWX || ($^O eq 'cygwin' and $created == $CREATED)) {
            return ($path, $prm_org);
        } elsif ($prm & $FOUND) {
            println "'$path' does not seem to be writable.";
            println "You will not be able to add new sequences to it.";
            if (AskyN("Do you want to use it?")) {
                return ($path, $prm_org);
            }
        }
    }
}

sub Posixify {
    ($_) = @_;

    # convert to POSIX path
    s|^/cygdrive/|/|;
    tr|\\|/|;
    s|^([a-zA-Z]):/|/$1/|;

    return $_;
}

sub Version {
      my $bin = basename($0);
      print << "END";
$bin version $VERSION
END

      exit 0;
}

sub Help {
    my ($exit) = @_;
    $exit = 0 unless ($exit);
      my $bin = basename($0);
      print << "END";
$bin version $VERSION

Utility to help configure the SRA tools to be able
to access the local reference repository,
determine which reference sequences a cSRA file relies
upon and to fetch them from NCBI.
Fetched references are placed in the local reference repository.

Usage: $bin [--fix] [--wordy] [cSRA_FILE ...]

Options
-f, --fix         re-download existing reference files;
                  fix refseq repository permissions
-v, --version     print version and exit
-w, --wordy       increase "fetcher" verbosity
END

      exit $exit;
}

sub FixRefseqCfg {
    my ($refseq_cfg, $question, $bye) = @_;

    unless (AskYn($question)) {
        print $bye;
        exit 1;
    }

    ($refseq_cfg->{refseq_dir}, $refseq_cfg{refseq_dir_prm}) = MakeRefseqDir();
    UpdateRefseqCfgNode($refseq_cfg->{refseq_dir});
}

sub UpdateRefseqCfgNode {
    ($_) = @_;
    $_ = Posixify($_);
    UpdateConfigNode('refseq/paths' , $_);
}

sub UpdateKryptoCfgNode {
    ($_) = @_;
    $_ = Posixify($_);
    UpdateConfigNode('krypto/pwfile', $_);
}
    
sub UpdateConfigNode {
    my ($name, $val) = @_;

    my $cmd = "$VDB_CONFIG -s \"$name=$val\"";
    `$cmd`;
    if ($?) {
        die "Cannot execute '$cmd': $!";
    }
}

sub DoSchemaCfg {
    my $error;

    print "checking schema configuration... ";

    my $tmp = `$VDB_CONFIG vdb/schema/paths 2>&1`;
    if ($? == 0) {
        chomp $tmp;

        if ($tmp =~ m|<paths>(.+)</paths>|) {
            my $paths = $1;
            println $paths;
            my @paths = split(/:/, $paths);

            $error += !CheckSchemaFile('align/align.vschema', @paths);
            $error += !CheckSchemaFile('ncbi/seq.vschema'   , @paths);
            $error += !CheckSchemaFile('vdb/vdb.vschema'    , @paths);
        } else {
            $error = "unexpected: $tmp";
            println $error;
        }
    } elsif ($tmp =~ /path not found/) {
        $error = "not found";
        println $error;
    } else {
        $error = "unknown vdb-config schema error";
        println $error;
    }

    if ($error) {
        println "--------------------------------------";
        println "WARNING: SCHEMA FILES CANNOT BE FOUND.";
        println "IT COULD CAUSE LOADERS TO FAIL.";
        println "--------------------------------------";
    }

    return ! $error;
}

sub CheckSchemaFile {
    my ($file, @dir) = @_;

    my $cygdrive = '/cygdrive';
    my $hasCygdrive = -e '/cygdrive';

    print "checking $file... ";

    foreach my $dir(@dir) {
        my $path = "$dir/$file";
        if (-e $path) {
            println $path;
            return 1;
        }
    }

    println "not found";

    return 0;
}

sub Load {
    my ($check_only, $refseq_dir, $f) = @_;
    println "Determining $f external dependencies...";
    my $cmd = "$ALIGN_INFO $f";
    my @info = `$cmd`;
    if ($?) {
        println "$f: error";
    } else {
        my $refs = 0;
        my $ok = 0;
        my $ko = 0;
        foreach (@info) {
            my $force = 0;
            chomp;
            my @r = split /,/;
            if ($#r >= 3) {
                my ($seqId, $remote) = ($r[0], $r[3]);
                my $refseqName;
                ++$refs;
                if ($remote =~ /^remote/ && ! $check_only) {
                    ($refseqName, $remote) = CheckPacked($seqId, $remote);
                }
                if ($remote eq 'remote') {
                    if ($check_only) {
                        println "$seqId: unresolved";
                        ++$ok;
                    } else {
                        ++$force;
                    }
                }
                elsif ($remote =~ /^remote/ && ! $check_only
                    && $options{all_references})
                {	++$force; }
                if ($force) {
                    my $res = Download($refseqName);
                    if ($res) {
                        ++$ko;
                    } else {
                        ++$ok;
                    }
                }
            }
        }
        if ($check_only) {
            if ($ok == 0) {
                println "All $refs references were found";
            } else {
                println "$refs references were checked, $ok missed";
            }
        } else {
            print "All $refs references were checked (";
            print "$ko failed, " if ($ko);
            println "$ok downloaded)";
        }
    }
}

sub Download {
    my ($file) = @_;
    
    my $refseq_dir = $refseq_cfg{refseq_dir};
    
    print "Downloading $file... ";
    println if ($options{wordy});
    
    my $dest = "$refseq_dir/$file";
    my $cmd = "$WGET \"$dest\"" .
        " http://ftp-private.ncbi.nlm.nih.gov/sra/refseq/$file 2>&1";

    my $res;
    if ($options{wordy}) {
        $res = system($cmd);
    } else {
        `$cmd`;
        $res = $?;
    }

    print "$file: " if ($options{wordy});

    if ($res) {
        println "failed";
        if (-z $dest)
        {   unlink($dest); }
    } else {
        println "ok";
    }

    return $res;
}

sub CheckPacked {
    my ($seqId, $remote) = @_;

    my $file = $seqId;
    my $refseq_dir = $refseq_cfg{refseq_dir};

    unless ($got_packed) {
        Download('packed.txt');
        ++$got_packed;

        my $f = "$refseq_dir/packed.txt";
        if (-e $f) {
            if (open(IN, $f)) {
                foreach (<IN>) {
                    next if (/^\s*\#/);
                    chomp;
                    if (/^\s*([\w\.]+)\s*$/) {
                        push (@packed, $1);
                    }
                }
                close(IN);
            }
        }
    }

    foreach (@packed) {
        if ($seqId =~ /^$_/) {
            $file = $_;
            if (-s "$refseq_dir/$file") {
                $remote = 'local';
            }
            last;
        }
    }

    return ($file, $remote);
}

sub CleanEmptyFiles {
    my ($refseq_dir) = @_;

    print "checking $refseq_dir for invalid empty reference files... ";

    my $i = 0;
    opendir DIR, $refseq_dir or die "cannot opendir $refseq_dir";
    while ($_ = readdir DIR) {
        next if (/^\.{1,2}$/);

        my $f = "$refseq_dir/$_";

        my $empty;
        if (-z $f) {
            ++$empty;
        } elsif (-d $f) {
            # skip a directory
        } elsif (-s $f < 999) {
            open F, $f or die "cannot open $f";
            my $data = '';
            while (<F>) {
                while(chomp) {};
                $data .= $_;
            }
            ++$empty if ($data =~ m|<title>404 - Not Found</title>|);
        }
        if ($empty) {
            unlink $f or die "cannot remove $f";
            ++$i;
        }
    }
    closedir DIR;

    if ($i)
    {   println "$i found"; }
    else
    {   println "none found"; }
}

sub FixRefseqDirPermissions {
    my ($dir, $read_only) = @_;
    print "checking refseq repository...";
    my $mode = (stat($dir))[2] & 07777;

    if ($read_only) {
        println " read-only";
        print "\tchecking directory permissions...";
        unless (-r $dir && -x $dir) {
            Fatal("\n$dir cannot be accessed. "
                . "Either update its permissions\n"
                . "or choose another directory for refseq repository.");
        }
        println " OK";
        print "\tchecking file permissions...";
        opendir REP, $dir or die "cannot opendir $dir";
        my $ok = 1;
        while (my $f = readdir REP) {
            next if ($f =~ /^\.{1,2}$/);
            unless (-r "$dir/$f") {
                Warn("\n$dir/$f is not readable. "
                    . "Either update its permissions\n"
                    . "or choose another directory for refseq repository.");
                $ok = 0;
            }
        }
        closedir REP;
        println " OK" if $ok;
        println "\tchecking parent directories...";
        my @dirs = File::Spec->splitdir($dir);
        my $p = File::Spec->rootdir;
        while (@dirs) {
            $_ = shift @dirs;
            $p = File::Spec->catdir($p, $_);
            print "\t\t$p";
            $ok = 1;
            unless (-r $p && -x $p) {
                Warn("\n$p cannot be accessed. "
                    . "Either update its permissions\n"
                    . "or choose another directory for refseq repository.");
                $ok = 0;
            }
            println " OK" if $ok;
        }
    } else {
        println " writable";
        print "\tchecking directory permissions...";
        my $ok = 1;
        if (($mode & 0755) != 0755) {
            $mode |= 0755;
            if (chmod($mode, $dir) != 1) {
                Warn("\n$dir is not acessible by some of the users\n"
                    . "Dumping of some files could fail if run by anoother user"
                  );
                $ok = 0;
            } else { println " fixed" }
        } else { println " OK" }
        $ok = "OK";
        print "\tchecking file permissions...";
        opendir REP, $dir or die "cannot opendir $dir";
        while (my $next = readdir REP) {
            next if ($next =~ /^\.{1,2}$/);
            my $f = "$dir/$next";
            my $mode = (stat($f))[2] & 07777;
            if (($mode & 0644) != 0644) {
                if ($ok) {
                    $mode |= 0644;
                    if (chmod($mode, $f) != 1) {
                        Warn("\n$f cannot be acessed by some of the users\n"
                                . "Dumping of some files could fail "
                                . "if run by anoother user");
                        $ok = "";
                    } else { $ok = "fixed" }
                }
            }
        }
        closedir REP;
        println " $ok" if ($ok);
        $ok = 1;
        println "\tchecking parent directories...";
        my @dirs = File::Spec->splitdir($dir);
        my $p = File::Spec->rootdir;
        while (@dirs) {
            $_ = shift @dirs;
            $p = File::Spec->catdir($p, $_);
            print "\t\t$p";
            my $mode = (stat($p))[2] & 07777;
            if (($mode & 0550) != 0550) {
                if ($ok) {
                    $mode |= 0550;
                    if (chmod($mode, $p) != 1) {
                        Warn("\n$p cannot be acessed by some of the users\n"
                                . "Dumping of some files could fail "
                                . "if run by anoother user");
                        $ok = 0;
                    } else { println " fixed" }
                }
            } else { println " OK" }
        }
    }
}

################################################################################

sub CheckDir {
    my ($dir, $create_missing) = @_;

    $dir = File::Spec->canonpath($dir);
    print "checking $dir... ";

    my $prm = 0;
    unless (-e $dir) {
        println "not found";
        return (0, 0) unless ($create_missing);

        print "checking ${dir}'s parents... ";
        $dir = File::Spec->canonpath($dir);
        my @dirs = File::Spec->splitdir($dir);
        my $test = File::Spec->rootdir();
        if ($^O eq 'MSWin32') {
            $test = "";
        } else {
            Fatal("bad root directory '$test'") unless (-e $test);
        }
        foreach (@dirs) {
            my $prev = $test;
            if ($test) {
                $test = File::Spec->catdir($test, $_);
            } else {
                $test = File::Spec->catdir($_, File::Spec->rootdir());
            }
            if (! -e $test) {
                $test = $prev;
                last;
            }
        }

        print "($test)... ";
        my $cygwin_beauty;
# cygwin does not detect -r for $ENV{USERPROFILE}
        if (! -r $test || ! -x $test) {
            if ($^O eq 'cygwin') {
                ++$cygwin_beauty;
            } else {
                println "not readable";
                return (0, 0);
            }
        }
        if (! -x $test) {
            if ($^O eq 'cygwin') {
                ++$cygwin_beauty;
            } else {
                println "not writable";
                return (0, 0);
            }
        }
        if ($cygwin_beauty) {
            println("fail to check");
        } else {
            println("ok");
        }

        print "creating $dir... ";
        unless (mkpath($dir)) {
            die "cannot mkdir $dir" unless ($cygwin_beauty);
            println "failed. Is it writable?";
            return (0, 0);
        }
        println("ok");
        $prm += $CREATED;
        print "checking $dir... ";
    }

    my $perm = (stat($dir))[2];
    $prm += $FOUND;

    {
        my $cygwin_beauty;
        my $failures;
        if (-r $dir) {
            $prm += $R;
        }
        if (-w $dir) {
            $prm += $W;
        }
        if (-x $dir) {
            $prm += $X;
        }

        if (! -r $dir || ! -x $dir) {
            if ($^O eq 'cygwin') {
                ++$cygwin_beauty;
            } else {
                println "not readable";
                ++$failures;
            }
        }
        if (! $failures and ! -w $dir) {
            if ($^O eq 'cygwin') {
                ++$cygwin_beauty;
            } else {
                println "not writable";
                ++$failures;
            }
        }
        if ($cygwin_beauty) {
            println("fail to check");
        } elsif (!$failures) {
            println("ok");
        }
    }

    return ($prm, $perm);
}

sub CheckRefseqCfg {
    print "checking refseq configuration... ";

    my %refseq_cfg;
    RefseqFromConfig(\%refseq_cfg);

    if ($refseq_cfg{repository} || $refseq_cfg{servers} || $refseq_cfg{volumes})
    {
        if ($refseq_cfg{repository}) {
            println "refseq/repository found:";
        } elsif ($refseq_cfg{servers}) {
            println "refseq/servers found:";
        } elsif ($refseq_cfg{volumes}) {
            println "refseq/volumes found:";
        }
        println "Seems to be running in NCBI environment:";
        println "      refseq configuration fix/update is disabled;";
        println "      reference files download is disabled.";
        ++$options{NCBI};
        return;
    }

    if ($refseq_cfg{paths}) {
        println "paths=$refseq_cfg{paths}";
    } else {
        println "not found";
        return %refseq_cfg;
    }

    if ($refseq_cfg{paths} and index($refseq_cfg{paths}, ":") != -1) {
        die "Unexpected configuration: paths=$refseq_cfg{paths}";
    }

    my $dir = "$refseq_cfg{paths}";

    if ($^O eq 'MSWin32') { # Windows: translate POSIX to Windows path
        $dir =~ tr|/|\\|;
        $dir =~ s/^\\([a-zA-Z])\\/$1:\\/;
    } elsif ($^O eq 'cygwin' and $dir =~ m|^/[a-zA-Z]/|) {
        $dir = "/cygdrive$dir";
    }

    $refseq_cfg{refseq_dir} = $dir;
    my ($prm, $perm) = CheckDir($dir);
    $refseq_cfg{refseq_dir_prm} = $prm;
    if ($prm == 0) { # not found
        return %refseq_cfg;
    } elsif ($prm != $RWX) {
        if ($^O ne 'cygwin') {
            ++$refseq_cfg{FIX_paths};
#           Fatal("refseq repository is invalid or read-only");
        } # else cygwin does not always can tell permissions correctly
    }
    return %refseq_cfg;
}

sub FindWget {
    my $WGET;

    print "checking for wget... ";
    my $out = `wget -h 2>&1`;
    if ($? == 0) {
        println "yes";
        if ($options{fix}) {
            $WGET = "wget -O";
        } else {
            $WGET = "wget -c -O";
        }
        ++$options{all_references};
    } else {
        println "no";
    }

    unless ($WGET) {
        print "checking for curl... ";
        my $out = `curl -h 2>&1`;
        if ($? == 0) {
            println "yes";
            $WGET = "curl -o";
            ++$options{all_references} if ($options{fix});
        } else {
            println "no";
        }
    }

    unless ($WGET) {
        print "checking for ./wget... ";
        my $cmd = dirname($0) ."/wget";
        my $out = `$cmd -h 2>&1`;
        if ($? == 0) {
            println "yes";
            if ($options{fix}) {
                $WGET = "$cmd -O";
            } else {
                $WGET = "$cmd -c -O";
            }
            ++$options{all_references};
        } else {
            println "no";
        }
    }

    unless ($WGET) {
        print "checking for ./wget.exe... ";
        my $cmd = dirname($0) ."/wget.exe";
        my $out = `$cmd -h 2>&1`;
        if ($? == 0) {
            println "yes";
            if ($options{fix}) {
                $WGET = "$cmd -O";
            } else {
                $WGET = "$cmd -c -O";
            }
            ++$options{all_references};
        } else {
            println "no";
        }
    }

    Fatal("none of wget, curl could be found") unless ($WGET);

    return $WGET;
}

sub FindBin {
    my ($name, $optional) = @_;

    my $prompt = "checking for $name";
    my $basedir = dirname($0);

    # built from sources
    print "$prompt (local build)... ";
    if (-e File::Spec->catfile($basedir, "Makefile")) {
        my $f = File::Spec->catfile($basedir, "build", "Makefile.env");
        if (-e $f) {
            my $dir = `make -s bindir -C $basedir 2>&1`;
            if ($? == 0) {
                chomp $dir;
                my $try = File::Spec->catfile($dir, $name);
                print "($try";
                if (-e $try) {
                    print ": found)... ";
                    my $tmp = `$try -h 2>&1`;
                    if ($? == 0) {
                        println "yes";
                        return ($try, $dir);
                    } else {
                        println "\nfailed to run '$try -h'";
                    }
                } else {
                    println ": not found)";
                }
            }
        }
    } else {
        println "no";
    }

    # try the script directory
    {
        my $try = File::Spec->catfile($basedir, $name);
        print "$prompt ($try";
        if (-e $try) {
            print ": found)... ";
            my $tmp = `$try -h 2>&1`;
            if ($? == 0) {
                println "yes";
                return ($try, $basedir);
            } else {
                println "\nfailed to run '$try -h'";
            }
        } else {
            println ": not found)";
        }
    }

    # the script directory: windows
    {
        my $try = File::Spec->catfile($basedir, "$name.exe");
        print "$prompt ($try";
        if (-e $try) {
            print ": found)... ";
            my $tmp = `$try -h 2>&1`;
            if ($? == 0) {
                println "yes";
                return ($try, $basedir);
            } else {
                println "\nfailed to run '$try -h'";
            }
        } else {
            println ": not found)";
        }
    }

    # check from PATH
    {
        my $try = "$name";
        print "$prompt ($try)... ";
        my $tmp = `$try -h 2>&1`;
        if ($? == 0) {
            println "yes";
            return ($try, "");
        } else {
            println "no";
        }
    }

    Fatal("$name could not be found") unless ($optional);
    return (undef, undef);
}

sub DoKryptoCfg {
    my $fixed_config = 0;

    print "checking krypto configuration... ";

    my $nm = 'pwfile';
    my $name = "krypto/$nm";

    my $v = `$VDB_CONFIG $name 2>&1`;
    if ($?) {
        die $! unless ($v =~ /path not found while opening node/);
        println "not found";
        if ($DECRYPTION_PKG) {
            if (AskYn("Do you want to set VDB password?")) {
                UpdateKryptoCfg('create kfg');
                $fixed_config = 1;
            }
        } else {
            if (AskyN("Do you want to set VDB password?")) {
                UpdateKryptoCfg('create kfg');
                $fixed_config = 1;
            }
        }
    } else {
        $v =~ /<$nm>(.*)<\/$nm>/;
        die "Invalid '$nm' configuration" unless ($1);
        $v = $1;
        print "$nm=$v";
        if ($v eq '1') { # fix bug introduced by previous script version
            println ": bad";
            if ($DECRYPTION_PKG) {
                if (AskYn("Do you want to set VDB password?")) {
                    UpdateKryptoCfg('create kfg');
                    $fixed_config = 1;
                }
            } else {
                if (AskyN("Do you want to set VDB password?")) {
                    UpdateKryptoCfg('create kfg');
                    $fixed_config = 1;
                }
            }
        } else {
            if (-e $v) {
                println ": found";
            } else {
                println ": not found";
            }
            if (-e $v) {
                if (AskyN("Do you want to update VDB password?")) {
                    UpdateKryptoCfg();
                    $fixed_config = 1;
                }
            } elsif (! $DECRYPTION_PKG) {
                if (AskyN("Do you want to set VDB password?")) {
                    UpdateKryptoCfg();
                    $fixed_config = 1;
                }
            } else {
                if (AskYn("Do you want to set VDB password?")) {
                    UpdateKryptoCfg();
                    $fixed_config = 1;
                }
            }
        }
    }

    return $fixed_config;
}

sub UpdateKryptoCfg {
    my ($update_kfg) = @_;

    my $VDB_PASSWD = (FindBin('vdb-passwd'))[0];

    my $home = Home();
    UpdateKryptoCfgNode("$home/.ncbi/vdb-passwd") if ($update_kfg);

    my $res = system("$VDB_PASSWD -q");
    if ($res) {
        println "password update failed";
    } else {
        println "password updated ok";
    }
}

sub RefseqConfig {
    my ($nm) = @_;

    $_ = `$VDB_CONFIG refseq/$nm 2>&1`;

    if ($?) {
        if (/path not found while opening node/) {
            $_ = '';
        } else {
            die $!;
        }
    } else {
        m|<$nm>(.*)</$nm>|s;
        die "Invalid 'refseq/$nm' configuration" unless ($1);
        # TODO die if (refseq/paths = "") : fix it
        $_ = $1;
    }

    return $_;
}

sub RefseqFromConfig {
    my ($refseq) = @_;

    $refseq->{paths} = RefseqConfig('paths');
    $refseq->{repository} = RefseqConfig('repository');
    $refseq->{servers} = RefseqConfig('servers');
    $refseq->{volumes} = RefseqConfig('volumes');
}

sub Fatal {
    my ($msg) = @_;

    print basename($0);
    println ": Fatal: $msg";

    exit 1;
}

sub Warn {
    my ($msg) = @_;

    print basename($0);
    println ": WARNING: $msg";
}

################################################################################
# EOF #
################################################################################
