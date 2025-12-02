/*
 * devoptab_macros.h
 *
 * Copyright (c) 2020-2025, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#pragma once

#ifndef __DEVOPTAB_MACROS_H__
#define __DEVOPTAB_MACROS_H__

#define DEVOPTAB_SDMC_DEVICE                "sdmc:"

#define DEVOPTAB_MOUNT_NAME_PREFIX          "ums"
#define DEVOPTAB_MOUNT_NAME_PREFIX_LENGTH   (sizeof(DEVOPTAB_MOUNT_NAME_PREFIX) - 1)
#define DEVOPTAB_MOUNT_NAME_LENGTH          32                                         // Including NULL terminator.

// Using errno automatically takes care of updating r->_errno as well.
#define DEVOPTAB_INIT_ERROR_STATE           int _errno = 0; \
                                            errno = 0
#define DEVOPTAB_SET_ERROR(x)               _errno = errno = (x)
#define DEVOPTAB_IS_ERROR_SET               (_errno != 0)

#define DEVOPTAB_DECL_LUN_FS_CTX            UsbHsFsDriveLogicalUnitFileSystemContext *lun_fs_ctx = (UsbHsFsDriveLogicalUnitFileSystemContext*)r->deviceData
#define DEVOPTAB_DECL_LUN_CTX               UsbHsFsDriveLogicalUnitContext *lun_ctx = lun_fs_ctx->lun_ctx
#define DEVOPTAB_DECL_DRIVE_CTX             UsbHsFsDriveContext *drive_ctx = lun_ctx->drive_ctx
#define DEVOPTAB_DECL_FS_CTX(type)          type *fs_ctx = (type*)lun_fs_ctx->fs_ctx
#define DEVOPTAB_DECL_FILE_STATE(type)      type *file = (type*)fd
#define DEVOPTAB_DECL_DIR_STATE(type)       type *dir = (dirState ? (type*)dirState->dirStruct : NULL)

#define DEVOPTAB_EXIT                       goto end
#define DEVOPTAB_SET_ERROR_AND_EXIT(x)      \
do { \
    DEVOPTAB_SET_ERROR((x)); \
    DEVOPTAB_EXIT; \
} while(0)

#define DEVOPTAB_RETURN_INT(x)              return (DEVOPTAB_IS_ERROR_SET ? -1 : (x))
#define DEVOPTAB_RETURN_PTR(x)              return (DEVOPTAB_IS_ERROR_SET ? NULL : (x))
#define DEVOPTAB_RETURN_BOOL                return (DEVOPTAB_IS_ERROR_SET ? false : true)
#define DEVOPTAB_RETURN_UNSUPPORTED_OP      errno = ENOSYS; \
                                            return -1

#define DEVOPTAB_INIT_VARS                  DEVOPTAB_INIT_ERROR_STATE; \
                                            DEVOPTAB_DECL_LUN_FS_CTX; \
                                            DEVOPTAB_DECL_LUN_CTX; \
                                            DEVOPTAB_DECL_DRIVE_CTX; \
                                            bool drive_ctx_valid = usbHsFsManagerIsDriveContextPointerValid(drive_ctx); \
                                            if (!drive_ctx_valid) DEVOPTAB_SET_ERROR_AND_EXIT(ENODEV)

#define DEVOPTAB_INIT_FILE_VARS(type)       DEVOPTAB_DECL_FILE_STATE(type); \
                                            DEVOPTAB_INIT_VARS; \
                                            if (!file) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL)

#define DEVOPTAB_INIT_DIR_VARS(type)        DEVOPTAB_DECL_DIR_STATE(type); \
                                            DEVOPTAB_INIT_VARS; \
                                            if (!dir) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL)

#define DEVOPTAB_DEINIT_VARS                if (drive_ctx_valid) rmutexUnlock(&(drive_ctx->rmtx))

#define DEVOPTAB_INVALID_ID                 UINT32_MAX

#endif  /* __DEVOPTAB_MACROS_H__ */
