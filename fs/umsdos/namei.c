/*
 *  linux/fs/umsdos/namei.c
 *
 *      Written 1993 by Jacques Gelinas 
 *      Inspired from linux/fs/msdos/... by Werner Almesberger
 *
 * Maintain and access the --linux alternate directory file.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/msdos_fs.h>
#include <linux/umsdos_fs.h>
#include <linux/malloc.h>

#if 1
/*
 * Wait for creation exclusivity.
 * Return 0 if the dir was already available.
 * Return 1 if a wait was necessary.
 * When 1 is return, it means a wait was done. It does not
 * mean the directory is available.
 */
static int umsdos_waitcreate (struct inode *dir)
{
	int ret = 0;

	if (dir->u.umsdos_i.u.dir_info.creating
	    && dir->u.umsdos_i.u.dir_info.pid != current->pid) {
		sleep_on (&dir->u.umsdos_i.u.dir_info.p);
		ret = 1;
	}
	return ret;
}

/*
 * Wait for any lookup process to finish
 */
static void umsdos_waitlookup (struct inode *dir)
{
	while (dir->u.umsdos_i.u.dir_info.looking) {
		sleep_on (&dir->u.umsdos_i.u.dir_info.p);
	}
}

/*
 * Lock all other process out of this directory.
 */
void umsdos_lockcreate (struct inode *dir)
{
	/* #Specification: file creation / not atomic
	 * File creation is a two step process. First we create (allocate)
	 * an entry in the EMD file and then (using the entry offset) we
	 * build a unique name for MSDOS. We create this name in the msdos
	 * space.
	 * 
	 * We have to use semaphore (sleep_on/wake_up) to prevent lookup
	 * into a directory when we create a file or directory and to
	 * prevent creation while a lookup is going on. Since many lookup
	 * may happen at the same time, the semaphore is a counter.
	 * 
	 * Only one creation is allowed at the same time. This protection
	 * may not be necessary. The problem arise mainly when a lookup
	 * or a readdir is done while a file is partially created. The
	 * lookup process see that as a "normal" problem and silently
	 * erase the file from the EMD file. Normal because a file
	 * may be erased during a MSDOS session, but not removed from
	 * the EMD file.
	 * 
	 * The locking is done on a directory per directory basis. Each
	 * directory inode has its wait_queue.
	 * 
	 * For some operation like hard link, things even get worse. Many
	 * creation must occur at once (atomic). To simplify the design
	 * a process is allowed to recursively lock the directory for
	 * creation. The pid of the locking process is kept along with
	 * a counter so a second level of locking is granted or not.
	 */
	/*
	 * Wait for any creation process to finish except
	 * if we (the process) own the lock
	 */
	while (umsdos_waitcreate (dir) != 0);
	dir->u.umsdos_i.u.dir_info.creating++;
	dir->u.umsdos_i.u.dir_info.pid = current->pid;
	umsdos_waitlookup (dir);
}

/*
 * Lock all other process out of those two directories.
 */
static void umsdos_lockcreate2 (struct inode *dir1, struct inode *dir2)
{
	/*
	 * We must check that both directory are available before
	 * locking anyone of them. This is to avoid some deadlock.
	 * Thanks to dglaude@is1.vub.ac.be (GLAUDE DAVID) for pointing
	 * this to me.
	 */
	while (1) {
		if (umsdos_waitcreate (dir1) == 0
		    && umsdos_waitcreate (dir2) == 0) {
			/* We own both now */
			dir1->u.umsdos_i.u.dir_info.creating++;
			dir1->u.umsdos_i.u.dir_info.pid = current->pid;
			dir2->u.umsdos_i.u.dir_info.creating++;
			dir2->u.umsdos_i.u.dir_info.pid = current->pid;
			break;
		}
	}
	umsdos_waitlookup (dir1);
	umsdos_waitlookup (dir2);
}

/*
 * Wait until creation is finish in this directory.
 */
void umsdos_startlookup (struct inode *dir)
{
	while (umsdos_waitcreate (dir) != 0);
	dir->u.umsdos_i.u.dir_info.looking++;
}

/*
 * Unlock the directory.
 */
void umsdos_unlockcreate (struct inode *dir)
{
	dir->u.umsdos_i.u.dir_info.creating--;
	if (dir->u.umsdos_i.u.dir_info.creating < 0) {
		printk ("UMSDOS: dir->u.umsdos_i.u.dir_info.creating < 0: %d"
			,dir->u.umsdos_i.u.dir_info.creating);
	}
	wake_up (&dir->u.umsdos_i.u.dir_info.p);
}

/*
 * Tell directory lookup is over.
 */
void umsdos_endlookup (struct inode *dir)
{
	dir->u.umsdos_i.u.dir_info.looking--;
	if (dir->u.umsdos_i.u.dir_info.looking < 0) {
		printk ("UMSDOS: dir->u.umsdos_i.u.dir_info.looking < 0: %d"
			,dir->u.umsdos_i.u.dir_info.looking);
	}
	wake_up (&dir->u.umsdos_i.u.dir_info.p);
}

#else
static void umsdos_lockcreate (struct inode *dir)
{
}
static void umsdos_lockcreate2 (struct inode *dir1, struct inode *dir2)
{
}
void umsdos_startlookup (struct inode *dir)
{
}
static void umsdos_unlockcreate (struct inode *dir)
{
}
void umsdos_endlookup (struct inode *dir)
{
}

#endif

