/*
 **************************************************************************
 * Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
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

#include "nss_data_plane.h"
#include "nss_core.h"
#include "nss_tx_rx_common.h"
#include <nss_dp_api_if.h>

#define NSS_DP_EDMA_SUPPORTED_FEATURES (NETIF_F_HIGHDMA | NETIF_F_HW_CSUM | NETIF_F_RXCSUM | NETIF_F_SG | NETIF_F_FRAGLIST | (NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_UFO))
#define NSS_DATA_PLANE_EDMA_MAX_INTERFACES 6
#define NSS_DATA_PLANE_EDMA_MAX_MTU_SIZE 9000
#define NSS_DATA_PLANE_EDMA_PREHEADER_SIZE 32

/*
 * nss_data_plane_edma_param
 */
struct nss_data_plane_edma_param {
	struct nss_dp_data_plane_ctx dpc;	/* data plane ctx base class */
	int if_num;				/* physical interface number */
	struct nss_ctx_instance *nss_ctx;	/* which nss core */
	int notify_open;			/* This data plane interface has been opened or not */
	uint32_t features;			/* skb types supported by this interface */
	uint32_t bypass_nw_process;		/* Do we want to bypass NW processing in NSS for this data plane? */
} nss_data_plane_edma_params[NSS_DATA_PLANE_EDMA_MAX_INTERFACES + 1];

/*
 * __nss_data_plane_init()
 */
static int __nss_data_plane_init(struct nss_dp_data_plane_ctx *dpc)
{
	struct net_device *netdev = dpc->dev;
	netdev->needed_headroom += 32;
	return NSS_DP_SUCCESS;
}

/*
 * __nss_data_plane_open()
 *	Called by nss-dp to notify open to nss-fw
 */
static int __nss_data_plane_open(struct nss_dp_data_plane_ctx *dpc, uint32_t tx_desc_ring, uint32_t rx_desc_ring, uint32_t mode)
{
	struct nss_data_plane_edma_param *dp = (struct nss_data_plane_edma_param *)dpc;

	if (dp->notify_open) {
		return NSS_DP_SUCCESS;
	}
	if (nss_phys_if_open(dp->nss_ctx, tx_desc_ring, rx_desc_ring, mode, dp->if_num, dp->bypass_nw_process) == NSS_TX_SUCCESS) {
		dp->notify_open = 1;
		return NSS_DP_SUCCESS;
	}
	return NSS_DP_FAILURE;
}

/*
 * __nss_data_plane_close()
 *	Called by nss-dp to notify close to nss-fw
 */
static int __nss_data_plane_close(struct nss_dp_data_plane_ctx *dpc)
{
	return NSS_DP_SUCCESS;
}

/*
 * __nss_data_plane_link_state()
 *	Called by nss-dp to notify link state change to nss-fw
 */
static int __nss_data_plane_link_state(struct nss_dp_data_plane_ctx *dpc, uint32_t link_state)
{
	struct nss_data_plane_edma_param *dp = (struct nss_data_plane_edma_param *)dpc;

	return nss_phys_if_link_state(dp->nss_ctx, link_state, dp->if_num);
}

/*
 * __nss_data_plane_mac_addr()
 *	Called by nss-dp to set mac address
 */
static int __nss_data_plane_mac_addr(struct nss_dp_data_plane_ctx *dpc, uint8_t *addr)
{
	struct nss_data_plane_edma_param *dp = (struct nss_data_plane_edma_param *)dpc;

	return nss_phys_if_mac_addr(dp->nss_ctx, addr, dp->if_num);
}

/*
 * __nss_data_plane_change_mtu()
 *	Called by nss-dp to change mtu of a data plane
 */
static int __nss_data_plane_change_mtu(struct nss_dp_data_plane_ctx *dpc, uint32_t mtu)
{
	struct nss_data_plane_edma_param *dp = (struct nss_data_plane_edma_param *)dpc;

	if (mtu > NSS_DATA_PLANE_EDMA_MAX_MTU_SIZE) {
		nss_warning("%p: MTU exceeds MAX size %d\n", dp, mtu);
		return NSS_DP_FAILURE;
	}

	return nss_phys_if_change_mtu(dp->nss_ctx, mtu, dp->if_num);
}

/*
 * __nss_data_plane_pause_on_off()
 *	Called by nss-dp to enable/disable pause frames
 */
