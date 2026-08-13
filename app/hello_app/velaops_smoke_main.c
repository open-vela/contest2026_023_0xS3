/****************************************************************************
 * VelaOps Sentinel board and software-stack smoke test.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/lcd/lcd.h>
#include <nuttx/lcd/lcd_dev.h>
#include <nuttx/video/fb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef CONFIG_SENSORS_QMA7981
#  include <nuttx/sensors/qma7981.h>
#endif
#ifdef CONFIG_LIB_SSH
#  include <libssh/libssh.h>
#endif

#define VELAOPS_SMOKE_VERSION "0.2.0"
#define VELAOPS_TEST_FILE     "/tmp/velaops-smoke.txt"

struct smoke_result_s
{
  unsigned int passed;
  unsigned int failed;
  unsigned int skipped;
};

/* Noto Sans CJK 20 px bitmap for "屏幕测试OK", 109 x 20, MSB first. */

static const uint8_t g_screen_ok_bitmap[20][14] =
{
  {0x00,0x00,0x00,0x30,0xc0,0x00,0x00,0x01,0x00,0x68,0x00,0x00,0x00,0x00},
  {0x1f,0xff,0xc7,0xff,0xfe,0x30,0x00,0x63,0x80,0x6c,0x00,0x00,0x00,0x00},
  {0x1f,0xff,0xc7,0xff,0xfe,0x3b,0xfb,0x61,0xc0,0x66,0x00,0x00,0x00,0x00},
  {0x18,0x00,0xc0,0x30,0xc0,0x1f,0x0b,0x60,0xc0,0x64,0x07,0xc0,0x60,0xe0},
  {0x18,0x00,0xc1,0xff,0xf8,0x03,0x4b,0x60,0x1f,0xfe,0x1f,0xf0,0x60,0xc0},
  {0x1f,0xff,0xc1,0x80,0x18,0x03,0x4b,0x60,0x1f,0xfe,0x3c,0x78,0x61,0xc0},
  {0x1f,0xff,0xc1,0x80,0x18,0x03,0x4b,0x60,0x00,0x60,0x38,0x38,0x63,0x80},
  {0x19,0x83,0x01,0xff,0xf8,0x73,0x4b,0x67,0x80,0x20,0x30,0x1c,0x67,0x00},
  {0x18,0xc6,0x01,0x80,0x18,0x3b,0x4b,0x67,0x80,0x20,0x70,0x1c,0x66,0x00},
  {0x1f,0xff,0xc1,0xff,0xf8,0x13,0x4b,0x60,0x8f,0xb0,0x70,0x0c,0x6e,0x00},
  {0x1f,0xff,0xc0,0x18,0x00,0x03,0x4b,0x60,0x8f,0xb0,0x70,0x0c,0x7f,0x00},
  {0x10,0xc6,0x07,0xff,0xfe,0x03,0x4b,0x60,0x86,0x30,0x70,0x0c,0x7b,0x00},
  {0x30,0xc6,0x07,0xff,0xfe,0x1b,0x4b,0x60,0x86,0x30,0x70,0x1c,0x71,0x80},
  {0x37,0xff,0xe0,0xe6,0x30,0x1b,0x4b,0x60,0x86,0x30,0x30,0x1c,0x61,0xc0},
  {0x37,0xff,0xe3,0xff,0xfc,0x1b,0x4b,0x60,0x86,0x32,0x38,0x38,0x60,0xc0},
  {0x30,0xc6,0x07,0xff,0xfe,0x30,0xc0,0x60,0xb7,0xb2,0x1c,0x78,0x60,0xe0},
  {0x31,0x86,0x00,0xc6,0x30,0x30,0xb0,0x61,0xff,0xda,0x1f,0xf0,0x60,0x60},
  {0x63,0x86,0x00,0xc6,0x30,0x31,0x98,0x61,0xde,0x1e,0x07,0xc0,0x60,0x70},
  {0x67,0x06,0x00,0xc6,0x70,0x67,0x09,0xe1,0x00,0x0e,0x00,0x00,0x00,0x00},
  {0x04,0x06,0x00,0x06,0x00,0x00,0x01,0x80,0x00,0x00,0x00,0x00,0x00,0x00}
};

