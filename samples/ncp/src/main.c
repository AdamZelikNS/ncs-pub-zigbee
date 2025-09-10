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

struct ncp_DBG_event_s;

typedef void (*ncp_DBG_ev_pr_cb_t)(const struct ncp_DBG_event_s * e, zb_uint16_t ev_num);

typedef struct ncp_DBG_event_s {
	zb_uint16_t volatile   st;
    ncp_DBG_ev_pr_cb_t     pr_cb;
    zb_uint32_t            pz[10];
} ncp_DBG_event_t;

#define ncp_DBG_ev_NUM (10 * 8)
ncp_DBG_event_t ncp_DBG_events[ncp_DBG_ev_NUM];
ncp_DBG_event_t * volatile ncp_DBG_last;


static void ncp_joining_DBG_mode_nondf_pshow(const struct ncp_DBG_event_s * e, zb_uint16_t ev_num);
static void ncp_joining_DBG_cmd_started_pshow(const struct ncp_DBG_event_s * e, zb_uint16_t ev_num);
static void ncp_joining_DBG_permit_joing_buf_pshow(const struct ncp_DBG_event_s * e, zb_uint16_t ev_num);
static void ncp_joining_DBG_permit_joing_sch_pshow(const struct ncp_DBG_event_s * e, zb_uint16_t ev_num);
static void ncp_joining_DBG_illegal_req_pshow(const struct ncp_DBG_event_s * e, zb_uint16_t ev_num);
static void ncp_joining_DBG_send_packet_pshow(const struct ncp_DBG_event_s * e, zb_uint16_t ev_num);
static void ncp_joining_DBG_send_later_pshow(const struct ncp_DBG_event_s * e, zb_uint16_t ev_num);
static void ncp_joining_DBG_fill_register_req_pshow(const struct ncp_DBG_event_s * e, zb_uint16_t ev_num);
static void ncp_joining_DBG_fill_resp_hdr_pshow(const struct ncp_DBG_event_s * e, zb_uint16_t ev_num);
static void ncp_joining_DBG_send_packet_fail_pshow(const struct ncp_DBG_event_s * e, zb_uint16_t ev_num);
static void ncp_joining_DBG_cmd_stopped_pshow(const struct ncp_DBG_event_s * e, zb_uint16_t ev_num);

static void ncp_joining_DBG_show_start_stop(zb_uint8_t trigg_id);

zb_uint8_t volatile ncp_DBG_show_sch_done = 0;

#if !defined(NRF54L_SERIES)
extern uint32_t nrf_802154_hp_timer_current_time_get(void);
#else
static inline uint32_t nrf_802154_hp_timer_current_time_get(void) { return 0; }
#endif

static void ncp_DBG_show_sched(void)     
{
    if (ncp_DBG_show_sch_done == 0)
    {
    	ZB_SCHEDULE_APP_ALARM(ncp_joining_DBG_show_start_stop,
                              (zb_uint8_t)2,
    						  (ZB_TIME_ONE_SECOND * 2));
        ncp_DBG_show_sch_done = 1;
    }

//     extern void ncp_joining_DBG_assert_for_diag(int a_v);
//     ncp_joining_DBG_assert_for_diag(0);
//     ncp_DBG_cmd_start_stop_show = 0;
//     ncp_joining_DBG_show_start_stop(?);
//     ncp_DBG_cmd_start_stop_show = 0;
}

void ncp_joining_DBG_req_cli_0c(zb_uint32_t pj_bufid, const void * pj_cb, zb_bool_t is_joined)
{
    ncp_DBG_event_t volatile * e = (ncp_DBG_last ++);

    e->st = 2;
    e->pz[0] = pj_bufid;
    e->pz[1] = (zb_uint32_t)pj_cb;
    e->pz[2] = (is_joined ? ((zb_uint32_t)1) : (zb_uint32_t)0);
    e->pr_cb = ncp_joining_DBG_permit_joing_buf_pshow;
    e->st = 1;
}

static void ncp_joining_DBG_permit_joing_buf_pshow(const struct ncp_DBG_event_s * e, zb_uint16_t ev_num)
{
    zb_uint32_t v32a = (e->pz[0]);
    zb_uint32_t v32b = (e->pz[1]);
    zb_uint16_t is_jo = (e->pz[2]);
    LOG_ERR("ncp_DBG ev[%d] permit_joing bufid %d cb %d is_joined %d", ev_num, v32a, v32b, is_jo);
}

