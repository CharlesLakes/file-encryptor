/**
 * @file    cli.h
 * @brief   Command-line client: uploading files to an archive at a URL.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @code
 * file-encryptor --upload https://host/archive.crls file1 [file2 ...]
 * @endcode
 *
 * The password is asked without echo (or read as one line from standard input
 * when it is not a terminal). Each file is encrypted on this machine and only
 * its ciphertext and the new index are sent (see core/remote.h): the password
 * never leaves the client.
 */

#ifndef CLI_CLI_H
#define CLI_CLI_H

namespace crls{

/**
 * @brief  Runs `--upload <url> <file>...`.
 * @param  argc Number of arguments, starting at "--upload".
 * @param  argv Arguments, starting at "--upload".
 * @return Process exit code: 0 if every file was uploaded, 1 if any failed,
 *         2 on a usage error.
 */
int cli_upload(int argc, char **argv);

}

#endif
