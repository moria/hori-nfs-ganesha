/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2017 Red Hat, Inc. and/or its affiliates.
 * Author: Jeff Layton <jlayton@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * recovery_rados_ng: a "safe by design" recovery backing store
 *
 * At startup, create a global write op, and set it up to clear out all of
 * the old keys. We then will spool up new client creation (and removals) to
 * that transaction during the grace period.
 *
 * When lifting the grace period, synchronously commit the transaction
 * to the kvstore. After that point, all client creation and removal is done
 * synchronously to the kvstore.
 *
 * This allows for better resilience when the server crashes during the grace
 * period. No changes are made to the backing store until the grace period
 * has been lifted.
 */

#include "config.h"
#include <netdb.h>
#include <rados/librados.h>
#include "log.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "recovery_rados.h"

static rados_write_op_t grace_op;
static pthread_mutex_t grace_op_lock = PTHREAD_MUTEX_INITIALIZER;

static int rados_ng_put(char *key, char *val, char *object)
{
	int ret;
	char *keys[1];
	char *vals[1];
	size_t lens[1];
	rados_write_op_t write_op = NULL;
	bool in_grace;

	keys[0] = key;
	vals[0] = val;
	lens[0] = strlen(val);

	/* When there is an active grace_op, spool up the changes to it */
	PTHREAD_MUTEX_lock(&grace_op_lock);
	in_grace = grace_op;
	write_op = grace_op;
	if (!write_op)
		write_op = rados_create_write_op();
	rados_write_op_omap_set(write_op, (const char * const*)keys,
					  (const char * const*)vals, lens, 1);
	PTHREAD_MUTEX_unlock(&grace_op_lock);
	if (in_grace)
		return 0;

	ret = rados_write_op_operate(write_op, rados_recov_io_ctx, object,
				     NULL, 0);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to put kv ret=%d, key=%s, val=%s",
			 ret, key, val);
	}
	rados_release_write_op(write_op);

	return ret;
}

static int rados_ng_del(char *key, char *object)
{
	int ret;
	char *keys[1];
	rados_write_op_t write_op;
	bool in_grace;

	keys[0] = key;

	PTHREAD_MUTEX_lock(&grace_op_lock);
	in_grace = grace_op;
	write_op = in_grace ? grace_op : rados_create_write_op();
	rados_write_op_omap_rm_keys(write_op, (const char * const*)keys, 1);
	PTHREAD_MUTEX_unlock(&grace_op_lock);

	if (in_grace)
		return 0;

	ret = rados_write_op_operate(write_op, rados_recov_io_ctx, object,
				     NULL, 0);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to del kv ret=%d, key=%s",
			 ret, key);
	}
	rados_release_write_op(write_op);

	return ret;
}

void rados_ng_init(void)
{
	int ret;
	char host[NI_MAXHOST];
	rados_write_op_t op;

	if (nfs_param.core_param.clustered) {
		snprintf(host, sizeof(host), "node%d", g_nodeid);
	} else {
		ret = gethostname(host, sizeof(host));
		if (ret) {
			LogEvent(COMPONENT_CLIENTID,
				 "Failed to gethostname: %s",
				 strerror(errno));
			return;
		}
	}

	snprintf(rados_recov_oid, sizeof(rados_recov_oid), "%s.recov", host);

	ret = rados_create(&rados_recov_cluster, rados_kv_param.userid);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to rados create");
		return;
	}
	ret = rados_conf_read_file(rados_recov_cluster,
				   rados_kv_param.ceph_conf);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to read ceph_conf");
		rados_shutdown(rados_recov_cluster);
		return;
	}
	ret = rados_connect(rados_recov_cluster);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to connect to cluster");
		rados_shutdown(rados_recov_cluster);
		return;
	}
	ret = rados_pool_create(rados_recov_cluster, rados_kv_param.pool);
	if (ret < 0 && ret != -EEXIST) {
		LogEvent(COMPONENT_CLIENTID, "Failed to create pool");
		rados_shutdown(rados_recov_cluster);
		return;
	}
	ret = rados_ioctx_create(rados_recov_cluster, rados_kv_param.pool,
				 &rados_recov_io_ctx);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to create ioctx");
		rados_shutdown(rados_recov_cluster);
		return;
	}

	op = rados_create_write_op();
	rados_write_op_create(op, LIBRADOS_CREATE_EXCLUSIVE, NULL);
	ret = rados_write_op_operate(op, rados_recov_io_ctx, rados_recov_oid,
				     NULL, 0);
	if (ret < 0 && ret != -EEXIST) {
		LogEvent(COMPONENT_CLIENTID, "Failed to create object");
		rados_release_write_op(op);
		rados_shutdown(rados_recov_cluster);
		return;
	}
	rados_release_write_op(op);

	/* Create new grace_op to spool changes until grace period is done */
	grace_op = rados_create_write_op();
	rados_write_op_omap_clear(grace_op);

	LogEvent(COMPONENT_CLIENTID, "Rados kv store init done");
}

