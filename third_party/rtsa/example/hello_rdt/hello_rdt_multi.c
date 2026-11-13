/*************************************************************
 * File  :  hello_rdt_multi.c
 * Module:  Agora SD-RTN SDK RTC C API demo application.
 *
 * This is a part of the Agora RTC Service SDK.
 * Copyright (C) 2020 Agora IO
 * All rights reserved.
 *
 *************************************************************/

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <pthread.h>

#include "pacer.h"
#include "utility.h"
#include "agora_rtc_api.h"
#include "md5.h"
#include "json_parser/include/jsmn.h"

#define TAG_APP "[app]"
#define TAG_API "[api]"
#define TAG_EVENT "[event]"

#define INVALID_FD -1

#define DEFAULT_CHANNEL_NAME "demo"
#define DEFAULT_SEND_FILE_NAME "hello_rdt"
#define SEND_FILE_PACKAGE_LENGTH (1024)
#define DEFAULT_MAX_USER_COUNT (3)
#define LOG_DIR_MAX_LENGTH (256)

#define LOGS(fmt, ...) fprintf(stdout, "" fmt "\n", ##__VA_ARGS__)
#define LOGI(fmt, ...) fprintf(stdout, "I/ " fmt "\n", ##__VA_ARGS__)
#define LOGD(fmt, ...) fprintf(stdout, "D/ " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) fprintf(stdout, "E/ " fmt "\n", ##__VA_ARGS__)
#define LOGW(fmt, ...) fprintf(stdout, "W/ " fmt "\n", ##__VA_ARGS__)

typedef struct {
  const char *p_sdk_log_dir;
  const char *p_appid;
  const char *p_token;
  const char *p_channel;
  uint32_t uid;
  uint32_t conn_id;
  int is_enable_send;
  int is_enable_repeat;
  int send_kbps;
  const char *p_send_file;
  const char *p_send_str;
} app_config_t;

typedef enum {
  FILE_START_FLAG,
  FILE_END_FLAG,
  FILE_DATA_FLAG,
} data_type_e;

typedef struct {
  char name[128];     // file name
  char md5_str[33];   // md5 hex string
  int  total_size;    // file total size
  FILE *fp;           // file pointer
  MD5_CTX md5_ctx;    // md5 ctx

  int          send_finished;                  // whether send finished
  int          have_sent_size;                 // have sent size total for this file
  char         buf[SEND_FILE_PACKAGE_LENGTH];  // send buffer
  int          buf_valid_flag;                 // send buffer whether is valid or not
  int          buf_valid_size;                 // buffer size of valid
  data_type_e  buf_data_type;                  // buffer data type

  uint64_t start_send_ts;
  uint64_t end_send_ts;
} file_send_ctx_t;

typedef struct {
  char name[128];     // file name
  char md5_str[33];   // md5 hex string
  int  total_size;    // file total size
  FILE *fp;           // file pointer
  MD5_CTX md5_ctx;    // md5 ctx

  int  recv_finished;                          // whether recv finished
  int  have_recv_size;                         // have recevied size total for this file
  uint64_t start_recv_ts;
  uint64_t end_recv_ts;
} file_recv_ctx_t;

typedef struct {
  uint32_t peer_uid;
  int32_t b_rdt_opened_flag;
  int32_t b_send_stop_flag;
  file_send_ctx_t send_ctx;
  file_recv_ctx_t recv_ctx;
  pthread_mutex_t ctx_mtx;
} rdt_tunnel_t;

typedef struct {
  app_config_t config;
  int32_t      b_exit_flag;
  int32_t      b_join_success_flag;
  int32_t      tunnel_cnt;
  rdt_tunnel_t tunnel[DEFAULT_MAX_USER_COUNT];
} app_t;

static app_t g_app_instance = {
  .config = {
      .p_sdk_log_dir              =   "io.agora.rtc_sdk",
      .p_appid                    =   "",
      .p_channel                  =   DEFAULT_CHANNEL_NAME,
      .p_token                    =   "",
      .uid                        =   0,
      .conn_id                    =   0,
      .is_enable_send             =   0,
      .is_enable_repeat           =   0,
      .send_kbps                  =   1000,
      .p_send_file                =   DEFAULT_SEND_FILE_NAME,
      .p_send_str                 =   "hello agora",
  },
  .b_exit_flag            = 0,
  .b_join_success_flag    = 0,
  .tunnel_cnt             = 0,
  .tunnel                 = {0},
};

