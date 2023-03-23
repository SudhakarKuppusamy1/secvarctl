/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2022-2023 IBM Corp.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "prlog.h"
#include "secvarctl.h"
#ifdef SECVAR_HOST_BACKEND
#include "host_svc_backend.h"
#endif
#ifdef SECVAR_GUEST_BACKEND
#include "guest_svc_backend.h"
#endif

#define HOST_BACKEND ((char *) "ibm,edk2-compat-v1")
#define GUEST_BACKEND ((char *) "ibm,plpks-sb-v1")

int verbose = PR_WARNING;

static struct backend backends[] = {
#ifdef SECVAR_HOST_BACKEND
  { .name = "ibm,edk2-compat-v1",
    .countCmds = sizeof (edk2_compat_command_table) / sizeof (struct command),
    .commands = edk2_compat_command_table },
#endif
#ifdef SECVAR_GUEST_BACKEND
  { .name = "ibm,plpks-sb-v1",
    .countCmds = sizeof (guest_command_table) / sizeof (struct command),
    .commands = guest_command_table },
#endif
};

void
usage ()
{
  printf ("\nUSAGE: \n\t$ secvarctl [MODE] [COMMAND]\n"
          "MODEs:\n"
          "-m, --mode\tsupports both the Guest and Host secure boot variables "
          "in two different modes\n"
          "\t\tand either -m host or -m guest are acceptable values.\n"
          "COMMANDs:\n"
          "\t--help/--usage\n\t"
          "read\t\tprints info on secure variables,\n\t\t\t"
          "use 'secvarctl [MODE] read --usage/help' for more information\n\t"
          "write\t\tupdates secure variable with new auth,\n\t\t\t"
          "use 'secvarctl [MODE] write --usage/help' for more information"
          "\n\tvalidate\tvalidates format of given esl/cert/auth,\n\t\t\t"
          "use 'secvarctl [MODE] validate --usage/help' for more "
          "information\n\t"
          "verify\t\tcompares proposed variable to the current "
          "variables,\n\t\t\t"
          "use 'secvarctl [MODE] verify --usage/help' for more information\n"
#ifdef SECVAR_CRYPTO_WRITE_FUNC
          "\tgenerate\tcreates relevant files for secure variable "
          "management,\n\t\t\t"
          "use 'secvarctl [MODE] generate --usage/help' for more information\n"
#endif
  );
}

void
help ()
{
  printf ("\nHELP:\n\t"
          "A command line tool for simplifying the reading and writing of "
          "secure boot variables.\n\t"
          "Commands are:\n\t\t"
          "read - print out information on their current secure vaiables\n\t\t"
          "write - update the given variable's key value, committed upon "
          "reboot\n\t\t"
          "validate  -  checks format requirements are met for the given file "
          "type\n\t\t"
          "verify - checks that the given files are correctly signed by the "
          "current variables\n"
#ifdef SECVAR_CRYPTO_WRITE_FUNC
          "\t\tgenerate - create files that are relevant to the secure "
          "variable management process\n"
#endif
  );

  usage ();
}

int
is_known_backend (const char *buff, struct backend **backend)
{
  int i = 0;
  int total_backend = sizeof (backends) / sizeof (struct backend);

  /* loop through all known backends */
  for (i = 0; i < total_backend; i++)
    {
      if (!strncmp (buff, backends[i].name, strlen (backends[i].name)))
        {
          prlog (PR_NOTICE, "found backend %s\n", backends[i].name);
          *backend = &backends[i];
          return BACKEND_FOUND;
        }
    }

  return UNKNOWN_BACKEND;
}

/*
 * Checks what backend the platform is running, CURRENTLY ONLY KNOWS EDK2
 * @return type of backend, or NULL if file could not be found or contained wrong contents,
 */