static int __nss_data_plane_pause_on_off(struct nss_dp_data_plane_ctx *dpc, uint32_t pause_on)
{
	struct nss_data_plane_edma_param *dp = (struct nss_data_plane_edma_param *)dpc;

	return nss_phys_if_pause_on_off(dp->nss_ctx, pause_on, dp->if_num);
}

/*
 * __nss_data_plane_vsi_assign()
 *	Called by nss-dp to assign vsi of a data plane
 */
static int __nss_data_plane_vsi_assign(struct nss_dp_data_plane_ctx *dpc, uint32_t vsi)
{
	struct nss_data_plane_edma_param *dp = (struct nss_data_plane_edma_param *)dpc;

	return nss_phys_if_vsi_assign(dp->nss_ctx, vsi, dp->if_num);
}

/*
 * __nss_data_plane_vsi_unassign()
 *	Called by nss-dp to unassign vsi of a data plane
 */
static int __nss_data_plane_vsi_unassign(struct nss_dp_data_plane_ctx *dpc, uint32_t vsi)
{
	struct nss_data_plane_edma_param *dp = (struct nss_data_plane_edma_param *)dpc;

	return nss_phys_if_vsi_unassign(dp->nss_ctx, vsi, dp->if_num);
}

/*
 * __nss_data_plane_buf()
 *	Called by nss-dp to pass a sk_buff for xmit
 */
static int __nss_data_plane_buf(struct nss_dp_data_plane_ctx *dpc, struct sk_buff *skb)
{
	struct nss_data_plane_edma_param *dp = (struct nss_data_plane_edma_param *)dpc;
	int nhead = dpc->dev->needed_headroom;
	bool expand_skb = false;

	if (skb_cloned(skb) || (skb_headroom(skb) < nhead)) {
		expand_skb = true;
	}

	if (expand_skb && pskb_expand_head(skb, nhead, 0, GFP_KERNEL)) {
		nss_trace("%p: Unable to expand skb for headroom\n", dp);
		return NSS_TX_FAILURE;
	}

	return nss_phys_if_buf(dp->nss_ctx, skb, dp->if_num);
}

/*
 * __nss_data_plane_set_features()
 *	Called by nss-dp to allow data plane to modify the set of features it supports
*/
static void __nss_data_plane_set_features(struct nss_dp_data_plane_ctx *dpc)
{
	dpc->dev->features |= NSS_DP_EDMA_SUPPORTED_FEATURES;
	dpc->dev->hw_features |= NSS_DP_EDMA_SUPPORTED_FEATURES;
	dpc->dev->vlan_features |= NSS_DP_EDMA_SUPPORTED_FEATURES;
	dpc->dev->wanted_features |= NSS_DP_EDMA_SUPPORTED_FEATURES;
}

/*
 * nss offload data plane ops
 */
static struct nss_dp_data_plane_ops dp_ops = {
	.init		= __nss_data_plane_init,
	.open		= __nss_data_plane_open,
	.close		= __nss_data_plane_close,
	.link_state	= __nss_data_plane_link_state,
	.mac_addr	= __nss_data_plane_mac_addr,
	.change_mtu	= __nss_data_plane_change_mtu,
	.xmit		= __nss_data_plane_buf,
	.set_features	= __nss_data_plane_set_features,
	.pause_on_off	= __nss_data_plane_pause_on_off,
	.vsi_assign	= __nss_data_plane_vsi_assign,
	.vsi_unassign	= __nss_data_plane_vsi_unassign,
};

/*
 * nss_data_plane_register_to_nss_dp()
 */