app_t *app_get_instance(void)
{
  return &g_app_instance;
}

static void app_signal_handler(int32_t sig)
{
  app_t *p_app = app_get_instance();
  switch (sig) {
  case SIGQUIT:
  case SIGABRT:
  case SIGINT:
    p_app->b_exit_flag = 1;
    break;
  default:
    LOGW("no handler, sig=%d", sig);
  }
}

void app_print_usage(int32_t argc, char **argv)
{
  LOGS("\nUsage: %s [OPTION]", argv[0]);
  LOGS(" -h, --help                : show help info");
  LOGS(" -i, --app-id              : application id, either app-id OR token MUST be set");
  LOGS(" -t, --token               : token for authentication");
  LOGS(" -c, --channel-id          : channel, default is 'demo'");
  LOGS(" -u, --local-id            : local id, default is 0");
  LOGS(" -p, --peer-id             : peer id, default is 0");
  LOGS(" -s, --send                : send file name via RTD");
  LOGS(" -k, --send-kbps           : send kbps, default 1000");
  LOGS(" -f, --send-file           : send file name");
  LOGS(" -r, --send-repeat         : send file repeat when file end");
  LOGS(" -l, --log-dir             : agora SDK log directory, default is '.'");

  LOGS("\nExample:");
  LOGS("    %s -i xxx [-t xxx] -c $ChannelName -u $SelfUid -p $PeerUid -s [-f xxx] [-k 100]", argv[0]);
}

int32_t app_parse_args(app_config_t *p_config, int32_t argc, char **argv)
{
  const char *av_short_option = "hi:t:c:u:l:k:srf:";
  const struct option av_long_option[] = { { "help", 0, NULL, 'h' },
                                           { "app-id", 1, NULL, 'i' },
                                           { "token", 1, NULL, 't' },
                                           { "channel-id", 1, NULL, 'c' },
                                           { "local-id", 1, NULL, 'u' },
                                           { "send", 0, NULL, 's' },
                                           { "send-kbps", 1, NULL, 'k' },
                                           { "send-file", 1, NULL, 'f' },
                                           { "send-repeat", 0, NULL, 'r' },
                                           { "log-dir", 1, NULL, 'l' },
                                           { 0, 0, 0, 0 } };

  int32_t ch = -1;
  int32_t optidx = 0;
  int32_t rval = 0;

  while (1) {
    ch = getopt_long(argc, argv, av_short_option, av_long_option, &optidx);
    if (ch == -1) {
      break;
    }

    switch (ch) {
    case 'h': {
      return -1;
    } break;
    case 'i': {
      p_config->p_appid = optarg;
    } break;
    case 't': {
      p_config->p_token = optarg;
    } break;
    case 'c': {
      p_config->p_channel = optarg;
    } break;
    case 'u': {
      p_config->uid = strtoul(optarg, NULL, 10);
    } break;
    case 's': {
      p_config->is_enable_send = 1;
    } break;
    case 'k': {
      p_config->send_kbps = strtoul(optarg, NULL, 10);
    } break;
    case 'f': {
      p_config->p_send_file = optarg;
    } break;
    case 'r': {
      p_config->is_enable_repeat = 1;
    } break;
    case 'l': {
      p_config->p_sdk_log_dir = optarg;
    } break;
    default: {
      LOGS("%s parse cmd param: %s error.", TAG_APP, av_long_option[optidx].name);
      return -1;
    }
    }
  }

  // check key parameters
  if (strcmp(p_config->p_appid, "") == 0) {
    LOGE("%s appid MUST be provided", TAG_APP);
    return -1;
  }

  if (!p_config->p_channel || strcmp(p_config->p_channel, "") == 0) {
    LOGE("%s invalid channel", TAG_APP);
    return -1;
  }

  if (access(p_config->p_send_file, R_OK) != 0) {
    LOGE("%s not have read permissions for send file %s", TAG_APP, p_config->p_send_file);
    return -1;
  }

  return 0;
}

static int32_t app_init(app_t *p_app)
{
  int32_t rval = 0;

  signal(SIGQUIT, app_signal_handler);
  signal(SIGABRT, app_signal_handler);
  signal(SIGINT, app_signal_handler);

  app_config_t *p_config = &p_app->config;

  for (int i = 0; i < DEFAULT_MAX_USER_COUNT; i++) {
    pthread_mutex_init(&p_app->tunnel[i].ctx_mtx, NULL);
  }

  return 0;
}

