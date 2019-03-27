//
//  src/dir_recurse.c
//  tbd
//
//  Created by inoahdev on 12/02/18.
//  Copyright © 2018 - 2019 inoahdev. All rights reserved.
//

#include <errno.h>
#include <dirent.h>

#include <stdlib.h>
#include <string.h>

#include "dir_recurse.h"
#include "path.h"

enum dir_recurse_result
dir_recurse(const char *const path,
            const uint64_t path_length,
            const bool sub_dirs,
            void *const callback_info,
            const dir_recurse_callback callback,
            const dir_recurse_fail_callback fail_callback)
{
    DIR *const dir = opendir(path);
    if (dir == NULL) {
        return E_DIR_RECURSE_FAILED_TO_OPEN;
    }

    /*
     * Set errno to zero so we can distinguish later when readdir() has failed,
     * and when there are no more files and sub-directories left.
     */

    errno = 0;

    do {
        struct dirent *entry = readdir(dir);
        if (entry == NULL) {
            closedir(dir);

            if (errno != 0) {
                fail_callback(path,
                              path_length,
                              E_DIR_RECURSE_FAILED_TO_READ_ENTRY,
                              entry,
                              callback_info);
            }

            return E_DIR_RECURSE_OK;
        }

        const char *const name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        bool should_exit = false;
        switch (entry->d_type) {
            case DT_DIR: {
                if (!sub_dirs) {
                    continue;
                }

                const uint64_t name_length =
                    strnlen(name, sizeof(entry->d_name));

                uint64_t length = 0;
                char *const entry_path =
                    path_append_component_with_len(path,
                                                   path_length,
                                                   name,
                                                   name_length,
                                                   &length);

                if (entry_path == NULL) {
                    const bool should_continue =
                        fail_callback(entry_path,
                                      length,
                                      E_DIR_RECURSE_FAILED_TO_ALLOCATE_PATH,
                                      entry,
                                      callback_info);

                    free(entry_path);

                    if (!should_continue) {
                        should_exit = true;
                    }

                    break;
                }

                const enum dir_recurse_result recurse_subdir_result =
                    dir_recurse(entry_path,
                                length,
                                true,
                                callback_info,
                                callback,
                                fail_callback);

                switch (recurse_subdir_result) {
                    case E_DIR_RECURSE_OK:
                        break;

                    case E_DIR_RECURSE_FAILED_TO_OPEN: {
                        const bool should_continue =
                            fail_callback(entry_path,
                                          length,
                                          E_DIR_RECURSE_FAILED_TO_OPEN_SUBDIR,
                                          entry,
                                          callback_info);

                        if (!should_continue) {
                            should_exit = true;
                        }

                        break;
                    }
                }

                /*
                 * Clear errno after reading sub-directories so our readdir()
                 * calls and checks work properly.
                 *
                 * Note: For the top-level directory, which the caller asked
                 * for, we do not set errno to zero at the very end, so the
                 * caller can have, at the least, a posibility of reading errno.
                 */

                free(entry_path);
                errno = 0;

                break;
            }

            case DT_REG: {
                const uint64_t name_length =
                    strnlen(name, sizeof(entry->d_name));

                uint64_t length = 0;
                char *const entry_path =
                    path_append_component_with_len(path,
                                                   path_length,
                                                   name,
                                                   name_length,
                                                   &length);

                if (entry_path == NULL) {
                    const bool should_continue =
                        fail_callback(entry_path,
                                      length,
                                      E_DIR_RECURSE_FAILED_TO_ALLOCATE_PATH,
                                      entry,
                                      callback_info);

                    if (!should_continue) {
                        should_exit = true;
                    }

                    break;
                }

                if (!callback(entry_path, length, entry, callback_info)) {
                    should_exit = true;
                }

                free(entry_path);
                errno = 0;

                break;
            }

            default:
                continue;
        }

        if (should_exit) {
            break;
        }
    } while (true);

    closedir(dir);
    return E_DIR_RECURSE_OK;
}
