//Thank you Zefir !!!!
static uint32_t
libhexs_get_target (double diff)
{
  unsigned nBits;
  int shift = 29;
  double ftarg = (double) 0x0000ffff / diff;
  while (ftarg < (double) 0x00008000)
    {
      shift--;
      ftarg *= 256.0;
    } while (ftarg >= (double) 0x00800000)
    {
      shift++;
      ftarg /= 256.0;
    } nBits = (int) ftarg + (shift << 24);
  return nBits;
}
static void
libhexs_csum (unsigned char *startptr, unsigned char *endptr,
              unsigned char *resptr)
{
  unsigned char *b = startptr;
  uint8_t sum = 0;
  while (b < endptr)
    sum += *b++;
  memcpy (resptr, &sum, 1);
}
static bool
libhexs_usb_dead (struct cgpu_info *hexminers)
{
  struct cg_usb_device *usbdev;
  struct hexminers_info *info = hexminers->device_data;
  usbdev = hexminers->usbdev;
  bool ret = (usbdev == NULL || usbdev->handle == NULL || hexminers->shutdown
              || info->shut_read || info->shut_write || info->shut_reset
              || hexminers->usbinfo.nodev || hexminers->deven != DEV_ENABLED);
  return ret;
}

static int
libhexs_sendHashData (struct cgpu_info *hexminers, unsigned char *sendbuf,
                      size_t buf_len)
{
  struct hexminers_info *info = hexminers->device_data;
  struct cg_usb_device *usbdev;
  int wrote = 0, written = 0;
  int err = LIBUSB_SUCCESS;
  usbdev = hexminers->usbdev;
  if (libhexs_usb_dead (hexminers))
    goto out;
  while (written < buf_len && err == LIBUSB_SUCCESS)
    {
      err =
        libusb_bulk_transfer (usbdev->handle, 0x02, sendbuf + written,
                              MIN (HEXS_USB_WR_SIZE, buf_len - written),
                              &wrote, HEXS_USB_WR_TIME_OUT);
      if (err == LIBUSB_SUCCESS)
        written += wrote;
    }
out:
  if (err == LIBUSB_ERROR_NO_DEVICE || err == LIBUSB_ERROR_NOT_FOUND)
    info->shut_write = true;
  return written;
}

static void
libhexs_reset (struct cgpu_info *hexminers)
{
  struct hexminers_info *info = hexminers->device_data;
  struct cg_usb_device *usbdev;
  int err = LIBUSB_SUCCESS;
  usbdev = hexminers->usbdev;
  if (libhexs_usb_dead (hexminers))
    goto out;
  err = libusb_reset_device (usbdev->handle);
out:
  if (err == LIBUSB_ERROR_NO_DEVICE || err == LIBUSB_ERROR_NOT_FOUND)
    info->shut_reset = true;
  info->usb_reset_count++;
}

static int
libhexs_readHashData (struct cgpu_info *hexminers, unsigned char *hash,
                      int *hash_write_pos, int timeout)
{
  struct hexminers_info *info = hexminers->device_data;
  struct cg_usb_device *usbdev;
  int read = 0;
  int err = LIBUSB_SUCCESS;
  usbdev = hexminers->usbdev;
  if (libhexs_usb_dead (hexminers))
    goto out;
  err =
    libusb_bulk_transfer (usbdev->handle, 0x82, hash + *hash_write_pos,
                          HEXS_USB_R_SIZE, &read, timeout);
  if (err == LIBUSB_SUCCESS)
    *hash_write_pos += MIN (read, HEXS_USB_R_SIZE);
out:
  if (err == LIBUSB_ERROR_NO_DEVICE || err == LIBUSB_ERROR_NOT_FOUND)
    info->shut_read = true;
  return err;
}

static double
hexminers_predecode_nonce (struct cgpu_info *hexminers, struct thr_info *thr,
                           uint32_t nonce, uint8_t work_id, bool diff1)
{
  struct hexminers_info *info = hexminers->device_data;
  if (info->hexworks[work_id]->pool == NULL)
    return 0;
  double diff = (diff1
                 || info->hexworks[work_id]->ping ? 1 : info->
                 hexworks[work_id]->work_difficulty);
  if (test_nonce (info->hexworks[work_id], nonce))
    {
      submit_tested_work_fast_clone (thr, info->hexworks[work_id], diff1
                                     || info->hexworks[work_id]->ping);
      return diff;
    }
  return -diff;
}

static void
libhexs_getvoltage (uint16_t wr_bukvoltage, int *info_pic_voltage_readings)
{
  float voltagehuman;
  voltagehuman =
    (float) ((float) wr_bukvoltage * (float) 3300 / (float) ((1 << 12) - 1));
  *info_pic_voltage_readings = (int) voltagehuman;
}

static void
libhexs_setvoltage (int info_voltage, uint16_t * refvoltage)
{
  uint16_t voltageadc;
  voltageadc =
    (uint16_t) ((float) info_voltage / (float) 1000 / (float) 3.3 *
                ((1 << 12) - 1));
  *refvoltage = htole16 (voltageadc);
}

static int
libhexs_eatHashData (struct work8_result *wr, unsigned char *hash,
                     int *hash_read_pos, int *hash_write_pos)
{
  uint8_t psum;
  int wrpos;
  unsigned char *csum_pos;
  bool ok;
  int places = 0;
eat:
  while (*hash_read_pos < *hash_write_pos && hash[*hash_read_pos] != 0x53)
    *hash_read_pos += 1;
  places = *hash_write_pos - *hash_read_pos;
  if (places < 8)
    return HEXS_BUF_SKIP;
  memcpy ((char *) &wr->startbyte, &hash[*hash_read_pos],
          HEXS_BASE_WORK_SIZE - 1);
  wr->address = htole16 (wr->address);
  ok = (wr->command == 0x52)
    && ((wr->address == HEXS_WORKANSWER_ADR && wr->datalength == 0x06)
        || (wr->address == 0x3008 && wr->datalength == 1));
  if (!ok)
    {
      *hash_read_pos += 1;
      goto eat;
    }
  if (places < HEXS_BASE_WORK_SIZE + wr->datalength * 2)
    return HEXS_BUF_SKIP;
  csum_pos =
    hash + *hash_read_pos + HEXS_BASE_WORK_SIZE + wr->datalength * 2 - 1;
  libhexs_csum (hash + *hash_read_pos, csum_pos, &psum);
  if (psum != *csum_pos)
    {
      *hash_read_pos += 1;
      return HEXS_BUF_ERR;
    }
  wrpos = (wr->address - HEXS_WORKANSWER_ADR) + HEXS_BASE_WORK_SIZE - 1;
  memcpy ((char *) &wr->startbyte + wrpos,
          &hash[*hash_read_pos + HEXS_BASE_WORK_SIZE - 1],
          wr->datalength * 2);
  *hash_read_pos += HEXS_BASE_WORK_SIZE + wr->datalength * 2;
  return HEXS_BUF_DATA;
}


