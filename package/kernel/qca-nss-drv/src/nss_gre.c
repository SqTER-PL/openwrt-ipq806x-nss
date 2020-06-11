/*
 **************************************************************************
 * Copyright (c) 2017, The Linux Foundation. All rights reserved.
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all copies.
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 **************************************************************************
 */

#include "nss_tx_rx_common.h"

#define NSS_GRE_TX_TIMEOUT 3000 /* 3 Seconds */

/*
 * Private data structure
 */
static struct {
	struct semaphore sem;
	struct completion complete;
	int response;
	void *cb;
	void *app_data;
} nss_gre_pvt;

/*
 * Data structures to store GRE nss debug stats
 */
static DEFINE_SPINLOCK(nss_gre_stats_lock);
static struct nss_stats_gre_session_debug session_debug_stats[NSS_GRE_MAX_DEBUG_SESSION_STATS];
static struct nss_stats_gre_base_debug base_debug_stats;

static atomic64_t pkt_cb_addr = ATOMIC64_INIT(0);

/*
 * nss_gre_rx_handler()
 *	GRE rx handler.
 */
static void nss_gre_rx_handler(struct net_device *dev, struct sk_buff *skb,
		    __attribute__((unused)) struct napi_struct *napi)
{
	nss_gre_data_callback_t cb;

	nss_gre_pkt_callback_t scb = (nss_gre_pkt_callback_t)(unsigned long)atomic64_read(&pkt_cb_addr);
	if (unlikely(scb)) {
		struct nss_gre_info *info = (struct nss_gre_info *)netdev_priv(dev);
		if (likely(info->next_dev)) {
			scb(info->next_dev, skb);
		}
	}

	cb = nss_top_main.gre_data_callback;
	cb(dev, skb, 0);
}

/*
 * nss_gre_session_debug_stats_sync()
 *	debug statistics sync for GRE session.
 */
static void nss_gre_session_debug_stats_sync(struct nss_ctx_instance *nss_ctx, struct nss_gre_session_stats_msg *sstats, uint16_t if_num)
{
	int i, j;
	spin_lock_bh(&nss_gre_stats_lock);
	for (i = 0; i < NSS_GRE_MAX_DEBUG_SESSION_STATS; i++) {
		if (session_debug_stats[i].if_num == if_num) {
			for (j = 0; j < NSS_STATS_GRE_SESSION_DEBUG_MAX; j++) {
				session_debug_stats[i].stats[j] += sstats->stats[j];
			}
			break;
		}
	}
	spin_unlock_bh(&nss_gre_stats_lock);
}

/*
 * nss_gre_base_debug_stats_sync()
 *	Debug statistics sync for GRE base node.
 */
static void nss_gre_base_debug_stats_sync(struct nss_ctx_instance *nss_ctx, struct nss_gre_base_stats_msg *bstats)
{
	int i;
	spin_lock_bh(&nss_gre_stats_lock);
	for (i = 0; i < NSS_STATS_GRE_BASE_DEBUG_MAX; i++) {
		base_debug_stats.stats[i] += bstats->stats[i];
	}
	spin_unlock_bh(&nss_gre_stats_lock);
}

/*
 * nss_gre_msg_handler()
 *	Handle NSS -> HLOS messages for GRE
 */
static void nss_gre_msg_handler(struct nss_ctx_instance *nss_ctx, struct nss_cmn_msg *ncm, __attribute__((unused))void *app_data)
{
	struct nss_gre_msg *ntm = (struct nss_gre_msg *)ncm;
	void *ctx;

	nss_gre_msg_callback_t cb;

	NSS_VERIFY_CTX_MAGIC(nss_ctx);
	BUG_ON(!(nss_is_dynamic_interface(ncm->interface) || ncm->interface == NSS_GRE_INTERFACE));

	/*
	 * Is this a valid request/response packet?
	 */
	if (ncm->type >= NSS_GRE_MSG_MAX) {
		nss_warning("%p: received invalid message %d for GRE STD interface", nss_ctx, ncm->type);
		return;
	}

	if (nss_cmn_get_msg_len(ncm) > sizeof(struct nss_gre_msg)) {
		nss_warning("%p: tx request for another interface: %d", nss_ctx, ncm->interface);
		return;
	}

	switch (ntm->cm.type) {
	case NSS_GRE_MSG_SESSION_STATS:
		/*
		 * debug stats embedded in stats msg
		 */
		nss_gre_session_debug_stats_sync(nss_ctx, &ntm->msg.sstats, ncm->interface);
		break;

	case NSS_GRE_MSG_BASE_STATS:
		nss_gre_base_debug_stats_sync(nss_ctx, &ntm->msg.bstats);
		break;

	default:
		break;

	}

	/*
	 * Update the callback and app_data for NOTIFY messages, gre sends all notify messages
	 * to the same callback/app_data.
	 */
	if (ncm->response == NSS_CMM_RESPONSE_NOTIFY) {
		ncm->cb = (nss_ptr_t)nss_ctx->nss_top->gre_msg_callback;
		ncm->app_data = (nss_ptr_t)nss_ctx->subsys_dp_register[ncm->interface].app_data;
	}

	/*
	 * Log failures
	 */
	nss_core_log_msg_failures(nss_ctx, ncm);

	/*
	 * callback
	 */
	cb = (nss_gre_msg_callback_t)ncm->cb;
	ctx = (void *)ncm->app_data;

	/*
	 * call gre-std callback
	 */
	if (!cb) {
		nss_warning("%p: No callback for gre-std interface %d",
			    nss_ctx, ncm->interface);
		return;
	}

	cb(ctx, ntm);
}

