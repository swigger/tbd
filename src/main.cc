//
//  src/main.cc
//  tbd
//
//  Created by inoahdev on 4/16/17.
//  Copyright © 2017 inoahdev. All rights reserved.
//

#include <sys/stat.h>

#include <iostream>
#include <cerrno>

#include <dirent.h>
#include <unistd.h>

#include "tbd/tbd.h"

enum class recurse {
    none,
    once,
    all
};

void loop_directory_for_libraries(DIR *directory, const std::string &directory_path, const recurse &recurse_type, const std::function<void(const std::string &)> &callback) {
    auto directory_entry = readdir(directory);
    while (directory_entry != nullptr) {
        const auto directory_entry_is_directory = directory_entry->d_type == DT_DIR;
        const auto should_recurse_all_of_directory_entry = recurse_type == recurse::all;

        if (directory_entry_is_directory && should_recurse_all_of_directory_entry) {
            if (strncmp(directory_entry->d_name, ".", directory_entry->d_namlen) == 0 ||
                strncmp(directory_entry->d_name, "..", directory_entry->d_namlen) == 0) {
                directory_entry = readdir(directory);
                continue;
            }

            auto sub_directory_path = directory_path;

            auto sub_directory_path_length = sub_directory_path.length();
            auto sub_directory_path_new_length = sub_directory_path_length + directory_entry->d_namlen + 1;

            sub_directory_path.reserve(sub_directory_path_new_length);

            sub_directory_path.append(directory_entry->d_name, &directory_entry->d_name[directory_entry->d_namlen]);
            sub_directory_path.append(1, '/');

            const auto sub_directory = opendir(sub_directory_path.data());
            if (sub_directory) {
                loop_directory_for_libraries(sub_directory, sub_directory_path, recurse_type, callback);
                closedir(sub_directory);
            } else {
                fprintf(stderr, "Warning: Failed to open sub-directory at path (%s), failing with error (%s)\n", sub_directory_path.data(), strerror(errno));
            }
        } else {
            const auto directory_entry_is_regular_file = directory_entry->d_type == DT_REG;
            if (directory_entry_is_regular_file) {
                auto directory_entry_path = directory_path + directory_entry->d_name;
                auto directory_entry_path_is_valid_library = macho::file::is_valid_library(directory_entry_path);

                if (directory_entry_path_is_valid_library) {
                    callback(directory_entry_path);
                }
            }
        }

        directory_entry = readdir(directory);
    }
}

const std::string &retrieve_current_directory() {
    static auto current_directory = std::string();
    if (current_directory.empty()) {
        const auto current_directory_string = getcwd(nullptr, 0);
        if (!current_directory_string) {
            fprintf(stderr, "Failed to get current-working-directory, failing with error (%s)\n", strerror(errno));
            exit(1);
        }

        current_directory = current_directory_string;
        if (current_directory.back() != '/') {
            current_directory.append(1, '/');
        }
    }

    return current_directory;
}

void parse_architectures_list(std::vector<const NXArchInfo *> &architectures, int &index, int argc, const char *argv[]) {
    while (index < argc) {
        const auto &architecture_string = argv[index];
        if (*architecture_string == '-' || *architecture_string == '/') {
            if (architectures.empty()) {
                fputs("Please provide a list of architectures to override the ones in the provided mach-o file(s)\n", stderr);
                exit(1);
            }

            break;
        }

        const auto architecture = NXGetArchInfoFromName(architecture_string);
        if (!architecture) {
            if (architectures.empty()) {
                fprintf(stderr, "Unrecognized architecture with name (%s)\n", architecture_string);
                exit(1);
            }

            break;
        }

        architectures.emplace_back(architecture);
        index++;
    }

    index--;
}

void recursively_create_directories_from_file_path(char *path) {
    char *slash = strchr(path, '/');
    while (slash != nullptr) {
        *slash = '\0';

        if (access(path, F_OK) != 0) {
            mkdir(path, 0755);
        }

        *slash = '/';
        slash = strchr(slash + 1, '/');
    }
}