static const uint8_t *ascii_glyph(char ch)
{
  static const uint8_t h[7] = {16,16,22,25,17,17,17};
  static const uint8_t e[7] = {0,0,14,17,31,16,14};
  static const uint8_t l[7] = {12,4,4,4,4,4,14};
  static const uint8_t o[7] = {0,0,14,17,17,17,14};
  static const uint8_t comma[7] = {0,0,0,0,0,4,8};
  static const uint8_t w[7] = {0,0,17,17,21,21,10};
  static const uint8_t r[7] = {0,0,22,25,16,16,16};
  static const uint8_t d[7] = {1,1,13,19,17,17,15};

  switch (ch)
    {
      case 'h': return h;
      case 'e': return e;
      case 'l': return l;
      case 'o': return o;
      case ',': return comma;
      case 'w': return w;
      case 'r': return r;
      case 'd': return d;
      default:  return NULL;
    }
}

static void lcd_pixel(uint16_t *pixels, int width, int height,
                      int x, int y, uint16_t color)
{
  if ((unsigned int)x < (unsigned int)width &&
      (unsigned int)y < (unsigned int)height)
    {
      pixels[y * width + x] = color;
    }
}

static int lcd_test_pattern(void)
{
  struct lcddev_area_align_s align;
  struct lcd_planeinfo_s plane;
  struct fb_videoinfo_s video;
  struct lcddev_area_s area;
  uint16_t *pixels;
  const char *text = "hello,world";
  int fd;
  int x;
  int y;
  int i;
  int ret;

  fd = open("/dev/lcd0", O_RDWR);
  if (fd < 0 ||
      ioctl(fd, LCDDEVIO_GETPLANEINFO, &plane) < 0 ||
      ioctl(fd, LCDDEVIO_GETVIDEOINFO, &video) < 0 ||
      ioctl(fd, LCDDEVIO_GETAREAALIGN, &align) < 0 || plane.bpp != 16)
    {
      printf("lcd-pattern: LCD unavailable: %s\n", strerror(errno));
      if (fd >= 0) close(fd);
      return 1;
    }

  pixels = aligned_alloc(align.buf_align,
                         video.xres * video.yres * sizeof(*pixels));
  if (pixels == NULL)
    {
      close(fd);
      return 1;
    }

  for (i = 0; i < video.xres * video.yres; i++) pixels[i] = 0xffff;

  /* Blue square. */

  for (y = 20; y < 70; y++)
    for (x = 15; x < 65; x++) lcd_pixel(pixels, video.xres, video.yres,
                                        x, y, 0x001f);

  /* Cyan triangle. */

  for (y = 20; y < 70; y++)
    {
      int half = (y - 20) / 2;
      for (x = 105 - half; x <= 105 + half; x++)
        lcd_pixel(pixels, video.xres, video.yres, x, y, 0x07ff);
    }

  /* Purple circle. */

  for (y = 20; y < 70; y++)
    for (x = 155; x < 205; x++)
      {
        int dx = x - 180;
        int dy = y - 45;
        if (dx * dx + dy * dy <= 24 * 24)
          lcd_pixel(pixels, video.xres, video.yres, x, y, 0x8010);
      }

  /* 5x7 ASCII, scaled 3x, centered. */

  x = (video.xres - (int)strlen(text) * 18) / 2;
  for (i = 0; text[i] != '\0'; i++)
    {
      const uint8_t *glyph = ascii_glyph(text[i]);
      int row;
      int col;
      if (glyph != NULL)
        for (row = 0; row < 7; row++)
          for (col = 0; col < 5; col++)
            if (glyph[row] & (16 >> col))
              for (y = 0; y < 3; y++)
                for (int sx = 0; sx < 3; sx++)
                  lcd_pixel(pixels, video.xres, video.yres,
                            x + col * 3 + sx, 112 + row * 3 + y, 0x0000);
      x += 18;
    }

  /* Chinese status text, centered near the bottom. */

  x = (video.xres - 109) / 2;
  for (y = 0; y < 20; y++)
    for (i = 0; i < 109; i++)
      if (g_screen_ok_bitmap[y][i / 8] & (0x80 >> (i & 7)))
        lcd_pixel(pixels, video.xres, video.yres, x + i, 200 + y, 0x0000);

  memset(&area, 0, sizeof(area));
  area.row_end = video.yres - 1;
  area.col_end = video.xres - 1;
  area.stride = video.xres * sizeof(*pixels);
  area.data = (uint8_t *)pixels;
  ret = ioctl(fd, LCDDEVIO_PUTAREA, (unsigned long)&area);
  printf("lcd-pattern: shapes and text rendered, ret=%d\n", ret);
  free(pixels);
  close(fd);
  return ret < 0 ? 1 : 0;
}

