//
//  src/macho_file_parse_load_commands.c
//  tbd
//
//  Created by inoahdev on 11/20/18.
//  Copyright © 2018 - 2019 inoahdev. All rights reserved.
//

#include <sys/types.h>
#include <errno.h>

#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include "copy.h"

#include "guard_overflow.h"
#include "objc.h"

#include "macho_file_parse_load_commands.h"
#include "macho_file_parse_symbols.h"

#include "path.h"
#include "swap.h"

#include "yaml.h"

static inline bool segment_has_image_info_sect(const char name[16]) {
    const uint64_t first = *(const uint64_t *)name;
    switch (first) {
        /*
         * case "__DATA" with 2 null characters.
         */

        case 71830128058207: {
            const uint64_t second = *((const uint64_t *)name + 1);
            if (second == 0) {
                return true;
            }

            break;
        }

        /*
         * case "__DATA_D".
         */

        case 4926728347494670175: {
            const uint64_t second = *((const uint64_t *)name + 1);
            if (second == 1498698313) {
                /*
                 * if (second == "IRTY") with 4 null characters.
                 */

                return true;
            }

            break;
        }

        /*
         * case "__DATA_C".
         */

        case 4854670753456742239: {
            const uint64_t second = *((const uint64_t *)name + 1);
            if (second == 1414745679) {
                /*
                 * if (second == "ONST") with 4 null characters.
                 */

                return true;
            }

            break;
        }

        case 73986219138911: {
            /*
             * if name == "__OBJC".
             */

            const uint64_t second = *((const uint64_t *)name + 1);
            if (second == 0) {
                return true;
            }

            break;
        }

    }

    return false;
}

static inline bool is_image_info_section(const char name[16]) {
    const uint64_t first = *(const uint64_t *)name;
    switch (first) {
        /*
         * case "__image".
         */

        case 6874014074396041055: {
            const uint64_t second = *((const uint64_t *)name + 1);
            if (second == 1868983913) {
                /*
                 * if (second == "_info") with 3 null characters.
                 */

                return true;
            }

            break;
        }

        /*
         * case "__objc_im".
         */

        case 7592896805339094879: {
            const uint64_t second = *((const uint64_t *)name + 1);
            if (second == 8027224784786383213) {
                /*
                 * if (second == "ageinfo") with 1 null character.
                 */

                return true;
            }

            break;
        }
    }

    return false;
}

static enum macho_file_parse_result
parse_section_from_file(struct tbd_create_info *const info_in,
                        uint32_t *const existing_swift_version_in,
                        const int fd,
                        const struct range full_range,
                        const struct range macho_range,
                        uint32_t sect_offset,
                        uint64_t sect_size,
                        const uint64_t options)
{
    if (sect_size != sizeof(struct objc_image_info)) {
        return E_MACHO_FILE_PARSE_INVALID_SECTION;
    }

    const struct range sect_range = {
        .begin = sect_offset,
        .end = sect_offset + sect_size
    };

    if (!range_contains_range(macho_range, sect_range)) {
        return E_MACHO_FILE_PARSE_INVALID_SECTION;
    }

    /*
     * Keep an offset of our original position, which we'll seek back to alter,
     * so we can return safetly back to the for loop.
     */

    const off_t original_pos = lseek(fd, 0, SEEK_CUR);
    if (options & O_MACHO_FILE_PARSE_SECT_OFF_ABSOLUTE) {
        if (lseek(fd, sect_offset, SEEK_SET) < 0) {
            return E_MACHO_FILE_PARSE_SEEK_FAIL;
        }
    } else {
        const off_t absolute = (off_t)(full_range.begin + sect_offset);
        if (lseek(fd, absolute, SEEK_SET) < 0) {
            return E_MACHO_FILE_PARSE_SEEK_FAIL;
        }
    }

    struct objc_image_info image_info = {};
    if (read(fd, &image_info, sizeof(image_info)) < 0) {
        return E_MACHO_FILE_PARSE_READ_FAIL;
    }

    /*
     * Seek back to our original-position to continue recursing the
     * load-commands.
     */

    if (lseek(fd, original_pos, SEEK_SET) < 0) {
        return E_MACHO_FILE_PARSE_SEEK_FAIL;
    }

    /*
     * Parse the objc-constraint and ensure it's the same as the one discovered
     * for another containers.
     */

    enum tbd_objc_constraint objc_constraint =
        TBD_OBJC_CONSTRAINT_RETAIN_RELEASE;

    if (image_info.flags & F_OBJC_IMAGE_INFO_REQUIRES_GC) {
        objc_constraint = TBD_OBJC_CONSTRAINT_GC;
    } else if (image_info.flags & F_OBJC_IMAGE_INFO_SUPPORTS_GC) {
        objc_constraint = TBD_OBJC_CONSTRAINT_RETAIN_RELEASE_OR_GC;
    } else if (image_info.flags & F_OBJC_IMAGE_INFO_IS_FOR_SIMULATOR) {
        objc_constraint = TBD_OBJC_CONSTRAINT_RETAIN_RELEASE_FOR_SIMULATOR;
    }

    const enum tbd_objc_constraint info_objc_constraint =
        info_in->objc_constraint;

    if (info_objc_constraint != 0) {
        if (info_objc_constraint != objc_constraint) {
            return E_MACHO_FILE_PARSE_CONFLICTING_OBJC_CONSTRAINT;
        }
    } else {
        info_in->objc_constraint = objc_constraint;
    }

    const uint32_t mask = objc_image_info_swift_version_mask;

    const uint32_t existing_swift_version = *existing_swift_version_in;
    const uint32_t image_swift_version = (image_info.flags & mask) >> 8;

    if (existing_swift_version != 0) {
        if (existing_swift_version != image_swift_version) {
            return E_MACHO_FILE_PARSE_CONFLICTING_SWIFT_VERSION;
        }
    } else {
        *existing_swift_version_in = image_swift_version;
    }

    return E_MACHO_FILE_PARSE_OK;
}

static enum macho_file_parse_result
add_export_to_info(struct tbd_create_info *const info_in,
                   const uint64_t arch_bit,
                   const enum tbd_export_type type,
                   const char *const string,
                   const uint32_t string_length)
{
    struct tbd_export_info export_info = {
        .archs = arch_bit,
        .archs_count = 1,
        .length = string_length,
        .string = (char *)string,
        .type = type
    };

    struct array *const exports = &info_in->exports;
    struct array_cached_index_info cached_info = {};

    struct tbd_export_info *const existing_info =
        array_find_item_in_sorted(exports,
                                  sizeof(export_info),
                                  &export_info,
                                  tbd_export_info_no_archs_comparator,
                                  &cached_info);

    if (existing_info != NULL) {
        const uint64_t archs = existing_info->archs;
        if (!(archs & arch_bit)) {
            existing_info->archs = archs | arch_bit;
            existing_info->archs_count += 1;
        }

        return E_MACHO_FILE_PARSE_OK;
    }

    /*
     * Do a quick check here to ensure that the string is a valid yaml-string
     * (with some additional boundaries).
     *
     * We do this after searching for an existing export-info, as its usually
     * not the case that a mach-o library has an invalid exported string of any
     * kind.
     */

    const bool needs_quotes = yaml_check_c_str(string, string_length);
    if (needs_quotes) {
        export_info.flags |= F_TBD_EXPORT_INFO_STRING_NEEDS_QUOTES;
    }

    /*
     * Copy the provided string as the original string comes from the large
     * load-command buffer which will soon be freed.
     */

    export_info.string = alloc_and_copy(export_info.string, export_info.length);
    if (export_info.string == NULL) {
        return E_MACHO_FILE_PARSE_ALLOC_FAIL;
    }

    const enum array_result add_export_info_result =
        array_add_item_with_cached_index_info(exports,
                                              sizeof(export_info),
                                              &export_info,
                                              &cached_info,
                                              NULL);

    if (add_export_info_result != E_ARRAY_OK) {
        free(export_info.string);
        return E_MACHO_FILE_PARSE_ARRAY_FAIL;
    }

    return E_MACHO_FILE_PARSE_OK;
}

