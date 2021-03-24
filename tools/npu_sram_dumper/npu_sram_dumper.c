#define _GNU_SOURCE

#include <fcntl.h>
#include <linux/android/binder.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sched.h>
#include <sys/wait.h>
#include <time.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/capability.h>


#define NPU_FILE "/dev/vertex10"
#define SRAM_FILE "/sys/kernel/debug/npu/SRAM-TCU"
#define SRAM_SIZE 0xe0000


int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    if (argc != 2) {
        printf("Usage: %s <output_filepath>\n", argv[0]);
        exit(0);
    }

    int npu_fd = open(NPU_FILE, O_RDONLY);
    if (npu_fd < 0) {
        printf("Error while opening %s (%s)\n", NPU_FILE, strerror(errno));
        exit(0);
    }

    int sram_fd = open(SRAM_FILE, O_RDONLY);
    if (sram_fd < 0) {
        printf("Error while opening %s (%s)\n", SRAM_FILE, strerror(errno));
        exit(0);
    }

    int outfile_fd = open(argv[1], O_TRUNC | O_RDWR | O_CREAT);
    if (outfile_fd < 0) {
        printf("Error while opening %s (%s)\n", argv[1], strerror(errno));
        exit(0);
    }

    chmod(argv[1], 0644);

    char buffer[SRAM_SIZE];
    read(sram_fd, buffer, sizeof(buffer));
    write(outfile_fd, buffer, sizeof(buffer));

    close(outfile_fd);
    close(sram_fd);
    close(npu_fd);

	return 0;
}