static int umsdos_nevercreat (
				     struct inode *dir,
				     struct dentry *dentry,
				     int errcod)
{				/* Length of the name */
	const char *name = dentry->d_name.name;
	int len = dentry->d_name.len;
	int ret = 0;

	if (umsdos_is_pseudodos (dir, dentry)) {
		/* #Specification: pseudo root / any file creation /DOS
		 * The pseudo sub-directory /DOS can't be created!
		 * EEXIST is returned.
		 * 
		 * The pseudo sub-directory /DOS can't be removed!
		 * EPERM is returned.
		 */
		ret = -EPERM;
		ret = errcod;
	} else if (name[0] == '.'
		   && (len == 1 || (len == 2 && name[1] == '.'))) {
		/* #Specification: create / . and ..
		 * If one try to creates . or .., it always fail and return
		 * EEXIST.
		 * 
		 * If one try to delete . or .., it always fail and return
		 * EPERM.
		 * 
		 * This should be test at the VFS layer level to avoid
		 * duplicating this in all file systems. Any comments ?
		 */
		ret = errcod;
	}
	return ret;
}

/*
 * Add a new file (ordinary or special) into the alternate directory.
 * The file is added to the real MSDOS directory. If successful, it
 * is then added to the EDM file.
 * 
 * Return the status of the operation. 0 mean success.
 */
static int umsdos_create_any (
				     struct inode *dir,
				     struct dentry *dentry,	/* name/length etc */
				     int mode,	/* Permission bit + file type ??? */
				     int rdev,	/* major, minor or 0 for ordinary file and symlinks */
				     char flags
)
{				/* Will hold the inode of the newly created file */

	int ret;
	struct dentry *fake;

	Printk (("umsdos_create_any /mn/: create %.*s in dir=%lu - nevercreat=/", (int) dentry->d_name.len, dentry->d_name.name, dir->i_ino));
	check_dentry_path (dentry, "umsdos_create_any");
	ret = umsdos_nevercreat (dir, dentry, -EEXIST);
	Printk (("%d/\n", ret));
	if (ret == 0) {
		struct umsdos_info info;

		ret = umsdos_parse (dentry->d_name.name, dentry->d_name.len, &info);

		if (ret == 0) {
			info.entry.mode = mode;
			info.entry.rdev = rdev;
			info.entry.flags = flags;
			info.entry.uid = current->fsuid;
			info.entry.gid = (dir->i_mode & S_ISGID)
			    ? dir->i_gid : current->fsgid;
			info.entry.ctime = info.entry.atime = info.entry.mtime
			    = CURRENT_TIME;
			info.entry.nlink = 1;
			umsdos_lockcreate (dir);
			ret = umsdos_newentry (dir, &info);
			if (ret == 0) {
				inc_count (dir);
				fake = creat_dentry (info.fake.fname, info.fake.len, NULL, dentry->d_parent);	/* create short name dentry */
				ret = msdos_create (dir, fake, S_IFREG | 0777);
				if (ret == 0) {
					struct inode *inode = fake->d_inode;

					umsdos_lookup_patch (dir, inode, &info.entry, info.f_pos);
					Printk (("inode %p[%lu], count=%d ", inode, inode->i_ino, inode->i_count));
					Printk (("Creation OK: [dir %lu] %.*s pid=%d pos %ld\n", dir->i_ino,
						 info.fake.len, info.fake.fname, current->pid, info.f_pos));

					check_dentry_path (dentry, "umsdos_create_any: BEG dentry");
					check_dentry_path (fake, "umsdos_create_any: BEG fake");
					d_instantiate (dentry, inode);	/* long name also gets inode info */
					inc_count (fake->d_inode); /* we must do it, since dput(fake) will iput(our_inode) which we still need for long name (dentry) */
					/* dput (fake);	/ * FIXME: is this OK ? we try to kill short name dentry that we don't need */
					check_dentry_path (dentry, "umsdos_create_any: END dentry");
					check_dentry_path (fake, "umsdos_create_any: END fake");

				} else {
					/* #Specification: create / file exist in DOS
					 * Here is a situation. Trying to create a file with
					 * UMSDOS. The file is unknown to UMSDOS but already
					 * exist in the DOS directory.
					 * 
					 * Here is what we are NOT doing:
					 * 
					 * We could silently assume that everything is fine
					 * and allows the creation to succeed.
					 * 
					 * It is possible not all files in the partition
					 * are mean to be visible from linux. By trying to create
					 * those file in some directory, one user may get access
					 * to those file without proper permissions. Looks like
					 * a security hole to me. Off course sharing a file system
					 * with DOS is some kind of security hole :-)
					 * 
					 * So ?
					 * 
					 * We return EEXIST in this case.
					 * The same is true for directory creation.
					 */
					if (ret == -EEXIST) {
						printk ("UMSDOS: out of sync, Creation error [%ld], "
							"deleting %.*s %d %d pos %ld\n", dir->i_ino
							,info.fake.len, info.fake.fname, -ret, current->pid, info.f_pos);
					}
					umsdos_delentry (dir, &info, 0);
				}
				Printk (("umsdos_create %.*s ret = %d pos %ld\n",
					 info.fake.len, info.fake.fname, ret, info.f_pos));
			}
			umsdos_unlockcreate (dir);
		}
	}
	/* d_add(dentry,dir); /mn/ FIXME: msdos_create already did this for short name ! */
	return ret;
}

/*
 * Initialise the new_entry from the old for a rename operation.
 * (Only useful for umsdos_rename_f() below).
 */
static void umsdos_ren_init (
				    struct umsdos_info *new_info,
				    struct umsdos_info *old_info,
				    int flags)
{				/* 0 == copy flags from old_name */
	/* != 0, this is the value of flags */
	new_info->entry.mode = old_info->entry.mode;
	new_info->entry.rdev = old_info->entry.rdev;
	new_info->entry.uid = old_info->entry.uid;
	new_info->entry.gid = old_info->entry.gid;
	new_info->entry.ctime = old_info->entry.ctime;
	new_info->entry.atime = old_info->entry.atime;
	new_info->entry.mtime = old_info->entry.mtime;
	new_info->entry.flags = flags ? flags : old_info->entry.flags;
	new_info->entry.nlink = old_info->entry.nlink;
}