static inline
enum macho_file_parse_result
parse_load_command(struct tbd_create_info *const info_in,
                   struct tbd_uuid_info *const uuid_info_in,
                   bool *const found_uuid_in,
                   const uint64_t arch_bit,
                   struct load_command load_cmd,
                   const uint8_t *const load_cmd_iter,
                   const bool is_big_endian,
                   const uint64_t tbd_options,
                   const uint64_t options,
                   const bool copy_strings,
                   bool *const found_identification_out,
                   struct symtab_command *const symtab_out)
{
    switch (load_cmd.cmd) {
        case LC_BUILD_VERSION: {
            /*
             * If the platform isn't needed, skip the unnecessary parsing.
             */

            if (tbd_options & O_TBD_PARSE_IGNORE_PLATFORM) {
                break;
            }

            /*
             * All build_version load-commands should be at least large enough
             * to hold a build_version_command.
             *
             * Note: build_version_command has an array of build-tool structures
             * directly following, so the cmdsize will not always match.
             */

            if (load_cmd.cmdsize < sizeof(struct build_version_command)) {
                return E_MACHO_FILE_PARSE_INVALID_LOAD_COMMAND;
            }

            /*
             * Ensure that the given platform is valid.
             */

            const struct build_version_command *const build_version =
                (const struct build_version_command *)load_cmd_iter;

            uint32_t build_version_platform = build_version->platform;
            if (is_big_endian) {
                build_version_platform = swap_uint32(build_version_platform);
            }

            if (build_version_platform < TBD_PLATFORM_MACOS) {
                /*
                 * If we're ignoring invalid fields, simply goto the next
                 * load-command.
                 */

                if (options & O_MACHO_FILE_PARSE_IGNORE_INVALID_FIELDS) {
                    break;
                }

                return E_MACHO_FILE_PARSE_INVALID_PLATFORM;
            }

            if (build_version_platform > TBD_PLATFORM_WATCHOS) {
                /*
                 * If we're ignoring invalid fields, simply goto the next
                 * load-command.
                 */

                if (options & O_MACHO_FILE_PARSE_IGNORE_INVALID_FIELDS) {
                    break;
                }

                return E_MACHO_FILE_PARSE_INVALID_PLATFORM;
            }

            /*
             * Verify that if a platform was found earlier, the platform matches
             * the given platform here.
             *
             * Note: Although this information should only be provided once, we
             * are pretty lenient, so we don't enforce this.
             */

            if (info_in->platform != 0) {
                const bool ignore_conflicting_fields =
                    options & O_MACHO_FILE_PARSE_IGNORE_CONFLICTING_FIELDS;

                if (!ignore_conflicting_fields) {
                    if (info_in->platform != build_version_platform) {
                        return E_MACHO_FILE_PARSE_CONFLICTING_PLATFORM;
                    }
                }
            } else {
                info_in->platform = build_version_platform;
            }

            break;
        }

        case LC_ID_DYLIB: {
            /*
             * We could check here if an LC_ID_DYLIB was already found for the
             * same container, but for the sake of leniency, we don't.
             */

            /*
             * If no information is needed, skip the unnecessary parsing.
             */

            if (tbd_options & O_TBD_PARSE_IGNORE_CURRENT_VERSION &&
                tbd_options & O_TBD_PARSE_IGNORE_COMPATIBILITY_VERSION &&
                tbd_options & O_TBD_PARSE_IGNORE_INSTALL_NAME)
            {
                *found_identification_out = true;
                break;
            }

            /*
             * Ensure that dylib_command can fit its basic structure and
             * information.
             *
             * An exact match cannot be made as dylib_command includes a full
             * install-name string in its cmdsize.
             */

            if (load_cmd.cmdsize < sizeof(struct dylib_command)) {
                return E_MACHO_FILE_PARSE_INVALID_LOAD_COMMAND;
            }

            const struct dylib_command *const dylib_command =
                (const struct dylib_command *)load_cmd_iter;

            uint32_t name_offset = dylib_command->dylib.name.offset;
            if (is_big_endian) {
                name_offset = swap_uint32(name_offset);
            }

            /*
             * Ensure that the install-name offset is not within the basic
             * structure, and not outside of the load-command.
             */

            if (name_offset < sizeof(struct dylib_command)) {
                if (options & O_MACHO_FILE_PARSE_IGNORE_INVALID_FIELDS) {
                    *found_identification_out = true;
                    break;
                }

                return E_MACHO_FILE_PARSE_INVALID_INSTALL_NAME;
            }

            if (name_offset >= load_cmd.cmdsize) {
                if (options & O_MACHO_FILE_PARSE_IGNORE_INVALID_FIELDS) {
                    *found_identification_out = true;
                    break;
                }

                return E_MACHO_FILE_PARSE_INVALID_INSTALL_NAME;
            }

            /*
             * The install-name is at the back of the load-command, extending
             * from the given offset to the end of the load-command.
             */

            const char *const name_ptr =
                (const char *)dylib_command + name_offset;

            const uint32_t max_length = load_cmd.cmdsize - name_offset;
            const uint32_t length = (uint32_t)strnlen(name_ptr, max_length);

            if (length == 0) {
                if (options & O_MACHO_FILE_PARSE_IGNORE_INVALID_FIELDS) {
                    *found_identification_out = true;
                    break;
                }

                return E_MACHO_FILE_PARSE_INVALID_INSTALL_NAME;
            }

            /*
             * Do a quick check here to ensure that install_name is a valid
             * yaml-string (with some additional boundaries).
             */

            const bool needs_quotes = yaml_check_c_str(name_ptr, length);
            if (needs_quotes) {
                info_in->flags |= F_TBD_CREATE_INFO_INSTALL_NAME_NEEDS_QUOTES;
            }

            /*
             * Verify that the information we have received is the same as the
             * information we may have received earlier.
             *
             * Note: Although this information should only be provided once, we
             * are pretty lenient, so we don't enforce this.
             */

            const struct dylib dylib = dylib_command->dylib;
            if (info_in->install_name != NULL) {
                const bool ignore_conflicting_fields =
                    options & O_MACHO_FILE_PARSE_IGNORE_CONFLICTING_FIELDS;

                if (ignore_conflicting_fields) {
                    *found_identification_out = true;
                    break;
                }

                if (info_in->current_version != dylib.current_version) {
                    return E_MACHO_FILE_PARSE_CONFLICTING_IDENTIFICATION;
                }

                const uint32_t compatibility_version =
                    dylib.compatibility_version;

                if (compatibility_version != dylib.compatibility_version) {
                    return E_MACHO_FILE_PARSE_CONFLICTING_IDENTIFICATION;
                }

                if (info_in->install_name_length != length) {
                    return E_MACHO_FILE_PARSE_CONFLICTING_IDENTIFICATION;
                }

                if (memcmp(info_in->install_name, name_ptr, length) != 0) {
                    return E_MACHO_FILE_PARSE_CONFLICTING_IDENTIFICATION;
                }
            } else {
                if (!(tbd_options & O_TBD_PARSE_IGNORE_CURRENT_VERSION)) {
                    info_in->current_version = dylib.current_version;
                }

                const bool ignore_compatibility_version =
                    options & O_TBD_PARSE_IGNORE_COMPATIBILITY_VERSION;

                if (!ignore_compatibility_version) {
                    info_in->compatibility_version =
                        dylib.compatibility_version;
                }

                if (!(tbd_options & O_TBD_PARSE_IGNORE_INSTALL_NAME)) {
                    if (copy_strings) {
                        char *const install_name =
                            alloc_and_copy(name_ptr, length);

                        if (install_name == NULL) {
                            return E_MACHO_FILE_PARSE_ALLOC_FAIL;
                        }

                        info_in->install_name = install_name;
                    } else {
                        info_in->install_name = name_ptr;
                    }

                    info_in->install_name_length = length;
                }
            }

            *found_identification_out = true;
            break;
        }

        case LC_REEXPORT_DYLIB: {
            if (tbd_options & O_TBD_PARSE_IGNORE_REEXPORTS) {
                break;
            }

            /*
             * Ensure that dylib_command can fit its basic structure and
             * information.
             *
             * An exact match cannot be made as dylib_command includes the full
             * re-export string in its cmdsize.
             */

            if (load_cmd.cmdsize < sizeof(struct dylib_command)) {
                return E_MACHO_FILE_PARSE_INVALID_LOAD_COMMAND;
            }

            const struct dylib_command *const reexport_dylib =
                (const struct dylib_command *)load_cmd_iter;

            uint32_t reexport_offset = reexport_dylib->dylib.name.offset;
            if (is_big_endian) {
                reexport_offset = swap_uint32(reexport_offset);
            }

            /*
             * Ensure that the reexport-string is not within the basic
             * structure, and not outside of the load-command itself.
             */

            if (reexport_offset < sizeof(struct dylib_command)) {
                return E_MACHO_FILE_PARSE_INVALID_REEXPORT;
            }

            if (reexport_offset >= load_cmd.cmdsize) {
                return E_MACHO_FILE_PARSE_INVALID_REEXPORT;
            }

            const char *const reexport_string =
                (const char *)reexport_dylib + reexport_offset;

            const uint32_t max_length = load_cmd.cmdsize - reexport_offset;
            const uint32_t length =
                (uint32_t)strnlen(reexport_string, max_length);

            if (length == 0) {
                if (options & O_MACHO_FILE_PARSE_IGNORE_INVALID_FIELDS) {
                    break;
                }

                return E_MACHO_FILE_PARSE_INVALID_REEXPORT;
            }

            const enum macho_file_parse_result add_reexport_result =
                add_export_to_info(info_in,
                                   arch_bit,
                                   TBD_EXPORT_TYPE_REEXPORT,
                                   reexport_string,
                                   length);

            if (add_reexport_result != E_MACHO_FILE_PARSE_OK) {
                return add_reexport_result;
            }

            break;
        }

        case LC_SUB_CLIENT: {
            /*
             * If no sub-clients are needed, skip the unnecessary parsing.
             */

            if (tbd_options & O_TBD_PARSE_IGNORE_CLIENTS) {
                break;
            }

            /*
             * Ensure that sub_client_command can fit its basic structure
             * and information.
             *
             * An exact match cannot be made as sub_client_command includes a
             * full client-string in its cmdsize.
             */

            if (load_cmd.cmdsize < sizeof(struct sub_client_command)) {
                return E_MACHO_FILE_PARSE_INVALID_LOAD_COMMAND;
            }

            /*
             * Ensure that the client is located fully within the load-command
             * and after the basic information of a sub_client_command
             * load-command structure.
             */

            const struct sub_client_command *const client_command =
                (const struct sub_client_command *)load_cmd_iter;

            uint32_t client_offset = client_command->client.offset;
            if (is_big_endian) {
                client_offset = swap_uint32(client_offset);
            }

            /*
             * Ensure that the client-string offset is not within the basic
             * structure, and not outside of the load-command.
             */

            if (client_offset < sizeof(struct sub_client_command)) {
                return E_MACHO_FILE_PARSE_INVALID_CLIENT;
            }

            if (client_offset >= load_cmd.cmdsize) {
                return E_MACHO_FILE_PARSE_INVALID_CLIENT;
            }

            const char *const string =
                (const char *)client_command + client_offset;

            const uint32_t max_length = load_cmd.cmdsize - client_offset;
            const uint32_t length = (uint32_t)strnlen(string, max_length);

            if (length == 0) {
                if (options & O_MACHO_FILE_PARSE_IGNORE_INVALID_FIELDS) {
                    break;
                }

                return E_MACHO_FILE_PARSE_INVALID_CLIENT;
            }

            const enum macho_file_parse_result add_client_result =
                add_export_to_info(info_in,
                                   arch_bit,
                                   TBD_EXPORT_TYPE_CLIENT,
                                   string,
                                   length);

            if (add_client_result != E_MACHO_FILE_PARSE_OK) {
                return add_client_result;
            }

            break;
        }

        case LC_SUB_FRAMEWORK: {
            /*
             * If no sub-umbrella are needed, skip the unnecessary parsing.
             */

            if (tbd_options & O_TBD_PARSE_IGNORE_PARENT_UMBRELLA) {
                break;
            }

            /*
             * Ensure that sub_framework_command can fit its basic structure and
             * information.
             *
             * An exact match cannot be made as sub_framework_command includes a
             * full umbrella-string in its cmdsize.
             */

            if (load_cmd.cmdsize < sizeof(struct sub_framework_command)) {
                return E_MACHO_FILE_PARSE_INVALID_LOAD_COMMAND;
            }

            const struct sub_framework_command *const framework_command =
                (const struct sub_framework_command *)load_cmd_iter;

            uint32_t umbrella_offset = framework_command->umbrella.offset;
            if (is_big_endian) {
                umbrella_offset = swap_uint32(umbrella_offset);
            }

            /*
             * Ensure that the umbrella-string offset is not within the basic
             * structure, and not outside of the load-command.
             */

            if (umbrella_offset < sizeof(struct sub_framework_command)) {
                if (options & O_MACHO_FILE_PARSE_IGNORE_INVALID_FIELDS) {
                    break;
                }

                return E_MACHO_FILE_PARSE_INVALID_PARENT_UMBRELLA;
            }

            if (umbrella_offset >= load_cmd.cmdsize) {
                if (options & O_MACHO_FILE_PARSE_IGNORE_INVALID_FIELDS) {
                    break;
                }

                return E_MACHO_FILE_PARSE_INVALID_PARENT_UMBRELLA;
            }

            /*
             * The umbrella-string is at the back of the load-command, extending
             * from the given offset to the end of the load-command.
             */

            const char *const umbrella =
                (const char *)load_cmd_iter + umbrella_offset;

            const uint32_t max_length = load_cmd.cmdsize - umbrella_offset;
            const uint32_t length = (uint32_t)strnlen(umbrella, max_length);

            if (length == 0) {
                if (options & O_MACHO_FILE_PARSE_IGNORE_INVALID_FIELDS) {
                    break;
                }

                return E_MACHO_FILE_PARSE_INVALID_PARENT_UMBRELLA;
            }

            /*
             * Do a quick check here to ensure that parent-umbrella is a valid
             * yaml-string (with some additional boundaries).
             */

            const bool needs_quotes = yaml_check_c_str(umbrella, length);
            if (needs_quotes) {
                info_in->flags |=
                    F_TBD_CREATE_INFO_PARENT_UMBRELLA_NEEDS_QUOTES;
            }

            if (info_in->parent_umbrella != NULL) {
                const bool ignore_conflicting_fields =
                    options & O_MACHO_FILE_PARSE_IGNORE_CONFLICTING_FIELDS;

                if (ignore_conflicting_fields) {
                    break;
                }

                if (info_in->parent_umbrella_length != length) {
                    return E_MACHO_FILE_PARSE_CONFLICTING_PARENT_UMBRELLA;
                }

                const char *const parent_umbrella = info_in->parent_umbrella;
                if (memcmp(parent_umbrella, umbrella, length) != 0) {
                    return E_MACHO_FILE_PARSE_CONFLICTING_PARENT_UMBRELLA;
                }
            } else {
                if (copy_strings) {
                    char *const umbrella_string =
                        alloc_and_copy(umbrella, length);

                    if (umbrella_string == NULL) {
                        return E_MACHO_FILE_PARSE_ALLOC_FAIL;
                    }

                    info_in->parent_umbrella = umbrella_string;
                } else {
                    info_in->parent_umbrella = umbrella;
                }

                info_in->parent_umbrella_length = length;
            }

            break;
        }

        case LC_SYMTAB: {
            /*
             * If symbols aren't needed, skip the unnecessary parsing.
             */

            if (tbd_options & O_TBD_PARSE_IGNORE_SYMBOLS) {
                break;
            }

            /*
             * All symtab load-commands should be of the same size.
             */

            if (load_cmd.cmdsize != sizeof(struct symtab_command)) {
                return E_MACHO_FILE_PARSE_INVALID_SYMBOL_TABLE;
            }

            /*
             * Fill in the symtab's load-command info to mark that we did indeed
             * fill in the symtab's info fields.
             */

            *symtab_out = *(const struct symtab_command *)load_cmd_iter;
            break;
        }

        case LC_UUID: {
            /*
             * If uuids aren't needed, skip the unnecessary parsing.
             */

            if (tbd_options & O_TBD_PARSE_IGNORE_UUID) {
                break;
            }

            if (load_cmd.cmdsize != sizeof(struct uuid_command)) {
                return E_MACHO_FILE_PARSE_INVALID_UUID;
            }

            const bool found_uuid = *found_uuid_in;
            const struct uuid_command *const uuid_cmd =
                (const struct uuid_command *)load_cmd_iter;

            if (found_uuid) {
                const char *const uuid_str = (const char *)uuid_info_in->uuid;
                const char *const uuid_cmd_uuid = (const char *)uuid_cmd->uuid;

                const bool ignore_conflicting_fields =
                    options & O_MACHO_FILE_PARSE_IGNORE_CONFLICTING_FIELDS;

                if (!ignore_conflicting_fields) {
                    if (memcmp(uuid_str, uuid_cmd_uuid, 16) != 0) {
                        return E_MACHO_FILE_PARSE_CONFLICTING_UUID;
                    }
                }
            } else {
                memcpy(uuid_info_in->uuid, uuid_cmd->uuid, 16);
                *found_uuid_in = true;
            }

            break;
        }

        case LC_VERSION_MIN_MACOSX: {
            /*
             * If the platform isn't needed, skip the unnecessary parsing.
             */

            if (tbd_options & O_TBD_PARSE_IGNORE_PLATFORM) {
                break;
            }

            /*
             * All version_min load-commands should be the of the same cmdsize.
             */

            if (load_cmd.cmdsize != sizeof(struct version_min_command)) {
                return E_MACHO_FILE_PARSE_INVALID_LOAD_COMMAND;
            }

            if (info_in->platform != 0) {
                const bool ignore_conflicting_fields =
                    options & O_MACHO_FILE_PARSE_IGNORE_CONFLICTING_FIELDS;

                if (!ignore_conflicting_fields) {
                    if (info_in->platform != TBD_PLATFORM_MACOS) {
                        return E_MACHO_FILE_PARSE_CONFLICTING_PLATFORM;
                    }
                }
            } else {
                info_in->platform = TBD_PLATFORM_MACOS;
            }

            break;
        }

        case LC_VERSION_MIN_IPHONEOS: {
            /*
             * If the platform isn't needed, skip the unnecessary parsing.
             */

            if (tbd_options & O_TBD_PARSE_IGNORE_PLATFORM) {
                break;
            }

            /*
             * All version_min load-commands should be the of the same cmdsize.
             */

            if (load_cmd.cmdsize != sizeof(struct version_min_command)) {
                return E_MACHO_FILE_PARSE_INVALID_LOAD_COMMAND;
            }

            if (info_in->platform != 0) {
                const bool ignore_conflicting_fields =
                    options & O_MACHO_FILE_PARSE_IGNORE_CONFLICTING_FIELDS;

                if (!ignore_conflicting_fields) {
                    if (info_in->platform != TBD_PLATFORM_IOS) {
                        return E_MACHO_FILE_PARSE_CONFLICTING_PLATFORM;
                    }
                }
            } else {
                info_in->platform = TBD_PLATFORM_IOS;
            }

            break;
        }

        case LC_VERSION_MIN_WATCHOS: {
            /*
             * If the platform isn't needed, skip the unnecessary parsing.
             */

            if (tbd_options & O_TBD_PARSE_IGNORE_PLATFORM) {
                break;
            }

            /*
             * All version_min load-commands should be the of the same cmdsize.
             */

            if (load_cmd.cmdsize != sizeof(struct version_min_command)) {
                return E_MACHO_FILE_PARSE_INVALID_LOAD_COMMAND;
            }

            if (info_in->platform != 0) {
                const bool ignore_conflicting_fields =
                    options & O_MACHO_FILE_PARSE_IGNORE_CONFLICTING_FIELDS;

                if (!ignore_conflicting_fields) {
                    if (info_in->platform != TBD_PLATFORM_WATCHOS) {
                        return E_MACHO_FILE_PARSE_CONFLICTING_PLATFORM;
                    }
                }
            } else {
                info_in->platform = TBD_PLATFORM_WATCHOS;
            }

            break;
        }

        case LC_VERSION_MIN_TVOS: {
            /*
             * If the platform isn't needed, skip the unnecessary parsing.
             */

            if (tbd_options & O_TBD_PARSE_IGNORE_PLATFORM) {
                break;
            }

            /*
             * All version_min load-commands should be the of the same cmdsize.
             */

            if (load_cmd.cmdsize != sizeof(struct version_min_command)) {
                return E_MACHO_FILE_PARSE_INVALID_LOAD_COMMAND;
            }

            if (info_in->platform != 0) {
                const bool ignore_conflicting_fields =
                    options & O_MACHO_FILE_PARSE_IGNORE_CONFLICTING_FIELDS;

                if (!ignore_conflicting_fields) {
                    if (info_in->platform != TBD_PLATFORM_TVOS) {
                        return E_MACHO_FILE_PARSE_CONFLICTING_PLATFORM;
                    }
                }
            } else {
                info_in->platform = TBD_PLATFORM_TVOS;
            }

            break;
        }

        default:
            break;
    }