static void app_deinit(app_t *p_app)
{
  p_app->b_join_success_flag = 0;
  for (int i = 0; i < DEFAULT_MAX_USER_COUNT; i++) {
    pthread_mutex_destroy(&p_app->tunnel[i].ctx_mtx);
  }

}

static int jsoneq(const char *json, jsmntok_t *tok, const char *s)
{
  if (tok->type == JSMN_STRING && (int)strlen(s) == tok->end - tok->start &&
      strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
    return 0;
  }
  return -1;
}

static void md5_hex_transfer(unsigned char src[16], char dest[33])
{
  for (int i = 0; i < 16; i++) {
    snprintf(dest + i * 2, 2 + 1, "%02x", src[i]);
  }
}

static rdt_tunnel_t* get_rdt_tunnel(app_t *p_app, uint32_t peer_uid)
{
  int i = 0;

  // find exist tunnel
  for (i = 0; i < p_app->tunnel_cnt; i++) {
    if (p_app->tunnel[i].peer_uid == peer_uid) {
      return &p_app->tunnel[i];
    }
  }

  // new tunnel
  if (p_app->tunnel_cnt == DEFAULT_MAX_USER_COUNT) {
    LOGW("%s more than %d users, ignore uid=%u", TAG_APP, DEFAULT_MAX_USER_COUNT, peer_uid);
    return NULL; // reach max peer count
  }
  p_app->tunnel[p_app->tunnel_cnt].peer_uid = peer_uid;
  return &p_app->tunnel[p_app->tunnel_cnt++];
}

static void file_send_ctx_reset(file_send_ctx_t *ctx)
{
  if (!ctx) return;
  if (ctx->fp) {
    fclose(ctx->fp);
    ctx->fp = NULL;
  }
  memset(ctx, 0, sizeof(file_send_ctx_t));
}

static void file_recv_ctx_reset(file_recv_ctx_t *ctx)
{
  if (!ctx) return;
  if (ctx->fp) {
    fflush(ctx->fp);
    fclose(ctx->fp);
    ctx->fp = NULL;
  }
  memset(ctx, 0, sizeof(file_recv_ctx_t));
}

static void print_send_progess(rdt_tunnel_t *tunnel)
{
  file_send_ctx_t *send_ctx = &tunnel->send_ctx;
  static uint64_t last_print_ts = 0;
  static uint64_t curr_print_ts = 0;
  static uint32_t last_sent_size = 0;
  static uint32_t curr_sent_size = 0;
  uint64_t diff_time = 0;
  uint32_t diff_size = 0;

  curr_print_ts = util_get_time_ms();
  diff_time = curr_print_ts - last_print_ts;
  if (!send_ctx->send_finished && diff_time < 1000) {
    return;
  }
  last_print_ts = curr_print_ts;
  if (diff_time == 0) diff_time = 1;

  curr_sent_size = send_ctx->have_sent_size;
  diff_size = curr_sent_size - last_sent_size;
  last_sent_size = curr_sent_size;
  float send_kbps = (float)diff_size * 8.0 / diff_time;
  float progress  = (float)send_ctx->have_sent_size * 100.0 / send_ctx->total_size;
  LOGD("%s [uid-%u] send progres=%.2f%% speed=%.2fkbps", TAG_APP, tunnel->peer_uid, progress, send_kbps);
  if (send_ctx->send_finished) {
    diff_time = send_ctx->end_send_ts - send_ctx->start_send_ts;
    if (diff_time == 0) diff_time = 1;
    float send_kbps_avg = send_ctx->total_size * 8 / diff_time;
    LOGD("%s [uid-%u] send file successful, total_size=%dKB time_spend=%us avg_kbps=%.2fkbps",
        TAG_APP, tunnel->peer_uid, send_ctx->total_size/1024, (uint32_t)diff_time/1000, send_kbps_avg);
  }
}