static int lcd_color_bars(void)
{
  static const uint16_t colors[] =
    {
      0x0000, /* black */
      0xf800, /* red */
      0x07e0, /* green */
      0x001f, /* blue */
      0xffff  /* white */
    };
  struct lcddev_area_align_s align;
  struct lcd_planeinfo_s plane;
  struct fb_videoinfo_s video;
  struct lcddev_area_s area;
  uint16_t *pixels;
  size_t count;
  size_t x;
  size_t y;
  int fd;
  int ret;

  fd = open("/dev/lcd0", O_RDWR);
  if (fd < 0)
    {
      printf("lcd-bars: open failed: %s\n", strerror(errno));
      return 1;
    }

  if (ioctl(fd, LCDDEVIO_GETPLANEINFO, &plane) < 0 ||
      ioctl(fd, LCDDEVIO_GETVIDEOINFO, &video) < 0 ||
      ioctl(fd, LCDDEVIO_GETAREAALIGN, &align) < 0)
    {
      printf("lcd-bars: query failed: %s\n", strerror(errno));
      close(fd);
      return 1;
    }

  if (plane.bpp != 16)
    {
      printf("lcd-bars: expected RGB565, got %u bpp\n", plane.bpp);
      close(fd);
      return 1;
    }

  count = (size_t)video.xres * video.yres;
  pixels = aligned_alloc(align.buf_align, count * sizeof(*pixels));
  if (pixels == NULL)
    {
      printf("lcd-bars: allocation failed\n");
      close(fd);
      return 1;
    }

  for (y = 0; y < video.yres; y++)
    {
      for (x = 0; x < video.xres; x++)
        {
          size_t bar = x * 5 / video.xres;
          pixels[y * video.xres + x] = colors[bar];
        }
    }

  memset(&area, 0, sizeof(area));
  area.row_start = 0;
  area.row_end = video.yres - 1;
  area.col_start = 0;
  area.col_end = video.xres - 1;
  area.data = (uint8_t *)pixels;
  area.stride = video.xres * sizeof(*pixels);
  ret = ioctl(fd, LCDDEVIO_PUTAREA, (unsigned long)&area);
  printf("lcd-bars: %ux%u, left-to-right BLACK RED GREEN BLUE WHITE, ret=%d\n",
         video.xres, video.yres, ret);
  free(pixels);
  close(fd);
  return ret < 0 ? 1 : 0;
}

static void record_result(struct smoke_result_s *result, const char *name,
                          int status, const char *detail)
{
  const char *label;
  if (status > 0)
    {
      result->passed++;
      label = "PASS";
    }
  else if (status == 0)
    {
      result->skipped++;
      label = "SKIP";
    }
  else
    {
      result->failed++;
      label = "FAIL";
    }

  printf("[%-4s] %-18s %s\n", label, name, detail ? detail : "");
}

static void test_uname(struct smoke_result_s *result)
{
  struct utsname info;
  char detail[160];
  if (uname(&info) < 0)
    {
      record_result(result, "openvela", -1, strerror(errno));
      return;
    }

  snprintf(detail, sizeof(detail), "%s %s %s/%s", info.sysname,
           info.release, info.machine, info.nodename);
  record_result(result, "openvela", 1, detail);
}

static void test_memory(struct smoke_result_s *result)
{
  struct mallinfo info = mallinfo();
  char detail[96];
  snprintf(detail, sizeof(detail), "arena=%ld used=%ld free=%ld",
           (long)info.arena, (long)info.uordblks, (long)info.fordblks);
  record_result(result, "heap", info.fordblks > 0 ? 1 : -1, detail);
}