    return E_MACHO_FILE_PARSE_OK;
}

enum macho_file_parse_result
macho_file_parse_load_commands_from_file(
    struct tbd_create_info *const info_in,
    const struct mf_parse_load_commands_from_file_info *const parse_info,
    struct symtab_command *const symtab_out)
{
    const uint32_t ncmds = parse_info->ncmds;
    if (ncmds == 0) {
        return E_MACHO_FILE_PARSE_NO_LOAD_COMMANDS;
    }

    /*
     * Verify the size and integrity of the load-commands.
     */

    const uint32_t sizeofcmds = parse_info->sizeofcmds;
    if (sizeofcmds < sizeof(struct load_command)) {
        return E_MACHO_FILE_PARSE_LOAD_COMMANDS_AREA_TOO_SMALL;
    }

    /*
     * Get the minimum size by multiplying the ncmds and
     * sizeof(struct load_command).
     *
     * Since sizeof(struct load_command) is a power of 2 (8), use a shift by 3
     * instead.
     */

    uint32_t minimum_size = sizeof(struct load_command);
    if (guard_overflow_mul(&minimum_size, ncmds)) {
        return E_MACHO_FILE_PARSE_TOO_MANY_LOAD_COMMANDS;
    }

    if (sizeofcmds < minimum_size) {
        return E_MACHO_FILE_PARSE_TOO_MANY_LOAD_COMMANDS;
    }