static void print_recv_progress(rdt_tunnel_t *tunnel)
{
  file_recv_ctx_t *recv_ctx = &tunnel->recv_ctx;
  static uint64_t last_print_ts = 0;
  static uint64_t curr_print_ts = 0;
  static uint32_t last_recv_size = 0;
  static uint32_t curr_recv_size = 0;
  uint64_t diff_time = 0;
  uint32_t diff_size = 0;

  curr_print_ts = util_get_time_ms();
  diff_time = curr_print_ts - last_print_ts;
  if (!recv_ctx->recv_finished && diff_time < 1000) {
    return;
  }
  last_print_ts = curr_print_ts;
  if (diff_time == 0) diff_time = 1;

  curr_recv_size = recv_ctx->have_recv_size;
  diff_size = curr_recv_size - last_recv_size;
  last_recv_size = curr_recv_size;
  float recv_kbps = (float)diff_size * 8.0 / diff_time;
  float progress  = (float)recv_ctx->have_recv_size * 100.0 / recv_ctx->total_size;
  LOGD("%s [uid-%u] recv progress=%.2f%% speed=%.2fkbps", TAG_APP, tunnel->peer_uid, progress, recv_kbps);
  if (recv_ctx->recv_finished) {
    diff_time = recv_ctx->end_recv_ts - recv_ctx->start_recv_ts;
    if (diff_time == 0) diff_time = 1;
    float recv_kbps_avg = recv_ctx->total_size * 8 / diff_time;
    LOGD("%s [uid-%u] recv file successful, total_size=%dKB time_spend=%us avg_kbps=%.2fkbps",
         TAG_APP, tunnel->peer_uid, recv_ctx->total_size/1024, (uint32_t)diff_time/1000, recv_kbps_avg);
  }
}

static int send_file_start(rdt_tunnel_t *tunnel)
{
  // 1. open file
  app_t *p_app = app_get_instance();
  file_send_ctx_t *send_ctx = &tunnel->send_ctx;
  file_send_ctx_reset(send_ctx);
  snprintf(send_ctx->name, sizeof(send_ctx->name), "%s", p_app->config.p_send_file);
  FILE* fp = fopen(send_ctx->name, "rb");
  if (fp == NULL) {
    LOGE("can not open send file %s", send_ctx->name);
    p_app->b_exit_flag = 1;
    return -1;
  }
  send_ctx->fp = fp;

  // 2. get file size
  fseek(fp, 0, SEEK_END);
  int len = ftell(fp);
  if (len <= 0) {
    LOGE("there is not any data in file %s", send_ctx->name);
    fclose(fp);
    send_ctx->fp = NULL;
    p_app->b_exit_flag = 1;
    return -1;
  }
  fseek(fp, 0, SEEK_SET);
  send_ctx->total_size = len;

  // 3. get file MD5
  MD5_CTX *md5_ctx = &send_ctx->md5_ctx;
  MD5Init(md5_ctx);
  while (!feof(fp)) {
    int size = fread(send_ctx->buf, 1, sizeof(send_ctx->buf), fp);
    MD5Update(md5_ctx, (unsigned char*)send_ctx->buf, size);
  }
  fseek(fp, 0, SEEK_SET);
  unsigned char md5[16] = { 0 };
  MD5Final(md5_ctx, md5);
  md5_hex_transfer(md5, send_ctx->md5_str);

  // 4. send start package
  send_ctx->start_send_ts = util_get_time_ms();
  len = snprintf(send_ctx->buf, sizeof(send_ctx->buf), "{\"name\": \"%s\", \"size\": %d, \"md5\": \"%s\"}",
           send_ctx->name, len, send_ctx->md5_str);
  int rval = agora_rtc_send_rdt_msg(p_app->config.conn_id, tunnel->peer_uid, RDT_STREAM_DATA,
                                    send_ctx->buf, len + 1); // include end char '\0' for json parser
  if (rval == 0) {
    send_ctx->buf_valid_flag = 0;
    LOGD("%s send start file: %s", TAG_APP, send_ctx->buf);
    return 0;
  } else {
    send_ctx->buf_valid_flag = 1;
    send_ctx->buf_valid_size = len + 1;
    send_ctx->buf_data_type = FILE_START_FLAG;
    LOGD("%s send start file failed", TAG_APP);
    return -1;
  }
}

static int send_file_end(rdt_tunnel_t *tunnel)
{
  app_t *p_app = app_get_instance();
  file_send_ctx_t *send_ctx = &tunnel->send_ctx;
  int len = snprintf(send_ctx->buf, SEND_FILE_PACKAGE_LENGTH, "{\"eof\": true}");
  int rval = agora_rtc_send_rdt_msg(p_app->config.conn_id, tunnel->peer_uid, RDT_STREAM_DATA,
                                    send_ctx->buf, len + 1); // include end char '\0' for json parser
  if (rval == 0) {
    send_ctx->buf_valid_flag = 0;
    LOGD("%s send end package: %s", TAG_APP, send_ctx->buf);
    return 0;
  } else {
    send_ctx->buf_valid_flag = 1;
    send_ctx->buf_valid_size = len + 1;
    send_ctx->buf_data_type = FILE_END_FLAG;
    LOGD("%s send end package failed", TAG_APP);
    return -1;
  }
}

