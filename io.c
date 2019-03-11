/************************************************************************
 * This program is Copyright (C) 1986 by Jonathan Payne.  JOVE is       *
 * provided to you without charge, and with no warranty.  You may give  *
 * away copies of JOVE, including sources, provided that this notice is *
 * included in all the files.                                           *
 ************************************************************************/

#include "jove.h"
#include "io.h"
#include "termcap.h"

#ifdef IPROCS
#   include <signal.h>
#endif

#include <sys/stat.h>
#ifndef MSDOS
#include <sys/file.h>
#else /* MSDOS */
#include <fcntl.h>
#endif /* MSDOS */
#include <errno.h>

#ifndef W_OK
#   define W_OK	2
#   define F_OK	0
#endif

long	io_chars;		/* number of chars in this open_file */
int	io_lines;		/* number of lines in this open_file */

#if defined(VMUNIX)||defined(MSDOS)
char	iobuff[LBSIZE],
	genbuf[LBSIZE],
	linebuf[LBSIZE];
#else
char	*iobuff,
	*genbuf,
	*linebuf;
#endif

#ifdef BACKUPFILES
int	BkupOnWrite = 0;
#endif

close_file(fp)
File	*fp;
{
	if (fp) {
		if (fp->f_flags & F_TELLALL)
			add_mess(" %d lines, %D characters.",
				 io_lines,
				 io_chars);
		f_close(fp);
	}
}

/* Write the region from line1/char1 to line2/char2 to FP.  This
   never CLOSES the file since we don't know if we want to. */

int	EndWNewline = 1;

putreg(fp, line1, char1, line2, char2, makesure)
register File	*fp;
Line	*line1,
	*line2;
{
	register int	c;
	register char	*lp;

	if (makesure)
		(void) fixorder(&line1, &char1, &line2, &char2);
	while (line1 != line2->l_next) {
		lp = lcontents(line1) + char1;
		if (line1 == line2) {
			fputnchar(lp, (char2 - char1), fp);
			io_chars += (char2 - char1);
		} else while (c = *lp++) {
			putc(c, fp);
			io_chars += 1;
		}
		if (line1 != line2) {
			io_lines += 1;
			io_chars += 1;
#ifdef MSDOS
			putc('\r', fp);
#endif /* MSDOS */
			putc('\n', fp);
		}
		line1 = line1->l_next;
		char1 = 0;
	}
	flush(fp);
}

read_file(file, is_insert)
char	*file;
{
	Bufpos	save;
	File	*fp;

	if (!is_insert) {
		curbuf->b_ntbf = 0;
		set_ino(curbuf);
	}
	fp = open_file(file, iobuff, F_READ, !COMPLAIN, !QUIET);
	if (fp == NIL) {
		if (!is_insert && errno == ENOENT)
			s_mess("(new file)");
		else
			s_mess(IOerr("open", file));
		return;
	}
	DOTsave(&save);
	dofread(fp);
	if (is_insert && io_chars > 0) {
		modify();
		set_mark();
	}
	SetDot(&save);
	getDOT();
	close_file(fp);
}

dofread(fp)
register File	*fp;
{
	char	end[LBSIZE];
	int	xeof = 0;
	Line	*savel = curline;
	int	savec = curchar;
	extern disk_line	f_getputl();

	strcpy(end, linebuf + curchar);
	xeof = f_gets(fp, linebuf + curchar, LBSIZE - curchar);
	SavLine(curline, linebuf);
	if (!xeof) do {
		curline = listput(curbuf, curline);
		xeof = f_getputl(curline, fp);
	} while (!xeof);
	getDOT();
	linecopy(linebuf, (curchar = strlen(linebuf)), end);
	SavLine(curline, linebuf);
	IFixMarks(savel, savec, curline, curchar);
}

SaveFile()
{
	if (IsModified(curbuf)) {
		if (curbuf->b_fname == 0)
			WriteFile();
		else {
			filemunge(curbuf->b_fname);
#ifndef MSDOS	/* not sure - kg - */
			chk_mtime(curbuf, curbuf->b_fname, "save");
#endif /* MSDOS */
			file_write(curbuf->b_fname, 0);
			unmodify();
		}
	} else
		message("No changes need to be written.");
}