    /*
     * Ensure that sizeofcmds doesn't go past mach-o's size.
     */

    const struct range full_range = parse_info->full_range;
    const struct range available_range = parse_info->available_range;

    const uint64_t macho_size = full_range.end - full_range.begin;
    const uint32_t max_sizeofcmds =
        (uint32_t)(available_range.end - available_range.begin);

    if (sizeofcmds > max_sizeofcmds) {
        return E_MACHO_FILE_PARSE_TOO_MANY_LOAD_COMMANDS;
    }

    const uint64_t header_size = available_range.begin - full_range.begin;
    const struct range relative_range = {
        .begin = header_size,
        .end = macho_size
    };

    bool found_identification = false;
    bool found_uuid = false;

    struct symtab_command symtab = {};
    struct tbd_uuid_info uuid_info = { .arch = parse_info->arch };

    /*
     * Allocate the entire load-commands buffer to allow fast parsing.
     */

    uint8_t *const load_cmd_buffer = malloc(sizeofcmds);
    if (load_cmd_buffer == NULL) {
        return E_MACHO_FILE_PARSE_ALLOC_FAIL;
    }

    const int fd = parse_info->fd;
    if (read(fd, load_cmd_buffer, sizeofcmds) < 0) {
        free(load_cmd_buffer);
        return E_MACHO_FILE_PARSE_READ_FAIL;
    }