static int send_file(rdt_tunnel_t *tunnel)
{
  app_t *p_app = app_get_instance();
  file_send_ctx_t *send_ctx = &tunnel->send_ctx;

  // check should send file start
  if (!send_ctx->fp) {
    if (send_file_start(tunnel) != 0) {
      return -1; // wait for next time send
    }
  }

  // check buf vaild last time send
  if (send_ctx->buf_valid_flag) {
    int rval = agora_rtc_send_rdt_msg(p_app->config.conn_id, tunnel->peer_uid, RDT_STREAM_DATA,
                                      send_ctx->buf, send_ctx->buf_valid_size);
    if (rval == 0) {
      send_ctx->buf_valid_flag = 0;
      if (send_ctx->buf_data_type == FILE_DATA_FLAG) {
        send_ctx->have_sent_size += send_ctx->buf_valid_size;
      }
      if (send_ctx->buf_data_type == FILE_END_FLAG) {
        return 0;
      }
    } else {
      // wait for next time send
      return -1;
    }
  }

  // check whether the send queue is cleared.
  // If multiple files are sent consecutively, you do not need to check them one by one
  if (send_ctx->have_sent_size == send_ctx->total_size) {
    rdt_status_info_t info = {0};
    if (agora_rtc_get_rdt_status_info(p_app->config.conn_id, tunnel->peer_uid, &info) == 0) {
      if (info.send_queue_size[RDT_STREAM_DATA] == 0) {
          send_ctx->send_finished = 1;
          send_ctx->end_send_ts = util_get_time_ms();
          print_send_progess(tunnel);
          file_send_ctx_reset(send_ctx);

          // stop send when not set repeat config
          if (!p_app->config.is_enable_repeat) {
            tunnel->b_send_stop_flag = 1;
          }
      }
    }
    return 0;
  }

  // read and send new data from file
  int size = fread(send_ctx->buf, 1, SEND_FILE_PACKAGE_LENGTH, send_ctx->fp);
  int rval = agora_rtc_send_rdt_msg(p_app->config.conn_id, tunnel->peer_uid, RDT_STREAM_DATA, send_ctx->buf, size);
  if (rval == 0) {
    send_ctx->buf_valid_flag = 0;
    send_ctx->have_sent_size += size;
  } else {
    send_ctx->buf_valid_flag = 1;
    send_ctx->buf_valid_size = size;
    send_ctx->buf_data_type = FILE_DATA_FLAG;
  }
  // check should send file end
  if (feof(send_ctx->fp)) {
    send_file_end(tunnel);
  }

  print_send_progess(tunnel);

  return 0;
}

static int recv_file_start(rdt_tunnel_t *tunnel, const void *msg, size_t length)
{
  app_t *p_app = app_get_instance();
  file_recv_ctx_t *recv_ctx = &tunnel->recv_ctx;
  file_recv_ctx_t recv_ctx_tmp = {0};
  jsmn_parser js_parser = { 0 };
  jsmntok_t js_tokens[8] = { 0 };
  jsmn_init(&js_parser);
  int js_token_num = jsmn_parse(&js_parser, msg, length, js_tokens, sizeof(js_tokens) / sizeof(jsmntok_t));
  if (js_token_num < 1 || js_tokens[0].type != JSMN_OBJECT) {
    // not file start
    return -1;
  }
  if (jsoneq(msg, &js_tokens[1], "name") != 0) {
    // not file start
    return -1;
  }
  // parser start package, format like as: {"name": "xxxxx", "size": xxxx, "md5": "xxxxxxxxxxxxxxxx"}
  for (int i = 1; i < js_token_num; i++) {
    if (jsoneq(msg, &js_tokens[i], "name") == 0) {
      i++;
      memcpy(recv_ctx_tmp.name, (char *)msg + js_tokens[i].start,
              js_tokens[i].end - js_tokens[i].start);
    } else if (jsoneq(msg, &js_tokens[i], "size") == 0) {
      i++;
      recv_ctx_tmp.total_size = atoi(msg + js_tokens[i].start);
    } else if (jsoneq(msg, &js_tokens[i], "md5") == 0) {
      i++;
      memcpy(recv_ctx_tmp.md5_str, msg + js_tokens[i].start, js_tokens[i].end - js_tokens[i].start);
    } else {
      LOGD("Unexpected start package format: %s", (const char *)msg);
      return -1;
    }
  }

  LOGD("received start package: %s", (char *)msg);

  file_recv_ctx_reset(recv_ctx);
  *recv_ctx = recv_ctx_tmp;
  recv_ctx->start_recv_ts = util_get_time_ms();
  char name[512];
  snprintf(name, sizeof(name), "recv_%s", recv_ctx->name);
  recv_ctx->fp = fopen(name, "wb");
  if (!recv_ctx->fp) {
    LOGE("can not create file %s", name);
    p_app->b_exit_flag = 1;
  }
  MD5Init(&recv_ctx->md5_ctx);
  return 0;
}

