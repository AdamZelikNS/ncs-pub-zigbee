/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 *  @brief Zigbee Network Co-processor sample
 */

#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/logging/log.h>
#include <zb_nrf_platform.h>
#include <zb_led_button.h>
#include <zb_osif_ext.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <dk_buttons_and_leds.h>
#include <ncp/ncp_dev_api.h>

#if CONFIG_BOOTLOADER_MCUBOOT
#include <zephyr/dfu/mcuboot.h>
#endif

#include <stdio.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#define VENDOR_SPECIFIC_LED DK_LED2

#define VENDOR_SPECIFIC_LED_ACTION_OFF (0U)
#define VENDOR_SPECIFIC_LED_ACTION_ON (1U)
#define VENDOR_SPECIFIC_LED_ACTION_TOGGLE (2U)

#define VENDOR_SPECIFIC_REQUEST_LEN (1U)
#define VENDOR_SPECIFIC_RESPONSE_LEN (1U)

#define VENDOR_SPECIFIC_IND_LEN (1U)
#define VENDOR_SPECIFIC_IND_DELAY (ZB_TIME_ONE_SECOND * 3)


/* The state of a led controlled by ncp custom commands */
static zb_uint8_t vendor_specific_led_state = VENDOR_SPECIFIC_LED_ACTION_OFF;


zb_ret_t zb_osif_bootloader_run_after_reboot(void)
{
#if DT_NODE_EXISTS(DT_ALIAS(rst0))
	int err = 0;
	const struct gpio_dt_spec rst0 = GPIO_DT_SPEC_GET(DT_ALIAS(rst0), gpios);

	if (!device_is_ready(rst0.port)) {
		return RET_ERROR;
	}

	err = gpio_pin_configure_dt(&rst0, GPIO_OUTPUT_ACTIVE);
	if (err) {
		return RET_ERROR;
	}
#endif
	return RET_OK;
}

void zb_osif_bootloader_report_successful_loading(void)
{
#if CONFIG_BOOTLOADER_MCUBOOT
	if (!boot_is_img_confirmed()) {
		int ret = boot_write_img_confirmed();

		if (ret) {
			LOG_ERR("Couldn't confirm image: %d", ret);
		} else {
			LOG_INF("Marked image as OK");
		}
	}
#endif
}

static void custom_indication(zb_uint8_t buf, zb_uint16_t led_idx)
{
	zb_uint8_t *ind_data = zb_buf_initial_alloc(buf, VENDOR_SPECIFIC_IND_LEN);

	*ind_data = (zb_uint8_t)led_idx;

	zb_ncp_custom_indication(buf);
}

static void perform_custom_indication(zb_uint8_t led_idx)
{
	zb_buf_get_out_delayed_ext(custom_indication, led_idx, 0);
}

#if (defined ZBOSS_PLATFORM_MAJOR) && (ZBOSS_PLATFORM_MAJOR < 5U)
static zb_ret_t ncp_vendor_specific_req_handler(zb_uint8_t buf)
#else /* (defined ZBOSS_PLATFORM_MAJOR) && (ZBOSS_PLATFORM_MAJOR < 5U) */
static zb_uint16_t ncp_vendor_specific_req_handler(zb_uint8_t buf)
#endif /* (defined ZBOSS_PLATFORM_MAJOR) && (ZBOSS_PLATFORM_MAJOR < 5U) */
{
	/* request tsn */
	zb_uint8_t tsn = *ZB_BUF_GET_PARAM(buf, zb_uint8_t);
	/* actual payload passed by the request */
	zb_uint8_t *led_action = (zb_uint8_t *)zb_buf_begin(buf);

	zb_uint8_t resp_buf = zb_buf_get(ZB_FALSE, VENDOR_SPECIFIC_RESPONSE_LEN);
	zb_uint8_t *resp_data;
	ncp_hl_custom_resp_t *resp_args;

	if (resp_buf == ZB_BUF_INVALID) {
		LOG_ERR("Couldn't get buf");
		return RET_NO_MEMORY;
	}

	resp_data = zb_buf_initial_alloc(resp_buf, VENDOR_SPECIFIC_RESPONSE_LEN);
	resp_args = ZB_BUF_GET_PARAM(resp_buf, ncp_hl_custom_resp_t);

	if (zb_buf_len(buf) == VENDOR_SPECIFIC_REQUEST_LEN) {
		switch (*led_action) {
		case VENDOR_SPECIFIC_LED_ACTION_OFF:
			zb_osif_led_off(VENDOR_SPECIFIC_LED);
			resp_args->status = RET_OK;

			vendor_specific_led_state = 0;
			break;
		case VENDOR_SPECIFIC_LED_ACTION_ON:
			zb_osif_led_on(VENDOR_SPECIFIC_LED);
			resp_args->status = RET_OK;

			vendor_specific_led_state = 1;
			break;
		case VENDOR_SPECIFIC_LED_ACTION_TOGGLE:
			vendor_specific_led_state ^= 0x01;
			if (vendor_specific_led_state) {
				zb_osif_led_on(VENDOR_SPECIFIC_LED);
			} else {
				zb_osif_led_off(VENDOR_SPECIFIC_LED);
			}
			resp_args->status = RET_OK;

			break;
		default:
			resp_args->status = RET_ERROR;
			break;
		}
	} else {
		resp_args->status = RET_ERROR;
	}

	resp_args->tsn = tsn;
	*resp_data = vendor_specific_led_state;
	ZVUNUSED(zb_ncp_custom_response(resp_buf));

	return NCP_RET_LATER;
}

