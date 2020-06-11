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

/**
 * nss_hal.c
 *	NSS HAL general APIs.
 */

#include <linux/err.h>
#include <linux/version.h>
#include <linux/firmware.h>
#include <linux/of.h>

#include "nss_hal.h"
#include "nss_core.h"
#include "nss_tx_rx_common.h"
#include "nss_data_plane.h"
#if (NSS_PM_SUPPORT == 1)
#include "nss_pm.h"
#endif
#if (NSS_FABRIC_SCALING_SUPPORT == 1)
#include <linux/fab_scaling.h>
#endif

/*
 * Macros
 */
#define MIN_IMG_SIZE (64*1024)
#define NSS_AP0_IMAGE "qca-nss0.bin"
#define NSS_AP1_IMAGE "qca-nss1.bin"

/*
 * File local/Static variables/functions
 */
static const struct net_device_ops nss_netdev_ops;
static const struct ethtool_ops nss_ethtool_ops;

int nss_hal_firmware_load(struct nss_ctx_instance *nss_ctx, struct platform_device *nss_dev, struct nss_platform_data *npd)
{
	const struct firmware *nss_fw;
	void __iomem *load_mem;
	int rc;

	if (nss_ctx->id == 0) {
		rc = request_firmware(&nss_fw, NSS_AP0_IMAGE, &(nss_dev->dev));
	} else if (nss_ctx->id == 1) {
		rc = request_firmware(&nss_fw, NSS_AP1_IMAGE, &(nss_dev->dev));
	} else {
		nss_warning("%p: Invalid nss dev: %d\n", nss_ctx, nss_ctx->id);
		return -EINVAL;
	}

	/*
	 *  Check if the file read is successful
	 */
	if (rc) {
		nss_info_always("%p: request_firmware failed with err code: %d", nss_ctx, rc);
		return rc;
	}

	if (nss_fw->size < MIN_IMG_SIZE) {
		nss_info_always("%p: nss firmware is truncated, size:%d", nss_ctx, (int)nss_fw->size);
		return rc;
	}

	load_mem = ioremap_nocache(npd->load_addr, nss_fw->size);
	if (!load_mem) {
		nss_info_always("%p: ioremap_nocache failed: %x", nss_ctx, npd->load_addr);
		release_firmware(nss_fw);
		return rc;
	}

	nss_info_always("nss_driver - fw of size %d  bytes copied to load addr: %x, nss_id : %d\n", (int)nss_fw->size, npd->load_addr, nss_dev->id);
	memcpy_toio(load_mem, nss_fw->data, nss_fw->size);
	release_firmware(nss_fw);
	iounmap(load_mem);
	return 0;
}

/*
 * nss_hal_dt_parse_features()
 */
