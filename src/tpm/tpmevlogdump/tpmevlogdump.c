/*
 * Copyright (C) 2020 Canonical
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#include "fwts.h"

#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "fwts_tpm.h"

#define FWTS_TPM_LOG_DIR_PATH		"/sys/kernel/security"


static void tpmevlogdump_data_hexdump(fwts_framework *fw, uint8_t *data, size_t size, char *str)
{
	size_t i;

	fwts_log_info_verbatim(fw, "%s: ", str);
	for (i = 0; i < size; i += 16) {
		char buffer[128];
		size_t left = size - i;

		fwts_dump_raw_data(buffer, sizeof(buffer), data + i, i, left > 16 ? 16 : left);
		fwts_log_info_verbatim(fw, "%s", buffer + 2);
	}
}

static char *tpmevlogdump_evtype_to_string (fwts_tpmlog_event_type event_type)
{

	char *str;

	switch (event_type) {
	case EV_PREBOOT_CERT:
		str = "EV_PREBOOT_CERT";
		break;
	case EV_POST_CODE:
		str = "EV_POST_CODE";
		break;
	case EV_UNUSED:
		str = "EV_UNUSED";
		break;
	case EV_NO_ACTION:
		str = "EV_NO_ACTION";
		break;
	case EV_SEPARATOR:
		str = "EV_SEPARATOR";
		break;
	case EV_ACTION:
		str = "EV_ACTION";
		break;
	case EV_EVENT_TAG:
		str = "EV_EVENT_TAG";
		break;
	case EV_S_CRTM_CONTENTS:
		str = "EV_S_CRTM_CONTENTS";
		break;
	case EV_S_CRTM_VERSION:
		str = "EV_S_CRTM_VERSION";
		break;
	case EV_CPU_MICROCODE:
		str = "EV_CPU_MICROCODE";
		break;
	case EV_PLATFORM_CONFIG_FLAGS:
		str = "EV_PLATFORM_CONFIG_FLAGS";
		break;
	case EV_TABLE_OF_DEVICES:
		str = "EV_TABLE_OF_DEVICES";
		break;
	case EV_IPL:
		str = "EV_IPL";
		break;
	case EV_IPL_PARTITION_DATA:
		str = "EV_IPL_PARTITION_DATA";
		break;
	case EV_NONHOST_CODE:
		str = "EV_NONHOST_CODE";
		break;
	case EV_NONHOST_CONFIG:
		str = "EV_NONHOST_CONFIG";
		break;
	case EV_NONHOST_INFO:
		str = "EV_NONHOST_INFO";
		break;
	case EV_OMIT_BOOT_DEVICE_EVENTS:
		str = "EV_OMIT_BOOT_DEVICE_EVENTS";
		break;
	case EV_EFI_EVENT_BASE:
		str = "EV_EFI_EVENT_BASE";
		break;
	case EV_EFI_VARIABLE_DRIVER_CONFIG:
		str = "EV_EFI_VARIABLE_DRIVER_CONFIG";
		break;
	case EV_EFI_VARIABLE_BOOT:
		str = "EV_EFI_VARIABLE_BOOT";
		break;
	case EV_EFI_BOOT_SERVICES_APPLICATION:
		str = "EV_EFI_BOOT_SERVICES_APPLICATION";
		break;
	case EV_EFI_BOOT_SERVICES_DRIVER:
		str = "EV_EFI_BOOT_SERVICES_DRIVER";
		break;
	case EV_EFI_RUNTIME_SERVICES_DRIVER:
		str = "EV_EFI_RUNTIME_SERVICES_DRIVER";
		break;
	case EV_EFI_GPT_EVENT:
		str = "EV_EFI_GPT_EVENT";
		break;
	case EV_EFI_ACTION:
		str = "EV_EFI_ACTION";
		break;
	case EV_EFI_PLATFORM_FIRMWARE_BLOB:
		str = "EV_EFI_PLATFORM_FIRMWARE_BLOB";
		break;
	case EV_EFI_HANDOFF_TABLES:
		str = "EV_EFI_HANDOFF_TABLES";
		break;
	case EV_EFI_HCRTM_EVENT:
		str = "EV_EFI_HCRTM_EVENT";
		break;
	case EV_EFI_VARIABLE_AUTHORITY:
		str = "EV_EFI_VARIABLE_AUTHORITY";
		break;
	default:
		str = "Unknown";
		break;
	}

	return str;
}

static char *tpmevlogdump_hash_to_string (TPM2_ALG_ID hash)
{

	char *str;

	switch (hash) {
	case TPM2_ALG_SHA1:
		str = "SHA1";
		break;
	case TPM2_ALG_SHA256:
		str = "SHA256";
		break;
	case TPM2_ALG_SHA384:
		str = "SHA384";
		break;
	case TPM2_ALG_SHA512:
		str = "SHA512";
		break;
	default:
		str = "Unknown";
		break;
	}

	return str;
}

static uint8_t tpmevlogdump_get_hash_size (TPM2_ALG_ID hash)
{
	uint8_t sz;

	switch (hash) {
	case TPM2_ALG_SHA1:
		sz = TPM2_SHA1_DIGEST_SIZE;
		break;
	case TPM2_ALG_SHA256:
		sz = TPM2_SHA256_DIGEST_SIZE;
		break;
	case TPM2_ALG_SHA384:
		sz = TPM2_SHA384_DIGEST_SIZE;
		break;
	case TPM2_ALG_SHA512:
		sz = TPM2_SHA512_DIGEST_SIZE;
		break;
	default:
		sz = 0;
		break;
	}

	return sz;
}

static size_t tpmevlogdump_specid_event_dump(fwts_framework *fw, uint8_t *data, size_t len)
{

	uint32_t i;
	size_t len_remain = len;
	uint8_t *pdata = data;
	char *str_info;

	/* check the data length for dumping */
	if (len_remain < sizeof(fwts_pc_client_pcr_event)) {
		fwts_log_info(fw, "Cannot get enough length for dumping data.");
		return 0;
	}
	fwts_pc_client_pcr_event *pc_event = (fwts_pc_client_pcr_event *)pdata;
	fwts_log_info_verbatim(fw, "PCRIndex:                    0x%8.8" PRIx32, pc_event->pcr_index);
	str_info = tpmevlogdump_evtype_to_string(pc_event->event_type);
	fwts_log_info_verbatim(fw, "EventType:                   0x%8.8" PRIx32 "(%s)", pc_event->event_type, str_info);
	tpmevlogdump_data_hexdump(fw, pc_event->digest, sizeof(pc_event->digest), "Digest");
	fwts_log_info_verbatim(fw, "EventSize:                   0x%8.8" PRIx32, pc_event->event_data_size);

	pdata += sizeof(fwts_pc_client_pcr_event);
	len_remain -= sizeof(fwts_pc_client_pcr_event);

	/* check the data length for dumping */
	if (len_remain < sizeof(fwts_efi_spec_id_event)) {
		fwts_log_info(fw, "Cannot get enough length for dumping data.");
		return 0;
	}
	fwts_efi_spec_id_event *specid_evcent = (fwts_efi_spec_id_event *)pdata;
	fwts_log_info_verbatim(fw, "EfiSpecIdEvent:");
	fwts_log_info_verbatim(fw, "  Signature:                 %s", (char *)specid_evcent->signature);
	fwts_log_info_verbatim(fw, "  platformClass:             0x%8.8" PRIx32, specid_evcent->platform_class);
	fwts_log_info_verbatim(fw, "  specVersionMinor:          0x%" PRIx8, specid_evcent->spec_version_minor);
	fwts_log_info_verbatim(fw, "  specVersionMajor:          0x%" PRIx8, specid_evcent->platform_class);
	fwts_log_info_verbatim(fw, "  specErrata:                0x%" PRIx8, specid_evcent->spec_errata);
	fwts_log_info_verbatim(fw, "  uintnSize:                 0x%" PRIx8, specid_evcent->uintn_size);
	fwts_log_info_verbatim(fw, "  NumberOfAlgorithms:        0x%8.8" PRIx32, specid_evcent->number_of_alg);

	pdata += sizeof(fwts_efi_spec_id_event);
	len_remain -= sizeof(fwts_efi_spec_id_event);
	fwts_spec_id_event_alg_sz *alg_sz = (fwts_spec_id_event_alg_sz *)pdata;
	for (i = 0; i < specid_evcent->number_of_alg; i++) {
		/* check the data length for dumping */
		if (len_remain < sizeof(fwts_spec_id_event_alg_sz)) {
			fwts_log_info(fw, "Cannot get enough length for dumping data.");
			return 0;
		}
		str_info = tpmevlogdump_hash_to_string(alg_sz->algorithm_id);
		fwts_log_info_verbatim(fw, "  digestSizes[%d].AlgId:      0x%4.4" PRIx16 "(%s)", i, alg_sz->algorithm_id, str_info);
		fwts_log_info_verbatim(fw, "  digestSizes[%d].DigestSize: %" PRIu16, i, alg_sz->digest_size);
		pdata += sizeof(fwts_spec_id_event_alg_sz);
		len_remain -= sizeof(fwts_spec_id_event_alg_sz);
		alg_sz = (fwts_spec_id_event_alg_sz *)pdata;
	}

	uint8_t	vendor_info_size = *(uint8_t *)pdata;
	fwts_log_info_verbatim(fw, "  vendorInfoSize:            0x%" PRIx8, vendor_info_size);
	pdata += sizeof(vendor_info_size);
	len_remain -= sizeof(vendor_info_size);
	if (vendor_info_size > 0) {
		/* check the data length for dumping */
		if (len_remain < vendor_info_size) {
			fwts_log_info(fw, "Cannot get enough length for dumping data.");
			return 0;
		}
		tpmevlogdump_data_hexdump(fw, pdata, vendor_info_size, "  vendorInfo");
		len_remain -= vendor_info_size;
	}

	return len_remain;
}