static struct backend *
get_backend ()
{
  char *buff = NULL, *secvar_format_location = "/sys/firmware/secvar/format";
  size_t buffSize = 0, max_buff_size = 0;
  struct backend *result = NULL;
  int i = 0;

  /* if file doesnt exist then print warning and keep going */
  if (is_file (secvar_format_location))
    {
      prlog (PR_WARNING,
             "WARNING!! platform does not support secure variables\n");
      return result;
    }

  /* get max size of backend name */
  max_buff_size = strlen (backends[0].name);
  for (i = 0; i < sizeof (backends) / sizeof (struct backend); i++)
    {
      if (strlen (backends[i].name) > max_buff_size)
        max_buff_size = strlen (backends[i].name);
    }

  buff = get_data_from_file (secvar_format_location, max_buff_size, &buffSize);
  if (buff == NULL)
    {
      prlog (PR_WARNING,
             "WARNING!! could not extract data from %s , "
             "assuming platform does not support secure variables\n",
             secvar_format_location);
    }
  else if (!is_known_backend (buff, &result))
    prlog (PR_WARNING, "WARNING!! %s  does not contain known backend format.\n",
           secvar_format_location);

  if (buff != NULL)
    free (buff);

  return result;
}

int
main (int argc, char *argv[])
{
  int rc, i, secvarctl_mode = -1;
  char *subcommand = NULL, *backend_name = NULL;
  struct backend *backend = NULL;

  if (argc < 2)
    {
      usage ();
      return ARG_PARSE_FAIL;
    }

  argv++;
  argc--;

  for (; argc > 0 && *argv[0] == '-'; argc--, argv++)
    {
      if (!strcmp ("--usage", *argv))
        {
          usage ();
          return SUCCESS;
        }
      else if (!strcmp ("--help", *argv) || !strcmp ("-h", *argv))
        {
          help ();
          return SUCCESS;
        }
      else if (!strcmp ("-m", *argv) || !strcmp ("--mode", *argv))
        {
          argv++;
          argc--;
          if (*argv != NULL && !strcmp ("guest", *argv))
            {
              secvarctl_mode = 1;
              backend_name = GUEST_BACKEND;
            }
          else if (*argv != NULL && !strcmp ("host", *argv))
            {
              secvarctl_mode = 0;
              backend_name = HOST_BACKEND;
            }
          else if (*argv != NULL)
            {
              prlog (PR_WARNING, "\nERROR: %s is unkonwn mode\n", *argv);
              usage ();
              return SUCCESS;
            }
          else
            {
              prlog (PR_WARNING, "\nERROR: mode name is needed\n");
              usage ();
              return SUCCESS;
            }
        }
      else if (!strcmp ("-v", *argv) || !strcmp ("--verbose", *argv))
        verbose = PR_DEBUG;
      else
        {
          usage ();
          return SUCCESS;
        }
    }

  if (argc <= 0)
    {
      prlog (PR_ERR, "\nERROR: commands not found\n");
      usage ();
      return ARG_PARSE_FAIL;
    }

  if (secvarctl_mode == -1)
    {
      usage ();
      return SUCCESS;
    }

  /* if backend is not edk2-compat print continuing despite some funtionality not working */
  backend = get_backend ();
  if (!backend)
    {

      if (is_known_backend (backend_name, &backend))
        prlog (PR_WARNING,
               "WARNING: unsupported backend detected, assuming "
               "%s\nread/write may not work as expected\n",
               backend->name);
      else
        {
          prlog (PR_WARNING, "WARNING!! %s mode is not enabled.\n",
                 (secvarctl_mode ? "guest" : "host"));
          return SUCCESS;
        }
    }

  /* next command should be one of main subcommands */
  subcommand = *argv;

  rc = UNKNOWN_COMMAND;
  for (i = 0; i < backend->countCmds; i++)
    {
      if (!strncmp (subcommand, backend->commands[i].name, 32))
        {
          rc = backend->commands[i].func (argc, argv);
          break;
        }
    }

  if (rc == UNKNOWN_COMMAND)
    {
      prlog (PR_ERR, "ERROR: unknown command %s\n", subcommand);
      usage ();
    }

  return rc;
}
