/*
 * KernelSU Userspace Tool
 *
 * This tool interacts with the KernelSU driver via ioctl to manage
 * application profiles and mount namespaces.
 * 
 * Authors: AzyrRuthless & j-hc
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

/* KernelSU Constants */
#define KSU_APP_PROFILE_VER 2
#define KSU_MAX_PACKAGE_NAME 256
#define KSU_MAX_GROUPS 32
#define KSU_SELINUX_DOMAIN 64

#define KSU_INSTALL_MAGIC1 0xDEADBEEF
#define KSU_INSTALL_MAGIC2 0xCAFEBABE

/* IOCTL Commands */
#define KSU_IOCTL_MAGIC 'K'
#define KSU_IOCTL_UID_SHOULD_UMOUNT _IOC(_IOC_READ | _IOC_WRITE, KSU_IOCTL_MAGIC, 9, 0)
#define KSU_IOCTL_GET_MANAGER_APPID _IOC(_IOC_READ, KSU_IOCTL_MAGIC, 10, 0)
#define KSU_IOCTL_GET_APP_PROFILE _IOC(_IOC_READ | _IOC_WRITE, KSU_IOCTL_MAGIC, 11, 0)
#define KSU_IOCTL_SET_APP_PROFILE _IOC(_IOC_WRITE, KSU_IOCTL_MAGIC, 12, 0)

/* Data Structures */
struct root_profile {
	int32_t uid;
	int32_t gid;
	int32_t groups_count;
	int32_t groups[KSU_MAX_GROUPS];
	struct {
		uint64_t effective;
		uint64_t permitted;
		uint64_t inheritable;
	} capabilities;
	char selinux_domain[KSU_SELINUX_DOMAIN];
	int32_t namespaces;
	uint64_t flags; // Required for KernelSU v3+ driver structure compatibility
};

struct non_root_profile {
	bool umount_modules;
};

struct app_profile {
	uint32_t version;
	char key[KSU_MAX_PACKAGE_NAME];
	int32_t current_uid;
	bool allow_su;
	union {
		struct {
			bool use_default;
			char template_name[KSU_MAX_PACKAGE_NAME];
			struct root_profile profile;
		} rp_config;
		struct {
			bool use_default;
			struct non_root_profile profile;
		} nrp_config;
	};
};

/* IOCTL Command Structures */
struct ksu_uid_should_umount_cmd {
	uint32_t uid;
	uint8_t should_umount;
};

struct ksu_get_manager_appid_cmd {
	uint32_t appid;
};

/* Helper Functions */

/**
 * ksu_get_driver_fd - Obtain file descriptor for KernelSU driver
 *
 * Returns a valid file descriptor on success, or -1 on failure.
 */
static int ksu_get_driver_fd(void)
{
	int fd = -1;
	/* Use the reboot syscall magic to request a driver handle */
	syscall(SYS_reboot, KSU_INSTALL_MAGIC1, KSU_INSTALL_MAGIC2, 0, &fd);
	return fd;
}

#define REPORT_ERR(op, err) \
	fprintf(stderr, "ERROR: %s failed: %s\n", op, strerror(err))

int main(int argc, char *argv[])
{
	int fd;
	long uid;
	int ret = 0;
	struct ksu_uid_should_umount_cmd umount_cmd = { 0 };
	struct ksu_get_manager_appid_cmd appid_cmd = { 0 };
	struct app_profile profile = { 0 };

	if (argc <= 2) {
		fprintf(stderr,
				"ksu_profile (github.com/AzyrRuthless & github.com/j-hc)\n"
				"Disables \"Unmount modules\" for given package\n"
				"Usage: %s <uid> <pkg name>\n", argv[0]);
		return 1;
	}

	uid = atol(argv[1]);

	/* 1. Get Driver FD */
	fd = ksu_get_driver_fd();
	if (fd < 0) {
		REPORT_ERR("ksu_get_driver_fd", errno);
		return 1;
	}

	/* 2. Check if UID should unmount */
	umount_cmd.uid = (uint32_t)uid;
	if (ioctl(fd, KSU_IOCTL_UID_SHOULD_UMOUNT, &umount_cmd) < 0) {
		REPORT_ERR("KSU_IOCTL_UID_SHOULD_UMOUNT", errno);
		ret = 1;
		goto out;
	}

	if (!umount_cmd.should_umount) {
		/* Not relevant for this UID */
		goto out;
	}

	/* 3. Get Manager AppID */
	if (ioctl(fd, KSU_IOCTL_GET_MANAGER_APPID, &appid_cmd) < 0) {
		REPORT_ERR("KSU_IOCTL_GET_MANAGER_APPID", errno);
		ret = 1;
		goto out;
	}

	/* 4. Switch to Manager UID to gain permission to edit profiles */
	/* Note: Current process must have CAP_SETUID (usually root) */
	if (setuid(appid_cmd.appid) != 0) {
		REPORT_ERR("setuid", errno);
		ret = 1;
		goto out;
	}

	/* 5. Get or Initialize App Profile */
	profile.current_uid = (int32_t)uid;
	if (ioctl(fd, KSU_IOCTL_GET_APP_PROFILE, &profile) < 0) {
		/* Profile might not exist, initialize a new one */
		printf("Creating new profile for %s\n", argv[2]);
		memset(&profile, 0, sizeof(struct app_profile));
		profile.version = KSU_APP_PROFILE_VER;
		profile.current_uid = (int32_t)uid;
		strncpy(profile.key, argv[2], KSU_MAX_PACKAGE_NAME - 1);
	}

	/* 6. Modify Profile (Disable module unmounting) */
	profile.nrp_config.use_default = false;
	profile.nrp_config.profile.umount_modules = false;

	/* 7. Apply Profile */
	if (ioctl(fd, KSU_IOCTL_SET_APP_PROFILE, &profile) < 0) {
		REPORT_ERR("KSU_IOCTL_SET_APP_PROFILE", errno);
		ret = 1;
		goto out;
	}

	printf("Success\n");

out:
	close(fd);
	return ret;
}