void nss_hal_dt_parse_features(struct device_node *np, struct nss_platform_data *npd)
{
	/*
	 * Read the features in
	 */
	npd->capwap_enabled = of_property_read_bool(np, "qcom,capwap-enabled");
	npd->crypto_enabled = of_property_read_bool(np, "qcom,crypto-enabled");
	npd->dtls_enabled = of_property_read_bool(np, "qcom,dtls-enabled");
	npd->gre_redir_enabled = of_property_read_bool(np, "qcom,gre-redir-enabled");
	npd->gre_tunnel_enabled = of_property_read_bool(np, "qcom,gre_tunnel_enabled");
	npd->ipsec_enabled = of_property_read_bool(np, "qcom,ipsec-enabled");
	npd->ipv4_enabled = of_property_read_bool(np, "qcom,ipv4-enabled");
	npd->ipv4_reasm_enabled = of_property_read_bool(np, "qcom,ipv4-reasm-enabled");
	npd->ipv6_enabled = of_property_read_bool(np, "qcom,ipv6-enabled");
	npd->ipv6_reasm_enabled = of_property_read_bool(np, "qcom,ipv6-reasm-enabled");
	npd->l2tpv2_enabled = of_property_read_bool(np, "qcom,l2tpv2-enabled");
	npd->map_t_enabled = of_property_read_bool(np, "qcom,map-t-enabled");
	npd->gre_enabled = of_property_read_bool(np, "qcom,gre-enabled");
	npd->oam_enabled = of_property_read_bool(np, "qcom,oam-enabled");
	npd->ppe_enabled = of_property_read_bool(np, "qcom,ppe-enabled");
	npd->pppoe_enabled = of_property_read_bool(np, "qcom,pppoe-enabled");
	npd->pptp_enabled = of_property_read_bool(np, "qcom,pptp-enabled");
	npd->portid_enabled = of_property_read_bool(np, "qcom,portid-enabled");
	npd->shaping_enabled = of_property_read_bool(np, "qcom,shaping-enabled");
	npd->tstamp_enabled = of_property_read_bool(np, "qcom,tstamp-enabled");
	npd->turbo_frequency = of_property_read_bool(np, "qcom,turbo-frequency");
	npd->tun6rd_enabled = of_property_read_bool(np, "qcom,tun6rd-enabled");
	npd->tunipip6_enabled = of_property_read_bool(np, "qcom,tunipip6-enabled");
	npd->wlanredirect_enabled = of_property_read_bool(np, "qcom,wlanredirect-enabled");
	npd->wifioffload_enabled = of_property_read_bool(np, "qcom,wlan-dataplane-offload-enabled");
	npd->bridge_enabled = of_property_read_bool(np, "qcom,bridge-enabled");
	npd->vlan_enabled = of_property_read_bool(np, "qcom,vlan-enabled");
}

/*
 * nss_hal_dummy_netdev_setup()
 *	Dummy setup for net_device handler
 */
static void nss_hal_dummy_netdev_setup(struct net_device *ndev)
{

}

/*
 * nss_hal_clean_up_netdevice()
 */
static void nss_hal_clean_up_netdevice(struct int_ctx_instance *int_ctx)
{
	int i;

	for (i = 0; i < NSS_MAX_IRQ_PER_INSTANCE; i++) {
		if (int_ctx->irq[i]) {
			free_irq(int_ctx->irq[i], int_ctx);
			int_ctx->irq[i] = 0;
		}
	}

	if (!int_ctx->ndev) {
		return;
	}

	unregister_netdev(int_ctx->ndev);
	free_netdev(int_ctx->ndev);
	int_ctx->ndev = NULL;
}

/*
 * nss_hal_register_netdevice()
 */
static int nss_hal_register_netdevice(struct nss_ctx_instance *nss_ctx, struct nss_platform_data *npd, int qnum)
{
	struct nss_top_instance *nss_top = &nss_top_main;
	struct net_device *netdev;
	struct netdev_priv_instance *ndev_priv;
	struct int_ctx_instance *int_ctx = &nss_ctx->int_ctx[qnum];
	int err = 0;

	/*
	 * Register netdevice handlers
	 */
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3, 16, 0))
	netdev = alloc_netdev(sizeof(struct netdev_priv_instance),
					"qca-nss-dev%d", nss_hal_dummy_netdev_setup);
#else
	netdev = alloc_netdev(sizeof(struct netdev_priv_instance),
					"qca-nss-dev%d", NET_NAME_ENUM, nss_hal_dummy_netdev_setup);
#endif
	if (!netdev) {
		nss_warning("%p: Could not allocate net_device for queue %d\n", nss_ctx, qnum);
		return -ENOMEM;
	}

	netdev->netdev_ops = &nss_netdev_ops;
	netdev->ethtool_ops = &nss_ethtool_ops;
	err = register_netdev(netdev);
	if (err) {
		nss_warning("%p: Could not register net_device %d\n", nss_ctx, qnum);
		free_netdev(netdev);
		return err;
	}

	/*
	 * request for IRQs
	 */
	int_ctx->nss_ctx = nss_ctx;
	int_ctx->ndev = netdev;
	err = nss_top->hal_ops->request_irq_for_queue(nss_ctx, npd, qnum);
	if (err) {
		nss_warning("%p: IRQ request for queue %d failed", nss_ctx, qnum);
		return err;
	}

	/*
	 * Register NAPI for NSS core interrupt
	 */
	ndev_priv = netdev_priv(netdev);
	ndev_priv->int_ctx = int_ctx;
	netif_napi_add(netdev, &int_ctx->napi, nss_core_handle_napi, 64);
	napi_enable(&int_ctx->napi);
	return 0;
}