static void ncp_vendor_specific_init(void)
{
	zb_osif_led_button_init();

	zb_ncp_custom_register_request_cb(ncp_vendor_specific_req_handler);

	ZB_SCHEDULE_APP_ALARM(perform_custom_indication, (zb_uint8_t)VENDOR_SPECIFIC_LED,
						  VENDOR_SPECIFIC_IND_DELAY);
}

void zb_dbg0_raise(zb_uint8_t s_l, zb_uint32_t err_id)
{
    LOG_ERR("ZBOSS ERR Raise err_id %d severity %d", err_id, (zb_uint32_t)s_l);
}

void zb_dbg0_abort(char * f, int line_nm)
{
    LOG_ERR("ZBOSS Abort line %d file %s", line_nm, f);
}

void zb_dbg0_assert1t(const char * f, zb_int_t line_nm)
{
    LOG_ERR("ZBOSS Assert line %d file %s", line_nm, f);
}

zb_uint16_t volatile ncp_DBG_cmd_stop_seq[4];
zb_uint32_t volatile ncp_DBG_cmd_startd_tsn;
zb_uint32_t volatile ncp_DBG_cmd_startd_callid;
zb_uint32_t volatile ncp_DBG_cmd_address_pib;
zb_uint32_t volatile ncp_DBG_cmd_address_req;

zb_uint32_t volatile ncp_DBG_cmd_permit_joing_bufid;
zb_uint32_t volatile ncp_DBG_cmd_permit_joing_cb;
zb_uint16_t volatile ncp_DBG_cmd_permit_joing_is_joind;

zb_uint16_t volatile ncp_DBG_cmd_permit_sch_id[2];
zb_uint32_t volatile ncp_DBG_cmd_permit_joing_tsn[2];

void ncp_joining_DBG_req_cli_0c(zb_uint32_t pj_bufid, const void * pj_cb, zb_bool_t is_joined)
{
    ncp_DBG_cmd_permit_joing_bufid      = pj_bufid;
    ncp_DBG_cmd_permit_joing_cb         = (zb_uint32_t)pj_cb;
    ncp_DBG_cmd_permit_joing_is_joind   = (is_joined ? 1 : 0);
}

void ncp_joining_DBG_req_cli_1c(zb_uint8_t schedule_id, zb_uint32_t req_tsn)
{
  zb_uint8_t n;
  for (n = 0; n < 2; n ++)
  {
    if (ncp_DBG_cmd_permit_sch_id[n] == 0xFFFFuL)
    {
        ncp_DBG_cmd_permit_sch_id[n]    = schedule_id;
        ncp_DBG_cmd_permit_joing_tsn[n] = req_tsn;
        break;
    }
  }
}

void zb_dbg0_assert2b(zb_uint16_t fi_id, zb_int_t line_nm)
{
    zb_uint32_t v32a, v32b;
    zb_uint16_t n;

    v32a = ncp_DBG_cmd_startd_tsn;
    v32b = ncp_DBG_cmd_startd_callid;
    if (v32a != 0xFFFFFFFFuL)
    {
         LOG_ERR("ncp_DBG started tsn %d call_id %d", v32a, v32b);
    }

    v32a = ncp_DBG_cmd_address_pib;
    v32b = ncp_DBG_cmd_address_req;
    if (v32a != 0xFFFFFFFFuL)
    {
         LOG_ERR("ncp_DBG req_addr pib %d req %d", v32a, v32b);
    }

    v32a = ncp_DBG_cmd_permit_joing_bufid;
    v32b = ncp_DBG_cmd_permit_joing_cb;
    n    = ncp_DBG_cmd_permit_joing_is_joind;
    if (v32a != 0xFFFFFFFFuL)
    {
         LOG_ERR("ncp_DBG permit_joing bufid %d cb %d is_joined %d", v32a, v32b, n);
    }

    for (n = 0; n < 4; n ++)
    {
      zb_uint16_t cmd_stop_id = ncp_DBG_cmd_stop_seq[n];
      if (cmd_stop_id != 0xFFFFuL)
      {
        LOG_ERR("ncp_DBG stop[%d] : %d", n,  cmd_stop_id);
      }
    }

    LOG_ERR("ZBOSS Assert file_id %d line %d", fi_id, line_nm);
}