#define chkstk() \
if (STACK_MAGIC != *(unsigned long *)current->kernel_stack_page){\
    printk(KERN_ALERT "UMSDOS: %s magic %x != %lx ligne %d\n" \
	   , current->comm,STACK_MAGIC \
	   ,*(unsigned long *)current->kernel_stack_page \
	   ,__LINE__); \
}

#undef chkstk
#define chkstk() do { } while (0);

/*
 * Rename a file (move) in the file system.
 */
 
static int umsdos_rename_f (
				   struct inode *old_dir,
				   struct dentry *old_dentry,
				   struct inode *new_dir,
				   struct dentry *new_dentry,
				   int flags)
{				/* 0 == copy flags from old_name */
				/* != 0, this is the value of flags */
	int ret = -EPERM;
	struct umsdos_info old_info;
	int old_ret = umsdos_parse (old_dentry->d_name.name, old_dentry->d_name.len, &old_info);
	struct umsdos_info new_info;
	int new_ret = umsdos_parse (new_dentry->d_name.name, new_dentry->d_name.len, &new_info);

	check_dentry_path (old_dentry, "umsdos_rename_f OLD");
	check_dentry_path (new_dentry, "umsdos_rename_f OLD");

	chkstk ();
	Printk (("umsdos_rename %d %d ", old_ret, new_ret));
	if (old_ret == 0 && new_ret == 0) {
		umsdos_lockcreate2 (old_dir, new_dir);
		chkstk ();
		Printk (("old findentry "));
		ret = umsdos_findentry (old_dir, &old_info, 0);
		chkstk ();
		Printk (("ret %d ", ret));
		if (ret == 0) {
			/* check sticky bit on old_dir */
			if (!(old_dir->i_mode & S_ISVTX) || capable (CAP_FOWNER) ||
			    current->fsuid == old_info.entry.uid ||
			    current->fsuid == old_dir->i_uid) {
				/* Does new_name already exist? */
				Printk (("new findentry "));
				ret = umsdos_findentry (new_dir, &new_info, 0);
				if (ret != 0 ||		/* if destination file exists, are we allowed to replace it ? */
				    !(new_dir->i_mode & S_ISVTX) || capable (CAP_FOWNER) ||
				  current->fsuid == new_info.entry.uid ||
				    current->fsuid == new_dir->i_uid) {
					Printk (("new newentry "));
					umsdos_ren_init (&new_info, &old_info, flags);
					ret = umsdos_newentry (new_dir, &new_info);
					chkstk ();
					Printk (("ret %d %d ", ret, new_info.fake.len));
					if (ret == 0) {
						struct dentry *old, *new, *d_old_dir, *dret;
						struct inode *oldid = NULL;

						d_old_dir = creat_dentry ("@d_old_dir@", 11, old_dir, NULL);
						dret = compat_umsdos_real_lookup (d_old_dir, old_info.fake.fname, old_info.fake.len);
						if (dret) oldid = dret->d_inode;
						old = creat_dentry (old_info.fake.fname, old_info.fake.len, oldid, old_dentry->d_parent);

						new = creat_dentry (new_info.fake.fname, new_info.fake.len, NULL, new_dentry->d_parent);

						Printk (("msdos_rename "));
						inc_count (old_dir);
						inc_count (new_dir);	/* Both inode are needed later */
						
						check_dentry_path (old, "umsdos_rename_f OLD2");
						check_dentry_path (new, "umsdos_rename_f NEW2");
						
						ret = msdos_rename (old_dir, old, new_dir, new);
						chkstk ();
						Printk (("after m_rename ret %d ", ret));
						kill_dentry (old);
						kill_dentry (new);

						if (ret != 0) {
							umsdos_delentry (new_dir, &new_info, S_ISDIR (new_info.entry.mode));
							chkstk ();
						} else {
							ret = umsdos_delentry (old_dir, &old_info, S_ISDIR (old_info.entry.mode));
							chkstk ();
							if (ret == 0) {
								/*
								 * This umsdos_lookup_x does not look very useful.
								 * It makes sure that the inode of the file will
								 * be correctly setup (umsdos_patch_inode()) in
								 * case it is already in use.
								 * 
								 * Not very efficient ...
								 */
								struct inode *inode;

								inc_count (new_dir);
								PRINTK ((KERN_DEBUG "rename lookup len %d %d -- ", new_len, new_info.entry.flags));
								ret = umsdos_lookup_x (new_dir, new_dentry, 0);
								inode = new_dentry->d_inode;
								chkstk ();
								if (ret != 0) {
									printk ("UMSDOS: partial rename for file %.*s\n"
										,new_info.entry.name_len, new_info.entry.name);
								} else {
									/*
									 * Update f_pos so notify_change will succeed
									 * if the file was already in use.
									 */
									umsdos_set_dirinfo (inode, new_dir, new_info.f_pos);
									d_move (old_dentry, new_dentry);
									chkstk ();
									/* iput (inode); FIXME */
								}
							}
							fin_dentry (dret);
						}
					}
				} else {
					/* sticky bit set on new_dir */
					Printk (("sticky set on new "));
					ret = -EPERM;
				}
			} else {
				/* sticky bit set on old_dir */
				Printk (("sticky set on old "));
				ret = -EPERM;
			}
		}
		Printk ((KERN_DEBUG "umsdos_rename_f: unlocking dirs...\n"));
		umsdos_unlockcreate (old_dir);
		umsdos_unlockcreate (new_dir);
	}
	check_dentry_path (old_dentry, "umsdos_rename_f OLD3");
	check_dentry_path (new_dentry, "umsdos_rename_f NEW3");

	Printk ((" _ret=%d\n", ret));
	return ret;
}