static int recv_file_end(rdt_tunnel_t *tunnel, const void *msg, size_t length)
{
  file_recv_ctx_t *recv_ctx = &tunnel->recv_ctx;
  jsmn_parser js_parser = { 0 };
  jsmntok_t js_tokens[8] = { 0 };
  jsmn_init(&js_parser);
  int js_token_num = jsmn_parse(&js_parser, msg, length, js_tokens, sizeof(js_tokens) / sizeof(jsmntok_t));
  if (js_token_num < 1 || js_tokens[0].type != JSMN_OBJECT) {
    // not file end
    return -1;
  }
  if (jsoneq(msg, &js_tokens[1], "eof") != 0) {
    // not file end
    return -1;
  }

  // parser end package, format like as: {"eof": true}
  for (int i = 1; i < js_token_num; i++) {
    if (jsoneq(msg, &js_tokens[i], "eof") == 0) {
      i++;
    } else {
      LOGD("Unexpected end package format");
      return -1;
    }
  }

  LOGD("received end package: %s", (char *)msg);

  if (!recv_ctx->fp) {
    LOGD("Have not start receive file.");
    return 0;
  }

  recv_ctx->end_recv_ts = util_get_time_ms();
  recv_ctx->recv_finished = 1;

  unsigned char md5[16] = { 0 };
  char md5_str[33] = { 0 };
  MD5Final(&recv_ctx->md5_ctx, md5);
  md5_hex_transfer(md5, md5_str);
  if (0 != strncmp(recv_ctx->md5_str, md5_str, 32)) {
    // MD5 check error
    LOGE("recv file %s failed, MD5 check error %s != %s", recv_ctx->name, recv_ctx->md5_str, md5_str);
    file_recv_ctx_reset(recv_ctx);
    return 0;
  }
  print_recv_progress(tunnel);
  file_recv_ctx_reset(recv_ctx);
  return 0;
}

static void recv_file(rdt_tunnel_t *tunnel, const void *msg, size_t length)
{
  file_recv_ctx_t *recv_ctx = &tunnel->recv_ctx;
  if (SEND_FILE_PACKAGE_LENGTH != length) {
    if (recv_file_start(tunnel, msg, length) == 0) {
      return;
    } else if (recv_file_end(tunnel, msg, length) == 0) {
      return;
    }
  }

  if (recv_ctx->fp == NULL) {
    return;
  }
  recv_ctx->have_recv_size += length;
  int num = fwrite(msg, 1, length, recv_ctx->fp);
  if (num == length) {
    // update MD5
    MD5Update(&recv_ctx->md5_ctx, (unsigned char *)msg, (unsigned int)length);
  } else {
    LOGE("write data to receive file error: %d", num);
  }

  print_recv_progress(tunnel);
}


static void __on_join_channel_success(connection_id_t conn_id, uint32_t uid, int32_t elapsed)
{
  app_t *p_app = app_get_instance();
  p_app->b_join_success_flag = 1;
  LOGI("%s join success, conn_id=%u uid=%u elapsed=%d", TAG_EVENT, conn_id, uid, elapsed);
}

static void __on_rejoin_channel_success(connection_id_t conn_id, uint32_t uid, int elapsed_ms)
{
  app_t *p_app = app_get_instance();
  p_app->b_join_success_flag = 1;
  LOGD("%s rejoin success conn_id=%u elapsed_ms=%d", TAG_EVENT, conn_id, elapsed_ms);
}

static void __on_reconnecting(connection_id_t conn_id)
{
  app_t *p_app = app_get_instance();
  p_app->b_join_success_flag = 0;
  LOGD("%s reconnecting conn_id=%u", TAG_EVENT, conn_id);
}