/*
 * nss_hal_probe()
 *	HLOS device probe callback
 */
int nss_hal_probe(struct platform_device *nss_dev)
{
	struct nss_top_instance *nss_top = &nss_top_main;
	struct nss_ctx_instance *nss_ctx = NULL;
	struct nss_platform_data *npd = NULL;
	int i, err = 0;
	struct net_device *tstamp_ndev = NULL;

	if (nss_top_main.nss_hal_common_init_done == false) {
		err = nss_top->hal_ops->common_reset(nss_dev);
		if (err) {
			nss_info_always("NSS HAL common init failed\n");
			return -EFAULT;
		}
	}

#if (NSS_DT_SUPPORT == 1)
	if (!nss_dev->dev.of_node) {
		pr_err("nss-driver: Device tree not available\n");
		return -ENODEV;
	}

	npd = nss_top->hal_ops->of_get_pdata(nss_dev);
	if (!npd) {
		return -EFAULT;
	}

	nss_ctx = &nss_top->nss[npd->id];
	nss_ctx->id = npd->id;
	nss_dev->id = nss_ctx->id;
#else
	npd = (struct nss_platform_data *) nss_dev->dev.platform_data;
	nss_ctx = &nss_top->nss[nss_dev->id];
	nss_ctx->id = nss_dev->id;
#endif
	nss_ctx->nss_top = nss_top;

	nss_info("%p: NSS_DEV_ID %s\n", nss_ctx, dev_name(&nss_dev->dev));

	/*
	 * Do firmware load from nss-drv if required
	 */
	err = nss_top->hal_ops->firmware_load(nss_ctx, nss_dev, npd);
	if (err) {
		nss_warning("%p: firmware load from driver failed\n", nss_ctx);
		goto err_init;
	}

	/*
	 * Both NSS cores controlled by same regulator, Hook only Once
	 */
	if (!nss_ctx->id) {
		err = nss_top->hal_ops->clock_configure(nss_ctx, nss_dev, npd);
		if (err) {
			nss_warning("%p: clock configure failed\n", nss_ctx);
			goto err_init;
		}
	}

	/*
	 * Get load address of NSS firmware
	 */
	nss_info("%p: Setting NSS%d Firmware load address to %x\n", nss_ctx, nss_ctx->id, npd->load_addr);
	nss_top->nss[nss_ctx->id].load = npd->load_addr;

	/*
	 * Get virtual and physical memory addresses for nss logical/hardware address maps
	 */

	/*
	 * Virtual address of CSM space
	 */
	nss_ctx->nmap = npd->nmap;

	/*
	 * Physical address of CSM space
	 */
	nss_ctx->nphys = npd->nphys;
	nss_assert(nss_ctx->nphys);

	/*
	 * Virtual address of logical registers space
	 */
	nss_ctx->vmap = npd->vmap;

	/*
	 * Virtual address of QGIC interrupt space
	 */
	nss_ctx->qgic_map = npd->qgic_map;

	/*
	 * Physical address of logical registers space
	 */
	nss_ctx->vphys = npd->vphys;
	nss_assert(nss_ctx->vphys);
	nss_info("%d:ctx=%p, vphys=%x, vmap=%p, nphys=%x, nmap=%p", nss_ctx->id,
			nss_ctx, nss_ctx->vphys, nss_ctx->vmap, nss_ctx->nphys, nss_ctx->nmap);

	for (i = 0; i < npd->num_queue; i++) {
		err = nss_hal_register_netdevice(nss_ctx, npd, i);
		if (err) {
			goto err_register_netdevice;
		}
	}

	/*
	 * Allocate tstamp net_device and register the net_device
	 */
	if (npd->tstamp_enabled == NSS_FEATURE_ENABLED) {
		tstamp_ndev = nss_tstamp_register_netdev();
		if (!tstamp_ndev) {
			nss_warning("%p: Unable to register the TSTAMP net_device", nss_ctx);
			npd->tstamp_enabled = NSS_FEATURE_NOT_ENABLED;
		}
	}
	spin_lock_bh(&(nss_top->lock));

	/*
	 * Check functionalities are supported by this NSS core
	 */
	if (npd->shaping_enabled == NSS_FEATURE_ENABLED) {
		nss_top->shaping_handler_id = nss_dev->id;
		nss_info("%d: NSS shaping is enabled", nss_dev->id);
	}

	if (npd->ipv4_enabled == NSS_FEATURE_ENABLED) {
		nss_top->ipv4_handler_id = nss_dev->id;
		nss_ipv4_register_handler();
		if (npd->pppoe_enabled == NSS_FEATURE_ENABLED) {
			nss_pppoe_register_handler();
		}

		nss_top->edma_handler_id = nss_dev->id;
		nss_edma_register_handler();
		nss_eth_rx_register_handler();
		nss_n2h_register_handler();
		nss_lag_register_handler();
		nss_dynamic_interface_register_handler();
		nss_top->trustsec_tx_handler_id = nss_dev->id;
		nss_trustsec_tx_register_handler();

		for (i = 0; i < NSS_MAX_VIRTUAL_INTERFACES; i++) {
			nss_top->virt_if_handler_id[i] = nss_dev->id;
		}

		nss_top->dynamic_interface_table[NSS_DYNAMIC_INTERFACE_TYPE_802_3_REDIR] = nss_dev->id;
	}

	if (npd->capwap_enabled == NSS_FEATURE_ENABLED) {
		nss_top->capwap_handler_id = nss_dev->id;
		nss_top->dynamic_interface_table[NSS_DYNAMIC_INTERFACE_TYPE_CAPWAP] = nss_dev->id;
	}

	if (npd->ipv4_reasm_enabled == NSS_FEATURE_ENABLED) {
		nss_top->ipv4_reasm_handler_id = nss_dev->id;
		nss_ipv4_reasm_register_handler();
	}

	if (npd->ipv6_enabled == NSS_FEATURE_ENABLED) {
		nss_top->ipv6_handler_id = nss_dev->id;
		nss_ipv6_register_handler();
	}

	if (npd->ipv6_reasm_enabled == NSS_FEATURE_ENABLED) {
		nss_top->ipv6_reasm_handler_id = nss_dev->id;
		nss_ipv6_reasm_register_handler();
	}

	if (npd->crypto_enabled == NSS_FEATURE_ENABLED) {
		nss_top->crypto_enabled = 1;
		nss_top->crypto_handler_id = nss_dev->id;
		nss_crypto_register_handler();
	}

	if (npd->ipsec_enabled == NSS_FEATURE_ENABLED) {
		nss_top->ipsec_handler_id = nss_dev->id;
		nss_ipsec_register_handler();
	}

	if (npd->wlanredirect_enabled == NSS_FEATURE_ENABLED) {
		nss_top->wlan_handler_id = nss_dev->id;
	}

	if (npd->tun6rd_enabled == NSS_FEATURE_ENABLED) {
		nss_top->tun6rd_handler_id = nss_dev->id;
	}

	if (npd->pptp_enabled == NSS_FEATURE_ENABLED) {
		nss_top->pptp_handler_id = nss_dev->id;
		nss_pptp_register_handler();
	}

	if (npd->ppe_enabled == NSS_FEATURE_ENABLED) {
		nss_top->ppe_handler_id = nss_dev->id;
		nss_ppe_register_handler();
	}

	if (npd->l2tpv2_enabled == NSS_FEATURE_ENABLED) {
		nss_top->l2tpv2_handler_id = nss_dev->id;
		nss_l2tpv2_register_handler();
	}

	if (npd->dtls_enabled == NSS_FEATURE_ENABLED) {
		nss_top->dtls_handler_id = nss_dev->id;
		nss_top->dynamic_interface_table[NSS_DYNAMIC_INTERFACE_TYPE_DTLS] = nss_dev->id;
		nss_dtls_register_handler();
	}

	if (npd->map_t_enabled == NSS_FEATURE_ENABLED) {
		nss_top->map_t_handler_id = nss_dev->id;
		nss_map_t_register_handler();
	}

	if (npd->gre_enabled == NSS_FEATURE_ENABLED) {
		nss_top->gre_handler_id = nss_dev->id;
		nss_gre_register_handler();
	}

	if (npd->tunipip6_enabled == NSS_FEATURE_ENABLED) {
		nss_top->tunipip6_handler_id = nss_dev->id;
		nss_tunipip6_register_handler();
	}

	if (npd->gre_redir_enabled == NSS_FEATURE_ENABLED) {
		nss_top->gre_redir_handler_id = nss_dev->id;
		nss_top->dynamic_interface_table[NSS_DYNAMIC_INTERFACE_TYPE_GRE_REDIR] = nss_dev->id;
		nss_gre_redir_register_handler();
		nss_sjack_register_handler();
	}

	if (npd->gre_tunnel_enabled == NSS_FEATURE_ENABLED) {
		nss_top->gre_tunnel_handler_id = nss_dev->id;
		nss_top->dynamic_interface_table[NSS_DYNAMIC_INTERFACE_TYPE_GRE_TUNNEL] = nss_dev->id;
	}

	if (npd->portid_enabled == NSS_FEATURE_ENABLED) {
		nss_top->portid_handler_id = nss_dev->id;
		nss_top->dynamic_interface_table[NSS_DYNAMIC_INTERFACE_TYPE_PORTID] = nss_dev->id;
		nss_portid_register_handler();
	}

	if (npd->wifioffload_enabled == NSS_FEATURE_ENABLED) {
		nss_top->wifi_handler_id = nss_dev->id;
		nss_top->dynamic_interface_table[NSS_DYNAMIC_INTERFACE_TYPE_VAP] = nss_dev->id;
		nss_wifi_register_handler();
	}

	if (npd->tstamp_enabled == NSS_FEATURE_ENABLED) {
		nss_top->tstamp_handler_id = nss_dev->id;
		nss_tstamp_register_handler(tstamp_ndev);
	}

	if (npd->oam_enabled == NSS_FEATURE_ENABLED) {
		nss_top->oam_handler_id = nss_dev->id;
		nss_oam_register_handler();
	}

	if (npd->bridge_enabled == NSS_FEATURE_ENABLED) {
		nss_top->bridge_handler_id = nss_dev->id;
		nss_top->dynamic_interface_table[NSS_DYNAMIC_INTERFACE_TYPE_BRIDGE] = nss_dev->id;
		nss_bridge_init();
	}

	if (npd->vlan_enabled == NSS_FEATURE_ENABLED) {
		nss_top->vlan_handler_id = nss_dev->id;
		nss_top->dynamic_interface_table[NSS_DYNAMIC_INTERFACE_TYPE_VLAN] = nss_dev->id;
		nss_vlan_register_handler();
	}

	if (nss_ctx->id == 0) {
#if (NSS_FREQ_SCALE_SUPPORT == 1)
		nss_freq_register_handler();
#endif
		nss_lso_rx_register_handler();
	}

	nss_top->frequency_handler_id = nss_dev->id;

	spin_unlock_bh(&(nss_top->lock));

	/*
	 * Initialize decongestion callbacks to NULL
	 */
	for (i = 0; i < NSS_MAX_CLIENTS; i++) {
		nss_ctx->queue_decongestion_callback[i] = 0;
		nss_ctx->queue_decongestion_ctx[i] = 0;
	}

	spin_lock_init(&(nss_ctx->decongest_cb_lock));
	nss_ctx->magic = NSS_CTX_MAGIC;

	nss_info("%p: Reseting NSS core %d now", nss_ctx, nss_ctx->id);

	/*
	 * Enable clocks and bring NSS core out of reset
	 */
	err = nss_top->hal_ops->core_reset(nss_dev, nss_ctx->nmap, nss_ctx->load, nss_top->clk_src);
	if (err) {
		goto err_register_netdevice;
	}

	/*
	 * Initialize max buffer size for NSS core
	 */
	nss_ctx->max_buf_size = NSS_NBUF_PAYLOAD_SIZE;

	/*
	 * Initialize S/G status pointers to NULL
	 */
	for (i = 0; i < NSS_N2H_DESC_RING_NUM; i++) {
		nss_ctx->n2h_desc_ring[i].head = NULL;
		nss_ctx->n2h_desc_ring[i].tail = NULL;
		nss_ctx->n2h_desc_ring[i].jumbo_start = NULL;
	}

	/*
	 * Increment number of cores
	 */
	nss_top->num_nss++;

	/*
	 * dev is required for dma map/unmap
	 */
	nss_ctx->dev = &nss_dev->dev;

	/*
	 * Enable interrupts for NSS core
	 */
	nss_hal_enable_interrupt(nss_ctx, nss_ctx->int_ctx[0].shift_factor, NSS_HAL_SUPPORTED_INTERRUPTS);

	if (npd->num_queue > 1) {
		nss_hal_enable_interrupt(nss_ctx, nss_ctx->int_ctx[1].shift_factor, NSS_HAL_SUPPORTED_INTERRUPTS);
	}

	nss_info("%p: All resources initialized and nss core%d has been brought out of reset", nss_ctx, nss_dev->id);
	goto out;

err_register_netdevice:
	for (i = 0; i < npd->num_queue; i++) {
		nss_hal_clean_up_netdevice(&nss_ctx->int_ctx[i]);
	}

err_init:
	if (nss_dev->dev.of_node) {
		if (npd->nmap) {
			iounmap(npd->nmap);
		}

		if (npd->vmap) {
			iounmap(npd->vmap);
		}
	}

out:
	if (nss_dev->dev.of_node) {
		devm_kfree(&nss_dev->dev, npd);
	}
	return err;
}

