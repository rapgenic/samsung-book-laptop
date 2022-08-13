#include <linux/gfp.h>
#include <linux/device.h>
#include <linux/acpi.h>
#include <linux/leds.h>

#define SCAI_SAFN 0x5843

#define SCAI_SASB_KB_BACKLIGHT 0x78

#define SCAI_GUNM_KB_BACKLIGHT_SET 0x82
#define SCAI_GUNM_KB_BACKLIGHT_GET 0x81

struct scai_buffer {
	u16 safn;
	u16 sasb;
	u8 rflg;
	union {
		struct {
			u8 gunm;
			u8 guds[139];
		};
		struct {
			u8 caid[128];
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

static const struct acpi_device_id device_ids[] = {
	{"SAM0428", 0},
	{"", 0}
};
MODULE_DEVICE_TABLE(acpi, device_ids);

static void scai_notify(struct acpi_device *acpi_dev, u32 event)
{
	pr_info("Notify %d\n", event);
}

static int scai_command(struct scai_data *data, acpi_string pathname, struct scai_buffer *buf, struct scai_buffer *ret, u32 len)
{
	union acpi_object buf_obj, *ret_obj;
	struct acpi_object_list buf_obj_list;
	struct acpi_buffer ret_buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	acpi_handle object;
	acpi_status status;

	u8 *buff = (u8 *)buf;

	pr_info("SCAI buffer: ");

	for (int i = 0; i < len - 2; i++)
		pr_info("0x%2x ", buff[i]);

	pr_info("\n");

	buf_obj_list.count = 1;
	buf_obj_list.pointer = &buf_obj;
	buf_obj.type = ACPI_TYPE_BUFFER;
	buf_obj.buffer.length = len;
	buf_obj.buffer.pointer = (u8 *) &buf;

	object = acpi_device_handle(data->acpi_dev);
	status = acpi_evaluate_object(object, pathname, &buf_obj_list, &ret_buffer);

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

	ret = scai_command(data, "CSFI", buf, buf, 0x15);
	if (ret != 0) {
		pr_err("CSFI command failed\n");
		return ret;
	}

	if (buf->rflg != 0xaa) {
		pr_err("CSFI command was not successful\n");
		return -1;
	} else {
		pr_info("Success!\n");
	}

	return 0;
}

static int scai_enable_command(struct scai_data *data, u16 sasb)
{
	struct scai_buffer buf = {0};

	buf.safn = SCAI_SAFN;
	buf.sasb = sasb;
	buf.gunm = 0xbb;
	buf.guds[0] = 0xaa;

	return scai_csfi_command(data, &buf);
}

static int scai_kb_backlight_set(struct scai_data *data, u8 value)
{
	struct scai_buffer buf = {0};

	buf.safn = SCAI_SAFN;
	buf.sasb = SCAI_SASB_KB_BACKLIGHT;
	buf.gunm = 0x82;
	buf.guds[0] = value;

	return scai_csfi_command(data, &buf);
}

static int scai_kb_led_set(struct led_classdev *led_cdev, enum led_brightness value)
{
	struct scai_buffer buf = {0};
	struct scai_data *data;
	union acpi_object buf_obj, *ret_obj;
	struct acpi_object_list buf_obj_list;
	struct acpi_buffer ret_buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	acpi_handle object;
	acpi_status status;

	data = container_of(led_cdev, struct scai_data, kb_led);

	buf.safn = SCAI_SAFN;
	buf.sasb = SCAI_SASB_KB_BACKLIGHT;
	buf.gunm = 0xbb;
	buf.guds[0] = 0xaa;

	buf_obj_list.count = 1;
	buf_obj_list.pointer = &buf_obj;
	buf_obj.type = ACPI_TYPE_BUFFER;
	buf_obj.buffer.length = 0x15;
	buf_obj.buffer.pointer = (u8 *) &buf;

	object = acpi_device_handle(data->acpi_dev);
	status = acpi_evaluate_object(object, "CSFI", &buf_obj_list, NULL);

	if (!ACPI_SUCCESS(status))
		return -1;

	buf.gunm = 0x82;
	buf.guds[0] = value;

	status = acpi_evaluate_object(object, "CSFI", &buf_obj_list, NULL);

	if (!ACPI_SUCCESS(status))
		return -1;

	// err = scai_enable_command(data, SCAI_SASB_KB_BACKLIGHT);
	// if (err != 0)
	// 	return err;

	// err = scai_kb_backlight_set(data, value);
	// if (err != 0)
	// 	return err;

	return 0;
}

static int scai_add(struct acpi_device *acpi_dev)
{
	struct scai_data *data;
	int err;

	pr_info("Add\n");

	data = devm_kzalloc(&acpi_dev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	acpi_dev->driver_data = data;
	data->acpi_dev = acpi_dev;

	data->kb_led.name = "scai::kbd_backlight";
	data->kb_led.brightness_set_blocking = scai_kb_led_set;
	data->kb_led.max_brightness = 3;

	err = devm_led_classdev_register(&acpi_dev->dev, &data->kb_led);
	if (err)
		return err;

	return 0;
}

static int scai_remove(struct acpi_device *acpi_dev)
{
	struct scai_data *data;

	pr_info("Remove\n");

	data = acpi_driver_data(acpi_dev);

	devm_led_classdev_unregister(&acpi_dev->dev, &data->kb_led);

	return 0;
}

static struct acpi_driver scai_driver = {
	.name = "Samsung SCAI Driver",
	.class = "hotkey",
	.ids = device_ids,
	.ops = {
		.add = scai_add,
		.remove = scai_remove,
		.notify = scai_notify
	},
};
module_acpi_driver(scai_driver);

MODULE_AUTHOR("Giulio Girardi <giulio.girardi@protechgroup.it>");
MODULE_LICENSE("GPL");