void zb_dbg0_verify(zb_uint16_t fi_id, zb_int_t line_nm, zb_uint32_t err_id)
{
    LOG_ERR("ZBOSS Verify fail file_id %d line %d err %d", fi_id, line_nm, err_id);
}

zb_uint32_t volatile ncp_DBG_fill_resp_hdr_tsn; // init 0xFFFFFFFFuL
zb_ret_t    volatile ncp_DBG_fill_resp_hdr_st;
zb_uint_t   volatile ncp_DBG_fill_resp_hdr_siz;

const char * volatile ncp_DBG_register_req_txt;
zb_uint32_t volatile  ncp_DBG_register_req_tsn;  // init 0xFFFFFFFFuL

zb_uint32_t volatile  ncp_DBG_send_packet_fail;  // init 0xFFFFFFFFuL

void ncp_joining_DBG_send_packet_failed(zb_uint16_t cId)
{
    ncp_DBG_send_packet_fail = cId;
}

zb_uint32_t volatile  ncp_DBG_mode_nondef_id;  // init 0xFFFFFFFFuL
zb_uint16_t volatile  ncp_DBG_mode_nondef_cat;
zb_uint16_t volatile  ncp_DBG_mode_nondef_len;

void ncp_joining_DBG_mode_nondef(zb_uint16_t ctx_mode, zb_uint16_t call_cat, zb_uint16_t lengt)
{
   ncp_DBG_mode_nondef_id   = ctx_mode;
   ncp_DBG_mode_nondef_cat  = call_cat;
   ncp_DBG_mode_nondef_len  = lengt;
}

static void ncp_joining_DBG_mode_nondf_show(void)
{
    if (ncp_DBG_mode_nondef_id != 0xFFFFFFFFuL)
    {
       zb_uint32_t fail_id = ncp_DBG_mode_nondef_id;
       zb_uint16_t cat = ncp_DBG_mode_nondef_cat;
       zb_uint16_t len = ncp_DBG_mode_nondef_len;
       LOG_INF("ncp_DBG mode_nondef - id %d cat %d len %d", fail_id, cat, len);
       ncp_DBG_mode_nondef_id = 0xFFFFFFFFuL;
    }
}

zb_uint32_t volatile  ncp_DBG_illeg_req_pkttype;  // init 0xFFFFFFFFuL
zb_uint16_t volatile  ncp_DBG_illeg_req_cat;
zb_uint16_t volatile  ncp_DBG_illeg_req_len;

void ncp_joining_DBG_illeg_req(zb_uint16_t pkt_typ, zb_uint16_t call_cat, zb_uint16_t lengt)
{
   ncp_DBG_illeg_req_pkttype = pkt_typ;
   ncp_DBG_illeg_req_cat = call_cat;
   ncp_DBG_illeg_req_len = lengt;
}

static void ncp_joining_DBG_illegal_req_show(void)
{
    if (ncp_DBG_illeg_req_pkttype != 0xFFFFFFFFuL)
    {
       zb_uint32_t pkt_type = ncp_DBG_illeg_req_pkttype;
       zb_uint16_t cat = ncp_DBG_illeg_req_cat;
       zb_uint16_t len = ncp_DBG_illeg_req_len;
       LOG_INF("ncp_DBG illegal_req - pkt_type %d cat %d len %d", pkt_type, cat, len);
       ncp_DBG_illeg_req_pkttype = 0xFFFFFFFFuL;
    }
}

const void * volatile  ncp_DBG_send_pkt_data;  // init 0
zb_uint16_t volatile   ncp_DBG_send_pkt_len;

void ncp_joining_DBG_send_pkt(const void *p_data, zb_uint16_t tx_lengt)
{
    ncp_DBG_send_pkt_data = p_data;
    ncp_DBG_send_pkt_len  = tx_lengt;
}