    info_in->flags |= F_TBD_CREATE_INFO_STRINGS_WERE_COPIED;

    /*
     * Iterate over the load-commands, which is located directly after the
     * mach-o header.
     */

    const bool is_64 = parse_info->is_64;
    const bool is_big_endian = parse_info->is_big_endian;

    const uint64_t arch_bit = parse_info->arch_bit;
    const uint64_t options = parse_info->options;
    const uint64_t tbd_options = parse_info->tbd_options;

    uint8_t *load_cmd_iter = load_cmd_buffer;
    uint32_t size_left = sizeofcmds;

    for (uint32_t i = 0; i != ncmds; i++) {
        /*
         * Verify that we still have space for a load-command.
         */

        if (size_left < sizeof(struct load_command)) {
            free(load_cmd_buffer);
            return E_MACHO_FILE_PARSE_INVALID_LOAD_COMMAND;
        }

        /*
         * Big-endian mach-o files have load-commands whose information is also
         * big-endian.
         */

        struct load_command load_cmd = *(struct load_command *)load_cmd_iter;
        if (is_big_endian) {
            load_cmd.cmd = swap_uint32(load_cmd.cmd);
            load_cmd.cmdsize = swap_uint32(load_cmd.cmdsize);
        }

        /*
         * Verify the cmdsize by checking that a load-cmd can actually fit.
         * More verification can be done here, but we don't aim to be too picky.
         */

        if (load_cmd.cmdsize < sizeof(struct load_command)) {
            free(load_cmd_buffer);
            return E_MACHO_FILE_PARSE_INVALID_LOAD_COMMAND;
        }

        if (size_left < load_cmd.cmdsize) {
            free(load_cmd_buffer);
            return E_MACHO_FILE_PARSE_INVALID_LOAD_COMMAND;
        }

        size_left -= load_cmd.cmdsize;

        /*
         * We can't check size_left here, as this could be the last
         * load-command, so we have to check at the very beginning of the loop.
         */

        switch (load_cmd.cmd) {
            case LC_SEGMENT: {
                /*
                 * If no information from a segment is needed, skip the
                 * unnecessary parsing.
                 */

                if ((tbd_options & O_TBD_PARSE_IGNORE_OBJC_CONSTRAINT) &&
                    (tbd_options & O_TBD_PARSE_IGNORE_SWIFT_VERSION))
                {
                    break;
                }

                /*
                 * Verify we have the right segment for the right word-size
                 * (32-bit vs 64-bit).
                 *
                 * We just ignore this as having the wrong segment-type doesn't
                 * matter for us.
                 */

                if (is_64) {
                    break;
                }

                if (load_cmd.cmdsize < sizeof(struct segment_command)) {
                    free(load_cmd_buffer);
                    return E_MACHO_FILE_PARSE_INVALID_LOAD_COMMAND;
                }

                const struct segment_command *const segment =
                    (const struct segment_command *)load_cmd_iter;

                if (!segment_has_image_info_sect(segment->segname)) {
                    break;
                }

                uint32_t nsects = segment->nsects;
                if (nsects == 0) {
                    break;
                }

                if (is_big_endian) {
                    nsects = swap_uint32(nsects);
                }

                /*
                 * Verify the size and integrity of the sections.
                 */

                uint64_t sections_size = sizeof(struct section);
                if (guard_overflow_mul(&sections_size, nsects)) {
                    free(load_cmd_buffer);
                    return E_MACHO_FILE_PARSE_TOO_MANY_SECTIONS;
                }

                const uint32_t max_sections_size =
                    load_cmd.cmdsize - sizeof(struct segment_command);

                if (sections_size > max_sections_size) {
                    free(load_cmd_buffer);
                    return E_MACHO_FILE_PARSE_TOO_MANY_SECTIONS;
                }

                uint32_t swift_version = 0;
                const uint8_t *const sect_ptr =
                    load_cmd_iter + sizeof(struct segment_command);

                const struct section *sect = (const struct section *)sect_ptr;
                for (uint32_t j = 0; j < nsects; j++, sect++) {
                    if (!is_image_info_section(sect->sectname)) {
                        continue;
                    }

                    uint32_t sect_offset = sect->offset;
                    uint32_t sect_size = sect->size;

                    if (is_big_endian) {
                        sect_offset = swap_uint32(sect_offset);
                        sect_size = swap_uint32(sect_size);
                    }

                    const enum macho_file_parse_result parse_section_result =
                        parse_section_from_file(info_in,
                                                &swift_version,
                                                fd,
                                                full_range,
                                                relative_range,
                                                sect_offset,
                                                sect_size,
                                                options);

                    if (parse_section_result != E_MACHO_FILE_PARSE_OK) {
                        free(load_cmd_buffer);
                        return parse_section_result;
                    }
                }

                if (info_in->swift_version != 0) {
                    const bool ignore_conflicting_fields =
                        options & O_MACHO_FILE_PARSE_IGNORE_CONFLICTING_FIELDS;

                    if (!ignore_conflicting_fields) {
                        if (info_in->swift_version != swift_version) {
                            free(load_cmd_buffer);
                            return E_MACHO_FILE_PARSE_CONFLICTING_SWIFT_VERSION;
                        }
                    }
                } else {
                    info_in->swift_version = swift_version;
                }

                break;
            }

            case LC_SEGMENT_64: {
                /*
                 * If no information from a segment is needed, skip the
                 * unnecessary parsing.
                 */

                if ((tbd_options & O_TBD_PARSE_IGNORE_OBJC_CONSTRAINT) &&
                    (tbd_options & O_TBD_PARSE_IGNORE_SWIFT_VERSION))
                {
                    break;
                }

                /*
                 * Verify we have the right segment for the right word-size
                 * (32-bit vs 64-bit).
                 *
                 * We just ignore this as having the wrong segment-type doesn't
                 * matter for us.
                 */

                if (!is_64) {
                    break;
                }

                if (load_cmd.cmdsize < sizeof(struct segment_command_64)) {
                    free(load_cmd_buffer);
                    return E_MACHO_FILE_PARSE_INVALID_LOAD_COMMAND;
                }

                const struct segment_command_64 *const segment =
                    (const struct segment_command_64 *)load_cmd_iter;

                if (!segment_has_image_info_sect(segment->segname)) {
                    break;
                }

                uint32_t nsects = segment->nsects;
                if (nsects == 0) {
                    break;
                }

                if (is_big_endian) {
                    nsects = swap_uint32(nsects);
                }

                /*
                 * Verify the size and integrity of the sections.
                 */

                uint64_t sections_size = sizeof(struct section_64);
                if (guard_overflow_mul(&sections_size, nsects)) {
                    free(load_cmd_buffer);
                    return E_MACHO_FILE_PARSE_TOO_MANY_SECTIONS;
                }

                const uint32_t max_sections_size =
                    load_cmd.cmdsize - sizeof(struct segment_command_64);

                if (sections_size > max_sections_size) {
                    free(load_cmd_buffer);
                    return E_MACHO_FILE_PARSE_TOO_MANY_SECTIONS;
                }

                uint32_t swift_version = 0;
                const uint8_t *const sect_ptr =
                    load_cmd_iter + sizeof(struct segment_command_64);

                const struct section_64 *sect =
                    (const struct section_64 *)sect_ptr;

                for (uint32_t j = 0; j < nsects; j++, sect++) {
                    if (!is_image_info_section(sect->sectname)) {
                        continue;
                    }

                    uint32_t sect_offset = sect->offset;
                    uint64_t sect_size = sect->size;

                    if (is_big_endian) {
                        sect_offset = swap_uint32(sect_offset);
                        sect_size = swap_uint64(sect_size);
                    }

                    const enum macho_file_parse_result parse_section_result =
                        parse_section_from_file(info_in,
                                                &swift_version,
                                                fd,
                                                full_range,
                                                relative_range,
                                                sect_offset,
                                                sect_size,
                                                options);

                    if (parse_section_result != E_MACHO_FILE_PARSE_OK) {
                        free(load_cmd_buffer);
                        return parse_section_result;
                    }
                }

                if (info_in->swift_version != 0) {
                    const bool ignore_conflicting_fields =
                        options & O_MACHO_FILE_PARSE_IGNORE_CONFLICTING_FIELDS;

                    if (!ignore_conflicting_fields) {
                        if (info_in->swift_version != swift_version) {
                            free(load_cmd_buffer);
                            return E_MACHO_FILE_PARSE_CONFLICTING_SWIFT_VERSION;
                        }
                    }
                } else {
                    info_in->swift_version = swift_version;
                }

                break;
            }

            default: {
                const enum macho_file_parse_result parse_load_command_result =
                    parse_load_command(info_in,
                                       &uuid_info,
                                       &found_uuid,
                                       arch_bit,
                                       load_cmd,
                                       load_cmd_iter,
                                       is_big_endian,
                                       tbd_options,
                                       options,
                                       true,
                                       &found_identification,
                                       &symtab);

                if (parse_load_command_result != E_MACHO_FILE_PARSE_OK) {
                    free(load_cmd_buffer);
                    return parse_load_command_result;
                }

                break;
            }
        }

        load_cmd_iter += load_cmd.cmdsize;
    }