static void __on_connection_lost(connection_id_t conn_id)
{
  app_t *p_app = app_get_instance();
  p_app->b_join_success_flag = 0;
  LOGD("%s connection lost conn_id=%u", TAG_EVENT, conn_id);
}

static void __on_user_joined(connection_id_t conn_id, uint32_t uid, int elapsed_ms)
{
  LOGD("%s user joined conn_id=%u uid=%u elapsed_ms=%d", TAG_EVENT, conn_id, uid, elapsed_ms);
}

static void __on_user_offline(connection_id_t conn_id, uint32_t uid, int reason)
{
  LOGD("%s user offline conn_id=%u uid=%u reason=%d", TAG_EVENT, conn_id, uid, reason);
}

static void __on_rdt_state(connection_id_t conn_id, uint32_t uid, rdt_state_e state)
{
  LOGD("%s user rdt state conn_id=%u uid=%u state=%d", TAG_EVENT, conn_id, uid, state);

  app_t *p_app = app_get_instance();
  rdt_tunnel_t *tunnel = get_rdt_tunnel(p_app, uid);
  if (!tunnel) return;

  pthread_mutex_lock(&tunnel->ctx_mtx);

  switch (state) {
    case RDT_STATE_CLOSED:
      tunnel->b_rdt_opened_flag = 0;
      if (!p_app->config.is_enable_repeat) {
        tunnel->b_send_stop_flag = 1;
      }
      file_send_ctx_reset(&tunnel->send_ctx);
      file_recv_ctx_reset(&tunnel->recv_ctx);
      break;
    case RDT_STATE_OPENED:
      tunnel->b_rdt_opened_flag = 1;
      break;
    case RDT_STATE_BLOCKED:
    case RDT_STATE_PENDING:
      tunnel->b_rdt_opened_flag = 0;
      break;
    case RDT_STATE_BROKEN:
      tunnel->b_rdt_opened_flag = 0;
      file_send_ctx_reset(&tunnel->send_ctx);
      file_recv_ctx_reset(&tunnel->recv_ctx);
    default:
      break;
  }

  pthread_mutex_unlock(&tunnel->ctx_mtx);
}

static void __on_rdt_msg(connection_id_t conn_id, uint32_t uid, rdt_stream_type_e type, const void *msg, size_t length)
{
  app_t *p_app = app_get_instance();

  if (type == RDT_STREAM_CMD) {
    const char *msg_str = (const char *)msg;
    LOGD("%s conn_id=%u uid=%u length=%zu msg=%s", TAG_EVENT, conn_id, uid, length, msg_str);
    return;
  }
  // LOGD("%s conn_id=%u uid=%u length=%zu", TAG_EVENT, conn_id, uid, length);

  rdt_tunnel_t *tunnel = get_rdt_tunnel(p_app, uid);
  if (!tunnel) return;

  // handle file receive
  pthread_mutex_lock(&tunnel->ctx_mtx);
  recv_file(tunnel, msg, length);
  pthread_mutex_unlock(&tunnel->ctx_mtx);
}

static void __on_error(connection_id_t conn_id, int code, const char *msg)
{
  app_t *p_app = app_get_instance();
  if (code == ERR_INVALID_APP_ID) {
    LOGE("%s invalid app-id, please double-check, code=%u msg=%s", TAG_EVENT, code, msg);
  } else if (code == ERR_INVALID_CHANNEL_NAME) {
    LOGE("%s invalid channel, please double-check, conn_id=%u code=%u msg=%s", TAG_EVENT, conn_id, code, msg);
  } else if (code == ERR_INVALID_TOKEN || code == ERR_TOKEN_EXPIRED) {
    LOGE("%s invalid token, please double-check, code=%u msg=%s", TAG_EVENT, code, msg);
  } else if (code == ERR_VIDEO_SEND_OVER_BANDWIDTH_LIMIT) {
    LOGW("%s send video over bandwdith limit, code=%u msg=%s", TAG_EVENT, code, msg);
  } else {
    LOGW("%s conn_id=%u code=%u msg=%s", TAG_EVENT, conn_id, code, msg);
  }
}

static agora_rtc_event_handler_t event_handler = {
  .on_join_channel_success = __on_join_channel_success,
  .on_connection_lost = __on_connection_lost,
  .on_reconnecting = __on_reconnecting,
  .on_rejoin_channel_success = __on_rejoin_channel_success,
  .on_user_joined = __on_user_joined,
  .on_user_offline = __on_user_offline,
  .on_rdt_state = __on_rdt_state,
  .on_rdt_msg = __on_rdt_msg,
  .on_error = __on_error,
};