static void ncp_joining_DBG_send_packet_show(void)
{
    static char sbuf[256];

    if (ncp_DBG_send_pkt_data != (const void *)0)
    {
       zb_uint16_t len = ncp_DBG_mode_nondef_len;
       unsigned char const * const p_d =
         (unsigned char const *)ncp_DBG_send_pkt_data;

       char * p_b = &(sbuf[0]);
       for (uint8_t j = 0; j <= len; j++)
       {
           int ch_added = sprintf(p_b, "%02x ", ((int)(p_d[j])));
           p_b += ch_added;
           if (p_b > &(sbuf[sizeof(sbuf)-4]))
           {
             (*p_b) = 0;
             break;
           }
       }
       LOG_INF("ncp_DBG send_packet - len %d d:%s", len, sbuf);
       ncp_DBG_send_pkt_data = (const void *)0;
    }
}

zb_uint32_t volatile  ncp_DBG_send_later_len;  // init 0xFFFFFFFFuL

void ncp_joining_DBG_send_later(zb_uint16_t lengt)
{
    ncp_DBG_send_later_len = lengt;
}

static void ncp_joining_DBG_send_later_show(void)
{
    if (ncp_DBG_send_later_len != 0xFFFFFFFFuL)
    {
       zb_uint16_t len = ncp_DBG_send_later_len;
       LOG_INF("ncp_DBG send_later - len %d", len);
       ncp_DBG_send_later_len = 0xFFFFFFFFuL;
    }
}

void ncp_joining_DBG_started(zb_uint32_t hdr_tsn, zb_uint32_t hdr_call_id)
{
    ncp_DBG_cmd_startd_tsn     = hdr_tsn;
    ncp_DBG_cmd_startd_callid  = hdr_call_id;
}

void ncp_joining_DBG_short_ad(zb_uint32_t addr_pib, zb_uint32_t addr_req)
{
   ncp_DBG_cmd_address_pib = addr_pib;
   ncp_DBG_cmd_address_req = addr_req;
}

zb_uint32_t volatile ncp_DBG_cmd_stopped;  // init 0xFFFFFFFFuL

zb_uint8_t ncp_joining_DBG_stopped(zb_uint8_t op_id)
{
  zb_uint8_t n;
  for (n = 0; n < 4; n ++)
  {
    if (ncp_DBG_cmd_stop_seq[n] == 0xFFFFuL)
    {
        ncp_DBG_cmd_stop_seq[n] = op_id;
        break;
    }
  }

  if (ncp_DBG_cmd_stopped == 0xFFFFFFFFuL)
  {
     ncp_DBG_cmd_stopped = op_id;
  }
  return 0;
}