    free(load_cmd_buffer);
    if (!found_identification) {
        return E_MACHO_FILE_PARSE_NO_IDENTIFICATION;
    }

    /*
     * Ensure that the uuid found is unique among all other containers before
     * adding to the fd's uuid arrays.
     */

    const uint8_t *const array_uuid =
        array_find_item(&info_in->uuids,
                        sizeof(uuid_info),
                        &uuid_info,
                        tbd_uuid_info_comparator,
                        NULL);

    if (array_uuid != NULL) {
        return E_MACHO_FILE_PARSE_CONFLICTING_UUID;
    }

    const enum array_result add_uuid_info_result =
        array_add_item(&info_in->uuids, sizeof(uuid_info), &uuid_info, NULL);

    if (add_uuid_info_result != E_ARRAY_OK) {
        return E_MACHO_FILE_PARSE_ARRAY_FAIL;
    }

    if (!(tbd_options & O_TBD_PARSE_IGNORE_PLATFORM)) {
        if (info_in->platform == 0) {
            return E_MACHO_FILE_PARSE_NO_PLATFORM;
        }
    }

    if (!(tbd_options & O_TBD_PARSE_IGNORE_SYMBOLS)) {
        if (symtab.cmd != LC_SYMTAB) {
            return E_MACHO_FILE_PARSE_NO_SYMBOL_TABLE;
        }
    }

    if (!(tbd_options & O_TBD_PARSE_IGNORE_UUID)) {
        if (!found_uuid) {
            return E_MACHO_FILE_PARSE_NO_UUID;
        }
    }

    /*
     * Retrieve the symbol-table and string-table info via the symtab_command.
     */

    if (is_big_endian) {
        symtab.symoff = swap_uint32(symtab.symoff);
        symtab.nsyms = swap_uint32(symtab.nsyms);

        symtab.stroff = swap_uint32(symtab.stroff);
        symtab.strsize = swap_uint32(symtab.strsize);
    }

    if (symtab_out != NULL) {
        *symtab_out = symtab;
    }

    if (options & O_MACHO_FILE_PARSE_DONT_PARSE_SYMBOL_TABLE) {
        return E_MACHO_FILE_PARSE_OK;
    }

    /*
     * Verify the symbol-table's information.
     */

    enum macho_file_parse_result ret = E_MACHO_FILE_PARSE_OK;
    if (is_64) {
        ret =
            macho_file_parse_symbols_64_from_file(info_in,
                                                  fd,
                                                  full_range,
                                                  available_range,
                                                  arch_bit,
                                                  is_big_endian,
                                                  symtab.symoff,
                                                  symtab.nsyms,
                                                  symtab.stroff,
                                                  symtab.strsize,
                                                  tbd_options);
    } else {
        ret =
            macho_file_parse_symbols_from_file(info_in,
                                               fd,
                                               full_range,
                                               available_range,
                                               arch_bit,
                                               is_big_endian,
                                               symtab.symoff,
                                               symtab.nsyms,
                                               symtab.stroff,
                                               symtab.strsize,
                                               tbd_options);
    }

    if (ret != E_MACHO_FILE_PARSE_OK) {
        return ret;
    }

    return E_MACHO_FILE_PARSE_OK;
}

static enum macho_file_parse_result
parse_section_from_map(struct tbd_create_info *const info_in,
                       uint32_t *const existing_swift_version_in,
                       const struct range map_range,
                       const struct range macho_range,
                       const uint8_t *const map,
                       const uint8_t *const macho,
                       uint32_t sect_offset,
                       uint64_t sect_size,
                       const uint64_t options)
{
    if (sect_size != sizeof(struct objc_image_info)) {
        return E_MACHO_FILE_PARSE_INVALID_SECTION;
    }

    const struct objc_image_info *image_info = NULL;
    const struct range sect_range = {
        .begin = sect_offset,
        .end = sect_offset + sect_size
    };

    if (options & O_MACHO_FILE_PARSE_SECT_OFF_ABSOLUTE) {
        if (!range_contains_range(map_range, sect_range)) {
            return E_MACHO_FILE_PARSE_INVALID_SECTION;
        }

        const void *const iter = map + sect_offset;
        image_info = (const struct objc_image_info *)iter;
    } else {
        if (!range_contains_range(macho_range, sect_range)) {
            return E_MACHO_FILE_PARSE_INVALID_SECTION;
        }

        const void *const iter = macho + sect_offset;
        image_info = (const struct objc_image_info *)iter;
    }

    /*
     * Parse the objc-constraint and ensure it's the same as the one discovered
     * for another containers.
     */

    const uint32_t flags = image_info->flags;
    enum tbd_objc_constraint objc_constraint =
        TBD_OBJC_CONSTRAINT_RETAIN_RELEASE;

    if (flags & F_OBJC_IMAGE_INFO_REQUIRES_GC) {
        objc_constraint = TBD_OBJC_CONSTRAINT_GC;
    } else if (flags & F_OBJC_IMAGE_INFO_SUPPORTS_GC) {
        objc_constraint = TBD_OBJC_CONSTRAINT_RETAIN_RELEASE_OR_GC;
    } else if (flags & F_OBJC_IMAGE_INFO_IS_FOR_SIMULATOR) {
        objc_constraint = TBD_OBJC_CONSTRAINT_RETAIN_RELEASE_FOR_SIMULATOR;
    }

    const enum tbd_objc_constraint info_objc_constraint =
        info_in->objc_constraint;

    if (info_objc_constraint != 0) {
        if (info_objc_constraint != objc_constraint) {
            return E_MACHO_FILE_PARSE_CONFLICTING_OBJC_CONSTRAINT;
        }
    } else {
        info_in->objc_constraint = objc_constraint;
    }

    const uint32_t mask = objc_image_info_swift_version_mask;

    const uint32_t existing_swift_version = *existing_swift_version_in;
    const uint32_t image_swift_version = (flags & mask) >> 8;

    if (existing_swift_version != 0) {
        if (existing_swift_version != image_swift_version) {
            return E_MACHO_FILE_PARSE_CONFLICTING_SWIFT_VERSION;
        }
    } else {
        *existing_swift_version_in = image_swift_version;
    }

    return E_MACHO_FILE_PARSE_OK;
}