void rados_ng_add_clid(nfs_client_id_t *clientid)
{
	char ckey[RADOS_KEY_MAX_LEN];
	char *cval;
	int ret;

	cval = gsh_malloc(RADOS_VAL_MAX_LEN);

	rados_kv_create_key(clientid, ckey);
	rados_kv_create_val(clientid, cval);

	LogDebug(COMPONENT_CLIENTID, "adding %s :: %s", ckey, cval);
	ret = rados_ng_put(ckey, cval, rados_recov_oid);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to add clid %lu",
			 clientid->cid_clientid);
		goto out;
	}

	clientid->cid_recov_tag = gsh_malloc(strlen(cval) + 1);
	strncpy(clientid->cid_recov_tag, cval, strlen(cval) + 1);
out:
	gsh_free(cval);
}

void rados_ng_rm_clid(nfs_client_id_t *clientid)
{
	char ckey[RADOS_KEY_MAX_LEN];
	int ret;

	rados_kv_create_key(clientid, ckey);

	LogDebug(COMPONENT_CLIENTID, "removing %s", ckey);
	ret = rados_ng_del(ckey, rados_recov_oid);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to del clid %lu",
			 clientid->cid_clientid);
		return;
	}

	free(clientid->cid_recov_tag);
	clientid->cid_recov_tag = NULL;
}

static void rados_ng_pop_clid_entry(char *key,
				    char *val,
				    add_clid_entry_hook add_clid_entry,
				    add_rfh_entry_hook add_rfh_entry,
				    bool old, bool takeover)
{
	char *dupval, *cl_name;
	char *rfh_names, *rfh_name;
	clid_entry_t *clid_ent;

	/* extract clid records */
	dupval = gsh_strdup(val);
	cl_name = strtok(dupval, "#");
	if (!cl_name)
		cl_name = dupval;
	clid_ent = add_clid_entry(cl_name);

	rfh_names = strtok(NULL, "#");
	rfh_name = strtok(rfh_names, "#");
	while (rfh_name) {
		add_rfh_entry(clid_ent, rfh_name);
		rfh_name = strtok(NULL, "#");
	}
	gsh_free(dupval);
}

static void
rados_ng_read_recov_clids_recover(add_clid_entry_hook add_clid_entry,
				       add_rfh_entry_hook add_rfh_entry)
{
	int ret;
	struct pop_args args = {
		.add_clid_entry = add_clid_entry,
		.add_rfh_entry = add_rfh_entry,
	};

	ret = rados_kv_traverse(rados_ng_pop_clid_entry, &args,
				rados_recov_oid);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to recover, processing old entries");
		return;
	}
}

void rados_ng_read_recov_clids_takeover(nfs_grace_start_t *gsp,
					add_clid_entry_hook add_clid_entry,
					add_rfh_entry_hook add_rfh_entry)
{
	if (!gsp) {
		rados_ng_read_recov_clids_recover(add_clid_entry,
						  add_rfh_entry);
		return;
	}

	LogEvent(COMPONENT_CLIENTID,
		 "Unable to perform takeover with rados_ng recovery backend.");
}

void rados_ng_cleanup_old(void)
{
	int ret;

	/* Commit pregrace transaction */
	PTHREAD_MUTEX_lock(&grace_op_lock);
	ret = rados_write_op_operate(grace_op, rados_recov_io_ctx,
				     rados_recov_oid, NULL, 0);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to commit grace period transactions: %s",
			 strerror(ret));
	}
	rados_release_write_op(grace_op);
	grace_op = NULL;
	PTHREAD_MUTEX_unlock(&grace_op_lock);
}

void rados_ng_add_revoke_fh(nfs_client_id_t *delr_clid, nfs_fh4 *delr_handle)
{
	int ret;
	char ckey[RADOS_KEY_MAX_LEN];
	char *cval;

	cval = gsh_malloc(RADOS_VAL_MAX_LEN);

	rados_kv_create_key(delr_clid, ckey);
	ret = rados_kv_get(ckey, cval, rados_recov_oid);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to get %s", ckey);
		goto out;
	}

	rados_kv_append_val_rdfh(cval, delr_handle->nfs_fh4_val,
				      delr_handle->nfs_fh4_len);

	ret = rados_ng_put(ckey, cval, rados_recov_oid);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to add rdfh for clid %lu",
			 delr_clid->cid_clientid);
	}

out:
	gsh_free(cval);
}

struct nfs4_recovery_backend rados_ng_backend = {
	.recovery_init = rados_ng_init,
	.recovery_cleanup = rados_ng_cleanup_old,
	.recovery_read_clids = rados_ng_read_recov_clids_takeover,
	.add_clid = rados_ng_add_clid,
	.rm_clid = rados_ng_rm_clid,
	.add_revoke_fh = rados_ng_add_revoke_fh,
};

void rados_ng_backend_init(struct nfs4_recovery_backend **backend)
{
	*backend = &rados_ng_backend;
}
