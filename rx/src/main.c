/*
 * Guitar Controller RX — BLE Central + USB HID Gamepad
 *
 * Uses NCS bt_scan module for LLPM-aware scanning/connecting,
 * bt_gatt_dm for GATT discovery, and USBD next stack for
 * composite CDC ACM + HID.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_hid.h>
#include <bluetooth/scan.h>
#include <bluetooth/gatt_dm.h>
#include <bluetooth/hci_vs_sdc.h>
#include <sdc_hci_vs.h>

/* ── UUIDs ────────────────────────────────────────────────────── */

static struct bt_uuid_128 guitar_svc_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xe0a00001, 0x5e00, 0x4d74, 0x9b80, 0x5b1a3e6a0a1e));

static struct bt_uuid_128 guitar_btn_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xe0a00002, 0x5e00, 0x4d74, 0x9b80, 0x5b1a3e6a0a1e));

/* ── LLPM ─────────────────────────────────────────────────────── */

#define INTERVAL_LLPM 0x0D01

static struct bt_le_conn_param *llpm_conn_param =
	BT_LE_CONN_PARAM(INTERVAL_LLPM, INTERVAL_LLPM, 0, 400);

/* ── USB HID Report Descriptor (Santroller GH Guitar) ─────────── */

static const uint8_t hid_report_desc[] = {
	0x05, 0x01,       /* Usage Page (Generic Desktop)       */
	0x09, 0x05,       /* Usage (Game Pad)                   */
	0xA1, 0x01,       /* Collection (Application)           */
	0x85, 0x01,       /*   Report ID (1)                    */
	0x05, 0x09,       /*   Usage Page (Button)              */
	0x19, 0x01,       /*   Usage Minimum (Button 1)         */
	0x29, 0x0D,       /*   Usage Maximum (Button 13)        */
	0x15, 0x00,       /*   Logical Minimum (0)              */
	0x25, 0x01,       /*   Logical Maximum (1)              */
	0x75, 0x01,       /*   Report Size (1)                  */
	0x95, 0x0D,       /*   Report Count (13)                */
	0x81, 0x02,       /*   Input (Data, Variable, Absolute) */
	0x75, 0x01,       /*   Report Size (1)                  */
	0x95, 0x03,       /*   Report Count (3)                 */
	0x81, 0x01,       /*   Input (Constant)                 */
	0x05, 0x01,       /*   Usage Page (Generic Desktop)     */
	0x09, 0x39,       /*   Usage (Hat Switch)               */
	0x15, 0x00,       /*   Logical Minimum (0)              */
	0x25, 0x07,       /*   Logical Maximum (7)              */
	0x35, 0x00,       /*   Physical Minimum (0)             */
	0x46, 0x3B, 0x01, /*   Physical Maximum (315)           */
	0x65, 0x14,       /*   Unit (Degrees)                   */
	0x75, 0x04,       /*   Report Size (4)                  */
	0x95, 0x01,       /*   Report Count (1)                 */
	0x81, 0x42,       /*   Input (Data, Variable, Null)     */
	0x75, 0x04,       /*   Report Size (4)                  */
	0x95, 0x01,       /*   Report Count (1)                 */
	0x81, 0x01,       /*   Input (Constant)                 */
	0x05, 0x01,       /*   Usage Page (Generic Desktop)     */
	0x15, 0x00,       /*   Logical Minimum (0)              */
	0x26, 0xFF, 0x00, /*   Logical Maximum (255)            */
	0x09, 0x30,       /*   Usage (X) - whammy               */
	0x09, 0x31,       /*   Usage (Y) - slider               */
	0x09, 0x32,       /*   Usage (Z) - tilt                 */
	0x75, 0x08,       /*   Report Size (8)                  */
	0x95, 0x03,       /*   Report Count (3)                 */
	0x81, 0x02,       /*   Input (Data, Variable, Absolute) */
	0xC0,             /* End Collection                     */
};

/* ── USBD (CDC ACM + HID composite) ──────────────────────────── */

USBD_DEVICE_DEFINE(guitar_usbd,
		   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
		   0x1209, 0x2882);

USBD_DESC_LANG_DEFINE(guitar_lang);
USBD_DESC_MANUFACTURER_DEFINE(guitar_mfr, "sanjay900");
USBD_DESC_PRODUCT_DEFINE(guitar_product, "Santroller");
USBD_DESC_CONFIG_DEFINE(guitar_fs_cfg, "FS Configuration");

USBD_CONFIGURATION_DEFINE(guitar_fs_config, 0, 100, &guitar_fs_cfg);