enum macho_file_parse_result
macho_file_parse_load_commands_from_map(
    struct tbd_create_info *const info_in,
    const struct mf_parse_load_commands_from_map_info *const parse_info,
    struct symtab_command *const symtab_out)
{
    const uint32_t ncmds = parse_info->ncmds;
    if (ncmds == 0) {
        return E_MACHO_FILE_PARSE_NO_LOAD_COMMANDS;
    }

    /*
     * Verify the size and integrity of the load-commands.
     */

    const uint32_t sizeofcmds = parse_info->sizeofcmds;
    if (sizeofcmds < sizeof(struct load_command)) {
        return E_MACHO_FILE_PARSE_LOAD_COMMANDS_AREA_TOO_SMALL;
    }

    /*
     * Get the minimum size by multiplying the ncmds and
     * sizeof(struct load_command).
     */

    uint32_t minimum_size = sizeof(struct load_command);
    if (guard_overflow_mul(&minimum_size, ncmds)) {
        return E_MACHO_FILE_PARSE_TOO_MANY_LOAD_COMMANDS;
    }

    if (sizeofcmds < minimum_size) {
        return E_MACHO_FILE_PARSE_TOO_MANY_LOAD_COMMANDS;
    }

    /*
     * Ensure that sizeofcmds doesn't go beyond end of mach-o.
     */

    const bool is_64 = parse_info->is_64;
    uint32_t header_size = sizeof(struct mach_header);

    if (is_64) {
        header_size += sizeof(uint32_t);
    }

    const uint64_t macho_size = parse_info->macho_size;
    const uint32_t max_sizeofcmds = (uint32_t)(macho_size - header_size);

    if (sizeofcmds > max_sizeofcmds) {
        return E_MACHO_FILE_PARSE_TOO_MANY_LOAD_COMMANDS;
    }

    const struct range relative_range = {
        .begin = 0,
        .end = macho_size
    };

    bool found_identification = false;
    bool found_uuid = false;

    struct symtab_command symtab = {};
    struct tbd_uuid_info uuid_info = { .arch = parse_info->arch };

    /*
     * Allocate the entire load-commands buffer to allow fast parsing.
     */

    const uint8_t *const macho = parse_info->macho;

    const uint8_t *load_cmd_iter = macho + header_size;
    const uint64_t options = parse_info->options;

    if (options & O_MACHO_FILE_PARSE_COPY_STRINGS_IN_MAP) {
        info_in->flags |= F_TBD_CREATE_INFO_STRINGS_WERE_COPIED;
    }

    const bool is_big_endian = parse_info->is_big_endian;
    const uint64_t tbd_options = parse_info->tbd_options;

    const uint8_t *const map = parse_info->map;
    const uint64_t arch_bit = parse_info->arch_bit;

    const struct range available_map_range = parse_info->available_map_range;
    uint32_t size_left = sizeofcmds;