static size_t tpmevlogdump_event_v2_dump(fwts_framework *fw, uint8_t *data, size_t len)
{
	uint32_t i;
	uint8_t *pdata = data;
	size_t len_remain = len;
	char *str_info;

	/* check the data length for dumping */
	if (len_remain < sizeof(fwts_tcg_pcr_event2)) {
		fwts_log_info(fw, "Cannot get enough length for dumping data.");
		return 0;
	}
	fwts_tcg_pcr_event2 *pcr_event2 = (fwts_tcg_pcr_event2 *)pdata;
	fwts_log_info_verbatim(fw, "PCRIndex:           0x%8.8" PRIx32, pcr_event2->pcr_index);
	str_info = tpmevlogdump_evtype_to_string(pcr_event2->event_type);
	fwts_log_info_verbatim(fw, "EventType:          0x%8.8" PRIx32 "(%s)", pcr_event2->event_type, str_info);
	fwts_log_info_verbatim(fw, "Digests Count :     0x%8.8" PRIx32, pcr_event2->digests_count);
	pdata += sizeof(fwts_tcg_pcr_event2);
	len_remain -= sizeof(fwts_tcg_pcr_event2);
	for (i = 0; i < pcr_event2->digests_count; i++) {

		uint8_t hash_size = 0;
		TPM2_ALG_ID alg_id = *(TPM2_ALG_ID *)pdata;

		/* check the data length for dumping */
		if (len_remain < sizeof(TPM2_ALG_ID)) {
			fwts_log_info(fw, "Cannot get enough length for dumping data.");
			return 0;
		}
		str_info = tpmevlogdump_hash_to_string(alg_id);
		fwts_log_info_verbatim(fw, "  Digests[%d].AlgId: 0x%4.4" PRIx16 "(%s)", i, alg_id, str_info);
		hash_size = tpmevlogdump_get_hash_size(alg_id);
		if (!hash_size) {
			fwts_log_info(fw, "Unknown hash algorithm. Aborted.");
			return 0;
		}
		pdata += sizeof(TPM2_ALG_ID);
		len_remain -= sizeof(TPM2_ALG_ID);
		/* check the data length for dumping */
		if (len_remain < sizeof(TPM2_ALG_ID)) {
			fwts_log_info(fw, "Cannot get enough length for dumping data.");
			return 0;
		}
		tpmevlogdump_data_hexdump(fw, pdata, hash_size, "  Digest");
		pdata += hash_size;
		len_remain -= hash_size;
	}

	uint32_t event_size = *(uint32_t *)pdata;

	/* check the data length for dumping */
	if (len_remain < event_size + sizeof(event_size)) {
		fwts_log_info(fw, "Cannot get enough length for dumping data.");
		return 0;
	}
	pdata += sizeof(event_size);
	len_remain -= sizeof(event_size);

	fwts_log_info_verbatim(fw, "  EventSize:        %" PRIu32, event_size);
	if (event_size > 0) {
		tpmevlogdump_data_hexdump(fw, pdata, event_size, "  Event");
		len_remain -= event_size;
	}

	return len_remain;
}