static int usbd_guitar_init(void)
{
	int err;

	err = usbd_add_descriptor(&guitar_usbd, &guitar_lang);
	if (err) return err;
	err = usbd_add_descriptor(&guitar_usbd, &guitar_mfr);
	if (err) return err;
	err = usbd_add_descriptor(&guitar_usbd, &guitar_product);
	if (err) return err;

	err = usbd_add_configuration(&guitar_usbd, USBD_SPEED_FS,
				     &guitar_fs_config);
	if (err) return err;

	err = usbd_register_all_classes(&guitar_usbd, USBD_SPEED_FS, 1, NULL);
	if (err) return err;

	usbd_device_set_code_triple(&guitar_usbd, USBD_SPEED_FS,
				    USB_BCC_MISCELLANEOUS, 0x02, 0x01);
	usbd_device_set_bcd_device(&guitar_usbd, 0x0300);

	err = usbd_init(&guitar_usbd);
	if (err) return err;

	return usbd_enable(&guitar_usbd);
}

/* ── USB HID ──────────────────────────────────────────────────── */

static const struct device *hid_dev;
static uint16_t last_button_state;
static uint8_t whammy_toggle;
static struct k_mutex report_mutex;

static void submit_hid_report(void)
{
	k_mutex_lock(&report_mutex, K_FOREVER);

	uint16_t s = last_button_state;

	static uint8_t report[7] __aligned(4);
	report[0] = 0x01;

	report[1] = (s & 0x1F)
		  | (((s >> 9) & 1) << 6)
		  | (((s >> 8) & 1) << 7);

	report[2] = (s >> 10) & 1;

	bool strum_up = (s >> 5) & 1;
	bool strum_down = (s >> 6) & 1;
	if (strum_up && !strum_down) report[3] = 0;
	else if (strum_down && !strum_up) report[3] = 4;
	else report[3] = 8;

	whammy_toggle ^= 1;
	report[4] = whammy_toggle ? 0xFF : 0x80;
	report[5] = 0x00;
	report[6] = ((s >> 7) & 1) ? 0xFF : 0x80;

	hid_device_submit_report(hid_dev, sizeof(report), report);

	k_mutex_unlock(&report_mutex);
}

/* Whammy oscillation timer — sends report every 100ms */
static void whammy_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(whammy_work, whammy_handler);

static void whammy_handler(struct k_work *work)
{
	submit_hid_report();
	k_work_schedule(&whammy_work, K_MSEC(100));
}

static void send_hid_report(const uint8_t *data, uint16_t len)
{
	k_mutex_lock(&report_mutex, K_FOREVER);
	if (len >= 2) last_button_state = data[0] | (data[1] << 8);
	else if (len >= 1) last_button_state = data[0];
	k_mutex_unlock(&report_mutex);

	submit_hid_report();
}

static void hid_iface_ready(const struct device *dev, const bool ready)
{
	printk("HID iface %s\n", ready ? "ready" : "not ready");
}

static int hid_get_report(const struct device *dev,
			  const uint8_t type, const uint8_t id,
			  const uint16_t len, uint8_t *const buf)
{ return 0; }

static int hid_set_report(const struct device *dev,
			  const uint8_t type, const uint8_t id,
			  const uint16_t len, const uint8_t *const buf)
{ return 0; }

static void hid_input_report_done(const struct device *dev)
{
	/* Async completion — nothing to do */
}

static const struct hid_device_ops hid_ops = {
	.iface_ready = hid_iface_ready,
	.get_report = hid_get_report,
	.set_report = hid_set_report,
	.input_report_done = hid_input_report_done,
};

/* ── LED ──────────────────────────────────────────────────────── */

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static void led_blink_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(led_blink_work, led_blink_handler);

static void led_blink_handler(struct k_work *work)
{
	gpio_pin_toggle_dt(&led);
	k_work_schedule(&led_blink_work, K_MSEC(500));
}

static void led_set_connected(bool connected)
{
	k_work_cancel_delayable(&led_blink_work);
	if (connected) gpio_pin_set_dt(&led, 1);
	else k_work_schedule(&led_blink_work, K_NO_WAIT);
}

/* ── BLE State ────────────────────────────────────────────────── */

static struct bt_conn *active_conn;
static struct bt_gatt_subscribe_params subscribe_params;

/* ── BLE Notification Handler ─────────────────────────────────── */

static uint8_t on_notify(struct bt_conn *conn,
			 struct bt_gatt_subscribe_params *params,
			 const void *data, uint16_t length)
{
	if (!data) {
		printk("Unsubscribed\n");
		params->value_handle = 0U;
		return BT_GATT_ITER_STOP;
	}

	if (length >= 1) {
		send_hid_report(data, length);
	}

	return BT_GATT_ITER_CONTINUE;
}

/* ── GATT Discovery (bt_gatt_dm) ─────────────────────────────── */

