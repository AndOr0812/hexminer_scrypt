/*$T indentinput.c GC 1.140 10/16/13 10:19:47 */
#include "config.h"
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <unistd.h>
#ifndef WIN32
#include <sys/select.h>
#include <termios.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#else
#include "compat.h"
#include <windows.h>
#include <io.h>
#endif
#include "elist.h"
#include "miner.h"
#include "usbutils.h"
#include "driver-hexminers.h"
#include "util.h"
extern unsigned int work_block;
extern bool stale_work (struct work *work, bool share);
extern void inc_hw_errors_hexs (struct thr_info *thr, int diff);
extern struct work *copy_work_noffset_fast_no_id (struct work *base_work,
                                                  int noffset);
struct device_drv hexminers_drv;
extern bool no_work;
int opt_hexminers_chip_mask = 0xFF;
int opt_hexminers_set_config_diff_to_one = 1;
int opt_hexminers_core_voltage = HEXS_DEFAULT_CORE_VOLTAGE;
#include "libhexs.c"

static int
hexminers_send_task (struct hexminers_task *ht, struct cgpu_info *hexminers)
{
  int ret = 0;
  size_t nr_len = HEXMINERS_TASK_SIZE;
  struct hexminers_info *info;
  info = hexminers->device_data;
  libhexs_csum (&ht->startbyte, &ht->csum, &ht->csum);
  ret = libhexs_sendHashData (hexminers, &ht->startbyte, nr_len);
  if (ret != nr_len)
    {
      libhexs_reset (hexminers);
      info->usb_w_errors++;
      return -1;
    }
  return ret;
}

static inline void
hexminers_create_task (bool reset_work, struct hexminers_task *ht,
                       struct work *work, bool diff1,
                       uint32_t * asic_difficulty, double *cached_diff,
                       uint32_t asic_difficulty_one)
{
  memcpy (ht->bheader, work->data, 80);
  ht->id = htole16 ((uint16_t) work->subid);
  if (reset_work)
    ht->status = htole16 ((uint16_t) HEXS_STAT_NEW_WORK_CLEAR_OLD);
  else
    ht->status = htole16 ((uint16_t) HEXS_STAT_NEW_WORK);
  if (work->ping || work->work_difficulty <= (double) 1)
    {
      ht->difficulty = asic_difficulty_one;
      work->ping = 1;
      return;
    }
  if (*cached_diff != work->work_difficulty)
    {
      *cached_diff = work->work_difficulty;
      *asic_difficulty =
        be32toh (libhexs_get_target
                 ((double) 1 / (double) 65536 * work->work_difficulty));
      ht->difficulty = *asic_difficulty;
      return;
    }
}

static inline void
hexminers_init_task_c (struct hexminers_config_task *htc,
                       struct hexminers_info *info)
{
  htc->startbyte = 0x53;
  htc->datalength =
    (uint8_t) ((sizeof (struct hexminers_config_task) - 6) / 2);
  htc->command = 0x57;
  htc->address = htole16 (0x3100);
  htc->hashclock = htole16 ((uint16_t) info->frequency);
  libhexs_setvoltage (info->core_voltage, &htc->refvoltage);
  htc->difficulty = info->asic_difficulty_one;
  htc->chip_mask = (uint8_t) info->chip_mask;
  libhexs_csum (&htc->startbyte, &htc->csum, &htc->csum);
}

static inline void
hexminers_init_task (struct hexminers_task *ht, struct hexminers_info *info)
{
  ht->startbyte = 0x53;
  ht->datalength = (uint8_t) ((HEXMINERS_TASK_SIZE - 6) / 2);
  ht->command = 0x57;
  ht->address = htole16 (0x3080);
  ht->difficulty = info->asic_difficulty_one;
  ht->endNonce = 0xFFFFFFFF;
}