void print_usage() {
    fputs("Usage: tbd [-p file-paths] [-v/--version v2] [-a/--archs architectures] [-o/-output output-paths-or-stdout]\n", stdout);
    fputs("Main options:\n", stdout);
    fputs("    -a, --archs,    Specify Architecture(s) to use, instead of the ones in the provieded mach-o file(s)\n", stdout);
    fputs("    -h, --help,     Print this message\n", stdout);
    fputs("    -o, --output,   Path(s) to output file(s) to write converted .tbd. If provided file(s) already exists, contents will get overrided. Can also provide \"stdout\" to print to stdout\n", stdout);
    fputs("    -p, --path,     Path(s) to mach-o file(s) to convert to a .tbd\n", stdout);
    fputs("    -u, --usage,    Print this message\n", stdout);
    fputs("    -v, --version,  Set version of tbd to convert to (default is v2)\n", stdout);

    fputs("\n", stdout);
    fputs("Extra options:\n", stdout);
    fputs("        --platform, Specify platform for all mach-o files provided\n", stdout);
    fputs("    -r, --recurse,  Specify directory to recurse and find mach-o files in. Use in conjunction with -p (ex. -p -r /path/to/directory)\n", stdout);
    fputs("        --versions, Print a list of all valid tbd-versions\n", stdout);

    fputs("\n", stdout);
    fputs("Outputting options:\n", stdout);
    fputs("        --maintain-directories, Maintain folders mach-o library was found in (subtracting the path provided)\n", stdout);


    fputs("\n", stdout);
    fputs("List options:\n", stdout);
    fputs("        --list-architectures,   List all valid architectures for tbd-files\n", stdout);
    fputs("        --list-macho-libraries, List all valid mach-o libraries in current-directory (or at provided path(s))\n", stdout);
}

