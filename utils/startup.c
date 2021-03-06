/*************************************************************************
    > File Name: startup.c
    > Author: Kevin
    > Created Time: 2019-12-20
    > Description:
 ************************************************************************/

/* Includes *********************************************************** */
#include <stdio.h>
#include <stdlib.h>

#include <setjmp.h>
#include <pthread.h>

#include "startup.h"
#include "logging.h"

#include "cli.h"
#include "mng.h"
#include "nwk.h"
#include "cfg.h"

/* Defines  *********************************************************** */
#define CONFIG_CACHE_COMMENT                                                   \
  "########################################################################\n" \
  "# ** Startup Config File **\n"                                              \
  "#   - DO NOT EDIT IT MANUALLY IF YOU ARE NOT SURE ABOUT THE RULES\n"        \
  "#   - The first config entry should always be if the connection between\n"  \
  "#     NCP host and target is encrypted or not\n"                            \
  "#   - For more information, look into the source code\n"                    \
  "########################################################################\n"

enum {
  ARG_DIRTY_ENC,
  ARG_DIRTY_PORT,
  ARG_DIRTY_BR,
  ARG_DIRTY_SOCK_SRV,
  ARG_DIRTY_SOCK_CLT,
  ARG_DIRTY_SOCK_ENC,
  ARG_MAX_INVALID
};

#define ARG_KEY_ISENC "Encryption"
#define ARG_KEY_PORT "Port"
#define ARG_KEY_BAUDRATE "Baud Rate"
#define ARG_KEY_SOCK_SRV "Socket Server"
#define ARG_KEY_SOCK_CLT "Socket Client"
#define ARG_KEY_SOCK_ENC "Socket Encription"

/* Global Variables *************************************************** */
pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;
jmp_buf initjmpbuf;

/* Static Variables *************************************************** */
static const char *arg_keys[] = {
  ARG_KEY_ISENC,
  ARG_KEY_PORT,
  ARG_KEY_BAUDRATE,
  ARG_KEY_SOCK_SRV,
  ARG_KEY_SOCK_CLT,
  ARG_KEY_SOCK_ENC,
};

static struct {
  bool started;
  pthread_t tid;
} mng_info;

static proj_args_t projargs = { 0 };
static init_func_t initfs[] = {
  cli_init,
  clr_all,
  init_ncp,
  cfg_init,
  mng_init,
};
static const int inits_num = ARR_LEN(initfs);

/* Static Functions Declaractions ************************************* */
static void setprojargs(int argc, char *argv[]);

/* extern void err_selftest(void); */
void startup(int argc, char *argv[])
{
  err_t e;
  if (ec_success != (e = cli_proc_init(0, NULL))) {
    elog(e);
    return;
  }
  /* err_selftest(); */
  /* exit(0); */
  cli_proc(argc, argv); /* should never return */

  logging_deinit();
}

int offsetof_initfunc(init_func_t fn)
{
  int i;
  for (i = 0; i < inits_num; i++) {
    if (initfs[i] == fn) {
      break;
    }
  }
  return i;
}

int cli_proc(int argc, char *argv[])
{
  int ret, tmp;
  err_t e;

#if (__APPLE__ == 1)
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
#endif
  setprojargs(argc, argv);

  ret = setjmp(initjmpbuf);
  LOGM("Program <VERSION - %d.%d.%d> Started Up, Initialization Bitmap - 0x%x\n",
       PROJ_VERSION_MAJOR,
       PROJ_VERSION_MINOR,
       PROJ_VERSION_PATCH,
       ret);
  if (mng_info.started) {
    if (0 != (tmp = pthread_cancel(mng_info.tid))) {
      LOGE("Cancel pthread error[%d:%s]\n", tmp, strerror(tmp));
    }
    get_mng()->state = nil;
  }

  mng_info.started = false;
  if (ret == 0) {
    ret = FULL_RESET;
  }
  for (int i = 0; i < sizeof(int) * 8; i++) {
    if (!IS_BIT_SET(ret, i)) {
      continue;
    }
    if (ec_success != (e = initfs[i](NULL))) {
      elog(e);
      exit(EXIT_FAILURE);
    }
  }

  e = nwk_init(NULL);
  elog(e);

  if (0 != (ret = pthread_create(&mng_info.tid, NULL, mng_mainloop, NULL))) {
    err_exit_en(ret, "pthread create");
  }
  mng_info.started = true;

  cli_mainloop(NULL);

  if (0 != (ret = pthread_join(mng_info.tid, NULL))) {
    err_exit_en(ret, "pthread_join");
  }

  return 0;
}