static void discovery_completed(struct bt_gatt_dm *dm, void *ctx)
{
	const struct bt_gatt_dm_attr *gatt_char;

	gatt_char = bt_gatt_dm_char_by_uuid(dm, &guitar_btn_uuid.uuid);
	if (!gatt_char) {
		printk("Button char not found\n");
		bt_gatt_dm_data_release(dm);
		return;
	}

	const struct bt_gatt_dm_attr *gatt_ccc;
	gatt_ccc = bt_gatt_dm_desc_by_uuid(dm, gatt_char,
					    BT_UUID_GATT_CCC);

	subscribe_params.notify = on_notify;
	subscribe_params.value = BT_GATT_CCC_NOTIFY;
	subscribe_params.value_handle = gatt_char->handle + 1;
	subscribe_params.ccc_handle = gatt_ccc ? gatt_ccc->handle :
					gatt_char->handle + 2;

	int err = bt_gatt_subscribe(active_conn, &subscribe_params);
	printk("Subscribe: %d\n", err);

	bt_gatt_dm_data_release(dm);
}

static void discovery_not_found(struct bt_conn *conn, void *ctx)
{
	printk("Service not found\n");
}

static void discovery_error(struct bt_conn *conn, int err, void *ctx)
{
	printk("Discovery error: %d\n", err);
}

static struct bt_gatt_dm_cb discovery_cb = {
	.completed = discovery_completed,
	.service_not_found = discovery_not_found,
	.error_found = discovery_error,
};

/* ── Scan (bt_scan module) ────────────────────────────────────── */

static void scan_start(void)
{
	int err = bt_scan_start(BT_SCAN_TYPE_SCAN_PASSIVE);
	if (err) {
		printk("Scan start: %d\n", err);
	}
}

static void scan_filter_match(struct bt_scan_device_info *dev_info,
			      struct bt_scan_filter_match *filter_match,
			      bool connectable)
{
	printk("TX found\n");
}

static void scan_connecting_error(struct bt_scan_device_info *dev_info)
{
	printk("Connect error, rescanning...\n");
	scan_start();
}

BT_SCAN_CB_INIT(scan_cb, scan_filter_match, NULL,
		scan_connecting_error, NULL);

static void scan_init(void)
{
	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_PASSIVE,
		.options = BT_LE_SCAN_OPT_FILTER_DUPLICATE,
		.interval = 0x0010,
		.window = 0x0010,
	};

	struct bt_scan_init_param scan_init_param = {
		.connect_if_match = true,
		.scan_param = &scan_param,
		.conn_param = llpm_conn_param,
	};

	bt_scan_init(&scan_init_param);
	bt_scan_cb_register(&scan_cb);

	int err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_UUID,
				     &guitar_svc_uuid.uuid);
	if (err) {
		printk("Scan filter add: %d\n", err);
		return;
	}

	err = bt_scan_filter_enable(BT_SCAN_UUID_FILTER, false);
	if (err) {
		printk("Scan filter enable: %d\n", err);
	}
}

/* ── BLE Callbacks ────────────────────────────────────────────── */

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Conn failed: %u\n", err);
		scan_start();
		return;
	}

	active_conn = bt_conn_ref(conn);
	led_set_connected(true);
	k_work_schedule(&whammy_work, K_MSEC(100));
	printk("Connected!\n");

	err = bt_gatt_dm_start(conn, &guitar_svc_uuid.uuid,
			       &discovery_cb, NULL);
	if (err) {
		printk("Discovery start: %d\n", err);
	}
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	led_set_connected(false);
	k_work_cancel_delayable(&whammy_work);
	printk("Disconnected: %u\n", reason);

	if (active_conn) {
		bt_conn_unref(active_conn);
		active_conn = NULL;
	}

	scan_start();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = on_connected,
	.disconnected = on_disconnected,
};

/* ── Main ─────────────────────────────────────────────────────── */

int main(void)
{
	int err;

	k_mutex_init(&report_mutex);

	/* Init LED */
	if (gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
		led_set_connected(false);
	}

	/* Register HID */
	hid_dev = DEVICE_DT_GET(DT_NODELABEL(hid_dev_0));
	if (device_is_ready(hid_dev)) {
		hid_device_register(hid_dev, hid_report_desc,
				    sizeof(hid_report_desc), &hid_ops);
	}

	/* Init USB */
	err = usbd_guitar_init();

	printk("\n=== Guitar RX ===\n");
	printk("USB: %d  HID: %s\n", err, device_is_ready(hid_dev) ? "OK" : "FAIL");

	/* Init Bluetooth + LLPM */
	err = bt_enable(NULL);
	if (err) {
		printk("BT: %d\n", err);
		return err;
	}

	sdc_hci_cmd_vs_llpm_mode_set_t llpm_cmd = { .enable = true };
	err = hci_vs_sdc_llpm_mode_set(&llpm_cmd);
	printk("BT OK, LLPM: %d\n", err);

	/* Init scan and start */
	scan_init();
	scan_start();
	printk("Scanning...\n");

	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