/*
 * nss_gre_callback()
 *	Callback to handle the completion of HLOS-->NSS messages.
 */
static void nss_gre_callback(void *app_data, struct nss_gre_msg *nim)
{
	nss_gre_msg_callback_t callback = (nss_gre_msg_callback_t)nss_gre_pvt.cb;
	void *data = nss_gre_pvt.app_data;

	nss_gre_pvt.cb = NULL;
	nss_gre_pvt.app_data = NULL;

	if (nim->cm.response != NSS_CMN_RESPONSE_ACK) {
		nss_warning("gre Error response %d\n", nim->cm.response);
		nss_gre_pvt.response = NSS_TX_FAILURE;
	} else {
		nss_gre_pvt.response = NSS_TX_SUCCESS;
	}

	if (callback) {
		callback(data, nim);
	}

	complete(&nss_gre_pvt.complete);
}

/*
 * nss_gre_session_debug_stats_get()
 *	Get GRE session debug statistics.
 */
void nss_gre_session_debug_stats_get(void *stats_mem, int size)
{
	struct nss_stats_gre_session_debug *stats = (struct nss_stats_gre_session_debug *)stats_mem;
	int i;

	if (!stats || (size < (sizeof(struct nss_stats_gre_session_debug) * NSS_STATS_GRE_SESSION_DEBUG_MAX)))  {
		nss_warning("No memory to copy gre stats");
		return;
	}

	spin_lock_bh(&nss_gre_stats_lock);
	for (i = 0; i < NSS_GRE_MAX_DEBUG_SESSION_STATS; i++) {
		if (session_debug_stats[i].valid) {
			memcpy(stats, &session_debug_stats[i], sizeof(struct nss_stats_gre_session_debug));
			stats++;
		}
	}
	spin_unlock_bh(&nss_gre_stats_lock);
}

/*
 * nss_gre_base_debug_stats_get()
 *	Get GRE debug base statistics.
 */
void nss_gre_base_debug_stats_get(void *stats_mem, int size)
{
	struct nss_stats_gre_base_debug *stats = (struct nss_stats_gre_base_debug *)stats_mem;

	if (!stats) {
		nss_warning("No memory to copy GRE base stats\n");
		return;
	}

	if (size < sizeof(struct nss_stats_gre_base_debug)) {
		nss_warning("Not enough memory to copy GRE base stats\n");
		return;
	}

	spin_lock_bh(&nss_gre_stats_lock);
	memcpy(stats, &base_debug_stats, sizeof(struct nss_stats_gre_base_debug));
	spin_unlock_bh(&nss_gre_stats_lock);
}

/*
 * nss_gre_register_pkt_callback()
 *	Register for data callback.
 */
void nss_gre_register_pkt_callback(nss_gre_pkt_callback_t cb)
{
	atomic64_set(&pkt_cb_addr, (unsigned long)cb);
}
EXPORT_SYMBOL(nss_gre_register_pkt_callback);

/*
 * nss_gre_unregister_pkt_callback()
 *	Unregister for data callback.
 */
void nss_gre_unregister_pkt_callback()
{
	atomic64_set(&pkt_cb_addr, 0);
}
EXPORT_SYMBOL(nss_gre_unregister_pkt_callback);

/*
 * nss_gre_tx_msg()
 *	Transmit a GRE message to NSS firmware
 */