char	*HomeDir;	/* home directory */
int	HomeLen = -1;	/* length of home directory string */

#ifndef CHDIR

char *
pr_name(fname, okay_home)
char	*fname;
{
	if (fname == 0)
		return 0;

	if (okay_home == YES && strncmp(fname, HomeDir, HomeLen) == 0) {
		static char	name_buf[100];

		sprintf(name_buf, "~%s", fname + HomeLen);
		return name_buf;
	}

	return fname;
}

#else

#define NDIRS	5

private char	*DirStack[NDIRS] = {0};
private int	DirSP = 0;	/* Directory stack pointer */
#define PWD	(DirStack[DirSP])

char *
pwd()
{
	return PWD;
}

char *
pr_name(fname, okay_home)
char	*fname;
{
	int	n;

	if (fname == 0)
		return 0;
	n = numcomp(fname, PWD);

	if ((PWD[n] == 0) &&	/* Matched to end of PWD */
	    (fname[n] == '/'))
		return fname + n + 1;

	if (okay_home == YES && strcmp(HomeDir, "/") != 0 && strncmp(fname, HomeDir, HomeLen) == 0) {
		static char	name_buf[100];

		sprintf(name_buf, "~%s", fname + HomeLen);
		return name_buf;
	}

	return fname;	/* return entire path name */
}

Chdir()
{
	char	dirbuf[FILESIZE];

	(void) ask_file((char *) 0, PWD, dirbuf);
	if (chdir(dirbuf) == -1) {
		s_mess("cd: cannot change into %s.", dirbuf);
		return;
	}
	UpdModLine = YES;
	setCWD(dirbuf);
}

#ifndef MSDOS
#ifndef JOB_CONTROL
char *
getwd()
{
	Buffer	*old = curbuf;
	char	*ret_val;

	SetBuf(do_select((Window *) 0, "pwd-output"));
	curbuf->b_type = B_PROCESS;
	(void) UnixToBuf("pwd-output", NO, 0, YES, "/bin/pwd", (char *) 0);
	ToFirst();
	ret_val = sprint(linebuf);
	SetBuf(old);
	return ret_val;
}
#endif
#endif /* MSDOS */

setCWD(d)
char	*d;
{
	if (PWD == 0)
		PWD = malloc((unsigned) strlen(d) + 1);
	else {
		extern char	*ralloc();

		PWD = ralloc(PWD, strlen(d) + 1);
	}
	strcpy(PWD, d);
}

getCWD()
{
	char	*cwd;
#ifndef MSDOS
#ifdef JOB_CONTROL
	extern char	*getwd();
	char	pathname[FILESIZE];
#endif
#else
	extern char	*getcwd();
	char	pathname[FILESIZE];
#endif

#ifndef MSDOS
	cwd = getenv("CWD");
	if (cwd == 0)
		cwd = getenv("PWD");
	if (cwd == 0)
#ifdef JOB_CONTROL
		cwd = getwd(pathname);
#else
		cwd = getwd();
#endif
#else /* MSDOS */
		cwd = getcwd(pathname, FILESIZE);
#endif /* MSDOS */

	setCWD(cwd);
}	

prDIRS()
{
	register int	i;

	s_mess(": %f ");
	for (i = DirSP; i >= 0; i--)
		add_mess("%s ", pr_name(DirStack[i], YES));
}

prCWD()
{
	s_mess(": %f => \"%s\"", PWD);
}

Pushd()
{
	char	*newdir,
		dirbuf[FILESIZE];

	newdir = ask_file((char *) 0, NullStr, dirbuf);
	UpdModLine = YES;
	if (*newdir == 0) {	/* Wants to swap top two entries */
		char	*old_top;

		if (DirSP == 0)
			complain("pushd: no other directory.");
		old_top = PWD;
		DirStack[DirSP] = DirStack[DirSP - 1];
		DirStack[DirSP - 1] = old_top;
		(void) chdir(PWD);
	} else {
		if (chdir(dirbuf) == -1) {
			s_mess("pushd: cannot change into %s.", dirbuf);
			return;
		}

		if (DirSP + 1 >= NDIRS)
			complain("pushd: full stack; max of %d pushes.", NDIRS);
		DirSP += 1;
		setCWD(dirbuf);
	}
	prDIRS();
}