static void test_file(struct smoke_result_s *result)
{
  static const char payload[] = "velaops-sentinel-smoke";
  char buffer[sizeof(payload)] = {0};
  ssize_t nread = -1;
  ssize_t nwritten;
  int fd = open(VELAOPS_TEST_FILE, O_CREAT | O_TRUNC | O_RDWR, 0600);

  if (fd < 0)
    {
      record_result(result, "filesystem", -1, strerror(errno));
      return;
    }

  nwritten = write(fd, payload, sizeof(payload));
  if (nwritten == sizeof(payload) && lseek(fd, 0, SEEK_SET) >= 0)
    {
      nread = read(fd, buffer, sizeof(buffer));
    }

  close(fd);
  unlink(VELAOPS_TEST_FILE);
  record_result(result, "filesystem",
                nread == sizeof(payload) &&
                memcmp(buffer, payload, sizeof(payload)) == 0 ? 1 : -1,
                VELAOPS_TEST_FILE);
}

static void test_node(struct smoke_result_s *result, const char *name,
                      const char *path, bool required)
{
  struct stat st;
  char detail[128];
  if (stat(path, &st) == 0)
    {
      snprintf(detail, sizeof(detail), "%s present", path);
      record_result(result, name, 1, detail);
    }
  else
    {
      snprintf(detail, sizeof(detail), "%s: %s", path, strerror(errno));
      record_result(result, name, required ? -1 : 0, detail);
    }
}

static void test_accel(struct smoke_result_s *result)
{
#ifdef CONFIG_SENSORS_QMA7981
  struct qma7981_data_s sample;
  char detail[96];
  int fd = open("/dev/accel0", O_RDONLY | O_NONBLOCK);
  ssize_t nread;
  if (fd < 0)
    {
      record_result(result, "accelerometer", -1, strerror(errno));
      return;
    }

  nread = read(fd, &sample, sizeof(sample));
  close(fd);
  if (nread != sizeof(sample))
    {
      snprintf(detail, sizeof(detail), "read=%ld errno=%d",
               (long)nread, errno);
      record_result(result, "accelerometer", -1, detail);
      return;
    }

  snprintf(detail, sizeof(detail), "x=%d y=%d z=%d",
           sample.x, sample.y, sample.z);
  record_result(result, "accelerometer", 1, detail);
#else
  record_result(result, "accelerometer", 0, "CONFIG_SENSORS_QMA7981=n");
#endif
}

static void test_network(struct smoke_result_s *result)
{
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    {
      record_result(result, "network-stack", -1, strerror(errno));
      return;
    }

  close(fd);
  record_result(result, "network-stack", 1, "TCP socket created");
}

static void test_libssh(struct smoke_result_s *result)
{
#ifdef CONFIG_LIB_SSH
  const char *version = ssh_version(0);
  record_result(result, "libssh", version != NULL ? 1 : -1,
                version ? version : "ssh_version failed");
#else
  record_result(result, "libssh", -1, "CONFIG_LIB_SSH=n");
#endif
}

static void test_ai_agent(struct smoke_result_s *result)
{
#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
  record_result(result, "ai_agent", 1, "compiled in; run: ai_agent");
#else
  record_result(result, "ai_agent", -1,
                "CONFIG_EXAMPLES_AI_AGENT_VELA=n");
#endif
}

/* Native TCP diagnostic: connect to a host, do the SSH banner exchange, then
 * send fixed-size payloads to determine whether the ESP32-S3 Wi-Fi TX path
 * alone can survive a large send(), independent of libssh/crypto. */

static int tcp_banner_payload_diag(const char *host, unsigned int port)
{
  static const char banner[] = "SSH-2.0-velaops-diag\r\n";
  static const uint32_t sizes[] = { 64, 256, 1024, 16, 48, 920, 70, 102, 54 };
  unsigned char payload[1024];
  unsigned char reply[256];
  struct sockaddr_in address;
  size_t reply_len = 0;
  ssize_t n;
  int fd;
  int i;
  int result = 0;

  memset(payload, 0x5a, sizeof(payload));

  fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0)
    {
      printf("tcp-diag: socket failed: errno=%d (%s)\n", errno, strerror(errno));
      return 1;
    }

  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  if (inet_pton(AF_INET, host, &address.sin_addr) != 1)
    {
      printf("tcp-diag: bad address %s: errno=%d\n", host, errno);
      close(fd);
      return 1;
    }

  if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
      printf("tcp-diag: connect failed: errno=%d (%s)\n",
             errno, strerror(errno));
      close(fd);
      return 1;
    }
  printf("tcp-diag: connected to %s:%u fd=%d\n", host, port, fd);

  n = send(fd, banner, sizeof(banner) - 1, 0);
  printf("tcp-diag: banner send -> %ld errno=%d\n", (long)n, errno);
  if (n != (ssize_t)(sizeof(banner) - 1))
    {
      close(fd);
      return 1;
    }

  while (reply_len + 1 < sizeof(reply))
    {
      n = recv(fd, reply + reply_len, 1, 0);
      if (n != 1)
        {
          printf("tcp-diag: banner recv failed: n=%ld errno=%d\n",
                 (long)n, errno);
          close(fd);
          return 1;
        }
      reply_len++;
      if (reply[reply_len - 1] == '\n')
        {
          break;
        }
    }
  printf("tcp-diag: server banner received: %.*s",
         (int)reply_len, reply);

  for (i = 0; i < (int)(sizeof(sizes) / sizeof(sizes[0])); i++)
    {
      n = send(fd, payload, sizes[i], 0);
      printf("tcp-diag: payload send[%d] len=%u -> %ld errno=%d\n",
             i, (unsigned int)sizes[i], (long)n, errno);
      if (n != (ssize_t)sizes[i])
        {
          result = 1;
          break;
        }
    }

  close(fd);
  printf("tcp-diag: done result=%d\n", result);
  return result;
}

