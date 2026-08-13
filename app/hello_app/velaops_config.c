#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

#include "velaops.h"

void velaops_secure_zero(void *data, unsigned int size)
{
  volatile unsigned char *p = data;
  while (size-- > 0)
    {
      *p++ = 0;
    }
}

static int read_line(const char *label, char *buffer, unsigned int size,
                     bool hidden, bool allow_empty)
{
  struct termios oldt;
  struct termios newt;
  bool echo_changed = false;
  size_t len;

  printf("%s", label);
  fflush(stdout);

  /* 确保 stdin 在规范模式（行缓冲） */
  if (tcgetattr(0, &oldt) == 0)
    {
      newt = oldt;
      newt.c_lflag |= ICANON;  /* 启用规范模式 */
      if (hidden)
        {
          newt.c_lflag &= ~ECHO;
        }
      else
        {
          newt.c_lflag |= ECHO;
        }
      tcsetattr(0, TCSANOW, &newt);
      echo_changed = true;
    }

  /* 使用 fgets 读取整行 */
  if (fgets(buffer, size, stdin) == NULL)
    {
      if (echo_changed) tcsetattr(0, TCSANOW, &oldt);
      return -1;
    }

  if (echo_changed)
    {
      tcsetattr(0, TCSANOW, &oldt);
      if (hidden) putchar('\n');
    }

  len = strcspn(buffer, "\r\n");
  buffer[len] = '\0';
  return len > 0 || allow_empty ? 0 : -1;
}

int velaops_config_prompt(struct velaops_profile *p, char *pin,
                          unsigned int pin_size)
{
  char port[8];
  memset(p, 0, sizeof(*p));
  p->port = 22;
  p->auth_kind = VELAOPS_AUTH_PASSWORD;

  puts("\nVelaOps first-run configuration (password authentication)");
  if (read_line("Server address: ", p->host, sizeof(p->host), false, false) < 0 ||
      read_line("SSH port [22]: ", port, sizeof(port), false, true) < 0 ||
      read_line("SSH username: ", p->user, sizeof(p->user), false, false) < 0 ||
      read_line("SSH password: ", p->secret, sizeof(p->secret), true, false) < 0 ||
      read_line("Vault PIN: ", pin, pin_size, true, false) < 0)
    {
      return -1;
    }

  if (port[0] != '\0')
    {
      unsigned int value;
      if (sscanf(port, "%u", &value) != 1 || value == 0 || value > 65535)
        return -1;
      p->port = value;
    }

  return 0;
}