/*
 * Setup un Symbolic link or a (pseudo) hard link
 * Return a negative error code or 0 if OK.
 */
static int umsdos_symlink_x (
				    struct inode *dir,
				    struct dentry *dentry,
				    const char *symname,	/* name will point to this path */
				    int mode,
				    char flags)
{
	/* #Specification: symbolic links / strategy
	 * A symbolic link is simply a file which hold a path. It is
	 * implemented as a normal MSDOS file (not very space efficient :-()
	 * 
	 * I see 2 different way to do it. One is to place the link data
	 * in unused entry of the EMD file. The other is to have a separate
	 * file dedicated to hold all symbolic links data.
	 * 
	 * Let's go for simplicity...
	 */


	int ret;

	inc_count (dir);	/* We keep the inode in case we need it */
	/* later */
	ret = umsdos_create_any (dir, dentry, mode, 0, flags);
	Printk (("umsdos_symlink ret %d ", ret));
	if (ret == 0) {
		int len = strlen (symname);
		struct file filp;

		fill_new_filp (&filp, dentry);
		filp.f_pos = 0;

		/* Make the inode acceptable to MSDOS FIXME */
		Printk ((KERN_WARNING "umsdos_symlink_x:  /mn/ is this OK?\n"));
		Printk ((KERN_WARNING "   symname=%s ; dentry name=%.*s (ino=%lu)\n", symname, (int) dentry->d_name.len, dentry->d_name.name, dentry->d_inode->i_ino));
		ret = umsdos_file_write_kmem_real (&filp, symname, len);
		/* dput(dentry); ?? where did this come from FIXME */
		
		if (ret >= 0) {
			if (ret != len) {
				ret = -EIO;
				printk ("UMSDOS: "
				     "Can't write symbolic link data\n");
			} else {
				ret = 0;
			}
		}
		if (ret != 0) {
			UMSDOS_unlink (dir, dentry);
			dir = NULL;
		}
	}
	/* d_instantiate(dentry,dir);   //already done in umsdos_create_any. */
	Printk (("\n"));
	return ret;
}

/*
 * Setup un Symbolic link.
 * Return a negative error code or 0 if OK.
 */
int UMSDOS_symlink (
			   struct inode *dir,
			   struct dentry *dentry,
			   const char *symname
)
{
	return umsdos_symlink_x (dir, dentry, symname, S_IFLNK | 0777, 0);
}

/*
 * Add a link to an inode in a directory
 */
int UMSDOS_link (
			struct dentry *olddentry,
			struct inode *dir,
			struct dentry *dentry)
{
	struct inode *oldinode = olddentry->d_inode;

	/* #Specification: hard link / strategy
	 * Hard links are difficult to implement on top of an MS-DOS FAT file
	 * system. Unlike Unix file systems, there are no inodes. A directory
	 * entry holds the functionality of the inode and the entry.
	 * 
	 * We will used the same strategy as a normal Unix file system
	 * (with inodes) except we will do it symbolically (using paths).
	 * 
	 * Because anything can happen during a DOS session (defragment,
	 * directory sorting, etc.), we can't rely on an MS-DOS pseudo
	 * inode number to record the link. For this reason, the link
	 * will be done using hidden symbolic links. The following
	 * scenario illustrates how it works.
	 * 
	 * Given a file /foo/file
	 * 
	 * #
	 * ln /foo/file /tmp/file2
	 * 
	 * become internally
	 * 
	 * mv /foo/file /foo/-LINK1
	 * ln -s /foo/-LINK1 /foo/file
	 * ln -s /foo/-LINK1 /tmp/file2
	 * #
	 * 
	 * Using this strategy, we can operate on /foo/file or /foo/file2.
	 * We can remove one and keep the other, like a normal Unix hard link.
	 * We can rename /foo/file or /tmp/file2 independently.
	 * 
	 * The entry -LINK1 will be hidden. It will hold a link count.
	 * When all link are erased, the hidden file is erased too.
	 */
	/* #Specification: weakness / hard link
	 * The strategy for hard link introduces a side effect that
	 * may or may not be acceptable. Here is the sequence
	 * 
	 * #
	 * mkdir subdir1
	 * touch subdir1/file
	 * mkdir subdir2
	 * ln    subdir1/file subdir2/file
	 * rm    subdir1/file
	 * rmdir subdir1
	 * rmdir: subdir1: Directory not empty
	 * #
	 * 
	 * This happen because there is an invisible file (--link) in
	 * subdir1 which is referenced by subdir2/file.
	 * 
	 * Any idea ?
	 */
	/* #Specification: weakness / hard link / rename directory
	 * Another weakness of hard link come from the fact that
	 * it is based on hidden symbolic links. Here is an example.
	 * 
	 * #
	 * mkdir /subdir1
	 * touch /subdir1/file
	 * mkdir /subdir2
	 * ln    /subdir1/file subdir2/file
	 * mv    /subdir1 subdir3
	 * ls -l /subdir2/file
	 * #
	 * 
	 * Since /subdir2/file is a hidden symbolic link
	 * to /subdir1/..hlinkNNN, accessing it will fail since
	 * /subdir1 does not exist anymore (has been renamed).
	 */
	int ret = 0;