/* Bidirectional burst diagnostic: connect to an echo server and hammer it
 * with large sends while draining the echo back, mirroring the high-volume
 * bidirectional pattern of an SSH KEX with pure TCP (no SSH/crypto code).
 */

/* Simulate SSH KEX packet sequence: 920B → 48B → 16B with poll-driven sends.
 * This replicates the exact timing pattern that triggers the ESP32-S3 WiFi
 * firmware crash during SSH KEX, but without any SSH/crypto overhead.
 *
 * When recv_match is true, recv sizes match send sizes (echo pattern).
 * When recv_match is false, recv sizes follow the SSH KEX pattern (1112, 524).
 */

static int tcp_kex_sim(const char *host, unsigned int port,
                        unsigned int rounds)
{
  static const unsigned int kex_send[] = {920, 48, 16};
  static const unsigned int kex_recv[] = {1112, 524, 0}; /* 0 = don't recv */
  static const int kex_count = sizeof(kex_send) / sizeof(kex_send[0]);
  unsigned char *payload;
  unsigned char *echo;
  struct sockaddr_in address;
  ssize_t n;
  int fd;
  int r;
  int k;
  int result = 0;

  payload = malloc(1514);
  echo = malloc(2048); /* larger buffer for recv sizes > send sizes */
  if (payload == NULL || echo == NULL)
    {
      printf("kex-sim: allocation failed\n");
      if (payload != NULL) free(payload);
      if (echo != NULL) free(echo);
      return 1;
    }

  memset(payload, 0x5a, 1514);

  fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0)
    {
      printf("kex-sim: socket failed: errno=%d (%s)\n", errno, strerror(errno));
      free(payload);
      free(echo);
      return 1;
    }

  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  if (inet_pton(AF_INET, host, &address.sin_addr) != 1)
    {
      printf("kex-sim: bad address %s: errno=%d\n", host, errno);
      close(fd);
      free(payload);
      free(echo);
      return 1;
    }

  if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
      printf("kex-sim: connect failed: errno=%d (%s)\n", errno, strerror(errno));
      close(fd);
      free(payload);
      free(echo);
      return 1;
    }

  printf("kex-sim: connected to %s:%u rounds=%u pattern=920-48-16\n",
         host, port, rounds);
  printf("kex-sim: recv pattern: %u-%u-%u (SSH KEX actual sizes)\n",
         kex_recv[0], kex_recv[1], kex_recv[2]);

  for (r = 0; r < (int)rounds && result == 0; r++)
    {
      for (k = 0; k < kex_count && result == 0; k++)
        {
          unsigned int send_sz = kex_send[k];
          unsigned int recv_sz = kex_recv[k];
          struct pollfd pfd;
          int pr;

          /* Wait for POLLOUT before send — same as libssh ssh_socket_pollcallback */
          pfd.fd = fd;
          pfd.events = POLLOUT;
          pfd.revents = 0;
          pr = poll(&pfd, 1, 5000);
          if (pr <= 0 || (pfd.revents & POLLOUT) == 0)
            {
              printf("kex-sim: poll[%d][%d] -> %d revents=%x errno=%d\n",
                     r, k, pr, pfd.revents, errno);
              result = 1;
              break;
            }

          n = send(fd, payload, send_sz, 0);
          if (n != (ssize_t)send_sz)
            {
              printf("kex-sim: send[%d][%d] len=%u -> %ld errno=%d\n",
                     r, k, send_sz, (long)n, errno);
              result = 1;
              break;
            }

          printf("kex-sim: round=%d step=%d sent=%u OK\n", r, k, send_sz);

          /* Receive response — use SSH KEX actual sizes */
          if (recv_sz > 0)
            {
              n = recv(fd, echo, recv_sz, 0);
              if (n != (ssize_t)recv_sz)
                {
                  printf("kex-sim: recv[%d][%d] expected=%u got=%ld errno=%d\n",
                         r, k, recv_sz, (long)n, errno);
                  result = 1;
                  break;
                }
            }
        }
    }

  close(fd);
  free(payload);
  free(echo);
  printf("kex-sim: done result=%d\n", result);
  return result;
}

