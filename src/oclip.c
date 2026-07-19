#include <argp.h>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <termios.h>
#include <unistd.h>

#define READ_BUFFER_SIZE (1 << 12) // 4Kb

#define IN stdin
#define OUT stdout
#define ERR stderr

#define OSC52_PREFIX "\033]52;c;"
#define OSC52_SUFFIX "\a"
#define OSC52_PREFIX_LEN (sizeof(OSC52_PREFIX) - 1)

/**
 * @brief Writes (up to) the 24 least signifant bits of `buffer` to `output`,
 * as base64.
 *
 * Characters are encoded and decoded from the MSB -> LSB.
 *
 * @param buffer value to encode.
 * @param n Only write first `n` base64 characters.
 * @param output output file pointer.
 */
void encodebase64(uint32_t buffer, uint8_t n, FILE *output)
{
  assert(n <= 4 && "buffer should fit no more than 4 base64 digits.");

  for (uint8_t i = 0; i < n; ++i) {
    uint8_t block = (buffer >> ((3 - i) * 6)) & 0b00111111;

    char c = 0;
    if (block == 63)
      c = '/';
    else if (block == 62)
      c = '+';
    else if (block >= 52)
      c = '0' + (block - 52);
    else if (block >= 26)
      c = 'a' + (block - 26);
    else
      c = 'A' + block;

    fputc(c, output);
  }
}

/**
 * @brief writes least significant 24 bits of `buffer` to output;
 *
 * Bytes are written from the MSB -> LSB.
 *
 * @param buffer value to write.
 * @param output output file pointer.
 * @return true on early exit, false otherwise.
 */
bool fwrite24r(uint32_t buffer, FILE *output)
{
  for (uint8_t i = 0; i < 3; ++i) {
    char c = buffer >> 8 * (2 - i);
    if (c == 0)
      return true;

    fputc(c, output);
  }

  return false;
}

/**
 * @brief Reads system clipboard (from stdin) and writes it to `out`.
 *
 * IMPORTANT: This function only works on POSIX systems.
 *
 * @param out output file pointer.
 * @return 0 on success, otherwise an error code.
 */
int readclipboard(FILE *out)
{
  int ec = EX_OK;

  char buffer[READ_BUFFER_SIZE];
  size_t size = 0;

  struct termios old;
  tcgetattr(STDIN_FILENO, &old);

  struct termios new = old;
  new.c_lflag &= ~ICANON;
  new.c_lflag &= ~ECHO;
  tcsetattr(STDIN_FILENO, TCSADRAIN, &new);

  FILE *tty = fopen("/dev/tty", "w");
  if (!tty) {
    ec = EX_IOERR;
    fputs("Failed to request clipboard!\n", ERR);
    goto cleanup;
  }

  fputs(OSC52_PREFIX "?" OSC52_SUFFIX, tty);
  fflush(tty);
  fclose(tty);

  size_t count = 0;
  while ((count = read(STDIN_FILENO, buffer, sizeof(buffer) - size))) {
    if ((size += count) >= OSC52_PREFIX_LEN)
      break;
  }

  if (count == 0) {
    ec = EX_DATAERR;
    fputs("File end before OSC52 prefix!", ERR);
    goto cleanup;
  }

  if (memcmp(buffer, OSC52_PREFIX, OSC52_PREFIX_LEN) != 0) {
    ec = EX_DATAERR;
    fputs("Unexpected stdin prefix!", ERR);
    goto cleanup;
  }

  size -= OSC52_PREFIX_LEN;

  const char *ptr = buffer + OSC52_PREFIX_LEN;
  uint8_t rem = 0;
  uint32_t value = 0;

  do {
    while (size) {
      for (; rem < 4 && size; ++rem, ++ptr, --size) {
        uint8_t chr = (uint8_t)*ptr;
        if (chr == '/')
          chr = 63;
        else if (chr == '+')
          chr = 62;
        else if (chr >= '0' && chr <= '9')
          chr = 52 + (chr - '0');
        else if (chr >= 'a' && chr <= 'z')
          chr = 26 + (chr - 'a');
        else if (chr >= 'A' && chr <= 'Z')
          chr = chr - 'A';
        else {
          if (chr != 27 && chr != '=')
            fprintf(ERR, "Unexpected stream end: %d!\n", chr);
          goto streamend;
        }

        value += chr << 6 * (3 - rem);
      }

      if (rem == 4) {
        rem = 0;
        if (fwrite24r(value, out))
          goto cleanup;

        value = 0;
      }
    }

    ptr = buffer;
  } while ((size = read(STDIN_FILENO, buffer, sizeof(buffer))));

streamend:
  if (rem)
    fwrite24r(value, out);

cleanup:
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
  return ec;
}

/**
 * @brief Reads `in` and writes content to the clipboard.
 *
 * @param in input file pointer.
 * @return 0 on success, otherwise an error code.
 */
int writeclipboard(FILE *in)
{
  char buffer[READ_BUFFER_SIZE];

  fputs(OSC52_PREFIX, OUT);

  size_t count = 0;
  uint8_t rem = 0;
  uint32_t value = 0;

  while ((count = fread(buffer, 1, sizeof(buffer), in))) {
    const char *ptr = buffer;

    while (count) {
      for (; rem < 3 && count; ++rem, --count, ++ptr)
        value += *ptr << 8 * (2 - rem);

      if (rem == 3) {
        encodebase64(value, 4, OUT);
        value = 0;
        rem = 0;
      }
    }
  }

  if (rem) {
    encodebase64(value, rem + 1, OUT);
    for (uint8_t i = 0; i < (3 - rem); ++i)
      fputc('=', OUT);
  }

  fputs(OSC52_SUFFIX, OUT);

  return EXIT_SUCCESS;
}

typedef struct {
  char *file;
  bool output;
} arguments;

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
  arguments *args = (arguments *)state->input;

  switch (key) {
    case ARGP_KEY_ARG:
      if (args->file)
        argp_error(state, "please provide AT MOST one positional arg.");

      args->file = arg;
      break;
    case 'o':
      args->output = true;
      break;
    default:
      return ARGP_ERR_UNKNOWN;
  }

  return EX_OK;
}

const char *argp_program_version = "0.1.0";

static char doc[] =
  "Writes and reads system clipboard.\n\n"
  "Uses OSC52 to interface with system clipboard, make sure your terminal of choice "
  "supports and has enabled OSC52 escape sequences.";

static struct argp_option options[] = {
  {
    .name = "output",
    .key = 'o',
    .arg = 0,
    .flags = 0,
    .doc = "Write clipboard contents to `file` (stdout by default). If not "
           "specified, read clipboard from `file` (stdin by default) instead",
  },
  {0},
};

static struct argp argp = {
  .options = options,
  .parser = parse_opt,
  .args_doc = "[file]",
  .doc = doc,
};

int main(int argc, char **argv)
{
  arguments args = {
    .file = 0,
    .output = false,
  };
  argp_parse(&argp, argc, argv, 0, 0, &args);

  if (args.file && strcmp(args.file, "-") == 0)
    args.file = 0;

  FILE *file = args.output ? OUT : IN;
  if (args.file && !(file = fopen(args.file, args.output ? "w" : "r")))
    return EX_IOERR;

  int ec = args.output ? readclipboard(file) : writeclipboard(file);

  if (args.file)
    fclose(file);

  return ec;
}