nss_tx_status_t nss_gre_tx_msg(struct nss_ctx_instance *nss_ctx, struct nss_gre_msg *msg)
{
	struct nss_gre_msg *nm;
	struct nss_cmn_msg *ncm = &msg->cm;
	struct sk_buff *nbuf;
	int32_t status;

	NSS_VERIFY_CTX_MAGIC(nss_ctx);
	if (unlikely(nss_ctx->state != NSS_CORE_STATE_INITIALIZED)) {
		nss_warning("%p: gre msg dropped as core not ready", nss_ctx);
		return NSS_TX_FAILURE_NOT_READY;
	}

	/*
	 * Sanity check the message
	 */
	if (!nss_is_dynamic_interface(ncm->interface)) {
		nss_warning("%p: tx request for non dynamic interface: %d", nss_ctx, ncm->interface);
		return NSS_TX_FAILURE;
	}

	if (ncm->type > NSS_GRE_MSG_MAX) {
		nss_warning("%p: message type out of range: %d", nss_ctx, ncm->type);
		return NSS_TX_FAILURE;
	}

	if (nss_cmn_get_msg_len(ncm) > sizeof(struct nss_gre_msg)) {
		nss_warning("%p: message length is invalid: %d", nss_ctx, nss_cmn_get_msg_len(ncm));
		return NSS_TX_FAILURE;
	}

	nbuf = dev_alloc_skb(NSS_NBUF_PAYLOAD_SIZE);
	if (unlikely(!nbuf)) {
		NSS_PKT_STATS_INCREMENT(nss_ctx, &nss_ctx->nss_top->stats_drv[NSS_STATS_DRV_NBUF_ALLOC_FAILS]);
		nss_warning("%p: msg dropped as command allocation failed", nss_ctx);
		return NSS_TX_FAILURE;
	}

	/*
	 * Copy the message to our skb
	 */
	nm = (struct nss_gre_msg *)skb_put(nbuf, sizeof(struct nss_gre_msg));
	memcpy(nm, msg, sizeof(struct nss_gre_msg));

	status = nss_core_send_buffer(nss_ctx, 0, nbuf, NSS_IF_CMD_QUEUE, H2N_BUFFER_CTRL, 0);
	if (status != NSS_CORE_STATUS_SUCCESS) {
		dev_kfree_skb_any(nbuf);
		nss_warning("%p: Unable to enqueue 'gre message'\n", nss_ctx);
		if (status == NSS_CORE_STATUS_FAILURE_QUEUE) {
			return NSS_TX_FAILURE_QUEUE;
		}
		return NSS_TX_FAILURE;
	}

	nss_hal_send_interrupt(nss_ctx, NSS_H2N_INTR_DATA_COMMAND_QUEUE);

	NSS_PKT_STATS_INCREMENT(nss_ctx, &nss_ctx->nss_top->stats_drv[NSS_STATS_DRV_TX_CMD_REQ]);
	return NSS_TX_SUCCESS;
}
EXPORT_SYMBOL(nss_gre_tx_msg);

/*
 * nss_gre_tx_msg_sync()
 *	Transmit a GRE message to NSS firmware synchronously.
 */
nss_tx_status_t nss_gre_tx_msg_sync(struct nss_ctx_instance *nss_ctx, struct nss_gre_msg *msg)
{
	nss_tx_status_t status;
	int ret = 0;

	down(&nss_gre_pvt.sem);
	nss_gre_pvt.cb = (void *)msg->cm.cb;
	nss_gre_pvt.app_data = (void *)msg->cm.app_data;

	msg->cm.cb = (nss_ptr_t)nss_gre_callback;
	msg->cm.app_data = (nss_ptr_t)NULL;

	status = nss_gre_tx_msg(nss_ctx, msg);
	if (status != NSS_TX_SUCCESS) {
		nss_warning("%p: gre_tx_msg failed\n", nss_ctx);
		up(&nss_gre_pvt.sem);
		return status;
	}
	ret = wait_for_completion_timeout(&nss_gre_pvt.complete, msecs_to_jiffies(NSS_GRE_TX_TIMEOUT));

	if (!ret) {
		nss_warning("%p: GRE STD tx sync failed due to timeout\n", nss_ctx);
		nss_gre_pvt.response = NSS_TX_FAILURE;
	}

	status = nss_gre_pvt.response;
	up(&nss_gre_pvt.sem);
	return status;
}
EXPORT_SYMBOL(nss_gre_tx_msg_sync);

/*
 * nss_gre_tx_buf()
 *	Send packet to GRE interface owned by NSS
 */
nss_tx_status_t nss_gre_tx_buf(struct nss_ctx_instance *nss_ctx, uint32_t if_num, struct sk_buff *skb)
{
	int32_t status;

	NSS_VERIFY_CTX_MAGIC(nss_ctx);
	if (unlikely(nss_ctx->state != NSS_CORE_STATE_INITIALIZED)) {
		nss_warning("%p: GRE std packet dropped as core not ready", nss_ctx);
		return NSS_TX_FAILURE_NOT_READY;
	}

	status = nss_core_send_buffer(nss_ctx, if_num, skb, NSS_IF_DATA_QUEUE_0, H2N_BUFFER_PACKET, H2N_BIT_FLAG_VIRTUAL_BUFFER);
	if (unlikely(status != NSS_CORE_STATUS_SUCCESS)) {
		nss_warning("%p: Unable to enqueue GRE std packet\n", nss_ctx);
		return NSS_TX_FAILURE_QUEUE;
	}

	/*
	 * Kick the NSS awake so it can process our new entry.
	 */
	nss_hal_send_interrupt(nss_ctx, NSS_H2N_INTR_DATA_COMMAND_QUEUE);

	NSS_PKT_STATS_INCREMENT(nss_ctx, &nss_ctx->nss_top->stats_drv[NSS_STATS_DRV_TX_PACKET]);
	return NSS_TX_SUCCESS;

}
EXPORT_SYMBOL(nss_gre_tx_buf);