static int tcp_burst_diag(const char *host, unsigned int port,
                          unsigned int rounds, unsigned int size)
{
  unsigned char *payload;
  unsigned char *echo;
  struct sockaddr_in address;
  ssize_t n;
  int fd;
  int i;
  int result = 0;

  if (size > 1514 || size == 0)
    {
      printf("burst: size %u out of range (1..1514)\n", size);
      return 1;
    }

  payload = malloc(size);
  echo = malloc(size);
  if (payload == NULL || echo == NULL)
    {
      printf("burst: allocation failed\n");
      if (payload != NULL) free(payload);
      if (echo != NULL) free(echo);
      return 1;
    }

  memset(payload, 0x5a, size);

  fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0)
    {
      printf("burst: socket failed: errno=%d (%s)\n", errno, strerror(errno));
      free(payload);
      free(echo);
      return 1;
    }

  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  if (inet_pton(AF_INET, host, &address.sin_addr) != 1)
    {
      printf("burst: bad address %s: errno=%d\n", host, errno);
      close(fd);
      free(payload);
      free(echo);
      return 1;
    }

  if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
      printf("burst: connect failed: errno=%d (%s)\n", errno, strerror(errno));
      close(fd);
      free(payload);
      free(echo);
      return 1;
    }
  printf("burst: connected to %s:%u rounds=%u size=%u\n",
         host, port, rounds, size);

  for (i = 0; i < (int)rounds; i++)
    {
      struct pollfd pfd;
      int pr;

      pfd.fd = fd;
      pfd.events = POLLOUT;
      pfd.revents = 0;
      pr = poll(&pfd, 1, 5000);
      if (pr <= 0 || (pfd.revents & POLLOUT) == 0)
        {
          printf("burst: poll[%d] -> %d revents=%x errno=%d\n",
                 i, pr, pfd.revents, errno);
          result = 1;
          break;
        }

      n = send(fd, payload, size, 0);
      if (n != (ssize_t)size)
        {
          printf("burst: send[%d] len=%u -> %ld errno=%d\n",
                 i, size, (long)n, errno);
          result = 1;
          break;
        }

      n = recv(fd, echo, size, 0);
      if (n != (ssize_t)size)
        {
          printf("burst: recv[%d] expected=%u got=%ld errno=%d\n",
                 i, size, (long)n, errno);
          result = 1;
          break;
        }
    }

  close(fd);
  free(payload);
  free(echo);
  printf("burst: done result=%d\n", result);
  return result;
}

/****************************************************************************
 * Network diagnostic: repeated TCP connect / read SSH banner / close cycle.
 * No libssh, no crypto — pure TCP to isolate Wi-Fi/driver issues.
 ****************************************************************************/

struct netdiag_stats
{
  unsigned int total;
  unsigned int ok;
  unsigned int fail_connect;
  unsigned int fail_send;
  unsigned int fail_recv;
  unsigned int fail_timeout;
  unsigned int consecutive_fail;
  unsigned int max_consecutive;
  int64_t      total_ms;
  int64_t      min_ms;
  int64_t      max_ms;
};

static int64_t netdiag_elapsed_ms(const struct timespec *start)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (int64_t)(now.tv_sec - start->tv_sec) * 1000 +
         (int64_t)(now.tv_nsec - start->tv_nsec) / 1000000;
}