/**
 * @defgroup project arguments
 * @{ */
static void print_usage(const char *name)
{
  fprintf(stderr, "Usage: %s [arguments...]\n"
                  "       -m mode [s/i]                       s - Secure NCP, i - Insecure NCP\n"
                  "       -p serial_port                      Valid in Insecure Mode\n"
                  "       -b baud_rate                        Valid in Insecure Mode\n"
                  "       -s server_domain_socket_path        Valid in Secure Mode\n"
                  "       -c client_domain_socket_path        Valid in Secure Mode\n"
                  "       -e is_domain_socket_encrypted[1/0]  Valid in Secure Mode\n",
          name);
  exit(EXIT_FAILURE);
}

const proj_args_t *getprojargs(void)
{
  return &projargs;
}

static void store_args(lbitmap_t *dirty, int argc, char *argv[])
{
  int c;
  while (-1 != (c = getopt(argc, argv, "m:p:b:s:c:e:f:"))) {
    switch (c) {
      case 'm':
        BIT_SET(*dirty, ARG_DIRTY_ENC);
        if (optarg[0] == 's') {
          projargs.enc = true;
        } else if (optarg[0] == 'i') {
          projargs.enc = false;
        } else {
          printf("-m arg INVALID\n");
          print_usage(argv[0]);
          exit(0);
        }
        break;
      case 'p':
        BIT_SET(*dirty, ARG_DIRTY_PORT);
        strcpy(projargs.serial.port, optarg);
        break;
      case 'b':
        BIT_SET(*dirty, ARG_DIRTY_BR);
        projargs.serial.br = atoi(optarg);
        break;
      case 's':
        BIT_SET(*dirty, ARG_DIRTY_SOCK_SRV);
        strcpy(projargs.sock.srv, optarg);
        break;
      case 'c':
        BIT_SET(*dirty, ARG_DIRTY_SOCK_CLT);
        strcpy(projargs.sock.clt, optarg);
        break;
      case 'e':
        BIT_SET(*dirty, ARG_DIRTY_SOCK_ENC);
        projargs.sock.enc = (bool)atoi(optarg);
        break;
      default:
        printf("Argument Not Realized\n");
        print_usage(argv[0]);
        exit(1);
        break;
    }
  }
}

static inline void __store_cache_config(const char *str, uint8_t key)
{
  char *val;
  val = strchr(str, '=');
  if (!val) {
    printf("Format error in .config file.\n"
           "Expected - key = value\n");
    exit(1);
  }
  val++;
  while (*val == ' ') {
    val++;
  }
  if (key == ARG_DIRTY_ENC) {
    if (val[0] == '0') {
      projargs.enc = false;
    } else {
      projargs.enc = true;
    }
  } else if (key == ARG_DIRTY_PORT) {
    if (projargs.enc) {
      return;
    }
    strcpy(projargs.serial.port, val);
  } else if (key == ARG_DIRTY_BR) {
    if (projargs.enc) {
      return;
    }
    projargs.serial.br = atoi(val);
  } else if (key == ARG_DIRTY_SOCK_SRV) {
    if (!projargs.enc) {
      return;
    }
    strcpy(projargs.sock.srv, val);
  } else if (key == ARG_DIRTY_SOCK_CLT) {
    if (!projargs.enc) {
      return;
    }
    strcpy(projargs.sock.clt, val);
  } else if (key == ARG_DIRTY_SOCK_ENC) {
    if (!projargs.enc) {
      return;
    }
    projargs.sock.enc = (bool)atoi(val);
  }
}

#define LINE_MAX_LENGTH 100
#define FMT "%s = %s\n"
static inline void __append_cfg(char *buf, const char *key, const char *val)
{
  char line[LINE_MAX_LENGTH] = { 0 };
  sprintf(line, FMT, key, val);
  strcat(buf, line);
}

