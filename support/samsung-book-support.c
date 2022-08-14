#include <fcntl.h>
#include <stdio.h>
#include <linux/input.h>
#include <sys/time.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#define UPOWER_DBUS_NAME						"org.freedesktop.UPower"
#define UPOWER_DBUS_PATH						"/org/freedesktop/UPower"
#define UPOWER_DBUS_PATH_KBDBACKLIGHT			"/org/freedesktop/UPower/KbdBacklight"
#define UPOWER_DBUS_INTERFACE					"org.freedesktop.UPower"
#define UPOWER_DBUS_INTERFACE_KBDBACKLIGHT		"org.freedesktop.UPower.KbdBacklight"

#define SAMSUNG_BOOK_KEYBOARD_INPUT				"/dev/input/event2"

GDBusProxy *upowerd;
GApplication *app;
int brightness_max;

static gboolean libevdev_event_handler(GIOChannel *source, GIOCondition condition, gpointer data)
{
	static struct input_event pv = {0};
	struct input_event ev;
	struct timeval td;
	gsize bytes_read;

	g_io_channel_read_chars(source, (gchar *)&ev, sizeof(ev), &bytes_read, NULL);

	if (bytes_read > 0) {
		if (bytes_read != sizeof(ev)) {
			g_warning("warning, only read %ld bytes from keyboard input", bytes_read);
			return TRUE;
		}
	} else {
		return TRUE;
	}

	if (ev.type == EV_MSC && ev.value == 0xac) {
		timersub(&ev.time, &pv.time, &td);
		memcpy(&pv, &ev, sizeof(struct input_event));

		GVariant *k_now = NULL;
		GVariant *k_set = NULL;
		GError *error = NULL;
		int brightness;

		// Debouncing
		if (td.tv_sec >= 0 && td.tv_usec >= 300000 || td.tv_sec > 0) {
			k_now = g_dbus_proxy_call_sync (upowerd,
											"GetBrightness",
											NULL,
											G_DBUS_CALL_FLAGS_NONE,
											-1,
											NULL,
											&error);
			if (k_now == NULL) {
				g_warning ("Failed to get brightness: %s", error->message);
				g_error_free (error);
				goto out;
			}

			g_variant_get (k_now, "(i)", &brightness);

			int next_brightness = (brightness + 1) % (brightness_max + 1);

			k_set = g_dbus_proxy_call_sync (upowerd,
											"SetBrightness",
											g_variant_new("(i)", next_brightness),
											G_DBUS_CALL_FLAGS_NONE,
											-1,
											NULL,
											&error);
			if (k_set == NULL) {
				g_warning ("Failed to get brightness: %s", error->message);
				g_error_free (error);
				goto out;
			}
		}

	out:
		if (k_now != NULL)
			g_variant_unref (k_now);
		if (k_set != NULL)
			g_variant_unref (k_set);
	}

	return TRUE;
}

void power_keyboard_proxy_ready_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	GVariant *k_max = NULL;
	GError *error = NULL;

	upowerd = g_dbus_proxy_new_for_bus_finish (res, &error);
	if (upowerd == NULL) {
		g_warning ("Could not connect to UPower: %s",
					error->message);
		g_error_free (error);
		goto out;
	}

	k_max = g_dbus_proxy_call_sync (upowerd,
									"GetMaxBrightness",
									NULL,
									G_DBUS_CALL_FLAGS_NONE,
									-1,
									NULL,
									&error);
	if (k_max == NULL) {
		g_warning ("Failed to get max brightness: %s", error->message);
		g_error_free (error);
		goto out;
	}

	g_variant_get (k_max, "(i)", &brightness_max);

out:
	if (k_max != NULL)
		g_variant_unref (k_max);
}

void application_activate_handler()
{
	GError *error = NULL;

	GIOChannel *channel = g_io_channel_new_file(SAMSUNG_BOOK_KEYBOARD_INPUT, "r", &error);
	if (channel == NULL) {
		g_warning ("Failed to open keyboard input: %s", error->message);
		g_error_free (error);
		goto out;
	}

	if (g_io_channel_set_encoding(channel, NULL, &error) != G_IO_STATUS_NORMAL) {
		g_warning ("Failed to open keyboard input: %s", error->message);
		g_error_free (error);
		goto out;
	}

	g_io_add_watch(channel, G_IO_IN, libevdev_event_handler, NULL);

	// Connect to upower daemon
	g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
								G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
								NULL,
								UPOWER_DBUS_NAME,
								UPOWER_DBUS_PATH_KBDBACKLIGHT,
								UPOWER_DBUS_INTERFACE_KBDBACKLIGHT,
								NULL,
								power_keyboard_proxy_ready_cb,
								NULL);

	return;

out:
	if (channel != NULL)
		g_io_channel_unref(channel);

	// Exit
	g_application_release(app);
}

int main(int argc, char *argv[])
{
	int status;
	app = g_application_new("com.rapgenic.SamsungBookSupport", G_APPLICATION_FLAGS_NONE);

	g_application_hold(app);
	g_signal_connect(app, "activate", application_activate_handler, NULL);

	status = g_application_run(app, argc, argv);
	g_object_unref(app);
	return status;
}

