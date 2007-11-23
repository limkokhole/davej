/*
 * linux/fs/nfsd/nfssvc.c
 *
 * Central processing for nfsd.
 *
 * Authors:	Olaf Kirch (okir@monad.swb.de)
 *
 * Copyright (C) 1995, 1996, 1997 Olaf Kirch <okir@monad.swb.de>
 */

#define __NO_VERSION__
#include <linux/config.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/nfs.h>
#include <linux/in.h>
#include <linux/uio.h>
#include <linux/version.h>
#include <linux/unistd.h>
#include <linux/malloc.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <linux/sunrpc/types.h>
#include <linux/sunrpc/stats.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/stats.h>
#include <linux/nfsd/cache.h>
#include <linux/nfsd/xdr.h>
#include <linux/lockd/bind.h>

#define NFSDDBG_FACILITY	NFSDDBG_SVC
#define NFSD_BUFSIZE		(1024 + NFSSVC_MAXBLKSIZE)
#define BLOCKABLE_SIGS		(~(_S(SIGKILL - 1) | _S(SIGSTOP - 1)))
#define _S(sig)			(1 << ((sig) - 1))

extern struct svc_program	nfsd_program;
static void			nfsd(struct svc_rqst *rqstp);
struct timeval			nfssvc_boot = { 0, 0 };

/*
 * Make a socket for nfsd
 */
static int
nfsd_makesock(struct svc_serv *serv, int protocol, unsigned short port)
{
	struct sockaddr_in	sin;

	dprintk("nfsd: creating socket proto = %d\n", protocol);
	sin.sin_family      = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port        = htons(port);
	return svc_create_socket(serv, protocol, &sin);
}

int
nfsd_svc(unsigned short port, int nrservs)
{
	struct svc_serv *	serv;
	int			error;

	dprintk("nfsd: creating service\n");
	if (nrservs < 0)
		return -EINVAL;
	if (nrservs > NFSD_MAXSERVS)
		nrservs = NFSD_MAXSERVS;

	serv = svc_create(&nfsd_program, NFSD_BUFSIZE, NFSSVC_XDRSIZE);
	if (serv == NULL) 
		return -ENOMEM;

	if ((error = nfsd_makesock(serv, IPPROTO_UDP, port)) < 0
	 || (error = nfsd_makesock(serv, IPPROTO_TCP, port)) < 0)
		goto failure;

	while (nrservs--) {
		error = svc_create_thread(nfsd, serv);
		if (error < 0)
			break;
	}

failure:
	svc_destroy(serv);		/* Release server */
	return error;
}

/*
 * This is the NFS server kernel thread
 */
static void
nfsd(struct svc_rqst *rqstp)
{
	struct svc_serv	*serv = rqstp->rq_server;
	sigset_t	oldsigmask;
	int		oldumask, err;

	lock_kernel();
	/* Lock module and set up kernel thread */
	MOD_INC_USE_COUNT;
	exit_mm(current);
	current->session = 1;
	current->pgrp = 1;
	sprintf(current->comm, "nfsd");

	oldumask = current->fs->umask;		/* Set umask to 0.  */
	current->fs->umask = 0;
	nfssvc_boot = xtime;			/* record boot time */
	lockd_up();				/* start lockd */

	/*
	 * The main request loop
	 */
	do {
		/*
		 * Find a socket with data available and call its
		 * recvfrom routine.
		 */
		while ((err = svc_recv(serv, rqstp)) == -EAGAIN)
			;
		if (err < 0)
			break;

		/* Lock the export hash tables for reading. */
		exp_readlock();

		/* Validate the client's address. This will also defeat
		 * port probes on port 2049 by unauthorized clients.
		 */
		rqstp->rq_client = exp_getclient(&rqstp->rq_addr);
		if (!rqstp->rq_client) {
			printk(KERN_WARNING "nfsd: unauthenticated request "
				"from (%08lx:%d)\n",
				ntohl(rqstp->rq_addr.sin_addr.s_addr),
				ntohs(rqstp->rq_addr.sin_port));
			svc_drop(rqstp);
			serv->sv_stats->rpcbadclnt++;
		} else {
			/* Process request with all signals blocked.  */
			oldsigmask = current->blocked;
			current->blocked = BLOCKABLE_SIGS;
			svc_process(serv, rqstp);
			current->blocked = oldsigmask;
		}

		/* Unlock export hash tables */
		exp_unlock();
	} while (err >= 0);

	if (err != -EINTR)
		printk(KERN_WARNING "nfsd: terminating on error %d\n", -err);

	/* Release lockd */
	lockd_down();

	/* Destroy the thread */
	svc_exit_thread(rqstp);
	current->fs->umask = oldumask;

	/* Release module */
	MOD_DEC_USE_COUNT;
}

static int
nfsd_dispatch(struct svc_rqst *rqstp, u32 *statp)
{
	struct svc_procedure	*proc;
	kxdrproc_t		xdr;
	u32			nfserr;

	dprintk("nfsd_dispatch: proc %d\n", rqstp->rq_proc);
	proc = rqstp->rq_procinfo;

	/* Check whether we have this call in the cache. */
	switch (nfsd_cache_lookup(rqstp, proc->pc_cachetype)) {
	case RC_INTR:
	case RC_DROPIT:
		return 0;
	case RC_REPLY:
		return 1;
	case RC_DOIT:
		/* do it */
	}

	/* Decode arguments */
	xdr = proc->pc_decode;
	if (xdr && !xdr(rqstp, rqstp->rq_argbuf.buf, rqstp->rq_argp)) {
		dprintk("nfsd: failed to decode arguments!\n");
		nfsd_cache_update(rqstp, RC_NOCACHE, NULL);
		*statp = rpc_garbage_args;
		return 1;
	}

	/* Now call the procedure handler, and encode NFS status. */
	nfserr = proc->pc_func(rqstp, rqstp->rq_argp, rqstp->rq_resp);
	if (rqstp->rq_proc != 0)
		svc_putlong(&rqstp->rq_resbuf, nfserr);

	/* Encode result.
	 * FIXME: Most NFSv3 calls return wcc data even when the call failed
	 */
	xdr = proc->pc_encode;
	if (!nfserr && xdr
	 && !xdr(rqstp, rqstp->rq_resbuf.buf, rqstp->rq_resp)) {
		/* Failed to encode result. Release cache entry */
		dprintk("nfsd: failed to encode result!\n");
		nfsd_cache_update(rqstp, RC_NOCACHE, NULL);
		*statp = rpc_system_err;
		return 1;
	}

	/* Store reply in cache. */
	nfsd_cache_update(rqstp, proc->pc_cachetype, statp + 1);
	return 1;
}

static struct svc_version	nfsd_version2 = {
	2, 18, nfsd_procedures2, nfsd_dispatch
};
#ifdef CONFIG_NFSD_NFS3
static struct svc_version	nfsd_version3 = {
	3, 23, nfsd_procedures3, nfsd_dispatch
};
#endif
static struct svc_version *	nfsd_version[] = {
	NULL,
	NULL,
	&nfsd_version2,
#ifdef CONFIG_NFSD_NFS3
	&nfsd_version3,
#endif
};

#define NFSD_NRVERS		(sizeof(nfsd_version)/sizeof(nfsd_version[0]))
struct svc_program		nfsd_program = {
	NFS_PROGRAM,		/* program number */
	2, NFSD_NRVERS-1,	/* version range */
	NFSD_NRVERS,		/* nr of entries in nfsd_version */
	nfsd_version,		/* version table */
	"nfsd",			/* program name */
	&nfsd_svcstats,		/* version table */
};