int main(void)
{
	LOG_INF("Starting Zigbee R23 Network Co-processor sample");

    ncp_DBG_fill_resp_hdr_tsn = 0xFFFFFFFFuL;
    ncp_DBG_register_req_tsn  = 0xFFFFFFFFuL;
    ncp_DBG_send_packet_fail  = 0xFFFFFFFFuL;
    ncp_DBG_send_later_len    = 0xFFFFFFFFuL;
    ncp_DBG_mode_nondef_id    = 0xFFFFFFFFuL;
    ncp_DBG_illeg_req_pkttype = 0xFFFFFFFFuL;
    ncp_DBG_send_pkt_data     = (const void *)0;
    ncp_DBG_cmd_stopped = 0xFFFFFFFFuL;
    ncp_DBG_cmd_stop_seq[0] = 0xFFFF;
    ncp_DBG_cmd_stop_seq[1] = 0xFFFF;
    ncp_DBG_cmd_stop_seq[2] = 0xFFFF;
    ncp_DBG_cmd_stop_seq[3] = 0xFFFF;
    ncp_DBG_cmd_startd_tsn     = 0xFFFFFFFFuL;
    ncp_DBG_cmd_startd_callid  = 0xFFFFFFFFuL;
    ncp_DBG_cmd_address_pib    = 0xFFFFFFFFuL;
    ncp_DBG_cmd_address_req    = 0xFFFFFFFFuL;
    ncp_DBG_cmd_permit_joing_bufid    = 0xFFFFFFFFuL;
    ncp_DBG_cmd_permit_joing_cb       = 0xFFFFFFFFuL;
    ncp_DBG_cmd_permit_joing_is_joind = 0xFFFFu;
    ncp_DBG_cmd_permit_sch_id[0] = 0xFFFFu;
    ncp_DBG_cmd_permit_joing_tsn[0] = 0xFFFFFFFFuL;
    ncp_DBG_cmd_permit_sch_id[1] = 0xFFFFu;
    ncp_DBG_cmd_permit_joing_tsn[1] = 0xFFFFFFFFuL;

#ifdef CONFIG_USB_DEVICE_STACK
	/* Enable USB device. */
	int ret = usb_enable(NULL);

	if ((ret != 0) && (ret != -EALREADY)) {
		LOG_ERR("USB initialization failed");
		return ret;
	}

	/* Configure line control if flow control supported by Zigbee Async serial. */
	if (IS_ENABLED(CONFIG_ZIGBEE_UART_SUPPORTS_FLOW_CONTROL)) {
		const struct device *uart_dev = DEVICE_DT_GET(DT_CHOSEN(ncs_zigbee_uart));
		uint32_t dtr = 0U;

		if (!device_is_ready(uart_dev)) {
			LOG_ERR("UART device not ready");
			return -ENODEV;
		}

		while (true) {
			/* Break loop if line control can't be retrieved. */
			if (uart_line_ctrl_get(uart_dev, UART_LINE_CTRL_DTR, &dtr)) {
				LOG_ERR("Couldn't get DTR signal during NCP serial initialization");
				break;
			}
			if (dtr) {
				break;
			}
			/* Give CPU resources to low priority threads. */
			k_sleep(K_MSEC(100));
		}

		/* Data Carrier Detect Modem - mark connection as established. */
		(void)uart_line_ctrl_set(uart_dev, UART_LINE_CTRL_DCD, 1);
		/* Data Set Ready - the NCP SoC is ready to communicate. */
		(void)uart_line_ctrl_set(uart_dev, UART_LINE_CTRL_DSR, 1);
	}

	/* Wait 1 sec for the host to do all settings */
	k_sleep(K_SECONDS(1));
#endif /* CONFIG_USB_DEVICE_STACK */

	zb_osif_ncp_set_nvram_filter();

	/* Setup ncp custom command handling */
	ncp_vendor_specific_init();
	
	LOG_INF("ncp_DBG extra logs 08v enabled");

	/* Start Zigbee default thread */
	zigbee_enable();

	LOG_INF("Zigbee R23 Network Co-processor sample started");

	while (1) {
        k_sleep(K_MSEC(200));

        if (ncp_DBG_fill_resp_hdr_tsn != 0xFFFFFFFFuL)
        {
           zb_uint32_t tsn = ncp_DBG_fill_resp_hdr_tsn;
           zb_uint32_t body_size = ncp_DBG_fill_resp_hdr_siz;
           zb_uint32_t sta = ncp_DBG_fill_resp_hdr_st;
           LOG_INF("ncp_DBG fill_resp_hdr - tsn %d status %d b_size %d", tsn, body_size, sta);
           ncp_DBG_fill_resp_hdr_tsn = 0xFFFFFFFFuL;
        }

        if (ncp_DBG_register_req_tsn != 0xFFFFFFFFuL)
        {
           zb_uint32_t tsn = ncp_DBG_register_req_tsn;
           const char * info_txt = ncp_DBG_register_req_txt;
           LOG_INF("ncp_DBG register_request - tsn %d info %s", tsn, info_txt);
           ncp_DBG_register_req_tsn = 0xFFFFFFFFuL;
        }

        if (ncp_DBG_send_packet_fail != 0xFFFFFFFFuL)
        {
           zb_uint32_t fail_id = ncp_DBG_send_packet_fail;
           LOG_INF("ncp_DBG send_packet_fail - id %d", fail_id);
           ncp_DBG_send_packet_fail = 0xFFFFFFFFuL;
        }

        if (ncp_DBG_cmd_stopped != 0xFFFFFFFFuL)
        {
           zb_uint32_t stop_id = ncp_DBG_cmd_stopped;
           LOG_INF("ncp_DBG stop_id  %d", stop_id);
           ncp_DBG_cmd_stopped = 0xFFFFFFFFuL;
        }

        ncp_joining_DBG_mode_nondf_show();
        ncp_joining_DBG_illegal_req_show();
        ncp_joining_DBG_send_packet_show();
        ncp_joining_DBG_send_later_show();
	}

	return 0;
}

void ncp_joining_DBG_fill_resp_hdr(zb_uint8_t tsnv, zb_ret_t st, zb_uint_t body_siz)
{
    ncp_DBG_fill_resp_hdr_st    = st;
    ncp_DBG_fill_resp_hdr_siz   = body_siz;
    ncp_DBG_fill_resp_hdr_tsn   = tsnv; // init 0xFFFFFFFFuL
}

void ncp_joining_DBG_register_request(zb_uint8_t tsnv, const char * info_txt)
{
    ncp_DBG_register_req_txt = info_txt;
    ncp_DBG_register_req_tsn = tsnv;  // init 0xFFFFFFFFuL
}