static int tcp_netdiag(const char *host, unsigned int port,
                       unsigned int rounds, unsigned int interval_ms)
{
  static const char banner[] = "SSH-2.0-velaops-netdiag\r\n";
  struct netdiag_stats stats;
  struct sockaddr_in address;
  struct timespec run_start;
  unsigned int i;

  memset(&stats, 0, sizeof(stats));
  stats.min_ms = INT64_MAX;

  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  if (inet_pton(AF_INET, host, &address.sin_addr) != 1)
    {
      printf("[netdiag] bad address %s\n", host);
      return 1;
    }

  printf("[netdiag] start host=%s port=%u rounds=%u interval=%ums\n",
         host, port, rounds, interval_ms);

  clock_gettime(CLOCK_MONOTONIC, &run_start);

  for (i = 0; rounds == 0 || i < rounds; i++)
    {
      struct timespec iter_start;
      struct pollfd   pfd;
      unsigned char   reply[256];
      ssize_t         n;
      int             fd;
      int             pr;
      int             failed = 0;
      int64_t         elapsed;
      size_t          reply_len = 0;

      clock_gettime(CLOCK_MONOTONIC, &iter_start);

      /* ---- connect ---- */

      fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (fd < 0)
        {
          stats.fail_connect++;
          failed = 1;
          goto record;
        }

      fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
      connect(fd, (struct sockaddr *)&address, sizeof(address));

      pfd.fd = fd;
      pfd.events = POLLOUT;
      pfd.revents = 0;
      pr = poll(&pfd, 1, 5000);
      if (pr <= 0 || (pfd.revents & POLLOUT) == 0)
        {
          stats.fail_connect++;
          stats.fail_timeout++;
          close(fd);
          failed = 1;
          goto record;
        }

      {
        int soerr = 0;
        socklen_t slen = sizeof(soerr);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &slen);
        if (soerr != 0)
          {
            stats.fail_connect++;
            close(fd);
            failed = 1;
            goto record;
          }
      }

      fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK);

      /* ---- send client banner ---- */

      n = send(fd, banner, sizeof(banner) - 1, 0);
      if (n != (ssize_t)(sizeof(banner) - 1))
        {
          stats.fail_send++;
          close(fd);
          failed = 1;
          goto record;
        }

      /* ---- recv server banner ---- */

      pfd.fd = fd;
      pfd.events = POLLIN;
      pfd.revents = 0;
      pr = poll(&pfd, 1, 5000);
      if (pr <= 0 || (pfd.revents & POLLIN) == 0)
        {
          stats.fail_recv++;
          stats.fail_timeout++;
          close(fd);
          failed = 1;
          goto record;
        }

      while (reply_len + 1 < sizeof(reply))
        {
          n = recv(fd, reply + reply_len, 1, 0);
          if (n != 1)
            {
              stats.fail_recv++;
              close(fd);
              failed = 1;
              goto record;
            }

          reply_len++;
          if (reply[reply_len - 1] == '\n')
            {
              break;
            }
        }

      /* success */

      stats.ok++;
      stats.consecutive_fail = 0;
      close(fd);