static void tpmevlogdump_parser(fwts_framework *fw, uint8_t *data, size_t len)
{
	size_t t_len = len;
	size_t len_remain = 0;
	uint8_t *pdata = data;

	len_remain = tpmevlogdump_specid_event_dump(fw, pdata, t_len);
	fwts_log_nl(fw);
	pdata += (t_len - len_remain);
	while (len_remain > 0) {
		t_len = len_remain;
		len_remain = tpmevlogdump_event_v2_dump(fw, pdata, t_len);
		pdata += (t_len - len_remain);
		fwts_log_nl(fw);
	}
	return;
}

static uint8_t *tpmevlogdump_load_file(const int fd, size_t *length)
{
	uint8_t *ptr = NULL, *tmp;
	size_t size = 0;
	char buffer[4096];

	*length = 0;

	for (;;) {
		ssize_t n = read(fd, buffer, sizeof(buffer));

		if (n == 0)
			break;
		if (n < 0) {
			if (errno != EINTR && errno != EAGAIN) {
				free(ptr);
				return NULL;
			}
			continue;
		}
		if (n > (ssize_t)sizeof(buffer))
			goto err;
		if (size + n > 0xffffffff)
			goto err;

		if ((tmp = (uint8_t*)realloc(ptr, size + n + 1)) == NULL) {
			free(ptr);
			return NULL;
		}
		ptr = tmp;
		memcpy(ptr + size, buffer, n);
		size += n;
	}

	if (!ptr || !size)
		goto err_no_data;

	*length = size;
	return ptr;

err:
	free(ptr);
err_no_data:
	*length = 0;
	return NULL;
}

