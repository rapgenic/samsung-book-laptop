#include <linux/device.h>
#include <linux/acpi.h>
#include <linux/leds.h>
#include <linux/uuid.h>

#define SCAI_CSFI_LEN 0x15
#define SCAI_CSXI_LEN 0x100

#define SCAI_SAFN 0x5843

#define SCAI_SASB_KB_BACKLIGHT   0x78
#define SCAI_SASB_BATTERY_SAFE   0x7a
#define SCAI_SASB_USB_CHARGE     0x68
#define SCAI_SASB_NOTIFICATION   0x86
#define SCAI_SASB_WEBCAM_DISABLE 0x8a

#define SCAI_GUNM_SET 0x82
#define SCAI_GUNM_GET 0x81

struct scai_buffer {
	u16 safn;
	u16 sasb;
	u8 rflg;
	union {
		struct {
			u8 gunm;
			u8 guds[249];
		};
		struct {
			u8 caid[16];
			u8 fncn;
			u8 subn;
			u8 iob0;
			u8 iob1;
			u8 iob2;
			u8 iob3;
			u8 iob4;
			u8 iob5;
			u8 iob6;
			u8 iob7;
			u8 iob8;
			u8 iob9;
		};
	};
};

struct scai_data {
	struct acpi_device *acpi_dev;
	struct led_classdev kb_led;
};

static const guid_t SCAI_CAID_PERFORMANCE_GUID = GUID_INIT(0x8246028d, 0x8bca, 0x4a55, 0xba, 0x0f, 0x6f, 0x1e, 0x6b, 0x92, 0x1b, 0x8f);

static const struct acpi_device_id device_ids[] = {
	{"SAM0428", 0},
	{"", 0}
};
MODULE_DEVICE_TABLE(acpi, device_ids);

static int scai_command_integer(struct scai_data *data, acpi_string pathname, u64 arg, u64 *ret)
{
	union acpi_object int_obj, *ret_obj;
	struct acpi_object_list obj_list;
	struct acpi_buffer ret_buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	acpi_handle object;
	acpi_status status;

	obj_list.count = 1;
	obj_list.pointer = &int_obj;
	int_obj.type = ACPI_TYPE_INTEGER;
	int_obj.integer.value = arg;

	object = acpi_device_handle(data->acpi_dev);

	if (ret == NULL)
		status = acpi_evaluate_object(object, pathname, &obj_list, NULL);
	else
		status = acpi_evaluate_object(object, pathname, &obj_list, &ret_buffer);

	if (ACPI_SUCCESS(status)) {
		if (ret) {
			ret_obj = ret_buffer.pointer;

			if (ret_obj->type != ACPI_TYPE_INTEGER) {
				pr_err("SCAI response is not a simple integer\n");
				return -1;
			}

			*ret = ret_obj->integer.value;
			kfree(ret_buffer.pointer);
		}

		return 0;
	} else
		return -1;
}

static int scai_command_complex(struct scai_data *data, acpi_string pathname, struct scai_buffer *buf, struct scai_buffer *ret, u32 len)
{
	union acpi_object buf_obj, *ret_obj;
	struct acpi_object_list obj_list;
	struct acpi_buffer ret_buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	acpi_handle object;
	acpi_status status;

	obj_list.count = 1;
	obj_list.pointer = &buf_obj;
	buf_obj.type = ACPI_TYPE_BUFFER;
	buf_obj.buffer.length = len;
	buf_obj.buffer.pointer = (u8 *) buf;

	object = acpi_device_handle(data->acpi_dev);
	status = acpi_evaluate_object(object, pathname, &obj_list, &ret_buffer);

	if (ACPI_SUCCESS(status)) {
		ret_obj = ret_buffer.pointer;

		if (ret_obj->type != ACPI_TYPE_BUFFER) {
			pr_err("SCAI response is not a buffer\n");
			return -1;
		}

		if (ret_obj->buffer.length != len) {
			pr_err("SCAI response length mismatch\n");
			return -1;
		}

		memcpy(ret, ret_obj->buffer.pointer, len);
		kfree(ret_buffer.pointer);

		return 0;
	} else
		return -1;
}

static int scai_csfi_command(struct scai_data *data, struct scai_buffer *buf)
{
	int ret;
	u8 *buff = (u8 *) buf;

	pr_info("CSFI request:  0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
		buff[0], buff[1], buff[2], buff[3], buff[4], buff[5], buff[6], buff[7], buff[8], buff[9], buff[10], buff[11], buff[12], buff[13], buff[14], buff[15], buff[16], buff[17], buff[18], buff[19], buff[20]);

	ret = scai_command_complex(data, "CSFI", buf, buf, SCAI_CSFI_LEN);

	pr_info("CSFI response: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
		buff[0], buff[1], buff[2], buff[3], buff[4], buff[5], buff[6], buff[7], buff[8], buff[9], buff[10], buff[11], buff[12], buff[13], buff[14], buff[15], buff[16], buff[17], buff[18], buff[19], buff[20]);

	if (ret != 0) {
		pr_err("CSFI command failed\n");
		return ret;
	}

	if (buf->rflg != 0xaa) {
		pr_err("CSFI command was not successful\n");
		return -1;
	}

	return 0;
}