static struct cgpu_info *
hexminers_detect_one (libusb_device * dev, struct usb_find_devices *found)
{
  int asic_count, frequency;
  struct hexminers_info *info;
  struct cgpu_info *hexminers;
  bool configured = false;
  int i = 0;
  hexminers = usb_alloc_cgpu (&hexminers_drv, HEXS_MINER_THREADS);
  if (!usb_init (hexminers, dev, found))
    {
      usb_uninit (hexminers);
      return NULL;
    }
  hexminers->device_data = calloc (sizeof (struct hexminers_info), 1);
  if (unlikely (!(hexminers->device_data)))
    {
      hexminers->device_data = NULL;
      usb_uninit (hexminers);
      return NULL;
    }
    
  if (opt_hexminers_options != NULL)
  	configured = (sscanf(opt_hexminers_options, "%d:%d", &asic_count, &frequency) == 2);
  if (opt_hexminers_core_voltage < HEXS_MIN_COREMV
     || opt_hexminers_core_voltage > HEXS_MAX_COREMV)
   	{
     	applog
       	(LOG_ERR, "Invalid hexminers-voltage %d must be %dmV - %dmV",
        	opt_hexminers_core_voltage, HEXS_MIN_COREMV, HEXS_MAX_COREMV);
     	free (hexminers->device_data);
     	hexminers->device_data = NULL;
     	usb_uninit (hexminers);
     	return NULL;
   	}  
  info = hexminers->device_data;
  info->hexworks = calloc (sizeof (struct work *), HEXMINERS_ARRAY_SIZE);
  if (unlikely (!(info->hexworks)))
    {
      free (hexminers->device_data);
      hexminers->device_data = NULL;
      usb_uninit (hexminers);
      return NULL;
    }
  info->wr = (struct work8_result *) malloc (sizeof (struct work8_result));
  info->readbuf = calloc (HEXS_HASH_BUF_SIZE, sizeof (unsigned char));
  info->hash_read_pos = 0;
  info->hash_write_pos = 0;
  info->shut_read = false;
  info->shut_write = false;
  info->shut_reset = false;
  info->work = NULL;
  info->cached_diff = -1;
  info->miner_count = HEXS_DEFAULT_MINER_NUM;
  info->asic_count = HEXS_DEFAULT_ASIC_NUM;
  info->frequency = HEXS_DEFAULT_FREQUENCY;
  info->pic_voltage_readings = HEXS_DEFAULT_CORE_VOLTAGE;
  info->core_voltage = opt_hexminers_core_voltage;
  info->chip_mask = opt_hexminers_chip_mask;
  info->diff1 = (bool) opt_hexminers_set_config_diff_to_one;
  info->wr->buf_empty_space = 63;
  info->work_block_local = work_block;
  info->reset_work = true;
  info->roll = 0;
  info->asic_difficulty_one =
    be32toh (libhexs_get_target ((double) 1 / (double) 65536));
  info->asic_difficulty = info->asic_difficulty_one;
  info->ht = calloc (sizeof (struct hexminers_task), 1);
  hexminers_init_task (info->ht, info);
  if (configured)
    {
      info->asic_count = asic_count;
      info->frequency = frequency;
    }
  while (i < HEXMINERS_ARRAY_SIZE)
    {
      info->hexworks[i] = calloc (1, sizeof (struct work));
      info->hexworks[i]->pool = NULL;
      i++;
    }
  if (!add_cgpu (hexminers))
    {
      free (info->hexworks);
      free (hexminers->device_data);
      hexminers->device_data = NULL;
      hexminers = usb_free_cgpu (hexminers);
      usb_uninit (hexminers);
      return NULL;
    }
  return hexminers;
}

static void
hexminers_detect (bool __maybe_unused hotplug)
{
  usb_detect (&hexminers_drv, hexminers_detect_one);
}

static void
do_hexminers_close (struct thr_info *thr)
{
  struct cgpu_info *hexminers = thr->cgpu;
  struct hexminers_info *info = hexminers->device_data;
  int i = 0;
  while (i < HEXMINERS_ARRAY_SIZE)
    {
      free_work (info->hexworks[i]);
      i++;
    }
  free (info->hexworks);
  free (info->readbuf);
  free (info->wr);
  free (info->ht);
  if (info->work)
    free_work (info->work);
}

static void
hexminers_shutdown (struct thr_info *thr)
{
  struct cgpu_info *hexminers = thr->cgpu;
  do_hexminers_close (thr);
  usb_nodev (hexminers);
}

static bool
hexminers_thread_init (struct thr_info *thr)
{
  struct cgpu_info *hexminers = thr->cgpu;
  struct hexminers_info *info = hexminers->device_data;
  info->thr = thr;
  struct hexminers_config_task *htc;
  htc = calloc (sizeof (struct hexminers_config_task), 1);
  hexminers_init_task_c (htc, info);
  int ret = libhexs_sendHashData (hexminers, &htc->startbyte,
                                  sizeof (struct hexminers_config_task));
  if (ret != sizeof (struct hexminers_config_task))
    applog (LOG_ERR, "HEXs %i Send config failed", hexminers->device_id);
  free (htc);
  return true;
}