/*
 * nss_hal_remove()
 *	HLOS device remove callback
 */
int nss_hal_remove(struct platform_device *nss_dev)
{
	struct nss_top_instance *nss_top = &nss_top_main;
	struct nss_ctx_instance *nss_ctx = &nss_top->nss[nss_dev->id];

	/*
	 * Clean up debugfs
	 */
	nss_stats_clean();

	/*
	 * Clean up netdev/interrupts
	 */
	nss_hal_disable_interrupt(nss_ctx, nss_ctx->int_ctx[0].shift_factor, NSS_HAL_SUPPORTED_INTERRUPTS);
	nss_hal_clean_up_netdevice(&nss_ctx->int_ctx[0]);

	/*
	 * Check if second interrupt is supported
	 * If so then clear resources for second interrupt as well
	 */
	if (nss_ctx->int_ctx[1].ndev) {
		nss_hal_disable_interrupt(nss_ctx, nss_ctx->int_ctx[1].shift_factor, NSS_HAL_SUPPORTED_INTERRUPTS);
		nss_hal_clean_up_netdevice(&nss_ctx->int_ctx[1]);
	}

	/*
	 * nss-drv is exiting, unregister and restore host data plane
	 */
	nss_top->data_plane_ops->data_plane_unregister();

#if (NSS_FABRIC_SCALING_SUPPORT == 1)
	fab_scaling_unregister(nss_core0_clk);
#endif

	if (nss_dev->dev.of_node) {
		if (nss_ctx->nmap) {
			iounmap(nss_ctx->nmap);
			nss_ctx->nmap = 0;
		}

		if (nss_ctx->vmap) {
			iounmap(nss_ctx->vmap);
			nss_ctx->vmap = 0;
		}
	}

	nss_info("%p: All resources freed for nss core%d", nss_ctx, nss_dev->id);
	return 0;
}