void ncp_joining_DBG_req_cli_1c(zb_uint8_t schedule_id, zb_uint32_t req_tsn)
{
    ncp_DBG_event_t volatile * e = (ncp_DBG_last ++);

    e->st = 2;
    e->pz[0] = schedule_id;
    e->pz[1] = req_tsn;
    e->pr_cb = ncp_joining_DBG_permit_joing_sch_pshow;
    e->st = 1;
}

static void ncp_joining_DBG_permit_joing_sch_pshow(const struct ncp_DBG_event_s * e, zb_uint16_t ev_num)
{
    zb_uint32_t v32a = (e->pz[0]);
    zb_uint32_t v32b = (e->pz[1]);

    LOG_ERR("ncp_DBG ev[%d] permit_joing schedule %d tsn %d", ev_num, v32a, v32b);
}

static void ncp_joining_DBG_cmd_started_pshow(const struct ncp_DBG_event_s * e, zb_uint16_t ev_num)
{
    zb_uint32_t v32a, v32b, rx_tm;
    v32a = (e->pz[0]);
    v32b = (e->pz[1]);
    rx_tm = (e->pz[5]);
    LOG_ERR("ncp_DBG ev[%d] started tsn %d call_id %d rxtm %d", ev_num, v32a, v32b, rx_tm);

    v32a = (e->pz[2]);
    v32b = (e->pz[3]);
    LOG_ERR("ncp_DBG ev[%d] req_addr pib %d req %d", ev_num, v32a, v32b);
}

static void ncp_joining_DBG_show_start_stop(zb_uint8_t trigg_id)
{
    ncp_DBG_event_t * p_ev = &(ncp_DBG_events[0]);
    zb_uint16_t n = 0;

    LOG_ERR("trigg %d  ncp_DBG events_list: ", (zb_uint16_t)trigg_id);

    do {
      zb_uint16_t ev_st = (p_ev->st);
      if (ev_st == 0)
      {
        break;
      }
      else if (ev_st == 1)
      {
        p_ev->pr_cb(p_ev, n);
      }
      p_ev ++;
      n ++;
    } while (n < ncp_DBG_ev_NUM);

    LOG_ERR(" : END_list");
}

void zb_dbg0_assert2b(zb_uint16_t fi_id, zb_int_t line_nm)
{
    ncp_joining_DBG_show_start_stop(1);
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
    ncp_DBG_event_t volatile * e = (ncp_DBG_last ++);

    e->st = 2;
    e->pz[0] = cId;
    e->pr_cb = ncp_joining_DBG_send_packet_fail_pshow;
    e->st = 1;

    ncp_DBG_send_packet_fail = cId;
}

zb_uint32_t volatile  ncp_DBG_mode_nondef_id;  // init 0xFFFFFFFFuL
zb_uint16_t volatile  ncp_DBG_mode_nondef_cat;
zb_uint16_t volatile  ncp_DBG_mode_nondef_len;

void ncp_joining_DBG_mode_nondef(zb_uint16_t ctx_mode, zb_uint16_t call_cat, zb_uint16_t lengt)
{
    ncp_DBG_event_t volatile * e = (ncp_DBG_last ++);

    e->st = 2;
    e->pz[0] = ctx_mode;
    e->pz[1] = call_cat;
    e->pz[2] = lengt;
    e->pr_cb = ncp_joining_DBG_mode_nondf_pshow;
    e->st = 1;

   ncp_DBG_mode_nondef_id   = ctx_mode;
   ncp_DBG_mode_nondef_cat  = call_cat;
   ncp_DBG_mode_nondef_len  = lengt;

   ncp_DBG_show_sched();
}

static void ncp_joining_DBG_mode_nondf_pshow(const struct ncp_DBG_event_s * e, zb_uint16_t ev_num)
{
   zb_uint32_t fail_id = (e->pz[0]);
   zb_uint16_t cat     = (e->pz[1]);
   zb_uint16_t len     = (e->pz[2]);
   LOG_INF("ncp_DBG ev[%d] mode_nondef - id %d cat %d len %d", ev_num, fail_id, cat, len);
}

zb_uint32_t volatile  ncp_DBG_illeg_req_pkttype;  // init 0xFFFFFFFFuL
zb_uint16_t volatile  ncp_DBG_illeg_req_cat;
zb_uint16_t volatile  ncp_DBG_illeg_req_len;

void ncp_joining_DBG_illeg_req(zb_uint16_t pkt_typ, zb_uint16_t call_cat, zb_uint16_t lengt)
{
    ncp_DBG_event_t volatile * e = (ncp_DBG_last ++);

    e->st = 2;
    e->pz[0] = pkt_typ;
    e->pz[1] = call_cat;
    e->pz[2] = lengt;
    e->pr_cb = ncp_joining_DBG_illegal_req_pshow;
    e->st = 1;

   ncp_DBG_illeg_req_pkttype = pkt_typ;
   ncp_DBG_illeg_req_cat = call_cat;
   ncp_DBG_illeg_req_len = lengt;

   ncp_DBG_show_sched();
}