static int scai_csxi_command(struct scai_data *data, struct scai_buffer *buf)
{
	int ret;
	u8 *buff = (u8 *) buf;

	pr_info("CSXI request: "
		"0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x "
		"0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x "
		"0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x ",
		buff[0x0], buff[0x1], buff[0x2], buff[0x3], buff[0x4], buff[0x5], buff[0x6], buff[0x7], buff[0x8], buff[0x9], buff[0xA], buff[0xB], buff[0xC], buff[0xD], buff[0xE], buff[0xF],
		buff[0x10], buff[0x11], buff[0x12], buff[0x13], buff[0x14], buff[0x15], buff[0x16], buff[0x17], buff[0x18], buff[0x19], buff[0x1A], buff[0x1B], buff[0x1C], buff[0x1D], buff[0x1E], buff[0x1F],
		buff[0x20], buff[0x21], buff[0x22], buff[0x23], buff[0x24], buff[0x25], buff[0x26], buff[0x27], buff[0x28], buff[0x29], buff[0x2A], buff[0x2B], buff[0x2C], buff[0x2D], buff[0x2E], buff[0x2F]);

	ret = scai_command_complex(data, "CSXI", buf, buf, SCAI_CSXI_LEN);

	pr_info("CSXI response: "
		"0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x "
		"0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x "
		"0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x ",
		buff[0x0], buff[0x1], buff[0x2], buff[0x3], buff[0x4], buff[0x5], buff[0x6], buff[0x7], buff[0x8], buff[0x9], buff[0xA], buff[0xB], buff[0xC], buff[0xD], buff[0xE], buff[0xF],
		buff[0x10], buff[0x11], buff[0x12], buff[0x13], buff[0x14], buff[0x15], buff[0x16], buff[0x17], buff[0x18], buff[0x19], buff[0x1A], buff[0x1B], buff[0x1C], buff[0x1D], buff[0x1E], buff[0x1F],
		buff[0x20], buff[0x21], buff[0x22], buff[0x23], buff[0x24], buff[0x25], buff[0x26], buff[0x27], buff[0x28], buff[0x29], buff[0x2A], buff[0x2B], buff[0x2C], buff[0x2D], buff[0x2E], buff[0x2F]);

	if (ret != 0) {
		pr_err("CSXI command failed\n");
		return ret;
	}

	if (buf->rflg != 0xaa) {
		pr_err("CSXI command was not successful\n");
		return -1;
	}

	return 0;

}

static int scai_enable_csfi_command(struct scai_data *data, u16 sasb)
{
	int err;
	struct scai_buffer buf = {0};

	buf.safn = SCAI_SAFN;
	buf.sasb = sasb;
	buf.gunm = 0xbb;
	buf.guds[0] = 0xaa;

	err = scai_csfi_command(data, &buf);
	if (err)
		return err;

	if (buf.gunm != 0xdd && buf.guds[0] != 0xcc)
		return -ENODEV;

	return 0;
}

static int scai_notification_set(struct scai_data *data)
{
	int err;
	struct scai_buffer buf = {0};

	buf.safn = SCAI_SAFN;
	buf.sasb = SCAI_SASB_NOTIFICATION;
	buf.gunm = 0x80;
	buf.guds[0] = 0x02;

	err = scai_csfi_command(data, &buf);
	if (err)
		return err;

	return 0;
}

static int scai_kb_backlight_set(struct scai_data *data, u8 value)
{
	struct scai_buffer buf = {0};

	buf.safn = SCAI_SAFN;
	buf.sasb = SCAI_SASB_KB_BACKLIGHT;
	buf.gunm = SCAI_GUNM_SET;
	buf.guds[0] = value;

	return scai_csfi_command(data, &buf);
}

static int scai_kb_backlight_get(struct scai_data *data, u8 *value)
{
	int err;
	struct scai_buffer buf = {0};

	buf.safn = SCAI_SAFN;
	buf.sasb = SCAI_SASB_KB_BACKLIGHT;
	buf.gunm = SCAI_GUNM_GET;

	err = scai_csfi_command(data, &buf);

	*value = buf.gunm;

	return err;
}

static int scai_battery_safe_set(struct scai_data *data, u8 value)
{
	struct scai_buffer buf = {0};

	buf.safn = SCAI_SAFN;
	buf.sasb = SCAI_SASB_BATTERY_SAFE;
	buf.gunm = SCAI_GUNM_SET;
	buf.guds[0] = 0xe9;
	buf.guds[1] = 0x90;
	buf.guds[2] = value;

	return scai_csfi_command(data, &buf);
}

