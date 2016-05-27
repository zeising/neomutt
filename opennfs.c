#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <nfs/nfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

#ifndef  NFS_SUPER_MAGIC
# define NFS_SUPER_MAGIC 0x6969
#endif

int opennfs(const char *path, int flags, int mode)
{
    char tmplock[NFS_MAXPATHLEN+1], sysname[256];
    char *slash, *ptr, *dir, *base, *clear = (char*)0;
    struct stat ps, ts;
    struct statfs fs;
    ssize_t len;
    int ret;

    if ((flags & (O_WRONLY|O_RDWR)) == 0)
	goto safe;

    if ((flags & (O_EXCL|O_CREAT)) != (O_EXCL|O_CREAT))
	goto safe;

#if defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#endif

    ret = -1;
    if ((clear = strdup(path)) == (char*)0)
	goto err;
    dir = dirname(clear);

    if ((ret = (statfs(dir, &fs))) < 0)
	goto err;

    if (fs.f_type != NFS_SUPER_MAGIC)
	goto safe;

    if ((ret = gethostname(sysname, sizeof(sysname))) < 0)
	goto err;

    ret = -1;
    ptr = &tmplock[0];
    if (((len = snprintf(ptr, NFS_MAXPATHLEN, "%s/.%s-XXXXXX", dir, sysname)) < 0) || (len >= NFS_MAXPATHLEN))
	goto err;
    ptr += len;
    slash = ptr;

    free(clear);
    clear = (char*)0;

    if (mkdtemp(tmplock) == (char*)0)
	goto err;

    ret = -1;
    if ((clear = strdup(path)) == (char*)0)
	goto rmd;
    base = basename(clear);

    ret = -1;
    if (((len = snprintf(ptr, NFS_MAXPATHLEN - len, "/%s", base)) < 0) || (len >= (NFS_MAXPATHLEN - len)))
	goto rmd;

    free(clear);
    clear = (char*)0;

    if ((ret = open(tmplock, flags, mode)) < 0)
	goto rmd;

    errno = 0;
    do {
	len = write(ret, "0", 2);
    } while ((len < 0) && (errno == EINTR));
    close(ret);

    ret = -1;
    errno = EBADF;
    if (len != 2)
	goto unl;

    errno = 0;
    if ((ret = lstat(tmplock, &ts)) < 0)
	goto unl;

    if (((ret = link(tmplock, path)) < 0) && (errno == EEXIST))
	goto unl;

    if ((ret = lstat(path, &ps)) < 0)
	goto unl;

    ret = -1;
    errno = EEXIST;
    if (ps.st_nlink != 2)
	goto unl;
    if ((ps.st_rdev != ts.st_rdev) || (ps.st_ino != ts.st_ino))
	goto unl;

    errno = 0;
    flags |= O_TRUNC;
    flags &= ~(O_EXCL|O_CREAT);
    ret = open(path, flags, mode);
unl:
    unlink(tmplock);
rmd:
    *slash = '\0';
    rmdir(tmplock);
err:
    if (clear) free(clear);
    return ret;
safe:
    if (clear) free(clear);
    return open(path, flags, mode);
}