int main(int argc, const char *argv[]) {
    if (argc < 2) {
        fputs("Please run -h or -u to see a list of options\n", stderr);
        return 1;
    }

    typedef struct tbd_recursive {
        const std::string provided_path;

        tbd tbd;
        recurse recurse;
    } tbd_recursive;

    auto architectures = std::vector<const NXArchInfo *>();
    auto tbds = std::vector<tbd_recursive>();

    auto platform_string = std::string();
    auto current_directory = std::string();

    auto output_paths_index = 0;
    auto version = tbd::version::v2;

    for (auto i = 1; i < argc; i++) {
        const auto &argument = argv[i];
        const auto &argument_front = *argument;

        if (argument_front != '-') {
            fprintf(stderr, "1. Unrecognized argument: %s\n", argument);
            return 1;
        }

        auto option = &argument[1];
        if (*option == '-') {
            option++;
        }

        if (!*option) {
            fputs("Please provide a valid option\n", stderr);
            exit(1);
        }

        const auto is_first_argument = i == 1;
        const auto is_last_argument = i == argc - 1;

        if (strcmp(option, "a") == 0 || strcmp(option, "archs") == 0) {
            if (is_last_argument) {
                fputs("Please provide a list of architectures to override the ones in the provided mach-o file(s)\n", stderr);
                return 1;
            }

            i++;
            parse_architectures_list(architectures, i, argc, argv);
        } else if (strcmp(option, "h") == 0 || strcmp(option, "help") == 0) {
            if (!is_first_argument || !is_last_argument) {
                fprintf(stderr, "Option (%s) should be run by itself\n", argument);
                return 1;
            }

            print_usage();
        } else if (strcmp(option, "list-architectures") == 0) {
            if (!is_first_argument || !is_last_argument) {
                fprintf(stderr, "Option (%s) should be run by itself\n", argument);
                return 1;
            }

            auto architectures = NXGetAllArchInfos();
            while (architectures->name != nullptr) {
                fprintf(stdout, "%s\n", architectures->name);
                architectures++;
            }

            return 0;
        } else if (strcmp(option, "list-macho-libraries") == 0) {
            if (!is_first_argument) {
                fprintf(stderr, "Option (%s) should be run by itself\n", argument);
                return 1;
            }

            auto paths = std::vector<std::pair<std::string, recurse>>();
            if (is_last_argument) {
                paths.emplace_back(retrieve_current_directory(), recurse::all);
            } else {
                auto recurse_type = recurse::none;
                for (i++; i < argc; i++) {
                    const auto &argument = argv[i];
                    const auto &argument_front = *argument;

                    if (argument_front == '-') {
                        auto option = &argument[1];
                        if (*option == '-') {
                            option++;
                        }

                        if (!*option) {
                            fputs("Please provide a valid option\n", stderr);
                            exit(1);
                        }

                        if (strcmp(option, "r") == 0 || strcmp(option, "recurse") == 0) {
                            recurse_type = recurse::all;
                        } else if (strncmp(option, "r=", 2) == 0 || strncmp(option, "recurse=", 8) == 0) {
                            const auto recurse_type_string = strchr(option, '=') + 1;
                            const auto &recurse_type_string_front = *recurse_type_string;

                            if (!recurse_type_string_front) {
                                fputs("Please provide a recurse type", stderr);
                                return 1;
                            }

                            if (strcmp(recurse_type_string, "once") == 0) {
                                recurse_type = recurse::once;
                            } else if (strcmp(recurse_type_string, "all") == 0) {
                                recurse_type = recurse::all;
                            } else {
                                fprintf(stderr, "Unrecognized recurse-type (%s)\n", recurse_type_string);
                                return 1;
                            }
                        } else {
                            fprintf(stderr, "2. Unrecognized argument: %s\n", argument);
                            return 1;
                        }

                        continue;
                    }

                    if (argument_front != '/') {
                        auto path = std::string(argument);
                        auto &current_directory = retrieve_current_directory();

                        path.insert(0, current_directory);
                        paths.emplace_back(std::move(path), recurse_type);
                    } else {
                        paths.emplace_back(argument, recurse_type);
                    }

                    recurse_type = recurse::none;
                }

                if (paths.empty()) {
                    paths.emplace_back(retrieve_current_directory(), recurse_type);
                }
            }

            for (const auto &pair : paths) {
                const auto &path = pair.first;
                const auto &recurse_type = pair.second;

                if (access(path.data(), F_OK) != 0) {
                    fprintf(stderr, "Object at path (%s) does not exist\n", path.data());
                    return 1;
                }

                struct stat sbuf;
                if (stat(path.data(), &sbuf) != 0) {
                    fprintf(stderr, "Failed to retrieve information on object at path (%s), failing with error (%s)\n", path.data(), strerror(errno));
                    return 1;
                }

                const auto path_is_directory = S_ISDIR(sbuf.st_mode);
                if (recurse_type != recurse::none) {
                    if (!path_is_directory && recurse_type != recurse::none) {
                        fprintf(stderr, "Cannot recurse file at path (%s)\n", path.data());
                        return 1;
                    }

                    const auto directory = opendir(path.data());
                    if (!directory) {
                        fprintf(stderr, "Failed to open directory at path (%s), failing with error (%s)\n", path.data(), strerror(errno));
                        return 1;
                    }

                    auto library_paths = std::vector<std::string>();
                    loop_directory_for_libraries(directory, path, recurse_type, [&](const std::string &path) {
                        library_paths.emplace_back(std::move(path));
                    });

                    if (library_paths.empty()) {
                        switch (recurse_type) {
                            case recurse::none:
                                break;

                            case recurse::once:
                                fprintf(stdout, "No mach-o library files were found while recursing once through path (%s)\n", path.data());
                                break;

                            case recurse::all:
                                fprintf(stdout, "No mach-o library files were found while recursing through path (%s)\n", path.data());
                                break;
                        }
                    } else {
                        switch (recurse_type) {
                            case recurse::none:
                                break;

                            case recurse::once:
                                fprintf(stdout, "Found the following mach-o libraries while recursing once through path (%s)\n", path.data());
                                break;

                            case recurse::all:
                                fprintf(stdout, "Found the following mach-o libraries while recursing through path (%s)\n", path.data());
                                break;
                        }

                        const auto &path_length = path.length();
                        for (auto &library_path : library_paths) {
                            library_path.erase(library_path.begin(), library_path.begin() + path_length);
                            fprintf(stdout, "%s\n", library_path.data());
                        }

                        fputs("\n", stdout);
                    }
                } else {
                    if (path_is_directory) {
                        fprintf(stderr, "Cannot open directory at path (%s) as a macho-file, use -r (or -r=) to recurse the directory\n", path.data());
                        return 1;
                    }

                    const auto path_is_library = macho::file::is_valid_library(path);
                    if (path_is_library) {
                        fprintf(stdout, "Mach-o file at path (%s) is a library\n", path.data());
                    } else {
                        fprintf(stdout, "Mach-o file at path (%s) is not a library\n", path.data());
                    }
                }
            }

            return 0;
        } else if (strcmp(option, "o") == 0 || strcmp(option, "output") == 0) {
            if (is_last_argument) {
                fputs("Please provide path(s) to output files\n", stderr);
                return 1;
            }

            auto should_maintain_directories = false;

            for (i++; i < argc; i++) {
                const auto &argument = argv[i];
                const auto &argument_front = *argument;

                if (argument_front == '-') {
                    auto option = &argument[1];
                    if (*option == '-') {
                        option++;
                    }

                    if (!*option) {
                        fputs("Please provide a valid option\n", stderr);
                        exit(1);
                    }

                    if (strcmp(option, "maintain-directories") == 0) {
                        should_maintain_directories = true;
                    } else {
                        fputs("Please provide path(s) to output files\n", stderr);
                        exit(1);
                    }

                    continue;
                }

                auto path = std::string(argument);
                auto &path_front = path.front();

                const auto tbds_size = tbds.size();
                if (output_paths_index >= tbds_size) {
                    fprintf(stderr, "No coresponding mach-o files for output-path (%s, at index %d)\n", path.data(), output_paths_index);
                    return 1;
                }

                auto &tbd_recursive = tbds.at(output_paths_index);
                auto &tbd = tbd_recursive.tbd;

                auto &macho_files = tbd.macho_files();
                auto macho_files_size = macho_files.size();

                if (path_front != '/' && path != "stdout") {
                    path.insert(0, retrieve_current_directory());
                } else if (path == "stdout" && macho_files.size() > 1) {
                    fputs("Can't output multiple mach-o files to stdout\n", stderr);
                    return 1;
                }

                auto &tbd_recurse_type = tbd_recursive.recurse;
                auto &output_files = tbd.output_files();

                struct stat sbuf;
                if (stat(path.data(), &sbuf) == 0) {
                    const auto path_is_directory = S_ISDIR(sbuf.st_mode);
                    if (path_is_directory) {
                        if (tbd_recurse_type == recurse::none) {
                            fprintf(stderr, "Cannot output tbd-file to a directory at path (%s), please provide a full path to a file to output to\n", path.data());
                            return 1;
                        } else if (path == "stdout") {
                            fputs("Cannot output recursive mach-o files to stdout. Please provide a directory to output to", stderr);
                            return 1;
                        }

                        const auto output_files_size = output_files.size();
                        const auto output_files_new_size = output_files_size + macho_files_size;

                        output_files.reserve(output_files_new_size);

                        for (const auto &macho_file : macho_files) {
                            auto macho_file_iter = std::string::npos;
                            if (should_maintain_directories) {
                                macho_file_iter = tbd_recursive.provided_path.length();
                            } else {
                                macho_file.find_last_of('/');
                            }

                            auto macho_file_output_path = macho_file.substr(macho_file_iter);

                            auto macho_file_output_path_length = macho_file_output_path.length();
                            auto macho_file_output_path_new_length = macho_file_output_path_length + path.length() + 4;

                            macho_file_output_path.reserve(macho_file_output_path_new_length);

                            macho_file_output_path.insert(0, path);
                            macho_file_output_path.append(".tbd");

                            recursively_create_directories_from_file_path((char *)macho_file_output_path.data());

                            output_files.emplace_back(std::move(macho_file_output_path));
                        }
                    } else {
                        const auto path_is_regular_file = S_ISREG(sbuf.st_mode);
                        if (path_is_regular_file) {
                            if (macho_files_size > 1) {
                                fprintf(stderr, "Can't output multiple mach-o files to file at path (%s)\n", path.data());
                                return 1;
                            }

                            output_files.emplace_back(path);
                        }
                    }
                } else {
                    if (macho_files_size > 1) {
                        fprintf(stderr, "Directory at path (%s) does not exist\n", path.data());
                        return 1;
                    }

                    output_files.emplace_back(path);
                }

                output_paths_index++;
                break;
            }
        } else if (strcmp(option, "p") == 0 || strcmp(option, "path") == 0) {
            if (is_last_argument) {
                fputs("Please provide path(s) to mach-o files\n", stderr);
                return 1;
            }

            auto local_architectures = std::vector<const NXArchInfo *>();

            auto local_platform = std::string();
            auto local_tbd_version = (enum tbd::version)0;

            auto recurse_type = recurse::none;
            for (i++; i < argc; i++) {
                const auto &argument = argv[i];
                if (*argument == '-') {
                    auto option = &argument[1];
                    if (*option == '-') {
                        option++;
                    }

                    if (!*option) {
                        fputs("Please provide a valid option\n", stderr);
                        exit(1);
                    }

                    const auto is_last_argument = i == argc - 1;
                    if (strcmp(option, "a") == 0 || strcmp(option, "archs") == 0) {
                        if (is_last_argument) {
                            fputs("Please provide a list of architectures to override the ones in the provided mach-o file(s)\n", stderr);
                            return 1;
                        }

                        i++;
                        parse_architectures_list(local_architectures, i, argc, argv);
                    } else if (strcmp(option, "p") == 0) {
                        fprintf(stderr, "Please provide a path for option (%s)\n", argument);
                        return 1;
                    } else if (strcmp(option, "platform") == 0) {
                        if (is_last_argument) {
                            fputs("Please provide a platform-string (ios, macosx, tvos, watchos)", stderr);
                            return 1;
                        }

                        i++;

                        const auto &platform_string_arg = argv[i];
                        if (tbd::string_to_platform(platform_string_arg) == (enum tbd::platform)-1) {
                            fprintf(stderr, "Platform-string (%s) is invalid\n", platform_string_arg);
                            return 1;
                        }

                        platform_string = platform_string_arg;
                    } else if (strcmp(option, "r") == 0 || strcmp(option, "recurse") == 0) {
                        recurse_type = recurse::all;
                    } else if (strncmp(option, "r=", 2) == 0 || strncmp(option, "recurse=", 8) == 0) {
                        const auto recurse_type_string = strchr(option, '=') + 1;
                        if (!*recurse_type_string) {
                            fputs("Please provide a recurse type", stderr);
                            return 1;
                        }

                        if (strcmp(recurse_type_string, "once") == 0) {
                            recurse_type = recurse::once;
                        } else if (strcmp(recurse_type_string, "all") == 0) {
                            recurse_type = recurse::all;
                        } else {
                            fprintf(stderr, "Unrecognized recurse-type (%s)\n", recurse_type_string);
                            return 1;
                        }
                    } else if (strcmp(option, "v") == 0 || strcmp(option, "version") == 0) {
                        if (is_last_argument) {
                            fputs("Please provide a tbd-version\n", stderr);
                            return 1;
                        }

                        i++;

                        local_tbd_version = tbd::string_to_version(argv[i]);
                        if (!(int)local_tbd_version) {
                            fprintf(stderr, "(%s) is not a valid tbd-version\n", argv[i]);
                            return 1;
                        }
                    } else {
                        fprintf(stderr, "3. Unrecognized argument: %s\n", argument);
                        return 1;
                    }

                    continue;
                }

                auto path = std::string(argument);
                auto &path_front = path.front();

                if (path_front != '/') {
                    path.insert(0, retrieve_current_directory());
                }

                struct stat sbuf;
                if (stat(path.data(), &sbuf) != 0) {
                    fprintf(stderr, "Failed to retrieve information on object at path (%s), failing with error (%s)\n", path.data(), strerror(errno));
                    return 1;
                }

                auto tbd = ::tbd();
                auto &macho_files = tbd.macho_files();

                const auto path_is_directory = S_ISDIR(sbuf.st_mode);
                if (path_is_directory) {
                    if (recurse_type == recurse::none) {
                        fprintf(stderr, "Cannot open directory at path (%s) as a macho-file, use -r to recurse the directory\n", path.data());
                        return 1;
                    }

                    const auto &path_back = path.back();
                    if (path_back != '/') {
                        path.append(1, '/');
                    }

                    const auto directory = opendir(path.data());
                    if (!directory) {
                        fprintf(stderr, "Failed to open directory at path (%s), failing with error (%s)\n", path.data(), strerror(errno));
                        return 1;
                    }

                    loop_directory_for_libraries(directory, path, recurse_type, [&](const std::string &path) {
                        macho_files.emplace_back(std::move(path));
                    });

                    closedir(directory);
                } else {
                    const auto path_is_regular_file = S_ISREG(sbuf.st_mode);
                    if (path_is_regular_file) {
                        if (recurse_type != recurse::none) {
                            fprintf(stderr, "Cannot recurse file at path (%s)\n", path.data());
                            return 1;
                        }

                        const auto path_is_valid_library = macho::file::is_valid_library(path);
                        if (!path_is_valid_library) {
                            fprintf(stderr, "File at path (%s) is not a valid mach-o library\n", path.data());
                            return 1;
                        }

                        macho_files.emplace_back(path);
                    } else {
                        fprintf(stderr, "Object at path (%s) is not a regular file\n", path.data());
                        return 1;
                    }
                }

                const auto &tbd_macho_files = tbd.macho_files();
                const auto tbd_macho_files_is_empty = tbd_macho_files.empty();

                if (tbd_macho_files_is_empty) {
                    switch (recurse_type) {
                        case recurse::none:
                            fprintf(stdout, "File at path (%s) is not a mach-o library file\n", path.data());
                            break;

                        case recurse::once:
                            fprintf(stdout, "No mach-o library files were found while recursing once in directory at path (%s)\n", path.data());
                            break;

                        case recurse::all:
                            fprintf(stdout, "No mach-o library files were found while recursing through all files and directories in directory at path (%s)\n", path.data());
                            break;
                    }

                    return 1;
                }

                auto tbd_architectures = &local_architectures;
                if (tbd_architectures->empty()) {
                    tbd_architectures = &architectures;
                }

                auto tbd_platform = &local_platform;
                if (tbd_platform->empty()) {
                    tbd_platform = &platform_string;
                }

                auto tbd_version = &local_tbd_version;
                if (!(int)*tbd_version) {
                    tbd_version = &version;
                }

                tbd.set_architectures(*tbd_architectures);
                tbd.set_platform(tbd::string_to_platform(tbd_platform->data()));
                tbd.set_version(*(enum tbd::version *)tbd_version);

                auto &output_files = tbd.output_files();
                output_files.reserve(macho_files.size());

                auto tbd_recurse = tbd_recursive({ path, tbd, recurse_type });
                tbds.emplace_back(tbd_recurse);

                local_architectures.clear();
                local_platform.clear();

                local_tbd_version = (enum tbd::version)0;
                recurse_type = recurse::none;

                break;
            }

            if (recurse_type != recurse::none || local_architectures.size() != 0 || local_platform.size() != 0 || (int)local_tbd_version != 0) {
                fputs("Please provide a path to a directory to recurse through\n", stderr);
                return 1;
            }
        } else if (strcmp(option, "platform") == 0) {
            if (is_last_argument) {
                fputs("Please provide a platform-string (ios, macosx, tvos, watchos)", stderr);
                return 1;
            }

            i++;

            const auto &platform_string_arg = argv[i];
            if ((int)tbd::string_to_platform(platform_string_arg) == -1) {
                fprintf(stderr, "Platform-string (%s) is invalid\n", platform_string_arg);
                return 1;
            }

            platform_string = platform_string_arg;
        } else if (strcmp(option, "u") == 0 || strcmp(option, "usage") == 0) {
            if (!is_first_argument || !is_last_argument) {
                fprintf(stderr, "Option (%s) should be run by itself\n", argument);
                return 1;
            }

            print_usage();
            return 0;
        } else if (strcmp(option, "v") == 0 || strcmp(option, "version") == 0) {
            if (is_last_argument) {
                fputs("Please provide a tbd-version\n", stderr);
                return 1;
            }

            i++;

            const auto &version_string = argv[i];
            if (*version_string == '-') {
                fputs("Please provide a tbd-version\n", stderr);
                return 1;
            }

            if (strcmp(version_string, "v1") == 0) {
                version = tbd::version::v1;
            } else if (strcmp(version_string, "v2") != 0) {
                fprintf(stderr, "tbd-version (%s) is invalid\n", version_string);
                return 1;
            }
        } else if (strcmp(option, "versions") == 0) {
            if (!is_first_argument || !is_last_argument) {
                fprintf(stderr, "Option (%s) should be run by itself\n", argument);
                return 1;
            }

            fputs("v1\nv2 (default)\n", stdout);
            return 0;
        } else {
            fprintf(stderr, "4. Unrecognized argument: %s\n", argument);
            return 1;
        }
    }

    const auto tbds_size = tbds.size();
    if (!tbds_size) {
        fputs("No mach-o files have been provided\n", stderr);
        return 1;
    }

    auto tbd_recursive_index = 0;
    for (auto &tbd_recursive : tbds) {
        auto &tbd = tbd_recursive.tbd;

        const auto &tbd_architectures = tbd.architectures();
        const auto &tbd_version = tbd.version();

        const auto architectures_size = architectures.size();
        const auto tbd_architectures_size = tbd_architectures.size();

        if (tbd_version == tbd::version::v2) {
            if (tbd_architectures_size != 0 || architectures_size != 0) {
                fputs("Cannot have custom architectures on tbd-version v2, Please specify tbd-version v1\n", stderr);
                return 1;
            }
        } else if (!tbd_architectures_size && architectures_size != 0) {
            tbd.set_architectures(architectures);
        }

        const auto &path = tbd_recursive.provided_path;
        auto platform = tbd.platform();

        if (platform == (enum tbd::platform)-1) {
            const auto path_is_directory = path.back() == '/';
            while (platform_string.empty() || (platform = tbd::string_to_platform(platform_string.data())) == (enum tbd::platform)-1) {
                if (path_is_directory) {
                    fprintf(stdout, "Please provide a platform for files in directory at path (%s) (ios, macosx, watchos, or tvos): ", path.data());
                } else {
                    fprintf(stdout, "Please provide a platform for file at path (%s) (ios, macosx, watchos, or tvos): ", path.data());
                }

                getline(std::cin, platform_string);
            }

            tbd.set_platform(platform);
        }

        auto &output_files = tbd.output_files();
        auto output_files_size = output_files.size();

        if (output_files_size != 0) {
            continue;
        }

        const auto &macho_files = tbd.macho_files();
        const auto tbd_recurse_type = tbd_recursive.recurse;

        if (tbd_recurse_type != recurse::none) {
            output_files.reserve(macho_files.size());
            for (const auto &macho_file : macho_files) {
                output_files.emplace_back(macho_file + ".tbd");
            }
        }

        tbd_recursive_index++;
    }

    for (auto &tbd_recursive : tbds) {
        auto &tbd = tbd_recursive.tbd;
        tbd.run();
    }
}