	if (S_ISDIR (oldinode->i_mode)) {
		/* #Specification: hard link / directory
		 * A hard link can't be made on a directory. EPERM is returned
		 * in this case.
		 */
		ret = -EPERM;
	} else if ((ret = umsdos_nevercreat (dir, dentry, -EPERM)) == 0) {
		struct inode *olddir;

		ret = umsdos_get_dirowner (oldinode, &olddir);
		Printk (("umsdos_link dir_owner = %lu -> %p [%d] ",
			 oldinode->u.umsdos_i.i_dir_owner, olddir, olddir->i_count));
		if (ret == 0) {
			struct umsdos_dirent entry;

			umsdos_lockcreate2 (dir, olddir);
			ret = umsdos_inode2entry (olddir, oldinode, &entry);
			if (ret == 0) {
				Printk (("umsdos_link :%.*s: ino %lu flags %d "
					 ,entry.name_len, entry.name
					 ,oldinode->i_ino, entry.flags));
				if (!(entry.flags & UMSDOS_HIDDEN)) {
					/* #Specification: hard link / first hard link
					 * The first time a hard link is done on a file, this
					 * file must be renamed and hidden. Then an internal
					 * symbolic link must be done on the hidden file.
					 * 
					 * The second link is done after on this hidden file.
					 * 
					 * It is expected that the Linux MSDOS file system
					 * keeps the same pseudo inode when a rename operation
					 * is done on a file in the same directory.
					 */
					struct umsdos_info info;

					ret = umsdos_newhidden (olddir, &info);
					if (ret == 0) {
						Printk (("olddir[%d] ", olddir->i_count));
						ret = umsdos_rename_f (olddentry->d_inode, olddentry, dir, dentry, UMSDOS_HIDDEN);
						if (ret == 0) {
							char *path = (char *) kmalloc (PATH_MAX, GFP_KERNEL);

							if (path == NULL) {
								ret = -ENOMEM;
							} else {
								struct dentry *temp;

								temp = creat_dentry (entry.name, entry.name_len, NULL, NULL);
								Printk (("olddir[%d] ", olddir->i_count));
								ret = umsdos_locate_path (oldinode, path);
								Printk (("olddir[%d] ", olddir->i_count));
								if (ret == 0) {
									inc_count (olddir);
									ret = umsdos_symlink_x (olddir, temp, path, S_IFREG | 0777, UMSDOS_HLINK);
									if (ret == 0) {
										inc_count (dir);
										ret = umsdos_symlink_x (dir, dentry, path, S_IFREG | 0777, UMSDOS_HLINK);
									}
								}
								kfree (path);
							}
						}
					}
				} else {
					char *path = (char *) kmalloc (PATH_MAX, GFP_KERNEL);

					if (path == NULL) {
						ret = -ENOMEM;
					} else {
						ret = umsdos_locate_path (oldinode, path);
						if (ret == 0) {
							inc_count (dir);
							ret = umsdos_symlink_x (dir, dentry, path, S_IFREG | 0777, UMSDOS_HLINK);
						}
						kfree (path);
					}
				}
			}
			umsdos_unlockcreate (olddir);
			umsdos_unlockcreate (dir);
		}
		/* iput (olddir); FIXME */
	}
	if (ret == 0) {
		struct iattr newattrs;

		oldinode->i_nlink++;
		newattrs.ia_valid = 0;
		ret = UMSDOS_notify_change (olddentry, &newattrs);
	}
/*  dput (olddentry);
 * dput (dentry); FIXME.... */

	Printk (("umsdos_link %d\n", ret));
	return ret;
}



/*
 * Add a new file into the alternate directory.
 * The file is added to the real MSDOS directory. If successful, it
 * is then added to the EDM file.
 * 
 * Return the status of the operation. 0 mean success.
 */
int UMSDOS_create (
			  struct inode *dir,
			  struct dentry *dentry,
			  int mode	/* Permission bit + file type ??? */
)
{				/* Will hold the inode of the newly created file */
	int ret;
	Printk ((KERN_ERR "UMSDOS_create: entering\n"));
	check_dentry_path (dentry, "UMSDOS_create START");
	ret = umsdos_create_any (dir, dentry, mode, 0, 0);
	check_dentry_path (dentry, "UMSDOS_create END");
	return ret;
}



/*
 * Add a sub-directory in a directory
 */
int UMSDOS_mkdir (
			 struct inode *dir,
			 struct dentry *dentry,
			 int mode)
{
	int ret = umsdos_nevercreat (dir, dentry, -EEXIST);

	if (ret == 0) {
		struct umsdos_info info;

		ret = umsdos_parse (dentry->d_name.name, dentry->d_name.len, &info);
		Printk (("umsdos_mkdir %d\n", ret));
		if (ret == 0) {
			info.entry.mode = mode | S_IFDIR;
			info.entry.rdev = 0;
			info.entry.uid = current->fsuid;
			info.entry.gid = (dir->i_mode & S_ISGID)
			    ? dir->i_gid : current->fsgid;
			info.entry.ctime = info.entry.atime = info.entry.mtime
			    = CURRENT_TIME;
			info.entry.flags = 0;
			umsdos_lockcreate (dir);
			info.entry.nlink = 1;
			ret = umsdos_newentry (dir, &info);
			Printk (("newentry %d ", ret));
			if (ret == 0) {
				struct dentry *temp, *tdir;

				tdir = creat_dentry ("@mkd-dir@", 9, dir, NULL);
				temp = creat_dentry (info.fake.fname, info.fake.len, NULL, tdir);
				inc_count (dir);
				ret = msdos_mkdir (dir, temp, mode);

				if (ret != 0) {
					umsdos_delentry (dir, &info, 1);
					/* #Specification: mkdir / Directory already exist in DOS
					 * We do the same thing as for file creation.
					 * For all user it is an error.
					 */
				} else {
					/* #Specification: mkdir / umsdos directory / create EMD
					 * When we created a new sub-directory in a UMSDOS
					 * directory (one with full UMSDOS semantic), we
					 * create immediately an EMD file in the new
					 * sub-directory so it inherit UMSDOS semantic.
					 */
					struct inode *subdir=NULL;
					struct dentry *d_dir, *dret;

					d_dir = creat_dentry ("@d_dir5@", 7, dir, NULL);
					dret = compat_umsdos_real_lookup (d_dir, info.fake.fname, info.fake.len);
					if (dret) {
						struct dentry *tdentry,	*tdsub;

						subdir = dret->d_inode;

						tdsub = creat_dentry ("@mkd-emd@", 9, subdir, NULL);
						tdentry = creat_dentry (UMSDOS_EMD_FILE, UMSDOS_EMD_NAMELEN, NULL, tdsub);
						ret = msdos_create (subdir, tdentry, S_IFREG | 0777);
						kill_dentry (tdentry);	/* we don't want empty EMD file to be visible ! too bad kill_dentry does nothing at the moment :-)  FIXME */
						kill_dentry (tdsub);
						umsdos_setup_dir_inode (subdir);	/* this should setup dir so it is promoted to EMD, and EMD file is not visible inside it */
						subdir = NULL;
						d_instantiate (dentry, temp->d_inode);
						/* iput (result); FIXME */
						fin_dentry (dret);
					}
					if (ret < 0) {
						printk ("UMSDOS: Can't create empty --linux-.---\n");
					}
					/*iput (subdir); FIXME*/
				}
			}
			umsdos_unlockcreate (dir);
		}
	}
	Printk (("umsdos_mkdir %d\n", ret));
	/* dput (dentry); / * FIXME /mn/ */
	return ret;
}