int32_t main(int32_t argc, char **argv)
{
  int i = 0;
  app_t *p_app = app_get_instance();
  app_config_t *p_config = &p_app->config;

  // 0. app parse args
  int32_t rval = app_parse_args(p_config, argc, argv);
  if (rval != 0) {
    app_print_usage(argc, argv);
    goto EXIT;
  }

  LOGS("%s Welcome to RTSA SDK v%s", TAG_APP, agora_rtc_get_version());

  // 1. app init
  rval = app_init(p_app);
  if (rval < 0) {
    LOGE("%s init failed, rval=%d", TAG_APP, rval);
    goto EXIT;
  }

  // 3. API: init agora rtc sdk
  int32_t appid_len = strlen(p_config->p_appid);
  void *p_appid = (void *)(appid_len == 0 ? NULL : p_config->p_appid);
  rtc_service_option_t serv_opt = { 0 };
  serv_opt.log_cfg.log_path = p_config->p_sdk_log_dir;
  rval = agora_rtc_init(p_appid, &event_handler, &serv_opt);
  if (rval < 0) {
    LOGE("%s agora sdk init failed, rval=%d error=%s", TAG_API, rval, agora_rtc_err_2_str(rval));
    goto EXIT;
  }

  agora_rtc_set_log_level(RTC_LOG_INFO);

  rval = agora_rtc_create_connection(&p_config->conn_id);
  if (rval != 0) {
    LOGE("%s agora_rtc_create_connection failed, rval=%d error=%s", TAG_API, rval, agora_rtc_err_2_str(rval));
    goto EXIT;
  }

  // 4. API: join channel
  rtc_channel_options_t channel_options = { 0 };
  channel_options.auto_subscribe_audio = false;
  channel_options.auto_subscribe_video = false;
  channel_options.enable_rdt = true;
  rval = agora_rtc_join_channel(p_config->conn_id, p_config->p_channel, p_config->uid, p_config->p_token,
                                &channel_options);
  if (rval < 0) {
    LOGE("%s join channel %s failed, rval=%d error=%s", TAG_API, p_config->p_channel, rval, agora_rtc_err_2_str(rval));
    goto EXIT;
  }

  // 5. wait until any rdt tunnel ready or Ctrl-C trigger stop
  while (1) {
    if (p_app->b_exit_flag) {
      break;
    }
    // wait any peer rdt tunnel ready
    for (i = 0; i < p_app->tunnel_cnt; i++) {
      if (p_app->tunnel[i].b_rdt_opened_flag) {
        break;
      }
    }
    if (i != p_app->tunnel_cnt) {
      break; // have rdt tunne is ready
    }
    util_sleep_ms(10);
  }

  // 6. custom message transmit loop with pace sender
  //    you need to control the transmission frequency to avoid affecting the live video
  uint32_t s_send_bps = p_config->send_kbps * 1024;
  uint32_t s_send_fps = (s_send_bps) / (1024 * 8);
  uint32_t s_send_interval = 1000 * 1000 / s_send_fps;
  void *pacer = pacer_create(s_send_interval, 0);

  while (!p_app->b_exit_flag) {
    // check is enable send
    if (!p_config->is_enable_send) {
      util_sleep_ms(1000);
      continue;
    }

    // check is all send finish and stop
    for (i = 0; i < p_app->tunnel_cnt; i++) {
      if (!p_app->tunnel[i].b_send_stop_flag) {
        break;
      }
    }
    if (i == p_app->tunnel_cnt) { // all tunnel had stop send
      util_sleep_ms(1000);
      continue;
    }

    if (is_time_to_send_audio(pacer)) {
      // send same file data to every tunnel
      for (i = 0; i < p_app->tunnel_cnt; i++) {
        rdt_tunnel_t *tunnel = &p_app->tunnel[i];
        if (tunnel->b_rdt_opened_flag && !tunnel->b_send_stop_flag) {
          pthread_mutex_lock(&tunnel->ctx_mtx);
          send_file(tunnel);
          pthread_mutex_unlock(&tunnel->ctx_mtx);
        }
      }
    }

    wait_before_next_send(pacer);
  }

  // 7. API: leave channel
  agora_rtc_leave_channel(p_config->conn_id);

  // 8. API: fini rtc sdk
  agora_rtc_fini();

EXIT:
  // 9. app deinit
  app_deinit(p_app);
  return rval;
}