/*
 ***********************************
 * Register/Unregister/Miscellaneous APIs
 ***********************************
 */

/*
 * nss_gre_register_if()
 *	Register data and message handlers for GRE.
 */
struct nss_ctx_instance *nss_gre_register_if(uint32_t if_num, nss_gre_data_callback_t data_callback,
			nss_gre_msg_callback_t event_callback, struct net_device *netdev, uint32_t features)
{
	struct nss_ctx_instance *nss_ctx = (struct nss_ctx_instance *)&nss_top_main.nss[nss_top_main.gre_handler_id];
	int i = 0;

	nss_assert(nss_ctx);
	nss_assert(nss_is_dynamic_interface(if_num));

	nss_ctx->subsys_dp_register[if_num].ndev = netdev;
	nss_ctx->subsys_dp_register[if_num].cb = nss_gre_rx_handler;
	nss_ctx->subsys_dp_register[if_num].app_data = netdev;
	nss_ctx->subsys_dp_register[if_num].features = features;

	nss_top_main.gre_msg_callback = event_callback;
	nss_top_main.gre_data_callback = data_callback;

	nss_core_register_handler(if_num, nss_gre_msg_handler, NULL);

	spin_lock_bh(&nss_gre_stats_lock);
	for (i = 0; i < NSS_GRE_MAX_DEBUG_SESSION_STATS; i++) {
		if (!session_debug_stats[i].valid) {
			session_debug_stats[i].valid = true;
			session_debug_stats[i].if_num = if_num;
			session_debug_stats[i].if_index = netdev->ifindex;
			break;
		}
	}
	spin_unlock_bh(&nss_gre_stats_lock);

	return nss_ctx;
}
EXPORT_SYMBOL(nss_gre_register_if);

/*
 * nss_gre_unregister_if()
 *	Unregister data and message handler.
 */
void nss_gre_unregister_if(uint32_t if_num)
{
	struct nss_ctx_instance *nss_ctx = (struct nss_ctx_instance *)&nss_top_main.nss[nss_top_main.gre_handler_id];
	int i;

	nss_assert(nss_ctx);
	nss_assert(nss_is_dynamic_interface(if_num));

	nss_ctx->subsys_dp_register[if_num].ndev = NULL;
	nss_ctx->subsys_dp_register[if_num].cb = NULL;
	nss_ctx->subsys_dp_register[if_num].app_data = NULL;
	nss_ctx->subsys_dp_register[if_num].features = 0;

	nss_top_main.gre_msg_callback = NULL;

	nss_core_unregister_handler(if_num);

	spin_lock_bh(&nss_gre_stats_lock);
	for (i = 0; i < NSS_GRE_MAX_DEBUG_SESSION_STATS; i++) {
		if (session_debug_stats[i].if_num == if_num) {
			memset(&session_debug_stats[i], 0, sizeof(struct nss_stats_gre_session_debug));
			break;
		}
	}
	spin_unlock_bh(&nss_gre_stats_lock);
}
EXPORT_SYMBOL(nss_gre_unregister_if);

/*
 * nss_get_gre_context()
 */
struct nss_ctx_instance *nss_gre_get_context()
{
	return (struct nss_ctx_instance *)&nss_top_main.nss[nss_top_main.gre_handler_id];
}
EXPORT_SYMBOL(nss_gre_get_context);

/*
 * nss_gre_msg_init()
 *	Initialize nss_gre msg.
 */
void nss_gre_msg_init(struct nss_gre_msg *ncm, uint16_t if_num, uint32_t type,  uint32_t len, void *cb, void *app_data)
{
	nss_cmn_msg_init(&ncm->cm, if_num, type, len, cb, app_data);
}
EXPORT_SYMBOL(nss_gre_msg_init);

/*
 * nss_gre_register_handler()
 *	debugfs stats msg handler received on static gre interface
 */
void nss_gre_register_handler(void)
{
	nss_info("nss_gre_register_handler");
	sema_init(&nss_gre_pvt.sem, 1);
	init_completion(&nss_gre_pvt.complete);
	nss_core_register_handler(NSS_GRE_INTERFACE, nss_gre_msg_handler, NULL);
}