/*
 * Add a new device special file into a directory.
 */
int UMSDOS_mknod (
			 struct inode *dir,
			 struct dentry *dentry,
			 int mode,
			 int rdev)
{
	/* #Specification: Special files / strategy
	 * Device special file, pipes, etc ... are created like normal
	 * file in the msdos file system. Of course they remain empty.
	 * 
	 * One strategy was to create those files only in the EMD file
	 * since they were not important for MSDOS. The problem with
	 * that, is that there were not getting inode number allocated.
	 * The MSDOS filesystems is playing a nice game to fake inode
	 * number, so why not use it.
	 * 
	 * The absence of inode number compatible with those allocated
	 * for ordinary files was causing major trouble with hard link
	 * in particular and other parts of the kernel I guess.
	 */

	int ret;
	
	check_dentry_path (dentry, "UMSDOS_mknod START");
	ret = umsdos_create_any (dir, dentry, mode, rdev, 0);
	check_dentry_path (dentry, "UMSDOS_mknod END");

	/* dput(dentry); / * /mn/ FIXME! */
	return ret;
}

/*
 * Remove a sub-directory.
 */
int UMSDOS_rmdir (
			 struct inode *dir,
			 struct dentry *dentry)
{
	/* #Specification: style / iput strategy
	 * In the UMSDOS project, I am trying to apply a single
	 * programming style regarding inode management.  Many
	 * entry points are receiving an inode to act on, and must
	 * do an iput() as soon as they are finished with
	 * the inode.
	 * 
	 * For simple cases, there is no problem.  When you introduce
	 * error checking, you end up with many iput() calls in the
	 * code.
	 * 
	 * The coding style I use all around is one where I am trying
	 * to provide independent flow logic (I don't know how to
	 * name this).  With this style, code is easier to understand
	 * but you must do iput() everywhere.  Here is an example
	 * of what I am trying to avoid.
	 * 
	 * #
	 * if (a){
	 * ...
	 * if(b){
	 * ...
	 * }
	 * ...
	 * if (c){
	 * // Complex state. Was b true? 
	 * ...
	 * }
	 * ...
	 * }
	 * // Weird state
	 * if (d){
	 * // ...
	 * }
	 * // Was iput finally done?
	 * return status;
	 * #
	 * 
	 * Here is the style I am using.  Still sometimes I do the
	 * first when things are very simple (or very complicated :-( ).
	 * 
	 * #
	 * if (a){
	 * if (b){
	 * ...
	 * }else if (c){
	 * // A single state gets here.
	 * }
	 * }else if (d){
	 * ...
	 * }
	 * return status;
	 * #
	 * 
	 * Again, while this help clarifying the code, I often get a lot
	 * of iput(), unlike the first style, where I can place few 
	 * "strategic" iput(). "strategic" also mean, more difficult
	 * to place.
	 * 
	 * So here is the style I will be using from now on in this project.
	 * There is always an iput() at the end of a function (which has
	 * to do an iput()). One iput by inode. There is also one iput()
	 * at the places where a successful operation is achieved. This
	 * iput() is often done by a sub-function (often from the msdos
	 * file system).  So I get one too many iput()?  At the place
	 * where an iput() is done, the inode is simply nulled, disabling
	 * the last one.
	 * 
	 * #
	 * if (a){
	 * if (b){
	 * ...
	 * }else if (c){
	 * msdos_rmdir(dir,...);
	 * dir = NULL;
	 * }
	 * }else if (d){
	 * ...
	 * }
	 * iput (dir);
	 * return status;
	 * #
	 * 
	 * Note that the umsdos_lockcreate() and umsdos_unlockcreate() function
	 * pair goes against this practice of "forgetting" the inode as soon
	 * as possible.
	 */

	int ret;

