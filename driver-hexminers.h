/*$T indentinput.h GC 1.140 10/16/13 10:20:01 */

#ifndef HEXS_H
#define HEXS_H
#ifdef USE_HEXMINERS
#include "util.h"

/* hexminers_task/work_reply status Definitions: */
#define HEXS_STAT_IDLE					0       /* Idle or data already Sent to the buffer */
#define HEXS_STAT_NEW_WORK				1       /* Request for write in the buffer */
#define HEXS_STAT_WAITING				2       /* Wait For Buffer Empty Position */
#define HEXS_STAT_CLR_BUFF				3       /* Force Buffer Empty */
#define HEXS_STAT_STOP_REQ				4       /* Stop Request */
#define HEXS_STAT_NEW_WORK_CLEAR_OLD		5       /* Clear Buffers and after that fill the first buffer */
#define HEXS_STAT_UNUSED					6

/* libhexs_eatHashData/BUF_reply status Definitions: */
#define HEXS_BUF_DATA 0
#define HEXS_BUF_ERR  1
#define HEXS_BUF_SKIP 2

/*MISC*/
#define HEXMINERS_ARRAY_PIC_SIZE		64
#define HEXMINERS_ARRAY_SIZE                  HEXMINERS_ARRAY_PIC_SIZE * 4
#define HEXMINERS_ARRAY_SIZE_REAL	HEXMINERS_ARRAY_SIZE - 2

#define HEXS_NONCE_CASH_SIZE				4

#define HEXS_USB_R_SIZE					64
#define HEXS_USB_WR_SIZE					64
#define HEXS_HASH_BUF_SIZE				1024
#define HEXS_HASH_BUF_SIZE_OK				HEXS_HASH_BUF_SIZE - 4
#define HEXMINERS_BULK_READ_TIMEOUT 1000
#define HEXS_USB_WR_TIME_OUT				500

#define HEXS_MINER_THREADS			1
#define HEXS_DEFAULT_MINER_NUM		0x01
#define HEXS_DEFAULT_ASIC_NUM		0x08
#define HEXS_MIN_FREQUENCY			200
#define HEXS_MAX_FREQUENCY			1200
#define HEXS_DEFAULT_FREQUENCY		800
#define HEXS_DEFAULT_CORE_VOLTAGE	840     /* in millivolts */
#define HEXS_MIN_COREMV				800     /* in millivolts */
#define HEXS_MAX_COREMV	1100    /* in millivolts */
struct chip_results8
{
  uint8_t nonce_cache_write_pos;
  uint32_t nonces[HEXS_NONCE_CASH_SIZE];
};
struct work8_result
{
  uint8_t startbyte;
  uint8_t datalength;
  uint8_t command;
  uint16_t address;
  uint32_t lastnonce;           //1x32
  uint8_t lastnonceid;          //1x32
  uint8_t status;
  uint16_t lastvoltage;         //1x32
  uint8_t lastchippos;          //1x32
  uint8_t buf_empty_space;      //16 bit words aligned with lastchippos
  uint8_t good_engines;
  uint8_t dum;                  //7
  uint8_t csum;
  uint8_t pad[2];
} __attribute__ ((packed, aligned (4)));
struct hexminers_info
{
  bool shut_read;
  bool shut_write;
  bool shut_reset;
  bool diff1;
  bool reset_work;
  int write_pos;
  int roll;
  double cached_diff;
  uint32_t asic_difficulty;
  uint32_t asic_difficulty_one;
  unsigned int work_block_local;
  struct work *work;
  int chip_mask;
  int miner_count;
  int asic_count;
  int core_voltage;
  int frequency;
  int usb_r_errors;
  int usb_w_errors;
  int usb_reset_count;
  int usb_bad_reads;
  int b_reset_count;
  int pic_voltage_readings;
  int hash_read_pos;
  int hash_write_pos;
  int matching_work[HEXS_DEFAULT_ASIC_NUM];
  int engines[HEXS_DEFAULT_ASIC_NUM];
  unsigned char *readbuf;
  struct work8_result *wr;
  struct thr_info *thr;
  struct work **hexworks;
  struct hexminers_task *ht;
};
struct hexminers_task
{
  uint8_t startbyte;
  uint8_t datalength;
  uint8_t command;
  uint16_t address;
  uint16_t id;
  uint16_t status;       
  uint32_t bheader[16];
  uint32_t midstate[4];
  uint32_t difficulty;
  uint32_t endNonce;
  uint8_t csum;
  uint8_t pad[2];
} __attribute__ ((packed, aligned (4)));
struct hexminers_config_task
{
  uint8_t startbyte;
  uint8_t datalength;
  uint8_t command;
  uint16_t address;
  uint16_t hashclock;
  uint16_t refvoltage;
  uint32_t difficulty;
  uint8_t chip_mask;
  uint8_t wr_interwal;
  uint8_t csum;
} __attribute__ ((packed, aligned (4)));

#define HEXS_WORKANSWER_ADR	0x3000
#define HEXMINERS_TASK_SIZE	(sizeof(struct hexminers_task)-2)
#define HEXS_MAX_WORK_SIZE		(sizeof(struct work8_result)-2)
#define HEXS_BASE_WORK_SIZE		6

extern int opt_hexminers_core_voltage;
extern int opt_hexminers_chip_mask;
extern int opt_hexminers_set_config_diff_to_one;

extern struct hexminers_info **hexminers_info;
#endif /* USE_HEXMINERS */
#endif /* HEXS_H */
