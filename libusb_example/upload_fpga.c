#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libusb-1.0/libusb.h>

#define INTERFACE     0
#define VID      0x27ae
#define PID      0x1016

static volatile bool do_exit = false;

static int upload_fpga(libusb_device_handle *dev_handle, const char *filename);

// This will catch user initiated CTRL+C type events and allow the program to exit
void sighandler(int signum) {
    printf("Exit\n");
    do_exit = true;
}

int main(int argc, char *argv[]) {
    int status = LIBUSB_SUCCESS;
    int fd = -1;
    libusb_context *ctx;
    libusb_device_handle* dev_handle;

    if (argc < 2) {
        printf("Usage: %s <filename>\n", argv[0]);
        return 1;
    }
    char *filename = argv[1];

    // Define signal handler to catch system generated signals
    // (If user hits CTRL+C, this will deal with it.)
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGQUIT, sighandler);

    status = libusb_init(&ctx);
    if (status) {
        fprintf(stderr, "%s\n", libusb_strerror((enum libusb_error)status));
        goto err_ret;
    }

    dev_handle = libusb_open_device_with_vid_pid(ctx, VID, PID);
    if (dev_handle == NULL) {
        fprintf(stderr, "Error: No device with VID=0x%04X, PID=0x%04X\n", VID, PID);
        status = 1;
        goto err_usb;
    }

    if (libusb_kernel_driver_active(dev_handle, INTERFACE) == 1) {
        printf("Warning: Kernel driver active, detaching kernel driver...");
        status = libusb_detach_kernel_driver(dev_handle, INTERFACE);
        if (status) {
            fprintf(stderr, "%s\n", libusb_strerror((enum libusb_error)status));
            goto err_dev;
        }
    }

    status = libusb_claim_interface(dev_handle, INTERFACE);
    if (status) {
        fprintf(stderr, "%s\n", libusb_strerror((enum libusb_error)status));
        goto err_dev;
    }

    status = upload_fpga(dev_handle, filename);

err_dev:
    libusb_close(dev_handle);
err_usb:
    libusb_exit(ctx);
err_ret:
    return status;
}

static int upload_fpga(libusb_device_handle *dev_handle, const char *filename) {
    int status = 0;
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        fprintf(stderr, "Error: Open file %s\n%s\n", filename, strerror(errno));
	return 1;
    }

    fseek(fp, 0L, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    const unsigned ep0_buf_size = 512; // TODO read this from the USB descriptor
    unsigned page = 0;
    unsigned num_pages = (size - 1) / ep0_buf_size + 1;

    printf("Upload FPGA configuration...\n");
    while (!feof(fp) && !do_exit) {
        unsigned char data[ep0_buf_size];

        size_t len = fread(data, sizeof(char), ep0_buf_size, fp);
	status = libusb_control_transfer(dev_handle, LIBUSB_RECIPIENT_DEVICE | LIBUSB_REQUEST_TYPE_VENDOR, 0x00, 0xff00, page, data, len, 1000);
        if (status < 0) {
            printf("\n");
            fprintf(stderr, "Error: Upload FPGA config\n%s\n", libusb_strerror((enum libusb_error)status));
            goto err_ret;
        }
        if (page % 30 == 0) {
            printf("%d%% .. ", page * 100 / num_pages);
            fflush(stdout);
        }
        page++;
    }
    status = libusb_control_transfer(dev_handle, LIBUSB_RECIPIENT_DEVICE | LIBUSB_REQUEST_TYPE_VENDOR, 0x00, 0xff00, 0xffff, NULL, 0, 1000);
    if (status) {
        printf("\n");
        fprintf(stderr, "Error: Upload FPGA config\n%s\n", libusb_strerror((enum libusb_error)status));
        goto err_ret;
    }
    printf("100%%\n");

    // wait until FPGA is loaded
    sleep(1); // TODO Check, if FPGA is ready
    printf("Done\n");

err_ret:
    fclose(fp);
    return status;
}