	ret = umsdos_nevercreat (dir, dentry, -EPERM);
	if (ret == 0) {
		/* volatile - DELME: I see no reason vor volatile /mn/ */ struct inode *sdir;

		inc_count (dir);
		ret = umsdos_lookup_x (dir, dentry, 0);
		sdir = dentry->d_inode;
		Printk (("rmdir lookup %d ", ret));
		if (ret == 0) {
			int empty;

			umsdos_lockcreate (dir);

			Printk ((" /mn/ rmdir: FIXME EBUSY TEST: hmmm, i_count is %d > 1 -- FAKING!\n", sdir->i_count));
			sdir->i_count = 1;	/* /mn/ FIXME! DELME! FOR TEST ONLY ! */

			if (sdir->i_count > 1) {
				Printk ((" /mn/ rmdir: FIXME EBUSY: hmmm, i_count is %d > 1 -- FAKING!\n", sdir->i_count));
				ret = -EBUSY;
			} else if ((empty = umsdos_isempty (sdir)) != 0) {
				struct dentry *tdentry, *tedir;

				tedir = creat_dentry ("@emd-rmd@", 9, dir, NULL);
				tdentry = creat_dentry (UMSDOS_EMD_FILE, UMSDOS_EMD_NAMELEN, NULL, tedir);
				umsdos_real_lookup (dir, tdentry);	/* fill inode part */
				Printk (("isempty %d i_count %d ", empty, sdir->i_count));
				/* check sticky bit */
				if (!(dir->i_mode & S_ISVTX) || capable (CAP_FOWNER) ||
				    current->fsuid == sdir->i_uid ||
				    current->fsuid == dir->i_uid) {
					if (empty == 1) {
						/* We have to remove the EMD file */
						ret = msdos_unlink (sdir, tdentry);
						Printk (("UMSDOS_rmdir: unlinking empty EMD ret=%d", ret));
						sdir = NULL;
					}
					/* sdir must be free before msdos_rmdir() */
					/* iput (sdir); FIXME */
					sdir = NULL;
					Printk (("isempty ret %d nlink %d ", ret, dir->i_nlink));
					if (ret == 0) {
						struct umsdos_info info;
						struct dentry *temp, *tdir;

						inc_count (dir);
						umsdos_parse (dentry->d_name.name, dentry->d_name.len, &info);
						/* The findentry is there only to complete */
						/* the mangling */
						umsdos_findentry (dir, &info, 2);

						tdir = creat_dentry ("@dir-rmd@", 9, dir, NULL);
						temp = creat_dentry (info.fake.fname, info.fake.len, NULL, tdir);
						umsdos_real_lookup (dir, temp);		/* fill inode part */

						Printk ((KERN_ERR "  rmdir start dir=%lu, dir->sb=%p\n", dir->i_ino, dir->i_sb));	/* FIXME: /mn/ debug only */
						Printk ((KERN_ERR "    dentry=%.*s d_count=%d ino=%lu\n", (int) temp->d_name.len, temp->d_name.name, temp->d_count, temp->d_inode->i_ino));
						Printk ((KERN_ERR "    d_parent=%.*s d_count=%d ino=%lu\n", (int) temp->d_parent->d_name.len, temp->d_parent->d_name.name, temp->d_parent->d_count, temp->d_parent->d_inode->i_ino));

						ret = msdos_rmdir (dir, temp);

						Printk ((KERN_ERR "  rmdir passed %d\n", ret));		/* FIXME: /mn/ debug only */
						Printk ((KERN_ERR "  rmdir end dir=%lu, dir->sb=%p\n", dir->i_ino, dir->i_sb));
						Printk ((KERN_ERR "    dentry=%.*s d_count=%d ino=%p\n", (int) temp->d_name.len, temp->d_name.name, temp->d_count, temp->d_inode));
						Printk ((KERN_ERR "    d_parent=%.*s d_count=%d ino=%lu\n", (int) temp->d_parent->d_name.len, temp->d_parent->d_name.name, temp->d_parent->d_count, temp->d_parent->d_inode->i_ino));

						kill_dentry (tdir);
						kill_dentry (temp);

						if (ret == 0) {
							ret = umsdos_delentry (dir, &info, 1);
							d_delete (dentry);
						}
					}
				} else {
					/* sticky bit set and we don't have permission */
					Printk (("sticky set "));
					ret = -EPERM;
				}
			} else {
				/*
				 * The subdirectory is not empty, so leave it there
				 */
				ret = -ENOTEMPTY;
			}
			/* iput(sdir); FIXME */
			umsdos_unlockcreate (dir);
		}
	}
	/*  dput(dentry); FIXME /mn/ */
	Printk (("umsdos_rmdir %d\n", ret));
	return ret;
}



/*
 * Remove a file from the directory.
 */
int UMSDOS_unlink (
			  struct inode *dir,
			  struct dentry *dentry)
{
	int ret;

	Printk ((" *** UMSDOS_unlink entering /mn/ *** \n"));

	ret = umsdos_nevercreat (dir, dentry, -EPERM);

	Printk (("UMSDOS_unlink /mn/: nevercreat=%d\n", ret));