static void ncp_joining_DBG_illegal_req_pshow(const struct ncp_DBG_event_s * e, zb_uint16_t ev_num)
{
   zb_uint32_t pkt_type = (e->pz[0]);
   zb_uint16_t cat      = (e->pz[1]);
   zb_uint16_t len      = (e->pz[2]);
   LOG_INF("ncp_DBG ev[%d] illegal_req - pkt_type %d cat %d len %d", ev_num, pkt_type, cat, len);
}

const void * volatile  ncp_DBG_send_pkt_data;  // init 0
zb_uint16_t volatile   ncp_DBG_send_pkt_len;

void ncp_joining_DBG_send_pkt(const void *p_data, zb_uint16_t tx_lengt)
{
    ncp_DBG_event_t volatile * e = (ncp_DBG_last ++);

    e->st = 2;
    e->pz[0] = (zb_uint32_t)p_data;
    e->pz[1] = tx_lengt;
    e->pr_cb = ncp_joining_DBG_send_packet_pshow;
    e->st = 1;

    ncp_DBG_send_pkt_data = p_data;
    ncp_DBG_send_pkt_len  = tx_lengt;

    ncp_DBG_show_sched();
}

static void ncp_joining_DBG_send_packet_pshow(const struct ncp_DBG_event_s * e, zb_uint16_t ev_num)
{
    zb_uint32_t sbuf = (e->pz[0]);
    zb_uint16_t len = (e->pz[1]);

    LOG_INF("ncp_DBG ev[%d] send_packet - len %d d:%08x", ev_num, len, sbuf);
}

zb_uint32_t volatile  ncp_DBG_send_later_len;  // init 0xFFFFFFFFuL

void ncp_joining_DBG_send_later(zb_uint16_t lengt)
{
    ncp_DBG_event_t volatile * e = (ncp_DBG_last ++);

    e->st = 2;
    e->pz[0] = lengt;
    e->pr_cb = ncp_joining_DBG_send_later_pshow;
    e->st = 1;

    ncp_DBG_show_sched();
}

static void ncp_joining_DBG_send_later_pshow(const struct ncp_DBG_event_s * e, zb_uint16_t ev_num)
{
    zb_uint16_t len = (e->pz[0]);

    LOG_INF("ncp_DBG ev[%d] send_later - len %d", ev_num, len);
}

zb_uint8_t ncp_joining_DBG_started(zb_uint32_t hdr_tsn, zb_uint32_t hdr_call_id)
{
    ncp_DBG_event_t volatile * e = (ncp_DBG_last ++);

    e->st = 2;
    e->pz[0] = hdr_tsn;
    e->pz[1] = hdr_call_id;
    e->pz[2] = 0xFFFFFFFFuL;
    e->pz[3] = 0xFFFFFFFFuL;
    e->pz[5] = nrf_802154_hp_timer_current_time_get();
    e->pr_cb = ncp_joining_DBG_cmd_started_pshow;
    e->st = 1;

    return (e - &(ncp_DBG_events[0])); // . / sizeof(*e);;
}

void ncp_joining_DBG_short_ad(zb_uint8_t start_id, zb_uint32_t addr_pib, zb_uint32_t addr_req)
{
    if (start_id < ncp_DBG_ev_NUM)
    {
        ncp_DBG_event_t volatile * e = &(ncp_DBG_events[start_id]);
        e->pz[2] = addr_pib;
        e->pz[3] = addr_req;
    }
}

zb_uint32_t volatile ncp_DBG_cmd_stopped;  // init 0xFFFFFFFFuL

zb_uint8_t ncp_joining_DBG_stopped(zb_uint8_t op_id)
{
    ncp_DBG_event_t volatile * e = (ncp_DBG_last ++);

    e->st = 2;
    e->pz[0] = op_id;
    e->pz[1] = 0xFFFFFFFFuL;
    e->pz[5] = nrf_802154_hp_timer_current_time_get();
    e->pr_cb = ncp_joining_DBG_cmd_stopped_pshow;
    e->st = 1;

    if (ncp_DBG_cmd_stopped == 0xFFFFFFFFuL)
    {
       ncp_DBG_cmd_stopped = op_id;
    }

    ncp_DBG_show_sched();

    return (e - &(ncp_DBG_events[0])); // . / sizeof(*e);
}