// static int scai_battery_safe_get(struct scai_data *data, u8 *value)
// {
// 	int err;
// 	struct scai_buffer buf = {0};

// 	buf.safn = SCAI_SAFN;
// 	buf.sasb = SCAI_SASB_BATTERY_SAFE;
// 	buf.gunm = 0x82;
// 	buf.guds[0] = 0xe9;
// 	buf.guds[1] = 0x92;

// 	err = scai_csfi_command(data, &buf);

// 	*value = buf.guds[1];

// 	return err;
// }

static int scai_webcam_disable_set(struct scai_data *data, u8 value)
{
	struct scai_buffer buf = {0};

	buf.safn = SCAI_SAFN;
	buf.sasb = SCAI_SASB_WEBCAM_DISABLE;
	buf.gunm = SCAI_GUNM_SET;
	buf.guds[0] = value;

	return scai_csfi_command(data, &buf);
}

static int scai_enable_commands(struct scai_data *data)
{
	int err;

	err = scai_enable_csfi_command(data, SCAI_SASB_BATTERY_SAFE);
	if (err != 0)
		return err;

	err = scai_enable_csfi_command(data, SCAI_SASB_KB_BACKLIGHT);
	if (err != 0)
		return err;

	err = scai_enable_csfi_command(data, SCAI_SASB_WEBCAM_DISABLE);
	if (err != 0)
		return err;

	err = scai_enable_csfi_command(data, SCAI_SASB_NOTIFICATION);
	if (err != 0)
		return err;

	return 0;
}

static int scai_enable(struct scai_data *data)
{
	int err;
	u64 ret;

	err = scai_command_integer(data, "SDLS", 1, &ret);

	pr_info("SLDS return value: 0x%llx\n", ret);

	return err;
}

static int scai_disable(struct scai_data *data)
{
	int err;
	u64 ret;

	err = scai_command_integer(data, "SDLS", 0, &ret);

	pr_info("SLDS return value: 0x%llx\n", ret);

	return err;
}

static int scai_kb_led_set(struct led_classdev *led_cdev, enum led_brightness value)
{
	struct scai_data *data;

	data = container_of(led_cdev, struct scai_data, kb_led);
	return scai_kb_backlight_set(data, value);
}

enum led_brightness scai_kb_led_get(struct led_classdev *led_cdev)
{
	int err;
	struct scai_data *data;
	u8 value;

	data = container_of(led_cdev, struct scai_data, kb_led);
	err = scai_kb_backlight_get(data, &value);
	if (err)
		return 0;

	return value;
}

static int scai_add(struct acpi_device *acpi_dev)
{
	struct scai_data *data;
	int err;

	data = devm_kzalloc(&acpi_dev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	acpi_dev->driver_data = data;
	data->acpi_dev = acpi_dev;

	err = scai_enable(data);
	if (err)
		return err;

	err = scai_enable_commands(data);
	if (err)
		return err;

	err = scai_notification_set(data);
	if (err)
		return err;

	err = scai_webcam_disable_set(data, 0);
	if (err)
		return err;

	data->kb_led.name = "scai::kbd_backlight";
	data->kb_led.brightness_set_blocking = scai_kb_led_set;
	data->kb_led.brightness_get = scai_kb_led_get;
	data->kb_led.max_brightness = 3;

	err = devm_led_classdev_register(&acpi_dev->dev, &data->kb_led);
	if (err)
		return err;

	return 0;
}

static int scai_remove(struct acpi_device *acpi_dev)
{
	int err;
	struct scai_data *data;

	data = acpi_driver_data(acpi_dev);

	devm_led_classdev_unregister(&acpi_dev->dev, &data->kb_led);

	err = scai_webcam_disable_set(data, 1);
	if (err)
		return err;

	err = scai_disable(data);
	if (err)
		return err;

	return 0;
}

static void scai_notify(struct acpi_device *acpi_dev, u32 event)
{
	struct scai_data *data;

	data = acpi_driver_data(acpi_dev);

	scai_command_integer(data, "SETM", event, NULL);

	pr_info("Notify %x", event);
}

static struct acpi_driver scai_driver = {
	.name = "Samsung SCAI Driver",
	.owner = THIS_MODULE,
	.ids = device_ids,
	.flags = ACPI_DRIVER_ALL_NOTIFY_EVENTS,
	.ops = {
		.add = scai_add,
		.remove = scai_remove,
		.notify = scai_notify
	},
};
module_acpi_driver(scai_driver);

MODULE_ALIAS("acpi*:SAM0428:*");
MODULE_AUTHOR("Giulio Girardi <giulio.girardi@protechgroup.it>");
MODULE_LICENSE("GPL");
