//
//  src/parse_macho_for_main.c
//  tbd
//
//  Created by inoahdev on 12/01/18.
//  Copyright © 2018 - 2019 inoahdev. All rights reserved.
//

#include <errno.h>

#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include "handle_macho_file_parse_result.h"

#include "macho_file.h"
#include "parse_macho_for_main.h"

static void
clear_create_info(struct tbd_create_info *const info_in,
                  const struct tbd_create_info *const orig)
{
    tbd_create_info_destroy(info_in);
    *info_in = *orig;
}

static int
read_magic(void *const magic_in,
           uint64_t *const magic_in_size_in,
           const int fd)
{
    const uint64_t magic_in_size = *magic_in_size_in;
    if (magic_in_size >= sizeof(uint32_t)) {
        return 0;
    }

    const uint64_t read_size = sizeof(uint32_t) - magic_in_size;
    if (read(fd, magic_in + magic_in_size, read_size) < 0) {
        return 1;
    }

    *magic_in_size_in = sizeof(uint32_t);
    return 0;
}

static void
handle_write_result(const struct tbd_for_main *const tbd,
                    const char *const path,
                    const char *const write_path,
                    const enum tbd_for_main_write_to_path_result result,
                    const bool print_paths)
{
    switch (result) {
        case E_TBD_FOR_MAIN_WRITE_TO_PATH_OK:
            break;

        case E_TBD_FOR_MAIN_WRITE_TO_PATH_ALREADY_EXISTS:
            if (tbd->flags & F_TBD_FOR_MAIN_IGNORE_WARNINGS) {
                return;
            }

            if (print_paths) {
                fprintf(stderr,
                        "Skipping over file (at path %s) as a file at its "
                        "output-path (%s) already exists\n",
                        path,
                        write_path);
            } else {
                fputs("Skipping over file at provided-path as a file at its "
                      "provided output-path already exists\n", stderr);
            }

            break;

        case E_TBD_FOR_MAIN_WRITE_TO_PATH_WRITE_FAIL:
            if (print_paths) {
                fprintf(stderr,
                        "Failed to write to output-file (at path %s)\n",
                        write_path);
            } else {
                fputs("Failed to write to provided output-file\n", stderr);
            }

            break;
    }
}

bool
parse_macho_file(void *const magic_in,
                 uint64_t *const magic_in_size_in,
                 uint64_t *const retained_info_in,
                 struct tbd_for_main *const global,
                 struct tbd_for_main *const tbd,
                 const char *const path,
                 const uint64_t path_length,
                 const int fd,
                 const bool ignore_non_macho_error,
                 const bool print_paths)
{
    if (read_magic(magic_in, magic_in_size_in, fd)) {
        if (errno == EOVERFLOW) {
            return false;
        }

        /*
         * Manually handle the read fail by passing on to
         * handle_macho_file_parse_result() as if we went to
         * macho_file_parse_from_file().
         */

        handle_macho_file_parse_result(retained_info_in,
                                       global,
                                       tbd,
                                       path,
                                       E_MACHO_FILE_PARSE_READ_FAIL,
                                       print_paths);

        return true;
    }

    const uint32_t magic = *(uint32_t *)magic_in;

    const uint64_t parse_options = tbd->parse_options;
    const uint64_t macho_options =
        O_MACHO_FILE_PARSE_IGNORE_INVALID_FIELDS | tbd->macho_options;

    struct tbd_create_info *const create_info = &tbd->info;
    struct tbd_create_info original_info = *create_info;

    const enum macho_file_parse_result parse_result =
        macho_file_parse_from_file(create_info,
                                   fd,
                                   magic,
                                   parse_options,
                                   macho_options);

    if (parse_result == E_MACHO_FILE_PARSE_NOT_A_MACHO) {
        if (!ignore_non_macho_error) {
            handle_macho_file_parse_result(retained_info_in,
                                           global,
                                           tbd,
                                           path,
                                           parse_result,
                                           print_paths);
        }

        return false;
    }

    const bool should_continue =
        handle_macho_file_parse_result(retained_info_in,
                                       global,
                                       tbd,
                                       path,
                                       parse_result,
                                       print_paths);

    if (!should_continue) {
        clear_create_info(create_info, &original_info);
        return true;
    }

    char *write_path = tbd->write_path;
    if (write_path != NULL) {
        uint64_t len = tbd->write_path_length;
        enum tbd_for_main_write_to_path_result ret =
            E_TBD_FOR_MAIN_WRITE_TO_PATH_OK;

        if (tbd->flags & F_TBD_FOR_MAIN_RECURSE_DIRECTORIES) {
            write_path =
                tbd_for_main_create_write_path(tbd,
                                               write_path,
                                               len,
                                               path,
                                               path_length,
                                               "tbd",
                                               3,
                                               true,
                                               &len);

            if (write_path == NULL) {
                fputs("Failed to allocate memory\n", stderr);
                exit(1);
            }

            ret = tbd_for_main_write_to_path(tbd, write_path, len, true);
            free(write_path);
        } else {
            ret = tbd_for_main_write_to_path(tbd, write_path, len, print_paths);
        }

        if (ret != E_TBD_FOR_MAIN_WRITE_TO_PATH_OK) {
            handle_write_result(tbd, path, write_path, ret, print_paths);
        }
    } else {
        tbd_for_main_write_to_stdout(tbd, path, true);
    }

    clear_create_info(create_info, &original_info);
    return true;
}