    for (uint32_t i = 0; i != ncmds; i++) {
        /*
         * Verify that we still have space for a load-command.
         */

        if (size_left < sizeof(struct load_command)) {
            return E_MACHO_FILE_PARSE_INVALID_LOAD_COMMAND;
        }

        /*
         * Big-endian mach-o files have load-commands whose information is also
         * big-endian.
         */

        struct load_command load_cmd =
            *(const struct load_command *)load_cmd_iter;

        if (is_big_endian) {
            load_cmd.cmd = swap_uint32(load_cmd.cmd);
            load_cmd.cmdsize = swap_uint32(load_cmd.cmdsize);
        }

        /*
         * Verify the cmdsize by checking that a load-cmd can actually fit.
         * More verification can be done here, but we don't aim to be too picky.
         */

        if (load_cmd.cmdsize < sizeof(struct load_command)) {
            return E_MACHO_FILE_PARSE_INVALID_LOAD_COMMAND;
        }

        if (size_left < load_cmd.cmdsize) {
            return E_MACHO_FILE_PARSE_INVALID_LOAD_COMMAND;
        }

        size_left -= load_cmd.cmdsize;

        /*
         * We can't check size_left here, as this could be the last
         * load-command, so we have to check at the very beginning of the loop.
         */

        switch (load_cmd.cmd) {
            case LC_SEGMENT: {
                /*
                 * If no information from a segment is needed, skip the
                 * unnecessary parsing.
                 */

                if ((tbd_options & O_TBD_PARSE_IGNORE_OBJC_CONSTRAINT) &&
                    (tbd_options & O_TBD_PARSE_IGNORE_SWIFT_VERSION))
                {
                    break;
                }

                /*
                 * Verify we have the right segment for the right word-size
                 * (32-bit vs 64-bit).
                 *
                 * We just ignore this as having the wrong segment-type doesn't
                 * matter for us.
                 */

                if (is_64) {
                    break;
                }

                if (load_cmd.cmdsize < sizeof(struct segment_command)) {
                    return E_MACHO_FILE_PARSE_INVALID_LOAD_COMMAND;
                }

                const struct segment_command *const segment =
                    (const struct segment_command *)load_cmd_iter;

                if (!segment_has_image_info_sect(segment->segname)) {
                    break;
                }

                uint32_t nsects = segment->nsects;
                if (nsects == 0) {
                    break;
                }

                if (is_big_endian) {
                    nsects = swap_uint32(nsects);
                }

                /*
                 * Verify the size and integrity of the sections.
                 */

                uint64_t sections_size = sizeof(struct section);
                if (guard_overflow_mul(&sections_size, nsects)) {
                    return E_MACHO_FILE_PARSE_TOO_MANY_SECTIONS;
                }

                const uint32_t max_sections_size =
                    load_cmd.cmdsize - sizeof(struct segment_command);

                if (sections_size > max_sections_size) {
                    return E_MACHO_FILE_PARSE_TOO_MANY_SECTIONS;
                }

                uint32_t swift_version = 0;
                const uint8_t *const sect_ptr =
                    load_cmd_iter + sizeof(struct segment_command);

                const struct section *sect = (const struct section *)sect_ptr;
                for (uint32_t j = 0; j < nsects; j++, sect++) {
                    if (!is_image_info_section(sect->sectname)) {
                        continue;
                    }

                    uint32_t sect_offset = sect->offset;
                    uint32_t sect_size = sect->size;

                    if (is_big_endian) {
                        sect_offset = swap_uint32(sect_offset);
                        sect_size = swap_uint32(sect_size);
                    }

                    const enum macho_file_parse_result parse_section_result =
                        parse_section_from_map(info_in,
                                               &swift_version,
                                               available_map_range,
                                               relative_range,
                                               map,
                                               macho,
                                               sect_offset,
                                               sect_size,
                                               options);

                    if (parse_section_result != E_MACHO_FILE_PARSE_OK) {
                        return parse_section_result;
                    }
                }

                if (info_in->swift_version != 0) {
                    const bool ignore_conflicting_fields =
                        options & O_MACHO_FILE_PARSE_IGNORE_CONFLICTING_FIELDS;

                    if (!ignore_conflicting_fields) {
                        if (info_in->swift_version != swift_version) {
                            return E_MACHO_FILE_PARSE_CONFLICTING_SWIFT_VERSION;
                        }
                    }
                } else {
                    info_in->swift_version = swift_version;
                }

                break;
            }

            case LC_SEGMENT_64: {
                /*
                 * If no information from a segment is needed, skip the
                 * unnecessary parsing.
                 */

                if ((tbd_options & O_TBD_PARSE_IGNORE_OBJC_CONSTRAINT) &&
                    (tbd_options & O_TBD_PARSE_IGNORE_SWIFT_VERSION))
                {
                    break;
                }

                /*
                 * Verify we have the right segment for the right word-size
                 * (32-bit vs 64-bit).
                 *
                 * We just ignore this as having the wrong segment-type doesn't
                 * matter for us.
                 */

                if (!is_64) {
                    break;
                }

                if (load_cmd.cmdsize < sizeof(struct segment_command_64)) {
                    return E_MACHO_FILE_PARSE_INVALID_LOAD_COMMAND;
                }

                const struct segment_command_64 *const segment =
                    (const struct segment_command_64 *)load_cmd_iter;

                if (!segment_has_image_info_sect(segment->segname)) {
                    break;
                }

                uint32_t nsects = segment->nsects;
                if (nsects == 0) {
                    break;
                }

                if (is_big_endian) {
                    nsects = swap_uint32(nsects);
                }

                /*
                 * Verify the size and integrity of the sections.
                 */

                uint64_t sections_size = sizeof(struct section_64);
                if (guard_overflow_mul(&sections_size, nsects)) {
                    return E_MACHO_FILE_PARSE_TOO_MANY_SECTIONS;
                }

                const uint32_t max_sections_size =
                    load_cmd.cmdsize - sizeof(struct segment_command_64);

                if (sections_size > max_sections_size) {
                    return E_MACHO_FILE_PARSE_TOO_MANY_SECTIONS;
                }

                uint32_t swift_version = 0;
                const uint8_t *const sect_ptr =
                    load_cmd_iter + sizeof(struct segment_command_64);

                const struct section_64 *sect =
                    (const struct section_64 *)sect_ptr;

                for (uint32_t j = 0; j < nsects; j++, sect++) {
                    if (!is_image_info_section(sect->sectname)) {
                        continue;
                    }

                    uint32_t sect_offset = sect->offset;
                    uint64_t sect_size = sect->size;

                    if (is_big_endian) {
                        sect_offset = swap_uint32(sect_offset);
                        sect_size = swap_uint64(sect_size);
                    }

                    const enum macho_file_parse_result parse_section_result =
                        parse_section_from_map(info_in,
                                               &swift_version,
                                               available_map_range,
                                               relative_range,
                                               map,
                                               macho,
                                               sect_offset,
                                               sect_size,
                                               options);

                    if (parse_section_result != E_MACHO_FILE_PARSE_OK) {
                        return parse_section_result;
                    }
                }

                if (info_in->swift_version != 0) {
                    const bool ignore_conflicting_fields =
                        options & O_MACHO_FILE_PARSE_IGNORE_CONFLICTING_FIELDS;

                    if (!ignore_conflicting_fields) {
                        if (info_in->swift_version != swift_version) {
                            return E_MACHO_FILE_PARSE_CONFLICTING_SWIFT_VERSION;
                        }
                    }
                } else {
                    info_in->swift_version = swift_version;
                }

                break;
            }

            default: {
                const bool copy_strings =
                    options & O_MACHO_FILE_PARSE_COPY_STRINGS_IN_MAP;

                const enum macho_file_parse_result parse_load_command_result =
                    parse_load_command(info_in,
                                       &uuid_info,
                                       &found_uuid,
                                       arch_bit,
                                       load_cmd,
                                       load_cmd_iter,
                                       is_big_endian,
                                       tbd_options,
                                       options,
                                       copy_strings,
                                       &found_identification,
                                       &symtab);

                if (parse_load_command_result != E_MACHO_FILE_PARSE_OK) {
                    return parse_load_command_result;
                }

                break;
            }
        }

        load_cmd_iter += load_cmd.cmdsize;
    }

    if (!found_identification) {
        return E_MACHO_FILE_PARSE_NO_IDENTIFICATION;
    }

    if (!(tbd_options & O_TBD_PARSE_IGNORE_UUID)) {
        if (!found_uuid) {
            return E_MACHO_FILE_PARSE_NO_UUID;
        }
    }

    /*
     * Ensure that the uuid found is unique among all other containers before
     * adding to the fd's uuid arrays.
     */

    const uint8_t *const array_uuid =
        array_find_item(&info_in->uuids,
                        sizeof(uuid_info),
                        &uuid_info,
                        tbd_uuid_info_comparator,
                        NULL);

    if (array_uuid != NULL) {
        return E_MACHO_FILE_PARSE_CONFLICTING_UUID;
    }

    const enum array_result add_uuid_info_result =
        array_add_item(&info_in->uuids, sizeof(uuid_info), &uuid_info, NULL);

    if (add_uuid_info_result != E_ARRAY_OK) {
        return E_MACHO_FILE_PARSE_ARRAY_FAIL;
    }

    if (symtab.cmd != LC_SYMTAB) {
        if (tbd_options & O_TBD_PARSE_IGNORE_SYMBOLS) {
            return E_MACHO_FILE_PARSE_OK;
        }

        if (tbd_options & O_TBD_PARSE_IGNORE_MISSING_EXPORTS) {
            return E_MACHO_FILE_PARSE_OK;
        }

        return E_MACHO_FILE_PARSE_NO_SYMBOL_TABLE;
    }

    /*
     * Retrieve the symbol-table and string-table info via the symtab_command.
     */

    if (is_big_endian) {
        symtab.symoff = swap_uint32(symtab.symoff);
        symtab.nsyms = swap_uint32(symtab.nsyms);

        symtab.stroff = swap_uint32(symtab.stroff);
        symtab.strsize = swap_uint32(symtab.strsize);
    }

    if (symtab_out != NULL) {
        *symtab_out = symtab;
    }

    if (options & O_MACHO_FILE_PARSE_DONT_PARSE_SYMBOL_TABLE) {
        return E_MACHO_FILE_PARSE_OK;
    }

    /*
     * Verify the symbol-table's information.
     */

    enum macho_file_parse_result ret = E_MACHO_FILE_PARSE_OK;
    if (is_64) {
        ret =
            macho_file_parse_symbols_64_from_map(info_in,
                                                 map,
                                                 available_map_range,
                                                 arch_bit,
                                                 is_big_endian,
                                                 symtab.symoff,
                                                 symtab.nsyms,
                                                 symtab.stroff,
                                                 symtab.strsize,
                                                 tbd_options);
    } else {
        ret =
            macho_file_parse_symbols_from_map(info_in,
                                              map,
                                              available_map_range,
                                              arch_bit,
                                              is_big_endian,
                                              symtab.symoff,
                                              symtab.nsyms,
                                              symtab.stroff,
                                              symtab.strsize,
                                              tbd_options);
    }

    if (ret != E_MACHO_FILE_PARSE_OK) {
        return ret;
    }

    return E_MACHO_FILE_PARSE_OK;
}
