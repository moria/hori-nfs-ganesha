/* This is a central clearing house for RPC definitions. Nothing
   should included anything related to RPC except this file */

#ifndef GSH_RPC_H
#define GSH_RPC_H

#include "config.h"

#include <stdbool.h>

/* Ganesha project has abstract_atomic.h file and tirpc also has a
 * similar header with the same name.  We want to include ganesha
 * project's header here, so include it here before including any
 * RPC headers.
 */
#include "abstract_atomic.h"
#include <rpc/xdr_inline.h>
#include <rpc/rpc.h>
#include <rpc/svc.h>
#include <rpc/clnt.h>

#include <rpc/svc_auth.h>
#ifdef _HAVE_GSSAPI
#include <rpc/auth_gss.h>
#include <rpc/gss_internal.h>	/* XXX */
#endif
#include <rpc/svc_rqst.h>
#include <rpc/rpc_msg.h>
#include "common_utils.h"
#include "abstract_mem.h"
#include "gsh_list.h"
#include "log.h"
#include "fridgethr.h"

#define NFS_LOOKAHEAD_NONE 0x0000
#define NFS_LOOKAHEAD_MOUNT 0x0001
#define NFS_LOOKAHEAD_OPEN 0x0002
#define NFS_LOOKAHEAD_CLOSE 0x0004
#define NFS_LOOKAHEAD_READ 0x0008
#define NFS_LOOKAHEAD_WRITE 0x0010
#define NFS_LOOKAHEAD_COMMIT 0x0020
#define NFS_LOOKAHEAD_CREATE 0x0040
#define NFS_LOOKAHEAD_REMOVE 0x0080
#define NFS_LOOKAHEAD_RENAME 0x0100
#define NFS_LOOKAHEAD_LOCK 0x0200 /* !_U types */
#define NFS_LOOKAHEAD_READDIR 0x0400
#define NFS_LOOKAHEAD_LAYOUTCOMMIT 0x0040
#define NFS_LOOKAHEAD_SETATTR 0x0080
#define NFS_LOOKAHEAD_SETCLIENTID 0x0100
#define NFS_LOOKAHEAD_SETCLIENTID_CONFIRM  0x0200
#define NFS_LOOKAHEAD_LOOKUP 0x0400
#define NFS_LOOKAHEAD_READLINK 0x0800
/* ... */

struct nfs_request_lookahead {
	uint32_t flags;
	uint16_t read;
	uint16_t write;
};

#define NFS_LOOKAHEAD_HIGH_LATENCY(lkhd)		\
	(((lkhd).flags & (NFS_LOOKAHEAD_READ |		\
			  NFS_LOOKAHEAD_WRITE |		\
			  NFS_LOOKAHEAD_COMMIT |	\
			  NFS_LOOKAHEAD_LAYOUTCOMMIT |	\
			  NFS_LOOKAHEAD_READDIR)))


#define XDR_ARRAY_MAXLEN 1024
#define XDR_BYTES_MAXLEN (1024*1024)
#define XDR_BYTES_MAXLEN_IO (64*1024*1024)
#define XDR_STRING_MAXLEN (8*1024)

typedef struct sockaddr_storage sockaddr_t;

#define SOCK_NAME_MAX 128

struct netconfig *getnetconfigent(const char *);
void freenetconfigent(struct netconfig *);

/**
 * @addtogroup config
 *
 * @{
 */

/**
 * @defgroup config_krb5 Structure and defaults for NFS_KRB5
 * @brief Constants and csturctures for KRB5 configuration
 *
 * @{
 */

/**
 * @brief Default value for krb5_param.gss.principal
 */
#define DEFAULT_NFS_PRINCIPAL "nfs"
/**
 * @brief default value for krb5_param.keytab
 *
 * The empty string lets GSSAPI use keytab specified in /etc/krb5.conf
 */
#define DEFAULT_NFS_KEYTAB ""

#ifdef _HAVE_GSSAPI
/**
 * @brief Kerberos 5 parameters
 */
typedef struct nfs_krb5_param {
	/** Kerberos keytab.  Defaults to DEFAULT_NFS_KEYTAB, settable
	    with KeytabPath. */
	char *keytab;
	/** The ganesha credential cache.  Defautls to
	    DEFAULT_NFS_CCACHE_DIR, unsettable by user. */
	char *ccache_dir;
	/**
	 * @note representation of GSSAPI service, independent of GSSRPC or
	 * TI-RPC global variables.  Initially, use it just for
	 * callbacks.
	 */
	struct {
		/** Principal used in callbacks, set to
		    DEFAULT_NFS_PRINCIPAL and unsettable by user. */
		char *principal;
		/** Expanded gss name from principal, equal to
		    principal/host\@domain.  Unsettable by user. */
		gss_name_t gss_name;
	} svc;
	/** Whether to activate Kerberos 5.  Defaults to true (if
	    Kerberos support is compiled in) and settable with
	    Active_krb5 */
	bool active_krb5;
} nfs_krb5_parameter_t;
/** @} */
/** @} */

void log_sperror_gss(char *, OM_uint32, OM_uint32);
const char *str_gc_proc(rpc_gss_proc_t);
#endif /* _HAVE_GSSAPI */

bool copy_xprt_addr(sockaddr_t *, SVCXPRT *);

int display_sockaddr(struct display_buffer *dspbuf, sockaddr_t *addr);

static inline void sprint_sockaddr(sockaddr_t *addr, char *buf, size_t len)
{
	struct display_buffer dspbuf = {len, buf, buf};

	buf[0] = '\0';
	display_sockaddr(&dspbuf, addr);
}

int sprint_sockip(sockaddr_t *, char *, int);
const char *xprt_type_to_str(xprt_type_t);

int cmp_sockaddr(sockaddr_t *, sockaddr_t *, bool);
int sockaddr_cmpf(sockaddr_t *, sockaddr_t *, bool);
uint64_t hash_sockaddr(sockaddr_t *, bool);

in_addr_t get_in_addr(sockaddr_t *);
int get_port(sockaddr_t *);

/* Returns an EAI value, accepts only numeric strings */
extern int ipstring_to_sockaddr(const char *, sockaddr_t *);

extern tirpc_pkg_params ntirpc_pp;
#endif /* GSH_RPC_H */