static bool
stale_ltcwork (struct work *ltcwork, bool bench, double cached_diff,
               bool * reset_work)
{
  if (stale_work (ltcwork, bench) || ltcwork->sdiff != ltcwork->pool->sdiff)
    {
      *reset_work = true;
      return true;
    }
  return false;
}

static void
do_write_hexs (struct thr_info *thr)
{
  struct cgpu_info *hexminers = thr->cgpu;
  struct hexminers_info *info = hexminers->device_data;
  struct work *tmpwork = NULL;
  int jobs_to_send = 8;
  int send_jobs, ret;
  send_jobs = 0;
  if (info->work)
    stale_ltcwork (info->work, false, info->cached_diff, &info->reset_work);
  while (!libhexs_usb_dead (hexminers)
         && ((info->work_block_local != work_block)
             || (info->wr->buf_empty_space > 40 && send_jobs < jobs_to_send)
             || info->reset_work))
    {
    again:
      if (!info->work)
        {
          info->roll = 0;
          info->work = get_work (thr, thr->id);
          info->work->ping = info->diff1;
          if (info->work_block_local != work_block)
            {
              info->reset_work = true;
              info->work_block_local = work_block;
            }
        }
      if (stale_ltcwork
          (info->work, false, info->cached_diff, &info->reset_work))
        {
          free_work (info->work);
          info->work = NULL;
          goto again;
        }
      if (info->write_pos >= HEXMINERS_ARRAY_SIZE_REAL || info->reset_work)
        info->write_pos = 0;
      info->work->subid = info->write_pos;
      tmpwork = copy_work_noffset_fast_no_id (info->work, info->roll++);
      hexminers_create_task (info->reset_work, info->ht, tmpwork, info->diff1,
                             &info->asic_difficulty, &info->cached_diff,
                             info->asic_difficulty_one);
      free_work (info->hexworks[info->write_pos]);
      info->hexworks[info->write_pos] = tmpwork;
      if (info->work->drv_rolllimit)
        {
          info->work->drv_rolllimit--;
        }
      else
        {
          free_work (info->work);
          info->work = NULL;
        }
      ret = hexminers_send_task (info->ht, hexminers);
      info->write_pos++;
      send_jobs++;
      if (ret == HEXMINERS_TASK_SIZE && info->reset_work)
        {
          info->reset_work = false;
          info->wr->buf_empty_space = 63;
          send_jobs -= 8;
        }
    }
}

static int64_t
hexminers_scanhash (struct thr_info *thr)
{
  struct cgpu_info *hexminers = thr->cgpu;
  struct hexminers_info *info = hexminers->device_data;
  uint32_t nonce;
  double found;
  double hash_count = 0;
  int ret_r = 0;
  int64_t rethash_count = 0;
  if (libhexs_usb_dead (hexminers))
    {
      hexminers->shutdown = true;
      return -1;
    }
  do_write_hexs (thr);
  if (libhexs_usb_dead (hexminers))
    {
      hexminers->shutdown = true;
      return -1;
    }
  if (info->hash_write_pos + HEXS_USB_R_SIZE > HEXS_HASH_BUF_SIZE_OK)
    {
      info->hash_write_pos = info->hash_write_pos - info->hash_read_pos;
      memcpy (info->readbuf, info->readbuf + info->hash_read_pos,
              info->hash_write_pos);
      info->hash_read_pos = 0;
    }
  if (info->hash_write_pos - info->hash_read_pos > 7)
    {
    again:
      ret_r =
        libhexs_eatHashData (info->wr, info->readbuf, &info->hash_read_pos,
                             &info->hash_write_pos);
      if (ret_r > HEXS_BUF_DATA)
        goto out;
      if (info->wr->datalength == 1)
        goto done;
      if (info->wr->lastnonceid > HEXMINERS_ARRAY_SIZE_REAL)
        info->wr->lastnonceid = 0;
      if (info->wr->lastchippos >= HEXS_DEFAULT_ASIC_NUM)
        info->wr->lastchippos = 7;
      {
        nonce = info->wr->lastnonce;
        found =
          hexminers_predecode_nonce (hexminers, thr, nonce,
                                     info->wr->lastnonceid, info->diff1);
        if (found > 0)
          {
            info->engines[(uint8_t) info->wr->lastchippos] =
              info->wr->good_engines;
            if (hash_count == 0)
              libhexs_getvoltage (htole16 (info->wr->lastvoltage),
                                  &info->pic_voltage_readings);
            hash_count += found;
            info->matching_work[info->wr->lastchippos]++;
          }
        else
          {
            inc_hw_errors_hexs (thr, (int) found);
          }
      }
    out:
      if (ret_r == HEXS_BUF_ERR)
        info->usb_r_errors++;
    done:
      if (ret_r != HEXS_BUF_SKIP)
        goto again;
    }
  ret_r =
    libhexs_readHashData (hexminers, info->readbuf, &info->hash_write_pos,
                          HEXMINERS_BULK_READ_TIMEOUT);
  if (ret_r != LIBUSB_SUCCESS)
    info->usb_bad_reads++;
  else
    info->usb_bad_reads = 0;
  if (info->usb_bad_reads > 20)
    libhexs_reset (hexminers);
  rethash_count = (0x0000ffffull * (int64_t) hash_count);
  if (libhexs_usb_dead (hexminers))
    {
      hexminers->shutdown = true;
      return -1;
    }
  return rethash_count;
}