record:
      elapsed = netdiag_elapsed_ms(&iter_start);
      stats.total++;
      stats.total_ms += elapsed;
      if (elapsed < stats.min_ms)
        {
          stats.min_ms = elapsed;
        }

      if (elapsed > stats.max_ms)
        {
          stats.max_ms = elapsed;
        }

      if (failed)
        {
          stats.consecutive_fail++;
          if (stats.consecutive_fail > stats.max_consecutive)
            {
              stats.max_consecutive = stats.consecutive_fail;
            }

          printf("[netdiag] round %u FAIL elapsed=%lldms errno=%d\n",
                 i + 1, (long long)elapsed, errno);
        }

      /* periodic progress every 10 rounds */

      if (stats.total % 10 == 0)
        {
          printf("[netdiag] %u/%u ok=%u fail=%u consec=%u avg=%lldms\n",
                 stats.total, rounds, stats.ok,
                 stats.fail_connect + stats.fail_send + stats.fail_recv,
                 stats.consecutive_fail,
                 (long long)(stats.total_ms / stats.total));
        }

      /* global timeout: 30 minutes */

      if (netdiag_elapsed_ms(&run_start) > 30LL * 60 * 1000)
        {
          printf("[netdiag] 30-minute time limit reached\n");
          break;
        }

      /* inter-round delay */

      if (interval_ms > 0)
        {
          usleep((useconds_t)interval_ms * 1000);
        }
    }

  /* final summary */

  {
    unsigned int total_f = stats.fail_connect +
                           stats.fail_send +
                           stats.fail_recv;
    int64_t run_ms = netdiag_elapsed_ms(&run_start);

    printf("\n[netdiag] === SUMMARY ===\n");
    printf("[netdiag] total=%u ok=%u fail=%u (%.1f%%)\n",
           stats.total, stats.ok, total_f,
           stats.total > 0 ?
             100.0 * total_f / stats.total : 0.0);
    printf("[netdiag] connect_fail=%u send_fail=%u "
           "recv_fail=%u timeout=%u\n",
           stats.fail_connect, stats.fail_send,
           stats.fail_recv, stats.fail_timeout);
    printf("[netdiag] max_consecutive_fail=%u\n",
           stats.max_consecutive);
    printf("[netdiag] round_time: min=%lldms avg=%lldms "
           "max=%lldms\n",
           (long long)(stats.min_ms == INT64_MAX ? 0 : stats.min_ms),
           (long long)(stats.total > 0 ?
             stats.total_ms / stats.total : 0),
           (long long)stats.max_ms);
    printf("[netdiag] total_time=%lldms (%.1fs)\n",
           (long long)run_ms, run_ms / 1000.0);
    printf("[netdiag] result=%s\n",
           (total_f == 0 && stats.max_consecutive == 0) ?
             "PASS" : "FAIL");
  }

  return 0;
}

int main(int argc, char *argv[])
{
  struct smoke_result_s result = {0};

  if (argc == 2 && strcmp(argv[1], "--lcd-bars") == 0)
    {
      return lcd_color_bars();
    }

  if (argc >= 3 && strcmp(argv[1], "--tcp-diag") == 0)
    {
      return tcp_banner_payload_diag(argv[2],
                                     argc >= 4 ? (unsigned int)atoi(argv[3]) : 22u);
    }

  if (argc >= 3 && strcmp(argv[1], "--tcp-burst") == 0)
    {
      return tcp_burst_diag(argv[2],
                            argc >= 4 ? (unsigned int)atoi(argv[3]) : 19999u,
                            argc >= 5 ? (unsigned int)atoi(argv[4]) : 20u,
                            argc >= 6 ? (unsigned int)atoi(argv[5]) : 1024u);
    }

  if (argc >= 3 && strcmp(argv[1], "--tcp-kex-sim") == 0)
    {
      return tcp_kex_sim(argv[2],
                         argc >= 4 ? (unsigned int)atoi(argv[3]) : 19999u,
                         argc >= 5 ? (unsigned int)atoi(argv[4]) : 20u);
    }

  if (argc >= 3 && strcmp(argv[1], "--netdiag") == 0)
    {
      return tcp_netdiag(argv[2],
                         argc >= 4 ? (unsigned int)atoi(argv[3]) : 22u,
                         argc >= 5 ? (unsigned int)atoi(argv[4]) : 100u,
                         argc >= 6 ? (unsigned int)atoi(argv[5]) : 1000u);
    }

  if (argc == 2 && strcmp(argv[1], "--lcd-pattern") == 0)
    {
      return lcd_test_pattern();
    }

  printf("\nVelaOps Sentinel smoke test v%s\n", VELAOPS_SMOKE_VERSION);
  printf("===================================\n");
  test_uname(&result);
  test_memory(&result);
  test_file(&result);
  test_network(&result);
  test_libssh(&result);
  test_ai_agent(&result);
  test_node(&result, "lcd", "/dev/lcd0", true);
  test_node(&result, "camera", "/dev/video0", true);
  test_node(&result, "microphone", "/dev/audio/pcm_in0", true);
  test_node(&result, "button", "/dev/buttons", true);
  test_node(&result, "led", "/dev/userleds", true);
  test_node(&result, "microsd", "/dev/mmcsd1", false);
  test_accel(&result);
  printf("-----------------------------------\n");
  printf("Result: %u passed, %u failed, %u skipped\n",
         result.passed, result.failed, result.skipped);
  printf("Next: ifconfig; wapi; ai_agent; SSH integration test\n\n");
  return result.failed == 0 ? 0 : 1;
}