static int tpmevlogdump_test1(fwts_framework *fw)
{
	DIR *dir;
	struct dirent *tpmdir;
	bool tpm_logfile_found = false;

	if (!(dir = opendir(FWTS_TPM_LOG_DIR_PATH))) {
		fwts_log_info(fw, "Cannot find the tpm event log. Aborted.");
		return FWTS_ABORTED;
	}

	do {
		tpmdir = readdir(dir);
		if (tpmdir && strstr(tpmdir->d_name, "tpm")) {
			char path[PATH_MAX];
			uint8_t *data;
			int fd;
			size_t length;

			fwts_log_nl(fw);
			fwts_log_info_verbatim(fw, "%s", tpmdir->d_name);

			snprintf(path, sizeof(path), FWTS_TPM_LOG_DIR_PATH "/%s/binary_bios_measurements", tpmdir->d_name);

			if ((fd = open(path, O_RDONLY)) >= 0) {
				data = tpmevlogdump_load_file(fd, &length);
				tpm_logfile_found = true;
				if (data == NULL) {
					fwts_log_info(fw, "Cannot load the tpm event logs. Aborted.");
					(void)closedir(dir);
					(void)close(fd);
					return FWTS_ABORTED;
				} else {
					/* check if the TPM2 eventlog */
					if (strstr((char *)(data + sizeof(fwts_pc_client_pcr_event)), FWTS_TPM_EVENTLOG_V2_SIGNATURE))
						tpmevlogdump_parser(fw, data, length);
					else {
						fwts_log_info(fw, "Cannot find the tpm2 event log. Aborted.");
						free(data);
						(void)closedir(dir);
						(void)close(fd);
						return FWTS_ABORTED;
					}
					free(data);
				}
				(void)close(fd);
			}
		}
	} while (tpmdir);

	(void)closedir(dir);

	if (!tpm_logfile_found) {
		fwts_log_info(fw, "Cannot find the tpm event log. Aborted.");
		return FWTS_ABORTED;
	}
	return FWTS_OK;
}

static fwts_framework_minor_test tpmevlogdump_tests[] = {
	{ tpmevlogdump_test1, "Dump Tpm2 Event Log." },
	{ NULL, NULL }
};

static fwts_framework_ops tpmevlogdump_ops = {
	.description = "Dump Tpm2 Event Log.",
	.minor_tests = tpmevlogdump_tests
};

FWTS_REGISTER("tpmevlogdump", &tpmevlogdump_ops, FWTS_TEST_ANYTIME, FWTS_FLAG_UTILS | FWTS_FLAG_ROOT_PRIV)