void ncp_joining_DBG_stop_result(zb_uint8_t stop_id, zb_uint32_t stop_tsn)
{
    if (stop_id < ncp_DBG_ev_NUM)
    {
        ncp_DBG_event_t volatile * e = &(ncp_DBG_events[stop_id]);
        e->pz[1] = stop_tsn;

    }
}

static void ncp_joining_DBG_fill_resp_hdr_pshow(const struct ncp_DBG_event_s * e, zb_uint16_t ev_num)
{
   zb_uint32_t tsn       = (e->pz[0]);
   zb_uint32_t sta       = (e->pz[1]);
   zb_uint32_t body_size = (e->pz[2]);
   LOG_INF("ncp_DBG ev[%d] fill_resp_hdr - tsn %d status %d b_size %d", ev_num, tsn, sta, body_size);
}

static void ncp_joining_DBG_fill_register_req_pshow(const struct ncp_DBG_event_s * e, zb_uint16_t ev_num)
{
   const char * info_txt = (const char *)(e->pz[0]);
   zb_uint32_t tsn       = (e->pz[1]);
   LOG_INF("ncp_DBG ev[%d] register_request - tsn %d info %s", ev_num, tsn, info_txt);
}

static void ncp_joining_DBG_send_packet_fail_pshow(const struct ncp_DBG_event_s * e, zb_uint16_t ev_num)
{
   zb_uint32_t fail_id = (e->pz[0]);
   LOG_INF("ncp_DBG ev[%d] send_packet_fail - id %d", ev_num, fail_id);
}

static void ncp_joining_DBG_cmd_stopped_pshow(const struct ncp_DBG_event_s * e, zb_uint16_t ev_num)
{
   zb_uint32_t stop_id  = (e->pz[0]);
   zb_uint32_t stop_tsn = (e->pz[1]);
   zb_uint32_t done_tm  = (e->pz[5]);
   LOG_INF("ncp_DBG ev[%d] stop_id %d tsn %d done_time %d", ev_num, stop_id, stop_tsn, done_tm);
}

int main(void)
{
    int n_seq;
	LOG_INF("Starting Zigbee R23 Network Co-processor sample");

    ncp_DBG_fill_resp_hdr_tsn = 0xFFFFFFFFuL;
    ncp_DBG_register_req_tsn  = 0xFFFFFFFFuL;
    ncp_DBG_send_packet_fail  = 0xFFFFFFFFuL;
    ncp_DBG_send_later_len    = 0xFFFFFFFFuL;
    ncp_DBG_mode_nondef_id    = 0xFFFFFFFFuL;
    ncp_DBG_illeg_req_pkttype = 0xFFFFFFFFuL;
    ncp_DBG_send_pkt_data     = (const void *)0;
    ncp_DBG_cmd_stopped = 0xFFFFFFFFuL;
    for (n_seq = 0; n_seq < ncp_DBG_ev_NUM; n_seq ++)
    {
        ncp_DBG_events[n_seq].st = 0;
    }
    ncp_DBG_last = &(ncp_DBG_events[0]);

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
	
	LOG_INF("ncp_DBG extra logs 25v enabled");

	/* Start Zigbee default thread */
	zigbee_enable();

	LOG_INF("Zigbee R23 Network Co-processor sample started");

	while (1) {
        k_sleep(K_MSEC(200));
	}

	return 0;
}

void ncp_joining_DBG_fill_resp_hdr(zb_uint8_t tsnv, zb_ret_t st, zb_uint_t body_siz)
{
    ncp_DBG_event_t volatile * e = (ncp_DBG_last ++);

    e->st = 2;
    e->pz[0] = tsnv;
    e->pz[1] = st;
    e->pz[2] = body_siz;
    e->pr_cb = ncp_joining_DBG_fill_resp_hdr_pshow;
    e->st = 1;

    ncp_DBG_fill_resp_hdr_st    = st;
    ncp_DBG_fill_resp_hdr_siz   = body_siz;
    ncp_DBG_fill_resp_hdr_tsn   = tsnv; // init 0xFFFFFFFFuL
    
    ncp_DBG_show_sched();
}

void ncp_joining_DBG_register_request(zb_uint8_t tsnv, const char * info_txt)
{
    ncp_DBG_event_t volatile * e = (ncp_DBG_last ++);

    e->st = 2;
    e->pz[0] = (zb_uint32_t)info_txt;
    e->pz[1] = tsnv;
    e->pr_cb = ncp_joining_DBG_fill_register_req_pshow;
    e->st = 1;

    ncp_DBG_register_req_txt = info_txt;
    ncp_DBG_register_req_tsn = tsnv;  // init 0xFFFFFFFFuL
}