	if (ret == 0) {
		struct umsdos_info info;

		ret = umsdos_parse (dentry->d_name.name, dentry->d_name.len, &info);
		if (ret == 0) {
			umsdos_lockcreate (dir);
			ret = umsdos_findentry (dir, &info, 1);
			Printk (("UMSDOS_unlink: findentry returned %d\n", ret));
			if (ret == 0) {
				Printk (("UMSDOS_unlink %.*s ", info.fake.len, info.fake.fname));
				/* check sticky bit */
				if (!(dir->i_mode & S_ISVTX) || capable (CAP_FOWNER) ||
				    current->fsuid == info.entry.uid ||
				    current->fsuid == dir->i_uid) {
					if (info.entry.flags & UMSDOS_HLINK) {
						/* #Specification: hard link / deleting a link
						 * When we delete a file, and this file is a link
						 * we must subtract 1 to the nlink field of the
						 * hidden link.
						 * 
						 * If the count goes to 0, we delete this hidden
						 * link too.
						 */
						/*
						 * First, get the inode of the hidden link
						 * using the standard lookup function.
						 */
						struct inode *inode;

						inc_count (dir);
						ret = umsdos_lookup_x (dir, dentry, 0);
						inode = dentry->d_inode;
						if (ret == 0) {
							Printk (("unlink nlink = %d ", inode->i_nlink));
							inode->i_nlink--;
							if (inode->i_nlink == 0) {
								struct inode *hdir = iget (inode->i_sb
											   ,inode->u.umsdos_i.i_dir_owner);
								struct umsdos_dirent entry;

								ret = umsdos_inode2entry (hdir, inode, &entry);
								if (ret == 0) {
									ret = UMSDOS_unlink (hdir, dentry);
								} else {
									/* iput (hdir); FIXME */
								}
							} else {
								struct iattr newattrs;

								newattrs.ia_valid = 0;
								ret = UMSDOS_notify_change (dentry, &newattrs);
							}
							/* iput (inode); FIXME */
						}
					}
					if (ret == 0) {
						ret = umsdos_delentry (dir, &info, 0);
						if (ret == 0) {
							struct dentry *temp,
							*tdir;

							Printk (("Before msdos_unlink %.*s ", info.fake.len, info.fake.fname));
							/* inc_count (dir);	/ * FIXME /mn/ is this needed any more now that msdos_unlink locks directories using d_parent ? */
							tdir = creat_dentry ("@dir-del@", 9, dir, NULL);	/* FIXME /mn/: do we need iget(dir->i_ino) or would dir itself suffice ? */
							temp = creat_dentry (info.fake.fname, info.fake.len, NULL, tdir);
							umsdos_real_lookup (dir, temp);		/* fill inode part */

							ret = msdos_unlink_umsdos (dir, temp);
							Printk (("msdos_unlink %.*s %o ret %d ", info.fake.len, info.fake.fname
								 ,info.entry.mode, ret));

							d_delete (dentry);

							kill_dentry (tdir);
							kill_dentry (temp);
						}
					}
				} else {
					/* sticky bit set and we've not got permission */
					Printk (("Sticky bit set. "));
					ret = -EPERM;
				}
			}
			umsdos_unlockcreate (dir);
		}
	}
	/* dput(dentry); FIXME: shouldn't this be done in msdos_unlink ? */
	Printk (("umsdos_unlink %d\n", ret));
	return ret;
}



/*
 * Rename (move) a file.
 */
int UMSDOS_rename (
			  struct inode *old_dir,
			  struct dentry *old_dentry,
			  struct inode *new_dir,
			  struct dentry *new_dentry)
{
	/* #Specification: weakness / rename
	 * There is a case where UMSDOS rename has a different behavior
	 * than a normal Unix file system.  Renaming an open file across
	 * directory boundary does not work.  Renaming an open file within
	 * a directory does work, however.
	 * 
	 * The problem may is in Linux VFS driver for msdos.
	 * I believe this is not a bug but a design feature, because
	 * an inode number represents some sort of directory address
	 * in the MSDOS directory structure, so moving the file into
	 * another directory does not preserve the inode number.
	 */
	int ret = umsdos_nevercreat (new_dir, new_dentry, -EEXIST);

	if (ret == 0) {
		/* umsdos_rename_f eat the inode and we may need those later */
		inc_count (old_dir);
		inc_count (new_dir);
		ret = umsdos_rename_f (old_dir, old_dentry, new_dir, new_dentry, 0);
		if (ret == -EEXIST) {
			/* #Specification: rename / new name exist
			 * If the destination name already exists, it will
			 * silently be removed.  EXT2 does it this way
			 * and this is the spec of SunOS.  So does UMSDOS.
			 * 
			 * If the destination is an empty directory it will
			 * also be removed.
			 */
			/* #Specification: rename / new name exist / possible flaw
			 * The code to handle the deletion of the target (file
			 * and directory) use to be in umsdos_rename_f, surrounded
			 * by proper directory locking.  This was ensuring that only
			 * one process could achieve a rename (modification) operation
			 * in the source and destination directory.  This was also
			 * ensuring the operation was "atomic".
			 * 
			 * This has been changed because this was creating a
			 * stack overflow (the stack is only 4 kB) in the kernel.  To avoid
			 * the code doing the deletion of the target (if exist) has
			 * been moved to a upper layer. umsdos_rename_f is tried
			 * once and if it fails with EEXIST, the target is removed
			 * and umsdos_rename_f is done again.
			 * 
			 * This makes the code cleaner and may solve a
			 * deadlock problem one tester was experiencing.
			 * 
			 * The point is to mention that possibly, the semantic of
			 * "rename" may be wrong. Anyone dare to check that :-)
			 * Be aware that IF it is wrong, to produce the problem you
			 * will need two process trying to rename a file to the
			 * same target at the same time. Again, I am not sure it
			 * is a problem at all.
			 */
			/* This is not terribly efficient but should work. */
			inc_count (new_dir);
			ret = UMSDOS_unlink (new_dir, new_dentry);
			chkstk ();
			Printk (("rename unlink ret %d -- ", ret));
			if (ret == -EISDIR) {
				inc_count (new_dir);
				ret = UMSDOS_rmdir (new_dir, new_dentry);
				chkstk ();
				Printk (("rename rmdir ret %d -- ", ret));
			}
			if (ret == 0) {
				ret = umsdos_rename_f (old_dir, old_dentry,
						 new_dir, new_dentry, 0);
				new_dir = old_dir = NULL;
			}
		}
	}
	/*
	 * dput (new_dentry);
	 * dput (old_dentry); FIXME /mn/ */
	return ret;
}