Popd()
{
	if (DirSP == 0)
		complain("popd: directory stack is empty.");
	UpdModLine = YES;
	free(PWD);
	PWD = 0;
	DirSP -= 1;
	(void) chdir(PWD);	/* If this doesn't work, we's in deep shit. */
	prDIRS();
}

private char *
dbackup(base, offset, c)
register char	*base,
		*offset,
		c;
{
	while (offset > base && *--offset != c)
		;
	return offset;
}

private
dfollow(file, into)
char	*file,
	*into;
{
	char	*dp,
		*sp;

	if (*file == '/') {		/* Absolute pathname */
		strcpy(into, "/");
		file += 1;
	} else
		strcpy(into, PWD);
	dp = into + strlen(into);

	sp = file;
	do {
		if (*file == 0)
			break;
		if (sp = index(file, '/'))
			*sp = 0;
		if (strcmp(file, ".") == 0)
			;	/* So it will get to the end of the loop */
		else if (strcmp(file, "..") == 0) {
			*(dp = dbackup(into, dp, '/')) = 0;
			if (dp == into)
				strcpy(into, "/"), dp = into + 1;
		} else {
			if (into[strlen(into) - 1] != '/')
				(void) strcat(into, "/");
			(void) strcat(into, file);
			dp += strlen(file);	/* stay at the end */
		}
		file = sp + 1;
	} while (sp != 0);
}

#endif /* CHDIR */

#ifndef MSDOS
private
get_hdir(user, buf)
register char	*user,
		*buf;
{
	char	fbuf[LBSIZE],
		pattern[100];
	register int	u_len;
	File	*fp;

	u_len = strlen(user);
	fp = open_file("/etc/passwd", fbuf, F_READ, COMPLAIN, QUIET);
	sprintf(pattern, "%s:[^:]*:[^:]*:[^:]*:[^:]*:\\([^:]*\\):", user);
	while (f_gets(fp, genbuf, LBSIZE) != EOF)
		if ((strncmp(genbuf, user, u_len) == 0) &&
		    (LookingAt(pattern, genbuf, 0))) {
			putmatch(1, buf, FILESIZE);
			close_file(fp);
			return;
		}
	f_close(fp);
	complain("[unknown user: %s]", user);
}
#endif /* MSDOS */

PathParse(name, intobuf)
char	*name,
	*intobuf;
{
	char	localbuf[FILESIZE];

	intobuf[0] = localbuf[0] = '\0';
	if (*name == '\0')
		return;
	if (*name == '~') {
		if (name[1] == '/' || name[1] == '\0') {
			strcpy(localbuf, HomeDir);
			name += 1;
#ifndef MSDOS
		} else {
			char	*uendp = index(name, '/'),
				unamebuf[30];

			if (uendp == 0)
				uendp = name + strlen(name);
			name = name + 1;
			null_ncpy(unamebuf, name, uendp - name);
			get_hdir(unamebuf, localbuf);
			name = uendp;
#endif /* MSDOS */
		}
	} 
#ifndef MSDOS
	 else if (*name == '\\')
		name += 1;
#endif /* MSDOS */
	(void) strcat(localbuf, name);
#ifdef CHDIR
	dfollow(localbuf, intobuf);
#else
	strcpy(intobuf, localbuf);
#endif
}