static void setprojargs(int argc, char *argv[])
{
  size_t len;
  FILE *fp;
  lbitmap_t dirty = 0;
  char tmp[LINE_MAX_LENGTH];
  char *buf = NULL;

  if (argc > 1 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
    print_usage(argv[0]);
    exit(0);
  }

  if (argc > 1) {
    store_args(&dirty, argc, argv);
  }

  if (-1 == access(CONFIG_CACHE_FILE_PATH, F_OK)) {
    /* File not exist */
    fp = fopen(CONFIG_CACHE_FILE_PATH, "w");
    fclose(fp);
  }

  fp = fopen(CONFIG_CACHE_FILE_PATH, "r");
  fseek(fp, 0, SEEK_END);
  len = ftell(fp);
  buf = calloc(1, len + ARG_MAX_INVALID * LINE_MAX_LENGTH);

  if (len) {
    rewind(fp);
    while (NULL != fgets(tmp, LINE_MAX_LENGTH, fp)) {
      if (tmp[0] == '#') {
        continue;
      }
      for (int i = 0; i < ARG_MAX_INVALID; i++) {
        if (!strstr(tmp, arg_keys[i])) {
          continue;
        }
        if (IS_BIT_SET(dirty, i)) {
          /* Handle the encription or not here */
          if (i == ARG_DIRTY_ENC) {
            char *val;
            val = strchr(tmp, '=');
            if (!val) {
              printf("Format error in .config file.\n"
                     "Expected - key = value\n");
              exit(1);
            }
            val++;
            while (*val == ' ') {
              val++;
            }
            *val = projargs.enc ? '1' : '0';
            strcat(buf, tmp);
            BIT_CLR(dirty, ARG_DIRTY_ENC);
          }
          break;
        }
        /* Not dirty, load it */
        strcat(buf, tmp);
        __store_cache_config(tmp, i);
      }
      memset(tmp, 0, LINE_MAX_LENGTH);
    }
  }

  /* Sanity check */
  if ((projargs.enc && (!projargs.sock.srv[0] || !projargs.sock.clt[0]))
      || (!projargs.enc && (!projargs.serial.port[0] || !projargs.serial.br))) {
    printf("**Arguments ERROR** - check the arguments and the .config file\n");
    print_usage(argv[0]);
    exit(1);
  }

  if (dirty) {
    for (int i = 0; i < ARG_MAX_INVALID && dirty; i++) {
      if (!IS_BIT_SET(dirty, i)) {
        continue;
      }
      if (i == ARG_DIRTY_ENC) {
        __append_cfg(buf, arg_keys[i], projargs.enc ? "1" : "0");
      } else if (i == ARG_DIRTY_PORT) {
        __append_cfg(buf, arg_keys[i], projargs.serial.port);
      } else if (i == ARG_DIRTY_BR) {
        char v[20] = { 0 };
        sprintf(v, "%u", projargs.serial.br);
        __append_cfg(buf, arg_keys[i], v);
      } else if (i == ARG_DIRTY_SOCK_SRV) {
        __append_cfg(buf, arg_keys[i], projargs.sock.srv);
      } else if (i == ARG_DIRTY_SOCK_CLT) {
        __append_cfg(buf, arg_keys[i], projargs.sock.clt);
      } else if (i == ARG_DIRTY_SOCK_ENC) {
        char v[20] = { 0 };
        sprintf(v, "%d", projargs.sock.enc);
        __append_cfg(buf, arg_keys[i], v);
      } else {
        ASSERT(0);
      }
      BIT_CLR(dirty, i);
    }
  }

  /* Write back to the file */
  LOGD("startup args dump\n%s\n", buf);
  fclose(fp);
  fp = fopen(CONFIG_CACHE_FILE_PATH, "w");
  fwrite(CONFIG_CACHE_COMMENT, strlen(CONFIG_CACHE_COMMENT), 1, fp);
  fwrite(buf, strlen(buf), 1, fp);
  fclose(fp);
  char *r;
  r = strrchr(projargs.serial.port, '\n');
  if (r) {
    *r = '\0';
  }
  r = strrchr(projargs.sock.clt, '\n');
  if (r) {
    *r = '\0';
  }
  r = strrchr(projargs.sock.srv, '\n');
  if (r) {
    *r = '\0';
  }

  projargs.initialized = true;
}
/**  @} */
