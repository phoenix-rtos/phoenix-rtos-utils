/*
 * Phoenix-RTOS
 *
 * phoenix-rtos-utils
 *
 * ldconfig
 *
 * Copyright 2024 Phoenix Systems
 * Author: Hubert Badocha
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */


#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>


#define SHLIB_EXT ".so"

#define VERSION_LEN 3


static void libSetNoVersion(long version[VERSION_LEN])
{
	for (int i = 0; i < VERSION_LEN; i++) {
		version[i] = -1;
	}
}


static int getVersion(const char *start, long version[VERSION_LEN])
{
	int versionLen;

	libSetNoVersion(version);

	for (versionLen = 0; (versionLen < VERSION_LEN) && ((*start) != '\0'); versionLen++) {
		if ((*start) != '.') {
			return -1;
		}
		start++;

		char *end;
		errno = 0;
		version[versionLen] = strtol(start, &end, 10);
		if ((errno != 0) || (version[versionLen] < 0)) {
			return -1;
		}
		start = end;
	}

	return versionLen;
}


static int getVersionFromName(const char *path, long version[VERSION_LEN])
{
	char *start = strstr(path, SHLIB_EXT);
	if (start == NULL) {
		return -1;
	}
	start += strlen(SHLIB_EXT);

	return getVersion(start, version);
}


static int libCmp(const long a[VERSION_LEN], const long b[VERSION_LEN])
{
	for (int i = 0; i < VERSION_LEN; i++) {
		if (a[i] > b[i]) {
			return 1;
		}
		if (a[i] < b[i]) {
			return -1;
		}
	}

	return 0;
}


static int createSymlink(const char *target, const char *linkPath)
{
	if (symlink(target, linkPath) == 0) {
		return 1;
	}

	if (errno != EEXIST) {
		return -errno;
	}
	/* No luck, we have to check if the new version is higher than the old version. */
	char buf[PATH_MAX];
	ssize_t len = readlink(linkPath, buf, PATH_MAX - 1);
	if (len == -1) {
		if (errno == EINVAL) {
			fprintf(stderr, "%s: is not a symlink\n", target);
		}
		return -errno;
	}
	buf[len] = '\0';

	long targetLib[VERSION_LEN], linkLib[VERSION_LEN];
	if (getVersionFromName(target, targetLib) == -1) {
		fprintf(stderr, "%s: points not at .so object\n", target);
		return -1;
	}
	if (getVersionFromName(linkPath, linkLib) == -1) {
		fprintf(stderr, "%s: points not at .so object\n", linkPath);
		return -1;
	}

	if (libCmp(linkLib, targetLib) != 1) {
		/* linkPath is older that what target points at, do not update. */
		return 0;
	}

	/* No risk of data loss as the link will be regenerated on next boot. */
	if (unlink(target) == -1) {
		return -errno;
	}

	if (symlink(target, linkPath) == -1) {
		return -errno;
	}
	return 1;
}


static int createLibphoenixAliases(const char *libsPath)
{
	const char *libphoenixNamePrefix = "libphoenix";
	const char *libphoenixAliases[] = { "libc", "libm", "libpthread", "libubsan" };

	DIR *dir = opendir(libsPath);
	if (dir == NULL) {
		fprintf(stderr, "Failed to find dir: %s!\n", libsPath);
		return errno;
	}

	int added = 0;
	/* Make sure newly added links are recursively processed. */
	do {
		if (added != 0) {
			added = 0;
			rewinddir(dir);
		}
		for (;;) {
			struct dirent *dp = readdir(dir);
			if (dp == NULL) {
				break;
			}

			const char *start = dp->d_name;
			if (strncmp(start, libphoenixNamePrefix, strlen(libphoenixNamePrefix)) != 0) {
				continue;
			}
			start += strlen(libphoenixNamePrefix);
			const char *const libphoenixNamePrefixEnd = start;
			if (strncmp(start, SHLIB_EXT, strlen(SHLIB_EXT)) != 0) {
				continue;
			}
			start += strlen(SHLIB_EXT);

			long currentVersion[VERSION_LEN];
			/* Ensure Major.Minor.Patch format */
			if (getVersion(start, currentVersion) != 3) {
				continue;
			}

			char libphoenixAbsPath[PATH_MAX];
			(void)sprintf(libphoenixAbsPath, "%s/%s", libsPath, dp->d_name); /* Must fit. */

			for (int i = 0; i < (sizeof(libphoenixAliases) / sizeof(libphoenixAliases[0])); i++) {
				char aliasName[PATH_MAX];
				int aliasLen = snprintf(aliasName, PATH_MAX, "%s/%s%s", libsPath, libphoenixAliases[i], libphoenixNamePrefixEnd);
				if (aliasLen >= PATH_MAX) {
					closedir(dir);
					return ENAMETOOLONG;
				}

				int ret = createSymlink(libphoenixAbsPath, aliasName);
				if (ret < 0) {
                    printf("DUPA\n");
					closedir(dir);
					return ret;
				}
				if (ret == 1) {
					added = 1;
				}
			}
			if (added == 1) {
				break; /* readdir is broken on phoenix, after each insert rewind is needed. */
			}
		}
	} while (added != 0);

	closedir(dir);
	return 0;
}


static int generateSymlinks(const char *libsPath)
{
	DIR *dir = opendir(libsPath);
	if (dir == NULL) {
		fprintf(stderr, "Failed to find dir: %s!\n", libsPath);
		return errno;
	}
	int added = 0;
	/* Make sure newly added links are recursively processed. */
	do {
		if (added != 0) {
			added = 0;
			rewinddir(dir);
		}
		for (;;) {
			struct dirent *dp = readdir(dir);
			if (dp == NULL) {
				break;
			}

			long version[VERSION_LEN];
			int versionLen = getVersionFromName(dp->d_name, version);
			if (versionLen < 1) {
				continue;
			}

			char libAbsPath[PATH_MAX];
			(void)sprintf(libAbsPath, "%s/%s", libsPath, dp->d_name); /* Must fit. */

			/* Create link without last version specifier. */
			char linkPath[PATH_MAX];
			(void)strcpy(linkPath, libAbsPath);
			char *lastDot = strrchr(linkPath, '.');
			assert(lastDot != NULL); /* getVersionFromName ensures dot is present. */
			*lastDot = '\0';

			int ret = createSymlink(libAbsPath, linkPath);
			if (ret < 0) {
				closedir(dir);
				return ret;
			}
			if (ret == 1) {
				added = 1;
				break; /* readdir is broken on phoenix, after each insert rewind is needed. */
			}
		}
	} while (added != 0);

	closedir(dir);
	return 0;
}


int main(void)
{
	int ret;

	/* TODO: should be configurable by /etc/ld.so.conf. */
	const char *libsPath = "/usr/lib";

	ret = createLibphoenixAliases(libsPath);
	if (ret < 0) {
		fprintf(stderr, "Failed to create libphoenix aliases: %d!\n", ret);
		return 1;
	}

	ret = generateSymlinks(libsPath);
	if (ret < 0) {
		fprintf(stderr, "Failed to create symlinks: %d!\n", ret);
		return 1;
	}
	return 0;
}