filemunge(newname)
char	*newname;
{
	struct stat	stbuf;

	if (newname == 0)
		return;
	if (stat(newname, &stbuf))
		return;
#ifndef MSDOS
	if (((stbuf.st_dev != curbuf->b_dev) ||
	     (stbuf.st_ino != curbuf->b_ino)) &&
#else /* MSDOS */
	if ( /* (stbuf.st_ino != curbuf->b_ino) && */
#endif /* MSDOS */
	    ((stbuf.st_mode & S_IFMT) != S_IFCHR) &&
	    (strcmp(newname, curbuf->b_fname) != 0)) {
		rbell();
		confirm("\"%s\" already exists; overwrite it? ", newname);
	}
}

WrtReg()
{
	DoWriteReg(NO);
}

AppReg()
{
	DoWriteReg(YES);
}

int	CreatMode = DFLT_MODE;

DoWriteReg(app)
{
	char	fnamebuf[FILESIZE],
		*fname;
	Mark	*mp = CurMark();
	File	*fp;

	/* Won't get here if there isn't a Mark */
	fname = ask_file((char *) 0, (char *) 0, fnamebuf);

#ifdef BACKUPFILES
	if (app == NO) {
		filemunge(fname);

		if (BkupOnWrite)
			file_backup(fname);
	}
#else
	if (!app)
		filemunge(fname);
#endif

	fp = open_file(fname, iobuff, app ? F_APPEND : F_WRITE, COMPLAIN, !QUIET);
	putreg(fp, mp->m_line, mp->m_char, curline, curchar, YES);
	close_file(fp);
}

int	OkayBadChars = 0;

WriteFile()
{
	char	*fname,
		fnamebuf[FILESIZE];

	fname = ask_file((char *) 0, curbuf->b_fname, fnamebuf);
	/* Don't allow bad characters when creating new files. */
	if (!OkayBadChars && strcmp(curbuf->b_fname, fnamebuf) != 0) {
#ifndef MSDOS
		static char	*badchars = "!$^&*()~`{}\"'\\|<>? ";
#else /* MSDOS */
		static char	*badchars = "*|<>? ";
#endif /* MSDOS */
		register char	*cp = fnamebuf;
		register int	c;

		while (c = *cp++)
			if (c < ' ' || c == '\177' || index(badchars, c))
				complain("'%p': bad character in filename.", c);
	}

#ifndef MSDOS
	chk_mtime(curbuf, fname, "write");
#endif /* MSDOS */
	filemunge(fname);
	curbuf->b_type = B_FILE;  	/* in case it wasn't before */
	setfname(curbuf, fname);
	file_write(fname, 0);
	unmodify();
}

/* Open file FNAME supplying the buffer IO routine with buffer BUF.
   HOW is F_READ, F_WRITE or F_APPEND.  IFBAD == COMPLAIN means that
   if we fail at opening the file, call complain.  LOUDNESS says
   whether or not to print the "reading ..." message on the message
   line.

   NOTE:  This opens the pr_name(fname, NO) of fname.  That is, FNAME
	  is usually an entire pathname, which can be slow when the
	  pathname is long and there are lots of symbolic links along
	  the way (which has become very common in my experience).  So,
	  this speeds up opens file names in the local directory.  It
	  will not speed up things like "../scm/foo.scm" simple because
	  by the time we get here that's already been expanded to an
	  absolute pathname.  But this is a start.
   */

File *
open_file(fname, buf, how, ifbad, loudness)
register char	*fname;
char	*buf;
register int	how;
{
	register File	*fp;

	io_chars = 0;
	io_lines = 0;

	fp = f_open(pr_name(fname, NO), how, buf, LBSIZE);
	if (fp == NIL) {
                message(IOerr((how == F_READ) ? "open" : "create", fname));
		if (ifbad == COMPLAIN)
			complain((char *) 0);
	} else {
		int	readonly = FALSE;

		if (access(pr_name(fname, NO), W_OK) == -1 && errno != ENOENT)
			readonly = TRUE;
							 
		if (loudness != QUIET) {
			fp->f_flags |= F_TELLALL;
			f_mess("\"%s\"%s", pr_name(fname, YES),
				   readonly ? " [Read only]" : NullStr);
		}
	}
	return fp;
}

#ifndef MSDOS
/* Check to see if the file has been modified since it was
   last written.  If so, make sure they know what they're
   doing.

   I hate to use another stat(), but to use confirm we gotta
   do this before we open the file.

   NOTE: This stats FNAME after converting it to a path-relative
	 name.  I can't see why this would cause a problem ...
   */

chk_mtime(thisbuf, fname, how)
Buffer	*thisbuf;
char	*fname,
	*how;
{
	struct stat	stbuf;
	Buffer	*b;
    	char	*mesg = "Shall I go ahead and %s anyway? ";

	if ((thisbuf->b_mtime != 0) &&		/* if we care ... */
	    (b = file_exists(fname)) &&		/* we already have this file */
	    (b == thisbuf) &&			/* and it's the current buffer */
	    (stat(pr_name(fname, NO), &stbuf) != -1) &&	/* and we can stat it */
	    (stbuf.st_mtime != b->b_mtime)) {	/* and there's trouble. */
	    	rbell();
		redisplay();	/* Ring that bell! */
	    	TOstart("Warning", TRUE);
	    	Typeout("\"%s\" now saved on disk is not what you last", pr_name(fname, YES));
		Typeout("visited or saved.  Probably someone else is editing");
		Typeout("your file at the same time.");
	    	if (how) {
			Typeout("");
			Typeout("Type \"y\" if I should %s, anyway.", how);
		    	f_mess(mesg, how);
		}
	    	TOstop();
	    	if (how)
		    	confirm(mesg, how);
	}
}

#endif /* MSDOS */

file_write(fname, app)
char	*fname;
{
	File	*fp;

#ifdef BACKUPFILES
	if (!app && BkupOnWrite)
		file_backup(fname);
#endif

	fp = open_file(fname, iobuff, app ? F_APPEND : F_WRITE, COMPLAIN, !QUIET);

	if (EndWNewline) {	/* Make sure file ends with a newLine */
		Bufpos	save;

		DOTsave(&save);
		ToLast();
		if (length(curline))	/* Not a blank Line */
			LineInsert(1);
		SetDot(&save);
	}
	putreg(fp, curbuf->b_first, 0, curbuf->b_last, length(curbuf->b_last), NO);
	set_ino(curbuf);
	close_file(fp);
}

ReadFile()
{
	Buffer	*bp;
	char	*fname,
		fnamebuf[FILESIZE];
	int	lineno;

	fname = ask_file((char *) 0, curbuf->b_fname, fnamebuf);
#ifndef MSDOS
	chk_mtime(curbuf, fname, "read");
#endif /* MSDOS */

	if (IsModified(curbuf)) {
		char	*y_or_n;
		int	c;

		for (;;) {
			rbell();
			y_or_n = ask(NullStr, "Shall I make your changes to \"%s\" permanent? ", curbuf->b_name);
			c = CharUpcase(*y_or_n);
			if (c == 'Y' || c == 'N')
				break;
		}			
		if (c == 'Y')
			SaveFile();
	}

	if ((bp = file_exists(fnamebuf)) &&
	    (bp == curbuf))
		lineno = pnt_line() - 1;
	else
		lineno = 0;

	unmodify();
	initlist(curbuf);
	setfname(curbuf, fname);
	read_file(fname, 0);
	SetLine(next_line(curbuf->b_first, lineno));
}

InsFile()
{
	char	*fname,
		fnamebuf[FILESIZE];

	fname = ask_file((char *) 0, curbuf->b_fname, fnamebuf);
	read_file(fname, 1);
}

#include "temp.h"

int	DOLsave = 0;	/* Do Lsave flag.  If lines aren't being save
			   when you think they should have been, this
			   flag is probably not being set, or is being
			   cleared before lsave() was called. */

private int	nleft,	/* number of good characters left in current block */
		tmpfd = -1;
disk_line	DFree = 1;
			/* pointer to end of tmp file */
private char	*tfname;

tmpinit()
{
	char	buf[FILESIZE];

	sprintf(buf, "%s/%s", TmpFilePath, d_tempfile);
	tfname = copystr(buf);
	tfname = mktemp(tfname);
	(void) close(creat(tfname, 0600));
#ifndef MSDOS
	tmpfd = open(tfname, 2);
#else /* MSDOS */
	tmpfd = open(tfname, 0x8002);	/* MSDOS fix	*/
#endif /* MSDOS */
	if (tmpfd == -1)
		complain("Warning: cannot create tmp file!");
}

tmpclose()
{
	if (tmpfd == -1)
		return;
	(void) close(tmpfd);
	tmpfd = -1;
	(void) unlink(tfname);
}

/* get a line at `tl' in the tmp file into `buf' which should be LBSIZE
   long */

int	Jr_Len;		/* length of Just Read Line */
private char	*getblock();

getline(addr, buf)
disk_line	addr;
register char	*buf;
{
	register char	*bp,
			*lp;

	lp = buf;
	bp = getblock(addr >> 1, READ);
	while (*lp++ = *bp++)
		;
	Jr_Len = (lp - buf) - 1;
}

/* Put `buf' and return the disk address */

disk_line
putline(buf)
char	*buf;
{
	register char	*bp,
			*lp;
	register int	nl;
	disk_line	free_ptr;

	lp = buf;
	free_ptr = DFree;
	bp = getblock(free_ptr, WRITE);
	nl = nleft;
	free_ptr = blk_round(free_ptr);
	while (*bp = *lp++) {
		if (*bp++ == '\n') {
			*--bp = 0;
			break;
		}
		if (--nl == 0) {
			free_ptr = forward_block(free_ptr);
			DFree = free_ptr;
			bp = getblock(free_ptr, WRITE);
			lp = buf;	/* start over ... */
			nl = nleft;
		}
	}
	free_ptr = DFree;
	DFree += (((lp - buf) + CH_SIZE - 1) / CH_SIZE);
	         /* (lp - buf) includes the null */
	return (free_ptr << 1);
}

/* The theory is that critical section of code inside this procedure
   will never cause a problem to occur.  Basically, we need to ensure
   that two blocks are in memory at the same time, but I think that
   this can never screw up. */

#define lockblock(addr)
#define unlockblock(addr)

disk_line
f_getputl(line, fp)
Line	*line;
register File	*fp;
{
	register char	*bp;
	register int	c,
			nl,
			max = LBSIZE;
	disk_line	free_ptr;
	char		*base;
#ifdef MSDOS
	char crleft = 0;
#endif /* MSDOS */

	free_ptr = DFree;
	base = bp = getblock(free_ptr, WRITE);
	nl = nleft;
	free_ptr = blk_round(free_ptr);
	while (--max > 0) {
#ifdef MSDOS
		if (crleft) {
		   c = crleft;
		   crleft = 0;
		} else
#endif /* MSDOS */
		c = getc(fp);
		if (c == EOF || c == '\n')
			break;
#ifdef MSDOS
		if (c == '\r') 
		    if ((crleft = getc(fp)) == '\n') {
			    crleft = 0;
			    break;
			}
#endif /* MSDOS */
		if (--nl == 0) {
			char	*newbp;
			int	nbytes;

			lockblock(free_ptr);
			DFree = free_ptr = forward_block(free_ptr);
			nbytes = bp - base;
			newbp = getblock(free_ptr, WRITE);
			nl = nleft;
			byte_copy(base, newbp, nbytes);
			bp = newbp + nbytes;
			base = newbp;
			unlockblock(free_ptr);
		}
		*bp++ = c;
	}
	*bp++ = '\0';
	free_ptr = DFree;
	DFree += (((bp - base) + CH_SIZE - 1) / CH_SIZE);
	line->l_dline = (free_ptr << 1);
	if (max == 0) {
		add_mess(" [Line too long]");
		rbell();
		return EOF;
	}
	if (c == EOF) {
		if (--bp != base)
			add_mess(" [Incomplete last line]");
		return EOF;
	}
	io_lines += 1;
	return NIL;
}

typedef struct block {
	short	b_dirty,
		b_bno;
	char	b_buf[BUFSIZ];
	struct block
		*b_LRUnext,
		*b_LRUprev,
		*b_HASHnext;
} Block;

#define HASHSIZE	7	/* Primes work best (so I'm told) */
#define B_HASH(bno)	(bno % HASHSIZE)

private Block	b_cache[NBUF],
		*bht[HASHSIZE] = {0},		/* Block hash table */
		*f_block = 0,
		*l_block = 0;
private int	max_bno = -1,
		NBlocks;

private int	(*blkio)();

private
real_blkio(b, iofcn)
register Block	*b;
register int	(*iofcn)();
{
	(void) lseek(tmpfd, (long) ((unsigned) b->b_bno) * BUFSIZ, 0);
	if ((*iofcn)(tmpfd, b->b_buf, BUFSIZ) != BUFSIZ)
		error("[Tmp file %s error; to continue editing would be dangerous]", (iofcn == read) ? "READ" : "WRITE");
}

private
fake_blkio(b, iofcn)
register Block	*b;
register int	(*iofcn)();
{
	tmpinit();
	blkio = real_blkio;
	real_blkio(b, iofcn);
}

d_cache_init()
{
	register Block	*bp,	/* Block pointer */
			**hp;	/* Hash pointer */
	register short	bno;

	for (bp = b_cache, bno = NBUF; --bno >= 0; bp++) {
		NBlocks += 1;
		bp->b_dirty = 0;
		bp->b_bno = bno;
		if (l_block == 0)
			l_block = bp;
		bp->b_LRUprev = 0;
		bp->b_LRUnext = f_block;
		if (f_block != 0)
			f_block->b_LRUprev = bp;
		f_block = bp;

		bp->b_HASHnext = *(hp = &bht[B_HASH(bno)]);
		*hp = bp;
	}
	blkio = fake_blkio;
}

SyncTmp()
{
	register Block	*b;
#ifdef IBMPC
	register int	bno = 0;
	Block	*lookup();

	/* sync the blocks in order, for floppy disks */
	for (bno = 0; bno <= max_bno; ) {
		if ((b = lookup(bno++)) && b->b_dirty) {
			(*blkio)(b, write);
			b->b_dirty = 0;
		}
	}
#else
	for (b = f_block; b != 0; b = b->b_LRUnext)
		if (b->b_dirty) {
			(*blkio)(b, write);
			b->b_dirty = 0;
		}
#endif
}

private Block *
lookup(bno)
register short	bno;
{
	register Block	*bp;

	for (bp = bht[B_HASH(bno)]; bp != 0; bp = bp->b_HASHnext)
		if (bp->b_bno == bno)
			break;
	return bp;
}

private
LRUunlink(b)
register Block	*b;
{
	if (b->b_LRUprev == 0)
		f_block = b->b_LRUnext;
	else
		b->b_LRUprev->b_LRUnext = b->b_LRUnext;
	if (b->b_LRUnext == 0)
		l_block = b->b_LRUprev;
	else
		b->b_LRUnext->b_LRUprev = b->b_LRUprev;
}

private Block *
b_unlink(bp)
register Block	*bp;
{
	register Block	*hp,
			*prev = 0;

	LRUunlink(bp);
	/* Now that we have the block, we remove it from its position
	   in the hash table, so we can THEN put it somewhere else with
	   it's new block assignment. */

	for (hp = bht[B_HASH(bp->b_bno)]; hp != 0; prev = hp, hp = hp->b_HASHnext)
		if (hp == bp)
			break;
	if (hp == 0) {
		printf("\rBlock %d missing!", bp->b_bno);
		finish(0);
	}
	if (prev)
		prev->b_HASHnext = hp->b_HASHnext;
	else
		bht[B_HASH(bp->b_bno)] = hp->b_HASHnext;

	if (bp->b_dirty) {	/* do, now, the delayed write */
		(*blkio)(bp, write);
		bp->b_dirty = 0;
	}

	return bp;
}

/* Get a block which contains at least part of the line with the address
   atl.  Returns a pointer to the block and sets the global variable
   nleft (number of good characters left in the buffer). */

private char *
getblock(atl, iof)
disk_line	atl;
{
	register int	bno,
			off;
	register Block	*bp;
	static Block	*lastb = 0;

	bno = daddr_to_bno(atl);
	off = daddr_to_off(atl);
	if (daddr_too_huge(atl))
		error("Tmp file too large.  Get help!");
	nleft = BUFSIZ - off;
	if (lastb != 0 && lastb->b_bno == bno) {
		lastb->b_dirty |= iof;
		return lastb->b_buf + off;
	}

	/* The requested block already lives in memory, so we move
	   it to the end of the LRU list (making it Most Recently Used)
	   and then return a pointer to it. */
	if (bp = lookup(bno)) {
		if (bp != l_block) {
			LRUunlink(bp);
			if (l_block == 0)
				f_block = l_block = bp;
			else
				l_block->b_LRUnext = bp;
			bp->b_LRUprev = l_block;
			l_block = bp;
			bp->b_LRUnext = 0;
		}
		if (bp->b_bno > max_bno)
			max_bno = bp->b_bno;
		bp->b_dirty |= iof;
		lastb = bp;
		return bp->b_buf + off;
	}

	/* The block we want doesn't reside in memory so we take the
	   least recently used clean block (if there is one) and use
	   it.  */
	bp = f_block;
	if (bp->b_dirty)	/* The best block is dirty ... */
		SyncTmp();

	bp = b_unlink(bp);
	if (l_block == 0)
		l_block = f_block = bp;
	else
		l_block->b_LRUnext = bp;	/* Place it at the end ... */
	bp->b_LRUprev = l_block;
	l_block = bp;
	bp->b_LRUnext = 0;		/* so it's Most Recently Used */

	bp->b_dirty = iof;
	bp->b_bno = bno;
	bp->b_HASHnext = bht[B_HASH(bno)];
	bht[B_HASH(bno)] = bp;

	/* Get the current contents of the block UNLESS this is a new
	   block that's never been looked at before, i.e., it's past
	   the end of the tmp file. */

	if (bp->b_bno <= max_bno)
		(*blkio)(bp, read);
	else
		max_bno = bno;

	lastb = bp;
	return bp->b_buf + off;
}

char *
lbptr(line)
Line	*line;
{
	return getblock(line->l_dline >> 1, READ);
}

/* save the current contents of linebuf, if it has changed */

lsave()
{
	if (curbuf == 0 || !DOLsave)	/* Nothing modified recently */
		return;

	if (strcmp(lbptr(curline), linebuf) != 0)
		SavLine(curline, linebuf);	/* Put linebuf on the disk. */
	DOLsave = 0;
}

#ifdef BACKUPFILES
file_backup(fname)
char *fname;
{
#ifndef MSDOS
	char	*s;
	register int	i;
	int	fd1,
		fd2;
	char	tmp1[BUFSIZ],
		tmp2[BUFSIZ];
 	struct stat buf;
 	int	mode;

	strcpy(tmp1, fname);
	if ((s = rindex(tmp1, '/')) == NULL)
		sprintf(tmp2, "#%s", fname);
	else {
		*s++ = '\0';
		sprintf(tmp2, "%s/#%s", tmp1, s);
	}

	if ((fd1 = open(fname, 0)) < 0)
		return;

	/* create backup file with same mode as input file */
	if (fstat(fd1, &buf) != 0)
		mode = CreatMode;
	else
		mode = buf.st_mode;
		
	if ((fd2 = creat(tmp2, mode)) < 0) {
		(void) close(fd1);
		return;
	}
	while ((i = read(fd1, tmp1, sizeof(tmp1))) > 0)
		write(fd2, tmp1, i);
#ifdef BSD4_2
	(void) fsync(fd2);
#endif
	(void) close(fd2);
	(void) close(fd1);
#else /* MSDOS */
	char	*dot,
			*slash,
			tmp[FILESIZE];
	
	strcpy(tmp, fname);
	slash = basename(tmp);
	if (dot = rindex(slash, '.')) {
	   if (!stricmp(dot,".bak")) 
		return;
	   else *dot = 0;
	}
	strcat(tmp, ".bak");
	unlink(tmp);
	rename(fname, tmp);
#endif /* MSDOS */
}
#endif