static bool nss_data_plane_register_to_nss_dp(struct nss_ctx_instance *nss_ctx, int if_num)
{
	struct nss_data_plane_edma_param *ndpp = &nss_data_plane_edma_params[if_num];
	struct nss_top_instance *nss_top = nss_ctx->nss_top;
	struct net_device *netdev;
	bool is_open;
	int core;

	netdev = nss_dp_get_netdev_by_macid(if_num);
	if (!netdev) {
		nss_info("%p: Platform don't have data plane%d enabled, don't bring up nss_phys_if and don't register to nss-dp\n", nss_ctx, if_num);
		return false;
	}

	is_open = nss_dp_is_in_open_state(netdev);
	ndpp->dpc.dev = netdev;
	ndpp->nss_ctx = nss_ctx;
	ndpp->if_num = if_num;
	ndpp->notify_open = 0;
	ndpp->features = 0;

	/*
	 * Check if NSS NW processing to be bypassed for this data plane
	 */
	if (nss_skip_nw_process) {
		ndpp->bypass_nw_process = 1;
	} else {
		ndpp->bypass_nw_process = 0;
	}

	if (nss_dp_override_data_plane(netdev, &dp_ops, (struct nss_dp_data_plane_ctx *)ndpp) != NSS_DP_SUCCESS) {
		nss_info("%p: Override nss-dp data plane for port %dfailed\n", nss_ctx, if_num);
		return false;
	}

	/*
	 * Setup the receive callback so that data pkts received form NSS-FW will
	 * be redirected to the nss-dp driver as we are overriding the data plane
	 */
	nss_top->phys_if_handler_id[if_num] = nss_ctx->id;
	nss_phys_if_register_handler(if_num);

	/*
	 * Packets recieved on physical interface can be exceptioned to HLOS
	 * from any NSS core so we need to register data plane for all
	 */
	for (core = 0; core < NSS_MAX_CORES; core++) {
		nss_top->nss[core].subsys_dp_register[if_num].ndev = netdev;
		nss_top->nss[core].subsys_dp_register[if_num].cb = nss_dp_receive;
		nss_top->nss[core].subsys_dp_register[if_num].app_data = NULL;
		nss_top->nss[core].subsys_dp_register[if_num].features = ndpp->features;
	}

	/*
	 * Now we are registered and our side is ready, if the data plane was opened, ask it to start again
	 */
	if (is_open) {
		nss_dp_start_data_plane(netdev, (struct nss_dp_data_plane_ctx *)ndpp);
	}
	return true;
}

/*
 * nss_data_plane_unregister_from_nss_dp()
 */
static void nss_data_plane_unregister_from_nss_dp(int if_num)
{
	nss_dp_restore_data_plane(nss_data_plane_edma_params[if_num].dpc.dev);
	nss_data_plane_edma_params[if_num].dpc.dev = NULL;
	nss_data_plane_edma_params[if_num].nss_ctx = NULL;
	nss_data_plane_edma_params[if_num].if_num = 0;
	nss_data_plane_edma_params[if_num].notify_open = 0;
	nss_data_plane_edma_params[if_num].bypass_nw_process = 0;
}

/*
 * __nss_data_plane_register()
 */
static void __nss_data_plane_register(struct nss_ctx_instance *nss_ctx)
{
	int i;

	for (i = 1; i < NSS_DATA_PLANE_EDMA_MAX_INTERFACES + 1; i++) {
		if (!nss_data_plane_register_to_nss_dp(nss_ctx, i)) {
			nss_warning("%p: Register data plane failed for data plane %d\n", nss_ctx, i);
		} else {
			nss_info("%p: Register data plan to data plane %d success\n", nss_ctx, i);
		}
	}
}

/*
 * __nss_data_plane_unregister()
 */
static void __nss_data_plane_unregister(void)
{
	int i, core;

	for (core = 0; core < NSS_MAX_CORES; core++) {
		for (i = 1; i < NSS_DATA_PLANE_EDMA_MAX_INTERFACES + 1; i++) {
			if (nss_top_main.nss[core].subsys_dp_register[i].ndev) {
				nss_data_plane_unregister_from_nss_dp(i);
				nss_top_main.nss[core].subsys_dp_register[i].ndev = NULL;
			}
		}
	}
}

/*
 * __nss_data_plane_stats_sync()
 */
static void __nss_data_plane_stats_sync(struct nss_phys_if_stats *stats, uint16_t interface)
{
	/*
	 * EDMA does not pass sync interface stats through phys_if_stats
	 */
}

/*
 * __nss_data_plane_get_mtu_sz()
 */
static uint16_t __nss_data_plane_get_mtu_sz(uint16_t mtu)
{
	/*
	 * Reserve space for preheader
	 */
	return mtu + NSS_DATA_PLANE_EDMA_PREHEADER_SIZE;
}

/*
 * nss_data_plane_edma_ops
 */
struct nss_data_plane_ops nss_data_plane_edma_ops = {
	.data_plane_register = &__nss_data_plane_register,
	.data_plane_unregister = &__nss_data_plane_unregister,
	.data_plane_stats_sync = &__nss_data_plane_stats_sync,
	.data_plane_get_mtu_sz = &__nss_data_plane_get_mtu_sz,
};