static void
get_hexminers_statline_before (char *buf, size_t bufsiz,
                               struct cgpu_info *hexminers)
{
  if (!hexminers->device_data)
    return;
  struct hexminers_info *info = hexminers->device_data;
  tailsprintf (buf, bufsiz, "%3d %4d/%4dmV", info->frequency,
               info->core_voltage, info->pic_voltage_readings);
}

extern void suffix_string (uint64_t val, char *buf, size_t bufsiz,
                           int sigdigits);

static struct api_data *
hexminers_api_stats (struct cgpu_info *cgpu)
{
  struct api_data *root = NULL;
  struct timeval now;
  struct hexminers_info *info = cgpu->device_data;
  char displayed_hashes[16], displayed_rolling[16];
  double dev_runtime, hwp;
  uint64_t dh64, dr64;
  int i;
  if (!info)
    return NULL;
  hwp =
    (cgpu->hw_errors +
     cgpu->diff1) ? (double) (cgpu->hw_errors) / (double) (cgpu->hw_errors +
                                                           cgpu->diff1) : 0;
  if (cgpu->dev_start_tv.tv_sec == 0)
    dev_runtime = total_secs;
  else
    {
      cgtime (&now);
      dev_runtime = tdiff (&now, &(cgpu->dev_start_tv));
    }
  if (dev_runtime < 1.0)
    dev_runtime = 1.0;
  dh64 = (double) cgpu->total_mhashes / dev_runtime * 1000000ull;
  dr64 = (double) cgpu->rolling * 1000000ull;
  suffix_string (dh64, displayed_hashes, sizeof (displayed_hashes), 4);
  suffix_string (dr64, displayed_rolling, sizeof (displayed_rolling), 4);
  root = api_add_string (root, "MHS 5s", displayed_rolling, true);
  root = api_add_string (root, "MHS av", displayed_hashes, true);
  root = api_add_int (root, "Hardware Errors", &(cgpu->hw_errors), true);
  root = api_add_percent (root, "Hardware Errors%", &hwp, true);
  root = api_add_int (root, "USB Read Errors", &(info->usb_r_errors), true);
  root = api_add_int (root, "USB Write Errors", &(info->usb_w_errors), true);
  root =
    api_add_int (root, "USB Reset Count", &(info->usb_reset_count), true);
  root =
    api_add_int (root, "Miner Reset Count", &(info->b_reset_count), true);
  root =
    api_add_time (root, "Last Share Time", &(cgpu->last_share_pool_time),
                  true);
  root = api_add_int (root, "Chip Count", &(info->asic_count), true);
  root = api_add_int (root, "Frequency", &(info->frequency), true);
  root = api_add_int (root, "Core Voltage", &(info->core_voltage), true);
  root =
    api_add_int (root, "PIC Voltage Readings", &(info->pic_voltage_readings),
                 true);
  for (i = 0; i < info->asic_count; i++)
    {
      char mcw[24];
      sprintf (mcw, "Chip%d Nonces", i + 1);
      root = api_add_int (root, mcw, &(info->matching_work[i]), true);
      sprintf (mcw, "Chip%d Engines", i + 1);
      root = api_add_int (root, mcw, &(info->engines[i]), true);
    }
  return root;
}

struct device_drv hexminers_drv = {
  .drv_id = DRIVER_hexminers,
  .dname = "hexminers",
  .name = "HEXS",
  .drv_detect = hexminers_detect,
  .thread_init = hexminers_thread_init,
  .hash_work = hash_driver_work,
  .scanwork = hexminers_scanhash,
  .get_api_stats = hexminers_api_stats,
  .get_statline_before = get_hexminers_statline_before,
  .thread_shutdown = hexminers_shutdown,
};
